#include "application/ExecutionCommands.h"
#include "application/DocumentFiles.h"
#include "application/Preferences.h"
#include "core/document/Document.h"
#include "core/graph/Debugger.h"
#include "core/graph/RuntimeProfile.h"
#include "core/runtime/TextEncoding.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <deque>
#include <limits>
#include <stdexcept>
#include <set>
#include <tuple>

namespace vdev {
namespace {
const EditorValue &Field(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key) return request.values[i];
    throw std::logic_error("Missing execution argument");
}
uint64_t OptionalInteger(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key) return request.values[i].integer;
    return 0;
}
EditorFields Record(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key) return request.values[i].fields;
    return {};
}
EditorValue Integer(const char *key, uint64_t value) { return {EditorLiteral(key), EDITOR_INTEGER_VALUE, {}, {}, value}; }
EditorValue Real(const char *key, double value) { return {EditorLiteral(key), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, value}; }
EditorValue Object(const char *key, ObjectRef value) { return {EditorLiteral(key), EDITOR_OBJECT, value}; }
}

struct ExecutionCommands::Data {
    enum class Action { Play, Pause, TogglePlay, Reset, StepFrame, DebugStep, Leave, Execute, Trace, ToggleTrace,
        Inside, MaxFrames, SimulatedFps, FramesPerUpdate, LockObject, LockScript, ClearLog, Reengage, Redraw, Time,
        Profile, RefreshProfile };
    struct Command { Data *owner; Action action; const char *id, *title; };
    EditorApplication &editor;
    std::function<Document *()> current;
    Preferences &preferences;
    Document *document = nullptr;
    std::unique_ptr<Debugger> debugger;
    StateData state;
    uint64_t stateRevision = 1;
    std::vector<Debugger::Entry> log;
    uint64_t logRevision = 0;
    ObjectRef lockedObject{}, lockedScript{};
    std::deque<Command> commands;
    unsigned refresh = 0;
    bool synchronizeAfterRelease = false;
    // Profiling follows the lifetime of view scopes, including provider changes
    // and closes while a file worker owns the context. Only Synchronize touches CK.
    std::set<EditorView> profilers;
    bool profilingApplied = false, profilingWasEnabled = false;
    RuntimeProfile profile;
    bool profileAvailable = false, profileDetails = false, geometryReady = false;
    uint64_t profileRevision = 0, faces = 0, vertices = 0;
    std::chrono::steady_clock::time_point profileTime{};
    DocumentToken profileDocument{};
    std::vector<ObjectRef> contextObjects, contextScripts, contextOwners;
    std::vector<std::string> objectNames, scriptNames;
    std::string behaviorName;
    std::tuple<uint64_t, uint64_t, uint64_t> contextVersion{};
    uint64_t contextRevision = 1;
    struct Reached { ObjectRef script{}, object{}, behavior{}; };
    std::vector<Reached> reached;
    struct Time {
        uint64_t frameMode = 0, behaviorMode = 0;
        double frameLimit = 60, behaviorLimit = 60, scale = 1, minimum = 0, maximum = 100;
    } time;
    uint64_t timeRevision = 1;
    bool hideFps = false;
    Data(EditorApplication &editor, std::function<Document *()> current,
         Preferences &preferences)
        : editor(editor), current(std::move(current)), preferences(preferences) {}
    bool Ready() const {
        if (!editor.NativeAvailable()) return false;
        return document && debugger && current() == document && document->Context() && !document->EditOpen();
    }
    void ClearPublished() {
        state = {}; ++stateRevision;
        lockedObject = {}; lockedScript = {};
        log.clear(); logRevision = 0; reached.clear();
        contextObjects.clear(); contextScripts.clear(); contextOwners.clear();
        objectNames.clear(); scriptNames.clear(); behaviorName.clear();
        contextVersion = {}; ++contextRevision;
        profile = {}; profileAvailable = profileDetails = geometryReady = false;
        profileDocument = {}; ++profileRevision;
    }
    void RestoreProfiling() {
        if (profilingApplied) {
            document->Context()->EnableProfiling(profilingWasEnabled ? TRUE : FALSE);
            profilingApplied = false;
        }
    }
    void ProfileViews() {
        for (auto it = profilers.begin(); it != profilers.end();)
            if (editor.Scope(*it).view != *it) it = profilers.erase(it);
            else ++it;
        if (!Ready()) return;
        if (profileDocument != document->Token()) {
            profile = {}; profileAvailable = profileDetails = geometryReady = false;
            profileDocument = document->Token(); ++profileRevision;
        }
        if (profilers.empty()) RestoreProfiling();
        else if (!profilingApplied && document->IsOpen()) {
            profilingWasEnabled = document->Context()->IsProfilingEnable() != 0;
            document->Context()->EnableProfiling(TRUE);
            profilingApplied = true;
        }
    }
    void ReadProfile(bool details) {
        if (!Ready() || !document->IsOpen()) return;
        details = details || profileDetails;
        const auto now = std::chrono::steady_clock::now();
        if (profileAvailable && geometryReady && (!details || profileDetails) &&
            now - profileTime < std::chrono::milliseconds(250)) return;
        auto next = ReadRuntimeProfile(*document, details);
        if (!geometryReady) {
            ReadRuntimeGeometry(*document, faces, vertices);
            geometryReady = true;
        }
        next.faces = faces; next.vertices = vertices;
        profile = std::move(next); profileTime = now;
        profileAvailable = true; profileDetails = details; ++profileRevision;
    }
    void ReadContext() {
        if (!Ready()) return;
        const auto version = std::make_tuple(document->Token().value, editor.Revision(), document->ScriptsRevision());
        if (contextVersion == version) return;
        contextObjects.clear(); contextScripts.clear(); contextOwners.clear();
        objectNames.clear(); scriptNames.clear();
        for (const auto &script : document->Scripts()) {
            if (!document->Resolve(script.reference)) continue;
            contextScripts.push_back(script.reference);
            contextOwners.push_back(script.ownerReference);
            scriptNames.push_back(script.name.empty() ? "<Unnamed>" : AnsiToUtf8(script.name));
            if (script.ownerReference && document->Resolve(script.ownerReference) &&
                std::find(contextObjects.begin(), contextObjects.end(), script.ownerReference) == contextObjects.end()) {
                contextObjects.push_back(script.ownerReference);
                objectNames.push_back(script.ownerName.empty() ? "<Unnamed>" : AnsiToUtf8(script.ownerName));
            }
        }
        contextVersion = version;
        ++contextRevision;
    }
    static bool NeedsDocument(Action action) {
        return action <= Action::Execute || action == Action::LockObject || action == Action::LockScript || action == Action::RefreshProfile;
    }
    bool ValidateLocks() {
        const auto previousObject = lockedObject, previousScript = lockedScript;
        if (lockedScript && !document->Resolve(lockedScript, CKCID_BEHAVIOR)) {
            lockedScript = {}; lockedObject = {};
        }
        if (lockedObject && !document->Resolve(lockedObject)) lockedObject = {};
        debugger->lockedObject = lockedObject.object;
        debugger->lockedScript = lockedScript.object;
        return lockedObject != previousObject || lockedScript != previousScript;
    }
    void Publish(bool readTime = false) {
        ++stateRevision;
        if (readTime) ++timeRevision;
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
        state.logRevision = debugger->LogRevision();
        state.reachedBreakpoints = debugger->ReachedBreakpoints().size();
        state.faults = debugger->Faults();
        const auto previousName = behaviorName;
        behaviorName.clear();
        if (auto *behavior = document->Resolve(state.behavior))
            behaviorName = behavior->GetName() && *behavior->GetName() ? AnsiToUtf8(behavior->GetName()) : "<Unnamed>";
        if (behaviorName != previousName) ++contextRevision;
        reached.clear();
        for (const auto &breakpoint : debugger->ReachedBreakpoints()) {
            auto *target = document->Resolve(breakpoint.object);
            CKBehavior *focus = CKBehavior::Cast(target);
            if (auto *input = CKParameterIn::Cast(target)) focus = CKBehavior::Cast(input->GetOwner());
            else if (auto *value = CKParameter::Cast(target)) {
                auto *owner = value->GetOwner();
                if (auto *operation = CKParameterOperation::Cast(owner)) owner = operation->GetOwner();
                focus = CKBehavior::Cast(owner);
            } else if (auto *link = CKBehaviorLink::Cast(target))
                focus = link->GetInBehaviorIO() ? link->GetInBehaviorIO()->GetOwner() : nullptr;
            reached.push_back({breakpoint.scriptRef, breakpoint.object, document->Reference(focus ? focus->GetID() : 0)});
        }
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
        ProfileViews();
        // Attach publishes the absence of a document even when native access
        // is disabled. Clear copied data without entering a worker's context.
        if (!editor.Scope().document.value && (state.document.value || profileDocument.value)) {
            ClearPublished();
            return;
        }
        if (!Ready()) return;
        if (state.document != document->Token() || synchronizeAfterRelease) {
            if (!synchronizeAfterRelease) debugger->Detach();
            synchronizeAfterRelease = false;
            lockedObject = {}; lockedScript = {};
            ValidateLocks();
            Publish(true);
        }
        ReadContext();
    }
    bool Valid(Action action, const EditorRequest &request) const {
        const auto boolean = [&](const char *key) { return Field(request, key).integer <= 1; };
        const auto positive = [&](const char *key) {
            const auto value = Field(request, key).real;
            return std::isfinite(value) && value > 0 && value <= std::numeric_limits<float>::max() && float(value) > 0;
        };
        switch (action) {
        case Action::Trace: case Action::Inside: case Action::Redraw: case Action::Profile: return boolean("enabled");
        case Action::MaxFrames: return Field(request, "value").integer <= uint64_t(std::numeric_limits<int>::max());
        case Action::FramesPerUpdate:
            return Field(request, "value").integer > 0 && Field(request, "value").integer <= uint64_t(std::numeric_limits<int>::max());
        case Action::SimulatedFps: return positive("value") && Field(request, "value").real >= 1;
        case Action::Reengage: return boolean("behavior") && boolean("link") && boolean("parameter");
        case Action::Time: {
            const auto display = Record(request, "display");
            if (display.count > 1 || (display.count &&
                (std::string(display.data[0].key.data, display.data[0].key.size) != "hide-fps" ||
                 display.data[0].type != EDITOR_INTEGER_VALUE || display.data[0].integer > 1))) return false;
            return Field(request, "frame-mode").integer <= 2 && boolean("behavior-mode") && positive("frame-limit") &&
                positive("behavior-limit") && positive("scale") && positive("minimum") && positive("maximum") &&
                Field(request, "minimum").real <= Field(request, "maximum").real;
        }
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
        if (action == Action::Profile) {
            if (!request.scope.view) return EDITOR_REFUSED;
            if (!Valid(action, request)) return EDITOR_INVALID;
            return bool(profilers.count(request.scope.view)) == bool(Field(request, "enabled").integer)
                ? EDITOR_NO_CHANGE : EDITOR_OK;
        }
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
        if (command.action == Action::Profile) {
            if (Field(*request, "enabled").integer) self.profilers.insert(request->scope.view);
            else self.profilers.erase(request->scope.view);
            return EDITOR_OK;
        }
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
        case Action::Redraw:
            debugger.redrawOnlyAtBreakpoint = enabled();
            self.preferences.redrawOnlyAtBreakpoint = debugger.redrawOnlyAtBreakpoint;
            break;
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
            const auto display = Record(*request, "display");
            if (display.count) self.preferences.SetBool("Time Settings\\Hide Fps", display.data[0].integer != 0);
            break;
        }
        case Action::RefreshProfile: self.geometryReady = false; break;
        case Action::Profile: break;
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
            Integer("log-entries", s.logEntries), Integer("log-revision", s.logRevision),
            Integer("reached-breakpoints", s.reachedBreakpoints), Integer("faults", s.faults)};
        return sink.write ? sink.write(sink.data, fields, uint32_t(std::size(fields))) : EDITOR_INVALID;
    }
    static uint64_t TimeRevision(void *data, const EditorHost *) {
        auto &self = *static_cast<Data *>(data);
        const auto hideFps = self.preferences.Bool("Time Settings\\Hide Fps");
        if (hideFps != self.hideFps) {
            self.hideFps = hideFps;
            ++self.timeRevision;
        }
        return self.timeRevision;
    }
    static EditorStatus ReadTime(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &t = static_cast<Data *>(data)->time;
        const auto hideFps = Integer("hide-fps", static_cast<Data *>(data)->preferences.Bool("Time Settings\\Hide Fps"));
        const EditorValue display{EDITOR_TEXT("display"), EDITOR_RECORD, {}, {}, 0, {}, {}, {}, 0, {}, {&hideFps, 1}};
        const EditorValue fields[] = {Integer("frame-mode", t.frameMode), Integer("behavior-mode", t.behaviorMode),
            Real("frame-limit", t.frameLimit), Real("behavior-limit", t.behaviorLimit), Real("scale", t.scale),
            Real("minimum", t.minimum), Real("maximum", t.maximum), display};
        return sink.write ? sink.write(sink.data, fields, uint32_t(std::size(fields))) : EDITOR_INVALID;
    }
    static EditorStatus ReadLog(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        const auto &self = *static_cast<Data *>(data);
        if (OptionalInteger(*request, "revision") && OptionalInteger(*request, "revision") != self.logRevision)
            return EDITOR_STALE;
        const auto index = Field(*request, "index").integer;
        if (index >= self.log.size()) return EDITOR_INVALID;
        const auto &entry = self.log[size_t(index)];
        const auto text = AnsiToUtf8(entry.text);
        const EditorValue fields[] = {{EDITOR_TEXT("text"), EDITOR_TEXT_VALUE, {}, EditorText(text)},
                                      Object("script", entry.script), Object("behavior", entry.behavior)};
        return sink.write ? sink.write(sink.data, fields, 3) : EDITOR_INVALID;
    }
    static uint64_t ContextRevision(void *data, const EditorHost *) {
        const auto &self = *static_cast<Data *>(data);
        return self.contextRevision + self.editor.Revision();
    }
    static EditorStatus Context(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        self.ReadContext();
        const auto first = OptionalInteger(*request, "first");
        const auto total = std::max(self.contextScripts.size(), self.contextObjects.size());
        if (first > total) return EDITOR_INVALID;
        const auto objectsBegin = std::min(size_t(first), self.contextObjects.size());
        const auto objectsEnd = std::min(size_t(first) + 128, self.contextObjects.size());
        const auto scriptsBegin = std::min(size_t(first), self.contextScripts.size());
        const auto scriptsEnd = std::min(size_t(first) + 128, self.contextScripts.size());
        std::vector<ObjectRef> objectRefs(self.contextObjects.begin() + objectsBegin, self.contextObjects.begin() + objectsEnd),
            scriptRefs(self.contextScripts.begin() + scriptsBegin, self.contextScripts.begin() + scriptsEnd),
            ownerRefs(self.contextOwners.begin() + scriptsBegin, self.contextOwners.begin() + scriptsEnd);
        std::vector<EditorString> objects, scripts;
        for (size_t i = objectsBegin; i < objectsEnd; ++i) objects.push_back(EditorText(self.objectNames[i]));
        for (size_t i = scriptsBegin; i < scriptsEnd; ++i) scripts.push_back(EditorText(self.scriptNames[i]));
        const EditorValue values[] = {
            {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {objectRefs.data(), uint32_t(objectRefs.size())}},
            {EDITOR_TEXT("object-names"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {objects.data(), uint32_t(objects.size())}},
            {EDITOR_TEXT("scripts"), EDITOR_OBJECT_LIST, {}, {}, 0, {scriptRefs.data(), uint32_t(scriptRefs.size())}},
            {EDITOR_TEXT("script-names"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {scripts.data(), uint32_t(scripts.size())}},
            {EDITOR_TEXT("script-owners"), EDITOR_OBJECT_LIST, {}, {}, 0, {ownerRefs.data(), uint32_t(ownerRefs.size())}},
            {EDITOR_TEXT("behavior-name"), EDITOR_TEXT_VALUE, {}, EditorText(self.behaviorName)},
            Integer("revision", ContextRevision(data, nullptr)), Integer("total", total), Integer("next", std::min(size_t(first) + 128, total))};
        return sink.write ? sink.write(sink.data, values, uint32_t(std::size(values))) : EDITOR_INVALID;
    }
    static EditorStatus ReachedBreakpoint(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        const auto &self = *static_cast<Data *>(data);
        if (Field(*request, "reached").integer != uint64_t(self.state.reached)) return EDITOR_STALE;
        const auto index = Field(*request, "index").integer;
        if (index >= self.reached.size()) return EDITOR_INVALID;
        const auto &entry = self.reached[size_t(index)];
        const EditorValue values[] = {Object("script", entry.script), Object("object", entry.object), Object("behavior", entry.behavior)};
        return sink.write ? sink.write(sink.data, values, 3) : EDITOR_INVALID;
    }
    static EditorStatus ProfileData(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        if (OptionalInteger(*request, "details") > 1) return EDITOR_INVALID;
        self.ReadProfile(OptionalInteger(*request, "details") != 0);
        const auto &p = self.profile; const auto &s = p.stats; const auto &r = p.render;
        std::vector<EditorField> fields;
        const auto integer = [&](const char *key, uint64_t value) { fields.push_back({key, EDITOR_INTEGER_VALUE, {}, {}, value}); };
        const auto real = [&](const char *key, double value) { EditorField field{key, EDITOR_REAL_VALUE}; field.real = value; fields.push_back(std::move(field)); };
        integer("available", self.profileAvailable); integer("revision", self.profileRevision);
        integer("show-dynamic", self.preferences.Bool("3D Layout\\Show Dynamic Objects"));
        real("frame-time", s.TotalFrameTime); real("interface-time", s.EstimatedInterfaceTime);
        real("render-time", s.RenderTime); real("graph-time", s.TotalBehaviorExecution - s.BehaviorCodeExecution);
        real("behavior-time", s.BehaviorCodeExecution); real("operation-time", s.ParametricOperations);
        real("animation-time", s.AnimationManagement);
        integer("objects", p.objects3d); integer("textures", p.textures); integer("sprites", p.sprites);
        integer("behaviors", p.behaviors); integer("faces", p.faces); integer("vertices", p.vertices);
        integer("drawn-faces", r.NbTrianglesDrawn); integer("drawn-objects", r.NbObjectDrawn); integer("drawn-lines", r.NbLinesDrawn);
        integer("video-textures", p.videoTextures); integer("texture-bytes", p.textureBytes);
        integer("video-sprites", p.videoSprites); integer("sprite-bytes", p.spriteBytes);
        integer("executed-objects", s.ActiveObjectsExecuted); integer("executed-behaviors", s.BehaviorsExecuted);
        integer("executed-blocks", s.BuildingBlockExecuted); integer("parsed-links", s.BehaviorLinksParsed);
        integer("delayed-links", s.BehaviorDelayedLinks);
        integer("row-count", p.rows.size());
        const auto borrowed = BorrowEditorFields(fields);
        return sink.write ? sink.write(sink.data, borrowed.data(), uint32_t(borrowed.size())) : EDITOR_INVALID;
    }
    static EditorStatus ProfileRows(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        const auto &self = *static_cast<Data *>(data);
        if (Field(*request, "revision").integer != self.profileRevision) return EDITOR_STALE;
        const auto first = Field(*request, "first").integer;
        if (first > self.profile.rows.size()) return EDITOR_INVALID;
        std::vector<EditorField> fields{{"revision", EDITOR_INTEGER_VALUE, {}, {}, self.profileRevision},
            {"total", EDITOR_INTEGER_VALUE, {}, {}, self.profile.rows.size()}};
        EditorField rows{"rows", EDITOR_RECORD};
        for (size_t i = size_t(first); i < std::min(self.profile.rows.size(), size_t(first) + 128); ++i) {
            const auto &row = self.profile.rows[i];
            EditorField item{std::to_string(i), EDITOR_RECORD};
            EditorField time{"time", EDITOR_REAL_VALUE}; time.real = row.time;
            item.fields = {{"name", EDITOR_TEXT_VALUE, {}, row.name}, time,
                {"parent", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(row.parent))},
                {"object", EDITOR_OBJECT, row.object}, {"script", EDITOR_OBJECT, row.script},
                {"dynamic", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(row.dynamic)}};
            rows.fields.push_back(std::move(item));
        }
        fields.push_back(std::move(rows));
        const auto borrowed = BorrowEditorFields(fields);
        return sink.write ? sink.write(sink.data, borrowed.data(), uint32_t(borrowed.size())) : EDITOR_INVALID;
    }
};

ExecutionCommands::ExecutionCommands(EditorApplication &editor, std::function<Document *()> current,
                                     Preferences &preferences, DocumentFiles *files)
    : m_Data(std::make_unique<Data>(editor, std::move(current), preferences)), m_Files(files) {}
ExecutionCommands::~ExecutionCommands() { Stop(); }
const Debugger *ExecutionCommands::Bind(Document &document) {
    if (m_Data->document) m_Data->RestoreProfiling();
    if (m_Data->debugger) m_Data->debugger->Detach();
    m_Data->document = &document;
    m_Data->synchronizeAfterRelease = false;
    m_Data->debugger = std::make_unique<Debugger>(document);
    m_Data->log.clear(); m_Data->logRevision = 0;
    m_Data->lockedObject = {}; m_Data->lockedScript = {};
    m_Data->Publish(true);
    m_Data->ReadContext();
    return m_Data->debugger.get();
}
void ExecutionCommands::DocumentChanging() {
    if (!m_Data->debugger) return;
    m_Data->RestoreProfiling();
    m_Data->debugger->Detach();
    m_Data->lockedObject = {}; m_Data->lockedScript = {};
    m_Data->ValidateLocks();
    m_Data->Publish();
}
void ExecutionCommands::ReleaseDocument() {
    m_Data->RestoreProfiling();
    if (m_Data->debugger) m_Data->debugger->Detach();
    m_Data->synchronizeAfterRelease = true;
}
void ExecutionCommands::Synchronize() { m_Data->Synchronize(); }
void ExecutionCommands::Stop() {
    if (m_Files) m_Files->Execution(nullptr);
    m_Data->RestoreProfiling();
    if (m_Data->debugger) m_Data->debugger->Detach();
    m_Data->debugger.reset(); m_Data->document = nullptr;
    m_Data->synchronizeAfterRelease = false;
    m_Data->profilers.clear();
    m_Data->ClearPublished();
}
const ExecutionCommands::StateData &ExecutionCommands::State() const { return m_Data->state; }
const Debugger *ExecutionCommands::BoundDebugger() const { return m_Data->debugger.get(); }
bool ExecutionCommands::Update(double budget) {
    auto &d = *m_Data;
    d.Synchronize();
    if (!d.Ready() || !d.document->IsOpen()) return false;
    const bool locksChanged = d.ValidateLocks();
    if (d.debugger->GetState() == Debugger::State::Stopped) {
        if (locksChanged) d.Publish();
        return false;
    }
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
        {EDITOR_TEXT("minimum"), EDITOR_REAL_VALUE}, {EDITOR_TEXT("maximum"), EDITOR_REAL_VALUE},
        {EDITOR_TEXT("display"), EDITOR_RECORD, 1}};
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
        {A::Time, "runtime.set-time", "Time Settings", time, 8},
        {A::Profile, "runtime.observe-profile", "Show Runtime Profile", &enabled, 1},
        {A::RefreshProfile, "runtime.refresh-profile", "Refresh Profile Geometry"}};
    std::vector<EditorCommandDesc> commands;
    for (const auto &description : descriptions) {
        m_Data->commands.push_back({m_Data.get(), description.action, description.id, description.title});
        auto &command = m_Data->commands.back();
        commands.push_back({sizeof(EditorCommandDesc), EditorLiteral(command.id), EDITOR_TEXT("builtin.execution"),
            EditorLiteral(command.title), description.parameters, description.count, &Data::Run, &command,
            Data::NeedsDocument(command.action) ? EDITOR_DOCUMENT : EDITOR_APPLICATION,
            command.action == A::Profile ? EDITOR_SESSION_CHANGE : EDITOR_RUNTIME_CONTROL, &Data::Check});
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
        {EDITOR_TEXT("log-entries"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("log-revision"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("reached-breakpoints"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("faults"), EDITOR_INTEGER_VALUE}};
    const EditorParameter index{EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE};
    const EditorParameter logArgs[] = {index, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter reachedArgs[] = {index, {EDITOR_TEXT("reached"), EDITOR_INTEGER_VALUE}};
    const EditorParameter details{EDITOR_TEXT("details"), EDITOR_INTEGER_VALUE, 1};
    const EditorParameter first{EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1};
    const EditorParameter logFields[] = {{EDITOR_TEXT("text"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("script"), EDITOR_OBJECT},
                                        {EDITOR_TEXT("behavior"), EDITOR_OBJECT}};
    const EditorParameter contextFields[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("object-names"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("scripts"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("script-names"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("script-owners"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("behavior-name"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE}};
    const EditorParameter reachedFields[] = {{EDITOR_TEXT("script"), EDITOR_OBJECT},
        {EDITOR_TEXT("object"), EDITOR_OBJECT}, {EDITOR_TEXT("behavior"), EDITOR_OBJECT}};
    std::vector<EditorParameter> profileFields;
    for (const auto *key : {"available", "revision", "show-dynamic", "objects", "textures", "sprites", "behaviors", "faces", "vertices",
                           "drawn-faces", "drawn-objects", "drawn-lines", "video-textures", "texture-bytes",
                           "video-sprites", "sprite-bytes", "executed-objects", "executed-behaviors", "executed-blocks",
                           "parsed-links", "delayed-links", "row-count"})
        profileFields.push_back({EditorLiteral(key), EDITOR_INTEGER_VALUE});
    for (const auto *key : {"frame-time", "interface-time", "render-time", "graph-time", "behavior-time", "operation-time", "animation-time"})
        profileFields.push_back({EditorLiteral(key), EDITOR_REAL_VALUE});
    const EditorParameter rowArgs[] = {{EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}};
    const EditorParameter rowFields[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("rows"), EDITOR_RECORD}};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("runtime.state"), EDITOR_TEXT("builtin.execution"), nullptr, 0,
            &Data::Read, m_Data.get(), EDITOR_QUERY_VERSIONED, EDITOR_APPLICATION, stateFields, uint32_t(std::size(stateFields)),
            [](void *data, const EditorHost *) { return static_cast<Data *>(data)->stateRevision; }},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("runtime.time"), EDITOR_TEXT("builtin.execution"), nullptr, 0,
            &Data::ReadTime, m_Data.get(), EDITOR_QUERY_VERSIONED, EDITOR_APPLICATION, time, 8, &Data::TimeRevision},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("runtime.log"), EDITOR_TEXT("builtin.execution"), logArgs, 2,
            &Data::ReadLog, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, logFields, 3},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("runtime.context"), EDITOR_TEXT("builtin.execution"), &first, 1,
            &Data::Context, m_Data.get(), EDITOR_QUERY_VERSIONED, EDITOR_APPLICATION, contextFields, uint32_t(std::size(contextFields)), &Data::ContextRevision},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("runtime.reached-breakpoint"), EDITOR_TEXT("builtin.execution"), reachedArgs, 2,
            &Data::ReachedBreakpoint, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, reachedFields, 3},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("runtime.profile"), EDITOR_TEXT("builtin.execution"), &details, 1,
            &Data::ProfileData, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, profileFields.data(), uint32_t(profileFields.size())},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("runtime.profile-rows"), EDITOR_TEXT("builtin.execution"), rowArgs, 2,
            &Data::ProfileRows, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, rowFields, 3}};
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
    module.data = this;
    module.start = [](void *data, const EditorHost *) {
        auto &self = *static_cast<ExecutionCommands *>(data);
        auto *document = self.m_Data->current();
        if (!document) return EDITOR_ERROR;
        self.Bind(*document);
        auto &debugger = *self.m_Data->debugger;
        const auto &preferences = self.m_Data->preferences;
        debugger.reengageBehavior = preferences.behaviorBreakpointReengage;
        debugger.reengageLink = preferences.linkBreakpointReengage;
        debugger.reengageParameter = preferences.parameterBreakpointReengage;
        debugger.redrawOnlyAtBreakpoint = preferences.redrawOnlyAtBreakpoint;
        self.m_Data->Publish(true);
        if (self.m_Files) self.m_Files->Execution(&self);
        return EDITOR_OK;
    };
    module.update = [](void *data, const EditorHost *, EditorModulePhase phase) {
        auto &self = *static_cast<ExecutionCommands *>(data);
        if (phase == EDITOR_PREPARE_FRAME) {
            auto &state = *self.m_Data;
            const auto document = state.editor.Scope().document;
            if ((!document.value && (state.state.document.value || state.profileDocument.value)) ||
                (state.Ready() && (document != state.state.document || state.synchronizeAfterRelease)))
                self.Synchronize();
        }
        else if (phase == EDITOR_AFTER_VIEWS && self.Update(0.010) && self.changed) self.changed();
    };
    module.stop = [](void *data, const EditorHost *) { static_cast<ExecutionCommands *>(data)->Stop(); };
    module.commands = commands.data(); module.commandCount = uint32_t(commands.size());
    module.queries = queries; module.queryCount = uint32_t(std::size(queries));
    module.bindings = bindings.data(); module.bindingCount = uint32_t(bindings.size());
    return m_Data->editor.Register(module, error);
}
} // namespace vdev
