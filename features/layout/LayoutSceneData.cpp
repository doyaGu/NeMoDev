#include "features/layout/LayoutSceneData.h"
#include "workspace/EditorClient.h"
#include <algorithm>
#include <limits>
#include <string_view>

namespace vdev {
namespace {
const EditorField *Field(const std::vector<EditorField> &fields, const char *key, EditorValueType type) {
    for (const auto &field : fields) if (field.key == key && field.type == type) return &field;
    return nullptr;
}
ObjectRef Object(const std::vector<EditorField> &fields, const char *key) {
    const auto *field = Field(fields, key, EDITOR_OBJECT); return field ? field->object : ObjectRef{};
}
std::string Text(const std::vector<EditorField> &fields, const char *key) {
    const auto *field = Field(fields, key, EDITOR_TEXT_VALUE); return field ? field->text : std::string();
}
uint64_t Number(const std::vector<EditorField> &fields, const char *key) {
    const auto *field = Field(fields, key, EDITOR_INTEGER_VALUE); return field ? field->integer : 0;
}
bool SameArguments(const std::vector<EditorField> &saved, const std::vector<EditorValue> &values) {
    if (saved.size() != values.size()) return false;
    for (size_t i = 0; i < saved.size(); ++i) {
        const auto &a = saved[i]; const auto &b = values[i];
        if (a.type != b.type || a.key != std::string_view(b.key.data, b.key.size)) return false;
        if (b.type == EDITOR_INTEGER_VALUE) { if (a.integer != b.integer) return false; }
        else if (b.type == EDITOR_TEXT_VALUE) { if (a.text != std::string_view(b.text.data ? b.text.data : "", b.text.size)) return false; }
        else return false;
    }
    return true;
}
} // namespace
const LayoutObjectRow *LayoutSceneData::Find(uint32_t id) const {
    for (const auto &row : rows) if (row.object.object == id) return &row;
    return nullptr;
}
bool LayoutSelectionData::Contains(uint32_t id) const {
    return std::any_of(objects.begin(), objects.end(), [id](ObjectRef ref) { return ref.object == id; });
}
bool ReadLayoutSelection(const EditorClient &client, LayoutSelectionData &selection) {
    const auto source = client.Revision("selection.objects"); const auto document = client.Scope().document;
    if (source && selection.m_SourceVersion == source && selection.m_Document == document) return true;
    const auto result = client.Query("selection.objects");
    if (result.status != EDITOR_OK) return false;
    const auto *objects = Field(result.fields, "objects", EDITOR_OBJECT_LIST);
    if (!objects) return false;
    LayoutSelectionData next; next.objects = objects->objects;
    next.locked = Number(result.fields, "locked") != 0; next.revision = Number(result.fields, "revision");
    next.m_Document = document; next.m_SourceVersion = client.Revision("selection.objects");
    selection = std::move(next); return true;
}
bool ReadLayoutScene(const EditorClient &client, const char *section, LayoutSceneData &data,
                     const std::vector<EditorValue> &arguments) {
    const auto source = client.Revision("layout.scene"); const auto document = client.Scope().document;
    const auto selection = client.Revision("selection.objects");
    if (source && selection && data.m_Document == document && data.m_SourceVersion == source && data.m_Section == section &&
        data.m_SelectionVersion == selection && SameArguments(data.m_Arguments, arguments)) return true;
    LayoutSceneData next; uint64_t first = 0, revision = 0;
    do {
        auto args = arguments;
        args.push_back({EDITOR_TEXT("section"), EDITOR_TEXT_VALUE, {}, EditorLiteral(section)});
        args.push_back(IntegerArgument("first", first)); args.push_back(IntegerArgument("revision", revision));
        const auto result = client.Query("layout.scene", args);
        if (result.status != EDITOR_OK) return false;
        const auto *rows = Field(result.fields, "rows", EDITOR_RECORD),
                   *end = Field(result.fields, "next", EDITOR_INTEGER_VALUE),
                   *total = Field(result.fields, "total", EDITOR_INTEGER_VALUE),
                   *version = Field(result.fields, "revision", EDITOR_INTEGER_VALUE);
        if (!rows || !end || !total || !version || total->integer > 1000000 || end->integer < first ||
            end->integer > total->integer || end->integer - first != rows->fields.size() ||
            (end->integer == first && first < total->integer) || (first && version->integer != revision)) return false;
        revision = version->integer;
        if (!first) {
            next.scene = Object(result.fields, "scene"); next.camera = Object(result.fields, "camera");
            next.savedCamera = Object(result.fields, "saved-camera"); next.frame = Object(result.fields, "frame");
            next.initialTarget = Object(result.fields, "initial-target"); next.matchingGroup = Object(result.fields, "matching-group");
            next.initial = Number(result.fields, "initial") != 0;
            const auto types = Number(result.fields, "selected-types");
            if (types > UINT32_MAX) return false;
            next.selectedTypes = uint32_t(types);
            if (const auto *shapes = Field(result.fields, "frame-shapes", EDITOR_TEXT_LIST)) next.frameShapes = shapes->texts;
        }
        for (const auto &value : rows->fields) {
            if (value.type != EDITOR_RECORD) return false;
            LayoutObjectRow row; row.object = Object(value.fields, "object");
            if (!row.object || row.object.document != client.Scope().document) return false;
            const auto cid = Number(value.fields, "cid"); if (cid > UINT32_MAX) return false;
            row.cid = uint32_t(cid); row.name = Text(value.fields, "name"); row.className = Text(value.fields, "class");
            row.kind = Text(value.fields, "kind"); row.view = Number(value.fields, "view") != 0;
            if (const auto *members = Field(value.fields, "members", EDITOR_OBJECT_LIST)) row.members = members->objects;
            if (const auto *scripts = Field(value.fields, "scripts", EDITOR_RECORD))
                for (const auto &script : scripts->fields) {
                    if (script.type != EDITOR_RECORD) return false;
                    const auto object = Object(script.fields, "object");
                    if (!object || object.document != row.object.document) return false;
                    row.scripts.push_back({object, Text(script.fields, "name")});
                }
            next.rows.push_back(std::move(row));
        }
        first = end->integer;
        if (first == total->integer) break;
    } while (true);
    next.m_Document = document; next.m_SourceVersion = client.Revision("layout.scene"); next.m_Section = section;
    next.m_SelectionVersion = selection;
    if (CopyEditorFields(next.m_Arguments, arguments.data(), uint32_t(arguments.size())) != EDITOR_OK) return false;
    data = std::move(next); return true;
}
} // namespace vdev
