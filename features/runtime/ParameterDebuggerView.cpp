#include "features/runtime/RuntimeViewData.h"
#include "editor/ImGuiDrawing.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <iterator>

namespace vdev {
namespace runtime {
namespace {
struct Watch {
    ObjectRef object{};
    EditorResponse value;
    EditorEdit edit = 0;
    std::string text;
    uint64_t textType = 0;
    bool typing = false;
};
struct ParameterDebugger {
    DocumentToken document{};
    std::vector<Watch> watches;
    ObjectRef selected{};
    std::string message;
    void Synchronize(const EditorClient &client) {
        if (document == client.Scope().document) return;
        document = client.Scope().document;
        watches.clear(); selected = {}; message.clear();
    }
    EditorStatus Navigate(const EditorClient &, const EditorRequest &);
    EditorStatus Inspect(const EditorClient &, EditorSink);
    void Draw(const EditorClient &, EditorViewFrame &);
    void Paste(const EditorClient &);
};
EditorStatus ParameterDebugger::Navigate(const EditorClient &client, const EditorRequest &request) {
    Synchronize(client);
    std::vector<Watch> added;
    ObjectRef remove{};
    bool clear = false, duplicate = false, appending = false;
    for (uint32_t i = 0; i < request.count; ++i) {
        const auto &value = request.values[i];
        const std::string key(value.key.data, value.key.size);
        if (key == "clear") {
            if (value.integer > 1) return EDITOR_INVALID;
            clear = value.integer != 0;
        } else if (key == "remove") remove = value.object;
        else if (key == "objects") {
            appending = value.objects.count != 0;
            for (uint32_t j = 0; j < value.objects.count; ++j) {
                const auto object = value.objects.data[j];
                const auto present = [&](const Watch &watch) { return watch.object == object; };
                if (std::any_of(watches.begin(), watches.end(), present) || std::any_of(added.begin(), added.end(), present)) {
                    duplicate = true; continue;
                }
                auto row = client.Query("parameter.watch", {ObjectArgument("target", object)});
                if (row.status == EDITOR_INVALID) continue; // Inputs cannot be watched.
                if (row.status != EDITOR_OK) return row.status;
                added.push_back({object, std::move(row)});
            }
        }
    }
    if ((clear && remove) || ((clear || remove) && appending)) return EDITOR_INVALID;
    bool changed = !added.empty();
    for (auto it = watches.begin(); it != watches.end();)
        if (clear || it->object == remove) {
            client.CancelEdit(it->edit);
            if (selected == it->object) selected = {};
            it = watches.erase(it); changed = true;
        } else ++it;
    watches.insert(watches.end(), std::make_move_iterator(added.begin()), std::make_move_iterator(added.end()));
    message = duplicate ? "Parameter already present" : "";
    return changed ? EDITOR_OK : EDITOR_NO_CHANGE;
}
EditorStatus ParameterDebugger::Inspect(const EditorClient &client, EditorSink sink) {
    Synchronize(client);
    std::vector<EditorField> fields{{"objects", EDITOR_OBJECT_LIST}, {"values", EDITOR_TEXT_LIST}, {"selected", EDITOR_OBJECT}};
    for (const auto &watch : watches) {
        // A docked tab may not draw for many frames. Its projection must still
        // reject dead objects; inspecting never submits or cancels an edit.
        const auto current = client.Query("parameter.watch", {ObjectArgument("target", watch.object)});
        if (current.status == EDITOR_STALE || current.status == EDITOR_INVALID) continue;
        fields[0].objects.push_back(watch.object);
        fields[1].texts.push_back(EditorClient::Text(current.status == EDITOR_OK ? current : watch.value, "display"));
        if (selected == watch.object) fields[2].object = selected;
    }
    const auto values = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
}
void ParameterDebugger::Paste(const EditorClient &client) {
    std::vector<ObjectRef> objects;
    uint64_t first = 0;
    for (;;) {
        const auto page = client.Query("parameter.clipboard", {IntegerArgument("first", first)});
        const auto *values = EditorClient::Find(page, "objects");
        if (page.status != EDITOR_OK || !values) return;
        objects.insert(objects.end(), values->objects.begin(), values->objects.end());
        const auto next = Integer(page, "next"), total = Integer(page, "total");
        if (next >= total) break;
        if (next <= first) return;
        first = next;
    }
    if (!objects.empty()) client.Command("runtime.watch-parameters", {ObjectsArgument("objects", objects), IntegerArgument("view", client.View())});
    else message = "Copy local parameters or outputs in the Schematic first (Ctrl+C)";
}
void ParameterDebugger::Draw(const EditorClient &client, EditorViewFrame &frame) {
    Synchronize(client);
    // Document queries stop at the host while a file worker owns native state.
    // Keep the last completed row, and do not call an editor with a stale value.
    EditorImGuiWindow window(frame);
    if (!window) return;
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (focused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) Paste(client);
    ObjectRef remove{};
    bool clear = false;
    if (ImGui::BeginTable("watches", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
            ImVec2(0, message.empty() ? 0 : -ImGui::GetFrameHeightWithSpacing()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 380);
        ImGui::TableSetupColumn("Owner Object", ImGuiTableColumnFlags_WidthFixed, 380);
        ImGui::TableSetupColumn("Owner Script", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (auto it = watches.begin(); it != watches.end();) {
            auto &watch = *it;
            const auto row = client.Query("parameter.watch", {ObjectArgument("target", watch.object)});
            if (row.status == EDITOR_STALE || row.status == EDITOR_INVALID) {
                client.CancelEdit(watch.edit);
                if (selected == watch.object) selected = {};
                it = watches.erase(it); continue;
            }
            const bool usable = row.status == EDITOR_OK;
            if (!usable || Integer(row, "type") != Integer(watch.value, "type") ||
                EditorClient::Text(row, "property") != EditorClient::Text(watch.value, "property")) {
                client.CancelEdit(watch.edit);
                watch.typing = false;
            }
            if (usable) watch.value = row;
            ImGui::PushID(int(watch.object.object));
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(EditorClient::Text(watch.value, "name").c_str(), selected == watch.object,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                selected = watch.object;
            }
            if (ImGui::BeginPopupContextItem("watch")) {
                selected = watch.object;
                if (ImGui::MenuItem("Go to source", nullptr, false, usable))
                    client.Command("script.show-parameter", {ObjectArgument("target", watch.object)});
                if (ImGui::MenuItem("Delete")) remove = watch.object;
                ImGui::Separator();
                if (ImGui::MenuItem("Paste", "Ctrl+V", false, usable)) Paste(client);
                if (ImGui::MenuItem("Clear")) clear = true;
                ImGui::EndPopup();
            }
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
            const auto property = EditorClient::Text(watch.value, "property");
            if (!usable || !Integer(watch.value, "editable")) ImGui::TextWrapped("%s", EditorClient::Text(watch.value, "display").c_str());
            else if (!property.empty()) client.DrawProperty(property, watch.object, watch.edit, "##value");
            else {
                if (!watch.typing) watch.text = EditorClient::Text(watch.value, "display");
                const bool accept = ImGui::InputText("##value", &watch.text, ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::IsItemActivated()) { watch.typing = true; watch.textType = Integer(watch.value, "type"); }
                if (watch.typing && ImGui::IsKeyPressed(ImGuiKey_Escape)) watch.typing = false;
                else if (watch.typing && (accept || ImGui::IsItemDeactivatedAfterEdit())) {
                    client.Command("parameter.set-text", {ObjectArgument("target", watch.object),
                        IntegerArgument("type", watch.textType), TextArgument("text", watch.text)});
                    watch.typing = false;
                }
            }
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(EditorClient::Text(watch.value, "owner-name").c_str());
            ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(EditorClient::Text(watch.value, "script-name").c_str());
            ImGui::PopID(); ++it;
        }
        ImGui::EndTable();
    }
    if (ImGui::BeginPopupContextWindow("watchspace", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Paste", "Ctrl+V")) Paste(client);
        if (ImGui::MenuItem("Clear", nullptr, false, !watches.empty())) clear = true;
        ImGui::EndPopup();
    }
    if (focused && selected && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::IsAnyItemActive()) remove = selected;
    if (clear) client.Command("runtime.clear-watches", {IntegerArgument("view", client.View())});
    else if (remove) client.Command("runtime.unwatch-parameter", {ObjectArgument("remove", remove), IntegerArgument("view", client.View())});
    if (!message.empty()) ImGui::TextDisabled("%s", message.c_str());
}
} // namespace
EditorViewDesc ParameterDebuggerView() {
    EditorViewDesc view{};
    view.size = sizeof(view); view.imguiVersion = EDITOR_IMGUI_VERSION;
    view.id = EDITOR_TEXT("runtime.parameters"); view.provider = EDITOR_TEXT("builtin.runtime-views");
    view.title = EDITOR_TEXT("Parameter Debugger"); view.area = EDITOR_APPLICATION;
    view.create = [](void *, const EditorHost *, EditorView) -> void * { return new ParameterDebugger; };
    view.destroy = [](void *data) { delete static_cast<ParameterDebugger *>(data); };
    view.draw = [](void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) {
        static_cast<ParameterDebugger *>(data)->Draw(EditorClient(host, id), *frame);
    };
    static const EditorParameter args[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, 1},
        {EDITOR_TEXT("remove"), EDITOR_OBJECT, 1}, {EDITOR_TEXT("clear"), EDITOR_INTEGER_VALUE, 1}};
    view.parameters = args; view.count = uint32_t(std::size(args));
    view.navigate = [](void *data, const EditorHost *host, const EditorRequest *request) {
        return static_cast<ParameterDebugger *>(data)->Navigate(EditorClient(host, request->scope.view), *request);
    };
    static const EditorParameter fields[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("values"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("selected"), EDITOR_OBJECT}};
    view.fields = fields; view.fieldCount = uint32_t(std::size(fields));
    view.inspect = [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
        return static_cast<ParameterDebugger *>(data)->Inspect(EditorClient(host, request->scope.view), sink);
    };
    return view;
}
} // namespace runtime
} // namespace vdev
