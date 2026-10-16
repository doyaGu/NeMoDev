#include "core/layout/LayoutView.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/Resources.h"

namespace vdev {

namespace {

const char *const VIEW_NAMES[LAYOUT_VIEW_CAMERA_COUNT] = {"Perspective View", "Top View", "Front View", "Right View",
                                                          "Orthographic View"};
SDL_Window *MakeWindow(int width, int height) {
    return SDL_CreateWindow("3D Layout", width, height, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
}

void MarkInterface(CKObject *object) {
    if (object) object->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
}

// Copies a dumped back buffer (any 16/24/32-bit format the rasterizer
// gives) as opaque BGRA rows.
void ToBgra(const VxImageDescEx &desc, std::vector<unsigned char> &pixels) {
    const int width = desc.Width, height = desc.Height;
    pixels.assign(static_cast<size_t>(width) * height * 4, 255);
    const int bytes = desc.BitsPerPixel / 8;
    auto channel = [](CKDWORD value, CKDWORD mask) -> unsigned char {
        if (!mask) return 0;
        int shift = 0;
        while (!(mask & 1)) {
            mask >>= 1;
            ++shift;
        }
        const CKDWORD v = (value >> shift) & mask;
        return static_cast<unsigned char>(mask == 0xFF ? v : v * 255 / mask);
    };
    for (int y = 0; y < height; ++y) {
        const unsigned char *row = desc.Image + static_cast<size_t>(y) * desc.BytesPerLine;
        unsigned char *out = &pixels[static_cast<size_t>(y) * width * 4];
        for (int x = 0; x < width; ++x) {
            CKDWORD value = 0;
            std::memcpy(&value, row + x * bytes, bytes);
            if (bytes == 4 && desc.RedMask == 0xFF0000 && desc.GreenMask == 0xFF00 && desc.BlueMask == 0xFF) {
                std::memcpy(out + x * 4, &value, 3);
            } else {
                out[x * 4 + 0] = channel(value, desc.BlueMask);
                out[x * 4 + 1] = channel(value, desc.GreenMask);
                out[x * 4 + 2] = channel(value, desc.RedMask);
            }
        }
    }
}

} // namespace

const char *LayoutViewCameraName(LayoutViewCamera view) {
    const int index = static_cast<int>(view);
    return index >= 0 && index < LAYOUT_VIEW_CAMERA_COUNT ? VIEW_NAMES[index] : "";
}

LayoutView::LayoutView(CkRuntimeHost &host) : m_Host(host) {}

LayoutView::~LayoutView() {
    Stop();
}

bool LayoutView::Started() const {
    return RenderContext() != nullptr;
}

CKContext *LayoutView::Context() const {
    return m_Host.Context();
}

CKRenderContext *LayoutView::RenderContext() const {
    CKContext *context = m_Host.Context();
    return context && m_RenderContext ? CKRenderContext::Cast(context->GetObject(m_RenderContext)) : nullptr;
}

bool LayoutView::Start(const std::string &driverName, std::string *error) {
    Stop();
    CKContext *context = m_Host.Context();
    CKRenderManager *renderer = context ? context->GetRenderManager() : nullptr;
    if (!renderer || renderer->GetRenderDriverCount() <= 0) {
        if (error) *error = "No render driver (the RenderEngines folder has no render engine)";
        return false;
    }
    m_Width = 640;
    m_Height = 480;
    SDL_Window *window = MakeWindow(m_Width, m_Height);
    if (!window) {
        if (error) *error = "Cannot create the 3D Layout window";
        return false;
    }
    m_Window = window;
    int driver = 0;
    for (int i = 0; !driverName.empty() && i < renderer->GetRenderDriverCount(); ++i)
        if (VxDriverDesc *desc = renderer->GetRenderDriverDescription(i)) {
            const char *name = DriverName(desc), *description = DriverDescription(desc);
            if ((name && driverName == name) || (description && driverName == description)) {
                driver = i;
                break;
            }
        }
    // Dev tries its driver, then the first one.
    CKRECT rect = {0, 0, m_Width, m_Height};
    CKRenderContext *rc = nullptr;
    GuardedCall([&] { rc = renderer->CreateRenderContext(window, driver, &rect, FALSE, -1, -1, -1, 0); }, &m_Host,
                "CKRenderManager::CreateRenderContext");
    if (!rc && driver != 0)
        GuardedCall([&] { rc = renderer->CreateRenderContext(window, 0, &rect, FALSE, -1, -1, -1, 0); }, &m_Host,
                    "CKRenderManager::CreateRenderContext");
    if (!rc) {
        SDL_DestroyWindow(window);
        m_Window = nullptr;
        if (error) *error = "Cannot create a render context";
        return false;
    }
    m_RenderContext = rc->GetID();
    CompositionChanged();
    return true;
}

void LayoutView::Stop() {
    CKContext *context = m_Host.Context();
    if (FullScreen()) StopFullScreen();
    if (context && m_InputSource)
        if (auto *input = static_cast<CKInputManager *>(context->GetManagerByGuid(INPUT_MANAGER_GUID)))
            input->SetInputViewport(nullptr, VxRect(), 0, 0);
    m_InputSource = nullptr;
    if (CKRenderContext *rc = RenderContext()) {
        rc->RemovePostRenderCallBack(&LayoutView::PostRender, this);
        rc->RemovePostSpriteRenderCallBack(&LayoutView::PostSpriteRender, this);
        DestroyInterfaceObjects();
        if (CKLevel *level = context->GetCurrentLevel()) level->RemoveRenderContext(rc);
        if (CKRenderManager *renderer = context->GetRenderManager())
            GuardedCall([&] { renderer->DestroyRenderContext(rc); }, &m_Host, "CKRenderManager::DestroyRenderContext");
    } else {
        DestroyInterfaceObjects(); // made without a render context
    }
    m_RenderContext = 0;
    if (m_Window) {
        SDL_DestroyWindow(m_Window);
    }
    m_Window = nullptr;
}

void LayoutView::CompositionChanged() {
    CKContext *context = m_Host.Context();
    if (!context) return;
    if (!InterfaceAlive()) {
        m_Camera = 0; // its id may be another object's after ClearAll
        CreateInterfaceObjects();
    }
    CKRenderContext *rc = RenderContext();
    if (!rc) {
        if (!Camera()) SetCamera(m_ViewCameras[0]);
        return;
    }
    // ClearAll drops the render context's callbacks: they are set again.
    rc->RemovePostRenderCallBack(&LayoutView::PostRender, this);
    rc->RemovePostSpriteRenderCallBack(&LayoutView::PostSpriteRender, this);
    rc->AddPostRenderCallBack(&LayoutView::PostRender, this, FALSE, FALSE);
    rc->AddPostSpriteRenderCallBack(&LayoutView::PostSpriteRender, this, FALSE);
    ApplyRenderMode();
    if (CKLevel *level = context->GetCurrentLevel()) {
        bool attached = false;
        for (int i = 0; i < level->GetRenderContextCount(); ++i) attached = attached || level->GetRenderContext(i) == rc;
        if (!attached) level->AddRenderContext(rc, TRUE);
        // Dev applies the level scene's environment to its new render context.
        CKScene *scene = context->GetCurrentScene() ? context->GetCurrentScene() : level->GetLevelScene();
        if (scene) GuardedCall([&] { scene->ApplyEnvironmentSettings(); }, &m_Host, "CKScene::ApplyEnvironmentSettings");
    }
    // Dev (sub_49DC50 -> sub_4AC040) updates the curves loaded, which makes
    // their line meshes: drawn from then on. (Dev also gives their points a
    // 0.4 scale and moves each curve's base to their center; that is left
    // to the curve tools here, not done to a composition just opened.)
    const XObjectPointerArray &curves = context->GetObjectListByType(CKCID_CURVE, TRUE);
    for (int i = 0; i < curves.Size(); ++i)
        if (CKCurve *curve = CKCurve::Cast(curves[i]))
            if (curve->GetControlPointCount() > 0) GuardedCall([&] { curve->Update(); }, &m_Host, "CKCurve::Update");
    if (!rc->GetAttachedCamera()) SetCamera(m_ViewCameras[0]);
}

void LayoutView::CreateInterfaceObjects() {
    CKContext *context = m_Host.Context();
    m_InterfaceObjects.clear();
    m_InterfaceGeneration = m_Host.Generation();
    auto make = [&](CK_CLASSID cid, const std::string &name) {
        CKObject *object = context->CreateObject(cid, const_cast<CKSTRING>(name.c_str()));
        MarkInterface(object);
        if (object) m_InterfaceObjects.push_back(object->GetID());
        return object;
    };
    // Dev.exe sub_4AD910. Perspective View: a target camera at (0, 10, -50)
    // aiming at its target at the origin, 4:3, field of view 0.6894.
    if (CKTargetCamera *camera = CKTargetCamera::Cast(make(CKCID_TARGETCAMERA, VIEW_NAMES[0]))) {
        camera->SetRenderChannels(FALSE);
        CK3dEntity *target = CK3dEntity::Cast(make(CKCID_3DENTITY, std::string(VIEW_NAMES[0]) + "_Target"));
        if (target) {
            target->SetRenderChannels(FALSE);
            camera->SetTarget(target);
            m_ViewTarget = target->GetID();
        }
        camera->SetPosition3f(0.0f, 10.0f, -50.0f);
        // A target camera outside the scene does not turn to its target by
        // itself here; the view aims it.
        const VxVector origin(0.0f, 0.0f, 0.0f);
        camera->LookAt(&origin);
        camera->SetAspectRatio(4, 3);
        camera->SetFov(0.68940508f);
        m_ViewCameras[0] = camera->GetID();
    }
    // The orthographic views, 1000 away from the origin: front plane 100,
    // back plane 2000, zoom 0.007 (Orthographic View: 0.01).
    struct Ortho {
        VxVector position, dir, up, right;
        bool orient;
        float zoom;
    };
    const Ortho views[4] = {
        {VxVector(0, 1000, 0), VxVector(0, -1, 0), VxVector(0, 0, 1), VxVector(1, 0, 0), true, 0.007f},  // Top
        {VxVector(0, 0, -1000), VxVector(), VxVector(), VxVector(), false, 0.007f},                       // Front
        {VxVector(1000, 0, 0), VxVector(-1, 0, 0), VxVector(0, 1, 0), VxVector(0, 0, 1), true, 0.007f},   // Right
        {VxVector(0, 1000, 0), VxVector(0, -1, 0), VxVector(0, 0, 1), VxVector(1, 0, 0), true, 0.01f}};   // Orthographic
    for (int i = 0; i < 4; ++i) {
        CKCamera *camera = CKCamera::Cast(make(CKCID_CAMERA, VIEW_NAMES[i + 1]));
        if (!camera) continue;
        camera->SetRenderChannels(FALSE);
        camera->SetPosition(&views[i].position);
        if (views[i].orient) camera->SetOrientation(&views[i].dir, &views[i].up, &views[i].right);
        camera->SetAspectRatio(4, 3);
        camera->SetProjectionType(CK_ORTHOGRAPHICPROJECTION);
        camera->SetOrthographicZoom(views[i].zoom);
        camera->SetFrontPlane(100.0f);
        camera->SetBackPlane(2000.0f);
        m_ViewCameras[i + 1] = camera->GetID();
    }
    // The world axis labels: Times New Roman 12, red X, green Y, blue Z.
    const char *labels[3] = {"X", "Y", "Z"};
    const char *names[3] = {"Virtools_SpriteAxisX", "Virtools_SpriteAxisY", "Virtools_SpriteAxisZ"};
    const CKDWORD colors[3] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF};
    for (int i = 0; i < 3; ++i) {
        CKSpriteText *text = CKSpriteText::Cast(make(CKCID_SPRITETEXT, names[i]));
        m_AxisLabels[i] = text ? text->GetID() : 0;
        if (!text) continue;
        text->Create(16, 16, 32, 0);
        text->SetFont((CKSTRING) "Times New Roman", -12, 400, FALSE, FALSE);
        text->SetText(const_cast<CKSTRING>(labels[i]));
        text->SetTextColor(colors[i]);
        text->SetAlign(static_cast<CKSPRITETEXT_ALIGNMENT>(CKSPRITETEXT_VCENTER | CKSPRITETEXT_HCENTER));
        text->SetZOrder(0xFFFF);
        text->SetPickable(FALSE);
        text->EnableClipToCamera(TRUE);
        text->SetPosition(Vx2DVector(-100.0f, -100.0f));
    }
    // The icon meshes (Dev.exe sub_4AD910): InterfacePlugins\interface.nmo
    // (resources/data, DataFilePath), "VIRTOOLS_x" renamed "Virtools_x";
    // VIRTOOLS_Frame_x are the 3D Frame representations.
    m_Icons.clear();
    m_FrameShapes.clear();
    const std::string file = DataFilePath(INTERFACE_FILE);
    CKObjectArray *loaded = CreateCKObjectArray();
    CKERROR result = CKERR_INVALIDFILE;
    GuardedCall([&] { result = context->Load(const_cast<CKSTRING>(file.c_str()), loaded, CK_LOAD_GEOMETRY); }, &m_Host,
                "CKContext::Load interface.nmo");
    if (result != CK_OK) m_Host.Log(LogLevel::Warning, "3D Layout: cannot load " + file);
    for (loaded->Reset(); !loaded->EndOfList(); loaded->Next()) {
        CKObject *object = loaded->GetData(context);
        if (!object) continue;
        MarkInterface(object);
        m_InterfaceObjects.push_back(object->GetID());
        // Not the composition's: out of every scene.
        if (CKSceneObject *sceneObject = CKSceneObject::Cast(object))
            for (int i = sceneObject->GetSceneInCount() - 1; i >= 0; --i)
                if (CKScene *scene = sceneObject->GetSceneIn(i)) scene->RemoveObjectFromScene(sceneObject);
        std::string name = object->GetName() ? object->GetName() : "";
        if (name.compare(0, 9, "VIRTOOLS_") == 0) {
            name = "Virtools_" + name.substr(9);
            object->SetName(const_cast<CKSTRING>(name.c_str()));
        }
        if (name.compare(0, 15, "Virtools_Frame_") == 0 && CKMesh::Cast(object))
            m_FrameShapes.push_back(object->GetID());
        else if (name.compare(0, 9, "Virtools_") == 0)
            m_Icons[name.substr(9)] = object->GetID();
    }
    DeleteCKObjectArray(loaded);
    // The entity that lends its matrix to the icons it renders.
    if (CK3dEntity *entity = CK3dEntity::Cast(make(CKCID_3DENTITY, "Virtools_Temporary_3D_Entity"))) {
        entity->SetPickable(FALSE);
        m_IconEntity = entity->GetID();
    }
}

std::vector<std::string> LayoutView::IconNames() const {
    std::vector<std::string> names;
    for (const auto &icon : m_Icons) names.push_back(icon.first);
    CKContext *context = m_Host.Context();
    for (CK_ID id : m_FrameShapes)
        if (CKObject *shape = context->GetObject(id)) names.push_back(shape->GetName() ? shape->GetName() : "");
    return names;
}

void LayoutView::DestroyInterfaceObjects() {
    CKContext *context = m_Host.Context();
    // Made before a ClearAll: gone with it, their ids perhaps another object's now.
    std::vector<CK_ID> alive;
    for (CK_ID id : m_InterfaceObjects)
        if (context && m_InterfaceGeneration == m_Host.Generation() && context->GetObject(id)) alive.push_back(id);
    if (!alive.empty())
        GuardedCall([&] { context->DestroyObjects(alive.data(), static_cast<int>(alive.size())); }, &m_Host,
                    "CKContext::DestroyObjects");
    m_InterfaceObjects.clear();
    if (IsViewCamera(m_Camera)) m_Camera = 0;
    std::fill(std::begin(m_ViewCameras), std::end(m_ViewCameras), 0);
    std::fill(std::begin(m_AxisLabels), std::end(m_AxisLabels), 0);
    m_ViewTarget = 0;
}

// ClearAll destroys the interface objects and CK hands their IDs out again
// (CkRuntimeHost::Generation); the camera is checked to be the 3D Layout's own
// too.
bool LayoutView::InterfaceAlive() const {
    if (m_InterfaceGeneration != m_Host.Generation()) return false;
    CKContext *context = m_Host.Context();
    CKObject *camera = context ? context->GetObject(m_ViewCameras[0]) : nullptr;
    return CKTargetCamera::Cast(camera) &&
           (camera->GetObjectFlags() & CK_OBJECT_NOTTOBELISTEDANDSAVED) == CK_OBJECT_NOTTOBELISTEDANDSAVED &&
           camera->GetName() && std::strcmp(camera->GetName(), VIEW_NAMES[0]) == 0;
}

CKScene *LayoutView::Scene() const {
    CKContext *context = m_Host.Context();
    if (!context) return nullptr;
    if (CKScene *scene = context->GetCurrentScene()) return scene;
    CKLevel *level = context->GetCurrentLevel();
    return level ? level->GetLevelScene() : nullptr;
}

bool LayoutView::IsInterfaceObject(CK_ID object) const {
    return std::find(m_InterfaceObjects.begin(), m_InterfaceObjects.end(), object) != m_InterfaceObjects.end();
}

void LayoutView::SetDisplay(const LayoutDisplay &display) {
    m_Display = display;
    ApplyRenderMode();
}

void LayoutView::ApplyRenderMode() {
    CKRenderContext *rc = RenderContext();
    if (!rc) return;
    rc->SetGlobalRenderMode(m_Display.shading, m_Display.textured, m_Display.wireframe);
    // Dev.exe sub_4B5450: "Ignore Camera Aspect Ratio" clears CK_RENDER_USECAMERARATIO.
    if (m_Display.ignoreCameraAspect)
        rc->ChangeCurrentRenderOptions(0, CK_RENDER_USECAMERARATIO);
    else
        rc->ChangeCurrentRenderOptions(CK_RENDER_USECAMERARATIO, 0);
}

std::vector<CK_ID> LayoutView::Cameras() const {
    std::vector<CK_ID> cameras;
    CKContext *context = m_Host.Context();
    if (!context) return cameras;
    for (CK_ID id : m_ViewCameras)
        if (id && context->GetObject(id)) cameras.push_back(id);
    const XObjectPointerArray &all = context->GetObjectListByType(CKCID_CAMERA, TRUE);
    for (int i = 0; i < all.Size(); ++i)
        if (CKObject *camera = all[i])
            if (!camera->IsPrivate() && !IsInterfaceObject(camera->GetID())) cameras.push_back(camera->GetID());
    return cameras;
}

CK_ID LayoutView::ViewCamera(LayoutViewCamera view) const {
    const int index = static_cast<int>(view);
    return index >= 0 && index < LAYOUT_VIEW_CAMERA_COUNT ? m_ViewCameras[index] : 0;
}

bool LayoutView::IsViewCamera(CK_ID camera) const {
    return camera && std::find(std::begin(m_ViewCameras), std::end(m_ViewCameras), camera) != std::end(m_ViewCameras);
}

CKCamera *LayoutView::Camera() const {
    if (CKRenderContext *rc = RenderContext()) return rc->GetAttachedCamera();
    CKContext *context = m_Host.Context();
    return context && m_Camera ? CKCamera::Cast(context->GetObject(m_Camera)) : nullptr;
}

void LayoutView::SetCamera(CK_ID id) {
    CKContext *context = m_Host.Context();
    CKCamera *camera = context ? CKCamera::Cast(context->GetObject(id)) : nullptr;
    if (!camera) return;
    m_Camera = id;
    if (CKRenderContext *rc = RenderContext()) rc->AttachViewpointToCamera(camera);
}

void LayoutView::SetViewSize(int width, int height) {
    if (RenderContext() || width <= 0 || height <= 0) return;
    m_Width = width;
    m_Height = height;
}

VxRect LayoutViewRect(int width, int height, CKCamera *camera, bool cameraRatio) {
    VxRect rect(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    if (!cameraRatio || !camera || (camera->GetFlags() & CK_3DENTITY_CAMERAIGNOREASPECT)) return rect;
    int across = 0, down = 0;
    camera->GetAspectRatio(across, down);
    if (across <= 0 || down <= 0 || width <= 0 || height <= 0) return rect;
    const float ratio = static_cast<float>(across) / static_cast<float>(down);
    const float w = static_cast<float>(width), h = static_cast<float>(height);
    if (w > h * ratio) {
        const float fitted = std::floor(h * ratio);
        rect.left = std::floor((w - fitted) * 0.5f);
        rect.right = rect.left + fitted;
    } else {
        const float fitted = std::floor(w / ratio);
        rect.top = std::floor((h - fitted) * 0.5f);
        rect.bottom = rect.top + fitted;
    }
    return rect;
}

VxRect LayoutView::ViewRect() const {
    if (CKRenderContext *rc = RenderContext()) {
        VxRect rect;
        rc->GetViewRect(rect);
        return rect;
    }
    return LayoutViewRect(m_Width, m_Height, Camera(), !m_Display.ignoreCameraAspect);
}

bool LayoutView::Projection(ViewProjection &projection) const {
    CKCamera *camera = Camera();
    if (!camera) return false;
    projection = ViewProjection::Of(camera, ViewRect());
    return true;
}

// Target cameras of the scene turn to their targets when rendered; the
// Perspective View, outside the scene, does not, so the view aims it.
void LayoutView::AimViewCamera() {
    CKRenderContext *rc = RenderContext();
    CKTargetCamera *camera = rc ? CKTargetCamera::Cast(rc->GetAttachedCamera()) : nullptr;
    CK3dEntity *target = camera && IsViewCamera(camera->GetID()) ? camera->GetTarget() : nullptr;
    if (!target) return;
    VxVector at, from;
    target->GetPosition(&at);
    camera->GetPosition(&from);
    if (SquareMagnitude(at - from) > 1e-8f) camera->LookAt(&at);
}

void LayoutView::SetScreenPosition(int left, int top) {
    if (m_Window && !FullScreen()) SDL_SetWindowPosition(m_Window, left, top);
}

bool LayoutView::SetInputViewport(SDL_Window *source, const VxRect &rect, int width, int height) {
    CKContext *context = Context();
    auto *input = context ? static_cast<CKInputManager *>(context->GetManagerByGuid(INPUT_MANAGER_GUID)) : nullptr;
    if (!input || FullScreen() || !input->SetInputViewport(source, rect, width, height)) return false;
    m_InputSource = source;
    m_InputRect = rect;
    m_InputWidth = width;
    m_InputHeight = height;
    return true;
}
bool LayoutView::HandleEvent(const SDL_Event &event) {
    if (!m_Window || SDL_GetWindowFromEvent(&event) != m_Window) return false;
    if (windowEvent) windowEvent(event);
    return true;
}

bool LayoutView::Render(int width, int height, std::vector<unsigned char> &pixels) {
    CKRenderContext *rc = RenderContext();
    if (!rc || width <= 0 || height <= 0 || rc->IsFullScreen()) return false;
    if (width != m_Width || height != m_Height) {
        SDL_SetWindowSize(m_Window, width, height);
        GuardedCall([&] { rc->Resize(0, 0, width, height); }, &m_Host, "CKRenderContext::Resize");
        m_Width = width;
        m_Height = height;
    }
    AimViewCamera();
    // BackToFront finishes the rasterizer's GPU submission. The SDL window
    // stays hidden; its retained back buffer supplies the editor viewport.
    const CKDWORD options = (rc->GetCurrentRenderOptions() | CK_RENDER_DOBACKTOFRONT) & CK_RENDER_OPTIONSMASK;
    bool rendered = false;
    GuardedCall([&] { rendered = rc->Render(static_cast<CK_RENDER_FLAGS>(options)) == CK_OK; }, &m_Host,
                "CKRenderContext::Render");
    if (!rendered) return false;
    VxImageDescEx desc;
    int size = 0;
    GuardedCall([&] { size = rc->DumpToMemory(nullptr, VXBUFFER_BACKBUFFER, desc); }, &m_Host,
                "CKRenderContext::DumpToMemory");
    if (size <= 0) return false;
    std::vector<unsigned char> buffer(static_cast<size_t>(size));
    desc.Image = buffer.data();
    GuardedCall([&] { size = rc->DumpToMemory(nullptr, VXBUFFER_BACKBUFFER, desc); }, &m_Host,
                "CKRenderContext::DumpToMemory");
    if (size <= 0 || desc.Width <= 0 || desc.Height <= 0 || desc.BitsPerPixel < 16) return false;
    ToBgra(desc, pixels);
    m_Width = desc.Width;
    m_Height = desc.Height;
    return true;
}

// ---------------------------------------------------------------- fullscreen

int LayoutFullScreenDriver(CKContext *context, const std::string &driverName) {
    CKRenderManager *renderer = context ? context->GetRenderManager() : nullptr;
    for (int i = 0; renderer && !driverName.empty() && i < renderer->GetRenderDriverCount(); ++i)
        if (VxDriverDesc *desc = renderer->GetRenderDriverDescription(i)) {
            const char *name = DriverName(desc), *description = DriverDescription(desc);
            if ((name && driverName == name) || (description && driverName == description)) return i;
        }
    return 0;
}

bool LayoutFullScreenMode(CKContext *context, int driver, const std::string &mode, VxDisplayMode &found) {
    CKRenderManager *renderer = context ? context->GetRenderManager() : nullptr;
    VxDriverDesc *desc = renderer && driver >= 0 && driver < renderer->GetRenderDriverCount()
                             ? renderer->GetRenderDriverDescription(driver)
                             : nullptr;
    int width = -1, height = -1, bpp = -1, rate = -1;
    if (!desc || std::sscanf(mode.c_str(), "%dx%dx%dx%d", &width, &height, &bpp, &rate) < 3 || width == -1 ||
        height == -1 || bpp < 16)
        return false;
    for (int i = 0; i < DisplayModeCount(desc); ++i) {
        const VxDisplayMode &candidate = DisplayModeAt(desc, i);
        if (candidate.Bpp == bpp && candidate.Width == width && candidate.Height == height &&
            (rate <= 0 || candidate.RefreshRate == rate)) {
            found = candidate;
            return true;
        }
    }
    return false;
}

bool LayoutView::FullScreen() const {
    CKRenderContext *rc = RenderContext();
    return rc && rc->IsFullScreen();
}

bool LayoutView::GoFullScreen(int driver, const VxDisplayMode &mode) {
    CKRenderContext *rc = RenderContext();
    if (!rc || !m_Window || rc->IsFullScreen()) return false;
    CKERROR result = CKERR_INVALIDRENDERCONTEXT;
    const bool completed = GuardedCall([&] {
        result = rc->GoFullScreen(mode.Width, mode.Height, mode.Bpp, driver, mode.RefreshRate);
    }, &m_Host, "CKRenderContext::GoFullScreen");
    if (!completed || result != CK_OK || !rc->IsFullScreen()) return false;
    // The SDL rasterizer renders to a caller-owned window. CK's fullscreen
    // render context does not change that window's desktop mode.
    SDL_DisplayMode displayMode{};
    const SDL_DisplayID display = SDL_GetDisplayForWindow(m_Window);
    const bool switched = SDL_GetClosestFullscreenDisplayMode(display, mode.Width, mode.Height,
            static_cast<float>(mode.RefreshRate), true, &displayMode) &&
        displayMode.w == mode.Width && displayMode.h == mode.Height &&
        SDL_SetWindowFullscreenMode(m_Window, &displayMode) && SDL_SetWindowFullscreen(m_Window, true);
    if (!switched) {
        SDL_SetWindowFullscreen(m_Window, false);
        GuardedCall([&] { rc->StopFullScreen(); }, &m_Host, "CKRenderContext::StopFullScreen");
        return false;
    }
    SDL_ShowWindow(m_Window);
    if (!SDL_SyncWindow(m_Window) || !(SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN)) {
        StopFullScreen();
        return false;
    }
    SDL_RaiseWindow(m_Window);
    SDL_SetWindowMouseGrab(m_Window, true);
    if (CKContext *context = Context())
        if (auto *input = static_cast<CKInputManager *>(context->GetManagerByGuid(INPUT_MANAGER_GUID))) {
            int width = 0, height = 0;
            SDL_GetWindowSize(m_Window, &width, &height);
            const VxRect rect(0, 0, static_cast<float>(width), static_cast<float>(height));
            input->SetInputViewport(m_Window, rect, mode.Width, mode.Height);
        }
    return true;
}

bool LayoutView::StopFullScreen() {
    CKRenderContext *rc = RenderContext();
    if (!rc || !m_Window || !rc->IsFullScreen()) return true;
    if (!SDL_SetWindowFullscreen(m_Window, false) || !SDL_SyncWindow(m_Window) ||
        (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN)) return false;
    CKERROR result = CKERR_INVALIDRENDERCONTEXT;
    const bool completed = GuardedCall([&] { result = rc->StopFullScreen(); }, &m_Host, "CKRenderContext::StopFullScreen");
    if (!completed || result != CK_OK || rc->IsFullScreen()) return false;
    SDL_SetWindowMouseGrab(m_Window, false);
    SDL_HideWindow(m_Window);
    if (m_InputSource) SetInputViewport(m_InputSource, m_InputRect, m_InputWidth, m_InputHeight);
    return true;
}

bool LayoutView::RenderFullScreen() {
    CKRenderContext *rc = RenderContext();
    if (!rc || !rc->IsFullScreen()) return false;
    AimViewCamera();
    const CKDWORD options = (rc->GetCurrentRenderOptions() | CK_RENDER_DOBACKTOFRONT) & CK_RENDER_OPTIONSMASK;
    bool rendered = false;
    GuardedCall([&] { rendered = rc->Render(static_cast<CK_RENDER_FLAGS>(options)) == CK_OK; }, &m_Host,
                "CKRenderContext::Render");
    return rendered;
}

void LayoutView::ResetCamera() {
    CKCamera *camera = Camera();
    if (!camera) return;
    const VxVector zero(0.0f, 0.0f, 0.0f), one(1.0f, 1.0f, 1.0f);
    auto orient = [](CK3dEntity *entity, VxVector dir, VxVector up, VxVector right) {
        entity->SetOrientation(&dir, &up, &right);
    };
    orient(camera, VxVector(0, 0, 1), VxVector(0, 1, 0), VxVector(1, 0, 0));
    camera->SetScale(&one, FALSE, TRUE);
    CK3dEntity *target = nullptr;
    if (CKTargetCamera *targeted = CKTargetCamera::Cast(camera)) {
        target = targeted->GetTarget();
        if (target) {
            target->SetPosition(&zero);
            orient(target, VxVector(0, 0, 1), VxVector(0, 1, 0), VxVector(1, 0, 0));
            target->SetScale(&one, FALSE, TRUE);
        }
    } else {
        camera->LookAt(&zero);
    }
    const CK_ID id = camera->GetID();
    if (id == ViewCamera(LayoutViewCamera::Perspective)) {
        camera->SetPosition3f(0.0f, 10.0f, -50.0f);
    } else if (id == ViewCamera(LayoutViewCamera::Top) || id == ViewCamera(LayoutViewCamera::Orthographic)) {
        camera->SetPosition3f(0.0f, 1000.0f, 0.0f);
        orient(camera, VxVector(0, -1, 0), VxVector(0, 0, 1), VxVector(1, 0, 0));
    } else if (id == ViewCamera(LayoutViewCamera::Front)) {
        camera->SetPosition3f(0.0f, 0.0f, -1000.0f);
        orient(camera, VxVector(0, 0, 1), VxVector(0, 1, 0), VxVector(1, 0, 0));
    } else if (id == ViewCamera(LayoutViewCamera::Right)) {
        camera->SetPosition3f(1000.0f, 0.0f, 0.0f);
        orient(camera, VxVector(-1, 0, 0), VxVector(0, 1, 0), VxVector(0, 0, 1));
    } else {
        if (camera->GetProjectionType() == CK_PERSPECTIVEPROJECTION) {
            camera->SetPosition3f(0.0f, 10.0f, -50.0f);
        } else {
            camera->SetPosition3f(1000.0f, 0.0f, 0.0f);
            orient(camera, VxVector(-1, 0, 0), VxVector(0, 1, 0), VxVector(0, 0, 1));
        }
        camera->SetAspectRatio(4, 3);
        camera->SetBackPlane(4000.0f);
        camera->SetFrontPlane(1.0f);
    }
    if (target) camera->LookAt(&zero, target);
    camera->SetFov(0.68940508f);
    camera->SetOrthographicZoom(id == ViewCamera(LayoutViewCamera::Orthographic) ? 0.01f : 0.007f);
}

std::vector<std::string> LayoutView::FrameShapeNames() const {
    std::vector<std::string> names;
    CKContext *context = m_Host.Context();
    for (CK_ID id : m_FrameShapes) {
        CKObject *shape = context ? context->GetObject(id) : nullptr;
        const std::string name = shape && shape->GetName() ? shape->GetName() : "";
        names.push_back(name.size() > 15 ? name.substr(15) : name);
    }
    return names;
}

VxMatrix LayoutView::GuideMatrix() const {
    VxMatrix world = VxMatrix::Identity();
    VxVector dir(0, 0, 1), up(0, 1, 0), right(1, 0, 0);
    switch (m_Display.gridOrientation) {
    case 3: // X+Y
        dir = VxVector(0, 1, 0), up = VxVector(0, 0, 1), right = VxVector(1, 0, 0);
        break;
    case 6: // Y+Z
        dir = VxVector(0, 1, 0), up = VxVector(1, 0, 0), right = VxVector(0, 0, 1);
        break;
    case 8:
        dir = m_Display.gridDir, up = m_Display.gridUp, right = m_Display.gridRight;
        break;
    default: // X+Z
        break;
    }
    world[0] = VxVector4(right.x, right.y, right.z, 0.0f);
    world[1] = VxVector4(up.x, up.y, up.z, 0.0f);
    world[2] = VxVector4(dir.x, dir.y, dir.z, 0.0f);
    return world;
}

} // namespace vdev
