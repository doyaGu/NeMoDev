// Dev's Script Debugger (dialog 670, Dev.exe sub_585EF0 / sub_586150 /
// sub_5866B0; language pack [Schematic]<Debug>): Trace Options, the Debug
// Context (locked to an object or a Script by the boxes beside them), Trace
// Tools, Breakpoints and the Debug Log, on vdev::Debugger. A modeless
// VIDialog laid out after the template; Play, Pause, Reset and Step One
// Frame are the status bar's and the 3D Layout's (as in Dev).

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "imgui.h"

#include "CKAll.h"

#include "core/graph/Debugger.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "ui/panels/EditorPanels.h"
#include "ui/app/Preferences.h"
#include "ui/controls/ObjectText.h"
#include "ui/schematic/SchematicCanvas.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

namespace {

std::string NameOf(CKContext *context, CK_ID id, const char *none = "none") {
    CKObject *object = context && id ? context->GetObject(id) : nullptr;
    return object ? DisplayName(object, none) : std::string(none);
}

// The bitmaps (resvis.dll): Breakpoint Reached lit and not (sub_587820
// blinks them), the Debug Log button's arrows (sub_586150's SetBitmaps).
constexpr int REACHED_LIT = 1109, REACHED_DIM = 1110, LOG_SHOW = 1114, LOG_HIDE = 1117;
// The template's sizes: with the Debug Log (static 1399) and without (1735).
constexpr float WIDTH = 408.0f, HEIGHT = 222.0f, HEIGHT_NO_LOG = 125.0f;
// The template sits 7.5 pixels inside the panel (as Dev shows it).
constexpr float PAD = 7.5f;
// The blinking after a breakpoint is reached (Dev's timer counts down).
constexpr double BLINK_SECONDS = 2.0;

} // namespace

void EditorPanels::DebuggerPanel(bool *open) {
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 units(WIDTH, m_DebugLogShown ? HEIGHT : HEIGHT_NO_LOG);
    const ImVec2 client = vi::TemplateClient(units, s, PAD);
    ImVec2 o;
    if (!vi::BeginWindow("Script Debugger", "Script Debugger", client, s, o)) {
        vi::EndWindow();
        return;
    }
    const vi::TemplateLayout l = vi::LayoutTemplate(o, units, s, PAD);
    vi::TemplatePanel(l);
    Debugger *debugger = m_State.debugger.get();
    CKContext *context = m_State.HasDocument() ? m_State.document->Context() : nullptr;
    const bool usable = debugger && context;
    auto text = [&](float x, float y, float w, float h, const char *label, bool bold = false) {
        vi::Text(l.Min(x, y), l.Max(x, y, w, h), label, bold);
    };

    // Trace Options.
    text(6, 4, 82, 8, "Trace Options", true);
    vi::Frame(l.Min(5, 14), l.Max(5, 14, 182, 31));
    // Debug Inside Frames: the full debug mode (it needs Trace Mode, turned on with it).
    bool inside = usable && debugger->insideFrames;
    if (vi::CheckButton("Debug Inside Frames", l.Min(8, 18), l.Max(8, 18, 83, 10), inside, "Debug Inside Frames", usable)) {
        debugger->insideFrames = inside;
        if (inside) debugger->trace = true;
    }
    Preferences &preferences = m_State.preferences;
    if (vi::CheckButton("Redraw Schematic Only When Breakpoint Reached", l.Min(8, 32), l.Max(8, 32, 177, 8),
                        preferences.redrawOnlyAtBreakpoint, "Redraw Schematic Only When Breakpoint Reached") &&
        debugger)
        debugger->redrawOnlyAtBreakpoint = preferences.redrawOnlyAtBreakpoint;

    // Debug Context: the combos show where the debugger is; a box locks it
    // to the object or the Script chosen (the object's combo then fixed;
    // locking a Script locks its owner too, sub_5872B0).
    text(5, 49, 82, 8, "Debug Context", true);
    vi::Frame(l.Min(5, 59), l.Max(5, 59, 182, 49));
    text(9, 65, 57, 8, "Current Object");
    text(9, 80, 57, 8, "Current Script");
    text(9, 95, 57, 8, "Current Behavior");
    if (usable) {
        if (!debugger->lockedObject) m_Doc.debugObject = debugger->CurrentObject();
        if (!debugger->lockedScript) m_Doc.debugScript = debugger->CurrentScript();
    }
    std::vector<CK_ID> owners, scripts;
    if (usable)
        for (const ScriptInfo &script : m_State.document->Scripts()) {
            if (script.ownerId && std::find(owners.begin(), owners.end(), script.ownerId) == owners.end())
                owners.push_back(script.ownerId);
            if (!m_Doc.debugObject || script.ownerId == m_Doc.debugObject) scripts.push_back(script.id);
        }
    auto names = [&](const std::vector<CK_ID> &ids) {
        std::vector<std::string> list{"none"};
        for (CK_ID id : ids) list.push_back(NameOf(context, id));
        return list;
    };
    const bool objectLocked = usable && debugger->lockedObject != 0;
    const bool scriptLocked = usable && debugger->lockedScript != 0;
    if (!objectLocked && !scriptLocked) {
        const int chosen = vi::Combo("##object", l.Min(73, 65), l.Max(73, 65, 94, 12), NameOf(context, m_Doc.debugObject),
                                     [&] { return names(owners); }, 0);
        if (chosen >= 0) m_Doc.debugObject = chosen == 0 ? 0 : owners[chosen - 1];
    } else {
        vi::Text(ImVec2(l.Min(73, 65).x + 3.0f * s, l.Min(73, 65).y + 2.0f * s), l.Max(73, 65, 94, 12),
                 NameOf(context, m_Doc.debugObject).c_str());
        ImGui::GetWindowDrawList()->AddLine(ImVec2(l.Min(73, 65).x, l.Max(73, 65, 94, 12).y - 1.5f),
                                            ImVec2(l.Max(73, 65, 94, 12).x, l.Max(73, 65, 94, 12).y - 1.5f), vi::Color(4));
    }
    bool lockObject = objectLocked;
    if (vi::CheckButton("##lockobject", l.Min(175, 65), l.Max(175, 65, 8, 8), lockObject, nullptr, usable && !scriptLocked)) {
        debugger->lockedObject = lockObject ? m_Doc.debugObject : 0;
        if (lockObject && !m_Doc.debugObject) debugger->lockedObject = 0;
    }
    if (!scriptLocked) {
        const int chosen = vi::Combo("##script", l.Min(73, 80), l.Max(73, 80, 94, 12), NameOf(context, m_Doc.debugScript),
                                     [&] { return names(scripts); }, 0);
        if (chosen >= 0) m_Doc.debugScript = chosen == 0 ? 0 : scripts[chosen - 1];
    } else {
        vi::Text(ImVec2(l.Min(73, 80).x + 3.0f * s, l.Min(73, 80).y + 2.0f * s), l.Max(73, 80, 94, 12),
                 NameOf(context, m_Doc.debugScript).c_str());
    }
    bool lockScript = scriptLocked;
    if (vi::CheckButton("##lockscript", l.Min(175, 80), l.Max(175, 80, 8, 8), lockScript, nullptr, usable)) {
        CKBehavior *script = lockScript ? CKBehavior::Cast(context->GetObject(m_Doc.debugScript)) : nullptr;
        debugger->lockedScript = script ? script->GetID() : 0;
        if (script && script->GetOwner()) {
            m_Doc.debugObject = script->GetOwner()->GetID();
            debugger->lockedObject = m_Doc.debugObject;
        } else if (!lockScript) {
            debugger->lockedObject = 0;
        }
    }
    text(73, 95, 94, 12, usable ? NameOf(context, debugger->CurrentBehavior()).c_str() : "none");

    // Trace Tools: Debug Step and Leave Behavior with Debug Inside Frames.
    text(192, 4, 82, 8, "Trace Tools", true);
    vi::Frame(l.Min(192, 14), l.Max(192, 14, 99, 94));
    const bool stepping = usable && debugger->trace && debugger->insideFrames;
    if (vi::Button("Debug Step", l.Min(196, 18), l.Max(196, 18, 57, 10), "Debug Step", stepping)) debugger->DebugStep();
    if (vi::Button("Leave Behavior", l.Min(196, 32), l.Max(196, 32, 57, 10), "Leave Behavior", stepping))
        debugger->LeaveBehavior();
    text(196, 50, 50, 8, "Max CK Frames");
    text(196, 65, 50, 8, "Simulated FPS");
    if (usable) {
        // The text typed is taken when the edit is left.
        if (m_MaxFramesText.empty()) m_MaxFramesText = std::to_string(debugger->maxFrames);
        if (m_FpsText.empty()) m_FpsText = std::to_string(static_cast<int>(debugger->simulatedFps + 0.5f));
        bool done = false;
        vi::Edit("##maxframes", l.Min(253, 50), l.Max(253, 50, 33, 12), m_MaxFramesText, vi::CENTER, &done);
        if (done) debugger->maxFrames = std::max(0, std::atoi(m_MaxFramesText.c_str()));
        if (!ImGui::IsItemActive()) m_MaxFramesText = std::to_string(debugger->maxFrames);
        done = false;
        vi::Edit("##fps", l.Min(253, 65), l.Max(253, 65, 33, 12), m_FpsText, vi::CENTER, &done);
        if (done) debugger->simulatedFps = std::max(1.0f, static_cast<float>(std::atof(m_FpsText.c_str())));
        if (!ImGui::IsItemActive()) m_FpsText = std::to_string(static_cast<int>(debugger->simulatedFps + 0.5f));
    }

    // Breakpoints: Add Breakpoint picks an element in the Schematic;
    // Breakpoint Reached blinks, then stays lit while Goto has reached ones
    // to go to, one after the other (sub_587B10).
    text(296, 4, 82, 8, "Breakpoints", true);
    vi::Frame(l.Min(296, 14), l.Max(296, 14, 106, 94));
    if (vi::Button("Add Breakpoint", l.Min(303, 18), l.Max(303, 18, 94, 10), "Add Breakpoint", usable))
        m_Canvas.PickBreakpoint();
    if (vi::Button("Configure Breakpoints", l.Min(303, 30), l.Max(303, 30, 94, 10), "Configure Breakpoints", usable))
        m_Canvas.ConfigureBreakpoints();
    if (vi::Button("Breakpoint List", l.Min(303, 65), l.Max(303, 65, 94, 10), "Breakpoint List", usable))
        m_Canvas.OpenBreakpointList();
    const size_t reachedCount = usable ? debugger->ReachedBreakpoints().size() : 0;
    if (usable && debugger->Reached() != m_DebugReached) {
        m_DebugReached = debugger->Reached();
        m_DebugBlinkStart = ImGui::GetTime();
        m_DebugGoto = -1;
    }
    bool lit = reachedCount > 0;
    if (lit && ImGui::GetTime() - m_DebugBlinkStart < BLINK_SECONDS)
        lit = static_cast<int>((ImGui::GetTime() - m_DebugBlinkStart) / 0.25) % 2 == 0;
    ImVec2 size;
    if (ImTextureID bitmap = DevBitmap(lit ? REACHED_LIT : REACHED_DIM, &size)) {
        const ImVec2 at = l.Min(303, 79);
        ImGui::GetWindowDrawList()->AddImage(bitmap, at, ImVec2(at.x + size.x * s, at.y + size.y * s));
    }
    if (vi::Button("Goto", l.Min(324, 95), l.Max(324, 95, 50, 10), "Goto", reachedCount > 0)) {
        const auto &reached = debugger->ReachedBreakpoints();
        m_DebugGoto = (m_DebugGoto + 1) % static_cast<int>(reached.size());
        m_Canvas.GoToBreakpoint(reached[m_DebugGoto].script, reached[m_DebugGoto].data);
    }

    // The Debug Log: its button shows or hides it (sub_5866B0 1200).
    if (ImTextureID arrow = DevBitmap(m_DebugLogShown ? LOG_HIDE : LOG_SHOW, &size)) {
        const ImVec2 at(l.Min(4, 114).x, l.Min(4, 114).y - 3.0f * s);
        ImGui::GetWindowDrawList()->AddImage(arrow, at, ImVec2(at.x + size.x * s, at.y + size.y * s));
        ImGui::SetCursorScreenPos(at);
        if (ImGui::InvisibleButton("Debug Log", ImVec2(66.0f * 1.5f * s, size.y * s))) m_DebugLogShown = !m_DebugLogShown;
        vi::Text(ImVec2(at.x + size.x * s + 3.0f * s, at.y + (size.y * s - ImGui::GetFontSize()) * 0.5f),
                 ImVec2(at.x + 66.0f * 1.5f * s, at.y + size.y * s), "Debug Log");
    }
    if (m_DebugLogShown) {
        if (vi::Button("Clear", l.Min(82, 113), l.Max(82, 113, 50, 10), "Clear", usable)) debugger->ClearLog();
        static const std::vector<vi::ListColumn> COLUMNS{{"Debugging Message List", 0.0f}};
        const std::vector<Debugger::Entry> empty;
        const std::vector<Debugger::Entry> &log = usable ? debugger->Log() : empty;
        bool twice = false;
        const int row = vi::ListView("##debuglog", l.Min(5, 125), l.Max(5, 125, 398, 93), s, COLUMNS,
                                     static_cast<int>(log.size()),
                                     [&](int r, int) { return AnsiToUtf8(log[r].text); }, nullptr, &twice, nullptr, true);
        if (row >= 0 && twice && log[row].script) ShowScript(log[row].script, ElementRef(), log[row].behavior);
    }

    if (vi::DialogButton("Close", ImVec2(l.right - 60.0f * s, l.buttonsY), s)) *open = false;
    vi::EndWindow();
}

} // namespace vdev
