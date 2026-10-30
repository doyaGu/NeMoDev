#include "features/values/SelectionValueEditors.h"
#include "features/values/ValueEditors.h"
#include "core/catalog/ValueText.h"
#include "ui/controls/DevControls.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vdev {
namespace {
std::string Text(EditorString text) {
    return {text.data ? text.data : "", text.size};
}
const EditorField *Field(const std::vector<EditorField> &fields, const char *key) {
    for (const auto &field : fields)
        if (field.key == key) return &field;
    return nullptr;
}
struct Control {
    const EditorValueInput &input;
    std::vector<EditorField> value;
    ImVec2 at;
    float scale;
    bool changed = false, active = false, accept = false, valid = true;
    explicit Control(const EditorValueInput &input, float width)
        : input(input), at(ImGui::GetCursorScreenPos()),
          scale(input.style == EDITOR_VALUE_DIALOG ? input.width / (width * 1.5f) : ImGui::GetFontSize() / 13) {
        valid = CopyEditorFields(value, &input.value, 1) == EDITOR_OK;
        ImGui::PushID(Text(input.label).c_str());
        ImGui::BeginGroup();
    }
    ~Control() {
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImVec2 P(float x, float y) const { return ImVec2(at.x + x * 1.5f * scale, at.y + y * 1.625f * scale); }
    void Observe() {
        active |= ImGui::IsItemActive() || ImGui::IsItemActivated();
        accept |= ImGui::IsItemDeactivatedAfterEdit();
    }
    void Label(float x, float y, float right, const char *text) { vi::Text(P(x, y), P(right, y + 10), text); }
    bool Integer(const char *id, float x, float y, float right, int32_t &number) {
        std::string text = std::to_string(number);
        bool done = false;
        const bool edited = vi::Edit(id, P(x, y), P(right, y + 12), text, vi::CENTER, &done);
        Observe();
        accept |= done;
        if (edited) {
            valid &= ParseIntegerValue(text, number);
            changed = true;
        }
        return edited && valid;
    }
    int Combo(const char *id, float x, float y, float right, const std::string &preview, const std::vector<std::string> &names) {
        const int choice = vi::Combo(id, P(x, y), P(right, y + 12), preview, [&] { return names; }, -1);
        Observe();
        return choice;
    }
    bool EditCombo(const char *id, std::string &text, const std::vector<std::string> &names) {
        bool editing = false;
        const bool done = vi::EditCombo(id, P(15, 0), P(210, 12), text, [&] { return names; }, &editing);
        active |= editing;
        accept |= done;
        return done;
    }
    EditorField *Member(const char *key) {
        if (!valid || value.empty()) return nullptr;
        for (auto &member : value.front().fields)
            if (member.key == key) return &member;
        return nullptr;
    }
    uint32_t Finish() {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (active) ImGui::ClearActiveID();
            return EDITOR_INPUT_CANCEL;
        }
        if (changed && valid) {
            auto borrowed = BorrowEditorFields(value);
            valid = input.update.write && input.update.write(input.update.data, borrowed.data(), 1) == EDITOR_OK;
        }
        return (changed ? EDITOR_INPUT_CHANGED : 0u) | (active ? EDITOR_INPUT_ACTIVE : 0u) | (accept ? EDITOR_INPUT_ACCEPT : 0u) |
               (!valid ? EDITOR_INPUT_INVALID : 0u);
    }
};
struct Choices {
    std::vector<EditorField> fields;
    bool Load(const EditorHost *host, const EditorValueInput &input, const std::vector<EditorField> &filters = {}) {
        std::vector<EditorField> arguments;
        for (uint32_t i = 0; i < input.choicesRequest.count; ++i)
            if (Text(input.choicesRequest.values[i].key) == "target") {
                std::vector<EditorField> copied;
                if (CopyEditorFields(copied, input.choicesRequest.values + i, 1) != EDITOR_OK) return false;
                arguments.push_back(std::move(copied.front()));
            }
        arguments.push_back({"filters", EDITOR_RECORD});
        arguments.back().fields = filters;
        const auto borrowed = BorrowEditorFields(arguments);
        const EditorRequest request{input.choicesRequest.scope, borrowed.data(), uint32_t(borrowed.size()), input.choicesRequest.handler};
        const auto copy = [](void *data, const EditorValue *values, uint32_t count) {
            return CopyEditorFields(*static_cast<std::vector<EditorField> *>(data), values, count);
        };
        fields.clear();
        return input.choicesQuery.size && host->query(host->data, input.choicesQuery, &request, {&fields, copy}) == EDITOR_OK;
    }
    const std::vector<std::string> &Names(const char *key = "labels") const {
        static const std::vector<std::string> empty;
        const auto *field = Field(fields, key);
        return field && field->type == EDITOR_TEXT_LIST ? field->texts : empty;
    }
    std::vector<ObjectRef> Objects(const char *key = "objects") const {
        const auto *field = Field(fields, key);
        return field && field->type == EDITOR_OBJECT_LIST ? field->objects : std::vector<ObjectRef>{};
    }
    std::vector<uint64_t> Integers(const char *key = "values") const {
        const auto *field = Field(fields, key);
        return field && field->type == EDITOR_INTEGER_LIST ? field->integers : std::vector<uint64_t>{};
    }
};
std::string Display(const EditorValueInput &input) {
    for (uint32_t i = 0; i < input.count; ++i)
        if (Text(input.fields[i].key) == "display" && input.fields[i].type == EDITOR_TEXT_VALUE) return Text(input.fields[i].text);
    return {};
}
void SaveOwner(ObjectRef object, const char *prefix = "script-owner") {
    const uint32_t words[] = {uint32_t(object.document.value), uint32_t(object.document.value >> 32), object.object,
                              uint32_t(object.lifetime), uint32_t(object.lifetime >> 32)};
    for (unsigned i = 0; i < 5; ++i) {
        char key[24];
        std::snprintf(key, sizeof(key), "%s-%u", prefix, i);
        ImGui::GetStateStorage()->SetInt(ImGui::GetID(key), int(words[i]));
    }
}
ObjectRef SavedOwner(const char *prefix = "script-owner") {
    uint32_t words[5]{};
    for (unsigned i = 0; i < 5; ++i) {
        char key[24];
        std::snprintf(key, sizeof(key), "%s-%u", prefix, i);
        words[i] = uint32_t(ImGui::GetStateStorage()->GetInt(ImGui::GetID(key)));
    }
    return {{uint64_t(words[0]) | (uint64_t(words[1]) << 32)}, words[2], uint64_t(words[3]) | (uint64_t(words[4]) << 32)};
}
struct Key {
    ImGuiKey key;
    unsigned scan;
};
// DirectInput scan codes are the stored CK key value; ImGui supplies input on
// every UI backend. This table does not depend on SDL or a runtime input manager.
const Key keys[] = {{ImGuiKey_Escape, 1},
                    {ImGuiKey_1, 2},
                    {ImGuiKey_2, 3},
                    {ImGuiKey_3, 4},
                    {ImGuiKey_4, 5},
                    {ImGuiKey_5, 6},
                    {ImGuiKey_6, 7},
                    {ImGuiKey_7, 8},
                    {ImGuiKey_8, 9},
                    {ImGuiKey_9, 10},
                    {ImGuiKey_0, 11},
                    {ImGuiKey_Minus, 12},
                    {ImGuiKey_Equal, 13},
                    {ImGuiKey_Backspace, 14},
                    {ImGuiKey_Tab, 15},
                    {ImGuiKey_Q, 16},
                    {ImGuiKey_W, 17},
                    {ImGuiKey_E, 18},
                    {ImGuiKey_R, 19},
                    {ImGuiKey_T, 20},
                    {ImGuiKey_Y, 21},
                    {ImGuiKey_U, 22},
                    {ImGuiKey_I, 23},
                    {ImGuiKey_O, 24},
                    {ImGuiKey_P, 25},
                    {ImGuiKey_LeftBracket, 26},
                    {ImGuiKey_RightBracket, 27},
                    {ImGuiKey_Enter, 28},
                    {ImGuiKey_LeftCtrl, 29},
                    {ImGuiKey_A, 30},
                    {ImGuiKey_S, 31},
                    {ImGuiKey_D, 32},
                    {ImGuiKey_F, 33},
                    {ImGuiKey_G, 34},
                    {ImGuiKey_H, 35},
                    {ImGuiKey_J, 36},
                    {ImGuiKey_K, 37},
                    {ImGuiKey_L, 38},
                    {ImGuiKey_Semicolon, 39},
                    {ImGuiKey_Apostrophe, 40},
                    {ImGuiKey_GraveAccent, 41},
                    {ImGuiKey_LeftShift, 42},
                    {ImGuiKey_Backslash, 43},
                    {ImGuiKey_Z, 44},
                    {ImGuiKey_X, 45},
                    {ImGuiKey_C, 46},
                    {ImGuiKey_V, 47},
                    {ImGuiKey_B, 48},
                    {ImGuiKey_N, 49},
                    {ImGuiKey_M, 50},
                    {ImGuiKey_Comma, 51},
                    {ImGuiKey_Period, 52},
                    {ImGuiKey_Slash, 53},
                    {ImGuiKey_RightShift, 54},
                    {ImGuiKey_KeypadMultiply, 55},
                    {ImGuiKey_LeftAlt, 56},
                    {ImGuiKey_Space, 57},
                    {ImGuiKey_CapsLock, 58},
                    {ImGuiKey_F1, 59},
                    {ImGuiKey_F2, 60},
                    {ImGuiKey_F3, 61},
                    {ImGuiKey_F4, 62},
                    {ImGuiKey_F5, 63},
                    {ImGuiKey_F6, 64},
                    {ImGuiKey_F7, 65},
                    {ImGuiKey_F8, 66},
                    {ImGuiKey_F9, 67},
                    {ImGuiKey_F10, 68},
                    {ImGuiKey_NumLock, 69},
                    {ImGuiKey_ScrollLock, 70},
                    {ImGuiKey_Keypad7, 71},
                    {ImGuiKey_Keypad8, 72},
                    {ImGuiKey_Keypad9, 73},
                    {ImGuiKey_KeypadSubtract, 74},
                    {ImGuiKey_Keypad4, 75},
                    {ImGuiKey_Keypad5, 76},
                    {ImGuiKey_Keypad6, 77},
                    {ImGuiKey_KeypadAdd, 78},
                    {ImGuiKey_Keypad1, 79},
                    {ImGuiKey_Keypad2, 80},
                    {ImGuiKey_Keypad3, 81},
                    {ImGuiKey_Keypad0, 82},
                    {ImGuiKey_KeypadDecimal, 83},
                    {ImGuiKey_F11, 87},
                    {ImGuiKey_F12, 88},
                    {ImGuiKey_F13, 100},
                    {ImGuiKey_F14, 101},
                    {ImGuiKey_F15, 102},
                    {ImGuiKey_KeypadEnter, 156},
                    {ImGuiKey_RightCtrl, 157},
                    {ImGuiKey_KeypadDivide, 181},
                    {ImGuiKey_PrintScreen, 183},
                    {ImGuiKey_RightAlt, 184},
                    {ImGuiKey_Pause, 197},
                    {ImGuiKey_Home, 199},
                    {ImGuiKey_UpArrow, 200},
                    {ImGuiKey_PageUp, 201},
                    {ImGuiKey_LeftArrow, 203},
                    {ImGuiKey_RightArrow, 205},
                    {ImGuiKey_End, 207},
                    {ImGuiKey_DownArrow, 208},
                    {ImGuiKey_PageDown, 209},
                    {ImGuiKey_Insert, 210},
                    {ImGuiKey_Delete, 211},
                    {ImGuiKey_LeftSuper, 219},
                    {ImGuiKey_RightSuper, 220},
                    {ImGuiKey_Menu, 221}};
} // namespace
uint32_t DrawTimeValue(void *, const EditorHost *, const EditorValueInput *input) {
    if (input->value.type != EDITOR_REAL_VALUE || !std::isfinite(input->value.real)) return EDITOR_INPUT_INVALID;
    Control ui(*input, 171);
    if (!ui.valid) return EDITOR_INPUT_INVALID;
    auto &time = ui.value.front().real;
    const double parts[] = {std::trunc(time / 60000), std::trunc(std::fmod(time, 60000) / 1000), std::fmod(time, 1000)};
    const char *ids[] = {"##min", "##s", "##ms"};
    const char *labels[] = {"Min:", "S:", "Ms:"};
    const float labelX[] = {5, 50, 90}, editX[] = {20, 60, 105}, factors[] = {60000, 1000, 1};
    for (unsigned i = 0; i < 3; ++i) {
        ui.Label(labelX[i], 0, editX[i] - 2, labels[i]);
        std::string text;
        char number[48];
        std::snprintf(number, sizeof(number), "%.12g", parts[i]);
        text = number;
        bool done = false;
        if (vi::Edit(ids[i], ui.P(editX[i], 0), ui.P(editX[i] + 25, 12), text, vi::CENTER, &done)) {
            double parsed = 0;
            ui.valid &= ParseRealValue(text, parsed);
            if (ui.valid) {
                time += (parsed - parts[i]) * factors[i];
                ui.changed = true;
            }
        }
        ui.Observe();
        ui.accept |= done;
    }
    return ui.Finish();
}
uint32_t DrawKeyValue(void *, const EditorHost *, const EditorValueInput *input) {
    if (input->value.type != EDITOR_INTEGER_VALUE) return EDITOR_INPUT_INVALID;
    Control ui(*input, 146);
    if (!ui.valid) return EDITOR_INPUT_INVALID;
    auto *storage = ImGui::GetStateStorage();
    const auto waitingKey = ImGui::GetID("key-capture");
    bool waiting = storage->GetBool(waitingKey);
    const bool fresh = ValueTargetChanged(*input);
    const bool abandoned = waiting && fresh;
    if (abandoned) waiting = false;
    std::string name = Display(*input);
    if (name.empty())
        for (const auto &key : keys)
            if (int64_t(input->value.integer) == key.scan) name = ImGui::GetKeyName(key.key);
    if (name.empty() || !input->value.integer) name = "Click Here and Push a Key";
    if (waiting) name = "Push a Key";
    const bool clicked = vi::Button("##key", ui.P(0, 1), ui.P(145, 14), name.c_str());
    const bool stopped = clicked && waiting;
    if (clicked) waiting = !waiting;
    const auto id = ImGui::GetItemID();
    if (abandoned || stopped || (waiting &&
        (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
         (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))))) {
        storage->SetBool(waitingKey, false);
        if (ImGui::GetActiveID() == id) ImGui::ClearActiveID();
        return EDITOR_INPUT_CANCEL;
    }
    if (waiting) {
        ImGui::SetActiveID(id, ImGui::GetCurrentWindow());
        ImGui::KeepAliveID(id);
        ImGui::GetCurrentContext()->ActiveIdUsingAllKeyboardKeys = true;
        ui.active = true;
        for (const auto &key : keys)
            if (ImGui::IsKeyPressed(key.key, false)) {
                ui.value.front().integer = key.scan;
                ui.changed = ui.accept = true;
                waiting = false;
                ImGui::SetKeyOwner(key.key, id, ImGuiInputFlags_LockUntilRelease);
                ImGui::ClearActiveID();
                break;
            }
    }
    storage->SetBool(waitingKey, waiting);
    return ui.Finish();
}
uint32_t DrawAnimationValue(void *, const EditorHost *host, const EditorValueInput *input) {
    if (input->value.type != EDITOR_OBJECT) return EDITOR_INPUT_INVALID;
    Control ui(*input, 216);
    if (!ui.valid) return EDITOR_INPUT_INVALID;
    Choices choices;
    if (!choices.Load(host, *input)) return EDITOR_INPUT_INVALID;
    const auto objects = choices.Objects();
    const auto &names = choices.Names();
    if (names.size() != objects.size()) return EDITOR_INPUT_INVALID;
    const int selected = ui.Combo("##objects", 15, 0, 210, Display(*input), names);
    if (selected >= 0) {
        ui.value.front().object = objects[size_t(selected)];
        ui.changed = ui.accept = true;
    }
    return ui.Finish();
}
uint32_t DrawScriptValue(void *, const EditorHost *host, const EditorValueInput *input) {
    if (input->value.type != EDITOR_OBJECT) return EDITOR_INPUT_INVALID;
    Control ui(*input, 216);
    if (!ui.valid) return EDITOR_INPUT_INVALID;
    const bool fresh = ValueTargetChanged(*input);
    auto *storage = ImGui::GetStateStorage();
    const auto classKey = ImGui::GetID("owner-class"), ownerKey = ImGui::GetID("owner-filter");
    if (fresh || SavedOwner("selected-script") != input->value.object) {
        storage->SetInt(classKey, 0);
        storage->SetBool(ownerKey, false);
    }
    SaveOwner(input->value.object, "selected-script");
    std::vector<EditorField> filters;
    if (const auto cid = storage->GetInt(classKey)) filters.push_back({"class", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(cid)});
    if (storage->GetBool(ownerKey)) filters.push_back({"owner", EDITOR_OBJECT, SavedOwner()});
    Choices choices;
    if (!choices.Load(host, *input, filters)) return EDITOR_INPUT_INVALID;
    auto owners = choices.Objects("owners");
    const auto &names = choices.Names("owner-labels");
    auto classes = choices.Integers("classes");
    const auto classNames = choices.Names("class-labels");
    const auto *owner = Field(choices.fields, "owner"), *cid = Field(choices.fields, "class");
    if (!owner || !cid || names.size() != owners.size() || classes.size() != classNames.size()) return EDITOR_INPUT_INVALID;
    std::string shown, classShown;
    for (size_t i = 0; i < owners.size(); ++i)
        if (owners[i] == owner->object) shown = names[i];
    for (size_t i = 0; i < classes.size(); ++i)
        if (classes[i] == cid->integer) classShown = classNames[i];
    int selected = ui.Combo("##owner", 15, 0, 135, shown, names);
    if (selected >= 0) {
        SaveOwner(owners[size_t(selected)]);
        storage->SetBool(ownerKey, true);
        filters.erase(std::remove_if(filters.begin(), filters.end(), [](const auto &field) { return field.key == "owner"; }),
                      filters.end());
        filters.push_back({"owner", EDITOR_OBJECT, owners[size_t(selected)]});
        if (!choices.Load(host, *input, filters)) return EDITOR_INPUT_INVALID;
        auto scripts = choices.Objects();
        ui.value.front().object = scripts.empty() ? ObjectRef{} : scripts.front();
        ui.changed = ui.accept = true;
    }
    selected = ui.Combo("##ownerclass", 135, 0, 210, classShown, classNames);
    if (selected >= 0) {
        storage->SetInt(classKey, int(classes[size_t(selected)]));
        storage->SetBool(ownerKey, true);
        SaveOwner({});
    }
    const auto scripts = choices.Objects();
    if (scripts.size() != choices.Names().size()) return EDITOR_INPUT_INVALID;
    selected = ui.Combo("##script", 15, 15, 210, Display(*input), choices.Names());
    if (selected >= 0) {
        ui.value.front().object = scripts[size_t(selected)];
        ui.changed = ui.accept = true;
    }
    return ui.Finish();
}
uint32_t DrawMessageValue(void *, const EditorHost *host, const EditorValueInput *input) {
    if (input->value.type != EDITOR_RECORD) return EDITOR_INPUT_INVALID;
    Control ui(*input, 216);
    if (!ui.valid) return EDITOR_INPUT_INVALID;
    Choices choices;
    if (!choices.Load(host, *input)) return EDITOR_INPUT_INVALID;
    auto *id = ui.Member("id"), *name = ui.Member("name");
    if (!id || !name) return EDITOR_INPUT_INVALID;
    auto text = name->text;
    if (ui.EditCombo("##message", text, choices.Names())) {
        id->integer = uint64_t(int64_t(-1));
        name->text = text;
        const auto values = choices.Integers();
        if (values.size() != choices.Names().size()) return EDITOR_INPUT_INVALID;
        for (size_t i = 0; i < values.size(); ++i)
            if (text == choices.Names()[i]) id->integer = values[i];
        ui.changed = ui.accept = true;
    }
    return ui.Finish();
}
uint32_t DrawAttributeValue(void *, const EditorHost *host, const EditorValueInput *input) {
    if (input->value.type != EDITOR_INTEGER_VALUE) return EDITOR_INPUT_INVALID;
    Control ui(*input, 221);
    if (!ui.valid) return EDITOR_INPUT_INVALID;
    auto *storage = ImGui::GetStateStorage();
    const auto categoryKey = ImGui::GetID("attribute-category");
    const bool fresh = ValueTargetChanged(*input);
    if (fresh) storage->SetInt(categoryKey, -1);
    Choices choices;
    if (!choices.Load(host, *input)) return EDITOR_INPUT_INVALID;
    const auto categories = choices.Names("categories");
    const int category = storage->GetInt(categoryKey, -1);
    if (category >= 0 && size_t(category) < categories.size())
        if (!choices.Load(host, *input, {{"category", EDITOR_TEXT_VALUE, {}, categories[size_t(category)]}})) return EDITOR_INPUT_INVALID;
    auto values = choices.Integers();
    if (values.size() != choices.Names().size()) return EDITOR_INPUT_INVALID;
    int selected = ui.Combo("##attribute", 15, 0, 210, Display(*input), choices.Names());
    if (selected >= 0) {
        ui.value.front().integer = values[size_t(selected)];
        ui.changed = ui.accept = true;
    }
    const auto *shown = Field(choices.fields, "category");
    selected = ui.Combo("##category", 15, 15, 170, shown ? shown->text : "", categories);
    if (selected >= 0) storage->SetInt(categoryKey, selected);
    ui.Label(170, 15, 208, "Category");
    return ui.Finish();
}
uint32_t DrawSynchroValue(void *, const EditorHost *host, const EditorValueInput *input) {
    if (input->value.type != EDITOR_RECORD) return EDITOR_INPUT_INVALID;
    Control ui(*input, 216);
    if (!ui.valid) return EDITOR_INPUT_INVALID;
    Choices choices;
    if (!choices.Load(host, *input)) return EDITOR_INPUT_INVALID;
    auto *object = ui.Member("object"), *name = ui.Member("name");
    if (!object || !name) return EDITOR_INPUT_INVALID;
    const auto refs = choices.Objects();
    const auto users = choices.Integers("users");
    if (refs.size() != choices.Names().size() || refs.size() != users.size()) return EDITOR_INPUT_INVALID;
    auto shown = name->text;
    if (object->object)
        for (size_t i = 0; i < refs.size(); ++i)
            if (refs[i] == object->object) shown = choices.Names()[i];
    std::vector<std::string> names;
    for (size_t i = 0; i < refs.size(); ++i)
        if (refs[i]) names.push_back(choices.Names()[i]);
    if (ui.EditCombo("##synchro", shown, names)) {
        object->object = {};
        name->text = shown;
        auto &members = ui.value.front().fields;
        members.erase(std::remove_if(members.begin(), members.end(), [](const auto &field) { return field.key == "set-users"; }),
                      members.end());
        object = ui.Member("object");
        name = ui.Member("name");
        for (size_t i = 0; i < refs.size(); ++i)
            if (refs[i] && shown == choices.Names()[i]) {
                object->object = refs[i];
                name->text.clear();
                auto *count = ui.Member("users");
                if (count) count->integer = users[i];
                break;
            }
        if (shown.empty()) {
            name->text.clear();
            auto &members = ui.value.front().fields;
            members.erase(std::remove_if(members.begin(), members.end(),
                                         [](const auto &field) { return field.key == "users" || field.key == "set-users"; }),
                          members.end());
        }
        ui.changed = ui.accept = true;
    }
    object = ui.Member("object"); name = ui.Member("name");
    const auto *usersEditable = Field(choices.fields, "has-users");
    const bool hasUsers = usersEditable && usersEditable->type == EDITOR_INTEGER_VALUE && usersEditable->integer;
    ui.Label(15, 15, 150, "Number Of Users:");
    if (hasUsers) {
        ImGui::BeginDisabled(!object->object && name->text.empty());
        auto *count = ui.Member("users");
        int32_t number = count ? int32_t(int64_t(count->integer)) : 0;
        if (ui.Integer("##users", 155, 15, 210, number)) {
            if (count) count->integer = uint64_t(int64_t(number));
            else ui.value.front().fields.push_back({"users", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(number))});
            if (!ui.Member("set-users")) ui.value.front().fields.push_back({"set-users", EDITOR_INTEGER_VALUE, {}, {}, 1});
        }
        ImGui::EndDisabled();
    }
    return ui.Finish();
}
} // namespace vdev
