// The 3D Layout's Selection Groups and its 3D Layout Explorer (Dev.exe
// sub_4B95C0 id 18, sub_4C3D10; the Filter Box dialog sub_59D400).
#include "ui/layout/LayoutWindow.h"
#include "core/layout/LayoutExplorerTypes.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <map>

#include "ui/controls/DevIcons.h"
#include "ui/controls/DevBitmaps.h"
#include "features/layout/LayoutSceneData.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

namespace {

void CopyText(char *buffer, size_t size, const std::string &text) {
    std::strncpy(buffer, text.c_str(), size - 1);
    buffer[size - 1] = 0;
}

std::string Lower(std::string text) {
    for (char &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

} // namespace

// ------------------------------------------------------------ Selection Groups

// Dev.exe sub_4C3D10 (6): after a selection change the combo shows the first
// group holding exactly the objects selected, else nothing.
void LayoutWindow::SelectionGroups(const ImVec2 &at, float width) {
    auto &groups = Window().groupData;
    auto &selection = Window().selectionData;
    const bool usable = m_Started && ReadLayoutScene(Client(), "groups", groups) && groups.scene && ReadLayoutSelection(Client(), selection);
    if (usable) {
        if (m_Group && (!groups.Find(m_Group.object) || groups.Find(m_Group.object)->object != m_Group)) m_Group = {};
        if (m_GroupRevision != selection.revision) { m_GroupRevision = unsigned(selection.revision); m_Group = groups.matchingGroup; }
    }
    if (!Window().groupEditing) {
        const auto *group = usable && m_Group ? groups.Find(m_Group.object) : nullptr;
        CopyText(Window().groupText, sizeof(Window().groupText), group ? group->name : std::string());
    }
    const float arrow = ImGui::GetFrameHeight();
    ImGui::SetCursorScreenPos(at);
    ImGui::SetNextItemWidth(std::max(1.0f, width - arrow));
    ImGui::BeginDisabled(!usable);
    const bool entered = ImGui::InputText("##selectiongroup", Window().groupText, sizeof(Window().groupText),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    Window().groupEditing = ImGui::IsItemActive() && !entered;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !Window().groupEditing)
        ImGui::SetTooltip("Selection Groups");
    if (entered)
        EnterGroup(Window().groupText);
    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::ArrowButton("##selectiongroups", ImGuiDir_Down)) ImGui::OpenPopup("##grouplist");
    ImGui::EndDisabled();
    ImGui::SetNextWindowPos(ImVec2(at.x, at.y + arrow));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(FLT_MAX, 300.0f * Window().scale));
    if (ImGui::BeginPopup("##grouplist")) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
        int listed = 0;
        for (const auto &group : groups.rows) {
            ImGui::PushID(int(group.object.object));
            if (ImGui::Selectable(group.name.c_str(), group.object == m_Group)) ChooseGroup(group.object);
            ImGui::PopID();
            ++listed;
        }
        if (!listed) ImGui::TextDisabled("No group in the scene");
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }
}

// Dev.exe sub_4B95C0 (0x7E8, Enter in the combo): the name of a group listed
// selects it, another name makes a group of the selection.
void LayoutWindow::EnterGroup(const std::string &name) {
    if (name.empty() || !ReadLayoutScene(Client(), "groups", Window().groupData))
        return;
    for (const auto &group : Window().groupData.rows)
        if (Lower(group.name) == Lower(name)) {
            ChooseGroup(group.object);
            return;
        }
    LayoutSelectionData selection;
    if (ReadLayoutSelection(Client(), selection))
        Execute("layout.create-selection-group", {TextArgument("name", name), ObjectsArgument("objects", selection.objects)}, AfterCommand::Group);
}

// Dev.exe sub_4B95C0 (id 18, CBN_SELCHANGE): not while the selection is
// locked; the group's 3D and 2D entities replace the selection.
void LayoutWindow::ChooseGroup(ObjectRef group) {
    Execute("layout.choose-group", {ObjectArgument("target", group)});
    m_GroupRevision = ~0u;
}

// Dev.exe sub_4B46C0: a group that exists is shown and its objects are
// selected, else the objects saved are; then the lock.


// Dev.exe sub_4B4E50: the group shown, else the selection; the lock.


// ------------------------------------------------------------ 3D Layout Explorer

void LayoutWindow::OpenExplorer() {
    if (Window().document.explorerOpen) {
        ImGui::SetWindowFocus("3D Layout Explorer");
        return;
    }
    LayoutSceneData scene; LayoutSelectionData selection;
    if (!ReadLayoutScene(Client(), "explorer", scene) || !scene.scene || !ReadLayoutSelection(Client(), selection)) return;
    Window().document.explorerOpen = true;
    Window().document.explorerSaved = selection.objects;
    Window().document.explorerSavedLock = selection.locked;
    Window().document.explorerAnchor = Window().document.explorerFocus = {};
    Window().document.explorerListRevision = ~0u;
    Window().explorerTyped.clear();
}

// Select keeps the selection; Cancel gives back the selection and the lock
// of the opening (Dev.exe sub_59E270, IDCANCEL). Both keep the types shown.
void LayoutWindow::CloseExplorer(bool cancel) {
    if (cancel && Client().Scope().document.value) {
        std::vector<ObjectRef> saved;
        for (auto object : Window().document.explorerSaved)
            if (Client().Query("object.details", {ObjectArgument("target", object)}).status == EDITOR_OK) saved.push_back(object);
        Execute("selection.restore",
                {ObjectsArgument("objects", saved), IntegerArgument("locked", Window().document.explorerSavedLock)});
    }
    Window().document.explorerOpen = false;
}

void LayoutWindow::ExplorerWindow() {
    if (!Window().document.explorerOpen)
        return;
    auto &scene = Window().explorerData;
    auto &selection = Window().selectionData;
    if (!ReadLayoutScene(Client(), "explorer", scene,
                         {IntegerArgument("shown", Window().explorerShown),
                          TextArgument("filter", std::string(Window().explorerFilter))}) ||
        !scene.scene || !ReadLayoutSelection(Client(), selection)) {
        Window().document.explorerOpen = false;
        return;
    }
    // Dialog 734 (MS Sans Serif 8: a dialog unit is 1.5 x 1.625 pixels).
    const float dx = 1.5f * Window().scale, dy = 1.625f * Window().scale;
    const ImGuiStyle &style = ImGui::GetStyle();
    const float footer = ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.0f;
    ImGui::SetNextWindowSize(ImVec2(381.0f * dx + style.WindowPadding.x * 2.0f,
                                    217.0f * dy + footer + style.WindowPadding.y * 2.0f + ImGui::GetFrameHeight()),
                             ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool open = true;
    const bool visible = ImGui::Begin("3D Layout Explorer", &open,
                                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
                                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    if (!visible) {
        ImGui::End();
        if (!open) CloseExplorer(true);
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    auto place = [&](float x, float y) { ImGui::SetCursorScreenPos(ImVec2(origin.x + x * dx, origin.y + y * dy)); };
    const bool locked = selection.locked;
    const auto &rows = scene.rows;
    auto indexOf = [&](ObjectRef id) {
        for (size_t k = 0; k < rows.size(); ++k)
            if (rows[k].object == id) return static_cast<int>(k);
        return -1;
    };
    auto listed = [&](ObjectRef id) {
        return std::find(Window().document.explorerList.begin(), Window().document.explorerList.end(), id) !=
               Window().document.explorerList.end();
    };
    // sub_59EC90: the list shows the 3D Layout's selection again after each
    // change of it (and when the types shown change), with its count.
    auto restore = [&] {
        Window().document.explorerList.clear();
        for (const auto &row : rows)
            if (selection.Contains(row.object.object))
                Window().document.explorerList.push_back(row.object);
        Window().document.explorerCount = static_cast<int>(Window().document.explorerList.size());
        Window().document.explorerListRevision = selection.revision;
    };
    if (Window().document.explorerListRevision != selection.revision)
        restore();
    // The list in its order.
    auto ordered = [&](const std::vector<ObjectRef> &ids) {
        std::vector<ObjectRef> result;
        for (const auto &row : rows)
            if (std::find(ids.begin(), ids.end(), row.object) != ids.end()) result.push_back(row.object);
        return result;
    };
    // sub_59EF40: the 3D Layout's selection made the list's (NM_CLICK and the
    // list buttons); while locked the list shows the 3D Layout's again.
    auto apply = [&] {
        if (locked) {
            restore();
            return;
        }
        Window().document.explorerList = ordered(Window().document.explorerList);
        Execute("selection.replace",
                {ObjectsArgument("objects", Window().document.explorerList), IntegerArgument("render-only", 1)});
        ReadLayoutSelection(Client(), selection); restore();
    };

    place(0, 2);
    ImGui::TextUnformatted("Item List");
    place(0, 14);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 255, 255));
    ImGui::BeginChild("##items", ImVec2(230.0f * dx, 181.0f * dy), ImGuiChildFlags_Borders);
    const ImGuiIO &io = ImGui::GetIO();
    const bool listFocused = ImGui::IsWindowFocused();
    bool clicked = false, doubleClicked = false, rightClicked = false, scrollToFocus = false;
    const float rowHeight = ImGui::GetTextLineHeight() + 2.0f * Window().scale;
    const float iconScale = std::max(1.0f, ImGui::GetTextLineHeight() / 13.0f);
    // The list view's selection by the mouse: alone, Ctrl toggles, Shift the
    // items from the anchor (with Ctrl added to the others).
    auto pick = [&](int i, bool shift, bool ctrl) {
        const auto id = rows[static_cast<size_t>(i)].object;
        const int anchor = shift ? indexOf(Window().document.explorerAnchor) : -1;
        if (anchor >= 0) {
            if (!ctrl)
                Window().document.explorerList.clear();
            for (int k = std::min(anchor, i); k <= std::max(anchor, i); ++k)
                if (!listed(rows[static_cast<size_t>(k)].object))
                    Window().document.explorerList.push_back(rows[static_cast<size_t>(k)].object);
        } else if (ctrl) {
            auto found = std::find(Window().document.explorerList.begin(), Window().document.explorerList.end(), id);
            if (found != Window().document.explorerList.end())
                Window().document.explorerList.erase(found);
            else
                Window().document.explorerList.push_back(id);
            Window().document.explorerAnchor = id;
        } else {
            Window().document.explorerList.assign(1, id);
            Window().document.explorerAnchor = id;
        }
        Window().document.explorerFocus = id;
    };
    // The keyboard (the list view's own handling): the arrows, Home, End,
    // Page Up / Down move the focus and select it alone, Shift the items from
    // the anchor, Ctrl only moves the focus; Space selects the focused item,
    // Ctrl+Space toggles it; typing selects the next item whose name starts
    // with what was typed. The 3D Layout's selection follows at once (Dev's
    // dialog has no LVN_ITEMCHANGED and waits for the next click).
    if (listFocused && !rows.empty() && !io.KeyAlt) {
        const int focus = indexOf(Window().document.explorerFocus);
        const int last = static_cast<int>(rows.size()) - 1;
        const int page = std::max(1, static_cast<int>(ImGui::GetWindowHeight() / rowHeight) - 1);
        int to = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) to = focus < 0 ? 0 : std::min(last, focus + 1);
        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) to = focus < 0 ? 0 : std::max(0, focus - 1);
        else if (ImGui::IsKeyPressed(ImGuiKey_Home)) to = 0;
        else if (ImGui::IsKeyPressed(ImGuiKey_End)) to = last;
        else if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) to = focus < 0 ? 0 : std::min(last, focus + page);
        else if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) to = focus < 0 ? 0 : std::max(0, focus - page);
        if (to >= 0) {
            if (io.KeyCtrl && !io.KeyShift) {
                Window().document.explorerFocus = rows[static_cast<size_t>(to)].object;
            } else {
                if (!io.KeyShift || indexOf(Window().document.explorerAnchor) < 0)
                    Window().document.explorerAnchor = rows[static_cast<size_t>(to)].object;
                pick(to, io.KeyShift, io.KeyCtrl);
                clicked = true;
            }
            scrollToFocus = true;
            Window().explorerTyped.clear();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Space) && focus >= 0) {
            pick(focus, io.KeyShift, io.KeyCtrl);
            clicked = true;
        } else if (!io.KeyCtrl && !io.InputQueueCharacters.empty()) {
            // Incremental search: the characters typed within a second.
            const double now = ImGui::GetTime();
            if (now - Window().explorerTypedTime > 1.0)
                Window().explorerTyped.clear();
            Window().explorerTypedTime = now;
            for (ImWchar c : io.InputQueueCharacters)
                if (c > ' ' && c < 0x80)
                    Window().explorerTyped.push_back(static_cast<char>(std::tolower(c)));
            if (!Window().explorerTyped.empty()) {
                // The same letter again goes to the next item starting with it.
                const bool repeated =
                    Window().explorerTyped.find_first_not_of(Window().explorerTyped[0]) == std::string::npos &&
                    Window().explorerTyped.size() > 1;
                const std::string prefix = repeated ? Window().explorerTyped.substr(0, 1) : Window().explorerTyped;
                const int start = focus < 0 ? 0 : (repeated || Window().explorerTyped.size() == 1 ? focus + 1 : focus);
                for (int n = 0; n <= last; ++n) {
                    const int k = (start + n) % (last + 1);
                    if (Lower(rows[static_cast<size_t>(k)].name).compare(0, prefix.size(), prefix) == 0) {
                        Window().document.explorerAnchor = rows[static_cast<size_t>(k)].object;
                        pick(k, false, false);
                        clicked = true;
                        scrollToFocus = true;
                        break;
                    }
                }
            }
        }
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        ImGui::PushID(static_cast<int>(row.object.object));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool selected = listed(row.object);
        if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, rowHeight))) {
            clicked = true;
            pick(static_cast<int>(i), io.KeyShift, io.KeyCtrl);
            doubleClicked = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            // The list view selects the item clicked when it is not (here
            // the 3D Layout's selection too, Dev leaves it).
            if (!selected) {
                Window().document.explorerList.assign(1, row.object);
                Window().document.explorerAnchor = row.object;
                clicked = true;
            }
            Window().document.explorerFocus = row.object;
            rightClicked = true;
        }
        if (scrollToFocus && row.object == Window().document.explorerFocus)
            ImGui::SetScrollHereY();
        DrawDevIcon(ImGui::GetWindowDrawList(),
                    ImVec2(at.x + 2.0f * Window().scale, at.y + (rowHeight - 10.0f * iconScale) * 0.5f), iconScale,
                    row.icon, IM_COL32(0, 0, 0, 255), IM_COL32(230, 230, 230, 255));
        ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + 16.0f * Window().scale, at.y + 1.0f * Window().scale),
                                            ImGui::GetColorU32(ImGuiCol_Text), row.name.c_str());
        // The focused item's dotted frame while the list has the focus.
        if (listFocused && row.object == Window().document.explorerFocus) {
            const ImVec2 b(at.x + ImGui::GetContentRegionAvail().x, at.y + rowHeight);
            for (float x = at.x; x < b.x; x += 2.0f) {
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, at.y), ImVec2(x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 255));
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, b.y - 1.0f), ImVec2(x + 1.0f, b.y), IM_COL32(0, 0, 0, 255));
            }
            for (float y = at.y; y < b.y; y += 2.0f) {
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, y), ImVec2(at.x + 1.0f, y + 1.0f), IM_COL32(0, 0, 0, 255));
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(b.x - 1.0f, y), ImVec2(b.x, y + 1.0f), IM_COL32(0, 0, 0, 255));
            }
        }
        ImGui::PopID();
    }
    // A click below the items clears the list's selection (then NM_CLICK).
    if (!clicked && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (!io.KeyCtrl)
            Window().document.explorerList.clear();
        clicked = true;
    }
    // NM_CLICK (Dev.exe sub_59E270), and here the keyboard: the 3D Layout's
    // selection becomes the list's (objects not listed leave it); while
    // locked the list shows the selection again (sub_59EC90).
    if (clicked) apply();
    std::vector<ObjectRef> listedSelected = ordered(Window().document.explorerList);
    // NM_DBLCLK, NM_RCLICK: Show Setup with one item selected.
    if (doubleClicked && listedSelected.size() == 1) Execute("object.open-setup", {ObjectArgument("target", listedSelected.front())});
    if (rightClicked && listedSelected.size() == 1) ImGui::OpenPopup("##explorermenu");
    if (ImGui::BeginPopup("##explorermenu")) {
        if (ImGui::MenuItem("Show Setup") && listedSelected.size() == 1)
            Execute("object.open-setup", {ObjectArgument("target", listedSelected.front())});
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Show in List.
    place(237, 2);
    ImGui::TextUnformatted("Show in List");
    {
        const ImVec2 a(origin.x + 237.0f * dx, origin.y + 14.0f * dy);
        ImGui::GetWindowDrawList()->AddRect(a, ImVec2(a.x + 144.0f * dx, a.y + 181.0f * dy), ImGui::GetColorU32(ImGuiCol_Border));
    }
    struct Box {
        const char *label;
        unsigned bit;
        float y;
    };
    static const Box boxes[] = {{"Lights", EXPLORER_LIGHTS, 22},         {"Cameras", EXPLORER_CAMERAS, 33},
                                {"2D Sprites", EXPLORER_2D_SPRITES, 44}, {"3D Sprites", EXPLORER_3D_SPRITES, 55},
                                {"3D Objects", EXPLORER_3D_OBJECTS, 66}, {"Characters", EXPLORER_CHARACTERS, 77},
                                {"Frames", EXPLORER_FRAMES, 88},         {"Curves", EXPLORER_CURVES, 99},
                                {"Scripted Objects", EXPLORER_SCRIPTED, 110}};
    // The types of the 3D Layout's selection the list shows (sub_59EC90, +2560).
    const unsigned selectedTypes = scene.selectedTypes;
    bool typesChanged = false;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f * Window().scale, 0.0f));
    for (const Box &box : boxes) {
        place(243, box.y);
        bool on = (Window().explorerShown & box.bit) != 0;
        if (ImGui::Checkbox(box.label, &on)) {
            // Dev refuses while objects of the type are selected.
            if (selectedTypes & box.bit)
                Window().document.explorerRefused = true;
            else {
                Window().explorerShown ^= box.bit;
                typesChanged = true;
            }
        }
    }
    ImGui::PopStyleVar();
    if (locked) {
        place(243, 131);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Selection Locked");
    }
    place(243, 142);
    // sub_59F2B0: the count of the 3D Layout's selection the list shows.
    ImGui::Text(Window().document.explorerCount >= 2 ? "%d Entities Selected." : "%d Entity Selected.",
                Window().document.explorerCount);
    place(243, 162);
    ImGui::TextUnformatted("Text Filter");
    place(243, 175);
    ImGui::SetNextItemWidth(123.0f * dx);
    // A change refills the list; it shows the 3D Layout's selection (Dev's
    // refilled list has none selected, sub_59EFF0).
    if (ImGui::InputText("##textfilter", Window().explorerFilter, sizeof(Window().explorerFilter)))
        Window().document.explorerListRevision = ~0u;
    // New types shown: the list again, with the 3D Layout's selection.
    if (typesChanged)
        Window().document.explorerListRevision = ~0u;

    // All / None / Invert: the list's items (not while locked).
    auto listButton = [&](const char *label, float x) {
        place(x, 201);
        return ImGui::Button(label, ImVec2(40.0f * dx, 14.0f * dy));
    };
    int listAction = 0;
    if (listButton("All", 237)) listAction = 1;
    if (listButton("None", 289)) listAction = 2;
    if (listButton("Invert", 341)) listAction = 3;
    if (listAction && !locked) {
        // The list's items, then sub_59EF40.
        std::vector<ObjectRef> objects;
        for (const auto &row : rows) {
            const bool was = listed(row.object);
            if (listAction == 1 || (listAction == 3 && !was)) objects.push_back(row.object);
        }
        Window().document.explorerList = objects;
        apply();
    }

    // Dev's buttons: Select and Cancel.
    const float buttonWidth = 80.0f * Window().scale;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 381.0f * dx - 2.0f * buttonWidth - style.ItemSpacing.x,
                                     origin.y + 217.0f * dy + style.ItemSpacing.y));
    bool close = false, cancel = false;
    if (ImGui::Button("Select", ImVec2(buttonWidth, 0.0f))) close = true;
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f))) close = cancel = true;

    if (Window().document.explorerRefused) {
        ImGui::OpenPopup("3D Layout Explorer##refused");
        Window().document.explorerRefused = false;
    }
    if (ImGui::BeginPopupModal("3D Layout Explorer##refused", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            "Some objects of this type are selected !\nPlease remove them from the selection before unchecking this button.");
        if (ImGui::Button("OK", ImVec2(90.0f * Window().scale, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsKeyPressed(ImGuiKey_Enter))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
    if (!open) close = cancel = true;
    if (close) CloseExplorer(cancel);
}

} // namespace vdev
