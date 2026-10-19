#include "ui/render/UiRenderer.h"
#include <algorithm>
#include <SDL3/SDL.h>
#include "imgui_impl_sdlgpu3.h"
#include <cstring>
#include <limits>

namespace vdev {
UiTexture::~UiTexture() { if (m_Texture) SDL_ReleaseGPUTexture(m_Device, m_Texture); }
ImTextureID UiTexture::Id() const { return static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_Texture)); }
UiRenderer::~UiRenderer() { Stop(); }
UiRenderer &MainRenderer() { static UiRenderer renderer; return renderer; }

bool UiRenderer::Start(SDL_Window *window, std::string *error) {
    Stop();
    m_Window = window;
    m_Device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                  SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    if (!m_Device || !SDL_ClaimWindowForGPUDevice(m_Device, window)) {
        if (error) *error = SDL_GetError();
        Stop(); return false;
    }
    ImGui_ImplSDLGPU3_InitInfo info;
    info.Device = m_Device;
    info.ColorTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    if (!ImGui_ImplSDLGPU3_Init(&info)) {
        if (error) *error = "Cannot initialize the ImGui GPU renderer";
        Stop(); return false;
    }
    return true;
}
void UiRenderer::Stop() {
    if (!m_Device) { m_Window = nullptr; return; }
    SDL_WaitForGPUIdle(m_Device);
    for (SDL_GPUFence *&fence : m_FrameFences) {
        if (fence) SDL_ReleaseGPUFence(m_Device, fence);
        fence = nullptr;
    }
    m_FrameIndex = 0;
    m_FrameTextures.clear();
    m_Frame.reset();
    if (ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData)
        ImGui_ImplSDLGPU3_Shutdown();
    if (m_Window) SDL_ReleaseWindowFromGPUDevice(m_Device, m_Window);
    SDL_DestroyGPUDevice(m_Device);
    m_Device = nullptr; m_Window = nullptr;
}
bool UiRenderer::NewFrame(std::string *error) {
    m_FrameTextures.clear();
    // Hidden windows have no swapchain acquisition to bound CPU submission.
    // Complete the oldest of two frames before uploads can cycle textures again.
    SDL_GPUFence *&fence = m_FrameFences[m_FrameIndex];
    if (fence) {
        if (!SDL_WaitForGPUFences(m_Device, true, &fence, 1)) {
            if (error) *error = SDL_GetError();
            return false;
        }
        SDL_ReleaseGPUFence(m_Device, fence);
        fence = nullptr;
    }
    ImGui_ImplSDLGPU3_NewFrame();
    return true;
}
void UiRenderer::RetainTextureForFrame(const std::shared_ptr<UiTexture> &texture) {
    if (texture && std::find(m_FrameTextures.begin(),m_FrameTextures.end(),texture)==m_FrameTextures.end())
        m_FrameTextures.push_back(texture);
}

std::unique_ptr<UiTexture> UiRenderer::MakeTarget(int width, int height) {
    if (!m_Device || width <= 0 || height <= 0) return {};
    SDL_GPUTextureCreateInfo info = {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = width; info.height = height; info.layer_count_or_depth = 1;
    info.num_levels = 1; info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(m_Device, &info);
    return texture ? std::unique_ptr<UiTexture>(new UiTexture(m_Device, texture, width, height)) : nullptr;
}
std::unique_ptr<UiTexture> UiRenderer::CreateTexture(int width, int height, const void *rgba) {
    auto texture = MakeTarget(width, height);
    return texture && UpdateTexture(*texture, rgba) ? std::move(texture) : nullptr;
}
bool UiRenderer::UpdateTexture(UiTexture &texture, const void *rgba) {
    const size_t bytes = static_cast<size_t>(texture.m_Width) * texture.m_Height * 4;
    if (!rgba || !m_Device || texture.m_Device != m_Device || bytes > std::numeric_limits<Uint32>::max()) return false;
    SDL_GPUTransferBufferCreateInfo info = {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; info.size = static_cast<Uint32>(bytes);
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(m_Device, &info);
    if (!transfer) return false;
    void *mapped = SDL_MapGPUTransferBuffer(m_Device, transfer, false);
    if (!mapped) { SDL_ReleaseGPUTransferBuffer(m_Device, transfer); return false; }
    std::memcpy(mapped, rgba, bytes);
    SDL_UnmapGPUTransferBuffer(m_Device, transfer);
    SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(m_Device);
    if (!command) { SDL_ReleaseGPUTransferBuffer(m_Device, transfer); return false; }
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(command);
    SDL_GPUTextureTransferInfo source = {}; source.transfer_buffer = transfer;
    SDL_GPUTextureRegion target = {}; target.texture = texture.m_Texture;
    target.w = texture.m_Width; target.h = texture.m_Height; target.d = 1;
    SDL_UploadToGPUTexture(copy, &source, &target, true);
    SDL_EndGPUCopyPass(copy);
    const bool submitted = SDL_SubmitGPUCommandBuffer(command);
    SDL_ReleaseGPUTransferBuffer(m_Device, transfer);
    return submitted;
}
bool UiRenderer::Draw(ImDrawData *data, UiTexture &texture, bool present, const std::array<float, 4> &clear, std::string *error,
                      SDL_GPUFence **completion) {
    SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(m_Device);
    if (!command) { if (error) *error = SDL_GetError(); return false; }
    ImGui_ImplSDLGPU3_PrepareDrawData(data, command);
    SDL_GPUColorTargetInfo target = {};
    target.texture = texture.m_Texture;
    target.clear_color = SDL_FColor{clear[0], clear[1], clear[2], clear[3]};
    target.load_op = SDL_GPU_LOADOP_CLEAR; target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command, &target, 1, nullptr);
    ImGui_ImplSDLGPU3_RenderDrawData(data, command, pass);
    SDL_EndGPURenderPass(pass);
    if (present) {
        SDL_GPUTexture *swap = nullptr;
        Uint32 width = 0, height = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(command, m_Window, &swap, &width, &height)) {
            if (error) *error = SDL_GetError();
            SDL_CancelGPUCommandBuffer(command); return false;
        }
        if (swap) {
            SDL_GPUBlitInfo blit = {};
            blit.source.texture = texture.m_Texture;
            blit.source.w = texture.m_Width; blit.source.h = texture.m_Height;
            blit.destination.texture = swap; blit.destination.w = width; blit.destination.h = height;
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE; blit.filter = SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(command, &blit);
        }
    }
    const bool submitted = completion ? (*completion = SDL_SubmitGPUCommandBufferAndAcquireFence(command)) != nullptr
                                     : SDL_SubmitGPUCommandBuffer(command);
    if (!submitted) { if (error) *error = SDL_GetError(); return false; }
    return true;
}
bool UiRenderer::Render(ImDrawData *data, bool present, std::string *error) {
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(m_Window, &width, &height)) return false;
    if (width <= 0 || height <= 0) return true;
    if (!m_Frame || m_Frame->Width() != width || m_Frame->Height() != height) m_Frame = MakeTarget(width, height);
    if (!m_Frame || !Draw(data, *m_Frame, present, {0.392f, 0.392f, 0.392f, 1.0f}, error,
                          &m_FrameFences[m_FrameIndex])) return false;
    m_FrameIndex = (m_FrameIndex + 1) % m_FrameFences.size();
    return true;
}
bool UiRenderer::ReadTexture(const UiTexture &texture, std::vector<unsigned char> &rgba) {
    const size_t bytes = static_cast<size_t>(texture.m_Width) * texture.m_Height * 4;
    if (bytes > std::numeric_limits<Uint32>::max()) return false;
    SDL_GPUTransferBufferCreateInfo info = {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; info.size = static_cast<Uint32>(bytes);
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(m_Device, &info);
    if (!transfer) return false;
    SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(m_Device);
    if (!command) { SDL_ReleaseGPUTransferBuffer(m_Device, transfer); return false; }
    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(command);
    SDL_GPUTextureRegion source = {}; source.texture = texture.m_Texture;
    source.w = texture.m_Width; source.h = texture.m_Height; source.d = 1;
    SDL_GPUTextureTransferInfo destination = {}; destination.transfer_buffer = transfer;
    SDL_DownloadFromGPUTexture(pass, &source, &destination);
    SDL_EndGPUCopyPass(pass);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
    bool done = fence && SDL_WaitForGPUFences(m_Device, true, &fence, 1);
    if (done) {
        const void *mapped = SDL_MapGPUTransferBuffer(m_Device, transfer, false);
        done = mapped != nullptr;
        if (mapped) { rgba.resize(bytes); std::memcpy(rgba.data(), mapped, bytes); SDL_UnmapGPUTransferBuffer(m_Device, transfer); }
    }
    if (fence) SDL_ReleaseGPUFence(m_Device, fence);
    SDL_ReleaseGPUTransferBuffer(m_Device, transfer);
    return done;
}
#ifdef VIRTOOLSDEV_UI_TESTS
bool UiRenderer::ReadFrame(std::vector<unsigned char> &rgba, int &width, int &height) {
    if (!m_Frame) return false;
    width = m_Frame->Width(); height = m_Frame->Height();
    return ReadTexture(*m_Frame, rgba);
}
#endif
bool UiRenderer::RenderPage(ImDrawData *data, int width, int height, std::vector<unsigned char> &rgba) {
    auto target = MakeTarget(width, height);
    return target && Draw(data, *target, false, {1.0f, 1.0f, 1.0f, 1.0f}, nullptr) && ReadTexture(*target, rgba);
}
}
