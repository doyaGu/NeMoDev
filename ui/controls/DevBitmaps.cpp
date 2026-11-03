#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevIcons.h"
#include "ui/render/UiRenderer.h"
#include "core/runtime/Resources.h"
#include "ui/schematic/Snapshots.h"
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace vdev {
namespace {
struct Bitmap {
    std::unique_ptr<UiTexture> view;
    ImVec2 size;
    bool tried = false;
    std::string id;
    EditorImageDesc source{};
};
enum Group { UI, PARAMETER, APP, GROUP_COUNT };
const char *const GROUP_NAMES[] = {"ui", "parameter", "app"};
using Bitmaps = std::map<std::pair<int, int>, Bitmap>;
Bitmaps g_Bitmaps, g_Icons, g_PreparedBitmaps, g_PreparedIcons;
std::map<std::string, EditorImageDesc> g_Images, g_PreparedImages;
std::set<std::string> g_Missing;
std::string Text(EditorString s) { return {s.data ? s.data : "", s.size}; }
bool SameImage(const EditorImageDesc &a, const EditorImageDesc &b) {
    return a.pixels == b.pixels && a.width == b.width && a.height == b.height && a.byteCount == b.byteCount;
}
Bitmap Make(const std::string &id, const EditorImageDesc *image, int size) {
    Bitmap result;
    result.tried = true;
    result.id = id;
    if (!image) return result;
    result.source = *image;
    const int width = size > 0 ? size : int(image->width), height = size > 0 ? size : int(image->height);
    std::vector<unsigned char> scaled;
    const unsigned char *pixels = image->pixels;
    if (width != int(image->width) || height != int(image->height)) {
        scaled.resize(static_cast<size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                for (int channel = 0; channel < 4; ++channel)
                    scaled[(static_cast<size_t>(y) * width + x) * 4 + channel] =
                        pixels[(static_cast<size_t>(y * image->height / height) * image->width +
                                x * image->width / width) * 4 + channel];
        pixels = scaled.data();
    }
    result.view = MainRenderer().CreateTexture(width, height, pixels);
    result.size = ImVec2(float(width), float(height));
    return result;
}
Bitmap &Load(Group group, const char *kind, int id, int size, bool icon) {
    auto &map = icon ? g_Icons : g_Bitmaps;
    Bitmap &bitmap = map[{static_cast<int>(group) + GROUP_COUNT * size, id}];
    if (bitmap.view && !bitmap.view->NativeTexture()) bitmap = {};
    if (bitmap.tried) return bitmap;
    const auto name = std::string(GROUP_NAMES[group]) + "." + kind + "." + std::to_string(id);
    const auto image = g_Images.find(name);
    bitmap = Make(name, image == g_Images.end() ? nullptr : &image->second, icon ? size : 0);
    // No image catalogue is the renderer's basic text/vector presentation.
    // Keep reporting missing assets and upload failures in configured catalogues.
    if (!bitmap.view && !g_Images.empty()) g_Missing.insert(ResourceName(GROUP_NAMES[group], kind, id));
    return bitmap;
}
Bitmap &LoadBitmapOf(Group group, int id) { return Load(group, "bitmap", id, 0, false); }
Bitmap &IconOf(Group group, int id, int size = 11) { return Load(group, "icon", id, size, true); }
}
ImTextureID DevBitmap(int id, ImVec2 *size) {
    Bitmap &bitmap = LoadBitmapOf(UI, id);
    if (size) *size = bitmap.size;
    return bitmap.view ? bitmap.view->Id() : ImTextureID();
}

ImTextureID CK2UIBitmap(int id, ImVec2 *size) {
    Bitmap &bitmap = LoadBitmapOf(PARAMETER, id);
    if (size) *size = bitmap.size;
    return bitmap.view ? bitmap.view->Id() : ImTextureID();
}

ImTextureID DevIconImage(int id) {
    const Bitmap &icon = IconOf(UI, id);
    return icon.view ? icon.view->Id() : ImTextureID();
}

ImTextureID DevExeIcon(int id) {
    const Bitmap &icon = IconOf(APP, id);
    return icon.view ? icon.view->Id() : ImTextureID();
}

ImTextureID DevMessageIcon(int id) {
    const Bitmap &icon = IconOf(UI, id, 32);
    return icon.view ? icon.view->Id() : ImTextureID();
}

const std::set<std::string> &MissingResources() {
    return g_Missing;
}

void ReleaseDevBitmaps() {
    g_Bitmaps.clear();
    g_Icons.clear();
    g_PreparedBitmaps.clear();
    g_PreparedIcons.clear();
}
bool PrepareDevImages(const std::vector<EditorImageDesc> &images, const std::map<std::string, EditorField> &values,
                      std::string &error) {
    g_PreparedImages.clear();
    g_PreparedBitmaps.clear();
    g_PreparedIcons.clear();
    for (const auto &image : images) g_PreparedImages.emplace(Text(image.id), image);
    for (const auto &entry : values)
        if (entry.first.compare(0, 6, "image.") == 0) {
            const auto source = std::find_if(images.begin(), images.end(),
                                             [&](const auto &i) { return Text(i.id) == entry.second.text; });
            if (source == images.end()) {
                error = "Unavailable theme image: " + entry.second.text;
                return false;
            }
            g_PreparedImages[entry.first.substr(6)] = *source;
        }
    const auto prepare = [&](const Bitmaps &current, Bitmaps &prepared, bool icons) {
        for (const auto &entry : current) {
            if (!entry.second.tried) continue;
            const auto next = g_PreparedImages.find(entry.second.id);
            const EditorImageDesc *source = next == g_PreparedImages.end() ? nullptr : &next->second;
            if (SameImage(entry.second.source, source ? *source : EditorImageDesc{})) continue;
            auto bitmap = Make(entry.second.id, source, icons ? entry.first.first / GROUP_COUNT : 0);
            if (source && !bitmap.view) {
                error = "Cannot prepare UI image: " + entry.second.id;
                return false;
            }
            prepared.emplace(entry.first, std::move(bitmap));
        }
        return true;
    };
    if (!prepare(g_Bitmaps, g_PreparedBitmaps, false) || !prepare(g_Icons, g_PreparedIcons, true)) {
        g_PreparedImages.clear();
        g_PreparedBitmaps.clear();
        g_PreparedIcons.clear();
        return false;
    }
    return true;
}
void ApplyDevImages() {
    g_Images = std::move(g_PreparedImages);
    for (auto &entry : g_PreparedBitmaps) g_Bitmaps[entry.first] = std::move(entry.second);
    for (auto &entry : g_PreparedIcons) g_Icons[entry.first] = std::move(entry.second);
    g_PreparedBitmaps.clear();
    g_PreparedIcons.clear();
    g_Missing.clear();
    const auto missing = [&](const Bitmaps &cache, const char *kind) {
        for (const auto &entry : cache)
            if (entry.second.tried && !entry.second.view)
                g_Missing.insert(ResourceName(GROUP_NAMES[entry.first.first % GROUP_COUNT], kind, entry.first.second));
    };
    if (!g_Images.empty()) {
        missing(g_Bitmaps, "bitmap");
        missing(g_Icons, "icon");
    }
}
void ResetDevImages() {
    ReleaseDevBitmaps();
    g_Images.clear();
    g_PreparedImages.clear();
    g_Missing.clear();
}

#ifdef VIRTOOLSDEV_UI_TESTS
const UiTexture *DevBitmapForTests(int id) { return LoadBitmapOf(UI, id).view.get(); }
#endif

bool DevBitmapButton(const char *id, const ImVec2 &at, const int bitmaps[5], bool checked, bool enabled, float scale,
                     const char *tooltip, bool *rightClicked) {
    ImVec2 bitmapSize;
    DevBitmap(bitmaps[0], &bitmapSize);
    if (bitmapSize.x <= 0.0f) bitmapSize = ImVec2(17.0f, 19.0f);
    const ImVec2 size(bitmapSize.x * scale, bitmapSize.y * scale);
    ImGui::SetCursorScreenPos(at);
    ImGui::PushID(id);
    if (!enabled) ImGui::BeginDisabled();
    const bool clicked =
        ImGui::InvisibleButton("##bitmap", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool right = clicked && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    if (!enabled) ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const bool held = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    ImGui::PopID();
    const int bitmap = !enabled ? bitmaps[4] : (checked || held) ? bitmaps[hovered ? 3 : 2] : bitmaps[hovered ? 1 : 0];
    if (ImTextureID texture = DevBitmap(bitmap))
        ImGui::GetWindowDrawList()->AddImage(texture, at, ImVec2(at.x + size.x, at.y + size.y));
    else {
        auto *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(at, {at.x + size.x, at.y + size.y},
                            ImGui::GetColorU32(held || checked ? ImGuiCol_ButtonActive
                                               : hovered       ? ImGuiCol_ButtonHovered
                                                               : ImGuiCol_Button));
        draw->AddRect(at, {at.x + size.x, at.y + size.y}, ImGui::GetColorU32(ImGuiCol_Border));
        const auto text = ImGui::CalcTextSize("?");
        draw->AddText({at.x + (size.x - text.x) * .5f, at.y + (size.y - text.y) * .5f},
                      ImGui::GetColorU32(ImGuiCol_Text), "?");
    }
    if (hovered && tooltip && *tooltip) ImGui::SetTooltip("%s", tooltip);
    if (rightClicked) *rightClicked = right && enabled;
    return clicked && !right && enabled;
}

} // namespace vdev
