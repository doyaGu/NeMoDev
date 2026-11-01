#include "features/setup/SetupQueries.h"
#include "core/runtime/SdkCompat.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <set>

namespace vdev {
void SetupDescription::Integer(const char *key, int64_t value) {
    fields.push_back({key, EDITOR_INTEGER_VALUE, {}, {}, uint64_t(value)});
}
void SetupDescription::Real(const char *key, double value) {
    EditorField field{key, EDITOR_REAL_VALUE};
    field.real = value;
    fields.push_back(std::move(field));
}
void SetupDescription::Text(const char *key, std::string value) {
    fields.push_back({key, EDITOR_TEXT_VALUE, {}, std::move(value)});
}
void SetupDescription::Link(const char *key, CKObject *object) {
    fields.push_back({key, EDITOR_OBJECT, document.Reference(object ? object->GetID() : 0)});
    Text((std::string(key) + "-name").c_str(), SetupObjectName(object));
}
void SetupDescription::Numbers(const char *key, std::vector<double> values) {
    EditorField field{key, EDITOR_REAL_LIST};
    field.reals = std::move(values);
    fields.push_back(std::move(field));
}
void SetupDescription::Rows(const char *key, std::vector<EditorField> rows) {
    for (size_t i = 0; i < rows.size(); ++i)
        rows[i].key = std::to_string(i);
    EditorField field{key, EDITOR_RECORD};
    field.fields = std::move(rows);
    fields.push_back(std::move(field));
}
EditorField SetupDescription::Row(CKObject *object) const {
    EditorField field{std::to_string(object ? object->GetID() : 0), EDITOR_RECORD};
    field.fields = {{"target", EDITOR_OBJECT, document.Reference(object ? object->GetID() : 0)},
                    {"name", EDITOR_TEXT_VALUE, {}, SetupObjectName(object)},
                    {"class", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(object ? object->GetClassID() : 0)}};
    const auto *name = object ? CKClassIDToString(object->GetClassID()) : nullptr;
    field.fields.push_back({"class-name", EDITOR_TEXT_VALUE, {}, name ? AnsiToUtf8(name) : "?"});
    return field;
}
std::string SetupObjectName(CKObject *object, const char *none) {
    return !object ? none : object->GetName() && *object->GetName() ? AnsiToUtf8(object->GetName()) : "<Unnamed>";
}
CKBehavior *SetupScriptOf(CKBehavior *behavior) {
    while (behavior && behavior->GetParent())
        behavior = behavior->GetParent();
    return behavior;
}
CKBehavior *SetupBehaviorOf(CKObject *object) {
    CKObject *owner = nullptr;
    if (auto *parameter = CKParameter::Cast(object))
        owner = parameter->GetOwner();
    else if (auto *input = CKParameterIn::Cast(object))
        owner = input->GetOwner();
    if (auto *operation = CKParameterOperation::Cast(owner))
        return operation->GetOwner();
    return CKBehavior::Cast(owner);
}
std::string SetupTypeName(CKContext *context, CKParameterType type) {
    const auto *name = context->GetParameterManager()->ParameterTypeToName(type);
    return name && *name ? AnsiToUtf8(name) : "?";
}
std::string SetupGuidText(CKGUID guid) {
    char text[32];
    std::snprintf(text, sizeof(text), "0x%08x, 0x%08x", unsigned(guid.d1), unsigned(guid.d2));
    return text;
}
namespace {
const EditorValue *Arg(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key)
            return request.values + i;
    return nullptr;
}
Document *Current(void *data, const EditorRequest &request) {
    auto *document = (*static_cast<std::function<Document *()> *>(data))();
    return document && document->Token() == request.scope.document ? document : nullptr;
}
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto values = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.values.size())) : EDITOR_INVALID;
}
EditorStatus Details(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto *document = Current(data, *request);
    auto *object = document ? document->Resolve(Arg(*request, "target")->object) : nullptr;
    if (!object)
        return EDITOR_STALE;
    const auto &text = Arg(*request, "section")->text;
    const std::string section(text.data, text.size);
    std::vector<uint64_t> indices;
    if (const auto *index = Arg(*request, "indices"); index && index->integers.count)
        indices.assign(index->integers.data, index->integers.data + index->integers.count);
    if (indices.size() > 8 || std::any_of(indices.begin(), indices.end(), [](auto value) { return value > INT32_MAX; }))
        return EDITOR_INVALID;
    SetupDescription description{*document};
    bool described = section == "structure";
    if (!GuardedCall(
            [&] {
                described = described || DescribeSetupObject(object, section, indices, description) ||
                            DescribeSetupRender(object, section, indices, description) ||
                            DescribeSetupScene(object, section, indices, description);
            },
            &document->Host(), "Read Setup"))
        return EDITOR_ERROR;
    if (!described)
        return EDITOR_STALE;
    EditorField record{"details", EDITOR_RECORD};
    record.fields = std::move(description.fields);
    return Write(sink, {{"target", EDITOR_OBJECT, document->Reference(object->GetID())},
                        {"document-revision", EDITOR_INTEGER_VALUE, {}, {}, document->ChangeRevision()},
                        std::move(record)});
}
EditorStatus Choices(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto *document = Current(data, *request);
    if (!document)
        return EDITOR_STALE;
    const auto cid = Arg(*request, "class")->integer;
    if (!cid || cid > INT32_MAX || !CKGetClassDesc(CK_CLASSID(cid)))
        return EDITOR_INVALID;
    auto objects = document->LiveObjects(CK_CLASSID(cid), true);
    objects.erase(std::remove_if(objects.begin(), objects.end(),
                                 [](auto *object) { return object->IsPrivate() || object->IsDynamic(); }),
                  objects.end());
    std::sort(objects.begin(), objects.end(), [](auto *a, auto *b) {
        return _stricmp(a->GetName() ? a->GetName() : "", b->GetName() ? b->GetName() : "") < 0;
    });
    EditorField refs{"objects", EDITOR_OBJECT_LIST}, names{"names", EDITOR_TEXT_LIST},
        classes{"classes", EDITOR_INTEGER_LIST};
    for (auto *object : objects) {
        refs.objects.push_back(document->Reference(object->GetID()));
        names.texts.push_back(SetupObjectName(object));
        classes.integers.push_back(object->GetClassID());
    }
    return Write(sink, {std::move(refs), std::move(names), std::move(classes)});
}
EditorStatus Properties(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    auto *document = Current(data, *request);
    if (!document || !document->Resolve(Arg(*request, "target")->object))
        return EDITOR_STALE;
    const auto &names = Arg(*request, "properties")->texts;
    if (!names.count || names.count > 256)
        return EDITOR_INVALID;
    EditorField values{"values", EDITOR_RECORD};
    std::set<std::string> seen;
    for (uint32_t i = 0; i < names.count; ++i) {
        const std::string property(names.data[i].data, names.data[i].size);
        if (property.empty() || property.size() > 128 || !seen.insert(property).second)
            return EDITOR_INVALID;
        const auto query = "setup." + property + "-value";
        EditorValue arguments[] = {*Arg(*request, "target"), {EDITOR_TEXT("indices"), EDITOR_INTEGER_LIST}};
        if (const auto *indices = Arg(*request, "indices"))
            arguments[1] = *indices;
        const EditorRequest read{request->scope, arguments, 2};
        EditorResponse response;
        const auto status =
            host->query(host->data, EditorText(query), &read,
                        {&response, [](void *p, const EditorValue *fields, uint32_t count) {
                             return CopyEditorFields(static_cast<EditorResponse *>(p)->fields, fields, count);
                         }});
        if (status != EDITOR_OK)
            return status;
        auto value = std::find_if(response.fields.begin(), response.fields.end(),
                                  [](const auto &field) { return field.key == "value"; });
        if (value == response.fields.end())
            return EDITOR_INVALID;
        value->key = property;
        values.fields.push_back(std::move(*value));
    }
    return Write(sink, {std::move(values)});
}
} // namespace
void AppendSetupPageQueries(std::vector<EditorQueryDesc> &queries, std::function<Document *()> *current) {
    static const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT};
    static const EditorParameter detailsArgs[] = {
        target, {EDITOR_TEXT("section"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("indices"), EDITOR_INTEGER_LIST, 1}};
    static const EditorParameter detailsFields[] = {
        target, {EDITOR_TEXT("document-revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("details"), EDITOR_RECORD}};
    static const EditorParameter choiceArgs{EDITOR_TEXT("class"), EDITOR_INTEGER_VALUE};
    static const EditorParameter choiceFields[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
                                                   {EDITOR_TEXT("names"), EDITOR_TEXT_LIST},
                                                   {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST}};
    static const EditorParameter propertyArgs[] = {
        target, {EDITOR_TEXT("properties"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("indices"), EDITOR_INTEGER_LIST, 1}};
    static const EditorParameter propertyFields{EDITOR_TEXT("values"), EDITOR_RECORD};
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("setup.details"), EDITOR_TEXT("builtin.setup"), detailsArgs,
                       3, Details, current, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, detailsFields, 3});
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("setup.choices"), EDITOR_TEXT("builtin.setup"), &choiceArgs,
                       1, Choices, current, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, choiceFields, 3});
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("setup.properties"), EDITOR_TEXT("builtin.setup"),
                       propertyArgs, 3, Properties, current, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, &propertyFields,
                       1});
}
} // namespace vdev
