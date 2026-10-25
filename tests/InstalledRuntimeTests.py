"""Check the relocated Linux installation without development library paths."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def clean_environment():
    excluded = {"LIBRARY_PATH", "VIRTOOLS_PATH", "CMAKE_PREFIX_PATH"}
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith("LD_") and key not in excluded}
    environment["PATH"] = os.defpath
    return environment


def dependencies(binary, environment):
    result = subprocess.run(["ldd", str(binary)], env=environment,
                            capture_output=True, text=True, timeout=15)
    if result.returncode or "not found" in result.stdout:
        raise RuntimeError("Installed runtime dependencies are unavailable:\n" + result.stdout + result.stderr)
    return [(name, Path(path)) for name, path in re.findall(
        r"^\s*(\S+)\s+=>\s+(/.*?)\s+\(0x[0-9a-fA-F]+\)", result.stdout, re.MULTILINE)]


def installed_haru(binary, environment, shared, build_directory):
    linked = dependencies(binary, environment)
    haru = [(name, path) for name, path in linked if name.startswith("libhpdf")]
    if len(haru) != int(shared):
        raise RuntimeError("Installed editor does not have the expected libhpdf linkage")
    for _, path in linked:
        resolved = path.resolve(strict=True)
        try:
            resolved.relative_to(build_directory)
        except ValueError:
            continue
        raise RuntimeError("Installed editor still loads a build-tree library: " + str(resolved))
    if not shared:
        if list(binary.parent.glob("libhpdf*.a")):
            raise RuntimeError("A static libhpdf archive was shipped as a runtime library")
        return None
    name, path = haru[0]
    bundled = binary.parent / name
    resolved = bundled.resolve(strict=True)
    if resolved.parent != binary.parent or path.resolve(strict=True) != resolved:
        raise RuntimeError("libhpdf was not loaded from the installed Bin directory")
    for entry in binary.parent.glob("libhpdf*.so*"):
        if entry.resolve(strict=True).parent != binary.parent:
            raise RuntimeError("A libhpdf symlink escapes the installed Bin directory")
    return resolved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--config", default="Release")
    parser.add_argument("--binary-name", required=True)
    parser.add_argument("--haru-library", type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve(strict=True)
    library = args.haru_library.resolve(strict=True)
    shared = library.suffix != ".a"
    environment = clean_environment()
    with tempfile.TemporaryDirectory(prefix="NeMoDev installed runtime ") as temporary:
        root = Path(temporary).resolve()
        original, relocated = root / "installation", root / "relocated package"
        for component in ("Runtime", "Application"):
            result = subprocess.run([args.cmake, "--install", str(build), "--prefix", str(original),
                                     "--component", component, "--config", args.config],
                                    capture_output=True, text=True, timeout=90)
            if result.returncode:
                raise RuntimeError("Installation failed:\n" + result.stdout + result.stderr)
        original.rename(relocated)
        binary = relocated / "Bin" / args.binary_name
        haru = installed_haru(binary, environment, shared, build)
        if haru and hashlib.sha256(haru.read_bytes()).digest() != hashlib.sha256(library.read_bytes()).digest():
            raise RuntimeError("The installed libhpdf differs from the selected build dependency")
        # These production checks run main() before SDL initialization. They
        # verify executable startup without needing a display or shipping a probe.
        subprocess.run([sys.executable, str(Path(__file__).with_name("CheckProductionBinary.py")),
                        "--binary", str(binary), "--package", str(relocated)],
                       env=environment, cwd=root, check=True, timeout=60)
        if haru:
            hidden = root / haru.name
            haru.rename(hidden)
            try:
                try:
                    installed_haru(binary, environment, True, build)
                except (RuntimeError, OSError):
                    pass
                else:
                    raise RuntimeError("Removing the packaged libhpdf did not fail dependency verification")
            finally:
                hidden.rename(haru)
        print("Relocated installation startup and libhpdf deployment passed without development library paths")


if __name__ == "__main__":
    main()
