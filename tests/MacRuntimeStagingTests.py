"""Repeated runtime staging must preserve libraries and report tool failures."""

import argparse
import hashlib
from pathlib import Path
import re
import subprocess


def library_hashes(runtime):
    return {path.relative_to(runtime): hashlib.sha256(path.read_bytes()).digest()
            for path in runtime.rglob("*.dylib") if path.is_file() and not path.is_symlink()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--config", required=True)
    args = parser.parse_args()
    build = args.build.resolve(strict=True)
    runtime = build / "runtime"
    original = library_hashes(runtime)
    if not original:
        raise RuntimeError("No staged SDK libraries were found")
    for _ in range(2):
        result = subprocess.run([args.cmake, "--build", str(build), "--target", "VirtoolsDevRuntime",
                                 "--config", args.config, "--parallel", "4"],
                                capture_output=True, text=True, timeout=120)
        output = result.stdout + result.stderr
        if result.returncode or re.search(r"(?im)^.*(?:error:|CMake Error)", output):
            raise RuntimeError("Repeated runtime staging failed:\n" + output)
        if library_hashes(runtime) != original:
            raise RuntimeError("Repeated staging rewrote the built SDK libraries")
        if (runtime / "Bin/data").exists() or not (runtime / "data/fonts/NotoSans-Regular.ttf").is_file():
            raise RuntimeError("Repeated staging changed the application data layout")
    print("Repeated macOS runtime staging preserved all library bytes and root data without tool errors")


if __name__ == "__main__":
    main()
