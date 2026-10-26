"""Package a Release install as Windows ZIP, Linux tar.xz/DEB or macOS DMG.

Run on the target operating system after installing Runtime and Application.
Only the installed payload is used; build trees and SDK sources are excluded.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

from PIL import Image


SOURCE = Path(__file__).resolve().parents[1]
PLUGINS = ("RenderEngines", "Managers", "BuildingBlocks", "Plugins")


def run(*command, **kwargs):
    try:
        return subprocess.run([str(item) for item in command], check=True, **kwargs)
    except subprocess.CalledProcessError as error:
        if error.stdout:
            print(error.stdout)
        if error.stderr:
            print(error.stderr, file=sys.stderr)
        raise


def check_production(binary, package):
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("LD_", "DYLD_"))
           and key not in ("VIRTOOLS_PATH", "LIBRARY_PATH", "CMAKE_PREFIX_PATH")}
    if sys.platform == "win32":
        system_root = os.environ["SystemRoot"]
        env["PATH"] = os.pathsep.join((os.path.join(system_root, "System32"), system_root))
    run(sys.executable, SOURCE / "tests/CheckProductionBinary.py",
        "--binary", binary, "--package", package, env=env)


def check_symlinks(package):
    for path in package.rglob("*"):
        if path.is_symlink():
            try:
                path.resolve(strict=True).relative_to(package.resolve())
            except (ValueError, OSError, RuntimeError) as error:
                raise RuntimeError(f"Installed symlink is broken or leaves the package: {path}") from error


def check_payload(package):
    check_symlinks(package)
    if (package / "Bin/data").exists():
        raise RuntimeError("Application data must be next to Bin, not inside it")
    required = ("data/interface.nmo", "Bin/BallancedRuntime.txt", "LICENSE", "THIRD_PARTY_NOTICES.md",
                "data/fonts/NotoSans-Regular.ttf", "data/fonts/NotoSansSC-Regular.otf")
    for name in required:
        if not (package / name).is_file():
            raise RuntimeError(f"Missing installed resource: {name}")
    for category in PLUGINS:
        if not (package / category).is_dir() or not any((package / category).iterdir()):
            raise RuntimeError(f"Missing installed plugins: {category}")
    binary = package / "Bin" / ("NeMoDev.exe" if sys.platform == "win32" else "NeMoDev")
    check_production(binary, package)


def write_text(path, text, mode=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    if mode is not None:
        path.chmod(mode)


def copy_icon(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with Image.open(SOURCE / "resources/images/app/appicon-10.ico") as image:
        image.convert("RGBA").save(path)


def windows_zip(package, output, name, work):
    for name_required in ("vcruntime140.dll", "msvcp140.dll", "SDL3.dll", "CK2.dll", "VxMath.dll"):
        if not (package / "Bin" / name_required).is_file():
            raise RuntimeError(f"Missing Windows runtime: {name_required}")
    write_text(package / "START-HERE.txt", "NeMoDev for Windows x64\n\n"
               "Extract the entire ZIP, then open Bin\\NeMoDev.exe. Keep all folders together.\n"
               "Requires Windows 10/11 and a graphics driver supporting SDL GPU.\n"
               "This build is not Authenticode signed. See LICENSE and THIRD_PARTY_NOTICES.md.\n")
    archive = output / f"{name}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as handle:
        for path in sorted(package.rglob("*")):
            if path.is_file():
                handle.write(path, Path("NeMoDev") / path.relative_to(package))
    extracted = work / "Extracted ZIP"
    with zipfile.ZipFile(archive) as handle:
        if handle.testzip():
            raise RuntimeError("ZIP integrity check failed")
        handle.extractall(extracted)
    check_payload(extracted / "NeMoDev")
    return archive


def elf_files(package):
    files = []
    for path in package.rglob("*"):
        if path.is_file() and not path.is_symlink():
            with path.open("rb") as handle:
                if handle.read(4) == b"\x7fELF":
                    files.append(path)
    return files


def linux_symbol_versions(text):
    versions = {}
    for name in re.findall(r"\bName:\s+((?:GLIBCXX|GLIBC|CXXABI)_[\w.]+)", text):
        family, version = name.split("_", 1)
        if name in ("CXXABI_TM_1", "CXXABI_FLOAT128"):
            continue
        if not re.fullmatch(r"[0-9]+(?:\.[0-9]+)+", version):
            raise RuntimeError(f"Unsupported Linux ABI requirement: {name}")
        parsed = tuple(map(int, version.split(".")))
        versions[family] = max(versions.get(family, ()), parsed)
    return versions


def check_linux_abi(package):
    # Keep the portable build usable on the Ubuntu 22.04 / Debian 12 generation
    # and newer glibc distributions. A filename alone cannot establish this.
    limits = {"GLIBC": (2, 35), "GLIBCXX": (3, 4, 30), "CXXABI": (1, 3, 13)}
    versions = {}
    for path in elf_files(package):
        result = run("readelf", "--version-info", "--wide", path, capture_output=True, text=True,
                     env={**os.environ, "LC_ALL": "C"})
        for family, version in linux_symbol_versions(result.stdout).items():
            if version > limits[family]:
                required = ".".join(map(str, version))
                raise RuntimeError(f"{path.name} requires {family}_{required}; build the portable package "
                                   "on the supported older Linux baseline instead")
            versions[family] = max(versions.get(family, ()), version)
    if "GLIBC" not in versions:
        raise RuntimeError("The portable package has no observable glibc requirements")
    return {family: ".".join(map(str, version)) for family, version in versions.items()}


def linux_tar(package, output, name, work):
    payload = work / "portable"
    shutil.copytree(package, payload, symlinks=True)
    versions = check_linux_abi(payload)
    info = json.loads((payload / "build-info.json").read_text())
    info["linux_abi"] = versions
    write_text(payload / "build-info.json", json.dumps(info, indent=2) + "\n")
    write_text(payload / "NeMoDev", (SOURCE / "tools/packaging/NeMoDev.sh").read_text(encoding="utf-8"), 0o755)
    write_text(payload / "START-HERE.txt", "NeMoDev for Linux x86_64\n\n"
               "Extract the entire archive and run ./NeMoDev. Keep all folders together.\n"
               "The launcher also works through a symlink and preserves your working directory.\n\n"
               "System requirements: glibc 2.35+, GTK3 with its Unix Print/CUPS backend, GIO,\n"
               "libpng16, zlib, libstdc++, Vulkan loader and a working graphics driver.\n"
               "Install Zenity for SDL's native file dialogs, and ALSA/PulseAudio libraries for audio.\n"
               "SDL3, libharu, the Ballanced runtime, plugins and fonts are included.\n"
               "Actual required symbol versions are recorded in build-info.json.\n\n"
               "Use your distribution's package manager to install these system libraries.\n"
               "This archive does not replace glibc, GTK, graphics drivers or system settings.\n"
               "See LICENSE and THIRD_PARTY_NOTICES.md.\n")
    archive = output / f"{name}.tar.xz"

    def owner(entry):
        entry.uid = entry.gid = 0
        entry.uname = entry.gname = ""
        return entry

    with tarfile.open(archive, "w:xz") as handle:
        handle.add(payload, arcname="NeMoDev", filter=owner)
    extracted = work / "Extracted Linux archive"
    with tarfile.open(archive) as handle:
        handle.extractall(extracted)
    check_payload(extracted / "NeMoDev")
    return archive


def linux_deb(package, output, name, version, work):
    if not shutil.which("dpkg-deb") or not shutil.which("dpkg-shlibdeps"):
        raise RuntimeError("DEB packaging requires dpkg-dev; select --linux-format portable otherwise")
    root = work / "deb"
    payload = root / "opt/nemodev"
    shutil.copytree(package, payload, symlinks=True)
    (root / "DEBIAN").mkdir()
    write_text(root / "usr/bin/nemodev", '#!/bin/sh\nexec /opt/nemodev/Bin/NeMoDev "$@"\n', 0o755)
    applications = root / "usr/share/applications"
    applications.mkdir(parents=True)
    shutil.copy2(SOURCE / "tools/packaging/nemodev.desktop", applications)
    mime = root / "usr/share/mime/packages"
    mime.mkdir(parents=True)
    shutil.copy2(SOURCE / "tools/packaging/nemodev.xml", mime)
    copy_icon(root / "usr/share/icons/hicolor/32x32/apps/nemodev.png")

    # Ask dpkg for the actual system-library requirements of every ELF, including
    # dynamically loaded CK plugins. Private libraries travel in the same package.
    analysis = work / "dependencies"
    write_text(analysis / "debian/control", "Source: nemodev\nSection: non-free/devel\nPriority: optional\n"
               "Maintainer: NeMoDev contributors <noreply@github.com>\n\nPackage: nemodev\nArchitecture: amd64\n"
               "Description: Virtools composition editor\n")
    dependencies = run("dpkg-shlibdeps", "-O", "--ignore-missing-info", f"-l{payload / 'Bin'}",
                       *(f"-e{path}" for path in elf_files(payload)), cwd=analysis,
                       capture_output=True, text=True)
    print(dependencies.stderr, end="")
    linked = next(line.partition("=")[2] for line in dependencies.stdout.splitlines()
                  if line.startswith("shlibs:Depends="))
    # SDL loads these backends at runtime, beyond the ELF dependency table.
    required = linked + ", libvulkan1, libasound2t64 | libasound2, libpulse0, zenity"
    size = sum(path.stat().st_size for path in root.rglob("*") if path.is_file()) // 1024 + 1
    write_text(root / "DEBIAN/control", f"Package: nemodev\nVersion: {version}\nArchitecture: amd64\n"
               "Section: non-free/devel\nPriority: optional\nMaintainer: NeMoDev contributors <noreply@github.com>\n"
               f"Installed-Size: {size}\nDepends: {required}\nHomepage: https://github.com/doyaGu/NeMoDev\n"
               "Description: Virtools composition editor\n Edit Scripts, objects and resources with Ballanced.\n")
    copyright_path = root / "usr/share/doc/nemodev/copyright"
    copyright_path.parent.mkdir(parents=True)
    shutil.copy2(payload / "THIRD_PARTY_NOTICES.md", copyright_path)
    for path in (root, *root.rglob("*")):
        if not path.is_symlink():
            path.chmod(0o755 if path.is_dir() or path.stat().st_mode & 0o111 else 0o644)
    archive = output / f"{name}.deb"
    run("dpkg-deb", "--build", "--root-owner-group", root, archive)
    extracted = work / "Extracted DEB"
    run("dpkg-deb", "--extract", archive, extracted)
    check_payload(extracted / "opt/nemodev")
    return archive


def move_mac_resources(contents):
    # macOS signs code directories as nested code. Keep SDK configuration and
    # runtime metadata in Resources, with internal relative links at the SDK's
    # existing lookup locations; no SDK source changes are required.
    metadata = [contents / "Frameworks/BallancedRuntime.txt", *contents.glob("PlugIns/**/*.ini")]
    for path in metadata:
        destination = contents / "Resources/runtime" / path.relative_to(contents)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(path), str(destination))
        path.symlink_to(os.path.relpath(destination, path.parent))


def mac_dmg(package, output, name, version, work):
    volume = work / "volume"
    app = volume / "NeMoDev.app"
    contents = app / "Contents"
    frameworks = contents / "Frameworks"
    resources = contents / "Resources"
    shutil.copytree(package / "Bin", frameworks, symlinks=True)
    resources.mkdir()
    (contents / "MacOS").mkdir()
    executable = contents / "MacOS/NeMoDev"
    shutil.move(str(frameworks / "NeMoDev"), str(executable))
    for category in PLUGINS:
        shutil.copytree(package / category, contents / "PlugIns" / category, symlinks=True)
    move_mac_resources(contents)
    for path in package.iterdir():
        if path.name == "Bin" or path.name in PLUGINS:
            continue
        if path.is_dir():
            shutil.copytree(path, resources / path.name, symlinks=True)
        else:
            shutil.copy2(path, resources)
    copy_icon(resources / "NeMoDev.icns")
    with (contents / "Info.plist").open("wb") as handle:
        plistlib.dump({"CFBundleExecutable": "NeMoDev", "CFBundleName": "NeMoDev",
                      "CFBundleIdentifier": "io.github.doyagu.NeMoDev", "CFBundlePackageType": "APPL",
                      "CFBundleVersion": version, "CFBundleShortVersionString": version,
                      "CFBundleIconFile": "NeMoDev.icns", "LSMinimumSystemVersion": "15.0",
                      "LSApplicationCategoryType": "public.app-category.developer-tools",
                      "NSHighResolutionCapable": True,
                      "CFBundleDocumentTypes": [{"CFBundleTypeName": "Virtools composition",
                          "CFBundleTypeExtensions": ["cmo", "nmo", "vmo"], "CFBundleTypeRole": "Editor"}]}, handle)
    run("cmake", f"-DAPP={app}", "-P", SOURCE / "tools/packaging/FixMacBundle.cmake")
    if any(frameworks.glob("libpng*.dylib")):
        png_prefix = Path(run("brew", "--prefix", "libpng", capture_output=True, text=True).stdout.strip())
        png_license = next((path for path in (png_prefix / "LICENSE", png_prefix / "LICENSE.md",
                           png_prefix / "share/doc/libpng/LICENSE") if path.is_file()), None)
        if png_license is None:
            raise RuntimeError("The bundled libpng requires its installed license file")
        shutil.copy2(png_license, resources / "licenses/libpng-LICENSE.txt")
    binaries = [path for path in contents.rglob("*") if not path.is_symlink()
                and path.is_file() and (path.suffix in (".dylib", ".so") or path == executable)]
    for binary in binaries:
        commands = run("otool", "-l", binary, capture_output=True, text=True).stdout
        for rpath in re.findall(r"cmd LC_RPATH\n.*?\n\s*path (.*?) \(offset", commands):
            if rpath.startswith("/"):
                run("install_name_tool", "-delete_rpath", rpath, binary)
        if binary != executable:
            run("codesign", "--force", "--sign", "-", "--timestamp=none", binary)
    run("codesign", "--force", "--sign", "-", "--timestamp=none", app)
    run("codesign", "--verify", "--deep", "--strict", app)
    check_production(executable, app)
    (volume / "Applications").symlink_to("/Applications", target_is_directory=True)
    write_text(volume / "READ-ME.txt", "NeMoDev for Apple Silicon (macOS 15+)\n\n"
               "Drag NeMoDev.app to Applications.\n"
               "This build is ad hoc signed and NOT NOTARIZED by Apple. Gatekeeper may block it.\n"
               "After verifying the download, use System Settings > Privacy & Security > Open Anyway.\n"
               "License and third-party notices are inside the app's Contents/Resources directory.\n")
    archive = output / f"{name}-unnotarized.dmg"
    run("hdiutil", "create", "-volname", "NeMoDev", "-srcfolder", volume, "-format", "UDZO", archive)
    mount = work / "Mounted DMG"
    run("hdiutil", "attach", "-readonly", "-nobrowse", "-mountpoint", mount, archive)
    try:
        mounted = mount / "NeMoDev.app"
        run("codesign", "--verify", "--deep", "--strict", mounted)
        check_production(mounted / "Contents/MacOS/NeMoDev", mounted)
    finally:
        run("hdiutil", "detach", mount)
    return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installed", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True, help="Three numeric components, e.g. 0.0.123")
    parser.add_argument("--revision", required=True)
    parser.add_argument("--sdk-revisions", type=Path, required=True)
    parser.add_argument("--linux-format", choices=("portable", "deb", "all"), default="portable",
                        help="Linux output: portable tar.xz (default), native DEB, or both")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.version):
        parser.error("version must contain three numeric components")
    if not re.fullmatch(r"[0-9a-f]{40}", args.revision):
        parser.error("revision must be a full Git commit ID")
    system = {"win32": "windows-x64", "darwin": "macos-arm64"}.get(sys.platform)
    if sys.platform == "linux":
        system = "linux-x64"
    expected_arch = "arm64" if sys.platform == "darwin" else "x86_64"
    actual_arch = platform.machine().lower().replace("amd64", "x86_64").replace("aarch64", "arm64")
    if not system or actual_arch != expected_arch:
        parser.error("packaging supports native Windows/Linux x64 and macOS ARM64 builds")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    name = f"NeMoDev-{args.version}-{args.revision[:8]}-{system}"
    with tempfile.TemporaryDirectory(prefix="package-", dir=output) as temporary:
        work = Path(temporary)
        package = work / "installed"
        shutil.copytree(args.installed.resolve(), package, symlinks=True)
        check_payload(package)
        (package / "build-info.json").write_text(json.dumps({"version": args.version,
            "revision": args.revision, "platform": system, "configuration": "Release"}, indent=2) + "\n")
        shutil.copy2(args.sdk_revisions, package / "ballanced-revisions.txt")
        if sys.platform == "win32":
            archives = [windows_zip(package, output, name, work)]
        elif sys.platform == "darwin":
            archives = [mac_dmg(package, output, name, args.version, work)]
        else:
            archives = []
            if args.linux_format in ("portable", "all"):
                archives.append(linux_tar(package, output, name, work))
            if args.linux_format in ("deb", "all"):
                archives.append(linux_deb(package, output, name, args.version, work))
        for archive in archives:
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            write_text(Path(str(archive) + ".sha256"), f"{digest}  {archive.name}\n")
            print(f"Verified distribution: {archive}")


if __name__ == "__main__":
    main()
