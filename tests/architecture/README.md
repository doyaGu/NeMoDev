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
