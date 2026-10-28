"""Measure complete Release editor frames with the native GPU viewport, in isolation."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import sys


PREFIX = "NEMODEV_VIEWPORT_BASELINE "
STABLE_COUNTERS = ("stable_readbacks", "stable_uploads", "stable_target_creations",
                   "stable_path_selections", "stable_target_releases")
CONDITIONS = ("version", "scenario", "scope", "driver", "gpu_device", "gpu_driver_info", "sdl_version", "viewport", "window", "window_pixels",
              "ui_scale", "warmup_frames", "requested_frames", "minimum_warmup_ms", "minimum_group_ms")


def summarize(measurement):
    if measurement["version"] != 1 or measurement["scope"] != "editor_frame_gpu_drained":
        raise ValueError("unsupported viewport measurement")
    for counter in STABLE_COUNTERS:
        if measurement[counter] != 0:
            raise ValueError(f"stable viewport performed {counter}")
    samples = measurement["samples"]
    if measurement["minimum_warmup_ms"] < 15000 or measurement["minimum_group_ms"] < 1000:
        raise ValueError("measurement needs at least 15 seconds warmup and 1 second per group")
    if len(samples) < 11:
        raise ValueError("at least 11 sample groups are required")
    values = []
    for sample in samples:
        frames, elapsed = sample["frames"], sample["elapsed_ms"]
        if (not isinstance(frames, int) or frames < measurement["requested_frames"] or
                not math.isfinite(elapsed) or elapsed < measurement["minimum_group_ms"]):
            raise ValueError("invalid frame count or duration")
        values.append(elapsed / frames)
    values.sort()
    def percentile(fraction):
        at = (len(values) - 1) * fraction
        lo, hi = math.floor(at), math.ceil(at)
        return values[lo] + (values[hi] - values[lo]) * (at - lo)
    median = statistics.median(values)
    mad = statistics.median(abs(value - median) for value in values)
    return {"median_ms": median, "p10_ms": percentile(0.1), "p90_ms": percentile(0.9),
            "mad_ms": mad, "mad_percent": mad / median * 100.0}


def parse_measurement(output):
    text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", output)
    measurements = [json.loads(line[len(PREFIX):]) for line in text.splitlines() if line.startswith(PREFIX)]
    if len(measurements) != 1:
        raise ValueError("native test did not emit one complete measurement")
    return measurements[0]


def compare(candidate, baseline):
    for key in CONDITIONS:
        if candidate["measurement"][key] != baseline["measurement"][key]:
            raise ValueError(f"incompatible measurement condition: {key}")
    for key in ("platform", "machine", "hardware", "build_type", "compiler", "compiler_flags",
                "sdk_revisions", "ui_dependencies", "harness_sha256"):
        if candidate["environment"][key] != baseline["environment"][key]:
            raise ValueError(f"incompatible environment: {key}")
    if candidate["environment"]["sdk_dirty"] or baseline["environment"]["sdk_dirty"]:
        raise ValueError("comparison requires unchanged SDK sources")
    previous, current = summarize(baseline["measurement"]), summarize(candidate["measurement"])
    if previous["mad_percent"] > 3.0:
        raise ValueError("baseline variation exceeds 3 percent")
    change = (current["median_ms"] / previous["median_ms"] - 1.0) * 100.0
    control = candidate["environment"]["binary_sha256"] == baseline["environment"]["binary_sha256"]
    return {"median_change_percent": change,
            "same_binary_control": control,
            "passed": (abs(change) if control else change) <= 3.0 + 1e-9 and current["mad_percent"] <= 3.0}


def git(directory, *arguments):
    return subprocess.check_output(["git", "-C", str(directory), *arguments], text=True).strip()


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def environment(source, build, binary, config):
    cache = {}
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if separator and ":" in key and not line.startswith(("#", "//")):
            cache[key.split(":", 1)[0]] = value
    build_type = cache.get("CMAKE_BUILD_TYPE") or config
    if build_type != "Release":
        raise ValueError("viewport baseline requires a Release build")
    sdk = Path(cache["VIRTOOLS_SDK_PATH"])
    revisions = {"root": git(sdk, "rev-parse", "HEAD")}
    for line in git(sdk, "submodule", "status", "--recursive").splitlines():
        fields = line.strip().lstrip("+-U").split()
        if len(fields) >= 2:
            revisions[fields[1]] = fields[0]
    hardware = platform.processor()
    if sys.platform == "darwin":
        hardware = subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"], text=True).strip()
    compiler = subprocess.check_output([cache["CMAKE_CXX_COMPILER"], "--version"], text=True).splitlines()[0] if os.name != "nt" else cache["CMAKE_CXX_COMPILER"]
    harness = hashlib.sha256()
    for name in ("tools/MeasureViewport.py", "tests/ui/ViewportBaselineTests.cpp"):
        harness.update((source / name).read_bytes())
    return {"source_revision": git(source, "rev-parse", "HEAD"),
            "source_dirty": bool(git(source, "status", "--porcelain")),
            "binary_sha256": sha256(binary), "harness_sha256": harness.hexdigest(),
            "platform": platform.platform(), "machine": platform.machine(), "hardware": hardware,
            "logical_cpus": os.cpu_count(),
            "build_type": build_type, "compiler": compiler,
            "compiler_flags": {key: cache.get(key, "") for key in
                               ("CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_RELEASE", "CMAKE_OSX_ARCHITECTURES")},
            "ui_dependencies": git(source, "submodule", "status", "--recursive"),
            "sdk_revisions": revisions,
            "sdk_dirty": bool(git(sdk, "status", "--porcelain"))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--config", default="Release")
    parser.add_argument("--samples", type=int, default=11)
    parser.add_argument("--frames", type=int, default=240)
    parser.add_argument("--warmup", type=int, default=480)
    args = parser.parse_args()
    if not 11 <= args.samples <= 101 or not 30 <= args.frames <= 10000 or not 60 <= args.warmup <= 10000:
        parser.error("samples must be 11..101, frames 30..10000, warmup 60..10000")
    source = Path(__file__).resolve().parent.parent
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        metadata = environment(source, args.build.resolve(), args.binary.resolve(), args.config)
        env = os.environ.copy()
        env.update(NEMODEV_BASELINE_SAMPLES=str(args.samples), NEMODEV_BASELINE_FRAMES=str(args.frames),
                   NEMODEV_BASELINE_WARMUP=str(args.warmup))
        result = subprocess.run([str(args.binary.resolve()), "--ui-tests", "^viewportbaseline$",
                                 "--ui-results", str(args.output.with_suffix(".xml").resolve())],
                                env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
        args.output.with_suffix(".log").write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise ValueError(f"native viewport test failed ({result.returncode}); see {args.output.with_suffix('.log')}")
        measurement = parse_measurement(result.stdout)
        if len(measurement["samples"]) != args.samples:
            raise ValueError("native test did not emit the complete requested measurement")
        if metadata["binary_sha256"] != sha256(args.binary):
            raise ValueError("test binary changed during measurement")
        report = {"environment": metadata, "measurement": measurement,
                  "statistics": summarize(measurement),
                  "unmeasured": ["GPU-only timestamps", "application allocations", "GPU preserve copies",
                                 "extra GPU passes/submissions", "native capability queries", "resource retain/release"]}
        passed = report["statistics"]["mad_percent"] <= 3.0 and not metadata["sdk_dirty"]
        if args.compare:
            report["comparison"] = compare(report, json.loads(args.compare.read_text(encoding="utf-8")))
            passed &= report["comparison"]["passed"]
        report["passed"] = passed
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report["statistics"], indent=2))
        print(f"{'PASS' if passed else 'UNSTABLE OR REGRESSED'}: {args.output}")
        return 0 if passed else 1
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
        print(f"Viewport baseline failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
