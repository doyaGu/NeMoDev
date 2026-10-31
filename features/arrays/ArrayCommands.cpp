#include "features/arrays/ArrayCommands.h"
#include "core/document/Clipboard.h"
#include "core/level/ArrayCommands.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <iterator>

namespace vdev {
namespace {
const EditorValue *Arg(const EditorRequest &r, const char *key) {
    for (uint32_t i = 0; i < r.count; ++i)
        if (std::string(r.values[i].key.data, r.values[i].key.size) == key)
            return r.values + i;
    return nullptr;
}
int Index(const EditorRequest &r, const char *key, int fallback = -1) {
    const auto *v = Arg(r, key);
    return v ? int(int64_t(v->integer)) : fallback;
}
uint64_t Guid(CKGUID g) { return (uint64_t(g.d1) << 32) | g.d2; }
CKGUID Guid(uint64_t g) { return CKGUID(CKDWORD(g >> 32), CKDWORD(g)); }
EditorField Number(const char *key, int64_t v) { return {key, EDITOR_INTEGER_VALUE, {}, {}, uint64_t(v)}; }
EditorField Text(const char *key, std::string v) { return {key, EDITOR_TEXT_VALUE, {}, std::move(v)}; }
EditorStatus Send(std::vector<EditorField> fields, EditorSink sink) {
    const auto v = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, v.data(), uint32_t(v.values.size())) : EDITOR_INVALID;
}
std::string CellText(CKDataArray &a, int row, int col) {
    const int size = a.GetElementStringValue(row, col, nullptr, 0);
    if (size <= 0)
        return {};
    std::string text(size, '\0');
    a.GetElementStringValue(row, col, text.data(), size);
    text.resize(std::strlen(text.c_str()));
    return AnsiToUtf8(text);
}
struct ColumnValue {
    std::string name;
    CK_ARRAYTYPE type = CKARRAYTYPE_INT;
    CKGUID parameter;
};
bool ReadColumnValue(Document &doc, const EditorValue *value, ColumnValue &out) {
    if (!value || value->type != EDITOR_RECORD || value->fields.count != 3)
        return false;
    const EditorRequest fields{{}, value->fields.data, value->fields.count};
    const auto *name = Arg(fields, "name"), *type = Arg(fields, "type"), *parameter = Arg(fields, "parameter");
    if (!name || name->type != EDITOR_TEXT_VALUE || !type || type->type != EDITOR_INTEGER_VALUE || type->integer < 1 ||
        type->integer > 5 || !parameter || parameter->type != EDITOR_INTEGER_VALUE)
        return false;
    out.name.assign(name->text.data ? name->text.data : "", name->text.size);
    if (out.name.find('\0') != std::string::npos)
        return false;
    out.type = CK_ARRAYTYPE(type->integer);
    out.parameter = Guid(parameter->integer);
    if (out.type == CKARRAYTYPE_PARAMETER) {
        auto *desc = doc.Context()->GetParameterManager()->GetParameterTypeDescription(out.parameter);
        if (!desc || !desc->Valid)
            return false;
    }
    return true;
}
std::vector<int> Rows(const EditorRequest &r) {
    const auto *v = Arg(r, "rows");
    std::vector<int> rows;
    if (v)
        for (uint32_t i = 0; i < v->integers.count; ++i)
            rows.push_back(int(int64_t(v->integers.data[i])));
    return rows;
}
CKDataArray *Array(Document *doc, const EditorRequest &r) {
    const auto *target = Arg(r, "target");
    return doc && target && doc->Token() == r.scope.document
               ? CKDataArray::Cast(doc->Resolve(target->object, CKCID_DATAARRAY))
               : nullptr;
}
std::vector<CK_ID> Sources(Document &doc, Clipboard &clipboard, CKDataArray &a, int col, bool shortcut) {
    std::vector<CK_ID> result;
    if (!clipboard.BelongsTo(doc) || col < 0 || col >= a.GetColumnCount() ||
        a.GetColumnType(col) != CKARRAYTYPE_PARAMETER)
        return result;
    for (auto id : clipboard.parameters) {
        auto *p = CKParameter::Cast(doc.Context()->GetObject(id));
        if (p && (!shortcut || (doc.IsLive(id) && p->GetOwner() != &a)) &&
            doc.Context()->GetParameterManager()->IsDerivedFrom(p->GetGUID(), a.GetColumnParameterGuid(col)))
            result.push_back(id);
    }
    return result;
}
const char *const commandIds[] = {
    "array.row-insert",      "array.rows-remove",      "array.rows-duplicate", "array.row-move",
    "array.column-set",      "array.column-remove",    "array.column-move",    "array.sort",
    "array.key-set",         "array.cells-set",        "array.copy",           "array.paste",
    "array.paste-shortcuts", "array.remove-shortcuts", "array.column-insert"};
const char *const titles[] = {
    "Insert Row",    "Delete Row",  "Duplicate Row",     "Move Row",        "Edit Column Properties",
    "Delete Column", "Move Column", "Sort Array",        "Key Column",      "Edit Array",
    "Copy",          "Paste",       "Paste As Shortcut", "Remove Shortcut", "Insert Column"};
} // namespace
EditorStatus ArrayCommands::Read(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto &e = *static_cast<Entry *>(data);
    auto *doc = e.owner->m_Current();
    auto *a = Array(doc, *request);
    if (!a)
        return EDITOR_STALE;
    for (uint32_t i = 0; i < request->count; ++i) {
        const auto &v = request->values[i];
        if (v.type == EDITOR_INTEGER_VALUE && (int64_t(v.integer) < INT_MIN || int64_t(v.integer) > INT_MAX))
            return EDITOR_INVALID;
        if (v.type == EDITOR_INTEGER_LIST)
            for (uint32_t n = 0; n < v.integers.count; ++n)
                if (v.integers.data[n] > INT_MAX)
                    return EDITOR_INVALID;
    }
    const int col = Index(*request, "column");
    const uint64_t revision = doc->TopologyRevision();
    if (e.kind == 3)
        return Send({Number("paste", !Sources(*doc, e.owner->m_Clipboard, *a, col, false).empty()),
                     Number("shortcuts", !Sources(*doc, e.owner->m_Clipboard, *a, col, true).empty())},
                    sink);
    if (e.kind == 1) {
        auto rows = Rows(*request);
        if (col < 0 || col >= a->GetColumnCount() || rows.empty())
            return EDITOR_INVALID;
        for (int row : rows)
            if (row < 0 || row >= a->GetRowCount())
                return EDITOR_STALE;
        EditorField selected{"rows", EDITOR_INTEGER_LIST};
        for (int row : rows)
            selected.integers.push_back(row);
        return Send({Text("value", CellText(*a, rows.front(), col)), std::move(selected), Number("column", col),
                     Number("revision", revision)},
                    sink);
    }
    if (e.kind == 2 || e.kind == 4) {
        const bool creating = e.kind == 4;
        if (col < (creating ? -1 : 0) || col >= a->GetColumnCount())
            return EDITOR_STALE;
        EditorField value{"value", EDITOR_RECORD};
        value.fields = {Text("name", creating ? "Column " + std::to_string(a->GetColumnCount())
                                              : AnsiToUtf8(a->GetColumnName(col) ? a->GetColumnName(col) : "")),
                        Number("type", creating ? CKARRAYTYPE_INT : a->GetColumnType(col)),
                        Number("parameter", creating ? 0 : Guid(a->GetColumnParameterGuid(col)))};
        EditorField names{"parameter-labels", EDITOR_TEXT_LIST}, types{"parameter-types", EDITOR_INTEGER_LIST};
        auto *pm = doc->Context()->GetParameterManager();
        for (int i = 0; i < pm->GetParameterTypesCount(); ++i)
            if (auto *type = pm->GetParameterTypeDescription(i))
                if (type->Valid && !(type->dwFlags & CKPARAMETERTYPE_HIDDEN)) {
                    names.texts.push_back(AnsiToUtf8(type->TypeName.CStr()));
                    types.integers.push_back(Guid(type->Guid));
                }
        return Send(
            {std::move(value), Number("column", col), Number("revision", revision), std::move(names), std::move(types)},
            sink);
    }
    EditorField columns{"columns", EDITOR_RECORD}, cells{"cells", EDITOR_RECORD};
    static const char *const labels[] = {"?", "Integer", "Float", "String", "Object", "Parameter"};
    for (int c = 0; c < a->GetColumnCount(); ++c) {
        EditorField column{std::to_string(c), EDITOR_RECORD};
        const auto type = a->GetColumnType(c);
        std::string label = type >= 1 && type <= 5 ? labels[type] : "?";
        if (type == CKARRAYTYPE_PARAMETER) {
            const auto p = a->GetColumnParameterGuid(c);
            const int i = doc->Context()->GetParameterManager()->ParameterGuidToType(p);
            if (auto *desc = doc->Context()->GetParameterManager()->GetParameterTypeDescription(i))
                label = AnsiToUtf8(desc->TypeName.CStr());
        }
        column.fields = {Text("name", AnsiToUtf8(a->GetColumnName(c) ? a->GetColumnName(c) : "")),
                         Text("type-name", std::move(label)), Number("type", type),
                         Number("sortable", SortableColumn(a, c))};
        columns.fields.push_back(std::move(column));
    }
    const int first = std::max(0, Index(*request, "first", 0)), count = std::clamp(Index(*request, "count", 0), 0, 128);
    for (int r = first; r < a->GetRowCount() && r - first < count; ++r) {
        EditorField row{std::to_string(r), EDITOR_RECORD}, values{"values", EDITOR_TEXT_LIST},
            shortcuts{"shortcuts", EDITOR_INTEGER_LIST};
        for (int c = 0; c < a->GetColumnCount(); ++c) {
            values.texts.push_back(CellText(*a, r, c));
            auto *p = a->GetColumnType(c) == CKARRAYTYPE_PARAMETER
                          ? reinterpret_cast<CKParameter *>(*a->GetElement(r, c))
                          : nullptr;
            shortcuts.integers.push_back(p && p->GetOwner() != a);
        }
        row.fields = {std::move(values), std::move(shortcuts)};
        cells.fields.push_back(std::move(row));
    }
    return Send({Number("rows", a->GetRowCount()), Number("key", a->GetKeyColumn()), Number("revision", revision),
                 std::move(columns), std::move(cells)},
                sink);
}
EditorStatus ArrayCommands::Check(void *data, const EditorHost *, const EditorRequest *r) {
    auto &e = *static_cast<Entry *>(data);
    auto *doc = e.owner->m_Current();
    auto *a = Array(doc, *r);
    if (!a)
        return EDITOR_STALE;
    const auto *v = Arg(*r, "revision");
    if (!v || v->integer != doc->TopologyRevision())
        return EDITOR_STALE;
    for (uint32_t i = 0; i < r->count; ++i) {
        const auto &arg = r->values[i];
        if (arg.type == EDITOR_INTEGER_VALUE && std::string(arg.key.data, arg.key.size) != "revision" &&
            (int64_t(arg.integer) < INT_MIN || int64_t(arg.integer) > INT_MAX))
            return EDITOR_INVALID;
        if (arg.type == EDITOR_INTEGER_LIST)
            for (uint32_t n = 0; n < arg.integers.count; ++n)
                if (arg.integers.data[n] > INT_MAX)
                    return EDITOR_INVALID;
    }
    if (e.kind == 4 || e.kind == 14) {
        ColumnValue value;
        if (!ReadColumnValue(*doc, Arg(*r, "value"), value))
            return EDITOR_INVALID;
    }
    return EDITOR_OK;
}
EditorStatus ArrayCommands::Write(void *data, const EditorHost *host, const EditorRequest *r, EditorSink sink) {
    auto &e = *static_cast<Entry *>(data);
    const auto allowed = Check(data, host, r);
    if (allowed != EDITOR_OK)
        return allowed;
    auto &doc = *e.owner->m_Current();
    auto *a = Array(&doc, *r);
    const auto id = a->GetID();
    const auto rows = Rows(*r);
    const int col = Index(*r, "column"), index = Index(*r, "index"), step = Index(*r, "step");
    if (e.kind == 10) {
        if (col < 0 || col >= a->GetColumnCount() || a->GetColumnType(col) != CKARRAYTYPE_PARAMETER || rows.empty())
            return EDITOR_REFUSED;
        std::vector<CK_ID> sources;
        for (int row : rows) {
            if (row < 0 || row >= a->GetRowCount())
                return EDITOR_STALE;
            if (auto *p = reinterpret_cast<CKParameter *>(*a->GetElement(row, col)))
                sources.push_back(p->GetID());
        }
        e.owner->m_Clipboard.SetParameters(doc, std::move(sources));
        return EDITOR_OK;
    }
    EditResult result = EditResult::Refused("Invalid array operation");
    switch (e.kind) {
    case 0:
        result = InsertArrayRow(doc, id, index);
        break;
    case 1:
        result = RemoveArrayRows(doc, id, rows);
        break;
    case 2:
        result = DuplicateArrayRows(doc, id, rows);
        break;
    case 3:
        result = MoveArrayRow(doc, id, index, step);
        break;
    case 4:
    case 14: {
        ColumnValue value;
        if (!ReadColumnValue(doc, Arg(*r, "value"), value))
            return EDITOR_INVALID;
        const auto name = Utf8ToAnsi(value.name);
        result = e.kind == 14 ? InsertArrayColumn(doc, id, col, name, value.type, value.parameter)
                              : EditArrayColumn(doc, id, col, name, value.type, value.parameter);
        break;
    }
    case 5:
        result = RemoveArrayColumn(doc, id, col);
        break;
    case 6:
        result = MoveArrayColumn(doc, id, col, step);
        break;
    case 7:
        result = SortArray(doc, id, col, Index(*r, "ascending") != 0);
        break;
    case 8:
        result = SetArrayKeyColumn(doc, id, col);
        break;
    case 9: {
        const auto *v = Arg(*r, "value");
        const std::string text(v->text.data ? v->text.data : "", v->text.size);
        if (text.find('\0') != std::string::npos)
            return EDITOR_INVALID;
        result = SetArrayCells(doc, id, rows, col, Utf8ToAnsi(text));
        break;
    }
    case 11:
        result = PasteArrayParameters(doc, id, rows, col, Sources(doc, e.owner->m_Clipboard, *a, col, false));
        break;
    case 12:
        result = PasteArrayShortcuts(doc, id, rows, col, Sources(doc, e.owner->m_Clipboard, *a, col, true));
        break;
    case 13:
        result = RemoveArrayShortcuts(doc, id, rows, col);
        break;
    }
    if (!result.ok && !result.Unchanged())
        Send({Text("message", AnsiToUtf8(result.message))}, sink);
    return result.ok                                ? EDITOR_OK
           : result.Unchanged()                     ? EDITOR_NO_CHANGE
           : result.Status() == EditStatus::Refused ? EDITOR_REFUSED
                                                    : EDITOR_ERROR;
}
bool ArrayCommands::Register(EditorApplication &app, std::string &error) {
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT},
        revision{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, rows{EDITOR_TEXT("rows"), EDITOR_INTEGER_LIST},
        col{EDITOR_TEXT("column"), EDITOR_INTEGER_VALUE}, index{EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE},
        step{EDITOR_TEXT("step"), EDITOR_INTEGER_VALUE};
    std::vector<std::vector<EditorParameter>> args = {
        {target, revision, index},
        {target, revision, rows},
        {target, revision, rows},
        {target, revision, index, step},
        {target, revision, col, {EDITOR_TEXT("value"), EDITOR_RECORD}},
        {target, revision, col},
        {target, revision, col, step},
        {target, revision, col, {EDITOR_TEXT("ascending"), EDITOR_INTEGER_VALUE}},
        {target, revision, col},
        {target, revision, rows, col, {EDITOR_TEXT("value"), EDITOR_TEXT_VALUE}},
        {target, revision, rows, col},
        {target, revision, rows, col},
        {target, revision, rows, col},
        {target, revision, rows, col},
        {target, revision, col, {EDITOR_TEXT("value"), EDITOR_RECORD}}};
    std::vector<EditorCommandDesc> commands;
    m_Entries.reserve(20);
    for (unsigned i = 0; i < 15; ++i)
        m_Entries.push_back({this, i});
    for (unsigned i = 0; i < 15; ++i)
        commands.push_back({sizeof(EditorCommandDesc), EditorLiteral(commandIds[i]), EDITOR_TEXT("builtin.arrays"),
                            EditorLiteral(titles[i]), args[i].data(), uint32_t(args[i].size()), Write, &m_Entries[i],
                            EDITOR_DOCUMENT, i == 10 ? EDITOR_SESSION_CHANGE : EDITOR_DOCUMENT_EDIT, Check});
    const EditorParameter tableArgs[] = {
        target, {EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("count"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter cellArgs[] = {target, rows, col}, columnArgs[] = {target, col};
    const EditorParameter tableFields[] = {{EDITOR_TEXT("rows"), EDITOR_INTEGER_VALUE},
                                           {EDITOR_TEXT("key"), EDITOR_INTEGER_VALUE},
                                           revision,
                                           {EDITOR_TEXT("columns"), EDITOR_RECORD},
                                           {EDITOR_TEXT("cells"), EDITOR_RECORD}};
    const EditorParameter cellFields[] = {{EDITOR_TEXT("value"), EDITOR_TEXT_VALUE}, rows, col, revision};
    const EditorParameter columnFields[] = {{EDITOR_TEXT("value"), EDITOR_RECORD},
                                            col,
                                            revision,
                                            {EDITOR_TEXT("parameter-labels"), EDITOR_TEXT_LIST},
                                            {EDITOR_TEXT("parameter-types"), EDITOR_INTEGER_LIST}};
    const EditorParameter pasteFields[] = {{EDITOR_TEXT("paste"), EDITOR_INTEGER_VALUE},
                                           {EDITOR_TEXT("shortcuts"), EDITOR_INTEGER_VALUE}};
    for (unsigned i = 0; i < 5; ++i)
        m_Entries.push_back({this, i});
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("array.table"), EDITOR_TEXT("builtin.arrays"), tableArgs, 3, Read,
         &m_Entries[15], EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, tableFields, uint32_t(std::size(tableFields))},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("array.cells-value"), EDITOR_TEXT("builtin.arrays"), cellArgs, 3, Read,
         &m_Entries[16], EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, cellFields, uint32_t(std::size(cellFields))},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("array.column-value"), EDITOR_TEXT("builtin.arrays"), columnArgs, 2, Read,
         &m_Entries[17], EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, columnFields, uint32_t(std::size(columnFields))},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("array.paste-options"), EDITOR_TEXT("builtin.arrays"), columnArgs, 2,
         Read, &m_Entries[18], EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, pasteFields, 2},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("array.column-new-value"), EDITOR_TEXT("builtin.arrays"), columnArgs, 2,
         Read, &m_Entries[19], EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, columnFields,
         uint32_t(std::size(columnFields))}};
    const EditorPropertyDesc properties[] = {
        {sizeof(EditorPropertyDesc), EDITOR_TEXT("array.cells"), EDITOR_TEXT("Edit Array"),
         EDITOR_TEXT("array.cells-value"), EDITOR_TEXT("value"), EDITOR_TEXT("array.cells-set"), EDITOR_TEXT("value"),
         EDITOR_TEXT("value.string")},
        {sizeof(EditorPropertyDesc), EDITOR_TEXT("array.column"), EDITOR_TEXT("Edit Column Properties"),
         EDITOR_TEXT("array.column-value"), EDITOR_TEXT("value"), EDITOR_TEXT("array.column-set"), EDITOR_TEXT("value"),
         EDITOR_TEXT("value.array-column")},
        {sizeof(EditorPropertyDesc), EDITOR_TEXT("array.new-column"), EDITOR_TEXT("Insert Column"),
         EDITOR_TEXT("array.column-new-value"), EDITOR_TEXT("value"), EDITOR_TEXT("array.column-insert"),
         EDITOR_TEXT("value"), EDITOR_TEXT("value.array-column")}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("arrays.editing");
    module.commands = commands.data();
    module.commandCount = uint32_t(commands.size());
    module.queries = queries;
    module.queryCount = 5;
    module.properties = properties;
    module.propertyCount = 3;
    return app.Register(module, error);
}
} // namespace vdev
