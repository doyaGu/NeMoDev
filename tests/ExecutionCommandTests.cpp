#include "application/Preferences.h"
#include "application/ExecutionCommands.h"
#include "application/ObjectSelection.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/graph/Debugger.h"
#include "core/graph/TopologyCommands.h"
#include "core/layout/LayoutCommands.h"
#include "core/level/LevelCommands.h"
#include "editor/ImGuiDrawing.h"
#include "features/objects/ObjectCommands.h"
#include "CKDebugContext.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
EditorValue Integer(const char *key, uint64_t value) { return {EditorLiteral(key), EDITOR_INTEGER_VALUE, {}, {}, value}; }
EditorValue Real(const char *key, double value) { return {EditorLiteral(key), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, value}; }
struct Frames : CKBaseManager {
    bool fail = false, slow = false;
    int calls = 0;
    explicit Frames(CKContext *context) : CKBaseManager(context, CKGUID(0x73f19462, 0x6cb31271), "Execution command frames") {}
    CKDWORD GetValidFunctionsMask() override { return CKMANAGER_FUNC_PreProcess; }
    CKERROR PreProcess() override {
        ++calls;
        if (fail) { fail = false; throw std::runtime_error("Injected execution failure"); }
        if (slow) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return CK_OK;
    }
};
}

void TestDebugFrameLifecycle(vdev::CkRuntimeHost &host) {
    using namespace vdev;
    for (const auto *ending : {"document-change", "release", "bind", "stop"}) {
        Document document(host);
        std::string error;
        Check(document.New(&error), error.c_str());
        const auto entity = CreateLayoutObject(document, LayoutObject::Frame3D);
        const auto script = CreateScript(document, entity.created, "Debug lifecycle");
        Check(entity.ok && script.ok, "cannot prepare debug lifecycle script");
        auto *context = document.Context();
        auto *behavior = CKBehavior::Cast(context->GetObject(script.created));
        context->GetCurrentLevel()->GetLevelScene()->SetObjectFlags(behavior,
            CK_SCENEOBJECT_FLAGS(CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_ACTIVE));
        struct Counts { int starts = 0, ends = 0; bool clearWhileDebugging = false; };
        auto counts = std::make_shared<Counts>();
        struct Probe : CKBaseManager {
            std::shared_ptr<Counts> counts;
            Probe(CKContext *context, std::shared_ptr<Counts> counts)
                : CKBaseManager(context, CKGUID(0x190de561, 0x643b997a), "Debug frame lifecycle"), counts(counts) {}
            CKDWORD GetValidFunctionsMask() override { return CKMANAGER_FUNC_PreProcess | CKMANAGER_FUNC_PostProcess | CKMANAGER_FUNC_PreClearAll; }
            CKERROR PreProcess() override { ++counts->starts; return CK_OK; }
            CKERROR PostProcess() override { ++counts->ends; return CK_OK; }
            CKERROR PreClearAll() override { counts->clearWhileDebugging |= m_Context->GetDebugContext()->InDebug != 0; return CK_OK; }
        };
        auto *probe = new Probe(context, counts);
        Check(context->RegisterNewManager(probe) == CK_OK, "cannot register debug lifecycle probe");
        EditorApplication editor([&] { return &document; });
        Preferences preferences;
        ExecutionCommands commands(editor, [&] { return &document; }, [] { return true; }, preferences);
        Check(commands.Register(error) && editor.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
        const auto *debugger = commands.Bind(document);
        editor.Attach(document.Token(), document.TopologyRevision(), true, true);
        const auto execute = [&](const char *id, std::vector<EditorValue> values = {}) {
            return editor.Execute(id, {editor.Scope(), values.data(), uint32_t(values.size())}).status;
        };
        Check(execute("runtime.set-inside-frames", {Integer("enabled", 1)}) == EDITOR_OK &&
              execute("runtime.play") == EDITOR_OK && execute("runtime.pause") == EDITOR_OK &&
              execute("runtime.debug-step") == EDITOR_OK && debugger->InFrame() && context->GetDebugContext()->InDebug,
              "debug lifecycle fixture did not stop inside a native frame");
        Check(counts->starts == 1 && counts->ends == 0, "debug frame ended before its lifecycle transition");
        if (std::string(ending) == "document-change") commands.DocumentChanging();
        else if (std::string(ending) == "release") commands.ReleaseDocument();
        else if (std::string(ending) == "bind") commands.Bind(document);
        else commands.Stop();
        Check(counts->ends == 1 && !context->GetDebugContext()->InDebug && !context->IsPlaying(),
              (std::string("native debug frame survived ") + ending).c_str());
        commands.Stop();
        document.Close();
        Check(counts->starts == counts->ends && !counts->clearWhileDebugging, "document closed before the native debug frame ended");
        delete probe;
    }
}

void TestExecutionCommands(vdev::CkRuntimeHost &host, const std::string &modulePath) {
    using namespace vdev;
    Document document(host);
    std::string error;
    Check(document.New(&error), error.c_str());
    bool native = true;
    EditorApplication editor([&] { return &document; }, [&] { return native; });
    Preferences preferences;
    ExecutionCommands commands(editor, [&] { return &document; }, [&] { return native; }, preferences);
    ObjectCommands objects([&] { return &document; });
    Check(commands.Register(error) && objects.Register(editor, error) && editor.Load(modulePath, error), error.c_str());
    auto config = EditorConfiguration{};
    config.providers = {{"command", "runtime.play", "example.execution"},
                        {"command", "object.rename", "builtin.objects"},
                        {"command", "object.create-frame3d", "builtin.objects"}};
    Check(editor.Activate(EDITOR_IMGUI_VERSION, error, config), error.c_str());
    const auto *debugger = commands.Bind(document);
    const auto attach = [&] { editor.Attach(document.Token(), document.TopologyRevision(), true, true); };
    const auto execute = [&](const char *id, std::vector<EditorValue> values = {}) {
        return editor.Execute(id, {editor.Scope(), values.data(), uint32_t(values.size())});
    };
    const auto read = [&](const char *id = "runtime.state", std::vector<EditorValue> values = {}) {
        const auto result = editor.Query(id, {editor.Scope(), values.data(), uint32_t(values.size())});
        Check(result.status == EDITOR_OK, (std::string("runtime projection unavailable: ") + id).c_str());
        return result;
    };
    const auto field = [&](const char *key) {
        for (const auto &value : read().fields) if (value.key == key) return value;
        throw std::runtime_error("runtime field missing");
    };
    const auto frame = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(frame.ok, "create runtime owner failed");
    const auto owner = document.Reference(frame.created);
    const auto script = CreateScript(document, frame.created, "Execution Script");
    Check(script.ok, "create runtime Script failed");
    const auto scriptRef = document.Reference(script.created);
    const auto graph = CreateBehaviorGraph(document, script.created, script.created, "Not a Script", Point(100, 100));
    Check(graph.ok, "create runtime graph failed");
    document.History().MarkSavePoint(); document.MarkSaved();
    ObjectSelection selection;
    selection.Restore(document, {frame.created});
    const auto selected = selection.References();
    const auto history = document.History().UndoCount();
    const auto token = document.Token();
    attach();
    auto *frames = new Frames(document.Context());
    Check(document.Context()->RegisterNewManager(frames) == CK_OK, "cannot register frame probe");
    const auto unchanged = [&] {
        Check(document.Token() == token && !document.EditOpen() &&
              document.History().UndoCount() == history && selection.References() == selected,
              "runtime control changed document identity, selection or undo history");
    };
    Check(execute("runtime.set-max-frames", {Integer("value", 2)}).status == EDITOR_OK, "frame limit refused");
    Check(execute("runtime.play-pause").status == EDITOR_OK && debugger->GetState() == Debugger::State::Running,
          "built-in toggle did not invoke configured Play");
    Check(read("example.play-count").fields.front().integer == 1, "configured Play provider was bypassed");
    commands.Update(0.01); commands.Update(0.01);
    Check(debugger->Frames() == 2 && debugger->GetState() == Debugger::State::Paused && !document.Context()->IsPlaying(),
          "bounded play changed frame or pause behavior");
    Check(field("frames").integer == 2 && field("state").integer == EDITOR_EXECUTION_PAUSED, "runtime state projection is stale");
    Check(execute("runtime.pause").status == EDITOR_NO_CHANGE, "pause did not retain no-change behavior");
    Check(execute("runtime.step-frame").status == EDITOR_OK && debugger->Frames() == 3, "one-frame command changed frame count");
    Check(!document.Dirty(), "playing or stepping changed the document save point");
    Check(execute("runtime.reset").status == EDITOR_OK && debugger->GetState() == Debugger::State::Stopped &&
          debugger->Frames() == 0 && document.Dirty(), "reset changed its existing topology-refresh behavior");
    unchanged();
    // Settings use finite numeric values; malformed values cannot partially
    // update either the debugger or the CK time manager.
    Check(execute("runtime.set-simulated-fps", {Real("value", 144.125)}).status == EDITOR_OK &&
          field("simulated-fps").real == 144.125, "real-valued setting was rounded or lost");
    for (double value : {0.0, -1.0, 0.5, std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::max()})
        Check(execute("runtime.set-simulated-fps", {Real("value", value)}).status == EDITOR_INVALID &&
              debugger->simulatedFps == 144.125f, "invalid floating value reached the engine");
    Check(execute("runtime.set-trace", {Integer("enabled", 2)}).status == EDITOR_INVALID, "invalid boolean accepted");
    Check(execute("runtime.set-max-frames", {Integer("value", uint64_t(-1))}).status == EDITOR_INVALID,
          "frame-limit overflow accepted");
    const std::vector<EditorValue> time = {Integer("frame-mode", 0), Integer("behavior-mode", 0),
        Real("frame-limit", 120.5), Real("behavior-limit", 60.25), Real("scale", 0.5), Real("minimum", 1), Real("maximum", 200)};
    Check(execute("runtime.set-time", time).status == EDITOR_OK, "time settings refused");
    const auto timeBefore = read("runtime.time");
    Check(timeBefore.fields[2].real == 120.5 && timeBefore.fields[4].real == 0.5, "time projection lost numeric values");
    auto invalidTime = time; invalidTime.back().real = -1;
    Check(execute("runtime.set-time", invalidTime).status == EDITOR_INVALID &&
          read("runtime.time").fields[2].real == 120.5, "time validation allowed a partial write");
    auto timeWithFps = time;
    auto hideFps = Integer("hide-fps", 1);
    timeWithFps.push_back({EDITOR_TEXT("display"), EDITOR_RECORD, {}, {}, 0, {}, {}, {}, 0, {}, {&hideFps, 1}});
    Check(execute("runtime.set-time", timeWithFps).status == EDITOR_OK && preferences.Bool("Time Settings\\Hide Fps") &&
              read("runtime.time").fields.back().fields.front().integer == 1, "time command did not own its display preference");
    auto rejected = timeWithFps;
    rejected[4].real = -1;
    hideFps.integer = 0;
    Check(execute("runtime.set-time", rejected).status == EDITOR_INVALID && preferences.Bool("Time Settings\\Hide Fps"),
          "invalid time input changed the display preference");
    rejected = timeWithFps;
    hideFps.integer = 2;
    Check(execute("runtime.set-time", rejected).status == EDITOR_INVALID && preferences.Bool("Time Settings\\Hide Fps"),
          "non-boolean display preference reached time settings");
    hideFps.key = EditorString EDITOR_TEXT("unknown");
    Check(execute("runtime.set-time", timeWithFps).status == EDITOR_INVALID, "unknown time display field was accepted");
    hideFps.key = EditorString EDITOR_TEXT("hide-fps");
    hideFps.type = EDITOR_TEXT_VALUE;
    Check(execute("runtime.set-time", timeWithFps).status == EDITOR_INVALID, "non-boolean time display field was accepted");
    hideFps.type = EDITOR_INTEGER_VALUE;
    hideFps.integer = 0;
    editor.EnterDraw();
    const auto queuedTime = execute("runtime.set-time", timeWithFps);
    editor.LeaveDraw();
    native = false;
    editor.EndFrame();
    native = true;
    Check(queuedTime.status == EDITOR_QUEUED && preferences.Bool("Time Settings\\Hide Fps"),
          "rejected queued time command changed display preferences");
    Check(execute("runtime.set-time", time).status == EDITOR_OK && preferences.Bool("Time Settings\\Hide Fps"),
          "time commands without display preferences reset the saved setting");
    Check(execute("runtime.debug-step").status == EDITOR_REFUSED, "debug step admitted without Trace/Inside Frames");
    Check(execute("runtime.set-inside-frames", {Integer("enabled", 1)}).status == EDITOR_OK && debugger->insideFrames &&
          debugger->trace && field("trace").integer == 1, "Inside Frames did not enable Trace");
    Check(execute("runtime.debug-step").status == EDITOR_OK, "traced debug step refused");
    execute("runtime.reset");
    execute("runtime.set-inside-frames", {Integer("enabled", 0)});
    execute("runtime.set-trace", {Integer("enabled", 0)});
    // Every script in a batch is validated before any native execution.
    std::vector<ObjectRef> scripts{scriptRef, document.Reference(graph.created)};
    const EditorValue scriptList{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {scripts.data(), uint32_t(scripts.size())}};
    const auto logBefore = debugger->Log().size();
    Check(execute("runtime.execute-scripts", {scriptList}).status == EDITOR_INVALID && debugger->Log().size() == logBefore,
          "invalid script batch executed its first item");
    scripts.resize(1);
    const EditorValue validScripts{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {scripts.data(), 1}};
    Check(execute("runtime.execute-scripts", {validScripts}).status == EDITOR_OK && debugger->Log().size() > logBefore,
          "script execution command failed");
    const auto logIndex = debugger->Log().size() - 1;
    Check(read("runtime.log", {Integer("index", logIndex)}).fields[1].object == scriptRef,
          "runtime log did not retain object identity");
    Check(execute("runtime.lock-script", {{EDITOR_TEXT("target"), EDITOR_OBJECT, scriptRef}}).status == EDITOR_OK &&
          debugger->lockedScript == script.created && debugger->lockedObject == frame.created,
          "script lock did not lock its owner");
    Check(execute("runtime.lock-object").status == EDITOR_REFUSED, "object lock changed a locked Script's owner");
    Check(execute("runtime.lock-script").status == EDITOR_OK && !debugger->lockedObject && !debugger->lockedScript,
          "unlock did not release both locks");
    Check(execute("runtime.lock-object", {{EDITOR_TEXT("target"), EDITOR_OBJECT, owner}}).status == EDITOR_OK, "object lock failed");
    unchanged();
    // No native access is performed by these projections while file work owns
    // the context. Mutations and advancement stay refused until it returns.
    native = false;
    Check(read().status == EDITOR_OK && read("runtime.time").status == EDITOR_OK &&
          read("runtime.log", {Integer("index", logIndex)}).status == EDITOR_OK, "busy runtime lost read-only state");
    Check(execute("runtime.play").status == EDITOR_REFUSED && !commands.Update(0.01), "busy runtime executed native code");
    native = true;
    {
        Edit draft(document, "Runtime admission");
        Check(execute("runtime.play").status == EDITOR_REFUSED && !commands.Update(0.01), "runtime ran through an open edit");
    }
    EditorStatus threadStatus = EDITOR_OK;
    std::thread wrong([&] { threadStatus = execute("runtime.play").status; }); wrong.join();
    Check(threadStatus == EDITOR_REFUSED, "wrong-thread runtime control admitted");
    // Deferred option values are copied. A closed view cannot execute queued
    // runtime controls after its scope has disappeared.
    const auto view = editor.OpenScope();
    EditorValue copied = Real("value", 240.25);
    editor.EnterDraw();
    auto queued = editor.Execute("runtime.set-simulated-fps", {editor.Scope(view), &copied, 1});
    editor.LeaveDraw(); copied.real = 33;
    editor.EndFrame();
    Check(queued.status == EDITOR_QUEUED && debugger->simulatedFps == 240.25f, "deferred real argument was borrowed");
    editor.EnterDraw();
    queued = editor.Execute("runtime.play", {editor.Scope(view), nullptr, 0});
    editor.LeaveDraw(); editor.CloseScope(view); editor.EndFrame();
    Check(debugger->GetState() == Debugger::State::Paused || debugger->GetState() == Debugger::State::Stopped,
          "closed view started playback");
    // Native failure is a failed command, while authoring state stays intact.
    execute("runtime.reset");
    const auto failures = field("faults").integer;
    frames->fail = true;
    Check(execute("runtime.step-frame").status == EDITOR_ERROR && field("faults").integer > failures,
          "native exception was reported as a successful command");
    execute("runtime.reset");
    unchanged();
    // The existing per-update time budget still limits repeated full frames.
    frames->slow = true;
    execute("runtime.set-max-frames", {Integer("value", 0)});
    execute("runtime.set-frames-per-update", {Integer("value", 100)});
    execute("runtime.play");
    const auto before = frames->calls;
    commands.Update(0.001);
    execute("runtime.pause"); frames->slow = false;
    Check(frames->calls == before + 1 && debugger->Frames() == 1, "controller bypassed the frame time budget");
    execute("runtime.reset");
    // A same-ID replacement cannot inherit a pending control, an existing
    // debug lock or a log entry's original script identity.
    auto disposable = CreateLayoutObject(document, LayoutObject::Frame3D);
    const auto stale = document.Reference(disposable.created);
    attach();
    Check(execute("runtime.lock-object", {{EDITOR_TEXT("target"), EDITOR_OBJECT, stale}}).status == EDITOR_OK, "lock setup failed");
    editor.EnterDraw();
    queued = execute("runtime.lock-object", {{EDITOR_TEXT("target"), EDITOR_OBJECT, stale}});
    editor.LeaveDraw();
    document.Context()->DestroyObject(stale.object, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID);
    auto *replacement = document.Context()->CreateObject(CKCID_3DENTITY, (CKSTRING) "Lock replacement");
    Check(replacement && replacement->GetID() == stale.object, "native lock ID was not reused");
    editor.EndFrame(); commands.Update(0.01);
    Check(!debugger->lockedObject && !field("locked-object").object,
          "a debug lock or its projection migrated to a same-ID replacement");
    execute("runtime.execute-scripts", {validScripts});
    const auto retained = debugger->Log().size() - 1;
    document.Context()->DestroyObject(scriptRef.object, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID);
    CKObject *newScript = nullptr;
    for (int i = 0; i < 256 && !newScript; ++i) {
        auto *created = document.Context()->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Log replacement");
        Check(created, "log replacement creation failed");
        if (created->GetID() == scriptRef.object) newScript = created;
    }
    Check(newScript, "native script ID was not reused");
    Check(debugger->Log()[retained].script == scriptRef && !document.Resolve(debugger->Log()[retained].script),
          "runtime log retargeted a replacement");
    // The published log follows appends, the bounded-history trim, and clear
    // without exposing old entries while a file worker owns execution.
    for (int i = 0; i < 2001; ++i)
        Check(execute("runtime.reset").status == EDITOR_OK, "log retention setup failed");
    const auto retainedCount = field("log-entries").integer;
    Check(retainedCount > 0 && retainedCount <= 2000 &&
          read("runtime.log", {Integer("index", retainedCount - 1)}).fields[0].text == "Reset", "trimmed log projection is stale");
    const EditorValue outsideLog = Integer("index", retainedCount);
    Check(editor.Query("runtime.log", {editor.Scope(), &outsideLog, 1}).status == EDITOR_INVALID,
          "trimmed log exposed an evicted entry");
    Check(execute("runtime.clear-log").status == EDITOR_OK && !field("log-entries").integer,
          "cleared log projection retained entries");
    const auto oldRevision = field("log-revision").integer;
    Check(execute("runtime.reset").status == EDITOR_OK && field("log-entries").integer == 1 &&
          read("runtime.log", {Integer("index", 0)}).fields[0].text == "Reset", "log append after clear failed");
    const EditorValue oldLog[] = {Integer("index", 0), Integer("revision", oldRevision)};
    Check(editor.Query("runtime.log", {editor.Scope(), oldLog, 2}).status == EDITOR_STALE,
          "old log row silently read a newer entry at the same index");
    commands.DocumentChanging();
    Check(document.New(&error), error.c_str());
    attach(); commands.Synchronize();
    Check(commands.State().document == document.Token() && !commands.State().lockedObject && debugger->Log().empty(),
          "execution state crossed document replacement");
    commands.Stop();
    Check(execute("runtime.play").status == EDITOR_REFUSED && !commands.State().available, "stopped owner accepted work");
}
