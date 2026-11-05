// Dev's Duplicate Names Found (Dev.exe dialog 0x2D6: sub_59B3A0 fills it,
// sub_59BA50 opens its menu, sub_59BCA0 runs its commands and OK;
// CKUICallBackFct shows it for CKUIM_DUPLICATEOBJECTS), in Dev pixels.
#include "ui/app/DuplicateNames.h"

#include <algorithm>
#include <climits>
#include <iterator>
#include <map>
#include <set>

#include "imgui.h"
#include "imgui_internal.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"

namespace vdev {

namespace {

struct DuplicateName {
    std::string name, categoryName;
    int cid = 0, categoryClass = 0;
    uint64_t category = 0, action = 0;
    int icon = 0, categoryIcon = 0;
};
const char *const ACTIONS[] = {"Ignore", "Rename", "Use Current"};
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
    uint64_t request = 0; // the application input the tree shows
    std::vector<Node> nodes;
    std::set<int> selected;
    int anchor = -1;
};

void Build(Tree &tree, const std::vector<DuplicateName> &duplicates, uint64_t request) {
    tree = Tree();
    tree.request = request;
    tree.nodes.push_back(Node{"All Objects"});
    std::map<uint64_t, int> categories;
    for (size_t i = 0; i < duplicates.size(); ++i) {
        const auto &duplicate = duplicates[i];
        auto [category, inserted] = categories.emplace(duplicate.category, int(tree.nodes.size()));
        if (inserted) {
            tree.nodes.push_back(Node{duplicate.categoryName, duplicate.categoryIcon});
            tree.nodes[0].children.push_back(category->second);
        }
        Node item{duplicate.name, duplicate.icon, static_cast<int>(i)};
        tree.nodes[category->second].children.push_back(static_cast<int>(tree.nodes.size()));
        tree.nodes.push_back(std::move(item));
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
void SetAction(Tree &tree, int node, uint64_t action, std::vector<DuplicateName> &duplicates) {
    const Node &n = tree.nodes[node];
    if (n.duplicate >= 0) duplicates[n.duplicate].action = action;
    for (int child : n.children) SetAction(tree, child, action, duplicates);
}

void OpenAll(Tree &tree, int node) {
    tree.nodes[node].open = true;
    for (int child : tree.nodes[node].children) OpenAll(tree, child);
}

} // namespace

struct DuplicateNamesDialog::Data {
    Tree tree;
    std::vector<DuplicateName> duplicates;
    EditorOperation response = 0;
    ImGuiID popup = 0;
    bool ready = false;
    std::string error;
    bool Read(const EditorClient &client, uint64_t input) {
        std::vector<DuplicateName> next;
        uint64_t first = 0, total = 0;
        do {
            const auto page = client.Query("document.duplicate-rows",
                                           {IntegerArgument("input", input), IntegerArgument("first", first)});
            if (page.status != EDITOR_OK) return false;
            const auto *rows = EditorClient::Find(page, "rows");
            const auto *count = EditorClient::Find(page, "total"), *end = EditorClient::Find(page, "next");
            if (!rows || rows->type != EDITOR_RECORD || !count || count->type != EDITOR_INTEGER_VALUE ||
                !end || end->type != EDITOR_INTEGER_VALUE || count->integer > uint64_t(INT_MAX) ||
                (first && count->integer != total) || end->integer > count->integer ||
                end->integer < first || end->integer - first != rows->fields.size() ||
                (end->integer == first && first != count->integer)) return false;
            total = count->integer;
            for (const auto &row : rows->fields) {
                if (row.key != std::to_string(next.size()) || row.type != EDITOR_RECORD) return false;
                auto field = [&](const char *key, EditorValueType type) -> const EditorField * {
                    for (const auto &value : row.fields) if (value.key == key && value.type == type) return &value;
                    return nullptr;
                };
                const auto *name = field("name", EDITOR_TEXT_VALUE), *categoryName = field("category-name", EDITOR_TEXT_VALUE);
                const auto *cid = field("class", EDITOR_INTEGER_VALUE), *category = field("category", EDITOR_INTEGER_VALUE);
                const auto *categoryClass = field("category-class", EDITOR_INTEGER_VALUE), *action = field("action", EDITOR_INTEGER_VALUE);
                if (!name || !categoryName || !cid || cid->integer > INT_MAX || !category || !categoryClass ||
                    categoryClass->integer > INT_MAX || !action || action->integer >= std::size(ACTIONS)) return false;
                next.push_back({name->text, categoryName->text, int(cid->integer), int(categoryClass->integer),
                                category->integer, action->integer});
                const auto *icon = field("icon", EDITOR_INTEGER_VALUE), *categoryIcon = field("category-icon", EDITOR_INTEGER_VALUE);
                if (!icon || !categoryIcon || icon->integer > INT_MAX || categoryIcon->integer > INT_MAX) return false;
                next.back().icon = int(icon->integer); next.back().categoryIcon = int(categoryIcon->integer);
            }
            first = end->integer;
        } while (first < total);
        duplicates = std::move(next);
        Build(tree, duplicates, input);
        return true;
    }
};
DuplicateNamesDialog::DuplicateNamesDialog() : m_Data(std::make_unique<Data>()) {}
DuplicateNamesDialog::~DuplicateNamesDialog() = default;
void DuplicateNamesDialog::Reset() {
    m_Data->tree = {};
    m_Data->duplicates.clear();
    m_Data->response = 0;
    m_Data->error.clear();
    const auto popup = m_Data->popup;
    m_Data->popup = 0;
    m_Data->ready = false;
    for (int i = 0; i < ImGui::GetCurrentContext()->OpenPopupStack.Size; ++i)
        if (ImGui::GetCurrentContext()->OpenPopupStack[i].PopupId == popup) {
            ImGui::ClosePopupToLevel(i, true);
            break;
        }
}
void DuplicateNamesDialog::Draw(const EditorClient &editor, uint64_t input) {
    const char *id = "###DuplicateNames";
    auto &data = *m_Data;
    auto &tree = data.tree;
    auto &duplicates = data.duplicates;
    if (tree.request != input) {
        Reset();
        Build(tree, {}, input);
        data.ready = data.Read(editor, input);
        if (!data.ready) data.error = "Cannot read duplicate names.";
    }
    if (data.response) {
        const auto result = editor.Completion(data.response);
        if (result.status != EDITOR_QUEUED) {
            data.response = 0;
            if (result.status == EDITOR_OK || result.status == EDITOR_CANCELLED) { Reset(); return; }
            data.error = result.message.empty() ? "The duplicate-name choices could not be applied." : result.message;
        }
    }
    // Reopen while this input is pending, including startup focus changes.
    if (!ImGui::IsPopupOpen(id)) {
        data.popup = ImGui::GetID(id);
        ImGui::OpenPopup(id);
    }
    const float s = ImGui::GetFontSize() / 13.0f * CurrentDevTheme().setupCoordinateScale;
    vi::DialogLayout layout;
    int closed = 0; // 1 OK, 2 Cancel
    if (vi::BeginScrollableDialog(id, "Duplicate Names Found", {520 * s, 400 * s}, s, layout)) {
        ImGui::BeginDisabled(data.response != 0);
        bool menu = false;
        if (vi::BeginDialogContent("##duplicate-names", layout)) {
            if (!data.error.empty()) ImGui::TextWrapped("%s", data.error.c_str());
            const ImVec2 size{ImGui::GetContentRegionAvail().x,
                              std::max(140 * s, ImGui::GetContentRegionAvail().y)};
            // Keep Dev's tree rows, hierarchy selection and action menu. The two
            // columns grow with the available space and measured names.
            ImGui::PushStyleColor(ImGuiCol_ChildBg, vi::Color(6));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
            PushDarkChrome();
            ImGui::BeginChild("##duplicates", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
            ImDrawList *dl = ImGui::GetWindowDrawList();
            const ImVec2 list = ImGui::GetCursorScreenPos();
            const float scrollY = ImGui::GetScrollY();
            const float rowHeight = IM_ROUND(ROW * s), headerHeight = IM_ROUND(HEADER * s);
            // The rows, depth first.
            std::vector<std::pair<int, int>> rows; // node, depth
            std::vector<std::pair<int, int>> stack{{0, 0}};
            while (!stack.empty()) {
                const auto row = stack.back();
                stack.pop_back();
                rows.push_back(row);
                const Node &node = tree.nodes[row.first];
                if (node.open)
                    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) stack.push_back({*it, row.second + 1});
            }
            float objectWidth = std::max(OBJECT_COLUMN * s,
                                          ImGui::GetCurrentWindow()->InnerRect.GetWidth() - ACTION_COLUMN * s);
            for (const auto &[index, depth] : rows) {
                const auto &node = tree.nodes[index];
                const float textWidth = std::max(VITextWidth(node.label.c_str(), false, s),
                                                  VITextWidth(node.label.c_str(), true, s));
                objectWidth = std::max(objectWidth, (23 + 13 * depth + (node.icon ? 13 : 0)) * s + textWidth);
            }
            const float x0 = list.x, x1 = x0 + IM_ROUND(objectWidth), x2 = x1 + IM_ROUND(ACTION_COLUMN * s);
            const float right = x2;
            ImGui::PushClipRect(ImVec2(list.x, list.y + scrollY + headerHeight),
                                ImVec2(right, list.y + scrollY + ImGui::GetWindowHeight()), true);
            for (size_t r = 0; r < rows.size(); ++r) {
                const int index = rows[r].first;
                Node &node = tree.nodes[index];
                const float y = list.y + headerHeight + rowHeight * r;
                const bool selected = tree.selected.count(index) != 0;
                const ImU32 ink = vi::Color(selected ? 9 : 0);
                dl->AddRectFilled(ImVec2(x0, y), ImVec2(right, y + rowHeight), vi::Color(selected ? 4 : 7));
                dl->AddRectFilled(ImVec2(x0, y + rowHeight - 1.0f), ImVec2(right, y + rowHeight), vi::Color(9));
                const int action = CommonAction(tree, index, duplicates);
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
                    if (io.KeyShift && tree.anchor >= 0) {
                        size_t from = r, to = r;
                        for (size_t k = 0; k < rows.size(); ++k)
                            if (rows[k].first == tree.anchor) from = k;
                        if (from > to) std::swap(from, to);
                        if (!io.KeyCtrl) tree.selected.clear();
                        for (size_t k = from; k <= to; ++k) tree.selected.insert(rows[k].first);
                    } else if (io.KeyCtrl) {
                        if (!tree.selected.erase(index)) tree.selected.insert(index);
                        tree.anchor = index;
                    } else {
                        tree.selected = {index};
                        tree.anchor = index;
                    }
                    if (folder && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) toggle = true;
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    if (!selected) tree.selected = {index};
                    tree.anchor = index;
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
        }
        vi::EndDialogContent();
        // The menu (sub_59BA50) on the rows selected: Expand Children, the
        // three actions, applied to each selected item and those below it.
        if (menu) ImGui::OpenPopup("##duplicatemenu");
        if (ImGui::BeginPopup("##duplicatemenu")) {
            if (ImGui::MenuItem("Expand Children"))
                for (int node : tree.selected) OpenAll(tree, node);
            ImGui::Separator();
            for (int a = 0; a < static_cast<int>(std::size(ACTIONS)); ++a)
                if (ImGui::MenuItem(ACTIONS[a]))
                    for (int node : tree.selected) SetAction(tree, node, uint64_t(a), duplicates);
            ImGui::EndPopup();
        }
        if (vi::DialogButton("Cancel", {layout.right - 60 * s, layout.buttonsY}, s, ImGuiKey_Escape)) closed = 2;
        ImGui::BeginDisabled(!data.ready);
        if (vi::DialogButton("OK", {layout.right - 130 * s, layout.buttonsY}, s, ImGuiKey_Enter)) closed = 1;
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        vi::EndDialog();
    }
    if (!closed) return;
    std::vector<uint64_t> actions;
    for (const auto &duplicate : duplicates) actions.push_back(duplicate.action);
    const EditorValue choices{EDITOR_TEXT("actions"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {actions.data(), uint32_t(actions.size())}};
    const auto result = editor.Command("document.respond", {IntegerArgument("input", input),
        TextArgument("choice", closed == 1 ? "accept" : "cancel"), choices});
    if (result.status == EDITOR_QUEUED) data.response = result.operation;
    else if (result.status == EDITOR_OK || result.status == EDITOR_CANCELLED) Reset();
    else data.error = result.message.empty() ? "The duplicate-name choices could not be applied." : result.message;
}

} // namespace vdev
