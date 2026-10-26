"""Check the actual ZIP after relocation, using an external runtime driver."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime_check", type=Path)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--desktop", action="store_true", help="Also check a real desktop window and GPU drawing")
    args = parser.parse_args()
    driver = args.runtime_check.resolve(strict=True)
    environment = {key: value for key, value in os.environ.items()
                   if key.upper() not in ("VIRTOOLS_PATH", "LIBRARY_PATH", "CMAKE_PREFIX_PATH")
                   and not key.upper().startswith(("SDL_", "LD_", "DYLD_"))}
    system = Path(os.environ["SystemRoot"])
    environment.update(PATH=str(system / "System32") + os.pathsep + str(system),
                       SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy")
    with tempfile.TemporaryDirectory(prefix="NeMoDev ZIP verification ") as temporary:
        root = Path(temporary)
        with zipfile.ZipFile(args.archive) as archive:
            if archive.testzip():
                raise RuntimeError("ZIP integrity check failed")
            archive.extractall(root / "original")
        package = root / "Relocated Package [test]"
        (root / "original/NeMoDev").rename(package)
        if (package / "Bin/data").exists() or not (package / "data/interface.nmo").is_file():
            raise RuntimeError("ZIP data must be at the package root next to Bin")
        subprocess.run([sys.executable, str(Path(__file__).with_name("CheckProductionBinary.py")),
                        "--binary", str(package / "Bin/NeMoDev.exe"), "--package", str(package)],
                       env=environment, cwd=root, check=True, timeout=60)
        check = package / "Bin/RuntimeCheck.exe"
        shutil.copy2(driver, check)
        subprocess.run([str(check), "--runtime", "auto"], env=environment, cwd=root, check=True, timeout=60)
        check.unlink()
        if args.desktop:
            subprocess.run([sys.executable, str(Path(__file__).with_name("InstalledWindowsAppSmoke.py")),
                            "--package", str(package)], cwd=root, check=True, timeout=150)
        print("Relocated ZIP: default runtime lookup, plugins, interface data and composition creation passed")


if __name__ == "__main__":
    main()
