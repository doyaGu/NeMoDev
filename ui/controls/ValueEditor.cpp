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
                            const std::string &filter, ParameterValue *remembered, const PropertyBinding *capturedProperty,
                            const EditorValueEditors *capturedEditors, unsigned representation, ParameterValue *beforeInteraction, EditorDrawMembers drawMembers, void *membersData) {
    if (!state.workspace) return EDITOR_INPUT_INVALID;
    auto &app = state.workspace->Application();
    const auto *property = capturedProperty && !capturedProperty->id.empty() ? capturedProperty : app.Property(ParameterProperty(description.value.kind));
    if (!property) return EDITOR_INPUT_INVALID;
    const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, state.document->Reference(work->GetID())};
    auto projection = app.Query(property->query, {app.Scope(), &target, 1, EditorText(property->queryProvider)});
    if (projection.status != EDITOR_OK) return EDITOR_INPUT_INVALID;
    auto current = std::find_if(projection.fields.begin(), projection.fields.end(), [&](const auto &field) { return field.key == property->field; });
    if (current == projection.fields.end() || current->type != property->type) return EDITOR_INPUT_INVALID;
    auto original = remembered ? ParameterDraftValue(description.value, *remembered) : description.value;
    *current = ParameterValueField(original, property->field);
    if (original.kind == ParameterValueKind::Object && original.object != description.value.object)
        for (auto &field : projection.fields) if (field.key == "display") field.text = "<deleted object>";
    projection.fields.push_back({"representation", EDITOR_INTEGER_VALUE, {}, {}, representation});
    const auto fields = BorrowEditorFields(projection.fields);
    const auto value = *std::find_if(fields.values.begin(), fields.values.end(), [&](const auto &field) {
        return std::string(field.key.data, field.key.size) == property->field;
    });
    struct Draft { EditorState *state; CKParameter *work; ParameterValueDescription description; ParameterValue original; ParameterValue *remembered; ParameterValue *before; EditorDrawMembers members; void *membersData; bool drawingMembers = false, usedMembers = false; };
    Draft draft{&state, work, description, std::move(original), remembered, beforeInteraction, drawMembers, membersData};
    const auto update = [](void *data, const EditorValue *value, uint32_t count) {
        auto &draft = *static_cast<Draft *>(data);
        ParameterValue parsed;
        if (!value || count != 1 || !ParameterValueFromEditor(draft.description, *value, parsed)) return EDITOR_INVALID;
        if (draft.before && draft.before->kind == ParameterValueKind::None && !(draft.drawingMembers && draft.original.kind == ParameterValueKind::Structure)) *draft.before = draft.original;
        if (!WriteParameterDraft(*draft.state->document, draft.work, parsed)) return EDITOR_REFUSED;
        if (draft.remembered) *draft.remembered = parsed;
        return EDITOR_OK;
    };
    const EditorValue choices[] = {target,
        {EDITOR_TEXT("class"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(cid)},
        {EDITOR_TEXT("derived"), EDITOR_INTEGER_VALUE, {}, {}, derived ? 1u : 2u},
        {EDITOR_TEXT("filter"), EDITOR_TEXT_VALUE, {}, EditorText(filter)}};
    const auto draw = [](void *data, const EditorHost *host, const EditorValueInput *input) {
        auto &draft = *static_cast<Draft *>(data);
        draft.drawingMembers = draft.usedMembers = true;
        const auto action = draft.members(draft.membersData, host, input);
        draft.drawingMembers = false;
        return action;
    };
    const EditorValueInput input{sizeof(input), 0, EditorLiteral(label), value, fields.data(), uint32_t(fields.size()), 0,
        {&draft, update}, EDITOR_VALUE_DIALOG, width, height, EditorText(property->choices),
        {app.Scope(), choices, 4, EditorText(property->choicesProvider)}, drawMembers ? draw : nullptr, &draft};
    ImGui::SetNextItemWidth(width);
    const auto selected = capturedEditors ? capturedEditors->find(property->editor) : EditorValueEditors::const_iterator{};
    auto action = capturedEditors && selected != capturedEditors->end() ? app.DrawValue(*selected->second, input, capturedEditors) : app.DrawValue(property->editor, input);
    if (beforeInteraction && beforeInteraction->kind == ParameterValueKind::None && (action & EDITOR_INPUT_ACTIVE) &&
        !(draft.usedMembers && draft.original.kind == ParameterValueKind::Structure))
        *beforeInteraction = draft.original;
    if (beforeInteraction && beforeInteraction->kind != ParameterValueKind::None) {
        if (action & EDITOR_INPUT_CANCEL) {
            // Escape belongs to this interaction even when ImGui already ended
            // the active widget. The containing dialog must stay open.
            action |= EDITOR_INPUT_ACTIVE;
            if (!WriteParameterDraft(*state.document, work, *beforeInteraction)) action |= EDITOR_INPUT_INVALID | EDITOR_INPUT_CHANGED;
            else if (remembered) *remembered = *beforeInteraction;
            *beforeInteraction = {};
        } else if (!(action & EDITOR_INPUT_INVALID) &&
                   ((action & (EDITOR_INPUT_ACCEPT | EDITOR_INPUT_DEACTIVATED)) ||
                    !(action & (EDITOR_INPUT_ACTIVE | EDITOR_INPUT_CHANGED)))) *beforeInteraction = {};
    }
    return action;
}
} // namespace
uint32_t ParameterDraftEditor(EditorState &state, CKParameter *work, const char *label, float width, float height,
                              CK_CLASSID cid, bool derived, const std::string &filter, ParameterValue *remembered,
                              const PropertyBinding *property, const EditorValueEditors *editors, unsigned representation, ParameterValue *beforeInteraction, EditorDrawMembers drawMembers, void *membersData) {
    ParameterValueDescription description;
    if (!state.document || !DescribeParameterValue(*state.document, work, description)) return EDITOR_INPUT_INVALID;
    return DrawParameterDraft(state, work, description, label, width, height, cid, derived, filter, remembered, property, editors, representation, beforeInteraction, drawMembers, membersData);
}

} // namespace vdev
