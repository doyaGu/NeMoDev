#include "tests/ui/UiTests.h"
#include "core/runtime/SdkCompat.h"
#include "ui/render/UiRenderer.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace vdevuitest {
namespace {
constexpr const char *PLAY = "//##StatusBar/play/##bitmap";

SDL_Window *EditorWindow() {
    return SDL_GetWindowFromID(static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(ImGui::GetMainViewport()->PlatformHandle)));
}
SDL_Window *RenderWindow() {
    CKRenderContext *rc = App().Layout().View().RenderContext();
    return rc ? static_cast<SDL_Window *>(rc->GetWindowHandle()) : nullptr;
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
        const auto &log = App().State().log;
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
    bool automatic = false;
    explicit Session(ImGuiTestContext *value) : ctx(value) {
        OnMainThread(ctx, [&] {
            auto &p = App().State().preferences;
            mode = p.Text("3D Layout\\Player Mode\\Render Mode");
            automatic = p.Bool("3D Layout\\Player Mode\\Auto FullScreen");
            p.SetBool("3D Layout\\Player Mode\\Auto FullScreen", false);
            SDL_ShowWindow(EditorWindow()); SDL_RaiseWindow(EditorWindow());
        });
    }
    ~Session() {
        OnMainThread(ctx, [&] {
            App().State().debugger->Reset();
            auto &p = App().State().preferences;
            p.Set("3D Layout\\Player Mode\\Render Mode", mode);
            p.SetBool("3D Layout\\Player Mode\\Auto FullScreen", automatic);
            if (other) SDL_DestroyWindow(other);
        });
        ctx->Yield(3);
    }
    void StealFocus() {
        OnMainThread(ctx, [&] {
            if (!other) other = SDL_CreateWindow("Player focus regression", 320, 160, 0);
            if (other) { SDL_ShowWindow(other); SDL_RaiseWindow(other); }
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

void MoveNativePointer(ImGuiTestContext *ctx, const ImVec2 &at) {
    // A pushed button event does not move SDL's native pointer. Match both
    // input sources before checking the viewport's mouse coordinates.
    if (!ctx->IsGuiFuncOnly()) ctx->MouseMoveToPos(at);
    OnMainThread(ctx, [=] { SDL_WarpMouseInWindow(EditorWindow(), at.x, at.y); });
    ctx->Yield(3);
}

bool Start(ImGuiTestContext *ctx) {
    ctx->ItemClick(PLAY);
    return Until(ctx, [] { return App().Layout().PlayerMode() && App().State().debugger->Frames() > 3; });
}

bool ChooseMode(ImGuiTestContext *ctx) {
    return Read<bool>(ctx, [ctx] {
        CKRenderManager *renderer = App().State().host.Context()->GetRenderManager();
        const auto &p = App().State().preferences;
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
        App().State().preferences.Set("3D Layout\\Player Mode\\Render Mode",
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
    ImGuiTest *t = IM_REGISTER_TEST(engine, "player", "windowed_input_focus");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        Session session(ctx);
        IM_CHECK(Until(ctx, [] { return SDL_GetKeyboardFocus() == EditorWindow(); }));
        IM_CHECK(Start(ctx));
        // The unchanged Add Control Point sample runs its Key Event -> Add
        // Control Point Script on Space. Assert the native curve relation,
        // not only the input manager's key state or the frame count.
        const int points = Read<int>(ctx, CurvePoints);
        OnMainThread(ctx, [] { PushKey(EditorWindow(), SDL_SCANCODE_SPACE, true); });
        ctx->Yield(3);
        OnMainThread(ctx, [] { PushKey(EditorWindow(), SDL_SCANCODE_SPACE, false); });
        IM_CHECK(Until(ctx, [=] { return CurvePoints() > points; }));
        ImVec2 min, max;
        IM_CHECK(App().Layout().ShownView(min, max));
        const ImVec2 at = Center(min, max);
        MoveNativePointer(ctx, at);
        OnMainThread(ctx, [&] {
            PushKey(EditorWindow(), SDL_SCANCODE_F6, true);
            PushMouse(EditorWindow(), at.x, at.y, true);
        });
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, [] { return Input()->IsKeyDown(CKKEY_F6) && Input()->IsMouseButtonDown(CK_MOUSEBUTTON_RIGHT); }));
        const Vx2DVector mouse = Read<Vx2DVector>(ctx, [] { Vx2DVector p; Input()->GetMousePosition(p, FALSE); return p; });
        IM_CHECK(std::fabs(mouse.x - (at.x - min.x)) < 2 && std::fabs(mouse.y - (at.y - min.y)) < 2);
        session.StealFocus();
        IM_CHECK(Until(ctx, [&] { return SDL_GetKeyboardFocus() == session.other; }));
        IM_CHECK(Until(ctx, [] { return App().State().debugger->GetState() == vdev::Debugger::State::Paused; }));
        const int frames = App().State().debugger->Frames();
        ctx->Yield(12);
        IM_CHECK_EQ(App().State().debugger->Frames(), frames);
        IM_CHECK(!App().Layout().PlayerMode());
        IM_CHECK(Released(ctx));
        // Closing the other window returns focus through the window manager;
        // RaiseWindow alone may be refused by desktop focus-stealing policy.
        OnMainThread(ctx, [&] { SDL_HideWindow(session.other); SDL_RaiseWindow(EditorWindow()); });
        IM_CHECK(Until(ctx, [] { return SDL_GetKeyboardFocus() == EditorWindow(); }));
        ctx->Yield(5);
        IM_CHECK_EQ(App().State().debugger->Frames(), frames);
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
            auto &p = App().State().preferences;
            p.Set("3D Layout\\Player Mode\\Render Mode", defaults.Text("3D Layout\\Player Mode\\Render Mode"));
            CKContext *context = App().State().host.Context();
            const int driver = vdev::LayoutFullScreenDriver(context, p.Text("3D Layout\\Player Mode\\FullScren Driver"));
            VxDisplayMode mode;
            return vdev::LayoutFullScreenMode(context, driver, p.Text("3D Layout\\Player Mode\\Render Mode"), mode);
        });
        IM_CHECK(Start(ctx));
        NativeWindowInput nativeInput(ctx);
        const size_t logged = App().State().log.size();
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
            IM_CHECK(!App().Layout().FullScreen());
            bool warned = false;
            for (size_t i = logged; i < App().State().log.size(); ++i)
                warned |= App().State().log[i].text.find("unavailable fullscreen mode") != std::string::npos;
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
        int width = 0, height = 0;
        OnMainThread(ctx, [&] { SDL_GetWindowSize(EditorWindow(), &width, &height); });
        for (int cycle = 0; cycle < 3; ++cycle) {
            AltEnter(ctx, Read<SDL_Window *>(ctx, [] { return EditorWindow(); }));
            IM_CHECK(Until(ctx, [] { return App().Layout().FullScreen() && SDL_GetKeyboardFocus() == RenderWindow(); }));
            IM_CHECK(Read<bool>(ctx, [] { return (SDL_GetWindowFlags(RenderWindow()) & SDL_WINDOW_FULLSCREEN) != 0; }));
            const int frames = App().State().debugger->Frames();
            ctx->Yield(6);
            IM_CHECK_GT(App().State().debugger->Frames(), frames);
            // Check sustained presentation before any readback can synchronize
            // the render queue or change the backbuffer being inspected.
            if (cycle == 0) {
                ctx->Yield(600);
                IM_CHECK_GT(App().State().debugger->Frames(), frames + 6);
            }
#if defined(_WIN32)
            const Image beforeReadback = DisplayedImage(ctx);
            IM_CHECK(SaveImage("fullscreen-before-readback.png", beforeReadback));
#endif
            const Image full = RenderedImage(ctx);
            IM_CHECK(Nonblank(full));
            IM_CHECK(SaveImage("fullscreen.png", full));
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
            ctx->Yield(3);
            OnMainThread(ctx, [] { PushKey(RenderWindow(), SDL_SCANCODE_SPACE, false); });
            IM_CHECK(Until(ctx, [=] { return CurvePoints() > points; }));
            const Image changed = RenderedImage(ctx);
            IM_CHECK(changed.width == full.width && changed.height == full.height && changed.pixels != full.pixels);
            IM_CHECK(SaveImage("fullscreen-after-input.png", changed));
            AltEnter(ctx, Read<SDL_Window *>(ctx, [] { return RenderWindow(); }));
            IM_CHECK(Until(ctx, [] { return !App().Layout().FullScreen() && SDL_GetKeyboardFocus() == EditorWindow(); }));
            IM_CHECK(App().Layout().PlayerMode());
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
            IM_CHECK(Read<bool>(ctx, [] { return Input()->IsKeyDown(CKKEY_F6) && Input()->IsMouseButtonDown(CK_MOUSEBUTTON_RIGHT); }));
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
        IM_CHECK(Read<bool>(ctx, [] { return Input()->IsKeyDown(CKKEY_F6) && Input()->IsMouseButtonDown(CK_MOUSEBUTTON_RIGHT); }));
        session.StealFocus();
        IM_CHECK(Until(ctx, [] { return !App().Layout().FullScreen() && !App().Layout().PlayerMode(); }));
        IM_CHECK(App().State().debugger->GetState() == vdev::Debugger::State::Paused);
        IM_CHECK(Released(ctx));
        IM_CHECK(Read<bool>(ctx, [&] { return SDL_GetKeyboardFocus() == session.other &&
            !(SDL_GetWindowFlags(EditorWindow()) & SDL_WINDOW_HIDDEN); }));
        const int paused = App().State().debugger->Frames();
        ctx->Yield(12);
        IM_CHECK_EQ(App().State().debugger->Frames(), paused);
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
        const int slot = App().State().snapshots.FirstFree();
        IM_CHECK_GE(slot, 0);
        ctx->ItemClick("//3D Layout/$$56/##tool");
        ctx->WindowMove("//Snapshot Buffer", ImVec2(20, 20));
        ctx->ItemClick("//Snapshot Buffer/Take");
        ctx->Yield(3);
        IM_CHECK(App().State().snapshots.Has(slot));
        const auto expected = vdev::ShrinkImage(before.pixels.data(), before.width, before.height, before.width * 4,
            vdev::SnapshotBuffer::WIDTH, vdev::SnapshotBuffer::HEIGHT);
        IM_CHECK(App().State().snapshots.Pixels(slot) == expected);
        IM_CHECK(SaveImage("snapshot-take.png", {vdev::SnapshotBuffer::WIDTH, vdev::SnapshotBuffer::HEIGHT, expected}));
        const int next = App().State().snapshots.FirstFree();
        ctx->ItemClick("//Snapshot Buffer/Capture");
        ctx->Yield(3);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().snapshots.FirstFree(), next);
        ctx->ItemClick("//Snapshot Buffer/Capture");
        ctx->Yield(3);
        ImVec2 min, max;
        IM_CHECK(App().Layout().ShownView(min, max));
        // The buffer was moved clear of the view before Capture was armed.
        ctx->MouseMoveToPos(ImVec2(std::floor((min.x + max.x) * 0.5f), std::floor((min.y + max.y) * 0.5f)));
        const ImVec2 at = ImGui::GetIO().MousePos;
        ctx->MouseClick(0);
        ctx->Yield(3);
        IM_CHECK(App().State().snapshots.Has(next));
        const auto crop = vdev::CaptureArea(before.pixels.data(), before.width, before.height, before.width * 4,
            static_cast<int>(at.x - min.x), static_cast<int>(at.y - min.y));
        IM_CHECK(App().State().snapshots.Pixels(next) == crop);
        IM_CHECK(SaveImage("snapshot-capture.png", {vdev::SnapshotBuffer::WIDTH, vdev::SnapshotBuffer::HEIGHT, crop}));
        ctx->ItemClick("//Snapshot Buffer/Close");
        const VxVector after = Position(ctx, entity.entity);
        IM_CHECK(position.x == after.x && position.y == after.y && position.z == after.z);
        IM_CHECK_EQ(App().State().document->Dirty(), dirty);
        IM_CHECK_EQ(App().State().History().UndoCount(), steps);
        OnMainThread(ctx, [=] { App().State().snapshots.Remove(slot); App().State().snapshots.Remove(next); });
    };
}
} // namespace vdevuitest
