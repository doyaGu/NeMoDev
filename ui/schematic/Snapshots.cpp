#include "ui/schematic/Snapshots.h"
#include "ui/render/UiRenderer.h"
#include "core/runtime/TextEncoding.h"
#include "CKAll.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace vdev {
namespace {
struct Texture {
    std::unique_ptr<UiTexture> view;
    ImVec2 size;
    std::weak_ptr<void> owner;
    bool tracked = false;
};
std::vector<std::unique_ptr<UiTexture>> &Retired() { static std::vector<std::unique_ptr<UiTexture>> value; return value; }
std::map<void *, Texture> &Textures() { static std::map<void *, Texture> value; return value; }
std::map<unsigned long long, Texture> &PixelTextures() { static std::map<unsigned long long, Texture> value; return value; }
}
void *BitmapFromPixels(const void *bgra, int width, int height) {
    if (!bgra || width <= 0 || height <= 0) return nullptr;
    VxImageDescEx image;
    image.Width = width; image.Height = height; image.BitsPerPixel = 32;
    image.BytesPerLine = width * 4;
    image.RedMask = 0x00ff0000; image.GreenMask = 0x0000ff00;
    image.BlueMask = 0x000000ff; image.AlphaMask = 0xff000000;
    image.Image = const_cast<XBYTE *>(static_cast<const XBYTE *>(bgra));
    return VxCreateBitmap(image);
}
bool PixelsOfBitmap(void *bitmap, std::vector<unsigned char> &bgra, int &width, int &height) {
    if (!bitmap) return false;
    VxImageDescEx image;
    std::unique_ptr<XBYTE[]> copy(VxConvertBitmap(bitmap, image));
    if (!copy || image.Width <= 0 || image.Height <= 0 || image.BitsPerPixel != 32) return false;
    width = image.Width; height = image.Height;
    bgra.resize(static_cast<size_t>(width) * height * 4);
    for (int row = 0; row < height; ++row)
        std::memcpy(bgra.data() + static_cast<size_t>(row) * width * 4,
                    copy.get() + static_cast<size_t>(row) * image.BytesPerLine, static_cast<size_t>(width) * 4);
    for (size_t i = 3; i < bgra.size(); i += 4) bgra[i] = 255;
    return true;
}
void *LoadSnapshotBitmap(const std::wstring &path, int maxWidth, int maxHeight, std::string *error) {
    std::vector<unsigned char> source;
    int width = 0, height = 0;
    if (maxWidth <= 0 || maxHeight <= 0 || !ReadImageRgba(WideToUtf8(path), source, width, height)) {
        if (error) *error = "Cannot decode the snapshot image";
        return nullptr;
    }
    const double scale = std::min(1.0, std::min(double(maxWidth)/width, double(maxHeight)/height));
    const int w = std::max(1, int(width * scale + 0.5)), h = std::max(1, int(height * scale + 0.5));
    SDL_Surface *input = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, source.data(), width * 4);
    SDL_Surface *output = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_BGRA32);
    if (!input || !output || !SDL_BlitSurfaceScaled(input, nullptr, output, nullptr, SDL_SCALEMODE_LINEAR)) {
        SDL_DestroySurface(input); SDL_DestroySurface(output);
        if (error) *error = "Cannot scale the snapshot image";
        return nullptr;
    }
    std::vector<unsigned char> bgra(static_cast<size_t>(w) * h * 4);
    for (int row = 0; row < h; ++row)
        std::memcpy(bgra.data() + static_cast<size_t>(row) * w * 4,
                    static_cast<unsigned char *>(output->pixels) + static_cast<size_t>(row) * output->pitch, static_cast<size_t>(w) * 4);
    for (size_t i = 3; i < bgra.size(); i += 4) bgra[i] = 255;
    SDL_DestroySurface(input); SDL_DestroySurface(output);
    void *bitmap = BitmapFromPixels(bgra.data(), w, h);
    if (!bitmap && error) *error = "Cannot create the snapshot bitmap";
    return bitmap;
}
void RetireTexture(std::unique_ptr<UiTexture> texture) {
    if (texture) Retired().push_back(std::move(texture));
}
void ReleaseRetiredTextures() {
    for (auto it = Textures().begin(); it != Textures().end();) {
        if (it->second.tracked && it->second.owner.expired()) {
            RetireTexture(std::move(it->second.view));
            it = Textures().erase(it);
        } else ++it;
    }
    Retired().clear();
}
#ifdef VIRTOOLSDEV_UI_TESTS
size_t SnapshotTextureCountForTests() { return Textures().size(); }
#endif
ImTextureID SnapshotTexture(void *bitmap, ImVec2 *size, const std::shared_ptr<void> &owner) {
    if (!bitmap || !MainRenderer().Started()) return ImTextureID();
    auto found = Textures().find(bitmap);
    if (found != Textures().end() &&
        (found->second.owner.owner_before(owner) || owner.owner_before(found->second.owner))) {
        RetireTexture(std::move(found->second.view));
        Textures().erase(found);
        found = Textures().end();
    }
    if (found == Textures().end()) {
        Texture texture;
        texture.owner = owner;
        texture.tracked = bool(owner);
        std::vector<unsigned char> pixels;
        int width = 0, height = 0;
        if (PixelsOfBitmap(bitmap, pixels, width, height)) {
            for (size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i+2]);
            texture.view = MainRenderer().CreateTexture(width, height, pixels.data());
            if (texture.view) texture.size = ImVec2(float(width), float(height));
        }
        found = Textures().emplace(bitmap, std::move(texture)).first;
    }
    if (size) *size = found->second.size;
    return found->second.view ? found->second.view->Id() : ImTextureID();
}
ImTextureID PixelTexture(unsigned long long key, int width, int height, const void *bgra, int pitch, bool alphaAsGray) {
    const bool device = MainRenderer().Started();
    if (!device || !bgra || width <= 0 || height <= 0 || width > 8192 || height > 8192) return ImTextureID();
    std::vector<unsigned char> rgba(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const unsigned char *row = static_cast<const unsigned char *>(bgra) + static_cast<size_t>(y) * pitch;
        unsigned char *out = &rgba[static_cast<size_t>(y) * width * 4];
        for (int x = 0; x < width; ++x) {
            if (alphaAsGray) {
                out[x * 4 + 0] = out[x * 4 + 1] = out[x * 4 + 2] = row[x * 4 + 3];
            } else {
                out[x * 4 + 0] = row[x * 4 + 2];
                out[x * 4 + 1] = row[x * 4 + 1];
                out[x * 4 + 2] = row[x * 4 + 0];
            }
            out[x * 4 + 3] = 255;
        }
    }
    Texture &texture = PixelTextures()[key];
    RetireTexture(std::move(texture.view)); // the frame may draw it still
    texture.view = MainRenderer().CreateTexture(width, height, rgba.data());
    texture.size = ImVec2(static_cast<float>(width), static_cast<float>(height));
    return texture.view ? texture.view->Id() : ImTextureID();
}

ImTextureID CachedPixelTexture(unsigned long long key) {
    auto found = PixelTextures().find(key);
    return found != PixelTextures().end() && found->second.view ? found->second.view->Id() : ImTextureID();
}

void ReleaseSnapshotTextures() {
    for (auto &entry : Textures()) RetireTexture(std::move(entry.second.view));
    Textures().clear();
    for (auto &entry : PixelTextures()) RetireTexture(std::move(entry.second.view));
    PixelTextures().clear();
}

} // namespace vdev
