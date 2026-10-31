#include "features/environment/EnvironmentViewData.h"
#include <set>

namespace vdev {
namespace environment {
namespace {
struct Plugins {
    std::set<std::string> open{"Bitmap Readers", "Movie Readers"};
    uint64_t revision = 0;
    std::vector<EditorField> rows;
    bool Read(const EditorClient &client) {
        std::vector<EditorField> next;
        uint64_t first = 0, version = 0;
        do {
            const auto page = client.Query("plugins.list", {IntegerArgument("first", first), IntegerArgument("revision", version)});
            const auto *values = EditorClient::Find(page, "rows");
            if (page.status != EDITOR_OK || !values) return false;
            const auto current = EditorClient::Integer(page, "revision"), end = EditorClient::Integer(page, "next"), total = EditorClient::Integer(page, "total");
            if ((version && version != current) || end < first || end > total || (end == first && first < total)) return false;
            version = current;
            next.insert(next.end(), values->fields.begin(), values->fields.end());
            if (end == total) break;
            first = end;
        } while (true);
        rows = std::move(next); revision = version; return true;
    }
    void Draw(const EditorClient &client, EditorViewFrame &frame) {
        const auto version = client.Revision("plugins.list");
        if (!revision || !version || version != revision) Read(client);
        ImGui::SetNextWindowSize(ImVec2(840, 600), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(.5f, .5f));
        EditorImGuiWindow window(frame, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse);
        if (!window) return;
        const bool usable = Available(client);
        const auto execute = [&](const char *command, const EditorField &row, bool active = false) {
            std::vector<EditorValue> args{IntegerArgument("plugin", Number(row, "id")), IntegerArgument("revision", revision)};
            if (std::string(command) == "plugins.set-manager-active") args.push_back(IntegerArgument("active", active));
            client.Command(command, args);
        };
        const float footer = ImGui::GetFrameHeightWithSpacing() + 6;
        if (ImGui::BeginTable("plugins", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX, ImVec2(0, -footer))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Plugin", ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed, 44);
            ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthFixed, 260);
            ImGui::TableSetupColumn("Dll", ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableSetupColumn("Author", ImGuiTableColumnFlags_WidthFixed, 110);
            ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 60); ImGui::TableHeadersRow();
            for (size_t i = 0; i < rows.size();) {
                const auto category = Text(rows[i], "category");
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemOpen(open.count(category) != 0, ImGuiCond_Always);
                const bool expanded = ImGui::TreeNodeEx(category.c_str(), ImGuiTreeNodeFlags_SpanAllColumns);
                if (expanded) open.insert(category); else open.erase(category);
                do {
                    const auto &row = rows[i++];
                    if (!expanded || !Number(row, "id")) continue;
                    ImGui::PushID(int(Number(row, "id"))); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Indent();
                    const auto label = Text(row, "name") + (Number(row, "unloaded") ? " (unloaded)" : "");
                    ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap); ImGui::Unindent();
                    if (Number(row, "loadable") && ImGui::BeginPopupContextItem("pluginmenu")) {
                        if (ImGui::MenuItem("Temporary Unload", nullptr, false, usable && Number(row, "loaded") && !Number(row, "unloaded"))) execute("plugins.unload", row);
                        if (ImGui::MenuItem("Reload", nullptr, false, usable && Number(row, "unloaded"))) execute("plugins.reload", row);
                        ImGui::EndPopup();
                    }
                    const auto error = Text(row, "error");
                    if (!error.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", error.c_str());
                    ImGui::TableSetColumnIndex(1);
                    const auto active = int64_t(Number(row, "active"));
                    if (active >= 0) {
                        const char *mark = active ? "A" : "-";
                        if (Number(row, "manager")) {
                            ImGui::BeginDisabled(!usable);
                            if (ImGui::Selectable(mark, false)) execute("plugins.set-manager-active", row, !active);
                            ImGui::EndDisabled();
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to switch the manager on or off");
                        } else ImGui::TextUnformatted(mark);
                    }
                    ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(Text(row, "description").c_str());
                    ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(Text(row, "dll").c_str());
                    ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(Text(row, "author").c_str());
                    ImGui::TableSetColumnIndex(5); ImGui::Text("%llu", static_cast<unsigned long long>(Number(row, "version"))); ImGui::PopID();
                } while (i < rows.size() && Text(rows[i], "category") == category);
                if (expanded) ImGui::TreePop();
            }
            ImGui::EndTable();
        }
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 90);
        if (ImGui::Button("Close", ImVec2(90, 0))) frame.close = 1;
    }
};
} // namespace
EditorViewDesc PluginView() {
    EditorViewDesc view{};
    view.size = sizeof(view); view.imguiVersion = EDITOR_IMGUI_VERSION; view.id = EDITOR_TEXT("environment.plugins");
    view.provider = EDITOR_TEXT("builtin.environment-views"); view.title = EDITOR_TEXT("Installed Plugins");
    view.area = EDITOR_APPLICATION; view.singleInstance = 1;
    view.create = [](void *, const EditorHost *, EditorView) -> void * { return new Plugins; };
    view.destroy = [](void *data) { delete static_cast<Plugins *>(data); };
    view.draw = [](void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) { static_cast<Plugins *>(data)->Draw(EditorClient(host, id), *frame); };
    static const EditorParameter fields[] = {{EDITOR_TEXT("open-categories"), EDITOR_TEXT_LIST}};
    view.fields = fields; view.fieldCount = 1; view.stateVersion = 1;
    view.saveState = view.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &plugins = *static_cast<Plugins *>(data);
        EditorField field{"open-categories", EDITOR_TEXT_LIST}; field.texts.assign(plugins.open.begin(), plugins.open.end());
        const std::vector<EditorField> fields{field};
        const auto values = BorrowEditorFields(fields);
        return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
    };
    view.restoreState = [](void *data, const EditorHost *, const EditorRequest *request) {
        if (request->count != 1 || request->values[0].type != EDITOR_TEXT_LIST || request->values[0].texts.count > 4096) return EDITOR_INVALID;
        std::set<std::string> open;
        const auto &values = request->values[0].texts;
        for (uint32_t i = 0; i < values.count; ++i) open.emplace(values.data[i].data, values.data[i].size);
        static_cast<Plugins *>(data)->open = std::move(open); return EDITOR_OK;
    };
    return view;
}
} // namespace environment
} // namespace vdev
