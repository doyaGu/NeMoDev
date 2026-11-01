// The 3D Layout's input: the camera tools, selection, transformations and
// 2D handles, the keys, the flycam and the curve tools.
#include "ui/layout/LayoutWindow.h"
#ifdef _WIN32
#include <windows.h>
#endif



#include "imgui_internal.h"

#include <algorithm>
#include <map>

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevTheme.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

namespace {
EditorField GestureOptions(const CameraSteps &camera, const LayoutTransformSettings &settings,
                           const TransformSteps &transform, const Snap2D &snapping, const Preferences &preferences,
                           int scaleMode, bool locked) {
    EditorField result{"options", EDITOR_RECORD};
    const auto real = [&](const char *key, float n) {
        EditorField v{key, EDITOR_REAL_VALUE};
        v.real = n;
        result.fields.push_back(std::move(v));
    };
    const auto number = [&](const char *key, uint64_t n) {
        result.fields.push_back({key, EDITOR_INTEGER_VALUE, {}, {}, n});
    };
    const auto vector = [&](const char *key, std::initializer_list<double> n) {
        EditorField v{key, EDITOR_REAL_LIST};
        v.reals = n;
        result.fields.push_back(std::move(v));
    };
    real("camera-rotate", camera.rotate);
    real("camera-translate", camera.translate);
    real("camera-zoom", camera.zoom);
    real("camera-ortho-zoom", camera.orthoZoom);
    real("camera-roll", camera.roll);
    real("camera-fov", camera.fov);
    real("free-orbit-value", camera.freeOrbitValue);
    real("object-translate", transform.translate);
    real("object-rotate", transform.rotate);
    real("object-scale", transform.scale);
    real("snap-angle", transform.snapAngle);
    real("snap-scale", transform.snapScale);
    number("camera-rotate-constant", camera.rotateConstant);
    number("camera-translate-constant", camera.translateConstant);
    number("camera-zoom-constant", camera.zoomConstant);
    number("camera-ortho-zoom-constant", camera.orthoZoomConstant);
    number("camera-roll-constant", camera.rollConstant);
    number("camera-fov-constant", camera.fovConstant);
    number("translate-sticky", transform.translateSticky);
    number("rotate-sticky", transform.rotateSticky);
    number("scale-sticky", transform.scaleSticky);
    number("hierarchy", settings.hierarchy);
    number("snap", settings.snap);
    number("homogenous", snapping.homogenous);
    number("shift", ImGui::GetIO().KeyShift);
    number("ctrl", ImGui::GetIO().KeyCtrl);
    number("locked", locked);
    number("free-orbit-type", camera.freeOrbitType);
    number("orbit-selection-type", camera.orbitSelectionType);
    number("axes", settings.axes);
    number("referential", settings.referential);
    number("pivot", settings.pivot);
    number("scale-mode", scaleMode);
    number("snap-mode", transform.snapMode);
    number("snap-radius", transform.snapRadius);
    number("snap-influence", snapping.influence);
    real("fly-turn", preferences.Float("3D Layout\\Actions\\Camera Fly Turn Step"));
    real("fly-advance", preferences.Float("3D Layout\\Actions\\Camera Fly Advance Step"));
    number("fly-constant", preferences.Bool("3D Layout\\Actions\\Camera Navigate Constant Speed Mode"));
    vector("world-min", {camera.worldMin.x, camera.worldMin.y, camera.worldMin.z});
    vector("world-max", {camera.worldMax.x, camera.worldMax.y, camera.worldMax.z});
    vector("world-step", {transform.worldStep.x, transform.worldStep.y, transform.worldStep.z});
    vector("screen-step", {snapping.screenStep[0], snapping.screenStep[1]});
    vector("screen-offset", {snapping.screenOffset[0], snapping.screenOffset[1]});
    return result;
}
EditorField MotionValue(int x, int y, bool move = false, bool idle = false) {
    const auto &io = ImGui::GetIO();
    EditorField value{"value", EDITOR_RECORD}, at{"position", EDITOR_REAL_LIST}, time{"time", EDITOR_REAL_VALUE};
    at.reals = {double(x), double(y)};
    time.real = ImGui::GetTime();
    value.fields = {std::move(at),
                    std::move(time),
                    {"shift", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(io.KeyShift)},
                    {"ctrl", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(io.KeyCtrl)},
                    {"move", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(move)},
                    {"idle", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(idle)},
                    {"buttons",
                     EDITOR_INTEGER_VALUE,
                     {},
                     {},
                     uint64_t((ImGui::IsMouseDown(ImGuiMouseButton_Left) ? 1 : 0) |
                              (ImGui::IsMouseDown(ImGuiMouseButton_Right) ? 2 : 0))}};
    return value;
}
} // namespace

EditorResponse LayoutWindow::InputState(int x, int y, bool pick, ObjectRef curve, bool ignoreUnpickable) const {
    EditorField position{"position", EDITOR_REAL_LIST};
    if (pick)
        position.reals = {double(x), double(y)};
    const std::vector<EditorField> fields{
        std::move(position),
        {"shift", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(ignoreUnpickable && ImGui::GetIO().KeyShift)},
        {"curve", EDITOR_OBJECT, curve}};
    const auto values = BorrowEditorFields(fields);
    return Client().Query("layout.input", {values.data(), values.data() + values.size()});
}

bool LayoutWindow::BeginGesture(std::vector<EditorField> fields, unsigned kind, unsigned action,
                                EditorOperation selection) {
    m_GestureKind = kind;
    m_GestureAction = action;
    if (selection) {
        m_PendingGesture = std::move(fields);
        m_GestureSelection = selection;
        return true;
    }
    const auto values = BorrowEditorFields(fields);
    if (Client().BeginCommand("layout.edit-gesture", {values.data(), values.data() + values.size()}, m_GestureEdit) !=
        EDITOR_OK)
        return false;
    for (const auto &field : fields)
        if (field.key == "value") {
            const std::vector<EditorField> owned{field};
            const auto value = BorrowEditorFields(owned);
            if (m_EditorHost->editValue(m_EditorHost->data, m_GestureEdit, value.data()) != EDITOR_OK) {
                Client().CancelEdit(m_GestureEdit);
                return false;
            }
        }
    m_Interaction = InputState();
    return true;
}

bool LayoutWindow::UpdateGesture(int x, int y, bool move, bool idle) {
    const std::vector<EditorField> fields{MotionValue(x, y, move, idle)};
    const auto value = BorrowEditorFields(fields);
    if (!m_GestureEdit || m_EditorHost->editValue(m_EditorHost->data, m_GestureEdit, value.data()) != EDITOR_OK) {
        CancelEdits();
        return false;
    }
    m_Interaction = InputState();
    UpdateCoordinates();
    return true;
}

void LayoutWindow::FinishGesture() {
    if (!m_GestureEdit)
        return;
    const auto status = m_EditorHost->finishEdit(m_EditorHost->data, m_GestureEdit, 1, nullptr);
    if (status != EDITOR_QUEUED && status != EDITOR_OK && status != EDITOR_NO_CHANGE)
        Client().CancelEdit(m_GestureEdit);
    m_GestureEdit = 0;
    m_CurveEditing = false;
}

std::vector<EditorField> LayoutWindow::GestureFields(const EditorResponse &input, unsigned kind, unsigned action, int x,
                                                     int y, const std::vector<ObjectRef> &objects,
                                                     const LayoutTransformSettings &settings) const {
    const auto *target = EditorClient::Find(input, "target");
    if (input.status != EDITOR_OK || !target || !target->object)
        return {};
    EditorField start{"start", EDITOR_REAL_LIST}, selected{"objects", EDITOR_OBJECT_LIST};
    start.reals = {double(x), double(y)};
    selected.objects = objects;
    const auto &selection = m_SelectionData;
    EditorField started{"started", EDITOR_REAL_VALUE};
    started.real = ImGui::GetTime();
    return {
        {"target", EDITOR_OBJECT, target->object},
        {"kind", EDITOR_INTEGER_VALUE, {}, {}, kind},
        {"action", EDITOR_INTEGER_VALUE, {}, {}, action},
        std::move(start),
        std::move(selected),
        GestureOptions(Steps(), settings, TransformStepsOf(), Snap2DOf(), m_Preferences, m_ScaleMode, selection.locked),
        MotionValue(x, y),
        started};
}

bool LayoutWindow::BeginCamera(CameraAction action, int x, int y, ImGuiMouseButton button) {
    if (!ReadLayoutSelection(Client(), m_SelectionData))
        return false;
    auto fields = GestureFields(InputState(), 0, unsigned(action), x, y, m_SelectionData.objects, Settings());
    if (fields.empty() || !BeginGesture(std::move(fields), 0, unsigned(action)))
        return false;
    m_GestureButton = button;
    return true;
}

void LayoutWindow::CancelEdits() {
    // Snapshot capture crosses from the buffer window into this viewport.
    // Its own cancel/close and viewport/document changes end it, not an
    // ordinary focus change that only cancels object editing.
    if (m_EditorHost)
        Client().CancelEdit(m_GestureEdit);
    m_PendingGesture.clear();
    m_GestureSelection = 0;
    if (m_Tool == LayoutTool::Fly)
        m_Tool = LayoutTool::Select;
    m_CurveEditing = false;
    m_Selecting = m_Rectangle = false;
    m_RightPressed = false;
}

// The mouse in the view (Dev.exe sub_4B95C0): the left button drags the
// current camera tool, the middle button pans (Alt: orbits), the wheel
// zooms; the right button cancels a drag.
void LayoutWindow::ViewInput(const ImVec2 &at, bool hovered) {
    const ImGuiIO &io = ImGui::GetIO();
    if (Busy() && (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                   (m_Tool != LayoutTool::Fly && ImGui::IsMouseClicked(ImGuiMouseButton_Right)))) {
        CancelEdits();
        return;
    }
    const int x = static_cast<int>(io.MousePos.x - at.x), y = static_cast<int>(io.MousePos.y - at.y);
    if (m_Tool == LayoutTool::Fly) {
        FlyFrame(x, y);
        return;
    }
    if (!m_PendingGesture.empty()) {
        const auto result = Client().Completion(m_GestureSelection);
        if (result.status == EDITOR_QUEUED)
            return;
        auto fields = std::move(m_PendingGesture);
        m_PendingGesture.clear();
        m_GestureSelection = 0;
        if ((result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE) ||
            !BeginGesture(std::move(fields), m_GestureKind, m_GestureAction)) {
            CancelEdits();
            return;
        }
    }
    if (m_GestureEdit) {
        const auto button = m_GestureKind == 0 ? m_GestureButton : ImGuiMouseButton_Left;
        if (!ImGui::IsMouseDown(button))
            FinishGesture();
        else
            UpdateGesture(x, y, io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f, m_GestureKind == 0);
        return;
    }
    if (m_Selecting) {
#ifdef _WIN32
        const float drag = std::max(1.0f, static_cast<float>(GetSystemMetrics(SM_CXDRAG)));
#else
        const float drag = std::max(1.0f, io.MouseDragThreshold);
#endif
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (m_Rectangle)
                ApplyRect(VxRect(m_PressAt.x, m_PressAt.y, static_cast<float>(x), static_cast<float>(y)));
            else {
                const auto input = InputState(int(m_PressAt.x), int(m_PressAt.y), true, {}, true);
                if (input.status == EDITOR_OK)
                    ApplyClick(EditorClient::Find(input, "picked")->object);
            }
            m_Selecting = m_Rectangle = false;
        } else if (std::fabs(x - m_PressAt.x) >= drag || std::fabs(y - m_PressAt.y) >= drag) {
            m_Rectangle = true;
        }
        return;
    }
    // Dev.exe sub_4B95C0 (WM_RBUTTONUP): the shortcut menu when the right
    // button alone was pressed and released, no camera tool dragging.
    if (m_RightPressed && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        m_RightPressed = false;
        if (hovered && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
            m_Tool != LayoutTool::AddPoints && !m_PlayerMode)
            OpenContextMenu(x, y);
    }
    if (m_RightPressed && (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)))
        m_RightPressed = false;
    if (!hovered) return;
    // The referent picked (Pivot Axis Object, Referential Pick Obj.).
    if (m_PickingReferent) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_PickingReferent = false;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_PickingReferent = false;
            const auto input = InputState(x, y, true);
            if (input.status == EDITOR_OK)
                if (const auto *picked = EditorClient::Find(input, "entity"); picked && picked->object) {
                    Settings().referent = picked->object;
                    m_State.Log(LogLevel::Info, "Referent: " + EditorClient::Text(input, "name"));
                }
        }
        return;
    }
    // The select cursor over an entity the Selection tool would pick.
    if (m_Tool == LayoutTool::Select && !m_PlayerMode && !ImGui::IsAnyMouseDown()) {
        if (m_HoverAt.x != x || m_HoverAt.y != y) {
            m_HoverAt = ImVec2(x, y);
            const auto input = InputState(x, y, true);
            const auto has = [&](const char *key) {
                const auto *v = EditorClient::Find(input, key);
                return v && bool(v->object);
            };
            m_HoverEntity = input.status == EDITOR_OK && (has("entity") || has("character") || has("sprite"));
        }
        if (m_HoverEntity) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    // Dev.exe sub_4BDF40: the handles' cursors (move, the corner, the height, the width).
    if (const int handle = HandleAt(x, y, nullptr)) {
        static const ImGuiMouseCursor cursors[5] = {ImGuiMouseCursor_Arrow, ImGuiMouseCursor_ResizeAll,
                                                     ImGuiMouseCursor_ResizeNWSE, ImGuiMouseCursor_ResizeNS,
                                                     ImGuiMouseCursor_ResizeEW};
        ImGui::SetMouseCursor(cursors[handle]);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && BeginHandle(x, y)) return;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        m_RightPressed = true;
        // Add, Insert and Delete Points take either button.
        if (m_Tool == LayoutTool::AddPoints || m_Tool == LayoutTool::InsertPoints || m_Tool == LayoutTool::DeletePoints)
            CurveClick(x, y, true);
    }
    if (m_PlayerMode && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        PlayerClick(x, y, true);
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        switch (m_Tool) {
        case LayoutTool::Select:
            m_Selecting = true;
            m_Rectangle = false;
            m_PressAt = ImVec2(static_cast<float>(x), static_cast<float>(y));
            break;
        case LayoutTool::Translate:
        case LayoutTool::Rotate:
        case LayoutTool::Scale:
            if (!BeginTransform(x, y)) {
                m_Selecting = true;
                m_Rectangle = false;
                m_PressAt = ImVec2(static_cast<float>(x), static_cast<float>(y));
            }
            break;
        case LayoutTool::Dolly: BeginCamera(CameraAction::Dolly, x, y, ImGuiMouseButton_Left); break;
        case LayoutTool::Zoom: BeginCamera(CameraAction::Zoom, x, y, ImGuiMouseButton_Left); break;
        case LayoutTool::Pan: BeginCamera(CameraAction::Pan, x, y, ImGuiMouseButton_Left); break;
        case LayoutTool::Orbit:
            BeginCamera(m_OrbitMode ? CameraAction::OrbitCamera : CameraAction::Orbit, x, y, ImGuiMouseButton_Left);
            break;
        case LayoutTool::Roll: BeginCamera(CameraAction::Roll, x, y, ImGuiMouseButton_Left); break;
        case LayoutTool::Fov: BeginCamera(CameraAction::Fov, x, y, ImGuiMouseButton_Left); break;
        case LayoutTool::AddPoints:
        case LayoutTool::InsertPoints:
        case LayoutTool::DeletePoints: CurveClick(x, y, false); break;
        case LayoutTool::EditPoints: BeginCurveEdit(x, y); break;
        default: break;
        }
        if (m_PlayerMode)
            PlayerClick(x, y, false);
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && !m_PlayerMode) {
        // Dev.exe sub_4C4E30: Alt orbits (in the Orbit tool's mode), else it pans.
        if (io.KeyAlt)
            BeginCamera(m_OrbitMode ? CameraAction::OrbitCamera : CameraAction::Orbit, x, y, ImGuiMouseButton_Middle);
        else
            BeginCamera(CameraAction::Pan, x, y, ImGuiMouseButton_Middle);
    }
    if (io.MouseWheel != 0.0f && !m_PlayerMode) {
        const int notches = static_cast<int>(io.MouseWheel > 0.0f ? std::ceil(io.MouseWheel) : std::floor(io.MouseWheel));
        const auto input = InputState();
        const auto *target = EditorClient::Find(input, "target");
        if (target && target->object) {
            EditorField extent{"extent", EDITOR_REAL_VALUE}, count{"notches", EDITOR_REAL_VALUE};
            extent.real = m_ZoomExtent;
            count.real = notches;
            std::vector<EditorField> args{
                {"target", EDITOR_OBJECT, target->object},
                GestureOptions(Steps(), Settings(), TransformStepsOf(), Snap2DOf(), m_Preferences, m_ScaleMode, false),
                extent,
                count};
            const auto values = BorrowEditorFields(args);
            Execute("layout.wheel-zoom", {values.data(), values.data() + values.size()});
        }
    }
}

#ifdef VIRTOOLSDEV_UI_TESTS
void LayoutWindow::StartupOptions(const std::string &tool, const std::string &camera) {
    static const std::map<std::string, int> tools = {{"select", 25}, {"translate", 9},  {"rotate", 11}, {"scale", 46},
                                                     {"orbit", 49},  {"orbitcamera", 50}, {"dolly", 23}, {"zoom", 51},
                                                     {"pan", 28},    {"roll", 24},       {"fov", 26}, {"fly", 68}};
    // The Curve Setup's tools, on the composition's first curve.
    static const std::map<std::string, int> curveTools = {
        {"addpoints", 128}, {"insertpoints", 129}, {"editpoints", 130}, {"deletepoints", 131}};
    auto found = tools.find(tool);
    auto curveTool = curveTools.find(tool);
    if (found != tools.end()) {
        Command(found->second);
    } else if (curveTool != curveTools.end()) {
        LayoutSceneData scene;
        const LayoutObjectRow *curve = nullptr;
        if (ReadLayoutScene(Client(), "explorer", scene))
            for (const auto &row : scene.rows)
                if (row.cid == CKCID_CURVE) {
                    curve = &row;
                    break;
                }
        if (curve) {
            Execute("layout.set-curve-tool", {ObjectArgument("target", curve->object),
                                              IntegerArgument("tool", curveTool->second), IntegerArgument("plane", 0)});
            SyncCurveTool();
        } else
            m_State.Log(LogLevel::Warning, "--layout-tool: no curve");
    } else if (!tool.empty()) {
        m_State.Log(LogLevel::Warning, "--layout-tool: unknown tool " + tool);
    }
    if (camera.empty()) return;
    LayoutSceneData cameras;
    if (ReadLayoutScene(Client(), "cameras", cameras)) for (const auto &row : cameras.rows) if (row.name == camera) {
        Execute("layout.set-camera", {ObjectArgument("target", row.object)}); return;
    }
    m_State.Log(LogLevel::Warning, "--layout-view: no camera named " + AnsiToUtf8(camera));
}

void LayoutWindow::StartupSelection(const std::vector<std::string> &names, const std::string &group,
                                    const std::vector<int> &commands) {
    LayoutSceneData scene;
    ReadLayoutScene(Client(), "explorer", scene);
    std::vector<ObjectRef> objects;
    for (const std::string &name : names) {
        const auto found = std::find_if(scene.rows.begin(), scene.rows.end(),
                                        [&](const LayoutObjectRow &row) { return row.name == AnsiToUtf8(name); });
        if (found != scene.rows.end())
            objects.push_back(found->object);
        else m_State.Log(LogLevel::Warning, "--layout-select: no 2D or 3D entity named " + AnsiToUtf8(name));
    }
    if (!objects.empty()) SetSelection(objects);
    if (!group.empty()) EnterGroup(AnsiToUtf8(group));
    for (int command : commands) Command(command);
}

void LayoutWindow::StartupDrop(const CKGUID &guid, float x, float y) {
    m_StartupDrop = guid;
    m_StartupDropAt = ImVec2(x, y);
    m_StartupDropFrames = 10;
}
#endif

void LayoutWindow::SetSelection(std::vector<ObjectRef> objects, bool invert) {
    LayoutSelectionData selection;
    if (!ReadLayoutSelection(Client(), selection) || selection.locked || (!invert && objects == selection.objects))
        return;
    Execute(invert ? "selection.invert" : "selection.replace", {ObjectsArgument("objects", objects)});
}

void LayoutWindow::RestoreSelection(const std::vector<ObjectRef> &objects, bool locked) {
    Execute("selection.restore", {ObjectsArgument("objects", objects), IntegerArgument("locked", locked)});
}

void LayoutWindow::LockSelection(bool locked) { Execute("selection.lock", {IntegerArgument("locked", locked)}); }

void LayoutWindow::ClearSelection() { SetSelection({}); }

void LayoutWindow::SelectAll(bool invert) {
    const auto result = Client().Query("layout.input", {IntegerArgument("select", 1)});
    if (result.status == EDITOR_OK)
        if (const auto *objects = EditorClient::Find(result, "objects"))
            SetSelection(objects->objects, invert);
}

void LayoutWindow::ApplyClick(ObjectRef picked) {
    LayoutSelectionData selection;
    if (!ReadLayoutSelection(Client(), selection) || selection.locked)
        return;
    const bool ctrl = ImGui::GetIO().KeyCtrl;
    auto objects = selection.objects;
    if (!picked) {
        if (!ctrl)
            objects.clear();
    } else if (std::find(objects.begin(), objects.end(), picked) != objects.end()) {
        if (objects.size() <= 1 || ctrl) objects.erase(std::remove(objects.begin(), objects.end(), picked), objects.end());
        else objects.assign(1, picked);
    } else {
        if (!ctrl)
            objects.clear();
        objects.push_back(picked);
    }
    SetSelection(std::move(objects));
}

void LayoutWindow::ApplyRect(const VxRect &rect) {
    LayoutSelectionData selection;
    if (!ReadLayoutSelection(Client(), selection) || selection.locked)
        return;
    EditorField rectangle{"rectangle", EDITOR_REAL_LIST};
    rectangle.reals = {rect.left, rect.top, rect.right, rect.bottom};
    const std::vector<EditorField> fields{
        rectangle,
        {"select", EDITOR_INTEGER_VALUE, {}, {}, 2},
        {"strict", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(m_Preferences.Bool("3D Layout\\Strict Selection"))}};
    const auto values = BorrowEditorFields(fields);
    const auto result = Client().Query("layout.input", {values.data(), values.data() + values.size()});
    const auto *picked = EditorClient::Find(result, "objects");
    if (result.status != EDITOR_OK || !picked)
        return;
    auto objects = ImGui::GetIO().KeyCtrl ? selection.objects : std::vector<ObjectRef>{};
    for (auto object : picked->objects)
        if (std::find(objects.begin(), objects.end(), object) == objects.end())
            objects.push_back(object);
    SetSelection(std::move(objects));
}

void LayoutWindow::PlayerClick(int x, int y, bool twice) {
    const auto input = InputState(x, y, true);
    if (input.status != EDITOR_OK)
        return;
    std::vector<EditorField> fields{{"double-click", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(twice)}};
    for (const char *key : {"entity", "sprite", "character"})
        if (const auto *field = EditorClient::Find(input, key))
            fields.push_back(*field);
    const auto values = BorrowEditorFields(fields);
    Execute("layout.player-click", {values.data(), values.data() + values.size()});
}

// Documentation "Axes Constraint": "when the current camera is set to the
// Top View, only X and Z axis are available for Select and Translate" (the
// Front view X and Y, the Right view Y and Z).
int LayoutWindow::AllowedAxes() const {
    if (m_Tool != LayoutTool::Translate && m_Tool != LayoutTool::Select)
        return 7;
    const auto state = InputState();
    return state.status == EDITOR_OK ? int(EditorClient::Integer(state, "allowed-axes")) : 7;
}

LayoutTransformSettings &LayoutWindow::Settings() {
    switch (m_Tool) {
    case LayoutTool::EditPoints: return m_CurveEdit;
    case LayoutTool::Rotate: return m_Transform[1];
    case LayoutTool::Scale: return m_Transform[2];
    default: return m_Transform[0];
    }
}

// A press with a transformation tool (Dev.exe sub_48DE10, sub_494F60,
// sub_496DE0): on an object, it is selected (Ctrl adds it) unless it is
// already, and the selection follows the mouse; on nothing, a selection
// rectangle. A locked selection is moved from anywhere.
bool LayoutWindow::BeginTransform(int x, int y) {
    const auto input = InputState(x, y, true, {}, true);
    if (input.status != EDITOR_OK || !ReadLayoutSelection(Client(), m_SelectionData))
        return false;
    const auto *picked = EditorClient::Find(input, "picked");
    auto objects = m_SelectionData.objects;
    EditorOperation selection = 0;
    if (!m_SelectionData.locked) {
        if (!picked || !picked->object)
            return false;
        if (!m_SelectionData.Contains(picked->object.object)) {
            if (!ImGui::GetIO().KeyCtrl)
                objects.clear();
            objects.push_back(picked->object);
            const auto result = Client().Command("selection.replace", {ObjectsArgument("objects", objects)});
            if (result.status == EDITOR_QUEUED)
                selection = result.operation;
            else if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE)
                return false;
        }
    }
    const auto action = m_Tool == LayoutTool::Rotate  ? TransformAction::Rotate
                        : m_Tool == LayoutTool::Scale ? TransformAction::Scale
                                                      : TransformAction::Translate;
    auto settings = Settings();
    unsigned kind = 1;
    if (EditorClient::Integer(input, "is-2d") && picked &&
        std::find(objects.begin(), objects.end(), picked->object) != objects.end()) {
        if (action == TransformAction::Rotate) return false;
        settings.axes = action == TransformAction::Translate ? (settings.axes == 1   ? 1
                                                                : settings.axes == 2 ? 2
                                                                                     : 3)
                                                             : settings.axes & 3;
        if (!settings.axes)
            return false;
        kind = 2;
    } else if (action == TransformAction::Translate) {
        const int allowed = int(EditorClient::Integer(input, "allowed-axes"));
        settings.axes = (settings.axes & allowed) ? settings.axes & allowed : allowed;
    }
    auto fields = GestureFields(input, kind, unsigned(action), x, y, objects, settings);
    if (fields.empty())
        return false;
    if (picked && picked->object && std::find(objects.begin(), objects.end(), picked->object) != objects.end()) {
        if (kind == 2)
            fields.push_back({"grabbed", EDITOR_OBJECT, picked->object});
        else if (EditorClient::Integer(input, "is-3d"))
            fields.push_back(*EditorClient::Find(input, "point"));
    }
    if (settings.referent) {
        const auto state = Client().Query("object.details", {ObjectArgument("target", settings.referent)});
        if (state.status == EDITOR_OK)
            fields.push_back({"referent", EDITOR_OBJECT, settings.referent});
        else if (state.status == EDITOR_STALE)
            Settings().referent = {};
    }
    return BeginGesture(std::move(fields), kind, unsigned(action), selection);
}

int LayoutWindow::HandleAt(int x, int y, CK_ID *entity) const {
    if (m_Tool == LayoutTool::Translate || m_Tool == LayoutTool::Fly || CurveToolActive()) return 0;
    const auto input = InputState(x, y, true);
    if (input.status != EDITOR_OK)
        return 0;
    if (entity) {
        const auto *sprite = EditorClient::Find(input, "sprite");
        *entity = sprite ? sprite->object.object : 0;
    }
    return int(EditorClient::Integer(input, "handle"));
}

bool LayoutWindow::BeginHandle(int x, int y) {
    const auto input = InputState(x, y, true);
    const int handle = int(EditorClient::Integer(input, "handle"));
    if (input.status != EDITOR_OK || !handle || !ReadLayoutSelection(Client(), m_SelectionData))
        return false;
    const auto action = handle == 1 ? TransformAction::Translate : TransformAction::Scale;
    auto settings = handle == 1 ? m_Transform[0] : m_Transform[2];
    settings.axes = handle == 3 ? 2 : handle == 4 ? 1 : 3;
    auto fields = GestureFields(input, 2, unsigned(action), x, y, m_SelectionData.objects, settings);
    const auto *sprite = EditorClient::Find(input, "sprite");
    if (fields.empty() || !sprite || !sprite->object)
        return false;
    fields.push_back({"grabbed", EDITOR_OBJECT, sprite->object});
    return BeginGesture(std::move(fields), 2, unsigned(action));
}

// N / Shift+N: the next or previous camera of the combo.
void LayoutWindow::NextCamera(bool previous) {
    auto &cameras = m_CameraData;
    if (!ReadLayoutScene(Client(), "cameras", cameras) || cameras.rows.empty()) return;
    size_t index = 0;
    for (size_t i = 0; i < cameras.rows.size(); ++i) if (cameras.rows[i].object == cameras.camera) index = i;
    index = (index + (previous ? cameras.rows.size() - 1 : 1)) % cameras.rows.size();
    Execute("layout.set-camera", {ObjectArgument("target", cameras.rows[index].object)});
}

void LayoutWindow::ZoomOn(bool scene) {
    const auto input = InputState();
    const auto *target = EditorClient::Find(input, "target");
    if (input.status != EDITOR_OK || !target || !target->object || !ReadLayoutSelection(Client(), m_SelectionData))
        return;
    const std::vector<EditorField> fields{{"target", EDITOR_OBJECT, target->object},
                                          {"objects", EDITOR_OBJECT_LIST, {}, {}, 0, m_SelectionData.objects},
                                          {"scene", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(scene)},
                                          GestureOptions(Steps(), Settings(), TransformStepsOf(), Snap2DOf(),
                                                         m_Preferences, m_ScaleMode, m_SelectionData.locked)};
    const auto values = BorrowEditorFields(fields);
    Execute("layout.zoom-on", {values.data(), values.data() + values.size()}, AfterCommand::Zoom);
}

// Dev's 3D Layout keys (Dev.exe sub_4B95C0, WM_KEYUP).
void LayoutWindow::Shortcuts() {
    const ImGuiIO &io = ImGui::GetIO();
    if (io.WantTextInput || (Busy() && !GestureActive(3)))
        return;
    auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };
    // Dev.exe sub_4B95C0 (WM_KEYDOWN): Escape ends the Flycam, playing or not.
    if (m_Tool == LayoutTool::Fly && pressed(ImGuiKey_Escape)) Command(68);
    // Play, Step and Restore IC are Dev's accelerators (EditorApp::GlobalShortcuts).
    if (io.KeyAlt || m_PlayerMode) return;
    if (io.KeyCtrl) {
        // Ctrl+A select all, Ctrl+D clear, Ctrl+I invert; Ctrl+C copies the
        // selection, Ctrl+V pastes copies of it (Dev.exe sub_4B0780 2, 3);
        // Ctrl+G the Reference Guide preferences.
        if (pressed(ImGuiKey_A)) SelectAll(false);
        if (pressed(ImGuiKey_D)) ClearSelection();
        if (pressed(ImGuiKey_I)) SelectAll(true);
        if (pressed(ImGuiKey_C)) {
            LayoutSelectionData selection;
            if (ReadLayoutSelection(Client(), selection)) Execute("layout.copy", {ObjectsArgument("objects", selection.objects)});
        }
        if (pressed(ImGuiKey_V)) {
            const auto clipboard = Client().Query("scene.clipboard");
            if (clipboard.status == EDITOR_OK)
                if (const auto *objects = EditorClient::Find(clipboard, "objects"); objects && objects->type == EDITOR_OBJECT_LIST)
                    Duplicate(objects->objects, EditorClient::Integer(clipboard, "revision"));
        }
        if (pressed(ImGuiKey_G)) {
            auto &editor = m_State.workspace->Application();
            const EditorValue page{EDITOR_TEXT("page"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("reference-guide")};
            editor.Execute("preferences.open", {editor.Scope(), &page, 1});
        }
        return;
    }
    if (!io.KeyShift && pressed(ImGuiKey_Delete)) DeleteSelection();
    const auto camera = InputState();
    const bool perspective = EditorClient::Integer(camera, "perspective") != 0;
    const bool targeted = EditorClient::Integer(camera, "targeted") != 0;
    if (io.KeyShift) {
        if (pressed(ImGuiKey_Z)) ZoomOn(false);
        if (pressed(ImGuiKey_N)) NextCamera(true);
        if (pressed(ImGuiKey_G)) Command(67);
        return;
    }
    const bool transforming = m_Tool == LayoutTool::Translate || m_Tool == LayoutTool::Rotate || m_Tool == LayoutTool::Scale;
    if (transforming) {
        // S snap, H hierarchy, F5..F7 the axes, F8 the next plane, F9 the next pivot.
        if (pressed(ImGuiKey_S)) Command(66);
        if (pressed(ImGuiKey_H)) Command(70);
        if (pressed(ImGuiKey_F5)) Command(12);
        if (pressed(ImGuiKey_F6)) Command(13);
        if (pressed(ImGuiKey_F7)) Command(14);
        if (pressed(ImGuiKey_F8)) Command(Settings().axes == 3 ? 43 : Settings().axes == 6 ? 44 : 42);
        if (pressed(ImGuiKey_F9) && m_Tool != LayoutTool::Translate) Command(72 + (Settings().pivot + 1) % 3);
    }
    if (pressed(ImGuiKey_A)) Command(25);
    if (pressed(ImGuiKey_X)) Command(9);
    if (pressed(ImGuiKey_C)) Command(11);
    // V cycles Normal, Volumetric and Uniform.
    if (pressed(ImGuiKey_V)) Command(46 + (m_Tool == LayoutTool::Scale ? (m_ScaleMode + 1) % 3 : m_ScaleMode));
    // O: the Orbit tool, pressed again its other mode.
    if (pressed(ImGuiKey_O)) Command(m_Tool == LayoutTool::Orbit ? (m_OrbitMode ? 49 : 50) : 49 + m_OrbitMode);
    if (pressed(ImGuiKey_D)) Command(23);
    if (pressed(ImGuiKey_R) && !targeted) Command(24);
    if (pressed(ImGuiKey_F) && perspective) Command(26);
    if (pressed(ImGuiKey_Z)) Command(51);
    if (pressed(ImGuiKey_P)) Command(28);
    if (pressed(ImGuiKey_Y)) Command(68);
    if (pressed(ImGuiKey_W)) ZoomOn(true);
    if (pressed(ImGuiKey_N)) NextCamera(false);
    if (pressed(ImGuiKey_G)) Command(29);
    if (pressed(ImGuiKey_Space)) Command(58);
    // 1 to 9: the creation tools.
    static const int creates[9] = {31, 32, 34, 33, 60, 75, 80, 81, 78};
    for (int i = 0; i < 9; ++i)
        if (pressed(static_cast<ImGuiKey>(ImGuiKey_1 + i)) && m_State.HasDocument()) Command(creates[i]);
}

// ------------------------------------------------------------------ flycam

void LayoutWindow::BeginFly() {
    if (!ReadLayoutSelection(Client(), m_SelectionData))
        return;
    const int x = int(ImGui::GetIO().MousePos.x - m_Origin.x - 76.0f * m_Scale);
    const int y = int(ImGui::GetIO().MousePos.y - m_Origin.y - 21.0f * m_Scale);
    auto fields = GestureFields(InputState(), 3, 0, x, y, m_SelectionData.objects, Settings());
    if (fields.empty() || !BeginGesture(std::move(fields), 3, 0))
        return;
    m_Tool = LayoutTool::Fly;
}

void LayoutWindow::EndFly() {
    if (m_Tool != LayoutTool::Fly) return;
    FinishGesture();
    m_Tool = LayoutTool::Select;
}

void LayoutWindow::FlyFrame(int x, int y) { UpdateGesture(x, y, true, true); }

// -------------------------------------------------------------- curve tools

bool LayoutWindow::CurveToolActive() const {
    return m_Tool == LayoutTool::AddPoints || m_Tool == LayoutTool::InsertPoints || m_Tool == LayoutTool::EditPoints ||
           m_Tool == LayoutTool::DeletePoints;
}

// The Curve Setup chooses the tool; a curve gone ends it.
void LayoutWindow::ClearCurveTool() {
    const auto tool = Client().Query("layout.curve-tool");
    if (tool.status != EDITOR_OK || !EditorClient::Integer(tool, "tool"))
        return;
    Execute("layout.set-curve-tool", {ObjectArgument("target", {}), IntegerArgument("tool", 0),
                                      IntegerArgument("plane", EditorClient::Integer(tool, "plane"))});
}

void LayoutWindow::SyncCurveTool() {
    const auto tool = Client().Query("layout.curve-tool");
    if (tool.status != EDITOR_OK)
        return;
    const auto next = EditorClient::Integer(tool, "tool");
    if (next >= 128 && next <= 131) {
        if (static_cast<int>(m_Tool) != int(next) && !GestureActive(1))
            m_Tool = LayoutTool(next);
    } else if (CurveToolActive() && !GestureActive(1))
        m_Tool = LayoutTool::Select;
}

void LayoutWindow::CurveClick(int x, int y, bool right) {
    const auto tool = Client().Query("layout.curve-tool");
    const auto *curve = EditorClient::Find(tool, "target");
    if (tool.status != EDITOR_OK || !curve || !curve->object)
        return;
    const auto camera = InputState();
    const bool ortho = !EditorClient::Integer(camera, "perspective");
    const auto action = m_Tool == LayoutTool::AddPoints ? 0u : m_Tool == LayoutTool::InsertPoints ? 1u : 2u;
    EditorField position{"position", EDITOR_REAL_LIST}, distance{"plane-distance", EDITOR_REAL_VALUE};
    position.reals = {double(x), double(y)};
    distance.real = m_Preferences.Float(ortho ? "3D Layout\\Actions\\Camera Plane Distance Ortho"
                                              : "3D Layout\\Actions\\Camera Plane Distance");
    const std::vector<EditorField> fields{
        *curve,
        {"action", EDITOR_INTEGER_VALUE, {}, {}, action},
        position,
        distance,
        {"right", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(right)},
        {"plane", EDITOR_INTEGER_VALUE, {}, {}, EditorClient::Integer(tool, "plane")}};
    const auto values = BorrowEditorFields(fields);
    Execute("layout.curve-click", {values.data(), values.data() + values.size()});
}

bool LayoutWindow::BeginCurveEdit(int x, int y) {
    const auto tool = Client().Query("layout.curve-tool");
    const auto *curve = EditorClient::Find(tool, "target");
    if (tool.status != EDITOR_OK || !curve || !curve->object || !ReadLayoutSelection(Client(), m_SelectionData))
        return false;
    const auto input = InputState(x, y, true, curve->object);
    const auto *picked = EditorClient::Find(input, "picked");
    if (input.status != EDITOR_OK || !picked || !picked->object)
        return false;
    auto fields = GestureFields(input, 1, unsigned(TransformAction::Translate), x, y, {picked->object}, m_CurveEdit);
    if (fields.empty())
        return false;
    fields.push_back({"curve", EDITOR_OBJECT, curve->object});
    fields.push_back(*EditorClient::Find(input, "point"));
    m_CurveEditing = BeginGesture(std::move(fields), 1, unsigned(TransformAction::Translate));
    return m_CurveEditing;
}

} // namespace vdev
