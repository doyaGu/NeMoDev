"""Measure complete Release editor frames with the native GPU viewport, in isolation."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import queue
import statistics
import subprocess
import sys
import threading
import time


PREFIX = "NEMODEV_VIEWPORT_BASELINE "
READY = "NEMODEV_VIEWPORT_READY "
STABLE_COUNTERS = ("stable_readbacks", "stable_uploads", "stable_target_creations",
                   "stable_path_selections", "stable_target_releases")
CONDITIONS = ("version", "scenario", "scope", "driver", "gpu_device", "gpu_driver_info", "sdl_version", "viewport", "window", "window_pixels",
              "ui_scale", "warmup_frames", "requested_frames", "minimum_warmup_ms", "minimum_group_ms")


def summarize(measurement):
    if measurement["version"] not in (1, 2) or measurement["scope"] != "editor_frame_gpu_drained":
        raise ValueError("unsupported viewport measurement")
    if measurement["version"] == 2 and measurement.get("group_control") not in ("continuous", "paired"):
        raise ValueError("unsupported viewport group control")
    if measurement["version"] == 2 and measurement.get("cpu_policy") not in ("user-initiated-activity", "system-default"):
        raise ValueError("unsupported viewport CPU policy")
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


def compare(candidate, baseline, sdk_change=False):
    for key in CONDITIONS:
        if candidate["measurement"][key] != baseline["measurement"][key]:
            raise ValueError(f"incompatible measurement condition: {key}")
    if candidate["measurement"].get("group_control") != baseline["measurement"].get("group_control"):
        raise ValueError("incompatible measurement condition: group_control")
    if candidate["measurement"].get("cpu_policy") != baseline["measurement"].get("cpu_policy"):
        raise ValueError("incompatible measurement condition: cpu_policy")
    if candidate["environment"].get("launch_policy") != baseline["environment"].get("launch_policy"):
        raise ValueError("incompatible environment: launch_policy")
    for key in ("platform", "machine", "hardware", "build_type", "compiler", "compiler_flags",
                "ui_dependencies", "harness_sha256"):
        if candidate["environment"][key] != baseline["environment"][key]:
            raise ValueError(f"incompatible environment: {key}")
    if not sdk_change and candidate["environment"]["sdk_revisions"] != baseline["environment"]["sdk_revisions"]:
        raise ValueError("incompatible environment: sdk_revisions")
    if not sdk_change and (candidate["environment"]["sdk_dirty"] or baseline["environment"]["sdk_dirty"]):
        raise ValueError("comparison requires unchanged SDK sources")
    previous, current = summarize(baseline["measurement"]), summarize(candidate["measurement"])
    if previous["mad_percent"] > 3.0:
        raise ValueError("baseline variation exceeds 3 percent")
    change = (current["median_ms"] / previous["median_ms"] - 1.0) * 100.0
    control = (candidate["environment"]["binary_sha256"] == baseline["environment"]["binary_sha256"] and
               candidate["environment"].get("sdk_runtime_sha256", {}) == baseline["environment"].get("sdk_runtime_sha256", {}))
    return {"median_change_percent": change,
            "sdk_change": sdk_change,
            "same_binary_control": control,
            "passed": (abs(change) if control else change) <= 3.0 + 1e-9 and current["mad_percent"] <= 3.0}


def git(directory, *arguments):
    return subprocess.check_output(["git", "-C", str(directory), *arguments], text=True).strip()


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class ViewportProcess:
    """Keep a viewport paused between GPU-drained sample groups."""

    def __init__(self, command, output, env):
        self.output = output
        self.log = output.with_suffix(".log").open("w", encoding="utf-8")
        self.lines = queue.Queue()
        self.measurement = None
        self.process = None
        try:
            self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                            stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                                            errors="replace", env=env, bufsize=1)
        except BaseException:
            self.log.close()
            raise
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        try:
            for line in self.process.stdout:
                self.log.write(line)
                self.lines.put(line)
        except Exception as error:
            self.lines.put(error)
        finally:
            self.lines.put(None)

    def _line(self, deadline):
        try:
            line = self.lines.get(timeout=max(0, deadline - time.monotonic()))
        except queue.Empty:
            raise ValueError(f"viewport timed out; see {self.output.with_suffix('.log')}") from None
        if isinstance(line, Exception):
            raise ValueError(f"viewport output failed: {line}")
        if line is None:
            return None
        line = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", line).strip()
        if line.startswith(PREFIX):
            if self.measurement is not None:
                raise ValueError("viewport emitted duplicate measurements")
            self.measurement = json.loads(line[len(PREFIX):])
        return line

    def ready(self, group, deadline):
        while True:
            line = self._line(deadline)
            if line is None or self.measurement is not None:
                raise ValueError(f"viewport ended before group {group}; see {self.output.with_suffix('.log')}")
            if line.startswith(READY):
                if line != READY + str(group):
                    raise ValueError(f"unexpected viewport group: {line}")
                return

    def render_group(self):
        self.process.stdin.write("\n")
        self.process.stdin.flush()

    def finish(self, deadline):
        while True:
            line = self._line(deadline)
            if line is None:
                break
            if line.startswith(READY):
                raise ValueError(f"unexpected viewport group: {line}")
        self.process.wait(timeout=max(0, deadline - time.monotonic()))
        if self.process.returncode or self.measurement is None:
            raise ValueError(f"viewport failed ({self.process.returncode}); see {self.output.with_suffix('.log')}")
        return self.measurement

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.reader.join()
        self.process.stdin.close()
        self.process.stdout.close()
        self.log.close()


def paired_measurements(binary, reference, output, env, samples):
    env = dict(env, NEMODEV_BASELINE_PAIRED="1")
    deadline = time.monotonic() + 360
    processes = []
    measurements = {}
    try:
        # Finish each warmup before starting the next. Both then wait with
        # their GPU idle, outside their frame-count and time measurements.
        for name, path in (("reference", reference), ("current", binary)):
            result = output if name == "current" else output.with_name(output.stem + "-reference.json")
            process = ViewportProcess([str(path), "--ui-tests", "^viewportbaseline$", "--ui-results",
                                       str(result.with_suffix(".xml"))], result, env)
            processes.append((name, process))
            process.ready(0, deadline)
        for group in range(samples):
            # Alternate the first renderer as well, to avoid a fixed order bias.
            order = processes if group % 2 == 0 else list(reversed(processes))
            for name, process in order:
                process.render_group()
                if group + 1 < samples:
                    process.ready(group + 1, deadline)
                else:
                    measurements[name] = process.finish(deadline)
        return measurements["current"], measurements["reference"]
    finally:
        for _, process in reversed(processes):
            process.close()


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
    patches = {}
    for name in revisions:
        directory = sdk if name == "root" else sdk / name
        patch = subprocess.check_output(["git", "-C", str(directory), "diff", "HEAD", "--binary"])
        untracked = git(directory, "ls-files", "--others", "--exclude-standard").splitlines()
        if patch or untracked:
            patches[name] = {"diff_sha256": hashlib.sha256(patch).hexdigest(),
                             "untracked_sha256": {path: sha256(directory / path) for path in untracked}}
    runtime = {str(path.relative_to(build)): sha256(path)
               for folder in ("lib", "runtime/Bin", "runtime/RenderEngines")
               for path in sorted((build / folder).glob("*"))
               if path.is_file() and any(name in path.name for name in ("CK2", "VxMath", "SDL3", "CKSdlGpu"))}
    hardware = platform.processor()
    if sys.platform == "darwin":
        hardware = subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"], text=True).strip()
    compiler = subprocess.check_output([cache["CMAKE_CXX_COMPILER"], "--version"], text=True).splitlines()[0] if os.name != "nt" else cache["CMAKE_CXX_COMPILER"]
    harness = hashlib.sha256()
    build_source = Path(cache["CMAKE_HOME_DIRECTORY"])
    harness.update((source / "tools/MeasureViewport.py").read_bytes())
    for name in ("ViewportBaselineTests.cpp", "ViewportScheduling.h", "ViewportSchedulingMac.mm"):
        harness.update((build_source / "tests/ui" / name).read_bytes())
    source_hash = hashlib.sha256()
    for folder in ("core", "ui", "editor", "application", "workspace", "features"):
        for path in sorted((build_source / folder).rglob("*")):
            if path.is_file() and path.suffix in (".h", ".cpp", ".mm", ".txt", ".def"):
                source_hash.update(str(path.relative_to(build_source)).encode("utf-8") + b"\0" + path.read_bytes())
    return {"source_revision": git(source, "rev-parse", "HEAD"),
            "source_dirty": bool(git(source, "status", "--porcelain")),
            "binary_sha256": sha256(binary), "harness_sha256": harness.hexdigest(),
            "build_source_sha256": source_hash.hexdigest(),
            "platform": platform.platform(), "machine": platform.machine(), "hardware": hardware,
            "launch_policy": "foreground-on-launch" if sys.platform == "darwin" else "system-default",
            "logical_cpus": os.cpu_count(),
            "build_type": build_type, "compiler": compiler,
            "compiler_flags": {key: cache.get(key, "") for key in
                               ("CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_RELEASE", "CMAKE_OSX_ARCHITECTURES")},
            "ui_dependencies": git(source, "submodule", "status", "--recursive"),
            "sdk_revisions": revisions,
            "sdk_patches": patches, "sdk_runtime_sha256": runtime,
            "build_source_directory": cache.get("CMAKE_HOME_DIRECTORY", ""),
            "sdk_dirty": bool(git(sdk, "status", "--porcelain"))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--reference-build", type=Path)
    parser.add_argument("--reference-binary", type=Path)
    parser.add_argument("--sdk-change", action="store_true",
                        help="Measure an intentional SDK change; retain SDK patch and runtime fingerprints")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--samples", type=int, default=11)
    parser.add_argument("--frames", type=int, default=240)
    parser.add_argument("--warmup", type=int, default=480)
    args = parser.parse_args()
    if bool(args.reference_build) != bool(args.reference_binary):
        parser.error("paired measurement needs both --reference-build and --reference-binary")
    if args.reference_binary and args.compare:
        parser.error("choose a paired reference or a saved comparison")
    if not 11 <= args.samples <= 101 or not 30 <= args.frames <= 10000 or not 60 <= args.warmup <= 10000:
        parser.error("samples must be 11..101, frames 30..10000, warmup 60..10000")
    source = Path(__file__).resolve().parent.parent
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        metadata = environment(source, args.build.resolve(), args.binary.resolve(), args.config)
        env = os.environ.copy()
        if sys.platform == "darwin":
            # SDL 3 may otherwise leave a command-line-launched hidden test in
            # the background. Keep this identical for both measured programs.
            env["SDL_MAC_BACKGROUND_APP"] = "0"
        env.update(NEMODEV_BASELINE_SAMPLES=str(args.samples), NEMODEV_BASELINE_FRAMES=str(args.frames),
                   NEMODEV_BASELINE_WARMUP=str(args.warmup))
        reference_report = None
        if args.reference_binary:
            reference_metadata = environment(source, args.reference_build.resolve(),
                                             args.reference_binary.resolve(), args.config)
            if reference_metadata["harness_sha256"] != metadata["harness_sha256"]:
                raise ValueError("paired programs must be built with the same benchmark source")
            measurement, reference_measurement = paired_measurements(
                args.binary.resolve(), args.reference_binary.resolve(), args.output.resolve(), env, args.samples)
            if (reference_metadata["binary_sha256"] != sha256(args.reference_binary) or
                    len(reference_measurement["samples"]) != args.samples):
                raise ValueError("reference binary changed or emitted incomplete samples")
            reference_report = {"environment": reference_metadata, "measurement": reference_measurement,
                                "statistics": summarize(reference_measurement)}
        else:
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
        passed = report["statistics"]["mad_percent"] <= 3.0 and (not metadata["sdk_dirty"] or args.sdk_change)
        report["sdk_change"] = args.sdk_change
        if reference_report:
            report["reference"] = reference_report
            try:
                report["comparison"] = compare(report, reference_report, args.sdk_change)
            except ValueError as error:
                # Keep both complete measurements even when the reference
                # itself is too variable or its conditions do not match.
                report["comparison"] = {"passed": False, "reason": str(error)}
            passed &= report["comparison"]["passed"]
        elif args.compare:
            report["comparison"] = compare(report, json.loads(args.compare.read_text(encoding="utf-8")), args.sdk_change)
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
