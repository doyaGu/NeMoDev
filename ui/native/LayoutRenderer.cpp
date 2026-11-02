#include "core/layout/LayoutInterface.h"
#include "core/layout/LayoutView.h"
#include "core/layout/SdlGpuInterop.h"
#include "core/runtime/TextEncoding.h"
#include "ui/layout/LayoutWindow.h"
#include "ui/layout/LayoutWindows.h"
#include "ui/schematic/Snapshots.h"
#include <algorithm>

namespace vdev {
LayoutWindow::LayoutWindow(CkRuntimeHost &runtime, ParameterCopies &copies, EditorApplication &editor)
    : m_ParameterCopies(copies), m_CommandEditor(editor), m_EditorHost(editor.Host()), m_View(std::make_unique<LayoutView>(runtime)), m_Interface(std::make_unique<LayoutInterfaceData>()) {}

LayoutWindow::~LayoutWindow() {
    CancelEdits();
    if (!ReleaseTexture()) {
        m_View->Stop();
        ResetViewportAfterContextStop();
    }
}

bool LayoutWindow::Start(SDL_Window *window, std::string *error) {
    m_MainWindow = window;
    if (!m_Window)
        return true;
    Window().needsStart = false;
    Window().capturing = 0;
    CancelEdits();
    if (!ReleaseTexture()) {
        // Keep the borrowed SDL texture alive until the old Context, and with
        // it the stale logical handle, has ceased to exist.
        m_View->Stop();
        ResetViewportAfterContextStop();
    }
    m_MainWindow = window;
    ApplyPreferences();
    std::string message;
    m_Started = m_View->Start(Utf8ToAnsi(m_Preferences.Text("3D Layout\\Author Mode\\Device Driver")), &message,
                              MainRenderer().Device(), m_MainRenderContext);
    SelectViewportContext();
    if (!m_Started) m_StartError = message;
    if (error) *error = message;
    m_View->windowEvent = [this](const SDL_Event &event) {
        if (!m_Switching) FullScreenEvent(event);
    };
    return m_Started;
}

void LayoutWindow::Stop() {
    if (m_Window)
        Window().capturing = 0;
    CancelEdits();
    const bool released = ReleaseTexture();
    m_View->Stop();
    if (!released)
        ResetViewportAfterContextStop();
    m_Started = false;
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
    m_ViewportContext = m_View->RasterizerContext();
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
    if (ConfigureGpuViewport(m_View->Width(), m_View->Height()))
        m_ViewportRenderer = &LayoutWindow::RenderGpuViewport;
    else
        m_ViewportRenderer = &LayoutWindow::RenderReadbackViewport;
    return (this->*m_ViewportRenderer)();
}

UiTexture *LayoutWindow::RenderReadbackViewport() {
    if (!m_View->RenderFrame() || !m_View->ReadPixels(m_Pixels) ||
        !Upload(m_Pixels, m_View->Width(), m_View->Height()))
        return nullptr;
    return m_Texture.get();
}

UiTexture *LayoutWindow::RenderGpuViewport() {
    return m_View->RenderFrame() ? m_GpuTexture.get() : nullptr;
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

bool LayoutWindow::CapturePixels(std::vector<unsigned char> &pixels, int &width, int &height) {
    if (!m_Started || m_View->Width() <= 0 || m_View->Height() <= 0)
        return false;
    if ((UsesGpuViewport() || m_View->FullScreen()) && !m_View->ReadPixels(m_Pixels))
        return false;
    if (m_Pixels.size() != size_t(m_View->Width()) * size_t(m_View->Height()) * 4)
        return false;
    pixels = m_Pixels;
    width = m_View->Width();
    height = m_View->Height();
    return true;
}

bool LayoutWindow::EnterFullScreen(const LayoutScreenMode &mode, std::string &error) {
    if (m_Window)
        Window().capturing = 0;
    // Fullscreen presents the render window normally; remove the editor-owned
    // virtual backbuffer before the Context changes mode or is recreated.
    if (!ReleaseGpuViewport()) {
        error = "3D Layout: cannot release the GPU viewport";
        return false;
    }
    m_ViewportContext = nullptr;
    m_ViewportRenderer = &LayoutWindow::RenderReadbackViewport;
    if (m_Windows)
        m_Windows->PrepareFullScreen(*this);
    m_Switching = true;
    VxDisplayMode nativeMode;
    nativeMode.Width = mode.width;
    nativeMode.Height = mode.height;
    nativeMode.Bpp = mode.bitsPerPixel;
    nativeMode.RefreshRate = mode.refreshRate;
    const bool full = m_View->GoFullScreen(mode.driver, nativeMode);
    if (full && m_MainWindow && !(SDL_GetWindowFlags(m_MainWindow) & SDL_WINDOW_HIDDEN))
        m_MainHiddenForFullScreen = SDL_HideWindow(m_MainWindow);
    m_Switching = false;
    SelectViewportContext();
    if (!full)
        error = "3D Layout: cannot enter fullscreen";
    return full;
}

bool LayoutWindow::ExitFullScreen(bool activateEditor, std::string &error) {
    if (m_Window)
        Window().capturing = 0;
    m_PendingToggle = m_PendingDeactivate = false;
    if (!m_View->FullScreen())
        return true;
    if (!ReleaseGpuViewport()) {
        error = "3D Layout: cannot release the GPU viewport";
        return false;
    }
    m_ViewportContext = nullptr;
    m_ViewportRenderer = &LayoutWindow::RenderReadbackViewport;
    m_Switching = true;
    const bool stopped = m_View->StopFullScreen();
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

UiTexture *LayoutWindow::RenderViewport(int width, int height) {
    UiTexture *frame = nullptr;
    const bool resized = width != m_View->Width() || height != m_View->Height();
    bool targetReleased = true;
    if (resized && m_ViewportRenderer == &LayoutWindow::RenderGpuViewport) {
        targetReleased = ReleaseGpuViewport();
        if (targetReleased)
            m_ViewportRenderer = &LayoutWindow::RenderUnselectedViewport;
    }
    if (targetReleased && m_View->PrepareFrame(width, height)) {
        const bool contextSelected =
            m_View->RasterizerContext() == m_ViewportContext ||
            SelectViewportContext();
        if (contextSelected)
            frame = (this->*m_ViewportRenderer)();
    }
    return frame;
}
bool LayoutWindow::HandleEvent(const SDL_Event &event) { return m_View->HandleEvent(event); }
void LayoutWindow::InputViewport(const ImVec4 &rect, int width, int height) {
    m_InputViewport = rect;
    m_InputWidth = width;
    m_InputHeight = height;
}
void LayoutWindow::ApplyInputViewport() {
    if (m_View->FullScreen())
        return;
    const auto &rect = m_InputViewport;
    const auto width = m_InputWidth, height = m_InputHeight;
    m_View->SetInputViewport(m_MainWindow, VxRect(rect.x, rect.y, rect.z, rect.w), width, height);
}
void LayoutWindow::ReleaseInputViewport() {
    m_View->ReleaseInputViewport();
}
void LayoutWindow::RenderPlayer() { if (m_View->FullScreen()) m_View->RenderFullScreen(); }
void LayoutWindow::ClearDropHighlight() { m_View->SetDropTarget(0); }


void LayoutWindow::ApplyLayoutData(const LayoutInterfaceData &data, const LayoutRestoreData &restore, bool restoreSelection) {
    *m_Interface = data; m_Group = {}; m_GroupRevision = ~0u;
    if (data.present && Client().Scope().document.value)
        Execute("layout.restore-interface", {IntegerArgument("restore-selection", restoreSelection), ObjectArgument("group", restore.group),
            ObjectArgument("camera", restore.camera), ObjectsArgument("objects", restore.selection), IntegerArgument("locked", restore.locked)}, AfterCommand::Group);
}

LayoutInterfaceData LayoutWindow::LayoutData() const {
    LayoutInterfaceData data = *m_Interface; data.present = true;
    LayoutSelectionData selection; LayoutSceneData scene, groups;
    if (ReadLayoutSelection(Client(), selection)) {
        data.selection.clear(); for (auto object : selection.objects) data.selection.push_back(object.object);
        data.locked = selection.locked;
    }
    if (ReadLayoutScene(Client(), "groups", groups)) {
        const auto *group = groups.Find(m_Group.object);
        data.group = group && group->object == m_Group ? m_Group.object : 0;
    }
    if (ReadLayoutScene(Client(), "cameras", scene)) data.cameras.assign(1, scene.savedCamera.object);
    return data;
}
} // namespace vdev
