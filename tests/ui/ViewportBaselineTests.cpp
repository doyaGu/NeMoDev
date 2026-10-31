#include "tests/ui/UiTests.h"
#include "tests/ui/ViewportScheduling.h"

#include "ui/render/UiRenderer.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
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

class ViewportWatchdog {
public:
    ViewportWatchdog(ImGuiTestEngineIO &io, float duration) : m_Io(io),
        m_Warning(io.ConfigWatchdogWarning), m_Kill(io.ConfigWatchdogKillTest) {
        io.ConfigWatchdogWarning = std::max(m_Warning, duration + 30.0f);
        io.ConfigWatchdogKillTest = std::max(m_Kill, duration + 60.0f);
    }
    ViewportWatchdog(const ViewportWatchdog &) = delete;
    ViewportWatchdog &operator=(const ViewportWatchdog &) = delete;
    ~ViewportWatchdog() {
        m_Io.ConfigWatchdogWarning = m_Warning;
        m_Io.ConfigWatchdogKillTest = m_Kill;
    }
private:
    ImGuiTestEngineIO &m_Io;
    float m_Warning, m_Kill;
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
        const int warmupMilliseconds = Setting("NEMODEV_BASELINE_WARMUP_MS", 15000, 15000, 120000);
        const int groupMilliseconds = Setting("NEMODEV_BASELINE_GROUP_MS", 1000, 1000, 10000);
        const bool paired = Setting("NEMODEV_BASELINE_PAIRED", 0, 0, 1) == 1;
        IM_CHECK(samples && frames && warmup && warmupMilliseconds && groupMilliseconds);
        // A paired test also waits for the other program's warmup and groups.
        // Keep a finite limit around the requested complete measurement.
        ViewportWatchdog watchdog(*ctx->EngineIO, (paired ? 2.0f : 1.0f) *
            (warmupMilliseconds + samples * groupMilliseconds + ViewportScheduling::MaximumCooldownMilliseconds) / 1000.0f);
        ViewportScheduling scheduling;
        Settle(ctx, "//3D Layout");
        OnMainThread(ctx, [&] {
            auto *viewport = ImGui::GetMainViewport();
            SDL_Window *window = SDL_GetWindowFromID(static_cast<SDL_WindowID>(
                reinterpret_cast<uintptr_t>(viewport->PlatformHandle)));
            IM_CHECK(window && SDL_SetWindowSize(window, 1280, 800));
            IM_CHECK(MakeScene());
            IM_CHECK(scheduling.Start());
            // Let the device cool without rendering or counting idle time as
            // work. Every measured group is retained even if heat returns.
            IM_CHECK(SDL_WaitForGPUIdle(vdev::MainRenderer().Device()));
            IM_CHECK(scheduling.WaitForNominal());
        });
        const Uint64 warmupStart = SDL_GetTicksNS();
        ctx->Yield(warmup);
        while (SDL_GetTicksNS() - warmupStart < Uint64(warmupMilliseconds) * 1000000ull) ctx->Yield(60);
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
        struct Sample {
            int frames;
            double milliseconds, drainMilliseconds, idleMilliseconds;
            ViewportScheduling::Usage before, after;
            bool usage;
            double testCpuMilliseconds = 0;
            int testQos = 0;
        };
        std::vector<Sample> measured;
        measured.reserve(samples);
        for (int sample = 0; sample < samples; ++sample) {
            Uint64 start = 0;
            int firstFrame = 0;
            Counters before{};
            ViewportScheduling::Usage usageBefore{};
            bool haveUsage = false;
            double idleMilliseconds = 0;
            ViewportScheduling::Usage testUsageBefore{};
            const bool testUsage = scheduling.Snapshot(testUsageBefore);
            OnMainThread(ctx, [&] {
                IM_CHECK(SDL_WaitForGPUIdle(vdev::MainRenderer().Device()));
                if (paired) {
                    // Pause while the other program renders its group. The
                    // GPU is idle and time/frame counting has not started.
                    std::printf("NEMODEV_VIEWPORT_READY %d\n", sample);
                    std::fflush(stdout);
                    const auto idleStart = SDL_GetPerformanceCounter();
                    IM_CHECK(std::getchar() == '\n');
                    idleMilliseconds = double(SDL_GetPerformanceCounter() - idleStart) * 1000.0 /
                                       double(SDL_GetPerformanceFrequency());
                }
                before = Counts();
                firstFrame = ImGui::GetFrameCount();
                haveUsage = scheduling.Snapshot(usageBefore);
                start = SDL_GetPerformanceCounter();
            });
            do {
                ctx->Yield(frames);
            } while (double(SDL_GetPerformanceCounter() - start) * 1000.0 /
                     double(SDL_GetPerformanceFrequency()) < groupMilliseconds);
            OnMainThread(ctx, [&] {
                const auto drainStart = SDL_GetPerformanceCounter();
                IM_CHECK(SDL_WaitForGPUIdle(vdev::MainRenderer().Device()));
                const auto end = SDL_GetPerformanceCounter();
                const double elapsed = double(end - start) * 1000.0 /
                                       double(SDL_GetPerformanceFrequency());
                const double drain = double(end - drainStart) * 1000.0 / double(SDL_GetPerformanceFrequency());
                ViewportScheduling::Usage usageAfter{};
                const bool usage = haveUsage && scheduling.Snapshot(usageAfter);
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
                measured.push_back({ImGui::GetFrameCount() - firstFrame, elapsed, drain, idleMilliseconds,
                                    usageBefore, usageAfter, usage});
            });
            IM_CHECK(measured.size() == size_t(sample + 1));
            ViewportScheduling::Usage testUsageAfter{};
            if (testUsage && scheduling.Snapshot(testUsageAfter)) {
                measured.back().testCpuMilliseconds = double(testUsageAfter.threadNanoseconds - testUsageBefore.threadNanoseconds) / 1e6;
                measured.back().testQos = testUsageBefore.qos;
            }
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
            std::printf("NEMODEV_VIEWPORT_BASELINE {\"version\":3,\"scenario\":\"cubes64-v1\","
                        "\"scope\":\"editor_frame_gpu_drained\",\"driver\":\"%s\","
                        "\"group_control\":\"%s\","
                        "\"cpu_policy\":\"%s\","
                        "\"thermal_policy\":\"%s\",\"minimum_cooldown_ms\":%d,\"maximum_cooldown_ms\":%d,\"cooldown_ms\":%.3f,"
                        "\"gpu_device\":%s,\"gpu_driver_info\":%s,\"window_pixels\":[%d,%d],"
                        "\"sdl_version\":%d,\"viewport\":[%d,%d],\"window\":[1280,800],"
                        "\"ui_scale\":%.6f,\"warmup_frames\":%d,\"requested_frames\":%d,"
                        "\"minimum_warmup_ms\":%d,\"minimum_group_ms\":%d,"
                        "\"verification_readbacks\":1,\"stable_readbacks\":0,\"stable_uploads\":0,"
                        "\"stable_target_creations\":0,\"stable_path_selections\":0,"
                        "\"stable_target_releases\":0,\"samples\":[",
                        SDL_GetGPUDeviceDriver(device), paired ? "paired" : "continuous", scheduling.Name(),
                        scheduling.ThermalPolicy(), scheduling.MinimumCooldownMilliseconds,
                        scheduling.MaximumCooldownMilliseconds, scheduling.CooldownMilliseconds(),
                        name.c_str(), driverInfo.c_str(),
                        pixelWidth, pixelHeight, SDL_GetVersion(), width, height,
                        ImGui::GetStyle().FontScaleDpi, warmup, frames, warmupMilliseconds, groupMilliseconds);
            for (size_t i = 0; i < measured.size(); ++i) {
                const auto &sample = measured[i];
                std::printf("%s{\"frames\":%d,\"elapsed_ms\":%.9f,\"drain_ms\":%.9f,\"idle_ms\":%.9f,"
                            "\"cpu_usage_available\":%s,\"thread_cpu_ms\":%.9f,\"process_cpu_ms\":%.9f,"
                            "\"voluntary_switches\":%llu,\"involuntary_switches\":%llu,"
                            "\"qos_before\":%d,\"qos_after\":%d,\"priority_before\":%d,\"priority_after\":%d,"
                            "\"foreground_before\":%s,\"foreground_after\":%s,"
                            "\"thermal_before\":%d,\"thermal_after\":%d,\"low_power_before\":%s,\"low_power_after\":%s,"
                            "\"task_role_before\":%d,\"task_role_after\":%d,"
                            "\"test_cpu_ms\":%.9f,\"test_qos\":%d}", i ? "," : "",
                            sample.frames, sample.milliseconds, sample.drainMilliseconds, sample.idleMilliseconds,
                            sample.usage ? "true" : "false",
                            sample.usage ? double(sample.after.threadNanoseconds - sample.before.threadNanoseconds) / 1e6 : 0,
                            sample.usage ? double(sample.after.processMicroseconds - sample.before.processMicroseconds) / 1e3 : 0,
                            sample.usage ? static_cast<unsigned long long>(sample.after.voluntarySwitches - sample.before.voluntarySwitches) : 0,
                            sample.usage ? static_cast<unsigned long long>(sample.after.involuntarySwitches - sample.before.involuntarySwitches) : 0,
                            sample.before.qos, sample.after.qos, sample.before.priority, sample.after.priority,
                            sample.before.foreground ? "true" : "false", sample.after.foreground ? "true" : "false",
                            sample.before.thermal, sample.after.thermal, sample.before.lowPower ? "true" : "false",
                            sample.after.lowPower ? "true" : "false",
                            sample.before.taskRole, sample.after.taskRole,
                            sample.testCpuMilliseconds, sample.testQos);
            }
            std::puts("]}");
            IM_CHECK(scheduling.Restore());
        });
    };
}

} // namespace vdevuitest
