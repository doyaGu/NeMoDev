#include "ui/layout/LayoutWindow.h"
#include "ui/schematic/Snapshots.h"

#include "ui/render/UiRenderer.h"
#include "core/layout/SdlGpuInterop.h"
#include <SDL3/SDL.h>

#include "imgui_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "ui/controls/DevBitmaps.h"
#include "core/level/LevelCommands.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/TypeAhead.h"
#include "core/level/ObjectEditCommands.h"
#include "core/runtime/TextEncoding.h"



namespace vdev {

namespace {

// Dev's 3D Layout preferences ("3D Layout\..." keys, General Preferences).
float Component(const std::string &text, int index, char separator) {
    size_t start = 0;
    for (int i = 0; i < index; ++i) {
        const size_t next = text.find(separator, start);
        if (next == std::string::npos) return 0.0f;
        start = next + 1;
    }
    return static_cast<float>(std::atof(text.c_str() + start));
}

VxVector Vector(const std::string &text) {
    return VxVector(Component(text, 0, '|'), Component(text, 1, '|'), Component(text, 2, '|'));
}

// Dev's colors (screenshots): the top bar strip, the left toolbar.
const ImU32 TOP_BAR = IM_COL32(65, 65, 65, 255);
const ImU32 TOP_BAR_BORDER = IM_COL32(157, 157, 157, 255);
const ImU32 LEFT_BAR = IM_COL32(128, 128, 128, 255);
const ImU32 LEFT_BAR_BORDER = IM_COL32(21, 21, 21, 255);
const ImU32 PANEL = IM_COL32(100, 100, 100, 255);

const char *const REFERENTIALS[] = {"Local", "World", "View", "Parent", "Ref. Guide", "Pick Obj."};

} // namespace

LayoutWindow::LayoutWindow(EditorState &state, EditorApplication &editor)
    : m_State(state), m_CommandEditor(editor), m_View(state.host) {}

LayoutWindow::~LayoutWindow() {
    CancelEdits();
    if (!ReleaseTexture()) {
        m_View.Stop();
        ResetViewportAfterContextStop();
    }
}

bool LayoutWindow::Start(SDL_Window *window, std::string *error) {
    m_EditorHost = m_State.workspace->Application().Host();
    m_Capturing = 0;
    CancelEdits();
    if (!ReleaseTexture()) {
        // Keep the borrowed SDL texture alive until the old Context, and with
        // it the stale logical handle, has ceased to exist.
        m_View.Stop();
        ResetViewportAfterContextStop();
    }
    m_MainWindow = window;
    ApplyPreferences();
    std::string message;
    m_Started = m_View.Start(Utf8ToAnsi(m_Preferences.Text("3D Layout\\Author Mode\\Device Driver")), &message,
                             MainRenderer().Device());
    SelectViewportContext();
    if (!m_Started) m_StartError = message;
    if (error) *error = message;
    m_View.windowEvent = [this](const SDL_Event &event) {
        if (!m_Switching) FullScreenEvent(event);
    };
    // The view as a pick target while it shows (drawn in the last frames).
    m_State.objectPick["3D Layout"] = [this](float x, float y, CK_CLASSID cid) -> CK_ID {
        if (!m_Started || ImGui::GetFrameCount() - m_ViewFrame > 2 || x < m_ViewMin.x || y < m_ViewMin.y ||
            x >= m_ViewMax.x || y >= m_ViewMax.y)
            return 0;
        EditorValue px{EDITOR_TEXT("x"), EDITOR_REAL_VALUE}, py{EDITOR_TEXT("y"), EDITOR_REAL_VALUE};
        px.real = int(x - m_ViewMin.x);
        py.real = int(y - m_ViewMin.y);
        const auto result = Client().Query("layout.pick-object", {px, py, IntegerArgument("class", uint64_t(cid))});
        const auto *picked = EditorClient::Find(result, "object");
        return result.status == EDITOR_OK && picked && picked->type == EDITOR_OBJECT ? picked->object.object : 0;
    };
    return m_Started;
}

void LayoutWindow::Stop() {
    m_Capturing = 0;
    CancelEdits();
    m_State.objectPick.erase("3D Layout");
    const bool released = ReleaseTexture();
    m_View.Stop();
    if (!released)
        ResetViewportAfterContextStop();
    m_Started = false;
}

// Dev's CUIKApp +0x100: set by Play (sub_5CD5D0), cleared by Pause and
// Stop (sub_5CD3D0) and when the context no longer plays (sub_5CD650).
bool LayoutWindow::PlayerMode() const {
    return EditorClient::Integer(Client().Query("layout.presentation"), "player") != 0;
}
bool LayoutWindow::FullScreen() const {
    return EditorClient::Integer(Client().Query("layout.presentation"), "fullscreen") != 0;
}
void LayoutWindow::ApplyPreferences() {
    const auto revision = Client().Revision("preferences.values");
    if (!revision || revision != m_PreferencesRevision) {
        const auto values = Client().Query("preferences.values");
        const auto *record = EditorClient::Find(values, "value");
        std::string error;
        if (values.status == EDITOR_OK && record && record->type == EDITOR_RECORD &&
            m_Preferences.ReadValues(record->fields, error))
            m_PreferencesRevision = EditorClient::Integer(values, "revision");
    }
    m_Presentation = Client().Query("layout.presentation");
    m_PlayerMode = m_Presentation.status == EDITOR_OK && EditorClient::Integer(m_Presentation, "player");
}

// Dev makes its interface objects again after ClearAll and adds its render
// context to each new level; here the window notices the level change.
void LayoutWindow::SyncComposition() {
    if (m_Presentation.status != EDITOR_OK)
        return;
    const auto revision = EditorClient::Integer(m_Presentation, "composition");
    if (revision != m_Composition) {
        m_Composition = revision;
        m_Group = {};
        m_GroupRevision = ~0u;
    }
}

void LayoutWindow::Draw(ImGuiID dock, bool openPopups) {
    PollCommands();
    if (!ReadLayoutSelection(Client(), m_SelectionData))
        m_SelectionData = {};
    // Set again by the view while the composition keeps the cursor.
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    // Every frame, shown or not: Player Mode starts with the selection of Play.
    ApplyPreferences();
    FullScreenCommands();
    // Alt+Enter is Dev's application's accelerator, whatever window has the focus.
    const ImGuiIO &io = ImGui::GetIO();
    if (m_Started && !io.WantTextInput && io.KeyAlt && !io.KeyCtrl && !io.KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        ToggleFullScreen();
    // Fullscreen, the frame goes to the screen, the 3D Layout shown or not.
    if (m_View.FullScreen()) m_View.RenderFullScreen();
    m_OpenPopups = openPopups;
    if (dock) ImGui::SetNextWindowDockID(dock, ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, PANEL);
    const bool visible = BeginDevWindow("3D Layout", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!visible) {
        m_Capturing = 0;
        CancelEdits();
        ImGui::End();
        ExplorerWindow();
        SnapshotWindow();
        CopyOptionsDialog();
        DropParametersDialog();
        return;
    }
    SyncComposition();
    SyncCurveTool();
    // Dev's status bar shows the 3D Layout's action after it was used last.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if ((Busy() || m_Tool == LayoutTool::Fly) && ImGui::IsKeyPressed(ImGuiKey_Escape)) CancelEdits();
        m_LastUsed = true;
        Shortcuts();
    } else if (ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
        CancelEdits();
        m_LastUsed = false;
    }
    m_Scale = ImGui::GetFontSize() / 13.0f;
    m_Origin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    // Dev's layout (sub_4B6C70): the top bar 21 high, the left toolbar from
    // x 6 to 76, the view from (76, 21) to the window's corner.
    const float top = 21.0f * m_Scale, left = 76.0f * m_Scale;
    TopBar(m_Origin, available.x);
    LeftBar(m_Origin, available.y);
    const ImVec2 viewAt(m_Origin.x + left, m_Origin.y + top);
    const ImVec2 viewSize(std::max(1.0f, available.x - left - 1.0f), std::max(1.0f, available.y - top - 1.0f));
    Viewport(viewAt, viewSize);
    ImGui::End();
    ExplorerWindow();
    SnapshotWindow();
    CopyOptionsDialog();
    DropParametersDialog();
}

// ------------------------------------------------------------------ toolbar

bool LayoutWindow::ToolbarButton(const Button &button, bool checked, bool enabled) {
    const float s = m_Scale;
    const ImVec2 a(m_Origin.x + button.x0 * s, m_Origin.y + button.y0 * s);
    const ImVec2 size((button.x1 - button.x0) * s, (button.y1 - button.y0) * s);
    ImGui::SetCursorScreenPos(a);
    ImGui::PushID(button.command);
    if (!enabled) ImGui::BeginDisabled();
    const bool clicked = ImGui::InvisibleButton("##tool", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool right = clicked && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    if (!enabled) ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const bool held = ImGui::IsItemActive();
    ImGui::PopID();
    const int bitmap = !enabled ? button.bitmaps[4]
                       : (checked || held) ? button.bitmaps[hovered ? 3 : 2]
                                           : button.bitmaps[hovered ? 1 : 0];
    ImVec2 bitmapSize;
    if (ImTextureID texture = DevBitmap(bitmap, &bitmapSize))
        ImGui::GetWindowDrawList()->AddImage(texture, a, ImVec2(a.x + bitmapSize.x * s, a.y + bitmapSize.y * s));
    else
        ImGui::GetWindowDrawList()->AddRect(a, ImVec2(a.x + size.x, a.y + size.y), checked ? IM_COL32_WHITE : IM_COL32(40, 40, 40, 255));
    if (hovered && button.tooltip) ImGui::SetTooltip("%s", button.tooltip);
    if (right) {
        RightClick(button.command);
        return false;
    }
    return clicked && enabled;
}

// A Dev menu button: a click runs its current entry; pressed and held, it
// shows its entries' icons to choose another.
bool LayoutWindow::MenuButton(const Button &button, const std::vector<Item> &items, int &current, bool checked,
                              bool enabled) {
    const float s = m_Scale;
    current = std::clamp(current, 0, static_cast<int>(items.size()) - 1);
    const Item &item = items[current];
    Button shown = button;
    shown.bitmaps[0] = item.bitmaps[0];
    shown.bitmaps[1] = item.bitmaps[1];
    shown.bitmaps[2] = shown.bitmaps[3] = item.bitmaps[2];
    shown.bitmaps[4] = item.bitmaps[0];
    const ImVec2 a(m_Origin.x + button.x0 * s, m_Origin.y + button.y0 * s);
    bool run = ToolbarButton(shown, checked, enabled);
    const char *popup = "##menubutton";
    ImGui::PushID(button.command);
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (m_HeldMenu != button.command) {
            m_HeldMenu = button.command;
            m_HeldSince = ImGui::GetTime();
        } else if (ImGui::GetTime() - m_HeldSince > 0.3) {
            ImGui::OpenPopup(popup);
            m_HeldMenu = 0;
        }
    } else if (m_HeldMenu == button.command && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_HeldMenu = 0;
    }
    ImGui::SetNextWindowPos(a);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, LEFT_BAR);
    if (ImGui::BeginPopup(popup)) {
        run = false;
        const ImVec2 itemSize((button.x1 - button.x0) * s, (button.y1 - button.y0) * s);
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            ImGui::PushID(i);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const bool chosen = ImGui::InvisibleButton("##item", itemSize);
            const bool hovered = ImGui::IsItemHovered();
            ImVec2 bitmapSize;
            if (ImTextureID texture = DevBitmap(items[i].bitmaps[i == current ? 2 : hovered ? 1 : 0], &bitmapSize))
                ImGui::GetWindowDrawList()->AddImage(texture, at, ImVec2(at.x + bitmapSize.x * s, at.y + bitmapSize.y * s));
            if (hovered && items[i].name) ImGui::SetTooltip("%s", items[i].name);
            if (chosen) {
                current = i;
                run = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return run;
}

void LayoutWindow::TopBar(const ImVec2 &origin, float width) {
    const float s = m_Scale;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(origin.x + 2.0f * s, origin.y), ImVec2(origin.x + width - 2.0f * s, origin.y + 21.0f * s),
                      TOP_BAR, 3.0f * s);
    dl->AddRect(ImVec2(origin.x + 2.0f * s, origin.y), ImVec2(origin.x + width - 2.0f * s, origin.y + 21.0f * s),
                TOP_BAR_BORDER, 3.0f * s);
    static const Button snapshot = {56, 7, 1, 26, 20, {809, 811, 810, 810, 809}, "Snapshot"};
    static const Button explorer = {41, 27, 1, 46, 20, {777, 779, 778, 778, 777}, "3D Layout Explorer"};
    static const Button preferences = {40, 347, 1, 366, 20, {804, 806, 805, 805, 804}, "General Preferences (Ctrl+P)"};
    const bool usable = m_Started && m_State.HasDocument();
    if (ToolbarButton(snapshot, m_SnapshotOpen, m_Started)) Command(snapshot.command);
    if (ToolbarButton(explorer, m_Doc.explorerOpen, usable)) Command(explorer.command);
    if (ToolbarButton(preferences, false)) Command(preferences.command);
    // The two combos: dark, white text and arrow (screenshots).
    ImGui::PushStyleColor(ImGuiCol_FrameBg, TOP_BAR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(80, 80, 80, 255));
    ImGui::PushStyleColor(ImGuiCol_Button, TOP_BAR);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 80, 80, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(236, 236, 236, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * s, std::max(0.0f, (19.0f * s - ImGui::GetFontSize()) * 0.5f)));
    // Selection Groups (Dev.exe id 18).
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(80, 80, 80, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(80, 80, 80, 255));
    SelectionGroups(ImVec2(origin.x + 51.0f * s, origin.y + 1.0f * s), 140.0f * s);
    ImGui::PopStyleColor(2);
    // The separator (Dev.exe sub_4B6C70's element "3": bitmap 705 at 196, 1).
    if (ImTextureID separator = DevBitmap(705))
        dl->AddImage(separator, ImVec2(origin.x + 196.0f * s, origin.y + 1.0f * s), ImVec2(origin.x + 197.0f * s, origin.y + 20.0f * s));
    // Available Cameras (id 19): Dev's views, then the composition's cameras.
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 202.0f * s, origin.y + 1.0f * s));
    ImGui::SetNextItemWidth(140.0f * s);
    auto &cameras = m_CameraData; const bool camerasReady = ReadLayoutScene(Client(), "cameras", cameras);
    const auto *current = camerasReady ? cameras.Find(cameras.camera.object) : nullptr;
    const std::string currentName = current ? current->name : std::string();
    ImGui::PopStyleColor(); // the list keeps the menu's text color
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(236, 236, 236, 255));
    const bool open = ImGui::BeginCombo("##cameras", currentName.c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && !open) ImGui::SetTooltip("Available Cameras");
    if (open) {
        TypeAhead typeAhead;
        if (camerasReady) for (const auto &camera : cameras.rows) {
            ImGui::PushID(int(camera.object.object));
            if (typeAhead.Selectable(camera.name.c_str(), current && current->object == camera.object))
                Execute("layout.set-camera", {ObjectArgument("target", camera.object)});
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
}

void LayoutWindow::LeftBar(const ImVec2 &origin, float height) {
    const float s = m_Scale;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 a(origin.x + 6.0f * s, origin.y + 21.0f * s), b(origin.x + 76.0f * s, origin.y + height);
    dl->AddRectFilled(a, b, LEFT_BAR);
    dl->AddRect(a, b, LEFT_BAR_BORDER);
    auto line = [&](int y) {
        dl->AddLine(ImVec2(origin.x + 10.0f * s, origin.y + y * s), ImVec2(origin.x + 72.0f * s, origin.y + y * s),
                    IM_COL32(100, 100, 100, 255));
        dl->AddLine(ImVec2(origin.x + 10.0f * s, origin.y + (y + 1) * s), ImVec2(origin.x + 72.0f * s, origin.y + (y + 1) * s),
                    IM_COL32(150, 150, 150, 255));
    };
    const bool transforming = m_Tool == LayoutTool::Translate || m_Tool == LayoutTool::Rotate ||
                              m_Tool == LayoutTool::Scale || m_Tool == LayoutTool::EditPoints;
    // Selection.
    static const Button select = {25, 8, 24, 29, 45, {774, 776, 775, 775, 774}, "Select (A)"};
    static const Button lock = {58, 30, 25, 51, 46, {692, 694, 693, 693, 692}, "Lock Selection (Spacebar)"};
    static const Button mode = {61, 52, 24, 73, 45, {1152, 1153, 1151, 1150, 1151}, "Selection Mode"};
    if (ToolbarButton(select, m_Tool == LayoutTool::Select)) Command(select.command);
    if (ToolbarButton(lock, m_SelectionData.locked))
        Command(lock.command);
    if (ToolbarButton(mode, m_Preferences.Bool("3D Layout\\Strict Selection")))
        Command(mode.command);
    line(49);
    // Transformations.
    static const Button move = {9, 8, 55, 29, 76, {699, 701, 700, 700, 699}, "Select and Translate (X)"};
    static const Button rotate = {11, 30, 55, 51, 76, {727, 729, 728, 728, 727}, "Select and Rotate (C)"};
    static const Button scale = {10, 52, 55, 73, 76, {}, "Select and Scale (V)"};
    // Mode 0 scales the three axes alike (Uniform), 2 the constrained ones (Normal).
    static const std::vector<Item> scales = {{46, {730, 732, 731}, "Uniform"}, {47, {736, 738, 737}, "Volumetric"},
                                             {48, {733, 735, 734}, "Normal"}};
    if (ToolbarButton(move, m_Tool == LayoutTool::Translate)) Command(move.command);
    if (ToolbarButton(rotate, m_Tool == LayoutTool::Rotate)) Command(rotate.command);
    if (MenuButton(scale, scales, m_ScaleMode, m_Tool == LayoutTool::Scale)) Command(scales[m_ScaleMode].command);
    // Transformation settings, shown with the transformation tools only, the
    // active tool's own (Dev.exe sub_492AC0, sub_496600).
    if (transforming) {
        LayoutTransformSettings &settings = Settings();
        static const Button axes[3] = {{12, 9, 79, 30, 100, {590, 592, 591, 591, 1187}, "Constrain X Axis (F5)"},
                                       {13, 31, 79, 52, 100, {596, 598, 597, 597, 1188}, "Constrain Y Axis (F6)"},
                                       {14, 53, 79, 74, 100, {602, 604, 603, 603, 1189}, "Constrain Z Axis (F7)"}};
        const int allowed = AllowedAxes();
        for (int i = 0; i < 3; ++i)
            if (ToolbarButton(axes[i], (settings.axes & (1 << i)) != 0, (allowed & (1 << i)) != 0)) Command(axes[i].command);
        static const Button plane = {39, 23, 100, 56, 115, {}, "Constrain Plane (F8)"};
        static const std::vector<Item> planes = {{42, {593, 595, 594}, "XY"}, {43, {599, 601, 600}, "YZ"},
                                                 {44, {605, 607, 606}, "XZ"}};
        static const int planeAxes[3] = {3, 6, 5};
        int planeIndex = settings.axes == 6 ? 1 : settings.axes == 5 ? 2 : 0;
        if (MenuButton(plane, planes, planeIndex, settings.axes == planeAxes[planeIndex]))
            Command(planes[planeIndex].command);
        // Referential Axis (id 15): a small combo; Rotate and Scale show it
        // about each object's axis only.
        if (m_Tool == LayoutTool::Translate || m_Tool == LayoutTool::EditPoints || settings.pivot == 1) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + 9.0f * s, origin.y + 116.0f * s));
            ImGui::SetNextItemWidth(64.0f * s);
            ImGui::PushFont(nullptr, 10.0f * s);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * s, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(200, 200, 200, 255));
            if (ImGui::BeginCombo("##referential", REFERENTIALS[std::clamp(settings.referential, 0, 5)])) {
                TypeAhead typeAhead;
                for (int i = 0; i < 6; ++i)
                    if (typeAhead.Selectable(REFERENTIALS[i], i == settings.referential)) {
                        settings.referential = i;
                        // Pick Obj.: the next click in the view picks it.
                        if (i == 5) {
                            m_PickingReferent = true;
                            m_State.Log(LogLevel::Info, "Pick the referential object in the 3D Layout");
                        }
                    }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Referential Axis");
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImGui::PopFont();
        }
        static const Button snap = {66, 8, 137, 29, 158, {1240, 1238, 1239, 1239, 1240}, "Toggle Snap (S)"};
        static const Button hierarchy = {70, 30, 137, 51, 158, {1256, 1255, 1254, 1257, 1256}, "Toggle Hierarchy (H)"};
        static const Button pivot = {71, 52, 137, 73, 158, {}, "Pivot Axis (F9)"};
        static const std::vector<Item> pivots = {{72, {1262, 1263, 1261}, "Object"}, {73, {1265, 1266, 1264}, "Axis"},
                                                 {74, {1268, 1269, 1267}, "Selection"}};
        if (ToolbarButton(snap, settings.snap)) Command(snap.command);
        if (ToolbarButton(hierarchy, settings.hierarchy)) Command(hierarchy.command);
        // Translate has no pivot.
        if (m_Tool != LayoutTool::Translate && m_Tool != LayoutTool::EditPoints &&
            MenuButton(pivot, pivots, settings.pivot, false))
            Command(pivots[settings.pivot].command);
    }
    line(160);
    // Guides.
    static const Button guide = {29, 31, 166, 52, 187, {1258, 1259, 1260, 1260, 1258}, "Toggle Reference Guide (G)"};
    static const Button screenGuide = {67, 53, 166, 74, 187, {678, 680, 679, 679, 678}, "Toggle Screen Guide (Shift+G)"};
    if (ToolbarButton(guide, m_View.Display().showGrid)) Command(guide.command);
    if (ToolbarButton(screenGuide, m_Preferences.Bool("3D Layout\\Show Screen Grid")))
        Command(screenGuide.command);
    line(191);
    // Creation.
    static const Button creates[8] = {{31, 9, 197, 30, 218, {615, 617, 616, 616, 615}, "Create Camera (1)"},
                                      {32, 31, 197, 52, 218, {624, 626, 625, 625, 624}, "Create Light (2)"},
                                      {34, 53, 197, 74, 218, {1278, 1276, 1277, 1277, 1278}, "Create 3D Frame (3)"},
                                      {33, 9, 218, 30, 239, {618, 620, 619, 619, 618}, "Create Curve (4)"},
                                      {60, 31, 218, 52, 239, {1148, 1147, 1146, 1146, 1147}, "Create Grid (5)"},
                                      {75, 53, 218, 74, 239, {621, 622, 623, 623, 621}, "Create 2D Frame (6)"},
                                      {80, 9, 239, 30, 260, {1324, 1325, 1326, 1326, 1324}, "Create Material (7)"},
                                      {81, 31, 239, 52, 260, {1327, 1328, 1329, 1329, 1327}, "Create Texture (8)"}};
    const bool document = Client().Scope().document.value != 0;
    for (const Button &button : creates)
        if (ToolbarButton(button, false, document)) Command(button.command);
    static const Button portal = {77, 53, 239, 74, 260, {}, "Create Portal (9)"};
    static const std::vector<Item> portals = {{79, {1340, 1338, 1339}, "Create Portals"}, {78, {1311, 1309, 1310}, "Create Portal"}};
    if (MenuButton(portal, portals, m_PortalMode, false, document)) Command(portals[m_PortalMode].command);
    line(264);
    // Navigation.
    const bool perspective = EditorClient::Integer(m_Presentation, "perspective");
    const bool targeted = EditorClient::Integer(m_Presentation, "targeted");
    static const Button dolly = {23, 9, 270, 30, 291, {664, 666, 665, 665, 664}, "Camera Dolly (D)"};
    static const Button fov = {26, 31, 270, 52, 291, {674, 676, 675, 675, 674}, "Camera Field of View (F)"};
    static const Button zoom = {27, 53, 270, 74, 291, {}, "Camera Zoom (Z)"};
    static const std::vector<Item> zooms = {{51, {836, 838, 837}, "Zoom"}, {52, {842, 844, 843}, "Zoom on Selection"},
                                            {57, {839, 841, 840}, "Zoom on Scene"}};
    static const Button roll = {24, 9, 291, 30, 312, {724, 726, 725, 725, 724}, "Roll Camera (R)"};
    static const Button pan = {28, 31, 291, 52, 312, {715, 717, 716, 716, 715}, "Camera Pan (P)"};
    static const Button orbit = {20, 53, 291, 74, 312, {}, "Orbit Target/Orbit Around (O)"};
    static const std::vector<Item> orbits = {{49, {706, 708, 707}, "Orbit"}, {50, {709, 711, 710}, "Orbit Around Camera"}};
    if (ToolbarButton(dolly, m_Tool == LayoutTool::Dolly)) Command(dolly.command);
    // Dev disables the field of view of orthographic cameras and the roll of target cameras.
    if (ToolbarButton(fov, m_Tool == LayoutTool::Fov, perspective)) Command(fov.command);
    if (MenuButton(zoom, zooms, m_ZoomMode, m_Tool == LayoutTool::Zoom)) Command(zooms[m_ZoomMode].command);
    if (ToolbarButton(roll, m_Tool == LayoutTool::Roll, !targeted)) Command(roll.command);
    if (ToolbarButton(pan, m_Tool == LayoutTool::Pan)) Command(pan.command);
    if (MenuButton(orbit, orbits, m_OrbitMode, m_Tool == LayoutTool::Orbit)) Command(orbits[m_OrbitMode].command);
}

// Toolbar and keyboard commands (Dev's command ids).
void LayoutWindow::Command(int command) {
    const bool flying = m_Tool == LayoutTool::Fly;
    if (command == 68) {
        if (flying) EndFly();
        else BeginFly();
        return;
    }
    switch (command) {
    case 25: m_Tool = LayoutTool::Select; break;
    case 9: m_Tool = LayoutTool::Translate; break;
    case 11: m_Tool = LayoutTool::Rotate; break;
    case 46: case 47: case 48:
        m_ScaleMode = command - 46;
        m_Tool = LayoutTool::Scale;
        break;
    case 58:
        // Dev locks a non-empty selection only.
        if (m_SelectionData.locked || !m_SelectionData.objects.empty())
            LockSelection(!m_SelectionData.locked);
        break;
    case 61:
        // Selection Mode: Dev's "Strict Selection" (+540), RectPick's Intersect.
        TogglePreference("3D Layout\\Strict Selection");
        break;
    case 12: case 13: case 14: {
        // Not an axis the view refuses (AllowedAxes).
        if (!(AllowedAxes() & (1 << (command - 12)))) break;
        // At least one axis stays constrained.
        const int axes = Settings().axes ^ (1 << (command - 12));
        if (axes & 7) Settings().axes = axes;
        break;
    }
    case 42: Settings().axes = 3; break; // XY
    case 43: Settings().axes = 6; break; // YZ
    case 44: Settings().axes = 5; break; // XZ
    case 66: Settings().snap = !Settings().snap; break;
    case 70: Settings().hierarchy = !Settings().hierarchy; break;
    case 72: case 73: case 74:
        Settings().pivot = command - 72;
        // Pivot Axis Object: the next click in the view picks the referent.
        if (command == 72) {
            m_PickingReferent = true;
            m_State.Log(LogLevel::Info, "Pick the referent object in the 3D Layout");
        }
        break;
    case 29: ToggleGuide(false); break;
    case 67: ToggleGuide(true); break;
    case 31: if (createObject) createObject(LayoutObject::Camera); break;
    case 32: if (createObject) createObject(LayoutObject::Light); break;
    case 34: if (createObject) createObject(LayoutObject::Frame3D); break;
    case 33: if (createObject) createObject(LayoutObject::Curve); break;
    case 60: if (createObject) createObject(LayoutObject::Grid); break;
    case 75: if (createObject) createObject(LayoutObject::Frame2D); break;
    case 80: if (createObject) createObject(LayoutObject::Material); break;
    case 81: if (createObject) createObject(LayoutObject::Texture); break;
    case 78: if (createObject) createObject(LayoutObject::Portal); break;
    case 79:
        // Automatic Portals (documentation "Automatic Portals"): a portal
        // between each two contiguous places.
        if (Client().Scope().document.value) {
            const auto made = Client().Command("object.create-automatic-portals");
            if (made.status != EDITOR_QUEUED)
                m_State.Log(made.status == EDITOR_OK ? LogLevel::Info : LogLevel::Warning, "Automatic Portals: " + made.message);
        }
        break;
    case 23: m_Tool = LayoutTool::Dolly; break;
    case 26: m_Tool = LayoutTool::Fov; break;
    case 51: m_Tool = LayoutTool::Zoom; break;
    case 52: ZoomOn(false); break;
    case 57: ZoomOn(true); break;
    case 24: m_Tool = LayoutTool::Roll; break;
    case 28: m_Tool = LayoutTool::Pan; break;
    case 49: case 50: m_Tool = LayoutTool::Orbit; break;
    case 40: {
        Execute("preferences.open");
        break;
    }
    case 41: OpenExplorer(); break;
    case 180: {
        LayoutSelectionData selection; if (ReadLayoutSelection(Client(), selection)) Duplicate(selection.objects); break;
    } // the shortcut menu's Duplicate
    case 56: if (m_Started) m_SnapshotOpen = true; break;
    default: break;
    }
    if (!CurveToolActive())
        ClearCurveTool();
    if (flying && m_Tool != LayoutTool::Fly) {
        const LayoutTool next = m_Tool;
        m_Tool = LayoutTool::Fly;
        EndFly();
        m_Tool = next;
    }
}

// Right-clicking a button opens its General Preferences page (Dev.exe
// sub_4B95C0, WM_NOTIFY): the tools the Actions page, the creation tools
// and the screen guide the Interface page, the reference guide its page,
// snap the Snap & Align page, the sphere the Rendering page.
void LayoutWindow::RightClick(int command) {
    auto openPreferences = [this](const char *page) { Execute("preferences.open", {TextArgument("page", page)}); };
    switch (command) {
    case 9: case 10: case 11: case 20: case 23: case 24: case 26: case 27: case 28: case 68: case 25:
        openPreferences("actions");
        break;
    case 31: case 32: case 33: case 34: case 60: case 67: case 75:
        openPreferences("interface");
        break;
    case 29: openPreferences("reference-guide"); break;
    case 66: openPreferences("snap-align"); break;
    case 40: openPreferences("rendering"); break;
    default: break;
    }
}

// G / Shift+G (Dev.exe sub_4B95C0): the guide flips and Dev stores it at once.
void LayoutWindow::TogglePreference(const char *key) {
    const auto values = Client().Query("preferences.values");
    if (values.status != EDITOR_OK)
        return;
    const auto *record = EditorClient::Find(values, "value");
    if (!record || record->type != EDITOR_RECORD)
        return;
    const auto found = std::find_if(record->fields.begin(), record->fields.end(),
                                    [key](const EditorField &field) { return field.key == key; });
    if (found == record->fields.end() || found->type != EDITOR_INTEGER_VALUE)
        return;
    EditorField changed{"value", EDITOR_RECORD};
    changed.fields = {*found};
    changed.fields.front().integer = !found->integer;
    const std::vector<EditorField> fields{
        {"revision", EDITOR_INTEGER_VALUE, {}, {}, EditorClient::Integer(values, "revision")}, std::move(changed)};
    const auto args = BorrowEditorFields(fields);
    Execute("preferences.apply", {args.data(), args.data() + args.size()});
    ApplyPreferences();
}
void LayoutWindow::ToggleGuide(bool screen) {
    TogglePreference(screen ? "3D Layout\\Show Screen Grid" : "3D Layout\\Author Mode\\Show Grid");
}

// ------------------------------------------------------------------- view

void LayoutWindow::Viewport(const ImVec2 &viewAt, const ImVec2 &viewSize) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    // Dev.exe sub_4B8CE0 (Environment\Screen Size): a size other than 0|0
    // renders at that size, centered in a larger view; a smaller view gets
    // scrollbars (the horizontal one first, as Dev) and shows the part
    // scrolled to. at / size: the render context's rectangle on the screen.
    const std::string screenSize = m_Preferences.Text("3D Layout\\Environment\\Screen Size");
    const int fixedW = static_cast<int>(Component(screenSize, 0, '|')), fixedH = static_cast<int>(Component(screenSize, 1, '|'));
    ImVec2 at = viewAt, size = viewSize, visible = viewSize;
    bool hBar = false, vBar = false;
    if (m_Started && fixedW > 0 && fixedH > 0) {
        const float bar = ImGui::GetStyle().ScrollbarSize;
        hBar = visible.x < fixedW;
        if (hBar) visible.y -= bar;
        vBar = visible.y < fixedH;
        if (vBar) {
            visible.x -= bar;
            hBar = visible.x < fixedW;
        }
        visible = ImVec2(std::max(1.0f, visible.x), std::max(1.0f, visible.y));
        m_ScreenScroll.x = hBar ? std::clamp(m_ScreenScroll.x, 0.0f, fixedW - visible.x) : 0.0f;
        m_ScreenScroll.y = vBar ? std::clamp(m_ScreenScroll.y, 0.0f, fixedH - visible.y) : 0.0f;
        at = ImVec2(viewAt.x + (hBar ? -m_ScreenScroll.x : std::floor((visible.x - fixedW) * 0.5f)),
                    viewAt.y + (vBar ? -m_ScreenScroll.y : std::floor((visible.y - fixedH) * 0.5f)));
        size = ImVec2(static_cast<float>(fixedW), static_cast<float>(fixedH));
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (hBar) {
            ImS64 scroll = static_cast<ImS64>(m_ScreenScroll.x);
            const ImRect bb(viewAt.x, viewAt.y + visible.y, viewAt.x + visible.x, viewAt.y + visible.y + bar);
            if (ImGui::ScrollbarEx(bb, window->GetID("##screenh"), ImGuiAxis_X, &scroll, static_cast<ImS64>(visible.x), fixedW))
                m_ScreenScroll.x = static_cast<float>(scroll);
        }
        if (vBar) {
            ImS64 scroll = static_cast<ImS64>(m_ScreenScroll.y);
            const ImRect bb(viewAt.x + visible.x, viewAt.y, viewAt.x + visible.x + bar, viewAt.y + visible.y);
            if (ImGui::ScrollbarEx(bb, window->GetID("##screenv"), ImGuiAxis_Y, &scroll, static_cast<ImS64>(visible.y), fixedH))
                m_ScreenScroll.y = static_cast<float>(scroll);
        }
        // Around the render context: Dev's editor view background.
        dl->AddRectFilled(viewAt, ImVec2(viewAt.x + visible.x, viewAt.y + visible.y), PANEL);
    }
    const ImVec2 visibleMax(viewAt.x + visible.x, viewAt.y + visible.y);
    const int width = static_cast<int>(size.x), height = static_cast<int>(size.y);
    m_ViewMin = at;
    m_ViewMax = ImVec2(std::min(at.x + size.x, visibleMax.x), std::min(at.y + size.y, visibleMax.y));
    m_ViewFrame = ImGui::GetFrameCount();
    if (m_Started && m_MainWindow) {
        const VxRect inputRect(at.x, at.y, at.x + size.x, at.y + size.y);
        m_View.SetInputViewport(m_MainWindow, inputRect, width, height);
    }
    ImGui::SetCursorScreenPos(viewAt);
    ImGui::InvisibleButton("##view", visible,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    // Dev.exe sub_4B95C0 (WM_SETCURSOR): in Player Mode without Update
    // Cursor In Player Mode the view leaves the cursor to the composition's
    // Building Blocks (Main.cpp does not put the arrow back).
    if (m_PlayerMode && ImGui::IsItemHovered() && !m_Preferences.Bool("3D Layout\\Player Mode\\Update Cursor"))
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    if (m_Started && m_Capturing) CaptureInput(at);
    else if (m_Started) ViewInput(at, ImGui::IsItemHovered());
    if (m_Started) DropTarget(at);
#ifdef VIRTOOLSDEV_UI_TESTS
    if (m_Started && m_StartupDropFrames > 0 && --m_StartupDropFrames == 0)
        DropBuildingBlocks(static_cast<int>(m_StartupDropAt.x - at.x), static_cast<int>(m_StartupDropAt.y - at.y),
                           {(uint64_t(m_StartupDrop.d1) << 32) | m_StartupDrop.d2});
#endif
    if (!m_Started) {
        dl->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), LEFT_BAR);
        const std::string text = "No 3D Layout: " + AnsiToUtf8(m_StartError);
        dl->AddText(ImVec2(at.x + 8.0f, at.y + 8.0f), IM_COL32(20, 20, 20, 255), text.c_str());
        return;
    }
    ContextMenuPopup();
    dl->PushClipRect(viewAt, visibleMax, true);
    const bool resized = width != m_View.Width() || height != m_View.Height();
    bool targetReleased = true;
    if (resized && m_ViewportRenderer == &LayoutWindow::RenderGpuViewport) {
        targetReleased = ReleaseGpuViewport();
        if (targetReleased)
            m_ViewportRenderer = &LayoutWindow::RenderUnselectedViewport;
    }
    UiTexture *frame = nullptr;
    if (targetReleased && m_View.PrepareFrame(width, height)) {
        const bool contextSelected =
            m_View.RasterizerContext() == m_ViewportContext ||
            SelectViewportContext();
        if (contextSelected)
            frame = (this->*m_ViewportRenderer)();
    }
    if (frame)
        dl->AddImage(frame->Id(), at, ImVec2(at.x + m_View.Width(), at.y + m_View.Height()));
    else
        dl->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), LEFT_BAR);
    // The selection rectangle being dragged.
    if (m_Rectangle) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        dl->AddRect(ImVec2(at.x + m_PressAt.x, at.y + m_PressAt.y), mouse, IM_COL32(255, 255, 255, 255));
    }
    if (debugData || m_Preferences.Bool("3D Layout\\Debug Mode")) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        DebugData(at, static_cast<int>(mouse.x - at.x), static_cast<int>(mouse.y - at.y));
    }
    dl->PopClipRect();
    // Lock Selection: "a thin green border around the preview pane".
    if (m_SelectionData.locked)
        dl->AddRect(viewAt, visibleMax, IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);
}

// Dev.exe sub_4A01F0: the mouse (and the frame rate while playing), then
// what is under it: the 2D entity, character and 3D entity (" (S)" when
// selected, " (D)" when dropped on), the mesh with the face's normal, its
// vertex nearest to the point, the texture coordinates (and the vertex
// colors of prelit meshes), the material, the texture. Unnamed objects are
// "Unknown" (Dev's string 51).
void LayoutWindow::DebugData(const ImVec2 &at, int x, int y) {
    EditorValue px{EDITOR_TEXT("x"), EDITOR_REAL_VALUE}, py{EDITOR_TEXT("y"), EDITOR_REAL_VALUE};
    px.real = x;
    py.real = y;
    const auto result = Client().Query("layout.debug-data", {px, py});
    if (result.status != EDITOR_OK)
        return;
    const auto &io = ImGui::GetIO();
    const int buttons = (ImGui::IsMouseDown(ImGuiMouseButton_Left) ? 1 : 0) |
                        (ImGui::IsMouseDown(ImGuiMouseButton_Right) ? 2 : 0) | (io.KeyShift ? 4 : 0) |
                        (io.KeyCtrl ? 8 : 0) | (ImGui::IsMouseDown(ImGuiMouseButton_Middle) ? 16 : 0);
    char line[128];
    if (EditorClient::Integer(m_Presentation, "playing"))
        std::snprintf(line, sizeof(line), "X:%5d Y:%5d M:%3d  FPS:%5d\n", x, y, buttons, int(io.Framerate + 0.5f));
    else
        std::snprintf(line, sizeof(line), "X:%5d Y:%5d M:%3d\n", x, y, buttons);
    const auto text = line + EditorClient::Text(result, "text");
    ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + 10.0f * m_Scale, at.y + 10.0f * m_Scale),
                                        IM_COL32(255, 255, 255, 255), text.c_str());
}

// "3D Layout\Actions\..." and the World Size (Interface page).
CameraSteps LayoutWindow::Steps() const {
    const Preferences &p = m_Preferences;
    CameraSteps steps;
    steps.rotate = p.Float("3D Layout\\Actions\\Camera Rotate Step");
    steps.translate = p.Float("3D Layout\\Actions\\Camera Translate Step");
    steps.zoom = p.Float("3D Layout\\Actions\\Camera Zoom Step");
    steps.orthoZoom = p.Float("3D Layout\\Actions\\Camera Orthographic Zoom Step");
    steps.roll = p.Float("3D Layout\\Actions\\Camera Roll Step");
    steps.fov = p.Float("3D Layout\\Actions\\Camera Fov Step");
    steps.rotateConstant = p.Bool("3D Layout\\Actions\\Camera Rotate Constant Speed Mode");
    steps.translateConstant = p.Bool("3D Layout\\Actions\\Camera Translate Constant Speed Mode");
    steps.zoomConstant = p.Bool("3D Layout\\Actions\\Camera Zoom Constant Speed Mode");
    steps.orthoZoomConstant = p.Bool("3D Layout\\Actions\\Camera OrthoZoom Constant Speed Mode");
    steps.rollConstant = p.Bool("3D Layout\\Actions\\Camera Roll Constant Speed Mode");
    steps.fovConstant = p.Bool("3D Layout\\Actions\\Camera Fov Constant Speed Mode");
    steps.freeOrbitType = p.Int("3D Layout\\Actions\\Free Camera Orbit Type");
    steps.freeOrbitValue = p.Float("3D Layout\\Actions\\Free Camera Orbit Value");
    steps.orbitSelectionType = p.Int("3D Layout\\Actions\\Orbit Selection Type");
    steps.worldMin = Vector(p.Text("3D Layout\\World Size Min"));
    steps.worldMax = Vector(p.Text("3D Layout\\World Size Max"));
    return steps;
}

std::string LayoutWindow::ActionText() const {
    if (!m_LastUsed) return std::string();
    const bool targeted = EditorClient::Integer(InputState(), "targeted") != 0;
    if (GestureActive(0))
        return CameraActionText(CameraAction(m_GestureAction), true, targeted,
                                EditorClient::Integer(m_Interaction, "around-selection") != 0);
    if (GestureActive(2))
        return TransformAction(m_GestureAction) == TransformAction::Translate ? "Moving Objects"
                                                                              : "Scaling Objects (Free)";
    if (GestureActive(1) && !m_CurveEditing) {
        static const char *const scaling[] = {"Scaling Objects (Uniform)", "Scaling Objects (Volumetric)",
                                              "Scaling Objects (Free)"};
        switch (TransformAction(m_GestureAction)) {
        case TransformAction::Translate: return "Moving Objects";
        case TransformAction::Rotate: return "Rotating Objects";
        case TransformAction::Scale:
            return scaling[std::clamp(m_ScaleMode, 0, 2)];
        }
    }
    if (m_Rectangle) return "Select Objects";
    switch (m_Tool) {
    case LayoutTool::Fly: return "Navigation active";
    case LayoutTool::AddPoints: return "Add curve points";
    case LayoutTool::InsertPoints: return "Insert points on curve";
    case LayoutTool::EditPoints:
        return GestureActive(1) ? "Moving curve points" : "Edit curve points";
    case LayoutTool::DeletePoints: return "Delete curve points";
    case LayoutTool::Dolly: return CameraActionText(CameraAction::Dolly, false, targeted, false);
    case LayoutTool::Zoom: return CameraActionText(CameraAction::Zoom, false, targeted, false);
    case LayoutTool::Pan: return CameraActionText(CameraAction::Pan, false, targeted, false);
    case LayoutTool::Orbit:
        return CameraActionText(m_OrbitMode ? CameraAction::OrbitCamera : CameraAction::Orbit, false, targeted, false);
    case LayoutTool::Roll: return CameraActionText(CameraAction::Roll, false, targeted, false);
    case LayoutTool::Fov: return CameraActionText(CameraAction::Fov, false, targeted, false);
    case LayoutTool::Translate: return "Select or Move Objects";
    case LayoutTool::Rotate: return "Select or Rotate Objects";
    case LayoutTool::Scale: return "Select or Scale Objects";
    default: return "Select Objects";
    }
}

bool LayoutWindow::Coordinates(std::string values[3]) const {
    for (int i = 0; i < 3; ++i) values[i] = m_Coordinates[i];
    return m_LastUsed;
}

// Dev's tools after each move (Dev.exe sub_48EED0, sub_4952A0, sub_497270,
// sub_48E630): the grabbed object's world position, its world matrix's
// Euler angles in degrees, its scale; a 2D entity's position and no Z.
void LayoutWindow::UpdateCoordinates() {
    const auto *coordinates = EditorClient::Find(m_Interaction, "coordinates");
    if (!coordinates || coordinates->type != EDITOR_REAL_LIST || coordinates->reals.size() < 2)
        return;
    for (size_t i = 0; i < 3; ++i) {
        if (i >= coordinates->reals.size())
            m_Coordinates[i] = "NA";
        else {
            char text[32];
            std::snprintf(text, sizeof(text), "%8.2f", coordinates->reals[i]);
            m_Coordinates[i] = text;
        }
    }
}

std::string LayoutWindow::SelectionText() const {
    return EditorClient::Text(Client().Query("layout.presentation"), "selection-text");
}

// "3D Layout\Actions\Object ..." and the Snap & Align steps.
TransformSteps LayoutWindow::TransformStepsOf() const {
    const Preferences &p = m_Preferences;
    TransformSteps steps;
    steps.translate = p.Float("3D Layout\\Actions\\Object Translate Step");
    steps.rotate = p.Float("3D Layout\\Actions\\Object Rotate Step");
    steps.scale = p.Float("3D Layout\\Actions\\Object Scale Step");
    steps.translateSticky = p.Bool("3D Layout\\Actions\\Object Translate Sticky Mode");
    steps.rotateSticky = p.Bool("3D Layout\\Actions\\Object Rotate Sticky Mode");
    steps.scaleSticky = p.Bool("3D Layout\\Actions\\Object Scale Sticky Mode");
    steps.snapAngle = p.Float("3D Layout\\Snap\\Rotation Angle");
    steps.snapScale = p.Float("3D Layout\\Snap\\Scaling Percentage");
    steps.snapMode = p.Int("3D Layout\\Snap\\Mode");
    steps.snapRadius = p.Int("3D Layout\\Snap\\Translation Influence (3D Entities)");
    steps.worldStep = Vector(p.Text("3D Layout\\World Step"));
    return steps;
}

// "3D Layout\Snap\..." for 2D entities and the Screen Guide (Interface page).
Snap2D LayoutWindow::Snap2DOf() const {
    const Preferences &p = m_Preferences;
    Snap2D snap;
    snap.mode = p.Int("3D Layout\\Snap\\Mode");
    snap.influence = p.Int("3D Layout\\Snap\\Translation Influence (Sprites)");
    const std::string step = p.Text("3D Layout\\Screen Step"), offset = p.Text("3D Layout\\Screen Offset");
    snap.screenStep[0] = Component(step, 0, '|');
    snap.screenStep[1] = Component(step, 1, '|');
    snap.screenOffset[0] = Component(offset, 0, '|');
    snap.screenOffset[1] = Component(offset, 1, '|');
    snap.homogenous = p.Bool("3D Layout\\Homogenous Screen Grid");
    return snap;
}

bool LayoutWindow::Upload(const std::vector<unsigned char> &pixels, int width, int height) {
    if (width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width) * height * 4) return false;
    std::vector<unsigned char> rgba = pixels;
    for (size_t i = 0; i < rgba.size(); i += 4) std::swap(rgba[i], rgba[i+2]);
    if (!m_Texture || width != m_TextureWidth || height != m_TextureHeight) {
        auto texture = MainRenderer().CreateTexture(width, height, rgba.data());
        if (!texture) return false;
        if (m_Texture) RetireTexture(std::move(m_Texture));
        m_Texture = std::move(texture);
        m_TextureWidth = width; m_TextureHeight = height;
#ifdef VIRTOOLSDEV_UI_TESTS
        ++m_ViewportUploads;
#endif
        return true;
    }
    const bool updated = MainRenderer().UpdateTexture(*m_Texture, rgba.data());
#ifdef VIRTOOLSDEV_UI_TESTS
    if (updated) ++m_ViewportUploads;
#endif
    return updated;
}
bool LayoutWindow::SelectViewportContext() {
    if (!ReleaseGpuViewport())
        return false;
    m_ViewportContext = m_View.RasterizerContext();
#ifdef VIRTOOLSDEV_UI_TESTS
    ++m_GpuViewportSelections;
#endif
    m_ViewportRenderer = m_ViewportContext && MainRenderer().Device()
        ? &LayoutWindow::RenderUnselectedViewport
        : &LayoutWindow::RenderReadbackViewport;
    return true;
}

bool LayoutWindow::ConfigureGpuViewport(int width, int height) {
    if (!m_ViewportContext || !MainRenderer().Device() || width <= 0 || height <= 0)
        return false;
    auto texture = MainRenderer().CreateRenderTarget(width, height);
    if (!texture) return false;
#ifdef VIRTOOLSDEV_UI_TESTS
    if (m_FailNextGpuViewport) {
        m_FailNextGpuViewport = false;
        return false;
    }
#endif
    CKDWORD handle = 0;
    if (!BindSdlGpuBackBuffer(m_ViewportContext, MainRenderer().Device(),
                              texture->NativeTexture(), width, height,
                              &handle))
        return false;
    // The selected path owns exactly one persistent viewport image. A CPU
    // fallback texture may exist when a rebuilt Context becomes eligible for
    // direct rendering; retire it before adopting the imported target.
    if (m_Texture)
        RetireTexture(std::move(m_Texture));
    m_TextureWidth = m_TextureHeight = 0;
    m_GpuTextureHandle = handle;
    m_GpuTexture = std::move(texture);
#ifdef VIRTOOLSDEV_UI_TESTS
    ++m_GpuViewportCreations;
#endif
    return true;
}

bool LayoutWindow::ReleaseGpuViewport() {
#ifdef VIRTOOLSDEV_UI_TESTS
    if (m_FailNextGpuViewportRelease) {
        m_FailNextGpuViewportRelease = false;
        return false;
    }
#endif
    const bool hadTexture = m_GpuTextureHandle != 0;
    if (m_GpuTextureHandle &&
        !ReleaseSdlGpuBackBuffer(m_ViewportContext, &m_GpuTextureHandle))
        return false;
    m_GpuTexture.reset();
#ifdef VIRTOOLSDEV_UI_TESTS
    if (hadTexture)
        ++m_GpuViewportReleases;
#endif
    return true;
}

UiTexture *LayoutWindow::RenderUnselectedViewport() {
    if (ConfigureGpuViewport(m_View.Width(), m_View.Height()))
        m_ViewportRenderer = &LayoutWindow::RenderGpuViewport;
    else
        m_ViewportRenderer = &LayoutWindow::RenderReadbackViewport;
    return (this->*m_ViewportRenderer)();
}

UiTexture *LayoutWindow::RenderReadbackViewport() {
    if (!m_View.RenderFrame() || !m_View.ReadPixels(m_Pixels) ||
        !Upload(m_Pixels, m_View.Width(), m_View.Height()))
        return nullptr;
    return m_Texture.get();
}

UiTexture *LayoutWindow::RenderGpuViewport() {
    return m_View.RenderFrame() ? m_GpuTexture.get() : nullptr;
}

bool LayoutWindow::ReleaseTexture() {
    if (!ReleaseGpuViewport())
        return false;
    m_ViewportContext = nullptr;
    m_ViewportRenderer = &LayoutWindow::RenderReadbackViewport;
    if (m_Texture) RetireTexture(std::move(m_Texture));
    m_TextureWidth = m_TextureHeight = 0;
    return true;
}

void LayoutWindow::ResetViewportAfterContextStop() {
    // Context shutdown destroys its borrowed logical handles without touching
    // external resources. Releasing the UI texture is safe only afterwards.
    m_GpuTextureHandle = 0;
    m_GpuTexture.reset();
    m_ViewportContext = nullptr;
    m_ViewportRenderer = &LayoutWindow::RenderReadbackViewport;
    if (m_Texture)
        RetireTexture(std::move(m_Texture));
    m_TextureWidth = m_TextureHeight = 0;
}

// Private window and GPU resource operations used by the Layout commands.
bool LayoutWindow::CapturePixels(std::vector<unsigned char> &pixels, int &width, int &height) {
    if (!m_Started || m_View.Width() <= 0 || m_View.Height() <= 0)
        return false;
    if ((UsesGpuViewport() || m_View.FullScreen()) && !m_View.ReadPixels(m_Pixels))
        return false;
    if (m_Pixels.size() != size_t(m_View.Width()) * size_t(m_View.Height()) * 4)
        return false;
    pixels = m_Pixels;
    width = m_View.Width();
    height = m_View.Height();
    return true;
}
bool LayoutWindow::EnterFullScreen(const LayoutScreenMode &mode, std::string &error) {
    m_Capturing = 0;
    // Fullscreen presents the render window normally; remove the editor-owned
    // virtual backbuffer before the Context changes mode or is recreated.
    if (!ReleaseGpuViewport()) {
        error = "3D Layout: cannot release the GPU viewport";
        return false;
    }
    m_ViewportContext = nullptr;
    m_ViewportRenderer = &LayoutWindow::RenderReadbackViewport;
    m_Switching = true;
    VxDisplayMode nativeMode;
    nativeMode.Width = mode.width;
    nativeMode.Height = mode.height;
    nativeMode.Bpp = mode.bitsPerPixel;
    nativeMode.RefreshRate = mode.refreshRate;
    const bool full = m_View.GoFullScreen(mode.driver, nativeMode);
    if (full && m_MainWindow && !(SDL_GetWindowFlags(m_MainWindow) & SDL_WINDOW_HIDDEN))
        m_MainHiddenForFullScreen = SDL_HideWindow(m_MainWindow);
    m_Switching = false;
    SelectViewportContext();
    if (!full)
        error = "3D Layout: cannot enter fullscreen";
    return full;
}
bool LayoutWindow::ExitFullScreen(bool activateEditor, std::string &error) {
    m_Capturing = 0;
    m_PendingToggle = m_PendingDeactivate = false;
    if (!m_View.FullScreen())
        return true;
    if (!ReleaseGpuViewport()) {
        error = "3D Layout: cannot release the GPU viewport";
        return false;
    }
    m_ViewportContext = nullptr;
    m_ViewportRenderer = &LayoutWindow::RenderReadbackViewport;
    m_Switching = true;
    const bool stopped = m_View.StopFullScreen();
    m_Switching = false;
    SelectViewportContext();
    if (!stopped) {
        error = "3D Layout: cannot leave fullscreen";
        return false;
    }
    if (m_MainWindow && m_MainHiddenForFullScreen) {
        // Showing the editor after another application takes focus must not
        // activate it and immediately take that application's focus back.
        const char *hint = SDL_GetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN);
        const bool hadHint = hint != nullptr;
        const std::string previous = hint ? hint : "";
        if (!activateEditor)
            SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
        SDL_ShowWindow(m_MainWindow);
        if (!activateEditor) {
            if (hadHint)
                SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, previous.c_str());
            else
                SDL_ResetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN);
        }
        m_MainHiddenForFullScreen = false;
    }
    if (m_MainWindow && activateEditor)
        SDL_RaiseWindow(m_MainWindow);
    return true;
}

} // namespace vdev
