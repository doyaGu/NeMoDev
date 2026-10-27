// The 3D Layout's input: the camera tools, selection, transformations and
// 2D handles, the keys, the flycam and the curve tools.
#include "ui/layout/LayoutWindow.h"
#ifdef _WIN32
#include <windows.h>
#endif



#include "imgui_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevTheme.h"
#include "core/level/ObjectEditCommands.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

bool LayoutWindow::BeginCamera(CameraAction action, int x, int y, ImGuiMouseButton button) {
    const ImGuiIO &io = ImGui::GetIO();
    if (!m_Gesture.Begin(m_View, action, Steps(), x, y, m_State.objectSelection.objects, m_State.objectSelection.locked,
                         io.KeyShift, io.KeyCtrl))
        return false;
    m_GestureButton = button;
    return true;
}

// The mouse in the view (Dev.exe sub_4B95C0): the left button drags the
// current camera tool, the middle button pans (Alt: orbits), the wheel
// zooms; the right button cancels a drag.
void LayoutWindow::ViewInput(const ImVec2 &at, bool hovered) {
    const ImGuiIO &io = ImGui::GetIO();
    const int x = static_cast<int>(io.MousePos.x - at.x), y = static_cast<int>(io.MousePos.y - at.y);
    if (m_Tool == LayoutTool::Fly) {
        FlyFrame(x, y);
        return;
    }
    if (m_Gesture.Active()) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_Gesture.Cancel();
        } else if (!ImGui::IsMouseDown(m_GestureButton)) {
            m_Gesture.End(m_State.HasDocument() ? m_State.document.get() : nullptr);
        } else {
            if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) m_Gesture.Move(x, y, io.KeyShift);
            m_Gesture.Idle(io.KeyShift);
        }
        return;
    }
    if (m_Gesture2D.Active()) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) m_Gesture2D.Cancel();
        else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_Gesture2D.End(m_State.HasDocument() ? m_State.document.get() : nullptr);
        else if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
            m_Gesture2D.Move(x, y);
            UpdateCoordinates();
        }
        return;
    }
    if (m_TransformGesture.Active()) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_TransformGesture.Cancel();
            m_CurveEditing = false;
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Editing a curve's points: their move and the curve's update, one undo step.
            std::unique_ptr<Edit> points;
            if (m_CurveEditing && m_State.HasDocument()) points = std::make_unique<Edit>(*m_State.document, "Edit Points");
            const bool moved = m_TransformGesture.End(m_State.HasDocument() ? m_State.document.get() : nullptr);
            CKCurve *curve = CKCurve::Cast(m_State.host.Context()->GetObject(m_State.curveTool.curve));
            if (points && moved && curve) {
                std::vector<CK_ID> ids;
                for (int i = 0; i < curve->GetControlPointCount(); ++i)
                    if (CKCurvePoint *point = curve->GetControlPoint(i)) ids.push_back(point->GetID());
                EditCurvePoints(*m_State.document, curve->GetID(), ids, 0, "Edit Points");
            }
            if (points) points->Commit();
            m_CurveEditing = false;
        } else if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
            m_TransformGesture.Move(x, y);
            UpdateCoordinates();
        }
        return;
    }
    if (m_Selecting) {
#ifdef _WIN32
        const float drag = std::max(1.0f, static_cast<float>(GetSystemMetrics(SM_CXDRAG)));
#else
        const float drag = std::max(1.0f, io.MouseDragThreshold);
#endif
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_Selecting = m_Rectangle = false; // Dev restores the selection
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (m_Rectangle)
                ApplyRect(VxRect(m_PressAt.x, m_PressAt.y, static_cast<float>(x), static_cast<float>(y)));
            else
                ApplyClick(m_View.Pick(static_cast<int>(m_PressAt.x), static_cast<int>(m_PressAt.y), io.KeyShift));
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
            const LayoutPickInfo info = m_View.PickInfo(x, y);
            if (CKObject *picked = m_State.host.Context()->GetObject(info.entity3d)) {
                Settings().referent = info.entity3d;
                m_State.Log(LogLevel::Info, "Referent: " + DevObjectName(picked));
            }
        }
        return;
    }
    // The select cursor over an entity the Selection tool would pick.
    if (m_Tool == LayoutTool::Select && !m_PlayerMode && !ImGui::IsAnyMouseDown()) {
        if (m_HoverAt.x != x || m_HoverAt.y != y) {
            m_HoverAt = ImVec2(x, y);
            const LayoutPickInfo info = m_View.PickInfo(static_cast<int>(x), static_cast<int>(y));
            m_HoverEntity = info.entity3d != 0 || info.character != 0 || info.entity2d != 0;
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
        LayoutSendClick(m_State.host.Context(), m_View.PickInfo(x, y), true);
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
        if (m_PlayerMode) LayoutSendClick(m_State.host.Context(), m_View.PickInfo(x, y), false);
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && !m_PlayerMode) {
        // Dev.exe sub_4C4E30: Alt orbits (in the Orbit tool's mode), else it pans.
        if (io.KeyAlt)
            BeginCamera(m_OrbitMode ? CameraAction::OrbitCamera : CameraAction::Orbit, x, y, ImGuiMouseButton_Middle);
        else
            BeginCamera(CameraAction::Pan, x, y, ImGuiMouseButton_Middle);
    }
    if (io.MouseWheel != 0.0f && !m_PlayerMode) {
        const int notches = static_cast<int>(io.MouseWheel > 0.0f ? std::ceil(io.MouseWheel) : std::floor(io.MouseWheel));
        WheelZoom(m_View, Steps(), notches, m_ZoomExtent);
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
        CKContext *context = m_State.host.Context();
        const XObjectPointerArray &curves = context->GetObjectListByType(CKCID_CURVE, TRUE);
        if (curves.Size() > 0 && curves[0]) {
            m_State.curveTool.curve = curves[0]->GetID();
            m_State.curveTool.tool = curveTool->second;
            SyncCurveTool();
        } else {
            m_State.Log(LogLevel::Warning, "--layout-tool: no curve");
        }
    } else if (!tool.empty()) {
        m_State.Log(LogLevel::Warning, "--layout-tool: unknown tool " + tool);
    }
    if (camera.empty()) return;
    CKContext *context = m_State.host.Context();
    for (CK_ID id : m_View.Cameras()) {
        CKObject *object = context->GetObject(id);
        if (object && object->GetName() && camera == object->GetName()) {
            m_View.SetCamera(id);
            return;
        }
    }
    m_State.Log(LogLevel::Warning, "--layout-view: no camera named " + AnsiToUtf8(camera));
}

void LayoutWindow::StartupSelection(const std::vector<std::string> &names, const std::string &group,
                                    const std::vector<int> &commands) {
    CKContext *context = m_State.host.Context();
    std::vector<CK_ID> objects;
    for (const std::string &name : names) {
        CKObject *object = context ? context->GetObjectByNameAndParentClass(const_cast<CKSTRING>(name.c_str()), CKCID_RENDEROBJECT, nullptr) : nullptr;
        if (object) objects.push_back(object->GetID());
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

void LayoutWindow::SetSelection(std::vector<CK_ID> objects) {
    if (m_State.objectSelection.locked || objects == m_State.objectSelection.objects) return;
    m_State.objectSelection.Set(std::move(objects));
}

// Undone creations and deleted objects leave the selection.
void LayoutWindow::ValidateSelection() {
    const std::vector<CK_ID> alive = AliveSelection();
    if (alive.size() != m_State.objectSelection.objects.size()) m_State.objectSelection.Set(alive);
}

// The selected objects still in the composition (not in the graveyard).
std::vector<CK_ID> LayoutWindow::AliveSelection() const {
    std::vector<CK_ID> alive;
    CKContext *context = m_State.host.Context();
    for (CK_ID id : m_State.objectSelection.objects)
        if (context && context->GetObject(id) && (!m_State.HasDocument() || !m_State.document->IsDiscarded(id)))
            alive.push_back(id);
    return alive;
}

void LayoutWindow::ClearSelection() {
    SetSelection({});
}

// Ctrl+A / Ctrl+I: the 3D and 2D entities of the current scene, body parts
// and private objects left out.
void LayoutWindow::SelectAll(bool invert) {
    CKContext *context = m_State.host.Context();
    CKScene *scene = m_View.Scene();
    if (!scene || m_State.objectSelection.locked) return;
    std::vector<CK_ID> objects;
    for (CK_CLASSID cid : {CKCID_3DENTITY, CKCID_2DENTITY}) {
        CKObjectArray *array = CreateCKObjectArray();
        scene->ComputeObjectList(array, cid, TRUE);
        for (array->Reset(); !array->EndOfList(); array->Next()) {
            CKObject *object = array->GetData(context);
            if (!object || CKBodyPart::Cast(object) || object->IsPrivate() || m_View.IsInterfaceObject(object->GetID()))
                continue;
            if (invert && m_State.objectSelection.Contains(object->GetID())) continue;
            objects.push_back(object->GetID());
        }
        DeleteCKObjectArray(array);
    }
    SetSelection(objects);
}

// Dev.exe sub_499870: a click on a selected object keeps only it (or, alone
// or with Ctrl, deselects it); on another object selects it (Ctrl adds it);
// on nothing clears the selection (unless Ctrl).
void LayoutWindow::ApplyClick(CK_ID picked) {
    if (m_State.objectSelection.locked) return;
    const bool ctrl = ImGui::GetIO().KeyCtrl;
    std::vector<CK_ID> objects = m_State.objectSelection.objects;
    if (!picked) {
        if (!ctrl) objects.clear();
    } else if (std::find(objects.begin(), objects.end(), picked) != objects.end()) {
        if (objects.size() <= 1 || ctrl) objects.erase(std::remove(objects.begin(), objects.end(), picked), objects.end());
        else objects.assign(1, picked);
    } else {
        if (!ctrl) objects.clear();
        objects.push_back(picked);
    }
    SetSelection(objects);
}

// A rectangle replaces the selection (Ctrl adds to it).
void LayoutWindow::ApplyRect(const VxRect &rect) {
    if (m_State.objectSelection.locked) return;
    std::vector<CK_ID> objects;
    if (ImGui::GetIO().KeyCtrl) objects = m_State.objectSelection.objects;
    for (CK_ID id : m_View.RectPick(rect, m_State.preferences.Bool("3D Layout\\Strict Selection")))
        if (std::find(objects.begin(), objects.end(), id) == objects.end()) objects.push_back(id);
    SetSelection(objects);
}

// Documentation "Axes Constraint": "when the current camera is set to the
// Top View, only X and Z axis are available for Select and Translate" (the
// Front view X and Y, the Right view Y and Z).
int LayoutWindow::AllowedAxes() const {
    CKCamera *camera = m_View.Camera();
    if (!camera || (m_Tool != LayoutTool::Translate && m_Tool != LayoutTool::Select)) return 7;
    const CK_ID id = camera->GetID();
    if (id == m_View.ViewCamera(LayoutViewCamera::Top)) return 5;
    if (id == m_View.ViewCamera(LayoutViewCamera::Front)) return 3;
    if (id == m_View.ViewCamera(LayoutViewCamera::Right)) return 6;
    return 7;
}

TransformSettings &LayoutWindow::Settings() {
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
    const ImGuiIO &io = ImGui::GetIO();
    VxVector point;
    const CK_ID picked = m_View.Pick(x, y, io.KeyShift, &point);
    const bool locked = m_State.objectSelection.locked;
    if (!locked) {
        if (!picked) return false;
        if (!m_State.objectSelection.Contains(picked)) ApplyClick(picked);
    }
    const TransformAction action = m_Tool == LayoutTool::Rotate  ? TransformAction::Rotate
                                   : m_Tool == LayoutTool::Scale ? TransformAction::Scale
                                                                 : TransformAction::Translate;
    CKObject *object = m_State.host.Context()->GetObject(picked);
    // A 2D entity grabbed: the 2D entities move or scale (Dev.exe sub_491060,
    // sub_496D60); Translate holds them on a single X or Y axis, Scale takes
    // the X and Y axes constrained.
    if (CK2dEntity::Cast(object) && m_State.objectSelection.Contains(picked)) {
        if (action == TransformAction::Rotate) return false;
        const TransformSettings &settings = Settings();
        const int axes = action == TransformAction::Translate ? (settings.axes == 1 ? 1 : settings.axes == 2 ? 2 : 3)
                                                              : (settings.axes & 3);
        if (!axes) return false;
        return m_Gesture2D.Begin(m_View, action, axes, settings.snap, Snap2DOf(), TransformStepsOf().snapScale,
                                 m_State.objectSelection.objects, picked, x, y);
    }
    const bool grabbed = picked && m_State.objectSelection.Contains(picked) && CK3dEntity::Cast(object);
    // Translate in a Top, Front or Right view: the axes of its plane only.
    TransformSettings settings = Settings();
    if (action == TransformAction::Translate) {
        const int allowed = AllowedAxes();
        settings.axes = (settings.axes & allowed) ? (settings.axes & allowed) : allowed;
    }
    return m_TransformGesture.Begin(m_View, action, settings, TransformStepsOf(), m_ScaleMode,
                                    m_State.objectSelection.objects, x, y, grabbed ? &point : nullptr);
}

// Dev.exe sub_4B5C60: the 2D entity under the mouse when it is selected and
// shown; no handles with Translate (Dev's sub_4BDF40 and sub_4C4A70 skip
// them then), the curve tools or the flight.
int LayoutWindow::HandleAt(int x, int y, CK_ID *entity) const {
    if (m_Tool == LayoutTool::Translate || m_Tool == LayoutTool::Fly || CurveToolActive()) return 0;
    const LayoutPickInfo info = m_View.PickInfo(x, y);
    CK2dEntity *sprite = CK2dEntity::Cast(m_State.host.Context()->GetObject(info.entity2d));
    if (!sprite || !m_State.objectSelection.Contains(sprite->GetID())) return 0;
    if (entity) *entity = sprite->GetID();
    return Handle2DAt(sprite, static_cast<float>(x), static_cast<float>(y));
}

// Dev.exe sub_4C4A70: the top-left handle Translates on X and Y (in Sticky
// Mode), the others Scale on both axes, Y or X; the tools' own Snap.
bool LayoutWindow::BeginHandle(int x, int y) {
    CK_ID entity = 0;
    const int handle = HandleAt(x, y, &entity);
    if (!handle) return false;
    const TransformAction action = handle == 1 ? TransformAction::Translate : TransformAction::Scale;
    const int axes = handle == 3 ? 2 : handle == 4 ? 1 : 3;
    const TransformSettings &settings = handle == 1 ? m_Transform[0] : m_Transform[2];
    return m_Gesture2D.Begin(m_View, action, axes, settings.snap, Snap2DOf(), TransformStepsOf().snapScale,
                             m_State.objectSelection.objects, entity, x, y);
}

// N / Shift+N: the next or previous camera of the combo.
void LayoutWindow::NextCamera(bool previous) {
    const std::vector<CK_ID> cameras = m_View.Cameras();
    if (cameras.empty()) return;
    CKCamera *current = m_View.Camera();
    const int count = static_cast<int>(cameras.size());
    int index = 0;
    for (int i = 0; i < count; ++i)
        if (current && cameras[i] == current->GetID()) index = i;
    m_View.SetCamera(cameras[(index + (previous ? count - 1 : 1)) % count]);
}

void LayoutWindow::ZoomOn(bool scene) {
    if (!ZoomOnObjects(m_View, m_State.objectSelection.objects, scene, &m_ZoomExtent) && !scene)
        m_State.Log(LogLevel::Info, "Nothing selected");
}

// Dev's 3D Layout keys (Dev.exe sub_4B95C0, WM_KEYUP).
void LayoutWindow::Shortcuts() {
    const ImGuiIO &io = ImGui::GetIO();
    if (io.WantTextInput || m_Gesture.Active() || m_TransformGesture.Active() || m_Gesture2D.Active() || m_Selecting) return;
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
            m_State.objectsCopied = true;
            std::vector<CK_ID> copied;
            for (CK_ID id : AliveSelection())
                if (!CKCurvePoint::Cast(m_State.host.Context()->GetObject(id))) copied.push_back(id);
            m_State.SetObjectClipboard(*m_State.document, std::move(copied));
        }
        if (pressed(ImGuiKey_V) && m_State.HasDocument()) {
            std::vector<CK_ID> alive;
            for (CK_ID id : m_State.objectClipboard)
                if (m_State.host.Context()->GetObject(id)) alive.push_back(id);
            if (!alive.empty()) Duplicate(alive);
        }
        if (pressed(ImGuiKey_G)) m_State.events.Post(ShowGeneralPreferences{3});
        return;
    }
    if (!io.KeyShift && pressed(ImGuiKey_Delete)) DeleteSelection();
    CKCamera *camera = m_View.Camera();
    const bool perspective = camera && camera->GetProjectionType() == CK_PERSPECTIVEPROJECTION;
    const bool targeted = CKTargetCamera::Cast(camera) && CKTargetCamera::Cast(camera)->GetTarget();
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
    CKCamera *camera = m_View.Camera();
    if (!camera) return;
    std::vector<CK_ID> moved{camera->GetID()};
    if (CKTargetCamera *targeted = CKTargetCamera::Cast(camera))
        if (CK3dEntity *target = targeted->GetTarget()) moved.push_back(target->GetID());
    if (!m_FlyUndo.Begin(m_State.host.Context(), moved)) return;
    m_Tool = LayoutTool::Fly;
    m_State.curveTool.tool = 0;
    m_Doc.flyCamera = camera->GetID();
    // Dev measures from where the cursor is when the flight begins.
    m_FlyStart = m_FlyLast = ImVec2(ImGui::GetIO().MousePos.x - m_Origin.x - 76.0f * m_Scale,
                                    ImGui::GetIO().MousePos.y - m_Origin.y - 21.0f * m_Scale);
    m_FlyMode = FlyMode::Advance;
    m_FlyLeft = m_FlyRight = false;
    m_FlyTime = ImGui::GetTime();
}

void LayoutWindow::EndFly() {
    if (m_Tool != LayoutTool::Fly) return;
    m_Tool = LayoutTool::Select;
    if (m_State.HasDocument())
        m_FlyUndo.Commit(*m_State.document, "Fly Camera", !m_View.IsViewCamera(m_Doc.flyCamera));
    else
        m_FlyUndo.Begin(nullptr, {});
}

// Dev.exe sub_48C2C0: a button pressed or released chooses the mode (both,
// with an object under the mouse and no Ctrl: Orbit around it) and the
// offsets count from there.
void LayoutWindow::FlyFrame(int x, int y) {
    const ImGuiIO &io = ImGui::GetIO();
    CKCamera *camera = m_View.Camera();
    if (!camera || camera->GetID() != m_Doc.flyCamera) {
        EndFly();
        return;
    }
    const bool left = ImGui::IsMouseDown(ImGuiMouseButton_Left), right = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (left != m_FlyLeft || right != m_FlyRight) {
        const bool leftPressed = left && !m_FlyLeft;
        m_FlyLeft = left;
        m_FlyRight = right;
        FlyMode mode = left && right ? (leftPressed ? FlyMode::Look : FlyMode::Strafe)
                       : left        ? FlyMode::Look
                       : right       ? FlyMode::Strafe
                                     : FlyMode::Advance;
        if (left && right && !io.KeyCtrl) {
            VxVector point;
            CK3dEntity *entity = CK3dEntity::Cast(m_State.host.Context()->GetObject(m_View.Pick(x, y, false, &point)));
            VxBbox box;
            if (entity && entity->GetCurrentMesh()) {
                m_FlyCenter = point;
                mode = FlyMode::Orbit;
            } else if (entity && LayoutBox(entity, box)) {
                m_FlyCenter = box.GetCenter();
                mode = FlyMode::Orbit;
            }
        }
        m_FlyMode = mode;
        m_FlyStart = m_FlyLast = ImVec2(static_cast<float>(x), static_cast<float>(y));
        m_FlyTime = ImGui::GetTime();
        return;
    }
    const Preferences &p = m_State.preferences;
    FlySteps steps;
    steps.turn = p.Float("3D Layout\\Actions\\Camera Fly Turn Step");
    steps.advance = p.Float("3D Layout\\Actions\\Camera Fly Advance Step");
    steps.constant = p.Bool("3D Layout\\Actions\\Camera Navigate Constant Speed Mode");
    steps.worldMin = Steps().worldMin;
    steps.worldMax = Steps().worldMax;
    const ImVec2 from = steps.constant ? m_FlyStart : m_FlyLast;
    m_FlyLast = ImVec2(static_cast<float>(x), static_cast<float>(y));
    const double now = ImGui::GetTime();
    const float seconds = static_cast<float>(now - m_FlyTime);
    m_FlyTime = now;
    const float nx = (from.x - x) / static_cast<float>(std::max(1, m_View.Width()));
    const float ny = (from.y - y) / static_cast<float>(std::max(1, m_View.Height()));
    Fly(m_View, m_FlyMode, nx, ny, seconds, steps, io.KeyShift, m_FlyCenter);
}

// -------------------------------------------------------------- curve tools

bool LayoutWindow::CurveToolActive() const {
    return m_Tool == LayoutTool::AddPoints || m_Tool == LayoutTool::InsertPoints || m_Tool == LayoutTool::EditPoints ||
           m_Tool == LayoutTool::DeletePoints;
}

// The Curve Setup chooses the tool; a curve gone ends it.
void LayoutWindow::SyncCurveTool() {
    CurveTool &tool = m_State.curveTool;
    CKContext *context = m_State.host.Context();
    const bool alive = m_State.HasDocument() && context && CKCurve::Cast(context->GetObject(tool.curve)) &&
                       !m_State.document->IsDiscarded(tool.curve);
    if (tool.tool && !alive) tool.tool = 0;
    if (tool.tool >= 128 && tool.tool <= 131) {
        if (static_cast<int>(m_Tool) != tool.tool && !m_TransformGesture.Active()) m_Tool = static_cast<LayoutTool>(tool.tool);
    } else if (CurveToolActive() && !m_TransformGesture.Active()) {
        m_Tool = LayoutTool::Select;
    }
}

// Add Points (Dev.exe sub_49ACC0): a click on the first point closes an open
// curve and ends the tool; elsewhere a point where the ray meets the
// referential's plane, appended (left button) or first (right button:
// CKCurve::InsertControlPoint before the first point). Insert Points
// (sub_49CD00): a point on the curve, before the first control point
// farther along it. Delete Points (sub_49B400): the point clicked; the
// tool ends with the last.
void LayoutWindow::CurveClick(int x, int y, bool right) {
    CKContext *context = m_State.host.Context();
    CKCurve *curve = CKCurve::Cast(context ? context->GetObject(m_State.curveTool.curve) : nullptr);
    if (!curve || !m_State.HasDocument()) return;
    const CK_ID curveId = curve->GetID();
    const int count = curve->GetControlPointCount();
    bool linear = count > 0;
    std::vector<CK_ID> points;
    for (int i = 0; i < count; ++i)
        if (CKCurvePoint *point = curve->GetControlPoint(i)) {
            points.push_back(point->GetID());
            if (!point->IsLinear()) linear = false;
        }
    auto report = [this](const EditResult &result, const char *what) {
        if (!result.ok && !result.message.empty()) m_State.Log(LogLevel::Warning, std::string(what) + ": " + AnsiToUtf8(result.message));
    };
    // The curve's points, not the curve, are picked (its own Pickable, saved with it, kept).
    auto pickPoint = [&]() {
        const CKBOOL pickable = curve->IsPickable();
        curve->SetPickable(FALSE);
        const CK_ID picked = m_View.Pick(x, y);
        curve->SetPickable(pickable);
        return picked;
    };
    switch (m_Tool) {
    case LayoutTool::AddPoints: {
        if (count > 1 && curve->IsOpen() && !points.empty() && pickPoint() == points.front()) {
            report(EditObjectsState(*m_State.document, {curveId}, "Close Curve",
                                    [](CKObject *o) {
                                        CKCurve::Cast(o)->Close();
                                        CKCurve::Cast(o)->Update();
                                    }),
                   "Close Curve");
            m_State.curveTool.tool = 0;
            return;
        }
        VxVector point, normal, hit;
        if (m_State.curveTool.referential == 0) {
            const VxMatrix guide = m_View.GuideMatrix();
            point = VxVector(guide[3][0], guide[3][1], guide[3][2]);
            normal = VxVector(guide[1][0], guide[1][1], guide[1][2]);
        } else {
            // View Plane: Camera Plane Distance in front of the camera.
            CKCamera *camera = m_View.Camera();
            if (!camera) return;
            const bool ortho = camera->GetProjectionType() != CK_PERSPECTIVEPROJECTION;
            const float distance = m_State.preferences.Float(ortho ? "3D Layout\\Actions\\Camera Plane Distance Ortho"
                                                                   : "3D Layout\\Actions\\Camera Plane Distance");
            VxVector up;
            camera->GetOrientation(&normal, &up);
            camera->GetPosition(&point);
            point += normal * distance;
        }
        if (!m_View.RayOnPlane(x, y, point, normal, hit)) return;
        report(AddCurvePointAt(*m_State.document, curveId, right ? 0 : count, hit, linear, "Add Points"),
               "Add Points");
        break;
    }
    case LayoutTool::InsertPoints: {
        VxVector hit;
        float along = 0.0f;
        if (!m_View.CurveAt(x, y, curveId, hit, &along)) return;
        const float distance = along * curve->GetLength();
        int index = 0;
        while (index < count && curve->GetControlPoint(index) && curve->GetControlPoint(index)->GetLength() <= distance)
            ++index;
        report(AddCurvePointAt(*m_State.document, curveId, index, hit, linear, "Insert Points"),
               "Insert Points");
        break;
    }
    case LayoutTool::DeletePoints: {
        const CK_ID picked = pickPoint();
        auto found = std::find(points.begin(), points.end(), picked);
        if (!picked || found == points.end()) return;
        points.erase(found);
        report(EditCurvePoints(*m_State.document, curveId, points, picked, "Delete Points"), "Delete Points");
        if (points.empty()) m_State.curveTool.tool = 0;
        break;
    }
    default: break;
    }
}

// Edit Points (Dev.exe sub_49B870): a control point of the curve pressed is
// dragged as Translate drags objects, with the tool's own constraints.
bool LayoutWindow::BeginCurveEdit(int x, int y) {
    CKContext *context = m_State.host.Context();
    CKCurve *curve = CKCurve::Cast(context ? context->GetObject(m_State.curveTool.curve) : nullptr);
    if (!curve || !m_State.HasDocument()) return false;
    VxVector grabbed;
    const CKBOOL pickable = curve->IsPickable();
    curve->SetPickable(FALSE);
    const CK_ID picked = m_View.Pick(x, y, false, &grabbed);
    curve->SetPickable(pickable);
    bool point = false;
    for (int i = 0; i < curve->GetControlPointCount(); ++i)
        if (curve->GetControlPoint(i) && curve->GetControlPoint(i)->GetID() == picked) point = true;
    if (!point) return false;
    m_CurveEditing = m_TransformGesture.Begin(m_View, TransformAction::Translate, m_CurveEdit, TransformStepsOf(), 0,
                                              {picked}, x, y, &grabbed);
    return m_CurveEditing;
}

} // namespace vdev
