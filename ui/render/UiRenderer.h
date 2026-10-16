#pragma once
#include <memory>
#include <string>
#include <vector>
#include <array>
#include "imgui.h"

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUTexture;
namespace vdev {
class UiRenderer;
class UiTexture {
public:
    ~UiTexture();
    UiTexture(const UiTexture &) = delete;
    UiTexture &operator=(const UiTexture &) = delete;
    ImTextureID Id() const;
    int Width() const { return m_Width; }
    int Height() const { return m_Height; }
private:
    friend class UiRenderer;
    UiTexture(SDL_GPUDevice *device, SDL_GPUTexture *texture, int width, int height)
        : m_Device(device), m_Texture(texture), m_Width(width), m_Height(height) {}
    SDL_GPUDevice *m_Device;
    SDL_GPUTexture *m_Texture;
    int m_Width;
    int m_Height;
};

// Owns the ImGui device and image transfers. CK renders on a separate device
// and window; its readback pixels enter this renderer through CreateTexture.
class UiRenderer {
public:
    UiRenderer() = default;
    ~UiRenderer();
    UiRenderer(const UiRenderer &) = delete;
    UiRenderer &operator=(const UiRenderer &) = delete;
    bool Start(SDL_Window *window, std::string *error);
    void Stop();
    void NewFrame();
    bool Render(ImDrawData *data, bool present, std::string *error);
    std::unique_ptr<UiTexture> CreateTexture(int width, int height, const void *rgba);
    bool UpdateTexture(UiTexture &texture, const void *rgba);
    bool ReadFrame(std::vector<unsigned char> &rgba, int &width, int &height);
    bool RenderPage(ImDrawData *data, int width, int height, std::vector<unsigned char> &rgba);
    bool Started() const { return m_Device != nullptr; }
private:
    bool ReadTexture(const UiTexture &texture, std::vector<unsigned char> &rgba);
    bool Draw(ImDrawData *data, UiTexture &texture, bool present, const std::array<float, 4> &clear, std::string *error);
    std::unique_ptr<UiTexture> MakeTarget(int width, int height);
    SDL_GPUDevice *m_Device = nullptr;
    SDL_Window *m_Window = nullptr; // borrowed, survives Stop
    std::unique_ptr<UiTexture> m_Frame;
};
UiRenderer &MainRenderer();
bool WriteBgraPng(const std::string &path, int width, int height, const unsigned char *pixels);
bool ReadImageRgba(const std::string &path, std::vector<unsigned char> &pixels, int &width, int &height);
}
