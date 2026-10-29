#include "features/resources/BuildingBlockViews.h"
#include "editor/ImGuiDrawing.h"
#include "editor/Resources.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "ui/controls/DevTheme.h"
#include "workspace/BuildingBlockDrag.h"
#include "workspace/EditorClient.h"
#include <algorithm>
#include <iterator>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <map>
#include <set>

namespace vdev {
namespace {
std::string Lower(std::string text) {
    for (auto &c : text)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
struct Block {
    uint64_t prototype = 0;
    uint32_t version = 0;
    bool targetable = false;
    std::string name, category, description, author, compatible;
    std::string sort[6], searchCategory;
};
struct Category {
    std::map<std::string, Category> children;
    std::vector<size_t> entries;
};
struct BlocksView {
    std::vector<Block> entries;
    Category categories;
    std::set<uint64_t> chosen;
    std::vector<size_t> shown;
    std::string filter, category;
    std::map<std::string, bool> expanded;
    uint64_t catalog = 0, revision = 1, anchor = 0;
    int sortColumn = 0;
    bool descending = false, listChanged = true, restoreExpanded = false;

    EditorStatus Refresh(const EditorClient &client);
    void Filter();
    void Draw(const EditorClient &, EditorViewFrame &);
    void Tree(const std::string &, const std::string &, const Category &);
    void Item(const EditorClient &, size_t);
    EditorStatus Navigate(const EditorClient &, const EditorRequest &);
    EditorStatus Save(EditorSink) const;
    EditorStatus Restore(const EditorRequest &);
    EditorStatus Inspect(EditorSink) const;
};
EditorStatus BlocksView::Refresh(const EditorClient &client) {
    const uint64_t version = client.Revision("catalog.building-blocks");
    if (!version)
        return EDITOR_REFUSED;
    if (version == catalog)
        return EDITOR_OK;
    std::vector<Block> next;
    uint64_t first = 0, total = 0;
    do {
        const auto page = client.Query("catalog.building-blocks", {IntegerArgument("first", first)});
        if (page.status != EDITOR_OK)
            return page.status;
        if (EditorClient::Integer(page, "revision") != version)
            return EDITOR_STALE;
        total = EditorClient::Integer(page, "total");
        const auto *names = EditorClient::Find(page, "names"), *paths = EditorClient::Find(page, "categories"),
                   *descriptions = EditorClient::Find(page, "descriptions"),
                   *authors = EditorClient::Find(page, "authors"), *compatible = EditorClient::Find(page, "compatible"),
                   *prototypes = EditorClient::Find(page, "prototypes"),
                   *versions = EditorClient::Find(page, "versions"),
                   *targetable = EditorClient::Find(page, "targetable");
        if (!names || !paths || !descriptions || !authors || !compatible || !prototypes || !versions || !targetable)
            return EDITOR_INVALID;
        const auto count = names->texts.size();
        if (paths->texts.size() != count || descriptions->texts.size() != count || authors->texts.size() != count ||
            compatible->texts.size() != count || prototypes->integers.size() != count ||
            versions->integers.size() != count || targetable->integers.size() != count || first + count > total ||
            total > 1000000 || (!count && first < total))
            return EDITOR_INVALID;
        for (size_t i = 0; i < count; ++i) {
            Block block;
            block.prototype = prototypes->integers[i];
            block.version = uint32_t(versions->integers[i]);
            block.targetable = targetable->integers[i] != 0;
            block.name = names->texts[i];
            block.category = paths->texts[i];
            block.description = descriptions->texts[i];
            block.author = authors->texts[i];
            block.compatible = compatible->texts[i];
            block.sort[0] = Lower(block.name);
            block.sort[1] = Lower(block.compatible);
            block.sort[2] = block.targetable ? "1" : "0";
            block.sort[3] = Lower(block.description);
            char versionText[16];
            std::snprintf(versionText, sizeof(versionText), "%08X", block.version);
            block.sort[4] = versionText;
            block.sort[5] = Lower(block.author);
            block.searchCategory = Lower(block.category);
            next.push_back(std::move(block));
        }
        first += count;
    } while (first < total);
    Category tree;
    std::set<uint64_t> live;
    for (size_t i = 0; i < next.size(); ++i) {
        live.insert(next[i].prototype);
        Category *node = &tree;
        const auto &path = next[i].category;
        for (size_t start = 0; start <= path.size();) {
            const auto end = std::min(path.find('/', start), path.size());
            const auto part = path.substr(start, end - start);
            node = &node->children[part.empty() ? "(Uncategorized)" : part];
            start = end + 1;
        }
        node->entries.push_back(i);
    }
    for (auto it = chosen.begin(); it != chosen.end();) {
        if (!live.count(*it))
            it = chosen.erase(it);
        else
            ++it;
    }
    if (!live.count(anchor))
        anchor = 0;
    entries = std::move(next);
    categories = std::move(tree);
    catalog = version;
    listChanged = true;
    ++revision;
    return EDITOR_OK;
}
void BlocksView::Filter() {
    if (!listChanged)
        return;
    shown.clear();
    if (!filter.empty()) {
        const auto text = Lower(filter);
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].sort[0].find(text) != std::string::npos ||
                entries[i].searchCategory.find(text) != std::string::npos)
                shown.push_back(i);
    } else if (!category.empty()) {
        const Category *node = &categories;
        for (size_t start = 0; node && start <= category.size();) {
            const auto end = std::min(category.find('/', start), category.size());
            const auto found = node->children.find(category.substr(start, end - start));
            node = found == node->children.end() ? nullptr : &found->second;
            start = end + 1;
        }
        const auto collect = [&](const auto &self, const Category &each) -> void {
            shown.insert(shown.end(), each.entries.begin(), each.entries.end());
            for (const auto &child : each.children)
                self(self, child.second);
        };
        if (node)
            collect(collect, *node);
    }
    std::stable_sort(shown.begin(), shown.end(), [&](size_t a, size_t b) {
        return descending ? entries[b].sort[sortColumn] < entries[a].sort[sortColumn]
                          : entries[a].sort[sortColumn] < entries[b].sort[sortColumn];
    });
    listChanged = false;
    ++revision;
}
void BlocksView::Tree(const std::string &name, const std::string &path, const Category &node) {
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (node.children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf;
    if (path == category)
        flags |= ImGuiTreeNodeFlags_Selected;
    if (restoreExpanded) {
        if (auto it = expanded.find(path); it != expanded.end())
            ImGui::SetNextItemOpen(it->second);
    }
    const bool open = ImGui::TreeNodeEx(name.c_str(), flags);
    expanded[path] = open;
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && category != path) {
        category = path;
        listChanged = true;
        ++revision;
    }
    if (!open)
        return;
    for (const auto &child : node.children)
        Tree(child.first, path + "/" + child.first, child.second);
    ImGui::TreePop();
}
void BlocksView::Item(const EditorClient &client, size_t index) {
    const auto &block = entries[index];
    ImGui::PushID(int(index));
    const bool selected = chosen.count(block.prototype) != 0;
    auto help = [&] {
        const auto topic =
            "behaviors/" + block.category.substr(0, block.category.find('/')) + "/" + block.name + ".html";
        client.Command("help.open", {TextArgument("topic", topic)});
    };
    if (ImGui::Selectable(block.name.c_str(), selected,
                          ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns)) {
        const auto &io = ImGui::GetIO();
        auto from = std::find_if(shown.begin(), shown.end(), [&](size_t i) { return entries[i].prototype == anchor; });
        auto to = std::find(shown.begin(), shown.end(), index);
        if (io.KeyShift && from != shown.end() && to != shown.end()) {
            if (to < from)
                std::swap(from, to);
            if (!io.KeyCtrl)
                chosen.clear();
            for (auto it = from; it <= to; ++it)
                chosen.insert(entries[*it].prototype);
        } else {
            if (!io.KeyCtrl)
                chosen = {block.prototype};
            else if (!chosen.erase(block.prototype))
                chosen.insert(block.prototype);
            anchor = block.prototype;
        }
        ++revision;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            help();
    }
    if (ImGui::IsItemHovered() && client.Host()->shortcut(client.Host()->data, EditorString EDITOR_TEXT("help.open"))) {
        help();
    }
    if (ImGui::BeginPopupContextItem("bbmenu")) {
        const EditorRequest request{client.Scope(), nullptr, 0};
        const bool available = client.Host()->commandState(client.Host()->data, EditorString EDITOR_TEXT("help.open"),
                                                           &request) == EDITOR_OK;
        if (ImGui::MenuItem("Html Help", "F1", false, available))
            help();
        ImGui::EndPopup();
    }
    if (ImGui::BeginDragDropSource()) {
        // ImGui keeps the complete selection. Closing a view or changing another
        // view's selection cannot change a drag already in progress.
        if (const auto *payload = ImGui::GetDragDropPayload();
            !payload || !payload->IsDataType(EDITOR_BUILDING_BLOCK_PAYLOAD)) {
            std::vector<EditorBuildingBlock> blocks;
            auto append = [&](uint64_t guid) { blocks.push_back({uint32_t(guid >> 32), uint32_t(guid)}); };
            if (selected) {
                for (const auto &entry : entries)
                    if (chosen.count(entry.prototype))
                        append(entry.prototype);
            } else
                append(block.prototype);
            const auto bytes =
                WriteBuildingBlockDrag({uint32_t(block.prototype >> 32), uint32_t(block.prototype)}, blocks);
            ImGui::SetDragDropPayload(EDITOR_BUILDING_BLOCK_PAYLOAD, bytes.data(), bytes.size(), ImGuiCond_Once);
        } else {
            ImGui::SetDragDropPayload(EDITOR_BUILDING_BLOCK_PAYLOAD, payload->Data, payload->DataSize, ImGuiCond_Once);
        }
        const auto *payload = ImGui::GetDragDropPayload();
        const int count = payload ? (payload->DataSize - int(sizeof(EditorBuildingBlockDragHeader))) /
                                        int(sizeof(EditorBuildingBlock))
                                  : 0;
        if (count > 1)
            ImGui::Text("%d Building Blocks", count);
        else
            ImGui::TextUnformatted(block.name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(block.name.c_str());
        ImGui::TextDisabled("%s", block.category.c_str());
        if (!block.compatible.empty())
            ImGui::TextDisabled("Apply to: %s%s", block.compatible.c_str(),
                                block.targetable ? "   (T: can use a target)" : "");
        ImGui::TextDisabled("Version: 0x%08X   Author: %s", block.version, block.author.c_str());
        if (!block.description.empty()) {
            ImGui::Separator();
            ImGui::PushTextWrapPos(420);
            ImGui::TextUnformatted(block.description.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTooltip();
    }
    ImGui::PopID();
}
void BlocksView::Draw(const EditorClient &client, EditorViewFrame &frame) {
    Refresh(client);
    EditorPrepareViewWindow(frame);
    bool open = true;
    const bool visible = BeginDevWindow(frame.window.data, frame.closable ? &open : nullptr);
    if (visible) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##filter", "Filter Building Blocks", &filter)) {
            listChanged = true;
            ++revision;
        }
        ImGui::TextDisabled("%zu prototypes - drag into the Schematic", entries.size());
        ImGui::Separator();
        const auto width = std::max(120.0f, ImGui::GetContentRegionAvail().x * 0.3f);
        ImGui::BeginChild("categories", ImVec2(width, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        for (const auto &child : categories.children)
            Tree(child.first, child.first, child.second);
        restoreExpanded = false;
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("blocks", ImVec2(0, 0), ImGuiChildFlags_Borders);
        const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp |
                                      ImGuiTableFlags_BordersInnerV;
        Filter();
        if (ImGui::BeginTable("blocklist", 6, flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            static const char *const labels[] = {"Behavior Name", "Apply to", "T", "Description", "Version", "Author"};
            const float widths[] = {2, 1.2f, 16, 4, .8f, 1.2f};
            for (int i = 0; i < 6; ++i)
                ImGui::TableSetupColumn(
                    labels[i],
                    (i == 2 ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch) |
                        (i == sortColumn ? ImGuiTableColumnFlags_DefaultSort : 0) |
                        (i == sortColumn && descending ? ImGuiTableColumnFlags_PreferSortDescending : 0),
                    widths[i]);
            ImGui::TableHeadersRow();
            if (auto *specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
                const int column = specs->Specs[0].ColumnIndex;
                const bool reverse = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                if (column != sortColumn || reverse != descending) {
                    sortColumn = column;
                    descending = reverse;
                    listChanged = true;
                    Filter();
                }
                specs->SpecsDirty = false;
            }
            for (const auto index : shown) {
                const auto &entry = entries[index];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                Item(client, index);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(entry.compatible.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(entry.targetable ? "T" : "");
                ImGui::TableNextColumn();
                const auto end = entry.description.find('\n');
                ImGui::TextUnformatted(entry.description.c_str(),
                                       entry.description.c_str() + std::min(end, entry.description.size()));
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", entry.version);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(entry.author.c_str());
            }
            ImGui::EndTable();
        }
        if (shown.empty())
            ImGui::TextDisabled(filter.empty() ? "Select a category" : "No Building Block found");
        ImGui::EndChild();
    }
    ImGui::End();
    if (!open)
        frame.close = 1;
}
EditorStatus BlocksView::Navigate(const EditorClient &client, const EditorRequest &request) {
    const auto status = Refresh(client);
    if (status != EDITOR_OK)
        return status;
    auto nextFilter = filter, nextCategory = category;
    auto nextChosen = chosen;
    for (uint32_t i = 0; i < request.count; ++i) {
        const auto &field = request.values[i];
        const std::string key(field.key.data, field.key.size);
        if (key == "filter")
            nextFilter.assign(field.text.data, field.text.size);
        if (key == "category")
            nextCategory.assign(field.text.data, field.text.size);
        if (key == "selection") {
            nextChosen.clear();
            for (uint32_t j = 0; j < field.integers.count; ++j) {
                const auto guid = field.integers.data[j];
                if (std::none_of(entries.begin(), entries.end(),
                                 [&](const auto &entry) { return entry.prototype == guid; }))
                    return EDITOR_INVALID;
                nextChosen.insert(guid);
            }
        }
    }
    if (!nextCategory.empty()) {
        const Category *node = &categories;
        for (size_t start = 0; start <= nextCategory.size();) {
            const auto end = std::min(nextCategory.find('/', start), nextCategory.size());
            auto it = node->children.find(nextCategory.substr(start, end - start));
            if (it == node->children.end())
                return EDITOR_INVALID;
            node = &it->second;
            start = end + 1;
        }
    }
    filter = std::move(nextFilter);
    category = std::move(nextCategory);
    chosen = std::move(nextChosen);
    listChanged = true;
    Filter();
    ++revision;
    return EDITOR_OK;
}
EditorStatus BlocksView::Inspect(EditorSink sink) const {
    const std::vector<uint64_t> selected(chosen.begin(), chosen.end());
    const EditorValue fields[] = {
        {EDITOR_TEXT("filter"), EDITOR_TEXT_VALUE, {}, EditorText(filter)},
        {EDITOR_TEXT("category"), EDITOR_TEXT_VALUE, {}, EditorText(category)},
        {EDITOR_TEXT("selection"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {selected.data(), uint32_t(selected.size())}},
        {EDITOR_TEXT("rows"), EDITOR_INTEGER_VALUE, {}, {}, shown.size()},
        {EDITOR_TEXT("catalog"), EDITOR_INTEGER_VALUE, {}, {}, catalog}};
    return sink.write ? sink.write(sink.data, fields, uint32_t(std::size(fields))) : EDITOR_INVALID;
}
EditorStatus BlocksView::Save(EditorSink sink) const {
    const std::vector<uint64_t> selected(chosen.begin(), chosen.end());
    std::vector<EditorString> paths;
    std::vector<uint64_t> opens;
    for (const auto &item : expanded) {
        paths.push_back(EditorText(item.first));
        opens.push_back(item.second);
    }
    const EditorValue fields[] = {
        {EDITOR_TEXT("filter"), EDITOR_TEXT_VALUE, {}, EditorText(filter)},
        {EDITOR_TEXT("category"), EDITOR_TEXT_VALUE, {}, EditorText(category)},
        {EDITOR_TEXT("selection"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {selected.data(), uint32_t(selected.size())}},
        {EDITOR_TEXT("column"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(sortColumn)},
        {EDITOR_TEXT("descending"), EDITOR_INTEGER_VALUE, {}, {}, descending},
        {EDITOR_TEXT("paths"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {paths.data(), uint32_t(paths.size())}},
        {EDITOR_TEXT("open"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {opens.data(), uint32_t(opens.size())}}};
    return sink.write ? sink.write(sink.data, fields, uint32_t(std::size(fields))) : EDITOR_INVALID;
}
EditorStatus BlocksView::Restore(const EditorRequest &request) {
    std::vector<EditorField> fields;
    if (CopyEditorFields(fields, request.values, request.count) != EDITOR_OK)
        return EDITOR_INVALID;
    const EditorResponse state{EDITOR_OK, {}, std::move(fields)};
    const auto *text = EditorClient::Find(state, "filter"), *path = EditorClient::Find(state, "category"),
               *selected = EditorClient::Find(state, "selection"), *column = EditorClient::Find(state, "column"),
               *reverse = EditorClient::Find(state, "descending"), *paths = EditorClient::Find(state, "paths"),
               *opens = EditorClient::Find(state, "open");
    if (!text || text->type != EDITOR_TEXT_VALUE || !path || path->type != EDITOR_TEXT_VALUE || !selected ||
        selected->type != EDITOR_INTEGER_LIST || !column || column->type != EDITOR_INTEGER_VALUE ||
        column->integer >= 6 || !reverse || reverse->type != EDITOR_INTEGER_VALUE || reverse->integer > 1 || !paths ||
        paths->type != EDITOR_TEXT_LIST || !opens || opens->type != EDITOR_INTEGER_LIST ||
        paths->texts.size() != opens->integers.size())
        return EDITOR_INVALID;
    std::map<std::string, bool> nextExpanded;
    for (size_t i = 0; i < paths->texts.size(); ++i) {
        if (opens->integers[i] > 1 || !nextExpanded.emplace(paths->texts[i], opens->integers[i] != 0).second)
            return EDITOR_INVALID;
    }
    filter = text->text;
    category = path->text;
    chosen = {selected->integers.begin(), selected->integers.end()};
    sortColumn = int(column->integer);
    descending = reverse->integer != 0;
    expanded = std::move(nextExpanded);
    restoreExpanded = listChanged = true;
    ++revision;
    return EDITOR_OK;
}
} // namespace
bool RegisterBuildingBlockViews(EditorApplication &editor, std::string &error) {
    const EditorString dependency = EDITOR_TEXT("resources.catalog");
    const EditorParameter parameters[] = {{EDITOR_TEXT("filter"), EDITOR_TEXT_VALUE, 1},
                                          {EDITOR_TEXT("category"), EDITOR_TEXT_VALUE, 1},
                                          {EDITOR_TEXT("selection"), EDITOR_INTEGER_LIST, 1}};
    const EditorParameter fields[] = {{EDITOR_TEXT("filter"), EDITOR_TEXT_VALUE},
                                      {EDITOR_TEXT("category"), EDITOR_TEXT_VALUE},
                                      {EDITOR_TEXT("selection"), EDITOR_INTEGER_LIST},
                                      {EDITOR_TEXT("rows"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("catalog"), EDITOR_INTEGER_VALUE}};
    EditorViewDesc view{};
    view.size = sizeof(view);
    view.imguiVersion = EDITOR_IMGUI_VERSION;
    view.id = EDITOR_TEXT("resource.building-blocks");
    view.provider = EDITOR_TEXT("builtin.building-blocks");
    view.title = EDITOR_TEXT("Building Blocks");
    view.area = EDITOR_WORKSPACE;
    view.create = [](void *, const EditorHost *, EditorView) -> void * { return new BlocksView; };
    view.destroy = [](void *data) { delete static_cast<BlocksView *>(data); };
    view.draw = [](void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) {
        static_cast<BlocksView *>(data)->Draw(EditorClient(host, id), *frame);
    };
    view.parameters = parameters;
    view.count = uint32_t(std::size(parameters));
    view.navigate = [](void *data, const EditorHost *host, const EditorRequest *request) {
        return static_cast<BlocksView *>(data)->Navigate(EditorClient(host, request->scope.view), *request);
    };
    view.stateVersion = 1;
    view.saveState = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        return static_cast<BlocksView *>(data)->Save(sink);
    };
    view.restoreState = [](void *data, const EditorHost *, const EditorRequest *request) {
        return static_cast<BlocksView *>(data)->Restore(*request);
    };
    view.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        return static_cast<BlocksView *>(data)->Inspect(sink);
    };
    view.inspectRevision = [](void *data, const EditorHost *) { return static_cast<BlocksView *>(data)->revision; };
    view.fields = fields;
    view.fieldCount = uint32_t(std::size(fields));
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("resources.building-blocks");
    module.dependencies = &dependency;
    module.dependencyCount = 1;
    module.views = &view;
    module.viewCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev
