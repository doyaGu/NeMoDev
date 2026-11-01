# Stage zero architecture checks

`coverage.json` lists the 57 source groups,
32 feature/extension targets and 46 state families. It records CTest results,
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
deadline for ordinary UI/native exposure is phase five. DocumentFiles has moved
to application and no longer depends on EditorState. Current mixed window headers remain explicitly listed until
their complete dependency path has migrated.
Objective-C++ sources and `#import` follow the same dependency and exposure
checks as C++ sources and `#include`.

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

`VirtoolsDev.NativeObjectLifecycles` preserves the notification-order, batch
destruction, ID-reuse and ClearAll baseline. `CK_DESTROY_NONOTIFY` still bypasses
sequence callbacks. Native object-table slots now own `CKObjectHandle` lifetimes;
unregistering the actual object expires its handle, including silent deletion
and direct destruction. Deferred or failed deletion retains the identity for
as long as the legacy `GetObject(id)` still returns that object. Notifications,
FREEID allocation order and file ID remapping keep their original behavior.

`DocumentToken` identifies an editing scope attached to the host's context.
Successful New/Open replaces that scope; Close ends it. `ObjectRef` adds the
native slot and lifetime, resolved only inside that document's native-access
scope. These non-owning values are transient; they are not written into NMO,
CMO, NMS or InterfaceData. Viewport recreation does not replace the document
token. The SDK owns native identity; Dev does not issue object lifetimes or
force notification flags. Loaded/created/graveyard membership records keep
the native lifetime so that a reused ID cannot inherit membership or retention.

`NeMoDevIdentityHeaders` consumes the identity values independently from C and
C++, without SDK or STL headers. The five identity core cases cover silent
same-ID/same-address reuse, document replacement and failed loading, resource
collection, stale undo targets and ScriptSession replacement. The app UI suite
checks selection, lock, clipboard, Setup and delayed navigation on ID reuse.
`NeMoDevSdkObjectBehavior` compares native behavior and exact uncompressed
NMO/CMO/NMS bytes to fixtures captured from CK2 `9473e3c` before this refactor.
The SDK's own identity suite additionally tests deferred/throwing deletion,
constructor failure, dynamic transitions, restart and table growth; the render
engine suite verifies the actual ClearAll-preserved render hierarchy.

`NeMoDevArchitectureChecker` tests rejected ownership, dependency,
indirect exposure and test coverage changes using temporary source repositories.
The phase-one object identity, registration, rename, configuration and view
checks have passed. `completed_phase` is one: the final paired Release comparison
and subsequent same-program control passed the original 3% limits. The current
median was 1.116982 ms, p10 1.100396 ms, p90 1.155450 ms and MAD 1.442%.
The reference median was 1.116888 ms, a 0.0084% increase. Raw results, rejected
measurements, reference preparation and timing limits are preserved in
`baselines/object-editor-macos-arm64-metal.json`. The future multi-viewport
and remaining counter work stays explicit in F16.

The final macOS Release run passed 95 CTests; 15 original-sample suites were
skipped. It used `SDL_AUDIO_DRIVER=dummy` after diagnosing a local CoreAudio
device-open wait. The GPU tests still use Metal. Windows/Linux execution and
the missing original samples are not counted as validated. No additional SDK
source changes were made in the phase-one module and measurement work.

```sh
cmake --build --preset macos-arm64 --parallel 8
SDL_AUDIO_DRIVER=dummy ctest --test-dir out/macos-arm64 --output-on-failure -j 8
```

Use the corresponding Windows or Linux preset. Missing original samples stay
visible as skips, and native desktop acceptance remains separate. The hosted
CI selection continues to require all selected self-contained tests to pass.

## Phase two: file and history commands in progress

The first batch moves document file workflows and Undo/Redo/Purge into registered
commands. DocumentCommands owns inputs and progression; DocumentFiles owns native
preparation, duplicate choices and save reports; RuntimeOperation owns exclusive
native work. EditorApplication stores queued command IDs and the last 256 final
results. The requesting view can close without discarding an already completed
write or its cleanup. Executor failure after publication retains the output path
and written count while reporting failure.

The default file input view uses public queries and document.respond, with no
SDK or mutable-document access. An independently compiled module replaces that
same singleton overlay view. Native UI regressions cover replacement, failed
replacement retaining the previous view, and command menu/key configuration.
Five module UI cases and the complete Dev run passed: 95 passed, 15 original-sample
suites skipped. The final run is in out/macos-arm64/phase-two-final-tests.log.

That first batch left selection/object commands for the next batch below.
completed_phase remains one. Object/NMS and resource operations, runtime controls,
webpage/print output ownership and document replacement failure combinations
remain in phase two. Paired Release timing passed the unchanged
3% limits: median +0.151739%, candidate MAD 1.718957%, reference MAD 1.969528%.
The subsequent same-program control passed with a -1.355418% median shift and
MAD 1.247587%/0.873817%. The same device/runtime and frozen phase-one executable
were used; the reference was remeasured in the pair. Raw groups, source/runtime
hashes and unmeasured counters are retained in
baselines/file-operations-macos-arm64-metal.json. No SDK sources
were changed in this batch.

## Phase two: object and selection commands

The next batch registers basic object creation, automatic portals, batch Script
creation/deletion, scene/group removal and irreversible native object deletion.
Creation and reversible changes share the existing Edit/UndoHistory; native
deletion retains history clearing. The default toolbar, keyboard and Level
Manager actions call these commands. A standalone module replaces the frame
creator and forwards a batch Script command through the same public interface.

The shared ObjectSelection owns references, its derived ID list and lock.
Actions use SelectionCommands; document attachment restores it and replacement
resets it. Read-only selection projection works while native work is busy.
Delete options now belong to ObjectCommands; native parameter controls keep only
a draft. Delete prompts and Explorer cancellation retain object lifetimes.
Export and deletion no longer share the same target container.

Version-three public values add copied object/integer arrays. Every object is
checked again at dispatch. Completed command outputs, including arrays, can be
read through application.operation-output without the original view. Specialized
parameter creation and curve tools remain with their planned view migrations.
Full regression passed 95 CTests with 15 original-sample skips; six independent
module UI cases passed. Final Release comparison median changed +0.662999%,
with MAD 0.616144%/1.829307%; the same-program control changed +0.543865%,
with MAD 0.617954%/0.339433%. A refused-selection UI regression first failed and
then passed; the complete final run is in object-command-selection-final-tests.log.
Earlier reference-MAD rejection, pre-fix captures and all raw groups remain in
baselines/object-commands-macos-arm64-metal.json. This does not advance the
completed phase or change SDK source.

## Phase two: resource loading and object/NMS exports

The third batch registers resource.load, resource.drop, script.load,
object.export and script.export. The Data Resources list, Level Manager and
3D Layout submit the same commands used by an independently compiled module.
Multi-file drags and multi-object receivers are one owned request. The file
owner keeps paths, class IDs and object references through duplicate-name
questions, and checks receiver lifetimes before native work. Each load retains
its existing undo behavior; partial batch results are recorded per item.

Object and behavior exports use the existing file owner and replaceable input
view. File dialogs, the object options and Behavior Information are projections
and document.respond inputs. Their windows own no native tasks or callbacks to
continue writing. The field form is also reused by the resource description
editor. Resource-tree browsing and editing existing DSC files remain with their
planned view migration.

Version-four values add text arrays with copied UTF-8 strings. The C/C++ header
consumers still include no STL/SDK/SDL headers. Resource batches are limited to
4096 load items and 256 KiB of expanded path text; the common request/result
limits remain 1 MiB of text and 64 MiB of arrays. Completed operation outputs
include actual published paths and aligned per-item resource results, using an
empty object reference for an item that created no object. Closing the
requesting view does not discard accepted work. Executor failure after a write
still records the file.

NMS and an optional DSC are written and checked before publication. If NMS
publication fails, the previous DSC is restored, or a new DSC is removed. A
failed restoration reports the remaining output and leaves a recovery file.
Cleanup diagnostics remain in the completed operation, including after its
requesting view has closed.
This is ordered file replacement with recovery, not a filesystem-wide atomic
operation. Recovery-failure injection remains explicit in F05. SDK source and
GPU rendering/transfer code are unchanged. Runtime controls, webpage/print
ownership and remaining document replacement combinations keep phase two open;
completed_phase stays one.

The final complete Dev run passed 95 CTests, with 15 original-sample suites
skipped. Seven independent-module UI cases passed; 154 unselected registry cases
are not counted as passes. The batch-result alignment and cleanup-diagnostic
regressions were reproduced before their fixes, then passed in the complete run.

Final paired Release timing: median 0.247288 ms, p10 0.246143 ms,
p90 0.249982 ms, MAD 0.277527%; median change -0.202904% from the remeasured
reference. The same-program control changed +2.274744%, with MAD
2.444251%/2.111606%. Both meet the original 3% limits. Stable readbacks, uploads,
target creations/releases and path selections were zero. The explicit
verification readback remained outside the sample groups. Remaining counters
are listed as unmeasured. Final and earlier captures, source/runtime hashes and
limits are in baselines/resource-files-macos-arm64-metal.json. SDK patches and
runtime hashes also match the accepted previous capture. The complete run log
is out/macos-arm64/resource-command-complete-tests.log.

## Phase two: runtime controls

ExecutionCommands owns the Debugger and its controls. Play, Pause, the
Play/Pause action, Reset, frame/debug stepping, Leave Behavior, Script
execution, Trace, debug options, locks and CK time settings are registered
commands. Status-bar buttons, application/Schematic shortcuts, fullscreen
input, startup playback and the debug panel dispatch those commands. The
Play/Pause action also resolves its Play/Pause providers through the registry.
Fullscreen input reads the configured shortcuts rather than another fixed map.

The command host applies the same document, thread, open-edit and native-worker
checks. runtime.state, runtime.time and runtime.log provide copied data; they
remain readable while a file worker owns the context. Debugger advancement
belongs to the application and uses the existing execution/time-budget behavior.
This batch does not add process isolation or forced native interruption.

Version five adds finite real values to the ordinary C descriptors. Numeric
settings are validated together before native writes. The private C++ owner
uses the existing Debugger; existing breakpoint views retain only a const
inspection pointer until their view migration. UI code cannot directly change
its runtime fields or invoke its native mutators. Locked targets, current debug
objects and log targets carry native lifetimes. Reset retains its existing
native reset, topology refresh and Dirty behavior, without adding an undo step.

Regression covers provider substitution, copied real values, malformed numbers,
Script batch validation, native exceptions, the existing full-frame budget,
read-only busy state, queued scope expiry and same-ID replacement. The native
UI case exercises the actual status buttons, debug checkbox, rebound Trace
shortcut and Time Settings form. Remaining breakpoint editing, complex debug
view queries and reliable termination of a stuck plugin remain explicit.

The complete Release build and Dev run passed: 95 tests passed, 15 original-sample
suites skipped, zero failures. Eight independent-module UI cases executed;
154 unselected cases are not counted as passes. The full run log is
out/macos-arm64/execution-complete-tests.log.

The 11-group paired Release comparison measured median 0.280926 ms,
p10 0.278432 ms, p90 0.285124 ms and MAD 0.553533%; median change was +0.687868%.
The final same-program control changed +0.255625%, with MAD
1.016297%/0.901419%. These runs meet the unchanged 3% limits for this scene.
Two earlier controls exceeded the variation limit; a 21-group extension exceeded
the native test's existing 60-second watchdog and is invalid. All captures and
failure logs remain recorded; the host was not uniformly quiet. Neither source,
harness nor thresholds changed between these runs.

Stable readbacks, uploads, target creations/releases and path selections were
zero; the one explicit verification readback was outside the sample groups.
Other counters remain unmeasured. Final source/binary/harness hashes and SDK
patch/runtime matches against the previous accepted capture are recorded in
baselines/execution-commands-macos-arm64-metal.json. This batch makes no SDK
source change. Phase two remains in progress.

## Phase two: web, print and replacement failures

OutputCommands owns web/print work, pending input IDs, retained options and final
outputs. Built-in web/print dialogs are ordinary replaceable overlay views;
independent modules use document.export-web, document.print, output.respond and
named output.print-setting fields through the same registry. No window keeps a
file-write continuation or an active print job. Menu placement and command
providers remain configurable.

OutputFiles stages declared files beside their actual destinations, preserves
previous contents before multi-file publication and restores in reverse order
when a later replacement fails. A single file keeps the existing atomic rename
path, without a redundant backup. Web composition/HTML, NMS/optional DSC and the
regression image pages reuse it. A failed restoration reports residual paths and
retains its backup; fault injection of that restoration failure remains a later
check. Web preview only opens after all outputs and native completion succeed.
Existing composition/player modes, dialog options, overwrite confirmation and
composition-save error messages remain. Production still excludes PNG print
drivers and regression options.

Print preparation captures independent Script models, display options and
cloned bitmaps in SchematicPrintContent. Page rendering holds neither the source
canvas nor the document. Existing page order, 2048-pixel tiling, physical margins,
printer setup/cancellation and submission identifiers remain. Native printer
submission is tested through a failing adapter; physical printers and the other
platforms still require their native checks.

New and Open prepare a candidate before detaching views. Native layout/link and
session preparation happens there. Execution ends only inside the admitted
commit, and old queued work expires only after an actual switch. Failed detach,
later admission or dispatch restores the original views and preserves document,
selection versions, save state, history and playback. A committed document stays
loaded when new-view attachment fails; diagnostics record both completion and
view failures. Failed layout capture finishes its operation without a write.
Runtime log queries read a copied projection while the worker can end execution;
append, trim and clear are covered without recopying all old entries per append.

Asynchronous ownership survives an outer provider throwing after a nested call
accepted work. The final operation retains actual output paths and both native
and callback errors. An unstarted command still expires with its view.

Final functional and performance records, including earlier failures, are in
baselines/phase-two-macos-arm64-metal.json. The final functional run has
95 passes and 15 missing-original-sample skips (44.64 seconds); ten module UI
cases and the standalone existing web UI case passed. Unselected cases are not
counted as passes. Production isolation, file-byte regressions and SDK object
behavior differential also passed. This batch makes no SDK source change.

The accepted 11-group paired Release capture measured median 0.247578 ms,
p10 0.244943 ms, p90 0.250803 ms and MAD 0.772728%; the reference median was
0.245305 ms and MAD 0.475474%. The median change was +0.926460%. The following
same-program control measured median 0.255154 ms, p10 0.248991 ms,
p90 0.269153 ms and MAD 1.815119%; the other side measured 0.251356 ms with
MAD 0.901400%, a +1.511027% difference. Both captures meet the unchanged 3%
limits. Stable readbacks, uploads, target creations/releases and path selections
were zero; one explicit verification readback stayed outside the groups. Other
counters remain unmeasured.

Two earlier old/new captures exceeded the variation limit on both sides. Their
full samples, system-load observations, the earlier passing control and the final
captures are retained. After an idle interval, the comparison and its following
control passed without source, harness or threshold changes. Source/binary/hash
checks match the final files, and SDK patches/runtime hashes match the preceding
accepted capture. This records this scene rather than uniform host stability.
completed_phase is now two; later-stage checks remain in coverage.json.

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

Version-two measurements also record sample-group control and CPU activity.
On macOS, the driver explicitly sets SDL's foreground-on-launch option, and
the test holds a user-initiated `NSProcessInfo` activity for the measurement.
That activity ends on normal completion and on test failure. Neither setting
changes production scheduling or adds per-frame work. The hidden test window
and the existing offscreen rendering path remain unchanged.

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

When scheduling varies between launches, compare two binaries by alternating
their sample groups. Each program completes its own warmup first and waits
with the GPU idle between groups. The first renderer alternates as well.
Only one program renders at a time; all waiting is outside the timed groups.
The driver stops its own child processes on failure or timeout.

```sh
python3 tools/MeasureViewport.py --build out/macos-arm64 \
  --binary out/macos-arm64/runtime/NeMoDev.app/Contents/MacOS/VirtoolsDevUiTests \
  --reference-build out/identity-before/build \
  --reference-binary out/identity-before/build/runtime/NeMoDev.app/Contents/MacOS/VirtoolsDevUiTests \
  --output out/macos-arm64/architecture/viewport-comparison.json --sdk-change
```

Use the current binary and build as both inputs for a same-program control.
Both binaries must use the identical measurement sources. A changed harness
requires rebuilding the reference; historical captures remain diagnostic
records and are not compared across protocols. A passing median/MAD does not
mean every sample had the same throughput: p10, p90 and all raw groups remain
in the report.

Reports retain all raw groups, median, linearly interpolated p10/p90, median
absolute deviation (MAD) and MAD/median. MAD/median above 3% is an unstable run.
`--compare REFERENCE.json` requires matching scene, dimensions, timing protocol,
GPU/driver, OS/hardware, compiler flags, SDL, SDK revisions and harness. A changed
SDK working tree or an unstable reference is rejected. A median regression over
3% fails. A comparison of the same binary is a control: a median shift of more
than 3% in either direction fails, rather than being counted as an improvement.
Binary hashes and the source revision identify each measurement;
different editor binaries are expected when comparing architecture changes.

For an intentionally authorized SDK change, add `--sdk-change`. That explicit
mode permits SDK revision/patch differences while retaining all other matching
conditions and the same 3% gates. Reports include SDK patch, untracked-source
and loaded CK2/VxMath/SDL/rasterizer binary hashes. A same-binary control also compares these
runtime hashes, so replacing a library cannot be mistaken for a control run.

The machine-specific reference in `baselines/` records results for its
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
acceptance remain explicit limitations. The stage-zero production render path
and SDK sources were unchanged. Later identity-refactor results are recorded
separately; it does not replace this historical reference or turn unavailable
original samples into passing tests.

`baselines/object-identity-macos-arm64-metal.json` records the subsequent SDK
identity refactor separately: old/new behavior and file checks, the 108-test Dev
run (93 passed, 15 unavailable-sample skips), SDK failures and old-CK2 controls,
all five timing captures and their calibration. The accepted pre-change median
is 0.250328 ms; the candidate is 0.238916 ms with p10/p90 0.232024/0.245778 ms
and MAD 0.004647 ms (1.945%). Its identical-binary control is 0.237766 ms,
a -0.481% shift. Both pass the 3% gates; no speedup is attributed to this change.
Stable readbacks, uploads, target creations, path selections and releases remain
zero. The SDK's full run still has 33 failures: 32 math/serialization targets
also fail with the old CK2 and the same subcase signatures, and StageLayout
rejects this machine's Homebrew SDL install name. These remain failures, while
all new identity and frozen differential tests pass.


## Object name editing and editor modules

`editor/Editor.h` is the shared registration and invocation interface for built-in
and separately compiled modules; `editor/ImGuiDrawing.h` provides the optional
ImGui drawing adapter. `application/EditorApplication` owns selected commands,
queries, property drafts, queued writes and view subscriptions. It uses the
existing `Document` and `Edit` for one undo step and rollback of nested commands.
`features/objects/ObjectCommands` is the native object reader/editor;
`ObjectViews` and `workspace` compile without SDK include directories.

Level Manager dialogs, Hierarchy inline input, Setup dialogs and registered
property views submit the same object-name draft. Configuration selects the
text editor and command implementation, rebinds the property shortcut and moves
its menu entry. Closing a view cancels its drafts and queued commands. Object
lifetime and document checks reject old callbacks and reused IDs.

`NeMoDevEditor` covers registration failure, drawing layout mismatch, query
fields, invalid configuration, nested undo/rollback, no-change redo, actual
module loading, failed duplicate loading, callback lifetime and reverse module
stop order. `VirtoolsDevUi.modules` adds three sample-free cases for the existing
rename widgets, property views, readonly and external views, configuration and
silent native ID reuse. The C/C++ public-header consumers include the complete
module interface without SDK/STL/SDL/ImGui dependencies.

The final Release run has 95 passing CTests and 15 missing-sample skips. It used
`SDL_AUDIO_DRIVER=dummy`: the first run waited in CoreAudio device initialization
and failed three sound-property changes. The same sound case and the full suite
passed with dummy audio. Real Metal rendering was retained.

`baselines/object-editor-macos-arm64-metal.json` retains all timing captures,
including rejected runs. The final unprofiled result is median 0.298538 ms,
p10/p90 0.209834/1.105763 ms and MAD 0.091522 ms (30.657%). It does not pass the
3% limits. The unchanged old program also varied substantially across controls;
a profiled candidate pass is not used for acceptance. Stable GPU readbacks,
uploads, target creations, path selections and releases remained zero.
F16 keeps the timing item open and the phase counter remains zero. The runtime
configuration is currently in memory; production module discovery, packaging,
profile persistence and state migration remain assigned to phase six.

## 阶段三第一批：工作区视图、Help 与 Event Log

本批仍为 `completed_phase=2`。Help 与 Event Log 通过相同的视图登记、命令、查询和 ImGui 适配运行；宿主的专用实例、显示 bool、绘制分支和帮助中央事件已删除。未迁移的 Level/Hierarchy/Attribute、资源浏览和复杂视图仍列在原覆盖清单中。

公开接口版本为七，仍可由独立 C/C++ 消费者编译。`EditorViewDesc` 声明绘制作用域、带参数的导航，以及可选的带版本状态捕获/恢复。`EditorViewFrame` 借用工作区提供的窗口名、焦点请求和待恢复的布局；视图通过 ImGui 适配绘制内容与样式。实例编号用来导航和聚焦，标题只负责显示。

- `workspace.open-view(type)` 创建视图，或定位其已声明的单实例。
- `workspace.focus-view(view)` 和 `workspace.close-view(view)` 在省略编号时作用于请求视图。
- `workspace.views` 返回实例、类型、提供者、显示标题及当前焦点；`workspace.view(view)` 返回单实例的显示窗口名和焦点。
- `help.open(topic?, view?)` 与菜单、键位使用相同导航；Help 保持单实例及原有关闭重开历史。`topics/positions/visited/current/pending/query/visit/zoom` 是第一版 Help 状态。
- `log.open` 与 `log.clear` 操作应用日志；`log.summary` 提供版本、数量、错误版本和状态文字。
- `log.entries(first?, offset?, revision?)` 在 UTF-8 边界分段返回最多 256 KiB 的文字。`next/offset` 是后续的行、字节位置；同一序号的片段连接为一个条目。续读固定版本，收到 Stale 后重新读取，避免混用淘汰或清除前后的内容。

创建和状态回调不能偷偷提交编辑或命令。工作区先准备所有待替换的实例，捕获并恢复同版状态，全部成功才替换；错误保留旧实例、导航、配置和焦点。已关闭单实例的布局和状态由工作区保存。跨启动持久化仍在阶段六。

新增回归覆盖单实例与多实例、错误线程与参数、失效编号、导航失败/抛异常、创建期间禁止修改、状态版本、状态中禁止原生对象引用、状态捕获与恢复失败、后续实例失败后的整体恢复，以及关闭重开。日志覆盖重复、淘汰、worker 入队/合并/清除、只读投影和超过 1 MiB 的 UTF-8 消息。

原生模块回归使用公开头编译的替代日志视图，验证提供者替换失败时保留旧视图，成功时保留位置和尺寸；同时验证多个对象视图、焦点、文档、选区与撤销状态。自包含帮助样本通过实际 HelpBook/HTML/ImGui 路径验证阅读位置、关闭重开、重绑键位、搜索输入，以及 worker 活跃时的 Help/日志交互。

本机未安装可选的原版帮助包，原版 Help 全书图形回归未执行。初次手动选择该组的失败日志保留在 `out/macos-arm64/phase-three-help-initial.log`；这不属于自包含样本的通过结果。初次含竖线的筛选没有匹配测试，原始结果同样保留；正式测试使用整个 modules 组。

日志摘要按 `EventLog::Revision` 缓存，与文档版本分开。`EDITOR_QUERY_VERSIONED` 使用提供者的只读版本回调，版本不变时复用已发布投影；内置 C++ 视图通过 `ReadQuery` 借用同一结果，公开 C 消费者仍通过原有 sink 读取。版本回调禁止提交修改，异常不使用旧值冒充新值；关闭视图立即清除该作用域的缓存。版本变化、投影复制、失败恢复和错误线程均有回归。

最终 Release 完整回归 **95 项通过，15 项缺样本跳过，零失败**，用时 42.80 秒；模块界面登记 166 项，实际执行 12 项，154 项未选择，零失败。架构检查覆盖 838 个文件，原有暴露仍为 63 项、后续缺口 29 项，阶段编号仍为二。

| 最终测量 | median (ms) | p10 (ms) | p90 (ms) | MAD (%) | 中位数变化 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 当前与冻结参考配对 | 0.268384 | 0.258997 | 0.334185 | 2.193863 | +0.294986% |
| 冻结参考 | 0.267594 | 0.259189 | 0.365430 | 2.658508 | — |
| 随后的同程序对照 A | 0.260838 | 0.254936 | 0.266563 | 1.304128 | +1.520299% |
| 随后的同程序对照 B | 0.256932 | 0.251782 | 0.270020 | 2.004644 | — |

相同 Release 场景和窗口/视口尺寸，每次至少 15 秒预热、11 组交替样本，每组至少 1 秒和 240 帧。稳定帧读回、上传、目标创建/释放和路径选择计数均为零；验证读回在组外仅一次。GPU 时间、应用分配、preserve copy、额外 pass/submit、能力查询和 retain/release 仍未测，不能将它们列为零。

早期对照中，参考 MAD 4.3488% 和配对中位数 +3.6410% 均未接受。源码检查发现状态栏每帧重建摘要，随后改为按提供者版本复用。最终实现的首次配对及相邻同程序对照均通过；仅凭早期时间差不能把所有波动归因于摘要查询。只读负载检查见到 watchpoint 99.1% CPU、WindowServer 31.2% CPU，系统没有记录热/性能警告；没有修改或停止其他进程。所有先前结果保留在原始目录。

最终完整结果和全部采样：[workspace-views-macos-arm64-metal.json](baselines/workspace-views-macos-arm64-metal.json)。最终程序、源码、测量程序的校验值在两次最终采样及当前文件中一致；SDK 修订、补丁和运行库校验值与阶段二一致。测量中的 `--sdk-change` 仅允许既有已批准的 SDK 身份改动，本批 SDK 未改动。

## Phase three: Level, Hierarchy and Attribute views

These built-ins are registered workspace instances. Level/Hierarchy use the same
pointer-free tree drawing, while Attribute manager/chooser share their catalog
projection. Each instance owns its rows, filters, expansion and inputs. Roles,
focus, pinned primary windows, default docking, instance inspection and picking
belong to EditorWorkspace. The old ManagerWindows, LevelManager and
HierarchyManager implementations are removed. Clipboard definitions now live in
core; the editing session retains clipboard ownership.

SceneCommands publishes versioned, paged scene/catalog data and owns document
Worksets. Changes reuse existing core commands and UndoHistory. Public C values
carry complete object identities and attribute catalog versions. Independent
modules can inspect the actual view selection and submit the same commands.
Parameter values and copy/delete options reuse the registered native ParameterBox
overlay. Failed input preparation preserves existing input; scope closure cancels
pending changes. Picking supports the parameter editor's modal target selection.
Pure views may use their cached projections during native work, while native
commands and fresh document queries remain subject to runtime admission.

Adopting a prepared document now establishes the receiving history save point;
undo of subsequent changes returns loaded/new compositions to clean state.
Imported or modified candidates remain modified. Original-sample validation also
repairs fixtures that created native objects without publishing topology changes,
tests missing editor-thread callbacks, asynchronous print completion/output paths
and clipped sound controls. The local sample link is ignored and redistributes
no original files.

The final Release build passes all 124 CTests without skips, including the three
original-sample manager suites (7 Level, 8 Hierarchy, 6 Attribute cases). Instance
independence, closed scopes, catalog index reuse, native-work exclusion and an
independent module are exercised. Public C/C++ header consumers and indirect
native-dependency rejection also pass. Isolated direct opening of the local
original Intro.nmo shows populated Level contents, scripts and default docking.

The following figures use 11 alternating paired groups, at least 15 seconds of
warmup per program and at least one second/240 frames per group. The frozen
reference uses the same SDK runtime and timing harness. Units are milliseconds
per GPU-drained whole editor frame, not GPU-only duration.

| Run | Median | p10 | p90 | MAD / median |
| --- | ---: | ---: | ---: | ---: |
| Frozen reference, remeasured | 0.254127 | 0.252343 | 0.257412 | 0.7021% |
| Scene views | 0.227060 | 0.222801 | 0.229328 | 0.9991% |
| Following same-program reference | 0.212893 | 0.211097 | 0.220034 | 0.7224% |
| Following same-program candidate | 0.211812 | 0.209119 | 0.223789 | 0.7175% |

The paired median improves 10.65%; the following same-program difference is
-0.51%. Both pass the original 3% limits. The control's faster absolute time is
not counted as another improvement. Stable readbacks, uploads, target creations,
releases and path selections remain zero; explicit verification reads back once
outside timed groups. Application allocations, GPU-only timestamps, preserve
copies, extra passes/submissions, native capability queries and resource
retain/release were not measured and are not inferred from frame timing.

Raw samples, runtime/source fingerprints and earlier diagnostic runs are recorded
in baselines/scene-views-macos-arm64-metal.json. SDK revisions, patch hashes and
runtime binaries equal the previous batch; no SDK sources or viewport transfer
implementation changed here. Resource browsers and default contribution
configuration still remain in phase three, so completed_phase stays 2.

## Phase three: Building Blocks browser
Building Blocks now uses the registered workspace view, navigation and inspection
paths. Each instance owns its filter, category, selection, expansion and sorting.
The catalog query returns bounded versioned UTF-8 pages; drawing reads published
metadata without accessing CKContext. Filtering and sorting run when their input
changes. The original EditorPanels implementation and global draggedBlocks state
are removed.

ImGui holds the complete ordered drag and source prototype. Schematic, 3D Layout
and scene-tree receivers validate the same data. Replacement keeps the dragged
prototype, while insertion retains catalog order. Help uses help.open and consumes
the configured chord, including rebinding. Workspace reset docks registered views
by type and checks floating windows before removing dock nodes. Final focus follows
the last request rather than instance-number drawing order; replacement and closed
instances also update pending focus.

The final Release build passes all 124 CTests without skips. This includes 15 module
UI cases, 6 Building Blocks cases and the existing original-sample manager, Layout,
Setup and file regressions. Native query checks compare all projected catalog
fields to the native catalog and cover paging, refresh, wrong-thread access and
malformed drags. Independent modules exercise actual browser instances, invalid
navigation preserving state, scoped closure, background browsing and remapped help.
Public C/C++ header consumers and indirect resource-view dependency checks pass.
Isolated direct Intro.nmo opening retains populated Level contents, groups and
scripts; a generated Data Resource opened at startup still docks with the browser.

The figures below follow the same frozen Release scene and 11 paired-group
procedure used above. Units are milliseconds per GPU-drained whole editor frame.
They compare the accumulated implementation with the frozen phase-one program;
they do not isolate a speedup from the browser migration alone.

| Run | Median | p10 | p90 | MAD / median |
| --- | ---: | ---: | ---: | ---: |
| Frozen reference, remeasured | 0.250955 | 0.249354 | 0.254204 | 0.4877% |
| Building Blocks views | 0.218007 | 0.215388 | 0.221008 | 0.7430% |
| Following same-program reference | 0.217971 | 0.216624 | 0.222313 | 0.6178% |
| Following same-program candidate | 0.219319 | 0.216823 | 0.222203 | 1.0211% |

The paired median changes -13.13%; the following same-program difference is
+0.62%. Both pass the original 3% limits. Stable readback, upload, target
creation/release and path-selection counters remain zero; verification reads back
once outside timing. Other GPU/allocator counters remain unmeasured. SDK revisions,
patch hashes and runtime hashes equal the preceding batch. The earlier browser
measurements are retained but do not describe the final focus-order correction.

One full run crashed during SDL/macOS window destruction at program shutdown. A
targeted rerun, five consecutive repetitions and the final full run passed. The
cause is not established; the bounded crash stack and failed run remain recorded.
SDK code was not changed. The original complete help archive is unavailable locally;
the self-contained help fixture covers this browser and remapped help commands.

Full samples, checks and diagnostics:
[building-block-views-macos-arm64-metal.json](baselines/building-block-views-macos-arm64-metal.json).
Data Resources and default contribution configuration remain in phase three, so
completed_phase stays 2.


## Phase three: Data Resources and default workspace configuration

Data Resources uses registered resource.data instances. DataResourceCommands owns
library contents, copied directory/file descriptions, file inputs and the list of
open libraries. Each view owns its folder, selection, anchor and expanded nodes.
Closing one view retains a shared library; closing its final view collects the
catalog and updates the existing preferences list. Catalogs and views survive
composition replacement. The old ToolWindows resource collection, drawing branch,
file-dialog callbacks and EditorState draggedResources list are removed.

resource.open-library, resource.create-library, resource.refresh-library,
resource.browse and resource.behavior-information use the same command registration
as independent modules. resource.library, resource.folders and resource.files
publish versioned data in pages of at most 64 rows; drawing does not access CKContext
or inspect the filesystem. Loading and importing continue through the existing
resource.load and document import commands. Loading uses the primary Level view's
actual selection, preserving Script and other non-rendering targets.

The resource.file-dialogs overlay uses the same file picker and behavior form as
document inputs. The application owns continuations and completion results. Cancel,
closed origins, malformed responses and background work retain the required state;
accepted inputs wait until native work is available. File operations share the
existing admission rule, preventing simultaneous resource and composition forms.
Description writes reuse OutputFiles and the existing DSC format. Failure reports
retain the destination path. Failed directory refresh, including recursive folder
links, keeps the previous catalog. Multi-output RSC creation recovery and large
folder scanning outside the UI thread remain explicit F20 work.

Resource drags contain their source row and complete file list inside ImGui-owned
bytes. Public C headers describe length-prefixed UTF-8 paths rather than a fixed
path buffer. Schematic, Level, Layout and bitmap Setup decode the same bounded
payload; malformed payloads leave previous outputs intact. No resource-drag query
or parallel EditorState list remains.

DefaultWorkspaceConfiguration declares provider choices, primary roles, startup
views, split regions, anchors and reset behavior. Defaults and custom layouts use
the same EditorWorkspace configuration path. Validation and instance preparation
precede publication; invalid splits, roles, regions, repeated legacy names or
creation failure keep the previous views and layout. Full layout changes are refused while
operations/dialogs are active; provider-only replacement retains its existing
path. Runtime changes apply at the next layout update, without a separate Reset.
The workspace also owns first-use layout checks. Default seed order preserves the
existing primary window identities; startup retains an existing dock layout.
Only the remaining Schematic/Layout/Setup windows use the named
legacy placements listed in this configuration.

The old DefaultObjectConfiguration helper and per-type default docking map are
removed. Registered menu queries retain Manager check marks. Public interface
version nine adds that optional query name and resource drag descriptions, with
no STL, SDK or SDL dependency. Query and command inputs are located by key; caller
order is preserved. Pending window focus is deferred while a modal is open, so a
module opening a library and its description in the same frame cannot dismiss it.

Regressions exercise real window positions, applying/restoring a custom layout,
multiple independent resource views, provider replacement and failed replacement,
closed tree restoration, multi-file drags, Script loading targets, directory
paging/failure, malformed and long drag paths, DSC replacement, input replacement,
closed origins and browsing during native work. The independent module includes
an alternate resource view and resource input implementation. Architecture checks
reject indirect native access from both resource views and default configuration.

The final macOS arm64 Release run passes all 124 CTests with no skips, including
20 module UI cases and the existing original-sample manager, Setup, Layout and
file suites. Direct Intro.nmo capture retains populated Level contents and scripts;
the generated resource tab shows its folder, file and description in the default
dock. SDK revisions, patch hashes and runtime hashes equal the preceding batch.
No SDK source or viewport render/transfer implementation changed.

| Run | Median (ms) | p10 (ms) | p90 (ms) | MAD (ms) | MAD / median |
| --- | ---: | ---: | ---: | ---: | ---: |
| Frozen reference | 0.246939 | 0.243728 | 0.251458 | 0.002264 | 0.9168% |
| Resource views and workspace configuration | 0.209801 | 0.205999 | 0.211575 | 0.001530 | 0.7293% |
| Following same-program reference | 0.206894 | 0.204996 | 0.225065 | 0.002258 | 1.0913% |
| Following same-program candidate | 0.206318 | 0.203865 | 0.215840 | 0.002275 | 1.1027% |

Both pairs pass the unchanged 3% limits. The cumulative comparison with the frozen
phase-one program changes the median by -15.0396%; the subsequent same-program
control changes it by -0.2782%. Neither number isolates a resource-browser speedup.
Each pair has 11 alternating groups, at least 15 seconds of warmup per program
and at least one second/240 frames per group. Stable readback, upload, target
creation/release and path-selection counts are zero; explicit verification reads
back once outside timed groups. Other GPU/allocator counters remain unmeasured.
Earlier implementation measurements and corrected UI failures are retained in
[resource-views-macos-arm64-metal.json](baselines/resource-views-macos-arm64-metal.json).
The manifest now records completed_phase 3, 43 source groups, 40 state families
and 55 remaining legacy exposures. Later-stage work stays assigned to its existing
feature entries; this does not mark the complex editor migrations complete.

## Phase four: names and basic parameter values

Names, booleans, integers, real numbers, enumerations and object references use
typed property values. A property joins a query field, a registered value editor
and a registered modification command. EditorApplication owns inline drafts and
copied metadata; renderers receive values and an update sink. Native parameter
dialogs retain their existing owned working parameters and submit through the
same selected commands. Public interface version ten replaces the text-only
editor description with EditorValueEditorDesc. Its C/C++ consumers include no
STL, SDK, SDL or ImGui headers.

ParameterValues owns basic native type resolution, validation, conversion and
the undo data record. ParameterCommands publishes read-only descriptions and
object choices, and registers modifications. ValueEditors renders only copied
data. Object options are queried when the chooser opens. An open dialog retains
its selected editor, command and choice provider even if the workspace selects a
different provider. The Views menu selects built-in or independently compiled
editors through the same configuration path.

Inline editing starts a draft on the first change and submits once on acceptance
or deactivation. Escape, owner closure and document replacement discard the
draft. Dialog Apply/OK groups all rows into one Edit; a refused row rolls back
the whole batch. No-change acceptance preserves Redo. Schematic renames and
parameter names use the existing object.name property. Its combined name, type,
link and value changes are one edit. Removing an incompatible shared input link
keeps the existing disconnected-input behavior and leaves its former shared
value untouched.

Regressions cover repeated updates, cancellation, invalid and sparse enum
values, non-finite and out-of-range numbers, percentage units, stale parameter
types, same-ID object reuse, busy native work, batch rollback, and more than 256
successive native interactions through a delegating command. Four native UI
cases exercise the real dialogs, an independent integer editor and modifier,
provider changes during an open dialog, object choices and single-step undo.
Complex values, nested drafts and the remaining Setup/Schematic/Layout view
migrations retain their assigned later work. completed_phase stays 3.

The final macOS arm64 Release run passes all 124 CTests without skips. The module
suite executes 24 of its 178 registered cases, including the four new cases;
unselected cases are not counted as passes. Existing Setup, Schematic, debugger,
Layout, original-sample file and direct-NMO regressions also pass. This task did
not edit SDK source or the viewport rendering/transfer implementation. Concurrent
SDK work changed the build inputs, so the phase-three reference was rebuilt; all
22 measured runtime files, revisions and patch hashes match between the pair.

Performance acceptance remains open. Both the first 11-group pair and the
following same-program control exceeded the original 3% MAD limit. Observed
concurrent CPU load does not establish a cause. An extended 10,000-frame attempt
exceeded the unchanged native test watchdog. No thresholds or benchmark sources
were changed, and no rejected run is treated as an accepted baseline.

| Run | Median (ms) | p10 (ms) | p90 (ms) | MAD (ms) | MAD / median |
| --- | ---: | ---: | ---: | ---: | ---: |
| Phase-three reference, remeasured | 0.257945 | 0.247368 | 0.276928 | 0.010577 | 4.1003% |
| Basic value editing | 0.253010 | 0.241428 | 0.273801 | 0.011583 | 4.5780% |
| Same-program reference | 0.298052 | 0.283378 | 0.321199 | 0.012446 | 4.1759% |
| Same-program candidate | 0.294294 | 0.289457 | 0.314709 | 0.003703 | 1.2581% |

Complete captures retain zero stable readback, upload, target creation/release
and path-selection counts, and one explicit verification readback outside
timing. Other GPU/allocator counters remain unmeasured. One earlier module run
crashed in native render-context cleanup during document replacement. Subsequent
targeted/full runs and three reference module repetitions passed; the cause is
unresolved and is not claimed fixed. Raw measurements, failed iterations, the
bounded native stack, fingerprints and remaining work are preserved in
[value-editing-macos-arm64-metal.json](baselines/value-editing-macos-arm64-metal.json).


## Phase four: compound numeric values

The same ParameterValue description and undo record now cover 2D/3D vectors,
quaternions, Euler angles, matrices, colors, bounding boxes and rectangles, as
well as the existing scalar values and angle controls. Public interface version
11 adds finite real arrays. Native values retain their component order: RGBA,
XYZ/W, row-major matrices, Min/Max boxes and corner-coordinate rectangles.
Rows, columns, component/row labels and display scale are copied query fields;
no native memory layout or SDK type reaches the public editor description.

ParameterBox no longer draws or writes these components through its own color,
vector, matrix and angle branches. It uses the same registered renderers as
inline values. Native byte conversion stays in ParameterValues; invalid counts,
non-finite components and values outside float range are refused together.
Ordinary derived parameter types inherit their nearest parent editor while
keeping their actual native GUID. Turn/degree controls, RGBA byte channels,
matrix collapse, rectangle representations and native object picking remain
available. Each rectangle row owns its display representation.

An inline interaction updates its owned draft and commits once. Native dialogs
retain working parameters and one pre-interaction value per row. Cancel restores
that value; accepting an input ends that interaction, and Apply groups all rows
into one history step. The color renderer owns only popup opening/closing state;
restoration belongs to the draft or working-copy owner. Child input activity
prevents the parent dialog from accepting while its picker is active. Cancelled
input consumes Escape for that frame even if ImGui has ended its active widget,
so the same key cannot also close the containing dialog. Query,
renderer, modifier and option-provider choices are captured when opening a native
dialog, so changing configuration cannot mix providers in an existing edit.

Five additional native UI cases cover an eight-type batch, invalid input,
color-picker cancellation (including Escape), a multi-frame inline drag,
Redo preservation, an independently compiled vector editor/query/modifier, and
angle-dial cancellation while the mouse remains held and continues moving.
The provider test changes configuration during an open dialog and verifies both
continued queries and the original renderer/modifier. Core tests exercise forty
updates for each compound shape, cancel/undo, malformed arrays, finite/range
checks, working copies, inherited type identity and copied command outputs.
C/C++ header consumers still include no STL, SDK, SDL or ImGui dependency.

The final macOS arm64 Release run passes 124/124 CTests without skips, including
the expanded compound-dialog case and the held-mouse cancellation regression.
The module group executes 29 of 183 registered UI cases; unselected cases are
not counted as passes. SDK
source state equals the start of this batch. The reference is the preceding
phase-four implementation at efe593c; source hashes and all 22 SDK runtime hashes
were checked. The viewport rendering/transfer implementation is unchanged.

Timing acceptance remains pending. The final eleven-group implementation pair
and its subsequent same-program control exceed the original 3% MAD limit. An
earlier source version passed its implementation pair (median +0.812768%), but
both of its controls were unstable. Those captures remain in the record and do
not validate the final source. Concurrent native CPU work was observed during
earlier sampling; that does not establish a cause. No benchmark source or limits
were changed, and all complete captures are retained.

| Run | Median (ms) | p10 (ms) | p90 (ms) | MAD (ms) | MAD / median |
| --- | ---: | ---: | ---: | ---: | ---: |
| Final implementation pair, reference | 0.261137 | 0.248244 | 0.285039 | 0.012590 | 4.8212% |
| Final implementation pair, candidate | 0.257971 | 0.241328 | 0.276659 | 0.015778 | 6.1162% |
| Final same-program control, reference | 0.313287 | 0.226684 | 0.405114 | 0.069908 | 22.3144% |
| Final same-program control, candidate | 0.287143 | 0.225574 | 0.379181 | 0.061569 | 21.4418% |

Complete captures report zero stable readback, upload, target creation/release
and path-selection counts; one explicit verification readback is outside timing.
Other GPU/allocator counters remain unmeasured. Sources, binary/runtime hashes,
full samples, corrected UI failures and remaining work are in
[compound-values-macos-arm64-metal.json](baselines/compound-values-macos-arm64-metal.json).
Structures/arrays, nested drafts and the remaining view migrations keep phase
four open; completed_phase remains 3. The preceding native teardown crash did
not recur here, and is not claimed fixed.

## Phase four: structures, object arrays and nested values

ParameterValue now describes structures, text and object-reference arrays,
including nested combinations with numeric values. Public C interface version
12 adds copied named records and child-editor composition. A child updates the
parent draft through its sink; it does not start another document edit. Record
copying checks nesting, member counts, aggregate sizes, unique keys and finite
components before changing the receiving value. Native structure queries include
member type GUIDs and full object references. Thirty native levels, including
their query metadata, pass through the editing path and undo as one value.
An empty record at the 64-level public limit is accepted; a 65th level is refused.

Inline editors retain one parent draft. Native dialogs retain their working
parameters and original layouts, including array selection, copy/paste,
ordering, deletion, object choosing and setup. Each supported subtree owns one
logical value; member rows access it by path. Cancelling a child restores its
interaction's initial value while retaining earlier accepted child edits.
Apply commits the whole dialog once. Query, modifier and child-editor choices
stay fixed during an open interaction. An independently compiled module tests
replacement of the structure renderer/query/modifier and composition with a
separately selected vector renderer, including reconfiguration while open.
Standalone inline arrays also test reordering, removal, undo and cancelled
selection. Their clipped list ends before drawing Add Object.

Core setters preserve member identities and validate nested references before
writing. A late setter rejection or exception restores earlier member writes;
the same compensation protects native working copies. Undo records retain
before/after values, member identities and referenced objects. Opaque native
values keep their copier and saved state through the registered
parameter.copy-value command, in the dialog's existing edit. A structure
containing a curve tests no-change, undo/redo and member identity preservation.
The older private value/action types in ValueCommands have distinct names to
avoid conflicting with the common ParameterValue model.

The Release build and all new regressions pass. The module group executes 35 of
189 registered UI cases, including six new cases. The final full CTest run is
**123/124**, with no skips: VirtoolsDevUi.filemenu crashes after its assertions
in AppKit destruction. An earlier full run crashes in SDL/Metal destruction.
The frozen pre-change program reproduces an SDL/Metal texture-access crash
during rendering; its different stack does not establish a common root cause.
The final web-export case passes three isolated repetitions; the save-callback
case passes three repetitions in each program. The group remains unresolved,
and passing repetitions do not replace the failed full run. No SDK/GPU
workaround or production debug logging was added.

Following the final file-menu crash, fifteen UI launches stopped at the macOS
restore-windows prompt. A process sample shows NSPersistentUIRestorer and
NSAlert.runModal before the debugger tests begin. Dismissing Don’t Reopen in each
test process allowed its cases to run. The final run includes those interventions
and is not a clean automated pass. Two test-only defects were corrected: cleanup
indexed an empty popup stack, and the unused-material check read a native object
pointer after destruction. The latter now captures the ID before deletion and
passes three successive runs. Earlier full results and their failures remain
recorded separately.

SDK revisions, existing patches and untracked contents equal the start of this
batch. Both measured programs use identical SDK runtime hashes. The viewport
rendering/transfer implementation and benchmark source are unchanged.

| Run | Median (ms) | p10 (ms) | p90 (ms) | MAD (ms) | MAD / median |
| --- | ---: | ---: | ---: | ---: | ---: |
| Before-change reference | 0.217586 | 0.203034 | 0.241736 | 0.014175 | 6.5145% |
| Nested-value implementation | 0.208834 | 0.203590 | 0.232499 | 0.006699 | 3.2080% |
| Same-program reference | 0.211175 | 0.210477 | 0.213604 | 0.000698 | 0.3307% |
| Same-program candidate | 0.212114 | 0.210596 | 0.213547 | 0.001262 | 0.5952% |

Each comparison contains eleven paired groups after warmup. The implementation's
observed median change is -4.0225%, but both sides exceed the original 3% MAD
limit. The final same-program control passes, with median +0.4446% and both MADs
below 1%. Timing acceptance remains pending. Earlier source captures are retained
alongside the fixes that required another measurement; no limits or benchmark
sources changed.
Stable readback, upload, target creation/release and path-selection counters
remain zero, with one explicit verification readback outside timing. Other
GPU/allocator counters remain unmeasured. Full samples, fingerprints and native
failure records are in
[nested-values-macos-arm64-metal.json](baselines/nested-values-macos-arm64-metal.json).

Specialized native renderers and the remaining Setup/Schematic/Layout work keep
phase four open. DataArray table rows, columns and cells belong to that remaining
Setup work; object-reference parameter arrays are the arrays completed here.
Coverage contains 876 files, 45 source groups, 32 features and 41 state entries;
the 55 existing UI native-access entries and completed_phase 3 are unchanged.

## Phase four: flags

Flags now use the same parameter description, registered value editor and
modification command in inline controls, native dialogs and nested structures.
The value retains all 32 bits, including unlisted bits and the sign bit. Choice
metadata retains zero and combined masks; inline controls keep their list while
native dialogs keep the single-bit grid and compact text representation. Public
interface version 12 and its existing integer/list fields are sufficient.

The old inline byte writes and native dialog mask reads/writes are removed.
The dialog reads its dimensions through its captured query provider. Independent
module regressions replace the flags query, renderer and modifier, then change
the configuration with a dialog open to verify that its original choices remain
active. Pressing Escape while holding a checkbox also cancels its pending click.

The macOS Release build and all 124 CTests pass without skips. The module group
executes 38 of 192 registered UI cases, including three new flags cases. Core
checks cover forty draft updates before one commit, cancellation and unchanged
values preserving Redo/Dirty, masks outside native storage, invalid native
descriptions, working copies, and nested undo/redo preserving member identity.
The previous native file-menu crash did not occur in this run; its cause remains
unresolved. SDK and viewport rendering/transfer sources are unchanged. Performance
results and remaining acceptance work are recorded in
[flags-values-macos-arm64-metal.json](baselines/flags-values-macos-arm64-metal.json).

| Release capture | median ms | p10 ms | p90 ms | MAD ms | MAD % |
| --- | ---: | ---: | ---: | ---: | ---: |
| Frozen reference | 0.208036 | 0.206459 | 0.210985 | 0.001319 | 0.6341% |
| Current | 0.208038 | 0.205986 | 0.210799 | 0.000765 | 0.3679% |
| Same-program reference | 0.212597 | 0.211141 | 0.213800 | 0.001080 | 0.5081% |
| Same-program candidate | 0.206519 | 0.204838 | 0.209502 | 0.001232 | 0.5964% |

Both eleven-group comparisons pass the original 3% limits: the implementation
median change is +0.0012%, and the same-program change is -2.8591%. This reuses
the preserved `aa75568` reference, measuring nested editing plus flags together;
it does not isolate flags against `e3244e4`. Paired SDK revisions, patches and
twenty-two runtime hashes are equal. Stable transfer/target/path counters remain
zero; additional GPU/allocator counters and remaining phase-four migration stay
open. Earlier rejected measurements and native crashes remain in their original
records.


## Phase four: specialized values

2D curves, Copy/Delete/Save/Replace dependencies and LOD options now use the same
parameter descriptions, registered editors and modification commands as other
values. Public C interface version 12 is unchanged: owned records and numeric
lists carry these values without SDK, STL, SDL or ImGui types.

Curve values retain fitting, point flags, TCB data and explicit tangents. Core
batch updates use owned SDK subclasses through protected value storage; they do
not downcast SDK objects or reimplement their file format. The undo record keeps
derived curve data as well, so Undo/Redo restore exact evaluation data without
recomputing or rounding it again. Native copies preserve existing data, including
empty legacy curves. Preview evaluation uses the SDK through a registered query
and the existing bounded query cache.

Dependencies retain every stored class and option bit, including hidden values;
mode changes do not discard them. LOD retains hidden proportions and unlisted
flag bits. The old native curve/dependency/LOD drawing and writing paths, row
value copies, and unused duplicate dependency editor are removed. A failing or
throwing held-value copier also restores and destroys its temporary carrier.

The five new native UI cases exercise dialog and nested one-step undo, curve
drag/cancel/add/remove/reopen, held dependency option cancellation, and independent
query/editor/modifier replacements captured across configuration changes.
Presentation state belongs to the control's ImGui window and resets against the
full target identity; it does not allocate keys for each new working copy.
Other native selectors and the remaining Setup/Schematic/Layout work keep phase
four open, with completed_phase still 3. Validation and performance results are
recorded in
[specialized-values-macos-arm64-metal.json](baselines/specialized-values-macos-arm64-metal.json).


The final macOS Release run passes all 124 CTests without skips; the module suite
executes 43 of 197 registered UI cases. Editing Y also preserves an untouched
legacy X outside 0..1. SDK state and twenty-two paired runtime
hashes are unchanged. No viewport rendering or transfer code changed.

| Release capture | median ms | p10 ms | p90 ms | MAD ms | MAD % |
| --- | ---: | ---: | ---: | ---: | ---: |
| Frozen reference | 0.257796 | 0.237904 | 0.341253 | 0.013253 | 5.1410% |
| Current | 0.245962 | 0.222612 | 0.296396 | 0.008720 | 3.5453% |
| Same-program reference | 0.216889 | 0.212463 | 0.217984 | 0.001043 | 0.4809% |
| Same-program candidate | 0.215983 | 0.213483 | 0.222039 | 0.002499 | 1.1572% |

That initial source capture has a lower observed paired median, but fails the
original 3% MAD limit; it is not used for acceptance. The same-program control passes with a
-0.4177% change. Both complete captures are retained. Stable transfer, target and
path-selection counts are zero; the explicitly listed unmeasured counters remain
open. The previous native cleanup crashes did not occur here, and are not marked
fixed.

A follow-up pair after that control passed. The final boundary-coordinate fix
then required another capture of the final binary; it and its subsequent
same-program control both pass the original limits. Earlier source captures
remain in the record.

| Final Release capture | median ms | p10 ms | p90 ms | MAD ms | MAD % |
| --- | ---: | ---: | ---: | ---: | ---: |
| Frozen reference | 0.207886 | 0.203326 | 0.210179 | 0.002293 | 1.1031% |
| Current | 0.206808 | 0.200925 | 0.227580 | 0.003304 | 1.5976% |
| Same-program reference | 0.211389 | 0.209548 | 0.223840 | 0.001451 | 0.6864% |
| Same-program candidate | 0.213784 | 0.209101 | 0.221016 | 0.004061 | 1.8995% |

The final paired median change is -0.5187%;
the final same-program change is +1.1329%.
Stable transfer/target/path counts remain zero. The other named GPU/allocator
counters remain unmeasured, and completed_phase remains 3.

## Time, key and remaining value selectors

时间、按键、脚本、动画、消息、属性类型及同步对象（含 State、Critical Section）已接入统一的值描述、查询、编辑器与修改命令。
时间保留原生毫秒精度，编辑单个单位不截断其余部分；按键保存原始扫描码，捕获在取消、点击其他控件或取消按钮、失焦和控件重新出现时主动结束。
新消息及同步对象只在确认时登记或创建。同步人数只有明确编辑时才加入修改，不会因为显示旧值覆盖外部修改。
消息类型名称与未编辑的人数归被引用资源所有，参数引用的撤销不回写它们。

删除原生对话框中的七类重复绘制、直接写入、创建标记和人数副本；内联、对话框及嵌套值使用同一草稿。
确认一次产生一步撤销，取消恢复交互开始值，无变化保留 Dirty 和 Redo。
可恢复数据沿用现有普通对象创建、对象状态修改和参数值记录。
已登记的消息类型编号沿用原生管理器的生命周期，撤销参数不会删除或重新编号。

编辑器 C 接口为 13；`EditorParameter::nullable` 区分必需对象参数的空引用与省略，模块需随宿主重新编译。
记录编辑器自行声明选项查询字段，标量名称/值配对检查保留。公开头无 STL、SDK、SDL 和 ImGui 依赖。
`parameter.selection-choices` 的可选 `filters` 记录区分省略、显式空所有者和空类别。
原生运行时 SDK、NMO/CMO/NMS 文件语义及 GPU 渲染/传输实现未改动。

新增六项原生界面回归，模块组执行 49/203 项。最终 Release 完整回归 **124/124**，无跳过，耗时 16.73 秒。
C/C++ 头布局、空引用与缺参、默认筛选、原生副本、嵌套身份和外部提供者保持均通过。
独立模块替换全部七类查询、控件和修改命令，以及选项查询；打开的对话框在配置切换后仍使用原选择。
本轮完整回归中的文件菜单原生清理崩溃均未复现，不能据此宣称其根因已经修复。

同尺寸 cubes64 场景、Release、每个程序至少 15 秒预热、11 组配对、每组至少 1 秒；采样时没有构建或 CTest。
修改前程序在改动开始前冻结；双方 SDK 的十六个仓库状态和二十二个运行库校验值一致。
`--sdk-change` 仅允许既有的 SDK 脏状态，本轮没有修改 SDK。

| 程序 | median ms | p10 ms | p90 ms | MAD ms | MAD/median |
| --- | ---: | ---: | ---: | ---: | ---: |
| 本轮 | 0.218521706 | 0.215187114 | 0.224610356 | 0.000815464 | 0.3732% |
| 修改前 | 0.218472280 | 0.217260692 | 0.224330255 | 0.001148762 | 0.5258% |
| 同程序对照 A | 0.211529768 | 0.208629687 | 0.216813103 | 0.001214001 | 0.5739% |
| 同程序对照 B | 0.212149787 | 0.209535184 | 0.218333524 | 0.002614603 | 1.2324% |

配对 median **+0.0226%**，同程序对照 **-0.2923%**，均通过原定 3% 门槛。
稳定读回、上传、目标创建、目标释放和路径选择计数为零；验证截图单独读回一次。
其余应用分配、GPU pass/submit/preserve 和 retain/native-query 计数仍未测，不作额外零计数声明。

完整记录、原始样本和早期失败见 `baselines/remaining-values-macos-arm64-metal.json`。
覆盖清单仍为 45 源码组、42 状态项、55 处旧 UI 暴露；阶段四继续保留 DataArray 表格及剩余 Setup/Schematic/Layout 迁移，`completed_phase` 仍为三。

## DataArray table editing

DataArray 的单元格、批量单元格及列属性共用查询、草稿、已登记编辑器和修改命令。单元格保留原生文本输入；列名、类型、参数类型组成一个记录。确认一次只记录一步撤销，取消不写入运行时；列类型下拉选择不会提前提交整个对话框。行列菜单与快捷键也调用相同命令。

表格绘制不读取 SDK 或 EditorState。选区、焦点、滚动及弹窗归窗口；行列位置与文档版本归草稿。排序、结构变更、Undo/Redo 和文档切换拒绝旧位置提交。滚出活动单元格、切换对象和关闭窗口会取消草稿，并清理 ImGui 的失活后回写。行列操作失败不会提前移动选区。

原生 DataArray 状态加载会重新创建参数，因此撤销改为表格数据记录，保留原有参数及其引用对象。参数状态继续使用原生状态块；删除操作保护原参数，受保护调用返回后恢复所有者。普通单元格 setter 能读取原来的所有者。任一单元格拒绝或参数文本函数写入后报错，都恢复整批。历史淘汰沿用现有对象回收。复制后删除源行再粘贴参数值仍可用。

编辑器 C 接口为 14，beginEdit/drawProperty 接收 EditorRequest，支持目标外的位置参数；外部模块需随宿主重编译。运行时 SDK 不动。独立模块替换单元格和列的查询、编辑器与修改命令，仍走同一入口；已有宿主草稿期间保留原有配置切换检查。C/C++ 公开头没有 STL、SDK、SDL 或 ImGui 依赖。

新增六项 DataArray 原生界面回归，模块组执行 55/209 项。最终完整 Release **124/124**，无跳过，27.78 秒；包含直接打开 NMO、历史回收、原生 GPU 与通用 fallback 的既有回归。源码覆盖为 47 组、42 状态项、55 处旧 UI 暴露，新增表格绘制禁止间接原生访问的检查。

性能使用相同 cubes64 场景、固定尺寸、至少十五秒预热、十一组配对及同程序对照，采样期间不运行构建或 CTest。首轮旧版本采样的参考 MAD 超过 3%，未用于最终验收；完整原始数据保留。最终结果：

| 程序 | median ms | p10 ms | p90 ms | MAD ms | MAD/median |
| --- | ---: | ---: | ---: | ---: | ---: |
| 本轮 | 0.533306156 | 0.523450200 | 0.560993601 | 0.009855956 | 1.8481% |
| 修改前 | 0.526828713 | 0.501568101 | 0.534379642 | 0.007550928 | 1.4333% |
| 同程序对照 A | 0.216117441 | 0.211341075 | 0.217156434 | 0.004227887 | 1.9563% |
| 同程序对照 B | 0.214393573 | 0.210584036 | 0.216926552 | 0.002331606 | 1.0875% |

配对 median **+1.2295%**，同程序对照 **+0.8041%**，均通过原定 3% 门槛。双方 SDK 十六个仓库状态及二十二个运行库校验值一致，GPU 渲染/传输代码未改。稳定读回、上传、目标创建、目标释放和路径选择计数为零；截图验证单独读回一次。其他应用分配、GPU pass/submit/preserve 和 retain/native-query 计数仍未测。

记录见 `baselines/data-array-macos-arm64-metal.json`。Setup 宿主及剩余 Setup/Schematic/Layout 迁移继续保留，完成阶段仍为三。此前的原生清理故障本轮未复现，未宣称根因已修复。

## Setup property editing

剩余 Setup 属性接入 `setup.*` 查询、属性草稿和修改命令。实时预览由命令保存恢复数据；确认前恢复，再提交一次正式修改。取消、切换对象、关闭窗口、文档替换及目标失效统一清理。普通字段、变换、引用、Mesh/通道、字体、声音、场景环境、图像格式、Grid 笔画和管理器属性均复用此流程。原来的 Live/LiveScene 和全局暂存状态已删除。

多层 Grid 绘制和批量可见性只形成一步撤销。位图保存格式及选项使用自身字节记录，保留旧文件的格式头；读写选项检查类型、成员和存储尺寸。没有图像的位图属性使用值记录，避免整对象加载改变 Current Slot。Automatic mipmap 使用原生 -1。Mesh 列表精确恢复，未变化的补充属性不重复调用 setter。材质保存效果参数的数据，参数值记录以材质、效果和成员路径定位，效果重建参数后仍能连续撤销。

编辑器 C 接口为 15，外部模块需重编译；预览回调只能用于可撤销文档修改，禁止重入修改。公开头仍无 STL、SDK、SDL 或 ImGui 依赖。Setup 默认提供者从同一组描述生成；替换查询或命令仍走公共入口。窗口所属对象和实际编辑目标分别保存；复合控件取消同时清除子输入的失活后回写。

最终完整 Release **124/124**，无跳过，17.69 秒。Setup 交互组 7/7，普通 Setup 组 17/17，模块组 55/213 项；包含 Import/Merge 失败、文件保护、原生 GPU 和通用 fallback 的既有回归。SDK 十六个仓库的版本、状态、差分和未跟踪文件校验值保持一致，二十二个运行库的校验值与对照一致。覆盖清单为 48 组、42 状态项、55 处旧 UI 暴露，完成阶段仍为三；Setup 窗口及工具迁移和剩余 Schematic/Layout 交互继续按后续步骤推进。

性能使用相同 cubes64 场景、固定尺寸、至少十五秒预热、十一组配对和同程序对照，采样期间没有构建或 CTest。

| 程序 | median ms | p10 ms | p90 ms | MAD ms | MAD/median |
| --- | ---: | ---: | ---: | ---: | ---: |
| 本轮 | 0.211963089 | 0.210513617 | 0.303155993 | 0.001137385 | 0.5366% |
| 修改前 | 0.210886421 | 0.209434996 | 0.218509616 | 0.000780046 | 0.3699% |
| 同程序对照 A | 0.203603567 | 0.201281723 | 0.206499686 | 0.000710441 | 0.3489% |
| 同程序对照 B | 0.203595533 | 0.202830366 | 0.204558735 | 0.000208490 | 0.1024% |

配对 median **+0.5105%**，同程序对照 **+0.0039%**，通过 3% 门槛。稳定读回、上传、目标创建、目标释放和路径选择计数均为零；截图验证单独读回一次。其余分配、GPU pass/submit/preserve 和 retain/native-query 计数仍未测。生产 GPU 渲染/传输代码未改。

完整数据与原始样本见 [Setup 验证记录](baselines/setup-properties-macos-arm64-metal.json)。早期完整运行发现的补充属性恢复副作用、材质参数生命期和分层问题已修正，失败日志保留在记录中。


## Remaining editing entry points

Schematic 的注释、脚本颜色、优先级和连线延迟改用共享草稿，删除各对话框独立保存的待提交值。
注释和脚本颜色通过 `schematic.*` 查询、属性及修改命令登记；移除颜色也使用登记命令。
优先级与延迟复用已有 Setup 属性，多选确认只形成一步撤销；首项无变化仍处理后续目标，任何失败回滚整批。
索引由完整对象引用及 Script 版本约束；旧槽位不能写入后来替换的注释。确认后的作用域保留到帧末执行。

本轮核对的文档编辑流程如下。浏览过滤、导航、运行设置、打印/导出选项及工作区偏好继续保持各自原有保存方式，不制造文档 Undo。

| 编辑入口 | 原值及草稿 | 正式修改 |
| --- | --- | --- |
| 名字、简单值、组合值、专用值、嵌套值 | EditorApplication 草稿或 ParameterBox 的原生工作副本 | 登记的名字/参数命令；工作副本整批提交 |
| DataArray 行列及单元格 | 固定位置、文档版本及父级记录 | array.* 查询、属性与命令 |
| Setup 属性 | 共享草稿与命令预览恢复数据 | setup.* 属性命令 |
| Schematic 注释、脚本颜色 | 共享草稿；固定完整目标及 Script 版本 | schematic.comment、schematic.script-color、schematic.reset-color |
| Schematic 优先级、连线延迟 | 共享草稿；固定全部目标 | 复用 setup.behavior.priority、setup.behavior-link.delay |
| Schematic 移动、缩放、框选建图及连线工具 | 现有图编辑数据和编辑事务 | 统一 Escape 及视图失焦/隐藏取消入口 |
| Layout 相机、三维变换、二维句柄及飞行 | 现有 ObjectsGesture 与原生状态 | 统一恢复及一次提交；原生工作副本继续覆盖二维取消 |

新增七项原生 UI 回归：多选优先级一步撤销、脚本颜色取消/重新编辑/Undo、Schematic 移动失焦取消和框选建图 Escape、Layout 变换 Escape/失焦取消和正交相机恢复。
原有注释用例补充实际输入、取消、重新编辑及 Undo；核心用例覆盖重复 Begin、二维取消时原值/历史/Redo/Dirty 保持、无变化批次、旧槽位、部分失败及旧文档草稿。
完整 Release **124/124**，无跳过，22.96 秒。Schematic **19/19**、Layout **20/20**、Setup **17/17**、属性交互 **7/7**，模块组执行 **55/220** 项。
架构检查 21 项通过，覆盖 909 文件、49 组、42 份状态、55 处旧 UI 暴露；新增 Schematic 特性视图禁止间接原生访问的检查。
SDK 十六个仓库和二十二个运行库校验值一致，编辑器 C 接口仍为十五；渲染和传输实现保持原样。

性能仍使用相同 cubes64 场景和尺寸、至少十五秒预热、十一组配对，采样期间没有构建或 CTest。

| 程序 | median ms | p10 ms | p90 ms | MAD ms | MAD/median |
| --- | ---: | ---: | ---: | ---: | ---: |
| 本轮 | 0.274438807 | 0.250026413 | 0.288783232 | 0.007127538 | 2.5971% |
| 修改前 | 0.276118415 | 0.256044046 | 0.285059150 | 0.006932091 | 2.5106% |
| 最终同程序对照 A | 0.219340523 | 0.208511167 | 0.237282275 | 0.010829356 | 4.9372% |
| 最终同程序对照 B | 0.210926613 | 0.209104483 | 0.230272729 | 0.001822129 | 0.8639% |

修改前后配对 median **-0.6083%**，双方 MAD **2.5971%/2.5106%**，该次配对通过。
同程序首次对照因参考 MAD **3.5087%** 超限；加长到每组一万帧的对照触发现有六十秒看门狗，结果无效。
恢复正常采样后的最终同程序对照 median **+3.9890%**、MAD **4.9372%/0.8639%**，仍未通过。
**本轮性能计时尚未验收**，保留所有原始结果，未放宽 3% 门槛。稳定读回、上传、目标创建/释放及路径选择计数为零；截图验证单独读回一次。
其余应用分配、GPU pass/submit/preserve、native query 和 retain/release 仍未测。

完成阶段仍为三。阶段四清单保留双实例导航和旧索引失效、图编辑通知及外部命令同结果、NMS 恢复本身失败的故障注入；本轮没有把未迁移的视图与图命令标作完成。
完整样本、SDK/程序校验值、早期失败及测试构造说明见 [编辑入口验证记录](baselines/editing-entry-points-macos-arm64-metal.json)。

## Schematic 多实例导航与元素位置

Schematic 通过 `script.schematic` 登记为工作区多实例视图，默认角色为 `main.schematic`。创建、销毁、导航、只读查询和状态恢复使用工作区入口，内置跳转也执行 `script.navigate`。导航参数中的 `background` 非零时由工作区保持原焦点；`script.show` 保留原有后台更新行为。

选区、可见栈、隐藏、排序、Script 开合、缩放平移和查找位置归视图实例。隐藏、排序及锚点保留完整对象引用；可见原生 ID 仅为投影。新文档身份才重置这些状态，切换失败保留每个实例的选区和浏览位置；开合不再修改共享的 ScriptSession。

`ElementRef` 继续用于当前模型和图算法，跨帧保存的位置改用 `ElementAddress`：完整对象引用、Script 版本和拓扑版本，引脚另有端点对象身份。旧槽位在后续编辑、Undo/Redo 或端点替换后失效，不能指向后来占用同一位置的元素。搜索、队列、命中缓存和错误连线列表统一检查；清除模型前清除命中指针，错误连线旧记录不能删除新槽位。

关闭画布取消其正在等待、已经排队和当前分发批次中的文件选择回调，并释放捕获内容；其他窗口继续工作。返回的路径在使用前以完整引用重新检查 Script 和图。只读实例查询只读取发布的数据，按变化更新；公开 C 枚举头没有 STL 或 SDK 依赖。

完整 Release **124/124**，无跳过，21.88 秒。Schematic **21/21**，Layout **20/20**，模块 **55/222**；文件对话框用例补齐关闭窗口及分发内取消，模型用例补齐六种索引、旧搜索结果、Undo/Redo、引脚无版本变化替换和旧错误连线。双实例用例覆盖后台焦点、文档切换失败、关闭实例后的排队跳转及非法导航值。原有 Building Block 接受/取消和 `.NMO` 直接打开回归保持通过。

SDK 十六个仓库状态和内容不变，二十二个运行库与修改前一致；GPU 渲染和传输源文件未改。架构检查覆盖 915 文件、49 组、42 状态、55 处旧 UI 暴露。F11 缺口已关闭；F05 和 F12 仍留在阶段四，完成阶段仍为三。

| 程序 | median ms | p10 ms | p90 ms | MAD ms | MAD/median |
| --- | ---: | ---: | ---: | ---: | ---: |
| 本轮 | 0.242097142 | 0.234574568 | 0.248637180 | 0.006208931 | 2.5646% |
| 修改前 | 0.236870863 | 0.230761492 | 0.246493313 | 0.005995014 | 2.5309% |
| 同程序对照 A | 0.248134934 | 0.231796469 | 0.253573746 | 0.013261593 | 5.3445% |
| 同程序对照 B | 0.237426599 | 0.233106168 | 0.252374663 | 0.004320431 | 1.8197% |

修改前后十一组配对 median **+2.2064%**，双方 MAD **2.5646%/2.5309%**，该配对通过。同程序十一组对照 median **+4.5102%**，双方 MAD **5.3445%/1.8197%**，未通过原定 3% 门槛。**计时尚未验收**，保留全部原始样本，没有放宽标准。稳定读回、上传、目标创建/释放及路径选择均为零，验证截图读回一次；应用分配、GPU pass/submit/preserve、native query 和 retain/release 仍未测。

完整采样、源码和运行库校验及测试说明见 [Schematic 导航验证记录](baselines/schematic-navigation-macos-arm64-metal.json)。

## Phase four: NMS publication recovery

File owners can supply `SaveFileOperations` for replacement and deletion. The
normal implementation retains the existing platform calls, permission checks and
exclusive-create behavior. Its interface belongs to `core/document`; platform
implementation stays in `detail`. PendingSaveFile owns candidates and OutputFiles
owns ordered publication and recovery. The editor C ABI stays unchanged.

The new NMS regression first failed because an exception during DSC recovery
escaped, triggered another recovery attempt and allowed destruction to remove the
previous DSC backup. A separate publication exception case showed that exceptions
outside std::exception skipped recovery entirely. OutputFiles now handles recovery
failure per file, continues restoring the other outputs, retains old backups and
lists the outputs still present. Candidate cleanup records errors and exceptions
in the completed operation. Failure to remove a new output is reported separately
from failure to restore a previous output.

NeMoDevEditor runs 27 actual script.export combinations: old or absent DSC,
publication and recovery failures reported as error codes or exceptions, and
cleanup failures. The requesting view closes before completion. Checks include
original NMS bytes, remaining DSC, recovery path and backup bytes across retries,
queryable results, document identity, content/topology versions, Dirty, selection,
Undo/Redo, display data, flags, interface chunks and write mode. A three-output
case additionally verifies reverse recovery order, exactly one attempt per file,
continued recovery after another file fails, and manual restoration from the
retained backup.

The final Release suite passed 124/124 without skips, including native UI and
representative NMO direct opening. SDK source state stayed unchanged. Timing,
transfer counters, source/runtime hashes and the stage completion status are in
`baselines/nms-recovery-macos-arm64-metal.json`.

F05 closes the last phase-four functional gap. completed_phase remains three
because the final timing pair and same-program control exceed the original
variation limit. All samples remain in the baseline record.

## Phase four: viewport diagnostics, acceptance deferred

性能验收按用户要求继续暂缓，原定 3% 回退、MAD 和同程序漂移门槛保持不变，
`completed_phase` 仍为三。第四阶段功能缺口为零；其余二十二项属于阶段五及后续。

计时仍覆盖原驱动下的完整编辑器帧及 GPU 完成等待。新增组间 CPU、调度、热状态、
低功耗和 GPU 等待记录；预热与组时长可显式增加，输出须与请求一致。macOS 准备阶段
在 GPU 空闲时等待热状态正常且无低功耗模式连续三十秒，最多三分钟，随后仍执行
至少十五秒实际渲染预热。准备与组间空闲不计作渲染时间，也不从已测组中扣除耗时。
控制器和原生看门狗均保留有限时长，结束或异常退出会恢复看门狗配置。

第三版测量会拒绝热状态、低功耗或调度角色变化的整轮记录，同时保留双方全部样本。
稳定传输与资源计数仍要求为零，验证截图要求一次显式读回。线程提级、进程提级、
交互活动和主循环驱动的实验均已撤回，未修改编辑器、SDK 或 GPU 渲染/传输实现。

热状态不能单独解释波动：正常热状态下的同程序对照仍出现 MAD 4.0352% / 3.1183%。
固定工作量的组间探针和调用栈采样不足以确认唯一根因；独立 CPU 探针尚未执行。
全部 28 轮诊断、634 组样本和各自源码、程序及运行库校验值保留于
[诊断记录](baselines/viewport-diagnostics-macos-arm64-metal.json)，均未用于宣告性能通过。

完整 Release 回归 124/124，无跳过；最终补充检查 3/3，计时工具测试 26/26。
SDK 十六个仓库前后状态不变，双方二十二项运行库校验值一致。

## 阶段五：调试视图与运行查询

第一批迁移 Script Debugger 和 Profiler。它们由工作区创建，每个实例保存自己的显示状态，
通过登记的查询读取运行状态、调试对象、日志、命中位置和统计数据，修改复用运行命令。
旧 DebuggerPanel、ToolWindows 中的 Profiler 实现，以及 EditorState、EditorPanels 和
ToolWindows 中对应的窗口开关与显示状态已删除。尚未迁移的 UI 访问项为 54 处。

运行状态、调试对象列表和工作区角色查询按版本缓存。对象列表与统计明细分页返回；
日志行和统计页带有版本，过期索引不能读取另一条记录。对象引用包含会话和生命周期，
保留的调试目标不能导航到后来复用同一原生 ID 的对象。暂停期间锁定对象失效也会更新投影。

原生统计和 profiling 设置由 ExecutionCommands 管理。多个 Profiler 同时打开时共享原生计数，
最后一个视图关闭后恢复之前的设置。后台任务期间窗口继续显示已经发布的数据；关闭窗口
只更新工作区，原生恢复在独占任务结束后执行。原生文档切换前先恢复 profiling，
切换完成后清除旧样本；即使文档未被替换，也会同步运行模块已经结束的调试状态。

新视图禁止间接包含 CKContext、可变 Document 和 EditorState，两个架构检查均包含该限制，
并补故障与生命周期回归。运行设置的持久化由应用装配处接收命令结果，窗口不再直接修改偏好。
SDK、文件格式、公开 C 头和 GPU 渲染/传输实现不变。性能验收继续暂缓，原门槛不变，
`completed_phase` 仍为三。

完整回归还发现旧的静默删除测试把仍登记于渲染上下文的对象直接销毁，留下失效的渲染引用。
故障注入现在先解除该对象的渲染登记，保留 NONOTIFY 和同 ID 复用检查，并在同一用例中
实际切换文档、确认文档身份已改变，验证视口拆除。没有修改 SDK 的删除通知行为。

阶段五尚未收尾：插件与路径管理、其余诊断和特殊工具，以及 Layout/Schematic 中剩余的
原生适配仍保留在迁移清单中；没有新增或延长旧访问例外。


## 阶段五：Parameter Debugger

Parameter Debugger 现在是 `runtime.parameters` 工作区视图，主实例使用 `main.parameters` 角色。
每个实例拥有独立的观察列表、选中项和未提交输入，列表只保存完整对象引用和已读取的值。
查询来自 `ParameterCommands`，不在视图中访问 CKContext、可变 Document、EditorState 或旧窗口。
移除 ToolWindows 中对应的实现、原生编号列表、窗口开关和 Schematic 依赖，删除已无调用者的
`ValueEditState`、`ParameterValueEditor` 和取消包装。现有 Setup 使用的参数草稿编辑保持原样。

`parameter.watch` 读取参数的名称、值、类型、所有者、Script 和可用属性编辑器。
输入参数拒绝观察；输出参数和没有所属 Script 的局部参数保持只读。
`parameter.clipboard` 和 `parameter.script-locals` 分页返回完整对象引用，每页最多 128 项；
后者明确要求 Script，不在领域查询中选择启动样例。捕获模式通过运行查询寻找第一个有局部参数
的 Script，普通视图不依赖该选择。原有剪贴板继续拥有复制来源，没有引入第二份剪贴板。
参数功能模块增加对现有 Clipboard 所在组 S01 的依赖；工具窗口不再依赖 Schematic 的 S21。

菜单、捕获模式和外部调用使用同一组打开、观察、移除、清空命令；命令可以显式指定视图。
Paste 读取已有剪贴板，Go to source 通过 `script.show-parameter` 进入现有 Schematic 导航。
支持的参数复用已登记的属性编辑器和修改命令；剩余文本转换通过 `parameter.set-text` 进入
原有领域编辑，并校验对象生命周期和参数类型。一次确认形成一步撤销，Escape、清除观察或
关闭视图会放弃未提交输入。对象类型变化和后台任务开始时取消该行输入，避免恢复显示后误提交。

后台任务占用原生上下文期间继续显示上次完成的行，查询由宿主拒绝，视图不进入原生上下文。
对象失效时移除该行；被遮住的标签页即使没有绘制，其观察查询也会过滤失效引用。
文档身份变化时清空列表，加载失败保留同一文档的列表。
不同视图互不修改观察列表，关闭工具命令会覆盖所有调试视图实例。
切换属性编辑器和修改提供者保留观察列表，外部实现通过相同流程产生撤销步骤。
外部测试编辑器调整为使用 ImGui 当前项目宽度，避免表格中的附加按钮落在可见区域之外；
没有为它增加专门的生产代码分支。

新增回归覆盖观察资格、所有者和 Script、分页剪贴板、类型变化、原生编号复用、文本修改与
Undo/Redo、双实例隔离、Ctrl+V、源导航、文档切换、忙时缓存，以及选择外部属性编辑器。
新视图继续受现有禁止原生状态包含的两项架构检查约束。SDK、公开 C 头及 GPU 渲染和传输
实现不变，性能验收继续暂缓，原门槛不变，`completed_phase` 仍为三。

本批构建与回归结果记录于
[Parameter Debugger 验证记录](baselines/parameter-debugger-macos-arm64-metal.json)。

本批完整 Release 功能回归 124/124，无跳过；其中工具组 7 项、模块组 57 项和调试组 6 项
实际执行并通过，未选择的 UI 用例未计为通过。SDK 十六个仓库及二十二项运行库校验值均未变。


## 阶段五：插件与路径管理

Installed Plugins 与 Path Manager 已迁入工作区，菜单、启动参数及“关闭所有管理器”
都通过登记命令打开和关闭视图。插件分类展开状态属于插件视图，并通过工作区保存；
路径视图只保存 UTF-8 草稿、选区与目录选择结果，Ok 才调用统一修改命令，Cancel 放弃草稿。
原有的分类、插件表、管理器开关、临时卸载/重载、路径添加/编辑/删除/排序和目录选择保留。

`EnvironmentCommands` 是原生插件和路径操作的入口。`plugins.list` 分页返回已复制的插件信息，
`paths.list` 返回路径分类与列表；版本查询只读已发布编号。视图打开时建立原生数据，
有读者时以 250 ms 间隔刷新；待恢复插件也会刷新插件投影，其余关闭的视图不触发枚举。
后台任务期间读取上次完成的数据，拒绝原生修改，关闭窗口不访问原生上下文。
文档代际变化即使没有打开视图，也会更新路径与插件版本，旧草稿不得覆盖新状态。

`plugins.unload`、`plugins.reload`、`plugins.set-manager-active` 与
`plugins.retry-reloads` 共用原生执行与恢复流程。卸载前登记完整 DLL 路径及发起视图，
不跨重载保留 DLL 或管理器索引。窗口关闭时等待原生任务和编辑结束再恢复；
自动恢复失败保留错误与待恢复项，避免逐帧重试，并允许显式重试。
退出时先结束资源库待输入请求，再恢复仍由打开视图持有的卸载项。插件及管理器操作刷新 Building Block、
参数和属性目录及 Schematic 显示，不改变文档、选区或撤销历史。
停用管理器的重新启用改用 SDK 已有的名称查询并校验 GUID，避免仅枚举活跃管理器造成开关失效。

`paths.apply` 接受完整分类列表及预期版本，校验后统一更新原生路径和应用配置。
配置文件使用现有 `OutputFiles` 暂存和替换流程；保存失败或抛异常时恢复原生路径，
保留旧配置。启动加载已保存目录也使用同一原生路径实现，保留 SDK 对重复目录的处理。
`paths.choose-folder` 的异步请求属于发起视图及文档，关闭或切换文档后拒绝迟到结果；
系统目录窗口只负责呈现输入并提交 `paths.folder-response`，可替换的命令也供内置窗口使用。

新增原生回归使用单独构建的 SDK 插件，验证实际卸载、显式重载失败、关闭视图后恢复失败、
重试和退出恢复。路径回归覆盖过期版本、非法输入、保存失败/异常回滚、启动重复目录、
文档切换、工作线程拒绝和异步结果失效。窗口回归覆盖输入取消、排序、确认、分类状态、
单实例、忙时显示和关闭，以及外部模块替换目录选择和保存命令。
嵌套路径投影还发现工作区的重复序列化漏传记录与实数列表，现已复用既有字段转换并补接口回归。
F21 的缺口关闭；其余二十一项和阶段五剩余迁移继续保留，`completed_phase` 仍为三。

本批验证记录见
[插件与路径管理验证记录](baselines/environment-macos-arm64-metal.json)。
SDK、公开 C 头及 GPU 渲染与传输源码未改动；性能验收继续暂缓，原门槛不变。

最终 Release 功能回归 124/124，无跳过；其中工具组 9 项、模块组 58 项和调试组 7 项
实际执行并通过。SDK 十六个仓库状态和二十二项运行库校验值均未变。


## 阶段五：独立诊断与连线属性

Unused Object Explorer、Building Block Version Test、All CK Object In Memory 与 About
已迁入工作区；Behavior Link Properties 也使用工作区实例与登记命令。
菜单、快捷键、Schematic 工具栏和启动入口走相同命令，不再由 ToolWindows 维护窗口开关。
删除旧 ToolWindows.cpp；剩下的 ToolWindows.h 只服务尚未迁移的 General Preferences。

`DiagnosticCommands` 通过既有 RuntimeOperation 执行检查并发布分页只读结果。
结果属于发起视图与文档，包含完整对象引用和内容版本；排除项、选择和排序属于视图实例。
检查失败保留上次结果，关闭视图或替换文档后丢弃迟到结果。
原生上下文被后台任务占用时继续显示已完成结果，查询不访问 CKContext。
不同实例的检查不会重置彼此的排除项和选择；插件目录变化使旧版本报告失效。

删除无用对象在执行时重验内容版本和对象生命期，保留既有不可撤销删除及清除历史的行为。
版本检查与 Schematic 替换共用 `schematic.replace-blocks`，先校验整批目标，再复用 core
替换操作；整批形成一步撤销，失败回滚。Schematic 在命令排队完成后更新最近使用列表、
选择及日志，不以排队状态当成替换成功。

连线属性只保存普通整数和布尔草稿，复用已登记的值编辑器，不创建临时 CK 参数。
宿主允许视图绘制自己的值草稿，继续拒绝绘制阶段之外、错误线程和错误描述符的调用，
并限制编辑器递归。`environment.link-options` 提供只读设置，
`environment.set-link-options` 统一校验版本、更新原生迭代上限与保存偏好。
保存失败或抛异常恢复原生设置；Cancel 放弃草稿，不改变文档、选区和撤销历史。
结束当前数值输入的 Enter 不会同时确认整个对话框，空闲时的 Enter 与 OK 都重验设置版本。
路径与连线设置保存复用同一现有 OutputFiles 配置写入流程。
工作区模态窗口的关闭清理由共用 EditorPopup 管理，忙时程序关闭也不会残留模态遮罩。

新增原生与 UI 回归覆盖分页、独立实例、失败保留、忙时显示和关闭、文档切换、
同编号对象复用、过期结果拒绝、批量替换回滚及单步 Undo，以及外部查询、命令和值编辑器。
完整 Release 功能回归 124/124，无跳过；工具组 11 项、模块组 59 项、调试组 8 项，
以及 Schematic 替换完成回归实际执行并通过。SDK 十六个仓库状态、二十二项运行库校验值、
公开 C 头和 GPU 渲染与传输源码均未变。性能验收继续暂缓，原门槛不变。

源码归属现为 53 组、44 类状态；旧 UI 访问由 54 处降至 53 处。
载入后连线检查、消息浏览等 Schematic 图内诊断仍单独列在清单中，
General Preferences、Layout/Schematic 剩余原生适配及 F20 资源待办继续保留。
本批不代表阶段五已完成，`completed_phase` 仍为三，二十一项待办没有延期。
验证记录见 [诊断与连线属性验证记录](baselines/diagnostics-macos-arm64-metal.json)。


## 阶段五：General Preferences

General Preferences 已迁为 `preferences.general` 单实例工作区视图。
菜单、Ctrl+P、Layout 按钮及右键、Ctrl+G 和启动入口都调用 `preferences.open`；
页面使用稳定名称寻址，七页布局、标签、Default/OK/Cancel 行为保留。
工作区保存页面和轴显示选择；未提交的配置草稿只属于视图，关闭或更换提供者即放弃。
删除最后的 ToolWindows 外壳、旧 GeneralPreferences.cpp 和相关中央事件。

原有 Preferences 数据及文件、注册表、dev.ini 读取实现移至 application。
Preferences 数据头不再包含 CK、可变文档或 ImGui；颜色转换留在 Schematic 呈现。
现有字段表同时服务持久化与偏好投影，未增加第二份共享配置。
`preferences.values` 返回复制的可编辑值和版本；`preferences.catalog` 返回设备、显示模式、
渲染引擎及可保存图像格式的复制信息，不跨帧保留 SDK 索引或指针。
目录只在需要时读取，插件变化或 Context 更换使旧选择失效；后台工作期间读取上次完成的数据。
窗口关闭后不继续轮询偏好和原生目录，GPU 呈现流程不增加查询或传输步骤。

所有确认经 `preferences.apply`，执行时再次校验版本、键、类型和数值，
只合并属于 General Preferences 的字段，不覆盖最近文件、路径、资源库、连线属性和调试设置。
Default 修改当前页的本地草稿，OK 才更新共享值和原生设置；Cancel 不影响文档、选区或历史。
常规布尔和数值输入复用已登记的值编辑器，外部模块可以替换查询、修改命令、编辑器和整个窗口。
菜单中的显示/隐藏动态对象也使用同一偏好修改流程，避免另一个写入入口。

命令先准备图像保存格式并保存实际原生设置，再更新设置和派生显示，最后通过既有 OutputFiles
写入配置。准备、显示更新或保存失败/抛异常时恢复配置、原生选项和显示，保留原文件。
渲染引擎选择保存后仍须重启；不会重建当前 Context，窗口按实际启动引擎保留重启提示。
旧配置中无法枚举的设备和引擎继续显示原值。

完整 Release 功能回归 124/124，无跳过。工具组 13 项、模块组 60 项、调试组 9 项、
应用组 25 项和 Layout 组 20 项实际执行并通过；新用例验证七页导航、草稿与默认值、
确认/取消、过期拒绝、单实例和关闭后状态、忙时显示及关闭，以及外部替换与动态对象菜单。
原生回归覆盖保存拒绝和异常、派生显示更新失败、不可用图像格式、非法输入、Context 重建、
延迟命令失效和文档/选区/Undo/Redo 保持。
迁移前冻结的默认与自定义配置逐字节比较通过，后者包含 UTF-8 路径、最近文件和资源库。

源码归属现为 54 组、45 类状态；旧 UI 访问从 53 处降到 48 处，依赖和包含检查已收紧。
SDK 十六个仓库状态、二十二项运行库校验值和公开 C 头均未变。
Layout 的改动仅将偏好入口接到命令；GPU 渲染、传输和 fallback 方法保持原样。
性能验收继续暂缓，原门槛不变，`completed_phase` 仍为三。
阶段五仍有 Schematic 图内诊断、Layout/Schematic 原生适配、资源待办与文档扩展数据等剩余项，
阶段六的完整配置迁移与部署也没有记为完成。
验证记录见 [General Preferences 验证记录](baselines/preferences-macos-arm64-metal.json)。

## 阶段五：Schematic 诊断、图命令和 Layout 场景查询

Schematic 的错误连线和消息浏览迁入工作区。原生图查询保存载入检查结果并发布复制的行、
消息使用位置和运行高亮；视图只保存所选行、来源 Schematic 和未完成命令编号。
来源关闭后拒绝跳转，避免转向另一个 Schematic。后台任务期间保留已显示结果，拒绝原生
查询和修改，窗口仍可关闭。载入结果不再放在 EditorState。

折叠图、切换 Target、创建图、删除图元素及错误连线使用登记的命令。
修改前校验整个批次的对象引用和 Script/拓扑版本，嵌套 core 编辑合成一步撤销。
错误连线删除先解析全部位置，再改显示数据；Undo 能恢复连线和保存点。
Script 删除确认框持有完整引用；Script 初始条件、执行、复制和优先级入口复用已有查询及命令。
复杂连线、图布局和手势仍属于后续画布迁移，未将它们记为已完成。

Layout 的相机、Selection Groups、Explorer 和右键菜单使用只读场景投影，修改复用对象、
场景、选择和新增的 Layout 命令。每个列表复用已读数据，按发布版本与参数失效；查询失败
保留旧副本。创建分组和选择已有分组分别提交，避免选择变更混进文档编辑事务。
Copy Options 仅保留草稿；确认后统一复制。Ctrl+V 按剪贴板来源和复制版本执行，允许复制
仍由剪贴板保留的撤销对象；选区后来改变不会替换复制来源，旧版本不能粘贴新来源。

查询缓存版本同时识别当前提供者；不同提供者即使发布相同的数字版本，也会使视图缓存失效。
数据中的分页/目录版本继续按原值校验，公开 C 结构和 ABI 15 不变。
Layout 数据及 Schematic 诊断视图禁止间接接触 SDK、可变文档和 EditorState；原生命令也不得
重新依赖 EditorState。旧画布与手势访问仍有明确的阶段五截止项，未扩大豁免范围。

呈现、GPU 普通纹理导入、fallback、截图读回和资源释放方法不变。Start 只增加编辑器命令宿主
的绑定；SDK 不改。性能验收仍暂缓，不改变原 3% 门槛，也不推进 completed_phase。
本轮验证记录见 [Schematic 与 Layout 验证记录](baselines/canvas-macos-arm64-metal.json)。

最终 macOS arm64 Release 功能回归 **124/124**，无失败、无跳过，逐项结果与所选 CTest 清单
核对通过。模块 UI 实际执行 62 项、Layout 21 项、Schematic 22 项、调试 9 项、应用 25 项，
包含原有 GPU 稳定纹理、fallback 和显式截图读回检查，以及代表性 `.NMO` 直接打开回归。
十六个 SDK 仓库及二十二项运行库校验值保持一致；公开 C 布局和 ABI 15 不变。
新投影及位置头可在没有 SDK 包含路径的消费程序中编译；架构检查的 27 个拒绝/接受场景通过。
源码归属为 57 组、46 类状态，旧 UI 访问为 47 份；复杂画布、工具及剩余原生适配继续保留在清单。

## 阶段五：复杂图编辑、连线与 Layout 手势

Schematic 的移动、Script/图/注释尺寸预览、分组/解组、框选建图、剪贴板和拖动复制，
以及连接、重连、类型转换、插入运算、批量连接、自动引脚、导出引脚、路由和控制点插入，
通过登记查询和命令调用原有 core 实现。原生几何、连接和复制实现由 SchematicGraphCommands
组合；没有为内置功能增加另一个执行入口。剪贴板仍为同一份，复制不依赖元素是否在画布中显示。
位置记录在开始时固定 Script、对象、引脚生命期及索引版本，执行前校验整个批次。

Layout 输入改读复制的相机、拾取、二维句柄、坐标、选区和曲线工具信息。
相机、三维/二维变换、Fly 和曲线修改复用已有算法和对象状态记录；
视图只保留鼠标输入、手势编号、选项及排队选择结果。点击未选中的对象时，
先完成选择命令，再开始变换。曲线工具及变换状态不再放在 EditorState。
参照对象使用完整引用，消除变换设置中把布尔值误填为原生对象编号的问题。

EditorApplication 在原有草稿和命令队列中管理连续命令。查询准备开始参数，
`beginCommand`/`editValue`/`finishEdit` 驱动同一交互；确认记录已经预览的结果，
取消、关闭、排队后关闭、切换文档和停止模块均恢复并释放原生状态。
一次交互只提交一步 Undo，无变化保留 Redo。Schematic 预览保存在显示副本，
不改变权威数据、Dirty、历史及内容版本；显示副本也不进入自动高度缓存。
完成编号同时提供提交后的文档版本，只允许在该版本仍有效时更新原选区的索引位置。
外部提供者的预览、提交及释放失败或抛异常均验证回滚；释放先于外层提交。
配置切换不能替换活动或排队交互的实现。

Dev 模块 C 接口升为 16，旧模块须重编译；新增连续命令回调和完成编号，公开头仍无 SDK、
SDL 或 STL 依赖。独立测试模块仅包含 Editor.h，委托内置实现并注入四种失败，
验证内置和外部实现共用执行、取消及撤销流程。没有修改 SDK 或文件格式。

完整 macOS arm64/Metal Release 功能回归 **124/124**，无失败、无跳过，并与所选 CTest
清单逐项核对。Schematic 22 项、Layout 22 项、模块 62 项、应用 25 项、文件菜单 10 项实际执行。
覆盖原 GPU 稳定纹理、fallback、显式截图读回及代表性 `.NMO` 直接打开。
首次完整运行发现复制依赖已绘制的 Script；修复后也验证折叠图内复制，应用组及完整回归通过。
架构检查器 28 项通过，已迁移输入文件及方法禁止重新直接访问文档或原生查询。

十六个 SDK 仓库的提交、已有修改及未跟踪文件校验值不变，二十二项运行库与上一批一致；
GPU 呈现及传输的十一项方法和 ui/render 源码逐字节一致。性能验收继续暂缓，原门槛不变。
阶段五仍保留画布模型/导航/搜索、剩余创建和参数菜单、图替换与文件对话流程、快照及呈现适配等迁移项，
`completed_phase` 仍为三，旧头文件的间接访问也没有视为已消除。
验证记录见 [复杂图编辑与手势验证记录](baselines/gestures-macos-arm64-metal.json)。
