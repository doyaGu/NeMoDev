#include "tests/ui/UiTests.h"

#include "ui/render/UiRenderer.h"

#include "CKRasterizer.h"
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "imgui_internal.h"

namespace vdevuitest {
namespace {

void Background(CKDWORD color) {
    CKScene *scene = App().State().document->Context()->GetCurrentLevel()->GetLevelScene();
    scene->SetBackgroundColor(color);
    scene->ApplyEnvironmentSettings();
}

bool Pixel(const std::vector<unsigned char> &image, int width, int x, int y,
           const std::array<unsigned char, 4> &rgba) {
    const size_t at = (size_t(y) * width + x) * 4;
    return at + 4 <= image.size() &&
           std::equal(rgba.begin(), rgba.end(), image.begin() + at);
}

SDL_Window *MainWindow() {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    return viewport && viewport->PlatformHandle
        ? SDL_GetWindowFromID(static_cast<SDL_WindowID>(
              reinterpret_cast<uintptr_t>(viewport->PlatformHandle)))
        : nullptr;
}

} // namespace

void RegisterGpuViewportTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = IM_REGISTER_TEST(
        engine, "app", "gpu_viewport_reuses_one_imported_texture");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        vdev::UiTexture *texture = nullptr;
        SDL_GPUTexture *native = nullptr;
        CKDWORD handle = 0;
        std::uint64_t readbacks = 0, uploads = 0, creations = 0,
                      selections = 0, releases = 0;
        OnMainThread(ctx, [&] {
            auto &layout = App().Layout();
            IM_CHECK(layout.UsesGpuViewport());
            texture = layout.GpuViewportTextureForTests();
            native = texture ? texture->NativeTexture() : nullptr;
            handle = layout.GpuViewportHandleForTests();
            auto *rasterizer = layout.View().RasterizerContext();
            CKTextureDesc desc;
            IM_CHECK(texture && native && handle && rasterizer &&
                     rasterizer->GetTextureDesc(handle, &desc));
            IM_CHECK(desc.Format.Width == texture->Width() &&
                     desc.Format.Height == texture->Height() &&
                     desc.MipMapCount == 1 && desc.Depth == 1 &&
                     (desc.Flags & CKRST_TEXTURE_RENDERTARGET));
            readbacks = layout.View().ReadbackFrames();
            uploads = layout.ViewportUploadsForTests();
            creations = layout.GpuViewportCreationsForTests();
            selections = layout.GpuViewportSelectionsForTests();
            releases = layout.GpuViewportReleasesForTests();
        });
        ctx->Yield(12);
        OnMainThread(ctx, [&] {
            auto &layout = App().Layout();
            IM_CHECK(layout.UsesGpuViewport());
            IM_CHECK(layout.GpuViewportTextureForTests() == texture);
            IM_CHECK(texture && texture->NativeTexture() == native);
            IM_CHECK(layout.GpuViewportHandleForTests() == handle);
            IM_CHECK(layout.View().ReadbackFrames() == readbacks);
            IM_CHECK(layout.ViewportUploadsForTests() == uploads);
            IM_CHECK(layout.GpuViewportCreationsForTests() == creations);
            IM_CHECK(layout.GpuViewportSelectionsForTests() == selections);
            IM_CHECK(layout.GpuViewportReleasesForTests() == releases);
        });
    };

    t = IM_REGISTER_TEST(engine, "app", "gpu_viewport_is_opaque");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        CKDWORD previous = 0;
        OnMainThread(ctx, [&] {
            CKScene *scene = App().State().document->Context()
                                 ->GetCurrentLevel()->GetLevelScene();
            previous = scene->GetBackgroundColor();
            Background(0x001234A6u); // Exercise the opaque-output contract.
        });
        ctx->Yield(6);
        OnMainThread(ctx, [&] {
            auto *texture = App().Layout().GpuViewportTextureForTests();
            std::vector<unsigned char> pixels;
            IM_CHECK(texture &&
                     vdev::MainRenderer().ReadTextureForTests(*texture, pixels));
            IM_CHECK(Pixel(pixels, texture->Width(), 4, 4,
                           {0x12, 0x34, 0xA6, 0xFF}));
            bool opaque = true;
            for (size_t i = 3; i < pixels.size(); i += 4)
                opaque = opaque && pixels[i] == 0xFF;
            IM_CHECK(opaque);
            Background(previous);
        });
    };

    t = IM_REGISTER_TEST(engine, "app", "gpu_viewport_resize_rebuilds_once");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        SDL_Window *window = nullptr;
        int width = 0, height = 0;
        std::uint64_t creations = 0, selections = 0, readbacks = 0,
                      uploads = 0;
        OnMainThread(ctx, [&] {
            auto &layout = App().Layout();
            window = MainWindow();
            IM_CHECK(window && SDL_GetWindowSize(window, &width, &height));
            creations = layout.GpuViewportCreationsForTests();
            selections = layout.GpuViewportSelectionsForTests();
            readbacks = layout.View().ReadbackFrames();
            uploads = layout.ViewportUploadsForTests();
            IM_CHECK(SDL_SetWindowSize(window, width + 80, height + 40));
        });
        ctx->Yield(8);
        OnMainThread(ctx, [&] {
            auto &layout = App().Layout();
            IM_CHECK(layout.UsesGpuViewport());
            IM_CHECK(layout.GpuViewportCreationsForTests() == creations + 1);
            IM_CHECK(layout.GpuViewportSelectionsForTests() == selections);
            IM_CHECK(layout.View().ReadbackFrames() == readbacks);
            IM_CHECK(layout.ViewportUploadsForTests() == uploads);
            creations = layout.GpuViewportCreationsForTests();
        });
        ctx->Yield(8);
        OnMainThread(ctx, [&] {
            IM_CHECK(App().Layout().GpuViewportCreationsForTests() == creations);
            IM_CHECK(SDL_SetWindowSize(window, width, height));
        });
        ctx->Yield(6);
    };

    t = IM_REGISTER_TEST(
        engine, "app", "gpu_viewport_snapshot_reads_only_on_request");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        const CKDWORD background = Read<CKDWORD>(ctx, [] {
            CKScene *scene = App().State().document->Context()
                                 ->GetCurrentLevel()->GetLevelScene();
            const CKDWORD old = scene->GetBackgroundColor();
            Background(0xFF3279B1u);
            return old;
        });
        ctx->Yield(5);
        const auto before = Read<std::uint64_t>(ctx, [] {
            return App().Layout().View().ReadbackFrames();
        });
        const int slot = Read<int>(ctx, [] {
            return App().State().snapshots.FirstFree();
        });
        IM_CHECK(slot >= 0);
        ctx->ItemClick("//3D Layout/$$56/##tool");
        ctx->ItemClick("//Snapshot Buffer/Take");
        IM_CHECK(Read<bool>(ctx, [&] {
            return App().State().snapshots.Has(slot) &&
                   App().Layout().View().ReadbackFrames() == before + 1 &&
                   Pixel(App().State().snapshots.Pixels(slot),
                         vdev::SnapshotBuffer::WIDTH, 1, 1,
                         {0xB1, 0x79, 0x32, 0xFF});
        }));
        ctx->Yield(10);
        IM_CHECK_EQ(Read<std::uint64_t>(ctx, [] {
            return App().Layout().View().ReadbackFrames();
        }), before + 1);
        ctx->ItemClick("//Snapshot Buffer/Close");
        OnMainThread(ctx, [slot, background] {
            App().State().snapshots.Remove(slot);
            Background(background);
        });
    };

    t = IM_REGISTER_TEST(
        engine, "app", "gpu_viewport_failure_latches_cpu_fallback");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        std::uint64_t creations = 0, selections = 0, readbacks = 0,
                      uploads = 0;
        OnMainThread(ctx, [&] {
            auto &layout = App().Layout();
            IM_CHECK(layout.UsesGpuViewport());
            creations = layout.GpuViewportCreationsForTests();
            selections = layout.GpuViewportSelectionsForTests();
            readbacks = layout.View().ReadbackFrames();
            uploads = layout.ViewportUploadsForTests();
            layout.FailNextGpuViewportForTests();
            IM_CHECK(layout.RecreateGpuViewportForTests());
        });
        ctx->Yield(7);
        OnMainThread(ctx, [&] {
            auto &layout = App().Layout();
            IM_CHECK(!layout.UsesGpuViewport());
            IM_CHECK(layout.GpuViewportCreationsForTests() == creations);
            IM_CHECK(layout.GpuViewportSelectionsForTests() == selections);
            IM_CHECK(layout.View().ReadbackFrames() > readbacks);
            IM_CHECK(layout.ViewportUploadsForTests() > uploads);
            readbacks = layout.View().ReadbackFrames();
            uploads = layout.ViewportUploadsForTests();
        });
        ctx->Yield(7);
        OnMainThread(ctx, [&] {
            auto &layout = App().Layout();
            IM_CHECK(!layout.UsesGpuViewport());
            IM_CHECK(layout.GpuViewportCreationsForTests() == creations);
            IM_CHECK(layout.GpuViewportSelectionsForTests() == selections);
            IM_CHECK(layout.View().ReadbackFrames() > readbacks);
            IM_CHECK(layout.ViewportUploadsForTests() > uploads);
        });
        OnMainThread(ctx, [&] { App().Layout().RetryGpuViewportForTests(); });
        ctx->Yield(5);
        IM_CHECK(Read<bool>(ctx, [] { return App().Layout().UsesGpuViewport(); }));
    };

    t = IM_REGISTER_TEST(
        engine, "app", "gpu_viewport_release_failure_preserves_owner");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        vdev::UiTexture *texture = nullptr;
        CKDWORD handle = 0;
        std::uint64_t selections = 0;
        OnMainThread(ctx, [&] {
            auto &layout = App().Layout();
            IM_CHECK(layout.UsesGpuViewport());
            texture = layout.GpuViewportTextureForTests();
            handle = layout.GpuViewportHandleForTests();
            selections = layout.GpuViewportSelectionsForTests();
            layout.FailNextGpuViewportReleaseForTests();
            layout.RetryGpuViewportForTests();
            IM_CHECK(layout.GpuViewportTextureForTests() == texture);
            IM_CHECK(layout.GpuViewportHandleForTests() == handle);
            IM_CHECK(layout.GpuViewportSelectionsForTests() == selections);
            // Consume the injected failure, then restore the normal selected
            // path so this negative test leaves no resource behind.
            layout.RetryGpuViewportForTests();
        });
        ctx->Yield(4);
        IM_CHECK(Read<bool>(ctx, [] {
            return App().Layout().UsesGpuViewport();
        }));
    };
}

} // namespace vdevuitest
