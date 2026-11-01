#include "ui/schematic/Snapshots.h"
#include "core/schematic/SchematicModel.h"
#include "ui/render/UiRenderer.h"
#include "core/runtime/TextEncoding.h"
#include "CKAll.h"
#include "core/document/ObjectPictures.h"
#include "core/runtime/Bitmaps.h"
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
    std::weak_ptr<const void> owner;
    bool tracked = false;
};
std::vector<std::unique_ptr<UiTexture>> &Retired() { static std::vector<std::unique_ptr<UiTexture>> value; return value; }
std::map<const void *, Texture> &Textures() {
    static std::map<const void *, Texture> value;
    return value;
}
std::map<unsigned long long, Texture> &PixelTextures() { static std::map<unsigned long long, Texture> value; return value; }
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
ImTextureID SnapshotTexture(const std::shared_ptr<const SchematicSnapshot> &image, ImVec2 *size) {
    if (!image || !MainRenderer().Started() || image->pixels.size() != size_t(image->width) * image->height * 4)
        return {};
    auto found = Textures().find(image.get());
    if (found != Textures().end() &&
        (found->second.owner.owner_before(image) || image.owner_before(found->second.owner))) {
        RetireTexture(std::move(found->second.view));
        Textures().erase(found);
        found = Textures().end();
    }
    if (found == Textures().end()) {
        Texture texture;
        texture.owner = image;
        texture.tracked = true;
        auto pixels = image->pixels;
        for (size_t i = 0; i < pixels.size(); i += 4)
            std::swap(pixels[i], pixels[i + 2]);
        texture.view = MainRenderer().CreateTexture(image->width, image->height, pixels.data());
        if (texture.view)
            texture.size = {float(image->width), float(image->height)};
        found = Textures().emplace(image.get(), std::move(texture)).first;
    }
    if (size)
        *size = found->second.size;
    return found->second.view ? found->second.view->Id() : ImTextureID();
}
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
