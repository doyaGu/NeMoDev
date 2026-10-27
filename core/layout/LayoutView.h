#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "CKAll.h"

#include "core/runtime/SdkCompat.h"
#include "core/layout/ViewProjection.h"

struct SDL_Window;
struct SDL_GPUDevice;
union SDL_Event;

namespace vdev {

class CkRuntimeHost;

// Dev's 3D Layout view: the composition rendered by a CK render context,
// with the 3D Layout's own interface objects drawn over it. Dev renders into
// the 3D Layout window; here the render context lives on a hidden window of
// the view. A host may provide the SDL GPU device before Context creation so
// the editor can bind an ordinary UI-owned texture as its virtual backbuffer.

// Dev's default views (Dev.exe sub_4AD910; language pack [3D Layout]
// <CameraView>): interface cameras the composition does not keep.
enum class LayoutViewCamera { Perspective, Top, Front, Right, Orthographic };
constexpr int LAYOUT_VIEW_CAMERA_COUNT = 5;
const char *LayoutViewCameraName(LayoutViewCamera view);

// The 3D Layout preferences the view draws with (General Preferences,
// 3D Layout - Rendering and - Reference Guide; "3D Layout\..." keys).
struct LayoutDisplay {
    VxShadeType shading = GouraudShading; // Author Mode\Render Type Index
    bool textured = true;                 // Author Mode\Render Type Textured
    bool wireframe = false;               // Author Mode\Render Type Wireframe ("Add Wireframe Layer")
    bool showFrames = true;
    bool showCurves = true;
    bool showWorldAxis = true;
    bool showLights = true;
    bool showCameras = true;
    bool showObjectAxis = true;
    // Interface Objects: Constant Object Axis (0 variable, 1 world, 2 screen size) and its Zoom Percentage.
    int constantObjectAxis = 0;
    float objectAxisZoom = 10.0f;
    // Interface Objects: Constant Size On Screen of the icons and its Zoom Percentage.
    bool constantFrame = false, constantCamera = false, constantLight = false, constantCurvePoint = false;
    float frameZoom = 60.0f, cameraZoom = 80.0f, lightZoom = 80.0f, curvePointZoom = 10.0f;
    bool showPortals = true;
    bool showGrid = true;            // Author Mode\Show Grid (Toggle Reference Guide, G)
    bool ignoreCameraAspect = false; // Environment\Ignore Camera Aspect Ratio
    // Reference Grid: Orientation 3 X+Y, 5 X+Z, 6 Y+Z, 8 free (Orientation Dir/Up/Right).
    int gridOrientation = 5;
    int gridSections[2] = {20, 20};
    float gridStep[2] = {10.0f, 10.0f};
    CKDWORD gridColor = 0xFFFFFFFF;
    VxVector gridDir = VxVector(0.0f, 0.0f, 1.0f);
    VxVector gridUp = VxVector(0.0f, 1.0f, 0.0f);
    VxVector gridRight = VxVector(1.0f, 0.0f, 0.0f);
    // The Screen Guide (Show Screen Grid, Shift+G): lines Screen Step pixels
    // apart from Screen Offset, or fractions of the view with Homogenous
    // Screen Grid (Interface page).
    bool showScreenGrid = false;
    bool homogenousScreenGrid = false;
    float screenStep[2] = {20.0f, 20.0f};
    float screenOffset[2] = {0.0f, 0.0f};
};

// Dev.exe sub_49F640: what lies under a view point, for the shortcut menu:
// the 2D entity (in front), else the 3D entity (an icon's owner), the
// character of a body part, the entity's mesh, the material of the face met
// (a 3D sprite's own material; a 2D entity's material) and its texture.
struct LayoutPickInfo {
    CK_ID entity2d = 0, entity3d = 0, character = 0, mesh = 0, material = 0, texture = 0;
    // The mesh's face met (-1: none), the point in the entity's referential,
    // its texture coordinates (On-screen Debug Data).
    int face = -1;
    VxVector point = VxVector(0.0f, 0.0f, 0.0f);
    float u = 0.0f, v = 0.0f;
};

// What the view gives the parameter UIs' target button for a class (Dev.exe
// sub_4C18A0, message 0x607): the 3D entity under the mouse for the 3D
// classes (Object and BeObject too), the 2D entity, character, mesh,
// material or texture for theirs, the curve of a curve point, the entity's
// place, the current scene and level; for a camera class the camera under
// the mouse, else camera (the composition's camera shown, 0 for the view's
// own). 0 for the other classes.
CK_ID LayoutPickObject(CKContext *context, const LayoutPickInfo &info, CK_CLASSID cid, CK_ID camera);

// A world point on the screen (x, y, z the depth): CKRenderContext::Transform
// without a referential takes the current world transformation (the SDK's
// remark), after a frame the last object's with CK2_3D 2.1, so it is set to
// the identity first.
inline void ScreenOfWorldPoint(CKRenderContext *rc, VxVector *screen, const VxVector &world) {
    VxVector point(world);
    rc->SetWorldTransformationMatrix(VxMatrix::Identity());
    rc->Transform(screen, &point, nullptr);
}

// Player Mode's clicks (Dev.exe sub_4B95C0, WM_LBUTTONDOWN / WM_LBUTTONDBLCLK
// while the composition plays): "OnClick" or "OnDblClick" sent to the 3D
// entity under the mouse, else the 2D entity, without a sender. Returns the
// object the message went to (0: none).
CK_ID LayoutSendClick(CKContext *context, const LayoutPickInfo &info, bool doubleClick);

// The viewport of a width x height frame looking through camera: with
// cameraRatio (CK_RENDER_USECAMERARATIO; Ignore Camera Aspect Ratio clears
// it) the largest rectangle of the camera's aspect ratio centered in it,
// unless the camera ignores it (CK_3DENTITY_CAMERAIGNOREASPECT); else the
// frame. The render context's, without one.
VxRect LayoutViewRect(int width, int height, CKCamera *camera, bool cameraRatio);

// Player Mode's Fullscreen Render Device (Dev.exe sub_4B2450): the render
// driver whose name or description is driverName, else the first.
int LayoutFullScreenDriver(CKContext *context, const std::string &driverName);
// Its Render Mode ("640x480x32x59": width x height x bits per pixel x
// refresh rate): the driver's display mode of that size and depth (of that
// rate too when it is above 0). False for none (or under 16 bits): Dev does
// not go fullscreen then.
bool LayoutFullScreenMode(CKContext *context, int driver, const std::string &mode, VxDisplayMode &found);

class LayoutView {
public:
    explicit LayoutView(CkRuntimeHost &host);
    ~LayoutView();
    LayoutView(const LayoutView &) = delete;
    LayoutView &operator=(const LayoutView &) = delete;

    // Dev.exe sub_4BE110: the render context, on the render driver whose
    // name or description is driverName (Author Mode\Device Driver), else the
    // first; it becomes the current level's main render context.
    // The view owns its SDL render window and CK render context.
    // The optional GPU device is borrowed until Stop. Its configuration is
    // fixed for this view's lifetime.
    bool Start(const std::string &driverName, std::string *error, SDL_GPUDevice *sdlGpuDevice = nullptr);
    void Stop();
    bool Started() const;
    CKRenderContext *RenderContext() const;
    CKRasterizerContext *RasterizerContext() const;
    // The composition's context; the view's cameras, interface objects,
    // projection, picking of icons and gestures need no render context.
    CKContext *Context() const;

    // The composition changed (opened, new, closed): the interface objects
    // are made again when ClearAll destroyed them (with or without a render
    // context), and the render context joins the current level as its main
    // context.
    void CompositionChanged();

    void SetDisplay(const LayoutDisplay &display);
    const LayoutDisplay &Display() const { return m_Display; }

    // Dev's camera combo: the default views, then the composition's cameras.
    std::vector<CK_ID> Cameras() const;
    CK_ID ViewCamera(LayoutViewCamera view) const;
    bool IsViewCamera(CK_ID camera) const;
    // The camera the view looks through (the render context's attached one).
    CKCamera *Camera() const;
    void SetCamera(CK_ID camera);

    // Renders a width x height frame into pixels: rows top down, BGRA,
    // opaque. False when there is no render context or the copy failed, and
    // in fullscreen.
    bool Render(int width, int height, std::vector<unsigned char> &pixels);
    // Split resize from drawing so a caller can recreate and bind a target
    // after Context resize and before any rendering starts.
    bool PrepareFrame(int width, int height);
    bool RenderFrame();
    // Convenience form without a CPU transfer.
    bool Render(int width, int height);
    // Explicit synchronous capture of the completed image, opaque BGRA.
    bool ReadPixels(std::vector<unsigned char> &pixels);
    // Completed viewport readbacks (explicit captures and compatibility rendering).
    std::uint64_t ReadbackFrames() const { return m_ReadbackFrames; }

    // Player Mode's fullscreen (Dev.exe sub_5C5CB0, Alt+Enter): the render
    // context goes fullscreen on its window, which shows, takes the mouse
    // (SetCapture, as Dev's view) and gets the input; StopFullScreen hides it
    // again with the windowed context back.
    bool GoFullScreen(int driver, const VxDisplayMode &mode);
    bool StopFullScreen();
    bool FullScreen() const;
    // A fullscreen frame, presented (CK_RENDER_DOBACKTOFRONT).
    bool RenderFullScreen();
    // The view window's messages (it has the input only in fullscreen):
    // handled when the function returns true, result then being returned.
    std::function<void(const SDL_Event &)> windowEvent;
    bool HandleEvent(const SDL_Event &event);
    int Width() const { return m_Width; }
    int Height() const { return m_Height; }
    // The view's size without a render context (Render sets it otherwise).
    void SetViewSize(int width, int height);
    // The viewport within the frame (the camera's aspect ratio, unless
    // ignored): the render context's, else as it would have it.
    VxRect ViewRect() const;
    // The camera seen through the viewport; false without a camera.
    bool Projection(ViewProjection &projection) const;

    // Objects of the 3D Layout itself (CK_OBJECT_NOTTOBELISTEDANDSAVED).
    bool IsInterfaceObject(CK_ID object) const;

    // The selection the view draws (white boxes, the objects' axes).
    void SetSelection(const std::vector<CK_ID> &objects) { m_Selection = objects; }
    // The cameras and lights whose cone is drawn.
    void SetCones(const std::vector<CK_ID> &objects) { m_Cones = objects; }
    // Player Mode: the frames, curves, lights, cameras and portals drawn
    // although their type is not (Automatic Show Selection: the selection
    // when the composition started playing; Dev.exe sub_4A1670 8).
    void SetPlayerShown(const std::vector<CK_ID> &objects) { m_PlayerShown = objects; }
    // Rendering window placement, independent of the displayed input viewport.
    void SetScreenPosition(int left, int top);
    // The displayed rectangle in the source SDL window's client coordinates,
    // mapped to render pixels. The source window is borrowed until Stop().
    bool SetInputViewport(SDL_Window *source, const VxRect &rect, int width, int height);
    // The 3D entity something dragged would be dropped on (a yellow box; 0: none).
    void SetDropTarget(CK_ID object) { m_DropTarget = object; }
    CK_ID DropTarget() const { return m_DropTarget; }
    // Dev.exe sub_49F940 / sub_499870: the render object under a view point
    // (2D entities first; a body part gives its character), 0 for none.
    // ignoreUnpickable: Dev passes Shift.
    // point: where the ray met the picked 3D entity, in the world.
    CK_ID Pick(int x, int y, bool ignoreUnpickable = false, VxVector *point = nullptr) const;
    LayoutPickInfo PickInfo(int x, int y) const;
    // Dev.exe sub_49EE70: the render objects in a view rectangle, wholly
    // inside it or (intersect, Selection Mode) touching it.
    std::vector<CK_ID> RectPick(const VxRect &rect, bool intersect) const;
    // The world ray under a view point: origin on the camera (orthographic:
    // on its plane), direction to the front plane.
    bool ViewRay(int x, int y, VxVector &origin, VxVector &direction) const;
    // Where that ray meets a plane (a point of it, its normal), in front of
    // the camera.
    bool RayOnPlane(int x, int y, const VxVector &point, const VxVector &normal, VxVector &hit) const;
    // The reference guide's matrix (Reference Grid Orientation): the guide is
    // its local plane Y = 0.
    VxMatrix GuideMatrix() const;
    // The curve's point nearest to a view point, within a few pixels (Dev's
    // samples, CKCurve::GetPos every 0.1 / step count); along: its step.
    bool CurveAt(int x, int y, CK_ID curve, VxVector &hit, float *along) const;
    // The interface objects are there (not destroyed by ClearAll).
    bool InterfaceAlive() const;
    // Turns the Perspective View to its target (done before each frame).
    void AimViewCamera();
    // Reset Current Camera Settings (Dev.exe sub_4B95C0, command 150): the
    // default views go back where Dev makes them; another camera (and its
    // target, at the origin) loses its orientation and scale and gets Dev's
    // defaults (0,10,-50 or, orthographic, 1000,0,0 looking down -X; 4:3,
    // planes 1 and 4000); every camera's field of view 0.6894 and zoom 0.007
    // (Orthographic View 0.01).
    void ResetCamera();
    // Each 3D Frame's representation (Dev.exe +156, Change Representation):
    // 0 none, n the n-th Virtools_Frame_ mesh; frames not listed have 1.
    void SetRepresentations(const std::map<CK_ID, int> &representations) { m_Representations = representations; }
    // The curves whose control points are not drawn (Show curve points off).
    void SetHiddenCurvePoints(const std::set<CK_ID> &curves) { m_HiddenCurvePoints = curves; }
    // The Virtools_Frame_ meshes' names less "Virtools_Frame_".
    std::vector<std::string> FrameShapeNames() const;
    // The icon meshes loaded from interface.nmo (Camera, Spot, Frame, ...)
    // and the 3D Frame representations (Virtools_Frame_x).
    std::vector<std::string> IconNames() const;
    // The scene the view shows: the current scene, else the level scene (a
    // composition opened is not launched).
    CKScene *Scene() const;
    // An icon of an object without geometry (Dev's 112-byte records): its
    // owner, mesh, world matrix (scaled for Constant Size On Screen) and
    // screen box.
    struct IconHit {
        CK_ID owner = 0, mesh = 0;
        VxMatrix world;
        VxRect rect;
    };
    // The icons as projection shows them (Dev.exe sub_4A1E00: cameras,
    // lights, their targets, 3D Frames, curve points, portals, in that
    // order), nothing drawn: DrawIcons draws them, the picking takes them as
    // the camera is now. through: the camera looked through (no icon).
    std::vector<IconHit> PlaceIcons(const ViewProjection &projection, CK_ID through) const;
    // The icons drawn in the last frame.
    const std::vector<IconHit> &DrawnIcons() const { return m_IconHits; }
    size_t IconCount() const { return m_IconHits.size(); }

private:
    std::uint64_t m_ReadbackFrames = 0;
    SDL_GPUDevice *m_SdlGpuDevice = nullptr;
    static void PostRender(CKRenderContext *context, void *argument);
    static void PostSpriteRender(CKRenderContext *context, void *argument);
    void CreateInterfaceObjects();
    void DestroyInterfaceObjects();
    void DrawReferenceGrid(CKRenderContext *rc);
    void DrawScreenGrid(CKRenderContext *rc);
    void DrawSelection2D(CKRenderContext *rc);
    void DrawSelection(CKRenderContext *rc);
    void DrawIcons(CKRenderContext *rc);
    void RenderIcon(CKRenderContext *rc, const IconHit &icon);
    // Whether an object of the scene gets its icon (not private, not an
    // interface object, visible), and its type is shown (Player Mode: the
    // objects selected at Play still are).
    bool IconShown(CKObject *object, CKScene *scene) const;
    bool IconListed(bool typeShown, CK_ID id) const;
    // The icons as the render context shows them now.
    std::vector<IconHit> PlaceIconsNow() const;
    bool IconBox(CK_ID owner, VxMatrix &world, VxBbox &box) const;
    // The object under a view point: a 2D entity, an icon's owner or a 3D
    // entity (a body part itself); desc: CK's pick, its face data for the
    // 3D entity returned only (FaceIndex -1 otherwise).
    CKObject *PickObject(int x, int y, bool ignoreUnpickable, VxVector *point, CKPICKRESULT *desc) const;
    void ComputeWorldAxis(CKRenderContext *rc);
    void DrawWorldAxis(CKRenderContext *rc);
    void ApplyRenderMode();

    CkRuntimeHost &m_Host;
    SDL_Window *m_Window = nullptr;
    SDL_Window *m_InputSource = nullptr;
    VxRect m_InputRect;
    int m_InputWidth = 0, m_InputHeight = 0;
    void *m_Owner = nullptr;  // its owner, left during fullscreen
    CK_ID m_RenderContext = 0;
    CK_ID m_Camera = 0; // SetCamera's (the render context's attached one when there is one)
    int m_Width = 640;
    int m_Height = 480;
    LayoutDisplay m_Display;
    CK_ID m_ViewCameras[LAYOUT_VIEW_CAMERA_COUNT] = {};
    CK_ID m_ViewTarget = 0;    // the Perspective View's target
    CK_ID m_AxisLabels[3] = {}; // "X", "Y", "Z" (Virtools_SpriteAxisX/Y/Z)
    std::vector<CK_ID> m_InterfaceObjects;
    std::uint64_t m_InterfaceGeneration = ~0ull; // the host's when they were made
    std::map<std::string, CK_ID> m_Icons; // interface.nmo's objects, by name less "Virtools_"
    std::vector<IconHit> m_IconHits; // drawn last frame (the selection's boxes of icon objects)
    std::vector<CK_ID> m_FrameShapes;     // Virtools_Frame_x meshes
    std::map<CK_ID, int> m_Representations;
    std::set<CK_ID> m_HiddenCurvePoints;
    CK_ID m_IconEntity = 0;               // Virtools_Temporary_3D_Entity
    std::vector<CK_ID> m_Selection;
    std::vector<CK_ID> m_Cones;
    std::vector<CK_ID> m_PlayerShown;
    CK_ID m_DropTarget = 0;
    // World axis of the last frame, screen space: origin, then the tips and
    // the two arrow points of X, Y and Z.
    struct AxisLines {
        bool valid = false;
        Vx2DVector origin;
        Vx2DVector tip[3], left[3], right[3];
    } m_Axis;
};

} // namespace vdev
