#include "ui/controls/DevTheme.h"
#include "ui/layout/LayoutWindows.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/TypeAhead.h"
#include "editor/ImGuiDrawing.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vdev {
namespace {
struct LayoutAction {
    int code;
    const char *id, *title, *chord;
    bool tool;
};
const LayoutAction actions[] = {
    {25, "layout.select", "Select", "A", true},
    {9, "layout.translate", "Select and Translate", "X", true},
    {11, "layout.rotate", "Select and Rotate", "C", true},
    {46, "layout.scale-uniform", "Uniform Scale", nullptr, true},
    {47, "layout.scale-volumetric", "Volumetric Scale", nullptr, true},
    {48, "layout.scale-normal", "Normal Scale", nullptr, true},
    {23, "layout.dolly", "Camera Dolly", "D", true},
    {26, "layout.fov", "Camera Field of View", "F", true},
    {51, "layout.zoom", "Camera Zoom", "Z", true},
    {24, "layout.roll", "Roll Camera", "R", true},
    {28, "layout.pan", "Camera Pan", "P", true},
    {49, "layout.orbit", "Orbit Target", nullptr, true},
    {50, "layout.orbit-camera", "Orbit Around Camera", nullptr, true},
    {168, "layout.fly", "Fly Camera", nullptr, true},
    {68, "layout.toggle-fly", "Toggle Fly", "Y", false},
    {128, "layout.add-points", "Add Curve Points", nullptr, true},
    {129, "layout.insert-points", "Insert Curve Points", nullptr, true},
    {130, "layout.edit-points", "Edit Curve Points", nullptr, true},
    {131, "layout.delete-points", "Delete Curve Points", nullptr, true},
    {58, "layout.lock-selection", "Lock Selection", "Space", false},
    {61, "layout.selection-mode", "Selection Mode", nullptr, false},
    {12, "layout.axis-x", "Constrain X Axis", "F5", false},
    {13, "layout.axis-y", "Constrain Y Axis", "F6", false},
    {14, "layout.axis-z", "Constrain Z Axis", "F7", false},
    {42, "layout.plane-xy", "Plane Xy", nullptr, false},
    {43, "layout.plane-yz", "Plane Yz", nullptr, false},
    {44, "layout.plane-xz", "Plane Xz", nullptr, false},
    {66, "layout.snap", "Toggle Snap", "S", false},
    {70, "layout.hierarchy", "Toggle Hierarchy", "H", false},
    {72, "layout.pivot-object", "Pivot Object", nullptr, false},
    {73, "layout.pivot-axis", "Pivot Axis", nullptr, false},
    {74, "layout.pivot-selection", "Pivot Selection", nullptr, false},
    {29, "layout.grid", "Toggle Reference Guide", "G", false},
    {67, "layout.screen-grid", "Toggle Screen Guide", "Shift+G", false},
    {31, "layout.create-camera", "Create Camera", "1", false},
    {32, "layout.create-light", "Create Light", "2", false},
    {34, "layout.create-frame3d", "Create 3D Frame", "3", false},
    {33, "layout.create-curve", "Create Curve", "4", false},
    {60, "layout.create-grid", "Create Grid", "5", false},
    {75, "layout.create-frame2d", "Create 2D Frame", "6", false},
    {80, "layout.create-material", "Create Material", "7", false},
    {81, "layout.create-texture", "Create Texture", "8", false},
    {78, "layout.create-portal", "Create Portal", "9", false},
    {79, "layout.create-portals", "Create Portals", nullptr, false},
    {52, "layout.zoom-selection", "Zoom Selection", "Shift+Z", false},
    {57, "layout.zoom-scene", "Zoom Scene", "W", false},
    {40, "layout.preferences", "General Preferences", nullptr, false},
    {41, "layout.explorer", "3D Layout Explorer", nullptr, false},
    {180, "layout.duplicate-selection", "Duplicate Selection", nullptr, false},
    {56, "layout.snapshots", "Snapshot", nullptr, false},
    {120, "layout.referential-local", "Referential Local", nullptr, false},
    {121, "layout.referential-world", "Referential World", nullptr, false},
    {122, "layout.referential-view", "Referential View", nullptr, false},
    {123, "layout.referential-parent", "Referential Parent", nullptr, false},
    {124, "layout.referential-guide", "Referential Guide", nullptr, false},
    {125, "layout.referential-pick", "Referential Pick", nullptr, false},
    {100, "layout.select-all", "Select All", "Ctrl+A", false},
    {101, "layout.clear-selection", "Clear Selection", "Ctrl+D", false},
    {102, "layout.invert-selection", "Invert Selection", "Ctrl+I", false},
    {103, "layout.copy-selection", "Copy Selection", "Ctrl+C", false},
    {104, "layout.paste-selection", "Paste Selection", "Ctrl+V", false},
    {105, "layout.reference-guide", "Reference Guide", "Ctrl+G", false},
    {106, "layout.delete-selection", "Delete Selection", "Delete", false},
    {107, "layout.previous-camera", "Previous Camera", "Shift+N", false},
    {108, "layout.next-camera", "Next Camera", "N", false},
    {109, "layout.next-plane", "Next Plane", "F8", false},
    {110, "layout.next-pivot", "Next Pivot", "F9", false},
    {111, "layout.next-scale", "Next Scale", "V", false},
    {112, "layout.next-orbit", "Next Orbit", "O", false},
};
} // namespace
const LayoutWindow::Button *LayoutWindow::ControlButtons(size_t &count) {
    static const Button buttons[] = {
        {56, 7, 1, 26, 20, {809, 811, 810, 810, 809}, "Snapshot"},
        {41, 27, 1, 46, 20, {777, 779, 778, 778, 777}, "3D Layout Explorer"},
        {40, 347, 1, 366, 20, {804, 806, 805, 805, 804}, "General Preferences (Ctrl+P)"},
        {25, 8, 24, 29, 45, {774, 776, 775, 775, 774}, "Select (A)"},
        {58, 30, 25, 51, 46, {692, 694, 693, 693, 692}, "Lock Selection (Spacebar)"},
        {61, 52, 24, 73, 45, {1152, 1153, 1151, 1150, 1151}, "Selection Mode"},
        {9, 8, 55, 29, 76, {699, 701, 700, 700, 699}, "Select and Translate (X)"},
        {11, 30, 55, 51, 76, {727, 729, 728, 728, 727}, "Select and Rotate (C)"},
        {10, 52, 55, 73, 76, {}, "Select and Scale (V)"},
        {12, 9, 79, 30, 100, {590, 592, 591, 591, 1187}, "Constrain X Axis (F5)"},
        {13, 31, 79, 52, 100, {596, 598, 597, 597, 1188}, "Constrain Y Axis (F6)"},
        {14, 53, 79, 74, 100, {602, 604, 603, 603, 1189}, "Constrain Z Axis (F7)"},
        {39, 23, 100, 56, 115, {}, "Constrain Plane (F8)"},
        {66, 8, 137, 29, 158, {1240, 1238, 1239, 1239, 1240}, "Toggle Snap (S)"},
        {70, 30, 137, 51, 158, {1256, 1255, 1254, 1257, 1256}, "Toggle Hierarchy (H)"},
        {71, 52, 137, 73, 158, {}, "Pivot Axis (F9)"},
        {29, 31, 166, 52, 187, {1258, 1259, 1260, 1260, 1258}, "Toggle Reference Guide (G)"},
        {67, 53, 166, 74, 187, {678, 680, 679, 679, 678}, "Toggle Screen Guide (Shift+G)"},
        {31, 9, 197, 30, 218, {615, 617, 616, 616, 615}, "Create Camera (1)"},
        {32, 31, 197, 52, 218, {624, 626, 625, 625, 624}, "Create Light (2)"},
        {34, 53, 197, 74, 218, {1278, 1276, 1277, 1277, 1278}, "Create 3D Frame (3)"},
        {33, 9, 218, 30, 239, {618, 620, 619, 619, 618}, "Create Curve (4)"},
        {60, 31, 218, 52, 239, {1148, 1147, 1146, 1146, 1147}, "Create Grid (5)"},
        {75, 53, 218, 74, 239, {621, 622, 623, 623, 621}, "Create 2D Frame (6)"},
        {80, 9, 239, 30, 260, {1324, 1325, 1326, 1326, 1324}, "Create Material (7)"},
        {81, 31, 239, 52, 260, {1327, 1328, 1329, 1329, 1327}, "Create Texture (8)"},
        {77, 53, 239, 74, 260, {}, "Create Portal (9)"},
        {23, 9, 270, 30, 291, {664, 666, 665, 665, 664}, "Camera Dolly (D)"},
        {26, 31, 270, 52, 291, {674, 676, 675, 675, 674}, "Camera Field of View (F)"},
        {27, 53, 270, 74, 291, {}, "Camera Zoom (Z)"},
        {24, 9, 291, 30, 312, {724, 726, 725, 725, 724}, "Roll Camera (R)"},
        {28, 31, 291, 52, 312, {715, 717, 716, 716, 715}, "Camera Pan (P)"},
        {20, 53, 291, 74, 312, {}, "Orbit Target/Orbit Around (O)"}};
    count = std::size(buttons);
    return buttons;
}
const LayoutWindow::Button *LayoutWindow::ControlButton(int command) {
    size_t count = 0;
    const auto *buttons = ControlButtons(count);
    for (size_t i = 0; i < count; ++i)
        if (buttons[i].command == command) return &buttons[i];
    return nullptr;
}
const char *LayoutWindow::ActionId(int command) {
    for (const auto &action : actions)
        if (action.code == command) return action.id;
    return nullptr;
}
const char *LayoutWindow::ControlId(int command) {
    if (const auto *id = ActionId(command)) return id;
    switch (command) {
    case 10:
        return "layout.scale-mode";
    case 39:
        return "layout.plane";
    case 71:
        return "layout.pivot";
    case 20:
        return "layout.orbit-mode";
    case 27:
        return "layout.zoom-mode";
    case 77:
        return "layout.portal-mode";
    case 18:
        return "layout.selection-groups";
    case 19:
        return "layout.cameras";
    case 15:
        return "layout.referential";
    default:
        return nullptr;
    }
}
const char *LayoutWindow::ControlAction(int command) {
    switch (command) {
    case 10:
        return ActionId(111);
    case 39:
        return ActionId(109);
    case 71:
        return ActionId(110);
    case 20:
        return ActionId(112);
    case 27:
        return ActionId(51);
    case 77:
        return ActionId(78);
    default:
        return ActionId(command);
    }
}
void LayoutWindow::ActivateTool(int command) {
    if (command == 168) {
        Window().tool = LayoutTool::Fly;
        return;
    }
    if (command >= 46 && command <= 48) {
        Window().scaleMode = command - 46;
        Window().tool = LayoutTool::Scale;
        return;
    }
    if (command == 49 || command == 50) {
        Window().orbitMode = command - 49;
        Window().tool = LayoutTool::Orbit;
        return;
    }
    if (command == 51) {
        Window().tool = LayoutTool::Zoom;
        return;
    }
    Window().tool = LayoutTool(command);
}
EditorStatus LayoutWindow::ActionState(int command) const {
    if (!m_Window) return EDITOR_REFUSED;
    const auto tool = Window().tool;
    const bool transform = tool == LayoutTool::Translate || tool == LayoutTool::Rotate || tool == LayoutTool::Scale ||
                           tool == LayoutTool::EditPoints;
    if ((command >= 12 && command <= 14) && (!transform || !(AllowedAxes() & (1 << (command - 12)))))
        return EDITOR_REFUSED;
    if ((command == 66 || command == 70 || command == 109 || command == 110) && !transform) return EDITOR_REFUSED;
    if (command == 110 && tool == LayoutTool::Translate) return EDITOR_REFUSED;
    if (command == 26 && !EditorClient::Integer(InputState(), "perspective")) return EDITOR_REFUSED;
    if (command == 24 && EditorClient::Integer(InputState(), "targeted")) return EDITOR_REFUSED;
    if ((command == 31 || command == 32 || command == 33 || command == 34 || command == 60 || command == 75 ||
         command == 80 || command == 81 || command == 78 || command == 79) &&
        !Client().Scope().document.value)
        return EDITOR_REFUSED;
    return EDITOR_OK;
}
uint32_t LayoutWindow::ToolFrame(const EditorToolInput &input) {
    if (input.phase == EDITOR_TOOL_CANCEL || input.phase == EDITOR_TOOL_LEAVE) {
        const bool fly = Window().tool == LayoutTool::Fly;
        if (input.phase == EDITOR_TOOL_LEAVE && fly) EndFly();
        CancelEdits();
        return fly ? EDITOR_TOOL_DEFAULT : 0;
    }
    if (input.phase != EDITOR_TOOL_FRAME) return 0;
    if (Window().tool == LayoutTool::Fly && !Window().gestureEdit) BeginFly();
    ViewInput(m_ViewMin, input.hovered != 0);
    return (Busy() ? EDITOR_TOOL_CAPTURED : 0) |
           (Window().tool == LayoutTool::Select && Client().ActiveTool() == "layout.fly" ? EDITOR_TOOL_DEFAULT : 0);
}
bool LayoutWindows::RegisterControls(EditorModuleDesc module, std::string &error) {
    m_Actions.clear();
    std::vector<EditorToolDesc> tools;
    std::vector<EditorCommandDesc> commands;
    std::vector<EditorToolbarItemDesc> controls;
    std::vector<EditorBindingDesc> bindings;
    const EditorToolbarLocation locations[] = {
        {EDITOR_TEXT("layout.top"), EDITOR_TEXT("3D Layout top toolbar"), EDITOR_TEXT("layout.viewport")},
        {EDITOR_TEXT("layout.left"), EDITOR_TEXT("3D Layout tools"), EDITOR_TEXT("layout.viewport")}};
    for (const auto &action : actions) {
        m_Actions.push_back({this, action.code});
        auto &entry = m_Actions.back();
        const auto check = [](void *p, const EditorHost *, const EditorRequest *r) {
            auto &entry = *static_cast<Action *>(p);
            auto *window = entry.owner->Find(r->scope.view);
            return window ? window->ActionState(entry.command) : EDITOR_STALE;
        };
        if (action.tool) {
            EditorToolDesc d{};
            d.size = sizeof(d);
            d.id = EditorLiteral(action.id);
            d.title = EditorLiteral(action.title);
            d.provider = EDITOR_TEXT("builtin.layout-tools");
            d.view = EDITOR_TEXT("layout.viewport");
            d.data = &entry;
            d.state = check;
            d.input = [](void *p, const EditorHost *, const EditorRequest *r, const EditorToolInput *input,
                         uint32_t *flags) {
                auto &entry = *static_cast<Action *>(p);
                auto *window = entry.owner->Find(r->scope.view);
                if (!window) return EDITOR_STALE;
                if (input->phase == EDITOR_TOOL_ENTER) {
                    window->ActivateTool(entry.command);
                    *flags = 0;
                    return EDITOR_OK;
                }
                if (input->phase == EDITOR_TOOL_LEAVE && entry.command >= 128 && entry.command <= 131)
                    window->ClearCurveTool();
                *flags = window->ToolFrame(*input);
                return EDITOR_OK;
            };
            tools.push_back(d);
        } else {
            EditorCommandDesc d{};
            d.size = sizeof(d);
            d.id = EditorLiteral(action.id);
            d.title = EditorLiteral(action.title);
            d.provider = EDITOR_TEXT("builtin.layout-tools");
            d.view = EDITOR_TEXT("layout.viewport");
            d.data = &entry;
            d.state = check;
            d.area = EDITOR_WORKSPACE;
            d.effect = EDITOR_SESSION_CHANGE;
            d.run = [](void *p, const EditorHost *, const EditorRequest *r, EditorSink) {
                auto &entry = *static_cast<Action *>(p);
                auto *window = entry.owner->Find(r->scope.view);
                if (!window) return EDITOR_STALE;
                window->RunAction(entry.command);
                return EDITOR_OK;
            };
            commands.push_back(d);
        }
        if (action.chord)
            bindings.push_back({EditorLiteral(action.id), EditorLiteral(action.chord), EDITOR_TEXT("layout.viewport")});
    }
    bindings.push_back({EDITOR_TEXT("layout.toggle-fullscreen"), EDITOR_TEXT("Alt+Enter"), {}});
    auto add = [&](int code, float x, float y, float width, float height, bool top) {
        m_Actions.push_back({this, code});
        auto &entry = m_Actions.back();
        EditorToolbarItemDesc d{};
        d.size = sizeof(d);
        d.imguiVersion = EDITOR_IMGUI_VERSION;
        d.id = EditorLiteral(LayoutWindow::ControlId(code));
        d.provider = EDITOR_TEXT("builtin.layout-toolbar");
        d.location = top ? EditorString EDITOR_TEXT("layout.top") : EditorString EDITOR_TEXT("layout.left");
        entry.title = LayoutWindow::ControlButton(code) ? LayoutWindow::ControlButton(code)->tooltip
                                                        : LayoutWindow::ControlId(code);
        if (const auto at = entry.title.find(" ("); at != std::string::npos) entry.title.resize(at);
        d.title = EditorText(entry.title);
        if (LayoutWindow::ControlAction(code)) d.action = EditorLiteral(LayoutWindow::ControlAction(code));
        d.x = x;
        d.y = y;
        d.width = width;
        d.height = height;
        d.data = &entry;
        d.draw = [](void *p, const EditorHost *, const EditorRequest *r, const EditorToolbarFrame *frame) {
            auto &entry = *static_cast<Action *>(p);
            auto *window = entry.owner->Find(r->scope.view);
            return window ? window->DrawControl(entry.command, *frame) : 0u;
        };
        controls.push_back(d);
    };
    size_t count = 0;
    const auto *buttons = LayoutWindow::ControlButtons(count);
    for (size_t i = 0; i < count; ++i) {
        const auto &b = buttons[i];
        add(b.command, b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0, b.y0 < 21);
    }
    add(18, 51, 1, 140, 19, true);
    add(19, 202, 1, 140, 19, true);
    add(15, 9, 116, 64, 17, false);
    module.tools = tools.data();
    module.toolCount = uint32_t(tools.size());
    module.commands = commands.data();
    module.commandCount = uint32_t(commands.size());
    module.toolbarItems = controls.data();
    module.toolbarItemCount = uint32_t(controls.size());
    module.toolbars = locations;
    module.toolbarCount = 2;
    module.bindings = bindings.data();
    module.bindingCount = uint32_t(bindings.size());
    return m_Editor.Register(module, error);
}

uint32_t LayoutWindow::DrawControl(int command, const EditorToolbarFrame &frame) {
    const float s = Window().scale;
    const bool transforming = Window().tool == LayoutTool::Translate || Window().tool == LayoutTool::Rotate ||
                              Window().tool == LayoutTool::Scale || Window().tool == LayoutTool::EditPoints;
    if ((command == 12 || command == 13 || command == 14 || command == 39 || command == 15 || command == 66 ||
         command == 70 || command == 71) &&
        !transforming)
        return 0;
    const auto *original = ControlButton(command);
    Button b{};
    if (original) b = *original;
    b.x0 = int(std::lround((frame.left - Window().origin.x) / s));
    b.y0 = int(std::lround((frame.top - Window().origin.y) / s));
    b.x1 = b.x0 + int(std::lround(frame.width / s));
    b.y1 = b.y0 + int(std::lround(frame.height / s));
    if (frame.tooltip.size) b.tooltip = frame.tooltip.data;
    auto &settings = Settings();
    if (command == 18 || command == 19) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, CurrentDevTheme().controls[2]);
        ImGui::PushStyleColor(ImGuiCol_Button, CurrentDevTheme().controls[2]);
        ImGui::PushStyleColor(ImGuiCol_Text, CurrentDevTheme().pill[3]);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(4 * s, std::max(0.0f, (19 * s - ImGui::GetFontSize()) * .5f)));
        if (command == 18)
            SelectionGroups({frame.left, frame.top}, frame.width);
        else {
            ImGui::SetCursorScreenPos({frame.left, frame.top});
            ImGui::SetNextItemWidth(frame.width);
            auto &cameras = Window().cameraData;
            const bool ready = ReadLayoutScene(Client(), "cameras", cameras);
            const auto *current = ready ? cameras.Find(cameras.camera.object) : nullptr;
            if (ImGui::BeginCombo("##cameras", current ? current->name.c_str() : "")) {
                TypeAhead typeAhead;
                if (ready)
                    for (const auto &camera : cameras.rows) {
                        ImGui::PushID(int(camera.object.object));
                        if (typeAhead.Selectable(camera.name.c_str(), current && current->object == camera.object))
                            Execute("layout.set-camera", {ObjectArgument("target", camera.object)});
                        ImGui::PopID();
                    }
                ImGui::EndCombo();
            }
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        return 0;
    }
    if (command == 15) {
        if (Window().tool != LayoutTool::Translate && Window().tool != LayoutTool::EditPoints && settings.pivot != 1)
            return 0;
        static const char *names[] = {"Local", "World", "View", "Parent", "Ref. Guide", "Pick Obj."};
        ImGui::SetCursorScreenPos({frame.left, frame.top});
        ImGui::SetNextItemWidth(frame.width);
        ImGui::PushFont(nullptr, 10 * s);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * s, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, CurrentDevTheme().controls[7]);
        if (ImGui::BeginCombo("##referential", names[std::clamp(settings.referential, 0, 5)])) {
            TypeAhead typeAhead;
            for (int i = 0; i < 6; ++i)
                if (typeAhead.Selectable(names[i], settings.referential == i)) {
                    Command(120 + i);
                }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::PopFont();
        return 0;
    }
    if (!original) return 0;
    const auto *action = ControlAction(command);
    if (!action) action = "";
    const bool replaced = frame.action.size && (frame.action.size != std::strlen(action) ||
                                                std::memcmp(frame.action.data, action, frame.action.size) != 0);
    if (replaced) return ToolbarButton(b, frame.checked, frame.enabled);
    auto menu = [&](const std::vector<Item> &items, int &index, bool checked) {
        if (MenuButton(b, items, index, checked)) {
            Command(items[index].command);
            return true;
        }
        return false;
    };
    if (command == 10) {
        static const std::vector<Item> v = {
            {46, {730, 732, 731}, "Uniform"}, {47, {736, 738, 737}, "Volumetric"}, {48, {733, 735, 734}, "Normal"}};
        menu(v, Window().scaleMode, Window().tool == LayoutTool::Scale);
        return 0;
    }
    if (command == 39) {
        static const std::vector<Item> v = {
            {42, {593, 595, 594}, "XY"}, {43, {599, 601, 600}, "YZ"}, {44, {605, 607, 606}, "XZ"}};
        int i = settings.axes == 6 ? 1 : settings.axes == 5 ? 2 : 0;
        menu(v, i, settings.axes == (i == 1 ? 6 : i == 2 ? 5 : 3));
        return 0;
    }
    if (command == 71) {
        if (Window().tool == LayoutTool::Translate || Window().tool == LayoutTool::EditPoints) return 0;
        static const std::vector<Item> v = {{72, {1262, 1263, 1261}, "Object"},
                                            {73, {1265, 1266, 1264}, "Axis"},
                                            {74, {1268, 1269, 1267}, "Selection"}};
        menu(v, settings.pivot, false);
        return 0;
    }
    if (command == 20) {
        static const std::vector<Item> v = {{49, {706, 708, 707}, "Orbit"},
                                            {50, {709, 711, 710}, "Orbit Around Camera"}};
        menu(v, Window().orbitMode, Window().tool == LayoutTool::Orbit);
        return 0;
    }
    if (command == 27) {
        static const std::vector<Item> v = {{51, {836, 838, 837}, "Zoom"},
                                            {52, {842, 844, 843}, "Zoom on Selection"},
                                            {57, {839, 841, 840}, "Zoom on Scene"}};
        menu(v, Window().zoomMode, Window().tool == LayoutTool::Zoom);
        return 0;
    }
    if (command == 77) {
        static const std::vector<Item> v = {{79, {1340, 1338, 1339}, "Create Portals"},
                                            {78, {1311, 1309, 1310}, "Create Portal"}};
        menu(v, Window().portalMode, false);
        return 0;
    }
    bool checked = frame.checked, enabled = frame.enabled;
    if (command == 58) checked = Window().selectionData.locked;
    if (command == 61) checked = m_Preferences.Bool("3D Layout\\Strict Selection");
    if (command >= 12 && command <= 14) {
        checked = (settings.axes & (1 << (command - 12))) != 0;
        enabled = enabled && (AllowedAxes() & (1 << (command - 12)));
    }
    if (command == 66) checked = settings.snap;
    if (command == 70) checked = settings.hierarchy;
    if (command == 29) checked = m_Preferences.Bool("3D Layout\\Author Mode\\Show Grid");
    if (command == 67) checked = m_Preferences.Bool("3D Layout\\Show Screen Grid");
    if (command == 56) {
        checked = Window().snapshotOpen;
        enabled = enabled && m_Started;
    }
    if (command == 41) {
        checked = Window().document.explorerOpen;
        enabled = enabled && m_Started && Client().Scope().document.value;
    }
    if (command == 26) enabled = enabled && EditorClient::Integer(m_Presentation, "perspective");
    if (command == 24) enabled = enabled && !EditorClient::Integer(m_Presentation, "targeted");
    if (command == 31 || command == 32 || command == 33 || command == 34 || command == 60 || command == 75 ||
        command == 80 || command == 81)
        enabled = enabled && Client().Scope().document.value;
    return ToolbarButton(b, checked, enabled);
}
} // namespace vdev
