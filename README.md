# NeMoDev

A desktop editor for Virtools compositions, built with SDL3, SDL GPU and Dear
ImGui. NeMoDev builds the Ballanced runtime from source and uses that same runtime
for editing, playback and tests.

The editor includes Schematic editing, 3D Layout, Level and Hierarchy Managers,
object and parameter editors, a Script debugger, Undo/Redo, snapshots, printing
and Building Block help. Compositions can be opened, edited, saved and reopened.

## Requirements

- CMake 3.25 or newer, a C compiler and a C++17 compiler.
- Shared SDL3 3.4.8 or newer, installed in a CMake package prefix.
- Git and Python 3.8 or newer with Pillow 9.1 or newer. CMake must find the
  Python interpreter that has Pillow installed.
- Optional local help: CHM documentation and 7-Zip (`7zz`, `7z` or `7za`).
- Windows: Visual Studio 2022 with C++ tools.
- Linux: Ninja for the preset, pkg-config, GTK3 Unix Print, GIO Unix and libharu
  development files. Ubuntu packages: `ninja-build`, `pkg-config`, `libgtk-3-dev`,
  `libglib2.0-dev` and `libhpdf-dev`. Native printing uses GTK's Unix print
  backend, including CUPS or Print to File.
- macOS: Xcode command-line tools, Ninja and libharu; printing uses AppKit.

The editor needs a working SDL GPU backend and graphics driver at runtime.
SDL3's own build dependencies depend on the window, audio and GPU backends
enabled in its installation. All native libraries must match the target
architecture.

The build presets are `windows-x86`, `windows-x64`, `linux-x64`, `macos-x64` and
`macos-arm64`. See [Verified platforms](#verified-platforms) for the platforms
actually checked.

## Prepare dependencies

```sh
git submodule update --init --recursive external/imgui external/litehtml external/imgui_test_engine
python -m pip install "Pillow>=9.1"
```

The presets enable UI tests, which use the separately licensed ImGui Test
Engine. Review [its license conditions](#imgui-test-engine-license) before use.

The editor always requires Dear ImGui and litehtml, including litehtml's bundled
Gumbo parser. Without UI tests, initialize only `external/imgui` and
`external/litehtml`, and configure with `-DVIRTOOLSDEV_BUILD_UI_TESTS=OFF`.

Provide a maintained Ballanced source checkout with VxMath, CK2, RenderEngine,
Managers, Plugins and BuildingBlocks, including their nested dependencies.
Initialize the required submodules in that checkout, then set
`VIRTOOLS_SDK_PATH` explicitly when configuring. The selected sources must
provide the SDL runtime and font deployment used by the editor. No SDK path or
SDK revision is supplied by the shared configuration.

NeMoDev builds these components as shared libraries directly from those sources,
without maintaining SDK copies or patches. The Ballanced root project, Player
and game-asset staging are excluded from the editor build. Original binary
Virtools SDKs and runtimes are not supported by this build.

For SDL3, build a shared installation with `SDL_SHARED=ON`, `SDL_STATIC=OFF` and
`SDL_TESTS=OFF`, then pass its installation prefix as `CMAKE_PREFIX_PATH`.

## Resources

Images, icons, `interface.nmo` and the Noto fonts are included under `resources/`,
the default resource directory. Optional CHM documentation and the original
test samples are supplied separately. To enable local help, copy
`data/documentation.chm` from an existing Virtools Dev 2.5 installation to
`local-resources/data/documentation.chm` before configuring. The same file can
be transferred to a Linux or macOS build machine.

To regenerate a local resource collection from that installation, build the
standalone resource extractor on Windows:

```powershell
cmake -S tools -B out/resource-tools -G "Visual Studio 17 2022" -A x64
cmake --build out/resource-tools --config Release
out/resource-tools/Release/ExtractResources.exe "<Virtools Dev install>" local-resources resources/images.txt
```

The extractor needs neither Ballanced nor the editor's resources to build. It
reads `Dev.exe`, `resvis.dll`, `Managers/CK2UI.dll` and the installation's data
files. The manifest at `resources/images.txt` selects the images to extract.

To verify the bundled fonts or download missing files:

```sh
python tools/PrepareFonts.py
```

This checks the recorded Noto fonts and OFL licenses in `resources/fonts`
against their pinned checksums, downloading only missing files. It reuses
matching files and refuses to overwrite different ones. For a custom resource
directory, pass `--output "<resource directory>/fonts"`.

An alternative directory containing `images.txt`, `images/`,
`data/interface.nmo` and `fonts/` can be selected with `VIRTOOLSDEV_RESOURCES_DIR`.
The build first looks for `data/documentation.chm` in that directory, then in
`local-resources/`. The CHM is excluded from Git. When present, 7-Zip extracts
it into local HTML while retaining the Building Block topic paths. Without
it, the build skips help extraction and deployment and does not require 7-Zip.
The editor disables local help menus and shortcuts when extracted help is not
installed. Resource images are embedded at build time. The SDK stages the
verified fonts and their licenses into `runtime/Bin/data/fonts`.

## Build and run

From the NeMoDev source directory on Windows:

```powershell
cmake --preset windows-x64 -DVIRTOOLS_SDK_PATH="<Ballanced source checkout>" -DCMAKE_PREFIX_PATH="<SDL3 installation prefix>"
cmake --build --preset windows-x64 --parallel 8
out/windows-x64/runtime/Bin/NeMoDev.exe
```

On Linux:

```sh
cmake --preset linux-x64 -DVIRTOOLS_SDK_PATH="<Ballanced source checkout>" -DCMAKE_PREFIX_PATH="<SDL3 installation prefix>"
cmake --build --preset linux-x64 --parallel 8
out/linux-x64/runtime/Bin/NeMoDev
```

For macOS, choose `macos-x64` or `macos-arm64`; the executable is
`out/<preset>/runtime/Bin/NeMoDev`. Add any other custom library prefixes to
`CMAKE_PREFIX_PATH`. Shared libraries, plugin category directories, rendering
settings and data are staged under `runtime/`; keep that directory tree together.
`VIRTOOLS_SDK_PATH` selects the Ballanced source tree to build against.
Machine-specific overrides belong in an ignored `CMakeUserPresets.json`.

The CMake targets and options retain the `VirtoolsDev` / `VIRTOOLSDEV_` names.
Use `VIRTOOLSDEV_BUILD_TESTS=OFF` and `VIRTOOLSDEV_BUILD_UI_TESTS=OFF` to build
without the regression executables. To keep core tests while omitting ImGui
Test Engine, set only `VIRTOOLSDEV_BUILD_UI_TESTS=OFF`.

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
not count as successful regressions. Some sample tests are registered only
when their fixtures exist; others report CTest skip code 77. Check both the
registered test list and skipped results when comparing runs.

The latest focused regression can be run with:

```sh
ctest --preset windows-x64 -E "VirtoolsDevUi|VirtoolsDevLayout$"
ctest --preset windows-x64 -R "^VirtoolsDevUi[.](setup|propertyedits|layout)$"
```

Replace the preset for another platform. The first command excludes all UI
groups and the standalone rendered Layout test. The second runs the Setup,
property-edit failure and Layout UI groups; neither command is the full suite.

On a Linux build host without a desktop, use
`SDL_AUDIO_DRIVER=dummy xvfb-run -a ctest --preset linux-x64 -LE desktop` with a
working SDL GPU backend. Xvfb supplies a display, not a GPU backend or window
manager. Desktop tests require a real window manager and can be
registered with `VIRTOOLSDEV_TEST_DESKTOP=ON`. They move focus, enter fullscreen
and operate native dialogs. Direct printing requires a configured printer.

The standalone HTML module can be built and tested without Ballanced, SDL3,
ImGui, Python or the editor's resources. Initialize `external/litehtml`, then run:

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

Validation on 2026-10-18, after the property-edit fixes in `fea0398`, used SDL3
3.4.18 and prepared resources and samples:

| Platform | Toolchain / preset | Result |
| --- | --- | --- |
| Windows 11 x64 | Visual Studio 2022; `windows-x64` configuration | Editor built; 53/53 core checks and 36/36 selected UI scenarios passed |
| Ubuntu 26.04 x64 | GCC 15.2; Release build | Editor built; 53/53 core checks and 36/36 selected UI scenarios passed under Xvfb |
| Windows x86 | `windows-x86` | Preset available; not verified in this baseline |
| macOS x64 | `macos-x64` | Preset available; editor build and runtime not verified |
| macOS ARM64 | `macos-arm64` | Preset available; editor build and runtime not verified |

The selected UI scenarios comprise 17 Setup checks, 3 property-edit checks and
16 Layout checks. Together with the core checks, they cover actual values,
texture pixels and slots, Undo/Redo, Escape cancellation, partial-write recovery
and document-close handling. These results cover the listed builds and
scenarios, not every graphics backend or desktop workflow. Native file dialogs,
Player Mode, fullscreen, focus transitions, direct printing and help launch were not rerun
as desktop acceptance tests in this baseline.

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
