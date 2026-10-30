#include "features/values/ValueEditors.h"
#include "core/catalog/ValueText.h"
#include <iterator>
#include "editor/ImGuiDrawing.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/TypeAhead.h"
#include "misc/cpp/imgui_stdlib.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace vdev {
namespace {
const EditorValue *Field(const EditorValueInput &input, const char *key) {
    for (uint32_t i = 0; i < input.count; ++i)
        if (std::string(input.fields[i].key.data, input.fields[i].key.size) == key) return &input.fields[i];
    return nullptr;
}
std::string Text(EditorString text) { return {text.data ? text.data : "", text.size}; }
uint32_t DrawText(void *data, const EditorHost *, const EditorValueInput *input) {
    auto text = Text(input->value.text);
    const auto label = Text(input->label);
    if (input->focus) ImGui::SetKeyboardFocusHere();
    bool accept = false, dialogDeactivated = false;
    if (input->style == EDITOR_VALUE_DIALOG) {
        const auto at = ImGui::GetCursorScreenPos();
        vi::Edit(label.c_str(), at, ImVec2(at.x + input->width, at.y + input->height), text, vi::RIGHT, &dialogDeactivated);
    } else accept = ImGui::InputText(label.c_str(), &text, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    bool changed = text != Text(input->value.text);
    bool deactivated = dialogDeactivated || ImGui::IsItemDeactivatedAfterEdit();
    const bool active = ImGui::IsItemActive() || ImGui::IsItemActivated();
    if (data) { ImGui::SameLine(); if (ImGui::SmallButton("Clear")) { text.clear(); changed = deactivated = true; } }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) return EDITOR_INPUT_CANCEL;
    auto value = input->value; value.text = EditorText(text);
    const bool valid = !changed || input->update.write(input->update.data, &value, 1) == EDITOR_OK;
    return (active ? EDITOR_INPUT_ACTIVE : 0u) | (accept ? EDITOR_INPUT_ACCEPT : 0u) |
        (changed ? EDITOR_INPUT_CHANGED : 0u) | (!valid ? EDITOR_INPUT_INVALID : 0u) |
        (deactivated ? EDITOR_INPUT_DEACTIVATED : 0u);
}
enum class Kind { Boolean, Integer, Real, Enumeration, Object };
uint32_t DrawScalar(void *data, const EditorHost *host, const EditorValueInput *input) {
    const auto kind = *static_cast<const Kind *>(data);
    const auto label = Text(input->label);
    auto value = input->value;
    bool changed = false, accept = false, valid = true;
    const bool dialog = input->style == EDITOR_VALUE_DIALOG;
    const auto at = ImGui::GetCursorScreenPos();
    const ImVec2 to(at.x + input->width, at.y + input->height);
    const auto *display = Field(*input, "display");
    const auto shown = display && display->type == EDITOR_TEXT_VALUE ? Text(display->text) : std::string();
    if (input->focus) ImGui::SetKeyboardFocusHere();
    if (kind == Kind::Boolean) {
        bool checked = value.integer != 0;
        changed = dialog ? vi::Check(label.c_str(), at, input->height, checked) : ImGui::Checkbox(label.c_str(), &checked);
        value.integer = checked ? 1 : 0;
        accept = changed;
    } else if (kind == Kind::Object) {
        std::vector<std::string> names;
        std::vector<ObjectRef> objects;
        auto load = [&] {
            std::vector<EditorField> fields;
            const auto copy = [](void *data, const EditorValue *values, uint32_t count) {
                return CopyEditorFields(*static_cast<std::vector<EditorField> *>(data), values, count);
            };
            if (!input->choicesQuery.size || host->query(host->data, input->choicesQuery, &input->choicesRequest, {&fields, copy}) != EDITOR_OK) {
                valid = false; return names;
            }
            for (auto &field : fields) {
                if (field.key == "labels" && field.type == EDITOR_TEXT_LIST) names = std::move(field.texts);
                if (field.key == "objects" && field.type == EDITOR_OBJECT_LIST) objects = std::move(field.objects);
            }
            if (names.size() != objects.size()) { valid = false; names.clear(); objects.clear(); }
            return names;
        };
        int chosen = -1;
        if (dialog) chosen = vi::Combo(label.c_str(), at, to, shown, load, -1);
        else if (ImGui::BeginCombo(label.c_str(), shown.c_str())) {
            load();
            TypeAhead typeAhead;
            for (size_t i = 0; i < names.size(); ++i) {
                ImGui::PushID(int(i));
                if (typeAhead.Selectable(names[i].c_str(), objects[i] == value.object)) chosen = int(i);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (valid && chosen >= 0 && size_t(chosen) < objects.size()) {
            value.object = objects[size_t(chosen)]; changed = accept = true;
        }
    } else if (kind == Kind::Enumeration) {
        const auto *labels = Field(*input, "labels"), *values = Field(*input, "values");
        if (!labels || labels->type != EDITOR_TEXT_LIST || !values || values->type != EDITOR_INTEGER_LIST ||
            labels->texts.count != values->integers.count) return EDITOR_INPUT_INVALID;
        const auto count = values->integers.count;
        int selected = -1, chosen = -1;
        for (uint32_t i = 0; i < count; ++i) if (values->integers.data[i] == value.integer) selected = int(i);
        const auto preview = selected >= 0 ? Text(labels->texts.data[selected]) : shown;
        if (dialog) chosen = vi::Combo(label.c_str(), at, to, preview, [&] {
            std::vector<std::string> names;
            for (uint32_t i = 0; i < count; ++i) names.push_back(Text(labels->texts.data[i]));
            return names;
        }, selected);
        else if (ImGui::BeginCombo(label.c_str(), preview.c_str())) {
            TypeAhead typeAhead;
            for (uint32_t i = 0; i < count; ++i) {
                ImGui::PushID(int(i));
                if (typeAhead.Selectable(Text(labels->texts.data[i]).c_str(), int(i) == selected)) chosen = int(i);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (chosen >= 0 && uint32_t(chosen) < count) { value.integer = values->integers.data[chosen]; changed = accept = true; }
    } else if (dialog) {
        // Preserve the dialog's centered text field; conversion is performed by
        // its parameter description, including native units such as percent.
        auto text = shown;
        bool deactivated = false;
        changed = vi::Edit(label.c_str(), at, to, text, vi::CENTER, &deactivated);
        if (changed) {
            int32_t integer = 0;
            const auto *scale = Field(*input, "scale");
            if (scale && scale->type != EDITOR_REAL_VALUE) return EDITOR_INPUT_INVALID;
            if (kind == Kind::Integer) {
                valid = ParseIntegerValue(text, integer);
                value.integer = uint64_t(int64_t(integer));
            } else valid = ParseRealValue(text, value.real, scale ? scale->real : 1);
            if (valid) valid = input->update.write(input->update.data, &value, 1) == EDITOR_OK;
        }
        accept = deactivated;
    } else if (kind == Kind::Integer) {
        int integer = int(int64_t(value.integer));
        changed = ImGui::DragInt(label.c_str(), &integer);
        value.integer = uint64_t(int64_t(integer));
        accept = ImGui::IsItemDeactivatedAfterEdit();
    } else {
        const auto *unit = Field(*input, "scale");
        if (unit && unit->type != EDITOR_REAL_VALUE) return EDITOR_INPUT_INVALID;
        const auto scale = unit ? unit->real : 1;
        if (!std::isfinite(scale) || scale <= 0) return EDITOR_INPUT_INVALID;
        float real = float(value.real * scale);
        changed = ImGui::DragFloat(label.c_str(), &real, .01f, 0, 0, scale == 100 ? "%.4g%%" : "%.4g");
        value.real = double(real) / scale;
        accept = ImGui::IsItemDeactivatedAfterEdit();
    }
    const bool active = ImGui::IsItemActive() || ImGui::IsItemActivated();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) return EDITOR_INPUT_CANCEL;
    if (changed && !(dialog && (kind == Kind::Integer || kind == Kind::Real)))
        valid = input->update.write(input->update.data, &value, 1) == EDITOR_OK;
    return (active ? EDITOR_INPUT_ACTIVE : 0u) | (accept ? EDITOR_INPUT_ACCEPT : 0u) |
        (changed ? EDITOR_INPUT_CHANGED : 0u) | (!valid ? EDITOR_INPUT_INVALID : 0u);
}
} // namespace
bool RegisterValueEditors(EditorApplication &editor, std::string &error) {
    static bool clear = true;
    static const Kind kinds[] = {Kind::Boolean, Kind::Integer, Kind::Real, Kind::Enumeration, Kind::Object};
    const EditorValueEditorDesc editors[] = {
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("text.single-line"), EDITOR_TEXT("builtin.text"), EDITOR_TEXT("Text"), DrawText, nullptr, EDITOR_TEXT_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("text.single-line"), EDITOR_TEXT("builtin.text-clear"), EDITOR_TEXT("Text with Clear"), DrawText, &clear, EDITOR_TEXT_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.boolean"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Boolean"), DrawScalar, const_cast<Kind *>(&kinds[0]), EDITOR_INTEGER_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.integer"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Integer"), DrawScalar, const_cast<Kind *>(&kinds[1]), EDITOR_INTEGER_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.real"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Number"), DrawScalar, const_cast<Kind *>(&kinds[2]), EDITOR_REAL_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.enumeration"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Enumeration"), DrawScalar, const_cast<Kind *>(&kinds[3]), EDITOR_INTEGER_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.object"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Object"), DrawScalar, const_cast<Kind *>(&kinds[4]), EDITOR_OBJECT}};
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("values.editors");
    module.editors = editors; module.editorCount = uint32_t(std::size(editors));
    return editor.Register(module, error);
}
} // namespace vdev
