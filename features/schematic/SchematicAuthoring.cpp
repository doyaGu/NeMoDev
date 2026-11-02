#include "features/schematic/SchematicAuthoring.h"
#include "features/schematic/SchematicPosition.h"
#include "core/document/Document.h"
#include "core/document/ScriptSession.h"
#include "core/catalog/ParameterCatalog.h"
#include "core/catalog/ValueText.h"
#include "core/graph/BehaviorCommands.h"
#include "core/graph/OperationCommands.h"
#include "core/graph/ReplaceCommands.h"
#include "core/graph/RuntimeGraph.h"
#include "core/graph/TopologyCommands.h"
#include "core/graph/ValueCommands.h"
#include "core/runtime/TextEncoding.h"
#include "core/schematic/SchematicEdits.h"
#include "core/schematic/SchematicGrid.h"
#include "core/schematic/SchematicSearch.h"
#include "core/schematic/SchematicModel.h"
#include "core/schematic/ParameterDisplayGeometry.h"
#include "core/graph/ShortcutCommands.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <set>

namespace vdev {
namespace {
const EditorValue *Arg(const EditorRequest &r, const char *key) {
    for (uint32_t i = 0; i < r.count; ++i)
        if (std::string(r.values[i].key.data, r.values[i].key.size) == key)
            return r.values + i;
    return nullptr;
}
uint64_t Number(const EditorRequest &r, const char *key) {
    const auto *v = Arg(r, key);
    return v ? v->integer : 0;
}
std::string Text(const EditorRequest &r, const char *key) {
    const auto *v = Arg(r, key);
    return v ? std::string(v->text.data ? v->text.data : "", v->text.size) : std::string();
}
ObjectRef Object(const EditorRequest &r, const char *key) {
    const auto *v = Arg(r, key);
    return v ? v->object : ObjectRef{};
}
uint64_t Type(CKGUID guid) { return uint64_t(guid.d1) << 32 | guid.d2; }
CKGUID Guid(uint64_t value) { return CKGUID(CKDWORD(value >> 32), CKDWORD(value)); }
std::string Name(CKObject *o) { return o ? AnsiToUtf8(o->GetName() ? o->GetName() : "") : std::string(); }
std::string ObjectText(CKObject *o) {
    const auto *cid = o ? CKClassIDToString(o->GetClassID()) : nullptr;
    return o ? Name(o) + "  [" + std::to_string(o->GetID()) + ", " + AnsiToUtf8(cid ? cid : "?") + "]" : "(none)";
}
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto v = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, v.data(), uint32_t(v.size())) : EDITOR_INVALID;
}
EditorStatus Result(Document &doc, const EditResult &r, EditorSink sink, const ElementAddress &address = {}) {
    Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(r.message)},
                 {"created", EDITOR_OBJECT, doc.Reference(r.created)},
                 SchematicPosition(address)});
    return r.ok                                ? EDITOR_OK
           : r.Unchanged()                     ? EDITOR_NO_CHANGE
           : r.Status() == EditStatus::Refused ? EDITOR_REFUSED
                                               : EDITOR_ERROR;
}
bool PointOf(const EditorRequest &r, Point &point) {
    const auto *v = Arg(r, "at");
    if (!v || v->reals.count != 2)
        return false;
    for (uint32_t i = 0; i < 2; ++i)
        if (!std::isfinite(v->reals.data[i]) || std::abs(v->reals.data[i]) > 100000000)
            return false;
    point = Point(float(v->reals.data[0]), float(v->reals.data[1]));
    return true;
}
std::vector<EditorField> Information(Document &doc, CKObject *object) {
    auto *behavior = CKBehavior::Cast(object);
    auto *operation = CKParameterOperation::Cast(object);
    auto *input = CKParameterIn::Cast(object);
    auto *parameter = CKParameter::Cast(object);
    CKObject *owner =
        behavior    ? (behavior->GetParent() ? static_cast<CKObject *>(behavior->GetParent()) : behavior->GetOwner())
        : operation ? static_cast<CKObject *>(operation->GetOwner())
        : input     ? input->GetOwner()
        : parameter ? parameter->GetOwner()
                    : nullptr;
    auto *root = OwningScript(object);
    if (behavior && doc.FindScript(behavior->GetID()))
        root = behavior;
    auto *value = input ? input->GetRealSource() : parameter;
    auto *local = CKParameterLocal::Cast(value);
    auto *pm = doc.Context()->GetParameterManager();
    const auto guid = input ? input->GetGUID() : parameter ? parameter->GetGUID() : CKGUID();
    const auto nativeType = pm ? pm->ParameterGuidToType(guid) : -1;
    const auto *type = nativeType >= 0 ? pm->ParameterTypeToName(nativeType) : nullptr;
    const auto *className = object ? CKClassIDToString(object->GetClassID()) : nullptr;
    const bool editableGraph =
        behavior && behavior->GetType() != CKBEHAVIORTYPE_BASE && !(behavior->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK);
    uint64_t pins = 0;
    for (auto kind : {PinKind::BehaviorIn, PinKind::BehaviorOut, PinKind::ParamIn, PinKind::ParamOut})
        if (behavior && (editableGraph || AcceptsVariablePin(behavior, kind)))
            pins |= 1ull << unsigned(kind);
    EditorField destinations{"destinations", EDITOR_TEXT_LIST};
    if (auto *out = CKParameterOut::Cast(parameter))
        for (int i = 0; i < out->GetDestinationCount(); ++i)
            destinations.texts.push_back(ObjectText(out->GetDestination(i)));
    EditorField signature{"signature", EDITOR_INTEGER_LIST};
    if (operation)
        signature.integers = {
            Type(operation->GetInParameter1() ? operation->GetInParameter1()->GetGUID() : CKPGUID_NONE),
            Type(operation->GetInParameter2() ? operation->GetInParameter2()->GetGUID() : CKPGUID_NONE),
            Type(operation->GetOutParameter() ? operation->GetOutParameter()->GetGUID() : CKPGUID_NONE),
            Type(operation->GetOperationGuid())};
    return {{"object", EDITOR_OBJECT, doc.Reference(object ? object->GetID() : 0)},
            {"name", EDITOR_TEXT_VALUE, {}, Name(object)},
            {"class", EDITOR_TEXT_VALUE, {}, className ? AnsiToUtf8(className) : ""},
            {"description", EDITOR_TEXT_VALUE, {}, ObjectText(object)},
            {"owner", EDITOR_OBJECT, doc.Reference(owner ? owner->GetID() : 0)},
            {"owner-name", EDITOR_TEXT_VALUE, {}, ObjectText(owner)},
            {"script", EDITOR_OBJECT, doc.Reference(root ? root->GetID() : 0)},
            {"graph", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(editableGraph)},
            {"operation", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(operation != nullptr)},
            {"settings", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(behavior && !BehaviorSettings(behavior).empty())},
            {"pin-kinds", EDITOR_INTEGER_VALUE, {}, {}, pins},
            {"targetable", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(behavior && behavior->IsTargetable())},
            {"using-target", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(behavior && behavior->IsUsingTarget())},
            {"prototype", EDITOR_INTEGER_VALUE, {}, {}, behavior ? Type(behavior->GetPrototypeGuid()) : 0},
            {"type", EDITOR_INTEGER_VALUE, {}, {}, Type(guid)},
            {"type-name", EDITOR_TEXT_VALUE, {}, type ? AnsiToUtf8(type) : ""},
            {"display", EDITOR_TEXT_VALUE, {}, value ? AnsiToUtf8(ParameterText(value)) : "NA"},
            {"self", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(local && local->IsMyselfParameter())},
            {"input", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(input != nullptr)},
            {"output", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(CKParameterOut::Cast(parameter) != nullptr)},
            {"enabled", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(input && input->IsEnabled())},
            {"direct-source", EDITOR_TEXT_VALUE, {}, ObjectText(input ? input->GetDirectSource() : nullptr)},
            {"shared-source", EDITOR_TEXT_VALUE, {}, ObjectText(input ? input->GetSharedSource() : nullptr)},
            std::move(destinations),
            std::move(signature)};
}
ParameterStyle NextStyle(int style) {
    switch (ParamDisplayMode(style)) {
    case PARAM_STYLE_COLLAPSED:
        return PARAM_STYLE_NAME;
    case PARAM_STYLE_NAME:
        return PARAM_STYLE_NAMEVALUE;
    case PARAM_STYLE_NAMEVALUE:
        return PARAM_STYLE_VALUE;
    default:
        return PARAM_STYLE_COLLAPSED;
    }
}
} // namespace
struct SchematicAuthoring::Data {
    std::function<Document *()> current;
    enum Action { Comment, Block, Local, This, Pin, Operation, Display, Replace, Shortcutize, Retarget };
    struct Entry {
        Data *owner;
        Action action;
    } entries[10];
    explicit Data(std::function<Document *()> current)
        : current(std::move(current)),
          entries{{this, Comment},   {this, Block},   {this, Local},   {this, This},        {this, Pin},
                  {this, Operation}, {this, Display}, {this, Replace}, {this, Shortcutize}, {this, Retarget}} {}
    Document *DocumentOf(const EditorRequest &r) const {
        auto *doc = current();
        return doc && doc->IsOpen() && doc->Token() == r.scope.document ? doc : nullptr;
    }
    EditorStatus Session(Document &doc, const EditorRequest &r, CK_ID &script) const {
        script = doc.NativeId(Object(r, "target"));
        auto *session = script && doc.FindScript(script) ? doc.Session(script) : nullptr;
        if (!session || session->Revision() != Number(r, "revision") || doc.TopologyRevision() != Number(r, "topology"))
            return EDITOR_STALE;
        return session->ReadOnly() ? EDITOR_REFUSED : EDITOR_OK;
    }
    static EditorStatus Info(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *doc = static_cast<Data *>(data)->DocumentOf(*r);
        auto *o = doc ? doc->Resolve(Object(*r, "target")) : nullptr;
        return o ? Write(sink, Information(*doc, o)) : EDITOR_STALE;
    }
    static EditorStatus Element(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *doc = static_cast<Data *>(data)->DocumentOf(*r);
        ElementAddress address;
        ElementRef ref;
        CK_ID script = 0;
        const auto *value = Arg(*r, "address");
        if (!value || !ReadSchematicPosition(*value, address))
            return EDITOR_INVALID;
        if (!doc || !address.Resolve(*doc, script, ref))
            return EDITOR_STALE;
        auto *object = doc->Resolve(ref.kind == ElementKind::Pin ? address.endpoint : address.object);
        return object ? Write(sink, Information(*doc, object)) : EDITOR_STALE;
    }
    static EditorStatus Types(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *doc = static_cast<Data *>(data)->DocumentOf(*r);
        if (!doc)
            return EDITOR_STALE;
        EditorField types{"types", EDITOR_RECORD};
        for (const auto &entry : ListParameterTypes(*doc)) {
            EditorField row{std::to_string(types.fields.size()), EDITOR_RECORD};
            row.fields = {{"type", EDITOR_INTEGER_VALUE, {}, {}, Type(entry.guid)},
                          {"name", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(entry.name)}};
            types.fields.push_back(std::move(row));
        }
        return Write(sink, {std::move(types), {"none", EDITOR_INTEGER_VALUE, {}, {}, Type(CKPGUID_NONE)}});
    }
    static EditorStatus Operations(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *doc = static_cast<Data *>(data)->DocumentOf(*r);
        if (!doc)
            return EDITOR_STALE;
        const auto *parts = Arg(*r, "parts"), *locks = Arg(*r, "locks");
        if (!parts || parts->integers.count != 4 || !locks || locks->integers.count != 4 || Number(*r, "easy") > 1)
            return EDITOR_INVALID;
        CKGUID p[4];
        bool locked[4];
        for (int i = 0; i < 4; ++i) {
            p[i] = Guid(parts->integers.data[i]);
            if (locks->integers.data[i] > 1)
                return EDITOR_INVALID;
            locked[i] = Number(*r, "easy") && locks->integers.data[i];
        }
        std::vector<CKGUID> choices[4];
        choices[0].push_back(CKPGUID_NONE);
        for (const auto &type : ListParameterTypes(*doc))
            choices[0].push_back(type.guid);
        choices[1] = choices[2] = choices[0];
        for (const auto &operation : ListParameterOperations(doc->Context()))
            choices[3].push_back(operation.guid);
        const auto distinct = [](const std::vector<OperationSignature> &list, int part) {
            std::vector<CKGUID> result;
            for (const auto &s : list) {
                auto g = part == 0 ? s.input1 : part == 1 ? s.input2 : part == 2 ? s.result : s.operation;
                if (std::find(result.begin(), result.end(), g) == result.end())
                    result.push_back(g);
            }
            return result;
        };
        if (std::any_of(locked, locked + 4, [](bool b) { return b; })) {
            const auto fitting =
                AvailableOperationSignatures(doc->Context(), locked[3] ? p + 3 : nullptr, locked[2] ? p + 2 : nullptr,
                                             locked[0] ? p : nullptr, locked[1] ? p + 1 : nullptr);
            for (int i = 0; i < 4; ++i)
                choices[i] = locked[i] ? std::vector<CKGUID>{p[i]} : distinct(fitting, i);
        } else if (!Number(*r, "easy"))
            choices[3] = distinct(AvailableOperationSignatures(doc->Context(), nullptr, p + 2, p, p + 1), 3);
        EditorField fields{"choices", EDITOR_RECORD};
        auto *pm = doc->Context()->GetParameterManager();
        for (int i = 0; i < 4; ++i) {
            EditorField row{std::to_string(i), EDITOR_RECORD};
            for (auto g : choices[i]) {
                EditorField choice{std::to_string(row.fields.size()), EDITOR_RECORD};
                const auto nativeType = i < 3 ? pm->ParameterGuidToType(g) : -1;
                const auto *name = i == 3              ? pm->OperationGuidToName(g)
                                   : g == CKPGUID_NONE ? "None"
                                   : nativeType >= 0   ? pm->ParameterTypeToName(nativeType)
                                                       : nullptr;
                choice.fields = {{"type", EDITOR_INTEGER_VALUE, {}, {}, Type(g)},
                                 {"name", EDITOR_TEXT_VALUE, {}, name ? AnsiToUtf8(name) : "?"}};
                row.fields.push_back(std::move(choice));
            }
            fields.fields.push_back(std::move(row));
        }
        const bool valid = !AvailableOperationSignatures(doc->Context(), p + 3, p + 2, p, p + 1).empty(),
                   direct =
                       valid && !AvailableOperationSignatures(doc->Context(), p + 3, p + 2, p, p + 1, false).empty();
        EditorField names{"names", EDITOR_TEXT_LIST};
        for (int i = 0; i < 4; ++i) {
            const auto nativeType = i < 3 ? pm->ParameterGuidToType(p[i]) : -1;
            const auto *name = i == 3                 ? pm->OperationGuidToName(p[i])
                               : p[i] == CKPGUID_NONE ? "None"
                               : nativeType >= 0      ? pm->ParameterTypeToName(nativeType)
                                                      : nullptr;
            names.texts.push_back(name ? AnsiToUtf8(name) : "None");
        }
        return Write(sink, {fields,
                            std::move(names),
                            {"valid", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(valid)},
                            {"direct", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(direct)}});
    }
    static EditorStatus Replacements(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *doc = static_cast<Data *>(data)->DocumentOf(*r);
        auto *object = doc ? CKBehavior::Cast(doc->Resolve(Object(*r, "target"))) : nullptr;
        if (!object)
            return EDITOR_STALE;
        EditorField objects{"objects", EDITOR_OBJECT_LIST}, scripts{"scripts", EDITOR_OBJECT_LIST},
            rows{"graphs", EDITOR_RECORD};
        for (const auto &target : SameNamedBehaviors(*doc, object->GetID())) {
            scripts.objects.push_back(doc->Reference(target.first));
            objects.objects.push_back(doc->Reference(target.second));
        }
        const uint64_t first = Number(*r, "first");
        uint64_t count = 0;
        for (const auto &info : doc->Scripts()) {
            std::vector<CKBehavior *> pending{CKBehavior::Cast(doc->Context()->GetObject(info.id))};
            std::set<CK_ID> visited;
            while (!pending.empty()) {
                auto *b = pending.back();
                pending.pop_back();
                if (!b || !visited.insert(b->GetID()).second)
                    continue;
                for (int i = b->GetSubBehaviorCount(); i-- > 0;)
                    pending.push_back(b->GetSubBehavior(i));
                if (b->GetID() == info.id || b == object || b->GetType() == CKBEHAVIORTYPE_BASE ||
                    (b->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK))
                    continue;
                if (count >= first && count - first < 128) {
                    EditorField row{std::to_string(count), EDITOR_RECORD};
                    row.fields = {{"object", EDITOR_OBJECT, doc->Reference(b->GetID())},
                                  {"name", EDITOR_TEXT_VALUE, {}, Name(b)},
                                  {"script", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(info.name)}};
                    rows.fields.push_back(std::move(row));
                }
                ++count;
            }
        }
        if (first > count)
            return EDITOR_INVALID;
        return Write(sink, {{"revision", EDITOR_INTEGER_VALUE, {}, {}, doc->ChangeRevision()},
                            {"name", EDITOR_TEXT_VALUE, {}, Name(object)},
                            {"description", EDITOR_TEXT_VALUE, {}, ObjectText(object)},
                            std::move(objects),
                            std::move(scripts),
                            std::move(rows),
                            {"next", EDITOR_INTEGER_VALUE, {}, {}, std::min(first + 128, count)},
                            {"total", EDITOR_INTEGER_VALUE, {}, {}, count}});
    }
    static EditorStatus Shortcuts(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *doc = static_cast<Data *>(data)->DocumentOf(*r);
        auto *object = doc ? CKParameter::Cast(doc->Resolve(Object(*r, "target"))) : nullptr;
        if (!object || Number(*r, "mode") > 1)
            return EDITOR_STALE;
        SearchQuery query;
        query.id = object->GetID();
        query.kinds = SearchParameters;
        EditorField rows{"rows", EDITOR_RECORD};
        for (const auto &found : SearchDocument(*doc, query))
            if ((found.ref.kind == ElementKind::Shortcut) == (Number(*r, "mode") != 0)) {
                const auto *script = doc->FindScript(found.script);
                EditorField row{std::to_string(rows.fields.size()), EDITOR_RECORD};
                row.fields = {
                    {"label", EDITOR_TEXT_VALUE, {}, (script ? AnsiToUtf8(script->name) : "?") + "  /  " + found.label},
                    SchematicPosition(found.address, "address", doc->Reference(found.focus))};
                rows.fields.push_back(std::move(row));
                if (!Number(*r, "mode"))
                    break;
            }
        return Write(sink, {{"source", EDITOR_TEXT_VALUE, {}, ObjectText(object)},
                            {"owner", EDITOR_TEXT_VALUE, {}, ObjectText(object->GetOwner())},
                            std::move(rows)});
    }
    static EditorStatus Run(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &entry = *static_cast<Entry *>(data);
        auto &self = *entry.owner;
        auto *doc = self.DocumentOf(*r);
        if (!doc)
            return EDITOR_STALE;
        if (entry.action == Replace) {
            if (Number(*r, "revision") != doc->ChangeRevision())
                return EDITOR_STALE;
            auto *source = CKBehavior::Cast(doc->Resolve(Object(*r, "source")));
            const auto *objects = Arg(*r, "objects");
            if (!source || !objects || !objects->objects.count)
                return EDITOR_STALE;
            std::vector<std::pair<CK_ID, CK_ID>> targets;
            std::set<ObjectRef> unique;
            for (uint32_t i = 0; i < objects->objects.count; ++i) {
                auto ref = objects->objects.data[i];
                auto *target = CKBehavior::Cast(doc->Resolve(ref));
                auto *script = target ? OwningScript(target) : nullptr;
                auto *session = script ? doc->Session(script->GetID()) : nullptr;
                if (!target || !script || !session || !unique.insert(ref).second)
                    return EDITOR_STALE;
                targets.emplace_back(script->GetID(), target->GetID());
            }
            bool changed = false;
            std::string message;
            for (const auto &result : ReplaceBehaviorGraphs(*doc, targets, source->GetID())) {
                changed |= result.ok;
                if (!result.ok && !result.Unchanged()) {
                    if (!message.empty())
                        message += "; ";
                    message += result.message;
                }
            }
            auto result = changed           ? EditResult::Applied()
                          : message.empty() ? EditResult::NoChange()
                                            : EditResult::Refused(message);
            if (changed)
                result.message = message;
            return Result(*doc, result, sink);
        }
        if (entry.action == Shortcutize || entry.action == Retarget) {
            const auto source = doc->NativeId(Object(*r, "source"));
            if (!source || !CKParameter::Cast(doc->Resolve(Object(*r, "source"))))
                return EDITOR_STALE;
            if (entry.action == Retarget) {
                const auto target = doc->NativeId(Object(*r, "destination"));
                return target ? Result(*doc, RetargetShortcutsInAllScripts(*doc, source, target), sink) : EDITOR_STALE;
            }
            std::vector<CK_ID> scripts;
            const auto *values = Arg(*r, "scripts");
            for (uint32_t i = 0; values && i < values->objects.count; ++i) {
                auto ref = values->objects.data[i];
                if (!doc->Resolve(ref) || !doc->FindScript(ref.object))
                    return EDITOR_STALE;
                scripts.push_back(ref.object);
            }
            auto result = scripts.empty() ? ShortcutizeLocalsInAllScripts(*doc, source)
                                          : ShortcutizeLocalsInScripts(*doc, scripts, source);
            if (result.ok)
                result.message = "Convert to Shortcut Source: " + result.message + " changed";
            else if (result.Unchanged())
                result.message = "Convert to Shortcut Source: no other local with the same name, type and value";
            return Result(*doc, result, sink);
        }
        CK_ID script = 0;
        const auto ready = self.Session(*doc, *r, script);
        if (ready != EDITOR_OK)
            return ready;
        if (entry.action == Display) {
            const auto *elements = Arg(*r, "elements");
            const auto mode = Number(*r, "mode"), style = Number(*r, "style");
            if (mode > 1 || (style != PARAM_STYLE_COLLAPSED && style != PARAM_STYLE_NAME &&
                             style != PARAM_STYLE_NAMEVALUE && style != PARAM_STYLE_VALUE))
                return EDITOR_INVALID;
            std::vector<ElementRef> items;
            std::set<ElementRef> unique;
            for (uint32_t i = 0; elements && i < elements->fields.count; ++i) {
                ElementAddress a;
                ElementRef ref;
                CK_ID in = 0;
                if (!ReadSchematicPosition(elements->fields.data[i], a))
                    return EDITOR_INVALID;
                if (!a.Resolve(*doc, in, ref) || in != script)
                    return EDITOR_STALE;
                if (!unique.insert(ref).second)
                    return EDITOR_INVALID;
                items.push_back(ref);
            }
            if (items.empty())
                return EDITOR_NO_CHANGE;
            bool changed = false;
            auto updated = doc->Session(script)->Data();
            {
                auto &data = updated;
                for (const auto &ref : items) {
                    auto *record = FindRecord(data, ref.owner);
                    if (!record)
                        continue;
                    if (ref.kind == ElementKind::LocalParam || ref.kind == ElementKind::Shortcut) {
                        auto &value = (ref.kind == ElementKind::LocalParam ? record->localParams
                                                                           : record->sharedParams)[size_t(ref.index)];
                        const auto next = mode ? NextStyle(value.style) : ParameterStyle(style);
                        if (value.style != next) {
                            value.style = next;
                            changed = true;
                        }
                    } else if (mode && ref.kind == ElementKind::Comment) {
                        auto &value = record->comments[size_t(ref.index)];
                        value.SetCollapsed(!value.IsCollapsed());
                        changed = true;
                    } else if (mode && ref.kind == ElementKind::Behavior) {
                        auto *b = CKBehavior::Cast(doc->Context()->GetObject(ref.object));
                        if (b && (b->GetType() & CKBEHAVIORTYPE_BEHAVIOR)) {
                            auto *value = FindRecord(data, ref.object);
                            if (value) {
                                SetBehaviorFolded(data, ref.object, !value->folded);
                                changed = true;
                            }
                        }
                    }
                }
            }
            Write(sink, {{"selection-unchanged", EDITOR_INTEGER_VALUE, {}, {}, 1}});
            if (!changed)
                return EDITOR_NO_CHANGE;
            return EditScriptData(*doc, script, "Change Display",
                                  [&](InterfaceData &data) { data = std::move(updated); })
                       ? EDITOR_OK
                       : EDITOR_NO_CHANGE;
        }
        const auto owner = doc->NativeId(Object(*r, "owner"));
        auto *session = doc->Session(script);
        if (!owner || !FindRecord(session->Data(), owner))
            return EDITOR_STALE;
        const auto name = Text(*r, "name");
        if (name.find('\0') != std::string::npos)
            return EDITOR_INVALID;
        Point at;
        if (entry.action != Pin && !PointOf(*r, at))
            return EDITOR_INVALID;
        EditResult result = EditResult::NoChange();
        ElementRef created;
        if (entry.action == Comment) {
            int slot = -1;
            const bool ok = EditScriptData(*doc, script, "Add Comment", [&](InterfaceData &data) {
                auto *record = FindRecord(data, owner);
                ::Comment comment;
                comment.hPos = SchematicGrid::SnapToGrid(at.h, SchematicGrid::SPACING);
                comment.vPos = SchematicGrid::SnapToGrid(at.v, SchematicGrid::SPACING);
                comment.width = 160;
                comment.height = 60;
                comment.text = "Comment";
                record->AddComment(comment);
                slot = int(record->comments.size()) - 1;
            });
            result = ok ? EditResult::Applied() : EditResult::NoChange();
            created = {ElementKind::Comment, owner, 0, slot};
        } else if (entry.action == Block) {
            std::vector<uint64_t> prototypes;
            if (const auto *list = Arg(*r, "prototypes"); list && list->integers.count)
                prototypes.assign(list->integers.data, list->integers.data + list->integers.count);
            else if (const auto *prototype = Arg(*r, "prototype"))
                prototypes.push_back(prototype->integer);
            if (prototypes.empty() || prototypes.size() > 256)
                return EDITOR_INVALID;
            for (auto prototype : prototypes)
                if (!CKGetObjectDeclarationFromGuid(Guid(prototype)))
                    return EDITOR_REFUSED;
            EditorField elements{"elements", EDITOR_RECORD}, inserted{"prototypes", EDITOR_INTEGER_LIST};
            for (size_t i = 0; i < prototypes.size(); ++i) {
                auto point = at;
                point.v += 60 * float(i);
                result = InsertBuildingBlock(*doc, script, owner, Guid(prototypes[i]), point);
                if (!result.ok)
                    return Result(*doc, result, sink);
                const auto address =
                    ElementAddress::Capture(*doc, script, {ElementKind::Behavior, owner, result.created});
                elements.fields.push_back(SchematicPosition(address, std::to_string(i).c_str()));
                inserted.integers.push_back(prototypes[i]);
            }
            return Write(
                sink,
                {std::move(elements), std::move(inserted), {"created", EDITOR_OBJECT, doc->Reference(result.created)}});
        } else if (entry.action == Local) {
            if (name.empty())
                return EDITOR_INVALID;
            result = AddLocalParameter(*doc, script, owner, Utf8ToAnsi(name), Guid(Number(*r, "type")), at);
        } else if (entry.action == This)
            result = AddThisParameter(*doc, script, owner, at);
        else if (entry.action == Pin) {
            const auto kind = Number(*r, "kind");
            if (kind < uint64_t(PinKind::BehaviorIn) || kind > uint64_t(PinKind::ParamOut))
                return EDITOR_INVALID;
            auto *b = CKBehavior::Cast(doc->Resolve(Object(*r, "owner")));
            if (!b)
                return EDITOR_STALE;
            auto title = Utf8ToAnsi(name);
            if (title.empty() && (kind == uint64_t(PinKind::BehaviorIn) || kind == uint64_t(PinKind::BehaviorOut)))
                title =
                    (kind == uint64_t(PinKind::BehaviorIn) ? "In " : "Out ") +
                    std::to_string(kind == uint64_t(PinKind::BehaviorIn) ? b->GetInputCount() : b->GetOutputCount());
            result = AcceptsVariablePin(b, PinKind(kind))
                         ? AddBuildingBlockPin(*doc, script, owner, PinKind(kind), title, Guid(Number(*r, "type")))
                         : AddGraphPin(*doc, script, owner, PinKind(kind), title, Guid(Number(*r, "type")));
        } else if (entry.action == Operation) {
            const auto *parts = Arg(*r, "parts");
            if (!parts || parts->integers.count != 4)
                return EDITOR_INVALID;
            CKGUID p[4];
            for (int i = 0; i < 4; ++i)
                p[i] = Guid(parts->integers.data[i]);
            if (AvailableOperationSignatures(doc->Context(), p + 3, p + 2, p, p + 1).empty())
                return EDITOR_REFUSED;
            const bool direct = !AvailableOperationSignatures(doc->Context(), p + 3, p + 2, p, p + 1, false).empty();
            OperationSignature signature{p[3], p[2], direct ? p[0] : p[1], direct ? p[1] : p[0], {}};
            if (auto existing = Object(*r, "operation")) {
                auto *op = CKParameterOperation::Cast(doc->Resolve(existing));
                if (!op || (!op->GetOwner() || op->GetOwner()->GetID() != owner) || !OwningScript(op) ||
                    OwningScript(op)->GetID() != script)
                    return EDITOR_STALE;
                int dropped = 0;
                result = ReplaceParameterOperation(*doc, script, op->GetID(), signature, &dropped);
                if (result.ok && dropped)
                    result.message =
                        "Modify Parameter Operation: " + std::to_string(dropped) + " link(s) did not fit the new types";
            } else
                result = AddParameterOperation(*doc, script, owner, signature.operation, signature.result,
                                               signature.input1, signature.input2, at);
            created = {ElementKind::Operation, owner, result.created};
        }
        if (result.ok && result.created && !created.IsValid()) {
            const auto *record = FindRecord(session->Data(), owner);
            for (size_t i = 0; i < record->localParams.size(); ++i)
                if (record->localParams[i].id == result.created) {
                    created = {ElementKind::LocalParam, owner, result.created, int(i)};
                    break;
                }
        }
        return Result(*doc, result, sink,
                      result.ok && created.IsValid() ? ElementAddress::Capture(*doc, script, created)
                                                     : ElementAddress{});
    }
};
SchematicAuthoring::SchematicAuthoring(std::function<Document *()> current)
    : m_Data(std::make_unique<Data>(std::move(current))) {}
SchematicAuthoring::~SchematicAuthoring() = default;
const std::vector<ProviderChoice> &SchematicAuthoring::DefaultProviders() {
    static const std::vector<ProviderChoice> choices = {
        {"query", "schematic.object-info", "builtin.schematic-authoring"},
        {"query", "schematic.element-info", "builtin.schematic-authoring"},
        {"query", "schematic.parameter-types", "builtin.schematic-authoring"},
        {"query", "schematic.operation-choices", "builtin.schematic-authoring"},
        {"query", "schematic.replacements", "builtin.schematic-authoring"},
        {"query", "schematic.shortcuts", "builtin.schematic-authoring"},
        {"command", "schematic.add-comment", "builtin.schematic-authoring"},
        {"command", "schematic.insert-block", "builtin.schematic-authoring"},
        {"command", "schematic.add-local", "builtin.schematic-authoring"},
        {"command", "schematic.add-this", "builtin.schematic-authoring"},
        {"command", "schematic.add-pin", "builtin.schematic-authoring"},
        {"command", "schematic.edit-operation", "builtin.schematic-authoring"},
        {"command", "schematic.set-display", "builtin.schematic-authoring"},
        {"command", "schematic.replace-graphs", "builtin.schematic-authoring"},
        {"command", "schematic.shortcutize-locals", "builtin.schematic-authoring"},
        {"command", "schematic.retarget-shortcuts", "builtin.schematic-authoring"}};
    return choices;
}
bool SchematicAuthoring::Register(EditorApplication &editor, std::string &error) {
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT};
    const EditorParameter info[] = {{EDITOR_TEXT("object"), EDITOR_OBJECT},
                                    {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE},
                                    {EDITOR_TEXT("class"), EDITOR_TEXT_VALUE},
                                    {EDITOR_TEXT("description"), EDITOR_TEXT_VALUE},
                                    {EDITOR_TEXT("owner"), EDITOR_OBJECT},
                                    {EDITOR_TEXT("owner-name"), EDITOR_TEXT_VALUE},
                                    {EDITOR_TEXT("script"), EDITOR_OBJECT},
                                    {EDITOR_TEXT("graph"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("settings"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("pin-kinds"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("targetable"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("using-target"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("prototype"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("type-name"), EDITOR_TEXT_VALUE},
                                    {EDITOR_TEXT("display"), EDITOR_TEXT_VALUE},
                                    {EDITOR_TEXT("self"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("output"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("enabled"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("direct-source"), EDITOR_TEXT_VALUE},
                                    {EDITOR_TEXT("shared-source"), EDITOR_TEXT_VALUE},
                                    {EDITOR_TEXT("destinations"), EDITOR_TEXT_LIST},
                                    {EDITOR_TEXT("signature"), EDITOR_INTEGER_LIST}};
    const EditorParameter address{EDITOR_TEXT("address"), EDITOR_RECORD};
    const EditorParameter types[] = {{EDITOR_TEXT("types"), EDITOR_RECORD}, {EDITOR_TEXT("none"), EDITOR_INTEGER_VALUE}};
    const EditorParameter operationArgs[] = {{EDITOR_TEXT("parts"), EDITOR_INTEGER_LIST},
                                             {EDITOR_TEXT("locks"), EDITOR_INTEGER_LIST},
                                             {EDITOR_TEXT("easy"), EDITOR_INTEGER_VALUE}};
    const EditorParameter operationFields[] = {{EDITOR_TEXT("choices"), EDITOR_RECORD},
                                               {EDITOR_TEXT("valid"), EDITOR_INTEGER_VALUE},
                                               {EDITOR_TEXT("direct"), EDITOR_INTEGER_VALUE},
                                               {EDITOR_TEXT("names"), EDITOR_TEXT_LIST}};
    const EditorParameter replacementArgs[] = {target, {EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter replacementFields[] = {
        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("description"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("scripts"), EDITOR_OBJECT_LIST},    {EDITOR_TEXT("graphs"), EDITOR_RECORD},
        {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE},     {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE}};
    const EditorParameter shortcutArgs[] = {target, {EDITOR_TEXT("mode"), EDITOR_INTEGER_VALUE}};
    const EditorParameter shortcutFields[] = {{EDITOR_TEXT("source"), EDITOR_TEXT_VALUE},
                                              {EDITOR_TEXT("owner"), EDITOR_TEXT_VALUE},
                                              {EDITOR_TEXT("rows"), EDITOR_RECORD}};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.object-info"), EDITOR_TEXT("builtin.schematic-authoring"),
         &target, 1, Data::Info, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, info, 25},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.element-info"), EDITOR_TEXT("builtin.schematic-authoring"),
         &address, 1, Data::Element, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, info, 25},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.parameter-types"), EDITOR_TEXT("builtin.schematic-authoring"),
         nullptr, 0, Data::Types, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, types, 2},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.operation-choices"),
         EDITOR_TEXT("builtin.schematic-authoring"), operationArgs, 3, Data::Operations, m_Data.get(),
         EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, operationFields, 4},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.replacements"), EDITOR_TEXT("builtin.schematic-authoring"),
         replacementArgs, 2, Data::Replacements, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT,
         replacementFields, 8},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.shortcuts"), EDITOR_TEXT("builtin.schematic-authoring"),
         shortcutArgs, 2, Data::Shortcuts, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, shortcutFields, 3}};
    const EditorParameter common[] = {target,
                                      {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("owner"), EDITOR_OBJECT},
                                      {EDITOR_TEXT("at"), EDITOR_REAL_LIST}};
    std::vector<EditorParameter> args[10];
    for (int i = 0; i < 6; ++i)
        args[i].assign(common, common + 5);
    args[1].push_back({EDITOR_TEXT("prototype"), EDITOR_INTEGER_VALUE, 1});
    args[1].push_back({EDITOR_TEXT("prototypes"), EDITOR_INTEGER_LIST, 1});
    args[2].push_back({EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE});
    args[2].push_back({EDITOR_TEXT("name"), EDITOR_TEXT_VALUE});
    args[4].erase(args[4].begin() + 4);
    args[4].push_back({EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE});
    args[4].push_back({EDITOR_TEXT("name"), EDITOR_TEXT_VALUE, 1});
    args[4].push_back({EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE, 1});
    args[5].push_back({EDITOR_TEXT("parts"), EDITOR_INTEGER_LIST});
    args[5].push_back({EDITOR_TEXT("operation"), EDITOR_OBJECT, 1, 1});
    args[6] = {common[0],
               common[1],
               common[2],
               {EDITOR_TEXT("elements"), EDITOR_RECORD},
               {EDITOR_TEXT("mode"), EDITOR_INTEGER_VALUE},
               {EDITOR_TEXT("style"), EDITOR_INTEGER_VALUE, 1}};
    args[7] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
               {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
               {EDITOR_TEXT("source"), EDITOR_OBJECT}};
    args[8] = {{EDITOR_TEXT("source"), EDITOR_OBJECT}, {EDITOR_TEXT("scripts"), EDITOR_OBJECT_LIST, 1}};
    args[9] = {{EDITOR_TEXT("source"), EDITOR_OBJECT}, {EDITOR_TEXT("destination"), EDITOR_OBJECT}};
    const char *ids[] = {"schematic.add-comment",       "schematic.insert-block",   "schematic.add-local",
                         "schematic.add-this",          "schematic.add-pin",        "schematic.edit-operation",
                         "schematic.set-display",       "schematic.replace-graphs", "schematic.shortcutize-locals",
                         "schematic.retarget-shortcuts"};
    const char *titles[] = {"Add Comment",
                            "Insert Building Block",
                            "Add Local Parameter",
                            "Add This Parameter",
                            "Add Pin",
                            "Edit Parameter Operation",
                            "Change Display",
                            "Replace Behavior Graph",
                            "Convert to Shortcut Source",
                            "Set Shortcut Source"};
    std::vector<EditorCommandDesc> commands;
    for (int i = 0; i < 10; ++i)
        commands.push_back({sizeof(EditorCommandDesc), EditorLiteral(ids[i]),
                            EDITOR_TEXT("builtin.schematic-authoring"), EditorLiteral(titles[i]), args[i].data(),
                            uint32_t(args[i].size()), Data::Run, &m_Data->entries[i], EDITOR_DOCUMENT,
                            EDITOR_DOCUMENT_EDIT});
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("schematic.authoring");
    module.queries = queries;
    module.queryCount = 6;
    module.commands = commands.data();
    module.commandCount = uint32_t(commands.size());
    return editor.Register(module, error);
}
} // namespace vdev
