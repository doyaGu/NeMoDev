// The 3D Layout's Snapshot Buffer (Dev.exe sub_5BBB20, dialog 733; the
// buffer itself sub_4C5860, EditorState::snapshots).
#include "ui/layout/LayoutWindow.h"


#include <algorithm>
#include <filesystem>
#include <cstring>

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/FileDialogs.h"
#include "ui/schematic/Snapshots.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

namespace {

// Dev.exe sub_5D55F0: the bitmap readers' types, for loading all of them
// first ("All Known Types (.bmp;.tga)"); extensions: each entry's.
std::string BitmapFilter(bool load, std::vector<std::string> *extensions) {
    std::vector<std::pair<std::string, std::string>> types; // description, extension
    CKPluginManager *plugins = CKGetPluginManager();
    for (int i = 0; plugins && i < plugins->GetPluginCount(CKPLUGIN_BITMAP_READER); ++i) {
        CKPluginEntry *reader = plugins->GetPluginInfo(CKPLUGIN_BITMAP_READER, i);
        if (!reader || !reader->m_PluginInfo.m_Extension.m_Data[0]) continue;
        const std::string extension = reader->m_PluginInfo.m_Extension.m_Data;
        std::string lower = extension;
        for (char &c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (lower.find("vmo") != std::string::npos || lower.find("nms") != std::string::npos ||
            lower.find("cmo") != std::string::npos)
            continue;
        const char *description = reader->m_PluginInfo.m_Description.CStr();
        types.emplace_back(description ? description : "", extension);
    }
    std::string filter;
    if (load && !types.empty()) {
        std::string all, patterns;
        for (const auto &type : types) {
            all += "." + type.second + ";";
            patterns += "*." + type.second + ";";
        }
        all.pop_back();
        patterns.pop_back();
        filter += "All Known Types (" + all + ")";
        filter += '\0';
        filter += patterns;
        filter += '\0';
        if (extensions) extensions->push_back("");
    }
    for (const auto &type : types) {
        filter += type.first + " (*." + type.second + ")";
        filter += '\0';
        filter += "*." + type.second;
        filter += '\0';
        if (extensions) extensions->push_back(type.second);
    }
    filter += '\0';
    return filter;
}

ImTextureID SlotTexture(const SnapshotBuffer &buffer, int slot, unsigned *revisions) {
    const unsigned long long key = 0x534E415000000000ull | static_cast<unsigned>(slot);
    if (revisions[slot] != buffer.Revision(slot) || !CachedPixelTexture(key)) {
        revisions[slot] = buffer.Revision(slot);
        const std::vector<unsigned char> &pixels = buffer.Pixels(slot);
        return PixelTexture(key, SnapshotBuffer::WIDTH, SnapshotBuffer::HEIGHT, pixels.data(), SnapshotBuffer::WIDTH * 4,
                            false);
    }
    return CachedPixelTexture(key);
}

} // namespace

// Take (Dev.exe sub_4A1C70, sub_4C5B20): the whole view stretched into the
// first empty slot.
void LayoutWindow::TakeSnapshot() {
    if (UsesGpuViewport() && !m_View.ReadPixels(m_Pixels)) return;
    if (!m_Started || m_Pixels.empty() || m_View.Width() <= 0 || m_View.Height() <= 0) return;
    std::vector<unsigned char> image = ShrinkImage(m_Pixels.data(), m_View.Width(), m_View.Height(), m_View.Width() * 4,
                                                   SnapshotBuffer::WIDTH, SnapshotBuffer::HEIGHT);
    if (m_State.snapshots.Put(std::move(image)) < 0) m_State.Log(LogLevel::Warning, "No more room for Snapshots");
}

// Capture (Dev.exe sub_5BC110): once the buttons are released a 104 x 80
// box follows the mouse; the left button captures what is under it, the
// right button or Esc gives up.
void LayoutWindow::CaptureInput(const ImVec2 &at) {
    if (!m_Capturing) return;
    const ImGuiIO &io = ImGui::GetIO();
    if (m_Capturing == 1) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) m_Capturing = 2;
        return;
    }
    const ImVec2 mouse = io.MousePos;
    ImGui::GetForegroundDrawList()->AddRect(mouse, ImVec2(mouse.x + 104.0f, mouse.y + 80.0f), IM_COL32(255, 255, 255, 255),
                                            0.0f, 0, 2.0f);
    ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_Capturing = 0;
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_Capturing = 0;
        if (UsesGpuViewport() && !m_View.ReadPixels(m_Pixels)) return;
        if (m_Pixels.empty()) return;
        std::vector<unsigned char> image =
            CaptureArea(m_Pixels.data(), m_View.Width(), m_View.Height(), m_View.Width() * 4,
                        static_cast<int>(mouse.x - at.x), static_cast<int>(mouse.y - at.y));
        if (m_State.snapshots.Put(std::move(image)) < 0) m_State.Log(LogLevel::Warning, "No more room for Snapshots");
    }
}

// Load Bitmap (Dev.exe sub_5BC900): an image read by CK's bitmap readers
// (CKLoadBitmap), stretched to the slot (ImageList_Replace).
void LayoutWindow::LoadSnapshot(int slot) {
    const std::wstring filter = AnsiToWide(BitmapFilter(true, nullptr));
    FileDialogOptions options;
    options.filter = filter.c_str();
    options.title = L"Load Bitmap";
    m_State.fileDialogs.Request(options, [this, slot](const FileDialogResult &result) {
        if (result.status == FileDialogResult::Status::Error) { m_State.Log(LogLevel::Error, result.error); return; }
        if (result.status != FileDialogResult::Status::Selected) return;
        const std::string &file = result.paths.front();
        BITMAP_HANDLE bitmap = CKLoadBitmap(const_cast<CKSTRING>(file.c_str()));
        std::vector<unsigned char> pixels;
        int width = 0, height = 0;
        const bool loaded = bitmap && PixelsOfBitmap(bitmap, pixels, width, height);
        if (bitmap) VxDeleteBitmap(bitmap);
        if (!loaded) { m_State.Log(LogLevel::Warning, "Load Bitmap: " + file + " cannot be read"); return; }
        m_State.snapshots.Put(ShrinkImage(pixels.data(), width, height, width * 4, SnapshotBuffer::WIDTH, SnapshotBuffer::HEIGHT), slot);
    });
}

void LayoutWindow::SaveSnapshot(int slot) {
    if (!m_State.snapshots.Has(slot)) return;
    std::vector<std::string> extensions;
    const std::wstring filter = AnsiToWide(BitmapFilter(false, &extensions));
    FileDialogOptions options;
    options.save = true;
    options.filter = filter.c_str();
    options.title = L"Save Bitmap";
    m_State.fileDialogs.Request(options, [this, extensions, pixels = m_State.snapshots.Pixels(slot)](const FileDialogResult &result) {
        if (result.status == FileDialogResult::Status::Error) { m_State.Log(LogLevel::Error, result.error); return; }
        if (result.status != FileDialogResult::Status::Selected) return;
        std::string path = result.paths.front();
        const size_t type = result.filterIndex > 0 ? static_cast<size_t>(result.filterIndex - 1) : 0;
        if (type < extensions.size() && !extensions[type].empty() && std::filesystem::u8path(path).extension().empty())
            path += "." + extensions[type];
        BITMAP_HANDLE bitmap = BitmapFromPixels(pixels.data(), SnapshotBuffer::WIDTH, SnapshotBuffer::HEIGHT);
        const bool saved = bitmap && CKSaveBitmap(const_cast<CKSTRING>(path.c_str()), bitmap);
        if (bitmap) VxDeleteBitmap(bitmap);
        if (!saved) m_State.Log(LogLevel::Warning, "Save as...: " + path + " cannot be written");
    });
}

// The dialog: the 36 slots at Dev's places (sub_5BBF90: from (10, 10),
// 56 apart across, 48 down), the empty ones with Dev's empty picture
// (resvis.dll 929); Take, Capture and Close below.
void LayoutWindow::SnapshotWindow() {
    if (!m_SnapshotOpen) return;
    SnapshotBuffer &buffer = m_State.snapshots;
    const float s = m_Scale;
    const ImGuiStyle &style = ImGui::GetStyle();
    const float footer = ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.0f;
    ImGui::SetNextWindowSize(ImVec2(362.0f * s + style.WindowPadding.x * 2.0f,
                                    304.0f * s + footer + style.WindowPadding.y * 2.0f + ImGui::GetFrameHeight()),
                             ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool open = true;
    const bool visible = ImGui::Begin("Snapshot Buffer", &open,
                                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
                                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    if (!visible) {
        ImGui::End();
        if (!open) m_SnapshotOpen = false;
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + 362.0f * s, origin.y + 304.0f * s), IM_COL32(255, 255, 255, 255));
    auto selected = [&](int slot) {
        return std::find(m_SnapshotSelection.begin(), m_SnapshotSelection.end(), slot) != m_SnapshotSelection.end();
    };
    for (int slot = 0; slot < SnapshotBuffer::COUNT; ++slot) {
        const ImVec2 at(origin.x + (10.0f + 56.0f * (slot % 6)) * s, origin.y + (10.0f + 48.0f * (slot / 6)) * s);
        const ImVec2 size(SnapshotBuffer::WIDTH * s, SnapshotBuffer::HEIGHT * s);
        ImGui::SetCursorScreenPos(at);
        ImGui::PushID(slot);
        ImGui::InvisibleButton("##slot", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (ImGui::GetIO().KeyCtrl) {
                auto found = std::find(m_SnapshotSelection.begin(), m_SnapshotSelection.end(), slot);
                if (found != m_SnapshotSelection.end()) m_SnapshotSelection.erase(found);
                else m_SnapshotSelection.push_back(slot);
            } else {
                m_SnapshotSelection.assign(1, slot);
            }
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (!selected(slot)) m_SnapshotSelection.assign(1, slot);
            m_SnapshotMenu = slot;
            ImGui::OpenPopup("##snapshotmenu");
        }
        // Dragged onto a Script header in the Schematic: its snapshot.
        if (buffer.Has(slot) && ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(SNAPSHOT_PAYLOAD, &slot, sizeof(slot));
            if (ImTextureID texture = SlotTexture(buffer, slot, m_SnapshotRevisions))
                ImGui::Image(texture, ImVec2(SnapshotBuffer::WIDTH * 1.0f, SnapshotBuffer::HEIGHT * 1.0f));
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
        ImTextureID texture = buffer.Has(slot) ? SlotTexture(buffer, slot, m_SnapshotRevisions) : DevBitmap(929);
        if (texture) dl->AddImage(texture, at, ImVec2(at.x + size.x, at.y + size.y));
        else dl->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), IM_COL32(160, 160, 160, 255));
        if (selected(slot))
            dl->AddRect(ImVec2(at.x - 2.0f * s, at.y - 2.0f * s), ImVec2(at.x + size.x + 2.0f * s, at.y + size.y + 2.0f * s),
                        IM_COL32(10, 36, 106, 255), 0.0f, 0, 2.0f * s);
        else if (hovered)
            dl->AddRect(ImVec2(at.x - 1.0f * s, at.y - 1.0f * s), ImVec2(at.x + size.x + 1.0f * s, at.y + size.y + 1.0f * s),
                        IM_COL32(120, 120, 120, 255));
    }
    // The shortcut menu (sub_5BC500): Load bitmap; with an image Save as...
    // and Delete (Del).
    if (ImGui::BeginPopup("##snapshotmenu")) {
        const int slot = m_SnapshotMenu;
        if (ImGui::MenuItem("Load bitmap")) LoadSnapshot(slot);
        if (buffer.Has(slot)) {
            if (ImGui::MenuItem("Save as...")) SaveSnapshot(slot);
            if (ImGui::MenuItem("Delete", "Del")) buffer.Remove(slot);
        }
        ImGui::EndPopup();
    }
    // Del deletes the slots selected (LVN_KEYDOWN).
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete))
        for (int slot : m_SnapshotSelection) buffer.Remove(slot);
    // Take, Capture, Close (Dev's custom buttons 1, 2 and 4).
    const float buttonWidth = 80.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + 304.0f * s + style.ItemSpacing.y));
    ImGui::BeginDisabled(!m_Started);
    if (ImGui::Button("Take", ImVec2(buttonWidth, 0.0f))) TakeSnapshot();
    ImGui::SameLine();
    if (ImGui::Button("Capture", ImVec2(buttonWidth, 0.0f))) m_Capturing = 1;
    ImGui::EndDisabled();
    ImGui::SameLine(origin.x - ImGui::GetWindowPos().x + 362.0f * s - buttonWidth);
    if (ImGui::Button("Close", ImVec2(buttonWidth, 0.0f))) open = false;
    ImGui::End();
    if (!open) {
        m_SnapshotOpen = false;
        m_Capturing = 0;
    }
}

// Dev.exe sub_5D1E20: the slot's image (52 x 40) is the object's picture. The
// composition has a change to save and the change is undone (Dev does neither);
// Current pictures and undo actions retain their bitmap allocations.
void EditorState::SetObjectPicture(CK_ID object, int slot) {
    if (!HasDocument() || !object || !snapshots.Has(slot)) return;
    void *bitmap = BitmapFromPixels(snapshots.Pixels(slot).data(), SnapshotBuffer::WIDTH, SnapshotBuffer::HEIGHT);
    if (!bitmap) return;
    SetObjectPictureBitmap(object, bitmap);
}

void EditorState::SetObjectPictureBitmap(CK_ID object, void *bitmap) {
    if (!HasDocument() || !object) return;
    const auto afterOwner = document->KeepBitmap(bitmap);
    auto found = objectPictures.find(object);
    if (!bitmap && found == objectPictures.end()) return;
    void *previous = found != objectPictures.end() ? found->second : nullptr;
    const auto beforeOwner = document->BitmapOwner(previous);
    Document *owner = document.get();
    std::map<CK_ID, void *> *pictures = &objectPictures;
    bool *changed = &objectPicturesChanged;
    auto show = [owner, pictures, changed, object, beforeOwner, afterOwner](bool after) {
        // Captures keep both sides alive until this undo action is evicted.
        void *picture = (after ? afterOwner : beforeOwner).get();
        if (picture) (*pictures)[object] = picture;
        else pictures->erase(object);
        owner->RetainPicture(object, picture);
        *changed = true;
        return true;
    };
    Edit edit(*document, "Object Picture");
    edit.Do([show] { return show(true); }, [show] { return show(false); });
    edit.Commit();
}

} // namespace vdev
