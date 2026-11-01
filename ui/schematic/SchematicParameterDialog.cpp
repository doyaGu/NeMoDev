#include "ui/schematic/SchematicParameterDialog.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/ObjectArrayInput.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
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
    uint32_t DrawValue(Row &row, EditorField &v, ImVec2 at, float s) {
        if (row.binding.editor.empty()) {
            vi::Text(at, {at.x + row.width * DX * s, at.y + 14 * DY * s}, Text(row.fields, "display").c_str());
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
            float scale;
            bool used = false;
        } members{this, &row, &v, at, s};
        const auto draw = [](void *data, const EditorHost *host, const EditorValueInput *input) {
            auto &m = *static_cast<Members *>(data);
            if (m.row->layout == "objects") {
                ImGui::SetCursorScreenPos(m.at);
                return m.row->arrayInput.Draw(host, *input);
            }
            m.used = true;
            return m.self->DrawRows(m.row->members, m.value->fields, m.at, m.scale, false);
        };
        auto size = UiSize(row, v, s);
        auto origin = at;
        const auto &kind = row.layout;
        const char *label = "##components";
        if (kind == "number" || kind == "enum" || kind == "bool" || kind == "string" || kind == "text" ||
            kind == "object") {
            origin.x += (kind == "bool" ? 5 : kind == "object" ? 19 : 15) * DX * s;
            if (kind == "bool") {
                origin.y += 2 * DY * s;
                size = {8 * DX * s, 8 * DX * s};
                label = "##bool";
            } else {
                size.x = (kind == "number" ? 55 : kind == "object" || kind == "enum" ? 195 : 205) * DX * s;
                size.y = kind == "string" && row.extended ? 200 * s : 12 * DY * s;
                label = kind == "number"   ? "##number"
                        : kind == "enum"   ? "##enum"
                        : kind == "object" ? "##object"
                        : kind == "text"   ? "##text"
                                           : "##string";
            }
        } else if (kind == "flags")
            label = "##flags";
        ImGui::SetCursorScreenPos(origin);
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
            const auto P = [&](float x, float y) { return ImVec2(at.x + x * DX * s, at.y + y * DY * s); };
            vi::Text(P(15, 15), P(43, 25), "Class", true, vi::LEFT);
            int index = -1;
            std::vector<std::string> names;
            for (size_t i = 0; i < row.classes.size(); ++i) {
                names.push_back(Text(row.classes[i].fields, "name"));
                if (Number(row.classes[i].fields, "id") == row.filterClass)
                    index = int(i);
            }
            const auto chosenClass = vi::Combo(
                "##class", P(53, 15), P(165, 27), index >= 0 ? names[size_t(index)] : "", [&] { return names; }, index);
            if (chosenClass >= 0)
                row.filterClass = unsigned(Number(row.classes[size_t(chosenClass)].fields, "id"));
            vi::Text(P(171, 15), P(199, 25), "Derived", true);
            vi::Check("##derived", P(206, 15), 8 * DX * s, row.derived);
            vi::Text(P(15, 30), P(55, 40), "Text Filter", true, vi::LEFT);
            vi::Edit("##filter", P(59, 30), P(165, 42), row.filter, vi::CENTER);
            if (vi::Button("##showsetup", P(170, 30), P(215, 40), "Show Setup", bool(v.object)))
                client.Command("object.open-setup", {ObjectArgument("target", v.object)});
        }
        return action;
    }
    uint32_t DrawRows(std::vector<Row> &items, std::vector<EditorField> &values, ImVec2 at, float s, bool minimum) {
        uint32_t action = 0;
        const auto labels = Labels(items, s, minimum);
        const auto extent = RowsSize(items, values, s, minimum);
        float y = at.y;
        auto *dl = ImGui::GetWindowDrawList();
        for (size_t i = 0; i < items.size(); ++i) {
            auto &row = items[i];
            auto *v = Field(values, row.key.c_str());
            if (!v) {
                action |= EDITOR_INPUT_INVALID;
                continue;
            }
            const auto size = UiSize(row, *v, s);
            const auto uiX = 14 * DX * s + labels + 2 * s;
            const auto height = std::max(size.y, (row.expand && row.pick ? 15 : 11) * DY * s) + 2 * s;
            if (i) {
                dl->AddRectFilled({at.x, y}, {at.x + extent.x, y + 1}, vi::Color(7));
                dl->AddLine({at.x + uiX, y + .5f}, {at.x + extent.x, y + .5f}, vi::Color(6));
                y += 1;
            }
            row.min = {at.x, y};
            row.max = {at.x + uiX + size.x, y + height};
            dl->AddRectFilled(row.min, {at.x + uiX - 1, y + height}, vi::Color(6));
            dl->AddLine({at.x + uiX - .5f, y}, {at.x + uiX - .5f, y + height}, vi::Color(0));
            dl->AddRectFilled({at.x + uiX, y}, row.max, vi::Color(7));
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
            const auto uiY = y + (height - size.y) * .5f;
            vi::Text({at.x + 14 * DX * s, uiY}, {at.x + uiX - 2 * s, uiY + 13 * s}, row.label.c_str(), true);
            action |= DrawValue(row, *v, {at.x + uiX, uiY}, s);
            ImGui::PopID();
            y += height;
        }
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
        if ((row.pick & 1))
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
        const auto s = ImGui::GetFontSize() / 13.f;
        const bool single = mode == 0;
        const auto size = RowsSize(rows, values->fields, s, !single);
        float width, bottom, rowY;
        ImVec2 origin;
        if (single) {
            width = std::max(265 * s, size.x + 2 * s) + 32 * s;
            rowY = (rows.empty() ? 74 : 53) * s;
            bottom = rowY + (rows.empty() ? 0 : size.y + 7 * s) + s;
        } else {
            width = std::max({size.x + 24 * s, vi::TextWidth(title.c_str(), true) + 40 * s, (apply ? 220 : 150) * s});
            rowY = 8 * s;
            bottom = 10 * s + size.y;
        }
        const auto P = [&](float x, float y) { return ImVec2(origin.x + x, origin.y + y); };
        if (!vi::BeginDialog(Id(), title, {width, bottom + 27 * s}, s, origin))
            return;
        vi::Panel(P(10 * s, single ? 5 * s : 6 * s), P(width - 10 * s, bottom), s);
        ImGui::BeginDisabled(operation || !open);
        bool changed = false, changedType = false;
        if (single) {
            auto *name = Field(value.fields, "name");
            const auto right = width - 16 * s, fieldWidth = 165 * s;
            vi::Text(P(16 * s, 13 * s), P(136 * s, 27 * s), "Parameter Name :", true);
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
                ImGui::SetCursorScreenPos(P(right - fieldWidth, 11 * s));
                const EditorValueInput input{sizeof(input),
                                             0,
                                             EDITOR_TEXT("##name"),
                                             values.values.front(),
                                             nullptr,
                                             0,
                                             0,
                                             {&update, write},
                                             EDITOR_VALUE_DIALOG,
                                             fieldWidth,
                                             16 * s};
                const auto editor = editors.find(nameProperty.editor);
                const auto action =
                    editor == editors.end() ? EDITOR_INPUT_INVALID : app.DrawValue(*editor->second, input, &editors);
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
            } else {
                inputActive = false;
                vi::Text(P(right - fieldWidth, 14 * s), P(right, 27 * s), name ? name->text.c_str() : "", false,
                         vi::RIGHT);
            }
            vi::Text(P(16 * s, 34 * s), P(136 * s, 48 * s), "Parameter Type :", true);
            if (typeEditable) {
                std::vector<std::string> names;
                for (const auto &type : types)
                    names.push_back(Text(type.fields, "name"));
                const auto selected = vi::Combo(
                    "##type", P(right - fieldWidth, 32 * s), P(right, 48 * s), typeName, [&] { return names; }, -1);
                if (selected >= 0) {
                    Field(value.fields, "type")->integer = Number(types[size_t(selected)].fields, "type");
                    values->fields.clear();
                    changed = changedType = true;
                }
            } else
                vi::Text(P(right - fieldWidth, 35 * s), P(right, 48 * s), typeName.c_str());
            if (rows.empty()) {
                vi::Text(P(16 * s, 55 * s), P(136 * s, 69 * s), "Parameter Value :", true);
                vi::Text(P(right - fieldWidth, 56 * s), P(right, 69 * s), display.c_str(), false, vi::RIGHT);
            }
        } else
            inputActive = false;
        const bool captured = bool(picking);
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
        const auto action =
            DrawRows(rows, values->fields, P((single ? 17 : 12) * s, rowY + (single ? s : 0)), s, !single);
        ImGui::EndDisabled();
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
        if (single && !rows.empty())
            ImGui::GetWindowDrawList()->AddRect(P(16 * s, rowY), P(18 * s + size.x, rowY + size.y + 2 * s),
                                                vi::Color(0));
        float x = width - 70 * s;
        const auto y = bottom + 4 * s;
        const bool cancel =
            !open || (vi::DialogButton("Cancel", P(x, y), s, ImGuiKey_Escape) && !inputActive && !confirming);
        ImGui::BeginDisabled(!valid || !AllValid(rows));
        if (apply) {
            x -= 70 * s;
            if (vi::DialogButton("Apply", P(x, y), s) && !inputActive)
                Finish(false);
        }
        x -= 70 * s;
        if (vi::DialogButton("OK", P(x, y), s, ImGuiKey_Enter) && !inputActive) {
            if (single && !invalidLinks.empty()) {
                confirming = true;
                ImGui::OpenPopup("Invalid Parameter Link");
            } else
                Finish(true);
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (confirming &&
            ImGui::BeginPopupModal("Invalid Parameter Link", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(
                "Parameter Type is incompatible with Parameter Input Type.\nThis link must be deleted.");
            if (invalidLinks.size() > 1)
                ImGui::Text("(%d links)", int(invalidLinks.size()));
            const bool keys = !ImGui::IsWindowAppearing();
            if (ImGui::Button("OK", {80, 0}) || (keys && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
                confirming = false;
                ImGui::CloseCurrentPopup();
                Field(value.fields, "delete-links")->integer = 1;
                if (Submit())
                    Finish(true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {80, 0}) || (keys && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
                confirming = false;
                ImGui::CloseCurrentPopup();
                open = false;
            }
            ImGui::EndPopup();
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
