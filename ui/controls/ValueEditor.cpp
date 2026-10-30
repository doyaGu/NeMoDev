#include "ui/controls/ValueEditor.h"
#include "ui/controls/TypeAhead.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <vector>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include "core/graph/RuntimeGraph.h"
#include "core/runtime/TextEncoding.h"
#include "core/graph/ValueCommands.h"
#include "core/graph/ParameterValues.h"
#include "features/values/ParameterCommands.h"
#include "workspace/EditorWorkspace.h"

namespace vdev {

std::string ParameterValueText(CKParameter *parameter) {
    return AnsiToUtf8(ParameterText(parameter));
}

namespace {
uint32_t DrawParameterDraft(EditorState &state, CKParameter *work, const ParameterValueDescription &description,
                            const char *label, float width, float height, CK_CLASSID cid, bool derived,
                            const std::string &filter, ObjectRef *remembered, const PropertyBinding *capturedProperty,
                            const EditorValueEditorDesc *capturedEditor, unsigned representation) {
    if (!state.workspace) return EDITOR_INPUT_INVALID;
    auto &app = state.workspace->Application();
    const auto *property = capturedProperty && !capturedProperty->id.empty() ? capturedProperty : app.Property(ParameterProperty(description.value.kind));
    if (!property) return EDITOR_INPUT_INVALID;
    const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, state.document->Reference(work->GetID())};
    auto projection = app.Query(property->query, {app.Scope(), &target, 1, EditorText(property->queryProvider)});
    if (projection.status != EDITOR_OK) return EDITOR_INPUT_INVALID;
    auto current = std::find_if(projection.fields.begin(), projection.fields.end(), [&](const auto &field) { return field.key == property->field; });
    if (current == projection.fields.end() || current->type != property->type) return EDITOR_INPUT_INVALID;
    if (remembered && current->type == EDITOR_OBJECT && remembered->object == current->object.object && *remembered != current->object) {
        current->object = *remembered;
        for (auto &field : projection.fields) if (field.key == "display") field.text = "<deleted object>";
    }
    projection.fields.push_back({"representation", EDITOR_INTEGER_VALUE, {}, {}, representation});
    const auto fields = BorrowEditorFields(projection.fields);
    const auto value = *std::find_if(fields.values.begin(), fields.values.end(), [&](const auto &field) {
        return std::string(field.key.data, field.key.size) == property->field;
    });
    struct Draft { EditorState *state; CKParameter *work; ParameterValueKind kind; ObjectRef *remembered; } draft{&state, work, description.value.kind, remembered};
    const auto update = [](void *data, const EditorValue *value, uint32_t count) {
        auto &draft = *static_cast<Draft *>(data);
        ParameterValue parsed;
        if (!value || count != 1 || !ParameterValueFromEditor(draft.kind, *value, parsed)) return EDITOR_INVALID;
        if (!WriteParameterDraft(*draft.state->document, draft.work, parsed)) return EDITOR_REFUSED;
        if (draft.remembered && parsed.kind == ParameterValueKind::Object) *draft.remembered = parsed.object;
        return EDITOR_OK;
    };
    const EditorValue choices[] = {target,
        {EDITOR_TEXT("class"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(cid)},
        {EDITOR_TEXT("derived"), EDITOR_INTEGER_VALUE, {}, {}, derived ? 1u : 2u},
        {EDITOR_TEXT("filter"), EDITOR_TEXT_VALUE, {}, EditorText(filter)}};
    const EditorValueInput input{sizeof(input), 0, EditorLiteral(label), value, fields.data(), uint32_t(fields.size()), 0,
        {&draft, update}, EDITOR_VALUE_DIALOG, width, height, EditorText(property->choices),
        {app.Scope(), choices, 4, EditorText(property->choicesProvider)}};
    ImGui::SetNextItemWidth(width);
    auto action = capturedEditor && capturedEditor->draw ? app.DrawValue(*capturedEditor, input) : app.DrawValue(property->editor, input);
    return action;
}
} // namespace
uint32_t ParameterDraftEditor(EditorState &state, CKParameter *work, const char *label, float width, float height,
                              CK_CLASSID cid, bool derived, const std::string &filter, ObjectRef *remembered,
                              const PropertyBinding *property, const EditorValueEditorDesc *editor, unsigned representation) {
    ParameterValueDescription description;
    if (!state.document || !DescribeParameterValue(*state.document, work, description)) return EDITOR_INPUT_INVALID;
    return DrawParameterDraft(state, work, description, label, width, height, cid, derived, filter, remembered, property, editor, representation);
}
void CancelParameterValueEdit(EditorState &state, ValueEditState &edit) {
    if (state.workspace) {
        if (edit.draft) state.workspace->Application().FinishEdit(edit.draft, false);
        state.workspace->CloseOwner(edit.owner);
    }
    edit.draft = 0; edit.editing = 0;
}
void ParameterValueEditor(EditorState &state, ValueEditState &edit, CK_ID script, CKParameter *parameter, bool editable) {
    if (!parameter) { ImGui::TextDisabled("(no value)"); return; }
    if (!editable) { ImGui::TextWrapped("%s", ParameterValueText(parameter).c_str()); return; }
    const auto id = parameter->GetID();
    ImGui::PushID(int(id));
    ImGui::SetNextItemWidth(-FLT_MIN);
    ParameterValueDescription description;
    if (state.workspace && DescribeParameterValue(*state.document, parameter, description)) {
        if (!edit.owner) edit.owner = state.workspace->Application().OpenScope();
        state.workspace->Application().EnterDraw();
        state.workspace->Application().DrawProperty(ParameterProperty(description.value.kind),
            state.workspace->Application().Scope(edit.owner), state.document->Reference(id), edit.draft, EditorLiteral("##value"));
        state.workspace->Application().LeaveDraw();
    } else {
        // Specialized types keep their existing text/flags presentation.
        auto *manager = state.document->Context()->GetParameterManager();
        auto *type = manager->GetParameterTypeDescription(parameter->GetType());
        auto *flags = type && (type->dwFlags & CKPARAMETERTYPE_FLAGS) ? manager->GetFlagsDescByType(parameter->GetType()) : nullptr;
        if (flags && parameter->GetDataSize() == sizeof(int)) {
            int integer = 0; parameter->GetValue(&integer, FALSE);
            for (int i = 0; i < flags->GetNumFlags(); ++i) {
                const int bit = flags->GetFlagValue(i);
                bool on = bit && (integer & bit) == bit;
                const auto *text = flags->GetFlagDescription(i);
                if (ImGui::Checkbox(text ? AnsiToUtf8(text).c_str() : "?", &on)) {
                    integer = on ? integer | bit : integer & ~bit;
                    std::vector<unsigned char> bytes(sizeof(integer)); std::memcpy(bytes.data(), &integer, sizeof(integer));
                    const auto result = SetParameterBytes(*state.document, script, id, bytes);
                    if (!result.ok && !result.Unchanged()) state.Log(LogLevel::Warning, "Set value: " + AnsiToUtf8(result.message));
                }
            }
        } else {
            if (edit.editing != id) edit.text = ParameterValueText(parameter);
            const bool accept = ImGui::InputText("##value", &edit.text, ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::IsItemActivated()) edit.editing = id;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) edit.editing = 0;
            else if (accept || ImGui::IsItemDeactivatedAfterEdit()) {
                const auto result = SetParameterText(*state.document, script, id, Utf8ToAnsi(edit.text));
                if (!result.ok && !result.Unchanged()) state.Log(LogLevel::Warning, AnsiToUtf8(result.message));
                edit.editing = 0;
            }
        }
    }
    ImGui::PopID();
}

bool DependenciesEditor(CKDependencies &dependencies, CK_DEPENDENCIES_OPMODE mode, float height) {
    bool changed = false;
    static const char *const modes[] = {"Custom", "None", "Full"};
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::RadioButton(modes[i], static_cast<int>(dependencies.m_Flags) == i)) {
            dependencies.m_Flags = static_cast<CK_DEPENDENCIES_FLAGS>(i);
            changed = true;
        }
    }
    const int classes = CKGetClassCount();
    if (dependencies.Size() < classes) {
        const int old = dependencies.Size();
        dependencies.Resize(classes);
        for (int i = old; i < classes; ++i) dependencies[i] = 0;
    }
    ImGui::BeginDisabled(dependencies.m_Flags != CK_DEPENDENCIES_CUSTOM);
    if (ImGui::BeginTable("##dependencies", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, height))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Class", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Options", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (int cid = 0; cid < classes; ++cid) {
            CKClassDesc *desc = CKGetClassDesc(cid);
            if (!desc || !desc->DependsFct || !desc->DependsCountFct) continue;
            const int count = desc->DependsCountFct(mode);
            if (count <= 0) continue;
            ImGui::PushID(static_cast<int>(cid));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const char *name = CKClassIDToString(cid);
            ImGui::TextUnformatted(name ? name : "?");
            ImGui::TableSetColumnIndex(1);
            CKDWORD &bits = dependencies[cid];
            for (int k = 0; k < count && k < 32; ++k) {
                const char *option = desc->DependsFct(k, mode);
                if (!option) continue;
                const float width = ImGui::CalcTextSize(option).x + ImGui::GetFrameHeight() + 12.0f;
                if (k && ImGui::GetContentRegionAvail().x > width) ImGui::SameLine();
                bool on = (bits & (1u << k)) != 0;
                ImGui::PushID(k);
                if (ImGui::Checkbox(option, &on)) {
                    bits = on ? (bits | (1u << k)) : (bits & ~(1u << k));
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    return changed;
}

} // namespace vdev
