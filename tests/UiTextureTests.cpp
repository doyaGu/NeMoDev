#include "core/schematic/SchematicModel.h"
#include "ui/render/UiRenderer.h"
#include "ui/schematic/Snapshots.h"
#include "imgui.h"
#include <SDL3/SDL.h>
#include <array>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
void Check(bool ok, const char *text) { if (!ok) throw std::runtime_error(text); }
std::array<unsigned char, 4> Pixel(ImTextureID id) {
    auto *device = vdev::MainRenderer().Device();
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = 4;
    auto *buffer = SDL_CreateGPUTransferBuffer(device, &info);
    Check(buffer != nullptr, "cannot allocate pixel download");
    auto *command = SDL_AcquireGPUCommandBuffer(device);
    auto *pass = SDL_BeginGPUCopyPass(command);
    SDL_GPUTextureRegion source{};
    source.texture = reinterpret_cast<SDL_GPUTexture *>(static_cast<intptr_t>(id));
    source.w = source.h = source.d = 1;
    SDL_GPUTextureTransferInfo destination{};
    destination.transfer_buffer = buffer;
    SDL_DownloadFromGPUTexture(pass, &source, &destination);
    SDL_EndGPUCopyPass(pass);
    auto *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
    Check(fence && SDL_WaitForGPUFences(device, true, &fence, 1), "pixel download did not finish");
    const auto *bytes = static_cast<const unsigned char *>(SDL_MapGPUTransferBuffer(device, buffer, false));
    Check(bytes != nullptr, "cannot map downloaded pixel");
    const std::array<unsigned char, 4> result{bytes[0], bytes[1], bytes[2], bytes[3]};
    SDL_UnmapGPUTransferBuffer(device, buffer);
    SDL_ReleaseGPUFence(device, fence);
    SDL_ReleaseGPUTransferBuffer(device, buffer);
    return result;
}
}
int main() {
    try {
        Check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        ImGui::CreateContext();
        auto *window = SDL_CreateWindow("Texture lifecycle regression", 32, 32, SDL_WINDOW_HIDDEN);
        Check(window != nullptr, SDL_GetError());
        struct Cleanup {
            SDL_Window *window;
            ~Cleanup() {
                vdev::ReleaseSnapshotTextures();
                vdev::ReleaseRetiredTextures();
                vdev::MainRenderer().Stop();
                SDL_DestroyWindow(window);
                ImGui::DestroyContext();
                SDL_Quit();
            }
        } cleanup{window};
        auto &renderer = vdev::MainRenderer();
        std::string error;
        Check(renderer.Start(window, &error), error.c_str());
        const auto generation = renderer.Generation();
        const unsigned char pixel[] = {1, 2, 3, 255};
        auto first = renderer.CreateTexture(1, 1, pixel), middle = renderer.CreateTexture(1, 1, pixel), last = renderer.CreateTexture(1, 1, pixel);
        Check(first && middle && last, "cannot create retained textures");
        middle.reset(); // Unlink an allocation while both neighbours are alive.
        alignas(vdev::SchematicSnapshot) unsigned char storage[sizeof(vdev::SchematicSnapshot)];
        const auto make = [&](std::vector<unsigned char> pixels) {
            auto *image = new (storage) vdev::SchematicSnapshot;
            image->width = image->height = 1;
            image->pixels = std::move(pixels);
            return std::shared_ptr<const vdev::SchematicSnapshot>(image, [](const auto *p) { p->~SchematicSnapshot(); });
        };
        auto image = make({11, 22, 33, 255});
        auto texture = vdev::SnapshotTexture(image, nullptr);
        Check(texture && Pixel(texture) == std::array<unsigned char, 4>{33, 22, 11, 255}, "snapshot pixel differs");
        Check(vdev::SnapshotTexture(image, nullptr) == texture, "stable snapshot recreated its texture");
        const void *address = image.get();
        image.reset();
        image = make({99, 88, 77, 255});
        Check(image.get() == address, "snapshot address reuse fixture failed");
        texture = vdev::SnapshotTexture(image, nullptr);
        Check(texture && Pixel(texture) == std::array<unsigned char, 4>{77, 88, 99, 255}, "address reuse returned old snapshot pixels");
        Check(vdev::PixelTexture(17, 1, 1, pixel, 4, false), "pixel cache fixture failed");
        renderer.Stop();
        Check(!first->NativeTexture() && !last->Id(), "device shutdown retained native texture handles");
        Check(renderer.Start(window, &error), error.c_str());
        Check(renderer.Generation() > generation, "device recreation retained its generation");
        first.reset(); last.reset(); // Must not call SDL with the destroyed device.
        Check(!vdev::CachedPixelTexture(17), "new device returned an old cached texture");
        texture = vdev::SnapshotTexture(image, nullptr);
        Check(texture && Pixel(texture) == std::array<unsigned char, 4>{77, 88, 99, 255}, "snapshot was not recreated for the new device");
        image.reset();
        vdev::ReleaseRetiredTextures();
        Check(vdev::SnapshotTextureCountForTests() == 0, "expired snapshot retained its texture");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
