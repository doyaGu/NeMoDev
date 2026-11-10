#include "ui/controls/ValueEditor.h"
#include "ui/controls/TypeAhead.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <vector>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include "core/graph/ParameterValueData.h"
#include "features/values/ParameterCommands.h"
#include "workspace/EditorWorkspace.h"

namespace vdev {

namespace {
uint32_t DrawParameterDraft(ParameterCopies &copies, ObjectRef work, const ParameterValue &nativeValue,
                            const char *label, float width, float height, int32_t cid, bool derived,
                            const std::string &filter, ParameterValue *remembered, const PropertyBinding *capturedProperty,
                            const EditorValueEditors *capturedEditors, unsigned representation, ParameterValue *beforeInteraction, EditorDrawMembers drawMembers, void *membersData) {
    auto &app = copies.Editor();
    const auto *property = capturedProperty && !capturedProperty->id.empty() ? capturedProperty : app.Property(ParameterProperty(nativeValue.kind));
    if (!property) return EDITOR_INPUT_INVALID;
    const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, work};
    auto projection = app.Query(property->query, {app.Scope(), &target, 1, EditorText(property->queryProvider)});
    if (projection.status != EDITOR_OK) return EDITOR_INPUT_INVALID;
    auto current = std::find_if(projection.fields.begin(), projection.fields.end(), [&](const auto &field) { return field.key == property->field; });
    if (current == projection.fields.end() || current->type != property->type) return EDITOR_INPUT_INVALID;
    auto original = remembered ? ParameterDraftValue(nativeValue, *remembered) : nativeValue;
    *current = ParameterValueField(original, property->field);
    if (original.kind == ParameterValueKind::Object && original.object != nativeValue.object)
        for (auto &field : projection.fields) if (field.key == "display") field.text = "<deleted object>";
    projection.fields.push_back({"representation", EDITOR_INTEGER_VALUE, {}, {}, representation});
    const auto fields = BorrowEditorFields(projection.fields);
    const auto value = *std::find_if(fields.values.begin(), fields.values.end(), [&](const auto &field) {
        return std::string(field.key.data, field.key.size) == property->field;
    });
    struct Draft { ParameterCopies *copies; ObjectRef work; ParameterValue original; ParameterValue *remembered; ParameterValue *before; EditorDrawMembers members; void *membersData; bool drawingMembers = false, usedMembers = false; };
    Draft draft{&copies, work, std::move(original), remembered, beforeInteraction, drawMembers, membersData};
    const auto update = [](void *data, const EditorValue *value, uint32_t count) {
        auto &draft = *static_cast<Draft *>(data);
        ParameterValue parsed;
        if (!value || count != 1 || !draft.copies->Parse(draft.work, *value, parsed)) return EDITOR_INVALID;
        if (draft.before && draft.before->kind == ParameterValueKind::None && !(draft.drawingMembers && draft.original.kind == ParameterValueKind::Structure)) *draft.before = draft.original;
        if (!draft.copies->Restore(draft.work, parsed)) return EDITOR_REFUSED;
        if (draft.remembered) *draft.remembered = parsed;
        return EDITOR_OK;
    };
    const EditorValue choices[] = {target,
        {EDITOR_TEXT("class"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(cid)},
        {EDITOR_TEXT("derived"), EDITOR_INTEGER_VALUE, {}, {}, derived ? 1u : 2u},
        {EDITOR_TEXT("filter"), EDITOR_TEXT_VALUE, {}, EditorText(filter)}};
    const EditorDrawMembers draw = [](void *data, const EditorHost *host, const EditorValueInput *input) {
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
            if (!copies.Restore(work, *beforeInteraction)) action |= EDITOR_INPUT_INVALID | EDITOR_INPUT_CHANGED;
            else if (remembered) *remembered = *beforeInteraction;
            *beforeInteraction = {};
        } else if (!(action & EDITOR_INPUT_INVALID) &&
                   ((action & (EDITOR_INPUT_ACCEPT | EDITOR_INPUT_DEACTIVATED)) ||
                    !(action & (EDITOR_INPUT_ACTIVE | EDITOR_INPUT_CHANGED)))) *beforeInteraction = {};
    }
    return action;
}
} // namespace
uint32_t ParameterDraftEditor(ParameterCopies &copies, ObjectRef work, const char *label, float width, float height,
                              int32_t cid, bool derived, const std::string &filter, ParameterValue *remembered,
                              const PropertyBinding *property, const EditorValueEditors *editors, unsigned representation, ParameterValue *beforeInteraction, EditorDrawMembers drawMembers, void *membersData) {
    ParameterValue value;
    if (!copies.Read(work, value)) return EDITOR_INPUT_INVALID;
    return DrawParameterDraft(copies, work, value, label, width, height, cid, derived, filter, remembered, property, editors, representation, beforeInteraction, drawMembers, membersData);
}

} // namespace vdev
