# Stage zero architecture checks

`coverage.json` lists the 41 source groups,
32 feature/extension targets and 36 state families. It records CTest results,
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
