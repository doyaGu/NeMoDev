// The 3D Layout's Snapshot Buffer (Dev.exe sub_5BBB20, dialog 733; the
// buffer itself sub_4C5860, SnapshotCommands).
#include "ui/layout/LayoutWindow.h"


#include <algorithm>

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/DevControls.h"
#include "ui/setup/SetupLayout.h"
#include "imgui_internal.h"
#include "ui/schematic/Snapshots.h"

namespace vdev {

namespace {
ImTextureID SlotTexture(const SnapshotData &buffer, int slot, unsigned *revisions) {
    const unsigned long long key = 0x534E415000000000ull | static_cast<unsigned>(slot);
    if (revisions[slot] != buffer.Revision(slot) || !CachedPixelTexture(key)) {
        revisions[slot] = buffer.Revision(slot);
        return PixelTexture(key, SnapshotData::WIDTH, SnapshotData::HEIGHT, buffer.Pixels(slot).data(),
                            SnapshotData::WIDTH * 4, false);
    }
    return CachedPixelTexture(key);
}
} // namespace

// Take (Dev.exe sub_4A1C70, sub_4C5B20): the whole view stretched into the
// first empty slot.
void LayoutWindow::TakeSnapshot() { Execute("snapshot.take"); }

// Capture (Dev.exe sub_5BC110): once the buttons are released a 104 x 80
// box follows the mouse; the left button captures what is under it, the
// right button or Esc gives up.
void LayoutWindow::CaptureInput(const ImVec2 &at) {
    if (!Window().capturing)
        return;
    const ImGuiIO &io = ImGui::GetIO();
    if (Window().capturing == 1) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
            Window().capturing = 2;
        return;
    }
    const ImVec2 mouse = io.MousePos;
    ImGui::GetForegroundDrawList()->AddRect(mouse, ImVec2(mouse.x + 104.0f, mouse.y + 80.0f),
                                            CurrentDevTheme().snapshotBackground, 0.0f, 0, 2.0f);
    ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        Window().capturing = 0;
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Window().capturing = 0;
        EditorValue x{EDITOR_TEXT("x"), EDITOR_REAL_VALUE}, y{EDITOR_TEXT("y"), EDITOR_REAL_VALUE};
        x.real = int(mouse.x - at.x);
        y.real = int(mouse.y - at.y);
        Execute("snapshot.capture", {x, y});
    }
}

// Load Bitmap (Dev.exe sub_5BC900): an image read by CK's bitmap readers
// (CKLoadBitmap), stretched to the slot (ImageList_Replace).
void LayoutWindow::LoadSnapshot(int slot) {
    Execute("snapshot.load",
            {IntegerArgument("slot", slot), IntegerArgument("revision", Window().snapshots.Revision(slot))});
}
void LayoutWindow::SaveSnapshot(int slot) {
    Execute("snapshot.save",
            {IntegerArgument("slot", slot), IntegerArgument("revision", Window().snapshots.Revision(slot))});
}

// The dialog: the 36 slots at Dev's places (sub_5BBF90: from (10, 10),
// 56 apart across, 48 down), the empty ones with Dev's empty picture
// (resvis.dll 929); Take, Capture and Close below.
void LayoutWindow::SnapshotWindow() {
    if (!Window().snapshotOpen)
        return;
    if (!ReadSnapshots(Client(), Window().snapshots))
        return;
    const auto &buffer = Window().snapshots;
    const float s = setup::PageUnit();
    vi::DialogLayout layout;
    bool open = true;
    if (!vi::BeginScrollableWindow(AuxiliaryName("Snapshot Buffer").c_str(), "Snapshot Buffer",
                                   {362 * s, 304 * s}, s, layout, 1, &open)) {
        vi::EndWindow();
        if (!open)
            Window().snapshotOpen = false;
        return;
    }
    ImGui::GetWindowDrawList()->AddRectFilled(
        {layout.contentMin.x + s, layout.contentMin.y + s},
        {layout.contentMin.x + layout.contentSize.x - s, layout.contentMin.y + layout.contentSize.y - s},
        CurrentDevTheme().snapshotBackground);
    auto selected = [&](int slot) {
        return std::find(Window().snapshotSelection.begin(), Window().snapshotSelection.end(), slot) !=
               Window().snapshotSelection.end();
    };
    bool menu = false;
    if (vi::BeginDialogContent("##snapshot-slots", layout)) {
        auto *dl = ImGui::GetWindowDrawList();
        const auto contentId = ImGui::GetID("");
        const int columns = std::clamp(int(ImGui::GetContentRegionAvail().x / (56 * s)), 1, 6);
        if (ImGui::BeginTable("##slots", columns, ImGuiTableFlags_NoSavedSettings)) {
            ImGui::PushOverrideID(contentId);
            for (int slot = 0; slot < SnapshotData::COUNT; ++slot) {
                ImGui::TableNextColumn();
                const auto at = ImGui::GetCursorScreenPos();
                const ImVec2 size(SnapshotData::WIDTH * s, SnapshotData::HEIGHT * s);
                ImGui::SetCursorScreenPos(at);
                ImGui::PushID(slot);
                ImGui::InvisibleButton("##slot", size,
                                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
                const bool hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    if (ImGui::GetIO().KeyCtrl) {
                        auto found = std::find(Window().snapshotSelection.begin(),
                                               Window().snapshotSelection.end(), slot);
                        if (found != Window().snapshotSelection.end())
                            Window().snapshotSelection.erase(found);
                        else
                            Window().snapshotSelection.push_back(slot);
                    } else {
                        Window().snapshotSelection.assign(1, slot);
                    }
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    if (!selected(slot))
                        Window().snapshotSelection.assign(1, slot);
                    Window().snapshotMenu = slot;
                    menu = true;
                }
                // Dragged onto a Script header in the Schematic: its snapshot.
                if (buffer.Has(slot) && ImGui::BeginDragDropSource()) {
                    const SnapshotDrag drag{uint32_t(slot), buffer.Revision(slot)};
                    ImGui::SetDragDropPayload(SNAPSHOT_PAYLOAD, &drag, sizeof(drag), ImGuiCond_Once);
                    if (ImTextureID texture = SlotTexture(buffer, slot, Window().snapshotRevisions))
                        ImGui::Image(texture,
                                     ImVec2(SnapshotData::WIDTH * 1.0f, SnapshotData::HEIGHT * 1.0f));
                    ImGui::EndDragDropSource();
                }
                ImGui::PopID();
                ImTextureID texture =
                    buffer.Has(slot) ? SlotTexture(buffer, slot, Window().snapshotRevisions) : DevBitmap(929);
                if (texture)
                    dl->AddImage(texture, at, ImVec2(at.x + size.x, at.y + size.y));
                else
                    dl->AddRect(at, ImVec2(at.x + size.x, at.y + size.y),
                                CurrentDevTheme().snapshotEmptyBorder);
                if (selected(slot))
                    dl->AddRect(ImVec2(at.x - 2.0f * s, at.y - 2.0f * s),
                                ImVec2(at.x + size.x + 2.0f * s, at.y + size.y + 2.0f * s),
                                CurrentDevTheme().selectionBorder, 0.0f, 0, 2.0f * s);
                else if (hovered)
                    dl->AddRect(ImVec2(at.x - 1.0f * s, at.y - 1.0f * s),
                                ImVec2(at.x + size.x + 1.0f * s, at.y + size.y + 1.0f * s),
                                CurrentDevTheme().hoverBorder);
            }
            ImGui::PopID();
            ImGui::EndTable();
        }
    }
    vi::EndDialogContent();
    if (menu)
        ImGui::OpenPopup("##snapshotmenu");
    // The shortcut menu (sub_5BC500): Load bitmap; with an image Save as...
    // and Delete (Del).
    if (ImGui::BeginPopup("##snapshotmenu")) {
        const int slot = Window().snapshotMenu;
        if (ImGui::MenuItem("Load bitmap")) LoadSnapshot(slot);
        if (buffer.Has(slot)) {
            if (ImGui::MenuItem("Save as...")) SaveSnapshot(slot);
            if (ImGui::MenuItem("Delete", "Del"))
                RemoveSnapshots(Client(), buffer, {slot});
        }
        ImGui::EndPopup();
    }
    // Del deletes the slots selected (LVN_KEYDOWN).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete))
        RemoveSnapshots(Client(), buffer, Window().snapshotSelection);
    // Take, Capture, Close (Dev's custom buttons 1, 2 and 4).
    const float buttonWidth =
        std::min(80.0f * s, (layout.right - layout.contentMin.x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3);
    ImGui::SetCursorScreenPos({layout.contentMin.x, layout.buttonsY});
    ImGui::BeginDisabled(!m_Started);
    if (ImGui::Button("Take", ImVec2(buttonWidth, 0.0f))) TakeSnapshot();
    ImGui::SameLine();
    if (ImGui::Button("Capture", ImVec2(buttonWidth, 0.0f)))
        Window().capturing = 1;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetCursorScreenPos({layout.right - buttonWidth, layout.buttonsY});
    if (ImGui::Button("Close", ImVec2(buttonWidth, 0.0f))) open = false;
    vi::EndWindow();
    if (!open) {
        Window().snapshotOpen = false;
        Window().capturing = 0;
    }
}

} // namespace vdev
