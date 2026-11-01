#include "features/setup/SetupViewData.h"
#include <cmath>
#include <limits>
#include <set>

namespace vdev {
namespace {
const EditorField *Field(const EditorResponse &result, const char *key, EditorValueType type) {
    const auto *field = EditorClient::Find(result, key);
    return field && field->type == type ? field : nullptr;
}
bool Object(const EditorResponse &result, const char *key, DocumentToken document, ObjectRef &out,
            bool optional = false) {
    const auto *field = Field(result, key, EDITOR_OBJECT);
    if (!field || (field->object ? field->object.document != document : !optional || field->object != ObjectRef{}))
        return false;
    out = field->object;
    return true;
}
bool Number(const EditorResponse &result, const char *key, int64_t low, int64_t high, int64_t &out) {
    const auto *field = Field(result, key, EDITOR_INTEGER_VALUE);
    if (!field || int64_t(field->integer) < low || int64_t(field->integer) > high)
        return false;
    out = int64_t(field->integer);
    return true;
}
bool Boolean(const EditorResponse &result, const char *key, bool &out) {
    int64_t value = 0;
    if (!Number(result, key, 0, 1, value))
        return false;
    out = value != 0;
    return true;
}
} // namespace

bool ReadSetupTarget(const EditorClient &client, ObjectRef requested, ObjectRef &target, int &point) {
    const auto result = client.Query("setup.target", {ObjectArgument("target", requested)});
    ObjectRef next{};
    int64_t index = -1;
    if (result.status != EDITOR_OK || !Object(result, "target", client.Scope().document, next) ||
        !Number(result, "point", -1, INT32_MAX, index))
        return false;
    target = next;
    point = int(index);
    return true;
}

bool ReadSetupPage(const EditorClient &client, ObjectRef target, int point, SetupPageData &data) {
    const auto result = client.Query(
        "setup.page", {ObjectArgument("target", target), IntegerArgument("point", uint64_t(int64_t(point)))});
    SetupPageData next;
    int64_t kind = 0, file = 0, cid = 0, layer = -1;
    if (result.status != EDITOR_OK || !Object(result, "target", client.Scope().document, next.target) ||
        next.target != target || !Object(result, "point", target.document, next.point, true) ||
        !Object(result, "grid", target.document, next.grid, true) ||
        !Number(result, "kind", 0, int64_t(SetupKind::InterfaceObjects), kind) ||
        !Number(result, "file-kind", 0, int64_t(SetupFileKind::Midi), file) ||
        !Number(result, "class", 1, INT32_MAX, cid) || !Number(result, "layer", -1, INT32_MAX, layer) ||
        !Boolean(result, "frame", next.frame) || !Boolean(result, "behavioral", next.behavioral) ||
        !Boolean(result, "attribute-drop", next.attributeDrop) || !Boolean(result, "scene-scoped", next.sceneScoped) ||
        !Boolean(result, "current-scene", next.currentScene) ||
        !Boolean(result, "in-current-scene", next.inCurrentScene) ||
        !Boolean(result, "initial-visible", next.initialVisible) ||
        !Boolean(result, "initial-here", next.initialHere) || !Boolean(result, "initial-value", next.initialValue))
        return false;
    const auto *title = Field(result, "title", EDITOR_TEXT_VALUE), *name = Field(result, "name", EDITOR_TEXT_VALUE),
               *filename = Field(result, "filename", EDITOR_TEXT_VALUE);
    if (!title || title->text.empty() || !name || !filename)
        return false;
    next.kind = SetupKind(kind);
    next.fileKind = SetupFileKind(file);
    next.classId = uint32_t(cid);
    next.layer = int(layer);
    next.title = title->text;
    next.name = name->text;
    next.filename = filename->text;
    data = std::move(next);
    return true;
}

bool ReadSetupObjects(const EditorClient &client, ObjectRef target, bool currentScene,
                      std::vector<SetupObjectRow> &rows) {
    const auto result = client.Query(
        "setup.objects", {ObjectArgument("target", target), IntegerArgument("current-scene", currentScene)});
    const auto *objects = Field(result, "objects", EDITOR_OBJECT_LIST),
               *names = Field(result, "names", EDITOR_TEXT_LIST);
    if (result.status != EDITOR_OK || !objects || !names || objects->objects.size() != names->texts.size())
        return false;
    std::set<ObjectRef> seen;
    std::vector<SetupObjectRow> next;
    for (size_t i = 0; i < objects->objects.size(); ++i) {
        const auto object = objects->objects[i];
        if (!object || object.document != client.Scope().document || !seen.insert(object).second)
            return false;
        next.push_back({object, names->texts[i]});
    }
    rows = std::move(next);
    return true;
}

bool ReadSetupTransform(const EditorClient &client, ObjectRef target, SetupTransformData &data) {
    const auto result = client.Query("setup.transform", {ObjectArgument("target", target)});
    if (result.status != EDITOR_OK)
        return false;
    SetupTransformData next;
    const auto copy = [&](const char *key, auto &values) {
        const auto *field = Field(result, key, EDITOR_REAL_LIST);
        if (!field || field->reals.size() != values.size())
            return false;
        for (size_t i = 0; i < values.size(); ++i) {
            const auto value = field->reals[i];
            if (!std::isfinite(value) || std::fabs(value) > std::numeric_limits<float>::max())
                return false;
            values[i] = float(value);
        }
        return true;
    };
    if (!copy("world", next.world) || !copy("local", next.local) || !copy("box", next.box))
        return false;
    data = next;
    return true;
}
const EditorField *SetupFields::Find(const char *key) const {
    if (m_Fields)
        for (const auto &field : *m_Fields)
            if (field.key == key)
                return &field;
    return nullptr;
}
int64_t SetupFields::Integer(const char *key) const {
    const auto *field = Find(key);
    return field && field->type == EDITOR_INTEGER_VALUE ? int64_t(field->integer) : 0;
}
double SetupFields::Real(const char *key) const {
    const auto *field = Find(key);
    return field && field->type == EDITOR_REAL_VALUE ? field->real : 0;
}
std::string SetupFields::Text(const char *key) const {
    const auto *field = Find(key);
    return field && field->type == EDITOR_TEXT_VALUE ? field->text : std::string();
}
ObjectRef SetupFields::Object(const char *key) const {
    const auto *field = Find(key);
    return field && field->type == EDITOR_OBJECT ? field->object : ObjectRef{};
}
#define VDEV_SETUP_FIELD_LIST(Method, Type, Kind, Member)                                                              \
    const std::vector<Type> &SetupFields::Method(const char *key) const {                                              \
        static const std::vector<Type> empty;                                                                          \
        const auto *field = Find(key);                                                                                 \
        return field && field->type == Kind ? field->Member : empty;                                                   \
    }
VDEV_SETUP_FIELD_LIST(Objects, ObjectRef, EDITOR_OBJECT_LIST, objects)
VDEV_SETUP_FIELD_LIST(Integers, uint64_t, EDITOR_INTEGER_LIST, integers)
VDEV_SETUP_FIELD_LIST(Reals, double, EDITOR_REAL_LIST, reals)
VDEV_SETUP_FIELD_LIST(Texts, std::string, EDITOR_TEXT_LIST, texts)
VDEV_SETUP_FIELD_LIST(Rows, EditorField, EDITOR_RECORD, fields)
#undef VDEV_SETUP_FIELD_LIST

bool ReadSetupDetails(const EditorClient &client, ObjectRef target, const char *section, SetupDetails &data,
                      const std::vector<uint64_t> &indices) {
    const EditorValue index{
        EDITOR_TEXT("indices"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {indices.data(), uint32_t(indices.size())}};
    const auto result =
        client.Query("setup.details", {ObjectArgument("target", target),
                                       {EDITOR_TEXT("section"), EDITOR_TEXT_VALUE, {}, EditorLiteral(section)},
                                       index});
    const auto *record = Field(result, "details", EDITOR_RECORD);
    const auto *revision = Field(result, "document-revision", EDITOR_INTEGER_VALUE);
    ObjectRef echoed{};
    if (result.status != EDITOR_OK || !record || !revision ||
        !Object(result, "target", client.Scope().document, echoed) || echoed != target)
        return false;
    SetupDetails next;
    next.target = target;
    next.documentRevision = revision->integer;
    next.fields = record->fields;
    data = std::move(next);
    return true;
}

bool ReadSetupProperties(const EditorClient &client, ObjectRef target, const std::vector<const char *> &properties,
                         SetupDetails &data, const std::vector<uint64_t> &indices) {
    std::vector<EditorString> names;
    for (auto *name : properties)
        names.push_back(EditorLiteral(name));
    EditorValue requested{EDITOR_TEXT("properties"), EDITOR_TEXT_LIST};
    requested.texts = {names.data(), uint32_t(names.size())};
    const EditorValue index{
        EDITOR_TEXT("indices"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {indices.data(), uint32_t(indices.size())}};
    const auto result = client.Query("setup.properties", {ObjectArgument("target", target), requested, index});
    const auto *values = Field(result, "values", EDITOR_RECORD);
    if (result.status != EDITOR_OK || !values || values->fields.size() != properties.size())
        return false;
    for (auto *name : properties)
        if (!SetupFields(values->fields).Find(name))
            return false;
    SetupDetails next;
    next.target = target;
    next.fields = values->fields;
    data = std::move(next);
    return true;
}

bool ReadSetupChoices(const EditorClient &client, uint32_t classId, std::vector<SetupObjectRow> &rows) {
    const auto result = client.Query("setup.choices", {IntegerArgument("class", classId)});
    const auto *objects = Field(result, "objects", EDITOR_OBJECT_LIST),
               *names = Field(result, "names", EDITOR_TEXT_LIST),
               *classes = Field(result, "classes", EDITOR_INTEGER_LIST);
    if (result.status != EDITOR_OK || !objects || !names || !classes ||
        objects->objects.size() != names->texts.size() || objects->objects.size() != classes->integers.size())
        return false;
    std::vector<SetupObjectRow> next;
    std::set<ObjectRef> seen;
    for (size_t i = 0; i < objects->objects.size(); ++i) {
        const auto object = objects->objects[i];
        if (!object || object.document != client.Scope().document || classes->integers[i] > INT32_MAX ||
            !seen.insert(object).second)
            return false;
        next.push_back({object, names->texts[i], uint32_t(classes->integers[i])});
    }
    rows = std::move(next);
    return true;
}

} // namespace vdev
