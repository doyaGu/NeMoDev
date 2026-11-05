// Dev's status bar (Dev.exe sub_423700 makes its items, sub_4229D0 places
// them from the bar's right end at each size) and its Time Settings
// (Dev.exe dialog 644, the Play button's right click).
#include "ui/app/StatusBar.h"

#include <iterator>
#include <cstdio>
#include <cstdlib>

#include "imgui.h"
#include "editor/ImGuiDrawing.h"
#include "imgui_internal.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "ui/setup/SetupLayout.h"

namespace vdev {

namespace {
bool HideFps(const EditorResponse &time) {
    const auto *display = EditorClient::Find(time, "display");
    if (display && display->type == EDITOR_RECORD)
        for (const auto &field : display->fields)
            if (field.key == "hide-fps" && field.type == EDITOR_INTEGER_VALUE) return field.integer != 0;
    return false;
}
} // namespace

// InterfaceControls.dll's colors (sub_24211910): ZC_MAINBACK 100,
// ZC_TOOLBARBACK 65 (the bar), ZC_TOOLBARBORDER 176 (its outline and texts).

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
        ImGui::PushStyleColor(ImGuiCol_Text, CurrentDevTheme().controls[6]);
        ImGui::RenderTextEllipsis(dl, min, wide, wide.x, text.c_str(), text.c_str() + text.size(), nullptr);
        ImGui::PopStyleColor();
    } else {
        dl->PushClipRect(min, wide, true);
        dl->AddText(min, CurrentDevTheme().controls[6], text.c_str());
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
void EditorStatusBar::Draw(const EditorClient &editor, const StatusBarInfo &info) {
    const auto time = editor.Query("runtime.time");
    const bool savedHideFps = HideFps(time);
    const bool hideFps = ImGui::IsPopupOpen("###TimeSettings") ? m_HideFps : savedHideFps;
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float s = ImGui::GetFontSize() / 13.0f;
    // Dev.exe sub_4209B0: 23 pixels at the bottom of the main window.
    const float height = IM_ROUND(23.0f * s); // whole pixels: crisp lines and bitmaps
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, CurrentDevTheme().controls[3]);
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
        dl->AddRectFilled(o, ImVec2(o.x + width, o.y + height), DevChrome(), 4.5f * s);
        dl->AddRect(ImVec2(o.x + 0.5f, o.y + 0.5f), ImVec2(o.x + width - 1.5f, o.y + height - 1.5f),
                    CurrentDevTheme().controls[6], 4.5f * s, 0, 1.0f);
        auto separator = [&](float right) {
            const ImVec2 a = fromRight(right, 5.0f);
            if (ImTextureID texture = DevBitmap(705)) dl->AddImage(texture, a, ImVec2(a.x + 1.0f * s, a.y + 19.0f * s));
        };

        // Selection: the Schematic's (the element or "%d elements selected"),
        // or the 3D Layout's after it was used.
        BarText(dl, at(10.0f, 6.0f), fromRight(871.0f, 18.0f), info.selection, false);
        separator(866.0f);
        BarText(dl, fromRight(863.0f, 4.0f), fromRight(713.0f, 17.0f), info.action, true);
        separator(712.0f);
        if (info.showCoordinates && info.information.empty()) {
            static const char *const AXES[3] = {"X:", "Y:", "Z:"};
            const float labels[3] = {709.0f, 614.0f, 519.0f};
            for (int i = 0; i < 3; ++i) {
                const float left = labels[i];
                BarText(dl, fromRight(left, 4.0f), fromRight(left - 20.0f, 17.0f), AXES[i], true);
                BarText(dl, fromRight(left - 21.0f, 6.0f), fromRight(left - 91.0f, 18.0f), info.coordinates[i], false);
                if (i < 2) separator(left - 92.0f);
            }
        } else {
            BarText(dl, fromRight(710.0f, 6.0f), fromRight(428.0f, 18.0f), info.information, false, true);
        }

        // The Event Log: the bomb flashes red for 4 s after an error
        // (sub_422830: ZC 10, 250 ms); a click opens the log, a right click
        // clears it (sub_59C5C0).
        const double now = ImGui::GetTime();
        const auto &log = editor.Query("log.summary");
        std::string status;
        for (const auto &field : log.fields) {
            if (field.key == "status") status = field.text;
            if (field.key == "error-revision") {
                if (field.integer != m_LogErrorsSeen) m_BombFlashUntil = now + 4.0;
                m_LogErrorsSeen = field.integer;
            }
        }
        BarText(dl, fromRight(407.0f, 6.0f), fromRight(207.0f, 18.0f),
                status, false, true);
        separator(206.0f);
        // The frame rate: NA when not playing (Dev hides it with Hide Fps).
        const auto &runtime = RuntimeState(editor);
        const bool running = EditorClient::Integer(runtime, "state") == EDITOR_EXECUTION_RUNNING;
        const auto *measured = EditorClient::Find(runtime, "measured-fps");
        const double measuredFps = measured ? measured->real : 0;
        if (!hideFps) {
            std::string fps = "NA";
            if (running && measuredFps > 0) {
                char text[32];
                std::snprintf(text, sizeof(text), "%.1f", measuredFps);
                fps = text;
            }
            BarText(dl, fromRight(185.0f, 7.0f), fromRight(155.0f, 19.0f), fps, false);
        }
        BarText(dl, fromRight(154.0f, 4.0f), fromRight(124.0f, 17.0f), "FPS", true);
        separator(123.0f);
        EditorToolbarFrame area{};
        area.size = sizeof(area);
        area.left = o.x;
        area.top = o.y;
        area.width = width;
        area.height = height;
        area.scale = s;
        editor.Toolbar("application.playback", area);
    }
    ImGui::End();
    PopDarkChrome();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    TimeSettingsDialog(editor);
}

bool EditorStatusBar::Register(EditorApplication &application, std::string &error) {
    const EditorToolbarLocation location{EDITOR_TEXT("application.playback"), EDITOR_TEXT("Playback and status"), {}};
    const struct {
        const char *id, *action, *title;
        float x;
    } controls[] = {{"status.eventlog", "log.open", "Event Log", 427},
                    {"status.profiler", "runtime.toggle-profiler", "Profiler", 203},
                    {"status.rewind", "runtime.reset", "Restore Initial Conditions", 120},
                    {"status.play", "runtime.play-pause", "Play/Pause", 81},
                    {"status.step", "runtime.step-frame", "Step One Frame", 42}};
    std::vector<EditorToolbarItemDesc> items;
    for (const auto &control : controls) {
        EditorToolbarItemDesc item{};
        item.size = sizeof(item);
        item.imguiVersion = EDITOR_IMGUI_VERSION;
        item.id = EditorLiteral(control.id);
        item.provider = EDITOR_TEXT("builtin.status-toolbar");
        item.location = location.id;
        item.action = EditorLiteral(control.action);
        item.title = EditorLiteral(control.title);
        item.x = control.x;
        item.y = 2;
        item.width = 19;
        item.height = 19;
        item.flags = EDITOR_TOOLBAR_RIGHT;
        item.data = this;
        item.draw = [](void *data, const EditorHost *host, const EditorRequest *request,
                       const EditorToolbarFrame *frame) {
            return static_cast<EditorStatusBar *>(data)->DrawControl(EditorClient(host, request->scope.view), *frame);
        };
        items.push_back(item);
    }
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("status.toolbar");
    module.toolbars = &location;
    module.toolbarCount = 1;
    module.toolbarItems = items.data();
    module.toolbarItemCount = uint32_t(items.size());
    return application.Register(module, error);
}
const EditorResponse &EditorStatusBar::RuntimeState(const EditorClient &editor) {
    const auto version = editor.Revision("runtime.state");
    const auto scope = editor.Scope();
    if (!version || version != m_RuntimeVersion || scope.document != m_RuntimeScope.document ||
        scope.view != m_RuntimeScope.view) {
        m_Runtime = editor.Query("runtime.state");
        m_RuntimeScope = scope;
        m_RuntimeVersion = m_Runtime.status == EDITOR_OK ? version : 0;
    }
    return m_Runtime;
}

uint32_t EditorStatusBar::DrawControl(const EditorClient &editor, const EditorToolbarFrame &frame) {
    const std::string action(frame.action.data, frame.action.size);
    const ImVec2 at{IM_ROUND(frame.left), IM_ROUND(frame.top)};
    static const int BOMB[] = {608, 1341, 1342, 1342, 608}, PROFILER[] = {961, 1343, 1344, 1344, 961},
                     REWIND[] = {911, 912, 913, 913, 911}, PLAY[] = {907, 908, 910, 910, 907},
                     STEP[] = {914, 915, 916, 916, 914};
    const int (*icons)[5] = &STEP;
    const char *id = "step", *title = "Step One Frame";
    bool checked = false;
    if (action == "log.open") {
        icons = &BOMB;
        id = "eventlog";
        title = "Event Log";
        const auto now = ImGui::GetTime();
        if (now < m_BombFlashUntil && int(now * 4) % 2 == 0)
            ImGui::GetWindowDrawList()->AddRectFilled(at, {at.x + frame.width, at.y + frame.height},
                                                      CurrentDevTheme().controls[10]);
    } else if (action == "runtime.toggle-profiler") {
        icons = &PROFILER;
        id = "profiler";
        title = "Profiler";
        checked =
            EditorClient::Integer(editor.Query("workspace.role", {TextArgument("role", "main.profiler")}), "view") != 0;
    } else if (action == "runtime.reset") {
        icons = &REWIND;
        id = "rewind";
        title = "Restore Initial Conditions";
    } else if (action == "runtime.play-pause") {
        icons = &PLAY;
        id = "play";
        title = "Play/Pause";
        checked = EditorClient::Integer(RuntimeState(editor), "state") == EDITOR_EXECUTION_RUNNING;
    }
    bool right = false;
    const bool clicked = DevBitmapButton(id, at, *icons, checked, frame.enabled, frame.scale, title, &right);
    if (right && action == "runtime.play-pause") m_OpenTimeSettings = true;
    if (right && action == "log.open") {
        editor.Command("log.clear");
        m_BombFlashUntil = 0;
    }
    if (clicked && action == "log.open") m_BombFlashUntil = 0;
    return clicked;
}

// Dev's Time Settings (dialog 644, in its dialog units): the frame rate
// free, synchronized to the screen or limited, the time scale, the
// behavioral rate linked to the frame rate or limited, the delta time's
// bounds (CKTimeManager), Hide Fps.
void EditorStatusBar::TimeSettingsDialog(const EditorClient &editor) {
    const char *id = "###TimeSettings";
    if (m_OpenTimeSettings) {
        const auto settings = editor.Query("runtime.time");
        if (settings.status != EDITOR_OK) { m_OpenTimeSettings = false; return; }
        m_HideFps = HideFps(settings);
        for (const auto &field : settings.fields) {
            if (field.key == "frame-mode") m_TimeFrame = int(field.integer);
            if (field.key == "behavior-mode") m_TimeBehavior = int(field.integer);
            if (field.key == "frame-limit") m_TimeFrameLimit = float(field.real);
            if (field.key == "behavior-limit") m_TimeBehaviorLimit = float(field.real);
            if (field.key == "scale") m_TimeScale = float(field.real);
            if (field.key == "minimum") m_TimeMin = float(field.real);
            if (field.key == "maximum") m_TimeMax = float(field.real);
        }
        ImGui::OpenPopup(id);
    }
    m_OpenTimeSettings = false;
    if (!ImGui::IsPopupOpen(id)) return;
    const float s = setup::PageUnit();
    vi::DialogLayout layout;
    if (!vi::BeginScrollableDialog(id, "Time Settings", {620 * s, 260 * s}, s, layout)) return;
    if (vi::BeginDialogContent("##time-settings", layout)) {
        auto number = [&](const char *field, float &value) {
            char text[32];
            std::snprintf(text, sizeof(text), "%g", value);
            std::string typed = text;
            const auto at = ImGui::GetCursorScreenPos();
            if (vi::Edit(field, at, {at.x + ImGui::GetContentRegionAvail().x, at.y + ImGui::GetFrameHeight()},
                         typed, vi::CENTER)) {
                const float parsed = static_cast<float>(std::atof(typed.c_str()));
                if (parsed > 0.0f) value = parsed;
            }
        };
        {
            setup::SetupColumns columns("##time-sections", 2);
            if (columns) {
                columns.Next();
                ImGui::SeparatorText("Frame Rate");
                const float radio = 12 * s;
                if (vi::Radio("##free", ImGui::GetCursorScreenPos(), radio, m_TimeFrame == 0, "Free")) m_TimeFrame = 0;
                if (vi::Radio("##sync", ImGui::GetCursorScreenPos(), radio, m_TimeFrame == 1, "Synchronize to Screen"))
                    m_TimeFrame = 1;
                if (vi::Radio("##limit", ImGui::GetCursorScreenPos(), radio, m_TimeFrame == 2, "Limit:")) m_TimeFrame = 2;
                {
                    setup::SetupForm form("##frame-settings");
                    if (form) {
                        form.Row("Limit (FPS)"); number("##frame", m_TimeFrameLimit);
                        form.Row("Scale Time (X)"); number("##scale", m_TimeScale);
                    }
                }
                columns.Next();
                ImGui::SeparatorText("Behavior Rate");
                if (vi::Radio("##linked", ImGui::GetCursorScreenPos(), radio, m_TimeBehavior == 0, "Linked to Frame Rate"))
                    m_TimeBehavior = 0;
                if (vi::Radio("##behlimit", ImGui::GetCursorScreenPos(), radio, m_TimeBehavior == 1, "Limit:")) m_TimeBehavior = 1;
                {
                    setup::SetupForm form("##behavior-settings");
                    if (form) { form.Row("Limit (FPS)"); number("##behavior", m_TimeBehaviorLimit); }
                }
                ImGui::SeparatorText("Delta Time");
                setup::SetupForm form("##delta-time");
                if (form) {
                    form.Row("Min:"); number("##min", m_TimeMin);
                    form.Row("Max:"); number("##max", m_TimeMax);
                }
            }
        }
        vi::Check("##hidefps", ImGui::GetCursorScreenPos(), 12 * s, m_HideFps);
        ImGui::SameLine();
        ImGui::TextUnformatted("Hide Fps");
    }
    vi::EndDialogContent();
    const bool cancel = vi::DialogButton("Cancel", {layout.right - 60 * s, layout.buttonsY}, s, ImGuiKey_Escape);
    const bool ok = vi::DialogButton("OK", {layout.right - 130 * s, layout.buttonsY}, s, ImGuiKey_Enter);
    if (ok) {
        const EditorValue hideFps{EDITOR_TEXT("hide-fps"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(m_HideFps)};
        const EditorValue settings[] = {
            {EDITOR_TEXT("frame-mode"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(m_TimeFrame)},
            {EDITOR_TEXT("behavior-mode"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(m_TimeBehavior)},
            {EDITOR_TEXT("frame-limit"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, m_TimeFrameLimit},
            {EDITOR_TEXT("behavior-limit"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, m_TimeBehaviorLimit},
            {EDITOR_TEXT("scale"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, m_TimeScale},
            {EDITOR_TEXT("minimum"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, m_TimeMin},
            {EDITOR_TEXT("maximum"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, m_TimeMax},
            {EDITOR_TEXT("display"), EDITOR_RECORD, {}, {}, 0, {}, {}, {}, 0, {}, {&hideFps, 1}}};
        const auto result = editor.Command("runtime.set-time", {std::begin(settings), std::end(settings)});
        if (result.status != EDITOR_OK && result.status != EDITOR_QUEUED) { vi::EndDialog(); return; }
    }
    if (ok || cancel) ImGui::CloseCurrentPopup();
    vi::EndDialog();
}

} // namespace vdev
