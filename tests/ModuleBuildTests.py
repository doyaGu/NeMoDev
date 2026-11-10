"""Build and install a module using only the public headers and build helper."""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--results", type=Path, required=True)
    args = parser.parse_args()
    args.results.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="nemodev-module-build-") as temporary:
        root = Path(temporary)
        source = root / "external project"
        source.mkdir()
        project = '''cmake_minimum_required(VERSION 3.25)
project(ExternalEditorModule LANGUAGES CXX)
set(VIRTOOLSDEV_SOURCE_DIR "${NEMODEV_SOURCE}")
include("${NEMODEV_SOURCE}/cmake/EditorModules.cmake")
set(_source "${NEMODEV_SOURCE}/tests/modules/NoEntryModule.cpp")
if(INVALID STREQUAL "sources")
    nemodev_add_module(Example)
elseif(INVALID STREQUAL "host")
    nemodev_add_module(Example HOST MissingHost SOURCES "${_source}")
elseif(INVALID STREQUAL "argument")
    nemodev_add_module(Example SOURCES "${_source}" HOST)
else()
    nemodev_add_module(Example SOURCES "${_source}")
    nemodev_add_module(UninstalledFixture SOURCES "${_source}")
    nemodev_install_module(Example)
endif()
'''
        (source / "CMakeLists.txt").write_text(project)

        def run(name, *command, ok=True):
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
            (args.results / (name + ".log")).write_bytes(result.stdout)
            if (result.returncode == 0) != ok:
                raise RuntimeError(f"Unexpected {name} result: " + result.stdout.decode(errors="replace"))

        for invalid in ("sources", "host", "argument"):
            run(invalid, args.cmake, "-S", str(source), "-B", str(root / invalid),
                "-DNEMODEV_SOURCE=" + args.source.as_posix(), "-DINVALID=" + invalid, ok=False)
        build = root / "build"
        run("configure", args.cmake, "-S", str(source), "-B", str(build),
            "-DNEMODEV_SOURCE=" + args.source.as_posix(), "-DCMAKE_BUILD_TYPE=Release")
        run("build", args.cmake, "--build", str(build), "--config", "Release", "--parallel", "2")
        installed = root / "Installed Package"
        run("install", args.cmake, "--install", str(build), "--prefix", str(installed),
            "--config", "Release", "--component", "Application")
        moved = root / "Moved Package"
        shutil.move(installed, moved)
        modules = list((moved / "Bin/modules").iterdir())
        if len(modules) != 1 or "Example" not in modules[0].name or not modules[0].is_file():
            raise RuntimeError("Installation included regression fixtures or used the wrong module directory")
    print("Public-header module built and installed; only explicitly selected modules were packaged")


if __name__ == "__main__":
    main()
