# NeMoDev

[![CI](https://github.com/doyaGu/NeMoDev/actions/workflows/ci.yml/badge.svg)](https://github.com/doyaGu/NeMoDev/actions/workflows/ci.yml)

NeMoDev is a desktop editor for Virtools compositions (`.cmo`, `.nmo` and `.vmo`).
It brings the Schematic, 3D Layout and object editors to an SDL3 / Dear ImGui
interface, running on [Ballanced](https://github.com/doyaGu/Ballanced).

- Edit Scripts, Building Blocks, parameters, pins and connections in the Schematic.
- Organize Levels, Scenes and objects with the Level and Hierarchy Managers.
- Preview compositions in Player Mode, debug Scripts and capture snapshots.
- Undo and redo edits, save compositions, print Schematics and browse local help.

The editor targets Windows x64, Linux x64 and macOS ARM64. The macOS x64 and
Windows x86 presets are experimental. NeMoDev builds its runtime from Ballanced
source; it does not use the original binary Virtools SDK or runtime.

## Downloads

Open a successful [CI run](https://github.com/doyaGu/NeMoDev/actions/workflows/ci.yml)
and download the matching **NeMoDev** artifact. These are Release builds of the
editor with its runtime, plugins, fonts and license materials. Each package
includes its source revisions and a SHA-256 checksum; test logs are separate.

| Platform | Package | Install and run |
| --- | --- | --- |
| Windows 10/11 x64 | Portable ZIP | Extract the entire archive and open `Bin/NeMoDev.exe`. Visual C++ runtime DLLs are included. |
| Ubuntu 24.04 x64 | DEB | Run `sudo apt install ./NeMoDev-*.deb`, then open NeMoDev from the application menu or run `nemodev`. APT installs the system dependencies. |
| macOS 15+ Apple Silicon | DMG | Open the image and drag `NeMoDev.app` to Applications. No Homebrew installation is needed. |

A working graphics driver with SDL GPU support is required. Windows builds
are not Authenticode signed. macOS builds are **ad hoc signed and not notarized**;
after verifying the download, allow the app through System Settings → Privacy &
Security if Gatekeeper blocks it. Builds use CI build numbers rather than a
stable-release version. Original CHM help is not included.

## Building

You will need:

- **CMake 3.25+** and a **C++17** toolchain: Visual Studio 2022 on Windows,
  GCC or Clang with Ninja on Linux, or Xcode command-line tools on macOS.
- **Shared SDL3 3.4.8+**, installed with its CMake package. See SDL's
  [build instructions](https://wiki.libsdl.org/SDL3/README-cmake).
- **Python 3.8+ and Pillow 9.1+** for compiling image resources.
- **Ballanced's public `sdl` branch**, with its submodules initialized.

Linux also needs the GTK3 Unix Print, GIO and libharu development packages:

```sh
sudo apt-get install build-essential cmake ninja-build pkg-config python3-venv libgtk-3-dev libglib2.0-dev libhpdf-dev
```

On macOS, install Ninja and libharu alongside the Xcode tools. SDL's
[Linux dependency list](https://wiki.libsdl.org/SDL3/README-linux) covers its
window and audio backends. Running the editor requires a working SDL GPU
backend and graphics driver. The [CI workflow](.github/workflows/ci.yml)
contains the complete dependency setup used on clean runners.

### Get the sources

```sh
git clone https://github.com/doyaGu/NeMoDev.git
cd NeMoDev
git submodule update --init --recursive external/imgui external/litehtml
```

Keep these submodules at the recorded revisions. Ballanced is a separate
checkout, selected explicitly at configure time; there is no default SDK path.
An existing local checkout can be used directly. To prepare a new one:

```sh
git clone --branch sdl --recurse-submodules https://github.com/doyaGu/Ballanced.git "<Ballanced checkout>"
```

NeMoDev builds the SDK libraries, managers, readers and Building Blocks from
that checkout. It does not build Ballanced's Player or stage its game assets.
Images, fonts and `interface.nmo` are already included in this repository.
You do not need CHM documentation or original sample compositions to build.

### Configure and build

Replace the two dependency paths below with your Ballanced checkout and SDL
installation prefix. Select the Python interpreter that has Pillow installed.

**Windows x64** — PowerShell:

```powershell
py -3 -m venv .venv
.venv/Scripts/python.exe -m pip install "Pillow>=9.1"
cmake --preset windows-x64-release "-DVIRTOOLS_SDK_PATH=<Ballanced checkout>" "-DCMAKE_PREFIX_PATH=<SDL installation>" "-DPython3_EXECUTABLE=$PWD/.venv/Scripts/python.exe"
cmake --build --preset windows-x64-release --parallel 4
out/windows-x64-release/runtime/Bin/NeMoDev.exe
```

**Linux x64:**

```sh
python3 -m venv .venv
.venv/bin/python -m pip install "Pillow>=9.1"
cmake --preset linux-x64-release -DVIRTOOLS_SDK_PATH="<Ballanced checkout>" -DCMAKE_PREFIX_PATH="<SDL installation>" -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build --preset linux-x64-release --parallel 4
out/linux-x64-release/runtime/Bin/NeMoDev
```

**macOS ARM64:** install `cmake`, `ninja`, `sdl3`, `libharu` and Python with
Homebrew, then prepare the Python virtual environment as above:

```sh
cmake --preset macos-arm64-release -DVIRTOOLS_SDK_PATH="<Ballanced checkout>" -DCMAKE_PREFIX_PATH="$(brew --prefix)" -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build --preset macos-arm64-release --parallel 4
out/macos-arm64-release/runtime/Bin/NeMoDev
```

The `-release` presets omit regression executables and ImGui Test Engine.
Other presets are listed in [CMakePresets.json](CMakePresets.json), including
`macos-x64-release` and `macos-arm64-release`. Use matching architectures for
the compiler and all native libraries. Local path overrides can be kept in
an ignored `CMakeUserPresets.json`.

### Install

Install both components into the same directory, replacing `<preset>` with
the preset you built:

```sh
cmake --install out/<preset> --config Release --prefix out/package --component Runtime
cmake --install out/<preset> --config Release --prefix out/package --component Application
```

Run `out/package/Bin/NeMoDev` (`NeMoDev.exe` on Windows). Keep the entire package
together: the executable needs its libraries, plugins, data, fonts and license
files. The Linux install includes SDL3 and shared libharu; GTK, GIO and the
graphics driver remain system dependencies. For native distribution packages,
`tools/Package.py --help` describes the packaging inputs. Run it on the target
platform after installing both components; DEB packaging also needs `dpkg-dev`.
It packages and checks the installed program without copying the build tree.

## Development and tests

Use a preset without `-release` to build the tests. To run core tests without
ImGui Test Engine, configure it with `VIRTOOLSDEV_BUILD_UI_TESTS=OFF`:

```sh
cmake --preset linux-x64 -DVIRTOOLS_SDK_PATH="<Ballanced checkout>" -DCMAKE_PREFIX_PATH="<SDL installation>" -DPython3_EXECUTABLE="$PWD/.venv/bin/python" -DVIRTOOLSDEV_BUILD_UI_TESTS=OFF
cmake --build --preset linux-x64 --parallel 4
ctest --preset linux-x64 -L self-contained --no-tests=error
```

Use `windows-x64` and the Windows Python path on Windows. The `self-contained`
suite covers editing, cancellation, Undo/Redo, save failure recovery, file
round trips, resources and deployment using inputs created by the tests.

UI tests additionally require `external/imgui_test_engine` and
`VIRTOOLSDEV_BUILD_UI_TESTS=ON`. Test Engine has a
[separate license](THIRD_PARTY_NOTICES.md#ui-test-dependency); the production
editor never links it. Rendering tests need a display and SDL GPU support.
On a headless Linux host, Xvfb can supply the display, with Mesa software Vulkan
providing the GPU backend as in CI.

The independent HTML/CSS renderer can be built without the editor or SDK:

```sh
cmake -S html -B out/html -DCMAKE_BUILD_TYPE=Release
cmake --build out/html --config Release --parallel 4
ctest --test-dir out/html -C Release --output-on-failure --no-tests=error
```

### Continuous integration

[CI](https://github.com/doyaGu/NeMoDev/actions/workflows/ci.yml) builds the
editor against Ballanced mainline on Windows, Linux and macOS ARM64. It runs the
self-contained regressions, checks installed runtime dependencies and verifies
that the production package contains no test code. Linux also runs the
self-contained UI groups through Xvfb and software Vulkan. The standalone
HTML module is built and tested on Windows, Linux and macOS.

Selected tests must all pass; skips fail CI. Each run records the Ballanced
revision and uploads test inventories, results and failure logs. Ballanced's
native build is cached by SDK revision, runner image, SDL version and build
configuration; editor source changes reuse it. Tests and package checks still
run on cache hits. Distribution
checks reopen the Windows ZIP, install and start the Ubuntu DEB in a clean
Ubuntu 24.04 container, and mount the macOS DMG to verify its bundle and loading.
Hosted Windows/macOS checks do not replace GPU and native-desktop acceptance.
A weekly run
checks compatibility with changes in Ballanced even when NeMoDev is unchanged.

Original-composition regressions need samples supplied under `local-samples/data`
or through `VIRTOOLSDEV_SAMPLE_ROOT`. Copy
`tests/data/verification/interfacechunk_samples.txt` into their `data/verification/`
directory and rerun configure. These tests, CHM content checks and real-desktop
fullscreen, focus, dialogs and printing checks are separate from hosted CI.
They remain available locally; desktop tests use `VIRTOOLSDEV_TEST_DESKTOP=ON`.

## Local help and compatibility

To enable the help window and Building Block F1 topics, place an original
`documentation.chm` at `local-resources/data/documentation.chm`, install 7-Zip
and rerun configure. The build converts it to local HTML. Without the archive,
help is unavailable and 7-Zip is not required.

Legacy CK strings keep their original bytes when unedited. Their display and
editing use the host's native character set; NeMoDev does not detect a
composition's encoding. Printing reports job submission, not completed paper
output. Workset membership lasts for the current editor session.

## License and credits

[CC BY-NC-SA 4.0](LICENSE) covers NeMoDev-authored code. Incorporated code adapted from
[VirtoolsScriptDeobfuscation](https://github.com/BearKidsTeam/VirtoolsScriptDeobfuscation)
also retains **CC BY-NC-SA 4.0**.

Ballanced, SDL3, Dear ImGui, litehtml, fonts and original Virtools resources
retain their own terms. See [third-party notices](THIRD_PARTY_NOTICES.md) for
attribution, license scope and unresolved runtime redistribution permissions.
