// The 3D Layout's Selection Groups and its 3D Layout Explorer (Dev.exe
// sub_4B95C0 id 18, sub_4C3D10; the Filter Box dialog sub_59D400).
#include "ui/layout/LayoutWindow.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <map>

#include "ui/controls/DevIcons.h"
#include "core/layout/LayoutExplorer.h"
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
void LayoutWindow::SyncGroup() {
    CKContext *context = m_State.host.Context();
    if (m_Group) {
        CKObject *group = context ? context->GetObject(m_Group) : nullptr;
        if (!CKGroup::Cast(group) || (m_State.HasDocument() && m_State.document->IsDiscarded(m_Group)))
            m_Group = 0;
    }
    if (m_GroupRevision == m_State.objectSelection.revision) return;
    m_GroupRevision = m_State.objectSelection.revision;
    m_Group = m_State.HasDocument() ? MatchingGroup(context, SceneGroups(context, m_View.Scene()), AliveSelection()) : 0;
}

void LayoutWindow::SelectionGroups(const ImVec2 &at, float width) {
    CKContext *context = m_State.host.Context();
    const bool usable = m_Started && m_State.HasDocument() && m_View.Scene();
    if (usable) SyncGroup();
    if (!m_GroupEditing) {
        CKObject *group = usable && m_Group ? context->GetObject(m_Group) : nullptr;
        CopyText(m_GroupText, sizeof(m_GroupText), group ? DevObjectName(group) : std::string());
    }
    const float arrow = ImGui::GetFrameHeight();
    ImGui::SetCursorScreenPos(at);
    ImGui::SetNextItemWidth(std::max(1.0f, width - arrow));
    ImGui::BeginDisabled(!usable);
    const bool entered =
        ImGui::InputText("##selectiongroup", m_GroupText, sizeof(m_GroupText), ImGuiInputTextFlags_EnterReturnsTrue);
    m_GroupEditing = ImGui::IsItemActive() && !entered;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !m_GroupEditing) ImGui::SetTooltip("Selection Groups");
    if (entered) EnterGroup(m_GroupText);
    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::ArrowButton("##selectiongroups", ImGuiDir_Down)) ImGui::OpenPopup("##grouplist");
    ImGui::EndDisabled();
    ImGui::SetNextWindowPos(ImVec2(at.x, at.y + arrow));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(FLT_MAX, 300.0f * m_Scale));
    if (ImGui::BeginPopup("##grouplist")) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
        int listed = 0;
        for (CK_ID id : usable ? SceneGroups(context, m_View.Scene()) : std::vector<CK_ID>()) {
            CKObject *group = context->GetObject(id);
            if (!group) continue;
            ImGui::PushID(static_cast<int>(id));
            if (ImGui::Selectable(DevObjectName(group).c_str(), id == m_Group)) ChooseGroup(id);
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
    CKContext *context = m_State.host.Context();
    CKScene *scene = m_View.Scene();
    if (name.empty() || !m_State.HasDocument() || !scene) return;
    // CB_FINDSTRINGEXACT: the whole name, any case.
    for (CK_ID id : SceneGroups(context, scene)) {
        CKObject *group = context->GetObject(id);
        if (group && _stricmp(DevObjectName(group).c_str(), name.c_str()) == 0) {
            ChooseGroup(id);
            return;
        }
    }
    const std::vector<CK_ID> selection = AliveSelection();
    if (selection.empty()) return;
    const EditResult result =
        CreateSelectionGroup(*m_State.document, scene->GetID(), Utf8ToAnsi(name), selection);
    if (result.ok) {
        m_Group = result.created;
        m_GroupRevision = m_State.objectSelection.revision;
    } else {
        m_State.Log(LogLevel::Warning, "Selection Groups: " + AnsiToUtf8(result.message));
    }
}

// Dev.exe sub_4B95C0 (id 18, CBN_SELCHANGE): not while the selection is
// locked; the group's 3D and 2D entities replace the selection.
void LayoutWindow::ChooseGroup(CK_ID id) {
    if (m_State.objectSelection.locked) return;
    const std::vector<CK_ID> objects = GroupSelection(m_State.host.Context(), id);
    if (objects.empty()) return;
    m_Group = id;
    SetSelection(objects);
    m_GroupRevision = ~0u; // matched again, as Dev's selection notification does
}

// Dev.exe sub_4B46C0: a group that exists is shown and its objects are
// selected, else the objects saved are; then the lock.
void LayoutWindow::ApplyLayoutData(const LayoutInterfaceData &data) {
    m_Interface = data;
    m_Group = 0;
    m_GroupRevision = ~0u;
    if (!data.present || !m_State.HasDocument()) return;
    CKContext *context = m_State.host.Context();
    std::vector<CK_ID> objects;
    if (CKGroup *group = CKGroup::Cast(context->GetObject(data.group))) {
        for (int i = 0; i < group->GetObjectCount(); ++i)
            if (CKBeObject *member = group->GetObject(i)) objects.push_back(member->GetID());
    } else {
        for (CK_ID id : data.selection)
            if (context->GetObject(id)) objects.push_back(id);
    }
    m_State.objectSelection.locked = false;
    m_State.objectSelection.Set(objects); // a new revision
    m_State.objectSelection.locked = data.locked && !objects.empty();
    if (context->GetObject(data.group)) {
        m_Group = data.group;
        m_GroupRevision = m_State.objectSelection.revision;
    }
    // The lists (core ReadLayoutLists), the camera.
    LayoutLists lists = ReadLayoutLists(context, data);
    m_State.layoutCones = std::move(lists.cones);
    m_State.playerShown = std::move(lists.playerShown);
    m_State.frameRepresentations = std::move(lists.representations);
    m_State.hiddenCurvePoints = std::move(lists.hiddenCurvePoints);
    if (!data.cameras.empty() && CKCamera::Cast(context->GetObject(data.cameras.front())))
        m_View.SetCamera(data.cameras.front());
}

// Dev.exe sub_4B4E50: the group shown, else the selection; the lock.
LayoutInterfaceData LayoutWindow::LayoutData() const {
    LayoutInterfaceData data = m_Interface;
    data.present = true;
    data.group = m_Group;
    data.selection = AliveSelection();
    data.locked = m_State.objectSelection.locked;
    // The lists, from the editor's state (sub_4B4E50); +180 as read.
    CKContext *context = m_State.host.Context();
    auto live = [&](CK_ID id) {
        return context && context->GetObject(id) && !m_View.IsInterfaceObject(id) &&
               !(m_State.HasDocument() && m_State.document->IsDiscarded(id));
    };
    LayoutLists lists;
    lists.cones = m_State.layoutCones;
    lists.playerShown = m_State.playerShown;
    lists.representations = m_State.frameRepresentations;
    lists.hiddenCurvePoints = m_State.hiddenCurvePoints;
    WriteLayoutLists(context, lists, live, data);
    // The camera, unless one of the 3D Layout's own views (not saved).
    CKCamera *camera = m_View.Camera();
    data.cameras.assign(1, camera && !m_View.IsViewCamera(camera->GetID()) ? camera->GetID() : 0);
    return data;
}

// ------------------------------------------------------------ 3D Layout Explorer

void LayoutWindow::OpenExplorer() {
    if (m_Doc.explorerOpen) {
        ImGui::SetWindowFocus("3D Layout Explorer");
        return;
    }
    if (!m_State.HasDocument() || !m_View.Scene()) return;
    m_Doc.explorerOpen = true;
    m_Doc.explorerSaved = AliveSelection();
    m_Doc.explorerSavedLock = m_State.objectSelection.locked;
    m_Doc.explorerAnchor = m_Doc.explorerFocus = 0;
    m_Doc.explorerListRevision = ~0u; // OnInitDialog: the 3D Layout's selection (sub_59EC90)
    m_ExplorerTyped.clear();
}

// Select keeps the selection; Cancel gives back the selection and the lock
// of the opening (Dev.exe sub_59E270, IDCANCEL). Both keep the types shown.
void LayoutWindow::CloseExplorer(bool cancel) {
    if (cancel && m_State.HasDocument()) {
        std::vector<CK_ID> saved;
        CKContext *context = m_State.host.Context();
        for (CK_ID id : m_Doc.explorerSaved)
            if (context->GetObject(id) && !m_State.document->IsDiscarded(id)) saved.push_back(id);
        if (saved != m_State.objectSelection.objects) m_State.objectSelection.Set(saved);
        m_State.objectSelection.locked = m_Doc.explorerSavedLock && !saved.empty();
    }
    m_Doc.explorerOpen = false;
}

void LayoutWindow::ExplorerWindow() {
    if (!m_Doc.explorerOpen) return;
    CKContext *context = m_State.host.Context();
    CKScene *scene = m_View.Scene();
    if (!m_State.HasDocument() || !scene) {
        m_Doc.explorerOpen = false;
        return;
    }
    // Dialog 734 (MS Sans Serif 8: a dialog unit is 1.5 x 1.625 pixels).
    const float dx = 1.5f * m_Scale, dy = 1.625f * m_Scale;
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
    ObjectSelection &selection = m_State.objectSelection;
    const bool locked = selection.locked;

    // The list: the scene's objects of the types shown whose name holds the
    // filter, sorted by name.
    struct Row {
        CK_ID id;
        std::string name;
        CK_CLASSID cid;
    };
    std::vector<Row> rows;
    const std::string filter = Lower(m_ExplorerFilter);
    for (const auto &entry : ExplorerObjects(context, scene, m_ExplorerShown)) {
        if (m_State.document->IsDiscarded(entry.first) || m_View.IsInterfaceObject(entry.first)) continue;
        CKObject *object = context->GetObject(entry.first);
        std::string name = DevObjectName(object);
        if (!filter.empty() && Lower(name).find(filter) == std::string::npos) continue;
        rows.push_back({entry.first, std::move(name), object->GetClassID()});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return _stricmp(a.name.c_str(), b.name.c_str()) < 0; });
    auto indexOf = [&](CK_ID id) {
        for (size_t k = 0; k < rows.size(); ++k)
            if (rows[k].id == id) return static_cast<int>(k);
        return -1;
    };
    auto listed = [&](CK_ID id) { return std::find(m_Doc.explorerList.begin(), m_Doc.explorerList.end(), id) != m_Doc.explorerList.end(); };
    // sub_59EC90: the list shows the 3D Layout's selection again after each
    // change of it (and when the types shown change), with its count.
    auto restore = [&] {
        m_Doc.explorerList.clear();
        for (const Row &row : rows)
            if (selection.Contains(row.id)) m_Doc.explorerList.push_back(row.id);
        m_Doc.explorerCount = static_cast<int>(m_Doc.explorerList.size());
        m_Doc.explorerListRevision = selection.revision;
    };
    if (m_Doc.explorerListRevision != selection.revision) restore();
    // The list in its order.
    auto ordered = [&](const std::vector<CK_ID> &ids) {
        std::vector<CK_ID> result;
        for (const Row &row : rows)
            if (std::find(ids.begin(), ids.end(), row.id) != ids.end()) result.push_back(row.id);
        return result;
    };
    // sub_59EF40: the 3D Layout's selection made the list's (NM_CLICK and the
    // list buttons); while locked the list shows the 3D Layout's again.
    auto apply = [&] {
        if (locked) {
            restore();
            return;
        }
        m_Doc.explorerList = ordered(m_Doc.explorerList);
        SetSelection(m_Doc.explorerList);
        restore();
    };

    place(0, 2);
    ImGui::TextUnformatted("Item List");
    place(0, 14);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 255, 255));
    ImGui::BeginChild("##items", ImVec2(230.0f * dx, 181.0f * dy), ImGuiChildFlags_Borders);
    const ImGuiIO &io = ImGui::GetIO();
    const bool listFocused = ImGui::IsWindowFocused();
    bool clicked = false, doubleClicked = false, rightClicked = false, scrollToFocus = false;
    const float rowHeight = ImGui::GetTextLineHeight() + 2.0f * m_Scale;
    const float iconScale = std::max(1.0f, ImGui::GetTextLineHeight() / 13.0f);
    // The list view's selection by the mouse: alone, Ctrl toggles, Shift the
    // items from the anchor (with Ctrl added to the others).
    auto pick = [&](int i, bool shift, bool ctrl) {
        const CK_ID id = rows[static_cast<size_t>(i)].id;
        const int anchor = shift ? indexOf(m_Doc.explorerAnchor) : -1;
        if (anchor >= 0) {
            if (!ctrl) m_Doc.explorerList.clear();
            for (int k = std::min(anchor, i); k <= std::max(anchor, i); ++k)
                if (!listed(rows[static_cast<size_t>(k)].id)) m_Doc.explorerList.push_back(rows[static_cast<size_t>(k)].id);
        } else if (ctrl) {
            auto found = std::find(m_Doc.explorerList.begin(), m_Doc.explorerList.end(), id);
            if (found != m_Doc.explorerList.end()) m_Doc.explorerList.erase(found);
            else m_Doc.explorerList.push_back(id);
            m_Doc.explorerAnchor = id;
        } else {
            m_Doc.explorerList.assign(1, id);
            m_Doc.explorerAnchor = id;
        }
        m_Doc.explorerFocus = id;
    };
    // The keyboard (the list view's own handling): the arrows, Home, End,
    // Page Up / Down move the focus and select it alone, Shift the items from
    // the anchor, Ctrl only moves the focus; Space selects the focused item,
    // Ctrl+Space toggles it; typing selects the next item whose name starts
    // with what was typed. The 3D Layout's selection follows at once (Dev's
    // dialog has no LVN_ITEMCHANGED and waits for the next click).
    if (listFocused && !rows.empty() && !io.KeyAlt) {
        const int focus = indexOf(m_Doc.explorerFocus);
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
                m_Doc.explorerFocus = rows[static_cast<size_t>(to)].id;
            } else {
                if (!io.KeyShift || indexOf(m_Doc.explorerAnchor) < 0) m_Doc.explorerAnchor = rows[static_cast<size_t>(to)].id;
                pick(to, io.KeyShift, io.KeyCtrl);
                clicked = true;
            }
            scrollToFocus = true;
            m_ExplorerTyped.clear();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Space) && focus >= 0) {
            pick(focus, io.KeyShift, io.KeyCtrl);
            clicked = true;
        } else if (!io.KeyCtrl && !io.InputQueueCharacters.empty()) {
            // Incremental search: the characters typed within a second.
            const double now = ImGui::GetTime();
            if (now - m_ExplorerTypedTime > 1.0) m_ExplorerTyped.clear();
            m_ExplorerTypedTime = now;
            for (ImWchar c : io.InputQueueCharacters)
                if (c > ' ' && c < 0x80) m_ExplorerTyped.push_back(static_cast<char>(std::tolower(c)));
            if (!m_ExplorerTyped.empty()) {
                // The same letter again goes to the next item starting with it.
                const bool repeated = m_ExplorerTyped.find_first_not_of(m_ExplorerTyped[0]) == std::string::npos &&
                                      m_ExplorerTyped.size() > 1;
                const std::string prefix = repeated ? m_ExplorerTyped.substr(0, 1) : m_ExplorerTyped;
                const int start = focus < 0 ? 0 : (repeated || m_ExplorerTyped.size() == 1 ? focus + 1 : focus);
                for (int n = 0; n <= last; ++n) {
                    const int k = (start + n) % (last + 1);
                    if (Lower(rows[static_cast<size_t>(k)].name).compare(0, prefix.size(), prefix) == 0) {
                        m_Doc.explorerAnchor = rows[static_cast<size_t>(k)].id;
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
        const Row &row = rows[i];
        ImGui::PushID(static_cast<int>(row.id));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool selected = listed(row.id);
        if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, rowHeight))) {
            clicked = true;
            pick(static_cast<int>(i), io.KeyShift, io.KeyCtrl);
            doubleClicked = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            // The list view selects the item clicked when it is not (here
            // the 3D Layout's selection too, Dev leaves it).
            if (!selected) {
                m_Doc.explorerList.assign(1, row.id);
                m_Doc.explorerAnchor = row.id;
                clicked = true;
            }
            m_Doc.explorerFocus = row.id;
            rightClicked = true;
        }
        if (scrollToFocus && row.id == m_Doc.explorerFocus) ImGui::SetScrollHereY();
        DrawDevIcon(ImGui::GetWindowDrawList(), ImVec2(at.x + 2.0f * m_Scale, at.y + (rowHeight - 10.0f * iconScale) * 0.5f),
                    iconScale, IconForClass(row.cid), IM_COL32(0, 0, 0, 255), IM_COL32(230, 230, 230, 255));
        ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + 16.0f * m_Scale, at.y + 1.0f * m_Scale),
                                            ImGui::GetColorU32(ImGuiCol_Text), row.name.c_str());
        // The focused item's dotted frame while the list has the focus.
        if (listFocused && row.id == m_Doc.explorerFocus) {
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
        if (!io.KeyCtrl) m_Doc.explorerList.clear();
        clicked = true;
    }
    // NM_CLICK (Dev.exe sub_59E270), and here the keyboard: the 3D Layout's
    // selection becomes the list's (objects not listed leave it); while
    // locked the list shows the selection again (sub_59EC90).
    if (clicked) apply();
    std::vector<CK_ID> listedSelected = ordered(m_Doc.explorerList);
    // NM_DBLCLK, NM_RCLICK: Show Setup with one item selected.
    if (doubleClicked && listedSelected.size() == 1) m_State.events.Post(ShowObjectSetup{listedSelected.front()});
    if (rightClicked && listedSelected.size() == 1) ImGui::OpenPopup("##explorermenu");
    if (ImGui::BeginPopup("##explorermenu")) {
        if (ImGui::MenuItem("Show Setup") && listedSelected.size() == 1)
            m_State.events.Post(ShowObjectSetup{listedSelected.front()});
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
    std::vector<CK_ID> layoutListed;
    for (const Row &row : rows)
        if (selection.Contains(row.id)) layoutListed.push_back(row.id);
    const unsigned selectedTypes = SelectedTypes(context, layoutListed);
    bool typesChanged = false;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f * m_Scale, 0.0f));
    for (const Box &box : boxes) {
        place(243, box.y);
        bool on = (m_ExplorerShown & box.bit) != 0;
        if (ImGui::Checkbox(box.label, &on)) {
            // Dev refuses while objects of the type are selected.
            if (selectedTypes & box.bit) m_Doc.explorerRefused = true;
            else {
                m_ExplorerShown ^= box.bit;
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
    ImGui::Text(m_Doc.explorerCount >= 2 ? "%d Entities Selected." : "%d Entity Selected.", m_Doc.explorerCount);
    place(243, 162);
    ImGui::TextUnformatted("Text Filter");
    place(243, 175);
    ImGui::SetNextItemWidth(123.0f * dx);
    // A change refills the list; it shows the 3D Layout's selection (Dev's
    // refilled list has none selected, sub_59EFF0).
    if (ImGui::InputText("##textfilter", m_ExplorerFilter, sizeof(m_ExplorerFilter))) m_Doc.explorerListRevision = ~0u;
    // New types shown: the list again, with the 3D Layout's selection.
    if (typesChanged) m_Doc.explorerListRevision = ~0u;

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
        std::vector<CK_ID> objects;
        for (const Row &row : rows) {
            const bool was = listed(row.id);
            if (listAction == 1 || (listAction == 3 && !was)) objects.push_back(row.id);
        }
        m_Doc.explorerList = objects;
        apply();
    }

    // Dev's buttons: Select and Cancel.
    const float buttonWidth = 80.0f * m_Scale;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 381.0f * dx - 2.0f * buttonWidth - style.ItemSpacing.x,
                                     origin.y + 217.0f * dy + style.ItemSpacing.y));
    bool close = false, cancel = false;
    if (ImGui::Button("Select", ImVec2(buttonWidth, 0.0f))) close = true;
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f))) close = cancel = true;

    if (m_Doc.explorerRefused) {
        ImGui::OpenPopup("3D Layout Explorer##refused");
        m_Doc.explorerRefused = false;
    }
    if (ImGui::BeginPopupModal("3D Layout Explorer##refused", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            "Some objects of this type are selected !\nPlease remove them from the selection before unchecking this button.");
        if (ImGui::Button("OK", ImVec2(90.0f * m_Scale, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsKeyPressed(ImGuiKey_Enter))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
    if (!open) close = cancel = true;
    if (close) CloseExplorer(cancel);
}

} // namespace vdev
