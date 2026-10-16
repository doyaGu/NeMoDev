#include "ui/controls/DevBitmaps.h"
#include "ui/render/UiRenderer.h"
#include "core/runtime/Resources.h"
#include "ui/schematic/Snapshots.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace vdev {
namespace {
struct Bitmap { UiTexture *view = nullptr; ImVec2 size; bool tried = false; };
enum Group { UI, PARAMETER, APP, GROUP_COUNT };
const char *const GROUP_NAMES[] = {"ui", "parameter", "app"};
std::map<std::pair<int,int>,Bitmap> g_Bitmaps, g_Icons;
std::set<std::string> g_Missing;
Bitmap &Load(Group group, const char *kind, int id, int size, bool icon) {
    auto &map = icon ? g_Icons : g_Bitmaps;
    Bitmap &bitmap = map[{static_cast<int>(group) + GROUP_COUNT * size, id}];
    if (bitmap.tried) return bitmap;
    bitmap.tried = true;
    const EmbeddedImage *image = FindEmbeddedImage(GROUP_NAMES[group], kind, id);
    if (!image) { g_Missing.insert(ResourceName(GROUP_NAMES[group], kind, id)); return bitmap; }
    const int width = icon && size > 0 ? size : image->width;
    const int height = icon && size > 0 ? size : image->height;
    std::vector<unsigned char> scaled;
    const unsigned char *pixels = image->rgba;
    if (width != image->width || height != image->height) {
        scaled.resize(static_cast<size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                for (int channel = 0; channel < 4; ++channel)
                    scaled[(static_cast<size_t>(y) * width + x) * 4 + channel] =
                        pixels[(static_cast<size_t>(y * image->height / height) * image->width +
                                x * image->width / width) * 4 + channel];
        pixels = scaled.data();
    }
    bitmap.view = MakeRgbaTexture(width, height, pixels);
    bitmap.size = ImVec2(float(width), float(height));
    if (!bitmap.view) g_Missing.insert(ResourceName(GROUP_NAMES[group], kind, id));
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

int DevClassIconId(CK_CLASSID cid) {
    // Dev.exe sub_5D1F80 gives the class's index in the list LoadIcons fills
    // (resvis.dll icons 0x175, 0x176, 0xC3 to 0xDA, 0x178, 0x177, 0x17C, ...).
    static const int LIST[] = {0x175, 0x176, 0xC3,  0xC4,  0xC5,  0xC6,  0xC7,  0xC8,  0xC9,  0xCA,  0xCB,  0xCC,
                               0xCD,  0xCE,  0xCF,  0xD0,  0xD1,  0xD2,  0xD3,  0xD4,  0xD5,  0xD6,  0xD7,  0xD8,
                               0xD9,  0xDA,  0x178, 0x177, 0x17C, 0x17E, 0x17F, 0x180, 0x181, 0x182, 0x183, 0x186,
                               0x185, 0x184, 0x188, 0x189, 0x20E, 0x214, 0x215, 0x52C, 0x3B5, 0x3B8, 0x3B6, 0x3B9,
                               0x216, 0x3B7, 0x3C3, 0x3C1, 0x3C0, 0x3C2, 0x3C4, 0x3C6, 0x3C5};
    int index = 20;
    switch (cid) {
    case CKCID_BEHAVIOR: index = 40; break;
    case CKCID_SCENE: index = 3; break;
    case CKCID_OBJECTANIMATION:
    case CKCID_ANIMATION:
    case CKCID_KEYEDANIMATION: index = 30; break;
    case CKCID_LEVEL: index = 2; break;
    case CKCID_PLACE: index = 4; break;
    case CKCID_GROUP: index = 31; break;
    case CKCID_SOUND:
    case CKCID_WAVESOUND:
    case CKCID_MIDISOUND: index = 29; break;
    case CKCID_2DENTITY:
    case CKCID_SPRITE:
    case CKCID_SPRITETEXT: index = 13; break;
    case CKCID_MATERIAL: index = 11; break;
    case CKCID_TEXTURE: index = 12; break;
    case CKCID_MESH:
    case CKCID_PATCHMESH: index = 6; break;
    case CKCID_3DENTITY: index = 10; break;
    case CKCID_CAMERA:
    case CKCID_TARGETCAMERA: index = 8; break;
    case CKCID_SPRITE3D: index = 14; break;
    case CKCID_LIGHT:
    case CKCID_TARGETLIGHT: index = 5; break;
    case CKCID_CHARACTER: index = 15; break;
    case CKCID_3DOBJECT: index = 21; break;
    case CKCID_BODYPART: index = 48; break;
    case CKCID_CURVE: index = 22; break;
    case CKCID_GRID: index = 55; break;
    case CKCID_DATAARRAY: index = 56; break;
    default: break;
    }
    return LIST[index];
}

const std::set<std::string> &MissingResources() {
    return g_Missing;
}

void ReleaseDevBitmaps() {
    for (auto &entry : g_Bitmaps)
        if (entry.second.view) delete entry.second.view;
    g_Bitmaps.clear();
    for (auto &entry : g_Icons)
        if (entry.second.view) delete entry.second.view;
    g_Icons.clear();
}

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
    if (hovered && tooltip && *tooltip) ImGui::SetTooltip("%s", tooltip);
    if (rightClicked) *rightClicked = right && enabled;
    return clicked && !right && enabled;
}

} // namespace vdev
