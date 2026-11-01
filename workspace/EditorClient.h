#pragma once
#include "application/EditorApplication.h"

namespace vdev {
class EditorClient {
  public:
    EditorClient(const EditorHost *host, EditorView view) : m_Host(host), m_View(view) {}
    EditorScope Scope() const { return m_Host->scope(m_Host->data, m_View); }
    uint64_t Revision(const char *query) const {
        uint64_t version = 0;
        return m_Host->queryRevision(m_Host->data, EditorLiteral(query), &version) == EDITOR_OK ? version : 0;
    }
    uint32_t DrawProperty(const std::string &property, ObjectRef target, EditorEdit &edit, const char *label) const {
        const EditorValue argument{EditorLiteral("target"),EDITOR_OBJECT,target};
        const EditorRequest request{Scope(),&argument,1};
        return m_Host->drawProperty(m_Host->data,EditorText(property),&request,&edit,EditorLiteral(label));
    }
    uint32_t DrawProperty(const std::string &property, const std::vector<EditorValue> &values, EditorEdit &edit, const char *label) const {
        const EditorRequest request{Scope(), values.data(), uint32_t(values.size())};
        return m_Host->drawProperty(m_Host->data, EditorText(property), &request, &edit, EditorLiteral(label));
    }
    EditorStatus BeginEdit(const char *property, const std::vector<EditorValue> &values, EditorEdit &edit) const {
        const EditorRequest request{Scope(), values.data(), uint32_t(values.size())};
        return m_Host->beginEdit(m_Host->data, EditorLiteral(property), &request, &edit);
    }
    void CancelEdit(EditorEdit &edit) const {
        if (edit)
            m_Host->finishEdit(m_Host->data, edit, 0, nullptr);
        edit = 0;
    }
    EditorStatus BeginCommand(const char *command, const std::vector<EditorValue> &values, EditorEdit &edit) const {
        const EditorRequest request{Scope(), values.data(), uint32_t(values.size())};
        return m_Host->beginCommand(m_Host->data, EditorLiteral(command), &request, &edit);
    }
    EditorResponse Query(const char *id, const std::vector<EditorValue> &values = {}) const {
        return Call(id, values, false);
    }
    EditorResponse Command(const char *id, const std::vector<EditorValue> &values = {}) const {
        return Call(id, values, true);
    }
    EditorResponse Inspect(EditorView view) const {
        EditorResponse result;
        result.status = m_Host->queryView(m_Host->data, view, Sink(result));
        return result;
    }
    EditorResponse Completion(EditorOperation operation) const {
        auto status = Query("application.operation", {Number("operation", operation)});
        if (status.status != EDITOR_OK) {
            return status;
        }
        const auto state = EditorStatus(Integer(status, "status"));
        if (state == EDITOR_QUEUED) {
            status.status = state;
            return status;
        }
        EditorResponse result;
        result.status = state;
        result.operation = operation;
        result.message = status.message;
        result.documentRevision = Integer(status, "document-revision");
        for (uint64_t i = 0, count = Integer(status, "count"); i < count; ++i) {
            const auto field =
                Query("application.operation-output", {Number("operation", operation), Number("index", i)});
            if (field.status != EDITOR_OK) {
                return field;
            }
            EditorField value;
            value.key = Text(field, "key");
            value.type = EditorValueType(Integer(field, "type"));
            if (const auto *v = Find(field, "object")) {
                value.object = v->object;
            }
            if (const auto *v = Find(field, "text")) {
                value.text = v->text;
            }
            if (const auto *v = Find(field, "integer")) {
                value.integer = v->integer;
            }
            if (const auto *v = Find(field, "objects")) {
                value.objects = v->objects;
            }
            if (const auto *v = Find(field, "integers")) {
                value.integers = v->integers;
            }
            if (const auto *v = Find(field, "texts")) {
                value.texts = v->texts;
            }
            if (const auto *v = Find(field, "real")) {
                value.real = v->real;
            }
            if (const auto *v = Find(field, "reals")) value.reals = v->reals;
            if (const auto *v = Find(field, "fields")) value.fields = v->fields;
            result.fields.push_back(std::move(value));
        }
        return result;
    }
    const EditorHost *Host() const { return m_Host; }
    EditorView View() const { return m_View; }
    static const EditorField *Find(const EditorResponse &result, const char *key) {
        for (const auto &field : result.fields) {
            if (field.key == key) {
                return &field;
            }
        }
        return nullptr;
    }
    static uint64_t Integer(const EditorResponse &result, const char *key) {
        const auto *field = Find(result, key);
        return field ? field->integer : 0;
    }
    static std::string Text(const EditorResponse &result, const char *key) {
        const auto *field = Find(result, key);
        return field ? field->text : std::string();
    }

  private:
    static EditorValue Number(const char *key, uint64_t value) {
        return {EditorLiteral(key), EDITOR_INTEGER_VALUE, {}, {}, value};
    }
    static EditorSink Sink(EditorResponse &result) {
        return {&result, [](void *data, const EditorValue *values, uint32_t count) {
                    auto &result = *static_cast<EditorResponse *>(data);
                    const auto status = CopyEditorFields(result.fields, values, count);
                    if (status != EDITOR_OK) {
                        return status;
                    }
                    for (const auto &field : result.fields) {
                        if (field.key == "operation") {
                            result.operation = field.integer;
                        }
                        if (field.key == "message") {
                            result.message = field.text;
                        }
                    }
                    return EDITOR_OK;
                }};
    }
    EditorResponse Call(const char *id, const std::vector<EditorValue> &values, bool command) const {
        EditorResponse result;
        const EditorRequest request{Scope(), values.data(), uint32_t(values.size())};
        result.status = command ? m_Host->execute(m_Host->data, EditorLiteral(id), &request, Sink(result))
                                : m_Host->query(m_Host->data, EditorLiteral(id), &request, Sink(result));
        return result;
    }
    const EditorHost *m_Host;
    EditorView m_View;
};
inline EditorValue ObjectArgument(const char *key, ObjectRef object) {
    return {EditorLiteral(key), EDITOR_OBJECT, object};
}
inline EditorValue ObjectsArgument(const char *key, const std::vector<ObjectRef> &objects) {
    return {EditorLiteral(key), EDITOR_OBJECT_LIST, {}, {}, 0, {objects.data(), uint32_t(objects.size())}};
}
inline EditorValue IntegerArgument(const char *key, uint64_t value) {
    return {EditorLiteral(key), EDITOR_INTEGER_VALUE, {}, {}, value};
}
inline EditorValue TextArgument(const char *key, const std::string &text) {
    return {EditorLiteral(key), EDITOR_TEXT_VALUE, {}, EditorText(text)};
}
} // namespace vdev
