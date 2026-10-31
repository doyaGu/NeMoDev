#include "features/environment/EnvironmentViewData.h"
#include "workspace/EditorPopup.h"
#include <algorithm>
#include <array>
#include <limits>

namespace vdev {
namespace environment {
namespace {
struct LinkOptions {
    EditorPopup popup;
    DocumentToken document{};
    std::array<EditorField, 3> values{{{"delay", EDITOR_INTEGER_VALUE}, {"prevent", EDITOR_INTEGER_VALUE}, {"iterations", EDITOR_INTEGER_VALUE}}};
    std::array<bool, 3> invalid{};
    bool loaded = false;
    uint64_t revision = 0;
    EditorOperation operation = 0;
    std::string message;
    void Draw(const EditorClient &client, EditorViewFrame &frame) {
        if (document != client.Scope().document) { document = client.Scope().document; loaded = false; operation = 0; invalid = {}; message.clear(); }
        if (!loaded) {
            const auto result = client.Query("environment.link-options");
            if (result.status == EDITOR_OK) {
                bool valid = true;
                for (auto &field : values) {
                    const auto *source = EditorClient::Find(result, field.key.c_str());
                    if (!source || source->type != EDITOR_INTEGER_VALUE) { valid = false; break; }
                    field = *source;
                }
                loaded = valid; revision = EditorClient::Integer(result, "revision");
            }
        }
        if (operation) {
            const auto result = client.Completion(operation);
            if (result.status != EDITOR_QUEUED) {
                operation = 0;
                if (result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE) { frame.close = 1; return; }
                message = result.message.empty() ? "The properties changed or could not be saved. Close and reopen this dialog." : result.message;
            }
        }
        popup.Open(frame);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
        if (!ImGui::BeginPopupModal(frame.window.data, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        const char *labels[] = {"Default Link Delay", "Prevent Infinite Loop", "Max Behavioral Iterations"};
        bool editing = false;
        ImGui::BeginDisabled(!loaded || operation != 0);
        if (ImGui::BeginTable("parameters", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, 220);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 180);
            for (size_t i = 0; i < values.size(); ++i) {
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(labels[i]); ImGui::TableSetColumnIndex(1); ImGui::PushID(int(i));
                const std::vector<EditorField> owned{values[i]}; const auto borrowed = BorrowEditorFields(owned);
                const std::vector<EditorField> metadata{{"display", EDITOR_TEXT_VALUE, {}, std::to_string(int64_t(values[i].integer))}};
                const auto fields = BorrowEditorFields(metadata);
                const EditorSink update{&values[i], [](void *data, const EditorValue *value, uint32_t count) {
                    if (!value || count != 1 || value->type != EDITOR_INTEGER_VALUE) return EDITOR_INVALID;
                    static_cast<EditorField *>(data)->integer = value->integer; return EDITOR_OK;
                }};
                const EditorValueInput input{sizeof(input), 0, EDITOR_TEXT("##value"), borrowed.values.front(), fields.data(), uint32_t(fields.size()),
                    0, update, EDITOR_VALUE_DIALOG, 180, ImGui::GetFrameHeight()};
                const auto action = client.Host()->drawValue(client.Host()->data, EditorLiteral(i == 1 ? "value.boolean" : "value.integer"), &input);
                editing |= (action & (EDITOR_INPUT_ACTIVE | EDITOR_INPUT_CHANGED | EDITOR_INPUT_ACCEPT | EDITOR_INPUT_DEACTIVATED)) != 0;
                if (action & EDITOR_INPUT_INVALID) invalid[i] = true;
                else if (action & EDITOR_INPUT_CHANGED) invalid[i] = false;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        const bool valid = std::none_of(invalid.begin(), invalid.end(), [](bool value) { return value; });
        const bool canApply = loaded && !operation && valid && Available(client);
        ImGui::BeginDisabled(!canApply);
        const bool confirm = ImGui::Button("OK", ImVec2(90, 0));
        const bool enter = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsAnyItemActive() && !editing;
        if (canApply && (confirm || enter)) {
            const auto delay = std::clamp(int64_t(values[0].integer), int64_t(0), int64_t(32764));
            const auto iterations = std::clamp(int64_t(values[2].integer), int64_t(1), int64_t(std::numeric_limits<int>::max()));
            const auto result = client.Command("environment.set-link-options", {IntegerArgument("revision", revision), IntegerArgument("delay", uint64_t(delay)),
                IntegerArgument("prevent", values[1].integer != 0), IntegerArgument("iterations", uint64_t(iterations))});
            if (result.status == EDITOR_QUEUED) operation = result.operation;
            else if (result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE) frame.close = 1;
            else message = result.message.empty() ? "The properties changed or could not be saved." : result.message;
        }
        ImGui::EndDisabled(); ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) frame.close = 1;
        if (!loaded) ImGui::TextUnformatted("The runtime is busy. Properties will be shown when it is available.");
        if (!valid) ImGui::TextUnformatted("Enter a valid integer before applying.");
        if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
        ImGui::EndPopup();
    }
};
}
EditorViewDesc LinkOptionsView() {
    EditorViewDesc view{}; view.size = sizeof(view); view.imguiVersion = EDITOR_IMGUI_VERSION;
    view.id = EDITOR_TEXT("environment.link-options"); view.provider = EDITOR_TEXT("builtin.environment-views");
    view.title = EDITOR_TEXT("Behavior Link Properties"); view.area = EDITOR_APPLICATION; view.singleInstance = 1;
    view.create = [](void *, const EditorHost *, EditorView) -> void * { return new LinkOptions; };
    view.destroy = [](void *data) { delete static_cast<LinkOptions *>(data); };
    view.draw = [](void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) { static_cast<LinkOptions *>(data)->Draw(EditorClient(host, id), *frame); };
    static const EditorParameter fields[] = {{EDITOR_TEXT("value"), EDITOR_RECORD}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}};
    view.fields = fields; view.fieldCount = 2;
    view.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &view = *static_cast<LinkOptions *>(data); EditorField record{"value", EDITOR_RECORD}; record.fields.assign(view.values.begin(), view.values.end());
        const std::vector<EditorField> fields{record, {"revision", EDITOR_INTEGER_VALUE, {}, {}, view.revision}};
        const auto values = BorrowEditorFields(fields); return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
    };
    return view;
}
} // namespace environment
} // namespace vdev
