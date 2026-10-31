#include "features/arrays/ArrayTable.h"
#include "editor/ImGuiDrawing.h"
#include "imgui_internal.h"
#include <algorithm>
#include <iterator>

namespace vdev {
namespace {
const EditorField *Field(const std::vector<EditorField> &fields, const char *key) {
    for (const auto &f : fields)
        if (f.key == key)
            return &f;
    return nullptr;
}
int Number(const std::vector<EditorField> &fields, const char *key) {
    const auto *v = Field(fields, key);
    return v ? int(int64_t(v->integer)) : 0;
}
std::string Text(const std::vector<EditorField> &fields, const char *key) {
    const auto *v = Field(fields, key);
    return v ? v->text : std::string();
}
std::string Text(EditorString s) { return {s.data ? s.data : "", s.size}; }
void CancelInput(unsigned id) {
    if (!id || !ImGui::GetCurrentContext())
        return;
    auto &g = *ImGui::GetCurrentContext();
    if (g.ActiveId == id)
        ImGui::ClearActiveID();
    // InputText keeps one final write after deactivation. A cancelled draft
    // must also drop that write before the same cell is drawn again.
    if (g.InputTextDeactivatedState.ID == id)
        g.InputTextDeactivatedState.ID = 0;
}
uint32_t DrawColumn(void *, const EditorHost *host, const EditorValueInput *input) {
    std::vector<EditorField> owned;
    if (CopyEditorFields(owned, &input->value, 1) != EDITOR_OK || owned.front().fields.size() != 3)
        return EDITOR_INPUT_INVALID;
    const auto &record = owned.front().fields;
    const char *const keys[] = {"name", "type", "parameter"};
    const EditorValueType types[] = {EDITOR_TEXT_VALUE, EDITOR_INTEGER_VALUE, EDITOR_INTEGER_VALUE};
    size_t indices[3]{};
    for (unsigned i = 0; i < 3; ++i) {
        const auto field = std::find_if(record.begin(), record.end(),
                                        [&](const auto &v) { return v.key == keys[i] && v.type == types[i]; });
        if (field == record.end())
            return EDITOR_INPUT_INVALID;
        indices[i] = size_t(field - record.begin());
    }
    uint32_t actions = 0;
    const char *const names[] = {"Name", "Type", "Parameter"};
    for (size_t i = 0; i < 3; ++i) {
        if (i == 2 && owned.front().fields[indices[1]].integer != 5)
            continue;
        auto &value = owned.front().fields[indices[i]];
        std::vector<EditorField> metadata;
        if (i == 1) {
            metadata = {{"labels", EDITOR_TEXT_LIST}, {"values", EDITOR_INTEGER_LIST}};
            metadata[0].texts = {"Integer", "Float", "String", "Object", "Parameter"};
            metadata[1].integers = {1, 2, 3, 4, 5};
        }
        if (i == 2) {
            std::vector<EditorField> copied;
            if (CopyEditorFields(copied, input->fields, input->count) != EDITOR_OK)
                return EDITOR_INPUT_INVALID;
            const auto *labels = Field(copied, "parameter-labels"), *values = Field(copied, "parameter-types");
            if (!labels || !values)
                return EDITOR_INPUT_INVALID;
            metadata = {*labels, *values};
            metadata[0].key = "labels";
            metadata[1].key = "values";
        }
        struct Update {
            std::vector<EditorField> *owned;
            size_t index;
            const EditorValueInput *input;
        } update{&owned, indices[i], input};
        auto write = [](void *data, const EditorValue *v, uint32_t count) -> EditorStatus {
            auto &u = *static_cast<Update *>(data);
            std::vector<EditorField> copied;
            if (count != 1 || CopyEditorFields(copied, v, count) != EDITOR_OK ||
                copied.front().type != u.owned->front().fields[u.index].type)
                return EDITOR_INVALID;
            auto next = *u.owned;
            copied.front().key = next.front().fields[u.index].key;
            next.front().fields[u.index] = std::move(copied.front());
            const auto fields = BorrowEditorFields(next);
            auto status = u.input->update.write(u.input->update.data, fields.data(), 1);
            if (status == EDITOR_OK)
                *u.owned = std::move(next);
            return status;
        };
        const std::vector<EditorField> childValue{value};
        const auto borrowed = BorrowEditorFields(childValue), fields = BorrowEditorFields(metadata);
        auto child = *input;
        child.label = EditorLiteral(names[i]);
        child.value = borrowed.values.front();
        child.update = {&update, write};
        child.fields = fields.data();
        child.count = uint32_t(fields.values.size());
        child.style = EDITOR_VALUE_INLINE;
        child.focus = input->focus && i == 0;
        child.choicesQuery = {};
        child.drawMembers = nullptr;
        child.membersData = nullptr;
        ImGui::SetNextItemWidth(240);
        const auto action =
            host->drawValue(host->data, EditorLiteral(i == 0 ? "value.string" : "value.enumeration"), &child);
        actions |= i == 0 ? action : action & ~uint32_t(EDITOR_INPUT_ACCEPT);
    }
    return actions;
}
} // namespace
bool RegisterArrayValueEditors(EditorApplication &app, std::string &error) {
    const EditorValueEditorDesc editor{sizeof(EditorValueEditorDesc),
                                       EDITOR_IMGUI_VERSION,
                                       EDITOR_TEXT("value.array-column"),
                                       EDITOR_TEXT("builtin.array-values"),
                                       EDITOR_TEXT("Array Column"),
                                       DrawColumn,
                                       nullptr,
                                       EDITOR_RECORD};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("arrays.values");
    module.editors = &editor;
    module.editorCount = 1;
    return app.Register(module, error);
}
} // namespace vdev
