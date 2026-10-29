#include "application/ExecutionCommands.h"
#include "core/document/Document.h"
#include "core/graph/Debugger.h"
#include "core/runtime/TextEncoding.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>

namespace vdev {
namespace {
const EditorValue &Field(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key) return request.values[i];
    throw std::logic_error("Missing execution argument");
}
EditorValue Integer(const char *key, uint64_t value) { return {EditorLiteral(key), EDITOR_INTEGER_VALUE, {}, {}, value}; }
EditorValue Real(const char *key, double value) { return {EditorLiteral(key), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, value}; }
EditorValue Object(const char *key, ObjectRef value) { return {EditorLiteral(key), EDITOR_OBJECT, value}; }
}

struct ExecutionCommands::Data {
    enum class Action { Play, Pause, TogglePlay, Reset, StepFrame, DebugStep, Leave, Execute, Trace, ToggleTrace,
        Inside, MaxFrames, SimulatedFps, FramesPerUpdate, LockObject, LockScript, ClearLog, Reengage, Redraw, Time };
    struct Command { Data *owner; Action action; const char *id, *title; };
    EditorApplication &editor;
    std::function<Document *()> current;
    std::function<bool()> nativeAvailable;
    Document *document = nullptr;
    std::unique_ptr<Debugger> debugger;
    StateData state;
    std::vector<Debugger::Entry> log;
    uint64_t logRevision = 0;
    ObjectRef lockedObject{}, lockedScript{};
    std::deque<Command> commands;
    unsigned refresh = 0;
    struct Time {
        uint64_t frameMode = 0, behaviorMode = 0;
        double frameLimit = 60, behaviorLimit = 60, scale = 1, minimum = 0, maximum = 100;
    } time;
    Data(EditorApplication &editor, std::function<Document *()> current, std::function<bool()> nativeAvailable)
        : editor(editor), current(std::move(current)), nativeAvailable(std::move(nativeAvailable)) {}
    bool Ready() const {
        if (nativeAvailable && !nativeAvailable()) return false;
        return document && debugger && current() == document && document->Context() && !document->EditOpen();
    }
    static bool NeedsDocument(Action action) {
        return action <= Action::Execute || action == Action::LockObject || action == Action::LockScript;
    }
    void ValidateLocks() {
        if (lockedScript && !document->Resolve(lockedScript, CKCID_BEHAVIOR)) {
            lockedScript = {}; lockedObject = {};
        }
        if (lockedObject && !document->Resolve(lockedObject)) lockedObject = {};
        debugger->lockedObject = lockedObject.object;
        debugger->lockedScript = lockedScript.object;
    }
    void Publish(bool readTime = false) {
        if (!debugger || !document) { state = {}; return; }
        state.document = document->Token();
        state.available = document->IsOpen();
        state.state = int(debugger->GetState());
        state.trace = debugger->trace;
        state.insideFrames = debugger->insideFrames;
        state.inFrame = debugger->InFrame();
        state.frames = debugger->Frames();
        state.reached = debugger->Reached();
        state.maxFrames = debugger->maxFrames;
        state.framesPerUpdate = debugger->framesPerUpdate;
        state.simulatedFps = debugger->simulatedFps;
        state.redrawOnlyAtBreakpoint = debugger->redrawOnlyAtBreakpoint;
        state.object = document->Reference(debugger->CurrentObject());
        state.script = document->Reference(debugger->CurrentScript());
        state.behavior = document->Reference(debugger->CurrentBehavior());
        state.lockedObject = lockedObject;
        state.lockedScript = lockedScript;
        state.logEntries = debugger->Log().size();
        state.reachedBreakpoints = debugger->ReachedBreakpoints().size();
        state.faults = debugger->Faults();
        if (logRevision != debugger->LogRevision()) {
            const auto &source = debugger->Log();
            // Append new entries once. Clear/trim changes the size by a
            // different amount than the revision and replaces the projection.
            if (source.size() >= log.size() && debugger->LogRevision() - logRevision == source.size() - log.size())
                log.insert(log.end(), source.begin() + log.size(), source.end());
            else log = source;
            logRevision = debugger->LogRevision();
        }
        state.measuredFps = 0;
        if (state.state == int(Debugger::State::Running)) {
            CKStats profile{};
            document->Context()->GetProfileStats(&profile);
            if (profile.TotalFrameTime > 0) state.measuredFps = 1000.0 / profile.TotalFrameTime;
        }
        if (readTime)
            if (auto *manager = document->Context()->GetTimeManager()) {
                const auto limits = manager->GetLimitOptions();
                time.frameMode = limits & CK_FRAMERATE_LIMIT ? 2 : limits & CK_FRAMERATE_SYNC ? 1 : 0;
                time.behaviorMode = limits & CK_BEHRATE_LIMIT ? 1 : 0;
                time.frameLimit = manager->GetFrameRateLimit();
                time.behaviorLimit = manager->GetBehavioralRateLimit();
                time.scale = manager->GetTimeScaleFactor();
                time.minimum = manager->GetMinimumDeltaTime();
                time.maximum = manager->GetMaximumDeltaTime();
            }
    }
    void Synchronize() {
        if (!Ready()) return;
        if (state.document != document->Token()) {
            debugger->Detach();
            lockedObject = {}; lockedScript = {};
            ValidateLocks();
            Publish(true);
        }
    }
    bool Valid(Action action, const EditorRequest &request) const {
        const auto boolean = [&](const char *key) { return Field(request, key).integer <= 1; };
        const auto positive = [&](const char *key) {
            const auto value = Field(request, key).real;
            return std::isfinite(value) && value > 0 && value <= std::numeric_limits<float>::max() && float(value) > 0;
        };
        switch (action) {
        case Action::Trace: case Action::Inside: case Action::Redraw: return boolean("enabled");
        case Action::MaxFrames: return Field(request, "value").integer <= uint64_t(std::numeric_limits<int>::max());
        case Action::FramesPerUpdate:
            return Field(request, "value").integer > 0 && Field(request, "value").integer <= uint64_t(std::numeric_limits<int>::max());
        case Action::SimulatedFps: return positive("value") && Field(request, "value").real >= 1;
        case Action::Reengage: return boolean("behavior") && boolean("link") && boolean("parameter");
        case Action::Time:
            return Field(request, "frame-mode").integer <= 2 && boolean("behavior-mode") && positive("frame-limit") &&
                positive("behavior-limit") && positive("scale") && positive("minimum") && positive("maximum") &&
                Field(request, "minimum").real <= Field(request, "maximum").real;
        case Action::Execute: {
            const auto list = Field(request, "objects").objects;
            for (uint32_t i = 0; i < list.count; ++i) {
                auto *script = CKBehavior::Cast(document->Resolve(list.data[i], CKCID_BEHAVIOR));
                if (!script || !(script->GetType() & CKBEHAVIORTYPE_SCRIPT)) return false;
            }
            return true;
        }
        case Action::LockScript: {
            const auto target = Field(request, "target").object;
            auto *script = target ? CKBehavior::Cast(document->Resolve(target, CKCID_BEHAVIOR)) : nullptr;
            return !target || (script && (script->GetType() & CKBEHAVIORTYPE_SCRIPT));
        }
        default: return true;
        }
    }
    EditorStatus Status(Action action, const EditorRequest &request) const {
        if (!Ready() || (NeedsDocument(action) && !document->IsOpen())) return EDITOR_REFUSED;
        if (!Valid(action, request)) return EDITOR_INVALID;
        if ((action == Action::DebugStep || action == Action::Leave) && (!debugger->trace || !debugger->insideFrames))
            return EDITOR_REFUSED;
        if (action == Action::LockObject && lockedScript) return EDITOR_REFUSED;
        if (action == Action::TogglePlay) {
            const auto *id = debugger->GetState() == Debugger::State::Running ? "runtime.pause" : "runtime.play";
            return editor.CommandState(id, {request.scope, nullptr, 0});
        }
        if (action == Action::Play && debugger->GetState() == Debugger::State::Running) return EDITOR_NO_CHANGE;
        if (action == Action::Pause && debugger->GetState() != Debugger::State::Running) return EDITOR_NO_CHANGE;
        if (action == Action::Execute && !Field(request, "objects").objects.count) return EDITOR_NO_CHANGE;
        return EDITOR_OK;
    }
    static EditorStatus Check(void *data, const EditorHost *, const EditorRequest *request) {
        const auto &command = *static_cast<Command *>(data);
        return command.owner->Status(command.action, *request);
    }
    static EditorStatus Run(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        const auto &command = *static_cast<Command *>(data);
        auto &self = *command.owner;
        self.Synchronize();
        auto &debugger = *self.debugger;
        self.ValidateLocks();
        const auto faults = debugger.Faults();
        const auto value = [&] { return Field(*request, "value").integer; };
        const auto enabled = [&] { return Field(*request, "enabled").integer != 0; };
        switch (command.action) {
        case Action::Play: debugger.Play(); break;
        case Action::Pause: debugger.Pause(); break;
        case Action::TogglePlay: {
            const auto *id = debugger.GetState() == Debugger::State::Running ? "runtime.pause" : "runtime.play";
            const auto result = self.editor.Execute(id, {request->scope, nullptr, 0});
            if (!result.message.empty()) {
                const EditorValue message{EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, {}, EditorText(result.message)};
                if (sink.write) sink.write(sink.data, &message, 1);
            }
            return result.status;
        }
        case Action::Reset: debugger.Reset(); break;
        case Action::StepFrame: debugger.StepFrame(); break;
        case Action::DebugStep: debugger.DebugStep(); break;
        case Action::Leave: debugger.LeaveBehavior(); break;
        case Action::Execute: {
            std::vector<CK_ID> scripts;
            const auto input = Field(*request, "objects").objects;
            for (uint32_t i = 0; i < input.count; ++i) scripts.push_back(input.data[i].object);
            debugger.ExecuteScripts(scripts);
            break;
        }
        case Action::Trace: debugger.trace = enabled(); break;
        case Action::ToggleTrace: debugger.trace = !debugger.trace; break;
        case Action::Inside:
            debugger.insideFrames = enabled();
            if (debugger.insideFrames) debugger.trace = true;
            break;
        case Action::MaxFrames: debugger.maxFrames = int(value()); break;
        case Action::FramesPerUpdate: debugger.framesPerUpdate = int(value()); break;
        case Action::SimulatedFps: debugger.simulatedFps = float(Field(*request, "value").real); break;
        case Action::LockObject: self.lockedObject = Field(*request, "target").object; break;
        case Action::LockScript: {
            self.lockedScript = Field(*request, "target").object;
            auto *script = self.lockedScript ? CKBehavior::Cast(self.document->Resolve(self.lockedScript)) : nullptr;
            self.lockedObject = self.document->Reference(script && script->GetOwner() ? script->GetOwner()->GetID() : 0);
            break;
        }
        case Action::ClearLog: debugger.ClearLog(); break;
        case Action::Reengage:
            debugger.reengageBehavior = Field(*request, "behavior").integer != 0;
            debugger.reengageLink = Field(*request, "link").integer != 0;
            debugger.reengageParameter = Field(*request, "parameter").integer != 0;
            break;
        case Action::Redraw: debugger.redrawOnlyAtBreakpoint = enabled(); break;
        case Action::Time: {
            auto *time = self.document->Context()->GetTimeManager();
            if (!time) return EDITOR_REFUSED;
            const CK_FRAMERATE_LIMITS modes[] = {CK_FRAMERATE_FREE, CK_FRAMERATE_SYNC, CK_FRAMERATE_LIMIT};
            time->ChangeLimitOptions(modes[Field(*request, "frame-mode").integer],
                                    Field(*request, "behavior-mode").integer ? CK_BEHRATE_LIMIT : CK_BEHRATE_SYNC);
            time->SetFrameRateLimit(float(Field(*request, "frame-limit").real));
            time->SetBehavioralRateLimit(float(Field(*request, "behavior-limit").real));
            time->SetTimeScaleFactor(float(Field(*request, "scale").real));
            time->SetMinimumDeltaTime(float(Field(*request, "minimum").real));
            time->SetMaximumDeltaTime(float(Field(*request, "maximum").real));
            break;
        }
        }
        self.ValidateLocks();
        self.Publish(command.action == Action::Time);
        if (debugger.Faults() != faults) {
            const std::string message = std::string(command.title) + " failed";
            const EditorValue field{EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, {}, EditorText(message)};
            if (sink.write) sink.write(sink.data, &field, 1);
            return EDITOR_ERROR;
        }
        return EDITOR_OK;
    }
    static EditorStatus Read(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &self = *static_cast<Data *>(data);
        const auto &s = self.state;
        const EditorValue fields[] = {
            Integer("available", s.available), Integer("state", s.state), Integer("trace", s.trace),
            Integer("inside-frames", s.insideFrames), Integer("in-frame", s.inFrame), Integer("frames", s.frames),
            Integer("reached", s.reached), Integer("max-frames", s.maxFrames), Integer("frames-per-update", s.framesPerUpdate),
            Real("simulated-fps", s.simulatedFps), Real("measured-fps", s.measuredFps),
            Integer("redraw-at-breakpoint", s.redrawOnlyAtBreakpoint), Object("object", s.object), Object("script", s.script),
            Object("behavior", s.behavior), Object("locked-object", s.lockedObject), Object("locked-script", s.lockedScript),
            Integer("log-entries", s.logEntries), Integer("reached-breakpoints", s.reachedBreakpoints), Integer("faults", s.faults)};
        return sink.write ? sink.write(sink.data, fields, uint32_t(std::size(fields))) : EDITOR_INVALID;
    }
    static EditorStatus ReadTime(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &t = static_cast<Data *>(data)->time;
        const EditorValue fields[] = {Integer("frame-mode", t.frameMode), Integer("behavior-mode", t.behaviorMode),
            Real("frame-limit", t.frameLimit), Real("behavior-limit", t.behaviorLimit), Real("scale", t.scale),
            Real("minimum", t.minimum), Real("maximum", t.maximum)};
        return sink.write ? sink.write(sink.data, fields, uint32_t(std::size(fields))) : EDITOR_INVALID;
    }
    static EditorStatus ReadLog(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        const auto &self = *static_cast<Data *>(data);
        const auto index = Field(*request, "index").integer;
        if (index >= self.log.size()) return EDITOR_INVALID;
        const auto &entry = self.log[size_t(index)];
        const auto text = AnsiToUtf8(entry.text);
        const EditorValue fields[] = {{EDITOR_TEXT("text"), EDITOR_TEXT_VALUE, {}, EditorText(text)},
                                      Object("script", entry.script), Object("behavior", entry.behavior)};
        return sink.write ? sink.write(sink.data, fields, 3) : EDITOR_INVALID;
    }
};

ExecutionCommands::ExecutionCommands(EditorApplication &editor, std::function<Document *()> current,
                                     std::function<bool()> nativeAvailable)
    : m_Data(std::make_unique<Data>(editor, std::move(current), std::move(nativeAvailable))) {}
ExecutionCommands::~ExecutionCommands() = default;
const Debugger *ExecutionCommands::Bind(Document &document) {
    m_Data->document = &document;
    m_Data->debugger = std::make_unique<Debugger>(document);
    m_Data->log.clear(); m_Data->logRevision = 0;
    m_Data->lockedObject = {}; m_Data->lockedScript = {};
    m_Data->Publish(true);
    return m_Data->debugger.get();
}
void ExecutionCommands::DocumentChanging() {
    if (!m_Data->debugger) return;
    m_Data->debugger->Detach();
    m_Data->lockedObject = {}; m_Data->lockedScript = {};
    m_Data->ValidateLocks();
    m_Data->Publish();
}
void ExecutionCommands::ReleaseDocument() {
    if (m_Data->debugger) m_Data->debugger->Detach();
}
void ExecutionCommands::Synchronize() { m_Data->Synchronize(); }
void ExecutionCommands::Stop() {
    m_Data->debugger.reset(); m_Data->document = nullptr; m_Data->state = {};
    m_Data->lockedObject = {}; m_Data->lockedScript = {};
    m_Data->log.clear(); m_Data->logRevision = 0;
}
const ExecutionCommands::StateData &ExecutionCommands::State() const { return m_Data->state; }
bool ExecutionCommands::Update(double budget) {
    auto &d = *m_Data;
    if (!d.Ready() || !d.document->IsOpen()) return false;
    d.Synchronize();
    d.ValidateLocks();
    if (d.debugger->GetState() == Debugger::State::Stopped) return false;
    const auto frames = d.debugger->Frames(), reached = d.debugger->Reached();
    const auto before = d.debugger->GetState();
    d.debugger->Update(budget);
    const bool redraw = d.debugger->redrawOnlyAtBreakpoint
        ? reached != d.debugger->Reached() || (before == Debugger::State::Running && d.debugger->GetState() != before)
        : frames != d.debugger->Frames() || ++d.refresh % 10 == 0;
    d.Publish();
    return redraw;
}

bool ExecutionCommands::Register(std::string &error) {
    using A = Data::Action;
    const EditorParameter enabled{EDITOR_TEXT("enabled"), EDITOR_INTEGER_VALUE};
    const EditorParameter integer{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE};
    const EditorParameter real{EDITOR_TEXT("value"), EDITOR_REAL_VALUE};
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT, 1};
    const EditorParameter objects{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST};
    const EditorParameter reengage[] = {{EDITOR_TEXT("behavior"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("link"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("parameter"), EDITOR_INTEGER_VALUE}};
    const EditorParameter time[] = {{EDITOR_TEXT("frame-mode"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("behavior-mode"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("frame-limit"), EDITOR_REAL_VALUE},
        {EDITOR_TEXT("behavior-limit"), EDITOR_REAL_VALUE}, {EDITOR_TEXT("scale"), EDITOR_REAL_VALUE},
        {EDITOR_TEXT("minimum"), EDITOR_REAL_VALUE}, {EDITOR_TEXT("maximum"), EDITOR_REAL_VALUE}};
    struct Description { A action; const char *id, *title; const EditorParameter *parameters = nullptr; uint32_t count = 0; };
    const Description descriptions[] = {
        {A::Play, "runtime.play", "Play"}, {A::Pause, "runtime.pause", "Pause"},
        {A::TogglePlay, "runtime.play-pause", "Play/Pause"}, {A::Reset, "runtime.reset", "Restore Initial Conditions"},
        {A::StepFrame, "runtime.step-frame", "Step One Frame"}, {A::DebugStep, "runtime.debug-step", "Debug Step"},
        {A::Leave, "runtime.leave-behavior", "Leave Behavior"}, {A::Execute, "runtime.execute-scripts", "Execute Scripts", &objects, 1},
        {A::Trace, "runtime.set-trace", "Trace Mode", &enabled, 1}, {A::ToggleTrace, "runtime.toggle-trace", "Trace Mode"},
        {A::Inside, "runtime.set-inside-frames", "Debug Inside Frames", &enabled, 1},
        {A::MaxFrames, "runtime.set-max-frames", "Max CK Frames", &integer, 1},
        {A::SimulatedFps, "runtime.set-simulated-fps", "Simulated FPS", &real, 1},
        {A::FramesPerUpdate, "runtime.set-frames-per-update", "Frames Per Update", &integer, 1},
        {A::LockObject, "runtime.lock-object", "Lock Debug Object", &target, 1},
        {A::LockScript, "runtime.lock-script", "Lock Debug Script", &target, 1},
        {A::ClearLog, "runtime.clear-log", "Clear Debug Log"},
        {A::Reengage, "runtime.set-breakpoint-reengagement", "Breakpoint Auto Reengage", reengage, 3},
        {A::Redraw, "runtime.set-redraw-at-breakpoint", "Redraw Schematic At Breakpoint", &enabled, 1},
        {A::Time, "runtime.set-time", "Time Settings", time, 7}};
    std::vector<EditorCommandDesc> commands;
    for (const auto &description : descriptions) {
        m_Data->commands.push_back({m_Data.get(), description.action, description.id, description.title});
        auto &command = m_Data->commands.back();
        commands.push_back({sizeof(EditorCommandDesc), EditorLiteral(command.id), EDITOR_TEXT("builtin.execution"),
            EditorLiteral(command.title), description.parameters, description.count, &Data::Run, &command,
            Data::NeedsDocument(command.action) ? EDITOR_DOCUMENT : EDITOR_APPLICATION, EDITOR_RUNTIME_CONTROL, &Data::Check});
    }
    const EditorParameter stateFields[] = {
        {EDITOR_TEXT("available"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("state"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("trace"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("inside-frames"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("in-frame"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("frames"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("reached"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("max-frames"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("frames-per-update"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("simulated-fps"), EDITOR_REAL_VALUE},
        {EDITOR_TEXT("measured-fps"), EDITOR_REAL_VALUE}, {EDITOR_TEXT("redraw-at-breakpoint"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("object"), EDITOR_OBJECT}, {EDITOR_TEXT("script"), EDITOR_OBJECT}, {EDITOR_TEXT("behavior"), EDITOR_OBJECT},
        {EDITOR_TEXT("locked-object"), EDITOR_OBJECT}, {EDITOR_TEXT("locked-script"), EDITOR_OBJECT},
        {EDITOR_TEXT("log-entries"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("reached-breakpoints"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("faults"), EDITOR_INTEGER_VALUE}};
    const EditorParameter index{EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE};
    const EditorParameter logFields[] = {{EDITOR_TEXT("text"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("script"), EDITOR_OBJECT},
                                        {EDITOR_TEXT("behavior"), EDITOR_OBJECT}};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("runtime.state"), EDITOR_TEXT("builtin.execution"), nullptr, 0,
            &Data::Read, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, stateFields, uint32_t(std::size(stateFields))},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("runtime.time"), EDITOR_TEXT("builtin.execution"), nullptr, 0,
            &Data::ReadTime, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, time, 7},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("runtime.log"), EDITOR_TEXT("builtin.execution"), &index, 1,
            &Data::ReadLog, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, logFields, 3}};
    std::deque<std::string> chords;
    std::vector<EditorBindingDesc> bindings;
    const auto bind = [&](const char *action, std::string chord) {
        chords.push_back(std::move(chord)); bindings.push_back({EditorLiteral(action), EditorText(chords.back())});
    };
    for (int super = 0; super < 2; ++super) {
        const std::string prefix = super ? "Super+" : "";
        bind("runtime.play-pause", prefix + "Ctrl+Space");
        bind("runtime.step-frame", prefix + "Alt+Space");
        bind("runtime.step-frame", prefix + "Ctrl+Alt+Space");
        bind("runtime.reset", prefix + "Alt+Backspace");
        for (int extra = 0; extra < 4; ++extra) {
            const auto modifiers = prefix + "Ctrl+" + (extra & 1 ? "Alt+" : "") + (extra & 2 ? "Shift+" : "");
            bind("runtime.reset", modifiers + "Backspace");
            bind("runtime.toggle-trace", modifiers + "T");
        }
    }
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("runtime.execution");
    module.commands = commands.data(); module.commandCount = uint32_t(commands.size());
    module.queries = queries; module.queryCount = uint32_t(std::size(queries));
    module.bindings = bindings.data(); module.bindingCount = uint32_t(bindings.size());
    return m_Data->editor.Register(module, error);
}
} // namespace vdev
