// Dev's Data Resources tabs (Resources > Open / Create New Data Resource):
// the resource's folder tree beside the files of the selected folder. A
// double click loads a file into the composition, as Dev does when one is
// dropped on the level; files can be dragged onto the Schematic.

#include "ui/tools/ToolWindows.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

#include "imgui.h"
#include "imgui_internal.h"

#include "ui/controls/DevTheme.h"
#include "ui/schematic/SchematicCanvas.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

namespace {

const char *ClassLabel(CK_CLASSID cid) {
    if (!cid) return "";
    const char *name = CKClassIDToString(cid);
    return name ? name : "";
}

} // namespace

bool ToolWindows::OpenDataResource(const std::string &file, bool remember) {
    for (const Resource &resource : m_Resources)
        if (_stricmp(resource.data.file.c_str(), file.c_str()) == 0) return true;
    Resource resource;
    std::string error;
    if (!ReadDataResource(file, resource.data, &error)) {
        m_State.Log(LogLevel::Error, error);
        return false;
    }
    // Dev adds the resource's folders to the path manager (Dev.exe sub_561750).
    const int added = AddDataResourcePaths(m_State.host.Context(), resource.data);
    resource.selectedFolder = resource.data.root.path;
    resource.selectedClass = resource.data.root.cid;
    resource.files = resource.data.root.files;
    m_Resources.push_back(std::move(resource));
    m_State.Log(LogLevel::Info, "Opened Data Resource " + file + " (" + std::to_string(added) +
                                    " paths added to the path manager)");
    if (remember) {
        auto &list = m_State.preferences.dataResources;
        if (std::find(list.begin(), list.end(), file) == list.end()) list.push_back(file);
        if (applyPreferences) applyPreferences();
    }
    return true;
}

bool ToolWindows::CreateNewDataResource(const std::string &file) {
    std::string error;
    if (!CreateDataResource(file, &error)) {
        m_State.Log(LogLevel::Error, error);
        return false;
    }
    std::filesystem::path rsc = std::filesystem::u8path(file);
    if (rsc.extension() != ".rsc") rsc += ".rsc";
    return OpenDataResource(rsc.u8string());
}

void ToolWindows::CloseAllResources() {
    m_Resources.clear();
}

void ToolWindows::LoadResource(const std::string &path, CK_CLASSID cid) {
    if (!m_State.HasDocument()) {
        m_State.Log(LogLevel::Info, "Open or create a composition first");
        return;
    }
    // The object the Level Manager shows selected receives Scripts and animations.
    const CK_ID target = levelTarget ? levelTarget() : 0;
    auto load = [this, path, cid, target](const std::vector<DuplicateName> *duplicates) {
        if (!m_State.HasDocument()) return;
        const EditResult result = LoadResourceFile(*m_State.document, path, cid, target, nullptr, duplicates);
        m_State.Log(result.ok ? LogLevel::Info : LogLevel::Warning,
                    (result.ok ? std::string("Loaded: ") : std::string("Load failed: ")) + AnsiToUtf8(result.message) + " (" + path + ")");
        if (result.ok) m_Canvas.Invalidate();
    };
    // The imported files ask about duplicate names (Dev.exe sub_56BC90).
    if (ResourceFileIsImported(cid)) m_State.LoadAskingDuplicates(path, load);
    else load(nullptr);
}

void ToolWindows::ImportResource(const std::string &path, Document::ImportMode mode) {
    if (!m_State.HasDocument()) return;
    m_State.LoadAskingDuplicates(path, [this, path, mode](const std::vector<DuplicateName> *duplicates) {
        std::string error;
        if (!m_State.HasDocument() || !m_State.document->Import(path, mode, &error, nullptr, duplicates, m_State.preferences.NewSceneEnvironment()))
            m_State.Log(LogLevel::Warning, error);
        m_Canvas.Invalidate();
    });
}

void ToolWindows::SelectResourceFolder(const std::string &relative) {
    if (m_Resources.empty() || relative.empty()) return;
    Resource &resource = m_Resources.back();
    std::function<const DataResourceFolder *(const DataResourceFolder &)> find =
        [&](const DataResourceFolder &folder) -> const DataResourceFolder * {
        if (folder.path.size() >= relative.size() &&
            _stricmp(folder.path.c_str() + folder.path.size() - relative.size(), relative.c_str()) == 0)
            return &folder;
        for (const DataResourceFolder &child : folder.folders)
            if (const DataResourceFolder *found = find(child)) return found;
        return nullptr;
    };
    if (const DataResourceFolder *folder = find(resource.data.root)) {
        ShowFiles(resource, *folder);
    } else {
        m_State.Log(LogLevel::Warning, "--resource-folder: no folder " + relative);
    }
}

void ToolWindows::ShowFiles(Resource &resource, const DataResourceFolder &folder) {
    resource.selectedFolder = folder.path;
    resource.selectedClass = folder.cid;
    resource.files = folder.files;
    resource.selectedFile.clear();
    resource.selectedFiles.clear();
    resource.descriptions.clear();
    for (const std::string &name : folder.files) {
        const std::string path = (std::filesystem::u8path(folder.path) / std::filesystem::u8path(name)).u8string();
        BehaviorDescription description;
        if (_stricmp(std::filesystem::u8path(name).extension().u8string().c_str(), ".nms") == 0 &&
            ReadBehaviorDescription(path, description))
            resource.descriptions[name] = description;
    }
}

void ToolWindows::ResourceFolder(Resource &resource, const DataResourceFolder &folder) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (folder.folders.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (resource.selectedFolder == folder.path) flags |= ImGuiTreeNodeFlags_Selected;
    if (&folder == &resource.data.root) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    const bool open = ImGui::TreeNodeEx(folder.path.c_str(), flags, "%s", folder.name.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) ShowFiles(resource, folder);
    if (folder.cid && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ClassLabel(folder.cid));
    if (!open) return;
    for (const DataResourceFolder &child : folder.folders) ResourceFolder(resource, child);
    ImGui::TreePop();
}

void ToolWindows::DataResources(ImGuiID dock) {
    int index = 0;
    for (Resource &resource : m_Resources) {
        const std::string title = std::filesystem::u8path(resource.data.file).stem().u8string() +
                                  "###resource" + std::to_string(index++);
        if (dock) ImGui::SetNextWindowDockID(dock, ImGuiCond_FirstUseEver);
        if (!BeginDevWindow(title.c_str(), &resource.open)) {
            ImGui::End();
            continue;
        }
        // Documentation "Data Resources": closed by its tab's menu (Close)
        // or with the Delete key.
        ImGuiWindow *host = ImGui::GetCurrentWindow();
        if (ImGui::BeginPopupContextItem("resourcetab")) {
            if (ImGui::MenuItem("Move to separated window", nullptr, false, host->DockIsActive))
                ImGui::DockContextQueueUndockWindow(ImGui::GetCurrentContext(), host);
            if (ImGui::MenuItem("Close")) resource.open = false;
            ImGui::EndPopup();
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            resource.open = false;
        const float treeWidth = std::max(240.0f, ImGui::GetContentRegionAvail().x * 0.3f);
        ImGui::BeginChild("folders", ImVec2(treeWidth, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        ResourceFolder(resource, resource.data.root);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("files", ImVec2(0, 0), ImGuiChildFlags_Borders);
        if (resource.files.empty()) ImGui::TextDisabled("(no file)");
        // Behavior files with their descriptions: Dev's columns (Apply to,
        // Description, Version, Author).
        const bool described = !resource.descriptions.empty();
        const bool table = described && ImGui::BeginTable("described", 5, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                                                              ImGuiTableFlags_SizingStretchProp);
        if (table) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("Apply to", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("Author", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableHeadersRow();
        }
        for (const std::string &name : resource.files) {
            const std::string path = (std::filesystem::u8path(resource.selectedFolder) / std::filesystem::u8path(name)).u8string();
            ImGui::PushID(name.c_str());
            if (table) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
            }
            const bool nms = _stricmp(std::filesystem::u8path(name).extension().u8string().c_str(), ".nms") == 0;
            // Documentation "Data Resources": "select one or several elements
            // and then drag the elements" (Ctrl: one more, Shift: a range).
            const bool fileSelected = resource.selectedFiles.count(name) != 0;
            if (ImGui::Selectable(name.c_str(), fileSelected,
                                  ImGuiSelectableFlags_AllowDoubleClick | (table ? ImGuiSelectableFlags_SpanAllColumns : 0))) {
                const ImGuiIO &io = ImGui::GetIO();
                if (io.KeyShift && !resource.selectedFile.empty()) {
                    auto from = std::find(resource.files.begin(), resource.files.end(), resource.selectedFile);
                    auto to = std::find(resource.files.begin(), resource.files.end(), name);
                    if (from != resource.files.end() && to != resource.files.end()) {
                        if (to < from) std::swap(from, to);
                        if (!io.KeyCtrl) resource.selectedFiles.clear();
                        for (auto it = from; it <= to; ++it) resource.selectedFiles.insert(*it);
                    }
                } else if (io.KeyCtrl) {
                    if (fileSelected) resource.selectedFiles.erase(name);
                    else resource.selectedFiles.insert(name);
                    resource.selectedFile = name;
                } else {
                    resource.selectedFiles = {name};
                    resource.selectedFile = name;
                }
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) LoadResource(path, resource.selectedClass);
            }
            if (ImGui::BeginDragDropSource()) {
                ResourceFilePayload payload = {};
                std::snprintf(payload.path, sizeof(payload.path), "%s", path.c_str());
                payload.cid = resource.selectedClass;
                // The files selected go together when this one is.
                if (!ImGui::GetDragDropPayload() || !ImGui::GetDragDropPayload()->IsDataType(RESOURCE_FILE_PAYLOAD)) {
                    m_State.draggedResources.clear();
                    std::vector<std::string> names{name};
                    if (fileSelected) names.assign(resource.selectedFiles.begin(), resource.selectedFiles.end());
                    for (const std::string &each : names) {
                        ResourceFilePayload one = {};
                        const auto selectedPath = (std::filesystem::u8path(resource.selectedFolder) / std::filesystem::u8path(each)).u8string();
                        std::snprintf(one.path, sizeof(one.path), "%s", selectedPath.c_str());
                        one.cid = resource.selectedClass;
                        m_State.draggedResources.push_back(one);
                    }
                }
                ImGui::SetDragDropPayload(RESOURCE_FILE_PAYLOAD, &payload, sizeof(payload));
                if (m_State.draggedResources.size() > 1) ImGui::Text("%d files", static_cast<int>(m_State.draggedResources.size()));
                else ImGui::TextUnformatted(name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginPopupContextItem("file")) {
                resource.selectedFile = name;
                if (!fileSelected) resource.selectedFiles = {name};
                if (ImGui::MenuItem("Load")) LoadResource(path, resource.selectedClass);
                ImGui::Separator();
                if (ImGui::MenuItem("Import as Character")) LoadResource(path, CKCID_CHARACTER);
                if (ImGui::MenuItem("Import as Place")) ImportResource(path, Document::ImportMode::Place);
                if (ImGui::MenuItem("Import as Scene")) ImportResource(path, Document::ImportMode::Scene);
                if (nms) {
                    ImGui::Separator();
                    if (ImGui::MenuItem("Behavior Information...")) m_BehaviorInfo.Open(path, std::string(), 0);
                }
                ImGui::EndPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
            if (table) {
                auto found = resource.descriptions.find(name);
                if (found != resource.descriptions.end()) {
                    const BehaviorDescription &d = found->second;
                    const char *className = d.cid ? CKClassIDToString(d.cid) : nullptr;
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(className ? AnsiToUtf8(className).c_str() : "");
                    ImGui::TableNextColumn();
                    ImGui::Text("%g", d.version);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(AnsiToUtf8(d.author).c_str());
                    ImGui::TableNextColumn();
                    const std::string text = AnsiToUtf8(d.description);
                    ImGui::TextUnformatted(text.substr(0, text.find('\n')).c_str());
                    if (ImGui::IsItemHovered() && !text.empty()) ImGui::SetTooltip("%s", text.c_str());
                }
            }
            ImGui::PopID();
        }
        if (table) ImGui::EndTable();
        // The description edited: the list shows it.
        if (m_BehaviorInfo.Draw() == DialogAction::Ok) {
            if (!m_BehaviorInfo.Written())
                m_State.Log(LogLevel::Warning,
                            "Behavior Information: cannot write " + BehaviorDescriptionPath(m_BehaviorInfo.Path()));
            BehaviorDescription description;
            const std::string name = std::filesystem::u8path(m_BehaviorInfo.Path()).filename().u8string();
            const std::string folder = std::filesystem::u8path(m_BehaviorInfo.Path()).parent_path().u8string();
            if (ReadBehaviorDescription(m_BehaviorInfo.Path(), description))
                for (Resource &each : m_Resources)
                    if (_stricmp(each.selectedFolder.c_str(), folder.c_str()) == 0) each.descriptions[name] = description;
        }
        ImGui::EndChild();
        ImGui::End();
    }
    // Closed tabs are forgotten (not opened again at start).
    for (const Resource &resource : m_Resources)
        if (!resource.open) {
            auto &list = m_State.preferences.dataResources;
            list.erase(std::remove(list.begin(), list.end(), resource.data.file), list.end());
            if (applyPreferences) applyPreferences();
        }
    m_Resources.erase(std::remove_if(m_Resources.begin(), m_Resources.end(), [](const Resource &r) { return !r.open; }),
                      m_Resources.end());
}

} // namespace vdev
