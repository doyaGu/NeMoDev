# NeMoDev

A desktop editor for Virtools compositions, built with SDL3, SDL GPU and Dear
ImGui. NeMoDev builds the Ballanced runtime from source and uses that same runtime
for editing, playback and tests.

The editor includes Schematic editing, 3D Layout, Level and Hierarchy Managers,
object and parameter editors, a Script debugger, Undo/Redo, snapshots, printing
and Building Block help. Compositions can be opened, edited, saved and reopened.

Build instructions: [Windows](#windows-x64), [Ubuntu](#ubuntu-x64),
[macOS](#macos), [installation](#install-a-runtime-directory), [tests](#tests).

## Requirements

- Git, CMake 3.25 or newer, a C compiler and a C++17 compiler.
- A complete Ballanced source checkout; use its public default `sdl` branch.
- Shared SDL3 3.4.8 or newer, with its CMake package installed. The source-build
  examples below use [SDL 3.4.18](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18).
- Python 3.8 or newer with Pillow 9.1 or newer in that same interpreter.

| Platform | Additional build tools and libraries |
| --- | --- |
| Windows | Visual Studio 2022, the Desktop development with C++ workload and a Windows SDK |
| Linux | GCC or Clang, Ninja, pkg-config, GTK3 Unix Print, GIO Unix and libharu development files |
| macOS | Xcode command-line tools, Ninja and libharu; printing uses AppKit |

A working SDL GPU backend and graphics driver are required to run the editor.
All native dependencies must match the target architecture. See
[Verified platforms](#verified-platforms) before choosing an untested target.

## Get the sources

```sh
git clone https://github.com/doyaGu/NeMoDev.git
cd NeMoDev
git submodule update --init --recursive external/imgui external/litehtml
```

Keep submodules at the revisions recorded by NeMoDev; do not use
`git submodule update --remote` to prepare a build. Dear ImGui and litehtml are
required for the editor. ImGui Test Engine is optional and is only needed for
[UI tests](#tests).

Choose a location for Ballanced and replace `<Ballanced source checkout>` in
the commands with its absolute path:

```sh
git clone --branch sdl https://github.com/doyaGu/Ballanced.git "<Ballanced source checkout>"
git -C "<Ballanced source checkout>" submodule update --init --recursive Source/VxMath Source/CK2 Source/RenderEngine Source/Plugins Source/BuildingBlocks Source/Managers/ParameterOperations Source/Managers/SdlInputManager Source/Managers/SdlSoundManager
```

An existing checkout can be used directly; run the same submodule command in
it first. These are the SDK components NeMoDev needs, including their nested
dependencies. It builds them together as shared libraries, without SDK copies
or patches. The Ballanced root project, Player and game-asset staging are
excluded. Original binary Virtools SDKs and runtimes are not supported.

`VIRTOOLS_SDK_PATH` is always an explicit input; the public configuration has
no default SDK path or pinned Ballanced revision. An ordinary build uses the
bundled images, icons, `interface.nmo` and Noto fonts in `resources/`. Original
test samples and optional CHM documentation are supplied separately.

## Build and run

Run the following commands from the NeMoDev source directory. The `-release`
presets build the editor without regression executables or ImGui Test Engine.
The commands select the virtual environment's Python explicitly, so activating
it or changing PowerShell's execution policy is unnecessary. An existing Python
with Pillow can also be selected with `Python3_EXECUTABLE`.

If shared SDL3 is already installed, skip its clone/build/install commands and
substitute its prefix in `CMAKE_PREFIX_PATH`. Use the installation prefix, not
the SDL source directory. See [SDL's CMake documentation](https://wiki.libsdl.org/SDL3/README-cmake)
for other installation methods.

### Windows x64

Install Visual Studio's C++ workload, Git, CMake and Python first. In PowerShell:

```powershell
py -3 -m venv .venv
.venv/Scripts/python.exe -m pip install "Pillow>=9.1"

git clone --depth 1 --branch release-3.4.18 https://github.com/libsdl-org/SDL.git out/deps/SDL
cmake -S out/deps/SDL -B out/deps/SDL-build-windows-x64 -G "Visual Studio 17 2022" -A x64 -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF
cmake --build out/deps/SDL-build-windows-x64 --config Release --parallel 8
cmake --install out/deps/SDL-build-windows-x64 --config Release --prefix "$PWD/out/deps/SDL-install-windows-x64"

cmake --preset windows-x64-release "-DVIRTOOLS_SDK_PATH=<Ballanced source checkout>" "-DCMAKE_PREFIX_PATH=$PWD/out/deps/SDL-install-windows-x64" "-DPython3_EXECUTABLE=$PWD/.venv/Scripts/python.exe"
cmake --build --preset windows-x64-release --parallel 8
out/windows-x64-release/runtime/Bin/NeMoDev.exe
```

For x86, use `windows-x86-release`, build SDL with `-A Win32` in a separate
directory, and select that SDL installation. The x86 preset has not been validated.

### Ubuntu x64

Install the editor's build dependencies:

```sh
sudo apt-get update
sudo apt-get install build-essential git cmake ninja-build pkg-config python3-venv python3-pip libgtk-3-dev libglib2.0-dev libhpdf-dev
```

When building SDL from source, also install its desktop, audio and GPU dependencies:

```sh
sudo apt-get install libasound2-dev libpulse-dev libudev-dev libdbus-1-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl1-mesa-dev libdrm-dev libgbm-dev libvulkan-dev
```

These cover the desktop backends used here. SDL's
[Linux dependency list](https://wiki.libsdl.org/SDL3/README-linux) describes
additional optional backends. Install a Vulkan-capable graphics driver for
your GPU; Xvfb alone cannot provide one. Check `cmake --version`: older Ubuntu
releases may need a newer CMake than their default package supplies. If it is
older than 3.25, install a current [CMake package](https://cmake.org/download/)
before continuing.

```sh
python3 -m venv .venv
.venv/bin/python -m pip install "Pillow>=9.1"

git clone --depth 1 --branch release-3.4.18 https://github.com/libsdl-org/SDL.git out/deps/SDL
cmake -S out/deps/SDL -B out/deps/SDL-build-linux-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF
cmake --build out/deps/SDL-build-linux-x64 --parallel 8
cmake --install out/deps/SDL-build-linux-x64 --prefix "$PWD/out/deps/SDL-install-linux-x64"

cmake --preset linux-x64-release -DVIRTOOLS_SDK_PATH="<Ballanced source checkout>" -DCMAKE_PREFIX_PATH="$PWD/out/deps/SDL-install-linux-x64" -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build --preset linux-x64-release --parallel 8
out/linux-x64-release/runtime/Bin/NeMoDev
```

### macOS

The macOS presets are available, but the editor build and runtime have not
been validated. With Xcode command-line tools and Homebrew installed, the
ARM64 configuration is:

```sh
brew install cmake ninja pkg-config python libharu
python3 -m venv .venv
.venv/bin/python -m pip install "Pillow>=9.1"

git clone --depth 1 --branch release-3.4.18 https://github.com/libsdl-org/SDL.git out/deps/SDL
cmake -S out/deps/SDL -B out/deps/SDL-build-macos-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DSDL_FRAMEWORK=OFF -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF
cmake --build out/deps/SDL-build-macos-arm64 --parallel 8
cmake --install out/deps/SDL-build-macos-arm64 --prefix "$PWD/out/deps/SDL-install-macos-arm64"

cmake --preset macos-arm64-release -DVIRTOOLS_SDK_PATH="<Ballanced source checkout>" -DCMAKE_PREFIX_PATH="$PWD/out/deps/SDL-install-macos-arm64;$(brew --prefix)" -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build --preset macos-arm64-release --parallel 8
out/macos-arm64-release/runtime/Bin/NeMoDev
```

For an Intel Mac, use `macos-x64-release`, `x86_64` for the SDL architecture and
separate SDL build/install directories. libharu must match that architecture.

### Install a runtime directory

The build stages the application under `out/<preset>/runtime/`. To prepare a
separate installation, run both component installs into the same new directory:

```sh
cmake --install out/windows-x64-release --config Release --prefix out/package-windows-x64 --component Runtime
cmake --install out/windows-x64-release --config Release --prefix out/package-windows-x64 --component Application
```

For Linux or macOS, replace the build and destination directories with those
for that platform. Start `Bin/NeMoDev.exe` on Windows or `Bin/NeMoDev` elsewhere.
Keep the whole installation together: `Bin/`, plugin category directories,
data, fonts, renderer settings and license materials. Copying the executable
alone is insufficient. Regression executables are not installed.

The Linux install includes shared libharu and SDL3 alongside the SDK libraries.
System libraries such as GTK, GIO and the graphics driver remain host prerequisites.
You do not need `VIRTOOLS_PATH` or a development `LD_LIBRARY_PATH` to run the
installed directory. Platform installers and a macOS application bundle are
not generated.

### Local configuration and common errors

Machine-specific overrides belong in an ignored `CMakeUserPresets.json` or on
the configure command line. The build caches dependency paths: rerun configure
when changing them. Use a separate build directory for another architecture
or compiler. Reduce `--parallel 8` on machines with less memory. The CMake
targets and options retain the `VirtoolsDev` / `VIRTOOLSDEV_` names.

| Configure/build failure | What to check |
| --- | --- |
| Missing Ballanced component or nested dependency | Initialize submodules in the selected Ballanced checkout, not just NeMoDev |
| Missing ImGui or litehtml | Run the NeMoDev submodule command under [Get the sources](#get-the-sources) |
| SDL3 package not found, too old or static only | Install shared SDL3 3.4.8+ for the target architecture and pass its installation prefix |
| Pillow missing or too old | Install it with the selected Python and pass that interpreter as `Python3_EXECUTABLE` |
| GTK Unix Print, GIO or `hpdf.h` missing | Install the Linux/macOS development dependencies; include custom prefixes in `CMAKE_PREFIX_PATH` |
| 7-Zip missing or cannot execute | It is needed only with CHM help; set `VIRTOOLSDEV_SEVEN_ZIP` to a working `7zz`, `7z` or `7za` executable |
| No SDL GPU device at startup | Check the installed graphics driver and available SDL GPU backend in the current desktop session |

Use a quoted, semicolon-separated `CMAKE_PREFIX_PATH` for multiple installations.
The prefixes must all contain libraries for the selected architecture.

## Optional resources

The checked-in resources are sufficient to build and run the editor. To enable
local help, copy `data/documentation.chm` from an existing Virtools Dev 2.5
installation to `local-resources/data/documentation.chm`, install 7-Zip and rerun
CMake configure. The same archive works on Windows, Linux and macOS. It is
converted to local HTML for the editor's help window, preserving Building
Block topic paths. Without the archive, the build requires no 7-Zip and the
editor disables unavailable help menus and shortcuts.

An alternative resource directory can be selected with `VIRTOOLSDEV_RESOURCES_DIR`;
it must contain `images.txt`, `images/`, `data/interface.nmo` and `fonts/`. The
build looks for `data/documentation.chm` there before `local-resources/`. Images
are embedded during the build; data and verified fonts are staged beside the
program. CHM files are excluded from Git.

To verify the bundled Noto fonts and OFL licenses or restore missing files,
run `.venv/Scripts/python.exe tools/PrepareFonts.py` on Windows or
`.venv/bin/python tools/PrepareFonts.py` on Linux/macOS. The script uses pinned
URLs and checksums, downloads only missing files and refuses to overwrite
different local files. Use `--output "<resource directory>/fonts"` for a custom
resource collection.

To regenerate a resource collection from an original installation, build the
standalone extractor on Windows:

```powershell
cmake -S tools -B out/resource-tools -G "Visual Studio 17 2022" -A x64
cmake --build out/resource-tools --config Release
out/resource-tools/Release/ExtractResources.exe "<Virtools Dev install>" local-resources resources/images.txt
```

The extractor needs neither Ballanced nor the editor's resources to build. It
reads `Dev.exe`, `resvis.dll`, `Managers/CK2UI.dll` and the installation's data
files. `resources/images.txt` selects the images to extract. The original
resources retain their own redistribution conditions.

## Text and paths

CK object names and parameter strings retain their original bytes when left
unedited. Display and edit conversions use the Windows active ANSI code page
or the current C runtime character set on other platforms. On those platforms,
the application initializes `LC_CTYPE` from the environment. It does not detect
a composition's character set or select one for it. Files from another system
can therefore display differently, while their unedited bytes remain intact.
Invalid native bytes may be replaced for display only; invalid or
unrepresentable text is rejected when converting an edit back to CK.

The editor and SDL use UTF-8 file paths. At legacy SDK calls, Windows paths
are converted to the native filename encoding; POSIX paths pass through as
native filesystem bytes. A Windows path that cannot be represented at an SDK
boundary is rejected rather than substituted. Text interpretation and
filesystem path handling are separate.

CK font rendering keeps Windows GDI behavior and uses stb_truetype on other
platforms. The prepared Noto fonts provide the deployed UI and fallback fonts.

## Tests

Use the presets without `-release` for development tests. They enable core and
UI tests by default. UI tests use the separately licensed ImGui Test Engine;
review [its conditions](#imgui-test-engine-license) before initializing it.
The regular editor remains free of regression code even in a test build.

To build core tests without Test Engine, reuse the SDK, SDL and Python paths
from the platform instructions and configure a separate test directory:

```powershell
cmake --preset windows-x64 "-DVIRTOOLS_SDK_PATH=<Ballanced source checkout>" "-DCMAKE_PREFIX_PATH=$PWD/out/deps/SDL-install-windows-x64" "-DPython3_EXECUTABLE=$PWD/.venv/Scripts/python.exe" -DVIRTOOLSDEV_BUILD_UI_TESTS=OFF
cmake --build --preset windows-x64 --parallel 8
ctest --preset windows-x64 --no-tests=error
```

On Linux, the equivalent is:

```sh
cmake --preset linux-x64 -DVIRTOOLS_SDK_PATH="<Ballanced source checkout>" -DCMAKE_PREFIX_PATH="$PWD/out/deps/SDL-install-linux-x64" -DPython3_EXECUTABLE="$PWD/.venv/bin/python" -DVIRTOOLSDEV_BUILD_UI_TESTS=OFF
cmake --build --preset linux-x64 --parallel 8
ctest --preset linux-x64 --no-tests=error
```

To include UI tests, initialize the dependency and reconfigure that test build
with `VIRTOOLSDEV_BUILD_UI_TESTS=ON`, then rebuild:

```sh
git submodule update --init --recursive external/imgui_test_engine
cmake --preset linux-x64 -DVIRTOOLSDEV_BUILD_UI_TESTS=ON
cmake --build --preset linux-x64 --parallel 8
ctest --preset linux-x64 -N
ctest --preset linux-x64 -LE desktop --no-tests=error
```

Use `windows-x64`, `windows-x86`, `macos-x64` or `macos-arm64` instead as appropriate.
`-N` lists registered tests. `-R "^VirtoolsDevUi[.](setup|propertyedits|layout)$"`
selects the Setup, property-edit and Layout UI groups for a focused run.

### Original samples

The tests cover composition editing, Undo/Redo, cancellation, save failures,
save/reopen, InterfaceChunk serialization, resources, text, layout, input,
snapshots and printing. Supply original sample compositions under
`local-samples/data`, or set `VIRTOOLSDEV_SAMPLE_ROOT` to the directory that
contains their `data/` folder. Preserve the original paths, including:

```text
data/BBSamples/Interface/2D Flow Layout.cmo
data/BBSamples/3D Transformations/Add Control Point.cmo
data/Demo/SoundIsland/SoundIsland.cmo
```

Copy `tests/data/verification/interfacechunk_samples.txt` into that collection's
`data/verification/` directory. It lists the additional sample paths used by
the model checks. Rerun configure after adding samples. Some tests are
registered only when their fixtures exist; others return CTest skip code 77.
Neither an absent test nor a skipped test counts as a successful regression.
Compare the test inventory as well as failures and skips.

### Desktop and display requirements

Core tests that render a Layout and all UI groups need a display and a working
SDL GPU backend. On a Linux host without a desktop, install Xvfb and use:

```sh
sudo apt-get install xvfb xauth
SDL_AUDIO_DRIVER=dummy xvfb-run -a -s "-screen 0 1920x1080x24" ctest --preset linux-x64 -LE desktop --no-tests=error
```

Xvfb supplies a display, not a GPU backend or window manager. Native desktop
tests must run in an unlocked desktop session as the logged-in user; they
move focus, enter fullscreen and operate real dialogs:

```sh
cmake --preset linux-x64 -DVIRTOOLSDEV_TEST_DESKTOP=ON
cmake --build --preset linux-x64 --parallel 8
ctest --preset linux-x64 -L desktop --no-tests=error
```

Linux file-dialog tests use the desktop's accessibility service and system
`/usr/bin/python3`; install `python3-gi` and `gir1.2-atspi-2.0` for them. These
are separate from the resource compiler's Python virtual environment. Linux
printing tests use GTK's Print to File destination and inspect cancellation,
submission errors and PDF output. A physical print needs a configured printer;
job acceptance does not prove paper output. Fullscreen and focus checks have
been validated through X11 in GNOME's Wayland desktop; native Wayland is not
covered by that result.

### Standalone HTML module

This module can be built and tested without Ballanced, SDL3, ImGui, Python or
the editor's resources. It needs CMake, Git and a C/C++ compiler. Initialize
`external/litehtml` as above, then run:

```sh
cmake -S html -B out/html -DCMAKE_BUILD_TYPE=Release -DNEMODEV_HTML_BUILD_TESTS=ON
cmake --build out/html --config Release --target NeMoDevHtmlTests --parallel 2
ctest --test-dir out/html -C Release --output-on-failure --no-tests=error
```

## ImGui Test Engine license

ImGui Test Engine is used only by `VirtoolsDevUiTests`; the regular `NeMoDev`
executable does not link it. The pinned dependency uses
[Dear ImGui Test Engine License v1.04](licenses/ImGuiTestEngine-License.txt),
which is separate from Dear ImGui's MIT license.

A free license applies to natural persons, users that are not legal entities,
not-for-profit entities, educational use, qualifying derivative software
released publicly under an OSI-defined Open Source license, or legal entities
with turnover below USD 2 million (or equivalent) in their last fiscal year.
Meeting any one condition is sufficient. If none applies, the license permits
a maximum 45-day trial; continued use then requires a paid license from DISCO
HELLO. Public source availability alone does not establish eligibility under
the Open Source license condition.

Preserve its copyright notice and full license when redistributing Test Engine
or the UI test executable. The full terms govern use and distribution; see also
[third-party notices](THIRD_PARTY_NOTICES.md#ui-test-dependency).

For the editor and core tests, initialize `external/imgui` and `external/litehtml`,
then configure with `-DVIRTOOLSDEV_BUILD_UI_TESTS=OFF` to omit Test Engine.

## Verified platforms

Validation on 2026-10-25 used a clean checkout of Ballanced's public default
`sdl` branch (`be42c19c95521e0bff4461063208b1221fdfe1ca`) and its recorded
submodules. No SDK patches were applied. The test build included the optional
help archive and original samples; these are needed to reproduce its inventory.

| Platform | Toolchain / preset | Result |
| --- | --- | --- |
| Windows 11 x64 | Visual Studio 2022; Release; SDL 3.4.16 | Clean editor build; 57/57 selected CTest tests passed; installed production program passed startup and isolation checks |
| Ubuntu 26.04.1 x64 | GCC 15.2; CMake 4.2.3; Release; SDL 3.4.18 | Clean build; 90/90 registered CTest tests passed, with no skips; relocated installation opened a blank composition and an original sample, rendered its UI and exited cleanly without development library paths |
| Windows x86 | `windows-x86` | Preset available; not verified |
| macOS x64 | `macos-x64` | Preset available; editor build and runtime not verified |
| macOS ARM64 | `macos-arm64` | Preset available; editor build and runtime not verified |

The Windows selection covers the core cases, sample save/reopen, HTML, font
and license deployment, production isolation, and the File menu, Schematic,
Level, Setup and property-edit UI groups. Windows desktop operations were
not rerun for this SDK update.

The Ubuntu run covered 86 non-desktop tests and four desktop groups: Player
Mode/fullscreen/focus/snapshots, asynchronous native file dialogs, native
printing and Building Block F1 help. It ran on GNOME with X11 and Vulkan on
Intel Iris Xe. The submitted PDF was independently checked as four A4 pages.
The installed CK2, VxMath, renderer and libharu were loaded from the relocated
package, and the production binary passed the Test Engine isolation check.
This did not verify physical paper output or native Wayland desktop behavior.

The editor-only `windows-x64-release` and `linux-x64-release` presets were also
built and installed without regression targets. A fresh Ubuntu checkout with
only ImGui and litehtml initialized built without CHM help or 7-Zip, and its
installed editor rendered blank and sample compositions under Xvfb. The
standalone HTML build and its two tests passed on both Windows and Ubuntu.

## Current limitations

- Building requires the separately supplied Ballanced sources;
  full regression tests also require original sample compositions.
  The repository alone is not a complete build or test distribution, and it
  does not pin a Ballanced revision.
- Legacy CK text depends on the host character set. Preserving its bytes does
  not guarantee identical display across machines or unrestricted Unicode
  names through every SDK interface.
- Worksets are kept for the current editor run; their membership is not saved
  with the composition.
- Printing depends on the platform's configured destinations. A successful
  submission reports acceptance by the print system, not completed paper
  output. Image-page tests do not verify a physical printer.
- Windows retains native structured-exception protection. Other platforms do
  not recover from native access violations.
- The build produces a staged runtime directory. Platform installers and a
  macOS application bundle are not provided.

## License

The [CC BY-NC-SA 4.0 license](LICENSE) applies only to NeMoDev-authored code. Incorporated
VirtoolsScriptDeobfuscation code and its adaptations remain CC BY-NC-SA 4.0;
dependencies, fonts and original Virtools resources retain their own terms.
The project license does not relicense the editor's third-party components.

[Third-party notices](THIRD_PARTY_NOTICES.md) describe the license scope,
authorship, copied license texts and unresolved SDK redistribution permissions.
The Runtime install stages `LICENSE`, `THIRD_PARTY_NOTICES.md` and `licenses/`
at the root of `runtime/`; keep those materials with the corresponding binaries.
They do not grant redistribution rights for original Virtools assets.

## Source layout and attribution

- `core/`: runtime ownership, compositions, edits and history, InterfaceChunk
  persistence, Schematic geometry and layout.
- `ui/`: SDL application, ImGui views, GPU textures, dialogs and platform printing.
- `html/`: independent static HTML/CSS renderer and its standalone tests.
- `tests/`: core and UI regressions; `tools/`: local resource preparation.
- `cmake/`: direct Ballanced source integration and runtime staging.

The InterfaceChunk reader/writer, Schematic reconstruction, geometry, routing,
runtime directory helpers and parameter verification were incorporated from
[BearKidsTeam/VirtoolsScriptDeobfuscation](https://github.com/BearKidsTeam/VirtoolsScriptDeobfuscation),
revision `9e8fc2ae01b4f25565df67488e03d5e8972bc2f9`. They retain their authorship
and [CC BY-NC-SA 4.0 license](licenses/VirtoolsScriptDeobfuscation-CC-BY-NC-SA-4.0.txt).
[The source manifest](core/interface/origin.json) records the original paths and
hashes. Adaptations include repository-relative includes, module placement,
portable bitmap handling, runtime loading and compiler fixes. These sources are
compiled directly into the editor; there is no dependency on an upstream binary
or checkout.

[Ballanced](https://github.com/doyaGu/Ballanced),
[Dear ImGui](https://github.com/ocornut/imgui),
[ImGui Test Engine](https://github.com/ocornut/imgui_test_engine), SDL3, Noto fonts
and the runtime components retain their respective licenses. Font licenses and
download checksums are kept under `resources/fonts/`. Original Virtools assets
and SDK samples retain their original ownership and are not covered by the
incorporated code's license.
