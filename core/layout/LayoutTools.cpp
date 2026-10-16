#include "core/layout/LayoutTools.h"

#include <algorithm>
#include <cmath>

#include "core/runtime/CkRuntimeHost.h"
#include "core/layout/GestureMath.h"
#include "core/layout/LayoutView.h"
#include "core/document/Document.h"
#include "core/document/UndoHistory.h"

namespace vdev {

namespace {

const VxVector AXIS_X(1.0f, 0.0f, 0.0f), AXIS_Y(0.0f, 1.0f, 0.0f);

VxVector PositionOf(CK3dEntity *entity) {
    VxVector position(0.0f, 0.0f, 0.0f);
    if (entity) entity->GetPosition(&position);
    return position;
}

CK3dEntity *TargetOf(CKCamera *camera) {
    CKTargetCamera *targeted = CKTargetCamera::Cast(camera);
    return targeted ? targeted->GetTarget() : nullptr;
}

// Orbit and Roll leave Dev's Top, Front and Right views for the
// Orthographic View, which takes their position, zoom and orientation
// (Dev.exe sub_493B80, sub_4949D0).
CKCamera *FreeView(LayoutView &view, CKCamera *camera) {
    if (camera->GetProjectionType() != CK_ORTHOGRAPHICPROJECTION || !view.IsViewCamera(camera->GetID())) return camera;
    CKCamera *ortho = CKCamera::Cast(camera->GetCKContext()->GetObject(view.ViewCamera(LayoutViewCamera::Orthographic)));
    if (!ortho || ortho == camera) return camera;
    VxVector position, dir, up, right;
    camera->GetPosition(&position);
    camera->GetOrientation(&dir, &up, &right);
    ortho->SetPosition(&position);
    ortho->SetOrthographicZoom(camera->GetOrthographicZoom());
    ortho->SetOrientation(&dir, &up, &right);
    view.SetCamera(ortho->GetID());
    return ortho;
}

} // namespace

const char *CameraActionText(CameraAction action, bool active, bool targeted, bool aroundSelection) {
    switch (action) {
    case CameraAction::Dolly: return active ? "Moving camera (Z Axis)" : "Camera Dolly";
    case CameraAction::Zoom: return active ? "Zooming camera" : "Zoom camera";
    case CameraAction::Pan: return active ? "Moving camera (XY Plane)" : "Pan camera";
    case CameraAction::Orbit:
        if (aroundSelection) return active ? "Orbiting around selection" : "Orbit around selection";
        return targeted ? (active ? "Orbiting around target" : "Orbit around target")
                        : (active ? "Orbiting viewpoint" : "Orbit viewpoint");
    case CameraAction::OrbitCamera: return active ? "Orbiting viewpoint" : "Orbit viewpoint";
    case CameraAction::Roll: return active ? "Rolling camera" : "Roll camera";
    case CameraAction::Fov: return active ? "Changing the Field Of View" : "Change the Field Of View";
    }
    return "";
}

bool LayoutBox(CK3dEntity *entity, VxBbox &box) {
    if (!entity) return false;
    if (CKCharacter::Cast(entity)) {
        box = entity->GetHierarchicalBox(FALSE);
        return box.IsValid() != FALSE;
    }
    if (entity->GetCurrentMesh()) {
        box = entity->GetBoundingBox(FALSE);
        if (box.IsValid()) return true;
    }
    const VxVector position = PositionOf(entity);
    box.SetCorners(position, position);
    return true;
}

bool CameraGesture::Inside(const VxVector &p) const {
    const VxVector &min = m_Steps.worldMin, &max = m_Steps.worldMax;
    return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y && p.z >= min.z && p.z < max.z;
}

float CameraGesture::Seconds() {
    if (!m_Milliseconds) {
        const float seconds = m_Clock.Current() * 0.001f;
        m_Clock.Reset();
        return seconds;
    }
    const float now = m_Milliseconds();
    const float seconds = (now - m_ClockStart) * 0.001f;
    m_ClockStart = now;
    return seconds;
}

void CameraGesture::ResetClock() {
    if (m_Milliseconds) m_ClockStart = m_Milliseconds();
    else m_Clock.Reset();
}

bool CameraGesture::ConstantSpeed() const {
    switch (m_Action) {
    case CameraAction::Dolly:
    case CameraAction::Zoom: {
        CKCamera *camera = m_View ? m_View->Camera() : nullptr;
        const bool ortho = camera && camera->GetProjectionType() == CK_ORTHOGRAPHICPROJECTION;
        return ortho ? m_Steps.orthoZoomConstant : m_Steps.zoomConstant;
    }
    case CameraAction::Pan: return m_Steps.translateConstant;
    case CameraAction::Orbit:
    case CameraAction::OrbitCamera: return m_Steps.rotateConstant;
    case CameraAction::Roll: return m_Steps.rollConstant;
    case CameraAction::Fov: return m_Steps.fovConstant;
    }
    return false;
}

bool CameraGesture::Begin(LayoutView &view, CameraAction action, const CameraSteps &steps, int x, int y,
                          const std::vector<CK_ID> &selection, bool locked, bool shift, bool ctrl) {
    m_Active = false;
    CKCamera *camera = view.Camera();
    if (!camera) return false;
    if (action == CameraAction::Orbit || action == CameraAction::OrbitCamera || action == CameraAction::Roll)
        camera = FreeView(view, camera);
    CKContext *context = camera->GetCKContext();
    CK3dEntity *target = TargetOf(camera);
    m_View = &view;
    m_Action = action;
    m_Steps = steps;
    m_Camera = camera->GetID();
    m_Target = target ? target->GetID() : 0;
    m_ViewCamera = view.IsViewCamera(m_Camera);
    m_LastX = x;
    m_LastY = y;
    m_DeltaX = m_DeltaY = 0;
    m_AroundSelection = false;
    m_Undo.Begin(context, {m_Camera, m_Target});
    const bool perspective = camera->GetProjectionType() == CK_PERSPECTIVEPROJECTION;
    switch (action) {
    case CameraAction::Dolly:
    case CameraAction::Zoom:
        m_OrthoZoom = camera->GetOrthographicZoom();
        break;
    case CameraAction::Orbit:
    case CameraAction::OrbitCamera: {
        // The pivot: the target, else a point in front of a perspective
        // camera (Free Camera Orbit), else the origin.
        m_Pivot = VxVector(0.0f, 0.0f, 0.0f);
        if (target) {
            m_Pivot = PositionOf(target);
        } else if (perspective) {
            float distance = steps.freeOrbitValue;
            if (steps.freeOrbitType == 0)
                distance = camera->GetFrontPlane() +
                           (camera->GetBackPlane() - camera->GetFrontPlane()) * steps.freeOrbitValue * 0.01f;
            else if (steps.freeOrbitType == 1)
                distance = camera->GetFrontPlane() + steps.freeOrbitValue;
            const VxVector ahead(0.0f, 0.0f, distance);
            camera->Transform(&m_Pivot, &ahead);
        }
        // Around the selection (sub_493510): the 3D entities selected besides
        // the camera and its target, unless Shift or Ctrl is held.
        if (action == CameraAction::Orbit && steps.orbitSelectionType != 2 && !shift && !ctrl &&
            (steps.orbitSelectionType == 0 || locked)) {
            VxBbox all;
            bool found = false;
            for (CK_ID id : selection) {
                CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(id));
                VxBbox box;
                if (!entity || id == m_Camera || id == m_Target || !LayoutBox(entity, box)) continue;
                if (!found) all = box;
                all.Max = Maximize(all.Max, box.Max);
                all.Min = Minimize(all.Min, box.Min);
                found = true;
            }
            if (found) {
                m_Pivot = all.GetCenter();
                m_AroundSelection = true;
            }
        }
        m_CameraMatrix = camera->GetWorldMatrix();
        m_CameraOffset = PositionOf(camera) - m_Pivot;
        if (target) {
            m_TargetMatrix = target->GetWorldMatrix();
            m_TargetOffset = PositionOf(target) - m_Pivot;
            m_TargetFromCamera = PositionOf(target) - PositionOf(camera);
        }
        m_Pitch = m_Yaw = 0.0f;
        break;
    }
    case CameraAction::Roll:
        camera->GetOrientation(&m_Dir, &m_Up, &m_Right);
        break;
    case CameraAction::Fov:
        m_Fov = camera->GetFov();
        break;
    case CameraAction::Pan:
        break;
    }
    ResetClock();
    m_Active = true;
    return true;
}

void CameraGesture::Move(int x, int y, bool shift) {
    if (!m_Active) return;
    const bool constant = ConstantSpeed();
    // Dev.exe sub_49A1D0: constant speed measures from where the drag began.
    m_DeltaX = m_LastX - x;
    m_DeltaY = m_LastY - y;
    if (!constant) {
        m_LastX = x;
        m_LastY = y;
        Process(shift);
    }
}

void CameraGesture::Idle(bool shift) {
    if (!m_Active) return;
    if (ConstantSpeed()) Process(shift);
    else ResetClock();
}

void CameraGesture::Process(bool shift) {
    CKContext *context = m_View->Context();
    CKCamera *camera = context ? CKCamera::Cast(context->GetObject(m_Camera)) : nullptr;
    CK3dEntity *target = context ? CK3dEntity::Cast(context->GetObject(m_Target)) : nullptr;
    const float seconds = Seconds();
    if (!camera || (m_DeltaX == 0 && m_DeltaY == 0)) return;
    const float width = static_cast<float>(std::max(1, m_View->Width())),
                height = static_cast<float>(std::max(1, m_View->Height()));
    const float dx = m_DeltaX / width, dy = m_DeltaY / height;
    const bool perspective = camera->GetProjectionType() == CK_PERSPECTIVEPROJECTION;
    switch (m_Action) {
    case CameraAction::Dolly:
    case CameraAction::Zoom: {
        // Dev.exe sub_48BA60.
        if (perspective) {
            const float offset = Speed(m_Steps.zoom, m_Steps.zoomConstant, seconds) * dy;
            const VxVector local(0.0f, 0.0f, offset);
            VxVector world;
            camera->TransformVector(&world, &local);
            const VxVector before = PositionOf(camera), after = before + world;
            if (!Inside(after)) break;
            if (m_Action == CameraAction::Dolly && target) {
                const VxVector moved = PositionOf(target) + world;
                if (Inside(moved)) target->SetPosition(&moved);
            }
            camera->SetPosition(&after);
            if (m_Action == CameraAction::Zoom && target) {
                // Zoom stops at the target.
                VxVector local, at = PositionOf(target);
                camera->InverseTransform(&local, &at);
                if (local.z < 0.0f) camera->SetPosition(&before);
            }
        } else {
            m_OrthoZoom = NextOrthoZoom(m_OrthoZoom, Speed(m_Steps.orthoZoom, m_Steps.orthoZoomConstant, seconds), dy);
            camera->SetOrthographicZoom(m_OrthoZoom);
        }
        break;
    }
    case CameraAction::Pan: {
        // Dev.exe sub_4942A0: Shift moves a perspective camera faster the
        // farther it is from the origin.
        const VxVector local = PanMove(dx, dy, Speed(m_Steps.translate, m_Steps.translateConstant, seconds),
                                       perspective && shift, Magnitude(PositionOf(camera)));
        VxVector world;
        camera->TransformVector(&world, &local);
        const VxVector after = PositionOf(camera) + world;
        if (!Inside(after)) break;
        camera->SetPosition(&after);
        if (target) {
            const VxVector moved = PositionOf(target) + world;
            target->SetPosition(&moved);
        }
        break;
    }
    case CameraAction::Orbit:
    case CameraAction::OrbitCamera: {
        // Dev.exe sub_4936C0: pitch around the camera's X axis, yaw around
        // the world's Y axis, from the matrices when the drag began.
        const float step = Speed(m_Steps.rotate, m_Steps.rotateConstant, seconds);
        m_Pitch += dy * step;
        m_Yaw += dx * step;
        camera->SetWorldMatrix(m_CameraMatrix);
        VxVector xAxis;
        Vx3DRotateVector(&xAxis, m_CameraMatrix, &AXIS_X);
        if (m_Action == CameraAction::Orbit) {
            const VxVector position = Orbited(m_Pivot, xAxis, m_Pitch, m_Yaw, m_CameraOffset);
            camera->SetPosition(&position);
            if (target) {
                const VxVector moved = Orbited(m_Pivot, xAxis, m_Pitch, m_Yaw, m_TargetOffset);
                target->SetPosition(&moved);
            }
            camera->Rotate(&AXIS_Y, m_Yaw);
            camera->Rotate(&AXIS_X, m_Pitch, camera);
        } else if (target) {
            // Around the camera: the target turns about it.
            const VxVector moved = PositionOf(camera) + Rotated(xAxis, m_Pitch, Rotated(AXIS_Y, m_Yaw, m_TargetFromCamera));
            target->SetPosition(&moved);
        } else {
            camera->Rotate(&AXIS_Y, m_Yaw);
            camera->Rotate(&AXIS_X, m_Pitch, camera);
        }
        break;
    }
    case CameraAction::Roll: {
        // Dev.exe sub_4948C0: about the direction the drag began with.
        const float angle = (dy + dx) * Speed(m_Steps.roll, m_Steps.rollConstant, seconds);
        camera->Rotate(&m_Dir, angle);
        break;
    }
    case CameraAction::Fov: {
        // Dev.exe sub_48D750: kept between 0.01 and 3.
        if (NextFov(m_Fov, dx, dy, Speed(m_Steps.fov, m_Steps.fovConstant, seconds), m_Fov)) camera->SetFov(m_Fov);
        break;
    }
    }
}

void CameraGesture::Cancel() {
    if (!m_Active) return;
    m_Active = false;
    m_Undo.Cancel(m_View ? m_View->Context() : nullptr);
}

bool CameraGesture::End(Document *document) {
    if (!m_Active) return false;
    m_Active = false;
    if (!document || !document->IsOpen()) {
        m_Undo.Begin(nullptr, {});
        return false;
    }
    // The 3D Layout's own cameras are not the composition's.
    return m_Undo.Commit(*document, CameraActionText(m_Action, false, m_Target != 0, m_AroundSelection),
                         !m_ViewCamera);
}

// ------------------------------------------------------------- transformations

namespace {

bool HasSelectedAncestor(CK3dEntity *entity, const std::vector<CK_ID> &selection) {
    for (CK3dEntity *parent = entity->GetParent(); parent; parent = parent->GetParent())
        if (std::find(selection.begin(), selection.end(), parent->GetID()) != selection.end()) return true;
    return false;
}

void AddDescendants(CK3dEntity *entity, std::vector<CK_ID> &ids) {
    for (int i = 0; i < entity->GetChildrenCount(); ++i)
        if (CK3dEntity *child = entity->GetChild(i)) {
            ids.push_back(child->GetID());
            AddDescendants(child, ids);
        }
}

// The referential's axes in the world (unit length).
void AxesOf(CK3dEntity *referential, VxVector axes[3]) {
    axes[0] = VxVector(1.0f, 0.0f, 0.0f);
    axes[1] = VxVector(0.0f, 1.0f, 0.0f);
    axes[2] = VxVector(0.0f, 0.0f, 1.0f);
    if (!referential) return;
    const VxMatrix &world = referential->GetWorldMatrix();
    for (int i = 0; i < 3; ++i) {
        axes[i] = VxVector(world[i][0], world[i][1], world[i][2]);
        if (SquareMagnitude(axes[i]) > 0.0f) axes[i].Normalize();
    }
}

} // namespace

// The referential of the left toolbar's combo (Dev.exe sub_491B60): the
// object (Local), the world, the camera (View), the object's parent, the
// object picked (Pick Obj.); the reference guide is no object (FrameOf).
CK3dEntity *TransformGesture::Referential(CK3dEntity *object) const {
    switch (m_Settings.referential) {
    case 0: return object;
    case 2: return m_View->Camera();
    case 3: return object ? object->GetParent() : nullptr;
    case 5: return m_View && m_View->Context() ? CK3dEntity::Cast(m_View->Context()->GetObject(m_Settings.referent)) : nullptr;
    default: return nullptr;
    }
}

void TransformGesture::FrameOf(CK3dEntity *object, VxVector axes[3], VxVector &origin) const {
    origin = VxVector(0.0f, 0.0f, 0.0f);
    if (m_Settings.referential == 4 && m_View) {
        const VxMatrix guide = m_View->GuideMatrix();
        for (int i = 0; i < 3; ++i) {
            axes[i] = VxVector(guide[i][0], guide[i][1], guide[i][2]);
            if (SquareMagnitude(axes[i]) > 0.0f) axes[i].Normalize();
        }
        origin = VxVector(guide[3][0], guide[3][1], guide[3][2]);
        return;
    }
    CK3dEntity *frame = Referential(object);
    AxesOf(frame, axes);
    if (frame) frame->GetPosition(&origin);
}

bool TransformGesture::MouseRay(int x, int y, VxVector &origin, VxVector &direction) const {
    return m_View->ViewRay(x, y, origin, direction);
}

bool TransformGesture::Begin(LayoutView &view, TransformAction action, const TransformSettings &settings,
                             const TransformSteps &steps, int scaleMode, const std::vector<CK_ID> &selection, int x,
                             int y, const VxVector *grabbed) {
    m_Active = false;
    CKContext *context = view.Context();
    if (!context) return false;
    m_View = &view;
    m_Action = action;
    m_Settings = settings;
    m_Steps = steps;
    m_ScaleMode = scaleMode;
    m_Objects.clear();
    std::vector<CK_ID> undoIds;
    VxBbox all;
    bool found = false;
    for (CK_ID id : selection) {
        CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(id));
        if (!entity || view.IsInterfaceObject(id)) continue;
        // A selected parent moves its selected children (with the hierarchy).
        if (settings.hierarchy && HasSelectedAncestor(entity, selection)) continue;
        Moved moved;
        moved.id = id;
        moved.world = entity->GetWorldMatrix();
        entity->GetPosition(&moved.position);
        entity->GetScale(&moved.scale);
        m_Objects.push_back(moved);
        undoIds.push_back(id);
        // Without the hierarchy the children stay where they are: their
        // local matrices change.
        if (!settings.hierarchy) AddDescendants(entity, undoIds);
        VxBbox box;
        if (LayoutBox(entity, box)) {
            if (!found) all = box;
            all.Max = Maximize(all.Max, box.Max);
            all.Min = Minimize(all.Min, box.Min);
            found = true;
        }
    }
    if (m_Objects.empty()) return false;
    m_Center = found ? all.GetCenter() : m_Objects.front().position;
    m_Grab = grabbed ? *grabbed : m_Center;
    m_StartX = m_LastX = x;
    m_StartY = m_LastY = y;
    m_Angles[0] = m_Angles[1] = 0.0f;
    m_Factors = VxVector(1.0f, 1.0f, 1.0f);
    // Translate's plane (sub_491B60): it holds the constrained axes; a single
    // axis takes, as its second, the axis seen longest on the screen.
    VxVector axes[3], frameOrigin;
    FrameOf(CK3dEntity::Cast(context->GetObject(m_Objects.front().id)), axes, frameOrigin);
    CKCamera *camera = view.Camera();
    m_SingleAxis = settings.axes == 1 || settings.axes == 2 || settings.axes == 4;
    m_Normal = TranslatePlaneNormal(settings.axes, axes, camera ? &camera->GetWorldMatrix() : nullptr);
    m_Undo.Begin(context, undoIds);
    m_Active = true;
    return true;
}

void TransformGesture::Move(int x, int y) {
    if (!m_Active) return;
    switch (m_Action) {
    case TransformAction::Translate: Translate(x, y); break;
    case TransformAction::Rotate: {
        // sub_4955C0: the mouse's travel over the view, Step degrees per view.
        const float width = static_cast<float>(std::max(1, m_View->Width())),
                    height = static_cast<float>(std::max(1, m_View->Height()));
        float first = 0.0f, second = 0.0f;
        RotateAngles(m_Settings.axes, (x - m_LastX) / width, (y - m_LastY) / height, m_Steps.rotate, first, second);
        m_Angles[0] += first;
        m_Angles[1] += second;
        Rotate();
        break;
    }
    case TransformAction::Scale: {
        // sub_497770: the factor of this move, (up - right) * Step per view.
        const float width = static_cast<float>(std::max(1, m_View->Width())),
                    height = static_cast<float>(std::max(1, m_View->Height()));
        const float f = ScaleStep((x - m_LastX) / width, (y - m_LastY) / height, m_Steps.scale);
        const VxVector factor = ScaleFactors(m_ScaleMode, m_Settings.axes, f);
        const VxVector next(m_Factors.x * factor.x, m_Factors.y * factor.y, m_Factors.z * factor.z);
        if (next.x >= 0.05f && next.y >= 0.05f && next.z >= 0.05f) {
            m_Factors = next;
            Scale();
        }
        break;
    }
    }
    m_LastX = x;
    m_LastY = y;
}

void TransformGesture::Translate(int x, int y) {
    CKContext *context = m_View->Context();
    CK3dEntity *first = CK3dEntity::Cast(context->GetObject(m_Objects.front().id));
    VxVector axes[3], frameOrigin;
    FrameOf(first, axes, frameOrigin);
    VxVector local(0.0f, 0.0f, 0.0f);
    if (m_Steps.translateSticky) {
        VxVector origin, direction, delta;
        if (!MouseRay(x, y, origin, direction) || !StickyTranslate(m_Normal, m_Grab, origin, direction, delta)) return;
        local = InAxes(delta, axes);
    } else {
        // The mouse's travel since the press, over the view, times the step.
        const float width = static_cast<float>(std::max(1, m_View->Width())),
                    height = static_cast<float>(std::max(1, m_View->Height()));
        const float dx = (x - m_StartX) / width * m_Steps.translate, dy = -(y - m_StartY) / height * m_Steps.translate;
        local = SlidingTranslate(m_Settings.axes, dx, dy);
    }
    VxVector delta = ConstrainedMove(m_Settings.axes, local, axes);
    if (m_Settings.snap && (m_Steps.snapMode & 0x3D)) delta = Snap(x, y, delta);
    for (const Moved &moved : m_Objects)
        if (CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(moved.id))) {
            const VxVector position = moved.position + delta;
            entity->SetPosition(&position, nullptr, m_Settings.hierarchy ? FALSE : TRUE);
        }
}

VxVector TransformGesture::Snap(int x, int y, const VxVector &delta) const {
    CKContext *context = m_View->Context();
    CK3dEntity *first = CK3dEntity::Cast(context->GetObject(m_Objects.front().id));
    ViewProjection projection;
    if (!first || !m_View->Projection(projection)) return delta;
    const int mode = m_Steps.snapMode;
    const VxVector at = m_Objects.front().position + delta;
    auto screen = [&projection](const VxVector &world) { return ScreenPoint(projection, world); };
    const VxVector point = screen(at);
    bool snapped = false;
    VxVector target = at;
    // World Sections: the corners of the World Step cell holding the point.
    if (mode & SNAP_WORLD_SECTIONS) {
        target = NearestCellCorner(at, m_Steps.worldStep, projection);
        snapped = true;
    }
    // Under the mouse (CK's pick), the first object hidden (Dev shows it again after).
    CKRenderContext *rc = m_View->RenderContext();
    if (!snapped && rc && (mode & (SNAP_OBJECTS_AXIS | SNAP_VERTICES | SNAP_EDGES | SNAP_FACES))) {
        const CKDWORD flags = first->GetObjectFlags();
        first->Show(CKHIDE);
        CKPICKRESULT pick = {};
        CK3dEntity *under = CK3dEntity::Cast(RenderPick(rc, x, y, &pick, FALSE));
        first->Show((flags & CK_OBJECT_VISIBLE) ? CKSHOW : (flags & CK_OBJECT_HIERACHICALHIDE) ? CKHIERARCHICALHIDE : CKHIDE);
        if (under && !m_View->IsInterfaceObject(under->GetID())) {
            if (mode & SNAP_OBJECTS_AXIS) {
                under->GetPosition(&target);
                snapped = true;
            }
            CKMesh *mesh = under->GetCurrentMesh();
            VxVector world[3], flat[3];
            const bool face = mesh && pick.FaceIndex >= 0 && pick.FaceIndex < mesh->GetFaceCount();
            if (face) {
                int index[3] = {0, 0, 0};
                mesh->GetFaceVertexIndex(pick.FaceIndex, index[0], index[1], index[2]);
                for (int k = 0; k < 3; ++k) {
                    VxVector local;
                    mesh->GetVertexPosition(index[k], &local);
                    under->Transform(&world[k], &local);
                    flat[k] = screen(world[k]);
                }
            }
            const float radius = static_cast<float>(m_Steps.snapRadius);
            if (!snapped && face && (mode & SNAP_VERTICES)) snapped = SnapToVertex(world, flat, point, radius, target);
            if (!snapped && face && (mode & SNAP_EDGES)) snapped = SnapToEdge(world, flat, point, radius, target);
            if (!snapped && (mode & SNAP_FACES)) {
                under->Transform(&target, &pick.IntersectionPoint);
                snapped = true;
            }
        }
    }
    return snapped ? SnapCorrection(delta, at, target) : delta;
}

void TransformGesture::Rotate() {
    CKContext *context = m_View->Context();
    float first = m_Angles[0], second = m_Angles[1];
    if (m_Settings.snap) {
        const float step = m_Steps.snapAngle * 0.017453292f;
        first = Snapped(first, step);
        second = Snapped(second, step);
    }
    const int axes = m_Settings.axes;
    // The axes the two angles turn about (in the referential).
    VxVector a(0.0f, 1.0f, 0.0f), b(1.0f, 0.0f, 0.0f);
    switch (axes) {
    case 1: a = VxVector(1.0f, 0.0f, 0.0f); break;
    case 2: a = VxVector(0.0f, 1.0f, 0.0f); break;
    case 4: a = VxVector(0.0f, 0.0f, 1.0f); break;
    case 3: a = VxVector(0.0f, 1.0f, 0.0f), b = VxVector(1.0f, 0.0f, 0.0f); break;
    case 5: a = VxVector(1.0f, 0.0f, 0.0f), b = VxVector(0.0f, 0.0f, 1.0f); break;
    case 6: a = VxVector(0.0f, 1.0f, 0.0f), b = VxVector(0.0f, 0.0f, 1.0f); break;
    default: break;
    }
    const CKBOOL keep = m_Settings.hierarchy ? FALSE : TRUE;
    // Pivot Axis Object (documentation: "The referent axis is another
    // object. If the referent object is in the selection, it won't rotate").
    CK3dEntity *referent = m_Settings.pivot == 0 ? CK3dEntity::Cast(context->GetObject(m_Settings.referent)) : nullptr;
    for (const Moved &moved : m_Objects) {
        CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(moved.id));
        if (!entity) continue;
        entity->SetWorldMatrix(moved.world, keep);
        if (entity == referent) continue;
        // The axes in the world: the referent's, else the referential's.
        VxVector worldAxes[3], origin(0.0f, 0.0f, 0.0f);
        if (m_Settings.pivot == 0) {
            AxesOf(referent, worldAxes);
            if (referent) referent->GetPosition(&origin);
        } else {
            FrameOf(entity, worldAxes, origin);
        }
        const VxVector wa = worldAxes[0] * a.x + worldAxes[1] * a.y + worldAxes[2] * a.z;
        const VxVector wb = worldAxes[0] * b.x + worldAxes[1] * b.y + worldAxes[2] * b.z;
        if (m_Settings.pivot == 2 || m_Settings.pivot == 0) {
            // About the selection's center, or the referent: the objects move too.
            const VxVector center = m_Settings.pivot == 2 ? m_Center : origin;
            VxVector offset = Rotated(wa, first, moved.position - center);
            if (second != 0.0f) offset = Rotated(wb, second, offset);
            const VxVector position = center + offset;
            entity->SetPosition(&position, nullptr, keep);
        }
        entity->Rotate(&wa, first, nullptr, keep);
        if (second != 0.0f) entity->Rotate(&wb, second, nullptr, keep);
    }
}

void TransformGesture::Scale() {
    CKContext *context = m_View->Context();
    VxVector factors = m_Factors;
    if (m_Settings.snap) {
        const float step = m_Steps.snapScale * 0.01f;
        factors = VxVector(Snapped(factors.x, step), Snapped(factors.y, step), Snapped(factors.z, step));
        if (factors.x < 0.05f || factors.y < 0.05f || factors.z < 0.05f) return;
    }
    const CKBOOL keep = m_Settings.hierarchy ? FALSE : TRUE;
    CK3dEntity *referent = m_Settings.pivot == 0 ? CK3dEntity::Cast(context->GetObject(m_Settings.referent)) : nullptr;
    VxVector referentAt(0.0f, 0.0f, 0.0f);
    if (referent) referent->GetPosition(&referentAt);
    for (const Moved &moved : m_Objects) {
        CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(moved.id));
        if (!entity) continue;
        entity->SetWorldMatrix(moved.world, keep);
        // The referent is not scaled (Pivot Axis Object).
        if (entity == referent) continue;
        if (m_Settings.pivot == 2 || m_Settings.pivot == 0) {
            // About the selection's center, or the referent.
            const VxVector center = m_Settings.pivot == 2 ? m_Center : referentAt;
            const VxVector offset = moved.position - center;
            const VxVector position = center + VxVector(offset.x * factors.x, offset.y * factors.y, offset.z * factors.z);
            entity->SetPosition(&position, nullptr, keep);
        }
        const VxVector scale(moved.scale.x * factors.x, moved.scale.y * factors.y, moved.scale.z * factors.z);
        entity->SetScale(&scale, keep, TRUE);
    }
}

void TransformGesture::Cancel() {
    if (!m_Active) return;
    m_Active = false;
    m_Undo.Cancel(m_View ? m_View->Context() : nullptr);
}

bool TransformGesture::End(Document *document) {
    if (!m_Active) return false;
    m_Active = false;
    if (!document || !document->IsOpen()) {
        m_Undo.Begin(nullptr, {});
        return false;
    }
    static const char *const labels[] = {"Move Objects", "Rotate Objects", "Scale Objects"};
    return m_Undo.Commit(*document, labels[static_cast<int>(m_Action)]);
}

void WheelZoom(LayoutView &view, const CameraSteps &steps, int notches, float extent) {
    CKCamera *camera = view.Camera();
    if (!camera || notches == 0) return;
    const float scale = extent / static_cast<float>(std::max(1, view.Height()));
    if (camera->GetProjectionType() == CK_PERSPECTIVEPROJECTION) {
        const VxVector local(0.0f, 0.0f, notches * scale * steps.zoom);
        VxVector world;
        camera->TransformVector(&world, &local);
        const VxVector before = PositionOf(camera), after = before + world;
        camera->SetPosition(&after);
        if (CK3dEntity *target = TargetOf(camera)) {
            VxVector local, at = PositionOf(target);
            camera->InverseTransform(&local, &at);
            if (local.z < 0.0f) camera->SetPosition(&before);
        }
    } else {
        const float zoom = (notches * scale * steps.orthoZoom + 1.0f) * camera->GetOrthographicZoom();
        camera->SetOrthographicZoom(zoom < 0.000001f ? 0.000001f : std::min(zoom, 1000000.0f));
    }
}

void Fly(LayoutView &view, FlyMode mode, float nx, float ny, float seconds, const FlySteps &steps, bool shift,
         const VxVector &center) {
    CKCamera *camera = view.Camera();
    if (!camera || (nx == 0.0f && ny == 0.0f)) return;
    CK3dEntity *target = TargetOf(camera);
    const float scale = steps.constant ? seconds : 1.0f;
    const float yaw = steps.turn * scale * nx, pitch = steps.turn * scale * ny;
    const float advanceX = steps.advance * scale * nx, advanceY = steps.advance * scale * ny;
    auto inside = [&](const VxVector &p) {
        return p.x >= steps.worldMin.x && p.x <= steps.worldMax.x && p.y >= steps.worldMin.y && p.y <= steps.worldMax.y &&
               p.z >= steps.worldMin.z && p.z <= steps.worldMax.z;
    };
    // The camera's X in the world.
    auto cameraX = [&]() {
        VxVector axis;
        Vx3DRotateVector(&axis, camera->GetWorldMatrix(), &AXIS_X);
        return axis;
    };
    const VxVector position = PositionOf(camera);
    switch (mode) {
    case FlyMode::Advance: {
        const VxVector local(0.0f, 0.0f, advanceY);
        VxVector delta;
        camera->TransformVector(&delta, &local);
        const VxVector moved = position + delta;
        if (!inside(moved)) return;
        if (target) {
            const VxVector at = PositionOf(target) + delta;
            if (inside(at)) {
                const VxVector turned = moved + Rotated(AXIS_Y, yaw, at - moved);
                target->SetPosition(&turned);
            }
        } else {
            camera->Rotate(&AXIS_Y, yaw);
        }
        camera->SetPosition(&moved);
        break;
    }
    case FlyMode::Look:
        if (target) {
            const VxVector axis = cameraX();
            const VxVector at = position + Rotated(axis, pitch, Rotated(AXIS_Y, yaw, PositionOf(target) - position));
            target->SetPosition(&at);
        } else {
            camera->Rotate(&AXIS_Y, yaw);
            camera->Rotate(&AXIS_X, pitch, camera);
        }
        break;
    case FlyMode::Strafe: {
        VxVector local(-advanceX, advanceY, 0.0f);
        if (camera->GetProjectionType() == CK_PERSPECTIVEPROJECTION && !shift) local *= Magnitude(position) / 70.0f;
        VxVector delta;
        camera->TransformVector(&delta, &local);
        const VxVector moved = position + delta;
        if (!inside(moved)) return;
        camera->SetPosition(&moved);
        if (target) {
            const VxVector at = PositionOf(target) + delta;
            if (inside(at)) target->SetPosition(&at);
        }
        break;
    }
    case FlyMode::Orbit: {
        const VxVector axis = cameraX();
        const VxVector moved = center + Rotated(axis, pitch, Rotated(AXIS_Y, yaw, position - center));
        if (target) {
            const VxVector at = center + Rotated(axis, pitch, Rotated(AXIS_Y, yaw, PositionOf(target) - center));
            target->SetPosition(&at);
        } else {
            camera->Rotate(&AXIS_Y, yaw);
            camera->Rotate(&AXIS_X, pitch, camera);
        }
        camera->SetPosition(&moved);
        break;
    }
    }
}

bool ZoomOnObjects(LayoutView &view, const std::vector<CK_ID> &objects, bool scene, float *extent) {
    CKCamera *camera = view.Camera();
    if (!camera) return false;
    CKContext *context = camera->GetCKContext();
    CK3dEntity *target = TargetOf(camera);
    std::vector<CK3dEntity *> entities;
    if (scene) {
        if (CKScene *current = view.Scene()) {
            CKObjectArray *array = CreateCKObjectArray();
            current->ComputeObjectList(array, CKCID_3DENTITY, TRUE);
            for (array->Reset(); !array->EndOfList(); array->Next())
                if (CK3dEntity *entity = CK3dEntity::Cast(array->GetData(context))) entities.push_back(entity);
            DeleteCKObjectArray(array);
        }
    } else {
        for (CK_ID id : objects)
            if (CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(id))) entities.push_back(entity);
    }
    VxBbox all;
    bool found = false;
    for (CK3dEntity *entity : entities) {
        VxBbox box;
        if (entity == camera || entity == target || view.IsInterfaceObject(entity->GetID()) || !LayoutBox(entity, box))
            continue;
        if (!found) all = box;
        all.Max = Maximize(all.Max, box.Max);
        all.Min = Minimize(all.Min, box.Min);
        found = true;
    }
    if (!found) return false;
    const VxVector center = all.GetCenter();
    const float radius = Magnitude(all.Max - center);
    if (extent) *extent = radius;
    if (camera->GetProjectionType() == CK_PERSPECTIVEPROJECTION) {
        VxVector dir, up, right;
        camera->GetOrientation(&dir, &up, &right);
        dir.Normalize();
        const VxVector position = center - dir * (radius / std::tan(camera->GetFov() * 0.5f));
        camera->SetPosition(&position);
        if (target) target->SetPosition(&center);
    } else {
        if (radius > 0.0f) camera->SetOrthographicZoom(1.0f / radius);
        VxVector local, onPlane;
        camera->InverseTransform(&local, &center);
        local.z = 0.0f;
        camera->Transform(&onPlane, &local);
        const VxVector delta = onPlane - PositionOf(camera);
        const VxVector position = PositionOf(camera) + delta;
        camera->SetPosition(&position);
        if (target) {
            const VxVector moved = PositionOf(target) + delta;
            target->SetPosition(&moved);
        }
    }
    return true;
}

// ------------------------------------------------------------ 2D entities

int Handle2DAt(CK2dEntity *entity, float x, float y, float size) {
    if (!entity || !entity->IsVisible()) return 0;
    VxRect source, rect;
    entity->GetExtents(source, rect);
    auto inside = [x, y](float left, float top, float right, float bottom) {
        return x >= left && x < right && y >= top && y < bottom;
    };
    const float half = size * 0.5f;
    const float cx = std::floor((rect.left + rect.right) * 0.5f), cy = std::floor((rect.top + rect.bottom) * 0.5f);
    if (inside(rect.left, rect.top, rect.left + size, rect.top + size)) return 1;
    if (inside(rect.right - size, rect.bottom - size, rect.right, rect.bottom)) return 2;
    if (inside(cx - half, rect.bottom - size, cx + half, rect.bottom)) return 3;
    if (inside(rect.right - size, cy - half, rect.right, cy + half)) return 4;
    return 0;
}

bool Transform2DGesture::Begin(LayoutView &view, TransformAction action, int axes, bool snap, const Snap2D &snapping,
                               float snapScale, const std::vector<CK_ID> &selection, CK_ID grabbed, int x, int y) {
    m_Active = false;
    CKContext *context = view.Context();
    if (!context || action == TransformAction::Rotate) return false;
    m_View = &view;
    m_Action = action;
    m_Axes = axes;
    m_Snap = snap;
    m_SnapScale = snapScale;
    m_Objects.clear();
    m_Targets.clear();
    m_Grabbed = 0;
    std::vector<CK_ID> ids;
    for (CK_ID id : selection) {
        CK2dEntity *entity = CK2dEntity::Cast(context->GetObject(id));
        if (!entity || view.IsInterfaceObject(id)) continue;
        Moved moved;
        moved.id = id;
        entity->GetPosition(moved.position);
        entity->GetSize(moved.size);
        if (id == grabbed) m_Grabbed = m_Objects.size();
        m_Objects.push_back(moved);
        ids.push_back(id);
    }
    if (m_Objects.empty()) return false;
    m_StartX = x;
    m_StartY = y;
    m_Influence = snapping.influence;
    // Dev.exe sub_491060: what the edges snap to.
    if (snap && action == TransformAction::Translate) {
        const float width = static_cast<float>(view.Width()), height = static_cast<float>(view.Height());
        if (snapping.mode & SNAP_SPRITES) {
            if (CKScene *scene = view.Scene()) {
                CKObjectArray *array = CreateCKObjectArray();
                scene->ComputeObjectList(array, CKCID_2DENTITY, TRUE);
                for (array->Reset(); !array->EndOfList(); array->Next()) {
                    CK2dEntity *other = CK2dEntity::Cast(array->GetData(context));
                    if (!other || other->IsPrivate() || !other->IsVisible() || view.IsInterfaceObject(other->GetID()) ||
                        std::find(ids.begin(), ids.end(), other->GetID()) != ids.end())
                        continue;
                    VxRect source, rect;
                    other->GetExtents(source, rect);
                    m_Targets.push_back(VxRect(std::floor(rect.left), std::floor(rect.top), std::floor(rect.right),
                                               std::floor(rect.bottom)));
                }
                DeleteCKObjectArray(array);
            }
        }
        if (snapping.mode & SNAP_SCREEN_BORDERS) m_Targets.push_back(VxRect(0.0f, 0.0f, width - 1.0f, height - 1.0f));
        if (snapping.mode & SNAP_SCREEN_GUIDE) {
            float offsetX = snapping.screenOffset[0], offsetY = snapping.screenOffset[1];
            float stepX = snapping.screenStep[0], stepY = snapping.screenStep[1];
            if (snapping.homogenous) {
                offsetX = width * offsetX + 0.5f;
                offsetY = height * offsetY + 0.5f;
                stepX = width * stepX + 0.5f;
                stepY = height * stepY + 0.5f;
            }
            // Each line an edge reaching far beyond the view.
            if (stepX >= 1.0f)
                for (int k = offsetX != 0.0f ? 0 : 1; k * stepX + offsetX <= width; ++k) {
                    const float at = std::floor(k * stepX + offsetX + 0.5f);
                    m_Targets.push_back(VxRect(at, -65536.0f, at, height + 65536.0f));
                }
            if (stepY >= 1.0f)
                for (int k = offsetY != 0.0f ? 0 : 1; k * stepY + offsetY <= height; ++k) {
                    const float at = std::floor(k * stepY + offsetY + 0.5f);
                    m_Targets.push_back(VxRect(-65536.0f, at, width + 65536.0f, at));
                }
        }
    }
    m_Undo.Begin(context, ids);
    m_Active = true;
    return true;
}

void Transform2DGesture::Move(int x, int y) {
    if (!m_Active) return;
    CKContext *context = m_View->Context();
    float dx = static_cast<float>(x - m_StartX), dy = static_cast<float>(y - m_StartY);
    if (m_Axes == 1) dy = 0.0f;
    if (m_Axes == 2) dx = 0.0f;
    const Moved &grabbed = m_Objects[m_Grabbed];
    CK2dEntity *primary = CK2dEntity::Cast(context->GetObject(grabbed.id));
    if (m_Action == TransformAction::Translate) {
        if (!m_Targets.empty() && primary) {
            // The grabbed entity's rectangle where the mouse would put it
            // (Dev: its homogeneous rectangle times the view's size, else its rectangle).
            VxRect rect;
            primary->SetPosition(Vx2DVector(grabbed.position.x + dx, grabbed.position.y + dy));
            if (primary->IsHomogeneousCoordinates()) {
                primary->GetHomogeneousRect(rect);
                rect.Transform(Vx2DVector(static_cast<float>(m_View->Width()), static_cast<float>(m_View->Height())),
                               Vx2DVector(1.0f, 1.0f));
            } else {
                primary->GetRect(rect);
            }
            int best[2];
            bool found[2];
            SnapEdges(rect, m_Targets, m_Influence, best, found);
            if (found[0] && m_Axes != 2) dx += static_cast<float>(best[0]);
            if (found[1] && m_Axes != 1) dy += static_cast<float>(best[1]);
        }
        for (const Moved &moved : m_Objects)
            if (CK2dEntity *entity = CK2dEntity::Cast(context->GetObject(moved.id)))
                entity->SetPosition(Vx2DVector(moved.position.x + dx, moved.position.y + dy));
        return;
    }
    // Scale: the grabbed entity's size grows by the travel.
    const Vx2DVector factors = ScaleFactors2D(m_Axes, dx, dy, grabbed.size, m_Snap ? m_SnapScale : 0.0f);
    const float fx = factors.x, fy = factors.y;
    for (const Moved &moved : m_Objects)
        if (CK2dEntity *entity = CK2dEntity::Cast(context->GetObject(moved.id)))
            entity->SetSize(Vx2DVector(moved.size.x * fx, moved.size.y * fy));
}

void Transform2DGesture::Cancel() {
    if (!m_Active) return;
    m_Undo.Cancel(m_View->Context());
    m_Active = false;
}

bool Transform2DGesture::End(Document *document) {
    if (!m_Active) return false;
    m_Active = false;
    if (!document) {
        m_Undo.Cancel(m_View->Context());
        return false;
    }
    return m_Undo.Commit(*document, m_Action == TransformAction::Translate ? "Move 2D Entities" : "Scale 2D Entities");
}

} // namespace vdev
