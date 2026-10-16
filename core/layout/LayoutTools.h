#pragma once

#include <functional>
#include <string>
#include <vector>

#include "CKAll.h"
#include "core/level/ObjectEditCommands.h"

namespace vdev {

class LayoutView;
class Document;

// The camera tools' settings (General Preferences, 3D Layout - Actions;
// "3D Layout\Actions\..." keys) and the World Size of the Interface page.
struct CameraSteps {
    float rotate = 1.0f;     // Camera Rotate Step (Orbit, radians per view)
    float translate = 100.0f; // Camera Translate Step (Pan)
    float zoom = 500.0f;     // Camera Zoom Step (Zoom, Dolly, wheel)
    float orthoZoom = 10.0f; // Camera Orthographic Zoom Step
    float roll = 5.0f;       // Camera Roll Step
    float fov = 1.0f;        // Camera Fov Step
    bool rotateConstant = false, translateConstant = false, zoomConstant = true, orthoZoomConstant = true,
         rollConstant = false, fovConstant = false;
    // Free Camera Orbit Type: 0 Value % of the clipping range in front of the
    // front plane, 1 the front plane + Value, 2 Value in front of the camera.
    int freeOrbitType = 0;
    float freeOrbitValue = 10.0f;
    // Orbit Selection Type: 0 when 3D entities are selected, 1 when the
    // selection is also locked, 2 never.
    int orbitSelectionType = 0;
    VxVector worldMin = VxVector(-1000000.0f, -1000000.0f, -1000000.0f);
    VxVector worldMax = VxVector(1000000.0f, 1000000.0f, 1000000.0f);
};

enum class CameraAction { Dolly, Zoom, Pan, Orbit, OrbitCamera, Roll, Fov };

// The status bar's action text of a camera tool (language pack [3D Layout]
// <Action Info>), idle or while dragging.
const char *CameraActionText(CameraAction action, bool active, bool targeted, bool aroundSelection);

// Dev's camera tools (Dev.exe sub_499AD0 and its subclasses: Dolly and Zoom
// sub_48B750, Pan sub_4940D0, Orbit sub_493210, Roll sub_494700, Field of
// View sub_48D590). The left button drags the view's camera (and its
// target); the mouse moves by (last - current) pixels, over the view's size.
// Constant speed tools measure from where the drag began and keep moving at
// that speed per second while the button is held (Idle). The right button
// cancels; the end of the drag is one undo step.
class CameraGesture {
public:
    // False when the view has no camera. selection: the 3D Layout's
    // selection (Orbit around it); shift and ctrl as Dev reads them.
    bool Begin(LayoutView &view, CameraAction action, const CameraSteps &steps, int x, int y,
               const std::vector<CK_ID> &selection, bool locked, bool shift, bool ctrl);
    void Move(int x, int y, bool shift);
    void Idle(bool shift);
    void Cancel();
    // Records the move as one undo step (false when nothing moved; no
    // document: nothing is recorded).
    bool End(Document *document);
    bool Active() const { return m_Active; }
    CameraAction Action() const { return m_Action; }
    bool AroundSelection() const { return m_AroundSelection; }
    // The camera the gesture moves (an orthographic default view gives way to
    // the Orthographic View when orbited or rolled).
    CK_ID Camera() const { return m_Camera; }
    // The time constant speed tools move by: milliseconds of a clock (none:
    // the wall clock).
    void SetClock(std::function<float()> milliseconds) { m_Milliseconds = std::move(milliseconds); }

private:
    void Process(bool shift);
    // At constant speed: Idle moves, Move only measures.
    bool ConstantSpeed() const;
    float Seconds();
    void ResetClock();
    bool Inside(const VxVector &position) const;

    LayoutView *m_View = nullptr;
    CameraAction m_Action = CameraAction::Zoom;
    CameraSteps m_Steps;
    bool m_Active = false;
    CK_ID m_Camera = 0, m_Target = 0;
    int m_LastX = 0, m_LastY = 0, m_DeltaX = 0, m_DeltaY = 0;
    VxTimeProfiler m_Clock;
    std::function<float()> m_Milliseconds;
    float m_ClockStart = 0.0f;
    ObjectsGesture m_Undo;
    bool m_ViewCamera = false;
    // Zoom: the orthographic zoom being changed.
    float m_OrthoZoom = 1.0f;
    // Orbit: the pivot, the angles so far, the start matrices and offsets.
    bool m_AroundSelection = false;
    VxVector m_Pivot, m_CameraOffset, m_TargetOffset, m_TargetFromCamera;
    float m_Pitch = 0.0f, m_Yaw = 0.0f;
    VxMatrix m_CameraMatrix, m_TargetMatrix;
    // Roll: the start orientation; Field of View: its value.
    VxVector m_Dir, m_Up, m_Right;
    float m_Fov = 0.5f;
};

enum class TransformAction { Translate, Rotate, Scale };

// A transformation tool's own settings: Dev keeps them per tool (Translate
// byte_641874.., Rotate byte_64189C.., Scale byte_6418A4..) and shows them
// on the left toolbar while the tool is active.
struct TransformSettings {
    int axes = 5;        // Constrain X 1, Y 2, Z 4 (F5..F8)
    int referential = 1; // 0 Local, 1 World, 2 View, 3 Parent, 4 Ref. Guide, 5 Pick Obj.
    int pivot = 1;       // Pivot Axis (F9): 0 Object, 1 Axis (each object's own), 2 Selection
    // The object picked for Pivot Axis Object (rotations and scales about it,
    // in its axes) and Referential Pick Obj. (its axes).
    CK_ID referent = 0;
    bool hierarchy = true; // Toggle Hierarchy (H): children follow
    bool snap = false;     // Toggle Snap (S)
};

// "3D Layout\Actions\Object ..." and the Snap & Align steps.
struct TransformSteps {
    float translate = 0.1f;  // Object Translate Step (views, not in Sticky Mode)
    float rotate = 180.0f;   // Object Rotate Step (degrees per view)
    float scale = 5.0f;      // Object Scale Step
    bool translateSticky = true, rotateSticky = false, scaleSticky = false;
    float snapAngle = 10.0f; // Snap\Rotation Angle (degrees)
    float snapScale = 10.0f; // Snap\Scaling Percentage
    // Translate's Snap & Align (Dev.exe dword_6464C4, the page's boxes):
    // the SNAP_ bits, the influence radius in pixels, the World Step.
    int snapMode = 0;
    int snapRadius = 8;
    VxVector worldStep = VxVector(1.0f, 1.0f, 1.0f);
};

// The Snap & Align page's boxes (Dev.exe dialog 746, sub_5AC1D0).
enum SnapMode {
    SNAP_WORLD_SECTIONS = 0x1,
    SNAP_OBJECTS_AXIS = 0x4,
    SNAP_VERTICES = 0x8,
    SNAP_EDGES = 0x10,
    SNAP_FACES = 0x20,
    SNAP_SCREEN_BORDERS = 0x40,
    SNAP_SCREEN_GUIDE = 0x80,
    SNAP_SPRITES = 0x100,
};

// Dev's Select and Translate / Rotate / Scale (Dev.exe sub_48DAA0 sub_491B60
// sub_48EED0; sub_494BD0 sub_495D80 sub_4955C0; sub_4969B0 sub_497770):
// the selected 3D entities (with their hierarchy unless Toggle Hierarchy is
// off) follow the mouse; the right button cancels; the end is one undo step.
// Translate in Sticky Mode keeps the point grabbed under the mouse on the
// plane of the constrained axes (in the referential's axes); otherwise the
// mouse's travel over the view times the step moves along them. Rotate turns
// (Step degrees per view) about the constrained axes; Scale (Step per view)
// multiplies the scale: 0 uniformly, 1 keeping the volume, 2 the
// constrained axes only (the Scale tool's menu order). With Toggle Snap,
// Translate snaps the first object as Dev.exe sub_48EED0 does: to the
// nearest corner (on the screen) of its World Step cell, else, under the
// mouse, to the entity's axis, the face's vertex or edge within the
// influence radius, or the face; the correction moves the axes being moved.
class TransformGesture {
public:
    bool Begin(LayoutView &view, TransformAction action, const TransformSettings &settings, const TransformSteps &steps,
               int scaleMode, const std::vector<CK_ID> &selection, int x, int y, const VxVector *grabbed);
    void Move(int x, int y);
    void Cancel();
    bool End(Document *document);
    bool Active() const { return m_Active; }
    TransformAction Action() const { return m_Action; }
    int ScaleMode() const { return m_ScaleMode; }
    // The first object moved (Dev's status bar shows its coordinates).
    CK_ID Grabbed() const { return m_Objects.empty() ? 0 : m_Objects.front().id; }

private:
    CK3dEntity *Referential(CK3dEntity *object) const;
    // The referential's axes and origin: an object's, the world's, the
    // reference guide's (LayoutView::GuideMatrix).
    void FrameOf(CK3dEntity *object, VxVector axes[3], VxVector &origin) const;
    bool MouseRay(int x, int y, VxVector &origin, VxVector &direction) const;
    void Translate(int x, int y);
    VxVector Snap(int x, int y, const VxVector &delta) const;
    void Rotate();
    void Scale();

    LayoutView *m_View = nullptr;
    TransformAction m_Action = TransformAction::Translate;
    TransformSettings m_Settings;
    TransformSteps m_Steps;
    int m_ScaleMode = 0;
    bool m_Active = false;
    struct Moved {
        CK_ID id = 0;
        VxMatrix world;
        VxVector position, scale;
    };
    std::vector<Moved> m_Objects;
    ObjectsGesture m_Undo;
    int m_StartX = 0, m_StartY = 0, m_LastX = 0, m_LastY = 0;
    VxVector m_Grab, m_Normal, m_Center;
    bool m_SingleAxis = false;
    VxVector m_Axis; // the constrained axis (single-axis translate), world
    float m_Angles[2] = {0.0f, 0.0f};
    VxVector m_Factors = VxVector(1.0f, 1.0f, 1.0f);
};

// The Snap & Align of 2D entities (Dev.exe sub_491060): Screen Borders,
// Screen Guide Grid and Sprites (SNAP_ bits), the influence size in pixels,
// the Screen Guide's lines (Screen Step / Screen Offset, fractions of the
// view with Homogenous Screen Grid).
struct Snap2D {
    int mode = 0;
    int influence = 5;
    float screenStep[2] = {20.0f, 20.0f};
    float screenOffset[2] = {0.0f, 0.0f};
    bool homogenous = false;
};

// The handle of a selected 2D entity under a view point (Dev.exe
// sub_4B5C60, handles of size pixels in its on-screen rectangle): 1 the
// top-left corner (moves it), 2 the bottom-right corner (both sizes), 3 the
// bottom middle (the height), 4 the right middle (the width); 0 none.
int Handle2DAt(CK2dEntity *entity, float x, float y, float size = 10.0f);

// Dev's Translate and Scale of 2D entities (Dev.exe sub_491060 / sub_48E630
// and sub_497770 / sub_497CA0; the 2D handles start them, sub_4C4A70): the
// selected 2D entities follow the mouse in pixels; axes 1 keeps them on X,
// 2 on Y, else both. Translate with snap moves the grabbed one's edges onto
// the edges within the influence (the smallest offset found, as Dev takes
// it) of the view's borders, the Screen Guide's lines and the other visible
// 2D entities of the scene. Scale multiplies the sizes so that the grabbed
// entity grows by the mouse's travel (at least to one pixel), in Scaling
// Percentage steps with snap. One undo step.
class Transform2DGesture {
public:
    bool Begin(LayoutView &view, TransformAction action, int axes, bool snap, const Snap2D &snapping, float snapScale,
               const std::vector<CK_ID> &selection, CK_ID grabbed, int x, int y);
    void Move(int x, int y);
    void Cancel();
    bool End(Document *document);
    bool Active() const { return m_Active; }
    TransformAction Action() const { return m_Action; }
    // The entity grabbed.
    CK_ID Grabbed() const { return m_Grabbed < m_Objects.size() ? m_Objects[m_Grabbed].id : 0; }

private:
    struct Moved {
        CK_ID id = 0;
        Vx2DVector position, size;
    };
    LayoutView *m_View = nullptr;
    TransformAction m_Action = TransformAction::Translate;
    int m_Axes = 3;
    bool m_Snap = false;
    float m_SnapScale = 10.0f;
    bool m_Active = false;
    std::vector<Moved> m_Objects;
    size_t m_Grabbed = 0;
    std::vector<VxRect> m_Targets; // the snap edges (Dev's rectangles)
    int m_Influence = 5;
    int m_StartX = 0, m_StartY = 0;
    ObjectsGesture m_Undo;
};

// The mouse wheel (Dev.exe sub_4C4BD0): a notch zooms as a drag of extent
// pixels would (extent: the half diagonal the last Zoom on Selection /
// Scene framed, 5 at first), not past the target. Dev records no undo step.
void WheelZoom(LayoutView &view, const CameraSteps &steps, int notches, float extent);

// Zoom on Selection (Shift+Z) and Zoom on Scene (W) (Dev.exe sub_4A9200):
// the camera backs away along its direction until the box of the objects
// (the selection, or the 3D entities of the current scene) fits its field
// of view, its target at the box's center; orthographic cameras zoom to
// 1 / the box's half diagonal. extent: set to that half diagonal (the
// wheel's step). False when there is nothing to frame.
bool ZoomOnObjects(LayoutView &view, const std::vector<CK_ID> &objects, bool scene, float *extent);

// Flycam (Y; Dev.exe sub_48C060 and its modes): the buttons held choose the
// mode. Advance (none, sub_48CD30): along the view by Fly Advance Step x
// ny, turning about the world Y by Fly Turn Step x nx. Look (left,
// sub_48CA50): turn about the world Y and pitch about the camera's X.
// Strafe (right, sub_48C690): in the view plane, perspective views (without
// Shift) by the camera's distance to the origin / 70. Orbit (both, on an
// object, sub_48D130): around center. nx, ny: the mouse's move over the
// view's size, (last - current), or with Constant Speed from where the mode
// began, then per second. A target camera's target turns with it; the
// camera stays within the World Size.
enum class FlyMode { Advance = 1, Look = 2, Strafe = 3, Orbit = 4 };
struct FlySteps {
    float turn = 1.0f;      // Camera Fly Turn Step
    float advance = 100.0f; // Camera Fly Advance Step
    bool constant = true;   // Camera Navigate Constant Speed Mode
    VxVector worldMin = VxVector(-1000000.0f, -1000000.0f, -1000000.0f);
    VxVector worldMax = VxVector(1000000.0f, 1000000.0f, 1000000.0f);
};
void Fly(LayoutView &view, FlyMode mode, float nx, float ny, float seconds, const FlySteps &steps, bool shift,
         const VxVector &center);

// The world box of an entity as the 3D Layout frames it (Dev.exe
// sub_4A89B0): its bounding box (characters and body parts: hierarchical),
// its position for entities without geometry.
bool LayoutBox(CK3dEntity *entity, VxBbox &box);

} // namespace vdev
