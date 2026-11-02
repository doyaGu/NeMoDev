#include "workspace/EditorWorkspace.h"
#include "workspace/EditorClient.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <set>

namespace vdev {
namespace {
std::string Text(EditorString value) { return {value.data ? value.data : "", value.size}; }
bool NoArguments(const EditorCommandDesc &command) {
    for (uint32_t i = 0; i < command.count; ++i)
        if (!command.parameters[i].optional) return false;
    return true;
}
} // namespace
void EditorWorkspace::ToolMenus() {
    const auto actions = m_Application.Commands();
    const auto locations = m_Application.Toolbars();
    const auto selectProvider = [&](const char *kind, const std::string &id, const std::string &provider) {
        auto config = m_Application.Configuration();
        config.providers.erase(std::remove_if(config.providers.begin(), config.providers.end(),
                                              [&](const auto &p) { return p.kind == kind && p.id == id; }),
                               config.providers.end());
        config.providers.push_back({kind, id, provider});
        Configure(config, m_Error);
    };
    if (ImGui::BeginMenu("Tools")) {
        for (const auto &tool : m_Application.Tools()) {
            const auto view = ActionView(Text(tool.view), Focused());
            const auto *selected = m_Application.ActiveTool(view);
            if (ImGui::MenuItem(tool.title.data, nullptr, selected && Text(selected->id) == Text(tool.id), view != 0))
                EditorClient(m_Application.Host(), view).Command(Text(tool.id).c_str());
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tool Providers")) {
        for (const auto &tool : m_Application.Tools(true)) {
            const auto label = Text(tool.title) + " (" + Text(tool.provider) + ")";
            if (ImGui::MenuItem(label.c_str())) selectProvider("command", Text(tool.id), Text(tool.provider));
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Shortcuts")) {
        for (const auto &action : actions) {
            if (!NoArguments(action)) continue;
            const auto id = Text(action.id);
            ImGui::PushID(id.c_str());
            if (ImGui::BeginMenu(action.title.data)) {
                auto draft = m_BindingDrafts.find(id);
                if (draft == m_BindingDrafts.end()) {
                    std::string chords;
                    for (const auto &binding : m_Application.Bindings())
                        if (binding.first == id) {
                            if (!chords.empty()) chords += ";";
                            chords += binding.second;
                        }
                    draft = m_BindingDrafts.emplace(id, std::move(chords)).first;
                }
                ImGui::TextUnformatted(action.view.size ? action.view.data : "Application");
                ImGui::InputText("Chords", &draft->second);
                ImGui::TextDisabled("Separate chords with ;  Empty disables the action.");
                if (ImGui::Button("Apply")) {
                    auto config = m_Application.Configuration();
                    config.bindings.erase(std::remove_if(config.bindings.begin(), config.bindings.end(),
                                                         [&](const auto &b) { return b.first == id; }),
                                          config.bindings.end());
                    size_t begin = 0;
                    do {
                        const auto end = draft->second.find(';', begin);
                        config.bindings.emplace_back(
                            id, draft->second.substr(begin, end == std::string::npos ? end : end - begin));
                        if (end == std::string::npos) break;
                        begin = end + 1;
                    } while (begin <= draft->second.size());
                    Configure(config, m_Error);
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset")) {
                    auto config = m_Application.Configuration();
                    config.bindings.erase(std::remove_if(config.bindings.begin(), config.bindings.end(),
                                                         [&](const auto &b) { return b.first == id; }),
                                          config.bindings.end());
                    if (Configure(config, m_Error)) m_BindingDrafts.erase(id);
                }
                ImGui::EndMenu();
            }
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }
    if (!ImGui::BeginMenu("Toolbar Entries")) return;
    const auto entries = m_Application.ToolbarItems();
    const auto place = [&](ToolbarPlacement item) {
        auto config = m_Application.Configuration();
        auto old = std::find_if(config.toolbarItems.begin(), config.toolbarItems.end(),
                                [&](const auto &p) { return p.id == item.id; });
        if (old == config.toolbarItems.end())
            config.toolbarItems.push_back(std::move(item));
        else
            *old = std::move(item);
        Configure(config, m_Error);
    };
    for (const auto &entry : entries) {
        ImGui::PushID(entry.id.c_str());
        if (ImGui::BeginMenu((entry.title.empty() ? entry.id : entry.title).c_str())) {
            ToolbarPlacement item;
            item.id = entry.id;
            for (const auto &old : m_Application.Configuration().toolbarItems)
                if (old.id == item.id) item = old;
            if (ImGui::MenuItem("Visible", nullptr, true)) {
                item.fields |= ToolbarVisibility;
                item.visible = false;
                place(item);
            }
            float at[] = {entry.x, entry.y}, size[] = {entry.width, entry.height};
            if (ImGui::InputFloat2("Position", at, "%.0f", ImGuiInputTextFlags_EnterReturnsTrue)) {
                item.x = at[0];
                item.y = at[1];
                item.fields |= ToolbarPosition;
                place(item);
            }
            if (ImGui::InputFloat2("Size", size, "%.0f", ImGuiInputTextFlags_EnterReturnsTrue)) {
                item.width = size[0];
                item.height = size[1];
                item.fields |= ToolbarSize;
                place(item);
            }
            if (ImGui::BeginMenu("Location")) {
                for (const auto &location : locations)
                    if (ImGui::MenuItem(location.second.title.data, nullptr, location.first == entry.location)) {
                        item.location = location.first;
                        item.fields |= ToolbarLocation;
                        place(item);
                    }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Action")) {
                for (const auto &action : actions)
                    if (NoArguments(action) &&
                        ImGui::MenuItem(action.title.data, nullptr, Text(action.id) == entry.action)) {
                        item.action = Text(action.id);
                        item.fields |= ToolbarAction;
                        place(item);
                    }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Provider")) {
                for (const auto &provider : m_Application.ToolbarProviders())
                    if (Text(provider.id) == entry.id &&
                        ImGui::MenuItem(provider.provider.data, nullptr, entry.provider == Text(provider.provider)))
                        selectProvider("toolbar", entry.id, Text(provider.provider));
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Reset")) {
                auto config = m_Application.Configuration();
                config.toolbarItems.erase(std::remove_if(config.toolbarItems.begin(), config.toolbarItems.end(),
                                                         [&](const auto &p) { return p.id == entry.id; }),
                                          config.toolbarItems.end());
                Configure(config, m_Error);
            }
            ImGui::EndMenu();
        }
        ImGui::PopID();
    }
    if (ImGui::BeginMenu("Hidden Items")) {
        const auto hidden = m_Application.Configuration().toolbarItems;
        for (auto item : hidden)
            if ((item.fields & ToolbarVisibility) && !item.visible && ImGui::MenuItem(item.id.c_str())) {
                item.visible = true;
                place(item);
            }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Add Item")) {
        ImGui::InputText("ID", &m_NewToolbarId);
        ImGui::InputText("Title", &m_NewToolbarTitle);
        ImGui::InputFloat2("Position", m_NewToolbarPosition);
        ImGui::InputFloat2("Size", m_NewToolbarSize);
        if (ImGui::BeginCombo("Location", m_NewToolbarLocation.c_str())) {
            for (const auto &location : locations)
                if (ImGui::Selectable(location.second.title.data, m_NewToolbarLocation == location.first))
                    m_NewToolbarLocation = location.first;
            ImGui::EndCombo();
        }
        if (ImGui::BeginCombo("Action", m_NewToolbarAction.c_str())) {
            for (const auto &action : actions)
                if (NoArguments(action) && ImGui::Selectable(action.title.data, m_NewToolbarAction == Text(action.id)))
                    m_NewToolbarAction = Text(action.id);
            ImGui::EndCombo();
        }
        if (ImGui::Button("Add")) {
            ToolbarPlacement item;
            item.id = m_NewToolbarId;
            item.title = m_NewToolbarTitle;
            item.location = m_NewToolbarLocation;
            item.action = m_NewToolbarAction;
            item.x = m_NewToolbarPosition[0];
            item.y = m_NewToolbarPosition[1];
            item.width = m_NewToolbarSize[0];
            item.height = m_NewToolbarSize[1];
            item.fields = ToolbarLocation | ToolbarAction | ToolbarTitle | ToolbarPosition | ToolbarSize;
            place(std::move(item));
        }
        ImGui::EndMenu();
    }
    ImGui::EndMenu();
}
} // namespace vdev
