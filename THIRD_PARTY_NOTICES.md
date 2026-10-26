# Third-party notices

The CC BY-NC-SA 4.0 grant in [LICENSE](LICENSE) covers NeMoDev-authored code only. Each
component below retains its own copyright and license. Inclusion of a license
text does not mean that every component is bundled in every build or that an
unresolved redistribution permission has been granted.

## Incorporated source

The InterfaceChunk reader/writer, Schematic reconstruction, geometry, routing,
runtime directory helpers, parameter verification and adapted build helpers
come from [BearKidsTeam/VirtoolsScriptDeobfuscation](https://github.com/BearKidsTeam/VirtoolsScriptDeobfuscation),
revision `9e8fc2ae01b4f25565df67488e03d5e8972bc2f9`.

These files and the NeMoDev adaptations of them remain under
[CC BY-NC-SA 4.0](licenses/VirtoolsScriptDeobfuscation-CC-BY-NC-SA-4.0.txt).
The license requires attribution, noncommercial use and sharing adaptations
under the same or a compatible license. NeMoDev-authored code uses CC BY-NC-SA
4.0 as well; dependencies and original resources keep their separate terms.

The source manifest is `core/interface/origin.json` in the source tree and
`licenses/VirtoolsScriptDeobfuscation-origin.json` in the staged runtime. It
records the original paths, revision, hashes and destinations, including the
adapted CMake helper. Changes include repository-relative includes, module
placement, portable bitmap ownership, runtime loading, native path conversion
and compiler fixes. NeMoDev compiles this material directly; it does not require
an upstream checkout or binary.

## Editor dependencies

| Component and authors | Use | License text |
| --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui), Omar Cornut and contributors | Editor UI and SDL3/SDL GPU backends; `v1.93.0-docking`, revision `b916186e66aa3c02b72c6ca17455516512ed8049` | [MIT](licenses/DearImGui-MIT.txt) |
| [SDL3](https://github.com/libsdl-org/SDL), Sam Lantinga and contributors | Shared window, input, GPU and platform runtime | [zlib](licenses/SDL3-zlib.txt) |
| [stb](https://github.com/nothings/stb), Sean Barrett and contributors | Editor image loading/writing and SDK image/font support | [MIT or public domain](licenses/stb-LICENSE.txt) |
| [libharu](https://github.com/libharu/libharu), Takeshi Kanno, Antony Dovgal and contributors | Internal PDF generation for Linux/macOS printing | [zlib-style license](licenses/libharu-zlib.txt) |

SDL3 and libharu versions are selected by the build environment. The license
text origins in `licenses/origin.json` identify the copied notices, not a
required SDK or library version. Preserve the matching upstream notices when
redistributing a different version or a library with additional dependencies.

## UI test dependency

[Dear ImGui Test Engine](https://github.com/ocornut/imgui_test_engine), copyright
2018-2026 Omar Cornut, is used only by `VirtoolsDevUiTests`. The `NeMoDev`
executable does not link it. The submodule revision is
`16b6efbb393fdef0dcac9b0794a8ee476606a152`.

It uses the [Dear ImGui Test Engine License v1.04](licenses/ImGuiTestEngine-License.txt),
not Dear ImGui's MIT license. A free license applies if any one of these
conditions holds:

- The user is a natural person.
- The user is not a legal entity, or is a not-for-profit legal entity.
- The use is educational.
- The software is used to create derivative software released publicly under
  an Open Source license as defined by the Open Source Initiative.
- The user is a legal entity whose turnover in its last fiscal year was less
  than USD 2 million, or equivalent.

Otherwise the license provides a maximum 45-day trial, after which continued use
requires a paid license from DISCO HELLO. Public source availability alone does
not establish eligibility under the Open Source license condition. Preserve
the copyright notice and full license when distributing Test Engine or its
derivatives, including the UI test executable.

The CMake presets enable UI tests by default. Set
`VIRTOOLSDEV_BUILD_UI_TESTS=OFF` to omit this dependency; core tests remain
available. Its license is included with the notices for reference even when
the UI test executable is not built.

## Ballanced runtime and its dependencies

The editor builds a separately supplied [Ballanced](https://github.com/doyaGu/Ballanced)
checkout. Its root project carries [MIT](licenses/Ballanced-MIT.txt), copyright
2022 kakuty. Runtime components and their nested dependencies have separate
terms; the root license does not replace them.

| Component | Role | License or status |
| --- | --- | --- |
| VxMath, CK2, RenderEngine, BuildingBlocks, Plugins and ParameterOperations | Runtime libraries, Building Blocks, readers and parameter operations | [Apache-2.0](licenses/Apache-2.0.txt); retain the selected checkout's copyright and NOTICE materials |
| [miniz](https://github.com/richgel999/miniz), RAD Game Tools, Valve Software, Rich Geldreich and Tenacious Software LLC | CK file compression | [MIT](licenses/miniz-MIT.txt) |
| [SIMDe](https://github.com/simd-everywhere/simde), Evan Nemerson and contributors | Portable VxMath SIMD operations | [MIT](licenses/SIMDe-MIT.txt) |
| [Qhull](https://github.com/qhull/qhull), C.B. Barber and The Geometry Center | 3D Building Block geometry | [Qhull license](licenses/Qhull-COPYING.txt); original source is available from [qhull.org](https://www.qhull.org/) |
| [dr_wav](https://github.com/mackron/dr_libs), David Reid | WAV reader | [MIT-0 or public domain](licenses/dr_wav-LICENSE.txt) |
| SdlInputManager and SdlSoundManager | Runtime input and sound managers | No explicit license grant was found in the examined component repositories; redistribution permission remains to be confirmed |
| [IVP](https://github.com/doyaGu/ivp), Valve LLC and Ipion Software GmbH | Linked into `physics_RT` | Valve/Ipion copyright notices exist, but no redistribution license file was found in the examined checkout; permission remains to be confirmed |

The SDK is external to this repository. The supplied notices record known
components; they do not grant missing rights or certify an arbitrary SDK
checkout for redistribution. In particular, including this notice does not
resolve the SDL manager or IVP permissions above. These components remain
enabled by the editor build; confirm their permissions before distributing a
runtime containing them. Additional SDK NOTICE files or dependencies must be
carried with the corresponding build.

## Fonts

Noto Sans and Noto Sans SC are by the Noto Project Authors and are licensed under
the SIL Open Font License 1.1. Copyright statements and full terms are in
`resources/fonts/OFL-NotoSans.txt` and `resources/fonts/OFL.txt` in the source
tree, and `licenses/fonts/` in the runtime. The SDK also places these notices
beside the deployed fonts in `Bin/data/fonts`.

`resources/fonts/sources.json` records immutable download revisions and SHA-256
checksums. It is copied to `licenses/fonts/sources.json` in the runtime. The
recorded fonts are included under `resources/fonts/`. Retain their notices
when bundling the fonts.

## System libraries and build tools

Linux uses the system GTK3 Unix Print and GLib/GIO libraries under the GNU LGPL
and other component licenses. A copy of [LGPL 2.1](licenses/LGPL-2.1.txt) is
provided for reference. The system packages
retain their own copyright notices and additional component licenses. They are
not copied into the runtime by this project. If bundling them, supply their
matching source, notices and the other materials required by their licenses.
macOS uses the system AppKit framework; Windows uses system APIs.

Windows binary packages include Microsoft's Visual C++ runtime DLLs under
the Visual Studio redistributable terms; the NeMoDev license does not cover
them. macOS packages include libpng when required by libharu, with the
installed libpng license copied to `Contents/Resources/licenses/libpng-LICENSE.txt`.

Python, Pillow, Git, CMake, platform compilers and 7-Zip are build tools supplied
by the user. This project does not bundle those tools. Their own licenses apply
if they are redistributed with a development environment or installer.

## Original Virtools resources

Virtools images, icons, `interface.nmo`, CHM documentation and sample
compositions retain their original ownership. NeMoDev provides no license or
redistribution grant for them. Images, icons and `interface.nmo` are included
under `resources/`; CHM documentation and samples are supplied locally and
excluded from the source repository. The build embeds images in the executable
and stages data and extracted help alongside it. A binary package containing
those assets requires the corresponding permissions. A source-code license is
not a grant for those resources.
