#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "CKAll.h"
#include "core/interface/InterfaceData.h"

namespace vdev {

class Document;

// Breakpoints are Dev's: the ExtraData entries of a Script's InterfaceChunk
// (type 1 parameter, 2 behavior, 3 link with its owning graph; sub-data are
// the comparisons of "Configure Breakpoint"). Editing them is a display
// edit of the Script (undoable, saved with the Schematic).

// Dev F9: adds the breakpoint, or removes it when present. False when the
// Script's display is read-only.
bool ToggleBreakpoint(Document &document, CK_ID script, ExtraDataType type, CK_ID object,
                      CK_ID owner = 0);
// Dev Shift+F9 (Breakpoints Configuration): the conditions of a breakpoint.
bool SetBreakpointConditions(Document &document, CK_ID script, ExtraDataType type,
                             CK_ID object, const std::vector<ExtraSubData> &conditions);
const ExtraData *FindBreakpoint(const InterfaceData &data, ExtraDataType type, CK_ID object);
// The comparison value bytes of text in a parameter type (empty when the type refuses it), and back.
std::vector<CKBYTE> ParameterBytesFromText(CKContext *context, const CKGUID &type, const std::string &text);
std::string ParameterTextFromBytes(CKContext *context, const CKGUID &type, const std::vector<CKBYTE> &bytes);
// The type a breakpoint condition compares (an input: its own type).
CKGUID ParameterType(CKObject *parameter);

// Dev's Script Debugger ("Trace Tools", language pack [Schematic]<Debug>)
// on the CK runtime: Play runs CK frames (CKContext::Process); in Trace
// Mode a frame runs behavior by behavior (CKContext::ProcessDebugStart/
// ProcessDebugStep/ProcessDebugEnd, the CKDebugContext Dev reads), so
// breakpoints stop it and Debug Step / Leave Behavior advance it. Every CK
// call is guarded; a structured exception stops the run.
//
// Running changes the composition's runtime state (values, activity);
// Reset (CKContext::Reset) restores the scene's initial conditions.
class Debugger {
public:
    enum class State { Stopped, Running, Paused };
    struct Entry {
        std::string text;
        CK_ID script = 0, behavior = 0;
    };

    explicit Debugger(Document &document) : m_Document(document) {}

    State GetState() const { return m_State; }
    bool InFrame() const { return m_InFrame; }
    bool trace = false;        // Trace Mode (Ctrl+T)
    // The Script Debugger's Debug Inside Frames (documentation: "a full
    // debug mode ... more precise than the previous one but much slower"):
    // a traced run advances one Debug Step per update, not a frame; Debug
    // Step and Leave Behavior need it.
    bool insideFrames = false;
    int maxFrames = 0;         // "Max CK Frames": stop after this many frames of a run (0: no limit)
    float simulatedFps = 60.f; // "Simulated FPS": the frame time of traced frames
    int framesPerUpdate = 1;   // untraced frames per UI frame
    // Dev preferences "Behavior/Link/Parameter Breakpoint Auto Reengage":
    // when off, a reached breakpoint stays off until Reset.
    bool reengageBehavior = true, reengageLink = true, reengageParameter = true;
    // "Redraw Schematic Only When Breakpoint Reached" (read by the UI).
    bool redrawOnlyAtBreakpoint = false;
    // Dev "Add Breakpoint on Message": message types (CKMessageManager) whose
    // sending stops a traced run, when a message-sending Building Block with
    // that message on an input executes. Not saved (Dev's ExtraData creates
    // types 1 to 3 only, see dev-2.5-extradata.json).
    std::set<int> messageBreakpoints;
    // How many times breakpoints were reached (the UI redraws on a change).
    int Reached() const { return m_Reached; }
    // The breakpoints reached since the last run or step began (the Script
    // Debugger's Goto goes from one to the next, Dev.exe sub_587B10).
    struct Breakpoint;
    const std::vector<Breakpoint> &ReachedBreakpoints() const { return m_ReachedList; }
    // Dev's Debug Context lock (the Script Debugger's boxes beside Current
    // Object and Current Script, Dev.exe sub_587040 / sub_5872B0): Debug
    // Step steps on until the debug context is on this object and this
    // Script (sub_5CCDA0 / sub_5CC9C0); 0: no lock.
    CK_ID lockedObject = 0, lockedScript = 0;

    void Play();
    void Pause();
    void Reset();
    // A whole CK frame (the rest of the current one when traced).
    void StepFrame();
    // Dev "Debug Step": one CKDebugContext step (starts a traced frame if needed).
    void DebugStep();
    // Dev "Leave Behavior": steps until the current behavior is done.
    void LeaveBehavior();
    // Called every UI frame: a running debugger advances (up to budget seconds of steps).
    void Update(double budget);
    // Dev "Execute Script(s)" (Alt+X): each Script's Start is activated and
    // the Script executed once (behaviors after frame 0 do not run); not traced.
    void ExecuteScripts(const std::vector<CK_ID> &scripts);

    CK_ID CurrentObject() const { return m_CurrentObject; }
    CK_ID CurrentScript() const { return m_CurrentScript; }
    CK_ID CurrentBehavior() const { return m_CurrentBehavior; }
    int Frames() const { return m_Frames; }
    const std::vector<Entry> &Log() const { return m_Log; }
    void ClearLog() { m_Log.clear(); }
    // Every breakpoint of the open Scripts.
    struct Breakpoint {
        CK_ID script = 0;
        ExtraData data;
    };
    std::vector<Breakpoint> Breakpoints() const;
    // Called when the document closes.
    void Detach();

private:
    bool Step();       // one guarded debug step; false at the end of the frame
    void BeginFrame();
    void EndFrame();
    void ReadContext();
    bool CheckBreakpoints(); // true when one was reached (logged)
    bool ConditionsHold(const ExtraData &breakpoint) const;
    void Stop(const std::string &why);
    void PauseContext();
    void Note(const std::string &text, CK_ID script = 0, CK_ID behavior = 0);

    Document &m_Document;
    State m_State = State::Stopped;
    bool m_InFrame = false;
    int m_Frames = 0;
    int m_RunFrames = 0;
    CK_ID m_CurrentObject = 0, m_CurrentScript = 0, m_CurrentBehavior = 0;
    CKDWORD m_Action = 0;
    std::vector<Entry> m_Log;
    // Parameter and link breakpoints fire on a change.
    std::map<CK_ID, std::vector<CKBYTE>> m_ParameterValues;
    std::map<CK_ID, bool> m_LinkActive;
    CK_ID m_Leaving = 0;
    std::set<std::pair<int, CK_ID>> m_Disengaged; // (type, object) reached and not re-engaged
    int m_Reached = 0;
    std::vector<Breakpoint> m_ReachedList;
    bool LockHolds() const;
};

// Dev's Remove All (Script Debugger): the breakpoints, as one undo step.
void RemoveBreakpoints(Document &document, const std::vector<Debugger::Breakpoint> &breakpoints);

} // namespace vdev
