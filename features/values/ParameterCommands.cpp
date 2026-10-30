#include "features/values/ParameterCommands.h"
#include "core/graph/ParameterValues.h"
#include "core/runtime/TextEncoding.h"
#include <cmath>
#include <iterator>

namespace vdev {
namespace {
struct Kind {
    ParameterValueKind kind;
    const char *property, *editor;
    EditorValueType type;
};
const Kind kinds[] = {
    {ParameterValueKind::Boolean, "parameter.boolean", "value.boolean", EDITOR_INTEGER_VALUE},
    {ParameterValueKind::Integer, "parameter.integer", "value.integer", EDITOR_INTEGER_VALUE},
    {ParameterValueKind::Real, "parameter.real", "value.real", EDITOR_REAL_VALUE},
    {ParameterValueKind::Enumeration, "parameter.enumeration", "value.enumeration", EDITOR_INTEGER_VALUE},
    {ParameterValueKind::Object, "parameter.object", "value.object", EDITOR_OBJECT},
    {ParameterValueKind::Angle, "parameter.angle", "value.angle", EDITOR_REAL_VALUE},
    {ParameterValueKind::Vector2, "parameter.vector2", "value.vector2", EDITOR_REAL_LIST},
    {ParameterValueKind::Vector3, "parameter.vector3", "value.vector3", EDITOR_REAL_LIST},
    {ParameterValueKind::Quaternion, "parameter.quaternion", "value.quaternion", EDITOR_REAL_LIST},
    {ParameterValueKind::EulerAngles, "parameter.euler", "value.euler", EDITOR_REAL_LIST},
    {ParameterValueKind::Matrix, "parameter.matrix", "value.matrix", EDITOR_REAL_LIST},
    {ParameterValueKind::Color, "parameter.color", "value.color", EDITOR_REAL_LIST},
    {ParameterValueKind::Box, "parameter.box", "value.box", EDITOR_REAL_LIST},
    {ParameterValueKind::Rect, "parameter.rect", "value.rect", EDITOR_REAL_LIST}};
const EditorValue *Find(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key) return &request.values[i];
    return nullptr;
}
uint64_t TypeId(CKGUID type) { return (uint64_t(type.d1) << 32) | type.d2; }
} // namespace
const char *ParameterProperty(ParameterValueKind kind) {
    for (const auto &entry : kinds) if (entry.kind == kind) return entry.property;
    return nullptr;
}
EditorValue BorrowParameterValue(const ParameterValue &value, EditorString key) {
    EditorValue result{};
    result.key = key;
    for (const auto &entry : kinds) if (entry.kind == value.kind) result.type = entry.type;
    result.integer = uint64_t(int64_t(value.integer)); result.real = value.real; result.object = value.object;
    result.reals = {value.components.data(), uint32_t(value.components.size())};
    return result;
}
bool ParameterValueFromEditor(ParameterValueKind kind, const EditorValue &value, ParameterValue &out) {
    if (kind == ParameterValueKind::None) return false;
    const auto count = ParameterComponentCount(kind);
    const auto expected = count ? EDITOR_REAL_LIST : kind == ParameterValueKind::Real || kind == ParameterValueKind::Angle ?
        EDITOR_REAL_VALUE : kind == ParameterValueKind::Object ? EDITOR_OBJECT : EDITOR_INTEGER_VALUE;
    if (value.type != expected) return false;
    ParameterValue parsed;
    parsed.kind = kind;
    if (expected == EDITOR_REAL_LIST) {
        if (value.reals.count != count || !value.reals.data) return false;
        parsed.components.assign(value.reals.data, value.reals.data + count);
    }
    if (expected == EDITOR_REAL_VALUE) parsed.real = value.real;
    else if (expected == EDITOR_OBJECT) parsed.object = value.object;
    if (expected == EDITOR_INTEGER_VALUE) {
        const auto integer = int64_t(value.integer);
        if (integer < INT32_MIN || integer > INT32_MAX) return false;
        parsed.integer = int32_t(integer);
    }
    out = parsed;
    return true;
}
EditorStatus ParameterCommands::Check(void *data, const EditorHost *, const EditorRequest *request) {
    const auto &entry = *static_cast<Entry *>(data);
    auto *document = entry.owner->m_Current();
    const auto *target = Find(*request, "target"), *value = Find(*request, "value"), *type = Find(*request, "type");
    if (!document || document->Token() != request->scope.document || !target || !value || !type) return EDITOR_STALE;
    auto *parameter = CKParameter::Cast(document->Resolve(target->object, CKCID_PARAMETER));
    if (!parameter || TypeId(parameter->GetGUID()) != type->integer) return EDITOR_STALE;
    ParameterValueDescription description;
    ParameterValue parsed;
    if (!ParameterValueFromEditor(kinds[entry.kind].kind, *value, parsed) ||
        !DescribeParameterValue(*document, parameter, description)) return EDITOR_INVALID;
    return ValidateParameterValue(*document, description, parsed) ? EDITOR_OK : EDITOR_REFUSED;
}
EditorStatus ParameterCommands::Describe(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto *document = static_cast<ParameterCommands *>(data)->m_Current();
    const auto *target = Find(*request, "target");
    if (!document || document->Token() != request->scope.document || !target) return EDITOR_STALE;
    auto *parameter = CKParameter::Cast(document->Resolve(target->object, CKCID_PARAMETER));
    ParameterValueDescription description;
    const char *property = DescribeParameterValue(*document, parameter, description) ? ParameterProperty(description.value.kind) : "";
    const EditorValue value{EDITOR_TEXT("property"), EDITOR_TEXT_VALUE, {}, EditorLiteral(property)};
    return sink.write ? sink.write(sink.data, &value, 1) : EDITOR_INVALID;
}
EditorStatus ParameterCommands::Read(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    const auto &entry = *static_cast<Entry *>(data);
    auto *document = entry.owner->m_Current();
    const auto *target = Find(*request, "target");
    if (!document || document->Token() != request->scope.document || !target) return EDITOR_STALE;
    auto *parameter = CKParameter::Cast(document->Resolve(target->object, CKCID_PARAMETER));
    ParameterValueDescription description;
    const auto &kind = kinds[entry.kind];
    if (!DescribeParameterValue(*document, parameter, description) || description.value.kind != kind.kind) return EDITOR_INVALID;
    std::vector<EditorString> labels;
    std::vector<uint64_t> values;
    std::vector<ObjectRef> objects;
    if (kind.kind != ParameterValueKind::Object) for (const auto &choice : description.choices) {
        labels.push_back(EditorText(choice.second));
        values.push_back(uint64_t(int64_t(choice.first)));
    }
    if (!description.componentLabels.empty()) {
        labels.clear();
        for (const auto &label : description.componentLabels) labels.push_back(EditorText(label));
    }
    std::vector<EditorString> rowLabels;
    for (const auto &label : description.rowLabels) rowLabels.push_back(EditorText(label));
    EditorValue fields[] = {
        BorrowParameterValue(description.value, EditorString EDITOR_TEXT("value")),
        {EDITOR_TEXT("display"), EDITOR_TEXT_VALUE, {}, EditorText(description.display)},
        {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE, {}, {}, TypeId(description.type)},
        {EDITOR_TEXT("labels"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {labels.data(), uint32_t(labels.size())}},
        {EDITOR_TEXT("values"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {values.data(), uint32_t(values.size())}},
        {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {objects.data(), uint32_t(objects.size())}},
        {EDITOR_TEXT("scale"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, description.displayScale},
        {EDITOR_TEXT("rows"), EDITOR_INTEGER_VALUE, {}, {}, description.rows},
        {EDITOR_TEXT("columns"), EDITOR_INTEGER_VALUE, {}, {}, description.columns},
        {EDITOR_TEXT("row-labels"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {rowLabels.data(), uint32_t(rowLabels.size())}}};
    return sink.write ? sink.write(sink.data, fields, 10) : EDITOR_INVALID;
}
EditorStatus ParameterCommands::Choices(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto *document = static_cast<ParameterCommands *>(data)->m_Current();
    const auto *target = Find(*request, "target");
    if (!document || !target || document->Token() != request->scope.document) return EDITOR_STALE;
    auto *parameter = CKParameter::Cast(document->Resolve(target->object, CKCID_PARAMETER));
    ParameterValueDescription description;
    if (!DescribeParameterValue(*document, parameter, description) || description.value.kind != ParameterValueKind::Object) return EDITOR_INVALID;
    const auto *classId = Find(*request, "class"), *derived = Find(*request, "derived"), *filter = Find(*request, "filter");
    const auto cid = classId && classId->integer ? CK_CLASSID(classId->integer) : description.objectClass;
    if (derived && derived->integer > 2) return EDITOR_INVALID;
    if (classId && classId->integer > INT32_MAX) return EDITOR_INVALID;
    if (!CKIsChildClassOf(cid, description.objectClass)) return EDITOR_INVALID;
    auto choices = ObjectsOfClass(*document, cid, !derived || derived->integer != 2,
        filter ? Utf8ToAnsi(std::string(filter->text.data ? filter->text.data : "", filter->text.size)) : std::string());
    std::vector<EditorString> labels;
    std::vector<ObjectRef> objects;
    for (auto &choice : choices) { choice.second = AnsiToUtf8(choice.second); objects.push_back(document->Reference(choice.first)); }
    for (const auto &choice : choices) labels.push_back(EditorText(choice.second));
    const EditorValue fields[] = {
        {EDITOR_TEXT("labels"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {labels.data(), uint32_t(labels.size())}},
        {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {objects.data(), uint32_t(objects.size())}}};
    return sink.write ? sink.write(sink.data, fields, 2) : EDITOR_INVALID;
}
EditorStatus ParameterCommands::Write(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    const auto &entry = *static_cast<Entry *>(data);
    auto *document = entry.owner->m_Current();
    const auto *target = Find(*request, "target"), *value = Find(*request, "value"), *type = Find(*request, "type");
    if (!document || document->Token() != request->scope.document || !target || !value || !type) return EDITOR_STALE;
    const auto guid = CKGUID(CKDWORD(type->integer >> 32), CKDWORD(type->integer));
    const auto &kind = kinds[entry.kind];
    ParameterValue parsed;
    if (!ParameterValueFromEditor(kind.kind, *value, parsed)) return EDITOR_INVALID;
    const auto result = SetParameterValue(*document, target->object, guid, parsed);
    if (!result.message.empty() && !result.ok && !result.Unchanged()) {
        const auto message = AnsiToUtf8(result.message);
        const EditorValue field{EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, {}, EditorText(message)};
        if (sink.write) sink.write(sink.data, &field, 1);
    }
    return result.ok ? EDITOR_OK : result.Unchanged() ? EDITOR_NO_CHANGE : EDITOR_REFUSED;
}
bool ParameterCommands::Register(EditorApplication &editor, std::string &error) {
    std::vector<EditorCommandDesc> commands;
    std::vector<EditorQueryDesc> queries;
    std::vector<EditorPropertyDesc> properties;
    constexpr auto count = std::size(kinds);
    EditorParameter arguments[count][3]{}, fields[count][10]{};
    m_Entries.resize(count);
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT};
    std::string queryIds[count], commandIds[count];
    for (unsigned i = 0; i < count; ++i) {
        const auto &kind = kinds[i];
        m_Entries[i] = {this, i};
        queryIds[i] = std::string(kind.property) + "-value";
        commandIds[i] = std::string(kind.property) + "-set";
        arguments[i][0] = target;
        arguments[i][1] = {EDITOR_TEXT("value"), kind.type};
        arguments[i][2] = {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE};
        fields[i][0] = arguments[i][1];
        fields[i][1] = {EDITOR_TEXT("display"), EDITOR_TEXT_VALUE};
        fields[i][2] = arguments[i][2];
        fields[i][3] = {EDITOR_TEXT("labels"), EDITOR_TEXT_LIST};
        fields[i][4] = {EDITOR_TEXT("values"), EDITOR_INTEGER_LIST};
        fields[i][5] = {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST};
        fields[i][6] = {EDITOR_TEXT("scale"), EDITOR_REAL_VALUE};
        fields[i][7] = {EDITOR_TEXT("rows"), EDITOR_INTEGER_VALUE};
        fields[i][8] = {EDITOR_TEXT("columns"), EDITOR_INTEGER_VALUE};
        fields[i][9] = {EDITOR_TEXT("row-labels"), EDITOR_TEXT_LIST};
        commands.push_back({sizeof(EditorCommandDesc), EditorText(commandIds[i]), EDITOR_TEXT("builtin.parameters"),
            EDITOR_TEXT("Set Value"), arguments[i], 3, Write, &m_Entries[i], EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT, Check});
        queries.push_back({sizeof(EditorQueryDesc), EditorText(queryIds[i]), EDITOR_TEXT("builtin.parameters"),
            &target, 1, Read, &m_Entries[i], EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, fields[i], 10});
        properties.push_back({sizeof(EditorPropertyDesc), EditorLiteral(kind.property), EDITOR_TEXT("Value"),
            EditorText(queryIds[i]), EDITOR_TEXT("value"), EditorText(commandIds[i]), EDITOR_TEXT("value"), EditorLiteral(kind.editor), kind.kind == ParameterValueKind::Object ? EditorString EDITOR_TEXT("parameter.object-choices") : EditorString{}});
    }
    const EditorParameter property{EDITOR_TEXT("property"), EDITOR_TEXT_VALUE};
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("parameter.description"), EDITOR_TEXT("builtin.parameters"),
        &target, 1, Describe, this, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, &property, 1});
    const EditorParameter choiceArgs[] = {target, {EDITOR_TEXT("class"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("derived"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("filter"), EDITOR_TEXT_VALUE, 1}};
    const EditorParameter choiceFields[] = {{EDITOR_TEXT("labels"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}};
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("parameter.object-choices"), EDITOR_TEXT("builtin.parameters"),
        choiceArgs, 4, Choices, this, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, choiceFields, 2});
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("parameters.editing");
    module.commands = commands.data(); module.commandCount = uint32_t(commands.size());
    module.queries = queries.data(); module.queryCount = uint32_t(queries.size());
    module.properties = properties.data(); module.propertyCount = uint32_t(properties.size());
    return editor.Register(module, error);
}
} // namespace vdev
