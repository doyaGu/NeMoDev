// Dev's status bar (Dev.exe sub_423700 makes its items, sub_4229D0 places
// them from the bar's right end at each size) and its Time Settings
// (Dev.exe dialog 644, the Play button's right click).
#include "ui/app/EditorApp.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "imgui.h"
#include "imgui_internal.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

// InterfaceControls.dll's colors (sub_24211910): ZC_MAINBACK 100,
// ZC_TOOLBARBACK 65 (the bar), ZC_TOOLBARBORDER 176 (its outline and texts).
constexpr ImU32 MAIN_BACK = IM_COL32(100, 100, 100, 255);
constexpr ImU32 BAR_BACK = IM_COL32(65, 65, 65, 255);
constexpr ImU32 BAR_TEXT = IM_COL32(176, 176, 176, 255);

// A static text of the bar in its rectangle (Dev's VIStaticText: top left,
// clipped; the Event Log's text ends with "..."), in the VI font (GetVIFont
// 0 or 1 by sub_41F8D0): laid out at its height, then narrowed about its
// left edge to GDI's width 5.
static void BarText(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const std::string &text, bool bold,
             bool ellipsis = false) {
    // PushFont takes a base size; ImGui applies DPI and user scaling itself.
    const float s = ImGui::GetStyle().FontSizeBase / 13.0f;
    const float narrow = VIFontWidth(bold);
    const ImVec2 wide(min.x + (max.x - min.x) / narrow, max.y);
    const int first = dl->VtxBuffer.Size;
    ImGui::PushFont(VIFont(bold), VIFontHeight(bold) * s);
    if (ellipsis) {
        ImGui::PushStyleColor(ImGuiCol_Text, BAR_TEXT);
        ImGui::RenderTextEllipsis(dl, min, wide, wide.x, text.c_str(), text.c_str() + text.size(), nullptr);
        ImGui::PopStyleColor();
    } else {
        dl->PushClipRect(min, wide, true);
        dl->AddText(min, BAR_TEXT, text.c_str());
        dl->PopClipRect();
    }
    ImGui::PopFont();
    if (narrow != 1.0f)
        for (int i = first; i < dl->VtxBuffer.Size; ++i) dl->VtxBuffer[i].pos.x = min.x + (dl->VtxBuffer[i].pos.x - min.x) * narrow;
}

// Dev's items, in Dev pixels from the bar's top and its right end R
// (sub_4229D0): Selection (L + 10 .. R - 871), Action (bold, R - 863 ..
// R - 713), X / Y / Z (R - 709 .. R - 428) or the info text in their place,
// the Event Log (bomb R - 427, text R - 407 .. R - 207), the Profiler
// (R - 203), the frame rate (R - 185, "FPS" bold R - 154), Restore Initial
// Conditions (R - 120), Play / Pause (R - 81), Step One Frame (R - 42);
// dotted separators (resvis.dll 705) between them.
void EditorApp::StatusBar() {
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float s = ImGui::GetFontSize() / 13.0f;
    // Dev.exe sub_4209B0: 23 pixels at the bottom of the main window.
    const float height = IM_ROUND(23.0f * s); // whole pixels: crisp lines and bitmaps
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, MAIN_BACK);
    PushDarkChrome();
    const bool open = ImGui::BeginViewportSideBar("##StatusBar", viewport, ImGuiDir_Down, height,
                                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                                      ImGuiWindowFlags_NoScrollWithMouse);
    if (open) {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 o = ImGui::GetWindowPos();
        const float width = ImGui::GetWindowWidth();
        const float R = width / s;
        auto at = [&](float x, float y) { return ImVec2(IM_ROUND(o.x + x * s), IM_ROUND(o.y + y * s)); };
        auto fromRight = [&](float x, float y) { return at(R - x, y); };
        // The bar (Dev.exe sub_419650, WM_PAINT): RoundRect over the whole
        // window, ellipse 9 x 9, ZC_TOOLBARBACK brush and a ZC_TOOLBARBORDER
        // pen; the window's round region shows the main back at the corners.
        dl->AddRectFilled(o, ImVec2(o.x + width, o.y + height), BAR_BACK, 4.5f * s);
        dl->AddRect(ImVec2(o.x + 0.5f, o.y + 0.5f), ImVec2(o.x + width - 1.5f, o.y + height - 1.5f), BAR_TEXT, 4.5f * s,
                    0, 1.0f);
        auto separator = [&](float right) {
            const ImVec2 a = fromRight(right, 5.0f);
            if (ImTextureID texture = DevBitmap(705)) dl->AddImage(texture, a, ImVec2(a.x + 1.0f * s, a.y + 19.0f * s));
        };

        // Selection: the Schematic's (the element or "%d elements selected"),
        // or the 3D Layout's after it was used.
        const Selection &selection = m_State.selection;
        std::string selected = "Nothing selected";
        if (selection.items.size() == 1) {
            const ElementRef &item = selection.items[0];
            CKObject *object = m_State.HasDocument() ? m_State.document->Context()->GetObject(item.object) : nullptr;
            selected = object && object->GetName() ? AnsiToUtf8(object->GetName()) : std::string("1 element");
        } else if (selection.items.size() > 1) {
            selected = std::to_string(selection.items.size()) + " elements selected";
        }
        const std::string layoutAction = m_Layout.ActionText();
        if (!layoutAction.empty()) selected = m_Layout.SelectionText();
        BarText(dl, at(10.0f, 6.0f), fromRight(871.0f, 18.0f), selected, false);
        separator(866.0f);
        BarText(dl, fromRight(863.0f, 4.0f), fromRight(713.0f, 17.0f),
                layoutAction.empty() ? std::string(m_Canvas.ActionText()) : layoutAction, true);
        separator(712.0f);
        // X / Y / Z once a 3D Layout tool is used (sub_424CB0), else the info text.
        std::string coordinates[3];
        if (m_Layout.Coordinates(coordinates) && m_Layout.InfoText().empty()) {
            static const char *const AXES[3] = {"X:", "Y:", "Z:"};
            const float labels[3] = {709.0f, 614.0f, 519.0f};
            for (int i = 0; i < 3; ++i) {
                const float left = labels[i];
                BarText(dl, fromRight(left, 4.0f), fromRight(left - 20.0f, 17.0f), AXES[i], true);
                BarText(dl, fromRight(left - 21.0f, 6.0f), fromRight(left - 91.0f, 18.0f), coordinates[i], false);
                if (i < 2) separator(left - 92.0f);
            }
        } else {
            BarText(dl, fromRight(710.0f, 6.0f), fromRight(428.0f, 18.0f), m_Layout.InfoText(), false, true);
        }

        // The Event Log: the bomb flashes red for 4 s after an error
        // (sub_422830: ZC 10, 250 ms); a click opens the log, a right click
        // clears it (sub_59C5C0).
        const double now = ImGui::GetTime();
        for (size_t i = std::min(m_LogSeen, m_State.log.size()); i < m_State.log.size(); ++i)
            if (m_State.log[i].level == LogLevel::Error) m_BombFlashUntil = now + 4.0;
        m_LogSeen = m_State.log.size();
        {
            static const int BOMB[5] = {608, 1341, 1342, 1342, 608};
            const ImVec2 a = fromRight(427.0f, 2.0f);
            if (now < m_BombFlashUntil && static_cast<int>(now * 4.0) % 2 == 0)
                dl->AddRectFilled(a, ImVec2(a.x + 19.0f * s, a.y + 19.0f * s), IM_COL32(255, 0, 0, 255));
            bool right = false;
            if (DevBitmapButton("eventlog", a, BOMB, false, true, s, "Event Log", &right)) {
                m_ShowLog = true;
                ImGui::SetWindowFocus("Event Log");
                m_BombFlashUntil = 0.0;
            }
            if (right) {
                m_State.log.clear();
                m_State.Log(LogLevel::Info, "Event Log Cleared");
                m_LogSeen = m_State.log.size();
                m_BombFlashUntil = 0.0;
            }
        }
        BarText(dl, fromRight(407.0f, 6.0f), fromRight(207.0f, 18.0f),
                m_State.log.empty() || !m_State.log.back().status ? std::string() : m_State.log.back().text, false, true);
        separator(206.0f);
        static const int PROFILER[5] = {961, 1343, 1344, 1344, 961};
        if (DevBitmapButton("profiler", fromRight(203.0f, 2.0f), PROFILER, m_Tools.showProfiler, true, s, "Profiler"))
            m_Tools.showProfiler = !m_Tools.showProfiler;
        // The frame rate: NA when not playing (Dev hides it with Hide Fps).
        Debugger *debugger = m_State.debugger.get();
        const bool running = debugger && debugger->GetState() == Debugger::State::Running;
        if (!m_HideFps) {
            std::string fps = "NA";
            CKStats stats = {};
            if (running && m_State.HasDocument()) m_State.document->Context()->GetProfileStats(&stats);
            if (running && stats.TotalFrameTime > 0.0f) {
                char text[32];
                std::snprintf(text, sizeof(text), "%.1f", 1000.0f / stats.TotalFrameTime);
                fps = text;
            }
            BarText(dl, fromRight(185.0f, 7.0f), fromRight(155.0f, 19.0f), fps, false);
        }
        BarText(dl, fromRight(154.0f, 4.0f), fromRight(124.0f, 17.0f), "FPS", true);
        separator(123.0f);
        // Restore Initial Conditions, Play / Pause (pressed while playing;
        // a right click opens the Time Settings), Step One Frame.
        const bool usable = m_State.HasDocument() && debugger;
        static const int REWIND[5] = {911, 912, 913, 913, 911};
        static const int PLAY[5] = {907, 908, 910, 910, 907};
        static const int STEP[5] = {914, 915, 916, 916, 914};
        if (DevBitmapButton("rewind", fromRight(120.0f, 2.0f), REWIND, false, usable, s,
                            "Restore Initial Conditions (Ctrl+Backspace)"))
            debugger->Reset();
        bool timeSettings = false;
        if (DevBitmapButton("play", fromRight(81.0f, 2.0f), PLAY, running, usable, s, "Play/Pause (Ctrl+Spacebar)",
                            &timeSettings)) {
            if (running) debugger->Pause();
            else debugger->Play();
        }
        if (timeSettings) m_OpenTimeSettings = true;
        if (DevBitmapButton("step", fromRight(42.0f, 2.0f), STEP, false, usable, s, "Step One Frame (Alt+Spacebar)"))
            debugger->StepFrame();
    }
    ImGui::End();
    PopDarkChrome();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    TimeSettingsDialog();
}

// Dev's Time Settings (dialog 644, in its dialog units): the frame rate
// free, synchronized to the screen or limited, the time scale, the
// behavioral rate linked to the frame rate or limited, the delta time's
// bounds (CKTimeManager), Hide Fps.
void EditorApp::TimeSettingsDialog() {
    const char *id = "###TimeSettings";
    CKContext *context = m_State.HasDocument() ? m_State.document->Context() : m_State.host.Context();
    CKTimeManager *time = context ? context->GetTimeManager() : nullptr;
    if (m_OpenTimeSettings && time) {
        const CKDWORD options = time->GetLimitOptions();
        m_TimeFrame = options & CK_FRAMERATE_LIMIT ? 2 : options & CK_FRAMERATE_SYNC ? 1 : 0;
        m_TimeBehavior = options & CK_BEHRATE_LIMIT ? 1 : 0;
        m_TimeFrameLimit = time->GetFrameRateLimit();
        m_TimeBehaviorLimit = time->GetBehavioralRateLimit();
        m_TimeScale = time->GetTimeScaleFactor();
        m_TimeMin = time->GetMinimumDeltaTime();
        m_TimeMax = time->GetMaximumDeltaTime();
        ImGui::OpenPopup(id);
    }
    m_OpenTimeSettings = false;
    if (!time || !ImGui::IsPopupOpen(id)) return;
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 client(231.0f * 1.5f * s, 95.0f * 1.625f * s + 26.0f * s);
    ImVec2 o;
    if (!vi::BeginDialog(id, "Time Settings", client, s, o)) return;
    auto P = [&](float x, float y) { return ImVec2(o.x + x * 1.5f * s, o.y + y * 1.625f * s); };
    ImDrawList *dl = ImGui::GetWindowDrawList();
    // The controls on a light panel, the buttons on the dialog's body below.
    vi::Panel(ImVec2(o.x + 2.0f * s, o.y + 2.0f * s), ImVec2(o.x + client.x - 2.0f * s, P(0, 95).y), s);
    // The frames (SS_BLACKFRAME).
    dl->AddRect(P(4, 4), P(109, 77), vi::Color(0));
    dl->AddRect(P(113, 5), P(226, 45), vi::Color(0));
    dl->AddRect(P(113, 48), P(226, 77), vi::Color(0));
    auto number = [&](const char *field, float x, float y, float w, float &value) {
        char text[32];
        std::snprintf(text, sizeof(text), "%g", value);
        std::string typed = text;
        if (vi::Edit(field, P(x, y), P(x + w, y + 12), typed, vi::CENTER)) {
            const float parsed = static_cast<float>(std::atof(typed.c_str()));
            if (parsed > 0.0f) value = parsed;
        }
    };
    const float radio = 8.0f * 1.5f * s;
    vi::Text(P(8, 8), P(60, 16), "Frame Rate", true);
    if (vi::Radio("##free", P(14, 21), radio, m_TimeFrame == 0, "Free")) m_TimeFrame = 0;
    if (vi::Radio("##sync", P(14, 33), radio, m_TimeFrame == 1, "Synchronize to Screen")) m_TimeFrame = 1;
    if (vi::Radio("##limit", P(14, 45), radio, m_TimeFrame == 2, "Limit:")) m_TimeFrame = 2;
    number("##frame", 49, 45, 18, m_TimeFrameLimit);
    vi::Text(P(70, 46), P(90, 56), "FPS");
    vi::Text(P(16, 62), P(66, 72), "Scale Time");
    number("##scale", 69, 61, 20, m_TimeScale);
    vi::Text(P(91, 62), P(102, 72), "X");
    vi::Text(P(118, 8), P(170, 16), "Behavior Rate", true);
    if (vi::Radio("##linked", P(117, 19), radio, m_TimeBehavior == 0, "Linked to Frame Rate")) m_TimeBehavior = 0;
    if (vi::Radio("##behlimit", P(117, 31), radio, m_TimeBehavior == 1, "Limit:")) m_TimeBehavior = 1;
    number("##behavior", 153, 32, 18, m_TimeBehaviorLimit);
    vi::Text(P(174, 33), P(194, 43), "FPS");
    vi::Text(P(117, 50), P(160, 58), "Delta Time", true);
    vi::Text(P(117, 63), P(132, 71), "Min:");
    number("##min", 134, 62, 32, m_TimeMin);
    vi::Text(P(169, 63), P(186, 71), "Max:");
    number("##max", 189, 62, 32, m_TimeMax);
    vi::Check("##hidefps", P(4, 83), 8.0f * 1.5f * s, m_HideFps);
    vi::Text(P(14, 83), P(100, 93), "Hide Fps");
    const float by = o.y + client.y - 23.0f * s;
    const bool cancel = vi::DialogButton("Cancel", ImVec2(o.x + client.x - 70.0f * s, by), s, ImGuiKey_Escape);
    const bool ok = vi::DialogButton("OK", ImVec2(o.x + client.x - 140.0f * s, by), s, ImGuiKey_Enter);
    if (ok) {
        static const CK_FRAMERATE_LIMITS FRAME[3] = {CK_FRAMERATE_FREE, CK_FRAMERATE_SYNC, CK_FRAMERATE_LIMIT};
        time->ChangeLimitOptions(FRAME[std::clamp(m_TimeFrame, 0, 2)], m_TimeBehavior ? CK_BEHRATE_LIMIT : CK_BEHRATE_SYNC);
        time->SetFrameRateLimit(m_TimeFrameLimit);
        time->SetBehavioralRateLimit(m_TimeBehaviorLimit);
        time->SetTimeScaleFactor(m_TimeScale);
        time->SetMinimumDeltaTime(m_TimeMin);
        time->SetMaximumDeltaTime(m_TimeMax);
        m_State.preferences.SetBool("Time Settings\\Hide Fps", m_HideFps);
    }
    if (cancel) m_HideFps = m_State.preferences.Bool("Time Settings\\Hide Fps");
    if (ok || cancel) ImGui::CloseCurrentPopup();
    vi::EndDialog();
}

} // namespace vdev
