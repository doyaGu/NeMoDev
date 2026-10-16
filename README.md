# NemoDev

A desktop editor for Virtools compositions, built with SDL3, SDL GPU and Dear
ImGui. NemoDev builds the Ballanced runtime from source and uses that same runtime
for editing, playback and tests.

The editor includes Schematic editing, 3D Layout, Level and Hierarchy Managers,
object and parameter editors, a Script debugger, Undo/Redo, snapshots, printing
and Building Block help. Compositions can be opened, edited, saved and reopened.

## Requirements

- CMake 3.25 or newer and a C/C++17 compiler.
- Shared SDL3 3.4.8 or newer, installed in a CMake package prefix.
- Python 3 with Pillow, Git and 7-Zip (`7zz`, `7z` or `7za`).
- Windows: Visual Studio 2022 with C++ tools.
- Linux: Ninja, GTK3 Unix Print, CUPS, libharu and the SDL graphics dependencies.
  On Ubuntu, the print packages are `libgtk-3-dev libcups2-dev libhpdf-dev`.
- macOS: Xcode command-line tools, Ninja and libharu.

The build presets are `windows-x86`, `windows-x64`, `linux-x64`, `macos-x64` and
`macos-arm64`. SDL3 and the other native libraries must match the chosen
architecture. The presets describe build configurations; they do not establish
that every platform has been tested.

## Prepare dependencies

```sh
git submodule update --init external/imgui external/imgui_test_engine
python -m pip install Pillow
python tools/PrepareFonts.py
```

Provide a maintained Ballanced source checkout with its runtime components
initialized, and set `VIRTOOLS_SDK_PATH` explicitly when configuring. No SDK path
is supplied by the shared configuration. NemoDev builds those sources directly
without maintaining SDK copies or patches. The Ballanced Player and game-asset
staging are excluded from the editor build.

For SDL3, build a shared installation with `SDL_SHARED=ON`, `SDL_STATIC=OFF` and
`SDL_TESTS=OFF`, then pass its installation prefix as `CMAKE_PREFIX_PATH`.

## Prepare local resources

Original Virtools images, icons, `interface.nmo`, CHM documentation and sample
compositions are not distributed here. To use an existing Virtools Dev 2.5
installation, build the standalone resource extractor on Windows:

```powershell
cmake -S tools -B out/resource-tools -G "Visual Studio 17 2022" -A x64
cmake --build out/resource-tools --config Release
out/resource-tools/Release/ExtractResources.exe "<Virtools Dev install>" local-resources resources/images.txt
```

The extractor needs neither Ballanced nor the editor's resources to build.
`PrepareFonts.py` downloads the recorded Noto fonts and OFL licenses into
`local-resources/fonts`, checking every file against its recorded checksum.

The resulting resource directory contains `images.txt`, `images/`,
`data/interface.nmo`, `data/documentation.chm` and `fonts/`. For Linux or macOS,
prepare that directory on Windows and transfer it to the build machine. An
existing resource directory can be selected with `VIRTOOLSDEV_RESOURCES_DIR`.
Resource images are embedded at build time; the CHM is extracted into local HTML
while retaining the Building Block topic paths.

## Build and run

For example, with an SDL3 installation available on the CMake package search path:

```sh
cmake --preset windows-x64 -DVIRTOOLS_SDK_PATH="<Ballanced source checkout>" -DCMAKE_PREFIX_PATH="<SDL3 installation prefix>"
cmake --build --preset windows-x64 --parallel 8
out/windows-x64/runtime/Bin/NemoDev.exe
```

Replace the preset for another platform. On Linux and macOS the executable is
`out/<preset>/runtime/Bin/NemoDev`. Shared libraries, plugins, rendering settings
and data are staged under `runtime/`; keep those directories together.
`VIRTOOLS_SDK_PATH` selects the Ballanced source tree to build against.
Machine-specific overrides belong in an ignored `CMakeUserPresets.json`.

The CMake targets and options retain the `VirtoolsDev` / `VIRTOOLSDEV_` names.
Use `VIRTOOLSDEV_BUILD_TESTS=OFF` and `VIRTOOLSDEV_BUILD_UI_TESTS=OFF` to build
without the regression executables. The ImGui Test Engine has a separate license.

CK strings default to CP936, configurable in General Preferences. Unedited
strings retain their original bytes; an unrepresentable edit reports an error.
Filesystem paths use UTF-8. Fonts use GDI or stb_truetype, and text conversion uses
Windows code-page APIs or iconv.

## Tests

```sh
ctest --preset windows-x64
```

The test sources cover composition editing, Undo/Redo, cancellation, file round
trips, InterfaceChunk serialization, resources, encoding, layout, input,
snapshots and printing. Original sample compositions are supplied separately
under `local-samples/data`, or through `VIRTOOLSDEV_SAMPLE_ROOT`. Copy
`tests/data/verification/interfacechunk_samples.txt` into that collection's
`data/verification/` directory. The list preserves the original sample paths.
Supply the complete sample collection for the full suite; missing samples do
not count as successful regressions.

On a Linux build host without a desktop, use
`SDL_AUDIO_DRIVER=dummy xvfb-run -a ctest --preset linux-x64 -LE desktop` with a
working SDL GPU backend. Desktop tests require a real window manager and can be
registered with `VIRTOOLSDEV_TEST_DESKTOP=ON`. They move focus, enter fullscreen
and operate native dialogs. Direct printing requires a configured printer.

Public CI checks include source layering, Python syntax and the standalone
Windows extractor build. Full editor builds and runtime regressions require the
local resources described above.

## Source layout and attribution

- `core/`: runtime ownership, compositions, edits and history, InterfaceChunk
  persistence, Schematic geometry and layout.
- `ui/`: SDL application, ImGui views, GPU textures, dialogs and platform printing.
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
