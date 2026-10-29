#include "tests/ui/UiTests.h"
#include "tests/ui/ViewportScheduling.h"

#include "ui/render/UiRenderer.h"

#include <SDL3/SDL.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace vdevuitest {
namespace {

int Setting(const char *name, int fallback, int minimum, int maximum) {
    const char *text = SDL_getenv(name);
    if (!text) return fallback;
    char *end = nullptr;
    const long value = std::strtol(text, &end, 10);
    return end != text && !*end && value >= minimum && value <= maximum ? static_cast<int>(value) : 0;
}

bool MakeScene() {
    auto &document = *App().State().document;
    CKContext *context = document.Context();
    CKLevel *level = context->GetCurrentLevel();
    CKScene *scene = level ? level->GetLevelScene() : nullptr;
    if (!scene) return false;
    auto *material = CKMaterial::Cast(context->CreateObject(CKCID_MATERIAL, (CKSTRING) "Baseline material"));
    auto *mesh = CKMesh::Cast(context->CreateObject(CKCID_MESH, (CKSTRING) "Baseline cube"));
    auto *camera = CKCamera::Cast(context->CreateObject(CKCID_CAMERA, (CKSTRING) "Baseline camera"));
    if (!material || !mesh || !camera) return false;
    material->SetDiffuse(VxColor(0.4f, 0.7f, 0.9f, 1.0f));
    material->SetTwoSided(TRUE);
    mesh->SetVertexCount(8);
    mesh->SetLitMode(VX_PRELITMESH);
    VxVector vertices[] = {{-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
                           {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1}};
    for (int i = 0; i < 8; ++i) {
        mesh->SetVertexPosition(i, &vertices[i]);
        mesh->SetVertexColor(i, 0xFF66B3E6u);
    }
    const int faces[][3] = {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},
                            {2,3,7},{2,7,6},{0,4,7},{0,7,3},{1,2,6},{1,6,5}};
    mesh->SetFaceCount(12);
    for (int i = 0; i < 12; ++i) {
        mesh->SetFaceVertexIndex(i, faces[i][0], faces[i][1], faces[i][2]);
        mesh->SetFaceMaterial(i, material);
    }
    std::vector<CK_ID> objects = {mesh->GetID(), material->GetID(), camera->GetID()};
    for (int row = 0; row < 8; ++row)
        for (int column = 0; column < 8; ++column) {
            auto *entity = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Baseline instance"));
            if (!entity) return false;
            entity->SetCurrentMesh(mesh);
            VxVector position((column - 3.5f) * 3.0f, (row - 3.5f) * 3.0f, 0.0f);
            entity->SetPosition(&position);
            level->AddObject(entity);
            scene->AddObject(entity);
            objects.push_back(entity->GetID());
        }
    VxVector position(0.0f, 0.0f, -45.0f), target(0.0f, 0.0f, 0.0f);
    camera->SetPosition(&position);
    camera->LookAt(&target);
    camera->SetFov(0.78f);
    camera->SetFrontPlane(1.0f);
    camera->SetBackPlane(200.0f);
    camera->SetAspectRatio(4, 3);
    level->AddObject(camera);
    scene->AddObject(camera);
    scene->SetBackgroundColor(0xFF1B2430u);
    scene->ApplyEnvironmentSettings();
    document.AdoptObjects(objects);
    App().Layout().View().SetCamera(camera->GetID());
    return true;
}

struct Counters {
    std::uint64_t readbacks, uploads, creations, selections, releases;
};

Counters Counts() {
    auto &layout = App().Layout();
    return {layout.View().ReadbackFrames(), layout.ViewportUploadsForTests(),
            layout.GpuViewportCreationsForTests(), layout.GpuViewportSelectionsForTests(),
            layout.GpuViewportReleasesForTests()};
}

std::string JsonString(const char *text) {
    std::string result = "\"";
    for (const unsigned char *at = reinterpret_cast<const unsigned char *>(text); *at; ++at) {
        if (*at == '"' || *at == '\\') { result += '\\'; result += static_cast<char>(*at); }
        else if (*at < 32) {
            char escaped[7];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", *at);
            result += escaped;
        } else result += static_cast<char>(*at);
    }
    return result + '"';
}

} // namespace

void RegisterViewportBaselineTests(ImGuiTestEngine *engine) {
    auto *test = IM_REGISTER_TEST(engine, "viewportbaseline", "release_cubes64");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        const int samples = Setting("NEMODEV_BASELINE_SAMPLES", 11, 11, 101);
        const int frames = Setting("NEMODEV_BASELINE_FRAMES", 240, 30, 10000);
        const int warmup = Setting("NEMODEV_BASELINE_WARMUP", 480, 60, 10000);
        const bool paired = Setting("NEMODEV_BASELINE_PAIRED", 0, 0, 1) == 1;
        IM_CHECK(samples && frames && warmup);
        ViewportScheduling scheduling;
        Settle(ctx, "//3D Layout");
        OnMainThread(ctx, [&] {
            auto *viewport = ImGui::GetMainViewport();
            SDL_Window *window = SDL_GetWindowFromID(static_cast<SDL_WindowID>(
                reinterpret_cast<uintptr_t>(viewport->PlatformHandle)));
            IM_CHECK(window && SDL_SetWindowSize(window, 1280, 800));
            IM_CHECK(MakeScene());
            IM_CHECK(scheduling.Start());
        });
        const Uint64 warmupStart = SDL_GetTicksNS();
        ctx->Yield(warmup);
        while (SDL_GetTicksNS() - warmupStart < 15000000000ull) ctx->Yield(60);
        vdev::UiTexture *texture = nullptr;
        int width = 0, height = 0;
        OnMainThread(ctx, [&] {
            auto &layout = App().Layout();
            IM_CHECK(layout.UsesGpuViewport());
            texture = layout.GpuViewportTextureForTests();
            IM_CHECK(texture);
            width = texture->Width();
            height = texture->Height();
        });
        struct Sample { int frames; double milliseconds; };
        std::vector<Sample> measured;
        measured.reserve(samples);
        for (int sample = 0; sample < samples; ++sample) {
            Uint64 start = 0;
            int firstFrame = 0;
            Counters before{};
            OnMainThread(ctx, [&] {
                IM_CHECK(SDL_WaitForGPUIdle(vdev::MainRenderer().Device()));
                if (paired) {
                    // Pause while the other program renders its group. The
                    // GPU is idle and time/frame counting has not started.
                    std::printf("NEMODEV_VIEWPORT_READY %d\n", sample);
                    std::fflush(stdout);
                    IM_CHECK(std::getchar() == '\n');
                }
                before = Counts();
                firstFrame = ImGui::GetFrameCount();
                start = SDL_GetPerformanceCounter();
            });
            do {
                ctx->Yield(frames);
            } while (double(SDL_GetPerformanceCounter() - start) / double(SDL_GetPerformanceFrequency()) < 1.0);
            OnMainThread(ctx, [&] {
                IM_CHECK(SDL_WaitForGPUIdle(vdev::MainRenderer().Device()));
                const double elapsed = double(SDL_GetPerformanceCounter() - start) * 1000.0 /
                                       double(SDL_GetPerformanceFrequency());
                auto &layout = App().Layout();
                const Counters after = Counts();
                IM_CHECK(layout.UsesGpuViewport() && layout.GpuViewportTextureForTests() == texture);
                IM_CHECK(texture->Width() == width && texture->Height() == height);
                IM_CHECK(after.readbacks == before.readbacks && after.uploads == before.uploads);
                IM_CHECK(after.creations == before.creations && after.selections == before.selections &&
                         after.releases == before.releases);
                VxStats stats{};
                layout.View().RenderContext()->GetStats(&stats);
                IM_CHECK(stats.NbObjectDrawn >= 64 && stats.NbTrianglesDrawn >= 768);
                measured.push_back({ImGui::GetFrameCount() - firstFrame, elapsed});
            });
        }
        IM_CHECK(static_cast<int>(measured.size()) == samples);
        OnMainThread(ctx, [&] {
            // One explicit verification readback, outside every timed group.
            std::vector<unsigned char> pixels;
            IM_CHECK(vdev::MainRenderer().ReadTextureForTests(*texture, pixels));
            size_t changed = 0;
            bool opaque = true;
            for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
                opaque = opaque && pixels[i + 3] == 255;
                if (pixels[i] != 0x1B || pixels[i + 1] != 0x24 || pixels[i + 2] != 0x30) ++changed;
            }
            IM_CHECK(opaque && changed > 1000);
            SDL_GPUDevice *device = vdev::MainRenderer().Device();
            const SDL_PropertiesID properties = SDL_GetGPUDeviceProperties(device);
            const auto name = JsonString(SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown"));
            const auto driverInfo = JsonString(SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING, ""));
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            SDL_Window *window = SDL_GetWindowFromID(static_cast<SDL_WindowID>(
                reinterpret_cast<uintptr_t>(viewport->PlatformHandle)));
            int pixelWidth = 0, pixelHeight = 0;
            IM_CHECK(window && SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight));
            std::printf("NEMODEV_VIEWPORT_BASELINE {\"version\":2,\"scenario\":\"cubes64-v1\","
                        "\"scope\":\"editor_frame_gpu_drained\",\"driver\":\"%s\","
                        "\"group_control\":\"%s\","
                        "\"cpu_policy\":\"%s\","
                        "\"gpu_device\":%s,\"gpu_driver_info\":%s,\"window_pixels\":[%d,%d],"
                        "\"sdl_version\":%d,\"viewport\":[%d,%d],\"window\":[1280,800],"
                        "\"ui_scale\":%.6f,\"warmup_frames\":%d,\"requested_frames\":%d,"
                        "\"minimum_warmup_ms\":15000,\"minimum_group_ms\":1000,"
                        "\"verification_readbacks\":1,\"stable_readbacks\":0,\"stable_uploads\":0,"
                        "\"stable_target_creations\":0,\"stable_path_selections\":0,"
                        "\"stable_target_releases\":0,\"samples\":[",
                        SDL_GetGPUDeviceDriver(device), paired ? "paired" : "continuous", scheduling.Name(),
                        name.c_str(), driverInfo.c_str(),
                        pixelWidth, pixelHeight, SDL_GetVersion(), width, height,
                        ImGui::GetStyle().FontScaleDpi, warmup, frames);
            for (size_t i = 0; i < measured.size(); ++i)
                std::printf("%s{\"frames\":%d,\"elapsed_ms\":%.9f}", i ? "," : "",
                            measured[i].frames, measured[i].milliseconds);
            std::puts("]}");
            IM_CHECK(scheduling.Restore());
        });
    };
}

} // namespace vdevuitest
