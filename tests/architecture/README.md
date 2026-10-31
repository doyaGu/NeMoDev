# Stage zero architecture checks

`coverage.json` lists the 45 source groups,
32 feature/extension targets and 42 state families. It records CTest results,
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
Only the remaining Schematic/Layout/Setup/Parameter Debugger windows use the named
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
