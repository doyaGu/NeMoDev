#include "features/environment/EnvironmentViewData.h"
#include "misc/cpp/imgui_stdlib.h"
#include "imgui_internal.h"
#include "ui/controls/TypeAhead.h"
#include <algorithm>
#include <set>

namespace vdev {
namespace environment {
namespace {
struct Category { std::string name; std::vector<std::string> paths; };
struct Paths {
    std::vector<Category> categories;
    size_t category = 0;
    std::set<size_t> selected;
    int editing = -1;
    std::string text, message;
    bool popup = false, loaded = false;
    ImGuiID popupId = 0;
    uint64_t revision = 0, changes = 0;
    EditorOperation applying = 0;
    struct Browse { EditorOperation operation = 0; uint64_t changes = 0; size_t category = 0, row = 0; std::string before; } browse;
    ~Paths() {
        if (!popupId) return;
        if (auto *context = ImGui::GetCurrentContext())
            for (int i = 0; i < context->OpenPopupStack.Size; ++i)
                if (context->OpenPopupStack[i].PopupId == popupId) { ImGui::ClosePopupToLevel(i, false); break; }
    }
    void Draw(const EditorClient &, EditorViewFrame &);
    void Finish(bool accept) {
        if (editing < 0 || category >= categories.size()) return;
        auto &paths = categories[category].paths;
        if (size_t(editing) < paths.size()) {
            if (accept) { paths[size_t(editing)] = text; ++changes; }
            if (paths[size_t(editing)].empty()) { paths.erase(paths.begin() + editing); selected.clear(); ++changes; }
        }
        editing = -1;
    }
    EditorField Draft() const {
        EditorField result{"categories", EDITOR_RECORD};
        for (size_t i = 0; i < categories.size(); ++i) {
            EditorField record{std::to_string(i), EDITOR_RECORD};
            record.fields = {{"name", EDITOR_TEXT_VALUE, {}, categories[i].name}, {"paths", EDITOR_TEXT_LIST}};
            record.fields[1].texts = categories[i].paths; result.fields.push_back(std::move(record));
        }
        return result;
    }
    bool Read(const EditorClient &client) {
        if (!Available(client)) return false;
        const auto data = client.Query("paths.list");
        const auto *values = EditorClient::Find(data, "categories");
        if (data.status != EDITOR_OK || !values) return false;
        std::vector<Category> next;
        for (const auto &record : values->fields) {
            const auto *name = Field(record, "name"), *paths = Field(record, "paths");
            if (!name || !paths || name->type != EDITOR_TEXT_VALUE || paths->type != EDITOR_TEXT_LIST) return false;
            next.push_back({name->text, paths->texts});
        }
        categories = std::move(next); revision = EditorClient::Integer(data, "revision"); loaded = true; return true;
    }
    void FinishBrowse(const EditorResponse &result) {
        const auto requested = browse; browse = {};
        if (requested.changes != changes || requested.category >= categories.size()) return;
        auto &paths = categories[requested.category].paths;
        if (requested.row >= paths.size() || paths[requested.row] != requested.before) return;
        if (result.status == EDITOR_OK && !EditorClient::Text(result, "path").empty()) {
            paths[requested.row] = EditorClient::Text(result, "path"); ++changes;
        } else {
            if (requested.before.empty()) { paths.erase(paths.begin() + requested.row); selected.clear(); ++changes; }
            if (result.status != EDITOR_CANCELLED) message = result.message.empty() ? "A folder could not be chosen." : result.message;
        }
    }
    void Complete(const EditorClient &client, EditorViewFrame &frame) {
        if (applying) {
            const auto result = client.Completion(applying);
            if (result.status != EDITOR_QUEUED) {
                applying = 0;
                if (result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE) { frame.close = 1; return; }
                message = !result.message.empty() ? result.message : result.status == EDITOR_STALE ?
                    "Search paths changed. Close and reopen Path Manager before applying." : "Search paths could not be applied.";
            }
        }
        if (!browse.operation) return;
        const auto result = client.Completion(browse.operation);
        if (result.status == EDITOR_QUEUED) return;
        FinishBrowse(result);
    }
};
void Paths::Draw(const EditorClient &client, EditorViewFrame &frame) {
    Complete(client, frame);
    if (frame.close) return;
    if (!loaded) Read(client);
    if (!popup) { popupId = ImGui::GetID(frame.window.data); ImGui::OpenPopup(frame.window.data); popup = true; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
    ImGui::SetNextWindowSize(ImVec2(680, 420), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(frame.window.data, nullptr, ImGuiWindowFlags_NoDocking)) return;
    const bool usable = Available(client) && !applying;
    if (!loaded || categories.empty()) {
        ImGui::TextUnformatted(loaded ? "The path manager has no categories." : "The runtime is busy. Search paths will be shown when it is available.");
        if (ImGui::Button("Cancel", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { frame.close = 1; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup(); return;
    }
    category = std::min(category, categories.size() - 1);
    ImGui::BeginDisabled(applying != 0);
    ImGui::SetNextItemWidth(480);
    if (ImGui::BeginCombo("##category", categories[category].name.c_str())) {
        TypeAhead typeAhead;
        for (size_t i = 0; i < categories.size(); ++i)
            if (typeAhead.Selectable(categories[i].name.c_str(), i == category)) { Finish(false); category = i; selected.clear(); }
        ImGui::EndCombo();
    }
    auto &paths = categories[category].paths;
    const bool wasEditing = editing >= 0;
    const float buttons = 110, footer = ImGui::GetFrameHeightWithSpacing() * (message.empty() ? 1 : 2) + 8;
    const float height = ImGui::GetContentRegionAvail().y - footer;
    ImGui::BeginChild("paths", ImVec2(ImGui::GetContentRegionAvail().x - buttons - 8, height), ImGuiChildFlags_Borders);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    for (size_t i = 0; i < paths.size(); ++i) {
        ImGui::PushID(int(i));
        if (editing == int(i)) {
            const float width = ImGui::GetFrameHeight() + 8;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - width - ImGui::GetStyle().ItemSpacing.x);
            if (!ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
            const bool accept = ImGui::InputText("##edit", &text, ImGuiInputTextFlags_EnterReturnsTrue);
            const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape), deactivated = ImGui::IsItemDeactivated();
            ImGui::SameLine();
            if (ImGui::ButtonEx("...", ImVec2(width, 0), ImGuiButtonFlags_PressedOnClick) && !browse.operation) {
                paths[i] = text; ++changes;
                const auto result = client.Command("paths.choose-folder");
                browse = {result.operation, changes, category, i, paths[i]};
                editing = -1;
                if (result.status != EDITOR_QUEUED) FinishBrowse(result);
                ImGui::PopID(); break;
            } else if (accept || cancel || deactivated) {
                Finish(!cancel); ImGui::PopID(); break;
            }
        } else {
            const bool chosen = selected.count(i) != 0;
            const std::string label = paths[i].empty() ? " " : paths[i];
            if (ImGui::Selectable(label.c_str(), chosen, ImGuiSelectableFlags_AllowDoubleClick)) {
                const bool alone = chosen && selected.size() == 1 && !ImGui::GetIO().KeyCtrl;
                if (!ImGui::GetIO().KeyCtrl) selected.clear();
                if (chosen && ImGui::GetIO().KeyCtrl) selected.erase(i); else selected.insert(i);
                if (alone || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { editing = int(i); text = paths[i]; }
            }
        }
        ImGui::PopID();
    }
    ImGui::PopStyleColor();
    const bool focused = ImGui::IsWindowFocused(); ImGui::EndChild();
    const auto remove = [&] {
        for (auto it = selected.rbegin(); it != selected.rend(); ++it) if (*it < paths.size()) paths.erase(paths.begin() + *it);
        selected.clear(); editing = -1; ++changes;
    };
    if (!applying && focused && editing < 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) remove();
        if (ImGui::IsKeyPressed(ImGuiKey_F2) && selected.size() == 1) { editing = int(*selected.begin()); text = paths[size_t(editing)]; }
    }
    ImGui::SameLine(); ImGui::BeginGroup();
    if (ImGui::Button("Add Path", ImVec2(buttons, 0))) { Finish(true); paths.emplace_back(); editing = int(paths.size() - 1); text.clear(); selected = {size_t(editing)}; ++changes; }
    if (ImGui::Button("Delete Path", ImVec2(buttons, 0))) remove();
    if (ImGui::Button("Up", ImVec2(buttons, 0))) {
        Finish(true);
        if (selected.size() == 1 && *selected.begin() > 0 && *selected.begin() < paths.size()) {
            const auto single = *selected.begin(); std::swap(paths[single], paths[single - 1]); selected = {single - 1}; ++changes;
        }
    }
    if (ImGui::Button("Down", ImVec2(buttons, 0))) {
        Finish(true);
        if (selected.size() == 1 && *selected.begin() + 1 < paths.size()) {
            const auto single = *selected.begin(); std::swap(paths[single], paths[single + 1]); selected = {single + 1}; ++changes;
        }
    }
    ImGui::EndGroup(); ImGui::EndDisabled();
    if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 180 - ImGui::GetStyle().ItemSpacing.x);
    ImGui::BeginDisabled(!usable || browse.operation != 0);
    if (ImGui::Button("Ok", ImVec2(90, 0))) {
        Finish(true);
        const std::vector<EditorField> fields{Draft()};
        const auto values = BorrowEditorFields(fields);
        const auto result = client.Command("paths.apply", {IntegerArgument("revision", revision), values.values.front()});
        if (result.status == EDITOR_QUEUED) applying = result.operation;
        else if (result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE) { frame.close = 1; ImGui::CloseCurrentPopup(); }
        else message = result.message.empty() ? "Search paths could not be applied." : result.message;
    }
    ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90, 0)) || (!wasEditing && ImGui::IsKeyPressed(ImGuiKey_Escape))) { frame.close = 1; ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}
} // namespace
EditorViewDesc PathView() {
    EditorViewDesc view{};
    view.size = sizeof(view); view.imguiVersion = EDITOR_IMGUI_VERSION; view.id = EDITOR_TEXT("environment.paths");
    view.provider = EDITOR_TEXT("builtin.environment-views"); view.title = EDITOR_TEXT("Path Manager");
    view.area = EDITOR_APPLICATION; view.singleInstance = 1;
    view.create = [](void *, const EditorHost *, EditorView) -> void * { return new Paths; };
    view.destroy = [](void *data) { delete static_cast<Paths *>(data); };
    view.draw = [](void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) { static_cast<Paths *>(data)->Draw(EditorClient(host, id), *frame); };
    static const EditorParameter fields[] = {{EDITOR_TEXT("categories"), EDITOR_RECORD}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE}};
    view.fields = fields; view.fieldCount = uint32_t(std::size(fields));
    view.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &paths = *static_cast<Paths *>(data);
        const std::vector<EditorField> fields{paths.Draft(), {"revision", EDITOR_INTEGER_VALUE, {}, {}, paths.revision}, {"message", EDITOR_TEXT_VALUE, {}, paths.message}};
        const auto values = BorrowEditorFields(fields);
        return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
    };
    return view;
}
} // namespace environment
} // namespace vdev
