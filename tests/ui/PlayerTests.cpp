#include "tests/ui/UiTests.h"
#include "core/runtime/SdkCompat.h"
#include "ui/render/UiRenderer.h"
#include "core/layout/SnapshotBuffer.h"
#include "features/layout/SnapshotData.h"
#include "imgui_internal.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#if defined(__APPLE__)
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace vdevuitest {
namespace {
vdev::SnapshotData Snapshots() {
    vdev::SnapshotData data;
    vdev::ReadSnapshots(vdev::EditorClient((&App().WorkspaceForTests())->Application().Host(), 0), data);
    return data;
}
constexpr const char *PLAY = "//##StatusBar/play/##bitmap";

SDL_Window *EditorWindow() {
    return SDL_GetWindowFromID(static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(ImGui::GetMainViewport()->PlatformHandle)));
}
SDL_Window *RenderWindow() {
    CKRenderContext *rc = App().Layout().View().RenderContext();
    return rc ? static_cast<SDL_Window *>(rc->GetWindowHandle()) : nullptr;
}
bool NativeFocus(SDL_Window *window) {
    if (!window)
        return false;
#if defined(__APPLE__)
    // Cocoa's minimization callbacks can reenter SDL's focus update and leave
    // its cached keyboard focus empty while this window is still the key window.
    // Assert the actual native focus, including after asynchronous transitions.
    const auto native = static_cast<id>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                              SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr));
    return native && reinterpret_cast<BOOL (*)(id, SEL)>(objc_msgSend)(native, sel_registerName("isKeyWindow"));
#elif defined(_WIN32)
    return GetForegroundWindow() == static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                                          SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
#else
    return SDL_GetKeyboardFocus() == window;
#endif
}
CKInputManager *Input() {
    return static_cast<CKInputManager *>(App().State().host.Context()->GetManagerByGuid(INPUT_MANAGER_GUID));
}
int CurvePoints() {
    const XObjectPointerArray &curves = App().State().host.Context()->GetObjectListByType(CKCID_CURVE, TRUE);
    int count = 0;
    for (int i = 0; i < curves.Size(); ++i)
        if (CKCurve *curve = CKCurve::Cast(curves[i])) count += curve->GetControlPointCount();
    return count;
}
template<class F> bool Until(ImGuiTestContext *ctx, F done) {
    const Uint64 deadline = SDL_GetTicks() + 5000;
    for (int frame = 0; frame < 180 && SDL_GetTicks() < deadline; ++frame) {
        if (Read<bool>(ctx, done)) return true;
        ctx->Sleep(0.01f);
    }
    OnMainThread(ctx, [&] {
        ctx->LogInfo("window wait: player=%d full=%d mainFlags=%llx renderFlags=%llx keyboard=%s",
            App().Layout().PlayerMode(), App().Layout().FullScreen(),
            static_cast<unsigned long long>(SDL_GetWindowFlags(EditorWindow())),
            static_cast<unsigned long long>(SDL_GetWindowFlags(RenderWindow())),
            SDL_GetKeyboardFocus() ? SDL_GetWindowTitle(SDL_GetKeyboardFocus()) : "none");
        const auto &log = App().State().eventLog.Entries();
        for (size_t i = log.size() > 6 ? log.size()-6 : 0; i < log.size(); ++i)
            ctx->LogInfo("editor: %s", log[i].text.c_str());
    });
    return false;
}
bool PushKey(SDL_Window *window, SDL_Scancode code, bool down, SDL_Keymod mod = SDL_KMOD_NONE) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = SDL_GetWindowID(window);
    event.key.scancode = code;
    event.key.key = SDL_GetKeyFromScancode(code, mod, true);
    event.key.mod = mod;
    event.key.down = down;
    event.key.timestamp = SDL_GetTicksNS();
    return SDL_PushEvent(&event);
}
bool PushMouse(SDL_Window *window, float x, float y, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = SDL_GetWindowID(window);
    event.button.button = SDL_BUTTON_RIGHT;
    event.button.down = down;
    event.button.x = x; event.button.y = y;
    event.button.timestamp = SDL_GetTicksNS();
    return SDL_PushEvent(&event);
}

// Tests own their second window and restore playback/preferences even after a
// failed assertion. The already-running desktop editor is a separate process.
struct Session {
    ImGuiTestContext *ctx;
    SDL_Window *other = nullptr;
    std::string mode;
    std::string minimize;
    bool automatic = false;
    bool hadMinimize = false;
    explicit Session(ImGuiTestContext *value) : ctx(value) {
        OnMainThread(ctx, [&] {
            auto &p = App().SettingsForTests();
            mode = p.Text("3D Layout\\Player Mode\\Render Mode");
            automatic = p.Bool("3D Layout\\Player Mode\\Auto FullScreen");
            p.SetBool("3D Layout\\Player Mode\\Auto FullScreen", false);
            if (const auto *hint = SDL_GetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS)) {
                minimize = hint;
                hadMinimize = true;
            }
            SDL_ShowWindow(EditorWindow()); SDL_RaiseWindow(EditorWindow());
            // Prepare the focus target before fullscreen starts. Creating and
            // activating it during Cocoa's fullscreen minimization races window
            // creation against the native focus transition under test.
            other = SDL_CreateWindow("Player focus regression", 320, 160, SDL_WINDOW_HIDDEN);
            IM_CHECK_NO_RET(other);
        });
    }
    ~Session() {
        OnMainThread(ctx, [&] {
            auto &editor = (&App().WorkspaceForTests())->Application();
            editor.Execute("runtime.reset", {editor.Scope(), nullptr, 0});
            auto &p = App().SettingsForTests();
            p.Set("3D Layout\\Player Mode\\Render Mode", mode);
            p.SetBool("3D Layout\\Player Mode\\Auto FullScreen", automatic);
            if (other) SDL_DestroyWindow(other);
            if (hadMinimize)
                SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, minimize.c_str());
            else
                SDL_ResetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS);
        });
        ctx->Yield(3);
    }
    void StealFocus(std::atomic<bool> *acquired = nullptr) {
        OnMainThread(ctx, [&] {
            if (other) { SDL_ShowWindow(other); SDL_RaiseWindow(other); }
            // A stale SDL focus flag can suppress a repeated focus-gained event
            // after minimization. Record the actual native transfer as well.
            if (acquired && NativeFocus(other)) *acquired = true;
        });
    }
};

// The engine normally discards SDL backend input. During native window
// transitions, allow that input (including focus events) through its supported
// interactive mode. Restore the flags before returning to simulated UI input.
struct NativeWindowInput {
    ImGuiTestContext *ctx;
    ImGuiTestRunFlags flags;
    int errors;
    explicit NativeWindowInput(ImGuiTestContext *value) : ctx(value), flags(value->RunFlags), errors(value->ErrorCounter) {
        ctx->RunFlags |= ImGuiTestRunFlags_GuiFuncOnly;
    }
    ~NativeWindowInput() {
        ctx->RunFlags = flags;
        // GuiFuncOnly deliberately logs assertions without failing the test.
        // We use it for native input, so retain failures when leaving that mode.
        if (ctx->ErrorCounter > errors) ctx->TestOutput->Status = ImGuiTestStatus_Error;
    }
};

// Count any focus taken back after the other window becomes the foreground
// window, including transitions too short for a frame-by-frame check to see.
struct FocusReturn {
    ImGuiTestContext *ctx;
    SDL_WindowID foreground = 0, editor = 0, render = 0;
    std::atomic<bool> left{false};
    std::atomic<int> returned{0};
    static bool SDLCALL Watch(void *data, SDL_Event *event) {
        auto &self = *static_cast<FocusReturn *>(data);
        if (event->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
            if (event->window.windowID == self.foreground)
                self.left = true;
            else if (self.left && (event->window.windowID == self.editor || event->window.windowID == self.render))
                ++self.returned;
        }
        return true;
    }
    FocusReturn(ImGuiTestContext *value, SDL_Window *other) : ctx(value) {
        OnMainThread(ctx, [&] {
            foreground = SDL_GetWindowID(other);
            editor = SDL_GetWindowID(EditorWindow());
            render = SDL_GetWindowID(RenderWindow());
            SDL_AddEventWatch(Watch, this);
        });
    }
    ~FocusReturn() { OnMainThread(ctx, [&] { SDL_RemoveEventWatch(Watch, this); }); }
};

void MoveNativePointer(ImGuiTestContext *ctx, const ImVec2 &at) {
    // A pushed button event does not move SDL's native pointer. Match both
    // input sources before checking the viewport's mouse coordinates.
    if (!ctx->IsGuiFuncOnly()) ctx->MouseMoveToPos(at);
    OnMainThread(ctx, [=] { SDL_WarpMouseInWindow(EditorWindow(), at.x, at.y); });
    ctx->Yield(3);
}

bool Start(ImGuiTestContext *ctx) {
    ctx->ItemClick(PLAY);
    return Until(ctx, [] { return App().Layout().PlayerMode() && App().DebuggerForTests()->Frames() > 3; });
}

bool ChooseMode(ImGuiTestContext *ctx) {
    return Read<bool>(ctx, [ctx] {
        CKRenderManager *renderer = App().State().host.Context()->GetRenderManager();
        const auto &p = App().SettingsForTests();
        const int driver = vdev::LayoutFullScreenDriver(App().State().host.Context(), p.Text("3D Layout\\Player Mode\\FullScren Driver"));
        const VxDriverDesc *desc = renderer->GetRenderDriverDescription(driver);
        const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(EditorWindow()));
        int chosen = -1;
        float nearestRate = -1.0f;
        for (int i = 0; desc && i < vdev::DisplayModeCount(desc); ++i) {
            const auto &candidate = vdev::DisplayModeAt(desc, i);
            if (candidate.Bpp < 24) continue;
            if (chosen < 0) chosen = i;
            if (desktop && candidate.Width == desktop->w && candidate.Height == desktop->h) {
                const float difference = std::fabs(candidate.RefreshRate - desktop->refresh_rate);
                if (nearestRate < 0.0f || difference < nearestRate) {
                    chosen = i;
                    nearestRate = difference;
                }
            }
        }
        if (chosen < 0) return false;
        const auto &mode = vdev::DisplayModeAt(desc, chosen);
        ctx->LogInfo("Fullscreen mode %dx%dx%d at %d Hz; desktop %dx%d at %.3f Hz",
            mode.Width, mode.Height, mode.Bpp, mode.RefreshRate,
            desktop ? desktop->w : 0, desktop ? desktop->h : 0, desktop ? desktop->refresh_rate : 0.0f);
        App().SettingsForTests().Set("3D Layout\\Player Mode\\Render Mode",
            std::to_string(mode.Width) + "x" + std::to_string(mode.Height) + "x" + std::to_string(mode.Bpp) + "x" + std::to_string(mode.RefreshRate));
        return true;
    });
}
void AltEnter(ImGuiTestContext *ctx, SDL_Window *window) {
    OnMainThread(ctx, [=] {
        PushKey(window, SDL_SCANCODE_LALT, true, SDL_KMOD_ALT);
        PushKey(window, SDL_SCANCODE_RETURN, true, SDL_KMOD_ALT);
        PushKey(window, SDL_SCANCODE_RETURN, false, SDL_KMOD_ALT);
        PushKey(window, SDL_SCANCODE_LALT, false);
    });
    ctx->Yield(3);
}
bool Released(ImGuiTestContext *ctx) {
    return Read<bool>(ctx, [] { return !Input()->IsKeyDown(CKKEY_F6) && !Input()->IsMouseButtonDown(CK_MOUSEBUTTON_RIGHT); });
}

struct Image {
    int width = 0, height = 0;
    std::vector<unsigned char> pixels; // top-down BGRA, independently decoded masks
};
Image RenderedImage(ImGuiTestContext *ctx) {
    return Read<Image>(ctx, [] {
        Image image;
        CKRenderContext *rc = App().Layout().View().RenderContext();
        VxImageDescEx desc;
        const int size = rc ? rc->DumpToMemory(nullptr, VXBUFFER_BACKBUFFER, desc) : 0;
        if (size <= 0) return image;
        std::vector<unsigned char> source(static_cast<size_t>(size));
        desc.Image = source.data();
        if (rc->DumpToMemory(nullptr, VXBUFFER_BACKBUFFER, desc) <= 0 ||
            desc.Width <= 0 || desc.Height <= 0 || desc.BitsPerPixel != 32) return image;
        image.width = desc.Width; image.height = desc.Height;
        image.pixels.resize(static_cast<size_t>(image.width) * image.height * 4);
        const CKDWORD masks[] = {desc.BlueMask, desc.GreenMask, desc.RedMask};
        for (int y = 0; y < image.height; ++y)
            for (int x = 0; x < image.width; ++x) {
                CKDWORD value = 0;
                std::memcpy(&value, source.data() + y * desc.BytesPerLine + x * 4, 4);
                const size_t at = (static_cast<size_t>(y) * image.width + x) * 4;
                for (int c = 0; c < 3; ++c) {
                    CKDWORD mask = masks[c];
                    if (!mask) { image.pixels[at+c] = 0; continue; }
                    int shift = 0;
                    while (!(mask & 1)) { ++shift; mask >>= 1; }
                    image.pixels[at+c] = static_cast<unsigned char>(((value >> shift) & mask) * 255 / mask);
                }
                image.pixels[at+3] = 255;
            }
        return image;
    });
}
bool Nonblank(const Image &image) {
    if (image.pixels.empty()) return false;
    size_t different = 0;
    for (size_t i = 0; i < image.pixels.size(); i += 4)
        if (!std::equal(image.pixels.begin(), image.pixels.begin()+3, image.pixels.begin()+i)) ++different;
    return different > 100;
}
#if defined(_WIN32)
Image DisplayedImage(ImGuiTestContext *ctx) {
    return Read<Image>(ctx, [] {
        Image image;
        HWND window = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(RenderWindow()),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        RECT rect{};
        POINT origin{};
        if (!window || !GetClientRect(window, &rect) || !ClientToScreen(window, &origin)) return image;
        const int width = rect.right, height = rect.bottom;
        HDC screen = GetDC(nullptr), memory = screen ? CreateCompatibleDC(screen) : nullptr;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void *pixels = nullptr;
        HBITMAP bitmap = memory ? CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
        HGDIOBJ previous = bitmap ? SelectObject(memory, bitmap) : nullptr;
        if (bitmap && pixels && BitBlt(memory, 0, 0, width, height, screen, origin.x, origin.y, SRCCOPY | CAPTUREBLT)) {
            GdiFlush();
            image.width = width; image.height = height;
            const auto *bytes = static_cast<const unsigned char *>(pixels);
            image.pixels.assign(bytes, bytes + static_cast<size_t>(width) * height * 4);
            for (size_t at = 3; at < image.pixels.size(); at += 4) image.pixels[at] = 255;
        }
        if (previous) SelectObject(memory, previous);
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        if (screen) ReleaseDC(nullptr, screen);
        return image;
    });
}
bool DisplaysScene(const Image &rendered, const Image &displayed) {
    if (rendered.width != displayed.width || rendered.height != displayed.height || rendered.pixels.empty()) return false;
    size_t bright = 0, visible = 0;
    for (size_t at = 0; at < rendered.pixels.size(); at += 4) {
        if (rendered.pixels[at] + rendered.pixels[at+1] + rendered.pixels[at+2] < 384) continue;
        ++bright;
        if (displayed.pixels[at] + displayed.pixels[at+1] + displayed.pixels[at+2] >= 192) ++visible;
    }
    // Compare the scene's bright regions, so a cursor on a black screen cannot pass.
    return bright > 1000 && visible > bright / 2;
}
#endif
bool SaveImage(const char *name, const Image &image) {
    const auto directory = std::filesystem::u8path(SDL_GetBasePath()) / "test-player";
    std::filesystem::create_directories(directory);
    return vdev::WriteBgraPng((directory / name).u8string(), image.width, image.height, image.pixels.data());
}
} // namespace

void RegisterPlayerTests(ImGuiTestEngine *engine) {
    auto *unavailable = IM_REGISTER_TEST(engine, "playerstate", "unavailable_fullscreen_keeps_both_viewports");
    unavailable->GuiFunc = MainThreadGui;
    unavailable->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        EditorView second = 0;
        std::string mode;
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            second = workspace.Open("layout.viewport", "test.unavailable-fullscreen");
            IM_CHECK_NO_RET(second && workspace.Focus(second));
            mode = App().SettingsForTests().Text("3D Layout\\Player Mode\\Render Mode");
            App().SettingsForTests().Set("3D Layout\\Player Mode\\Render Mode", "1x1x32x1");
            auto &editor = workspace.Application();
            IM_CHECK_NO_RET(editor.Execute("runtime.play", {editor.Scope()}).status == EDITOR_OK);
        });
        ctx->Yield(5);
        const auto token = App().State().document->Token();
        const auto revision = App().State().document->ChangeRevision();
        const auto history = App().State().History().UndoCount();
        const auto redo = App().State().History().CanRedo();
        const auto selection = App().State().objectSelection.References();
        std::array<SDL_GPUTexture *, 2> textures{};
        std::array<CKDWORD, 2> handles{};
        bool hidden = false;
        size_t warnings = 0;
        const auto countWarnings = [] {
            size_t count = 0;
            for (const auto &entry : App().State().eventLog.Entries())
                count += entry.text.find("unavailable fullscreen mode 1x1x32x1") != std::string::npos;
            return count;
        };
        OnMainThread(ctx, [&] {
            int i = 0;
            for (auto *view : {&App().Layout(), App().Layout(second)}) {
                IM_CHECK_NO_RET(view && view->UsesGpuViewport());
                textures[i] = view->GpuViewportTextureForTests()->NativeTexture();
                handles[i++] = view->GpuViewportHandleForTests();
            }
            warnings = countWarnings();
            hidden = (SDL_GetWindowFlags(EditorWindow()) & SDL_WINDOW_HIDDEN) != 0;
        });
        ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_Enter);
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(countWarnings() == warnings + 1);
            int i = 0;
            for (auto *view : {&App().Layout(), App().Layout(second)}) {
                IM_CHECK_NO_RET(view && !view->FullScreen() && view->UsesGpuViewport() &&
                                view->GpuViewportTextureForTests()->NativeTexture() == textures[i] &&
                                view->GpuViewportHandleForTests() == handles[i]);
                ++i;
            }
            IM_CHECK_NO_RET(App().State().document->Token() == token &&
                            App().State().document->ChangeRevision() == revision &&
                            App().State().History().UndoCount() == history &&
                            App().State().History().CanRedo() == redo &&
                            App().State().objectSelection.References() == selection);
            IM_CHECK_NO_RET(((SDL_GetWindowFlags(EditorWindow()) & SDL_WINDOW_HIDDEN) != 0) == hidden);
            auto &editor = App().WorkspaceForTests().Application();
            IM_CHECK_NO_RET(editor.Execute("runtime.reset", {editor.Scope()}).status == EDITOR_OK);
            App().SettingsForTests().Set("3D Layout\\Player Mode\\Render Mode", mode);
            IM_CHECK_NO_RET(App().WorkspaceForTests().Close(second));
        });
    };
    auto *multiple =
        IM_REGISTER_TEST(engine, "player", "second_layout_fullscreen_keeps_first_viewport_and_restores_gpu");
    multiple->GuiFunc = MainThreadGui;
    multiple->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        Session session(ctx);
        EditorView second = 0;
        SDL_GPUTexture *texture = nullptr;
        CKObjectHandle camera{};
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            second = workspace.Open("layout.viewport", "test.player-layout");
            IM_CHECK_NO_RET(second && App().Layout(second));
            workspace.Focus(second);
            auto *image = App().Layout().GpuViewportTextureForTests();
            IM_CHECK_NO_RET(image);
            texture = image->NativeTexture();
            camera = App().State().document->Context()->GetObjectHandle(App().Layout().View().Camera()->GetID());
        });
        ctx->Yield(5);
        IM_CHECK(ChooseMode(ctx));
        IM_CHECK(Start(ctx));
        {
            NativeWindowInput nativeInput(ctx);
            AltEnter(ctx, Read<SDL_Window *>(ctx, [] { return EditorWindow(); }));
            IM_CHECK(Until(ctx, [&] {
                auto *view = App().Layout(second);
                auto *render = view ? view->View().RenderContext() : nullptr;
                return view && view->FullScreen() && !App().Layout().FullScreen() && render &&
                       NativeFocus(static_cast<SDL_Window *>(render->GetWindowHandle()));
            }));
            OnMainThread(ctx, [&] {
                auto &first = App().Layout();
                auto &other = *App().Layout(second);
                auto *context = App().State().document->Context();
                IM_CHECK_NO_RET(context->GetObjectByHandle(camera) == first.View().Camera() &&
                                first.UsesGpuViewport() &&
                                first.GpuViewportTextureForTests()->NativeTexture() == texture);
                IM_CHECK_NO_RET(context->GetPlayerRenderContext() == other.View().RenderContext());
                const auto before = other.View().ReadbackFrames();
                const vdev::EditorClient client(App().WorkspaceForTests().Application().Host(), second);
                const auto taken = client.Command("snapshot.take");
                IM_CHECK_NO_RET(taken.status == EDITOR_OK && other.View().ReadbackFrames() == before + 1);
                vdev::SnapshotData data;
                IM_CHECK_NO_RET(vdev::ReadSnapshots(client, data));
                IM_CHECK_NO_RET(
                    vdev::RemoveSnapshots(client, data, {int(vdev::EditorClient::Integer(taken, "slot"))}).status ==
                    EDITOR_OK);
                IM_CHECK_NO_RET(client.Command("layout.leave-fullscreen").status == EDITOR_OK);
            });
            IM_CHECK(Until(ctx, [&] {
                return !App().Layout(second)->FullScreen() && NativeFocus(EditorWindow()) &&
                       App().Layout(second)->UsesGpuViewport();
            }));
            // Enter again through the same window shortcut, then transfer
            // native focus while keys and a mouse button are still held.
            AltEnter(ctx, Read<SDL_Window *>(ctx, [] { return EditorWindow(); }));
            IM_CHECK(Until(ctx, [&] {
                auto *view = App().Layout(second);
                return view && view->FullScreen() &&
                       NativeFocus(static_cast<SDL_Window *>(view->View().RenderContext()->GetWindowHandle()));
            }));
            OnMainThread(ctx, [&] {
                auto *window = static_cast<SDL_Window *>(App().Layout(second)->View().RenderContext()->GetWindowHandle());
                PushKey(window, SDL_SCANCODE_F6, true);
                PushMouse(window, 100, 100, true);
            });
            IM_CHECK(Until(ctx, [] {
                return Input()->IsKeyDown(CKKEY_F6) && Input()->IsMouseButtonDown(CK_MOUSEBUTTON_RIGHT);
            }));
            session.StealFocus();
            IM_CHECK(Until(ctx, [&] {
                auto *view = App().Layout(second);
                return view && !view->FullScreen() && !view->PlayerMode() && view->UsesGpuViewport() &&
                       NativeFocus(session.other) && !(SDL_GetWindowFlags(EditorWindow()) & SDL_WINDOW_HIDDEN);
            }));
            IM_CHECK(Released(ctx));
            IM_CHECK(App().DebuggerForTests()->GetState() == vdev::Debugger::State::Paused);
        }
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(App().Layout().GpuViewportTextureForTests()->NativeTexture() == texture &&
                            App().State().document->Context()->GetObjectByHandle(camera) ==
                                App().Layout().View().Camera());
            IM_CHECK_NO_RET(App().WorkspaceForTests().Close(second));
        });
    };
    auto *fallback =
        IM_REGISTER_TEST(engine, "playerfallback", "second_layout_fullscreen_fallback_resize_and_reopen");
    fallback->GuiFunc = MainThreadGui;
    fallback->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        Session session(ctx);
        EditorView second = 0;
        SDL_GPUTexture *firstTexture = nullptr;
        CKObjectHandle firstCamera{};
        uint64_t firstReads = 0, firstUploads = 0, firstCreations = 0;
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            const vdev::EditorClient client(workspace.Application().Host(), 0);
            const auto made = client.Command("object.create-frame3d");
            const auto *created = vdev::EditorClient::Find(made, "created");
            IM_CHECK_NO_RET(made.status == EDITOR_OK && created && created->object);
            const auto selected = created->object;
            IM_CHECK_NO_RET(client.Command("object.create-frame3d").status == EDITOR_OK);
            IM_CHECK_NO_RET(client.Command("history.undo").status == EDITOR_OK);
            IM_CHECK_NO_RET(client.Command("selection.replace", {vdev::ObjectsArgument("objects", {selected})}).status <=
                            EDITOR_NO_CHANGE);
            IM_CHECK_NO_RET(App().State().History().UndoCount() > 0 && App().State().History().CanRedo() &&
                            !App().State().objectSelection.References().empty());
            second = workspace.Open("layout.viewport", "test.player-fallback");
            IM_CHECK_NO_RET(second && workspace.Focus(second));
        });
        ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto *window = ImGui::FindWindowByName(App().WorkspaceForTests().WindowName(second).c_str());
            IM_CHECK_NO_RET(window);
            ImGui::SetWindowDock(window, 0, ImGuiCond_Always);
            ImGui::SetWindowPos(window, {540, 90});
            ImGui::SetWindowSize(window, {600, 440});
        });
        ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &first = App().Layout();
            IM_CHECK_NO_RET(first.UsesGpuViewport() && App().Layout(second)->UsesGpuViewport());
            firstTexture = first.GpuViewportTextureForTests()->NativeTexture();
            firstCamera = App().State().document->Context()->GetObjectHandle(first.View().Camera()->GetID());
            firstReads = first.View().ReadbackFrames();
            firstUploads = first.ViewportUploadsForTests();
            firstCreations = first.GpuViewportCreationsForTests();
        });
        const auto token = App().State().document->Token();
        const auto history = App().State().History().UndoCount();
        const auto redo = App().State().History().CanRedo();
        const auto selection = App().State().objectSelection.References();
        IM_CHECK(ChooseMode(ctx));
        IM_CHECK(Start(ctx));
        {
            NativeWindowInput nativeInput(ctx);
            AltEnter(ctx, Read<SDL_Window *>(ctx, [] { return EditorWindow(); }));
            IM_CHECK(Until(ctx, [&] {
                auto *view = App().Layout(second);
                return view && view->FullScreen() &&
                       NativeFocus(static_cast<SDL_Window *>(view->View().RenderContext()->GetWindowHandle()));
            }));
            OnMainThread(ctx, [&] {
                // Fail the first imported target after returning to the editor.
                // The normal per-Context selection must latch CPU fallback.
                App().Layout(second)->FailNextGpuViewportForTests();
                const vdev::EditorClient client(App().WorkspaceForTests().Application().Host(), second);
                IM_CHECK_NO_RET(client.Command("layout.leave-fullscreen").status == EDITOR_OK);
            });
            IM_CHECK(Until(ctx, [&] {
                auto *view = App().Layout(second);
                return view && !view->FullScreen() && !view->UsesGpuViewport() &&
                       view->ViewportUploadsForTests() > 0 && NativeFocus(EditorWindow());
            }));
        }
        uint64_t reads = 0, uploads = 0, selections = 0, creations = 0;
        OnMainThread(ctx, [&] {
            auto &view = *App().Layout(second);
            reads = view.View().ReadbackFrames();
            uploads = view.ViewportUploadsForTests();
            selections = view.GpuViewportSelectionsForTests();
            creations = view.GpuViewportCreationsForTests();
            auto *window = ImGui::FindWindowByName(App().WorkspaceForTests().WindowName(second).c_str());
            IM_CHECK_NO_RET(window);
            ImGui::SetWindowSize(window, {640, 470});
        });
        ctx->Yield(8);
        OnMainThread(ctx, [&] {
            auto &view = *App().Layout(second);
            IM_CHECK_NO_RET(!view.UsesGpuViewport() && view.View().ReadbackFrames() > reads &&
                            view.ViewportUploadsForTests() > uploads &&
                            view.GpuViewportSelectionsForTests() == selections &&
                            view.GpuViewportCreationsForTests() == creations);
            Image displayed;
            IM_CHECK_NO_RET(vdev::MainRenderer().ReadFrame(displayed.pixels, displayed.width, displayed.height));
            for (size_t at = 0; at < displayed.pixels.size(); at += 4)
                std::swap(displayed.pixels[at], displayed.pixels[at + 2]);
            IM_CHECK_NO_RET(SaveImage("second-layout-fallback.png", displayed));
            auto &workspace = App().WorkspaceForTests();
            const auto closed = second;
            IM_CHECK_NO_RET(workspace.Close(closed));
            const vdev::EditorClient old(workspace.Application().Host(), closed);
            IM_CHECK_NO_RET(old.Query("layout.input").status == EDITOR_STALE);
            second = workspace.Open("layout.viewport", "test.player-fallback");
            IM_CHECK_NO_RET(second && second != closed && workspace.Focus(second));
        });
        ctx->Yield(8);
        OnMainThread(ctx, [&] {
            auto &first = App().Layout();
            IM_CHECK_NO_RET(App().Layout(second)->UsesGpuViewport() && first.UsesGpuViewport() &&
                            first.GpuViewportTextureForTests()->NativeTexture() == firstTexture &&
                            first.GpuViewportCreationsForTests() == firstCreations &&
                            first.View().ReadbackFrames() == firstReads && first.ViewportUploadsForTests() == firstUploads &&
                            App().State().document->Context()->GetObjectByHandle(firstCamera) == first.View().Camera());
            IM_CHECK_NO_RET(App().State().document->Token() == token &&
                            App().State().History().UndoCount() == history && App().State().History().CanRedo() == redo &&
                            App().State().objectSelection.References() == selection);
            IM_CHECK_NO_RET(App().WorkspaceForTests().Close(second));
        });
    };
    ImGuiTest *queued = IM_REGISTER_TEST(engine, "player", "fullscreen_delayed_focus_loss_and_close");
    queued->GuiFunc = MainThreadGui;
    queued->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        Session session(ctx);
        IM_CHECK(ChooseMode(ctx));
        IM_CHECK(Start(ctx));
        NativeWindowInput nativeInput(ctx);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0"));
            IM_CHECK_NO_RET(vdev::EditorClient((&App().WorkspaceForTests())->Application().Host(), 0)
                                .Command("layout.toggle-fullscreen").status == EDITOR_OK);
        });
        IM_CHECK(Until(ctx, [] { return App().Layout().FullScreen() && NativeFocus(RenderWindow()); }));
        const auto send = [&](SDL_EventType type) {
            OnMainThread(ctx, [&] {
                SDL_Event event{};
                event.type = type;
                event.window.windowID = SDL_GetWindowID(RenderWindow());
                event.window.timestamp = SDL_GetTicksNS();
                IM_CHECK_NO_RET(SDL_PushEvent(&event));
            });
            ctx->Yield(3);
        };
        // The native window has already regained focus when this old event
        // reaches the application's deferred fullscreen commands.
        send(SDL_EVENT_WINDOW_FOCUS_LOST);
        IM_CHECK(Read<bool>(ctx, [] {
            return App().Layout().FullScreen() && App().Layout().PlayerMode() && NativeFocus(RenderWindow());
        }));
        // Close must still pause and exit even though the window has focus.
        send(SDL_EVENT_WINDOW_CLOSE_REQUESTED);
        IM_CHECK(Until(ctx, [] { return !App().Layout().FullScreen() && !App().Layout().PlayerMode(); }));
        IM_CHECK(Read<bool>(ctx, [] {
            return !(SDL_GetWindowFlags(EditorWindow()) & SDL_WINDOW_HIDDEN) &&
                   (SDL_GetWindowFlags(RenderWindow()) & SDL_WINDOW_HIDDEN) &&
                   !(SDL_GetWindowFlags(RenderWindow()) & SDL_WINDOW_NOT_FOCUSABLE);
        }));
    };

    ImGuiTest *focus = IM_REGISTER_TEST(engine, "player", "fullscreen_focus_loss_keeps_foreground");
    focus->GuiFunc = MainThreadGui;
    focus->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        Session session(ctx);
        IM_CHECK(ChooseMode(ctx));
        IM_CHECK(Start(ctx));
        NativeWindowInput nativeInput(ctx);
        for (int cycle = 0; cycle < 6; ++cycle) {
            ctx->LogInfo("focus loss cycle %d, minimize=%d", cycle, cycle >= 3);
            OnMainThread(ctx, [&] {
                IM_CHECK_NO_RET(SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, cycle >= 3 ? "1" : "0"));
            });
            if (cycle) {
                const int frames = App().DebuggerForTests()->Frames();
                OnMainThread(ctx, [&] {
                    IM_CHECK_NO_RET(vdev::EditorClient((&App().WorkspaceForTests())->Application().Host(), 0)
                                        .Command("runtime.play").status == EDITOR_OK);
                });
                IM_CHECK(Until(ctx, [&] {
                    return App().Layout().PlayerMode() && App().DebuggerForTests()->Frames() > frames + 3;
                }));
            }
            OnMainThread(ctx, [&] {
                const vdev::EditorClient client((&App().WorkspaceForTests())->Application().Host(), 0);
                IM_CHECK_NO_RET(client.Command("layout.toggle-fullscreen").status == EDITOR_OK);
            });
            IM_CHECK(Until(ctx, [] { return App().Layout().FullScreen() && SDL_GetKeyboardFocus() == RenderWindow(); }));
            OnMainThread(ctx, [] {
                PushKey(RenderWindow(), SDL_SCANCODE_F6, true);
                PushMouse(RenderWindow(), 100, 100, true);
            });
            IM_CHECK(Until(ctx, [] {
                return Input()->IsKeyDown(CKKEY_F6) && Input()->IsMouseButtonDown(CK_MOUSEBUTTON_RIGHT);
            }));
            const size_t steps = App().State().History().UndoCount();
            const bool dirty = App().State().document->Dirty();
            const auto selection = Read<std::vector<vdev::ObjectRef>>(ctx, [] {
                const vdev::EditorClient client((&App().WorkspaceForTests())->Application().Host(), 0);
                const auto result = client.Query("selection.objects");
                const auto *objects = vdev::EditorClient::Find(result, "objects");
                return objects ? objects->objects : std::vector<vdev::ObjectRef>{};
            });
            const uint64_t reads = App().Layout().View().ReadbackFrames();
            const uint64_t uploads = App().Layout().ViewportUploadsForTests();
            FocusReturn focus(ctx, session.other);
            session.StealFocus(&focus.left);
            IM_CHECK(Until(ctx, [] { return !App().Layout().FullScreen() && !App().Layout().PlayerMode(); }));
            IM_CHECK(Until(ctx, [&] { return NativeFocus(session.other); }));
            ctx->Sleep(0.15f);
            IM_CHECK(focus.left.load());
            IM_CHECK_EQ(focus.returned.load(), 0);
            IM_CHECK(Read<bool>(ctx, [&] {
                return NativeFocus(session.other) && !NativeFocus(EditorWindow()) && !NativeFocus(RenderWindow()) &&
                       !(SDL_GetWindowFlags(EditorWindow()) & SDL_WINDOW_HIDDEN) &&
                       (SDL_GetWindowFlags(RenderWindow()) & SDL_WINDOW_HIDDEN) &&
                       !(SDL_GetWindowFlags(RenderWindow()) & SDL_WINDOW_NOT_FOCUSABLE) &&
                       !SDL_GetWindowMouseGrab(RenderWindow());
            }));
            IM_CHECK(Released(ctx));
            IM_CHECK(App().DebuggerForTests()->GetState() == vdev::Debugger::State::Paused);
            const int frames = App().DebuggerForTests()->Frames();
            IM_CHECK(Until(ctx, [] { return App().Layout().UsesGpuViewport(); }));
            const uint64_t allocations = App().Layout().GpuViewportCreationsForTests();
            ctx->Yield(12);
            IM_CHECK_EQ(App().DebuggerForTests()->Frames(), frames);
            OnMainThread(ctx, [&] {
                const vdev::EditorClient client((&App().WorkspaceForTests())->Application().Host(), 0);
                const auto result = client.Query("selection.objects");
                const auto *objects = vdev::EditorClient::Find(result, "objects");
                IM_CHECK_NO_RET(objects && objects->objects == selection && App().State().History().UndoCount() == steps &&
                                App().State().document->Dirty() == dirty);
                IM_CHECK_NO_RET(App().Layout().View().ReadbackFrames() == reads &&
                                App().Layout().ViewportUploadsForTests() == uploads &&
                                App().Layout().GpuViewportCreationsForTests() == allocations);
                IM_CHECK_NO_RET(NativeFocus(session.other));
            });
            OnMainThread(ctx, [&] {
                SDL_HideWindow(session.other);
                SDL_RaiseWindow(EditorWindow());
            });
            IM_CHECK(Until(ctx, [] { return SDL_GetKeyboardFocus() == EditorWindow(); }));
        }
    };

    ImGuiTest *t = IM_REGISTER_TEST(engine, "player", "windowed_input_focus");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        Session session(ctx);
        IM_CHECK(Until(ctx, [] { return SDL_GetKeyboardFocus() == EditorWindow(); }));
        IM_CHECK(Start(ctx));
        {
            NativeWindowInput nativeInput(ctx);
            // The unchanged Add Control Point sample runs its Key Event -> Add
            // Control Point Script on Space. Assert the native curve relation,
            // not only the input manager's key state or the frame count.
            const int points = Read<int>(ctx, CurvePoints);
            OnMainThread(ctx, [] { PushKey(EditorWindow(), SDL_SCANCODE_SPACE, true); });
            const bool reacted = Until(ctx, [=] { return CurvePoints() > points; });
            OnMainThread(ctx, [] { PushKey(EditorWindow(), SDL_SCANCODE_SPACE, false); });
            IM_CHECK(reacted);
            ImVec2 min, max;
            IM_CHECK(App().Layout().ShownView(min, max));
            const ImVec2 at = Center(min, max);
            MoveNativePointer(ctx, at);
            OnMainThread(ctx, [&] {
                PushKey(EditorWindow(), SDL_SCANCODE_F6, true);
                PushMouse(EditorWindow(), at.x, at.y, true);
            });
            ctx->Yield(3);
            IM_CHECK(Until(
                ctx, [] { return Input()->IsKeyDown(CKKEY_F6) && Input()->IsMouseButtonDown(CK_MOUSEBUTTON_RIGHT); }));
            const Vx2DVector mouse = Read<Vx2DVector>(ctx, [] {
                Vx2DVector p;
                Input()->GetMousePosition(p, FALSE);
                return p;
            });
            IM_CHECK(std::fabs(mouse.x - (at.x - min.x)) < 2 && std::fabs(mouse.y - (at.y - min.y)) < 2);
            session.StealFocus();
            IM_CHECK(Until(ctx, [&] { return SDL_GetKeyboardFocus() == session.other; }));
            IM_CHECK(Until(ctx, [] { return App().DebuggerForTests()->GetState() == vdev::Debugger::State::Paused; }));
            const int frames = App().DebuggerForTests()->Frames();
            ctx->Yield(12);
            IM_CHECK_EQ(App().DebuggerForTests()->Frames(), frames);
            IM_CHECK(Read<bool>(ctx, [] { return !App().Layout().PlayerMode(); }));
            IM_CHECK(Released(ctx));
            // Closing the other window returns focus through the window manager;
            // RaiseWindow alone may be refused by desktop focus-stealing policy.
            OnMainThread(ctx, [&] {
                SDL_HideWindow(session.other);
                SDL_RaiseWindow(EditorWindow());
            });
            IM_CHECK(Until(ctx, [] { return SDL_GetKeyboardFocus() == EditorWindow(); }));
            ctx->Yield(5);
            IM_CHECK_EQ(App().DebuggerForTests()->Frames(), frames);
        }
        IM_CHECK(Start(ctx));
        IM_CHECK(Released(ctx));
    };

    t = IM_REGISTER_TEST(engine, "player", "fullscreen_default_mode");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        Session session(ctx);
        const bool available = Read<bool>(ctx, [] {
            const vdev::Preferences defaults;
            auto &p = App().SettingsForTests();
            p.Set("3D Layout\\Player Mode\\Render Mode", defaults.Text("3D Layout\\Player Mode\\Render Mode"));
            CKContext *context = App().State().host.Context();
            const int driver = vdev::LayoutFullScreenDriver(context, p.Text("3D Layout\\Player Mode\\FullScren Driver"));
            VxDisplayMode mode;
            return vdev::LayoutFullScreenMode(context, driver, p.Text("3D Layout\\Player Mode\\Render Mode"), mode);
        });
        IM_CHECK(Start(ctx));
        NativeWindowInput nativeInput(ctx);
        const size_t logged = App().State().eventLog.Entries().size();
        AltEnter(ctx, Read<SDL_Window *>(ctx, [] { return EditorWindow(); }));
        if (available) {
            IM_CHECK(Until(ctx, [] { return App().Layout().FullScreen() && SDL_GetKeyboardFocus() == RenderWindow(); }));
            ctx->Yield(12);
            const Image image = RenderedImage(ctx);
            IM_CHECK(Nonblank(image));
            IM_CHECK(SaveImage("fullscreen-default.png", image));
#if defined(_WIN32)
            const Image displayed = DisplayedImage(ctx);
            IM_CHECK(SaveImage("fullscreen-default-desktop.png", displayed));
            IM_CHECK(DisplaysScene(image, displayed));
#endif
            AltEnter(ctx, Read<SDL_Window *>(ctx, [] { return RenderWindow(); }));
            IM_CHECK(Until(ctx, [] { return !App().Layout().FullScreen() && SDL_GetKeyboardFocus() == EditorWindow(); }));
        } else {
            IM_CHECK(Read<bool>(ctx, [] { return !App().Layout().FullScreen(); }));
            bool warned = false;
            for (size_t i = logged; i < App().State().eventLog.Entries().size(); ++i)
                warned |= App().State().eventLog.Entries()[i].text.find("unavailable fullscreen mode") != std::string::npos;
            IM_CHECK(warned);
            IM_CHECK(Read<bool>(ctx, [] { return !(SDL_GetWindowFlags(EditorWindow()) & SDL_WINDOW_HIDDEN); }));
        }
    };

    t = IM_REGISTER_TEST(engine, "player", "fullscreen_roundtrip_focus");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        Session session(ctx);
        IM_CHECK(ChooseMode(ctx));
        IM_CHECK(Start(ctx));
        NativeWindowInput nativeInput(ctx);
        // A failed release must leave the editor and its imported target intact.
        if (Read<bool>(ctx, [] { return App().Layout().UsesGpuViewport(); })) {
            OnMainThread(ctx, [&] {
                auto &layout = App().Layout();
                const auto texture = layout.GpuViewportTextureForTests();
                const auto handle = layout.GpuViewportHandleForTests();
                const auto history = App().State().History().UndoCount();
                const auto selected = App().State().objectSelection.References();
                layout.FailNextGpuViewportReleaseForTests();
                const auto result = vdev::EditorClient((&App().WorkspaceForTests())->Application().Host(), 0)
                                        .Command("layout.toggle-fullscreen");
                IM_CHECK(result.status == EDITOR_ERROR && !layout.FullScreen());
                IM_CHECK(layout.GpuViewportTextureForTests() == texture &&
                         layout.GpuViewportHandleForTests() == handle);
                IM_CHECK(!(SDL_GetWindowFlags(EditorWindow()) & SDL_WINDOW_HIDDEN));
                IM_CHECK(App().State().History().UndoCount() == history &&
                         App().State().objectSelection.References() == selected);
            });
        }
        int width = 0, height = 0;
        OnMainThread(ctx, [&] { SDL_GetWindowSize(EditorWindow(), &width, &height); });
        for (int cycle = 0; cycle < 3; ++cycle) {
            AltEnter(ctx, Read<SDL_Window *>(ctx, [] { return EditorWindow(); }));
            IM_CHECK(Until(ctx, [] { return App().Layout().FullScreen() && SDL_GetKeyboardFocus() == RenderWindow(); }));
            IM_CHECK(Read<bool>(ctx, [] { return (SDL_GetWindowFlags(RenderWindow()) & SDL_WINDOW_FULLSCREEN) != 0; }));
            const int frames = App().DebuggerForTests()->Frames();
            ctx->Yield(6);
            IM_CHECK_GT(App().DebuggerForTests()->Frames(), frames);
            // Check sustained presentation before any readback can synchronize
            // the render queue or change the backbuffer being inspected.
            if (cycle == 0) {
                ctx->Yield(600);
                IM_CHECK_GT(App().DebuggerForTests()->Frames(), frames + 6);
            }
#if defined(_WIN32)
            const Image beforeReadback = DisplayedImage(ctx);
            IM_CHECK(SaveImage("fullscreen-before-readback.png", beforeReadback));
#endif
            const Image full = RenderedImage(ctx);
            IM_CHECK(Nonblank(full));
            IM_CHECK(SaveImage("fullscreen.png", full));
            if (cycle == 0)
                OnMainThread(ctx, [&] {
                    const auto client = vdev::EditorClient((&App().WorkspaceForTests())->Application().Host(), 0);
                    vdev::SnapshotData data;
                    IM_CHECK(vdev::ReadSnapshots(client, data));
                    const auto slot = data.FirstFree();
                    IM_CHECK(slot >= 0);
                    const auto reads = App().Layout().View().ReadbackFrames();
                    const auto history = App().State().History().UndoCount();
                    IM_CHECK(client.Command("snapshot.take").status == EDITOR_OK);
                    IM_CHECK(App().Layout().View().ReadbackFrames() == reads + 1);
                    IM_CHECK(vdev::ReadSnapshots(client, data) && data.Has(slot));
                    IM_CHECK(data.Pixels(slot).size() ==
                             size_t(vdev::SnapshotData::WIDTH * vdev::SnapshotData::HEIGHT * 4));
                    IM_CHECK(vdev::RemoveSnapshots(client, data, {slot}).status == EDITOR_OK);
                    IM_CHECK(App().State().History().UndoCount() == history);
                });
#if defined(_WIN32)
            IM_CHECK(DisplaysScene(full, beforeReadback));
            const Image displayed = DisplayedImage(ctx);
            IM_CHECK(SaveImage("fullscreen-desktop.png", displayed));
            IM_CHECK(DisplaysScene(full, displayed));
#endif
            if (cycle == 0) {
                // The hidden editor still submits UI frames during fullscreen.
                // Keep it running long enough to expose an unbounded GPU queue.
                ctx->Yield(600);
                const Image running = RenderedImage(ctx);
                IM_CHECK(Nonblank(running));
#if defined(_WIN32)
                const Image screen = DisplayedImage(ctx);
                IM_CHECK(SaveImage("fullscreen-desktop-running.png", screen));
                IM_CHECK(DisplaysScene(running, screen));
#endif
            }
            const int points = Read<int>(ctx, CurvePoints);
            OnMainThread(ctx, [] { PushKey(RenderWindow(), SDL_SCANCODE_SPACE, true); });
            const bool reacted = Until(ctx, [=] { return CurvePoints() > points; });
            OnMainThread(ctx, [] { PushKey(RenderWindow(), SDL_SCANCODE_SPACE, false); });
            IM_CHECK(reacted);
            const Image changed = RenderedImage(ctx);
            IM_CHECK(changed.width == full.width && changed.height == full.height && changed.pixels != full.pixels);
            IM_CHECK(SaveImage("fullscreen-after-input.png", changed));
            AltEnter(ctx, Read<SDL_Window *>(ctx, [] { return RenderWindow(); }));
            IM_CHECK(Until(ctx, [] { return !App().Layout().FullScreen() && SDL_GetKeyboardFocus() == EditorWindow(); }));
            IM_CHECK(Read<bool>(ctx, [] { return App().Layout().PlayerMode(); }));
            IM_CHECK(Read<bool>(ctx, [=] {
                int w = 0, h = 0; SDL_GetWindowSize(EditorWindow(), &w, &h);
                return w == width && h == height && (SDL_GetWindowFlags(RenderWindow()) & SDL_WINDOW_HIDDEN) &&
                       !SDL_GetWindowMouseGrab(RenderWindow());
            }));
            IM_CHECK(Released(ctx));
            ImVec2 min, max;
            IM_CHECK(App().Layout().ShownView(min, max));
            const ImVec2 at = Center(min, max);
            MoveNativePointer(ctx, at);
            OnMainThread(ctx, [&] {
                PushKey(EditorWindow(), SDL_SCANCODE_F6, true);
                PushMouse(EditorWindow(), at.x, at.y, true);
            });
            ctx->Yield(3);
            IM_CHECK(Until(
                ctx, [] { return Input()->IsKeyDown(CKKEY_F6) && Input()->IsMouseButtonDown(CK_MOUSEBUTTON_RIGHT); }));
            const Vx2DVector mouse = Read<Vx2DVector>(ctx, [] { Vx2DVector p; Input()->GetMousePosition(p, FALSE); return p; });
            IM_CHECK(std::fabs(mouse.x - (at.x - min.x)) < 2 && std::fabs(mouse.y - (at.y - min.y)) < 2);
            OnMainThread(ctx, [&] {
                PushKey(EditorWindow(), SDL_SCANCODE_F6, false);
                PushMouse(EditorWindow(), at.x, at.y, false);
            });
            ctx->Yield(3);
            IM_CHECK(Released(ctx));
        }
        AltEnter(ctx, Read<SDL_Window *>(ctx, [] { return EditorWindow(); }));
        IM_CHECK(Until(ctx, [] { return App().Layout().FullScreen() && SDL_GetKeyboardFocus() == RenderWindow(); }));
        OnMainThread(ctx, [] { PushKey(RenderWindow(), SDL_SCANCODE_F6, true); PushMouse(RenderWindow(), 100, 100, true); });
        ctx->Yield(3);
        IM_CHECK(Until(
            ctx, [] { return Input()->IsKeyDown(CKKEY_F6) && Input()->IsMouseButtonDown(CK_MOUSEBUTTON_RIGHT); }));
        session.StealFocus();
        IM_CHECK(Until(ctx, [] { return !App().Layout().FullScreen() && !App().Layout().PlayerMode(); }));
        IM_CHECK(App().DebuggerForTests()->GetState() == vdev::Debugger::State::Paused);
        IM_CHECK(Released(ctx));
        IM_CHECK(Until(ctx, [&] {
            return NativeFocus(session.other) && !(SDL_GetWindowFlags(EditorWindow()) & SDL_WINDOW_HIDDEN);
        }));
        const int paused = App().DebuggerForTests()->Frames();
        ctx->Yield(12);
        IM_CHECK_EQ(App().DebuggerForTests()->Frames(), paused);
    };

    t = IM_REGISTER_TEST(engine, "player", "snapshot_take_capture_cancel");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        Session session(ctx);
        InView entity;
        IM_CHECK(SelectEntity(ctx, entity));
        const bool dirty = App().State().document->Dirty();
        const size_t steps = App().State().History().UndoCount();
        const VxVector position = Position(ctx, entity.entity);
        const Image before = RenderedImage(ctx);
        IM_CHECK(Nonblank(before));
        IM_CHECK(SaveImage("windowed.png", before));
        const int slot = Read<vdev::SnapshotData>(ctx, Snapshots).FirstFree();
        IM_CHECK_GE(slot, 0);
        ctx->ItemClick("//3D Layout/$$56/##tool");
        ctx->WindowMove("//Snapshot Buffer", ImVec2(20, 20));
        ctx->ItemClick("//Snapshot Buffer/Take");
        ctx->Yield(3);
        IM_CHECK(Read<vdev::SnapshotData>(ctx, Snapshots).Has(slot));
        const auto expected = vdev::ShrinkImage(before.pixels.data(), before.width, before.height, before.width * 4,
            vdev::SnapshotBuffer::WIDTH, vdev::SnapshotBuffer::HEIGHT);
        IM_CHECK(Read<vdev::SnapshotData>(ctx, Snapshots).Pixels(slot) == expected);
        IM_CHECK(SaveImage("snapshot-take.png", {vdev::SnapshotBuffer::WIDTH, vdev::SnapshotBuffer::HEIGHT, expected}));
        const int next = Read<vdev::SnapshotData>(ctx, Snapshots).FirstFree();
        ctx->ItemClick("//Snapshot Buffer/Capture");
        ctx->Yield(3);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK_EQ(Read<vdev::SnapshotData>(ctx, Snapshots).FirstFree(), next);
        ctx->ItemClick("//Snapshot Buffer/Capture");
        ctx->Yield(3);
        ImVec2 min, max;
        IM_CHECK(App().Layout().ShownView(min, max));
        // The buffer was moved clear of the view before Capture was armed.
        ctx->MouseMoveToPos(ImVec2(std::floor((min.x + max.x) * 0.5f), std::floor((min.y + max.y) * 0.5f)));
        const ImVec2 at = ImGui::GetIO().MousePos;
        ctx->MouseClick(0);
        ctx->Yield(3);
        IM_CHECK(Read<vdev::SnapshotData>(ctx, Snapshots).Has(next));
        const auto crop = vdev::CaptureArea(before.pixels.data(), before.width, before.height, before.width * 4,
            static_cast<int>(at.x - min.x), static_cast<int>(at.y - min.y));
        IM_CHECK(Read<vdev::SnapshotData>(ctx, Snapshots).Pixels(next) == crop);
        IM_CHECK(SaveImage("snapshot-capture.png", {vdev::SnapshotBuffer::WIDTH, vdev::SnapshotBuffer::HEIGHT, crop}));
        ctx->ItemClick("//Snapshot Buffer/Close");
        const VxVector after = Position(ctx, entity.entity);
        IM_CHECK(position.x == after.x && position.y == after.y && position.z == after.z);
        IM_CHECK_EQ(App().State().document->Dirty(), dirty);
        IM_CHECK_EQ(App().State().History().UndoCount(), steps);
        OnMainThread(ctx, [=] {
            vdev::RemoveSnapshots(vdev::EditorClient((&App().WorkspaceForTests())->Application().Host(), 0), Snapshots(),
                                  {slot, next});
        });
    };
}
} // namespace vdevuitest
