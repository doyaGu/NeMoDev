#include "core/graph/Debugger.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/SdkCompat.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/document/ScriptSession.h"
#include "core/document/UndoHistory.h"

namespace vdev {

namespace {

const char *NameOf(CKObject *object) {
    return object && object->GetName() ? object->GetName() : "";
}

// The value bytes a breakpoint compares (an input reads its real source).
std::vector<CKBYTE> ValueOf(CKObject *object) {
    CKParameter *parameter = CKParameter::Cast(object);
    if (CKParameterIn *input = CKParameterIn::Cast(object)) parameter = input->GetRealSource();
    std::vector<CKBYTE> bytes;
    if (!parameter) return bytes;
    const int size = parameter->GetDataSize();
    const void *data = size > 0 && size < (1 << 20) ? parameter->GetReadDataPtr(FALSE) : nullptr;
    if (data) bytes.assign(static_cast<const CKBYTE *>(data), static_cast<const CKBYTE *>(data) + size);
    return bytes;
}

CKGUID TypeOf(CKObject *object) {
    if (CKParameterIn *input = CKParameterIn::Cast(object)) return input->GetGUID();
    if (CKParameter *parameter = CKParameter::Cast(object)) return parameter->GetGUID();
    return CKGUID();
}

// -1 / 0 / 1; ordered is false when only (in)equality is meaningful.
int Compare(CKContext *context, const CKGUID &type, const std::vector<CKBYTE> &a, const std::vector<CKBYTE> &b,
            bool &ordered) {
    CKParameterManager *manager = context->GetParameterManager();
    ordered = false;
    if (a.size() == 4 && b.size() == 4 && manager) {
        if (type == CKPGUID_FLOAT || manager->IsDerivedFrom(type, CKPGUID_FLOAT)) {
            float x, y;
            std::memcpy(&x, a.data(), 4);
            std::memcpy(&y, b.data(), 4);
            ordered = true;
            return x < y ? -1 : x > y ? 1 : 0;
        }
        if (type == CKPGUID_INT || manager->IsDerivedFrom(type, CKPGUID_INT) || type == CKPGUID_BOOL) {
            int x, y;
            std::memcpy(&x, a.data(), 4);
            std::memcpy(&y, b.data(), 4);
            ordered = true;
            return x < y ? -1 : x > y ? 1 : 0;
        }
    }
    return a == b ? 0 : 1;
}

} // namespace

const ExtraData *FindBreakpoint(const InterfaceData &data, ExtraDataType type, CK_ID object) {
    for (const auto &entry : data.extraData)
        if (entry.type == type && entry.id1 == object) return &entry;
    return nullptr;
}

bool ToggleBreakpoint(Document &document, CK_ID script, ExtraDataType type, CK_ID object,
                      CK_ID owner) {
    if (!object) return false;
    return EditScriptData(document, script, "Breakpoint", [&](InterfaceData &data) {
        auto &entries = data.extraData;
        auto found = std::find_if(entries.begin(), entries.end(),
                                  [&](const ExtraData &entry) { return entry.type == type && entry.id1 == object; });
        if (found != entries.end()) {
            entries.erase(found);
            return;
        }
        ExtraData entry;
        entry.type = type;
        entry.id1 = object;
        entry.id2 = owner;
        // Dev 2.5 writes the third ExtraData layout (sub-data counts).
        if (data.extraDataVersion == 0) data.extraDataVersion = 3;
        entries.push_back(entry);
    });
}

bool SetBreakpointConditions(Document &document, CK_ID script, ExtraDataType type,
                             CK_ID object, const std::vector<ExtraSubData> &conditions) {
    // No breakpoint: nothing recorded (a step changing nothing would be undone for nothing).
    ScriptSession *session = document.Session(script);
    if (!session || !FindBreakpoint(session->Data(), type, object)) return false;
    return EditScriptData(document, script, "Configure Breakpoint", [&](InterfaceData &data) {
        for (auto &entry : data.extraData)
            if (entry.type == type && entry.id1 == object) entry.subData = conditions;
    });
}

std::vector<CKBYTE> ParameterBytesFromText(CKContext *context, const CKGUID &type, const std::string &text) {
    std::vector<CKBYTE> bytes;
    if (!context) return bytes;
    // A scratch parameter of the type parses the text.
    CKParameterOut *scratch = context->CreateCKParameterOut((CKSTRING) "Breakpoint value", type, TRUE);
    if (!scratch) return bytes;
    if (scratch->SetStringValue(const_cast<CKSTRING>(text.c_str())) == CK_OK) bytes = ValueOf(scratch);
    context->DestroyObject(scratch);
    return bytes;
}

std::string ParameterTextFromBytes(CKContext *context, const CKGUID &type, const std::vector<CKBYTE> &bytes) {
    std::string text;
    if (!context || bytes.empty()) return text;
    CKParameterOut *scratch = context->CreateCKParameterOut((CKSTRING) "Breakpoint value", type, TRUE);
    if (!scratch) return text;
    if (scratch->SetValue(bytes.data(), static_cast<int>(bytes.size())) == CK_OK) {
        const int size = scratch->GetStringValue(nullptr, FALSE);
        if (size > 0 && size < 65536) {
            text.resize(static_cast<size_t>(size));
            scratch->GetStringValue(&text[0], FALSE);
            text.resize(std::strlen(text.c_str()));
        }
    }
    context->DestroyObject(scratch);
    return text;
}

CKGUID ParameterType(CKObject *parameter) {
    return TypeOf(parameter);
}

// ---------------------------------------------------------------------------

std::vector<Debugger::Breakpoint> Debugger::Breakpoints() const {
    std::vector<Breakpoint> all;
    for (const auto &entry : m_Document.Sessions())
        if (entry.second)
            for (const auto &data : entry.second->Data().extraData)
                if (data.type == EXTRA_DATA_PARAMETER || data.type == EXTRA_DATA_BEHAVIOR ||
                    data.type == EXTRA_DATA_CONNECTION)
                    all.push_back({entry.first, data});
    return all;
}

void Debugger::Note(const std::string &text, CK_ID script, CK_ID behavior) {
    m_Log.push_back({text, script, behavior});
    if (m_Log.size() > 2000) m_Log.erase(m_Log.begin(), m_Log.begin() + 500);
}

void Debugger::Stop(const std::string &why) {
    m_State = State::Paused;
    PauseContext();
    Note(why, m_CurrentScript, m_CurrentBehavior);
}

// Dev pauses the context when it stops playing (Dev.exe sub_5CD3D0) and
// after Step One Frame (sub_5CD460): the managers get OnCKPause (the sound
// manager pauses the sounds). Not within a traced frame.
void Debugger::PauseContext() {
    CKContext *context = m_Document.Context();
    if (context && !m_InFrame && context->IsPlaying())
        GuardedCall([&] { context->Pause(); }, &m_Document.Host(), "CKContext::Pause");
}

void Debugger::ReadContext() {
    CKDebugContext *debug = m_Document.Context() ? m_Document.Context()->GetDebugContext() : nullptr;
    if (!debug) return;
    m_CurrentObject = debug->CurrentObject ? debug->CurrentObject->GetID() : 0;
    m_CurrentScript = debug->CurrentScript ? debug->CurrentScript->GetID() : 0;
    m_CurrentBehavior = debug->CurrentBehavior ? debug->CurrentBehavior->GetID() : 0;
    // CurrentBehaviorAction (CKDebugContext::DebugStep, CK2 0x2400629F,
    // writes this[21]).
    m_Action = DebugCurrentAction(debug);
}

void Debugger::Play() {
    CKContext *context = m_Document.Context();
    if (!context) return;
    // Behaviors run for the objects the behavior manager holds; a scene
    // never launched (loaded composition, objects added since) holds none:
    // the current scene (else the level scene) is launched first, which
    // activates its objects by their scene flags.
    CKLevel *level = context->GetCurrentLevel();
    CKBehaviorManager *behaviors = context->GetBehaviorManager();
    if (m_State == State::Stopped && level && behaviors && behaviors->GetObjectsCount() == 0) {
        CKScene *scene = level->GetCurrentScene() ? level->GetCurrentScene() : level->GetLevelScene();
        GuardedCall([&] { level->LaunchScene(scene); }, &m_Document.Host(), "CKLevel::LaunchScene");
        Note(std::string("Launched scene <") + NameOf(scene) + ">");
    }
    if (!context->IsPlaying() &&
        !GuardedCall([&] { context->Play(); }, &m_Document.Host(), "CKContext::Play")) {
        Note("Play raised an exception");
        return;
    }
    m_State = State::Running;
    m_RunFrames = 0;
    m_ReachedList.clear();
    Note(trace ? "Play (Trace Mode)" : "Play");
}

void Debugger::Pause() {
    if (m_State == State::Running) {
        m_State = State::Paused;
        PauseContext();
        Note("Pause");
    }
}

void Debugger::Reset() {
    CKContext *context = m_Document.Context();
    if (!context) return;
    if (m_InFrame) GuardedCall([&] { context->ProcessDebugEnd(); }, &m_Document.Host(), "ProcessDebugEnd");
    m_InFrame = false;
    GuardedCall([&] { context->Reset(); }, &m_Document.Host(), "CKContext::Reset");
    m_State = State::Stopped;
    m_Frames = 0;
    m_CurrentObject = m_CurrentScript = m_CurrentBehavior = 0;
    m_ParameterValues.clear();
    m_LinkActive.clear();
    m_Disengaged.clear();
    m_ReachedList.clear();
    // Building Blocks may have changed pins while running.
    m_Document.MarkTopologyChanged();
    Note("Reset");
}

void Debugger::Detach() {
    if (m_State != State::Stopped && m_Document.Context()) Reset();
    m_State = State::Stopped;
    m_Log.clear();
}

void Debugger::BeginFrame() {
    CKContext *context = m_Document.Context();
    if (!context->IsPlaying()) GuardedCall([&] { context->Play(); }, &m_Document.Host(), "CKContext::Play");
    const float delta = 1000.0f / std::max(1.0f, simulatedFps);
    if (!GuardedCall([&] { context->ProcessDebugStart(delta); }, &m_Document.Host(), "ProcessDebugStart")) {
        Stop("Debug frame start raised an exception");
        return;
    }
    m_InFrame = true;
    // Values at the frame start: parameter breakpoints fire on a change.
    for (const auto &breakpoint : Breakpoints())
        if (breakpoint.data.type == EXTRA_DATA_PARAMETER && !m_ParameterValues.count(breakpoint.data.id1))
            m_ParameterValues[breakpoint.data.id1] = ValueOf(context->GetObject(breakpoint.data.id1));
}

void Debugger::EndFrame() {
    CKContext *context = m_Document.Context();
    GuardedCall([&] { context->ProcessDebugEnd(); }, &m_Document.Host(), "ProcessDebugEnd");
    m_InFrame = false;
    ++m_Frames;
    ++m_RunFrames;
    m_CurrentObject = m_CurrentScript = m_CurrentBehavior = 0;
    if (maxFrames > 0 && m_RunFrames >= maxFrames && m_State == State::Running) {
        m_State = State::Paused;
        PauseContext();
        Note("Stop, CK frame completed");
    }
}

bool Debugger::Step() {
    CKContext *context = m_Document.Context();
    bool more = false;
    if (!GuardedCall([&] { more = context->ProcessDebugStep() != 0; }, &m_Document.Host(), "ProcessDebugStep")) {
        ReadContext();
        Stop(std::string("Behavior <") + NameOf(context->GetObject(m_CurrentBehavior)) + "> raised an exception");
        return false;
    }
    ReadContext();
    if (std::getenv("SCHEMATIC_DEBUG_STEPS"))
        Note("step object " + std::to_string(m_CurrentObject) + " script " + std::to_string(m_CurrentScript) +
             " behavior " + std::to_string(m_CurrentBehavior) + " action " + std::to_string(m_Action) +
             (more ? "" : " (frame end)"));
    return more;
}

bool Debugger::ConditionsHold(const ExtraData &breakpoint) const {
    if (breakpoint.subData.empty()) return true;
    CKContext *context = m_Document.Context();
    bool result = true;
    for (size_t i = 0; i < breakpoint.subData.size(); ++i) {
        const ExtraSubData &condition = breakpoint.subData[i];
        CKObject *left = context->GetObject(condition.id1);
        const std::vector<CKBYTE> value = ValueOf(left);
        const bool reference = condition.UsesParameterReference();
        const std::vector<CKBYTE> other = reference ? ValueOf(context->GetObject(condition.id2)) : condition.buffer;
        bool ordered = false;
        const int order = Compare(context, TypeOf(left), value, other, ordered);
        bool holds = false;
        switch (condition.GetComparisonType()) {
        case EXTRA_COMPARE_EQUAL_VALUE:
        case EXTRA_COMPARE_EQUAL_PARAMETER: holds = order == 0; break;
        case EXTRA_COMPARE_NOT_EQUAL_VALUE:
        case EXTRA_COMPARE_NOT_EQUAL_PARAMETER: holds = order != 0; break;
        case EXTRA_COMPARE_LESS_VALUE:
        case EXTRA_COMPARE_LESS_PARAMETER: holds = ordered && order < 0; break;
        case EXTRA_COMPARE_LESS_EQUAL_VALUE:
        case EXTRA_COMPARE_LESS_EQUAL_PARAMETER: holds = ordered && order <= 0; break;
        case EXTRA_COMPARE_GREATER_VALUE:
        case EXTRA_COMPARE_GREATER_PARAMETER: holds = ordered && order > 0; break;
        case EXTRA_COMPARE_GREATER_EQUAL_VALUE:
        case EXTRA_COMPARE_GREATER_EQUAL_PARAMETER: holds = ordered && order >= 0; break;
        }
        if (i == 0) result = holds;
        else if (condition.GetLogicalOperator() == EXTRA_LOGICAL_OR) result = result || holds;
        else if (condition.GetLogicalOperator() == EXTRA_LOGICAL_XOR) result = result != holds;
        else result = result && holds;
    }
    return result;
}

bool Debugger::CheckBreakpoints() {
    CKContext *context = m_Document.Context();
    bool reached = false;
    for (const auto &breakpoint : Breakpoints()) {
        const ExtraData &data = breakpoint.data;
        CKObject *object = context->GetObject(data.id1);
        if (!object) continue;
        bool hit = false;
        std::string text;
        if (data.type == EXTRA_DATA_BEHAVIOR) {
            hit = m_CurrentBehavior == data.id1 && m_Action == CKDEBUG_BEHEXECUTE;
            text = std::string("Breakpoint on behavior <") + NameOf(object) + ">";
        } else if (data.type == EXTRA_DATA_CONNECTION) {
            CKBehaviorLink *link = CKBehaviorLink::Cast(object);
            CKBehaviorIO *out = link ? link->GetOutBehaviorIO() : nullptr;
            const bool active = out && out->IsActive();
            hit = active && !m_LinkActive[data.id1];
            m_LinkActive[data.id1] = active;
            CKBehaviorIO *in = link ? link->GetInBehaviorIO() : nullptr;
            text = std::string("Breakpoint on link between <") + NameOf(in ? in->GetOwner() : nullptr) + "> and <" +
                   NameOf(out ? out->GetOwner() : nullptr) + ">";
        } else if (data.type == EXTRA_DATA_PARAMETER) {
            const std::vector<CKBYTE> value = ValueOf(object);
            auto &last = m_ParameterValues[data.id1];
            hit = value != last;
            last = value;
            text = std::string("Breakpoint on parameter <") + NameOf(object) + ">";
        }
        if (hit && m_Disengaged.count({static_cast<int>(data.type), data.id1})) hit = false;
        if (hit && ConditionsHold(data)) {
            Note(text + " --> reached", breakpoint.script,
                 data.type == EXTRA_DATA_BEHAVIOR ? data.id1 : m_CurrentBehavior);
            reached = true;
            m_ReachedList.push_back(breakpoint);
            const bool reengage = data.type == EXTRA_DATA_BEHAVIOR   ? reengageBehavior
                                  : data.type == EXTRA_DATA_CONNECTION ? reengageLink
                                                                       : reengageParameter;
            if (!reengage) m_Disengaged.insert({static_cast<int>(data.type), data.id1});
        }
    }
    // Message breakpoints: a sender about to execute with that message.
    if (!messageBreakpoints.empty() && m_Action == CKDEBUG_BEHEXECUTE) {
        CKBehavior *behavior = CKBehavior::Cast(context->GetObject(m_CurrentBehavior));
        CKParameterManager *manager = context->GetParameterManager();
        CKMessageManager *messages = context->GetMessageManager();
        const CKParameterType messageType = manager ? manager->ParameterGuidToType(CKPGUID_MESSAGE) : -1;
        if (behavior && (behavior->GetFlags() & CKBEHAVIOR_MESSAGESENDER))
            for (int i = 0; i < behavior->GetInputParameterCount(); ++i) {
                CKParameterIn *input = behavior->GetInputParameter(i);
                CKParameter *source = input && input->GetType() == messageType ? input->GetRealSource() : nullptr;
                int message = -1;
                if (source) source->GetValue(&message);
                if (source && messageBreakpoints.count(message)) {
                    const char *name = messages ? messages->GetMessageTypeName(message) : nullptr;
                    Note(std::string("Breakpoint on message <") + (name ? name : "?") + "> --> reached", m_CurrentScript,
                         m_CurrentBehavior);
                    reached = true;
                }
            }
    }
    if (reached) ++m_Reached;
    return reached;
}

void Debugger::Update(double budget) {
    if (m_State != State::Running || !m_Document.Context()) return;
    CKContext *context = m_Document.Context();
    if (!trace && !m_InFrame) {
        for (int i = 0; i < std::max(1, framesPerUpdate) && m_State == State::Running; ++i) {
            if (!GuardedCall([&] { context->Process(); }, &m_Document.Host(), "CKContext::Process")) {
                Stop("A CK frame raised an exception");
                return;
            }
            ++m_Frames;
            if (maxFrames > 0 && ++m_RunFrames >= maxFrames) {
                m_State = State::Paused;
                PauseContext();
                Note("Stop, CK frame completed");
            }
        }
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    while (m_State == State::Running) {
        if (!m_InFrame) BeginFrame();
        if (!m_InFrame) return;
        const bool more = Step();
        if (insideFrames && m_State == State::Running) {
            // One Debug Step per update.
            if (CheckBreakpoints()) {
                m_State = State::Paused;
                return;
            }
            if (!more) EndFrame();
            return;
        }
        if (m_State != State::Running) return;
        if (CheckBreakpoints()) {
            m_State = State::Paused;
            return;
        }
        if (!more) {
            EndFrame();
            return; // one traced frame per UI frame
        }
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() > budget) return;
    }
}

// The debug context is where the lock wants it (no lock: anywhere).
bool Debugger::LockHolds() const {
    return (!lockedObject || m_CurrentObject == lockedObject) && (!lockedScript || m_CurrentScript == lockedScript);
}

void Debugger::DebugStep() {
    CKContext *context = m_Document.Context();
    if (!context) return;
    m_State = State::Paused;
    m_ReachedList.clear();
    if (lockedScript && !context->GetObject(lockedScript)) lockedScript = 0;
    if (lockedObject && !context->GetObject(lockedObject)) lockedObject = 0;
    if (!lockedObject && !lockedScript) {
        if (!m_InFrame) BeginFrame();
        if (!m_InFrame) return;
        const bool more = Step();
        CheckBreakpoints();
        if (!more) {
            EndFrame();
            Note("CK frame completed");
        }
        return;
    }
    // Locked: steps on (frame after frame) until the debug context is on
    // the locked object and Script; an inactive locked Script stops it.
    CKBehavior *script = CKBehavior::Cast(context->GetObject(lockedScript));
    if (script && !script->IsActive()) {
        Note("Debug - Locked script in debug context is not active");
        return;
    }
    constexpr int MAX_FRAMES = 1000;
    for (int frames = 0; frames < MAX_FRAMES;) {
        if (!m_InFrame) BeginFrame();
        if (!m_InFrame) return;
        const bool more = Step();
        if (CheckBreakpoints()) return;
        if (more && LockHolds()) return;
        if (!more) {
            EndFrame();
            Note("Debug - CK frame completed");
            ++frames;
        }
    }
    Note("Debug context locked: the locked context was not reached");
}

void Debugger::LeaveBehavior() {
    if (!m_Document.Context()) return;
    const CK_ID leaving = m_CurrentBehavior;
    if (!leaving || !m_InFrame) {
        DebugStep();
        return;
    }
    m_State = State::Paused;
    m_ReachedList.clear();
    CKObject *behavior = m_Document.Context()->GetObject(leaving);
    const std::string name = NameOf(behavior);
    for (int i = 0; i < 100000; ++i) {
        const bool more = Step();
        if (m_CurrentBehavior == leaving && m_Action == CKDEBUG_BEHEXECUTEDONE) {
            Note("Leave behavior: <" + name + "> left successfully");
            return;
        }
        if (CheckBreakpoints()) return;
        if (!more) {
            EndFrame();
            Note("Leave behavior: <" + name + "> no yet left");
            return;
        }
    }
    Note("Leave behavior error");
}

void Debugger::StepFrame() {
    CKContext *context = m_Document.Context();
    if (!context) return;
    m_State = State::Paused;
    if (!trace && !m_InFrame) {
        if (!context->IsPlaying()) GuardedCall([&] { context->Play(); }, &m_Document.Host(), "CKContext::Play");
        if (!GuardedCall([&] { context->Process(); }, &m_Document.Host(), "CKContext::Process"))
            Stop("A CK frame raised an exception");
        ++m_Frames;
        PauseContext();
        return;
    }
    if (!m_InFrame) BeginFrame();
    while (m_InFrame) {
        const bool more = Step();
        if (m_State != State::Paused) return;
        if (CheckBreakpoints()) return;
        if (!more) EndFrame();
    }
    PauseContext();
    Note("CK frame completed");
}

void Debugger::ExecuteScripts(const std::vector<CK_ID> &scripts) {
    CKContext *context = m_Document.Context();
    if (!context) return;
    const float delta = 1000.0f / std::max(1.0f, simulatedFps);
    for (CK_ID id : scripts) {
        CKBehavior *script = CKBehavior::Cast(context->GetObject(id));
        if (!script || !(script->GetType() & CKBEHAVIORTYPE_SCRIPT)) continue;
        const bool ok = GuardedCall(
            [&] {
                script->Activate(TRUE, TRUE);
                if (script->GetInputCount() > 0) script->ActivateInput(0);
                script->Execute(delta);
            },
            &m_Document.Host(), "Execute Script");
        Note(std::string(ok ? "Executed Script <" : "Execute Script raised an exception: <") + NameOf(script) + ">", id);
    }
}

void RemoveBreakpoints(Document &document, const std::vector<Debugger::Breakpoint> &breakpoints) {
    EditGroup group(document, "Remove All Breakpoints");
    for (const auto &breakpoint : breakpoints)
        ToggleBreakpoint(document, breakpoint.script, breakpoint.data.type, breakpoint.data.id1, breakpoint.data.id2);
}

} // namespace vdev
