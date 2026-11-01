#include "features/schematic/SchematicGraphCommands.h"
#include "features/schematic/SchematicPosition.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/document/LinkValidation.h"
#include "core/document/ScriptSession.h"
#include "core/runtime/TextEncoding.h"
#include "core/schematic/SchematicEdits.h"
#include "core/schematic/SchematicGrid.h"
#include "core/graph/TopologyCommands.h"
#include "core/graph/RuntimeGraph.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace vdev {
namespace {
const EditorValue *Arg(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key) return request.values + i;
    return nullptr;
}
uint64_t Number(const EditorRequest &request, const char *key) {
    const auto *value = Arg(request, key); return value ? value->integer : 0;
}
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto values = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
}
std::string Name(CKObject *object) { return object ? AnsiToUtf8(object->GetName() ? object->GetName() : "") : std::string(); }
bool DecodeAddress(const EditorValue &value, ElementAddress &address) {
    if (value.type != EDITOR_RECORD) return false;
    EditorRequest request{}; request.values = value.fields.data; request.count = value.fields.count;
    for (uint32_t i = 0; i < request.count; ++i) {
        const auto &field = request.values[i]; const std::string key(field.key.data, field.key.size);
        if (key == "target" || key == "owner" || key == "object" || key == "endpoint") {
            if (field.type != EDITOR_OBJECT) return false;
            if (key == "target") address.script = field.object;
            if (key == "owner") address.owner = field.object;
            if (key == "object") address.object = field.object;
            if (key == "endpoint") address.endpoint = field.object;
        } else {
            if (field.type != EDITOR_INTEGER_VALUE) return false;
            const auto v = field.integer;
            if (key == "kind") { if (v > uint64_t(ElementKind::Start)) return false; address.element.kind = ElementKind(v); }
            else if (key == "pin") { if (v > uint64_t(PinKind::Target)) return false; address.element.pin = PinKind(v); }
            else if (key == "index" || key == "sub") {
                if (v != UINT64_MAX && v > INT_MAX) return false;
                const auto index = v == UINT64_MAX ? -1 : int(v);
                if (key == "index") address.element.index = index; else address.element.sub = index;
            } else if (key == "revision") address.revision = v;
            else if (key == "topology") address.topology = v;
            else return false;
        }
    }
    address.element.owner = address.owner.object; address.element.object = address.object.object;
    return address.script && address.element.IsValid();
}
EditorStatus Result(Document &doc, const EditResult &result, EditorSink sink, const ElementAddress &address = {}) {
    if (sink.write) Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(result.message)},
        {"created", EDITOR_OBJECT, doc.Reference(result.created)}, SchematicPosition(address)});
    return result.Unchanged() ? EDITOR_NO_CHANGE : result.ok ? EDITOR_OK : result.Status() == EditStatus::Refused ? EDITOR_REFUSED : EDITOR_ERROR;
}
} // namespace
struct SchematicGraphCommands::Data {
    std::function<Document *()> current;
    std::function<bool()> testTypes;
    DocumentToken document{};
    std::vector<InvalidLink> invalid;
    uint64_t revision = 1;
    Data(std::function<Document *()> current, std::function<bool()> testTypes)
        : current(std::move(current)), testTypes(std::move(testTypes)) {}
    Document *DocumentOf(const EditorRequest &request) {
        auto *doc = current();
        if (!doc || !doc->IsOpen() || doc->Token() != request.scope.document) return nullptr;
        if (document != doc->Token()) { document = doc->Token(); invalid.clear(); ++revision; }
        return doc;
    }
    void Refresh(Document &doc) {
        for (const auto &link : invalid) {
            CK_ID script = 0; ElementRef ref;
            if (!link.address.Resolve(doc, script, ref)) {
                invalid = FindInvalidLinks(doc, !testTypes || testTypes()); ++revision; break;
            }
        }
    }
    ScriptSession *Session(Document &doc, const EditorRequest &request) const {
        const auto *target = Arg(request, "target");
        const auto script = target ? doc.NativeId(target->object) : 0;
        auto *session = script ? doc.Session(script) : nullptr;
        return session && !session->ReadOnly() && session->Revision() == Number(request, "revision") &&
            doc.TopologyRevision() == Number(request, "topology") ? session : nullptr;
    }
    static EditorStatus ScriptState(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data); auto *doc = self.DocumentOf(*request); if (!doc) return EDITOR_STALE;
        const auto *target = Arg(*request, "target"); auto *script = target ? CKBehavior::Cast(doc->Resolve(target->object)) : nullptr;
        auto *session = script && doc->FindScript(script->GetID()) ? doc->Session(script->GetID()) : nullptr;
        if (!session) return EDITOR_STALE;
        const auto id = Number(*request, "id"); if (id > UINT32_MAX) return EDITOR_INVALID;
        auto *object = id ? doc->Context()->GetObject(CK_ID(id)) : nullptr;
        if (id) {
            const auto *root = OwningScript(object);
            if (!object || (object != script && !FindRecord(session->Data(), CK_ID(id)) && (!root || root != script)) || doc->IsDiscarded(CK_ID(id))) return EDITOR_STALE;
        }
        return Write(sink, {{"target", EDITOR_OBJECT, target->object}, {"object", EDITOR_OBJECT, object ? doc->Reference(object->GetID()) : ObjectRef{}},
            {"revision", EDITOR_INTEGER_VALUE, {}, {}, session->Revision()}, {"topology", EDITOR_INTEGER_VALUE, {}, {}, doc->TopologyRevision()},
            {"readonly", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(session->ReadOnly())},
            {"graph", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(CKBehavior::Cast(object) && (CKBehavior::Cast(object)->GetType() & CKBEHAVIORTYPE_BEHAVIOR))}});
    }
    static EditorStatus Fold(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data); auto *doc = self.DocumentOf(*request); if (!doc) return EDITOR_STALE;
        auto *session = self.Session(*doc, *request); const auto *object = Arg(*request, "object");
        if (!session || !object) return EDITOR_STALE;
        const auto behavior = doc->NativeId(object->object); const auto *record = FindRecord(session->Data(), behavior);
        if (!record) return EDITOR_STALE;
        const auto script = Arg(*request, "target")->object.object; const bool folded = !record->folded;
        const bool changed = EditScriptData(*doc, script, folded ? "Collapse graph" : "Expand graph",
            [=](InterfaceData &data) { SetBehaviorFolded(data, behavior, folded); });
        return Result(*doc, changed ? EditResult::Applied() : EditResult::NoChange(), sink);
    }
    static EditorStatus CreateGraph(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data); auto *doc = self.DocumentOf(*request); if (!doc) return EDITOR_STALE;
        auto *session = self.Session(*doc, *request); const auto *owner = Arg(*request, "owner"), *at = Arg(*request, "at");
        if (!session || !owner) return EDITOR_STALE;
        if (!at || at->reals.count != 2) return EDITOR_INVALID;
        for (uint32_t i = 0; i < 2; ++i) if (std::abs(at->reals.data[i]) > std::numeric_limits<float>::max()) return EDITOR_INVALID;
        const auto script = Arg(*request, "target")->object.object, graph = doc->NativeId(owner->object);
        if (!FindRecord(session->Data(), graph)) return EDITOR_STALE;
        const Point point(float(at->reals.data[0]), float(at->reals.data[1]));
        const auto result = CreateBehaviorGraph(*doc, script, graph, "Behavior Graph", point);
        return Result(*doc, result, sink, result.ok ? ElementAddress::Capture(*doc, script, {ElementKind::Behavior, graph, result.created}) : ElementAddress{});
    }
    static EditorStatus Targets(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data); auto *doc = self.DocumentOf(*request); if (!doc) return EDITOR_STALE;
        if (!self.Session(*doc, *request)) return EDITOR_STALE;
        const auto *objects = Arg(*request, "objects"); if (!objects) return EDITOR_INVALID;
        const auto script = Arg(*request, "target")->object.object;
        std::vector<CKBehavior *> behaviors; std::set<CK_ID> unique;
        for (uint32_t i = 0; i < objects->objects.count; ++i) {
            auto *behavior = CKBehavior::Cast(doc->Resolve(objects->objects.data[i]));
            auto *root = behavior ? OwningScript(behavior) : nullptr;
            if (!behavior || (behavior->GetID() != script && (!root || root->GetID() != script))) return EDITOR_STALE;
            if (unique.insert(behavior->GetID()).second) behaviors.push_back(behavior);
        }
        if (behaviors.empty()) return EDITOR_NO_CHANGE;
        Edit edit(*doc, "Target");
        for (auto *behavior : behaviors) {
            const auto result = SetTargetUsed(*doc, script, behavior->GetID(), !behavior->IsUsingTarget());
            if (!result.ok && !result.Unchanged()) return Result(*doc, result, sink);
        }
        return Result(*doc, edit.Commit(), sink);
    }
    static EditorStatus DeleteElements(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data); auto *doc = self.DocumentOf(*request); if (!doc) return EDITOR_STALE;
        if (!self.Session(*doc, *request)) return EDITOR_STALE;
        const auto script = Arg(*request, "target")->object.object; const auto *elements = Arg(*request, "elements");
        if (!elements) return EDITOR_INVALID;
        std::vector<ElementRef> items; std::set<ElementRef> unique; bool displayOnly = true;
        for (uint32_t i = 0; i < elements->fields.count; ++i) {
            ElementAddress address; CK_ID inScript = 0; ElementRef ref;
            if (!DecodeAddress(elements->fields.data[i], address)) return EDITOR_INVALID;
            if (!address.Resolve(*doc, inScript, ref) || inScript != script) return EDITOR_STALE;
            if (!unique.insert(ref).second) return EDITOR_INVALID;
            displayOnly &= ref.kind == ElementKind::Comment || ref.kind == ElementKind::ControlPoint; items.push_back(ref);
        }
        if (items.empty()) return EDITOR_NO_CHANGE;
        if (!displayOnly) return Result(*doc, vdev::DeleteElements(*doc, script, items), sink);
        std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) {
            return a.owner != b.owner ? a.owner < b.owner : a.index != b.index ? a.index > b.index : a.sub > b.sub;
        });
        const bool changed = EditScriptData(*doc, script, "Delete", [&](InterfaceData &data) {
            for (const auto &ref : items) if (auto *record = FindRecord(data, ref.owner)) {
                if (ref.kind == ElementKind::Comment) record->comments.erase(record->comments.begin() + ref.index);
                else record->links[size_t(ref.index)].RemoveControlPoint(ref.sub);
            }
        });
        return Result(*doc, changed ? EditResult::Applied() : EditResult::NoChange(), sink);
    }
    static EditorStatus Invalid(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data); auto *doc = self.DocumentOf(*request); if (!doc) return EDITOR_STALE;
        self.Refresh(*doc);
        const auto expected = Number(*request, "revision"), first = Number(*request, "first");
        if (expected && expected != self.revision) return EDITOR_STALE;
        if (first > self.invalid.size()) return EDITOR_INVALID;
        EditorField rows{"rows", EDITOR_RECORD}; const auto end = std::min<uint64_t>(self.invalid.size(), first + 128);
        for (auto i = first; i < end; ++i) {
            const auto &link = self.invalid[size_t(i)]; EditorField row{std::to_string(i), EDITOR_RECORD};
            row.fields = {{"slot", EDITOR_INTEGER_VALUE, {}, {}, i}, {"script", EDITOR_TEXT_VALUE, {}, Name(doc->Context()->GetObject(link.script))},
                {"graph", EDITOR_TEXT_VALUE, {}, Name(doc->Context()->GetObject(link.graph))}, {"reason", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(link.reason)}, SchematicPosition(link.address, "address", doc->Reference(link.graph))};
            rows.fields.push_back(std::move(row));
        }
        return Write(sink, {{"revision", EDITOR_INTEGER_VALUE, {}, {}, self.revision}, {"total", EDITOR_INTEGER_VALUE, {}, {}, self.invalid.size()},
            {"next", EDITOR_INTEGER_VALUE, {}, {}, end}, std::move(rows)});
    }
    static EditorStatus Summary(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data); auto *doc = self.DocumentOf(*request); if (!doc) return EDITOR_STALE;
        self.Refresh(*doc);
        return Write(sink, {{"revision", EDITOR_INTEGER_VALUE, {}, {}, self.revision}, {"total", EDITOR_INTEGER_VALUE, {}, {}, self.invalid.size()}});
    }
    static EditorStatus ScanInvalid(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data); auto *doc = self.DocumentOf(*request); if (!doc) return EDITOR_STALE;
        self.invalid = FindInvalidLinks(*doc, !self.testTypes || self.testTypes()); ++self.revision;
        return Write(sink, {{"revision", EDITOR_INTEGER_VALUE, {}, {}, self.revision}, {"total", EDITOR_INTEGER_VALUE, {}, {}, self.invalid.size()}});
    }
    static EditorStatus Activity(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data); auto *doc = self.DocumentOf(*request); if (!doc) return EDITOR_STALE;
        const auto *target = Arg(*request, "target");
        auto *root = target ? CKBehavior::Cast(doc->Resolve(target->object)) : nullptr;
        if (!root || !doc->FindScript(root->GetID())) return EDITOR_STALE;
        EditorField running{"running", EDITOR_OBJECT_LIST}, firing{"firing", EDITOR_OBJECT_LIST};
        std::vector<CKBehavior *> pending{root}; std::set<CK_ID> visited;
        while (!pending.empty()) {
            auto *behavior = pending.back(); pending.pop_back();
            if (!behavior || !visited.insert(behavior->GetID()).second) continue;
            if (behavior != root && behavior->IsActive()) running.objects.push_back(doc->Reference(behavior->GetID()));
            for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i) pending.push_back(behavior->GetSubBehavior(i));
            for (int i = 0; i < behavior->GetSubBehaviorLinkCount(); ++i)
                if (auto *link = behavior->GetSubBehaviorLink(i)) if (auto *input = link->GetInBehaviorIO())
                    if (input->IsActive()) firing.objects.push_back(doc->Reference(link->GetID()));
        }
        return Write(sink, {std::move(running), std::move(firing), {"active", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(root->IsActive())}});
    }
    static EditorStatus DeleteInvalid(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data); auto *doc = self.DocumentOf(*request); if (!doc) return EDITOR_STALE;
        self.Refresh(*doc);
        if (Number(*request, "revision") != self.revision) return EDITOR_STALE;
        const auto *slots = Arg(*request, "slots"); if (!slots) return EDITOR_INVALID;
        std::vector<InvalidLink> links; std::set<uint64_t> unique;
        for (uint32_t i = 0; i < slots->integers.count; ++i) {
            const auto slot = slots->integers.data[i]; if (slot >= self.invalid.size() || !unique.insert(slot).second) return EDITOR_INVALID;
            const auto &link = self.invalid[size_t(slot)]; CK_ID script = 0; ElementRef ref;
            if (!link.address.Resolve(*doc, script, ref)) return EDITOR_STALE;
            const auto *session = doc->Session(script); if (!session || session->ReadOnly()) return EDITOR_REFUSED;
            links.push_back(link);
        }
        if (links.empty()) return EDITOR_NO_CHANGE;
        const auto result = vdev::DeleteInvalidLinks(*doc, links);
        if (!result.ok && !result.Unchanged()) return Result(*doc, result, sink);
        const auto removed = result.ok ? links.size() : 0;
        if (removed) { self.invalid = FindInvalidLinks(*doc, !self.testTypes || self.testTypes()); ++self.revision; }
        if (sink.write) Write(sink, {{"removed", EDITOR_INTEGER_VALUE, {}, {}, removed}});
        return result.Unchanged() ? EDITOR_NO_CHANGE : EDITOR_OK;
    }
    static EditorStatus ReadMessages(void *data, const EditorRequest *request, EditorSink sink, bool detailed) {
        auto &self = *static_cast<Data *>(data); auto *doc = self.DocumentOf(*request); if (!doc) return EDITOR_STALE;
        auto *context = doc->Context(); auto *messages = context->GetMessageManager(); auto *manager = context->GetParameterManager();
        if (const auto expected = Number(*request, "revision"); expected && expected != doc->ChangeRevision()) return EDITOR_STALE;
        const auto selected = Number(*request, "message");
        if (detailed && (!messages || selected >= uint64_t(messages->GetMessageTypeCount()))) return EDITOR_INVALID;
        const auto type = manager ? manager->ParameterGuidToType(CKPGUID_MESSAGE) : -1;
        std::map<int, uint64_t> counts;
        std::vector<EditorField> uses;
        for (const auto &info : doc->Scripts()) {
            auto *session = doc->Session(info.id); auto *script = CKBehavior::Cast(context->GetObject(info.id));
            if (!script) continue;
            std::vector<CKBehavior *> pending{script}; std::set<CK_ID> visited;
            while (!pending.empty()) {
                auto *behavior = pending.back(); pending.pop_back();
                if (!behavior || !visited.insert(behavior->GetID()).second) continue;
                for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i) pending.push_back(behavior->GetSubBehavior(i));
                const auto flags = behavior->GetFlags(); if (!(flags & (CKBEHAVIOR_MESSAGESENDER | CKBEHAVIOR_MESSAGERECEIVER))) continue;
                for (int i = 0; i < behavior->GetInputParameterCount(); ++i) {
                    auto *input = behavior->GetInputParameter(i); if (!input || input->GetType() != type) continue;
                    int message = -1; if (auto *source = input->GetRealSource()) source->GetValue(&message);
                    ++counts[message];
                    if (!detailed || message < 0 || uint64_t(message) != selected) continue;
                    const auto *record = session ? FindRecord(session->Data(), behavior->GetID()) : nullptr;
                    const auto address = ElementAddress::Capture(*doc, info.id, {ElementKind::Behavior, record ? record->parentId : info.id, behavior->GetID()});
                    EditorField use{std::to_string(uses.size()), EDITOR_RECORD};
                    use.fields = {{"object", EDITOR_OBJECT, doc->Reference(behavior->GetID())}, {"name", EDITOR_TEXT_VALUE, {}, Name(behavior)},
                        {"script", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(info.name)}, {"sender", EDITOR_INTEGER_VALUE, {}, {}, uint64_t((flags & CKBEHAVIOR_MESSAGESENDER) != 0)},
                        SchematicPosition(address, "address", doc->Reference(behavior->GetID()))};
                    uses.push_back(std::move(use));
                }
            }
        }
        const uint64_t count = detailed ? uses.size() : messages ? messages->GetMessageTypeCount() : 0;
        const auto first = Number(*request, "first");
        if (first > count) return EDITOR_INVALID;
        const auto end = std::min<uint64_t>(count, first + 128); EditorField rows{"rows", EDITOR_RECORD};
        for (auto i = first; i < end; ++i) {
            if (detailed) { rows.fields.push_back(std::move(uses[size_t(i)])); continue; }
            const auto *name = messages->GetMessageTypeName(int(i)); EditorField row{std::to_string(i), EDITOR_RECORD};
            row.fields = {{"type", EDITOR_INTEGER_VALUE, {}, {}, i}, {"name", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(name ? name : "")},
                {"count", EDITOR_INTEGER_VALUE, {}, {}, counts[int(i)]}};
            rows.fields.push_back(std::move(row));
        }
        return Write(sink, {{"revision", EDITOR_INTEGER_VALUE, {}, {}, doc->ChangeRevision()}, {"total", EDITOR_INTEGER_VALUE, {}, {}, count},
            {"next", EDITOR_INTEGER_VALUE, {}, {}, end}, std::move(rows)});
    }
    static EditorStatus Messages(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) { return ReadMessages(data, request, sink, false); }
    static EditorStatus MessageUses(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) { return ReadMessages(data, request, sink, true); }
};
SchematicGraphCommands::SchematicGraphCommands(std::function<Document *()> current, std::function<bool()> testTypes)
    : m_Data(std::make_unique<Data>(std::move(current), std::move(testTypes))) {}
SchematicGraphCommands::~SchematicGraphCommands() = default;
void SchematicGraphCommands::LoadedLinks(const std::vector<InvalidLink> &links) {
    auto *doc = m_Data->current(); m_Data->document = doc ? doc->Token() : DocumentToken{};
    m_Data->invalid = links; ++m_Data->revision;
}
const std::vector<ProviderChoice> &SchematicGraphCommands::DefaultProviders() {
    static const std::vector<ProviderChoice> choices{{"query", "schematic.invalid-links", "builtin.schematic-graph"},
        {"query", "schematic.invalid-summary", "builtin.schematic-graph"}, {"query", "schematic.messages", "builtin.schematic-graph"},
        {"command", "schematic.delete-invalid-links", "builtin.schematic-graph"}, {"query", "schematic.activity", "builtin.schematic-graph"},
        {"query", "schematic.message-uses", "builtin.schematic-graph"}, {"query", "schematic.script-state", "builtin.schematic-graph"}, {"command", "schematic.toggle-fold", "builtin.schematic-graph"},
        {"command", "schematic.toggle-targets", "builtin.schematic-graph"}, {"command", "schematic.create-graph", "builtin.schematic-graph"},
        {"command", "schematic.delete-elements", "builtin.schematic-graph"}, {"command", "schematic.refresh-invalid-links", "builtin.schematic-graph"}};
    return choices;
}
bool SchematicGraphCommands::Register(EditorApplication &editor, std::string &error) {
    const EditorParameter page[] = {{EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter fields[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("rows"), EDITOR_RECORD}};
    const EditorParameter usesArgs[] = {page[0], page[1], {EDITOR_TEXT("message"), EDITOR_INTEGER_VALUE}};
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT};
    const EditorParameter activity[] = {{EDITOR_TEXT("running"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("firing"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("active"), EDITOR_INTEGER_VALUE}};
    const EditorParameter scriptArgs[] = {target, {EDITOR_TEXT("id"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter scriptFields[] = {target, {EDITOR_TEXT("object"), EDITOR_OBJECT}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
            {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("readonly"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("graph"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.invalid-links"), EDITOR_TEXT("builtin.schematic-graph"), page, 2, Data::Invalid, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, fields, 4},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.invalid-summary"), EDITOR_TEXT("builtin.schematic-graph"), nullptr, 0, Data::Summary, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, fields, 2},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.messages"), EDITOR_TEXT("builtin.schematic-graph"), page, 2, Data::Messages, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, fields, 4},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.activity"), EDITOR_TEXT("builtin.schematic-graph"), &target, 1, Data::Activity, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, activity, 3},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.script-state"), EDITOR_TEXT("builtin.schematic-graph"), scriptArgs, 2, Data::ScriptState, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, scriptFields, 6},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.message-uses"), EDITOR_TEXT("builtin.schematic-graph"), usesArgs, 3, Data::MessageUses, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, fields, 4}};
    const EditorParameter args[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("slots"), EDITOR_INTEGER_LIST}};
    const EditorParameter fold[] = {target, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("object"), EDITOR_OBJECT}};
    const EditorParameter targets[] = {fold[0], fold[1], fold[2], {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}};
    const EditorParameter create[] = {fold[0], fold[1], fold[2], {EDITOR_TEXT("owner"), EDITOR_OBJECT}, {EDITOR_TEXT("at"), EDITOR_REAL_LIST}};
    const EditorParameter elements[] = {fold[0], fold[1], fold[2], {EDITOR_TEXT("elements"), EDITOR_RECORD}};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.delete-invalid-links"), EDITOR_TEXT("builtin.schematic-graph"), EDITOR_TEXT("Delete Invalid Links"), args, 2, Data::DeleteInvalid, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.toggle-fold"), EDITOR_TEXT("builtin.schematic-graph"), EDITOR_TEXT("Fold Graph"), fold, 4, Data::Fold, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.toggle-targets"), EDITOR_TEXT("builtin.schematic-graph"), EDITOR_TEXT("Target"), targets, 4, Data::Targets, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.create-graph"), EDITOR_TEXT("builtin.schematic-graph"), EDITOR_TEXT("Create Behavior Graph"), create, 5, Data::CreateGraph, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.delete-elements"), EDITOR_TEXT("builtin.schematic-graph"), EDITOR_TEXT("Delete"), elements, 4, Data::DeleteElements, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.refresh-invalid-links"), EDITOR_TEXT("builtin.schematic-graph"), EDITOR_TEXT("Refresh Invalid Links"), nullptr, 0, Data::ScanInvalid, m_Data.get(), EDITOR_DOCUMENT, EDITOR_RUNTIME_CONTROL}};
    EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("schematic.graph");
    module.queries = queries; module.queryCount = 6; module.commands = commands; module.commandCount = 6;
    return editor.Register(module, error);
}
} // namespace vdev
