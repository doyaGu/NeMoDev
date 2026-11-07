#include "core/schematic/SchematicModelBuilder.h"
#include "features/schematic/SchematicGraphCommands.h"
#include "application/FileLoadState.h"
#include "features/schematic/SchematicPosition.h"
#include "features/schematic/SchematicGeometry.h"
#include "features/schematic/SchematicConnections.h"
#include "features/schematic/SchematicCopying.h"
#include "features/schematic/SchematicAuthoring.h"
#include "features/schematic/SchematicParameterEdits.h"
#include "features/schematic/SchematicModelCommands.h"
#include "features/schematic/SchematicScriptCommands.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/document/LinkValidation.h"
#include "core/document/ScriptSession.h"
#include "core/runtime/TextEncoding.h"
#include "core/schematic/SchematicEdits.h"
#include "core/schematic/SchematicGrid.h"
#include "core/schematic/SchematicHitTest.h"
#include "core/graph/TopologyCommands.h"
#include "core/graph/RuntimeGraph.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <iterator>
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
EditorStatus Result(Document &doc, const EditResult &result, EditorSink sink, const ElementAddress &address = {}) {
    if (sink.write) Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(result.message)},
        {"created", EDITOR_OBJECT, doc.Reference(result.created)}, SchematicPosition(address)});
    return result.Unchanged() ? EDITOR_NO_CHANGE : result.ok ? EDITOR_OK : result.Status() == EditStatus::Refused ? EDITOR_REFUSED : EDITOR_ERROR;
}
} // namespace
struct SchematicGraphCommands::Data {
    std::function<Document *()> current;
    std::function<bool()> testTypes;
    SchematicGeometry geometry;
    SchematicConnections connections;
    SchematicCopying copying;
    SchematicAuthoring authoring;
    SchematicParameterEdits parameters;
    SchematicModelCommands models;
    SchematicScriptCommands scripts;
    DocumentToken document{};
    std::vector<InvalidLink> invalid;
    uint64_t revision = 1;
    Data(std::function<Document *()> current, std::function<bool()> testTypes, Clipboard &clipboard)
        : current(current), testTypes(std::move(testTypes)), geometry(current), connections(current),
          copying(current, clipboard), authoring(current), parameters(current), models(current),
          scripts(std::move(current)) {}
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
        return Write(sink, {{"target", EDITOR_OBJECT, target->object},
                            {"object", EDITOR_OBJECT, object ? doc->Reference(object->GetID()) : ObjectRef{}},
                            {"revision", EDITOR_INTEGER_VALUE, {}, {}, session->Revision()},
                            {"topology", EDITOR_INTEGER_VALUE, {}, {}, doc->TopologyRevision()},
                            {"readonly", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(session->ReadOnly())},
                            {"document-revision", EDITOR_INTEGER_VALUE, {}, {}, doc->ChangeRevision()},
                            {"graph",
                             EDITOR_INTEGER_VALUE,
                             {},
                             {},
                             uint64_t(CKBehavior::Cast(object) &&
                                      (CKBehavior::Cast(object)->GetType() & CKBEHAVIORTYPE_BEHAVIOR))}});
    }
    static EditorStatus Positions(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.DocumentOf(*r);
        if (!doc || doc->ChangeRevision() != Number(*r, "document-revision"))
            return EDITOR_STALE;
        const auto *elements = Arg(*r, "elements");
        EditorField result{"elements", EDITOR_RECORD};
        for (uint32_t i = 0; elements && i < elements->fields.count; ++i) {
            ElementAddress previous;
            if (!ReadSchematicPosition(elements->fields.data[i], previous))
                return EDITOR_INVALID;
            const auto script = doc->NativeId(previous.script);
            if (!script || (previous.owner && !doc->Resolve(previous.owner)) ||
                (previous.object && !doc->Resolve(previous.object)))
                return EDITOR_STALE;
            const auto address = ElementAddress::Capture(*doc, script, previous.element);
            if (!address || address.endpoint != previous.endpoint)
                return EDITOR_STALE;
            result.fields.push_back(SchematicPosition(address, std::to_string(i).c_str()));
        }
        return Write(sink, {result});
    }
    static EditorStatus Position(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.DocumentOf(*request);
        if (!doc)
            return EDITOR_STALE;
        const auto *target = Arg(*request, "target");
        const auto script = target ? doc->NativeId(target->object) : 0;
        auto *session = script ? doc->Session(script) : nullptr;
        if (!session || session->Revision() != Number(*request, "revision") ||
            doc->TopologyRevision() != Number(*request, "topology"))
            return EDITOR_STALE;
        const auto kind = Number(*request, "kind"), pin = Number(*request, "pin"), owner = Number(*request, "owner-id"),
                   object = Number(*request, "object-id"), index = Number(*request, "index"),
                   sub = Number(*request, "sub");
        if (kind > uint64_t(ElementKind::Start) || pin > uint64_t(PinKind::Target) || owner > UINT32_MAX ||
            object > UINT32_MAX || (index != UINT64_MAX && index != UINT64_MAX - 1 && index > INT_MAX) ||
            (sub != UINT64_MAX && sub > INT_MAX))
            return EDITOR_INVALID;
        ElementRef ref{ElementKind(kind),
                       uint32_t(owner),
                       uint32_t(object),
                       index == UINT64_MAX       ? -1
                       : index == UINT64_MAX - 1 ? -2
                                                 : int(index),
                       sub == UINT64_MAX ? -1 : int(sub),
                       PinKind(pin)};
        const auto address = ElementAddress::Capture(*doc, script, ref);
        uint32_t resolvedScript = 0;
        ElementRef resolved;
        if (!address.Resolve(*doc, resolvedScript, resolved))
            return EDITOR_STALE;
        return Write(sink, {SchematicPosition(address)});
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
    static EditorStatus DrawGraph(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.DocumentOf(*r);
        if (!doc || !self.Session(*doc, *r))
            return EDITOR_STALE;
        const auto script = Arg(*r, "target")->object.object;
        const auto *rect = Arg(*r, "rect");
        if (!rect || rect->reals.count != 4)
            return EDITOR_INVALID;
        for (uint32_t i = 0; i < 4; ++i)
            if (!std::isfinite(rect->reals.data[i]) || std::abs(rect->reals.data[i]) > 100000000)
                return EDITOR_INVALID;
        if (rect->reals.data[2] < 0 || rect->reals.data[3] < 0)
            return EDITOR_INVALID;
        const Rect bounds(float(rect->reals.data[0]), float(rect->reals.data[1]), float(rect->reals.data[2]),
                          float(rect->reals.data[3]));
        SchematicModel model;
        model = BuildSchematicModel(*doc->Session(script), doc->Context(), {}, true);
        std::vector<CK_ID> members;
        for (const auto &ref : ElementsInRect(model, bounds, true))
            if (ref.kind == ElementKind::Behavior)
                if (const auto *behavior = model.FindBehavior(ref.object)) {
                    const bool nested = std::find(members.begin(), members.end(), behavior->parent) != members.end();
                    if (!nested)
                        members.push_back(behavior->id);
                }
        if (!members.empty())
            return Result(*doc, GroupBehaviors(*doc, script, members), sink);
        const Point at(bounds.hPos, bounds.vPos);
        const auto graph = model.GraphAt(at);
        return Result(*doc, CreateBehaviorGraph(*doc, script, graph ? graph : script, "Behavior Graph", at), sink);
    }
    static EditorStatus Group(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.DocumentOf(*r);
        if (!doc || !self.Session(*doc, *r))
            return EDITOR_STALE;
        const auto script = Arg(*r, "target")->object.object;
        const auto *objects = Arg(*r, "objects");
        std::vector<CK_ID> ids;
        std::set<CK_ID> unique;
        for (uint32_t i = 0; objects && i < objects->objects.count; ++i) {
            auto *object = CKBehavior::Cast(doc->Resolve(objects->objects.data[i]));
            if (!object || !OwningScript(object) || OwningScript(object)->GetID() != script)
                return EDITOR_STALE;
            if (!unique.insert(object->GetID()).second)
                return EDITOR_INVALID;
            ids.push_back(object->GetID());
        }
        if (ids.empty())
            return EDITOR_NO_CHANGE;
        if (!Number(*r, "ungroup"))
            return Result(*doc, GroupBehaviors(*doc, script, ids), sink);
        if (Number(*r, "ungroup") > 1)
            return EDITOR_INVALID;
        bool changed = false;
        std::string message;
        for (auto id : ids) {
            const auto result = UngroupGraph(*doc, script, id);
            changed |= result.ok;
            if (!result.ok && !result.Unchanged())
                message = result.message;
        }
        if (changed) {
            auto result = EditResult::Applied();
            result.message = message;
            return Result(*doc, result, sink);
        }
        return Result(*doc, message.empty() ? EditResult::NoChange() : EditResult::Refused(message), sink);
    }
    static EditorStatus DeleteElements(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data); auto *doc = self.DocumentOf(*request); if (!doc) return EDITOR_STALE;
        if (!self.Session(*doc, *request)) return EDITOR_STALE;
        const auto script = Arg(*request, "target")->object.object; const auto *elements = Arg(*request, "elements");
        if (!elements) return EDITOR_INVALID;
        std::vector<ElementRef> items; std::set<ElementRef> unique; bool displayOnly = true;
        for (uint32_t i = 0; i < elements->fields.count; ++i) {
            ElementAddress address; CK_ID inScript = 0; ElementRef ref;
            if (!ReadSchematicPosition(elements->fields.data[i], address))
                return EDITOR_INVALID;
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
    static EditorStatus PickObjects(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.DocumentOf(*request);
        if (!doc)
            return EDITOR_STALE;
        const auto *target = Arg(*request, "target");
        auto *script = target ? CKBehavior::Cast(doc->Resolve(target->object)) : nullptr;
        if (!script || !doc->FindScript(script->GetID()))
            return EDITOR_STALE;
        CKObject *element = nullptr;
        CKParameter *parameter = nullptr;
        if (const auto *position = Arg(*request, "address")) {
            ElementAddress address;
            CK_ID scriptId = 0;
            ElementRef ref;
            if (!ReadSchematicPosition(*position, address))
                return EDITOR_INVALID;
            if (address.script != target->object || !address.Resolve(*doc, scriptId, ref))
                return EDITOR_STALE;
            element = doc->Resolve(address.object);
            if (ref.kind == ElementKind::LocalParam || ref.kind == ElementKind::Shortcut)
                parameter = CKParameter::Cast(element);
            else if (ref.kind == ElementKind::Pin) {
                // Resolve the retained endpoint, never a new object at the same pin index.
                auto *endpoint = doc->Resolve(address.endpoint);
                if (auto *input = CKParameterIn::Cast(endpoint))
                    parameter = input->GetRealSource();
                else
                    parameter = CKParameter::Cast(endpoint);
            }
        }
        EditorField objects{"objects", EDITOR_OBJECT_LIST};
        const auto add = [&](CKObject *object) {
            const auto ref = object ? doc->Reference(object->GetID()) : ObjectRef{};
            if (ref && !doc->IsDiscarded(ref.object) &&
                std::find(objects.objects.begin(), objects.objects.end(), ref) == objects.objects.end())
                objects.objects.push_back(ref);
        };
        // Parameter value, element, Script, then its owner: Dev's pick priority.
        auto *manager = doc->Context()->GetParameterManager();
        if (parameter && manager && manager->TypeToClassID(parameter->GetType()) != 0 &&
            parameter->GetDataSize() == int(sizeof(CK_ID))) {
            CK_ID held = 0;
            if (parameter->GetValue(&held, FALSE) == CK_OK)
                add(doc->Context()->GetObject(held));
        }
        add(element);
        add(script);
        add(script->GetOwner());
        return Write(sink, {std::move(objects)});
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
        const auto *session = doc->Session(root->GetID());
        if (!session)
            return EDITOR_STALE;
        return Write(sink, {std::move(running),
                            std::move(firing),
                            {"active", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(root->IsActive())},
                            {"revision", EDITOR_INTEGER_VALUE, {}, {}, session->Revision()},
                            {"topology", EDITOR_INTEGER_VALUE, {}, {}, doc->TopologyRevision()}});
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
SchematicGraphCommands::SchematicGraphCommands(std::function<Document *()> current, std::function<bool()> testTypes,
                                               Clipboard &clipboard)
    : m_Data(std::make_unique<Data>(std::move(current), std::move(testTypes), clipboard)) {}
SchematicGraphCommands::~SchematicGraphCommands() = default;
void SchematicGraphCommands::LoadedLinks(const std::vector<InvalidLink> &links) {
    auto *doc = m_Data->current(); m_Data->document = doc ? doc->Token() : DocumentToken{};
    m_Data->invalid = links; ++m_Data->revision;
}
const std::vector<ProviderChoice> &SchematicGraphCommands::DefaultProviders() {
    static const std::vector<ProviderChoice> choices = [] {
        std::vector<ProviderChoice> choices{
            {"query", "schematic.invalid-links", "builtin.schematic-graph"},
            {"query", "schematic.invalid-summary", "builtin.schematic-graph"},
            {"query", "schematic.messages", "builtin.schematic-graph"},
            {"command", "schematic.delete-invalid-links", "builtin.schematic-graph"},
            {"query", "schematic.activity", "builtin.schematic-graph"},
            {"query", "schematic.pick-objects", "builtin.schematic-graph"},
            {"query", "schematic.message-uses", "builtin.schematic-graph"},
            {"query", "schematic.script-state", "builtin.schematic-graph"},
            {"command", "schematic.toggle-fold", "builtin.schematic-graph"},
            {"command", "schematic.toggle-targets", "builtin.schematic-graph"},
            {"command", "schematic.create-graph", "builtin.schematic-graph"},
            {"command", "schematic.group", "builtin.schematic-graph"},
            {"command", "schematic.ungroup", "builtin.schematic-graph"},
            {"command", "schematic.draw-graph", "builtin.schematic-graph"},
            {"command", "schematic.delete-elements", "builtin.schematic-graph"},
            {"command", "schematic.refresh-invalid-links", "builtin.schematic-graph"},
            {"query", "schematic.clipboard", "builtin.schematic-copying"},
            {"query", "schematic.copy-selection", "builtin.schematic-copying"},
            {"command", "schematic.copy", "builtin.schematic-copying"},
            {"command", "schematic.cut", "builtin.schematic-copying"},
            {"command", "schematic.paste", "builtin.schematic-copying"},
            {"command", "schematic.paste-shortcuts", "builtin.schematic-copying"},
            {"command", "schematic.duplicate", "builtin.schematic-copying"},
            {"command", "schematic.copy-elements", "builtin.schematic-copying"},
            {"query", "schematic.connection", "builtin.schematic-connections"},
            {"command", "schematic.route-links", "builtin.schematic-connections"},
            {"command", "schematic.insert-control-point", "builtin.schematic-connections"},
            {"command", "schematic.connect", "builtin.schematic-connections"},
            {"command", "schematic.export-pins", "builtin.schematic-connections"},
            {"query", "schematic.positions", "builtin.schematic-graph"},
            {"query", "schematic.position", "builtin.schematic-graph"},
            {"query", "schematic.geometry", "builtin.schematic-geometry"},
            {"command", "schematic.edit-geometry", "builtin.schematic-geometry"}};
        const auto &authoring = SchematicAuthoring::DefaultProviders();
        choices.insert(choices.end(), authoring.begin(), authoring.end());
        const auto &parameters = SchematicParameterEdits::DefaultProviders();
        choices.insert(choices.end(), parameters.begin(), parameters.end());
        const auto &model = SchematicModelCommands::DefaultProviders();
        choices.insert(choices.end(), model.begin(), model.end());
        const auto &scripts = SchematicScriptCommands::DefaultProviders();
        choices.insert(choices.end(), scripts.begin(), scripts.end());
        return choices;
    }();
    return choices;
}
bool SchematicGraphCommands::Register(EditorApplication &editor, std::string &error) {
    const EditorParameter page[] = {{EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter fields[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("rows"), EDITOR_RECORD}};
    const EditorParameter usesArgs[] = {page[0], page[1], {EDITOR_TEXT("message"), EDITOR_INTEGER_VALUE}};
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT};
    const EditorParameter activity[] = {{EDITOR_TEXT("running"), EDITOR_OBJECT_LIST},
                                        {EDITOR_TEXT("firing"), EDITOR_OBJECT_LIST},
                                        {EDITOR_TEXT("active"), EDITOR_INTEGER_VALUE},
                                        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
                                        {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE}};
    const EditorParameter scriptArgs[] = {target, {EDITOR_TEXT("id"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter scriptFields[] = {target,
                                            {EDITOR_TEXT("object"), EDITOR_OBJECT},
                                            {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("readonly"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("graph"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("document-revision"), EDITOR_INTEGER_VALUE}};
    const EditorParameter position[] = {target,
                                        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
                                        {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE},
                                        {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE},
                                        {EDITOR_TEXT("owner-id"), EDITOR_INTEGER_VALUE},
                                        {EDITOR_TEXT("object-id"), EDITOR_INTEGER_VALUE},
                                        {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE},
                                        {EDITOR_TEXT("sub"), EDITOR_INTEGER_VALUE},
                                        {EDITOR_TEXT("pin"), EDITOR_INTEGER_VALUE}};
    const EditorParameter address{EDITOR_TEXT("address"), EDITOR_RECORD};
    const EditorParameter pickArgs[] = {target, {EDITOR_TEXT("address"), EDITOR_RECORD, 1}};
    const EditorParameter pickObjects{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST};
    const EditorParameter positions[] = {{EDITOR_TEXT("elements"), EDITOR_RECORD},
                                         {EDITOR_TEXT("document-revision"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.pick-objects"), EDITOR_TEXT("builtin.schematic-graph"),
         pickArgs, 2, Data::PickObjects, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, &pickObjects, 1},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.positions"), EDITOR_TEXT("builtin.schematic-graph"), positions,
         2, Data::Positions, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, positions, 1},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.position"), EDITOR_TEXT("builtin.schematic-graph"), position,
         9, Data::Position, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, &address, 1},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.invalid-links"), EDITOR_TEXT("builtin.schematic-graph"), page,
         2, Data::Invalid, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, fields, 4},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.invalid-summary"), EDITOR_TEXT("builtin.schematic-graph"),
         nullptr, 0, Data::Summary, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, fields, 2},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.messages"), EDITOR_TEXT("builtin.schematic-graph"), page, 2,
         Data::Messages, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, fields, 4},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.activity"), EDITOR_TEXT("builtin.schematic-graph"), &target, 1,
         Data::Activity, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, activity, 5},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.script-state"), EDITOR_TEXT("builtin.schematic-graph"),
         scriptArgs, 2, Data::ScriptState, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, scriptFields, 7},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.message-uses"), EDITOR_TEXT("builtin.schematic-graph"),
         usesArgs, 3, Data::MessageUses, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, fields, 4}};
    const EditorParameter args[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("slots"), EDITOR_INTEGER_LIST}};
    const EditorParameter fold[] = {target, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("object"), EDITOR_OBJECT}};
    const EditorParameter targets[] = {fold[0], fold[1], fold[2], {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}};
    const EditorParameter create[] = {fold[0], fold[1], fold[2], {EDITOR_TEXT("owner"), EDITOR_OBJECT}, {EDITOR_TEXT("at"), EDITOR_REAL_LIST}};
    const EditorParameter elements[] = {fold[0], fold[1], fold[2], {EDITOR_TEXT("elements"), EDITOR_RECORD}};
    const EditorParameter draw[] = {fold[0], fold[1], fold[2], {EDITOR_TEXT("rect"), EDITOR_REAL_LIST}};
    const auto group = [](void *data, const EditorHost *host, const EditorRequest *r, EditorSink sink) {
        std::vector<EditorValue> values(r->values, r->values + r->count);
        values.push_back({EDITOR_TEXT("ungroup"), EDITOR_INTEGER_VALUE});
        auto request = *r;
        request.values = values.data();
        request.count = uint32_t(values.size());
        return Data::Group(data, host, &request, sink);
    };
    const auto ungroup = [](void *data, const EditorHost *host, const EditorRequest *r, EditorSink sink) {
        std::vector<EditorValue> values(r->values, r->values + r->count);
        values.push_back({EDITOR_TEXT("ungroup"), EDITOR_INTEGER_VALUE, {}, {}, 1});
        auto request = *r;
        request.values = values.data();
        request.count = uint32_t(values.size());
        return Data::Group(data, host, &request, sink);
    };
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.draw-graph"), EDITOR_TEXT("builtin.schematic-graph"),
         EDITOR_TEXT("Draw Behavior Graph"), draw, 4, Data::DrawGraph, m_Data.get(), EDITOR_DOCUMENT,
         EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.group"), EDITOR_TEXT("builtin.schematic-graph"),
         EDITOR_TEXT("Group Behaviors"), targets, 4, group, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.ungroup"), EDITOR_TEXT("builtin.schematic-graph"),
         EDITOR_TEXT("Ungroup Behaviors"), targets, 4, ungroup, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.delete-invalid-links"),
         EDITOR_TEXT("builtin.schematic-graph"), EDITOR_TEXT("Delete Invalid Links"), args, 2, Data::DeleteInvalid,
         m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.toggle-fold"), EDITOR_TEXT("builtin.schematic-graph"),
         EDITOR_TEXT("Fold Graph"), fold, 4, Data::Fold, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.toggle-targets"), EDITOR_TEXT("builtin.schematic-graph"),
         EDITOR_TEXT("Target"), targets, 4, Data::Targets, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.create-graph"), EDITOR_TEXT("builtin.schematic-graph"),
         EDITOR_TEXT("Create Behavior Graph"), create, 5, Data::CreateGraph, m_Data.get(), EDITOR_DOCUMENT,
         EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.delete-elements"), EDITOR_TEXT("builtin.schematic-graph"),
         EDITOR_TEXT("Delete"), elements, 4, Data::DeleteElements, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.refresh-invalid-links"),
         EDITOR_TEXT("builtin.schematic-graph"), EDITOR_TEXT("Refresh Invalid Links"), nullptr, 0, Data::ScanInvalid,
         m_Data.get(), EDITOR_DOCUMENT, EDITOR_RUNTIME_CONTROL}};
    EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("schematic.graph");
    module.data = this;
    module.document = [](void *data, const EditorHost *host, const EditorDocumentChange *change) {
        auto &self = *static_cast<SchematicGraphCommands *>(data);
        if (change->phase == EDITOR_DOCUMENT_REPLACED) self.LoadedLinks({});
        if (change->phase == EDITOR_DOCUMENT_LOADED) {
            FileLoadState loaded;
            auto *doc = self.m_Data->current();
            if (!ReadFileLoadState(host, loaded) || !doc) return EDITOR_ERROR;
            if (loaded.opened && loaded.composition) {
                std::vector<InvalidLink> invalid;
                uint64_t first = 0;
                for (;;) {
                    const EditorValue firstValue{EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, {}, {}, first};
                    const EditorRequest request{host->scope(host->data, 0), &firstValue, 1};
                    std::vector<EditorField> fields;
                    const EditorSink sink{&fields, [](void *data, const EditorValue *values, uint32_t count) {
                        return CopyEditorFields(*static_cast<std::vector<EditorField> *>(data), values, count);
                    }};
                    if (host->query(host->data, EditorString EDITOR_TEXT("document.file-invalid-links"), &request, sink) != EDITOR_OK)
                        return EDITOR_ERROR;
                    const EditorField *rows = nullptr, *next = nullptr, *total = nullptr;
                    for (const auto &field : fields) {
                        if (field.key == "rows") rows = &field;
                        if (field.key == "next") next = &field;
                        if (field.key == "total") total = &field;
                    }
                    if (!rows || rows->type != EDITOR_RECORD || !next || next->type != EDITOR_INTEGER_VALUE ||
                        !total || total->type != EDITOR_INTEGER_VALUE || next->integer > total->integer ||
                        next->integer < first || rows->fields.size() != next->integer - first) return EDITOR_INVALID;
                    for (const auto &row : rows->fields) {
                        if (row.type != EDITOR_RECORD || row.fields.size() != 6) return EDITOR_INVALID;
                        InvalidLink link;
                        unsigned seen = 0;
                        for (const auto &field : row.fields) {
                            unsigned bit = 0;
                            if (field.key == "script" && field.type == EDITOR_OBJECT) { bit = 1; link.script = doc->NativeId(field.object); }
                            else if (field.key == "graph" && field.type == EDITOR_OBJECT) { bit = 2; link.graph = doc->NativeId(field.object); }
                            else if (field.key == "slot" && field.type == EDITOR_INTEGER_VALUE && field.integer <= INT32_MAX) { bit = 4; link.slot = int(field.integer); }
                            else if (field.key == "stored-id" && field.type == EDITOR_INTEGER_VALUE && field.integer <= UINT32_MAX) { bit = 8; link.link = CK_ID(field.integer); }
                            else if (field.key == "behavior" && field.type == EDITOR_INTEGER_VALUE && field.integer <= 1) { bit = 16; link.behaviorLink = field.integer != 0; }
                            else if (field.key == "reason" && field.type == EDITOR_TEXT_VALUE) { bit = 32; link.reason = field.text; }
                            if (!bit || (seen & bit)) return EDITOR_INVALID;
                            seen |= bit;
                        }
                        if (seen != 63 || !link.script) return EDITOR_INVALID;
                        link.address = ElementAddress::Capture(*doc, link.script, {ElementKind::Link, link.graph, link.link, link.slot});
                        invalid.push_back(std::move(link));
                    }
                    if (next->integer == total->integer) break;
                    if (next->integer == first) return EDITOR_INVALID;
                    first = next->integer;
                }
                self.LoadedLinks(invalid);
            }
        }
        return EDITOR_OK;
    };
    module.queries = queries;
    module.queryCount = uint32_t(std::size(queries));
    module.commands = commands;
    module.commandCount = uint32_t(std::size(commands));
    return editor.Register(module, error) && m_Data->geometry.Register(editor, error) &&
           m_Data->connections.Register(editor, error) && m_Data->copying.Register(editor, error) &&
           m_Data->authoring.Register(editor, error) && m_Data->parameters.Register(editor, error) &&
           m_Data->models.Register(editor, error) && m_Data->scripts.Register(editor, error);
}
} // namespace vdev
