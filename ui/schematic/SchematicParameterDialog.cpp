#include "ui/schematic/SchematicParameterDialog.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/ObjectArrayInput.h"
#include "ui/setup/SetupLayout.h"
#include <algorithm>
#include <optional>

namespace vdev {
namespace {
const EditorField *Field(const std::vector<EditorField> &fields, const char *key) {
    for (const auto &field : fields)
        if (field.key == key)
            return &field;
    return nullptr;
}
EditorField *Field(std::vector<EditorField> &fields, const char *key) {
    for (auto &field : fields)
        if (field.key == key)
            return &field;
    return nullptr;
}
std::string Text(const std::vector<EditorField> &fields, const char *key) {
    const auto *field = Field(fields, key);
    return field ? field->text : std::string();
}
uint64_t Number(const std::vector<EditorField> &fields, const char *key) {
    const auto *field = Field(fields, key);
    return field ? field->integer : 0;
}
EditorField Integer(const char *key, uint64_t value) { return {key, EDITOR_INTEGER_VALUE, {}, {}, value}; }
constexpr float DX = 1.5f, DY = 1.625f;
} // namespace
struct SchematicParameterDialog::Data {
    EditorApplication &app;
    EditorClient client;
    ObjectRef script{}, object{};
    unsigned mode = 0;
    EditorEdit edit = 0;
    EditorOperation operation = 0;
    bool open = false, shown = false, closeAfter = false, valid = true, inputActive = false;
    bool confirming = false, pickingClick = false, pickingToggle = false;
    int pickingFrame = 0;
    std::string message, title, typeName, display;
    bool nameEditable = false, typeEditable = false, apply = false;
    EditorValueEditors editors;
    PropertyBinding nameProperty;
    std::optional<EditorField> nameBefore;
    std::vector<EditorField> arguments, types, invalidLinks;
    EditorField value;
    std::optional<EditorField> pickRequest;
    struct Row {
        std::string key, label, layout, property, filter;
        ObjectRef target{};
        float width = 0, height = 0;
        unsigned pick = 0, pickClass = 0, filterClass = 0, representation = 0;
        bool expand = false, extended = false, collapsed = false, derived = true, invalid = false;
        std::vector<EditorField> fields, classes;
        std::vector<Row> members;
        std::optional<EditorField> before;
        ImVec2 min{}, max{};
        ObjectArrayInput arrayInput;
        PropertyBinding binding;
    };
    std::vector<Row> rows;
    ObjectRef picking{};
    explicit Data(EditorApplication &app, const EditorHost *host, EditorView view) : app(app), client(host, view) {}
    const char *Id() const { return mode == 0 ? "###EditParameter" : mode == 1 ? "###EditParameters" : "###Settings"; }
    bool SameScope() const { return client.Scope().document == script.document; }
    void Close() {
        client.CancelEdit(edit);
        operation = 0;
        open = shown = confirming = false;
        picking = {};
        pickingClick = pickingToggle = false;
        valid = true;
        inputActive = closeAfter = false;
        nameBefore.reset();
        pickRequest.reset();
        nameProperty = {};
        rows.clear();
        arguments.clear();
        types.clear();
        invalidLinks.clear();
        value = {};
        editors.clear();
    }
    Row ReadRow(const EditorField &field, std::vector<Row> &previous) {
        Row row;
        const auto *target = Field(field.fields, "target");
        if (target)
            row.target = target->object;
        const auto *metadata = Field(field.fields, "fields");
        bool restored = false;
        for (auto &old : previous)
            if (old.target == row.target && old.key == field.key && metadata &&
                Number(old.fields, "type") == Number(metadata->fields, "type")) {
                row = std::move(old);
                restored = true;
                break;
            }
        row.key = field.key;
        row.label = Text(field.fields, "label");
        row.property = Text(field.fields, "property");
        row.layout = Text(field.fields, "layout");
        row.width = float(Number(field.fields, "width"));
        row.height = float(Number(field.fields, "height"));
        row.pick = unsigned(Number(field.fields, "pick"));
        row.pickClass = unsigned(Number(field.fields, "pick-class"));
        if (!restored) {
            row.filterClass = row.pickClass;
            row.extended = Number(field.fields, "extended") != 0;
        }
        row.expand = Number(field.fields, "expand") != 0;
        if (const auto *fields = Field(field.fields, "fields"))
            row.fields = fields->fields;
        if (const auto *classes = Field(field.fields, "classes"))
            row.classes = classes->fields;
        if (!restored)
            if (const auto *binding = app.Property(row.property))
                row.binding = *binding;
        if (!restored && row.binding.editor.empty())
            row.binding.editor = Text(field.fields, "editor");
        auto oldMembers = std::move(row.members);
        row.members.clear();
        if (const auto *members = Field(field.fields, "members"))
            for (const auto &member : members->fields)
                row.members.push_back(ReadRow(member, oldMembers));
        return row;
    }
    bool Refresh() {
        const auto result = client.Query(
            "schematic.parameter-editor",
            {ObjectArgument("target", script), ObjectArgument("object", object), IntegerArgument("mode", mode)});
        if (result.status != EDITOR_OK) {
            message = result.message.empty() ? "The parameter is no longer available" : result.message;
            return false;
        }
        const auto *args = EditorClient::Find(result, "arguments"), *items = EditorClient::Find(result, "rows");
        const auto *candidate = args ? Field(args->fields, "value") : nullptr;
        if (!candidate || !items)
            return false;
        arguments = args->fields;
        value = *candidate;
        title = EditorClient::Text(result, "title");
        typeName = EditorClient::Text(result, "type-name");
        display = EditorClient::Text(result, "display");
        nameEditable = EditorClient::Integer(result, "name-editable");
        typeEditable = EditorClient::Integer(result, "type-editable");
        apply = EditorClient::Integer(result, "apply");
        if (const auto *links = EditorClient::Find(result, "invalid-links"))
            invalidLinks = links->fields;
        auto previous = std::move(rows);
        rows.clear();
        for (const auto &item : items->fields)
            rows.push_back(ReadRow(item, previous));
        return true;
    }
    bool Submit(const EditorField *candidate = nullptr) {
        const std::vector<EditorField> values{candidate ? *candidate : value};
        const auto borrowed = BorrowEditorFields(values);
        const auto status = client.Host()->editValue(client.Host()->data, edit, borrowed.data());
        valid = status == EDITOR_OK || status == EDITOR_NO_CHANGE;
        if (!valid) {
            message = "The parameter value was refused";
            client.CancelEdit(edit);
            open = false;
        }
        return valid;
    }
    bool Begin() {
        if (!Refresh())
            return false;
        const auto values = BorrowEditorFields(arguments);
        if (client.BeginCommand("schematic.edit-parameters", {values.data(), values.data() + values.size()}, edit) !=
                EDITOR_OK ||
            !Submit())
            return false;
        return Refresh();
    }
    bool Open(ObjectRef targetScript, ObjectRef targetObject, unsigned targetMode) {
        Close();
        script = targetScript;
        object = targetObject;
        mode = targetMode;
        editors = app.CaptureValueEditors();
        if (const auto *p = app.Property("object.name"))
            nameProperty = *p;
        if (!Begin()) {
            client.CancelEdit(edit);
            return false;
        }
        if (mode != 0 && rows.empty()) {
            Close();
            message = "No parameter to edit (the inputs are linked)";
            return false;
        }
        types.clear();
        if (typeEditable)
            if (const auto result = client.Query("schematic.parameter-types"); result.status == EDITOR_OK)
                if (const auto *items = EditorClient::Find(result, "types"))
                    types = items->fields;
        open = true;
        return true;
    }
    ImVec2 UiSize(const Row &r, const EditorField &v, float s) const {
        const auto dlu = [s](float w, float h) { return ImVec2(w * DX * s, h * DY * s); };
        const auto &kind = r.layout;
        if (kind == "angle")
            return dlu(216, 18);
        if (kind == "euler")
            return {216 * DX * s, 69 * s};
        if (kind == "object")
            return dlu(219, r.extended ? 43 : 14);
        if (kind == "string")
            return r.extended ? ImVec2(220 * DX * s + 5 * s, 205 * s) : dlu(231, 14);
        if (kind == "matrix")
            return r.collapsed ? ImVec2(319 * DX * s + 5 * s, 18 * DY * s) : dlu(336, 57);
        if (kind == "flags")
            return r.collapsed ? ImVec2(56 * DX * s + 5 * s, 11 * DY * s + 5 * s)
                               : ImVec2(169 * DX * s + 5 * s, (2 + 10 * Number(r.fields, "rows")) * DY * s + 5 * s);
        if (kind == "dependencies")
            return {296 * DX * s, 196 * DY * s - (Number(v.fields, "mode") ? 260 * s : 0)};
        if (kind == "objects")
            return dlu(216, 105);
        if (kind == "lod")
            return dlu(231, 55);
        if (kind == "curve")
            return r.collapsed ? ImVec2(206 * s, 46 * s) : dlu(213, 169);
        if (kind == "structure")
            return RowsSize(r.members, v.fields, s, false);
        if (kind == "none")
            return dlu(231, 14);
        return dlu(r.width, r.height);
    }
    float Labels(const std::vector<Row> &items, float s, bool minimum) const {
        float width = minimum ? 100 * s : 0;
        for (const auto &row : items)
            width = std::max(width, vi::TextWidth(row.label.c_str(), true) + 2 * s);
        return width;
    }
    ImVec2 RowsSize(const std::vector<Row> &items, const std::vector<EditorField> &values, float s,
                    bool minimum) const {
        ImVec2 result{};
        const auto label = Labels(items, s, minimum);
        for (const auto &row : items) {
            const auto *v = Field(values, row.key.c_str());
            if (!v)
                continue;
            const auto size = UiSize(row, *v, s);
            result.x = std::max(result.x, 14 * DX * s + label + 2 * s + size.x);
            result.y += std::max(size.y, (row.expand && row.pick ? 15 : 11) * DY * s) + 2 * s +
                        (&row != &items.front() ? 1 : 0);
        }
        return result;
    }
    uint32_t DrawValue(Row &row, EditorField &v, ImVec2 at, float s, float width) {
        if (row.binding.editor.empty()) {
            ImGui::SetCursorScreenPos(at);
            ImGui::TextWrapped("%s", Text(row.fields, "display").c_str());
            return 0;
        }
        auto fields = row.fields;
        fields.push_back(Integer("representation",
                                 row.layout == "rect" ? row.representation : unsigned(row.collapsed || row.extended)));
        const std::vector<EditorField> owned{v};
        const auto values = BorrowEditorFields(owned), metadata = BorrowEditorFields(fields);
        const auto original = v;
        struct Update {
            EditorField *value;
            bool changed = false;
        } update{&v};
        const auto write = [](void *data, const EditorValue *value, uint32_t count) {
            auto &u = *static_cast<Update *>(data);
            std::vector<EditorField> fields;
            if (count != 1 || CopyEditorFields(fields, value, count) != EDITOR_OK)
                return EDITOR_INVALID;
            fields[0].key = u.value->key;
            *u.value = std::move(fields[0]);
            u.changed = true;
            return EDITOR_OK;
        };
        const EditorValue choices[] = {ObjectArgument("target", row.target),
                                       IntegerArgument("class", row.extended ? row.filterClass : row.pickClass),
                                       IntegerArgument("derived", row.derived ? 1 : 2),
                                       TextArgument("filter", row.filter)};
        struct Members {
            Data *self;
            Row *row;
            EditorField *value;
            ImVec2 at;
            float scale, width;
            bool used = false;
        } members{this, &row, &v, at, s, width};
        const auto draw = [](void *data, const EditorHost *host, const EditorValueInput *input) {
            auto &m = *static_cast<Members *>(data);
            if (m.row->layout == "objects") {
                ImGui::SetCursorScreenPos(m.at);
                return m.row->arrayInput.Draw(host, *input);
            }
            m.used = true;
            return m.self->DrawRows(m.row->members, m.value->fields, m.at, m.scale, false, m.width);
        };
        auto size = UiSize(row, v, s);
        size.x = width;
        const auto &kind = row.layout;
        const float height = std::max(ImGui::GetFrameHeight(), 19.5f * s);
        const char *label = "##components";
        if (kind == "number" || kind == "enum" || kind == "bool" || kind == "string" || kind == "text" ||
            kind == "object") {
            size.y = kind == "bool" ? 12 * s : kind == "string" && row.extended ? 200 * s : height;
            label = kind == "number"   ? "##number"
                    : kind == "enum"   ? "##enum"
                    : kind == "bool"   ? "##bool"
                    : kind == "object" ? "##object"
                    : kind == "text"   ? "##text"
                                       : "##string";
        } else if (kind == "flags")
            label = "##flags";
        ImGui::SetCursorScreenPos(at);
        std::optional<setup::SetupForm> objectForm;
        if (kind == "object") {
            objectForm.emplace("##object-fields", width);
            if (!*objectForm)
                return 0;
            objectForm->Row("Object");
            size.x = std::max(1.f, ImGui::GetContentRegionAvail().x);
        }
        const EditorValueInput input{sizeof(input),
                                     0,
                                     EditorLiteral(label),
                                     values.values.front(),
                                     metadata.data(),
                                     uint32_t(metadata.size()),
                                     0,
                                     {&update, write},
                                     EDITOR_VALUE_DIALOG,
                                     size.x,
                                     size.y,
                                     EditorText(row.binding.choices),
                                     {client.Scope(), choices, 4, EditorText(row.binding.choicesProvider)},
                                     (kind == "structure" || kind == "objects") ? draw : nullptr,
                                     &members};
        const auto chosen = editors.find(row.binding.editor);
        auto action = chosen == editors.end() ? EDITOR_INPUT_INVALID : app.DrawValue(*chosen->second, input, &editors);
        if (!members.used && !row.before && (update.changed || (action & EDITOR_INPUT_ACTIVE)))
            row.before = original;
        if (row.before) {
            if (action & EDITOR_INPUT_CANCEL) {
                v = *row.before;
                row.before.reset();
                action |= EDITOR_INPUT_CHANGED | EDITOR_INPUT_ACTIVE;
                row.invalid = false;
            } else if (!(action & EDITOR_INPUT_INVALID) &&
                       ((action & (EDITOR_INPUT_ACCEPT | EDITOR_INPUT_DEACTIVATED)) ||
                        !(action & (EDITOR_INPUT_ACTIVE | EDITOR_INPUT_CHANGED))))
                row.before.reset();
        }
        if (action & EDITOR_INPUT_INVALID)
            row.invalid = true;
        else if (action & EDITOR_INPUT_CHANGED)
            row.invalid = false;
        if (kind == "object" && row.extended) {
            objectForm->Row("Class");
            int index = -1;
            std::vector<std::string> names;
            for (size_t i = 0; i < row.classes.size(); ++i) {
                names.push_back(Text(row.classes[i].fields, "name"));
                if (Number(row.classes[i].fields, "id") == row.filterClass)
                    index = int(i);
            }
            const auto chosenClass =
                vi::Combo("##class", index >= 0 ? names[size_t(index)] : "", [&] { return names; }, index);
            if (chosenClass >= 0)
                row.filterClass = unsigned(Number(row.classes[size_t(chosenClass)].fields, "id"));
            objectForm->Row("Derived");
            vi::Check("##derived", ImGui::GetCursorScreenPos(), 12 * s, row.derived);
            objectForm->Row("Text Filter");
            vi::Edit("##filter", row.filter, vi::CENTER);
            objectForm->Row("");
            if (vi::Button("##showsetup", "Show Setup", bool(v.object)))
                client.Command("object.open-setup", {ObjectArgument("target", v.object)});
        }
        return action;
    }
    uint32_t DrawRows(std::vector<Row> &items, std::vector<EditorField> &values, ImVec2 at, float s,
                      bool minimum, float available = 0) {
        uint32_t action = 0;
        const auto labels = Labels(items, s, minimum);
        const float width = std::max(1.f, available > 0 ? available : ImGui::GetContentRegionAvail().x);
        const float uiX = 14 * DX * s + labels + 2 * s;
        const float uiWidth = std::max(100.f * s, width - uiX);
        float y = at.y, measuredWidth = width;
        auto *dl = ImGui::GetWindowDrawList();
        ImDrawListSplitter paint;
        paint.Split(dl, 2);
        ImGui::SetCursorScreenPos(at);
        ImGui::BeginGroup();
        for (size_t i = 0; i < items.size(); ++i) {
            auto &row = items[i];
            auto *v = Field(values, row.key.c_str());
            if (!v) {
                action |= EDITOR_INPUT_INVALID;
                continue;
            }
            if (i)
                y += 1;
            paint.SetCurrentChannel(dl, 1);
            ImGui::PushID(int(i));
            const auto button = [&](const char *id, float dy, const int icons[3], bool pushed) {
                const ImVec2 p(at.x + 3 * DX * s, y + dy * DY * s);
                ImGui::SetCursorScreenPos(p);
                const bool click = ImGui::InvisibleButton(id, {11 * s, 11 * s});
                if (auto icon = CK2UIBitmap(pushed || ImGui::IsItemActive() ? icons[2]
                                            : ImGui::IsItemHovered()        ? icons[1]
                                                                            : icons[0]))
                    dl->AddImage(icon, p, {p.x + 11 * s, p.y + 11 * s});
                return click;
            };
            static const int expandIcons[] = {2006, 2008, 2003}, pickIcons[] = {2007, 2005, 2004};
            if (row.expand && button("##expand", 1, expandIcons, false)) {
                if (row.layout == "rect")
                    row.representation = (row.representation + 1) % 3;
                else if (row.layout == "matrix" || row.layout == "flags" || row.layout == "curve")
                    row.collapsed = !row.collapsed;
                else
                    row.extended = !row.extended;
            }
            if (row.pick) {
                button("##pick", row.expand ? 8 : 1, pickIcons, picking == row.target);
                if (ImGui::IsItemActivated()) {
                    pickingToggle = picking == row.target && pickingClick;
                    picking = row.target;
                    pickingClick = false;
                }
                if (ImGui::IsItemDeactivated() && picking == row.target) {
                    if (ImGui::IsItemHovered()) {
                        pickingClick = !pickingToggle;
                        pickingFrame = ImGui::GetFrameCount();
                        if (pickingToggle)
                            picking = {};
                    } else {
                        Pick(row, ImGui::GetIO().MousePos);
                        picking = {};
                        action |= EDITOR_INPUT_CHANGED;
                    }
                }
            }
            const float uiY = y + s;
            ImGui::SetCursorScreenPos({at.x + uiX, uiY});
            ImGui::BeginGroup();
            action |= DrawValue(row, *v, {at.x + uiX, uiY}, s, uiWidth);
            ImGui::EndGroup();
            const auto size = ImGui::GetItemRectSize();
            const float side = (row.expand && row.pick ? 15 : 11) * DY * s;
            const float height = std::max(size.y, side) + 2 * s;
            const float right = std::max(width, uiX + std::max(uiWidth, size.x));
            vi::Text({at.x + 14 * DX * s, uiY}, {at.x + uiX - 2 * s, uiY + 13 * s}, row.label.c_str(), true);
            ImGui::PopID();
            row.min = {at.x, y};
            row.max = {at.x + right, y + height};
            measuredWidth = std::max(measuredWidth, right);
            paint.SetCurrentChannel(dl, 0);
            if (i) {
                dl->AddRectFilled({at.x, y - 1}, {row.max.x, y}, vi::Color(7));
                dl->AddLine({at.x + uiX, y - .5f}, {row.max.x, y - .5f}, vi::Color(6));
            }
            dl->AddRectFilled(row.min, {at.x + uiX - 1, row.max.y}, vi::Color(6));
            dl->AddLine({at.x + uiX - .5f, y}, {at.x + uiX - .5f, row.max.y}, vi::Color(0));
            dl->AddRectFilled({at.x + uiX, y}, row.max, vi::Color(7));
            y = row.max.y;
        }
        paint.Merge(dl);
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({measuredWidth, y - at.y});
        ImGui::EndGroup();
        return action;
    }
    Row *FindRow(std::vector<Row> &items, ObjectRef target, std::vector<uint64_t> *path = nullptr) {
        for (size_t i = 0; i < items.size(); ++i) {
            if (path)
                path->push_back(i);
            if (items[i].target == target)
                return &items[i];
            if (auto *found = FindRow(items[i].members, target, path))
                return found;
            if (path)
                path->pop_back();
        }
        return nullptr;
    }
    Row *RowAt(std::vector<Row> &items, ImVec2 point) {
        for (auto &row : items) {
            if (auto *member = RowAt(row.members, point))
                return member;
            if (row.target != picking && ImRect(row.min, row.max).Contains(point))
                return &row;
        }
        return nullptr;
    }
    bool Pick(Row &row, ImVec2 mouse) {
        ObjectRef picked{};
        if ((row.pick & 1) && ImGui::GetCurrentWindow()->InnerClipRect.Contains(mouse))
            if (auto *other = RowAt(rows, mouse))
                picked = other->target;
        if (!picked) {
            EditorField point{"point", EDITOR_REAL_LIST};
            point.reals = {mouse.x, mouse.y};
            const std::vector<EditorField> args{point, Integer("class", row.pickClass)};
            const auto borrowed = BorrowEditorFields(args);
            const auto result =
                client.Query("parameter.object-at", {borrowed.data(), borrowed.data() + borrowed.size()});
            if (const auto *target = EditorClient::Find(result, "target"))
                picked = target->object;
        }
        if (!picked)
            return false;
        std::vector<uint64_t> path;
        if (!FindRow(rows, row.target, &path) || path.empty())
            return false;
        EditorField pick{"pick", EDITOR_RECORD}, nested{"path", EDITOR_INTEGER_LIST};
        nested.integers.assign(path.begin() + 1, path.end());
        pick.fields = {Integer("row", path.front()),
                       std::move(nested),
                       {"object", EDITOR_OBJECT, picked},
                       Integer("options", (ImGui::GetIO().KeyShift ? 1 : 0) | (row.derived ? 0 : 2)),
                       Integer("class", row.extended ? row.filterClass : row.pickClass),
                       {"filter", EDITOR_TEXT_VALUE, {}, row.extended ? row.filter : ""}};
        pickRequest = std::move(pick);
        return true;
    }
    void Finish(bool close) {
        if (!edit || !valid)
            return;
        closeAfter = close;
        const auto status = client.Host()->finishEdit(client.Host()->data, edit, 1, &operation);
        if (status == EDITOR_REFUSED) {
            message = "Finish the current operation before applying parameters";
            return;
        }
        if (status == EDITOR_QUEUED)
            edit = 0;
        else
            client.CancelEdit(edit);
        if (status != EDITOR_QUEUED) {
            operation = 0;
            if (status != EDITOR_OK && status != EDITOR_NO_CHANGE)
                message = "Editing parameters failed";
            if (status == EDITOR_OK || status == EDITOR_NO_CHANGE) {
                if (close) {
                    open = false;
                    ImGui::CloseCurrentPopup();
                } else if (!Begin())
                    open = false;
            } else if (!Begin())
                open = false;
        }
    }
    bool AllValid(const std::vector<Row> &items) const {
        return std::all_of(items.begin(), items.end(),
                           [&](const auto &r) { return !r.invalid && AllValid(r.members); });
    }
    void Draw() {
        if (!open)
            return;
        if (!SameScope()) {
            Close();
            return;
        }
        if (operation) {
            const auto result = client.Completion(operation);
            if (result.status != EDITOR_QUEUED) {
                operation = 0;
                const bool ok = result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE;
                if (!ok)
                    message = result.message.empty() ? "Editing parameters failed" : result.message;
                if (ok && closeAfter) {
                    open = false;
                } else if (!Begin())
                    open = false;
            }
        }
        if (!shown) {
            ImGui::OpenPopup(Id());
            shown = true;
        }
        if (!operation && open && !Refresh()) {
            client.CancelEdit(edit);
            open = false;
        }
        if (!ImGui::IsPopupOpen(Id())) {
            Close();
            return;
        }
        auto *values = Field(value.fields, "rows");
        if (!values) {
            Close();
            return;
        }
        const auto s = setup::PageUnit();
        const bool single = mode == 0;
        const auto size = RowsSize(rows, values->fields, s, !single);
        vi::DialogLayout layout;
        if (!vi::BeginScrollableDialog(Id(), title,
                                        {size.x + 4 * s, size.y + (single ? (rows.empty() ? 74 : 53) : 4) * s}, s,
                                        layout))
            return;
        ImGui::BeginDisabled(operation || !open);
        bool changed = false, changedType = false;
        inputActive = false;
        const bool captured = bool(picking);
        uint32_t action = 0;
        if (vi::BeginDialogContent("##parameter-values", layout)) {
            if (single) {
                setup::SetupForm form("##parameter-info");
                if (form) {
                    auto *name = Field(value.fields, "name");
                    form.Row("Parameter Name :");
                    if (nameEditable && name) {
                        const auto owned = std::vector<EditorField>{*name};
                        const auto values = BorrowEditorFields(owned);
                        struct Update {
                            EditorField *name;
                            bool *changed;
                        } update{name, &changed};
                        const auto write = [](void *data, const EditorValue *v, uint32_t n) {
                            auto &u = *static_cast<Update *>(data);
                            std::vector<EditorField> fields;
                            if (n != 1 || v->type != EDITOR_TEXT_VALUE || CopyEditorFields(fields, v, n) != EDITOR_OK)
                                return EDITOR_INVALID;
                            fields[0].key = "name";
                            *u.name = std::move(fields[0]);
                            *u.changed = true;
                            return EDITOR_OK;
                        };
                        const EditorValueInput input{sizeof(input),
                                                     0,
                                                     EDITOR_TEXT("##name"),
                                                     values.values.front(),
                                                     nullptr,
                                                     0,
                                                     0,
                                                     {&update, write},
                                                     EDITOR_VALUE_DIALOG,
                                                     std::max(1.f, ImGui::GetContentRegionAvail().x),
                                                     16 * s};
                        const auto editor = editors.find(nameProperty.editor);
                        const auto action = editor == editors.end()
                                                ? EDITOR_INPUT_INVALID
                                                : app.DrawValue(*editor->second, input, &editors);
                        if (!nameBefore && (changed || (action & EDITOR_INPUT_ACTIVE)))
                            nameBefore = owned.front();
                        if (nameBefore && (action & EDITOR_INPUT_CANCEL)) {
                            *name = *nameBefore;
                            nameBefore.reset();
                            changed = true;
                            inputActive = true;
                        } else {
                            inputActive = action & EDITOR_INPUT_ACTIVE;
                            if (action & (EDITOR_INPUT_ACCEPT | EDITOR_INPUT_DEACTIVATED))
                                nameBefore.reset();
                        }
                    } else
                        ImGui::TextWrapped("%s", name ? name->text.c_str() : "");
                    form.Row("Parameter Type :");
                    if (typeEditable) {
                        std::vector<std::string> names;
                        for (const auto &type : types)
                            names.push_back(Text(type.fields, "name"));
                        const auto at = ImGui::GetCursorScreenPos();
                        const ImVec2 max{at.x + std::max(1.f, ImGui::GetContentRegionAvail().x), at.y + 16 * s};
                        const auto selected =
                            vi::Combo("##type", at, max, typeName, [&] { return names; }, -1);
                        if (selected >= 0) {
                            Field(value.fields, "type")->integer = Number(types[size_t(selected)].fields, "type");
                            values->fields.clear();
                            changed = changedType = true;
                        }
                    } else
                        ImGui::TextWrapped("%s", typeName.c_str());
                    if (rows.empty()) {
                        form.Row("Parameter Value :");
                        ImGui::TextWrapped("%s", display.c_str());
                    }
                }
            }
            if (picking) {
                const auto mouse = ImGui::GetIO().MousePos;
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::GetForegroundDrawList()->AddCircle(mouse, 6 * s, vi::Color(10), 0, 2);
                if (auto *row = FindRow(rows, picking))
                    changed = Pick(*row, mouse) || changed;
                if (pickingClick && ImGui::GetFrameCount() > pickingFrame &&
                    (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1) || ImGui::IsKeyPressed(ImGuiKey_Escape)))
                    picking = {};
            }
            vi::BlockDialogButtons(captured || changedType);
            ImGui::BeginDisabled(captured && pickingClick);
            const auto at = ImGui::GetCursorScreenPos();
            action = DrawRows(rows, values->fields, at, s, !single);
            ImGui::EndDisabled();
            const auto measured = ImGui::GetItemRectSize();
            if (single && !rows.empty())
                ImGui::GetWindowDrawList()->AddRect({at.x - s, at.y - s},
                                                    {at.x + measured.x + s, at.y + measured.y + s}, vi::Color(0));
        }
        vi::EndDialogContent();
        vi::BlockDialogButtons(captured || changedType);
        inputActive |=
            (action & EDITOR_INPUT_ACTIVE) || captured || changedType ||
            ImGui::GetCurrentContext()->OpenPopupStack.Size > ImGui::GetCurrentContext()->BeginPopupStack.Size;
        changed |= action & EDITOR_INPUT_CHANGED;
        if ((changed || pickRequest) && !operation) {
            auto candidate = value;
            if (pickRequest)
                candidate.fields.push_back(std::move(*pickRequest));
            pickRequest.reset();
            if (Submit(&candidate))
                Refresh();
        }
        float x = layout.right - 60 * s;
        const auto y = layout.buttonsY;
        const bool cancel =
            !open || (vi::DialogButton("Cancel", {x, y}, s, ImGuiKey_Escape) && !inputActive && !confirming);
        ImGui::BeginDisabled(!valid || !AllValid(rows));
        if (apply) {
            x -= 70 * s;
            if (vi::DialogButton("Apply", {x, y}, s) && !inputActive)
                Finish(false);
        }
        x -= 70 * s;
        if (vi::DialogButton("OK", {x, y}, s, ImGuiKey_Enter) && !inputActive) {
            if (single && !invalidLinks.empty()) {
                confirming = true;
                ImGui::OpenPopup("Invalid Parameter Link");
            } else
                Finish(true);
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        vi::DialogLayout confirmation;
        if (confirming && vi::BeginScrollableDialog("Invalid Parameter Link", "Invalid Parameter Link",
                                                    {400 * s, 70 * s}, s, confirmation)) {
            const bool keys = !ImGui::IsWindowAppearing();
            if (vi::BeginDialogContent("##invalid-parameter-link", confirmation)) {
                ImGui::TextWrapped("Parameter Type is incompatible with Parameter "
                                   "Input Type.\nThis link must be deleted.");
                if (invalidLinks.size() > 1) ImGui::Text("(%d links)", int(invalidLinks.size()));
            }
            vi::EndDialogContent();
            ImGui::SetCursorScreenPos({confirmation.right - 170 * s, confirmation.buttonsY});
            if (ImGui::Button("OK", {80 * s, 18 * s}) || (keys && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
                confirming = false;
                ImGui::CloseCurrentPopup();
                Field(value.fields, "delete-links")->integer = 1;
                if (Submit())
                    Finish(true);
            }
            ImGui::SameLine(0, 10 * s);
            if (ImGui::Button("Cancel", {80 * s, 18 * s}) || (keys && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
                confirming = false;
                ImGui::CloseCurrentPopup();
                open = false;
            }
            vi::EndDialog();
        }
        if (cancel || !open) {
            client.CancelEdit(edit);
            ImGui::CloseCurrentPopup();
            open = false;
        }
        vi::EndDialog();
        if (!open) {
            rows.clear();
            editors.clear();
            picking = {};
        }
    }
};
SchematicParameterDialog::SchematicParameterDialog(EditorApplication &app, const EditorHost *host, EditorView view)
    : m_Data(std::make_unique<Data>(app, host, view)) {}
SchematicParameterDialog::~SchematicParameterDialog() { Close(); }
bool SchematicParameterDialog::Open(ObjectRef script, ObjectRef object, unsigned mode) {
    return m_Data->Open(script, object, mode);
}
void SchematicParameterDialog::Draw() { m_Data->Draw(); }
void SchematicParameterDialog::Close() { m_Data->Close(); }
std::string SchematicParameterDialog::TakeMessage() {
    auto result = std::move(m_Data->message);
    m_Data->message.clear();
    return result;
}
} // namespace vdev
