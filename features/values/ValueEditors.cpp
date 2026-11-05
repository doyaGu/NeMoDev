#include "features/values/ValueEditors.h"
#include "features/values/SpecializedValueEditors.h"
#include "features/values/SelectionValueEditors.h"
#include "core/catalog/ValueText.h"
#include <iterator>
#include <algorithm>
#include <limits>
#include "editor/ImGuiDrawing.h"
#include "imgui_internal.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/TypeAhead.h"
#include "ui/setup/SetupLayout.h"
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
        if (input->height > 30) ImGui::InputTextMultiline(label.c_str(), &text, ImVec2(input->width, input->height));
        else vi::Edit(label.c_str(), at, ImVec2(at.x + input->width, at.y + input->height), text, vi::LEFT, &dialogDeactivated);
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
uint32_t DrawFlags(void *, const EditorHost *, const EditorValueInput *input) {
    const auto *labels = Field(*input, "labels"), *values = Field(*input, "values");
    if (input->value.type != EDITOR_INTEGER_VALUE || !labels || labels->type != EDITOR_TEXT_LIST ||
        !values || values->type != EDITOR_INTEGER_LIST || labels->texts.count != values->integers.count ||
        (values->integers.count && (!labels->texts.data || !values->integers.data))) return EDITOR_INPUT_INVALID;
    const bool dialog = input->style == EDITOR_VALUE_DIALOG;
    const auto *representation = Field(*input, "representation");
    const auto *display = Field(*input, "display");
    // Collapsed dialog values keep the native text representation.
    const bool collapsed = dialog && representation && representation->type == EDITOR_INTEGER_VALUE && representation->integer;
    const auto scale = setup::PageUnit();
    if (collapsed) {
        if (display && display->type == EDITOR_TEXT_VALUE)
            ImGui::TextWrapped("%s", Text(display->text).c_str());
        return 0;
    }
    auto bits = uint32_t(input->value.integer);
    bool changed = false, active = false, deactivated = false;
    const auto label = Text(input->label);
    ImGui::PushID(label.c_str()); ImGui::BeginGroup();
    {
        setup::SetupColumns columns("##flag-fields", 2, input->width);
        if (columns) {
            for (uint32_t i = 0; i < values->integers.count; ++i) {
                const auto mask = uint32_t(values->integers.data[i]);
                if (dialog && (!mask || (mask & (mask - 1)))) continue;
                columns.Next();
                bool on = mask && (bits & mask) == mask;
                ImGui::PushID(int(i));
                const auto name = Text(labels->texts.data[i]);
                bool toggle = false;
                if (dialog) {
                    ImGui::BeginGroup();
                    toggle = vi::Check("##flag", ImGui::GetCursorScreenPos(), 12 * scale, on);
                    active |= ImGui::IsItemActive() || ImGui::IsItemActivated();
                    deactivated |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::SameLine(); ImGui::TextWrapped("%s", name.c_str());
                    ImGui::EndGroup();
                } else {
                    toggle = ImGui::Checkbox(name.c_str(), &on);
                    active |= ImGui::IsItemActive() || ImGui::IsItemActivated();
                    deactivated |= ImGui::IsItemDeactivatedAfterEdit();
                }
                if (toggle) { bits = on ? bits | mask : bits & ~mask; changed = true; }
                ImGui::PopID();
            }
        }
    }
    ImGui::EndGroup(); ImGui::PopID();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (active) ImGui::ClearActiveID();
        return EDITOR_INPUT_CANCEL;
    }
    auto value = input->value; value.integer = uint64_t(int64_t(int32_t(bits)));
    const bool valid = !changed || input->update.write(input->update.data, &value, 1) == EDITOR_OK;
    return (active ? EDITOR_INPUT_ACTIVE : 0u) | (changed ? EDITOR_INPUT_CHANGED | EDITOR_INPUT_ACCEPT : 0u) |
        (deactivated ? EDITOR_INPUT_DEACTIVATED : 0u) | (!valid ? EDITOR_INPUT_INVALID : 0u);
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
// A container delegates child changes to its own sink; children never start
// another document edit. Native working copies may supply their member layout.
uint32_t DrawStructure(void *, const EditorHost *host, const EditorValueInput *input) {
    if (input->drawMembers) return input->drawMembers(input->membersData, host, input);
    const auto *schema = Field(*input, "schema");
    if (input->value.type != EDITOR_RECORD || !schema || schema->type != EDITOR_RECORD ||
        schema->fields.count != input->value.fields.count) return EDITOR_INPUT_INVALID;
    std::vector<EditorField> owned;
    if (CopyEditorFields(owned, &input->value, 1) != EDITOR_OK) return EDITOR_INPUT_INVALID;
    uint32_t action = 0;
    ImGui::PushID(Text(input->label).c_str());
    for (size_t i = 0; i < owned.front().fields.size(); ++i) {
        const auto &member = owned.front().fields[i];
        const EditorValue *description = nullptr;
        for (uint32_t j = 0; j < schema->fields.count; ++j)
            if (Text(schema->fields.data[j].key) == member.key) description = &schema->fields.data[j];
        if (!description || description->type != EDITOR_RECORD) { action |= EDITOR_INPUT_INVALID; break; }
        EditorValueInput metadata{}; metadata.fields = description->fields.data; metadata.count = description->fields.count;
        const auto *name = Field(metadata, "name"), *editor = Field(metadata, "editor"), *choices = Field(metadata, "choices"), *target = Field(metadata, "target");
        if (!name || name->type != EDITOR_TEXT_VALUE || !editor || editor->type != EDITOR_TEXT_VALUE || !target || target->type != EDITOR_OBJECT) {
            action |= EDITOR_INPUT_INVALID; break;
        }
        ImGui::PushID(member.key.c_str());
        ImGui::TextUnformatted(Text(name->text).c_str());
        const std::vector<EditorField> childOwned{member};
        const auto childValue = BorrowEditorFields(childOwned);
        struct Update { std::vector<EditorField> *owned; const EditorValueInput *input; size_t member; } update{&owned, input, i};
        const auto write = [](void *data, const EditorValue *value, uint32_t count) -> EditorStatus {
            auto &u = *static_cast<Update *>(data);
            std::vector<EditorField> fields;
            if (count != 1 || CopyEditorFields(fields, value, count) != EDITOR_OK) return EDITOR_INVALID;
            auto candidate = *u.owned;
            fields.front().key = candidate.front().fields[u.member].key;
            candidate.front().fields[u.member] = std::move(fields.front());
            const auto borrowed = BorrowEditorFields(candidate);
            const auto status = u.input->update.write(u.input->update.data, borrowed.data(), 1);
            if (status == EDITOR_OK) *u.owned = std::move(candidate);
            return status;
        };
        auto child = *input;
        const auto label = "##" + Text(name->text);
        child.label = EditorText(label); child.value = childValue.values.front(); child.edit = 0;
        child.fields = description->fields.data; child.count = description->fields.count;
        child.update = {&update, write}; child.drawMembers = nullptr; child.membersData = nullptr;
        child.choicesQuery = choices && choices->type == EDITOR_TEXT_VALUE ? choices->text : EditorString{};
        child.choicesRequest.values = target; child.choicesRequest.count = 1; child.choicesRequest.handler = {};
        ImGui::SetNextItemWidth(-1);
        action |= host->drawValue(host->data, editor->text, &child);
        ImGui::PopID();
    }
    ImGui::PopID();
    return action;
}
uint32_t DrawObjectArray(void *, const EditorHost *host, const EditorValueInput *input) {
    if (input->drawMembers) return input->drawMembers(input->membersData, host, input);
    if (input->value.type != EDITOR_OBJECT_LIST || (input->value.objects.count && !input->value.objects.data) || input->value.objects.count > 65536)
        return EDITOR_INPUT_INVALID;
    std::vector<ObjectRef> objects;
    if (input->value.objects.count) objects.assign(input->value.objects.data, input->value.objects.data + input->value.objects.count);
    uint32_t action = 0;
    bool changed = false;
    ImGui::PushID(Text(input->label).c_str());
    struct Update { std::vector<ObjectRef> *objects; size_t index; bool *changed; };
    const auto write = [](void *data, const EditorValue *value, uint32_t count) -> EditorStatus {
        auto &u = *static_cast<Update *>(data);
        if (!value || count != 1 || value->type != EDITOR_OBJECT || !value->object) return EDITOR_INVALID;
        if (u.index == u.objects->size()) u.objects->push_back(value->object);
        else (*u.objects)[u.index] = value->object;
        *u.changed = true; return EDITOR_OK;
    };
    ImGuiListClipper clipper; clipper.Begin(int(objects.size()), ImGui::GetFrameHeightWithSpacing());
    while (!changed && clipper.Step()) for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        ImGui::PushID(i);
        auto child = *input; child.label = EDITOR_TEXT("Object"); child.value = {}; child.value.key = EDITOR_TEXT("value");
        child.value.type = EDITOR_OBJECT; child.value.object = objects[size_t(i)]; child.drawMembers = nullptr; child.membersData = nullptr;
        const auto *names = Field(*input, "row-labels");
        EditorValue display{}; display.key = EDITOR_TEXT("display"); display.type = EDITOR_TEXT_VALUE;
        if (names && names->type == EDITOR_TEXT_LIST && uint32_t(i) < names->texts.count) display.text = names->texts.data[i];
        child.fields = &display; child.count = 1;
        Update update{&objects, size_t(i), &changed}; child.update = {&update, write};
        ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - 145));
        action |= host->drawValue(host->data, EditorString EDITOR_TEXT("value.object"), &child);
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) { objects.erase(objects.begin() + i); changed = true; }
        ImGui::SameLine(); ImGui::BeginDisabled(i == 0 || changed);
        if (ImGui::SmallButton("Up")) { std::swap(objects[size_t(i)], objects[size_t(i - 1)]); changed = true; }
        ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(size_t(i + 1) >= objects.size() || changed);
        if (ImGui::SmallButton("Down")) { std::swap(objects[size_t(i)], objects[size_t(i + 1)]); changed = true; }
        ImGui::EndDisabled();
        ImGui::PopID();
        if (changed) break;
    }
    clipper.End();
    auto add = *input; add.label = EDITOR_TEXT("Add Object"); add.value = {}; add.value.key = EDITOR_TEXT("value"); add.value.type = EDITOR_OBJECT;
    add.drawMembers = nullptr; add.membersData = nullptr; add.fields = nullptr; add.count = 0;
    Update update{&objects, objects.size(), &changed}; add.update = {&update, write};
    action |= host->drawValue(host->data, EditorString EDITOR_TEXT("value.object"), &add);
    if (changed) {
        auto value = input->value; value.objects = {objects.data(), uint32_t(objects.size())};
        if (input->update.write(input->update.data, &value, 1) != EDITOR_OK) action |= EDITOR_INPUT_INVALID;
        action |= EDITOR_INPUT_CHANGED | EDITOR_INPUT_ACCEPT;
    }
    ImGui::PopID(); return action;
}
enum class CompositeKind { Angle, Vector, Euler, Matrix, Color, Box, Rect };
uint32_t DrawComponents(void *data, const EditorHost *, const EditorValueInput *input) {
    const auto kind = *static_cast<const CompositeKind *>(data);
    const bool dialog = input->style == EDITOR_VALUE_DIALOG;
    const auto *rowsField = Field(*input, "rows"), *columnsField = Field(*input, "columns"), *labels = Field(*input, "labels");
    if (!rowsField || rowsField->type != EDITOR_INTEGER_VALUE || !columnsField || columnsField->type != EDITOR_INTEGER_VALUE ||
        !labels || labels->type != EDITOR_TEXT_LIST) return EDITOR_INPUT_INVALID;
    const auto rows = rowsField->integer, columns = columnsField->integer;
    if (!rows || !columns || rows > 4 || columns > 4 || rows * columns > 16 || labels->texts.count != columns) return EDITOR_INPUT_INVALID;
    const bool shape = kind == CompositeKind::Angle ? rows == 1 && columns == 1 :
        kind == CompositeKind::Vector ? rows == 1 && columns >= 2 :
        kind == CompositeKind::Euler ? rows == 3 && columns == 1 :
        kind == CompositeKind::Matrix ? rows == 4 && columns == 4 :
        kind == CompositeKind::Color ? rows == 1 && columns == 4 :
        kind == CompositeKind::Box ? rows == 2 && columns == 3 : rows == 2 && columns == 2;
    if (!shape) return EDITOR_INPUT_INVALID;
    const auto count = unsigned(rows * columns);
    const bool angle = kind == CompositeKind::Angle;
    if ((angle && input->value.type != EDITOR_REAL_VALUE) || (!angle && (input->value.type != EDITOR_REAL_LIST ||
        input->value.reals.count != count || !input->value.reals.data))) return EDITOR_INPUT_INVALID;
    float values[16]{};
    for (unsigned i = 0; i < count; ++i) {
        const auto value = angle ? input->value.real : input->value.reals.data[i];
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return EDITOR_INPUT_INVALID;
        values[i] = float(value);
    }
    const auto scale = setup::PageUnit();
    const float width = std::max(1.f, input->width > 0 ? input->width : ImGui::GetContentRegionAvail().x);
    const auto *modeField = Field(*input, "representation");
    const unsigned mode = modeField && modeField->type == EDITOR_INTEGER_VALUE ? unsigned(modeField->integer) : 0;
    const auto *rowLabels = Field(*input, "row-labels");
    bool changed = false, valid = true, active = false, deactivated = false, accept = false, cancel = false;
    const auto track = [&] {
        active |= ImGui::IsItemActive() || ImGui::IsItemActivated();
        deactivated |= ImGui::IsItemDeactivatedAfterEdit();
    };
    const auto number = [&](const char *id, float &value, bool channel = false) {
        char buffer[48];
        if (channel) std::snprintf(buffer, sizeof(buffer), "%d", int(std::clamp(double(value), double(INT32_MIN), double(INT32_MAX))));
        else std::snprintf(buffer, sizeof(buffer), "%.6g", double(value));
        std::string text = buffer;
        bool ended = false;
        if (vi::Edit(id, text, vi::CENTER, &ended)) {
            double parsed = 0;
            int32_t integer = 0;
            const bool parsedOk = channel ? ParseIntegerValue(text, integer) : ParseRealValue(text, parsed);
            if (channel) parsed = integer;
            valid &= parsedOk; changed = true;
            if (parsedOk) value = channel ? std::clamp(float(parsed), 0.0f, 255.0f) : float(parsed);
        }
        track(); deactivated |= ended;
    };
    const auto write = [&](const float *source) {
        double components[16]{};
        for (unsigned i = 0; i < count; ++i) components[i] = source[i];
        auto value = input->value;
        if (angle) value.real = source[0];
        else value.reals = {components, count};
        return input->update.write(input->update.data, &value, 1) == EDITOR_OK;
    };
    const auto dial = [&](const char *id, float &value) {
        constexpr double pi = 3.14159265358979323846;
        const double turns = std::trunc(double(value) / (2 * pi));
        const double rest = std::fmod(double(value), 2 * pi);
        ImGui::PushID(id);
        {
            setup::SetupColumns fields("##angle-fields", 2, width);
            if (fields) {
                fields.Next();
                {
                    setup::SetupForm form("##angle-numbers");
                    if (form) {
                        form.Row("Turn");
                        char buffer[64]; std::snprintf(buffer, sizeof(buffer), "%.0f", turns);
                        std::string text = buffer;
                        if (vi::Edit("##turn", text, vi::CENTER)) {
                            int32_t parsed = 0; const bool ok = ParseIntegerValue(text, parsed); valid &= ok; changed = true;
                            if (ok) value = float(parsed * 2 * pi + rest);
                        }
                        track();
                        form.Row("Degree");
                        std::snprintf(buffer, sizeof(buffer), "%.3g", rest * 180 / pi); text = buffer;
                        if (vi::Edit("##degree", text, vi::CENTER)) {
                            double parsed = 0; const bool ok = ParseRealValue(text, parsed); valid &= ok; changed = true;
                            if (ok) value = float(turns * 2 * pi + parsed * pi / 180);
                        }
                        track();
                    }
                }
                fields.Next();
                const auto at = ImGui::GetCursorScreenPos();
                const ImVec2 center(at.x + 10 * scale, at.y + 10 * scale);
                ImGui::InvisibleButton("##dial", ImVec2(20 * scale, 20 * scale));
                if (ImGui::IsItemActive() && (ImGui::IsItemActivated() ||
                    ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0)) {
                    const auto mouse = ImGui::GetIO().MousePos;
                    double radians = std::atan2(center.y - mouse.y, mouse.x - center.x);
                    if (radians < 0) radians += 2 * pi;
                    value = float(turns * 2 * pi + radians); changed = true;
                }
                track();
                auto *draw = ImGui::GetWindowDrawList();
                draw->AddCircleFilled(center, 10 * scale, vi::Color(8)); draw->AddCircle(center, 10 * scale, vi::Color(0));
                draw->AddLine(center, ImVec2(center.x + 10 * scale * float(std::cos(rest)), center.y - 10 * scale * float(std::sin(rest))), vi::Color(0));
            }
        }
        ImGui::PopID();
    };
    ImGui::PushID(Text(input->label).c_str());
    ImGui::BeginGroup();
    if (kind == CompositeKind::Matrix && dialog && mode == 1) {
        const auto *display = Field(*input, "display");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextWrapped("%s", display && display->type == EDITOR_TEXT_VALUE ? Text(display->text).c_str() : "");
        ImGui::PopTextWrapPos();
    } else if ((angle || kind == CompositeKind::Euler) && dialog) {
        for (unsigned i = 0; i < count; ++i) {
            ImGui::PushID(int(i));
            dial(angle ? "##angle" : "##euler", values[i]);
            ImGui::PopID();
        }
    } else if (kind == CompositeKind::Color && input->height > 100) {
        ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoSidePreview;
        const auto *alpha = Field(*input, "alpha");
        if (alpha && alpha->type == EDITOR_INTEGER_VALUE && !alpha->integer) flags |= ImGuiColorEditFlags_NoAlpha;
        ImGui::SetNextItemWidth(input->width);
        changed = ImGui::ColorPicker4(Text(input->label).c_str(), values, flags);
        active = ImGui::IsAnyItemActive();
    } else if (kind == CompositeKind::Color) {
        if (count != 4) { ImGui::EndGroup(); ImGui::PopID(); return EDITOR_INPUT_INVALID; }
        auto *storage = ImGui::GetStateStorage();
        const ImGuiID wasOpenKey = ImGui::GetID("##picker-open");
        const bool wasOpen = storage->GetBool(wasOpenKey);
        setup::SetupColumns channels("##color-fields", 5, width);
        if (channels) {
            channels.Next();
            const auto swatch = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(swatch);
            const auto size = ImVec2(std::min(57 * scale, ImGui::GetContentRegionAvail().x),
                                    std::max(ImGui::GetFrameHeight(), 19.5f * scale));
            bool open = false;
            if (dialog) {
                open = ImGui::InvisibleButton("##swatch", size);
                auto *draw = ImGui::GetWindowDrawList();
                const ImVec2 end(swatch.x + size.x, swatch.y + size.y);
                draw->AddRectFilled(swatch, end, ImGui::ColorConvertFloat4ToU32(ImVec4(values[0], values[1], values[2], 1)), 3 * scale);
                draw->AddRect(swatch, end, vi::Color(3), 3 * scale);
            } else open = ImGui::ColorButton("##swatch", ImVec4(values[0], values[1], values[2], values[3]), ImGuiColorEditFlags_AlphaPreviewHalf, size);
            if (open) {
                ImGui::OpenPopup("##colorpicker");
            }
            bool pickerOpen = ImGui::IsPopupOpen("##colorpicker");
            if (ImGui::BeginPopup("##colorpicker")) {
                if (ImGui::ColorPicker4("##picker", values, ImGuiColorEditFlags_AlphaBar)) changed = true;
                if (ImGui::Button("OK")) { accept = true; ImGui::CloseCurrentPopup(); }
                ImGui::SameLine();
                if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    cancel = true; ImGui::CloseCurrentPopup();
                }
                active = true;
                ImGui::EndPopup();
            }
            if (wasOpen && !pickerOpen) accept = true;
            storage->SetBool(wasOpenKey, pickerOpen && !accept && !cancel);
            static const char *ids[] = {"##r", "##g", "##b", "##a"};
            for (unsigned i = 0; i < 4; ++i) {
                channels.Next();
                char formId[32]; std::snprintf(formId, sizeof(formId), "##color-channel-%u", i);
                setup::SetupForm form(formId);
                if (!form) continue;
                const auto name = Text(labels->texts.data[i]) + ":";
                form.Row(name.c_str());
                if (dialog) {
                    auto value = values[i] * 255;
                    number(ids[i], value, true);
                    // Formatting a channel must not quantize the other channels.
                    if (changed && !pickerOpen && ImGui::IsItemEdited()) values[i] = value / 255;
                } else {
                    changed |= ImGui::DragFloat(ids[i], &values[i], .005f, 0, 1, "%.3g"); track();
                }
            }
            active |= pickerOpen || wasOpen;
        }
    } else {
        float shown[16]{}; std::copy(values, values + count, shown);
        if (kind == CompositeKind::Rect) {
            if (mode == 1) { shown[2] -= shown[0]; shown[3] -= shown[1]; }
            if (mode == 2) {
                shown[2] = (values[2] - values[0]) / 2; shown[3] = (values[3] - values[1]) / 2;
                shown[0] += shown[2]; shown[1] += shown[3];
            }
        }
        static const char *ids[] = {"##x", "##y", "##z", "##w"};
        for (unsigned r = 0; r < rows; ++r) {
            if (dialog && rowLabels && rowLabels->type == EDITOR_TEXT_LIST && rowLabels->texts.count == rows) {
                const char *rectLabels[][2] = {{"Left,Top", "Right,Bottom"}, {"Left,Top", "Size"}, {"Center", "Half Size"}};
                const auto label = kind == CompositeKind::Rect ? std::string(rectLabels[mode < 3 ? mode : 0][r]) : Text(rowLabels->texts.data[r]);
                ImGui::TextWrapped("%s", label.c_str());
            }
            char rowId[32]; std::snprintf(rowId, sizeof(rowId), "##component-row-%u", r);
            setup::SetupColumns fields(rowId, int(columns), width);
            if (!fields) continue;
            for (unsigned c = 0; c < columns; ++c) {
                fields.Next();
                const auto i = r * unsigned(columns) + c;
                const bool indexed = !dialog || rows > 1;
                {
                    char formId[32]; std::snprintf(formId, sizeof(formId), "##component-field-%u", i);
                    setup::SetupForm form(formId);
                    if (form) {
                        const auto name = Text(labels->texts.data[c]) + ":";
                        form.Row(name.c_str());
                        if (indexed) ImGui::PushID(int(i));
                        if (dialog) {
                            number(kind == CompositeKind::Matrix ? "##m" : kind == CompositeKind::Box ? "##b" :
                                kind == CompositeKind::Rect ? "##r" : ids[c], shown[i]);
                        } else {
                            auto displayed = shown[i];
                            if (angle || kind == CompositeKind::Euler) displayed *= 180.0f / 3.14159265358979323846f;
                            if (ImGui::DragFloat(ids[c], &displayed, .01f, 0, 0, "%.6g")) {
                                shown[i] = (angle || kind == CompositeKind::Euler) ? displayed * 3.14159265358979323846f / 180.0f : displayed;
                                changed = true;
                            }
                            track();
                        }
                        if (indexed) ImGui::PopID();
                    }
                }
            }
        }
        std::copy(shown, shown + count, values);
        if (kind == CompositeKind::Rect && mode == 1) { values[2] += values[0]; values[3] += values[1]; }
        if (kind == CompositeKind::Rect && mode == 2) {
            values[0] = shown[0] - shown[2]; values[1] = shown[1] - shown[3];
            values[2] = shown[0] + shown[2]; values[3] = shown[1] + shown[3];
        }
    }
    ImGui::EndGroup(); ImGui::PopID();
    if (changed && valid) valid = write(values);
    if (!cancel && ImGui::IsKeyPressed(ImGuiKey_Escape)) cancel = true;
    if (cancel && active) ImGui::ClearActiveID();
    return (changed ? EDITOR_INPUT_CHANGED : 0u) | (!valid ? EDITOR_INPUT_INVALID : 0u) |
        (active ? EDITOR_INPUT_ACTIVE : 0u) | ((accept || (deactivated && !active)) ? EDITOR_INPUT_ACCEPT : 0u) |
        (cancel ? EDITOR_INPUT_CANCEL : 0u);
}
} // namespace
bool ValueTargetChanged(const EditorValueInput &input) {
    // Keep presentation state in the ImGui window, with fixed keys per control.
    // A reopened control or a different full object reference starts fresh.
    const EditorValue *target = nullptr;
    for (uint32_t i = 0; i < input.choicesRequest.count; ++i)
        if (Text(input.choicesRequest.values[i].key) == "target" && input.choicesRequest.values[i].type == EDITOR_OBJECT) target = &input.choicesRequest.values[i];
    const auto object = target ? target->object : EditorObject{input.choicesRequest.scope.document, 0, input.choicesRequest.scope.view};
    const uint32_t identity[] = {uint32_t(object.document.value), uint32_t(object.document.value >> 32),
        object.object, uint32_t(object.lifetime), uint32_t(object.lifetime >> 32)};
    auto *storage = ImGui::GetStateStorage(); const auto frameKey = ImGui::GetID("last-frame");
    const int frame = ImGui::GetFrameCount(), previous = storage->GetInt(frameKey, -2);
    // Dialog working copies keep their presentation while native work pauses
    // drawing. A new working copy has a different full object reference.
    bool fresh = input.style == EDITOR_VALUE_INLINE && previous != frame && previous != frame - 1;
    for (unsigned i = 0; i < 5; ++i) {
        char name[24]; std::snprintf(name, sizeof(name), "value-owner-%u", i); const auto key = ImGui::GetID(name);
        fresh |= uint32_t(storage->GetInt(key)) != identity[i]; storage->SetInt(key, int(identity[i]));
    }
    storage->SetInt(frameKey, frame); return fresh;
}
bool RegisterValueEditors(EditorApplication &editor, std::string &error) {
    static bool clear = true;
    static const Kind kinds[] = {Kind::Boolean, Kind::Integer, Kind::Real, Kind::Enumeration, Kind::Object};
    static const CompositeKind components[] = {CompositeKind::Angle, CompositeKind::Vector, CompositeKind::Euler, CompositeKind::Matrix, CompositeKind::Color, CompositeKind::Box, CompositeKind::Rect};
    const EditorValueEditorDesc editors[] = {
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.time"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Time"), DrawTimeValue, nullptr, EDITOR_REAL_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.key"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Key"), DrawKeyValue, nullptr, EDITOR_INTEGER_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.script"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Script"), DrawScriptValue, nullptr, EDITOR_OBJECT},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.animation"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Animation"), DrawAnimationValue, nullptr, EDITOR_OBJECT},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.message"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Message"), DrawMessageValue, nullptr, EDITOR_RECORD},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.attribute"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Attribute"), DrawAttributeValue, nullptr, EDITOR_INTEGER_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.synchro"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Synchro"), DrawSynchroValue, nullptr, EDITOR_RECORD},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.curve2d"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("2D Curve"), DrawCurve2DValue, nullptr, EDITOR_RECORD},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.dependencies"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Dependencies"), DrawDependenciesValue, nullptr, EDITOR_RECORD},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.lod-options"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("LOD Options"), DrawLodOptionsValue, nullptr, EDITOR_RECORD},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.structure"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Structure"), DrawStructure, nullptr, EDITOR_RECORD},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.object-array"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Object Array"), DrawObjectArray, nullptr, EDITOR_OBJECT_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.string"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("String"), DrawText, nullptr, EDITOR_TEXT_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("text.single-line"), EDITOR_TEXT("builtin.text"), EDITOR_TEXT("Text"), DrawText, nullptr, EDITOR_TEXT_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("text.single-line"), EDITOR_TEXT("builtin.text-clear"), EDITOR_TEXT("Text with Clear"), DrawText, &clear, EDITOR_TEXT_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.boolean"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Boolean"), DrawScalar, const_cast<Kind *>(&kinds[0]), EDITOR_INTEGER_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.integer"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Integer"), DrawScalar, const_cast<Kind *>(&kinds[1]), EDITOR_INTEGER_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.real"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Number"), DrawScalar, const_cast<Kind *>(&kinds[2]), EDITOR_REAL_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.enumeration"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Enumeration"), DrawScalar, const_cast<Kind *>(&kinds[3]), EDITOR_INTEGER_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.flags"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Flags"), DrawFlags, nullptr, EDITOR_INTEGER_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.object"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Object"), DrawScalar, const_cast<Kind *>(&kinds[4]), EDITOR_OBJECT},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.angle"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Angle"), DrawComponents, const_cast<CompositeKind *>(&components[0]), EDITOR_REAL_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.vector2"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("2D Vector"), DrawComponents, const_cast<CompositeKind *>(&components[1]), EDITOR_REAL_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.vector3"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("3D Vector"), DrawComponents, const_cast<CompositeKind *>(&components[1]), EDITOR_REAL_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.quaternion"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Quaternion"), DrawComponents, const_cast<CompositeKind *>(&components[1]), EDITOR_REAL_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.euler"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Euler Angles"), DrawComponents, const_cast<CompositeKind *>(&components[2]), EDITOR_REAL_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.matrix"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Matrix"), DrawComponents, const_cast<CompositeKind *>(&components[3]), EDITOR_REAL_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.color"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Color"), DrawComponents, const_cast<CompositeKind *>(&components[4]), EDITOR_REAL_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.box"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Bounding Box"), DrawComponents, const_cast<CompositeKind *>(&components[5]), EDITOR_REAL_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.rect"), EDITOR_TEXT("builtin.values"), EDITOR_TEXT("Rectangle"), DrawComponents, const_cast<CompositeKind *>(&components[6]), EDITOR_REAL_LIST}};
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("values.editors");
    module.editors = editors; module.editorCount = uint32_t(std::size(editors));
    return editor.Register(module, error);
}
} // namespace vdev
