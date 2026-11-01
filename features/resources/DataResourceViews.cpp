#include "features/resources/DataResourceViews.h"
#include "editor/ImGuiDrawing.h"
#include "imgui_internal.h"
#include "workspace/EditorClient.h"
#include "workspace/ResourceDrag.h"
#include <algorithm>
#include <iterator>
#include <set>
#include <stdexcept>

namespace vdev {
namespace {
const EditorValue &Field(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key)
            return request.values[i];
    throw std::invalid_argument("Missing resource navigation argument");
}
struct ResourceView {
    struct Folder {
        std::string name, path, type;
        uint64_t parent = 0, cid = 0;
    };
    struct File {
        std::string name, path, type, version, author, description;
        bool behavior = false;
    };
    std::string library, title, folder, anchor;
    std::vector<Folder> folders;
    std::vector<File> files;
    std::set<std::string> selected, expanded;
    uint64_t catalog = 0, sourceVersion = 0, revision = 1, cid = 0;
    bool restoreExpanded = true;
    EditorStatus Refresh(const EditorClient &);
    EditorStatus ReadFiles(const EditorClient &, const std::string &path);
    EditorStatus Navigate(const EditorClient &, const EditorRequest &);
    EditorStatus Save(EditorSink) const;
    EditorStatus Restore(const EditorRequest &);
    EditorStatus Inspect(EditorSink) const;
    void Draw(const EditorClient &, EditorViewFrame &);
    void Tree(const EditorClient &, size_t);
    void Load(const EditorClient &, const File &) const;
};
const std::vector<std::string> *Texts(const EditorResponse &result, const char *key, size_t count) {
    const auto *field = EditorClient::Find(result, key);
    return field && field->texts.size() == count ? &field->texts : nullptr;
}
EditorStatus ResourceView::ReadFiles(const EditorClient &client, const std::string &path) {
    std::vector<File> next;
    uint64_t first = 0, total = 0, kind = 0;
    do {
        const auto page = client.Query("resource.files", {TextArgument("path", library), TextArgument("folder", path),
                                                          IntegerArgument("first", first)});
        if (page.status != EDITOR_OK)
            return page.status;
        if (EditorClient::Integer(page, "revision") != catalog)
            return EDITOR_STALE;
        total = EditorClient::Integer(page, "total");
        kind = EditorClient::Integer(page, "class");
        const auto *names = EditorClient::Find(page, "names"), *behavior = EditorClient::Find(page, "behaviors");
        if (!names || !behavior)
            return EDITOR_INVALID;
        const auto count = names->texts.size();
        const auto *paths = Texts(page, "paths", count), *types = Texts(page, "types", count),
                   *versions = Texts(page, "versions", count), *authors = Texts(page, "authors", count),
                   *descriptions = Texts(page, "descriptions", count);
        if (!paths || !types || !versions || !authors || !descriptions || behavior->integers.size() != count ||
            first + count > total || total > 1000000 || (!count && first < total))
            return EDITOR_INVALID;
        for (size_t i = 0; i < count; ++i)
            next.push_back({names->texts[i], (*paths)[i], (*types)[i], (*versions)[i], (*authors)[i],
                            (*descriptions)[i], behavior->integers[i] != 0});
        first += count;
    } while (first < total);
    files = std::move(next);
    cid = kind;
    folder = path;
    for (auto it = selected.begin(); it != selected.end();)
        if (std::none_of(files.begin(), files.end(), [&](const File &file) { return file.path == *it; }))
            it = selected.erase(it);
        else
            ++it;
    ++revision;
    return EDITOR_OK;
}
EditorStatus ResourceView::Refresh(const EditorClient &client) {
    if (library.empty())
        return EDITOR_NO_CHANGE;
    const auto version = client.Revision("resource.library");
    if (!version)
        return EDITOR_REFUSED;
    if (version == sourceVersion)
        return EDITOR_OK;
    const auto info = client.Query("resource.library", {TextArgument("path", library)});
    if (info.status != EDITOR_OK)
        return info.status;
    const auto catalogVersion = EditorClient::Integer(info, "revision");
    ResourceView next = *this;
    next.title = EditorClient::Text(info, "title");
    next.folders.clear();
    uint64_t first = 0, total = 0;
    do {
        const auto page =
            client.Query("resource.folders", {TextArgument("path", library), IntegerArgument("first", first)});
        if (page.status != EDITOR_OK)
            return page.status;
        if (EditorClient::Integer(page, "revision") != catalogVersion)
            return EDITOR_STALE;
        total = EditorClient::Integer(page, "total");
        const auto *names = EditorClient::Find(page, "names"), *classes = EditorClient::Find(page, "classes"),
                   *parents = EditorClient::Find(page, "parents");
        if (!names || !classes || !parents)
            return EDITOR_INVALID;
        const auto count = names->texts.size();
        const auto *paths = Texts(page, "paths", count), *types = Texts(page, "types", count);
        if (!paths || !types || classes->integers.size() != count || parents->integers.size() != count ||
            first + count > total || total > 100000 || (!count && first < total))
            return EDITOR_INVALID;
        for (size_t i = 0; i < count; ++i) {
            if (parents->integers[i] > first + i)
                return EDITOR_INVALID;
            next.folders.push_back(
                {names->texts[i], (*paths)[i], (*types)[i], parents->integers[i], classes->integers[i]});
        }
        first += count;
    } while (first < total);
    if (next.folders.empty())
        return EDITOR_INVALID;
    if (std::none_of(next.folders.begin(), next.folders.end(),
                     [&](const Folder &entry) { return entry.path == next.folder; }))
        next.folder = next.folders.front().path;
    next.catalog = catalogVersion;
    next.sourceVersion = version;
    const auto status = next.ReadFiles(client, next.folder);
    if (status != EDITOR_OK)
        return status;
    if (client.Revision("resource.library") != version) return EDITOR_STALE;
    *this = std::move(next);
    return EDITOR_OK;
}
EditorStatus ResourceView::Navigate(const EditorClient &client, const EditorRequest &request) {
    ResourceView next = *this;
    bool opened = false;
    const auto &path = Field(request, "path").text;
    if (path.size) {
        const std::string value(path.data, path.size);
        if (value != library) {
            next = {};
            next.library = value;
            opened = true;
        }
    }
    if (next.library.empty())
        return EDITOR_INVALID;
    auto status = next.Refresh(client);
    if (status != EDITOR_OK)
        return status;
    if (opened && !next.folders.empty())
        next.expanded.insert(next.folders.front().path);
    const auto &requested = Field(request, "folder").text;
    if (requested.size) {
        const std::string suffix(requested.data, requested.size);
        std::string chosen;
        for (const auto &entry : next.folders) {
            const auto &path = entry.path;
            if (path == suffix ||
                (path.size() > suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                 (path[path.size() - suffix.size() - 1] == '/' || path[path.size() - suffix.size() - 1] == '\\'))) {
                if (!chosen.empty())
                    return EDITOR_INVALID;
                chosen = path;
            }
        }
        if (chosen.empty())
            return EDITOR_INVALID;
        if (chosen != next.folder) {
            next.selected.clear();
            next.anchor.clear();
        }
        status = next.ReadFiles(client, chosen);
        if (status != EDITOR_OK)
            return status;
    }
    const auto &files = Field(request, "files").texts;
    if (files.count) {
        std::set<std::string> selected;
        for (uint32_t i = 0; i < files.count; ++i) {
            const std::string path(files.data[i].data, files.data[i].size);
            if (std::none_of(next.files.begin(), next.files.end(), [&](const File &file) { return file.path == path; }))
                return EDITOR_INVALID;
            selected.insert(path);
        }
        next.selected = std::move(selected);
    }
    ++next.revision;
    *this = std::move(next);
    return EDITOR_OK;
}
void ResourceView::Load(const EditorClient &client, const File &file) const {
    ObjectRef target{};
    const auto instances = client.Query("workspace.instances");
    const auto *ids = EditorClient::Find(instances, "views"), *roles = EditorClient::Find(instances, "roles");
    if (instances.status == EDITOR_OK && ids && roles && ids->integers.size() == roles->texts.size()) {
        for (size_t i = 0; i < ids->integers.size(); ++i)
            if (roles->texts[i] == "main.level") {
                const auto selection = client.Inspect(ids->integers[i]);
                const auto *objects = EditorClient::Find(selection, "selection");
                if (selection.status == EDITOR_OK && objects && objects->type == EDITOR_OBJECT_LIST &&
                    objects->objects.size() == 1)
                    target = objects->objects[0];
                break;
            }
    }
    const auto path = EditorText(file.path);
    const auto kind = cid;
    client.Command("resource.load",
                   {{EDITOR_TEXT("paths"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {&path, 1}},
                    {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {&kind, 1}},
                    {EDITOR_TEXT("targets"), EDITOR_OBJECT_LIST, {}, {}, 0, {&target, target ? 1u : 0u}}});
}
void ResourceView::Tree(const EditorClient &client, size_t index) {
    const auto &entry = folders[index];
    const bool children = index + 1 < folders.size() && folders[index + 1].parent == index + 1;
    const auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                       ImGuiTreeNodeFlags_SpanAvailWidth | (children ? 0 : ImGuiTreeNodeFlags_Leaf) |
                       (folder == entry.path ? ImGuiTreeNodeFlags_Selected : 0);
    if (restoreExpanded)
        ImGui::SetNextItemOpen(expanded.count(entry.path) != 0, ImGuiCond_Always);
    const bool open = ImGui::TreeNodeEx(entry.path.c_str(), flags, "%s", entry.name.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && folder != entry.path) {
        const auto status = ReadFiles(client, entry.path);
        if (status == EDITOR_OK) {
            selected.clear();
            anchor.clear();
        }
    }
    if (ImGui::IsItemHovered() && !entry.type.empty())
        ImGui::SetTooltip("%s", entry.type.c_str());
    if (open) {
        if (expanded.insert(entry.path).second)
            ++revision;
        for (size_t i = index + 1; i < folders.size(); ++i)
            if (folders[i].parent == index + 1)
                Tree(client, i);
        ImGui::TreePop();
    } else if (expanded.erase(entry.path))
        ++revision;
}
void ResourceView::Draw(const EditorClient &client, EditorViewFrame &frame) {
    Refresh(client);
    std::string window = frame.window.data;
    if (!title.empty())
        window = title + window.substr(window.find("###"));
    EditorViewFrame display = frame;
    display.window = EditorText(window);
    ImGui::SetNextWindowSize({640, 350}, ImGuiCond_FirstUseEver);
    EditorImGuiWindow visible(display);
    frame.close = display.close;
    if (!visible)
        return;
    if (ImGui::BeginPopupContextItem("resourcetab")) {
        if (ImGui::MenuItem("Move to separated window", nullptr, false, ImGui::GetCurrentWindow()->DockIsActive))
            ImGui::DockContextQueueUndockWindow(ImGui::GetCurrentContext(), ImGui::GetCurrentWindow());
        if (ImGui::MenuItem("Close"))
            frame.close = 1;
        ImGui::EndPopup();
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        frame.close = 1;
    if (library.empty()) {
        ImGui::TextDisabled("Open a Data Resource");
        return;
    }
    const float width = std::max(240.0f, ImGui::GetContentRegionAvail().x * .3f);
    ImGui::BeginChild("folders", {width, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    if (!folders.empty())
        Tree(client, 0);
    restoreExpanded = false;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("files", {0, 0}, ImGuiChildFlags_Borders);
    if (files.empty())
        ImGui::TextDisabled("(no file)");
    const bool described =
        std::any_of(files.begin(), files.end(), [](const File &file) { return !file.version.empty(); });
    const bool table = described && ImGui::BeginTable("described", 5,
                                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                                          ImGuiTableFlags_SizingStretchProp);
    if (table) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2);
        ImGui::TableSetupColumn("Apply to", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthStretch, .6f);
        ImGui::TableSetupColumn("Author", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableHeadersRow();
    }
    for (size_t i = 0; i < files.size(); ++i) {
        const auto &file = files[i];
        ImGui::PushID(file.path.c_str());
        if (table) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
        }
        const bool chosen = selected.count(file.path) != 0;
        if (ImGui::Selectable(file.name.c_str(), chosen,
                              ImGuiSelectableFlags_AllowDoubleClick |
                                  (table ? ImGuiSelectableFlags_SpanAllColumns : 0))) {
            const auto &io = ImGui::GetIO();
            if (io.KeyShift && !anchor.empty()) {
                const auto start =
                    std::find_if(files.begin(), files.end(), [&](const File &f) { return f.path == anchor; });
                if (start != files.end()) {
                    auto from = size_t(start - files.begin()), to = i;
                    if (to < from)
                        std::swap(from, to);
                    if (!io.KeyCtrl)
                        selected.clear();
                    for (size_t row = from; row <= to; ++row)
                        selected.insert(files[row].path);
                }
            } else if (io.KeyCtrl) {
                if (chosen)
                    selected.erase(file.path);
                else
                    selected.insert(file.path);
                anchor = file.path;
            } else {
                selected = {file.path};
                anchor = file.path;
            }
            ++revision;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                Load(client, file);
        }
        if (ImGui::BeginDragDropSource()) {
            std::vector<ResourceFile> drag;
            uint32_t source = 0;
            for (const auto &entry : files)
                if (entry.path == file.path || (chosen && selected.count(entry.path))) {
                    if (entry.path == file.path)
                        source = uint32_t(drag.size());
                    drag.push_back({entry.path, uint32_t(cid)});
                }
            const auto bytes = WriteResourceDrag(source, drag);
            if (!bytes.empty())
                ImGui::SetDragDropPayload(EDITOR_RESOURCE_FILE_PAYLOAD, bytes.data(), bytes.size(), ImGuiCond_Once);
            if (drag.size() > 1)
                ImGui::Text("%d files", int(drag.size()));
            else
                ImGui::TextUnformatted(file.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("file")) {
            if (!chosen) {
                selected = {file.path};
                anchor = file.path;
                ++revision;
            }
            if (ImGui::MenuItem("Load"))
                Load(client, file);
            ImGui::Separator();
            if (ImGui::MenuItem("Import as Character"))
                client.Command("document.import-character", {TextArgument("path", file.path)});
            if (ImGui::MenuItem("Import as Place"))
                client.Command("document.import-place", {TextArgument("path", file.path)});
            if (ImGui::MenuItem("Import as Scene"))
                client.Command("document.import-scene", {TextArgument("path", file.path)});
            if (file.behavior) {
                ImGui::Separator();
                if (ImGui::MenuItem("Behavior Information..."))
                    client.Command("resource.behavior-information", {TextArgument("path", file.path)});
            }
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", file.path.c_str());
        if (table) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(file.type.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(file.version.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(file.author.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(file.description.substr(0, file.description.find('\n')).c_str());
            if (ImGui::IsItemHovered() && !file.description.empty())
                ImGui::SetTooltip("%s", file.description.c_str());
        }
        ImGui::PopID();
    }
    if (table)
        ImGui::EndTable();
    ImGui::EndChild();
}
EditorStatus ResourceView::Save(EditorSink sink) const {
    std::vector<EditorString> chosen, open;
    for (const auto &path : selected)
        chosen.push_back(EditorText(path));
    for (const auto &path : expanded)
        open.push_back(EditorText(path));
    const EditorValue values[] = {
        TextArgument("library", library),
        TextArgument("folder", folder),
        TextArgument("anchor", anchor),
        {EDITOR_TEXT("selection"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {chosen.data(), uint32_t(chosen.size())}},
        {EDITOR_TEXT("expanded"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {open.data(), uint32_t(open.size())}}};
    return sink.write ? sink.write(sink.data, values, uint32_t(std::size(values))) : EDITOR_INVALID;
}
EditorStatus ResourceView::Restore(const EditorRequest &request) {
    ResourceView next;
    for (uint32_t i = 0; i < request.count; ++i) {
        const auto &value = request.values[i];
        const std::string key(value.key.data, value.key.size);
        if (key == "library" || key == "folder" || key == "anchor") {
            if (value.type != EDITOR_TEXT_VALUE)
                return EDITOR_INVALID;
            auto &target = key == "library" ? next.library : key == "folder" ? next.folder : next.anchor;
            target.assign(value.text.data, value.text.size);
        } else if (key == "selection" || key == "expanded") {
            if (value.type != EDITOR_TEXT_LIST)
                return EDITOR_INVALID;
            auto &target = key == "selection" ? next.selected : next.expanded;
            for (uint32_t j = 0; j < value.texts.count; ++j)
                target.emplace(value.texts.data[j].data, value.texts.data[j].size);
        } else
            return EDITOR_INVALID;
    }
    next.revision = revision + 1;
    *this = std::move(next);
    return EDITOR_OK;
}
EditorStatus ResourceView::Inspect(EditorSink sink) const {
    std::vector<EditorString> chosen, entries;
    for (const auto &path : selected)
        chosen.push_back(EditorText(path));
    for (const auto &file : files)
        entries.push_back(EditorText(file.path));
    const EditorValue values[] = {
        TextArgument("library", library),
        TextArgument("folder", folder),
        {EDITOR_TEXT("selection"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {chosen.data(), uint32_t(chosen.size())}},
        {EDITOR_TEXT("entries"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {entries.data(), uint32_t(entries.size())}}};
    return sink.write ? sink.write(sink.data, values, uint32_t(std::size(values))) : EDITOR_INVALID;
}
} // namespace
bool RegisterDataResourceViews(EditorApplication &editor, std::string &error) {
    const EditorParameter parameters[] = {{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, 1},
                                          {EDITOR_TEXT("folder"), EDITOR_TEXT_VALUE, 1},
                                          {EDITOR_TEXT("files"), EDITOR_TEXT_LIST, 1}};
    const EditorParameter fields[] = {{EDITOR_TEXT("library"), EDITOR_TEXT_VALUE},
                                      {EDITOR_TEXT("folder"), EDITOR_TEXT_VALUE},
                                      {EDITOR_TEXT("selection"), EDITOR_TEXT_LIST},
                                      {EDITOR_TEXT("entries"), EDITOR_TEXT_LIST}};
    EditorViewDesc view{};
    view.size = sizeof(view);
    view.imguiVersion = EDITOR_IMGUI_VERSION;
    view.id = EDITOR_TEXT("resource.data");
    view.provider = EDITOR_TEXT("builtin.data-resources");
    view.title = EDITOR_TEXT("Data Resource");
    view.area = EDITOR_WORKSPACE;
    view.create = [](void *, const EditorHost *, EditorView) -> void * { return new ResourceView; };
    view.destroy = [](void *data) { delete static_cast<ResourceView *>(data); };
    view.draw = [](void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) {
        static_cast<ResourceView *>(data)->Draw(EditorClient(host, id), *frame);
    };
    view.parameters = parameters;
    view.count = uint32_t(std::size(parameters));
    view.navigate = [](void *data, const EditorHost *host, const EditorRequest *request) {
        return static_cast<ResourceView *>(data)->Navigate(EditorClient(host, request->scope.view), *request);
    };
    view.stateVersion = 1;
    view.saveState = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        return static_cast<ResourceView *>(data)->Save(sink);
    };
    view.restoreState = [](void *data, const EditorHost *, const EditorRequest *request) {
        return static_cast<ResourceView *>(data)->Restore(*request);
    };
    view.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        return static_cast<ResourceView *>(data)->Inspect(sink);
    };
    view.inspectRevision = [](void *data, const EditorHost *) { return static_cast<ResourceView *>(data)->revision; };
    view.fields = fields;
    view.fieldCount = uint32_t(std::size(fields));
    const EditorString dependency = EDITOR_TEXT("resources.libraries");
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("resources.views");
    module.dependencies = &dependency;
    module.dependencyCount = 1;
    module.views = &view;
    module.viewCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev
