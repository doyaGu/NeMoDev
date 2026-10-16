// Dev's Duplicate Names Found (Dev.exe dialog 0x2D6: sub_59B3A0 fills it,
// sub_59BA50 opens its menu, sub_59BCA0 runs its commands and OK;
// CKUICallBackFct shows it for CKUIM_DUPLICATEOBJECTS), in Dev pixels.
#include "ui/app/EditorApp.h"

#include <algorithm>
#include <iterator>
#include <set>

#include "imgui.h"
#include "imgui_internal.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

namespace {

// The categories: the first of these classes an object belongs to
// (sub_59B3A0's table), made when its first object is listed.
struct Category {
    CK_CLASSID cid;
    const char *name;
};
const Category CATEGORIES[] = {{CKCID_CHARACTER, "Characters"}, {CKCID_SPRITE3D, "3D Sprites"},
                               {CKCID_BODYPART, "Bodyparts"},   {CKCID_3DOBJECT, "3D Objects"},
                               {CKCID_GRID, "Grids"},           {CKCID_CAMERA, "Cameras"},
                               {CKCID_LIGHT, "Lights"},         {CKCID_3DENTITY, "3D Frames"},
                               {CKCID_DATAARRAY, "Arrays"},     {CKCID_MESH, "Meshes"},
                               {CKCID_MATERIAL, "Materials"},   {CKCID_TEXTURE, "Textures"},
                               {CKCID_GROUP, "Groups"},         {CKCID_SPRITE, "2D Sprites"},
                               {CKCID_OBJECT, "Others"}};
// The actions, by the menu's commands 291 to 294 (Replace with the 2.5
// runtime only: DUPLICATE_REPLACE_AVAILABLE).
const char *const ACTIONS[] = {"Ignore", "Rename", "Use Current", "Replace"};
constexpr int ARROW_CLOSED = 0xD9, ARROW_OPEN = 0xDA; // Dev.exe's icons
constexpr float ROW = 17.0f, HEADER = 15.0f;
constexpr float OBJECT_COLUMN = 250.0f, ACTION_COLUMN = 118.0f;

// The tree: the root (All Objects), the categories, the objects (an index
// in the request's duplicates).
struct Node {
    std::string label; // UTF-8
    int icon = 0;      // resvis.dll
    int duplicate = -1;
    std::vector<int> children;
    bool open = true; // sub_59BC50 opens every item
};
struct Tree {
    unsigned request = 0; // the request the tree shows (DuplicateNamesRequest::serial)
    std::vector<Node> nodes;
    std::set<int> selected;
    int anchor = -1;
};
Tree g_Tree;

void Build(Tree &tree, const std::vector<DuplicateName> &duplicates, unsigned request) {
    tree = Tree();
    tree.request = request;
    tree.nodes.push_back(Node{"All Objects"});
    int categories[sizeof(CATEGORIES) / sizeof(CATEGORIES[0])];
    std::fill(std::begin(categories), std::end(categories), -1);
    for (size_t i = 0; i < duplicates.size(); ++i) {
        const DuplicateName &duplicate = duplicates[i];
        size_t c = 0;
        while (c + 1 < std::size(CATEGORIES) && !CKIsChildClassOf(duplicate.cid, CATEGORIES[c].cid)) ++c;
        if (categories[c] < 0) {
            categories[c] = static_cast<int>(tree.nodes.size());
            tree.nodes.push_back(Node{CATEGORIES[c].name, DevClassIconId(CATEGORIES[c].cid)});
            tree.nodes[0].children.push_back(categories[c]);
        }
        Node item{AnsiToUtf8(duplicate.name), DevClassIconId(duplicate.cid), static_cast<int>(i)};
        tree.nodes[categories[c]].children.push_back(static_cast<int>(tree.nodes.size()));
        tree.nodes.push_back(item);
    }
}

// The action of a node's objects, -1 when they differ (sub_59B290).
int CommonAction(const Tree &tree, int node, const std::vector<DuplicateName> &duplicates) {
    const Node &n = tree.nodes[node];
    if (n.duplicate >= 0) return static_cast<int>(duplicates[n.duplicate].action);
    int common = -2;
    for (int child : n.children) {
        const int action = CommonAction(tree, child, duplicates);
        if (action < 0 || (common >= 0 && common != action)) return -1;
        common = action;
    }
    return common < 0 ? -1 : common;
}

// sub_59BBD0: the node and every node below it.
void SetAction(Tree &tree, int node, DuplicateAction action, std::vector<DuplicateName> &duplicates) {
    const Node &n = tree.nodes[node];
    if (n.duplicate >= 0) duplicates[n.duplicate].action = action;
    for (int child : n.children) SetAction(tree, child, action, duplicates);
}

void OpenAll(Tree &tree, int node) {
    tree.nodes[node].open = true;
    for (int child : tree.nodes[node].children) OpenAll(tree, child);
}

} // namespace

void EditorApp::DuplicateNamesDialog() {
    const char *id = "###DuplicateNames";
    if (m_State.duplicateNames.empty()) {
        g_Tree = Tree();
        return;
    }
    EditorState::DuplicateNamesRequest &request = m_State.duplicateNames.front();
    std::vector<DuplicateName> &duplicates = request.duplicates;
    if (g_Tree.request != request.serial) Build(g_Tree, duplicates, request.serial);
    // Open until answered (the startup's window focus would close it).
    if (m_FocusSchematic <= 0 && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    const float s = ImGui::GetFontSize() / 13.0f;
    // The client 400 x 400 (sub_59B3A0), the buttons' bar below it.
    const ImVec2 client(400.0f * s, 400.0f * s + 26.0f * s);
    ImVec2 o;
    int closed = 0; // 1 OK, 2 Cancel
    if (vi::BeginDialog(id, "Duplicate Names Found", client, s, o)) {
        // The tree list, (6, 6) to (394, 394): its header, the rows 17 high
        // on ZC 7 with a white line under each, the selected ones on ZC 4
        // with white text; a category with one action for all its objects
        // shows it in bold (sub_59B320).
        ImGui::SetCursorScreenPos(ImVec2(o.x + 6.0f * s, o.y + 6.0f * s));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, vi::Color(6));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        PushDarkChrome();
        ImGui::BeginChild("##duplicates", ImVec2(388.0f * s, 388.0f * s), ImGuiChildFlags_Borders);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 list = ImGui::GetCursorScreenPos();
        const float scrollY = ImGui::GetScrollY();
        const float rowHeight = IM_ROUND(ROW * s), headerHeight = IM_ROUND(HEADER * s);
        const float x0 = list.x, x1 = x0 + IM_ROUND(OBJECT_COLUMN * s), x2 = x1 + IM_ROUND(ACTION_COLUMN * s);
        const float right = std::max(x2, list.x + ImGui::GetContentRegionAvail().x);
        ImGui::PushClipRect(ImVec2(list.x, list.y + scrollY + headerHeight),
                            ImVec2(right, list.y + scrollY + ImGui::GetWindowHeight()), true);
        // The rows, depth first.
        std::vector<std::pair<int, int>> rows; // node, depth
        std::vector<std::pair<int, int>> stack{{0, 0}};
        while (!stack.empty()) {
            const auto row = stack.back();
            stack.pop_back();
            rows.push_back(row);
            const Node &node = g_Tree.nodes[row.first];
            if (node.open)
                for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) stack.push_back({*it, row.second + 1});
        }
        bool menu = false;
        for (size_t r = 0; r < rows.size(); ++r) {
            const int index = rows[r].first;
            Node &node = g_Tree.nodes[index];
            const float y = list.y + headerHeight + rowHeight * r;
            const bool selected = g_Tree.selected.count(index) != 0;
            const ImU32 ink = vi::Color(selected ? 9 : 0);
            dl->AddRectFilled(ImVec2(x0, y), ImVec2(right, y + rowHeight), vi::Color(selected ? 4 : 7));
            dl->AddRectFilled(ImVec2(x0, y + rowHeight - 1.0f), ImVec2(right, y + rowHeight), vi::Color(9));
            const int action = CommonAction(g_Tree, index, duplicates);
            const bool folder = !node.children.empty();
            const bool bold = folder && action >= 0;
            const ImVec2 arrow(IM_ROUND(x0 + 4.0f * s + 13.0f * s * rows[r].second), IM_ROUND(y + (rowHeight - 11.0f * s) * 0.5f));
            ImGui::PushID(index);
            bool toggle = false;
            if (folder) {
                if (ImTextureID texture = DevExeIcon(node.open ? ARROW_OPEN : ARROW_CLOSED))
                    dl->AddImage(texture, arrow, ImVec2(arrow.x + 11.0f * s, arrow.y + 11.0f * s));
                ImGui::SetCursorScreenPos(arrow);
                toggle = ImGui::InvisibleButton("##arrow", ImVec2(11.0f * s, 11.0f * s));
            }
            float textX = arrow.x + 13.0f * s;
            if (node.icon) {
                if (ImTextureID texture = DevIconImage(node.icon))
                    dl->AddImage(texture, ImVec2(textX, arrow.y), ImVec2(textX + 11.0f * s, arrow.y + 11.0f * s));
                textX += 13.0f * s;
            }
            const ImVec2 clip(x1 - 1.0f, y + rowHeight);
            DrawVIText(dl, ImVec2(IM_ROUND(textX), IM_ROUND(y + (rowHeight - VIFontHeight(bold) * s) * 0.5f)), ink,
                       node.label.c_str(), bold, s, &clip);
            if (action >= 0 && (!folder || bold)) {
                const float width = VITextWidth(ACTIONS[action], bold, s);
                const ImVec2 actionClip(x2, y + rowHeight);
                DrawVIText(dl, ImVec2(IM_ROUND((x1 + x2 - width) * 0.5f), IM_ROUND(y + (rowHeight - VIFontHeight(bold) * s) * 0.5f)),
                           ink, ACTIONS[action], bold, s, &actionClip);
            }
            // The row: a click selects it (Ctrl adds or removes, Shift the
            // rows from the last one clicked), a double click opens or closes
            // a category, a right click opens the menu.
            ImGui::SetCursorScreenPos(ImVec2(x0, y));
            ImGui::InvisibleButton("##row", ImVec2(right - x0, rowHeight - 1.0f),
                                   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const ImGuiIO &io = ImGui::GetIO();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                if (io.KeyShift && g_Tree.anchor >= 0) {
                    size_t from = r, to = r;
                    for (size_t k = 0; k < rows.size(); ++k)
                        if (rows[k].first == g_Tree.anchor) from = k;
                    if (from > to) std::swap(from, to);
                    if (!io.KeyCtrl) g_Tree.selected.clear();
                    for (size_t k = from; k <= to; ++k) g_Tree.selected.insert(rows[k].first);
                } else if (io.KeyCtrl) {
                    if (!g_Tree.selected.erase(index)) g_Tree.selected.insert(index);
                    g_Tree.anchor = index;
                } else {
                    g_Tree.selected = {index};
                    g_Tree.anchor = index;
                }
                if (folder && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) toggle = true;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                if (!selected) g_Tree.selected = {index};
                g_Tree.anchor = index;
                menu = true;
            }
            if (toggle) node.open = !node.open;
            ImGui::PopID();
        }
        if (!rows.empty()) {
            const float last = list.y + headerHeight + rowHeight * rows.size();
            dl->AddRectFilled(ImVec2(x0, last - 1.0f), ImVec2(right, last), vi::Color(0));
        }
        ImGui::PopClipRect();
        // The header (VI font 0, centered): Object, Action.
        {
            const float top = list.y + scrollY;
            dl->AddRectFilled(ImVec2(x0, top), ImVec2(right, top + headerHeight), vi::Color(6));
            const char *const titles[2] = {"Object", "Action"};
            const float edges[3] = {x0, x1, x2};
            for (int i = 0; i < 2; ++i) {
                const float width = VITextWidth(titles[i], false, s);
                DrawVIText(dl, ImVec2(IM_ROUND((edges[i] + edges[i + 1] - width) * 0.5f), IM_ROUND(top + (headerHeight - 12.0f * s) * 0.5f)),
                           vi::Color(0), titles[i], false, s);
                dl->AddRectFilled(ImVec2(edges[i + 1] - 1.0f, top), ImVec2(edges[i + 1], top + headerHeight), vi::Color(0));
            }
            dl->AddRectFilled(ImVec2(x0, top + headerHeight - 1.0f), ImVec2(right, top + headerHeight), vi::Color(0));
        }
        ImGui::SetCursorScreenPos(list);
        ImGui::Dummy(ImVec2(x2 - x0, headerHeight + rowHeight * rows.size()));
        ImGui::EndChild();
        PopDarkChrome();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        // The menu (sub_59BA50) on the rows selected: Expand Children, the
        // four actions (sub_59BCA0: each selected item and those below it).
        if (menu) ImGui::OpenPopup("##duplicatemenu");
        if (ImGui::BeginPopup("##duplicatemenu")) {
            if (ImGui::MenuItem("Expand Children"))
                for (int node : g_Tree.selected) OpenAll(g_Tree, node);
            ImGui::Separator();
            for (int a = 0; a < (DUPLICATE_REPLACE_AVAILABLE ? 4 : 3); ++a)
                if (ImGui::MenuItem(ACTIONS[a]))
                    for (int node : g_Tree.selected) SetAction(g_Tree, node, static_cast<DuplicateAction>(a), duplicates);
            ImGui::EndPopup();
        }
        const float by = o.y + client.y - 23.0f * s;
        if (vi::DialogButton("Cancel", ImVec2(o.x + client.x - 70.0f * s, by), s, ImGuiKey_Escape)) closed = 2;
        if (vi::DialogButton("OK", ImVec2(o.x + client.x - 140.0f * s, by), s, ImGuiKey_Enter)) closed = 1;
        if (closed) ImGui::CloseCurrentPopup();
        vi::EndDialog();
    }
    if (!closed) return;
    // The load runs after the dialog (OK), or not at all (Cancel).
    EditorState::DuplicateNamesRequest done = std::move(request);
    m_State.duplicateNames.erase(m_State.duplicateNames.begin());
    g_Tree = Tree();
    if (closed == 1 && done.load) done.load(&done.duplicates);
    else m_State.Log(LogLevel::Info, "Load cancelled: " + done.path);
}

} // namespace vdev
