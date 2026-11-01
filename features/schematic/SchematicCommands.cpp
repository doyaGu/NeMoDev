#include "features/schematic/SchematicCommands.h"

#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/document/ScriptSession.h"
#include "core/interface/InterfaceChunkFormat.h"
#include "core/runtime/TextEncoding.h"
#include "core/schematic/SchematicEdits.h"
#include "core/graph/ReplaceCommands.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace vdev {
namespace {
const EditorValue *Argument(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key) return request.values + i;
    return nullptr;
}
std::vector<ObjectRef> Targets(Document &doc, const EditorRequest &request) {
    const auto *target = Argument(request, "target");
    if (!target || !doc.Resolve(target->object, CKCID_BEHAVIOR)) return {};
    std::vector<ObjectRef> result{target->object};
    if (const auto *targets = Argument(request, "targets"))
        for (uint32_t i = 0; i < targets->objects.count; ++i) {
            const auto ref = targets->objects.data[i];
            if (!doc.Resolve(ref, CKCID_BEHAVIOR)) return {};
            if (std::find(result.begin(), result.end(), ref) == result.end()) result.push_back(ref);
        }
    for (auto ref : result) {
        auto *session = doc.Session(doc.NativeId(ref));
        if (!session || session->ReadOnly()) return {};
    }
    return result;
}
const Comment *CommentAt(Document &doc, const EditorRequest &request) {
    const auto *target = Argument(request, "target"), *owner = Argument(request, "owner"),
               *index = Argument(request, "index");
    if (!target || !owner || !index || index->integer > uint64_t(std::numeric_limits<int>::max())) return nullptr;
    const auto script = doc.NativeId(target->object), graph = doc.NativeId(owner->object);
    const auto *session = script && graph ? doc.Session(script) : nullptr;
    const auto *record = session ? FindRecord(session->Data(), graph) : nullptr;
    return record && index->integer < record->comments.size() ? &record->comments[size_t(index->integer)] : nullptr;
}
EditorStatus Result(const EditResult &result) {
    return result.status == EditStatus::NoChange ? EDITOR_NO_CHANGE : result.ok ? EDITOR_OK : EDITOR_ERROR;
}
} // namespace

SchematicCommands::SchematicCommands(std::function<Document *()> current)
    : m_Current(std::move(current)), m_Entries{{this, true}, {this, false}, {this, false, true}} {}

EditorStatus SchematicCommands::Read(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto &entry = *static_cast<Entry *>(data);
    auto *doc = entry.owner->m_Current();
    if (!doc) return EDITOR_STALE;
    const auto targets = Targets(*doc, *request);
    if (targets.empty() || (entry.comment && targets.size() != 1)) return EDITOR_STALE;
    if (const auto *revision = Argument(*request, "revision"))
        if (revision->integer && doc->Session(targets.front().object)->Revision() != revision->integer)
            return EDITOR_STALE;
    if (const auto *topology = Argument(*request, "topology"))
        if (topology->integer && doc->TopologyRevision() != topology->integer)
            return EDITOR_STALE;
    EditorField value{"value", entry.comment ? EDITOR_TEXT_VALUE : EDITOR_REAL_LIST};
    if (entry.comment) {
        const auto *comment = CommentAt(*doc, *request);
        if (!comment) return EDITOR_STALE;
        value.text = AnsiToUtf8(comment->text);
    } else {
        const auto color = doc->Session(doc->NativeId(targets.front()))->Data().header.color;
        value.reals = {double(color & 255) / 255, double((color >> 8) & 255) / 255, double((color >> 16) & 255) / 255,
                       1};
    }
    std::vector<EditorField> fields{std::move(value),
                                    {"owner", EDITOR_OBJECT},
                                    {"index", EDITOR_INTEGER_VALUE},
                                    {"targets", EDITOR_OBJECT_LIST},
                                    {"revisions", EDITOR_INTEGER_LIST},
                                    {"rows", EDITOR_INTEGER_VALUE, {}, {}, 1},
                                    {"columns", EDITOR_INTEGER_VALUE, {}, {}, 4},
                                    {"labels", EDITOR_TEXT_LIST},
                                    {"alpha", EDITOR_INTEGER_VALUE}};
    fields[7].texts = {"R", "G", "B", "A"};
    fields[3].objects = targets;
    for (auto ref : targets)
        fields[4].integers.push_back(doc->Session(doc->NativeId(ref))->Revision());
    for (const auto *key : {"owner", "index"})
        if (const auto *arg = Argument(*request, key)) {
            std::vector<EditorField> owned;
            if (CopyEditorFields(owned, arg, 1) != EDITOR_OK) return EDITOR_INVALID;
            for (auto &field : fields)
                if (field.key == key) field = std::move(owned.front());
        }
    const auto values = BorrowEditorFields(fields);
    return sink.write(sink.data, values.data(), uint32_t(values.values.size()));
}
EditorStatus SchematicCommands::Check(void *data, const EditorHost *, const EditorRequest *request) {
    auto &entry = *static_cast<Entry *>(data);
    auto *doc = entry.owner->m_Current();
    if (!doc) return EDITOR_STALE;
    const auto targets = Targets(*doc, *request);
    if (entry.reset) return targets.empty() ? EDITOR_STALE : EDITOR_OK;
    const auto *revisions = Argument(*request, "revisions"), *value = Argument(*request, "value");
    if (targets.empty() || !revisions || revisions->integers.count != targets.size() || !value) return EDITOR_STALE;
    for (size_t i = 0; i < targets.size(); ++i)
        if (doc->Session(doc->NativeId(targets[i]))->Revision() != revisions->integers.data[i]) return EDITOR_STALE;
    if (entry.comment) {
        if (targets.size() != 1 || !CommentAt(*doc, *request)) return EDITOR_STALE;
        if (value->type != EDITOR_TEXT_VALUE ||
            std::string(value->text.data ? value->text.data : "", value->text.size).find('\0') != std::string::npos)
            return EDITOR_INVALID;
    } else {
        if (value->type != EDITOR_REAL_LIST || value->reals.count != 4) return EDITOR_INVALID;
        for (uint32_t i = 0; i < 4; ++i)
            if (!std::isfinite(value->reals.data[i]) || value->reals.data[i] < 0 || value->reals.data[i] > 1)
                return EDITOR_INVALID;
    }
    return EDITOR_OK;
}
EditorStatus SchematicCommands::Write(void *data, const EditorHost *host, const EditorRequest *request, EditorSink) {
    const auto allowed = Check(data, host, request);
    if (allowed != EDITOR_OK) return allowed;
    auto &entry = *static_cast<Entry *>(data);
    auto &doc = *entry.owner->m_Current();
    const auto targets = Targets(doc, *request);
    const auto *value = Argument(*request, "value");
    const auto label = entry.comment ? "Edit Comment" : entry.reset ? "Remove Color" : "Set Color";
    Edit edit(doc, label, Edit::Marks::Scripts);
    if (entry.comment) {
        const auto graph = doc.NativeId(Argument(*request, "owner")->object);
        const auto index = size_t(Argument(*request, "index")->integer);
        const auto text = Utf8ToAnsi(std::string(value->text.data ? value->text.data : "", value->text.size));
        if (CommentAt(doc, *request)->text == text) return EDITOR_NO_CHANGE;
        auto *data = edit.ScriptData(doc.NativeId(targets.front()));
        if (!data) return EDITOR_ERROR;
        FindRecord(*data, graph)->comments[index].text = text;
    } else {
        const auto color = entry.reset ? InterfaceChunkFormat::DEFAULT_HEADER_COLOR
                                       : CKDWORD(value->reals.data[0] * 255 + .5) |
                                             CKDWORD(value->reals.data[1] * 255 + .5) << 8 |
                                             CKDWORD(value->reals.data[2] * 255 + .5) << 16;
        for (auto ref : targets) {
            if (doc.Session(doc.NativeId(ref))->Data().header.color == color) continue;
            auto *data = edit.ScriptData(doc.NativeId(ref));
            if (!data) return EDITOR_ERROR;
            data->header.color = color;
        }
    }
    return Result(edit.Commit());
}
const std::vector<ProviderChoice> &SchematicCommands::DefaultProviders() {
    static const std::vector<ProviderChoice> result = {{"query", "schematic.comment-value", "builtin.schematic"},
                                                       {"command", "schematic.comment-set", "builtin.schematic"},
                                                       {"query", "schematic.script-color-value", "builtin.schematic"},
                                                       {"command", "schematic.script-color-set", "builtin.schematic"},
                                                       {"command", "schematic.reset-color", "builtin.schematic"},
                                                       {"command", "schematic.replace-blocks", "builtin.schematic"}};
    return result;
}
EditorStatus SchematicCommands::Replace(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto *doc = static_cast<SchematicCommands *>(data)->m_Current();
    if (!doc || doc->Token() != request->scope.document) return EDITOR_STALE;
    if (const auto *revision = Argument(*request, "revision"))
        if (revision->integer && doc->ChangeRevision() != revision->integer)
            return EDITOR_STALE;
    const auto *scripts = Argument(*request, "scripts"), *objects = Argument(*request, "objects"),
               *prototypes = Argument(*request, "prototypes");
    const auto count = objects->objects.count;
    if (scripts->objects.count != count || prototypes->integers.count != count || count > 65536) return EDITOR_INVALID;
    std::set<ObjectRef> unique;
    for (uint32_t i = 0; i < count; ++i) {
        const auto script = scripts->objects.data[i], object = objects->objects.data[i];
        auto *session = doc->Session(doc->NativeId(script));
        auto *block = CKBehavior::Cast(doc->Resolve(object, CKCID_BEHAVIOR));
        const auto guid = prototypes->integers.data[i];
        if (!session || session->ReadOnly() || !block || !unique.insert(object).second ||
            !FindRecord(session->Data(), object.object)) return EDITOR_STALE;
        if (!(block->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK) || !CKGetObjectDeclarationFromGuid(CKGUID(CKDWORD(guid >> 32), CKDWORD(guid)))) return EDITOR_INVALID;
    }
    std::vector<ObjectRef> replaced;
    for (uint32_t i = 0; i < count; ++i) {
        const auto guid = prototypes->integers.data[i];
        const auto result = ReplaceBuildingBlock(*doc, scripts->objects.data[i].object, objects->objects.data[i].object,
            CKGUID(CKDWORD(guid >> 32), CKDWORD(guid)));
        if (!result.ok) {
            const auto message = AnsiToUtf8(result.message);
            const EditorValue field{EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, {}, EditorText(message)};
            if (sink.write) sink.write(sink.data, &field, 1);
            return result.Unchanged() ? EDITOR_NO_CHANGE : EDITOR_ERROR;
        }
        replaced.push_back(doc->Reference(result.created));
    }
    const EditorValue fields[] = {{EDITOR_TEXT("replaced"), EDITOR_INTEGER_VALUE, {}, {}, replaced.size()},
        {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {replaced.data(), uint32_t(replaced.size())}}};
    if (sink.write) sink.write(sink.data, fields, 2);
    return replaced.empty() ? EDITOR_NO_CHANGE : EDITOR_OK;
}
bool SchematicCommands::Register(EditorApplication &app, std::string &error) {
    const EditorParameter queryArgs[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT},
                                         {EDITOR_TEXT("owner"), EDITOR_OBJECT, 1, 1},
                                         {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE, 1},
                                         {EDITOR_TEXT("targets"), EDITOR_OBJECT_LIST, 1},
                                         {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, 1},
                                         {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE, 1}};
    std::vector<EditorCommandDesc> commands;
    std::vector<EditorQueryDesc> queries;
    std::vector<EditorPropertyDesc> properties;
    std::vector<std::vector<EditorParameter>> arguments(2), fields(2);
    const char *ids[] = {"schematic.comment", "schematic.script-color"};
    const char *queryIds[] = {"schematic.comment-value", "schematic.script-color-value"};
    const char *commandIds[] = {"schematic.comment-set", "schematic.script-color-set"};
    const char *labels[] = {"Edit Comment", "Set Color"};
    for (size_t i = 0; i < 2; ++i) {
        const auto type = i == 0 ? EDITOR_TEXT_VALUE : EDITOR_REAL_LIST;
        arguments[i] = {queryArgs[0],
                        {EDITOR_TEXT("value"), type},
                        {EDITOR_TEXT("owner"), EDITOR_OBJECT, 0, 1},
                        {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE},
                        {EDITOR_TEXT("targets"), EDITOR_OBJECT_LIST},
                        {EDITOR_TEXT("revisions"), EDITOR_INTEGER_LIST}};
        fields[i].assign(arguments[i].begin() + 1, arguments[i].end());
        fields[i].push_back({EDITOR_TEXT("rows"), EDITOR_INTEGER_VALUE});
        fields[i].push_back({EDITOR_TEXT("columns"), EDITOR_INTEGER_VALUE});
        fields[i].push_back({EDITOR_TEXT("labels"), EDITOR_TEXT_LIST});
        fields[i].push_back({EDITOR_TEXT("alpha"), EDITOR_INTEGER_VALUE});
        commands.push_back({sizeof(EditorCommandDesc), EditorLiteral(commandIds[i]), EDITOR_TEXT("builtin.schematic"),
                            EditorLiteral(labels[i]), arguments[i].data(), uint32_t(arguments[i].size()), Write,
                            &m_Entries[i], EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT, Check});
        queries.push_back({sizeof(EditorQueryDesc), EditorLiteral(queryIds[i]), EDITOR_TEXT("builtin.schematic"),
                           queryArgs, 6, Read, &m_Entries[i], EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, fields[i].data(),
                           uint32_t(fields[i].size())});
        properties.push_back(
            {sizeof(EditorPropertyDesc), EditorLiteral(ids[i]), EditorLiteral(labels[i]), EditorLiteral(queryIds[i]),
             EDITOR_TEXT("value"), EditorLiteral(commandIds[i]), EDITOR_TEXT("value"),
             i == 0 ? EditorString EDITOR_TEXT("value.string") : EditorString EDITOR_TEXT("value.color")});
    }
    const EditorParameter resetArgs[] = {queryArgs[0], queryArgs[3]};
    const EditorParameter replaceArgs[] = {{EDITOR_TEXT("scripts"), EDITOR_OBJECT_LIST},
                                           {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
                                           {EDITOR_TEXT("prototypes"), EDITOR_INTEGER_LIST},
                                           {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, 1}};
    commands.push_back({sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.replace-blocks"),
                        EDITOR_TEXT("builtin.schematic"), EDITOR_TEXT("Replace Building Blocks"), replaceArgs, 4,
                        Replace, this, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT});
    commands.push_back({sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.reset-color"),
                        EDITOR_TEXT("builtin.schematic"), EDITOR_TEXT("Remove Color"), resetArgs, 2, Write,
                        &m_Entries[2], EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT, Check});
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("schematic.editing");
    module.commands = commands.data();
    module.commandCount = uint32_t(commands.size());
    module.queries = queries.data();
    module.queryCount = uint32_t(queries.size());
    module.properties = properties.data();
    module.propertyCount = uint32_t(properties.size());
    return app.Register(module, error);
}
} // namespace vdev
