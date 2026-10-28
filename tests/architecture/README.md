# Stage zero architecture checks

`coverage.json` is the executable source of truth for the 32 source groups,
32 feature/extension targets and 25 state families. It records CTest evidence,
future verification gaps, existing include dependencies and bounded legacy
UI access. These are migration records, not runtime registries.

`NeMoDevArchitecture` runs from CTest. It includes tracked and untracked,
non-ignored files, so a newly added file must have exactly one source group.
Each group must belong to a feature, each state must have one named owner and
lifetime, and test anchors must exist in the configured CTest inventory.
UI anchors are required only when UI tests are configured. Open gaps have an
explicit phase deadline; advancing `completed_phase` does not waive them.

The include graph checks direct dependencies between groups and transitive
UI exposure to the mutable document, EditorState and SDK. Existing exposure
cannot grow beyond commit `029f2c9`, even by adding a migration exception.
Removed edges and exceptions must also be removed from the manifest. The final
deadline for ordinary UI/native exposure is phase five; DocumentFiles is due
in phase two. Current mixed window headers remain explicitly listed until
their complete dependency path has migrated.

This is a static include constraint, not a proof that every write is correct.
`VirtoolsDevLayering` still enforces the existing core/UI, private-header,
standalone HTML and production/test rules. Both checks must pass. Future
module extraction narrows compile-time access rather than expanding these
legacy exceptions. Git and the fixed baseline commit are required; editor CI
checks out history for that purpose.

`UiCases.def` freezes 143 existing portable, literal UI test registrations.
The app suite verifies them against the actual Test Engine registry and checks
default dock relationships, Reset Interface and Ctrl+P. Dynamic property and
native-desktop cases retain their separate checks. The complete core case list
is also retained in the manifest. These inventories supplement behavior tests;
they do not turn unavailable sample regressions into passing tests.

`VirtoolsDev.NativeObjectLifecycles` verifies existing SDK notification order,
batch destruction, actual ID reuse and ClearAll generations without SDK
changes. It also records that `CK_DESTROY_NONOTIFY` bypasses sequence callbacks
while still allowing ID reuse. Phase one must cover that path through controlled
destruction and a validated native-access identity policy; notification callbacks
alone do not prove lifetime tracking. `NeMoDevArchitectureChecker` tests rejected ownership, dependency,
indirect exposure and evidence changes using temporary source repositories.

```sh
cmake --build --preset macos-arm64 --parallel 8
ctest --test-dir out/macos-arm64 --output-on-failure -j 8
```

Use the corresponding Windows or Linux preset. Missing original samples stay
visible as skips, and native desktop acceptance remains separate. The hosted
CI selection continues to require all selected self-contained tests to pass.

## Default workspace and input inventory

Reset Interface places 3D Layout above Schematic/Level Manager on the left,
Building Blocks on the top right, and Schematic/Level Manager in the same lower
dock. The default-workspace UI test observes these relationships rather than
saved dock IDs or exact pixel coordinates.

The application shortcut baseline below follows the existing focus and modal
rules. Text input, an active edit/tool or a popup can prevent application
shortcuts. View-local handling takes precedence where indicated. These entries
document the default behavior; they are not a second runtime binding table.

| Input | Current default operation |
| --- | --- |
| Ctrl+L or Ctrl+O | Load Composition |
| Ctrl+S | Save Composition |
| Ctrl+Shift+S | Save Version |
| Ctrl+Z / Ctrl+Y | Undo / Redo |
| Ctrl+Shift+Z | Revert to Saved |
| Ctrl+P | General Preferences |
| Ctrl+T | Trace mode outside Schematic; Schematic owns its local handling |
| Ctrl+Space | Play/Pause without Shift; Alt changes this to a step |
| Alt+Space or Ctrl+Alt+Space | One step without Shift |
| Ctrl+Backspace, including Shift/Alt variants | Restore Initial Conditions |
| Alt+Backspace without Shift | Restore Initial Conditions |
| F1 | Selected behavior help in Schematic; otherwise the reference |
| F3 / Shift+F3 | View-local Find, then Schematic/Explorer next or previous |
| Ctrl+F3 in Schematic | Find selected element, Shift reverses direction |
| Ctrl+F | Local Find/search in Level Manager, Schematic or Help; otherwise Explorer |
| Ctrl+Shift+F | Explorer where the focused view does not consume Find |
| Ctrl+Alt+F12 | All CK Objects In Memory |

Local keys, mouse tools, drag/drop and dialog accelerators retain the following
source owners and feature coverage until migrated. The fixed core/UI cases
exercise their behavior where fixtures are available; the remaining fixture
and replacement tests stay explicit in the feature gaps.

| Input surface | Current implementation | Coverage |
| --- | --- | --- |
| Script selection, navigation, links, shortcuts, context menus | `ui/schematic/SchematicInput.cpp`, `SchematicLinking.cpp`, `SchematicMenus.cpp`, `SchematicDialogs.cpp` | F11/F12/F18/F23 |
| 3D navigation, transforms, curves, fullscreen and Player Mode | `ui/layout/LayoutInput.cpp`, `LayoutMenu.cpp`, `LayoutDrop.cpp`, `LayoutFullScreen.cpp` | F15/F16/F17/F23 |
| Level/Hierarchy/Attribute rows and editors | `ui/panels/LevelManager.cpp`, `HierarchyManager.cpp`, `ManagerWindows.cpp` | F08/F09/F10 |
| Values, pickers, live controls and confirmation/cancel | `ui/controls/ValueEditor.cpp`, `ParameterBox.cpp`, `DevControls.cpp`, `TypeAhead.cpp`, `ui/setup/` | F13/F14/F15 |
| File prompts, native dialogs, external drop and clipboard | `ui/app/EditorApp.cpp`, `FileMenu.cpp`, `ui/controls/FileDialogs.cpp`, `ui/app/Main.cpp` | F01/F02/F03/F04/F23/F28 |
| Resources, diagnostics, Help and their local search | `ui/tools/`, `ui/panels/DebuggerPanel.cpp`, `ui/help/HelpWindow.cpp` | F19/F20/F21/F26 |

## Native viewport performance baseline

The benchmark runs the real editor with isolated preferences, a 1280 by 800
window and 64 instances of a shared 12-triangle cube. It validates actual draw
statistics and opaque pixels. The viewport dimensions, framebuffer dimensions,
GPU and driver information are recorded, rather than assuming display scale.
It uses the existing imported texture path and ordinary UI submission.

There are at least fifteen seconds of warmup, then at least 11 groups, each lasting
at least one second. `--warmup` and `--frames` set minimum frame counts, not a
replacement for those durations. The test driver respects Test Engine's
maximum-speed request only in this dedicated timing category; existing functional
tests retain their original frame delay and the production loop is unchanged. GPU idle waits occur
at group boundaries only. One verification readback occurs after measurement.

The measured quantity is complete editor-frame throughput including GPU drain,
not a GPU-only timestamp or an isolated texture-transfer timer. Viewport image
readbacks, CPU image uploads, target creations, path selections and releases
must remain zero, and the same UiTexture remains bound. Application allocation,
GPU copy/pass/submit counts, capability queries and resource retain/release are
not instrumented; reports list these explicitly as unmeasured.

Run timing separately from builds and regression suites:

```sh
python3 tools/MeasureViewport.py --build out/macos-arm64 \
  --binary out/macos-arm64/runtime/NeMoDev.app/Contents/MacOS/VirtoolsDevUiTests \
  --output out/macos-arm64/architecture/viewport.json
```

Windows and Linux use their built `VirtoolsDevUiTests` executable. Alternatively,
configure `VIRTOOLSDEV_TEST_PERFORMANCE=ON` and run `ctest -L performance`; its
test runs serially. Timing is excluded by default from shared hosted CI.

Before accepting a reference on a new machine, repeat the same command with a
different output path and `--compare` pointing at the first report. Both runs
must pass their variation limit and the same-binary control limit. Preserve both
raw captures; do not record a faster control run as an architecture improvement.

Reports retain all raw groups, median, linearly interpolated p10/p90, median
absolute deviation (MAD) and MAD/median. MAD/median above 3% is an unstable run.
`--compare REFERENCE.json` requires matching scene, dimensions, timing protocol,
GPU/driver, OS/hardware, compiler flags, SDL, SDK revisions and harness. A changed
SDK working tree or an unstable reference is rejected. A median regression over
3% fails. A comparison of the same binary is a control: a median shift of more
than 3% in either direction fails, rather than being counted as an improvement.
Binary hashes and the source revision identify each measurement;
different editor binaries are expected when comparing architecture changes.

The machine-specific reference in `baselines/` is evidence for its recorded
environment, not a portable timing limit for other devices. Keep references
unchanged; write candidate measurements to a different output path. The Python
statistics/comparison tests run without a GPU as `NeMoDevViewportMeasurement`.

The recorded macOS ARM64 / Metal reference includes both 11-group captures,
their same-binary control result and the discarded five-second warmup protocol.
Its median is 0.251198 ms per complete editor frame, p10/p90 are
0.249723/0.256822 ms and MAD is 0.001132 ms (0.451%). The control median is
0.244323 ms, a -2.737% shift, within the symmetric 3% control limit. These are
observed baseline timings, not an architecture performance improvement.

The functional record preserves the initial 82 passes and 15 sample skips, and
the stage-zero 86 passes and the same 15 skips. Original sample and other-platform
acceptance remain explicit limitations. The production render path and SDK
sources are unchanged.
