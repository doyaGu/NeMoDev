#include "features/schematic/SchematicScriptCommands.h"
#include "features/schematic/SchematicScriptData.h"
#include "features/schematic/SchematicPosition.h"
#include "core/document/Document.h"
#include "core/document/ScriptSession.h"
#include "core/graph/Debugger.h"
#include "core/graph/RuntimeGraph.h"
#include "core/graph/ScriptCommands.h"
#include "core/schematic/SchematicEdits.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <cstring>
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
ObjectRef Object(const EditorRequest &r, const char *key) {
    const auto *v = Arg(r, key);
    return v ? v->object : ObjectRef{};
}
EditorField Int(const char *key, uint64_t v) { return {key, EDITOR_INTEGER_VALUE, {}, {}, v}; }
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto values = BorrowEditorFields(fields);
    return sink.write(sink.data, values.data(), uint32_t(values.size()));
}
std::string Name(CKObject *object) { return object && object->GetName() ? AnsiToUtf8(object->GetName()) : "?"; }
CKBehavior *OwnerBehavior(CKObject *object) {
    CKObject *owner = nullptr;
    if (auto *p = CKParameter::Cast(object))
        owner = p->GetOwner();
    else if (auto *p = CKParameterIn::Cast(object))
        owner = p->GetOwner();
    if (auto *op = CKParameterOperation::Cast(owner))
        owner = op->GetOwner();
    return CKBehavior::Cast(owner);
}
bool Ordered(Document &d, ObjectRef ref) {
    auto *p = d.Resolve(ref);
    if (!p)
        return false;
    auto *m = d.Context()->GetParameterManager();
    const auto type = ParameterType(p);
    return m->IsDerivedFrom(type, CKPGUID_INT) || m->IsDerivedFrom(type, CKPGUID_FLOAT);
}
bool IsParameter(CKObject *o) { return CKParameter::Cast(o) || CKParameterIn::Cast(o); }
bool Compatible(Document &d, ObjectRef a, ObjectRef b) {
    auto *left = d.Resolve(a), *right = d.Resolve(b);
    if (!IsParameter(left) || !IsParameter(right))
        return false;
    auto *m = d.Context()->GetParameterManager();
    return m->IsTypeCompatible(m->ParameterGuidToType(ParameterType(left)),
                               m->ParameterGuidToType(ParameterType(right)));
}
bool ByParameter(int comparison) { return comparison == 2 || comparison == 3 || comparison >= 8; }
EditorField Row(Document &d, CK_ID script, const ExtraData &data) {
    SchematicBreakpoint b;
    b.script = d.Reference(script);
    b.object = d.Reference(data.id1);
    b.owner = d.Reference(data.id2);
    b.type = uint32_t(data.type);
    b.revision = d.Session(script)->Revision();
    b.topology = d.TopologyRevision();
    const auto &records = d.Session(script)->Data().extraData;
    auto record = std::find_if(records.begin(), records.end(), [&](const ExtraData &v) { return &v == &data; });
    if (record == records.end())
        record = std::find_if(records.begin(), records.end(),
                              [&](const ExtraData &v) { return v.type == data.type && v.id1 == data.id1; });
    if (record != records.end())
        b.index = int(record - records.begin());

    auto *object = d.Resolve(b.object);
    CKBehavior *focus = nullptr;
    if (data.type == EXTRA_DATA_BEHAVIOR) {
        b.label = "Breakpoint on behavior <" + Name(object) + ">";
        focus = CKBehavior::Cast(object);
    } else if (data.type == EXTRA_DATA_PARAMETER) {
        b.label = "Breakpoint on parameter <" + Name(object) + ">";
        focus = OwnerBehavior(object);
    } else {
        auto *link = CKBehaviorLink::Cast(object);
        auto *in = link ? link->GetInBehaviorIO() : nullptr;
        auto *out = link ? link->GetOutBehaviorIO() : nullptr;
        focus = in ? in->GetOwner() : nullptr;
        b.label =
            "Breakpoint on link between <" + Name(focus) + "> and <" + Name(out ? out->GetOwner() : nullptr) + ">";
    }
    b.focus = d.Reference(focus ? focus->GetID() : 0);
    auto row = BreakpointField(b);
    row.fields.push_back({"label", EDITOR_TEXT_VALUE, {}, b.label});
    row.fields.push_back({"focus-target", EDITOR_OBJECT, b.focus});
    return row;
}
bool ReadTarget(const EditorValue &value, SchematicBreakpoint &target) {
    std::vector<EditorField> fields;
    return CopyEditorFields(fields, &value, 1) == EDITOR_OK && fields.size() == 1 &&
           ReadBreakpoint(fields.front(), target);
}
} // namespace
struct SchematicScriptCommands::Data {
    std::function<Document *()> current;
    Document *Doc(const EditorRequest &r) {
        auto *d = current();
        return d && d->IsOpen() && d->Token() == r.scope.document ? d : nullptr;
    }
    static ScriptSession *Session(Document &d, ObjectRef ref) {
        return d.Resolve(ref) && d.FindScript(ref.object) ? d.Session(ref.object) : nullptr;
    }
    static bool Valid(Document &d, const SchematicBreakpoint &b) {
        auto *s = Session(d, b.script);
        if (!s || s->Revision() != b.revision || d.TopologyRevision() != b.topology)
            return false;
        if (b.index >= 0) {
            const auto &records = s->Data().extraData;
            if (size_t(b.index) >= records.size())
                return false;
            const auto &entry = records[size_t(b.index)];
            return uint32_t(entry.type) == b.type && d.Reference(entry.id1) == b.object &&
                   d.Reference(entry.id2) == b.owner;
        }
        auto *object = d.Resolve(b.object);
        if (!object || (b.owner && !d.Resolve(b.owner)))
            return false;
        if (b.type == EXTRA_DATA_BEHAVIOR || b.type == EXTRA_DATA_PARAMETER) {
            if (OwningScript(object) != d.Resolve(b.script))
                return false;
            return b.type == EXTRA_DATA_BEHAVIOR ? CKBehavior::Cast(object) != nullptr : IsParameter(object);
        }
        auto *link = CKBehaviorLink::Cast(object);
        return b.type == EXTRA_DATA_CONNECTION && link && link->GetInBehaviorIO() &&
               OwningScript(link->GetInBehaviorIO()) == d.Resolve(b.script) && CKBehavior::Cast(d.Resolve(b.owner));
    }
    static const ExtraData *Record(Document &d, const SchematicBreakpoint &b) {
        if (!Valid(d, b) || b.index < 0)
            return nullptr;
        return &d.Session(b.script.object)->Data().extraData[size_t(b.index)];
    }
    static bool FromPosition(Document &d, const EditorValue &v, SchematicBreakpoint &b) {
        ElementAddress a;
        uint32_t script = 0;
        ElementRef ref;
        if (!ReadSchematicPosition(v, a) || !a.Resolve(d, script, ref))
            return false;
        auto *session = d.Session(script);
        CKObject *object = nullptr;
        ExtraDataType type = EXTRA_DATA_PARAMETER;
        CK_ID owner = 0;
        if (ref.kind == ElementKind::Behavior) {
            object = d.Resolve(a.object);
            type = EXTRA_DATA_BEHAVIOR;
        } else if (ref.kind == ElementKind::Link) {
            auto *record = FindRecord(session->Data(), ref.owner);
            if (!record || ref.index < 0 || size_t(ref.index) >= record->links.size() ||
                !record->links[size_t(ref.index)].IsBehaviorLink())
                return false;
            object = d.Resolve(a.object);
            type = EXTRA_DATA_CONNECTION;
            owner = ref.owner;
        } else if (ref.kind == ElementKind::Pin) {
            if (ref.pin != PinKind::ParamIn && ref.pin != PinKind::ParamOut)
                return false;
            object = d.Resolve(a.endpoint);
        } else if (ref.kind == ElementKind::LocalParam || ref.kind == ElementKind::Shortcut) {
            auto *record = FindRecord(session->Data(), ref.owner);
            if (!record)
                return false;
            auto *parameter = CKParameter::Cast(d.Resolve(a.object));
            object = parameter;
            if (ref.kind == ElementKind::Shortcut) {
                const auto &p = record->sharedParams[size_t(ref.index)];
                object = d.Context()->GetObject(p.id);
            }
        }
        if (!object)
            return false;
        b.script = d.Reference(script);
        b.object = d.Reference(object->GetID());
        b.owner = d.Reference(owner);
        b.type = uint32_t(type);
        b.revision = session->Revision();
        b.topology = d.TopologyRevision();
        return Valid(d, b);
    }
    static EditorStatus Header(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        auto *s = d ? Session(*d, Object(*r, "target")) : nullptr;
        if (!s)
            return EDITOR_STALE;
        auto *b = CKBehavior::Cast(d->Resolve(Object(*r, "target")));
        return Write(sink, {{"owner", EDITOR_OBJECT, d->Reference(b->GetOwner() ? b->GetOwner()->GetID() : 0)},
                            Int("revision", s->Revision()),
                            Int("topology", d->TopologyRevision()),
                            Int("readonly", s->ReadOnly()),
                            Int("load-failed", s->Origin() == SessionOrigin::LoadFailed),
                            {"message", EDITOR_TEXT_VALUE, {}, s->LoadMessage()},
                            {"name", EDITOR_TEXT_VALUE, {}, Name(b)}});
    }
    static EditorStatus Target(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        if (!d)
            return EDITOR_STALE;
        SchematicBreakpoint b;
        const auto *position = Arg(*r, "position");
        if (!position || !FromPosition(*d, *position, b))
            return EDITOR_STALE;
        ExtraData entry;
        entry.type = ExtraDataType(b.type);
        entry.id1 = b.object.object;
        entry.id2 = b.owner.object;
        auto row = Row(*d, b.script.object, entry);
        return Write(
            sink,
            {row, Int("present", FindBreakpoint(d->Session(b.script.object)->Data(), entry.type, entry.id1) != nullptr),
             Int("readonly", d->Session(b.script.object)->ReadOnly())});
    }
    static EditorStatus List(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        if (!d)
            return EDITOR_STALE;
        EditorField rows{"breakpoints", EDITOR_RECORD};
        for (const auto &entry : d->Sessions())
            for (const auto &b : entry.second->Data().extraData)
                if (b.type >= EXTRA_DATA_PARAMETER && b.type <= EXTRA_DATA_CONNECTION) {
                    auto row = Row(*d, entry.first, b);
                    row.key = std::to_string(rows.fields.size());
                    rows.fields.push_back(std::move(row));
                }
        return Write(sink, {rows});
    }
    static EditorStatus Parameter(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        if (!d)
            return EDITOR_STALE;
        const auto ref = Object(*r, "parameter"), compared = Object(*r, "compared");
        auto *p = d->Resolve(ref);
        if (!IsParameter(p))
            return EDITOR_STALE;
        auto *owner = OwnerBehavior(p);
        return Write(sink, {{"object", EDITOR_OBJECT, ref},
                            {"name", EDITOR_TEXT_VALUE, {}, Name(p)},
                            Int("ordered", Ordered(*d, ref)),
                            Int("compatible", !compared || Compatible(*d, compared, ref)),
                            {"focus-target", EDITOR_OBJECT, d->Reference(owner ? owner->GetID() : 0)},
                            {"target", EDITOR_OBJECT, d->Reference(OwningScript(p) ? OwningScript(p)->GetID() : 0)}});
    }
    static EditorStatus Detail(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        SchematicBreakpoint b;
        const auto *value = Arg(*r, "breakpoint");
        if (!d || !value || !ReadTarget(*value, b) || !Valid(*d, b))
            return EDITOR_STALE;
        const auto *entry = Record(*d, b);
        if (!entry)
            return EDITOR_STALE;
        EditorField candidates{"parameters", EDITOR_RECORD}, conditions{"conditions", EDITOR_RECORD};
        auto add = [&](CKObject *p, const std::string &prefix) {
            if (!IsParameter(p))
                return;
            const auto ref = d->Reference(p->GetID());
            for (const auto &row : candidates.fields)
                if (row.fields.front().object == ref)
                    return;
            EditorField row{std::to_string(candidates.fields.size()), EDITOR_RECORD};
            row.fields = {{"object", EDITOR_OBJECT, ref},
                          {"name", EDITOR_TEXT_VALUE, {}, prefix + Name(p)},
                          Int("ordered", Ordered(*d, ref))};
            candidates.fields.push_back(std::move(row));
        };
        auto *object = d->Resolve(b.object);
        auto *behavior = b.type == EXTRA_DATA_BEHAVIOR ? CKBehavior::Cast(object) : nullptr;
        if (b.type == EXTRA_DATA_PARAMETER)
            add(object, "");
        if (b.type == EXTRA_DATA_CONNECTION)
            if (auto *link = CKBehaviorLink::Cast(object))
                if (link->GetInBehaviorIO())
                    behavior = link->GetInBehaviorIO()->GetOwner();
        if (behavior) {
            for (int i = 0; i < behavior->GetInputParameterCount(); ++i)
                add(behavior->GetInputParameter(i), "pIn ");
            for (int i = 0; i < behavior->GetOutputParameterCount(); ++i)
                add(behavior->GetOutputParameter(i), "pOut ");
            for (int i = 0; i < behavior->GetLocalParameterCount(); ++i)
                add(behavior->GetLocalParameter(i), "Local ");
        }
        for (const auto &c : entry->subData) {
            add(d->Context()->GetObject(c.id1), "");
            if (c.UsesParameterReference())
                add(d->Context()->GetObject(c.id2), "");
            EditorField row{std::to_string(conditions.fields.size()), EDITOR_RECORD};
            row.fields = {{"parameter", EDITOR_OBJECT, d->Reference(c.id1)},
                          {"reference", EDITOR_OBJECT, c.UsesParameterReference() ? d->Reference(c.id2) : ObjectRef{}},
                          Int("missing", c.id1 && !d->Reference(c.id1)),
                          Int("comparison", std::clamp(int(c.GetComparisonType()), 0, 11)),
                          Int("logical", std::clamp(int(c.GetLogicalOperator()), 0, 2)),
                          {"value",
                           EDITOR_TEXT_VALUE,
                           {},
                           c.UsesParameterReference() ? Name(d->Context()->GetObject(c.id2))
                           : !d->Reference(c.id1)
                               ? std::string()
                               : AnsiToUtf8(ParameterTextFromBytes(
                                     d->Context(), ParameterType(d->Context()->GetObject(c.id1)), c.buffer))}};
            conditions.fields.push_back(std::move(row));
        }
        return Write(sink, {Row(*d, b.script.object, *entry), candidates, conditions});
    }
    static EditorStatus Toggle(void *data, const EditorHost *, const EditorRequest *r, EditorSink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        SchematicBreakpoint b;
        const auto *position = Arg(*r, "position");
        if (!d || !position || !FromPosition(*d, *position, b))
            return EDITOR_STALE;
        if (d->Session(b.script.object)->ReadOnly())
            return EDITOR_REFUSED;
        return ToggleBreakpoint(*d, b.script.object, ExtraDataType(b.type), b.object.object, b.owner.object)
                   ? EDITOR_OK
                   : EDITOR_NO_CHANGE;
    }
    static EditorStatus Configure(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        SchematicBreakpoint b;
        const auto *value = Arg(*r, "breakpoint"), *rows = Arg(*r, "conditions");
        if (!d || !value || !ReadTarget(*value, b) || !Valid(*d, b) || !Record(*d, b))
            return EDITOR_STALE;
        if (d->Session(b.script.object)->ReadOnly())
            return EDITOR_REFUSED;
        if (!rows || rows->fields.count > 4096)
            return EDITOR_INVALID;
        std::vector<ExtraSubData> conditions;
        for (uint32_t i = 0; i < rows->fields.count; ++i) {
            const auto &row = rows->fields.data[i];
            if (row.type != EDITOR_RECORD)
                return EDITOR_INVALID;
            EditorRequest condition{r->scope, row.fields.data, row.fields.count};
            std::set<std::string> keys;
            for (uint32_t j = 0; j < condition.count; ++j) {
                const auto &v = condition.values[j];
                const std::string key(v.key.data, v.key.size);
                if (!keys.insert(key).second)
                    return EDITOR_INVALID;
                const auto expected = key == "parameter" || key == "reference" ? EDITOR_OBJECT
                                      : key == "comparison" || key == "logical" || key == "missing"
                                          ? EDITOR_INTEGER_VALUE
                                      : key == "value" ? EDITOR_TEXT_VALUE
                                                       : EditorValueType(0);
                if (v.type != expected)
                    return EDITOR_INVALID;
            }
            if (!Arg(condition, "parameter") || !Arg(condition, "comparison") || !Arg(condition, "logical") ||
                !Arg(condition, "value"))
                return EDITOR_INVALID;

            const auto parameter = Object(condition, "parameter");
            if (Number(condition, "missing"))
                return EDITOR_STALE;
            if (!parameter)
                continue;
            const auto reference = Object(condition, "reference");
            const auto comparison = Number(condition, "comparison"), logical = Number(condition, "logical");
            if (comparison > 11 || logical > 2)
                return EDITOR_INVALID;
            if (!IsParameter(d->Resolve(parameter)))
                return EDITOR_STALE;
            if (comparison >= 4 && !Ordered(*d, parameter))
                return EDITOR_INVALID;
            ExtraSubData c;
            c.id1 = parameter.object;
            c.SetComparisonType(ExtraDataComparisonType(comparison));
            c.SetLogicalOperator(ExtraDataLogicalOperator(logical));
            if (ByParameter(int(comparison))) {
                if (!Compatible(*d, parameter, reference))
                    return EDITOR_STALE;
                c.id2 = reference.object;
            } else {
                const auto *text = Arg(condition, "value");
                if (!text || text->type != EDITOR_TEXT_VALUE)
                    return EDITOR_INVALID;
                c.buffer = ParameterBytesFromText(
                    d->Context(), ParameterType(d->Resolve(parameter)),
                    Utf8ToAnsi(std::string(text->text.data ? text->text.data : "", text->text.size)));
                if (c.buffer.empty()) {
                    Write(sink,
                          {{"message", EDITOR_TEXT_VALUE, {}, "A breakpoint value does not fit its parameter's type"}});
                    return EDITOR_REFUSED;
                }
            }
            conditions.push_back(std::move(c));
        }
        if (!Record(*d, b))
            return EDITOR_STALE;
        return SetBreakpointConditions(*d, b.script.object, ExtraDataType(b.type), Record(*d, b)->id1, conditions)
                   ? EDITOR_OK
                   : EDITOR_NO_CHANGE;
    }
    static EditorStatus Remove(void *data, const EditorHost *, const EditorRequest *r, EditorSink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        if (!d)
            return EDITOR_STALE;
        const auto *items = Arg(*r, "breakpoints");
        std::vector<Debugger::Breakpoint> remove;
        std::set<std::pair<CK_ID, int>> unique;
        for (uint32_t i = 0; items && i < items->fields.count; ++i) {
            SchematicBreakpoint b;
            if (!ReadTarget(items->fields.data[i], b) || !Valid(*d, b))
                return EDITOR_STALE;
            auto *session = d->Session(b.script.object);
            if (session->ReadOnly())
                return EDITOR_REFUSED;
            const auto *entry = Record(*d, b);
            if (!entry)
                return EDITOR_STALE;
            if (unique.insert({b.script.object, b.index}).second)
                remove.push_back({b.script.object, *entry});
        }
        if (remove.empty())
            return EDITOR_NO_CHANGE;
        RemoveBreakpoints(*d, remove);
        return EDITOR_OK;
    }
    static EditorStatus Rebuild(void *data, const EditorHost *, const EditorRequest *r, EditorSink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        auto *s = d ? Session(*d, Object(*r, "target")) : nullptr;
        if (!s || s->Revision() != Number(*r, "revision") || d->TopologyRevision() != Number(*r, "topology"))
            return EDITOR_STALE;
        if (s->Origin() != SessionOrigin::LoadFailed)
            return EDITOR_NO_CHANGE;
        return s->Reconstruct(d->Context()) ? EDITOR_OK : EDITOR_REFUSED;
    }
    static EditorStatus Picture(void *data, const EditorHost *, const EditorRequest *r, EditorSink) {
        auto *d = static_cast<Data *>(data)->Doc(*r);
        auto *s = d ? Session(*d, Object(*r, "target")) : nullptr;
        if (!s || s->Revision() != Number(*r, "revision") || d->TopologyRevision() != Number(*r, "topology"))
            return EDITOR_STALE;
        if (s->ReadOnly())
            return EDITOR_REFUSED;
        const auto w = Number(*r, "width"), h = Number(*r, "height");
        const auto *pixels = Arg(*r, "pixels");
        if (w > 48 || h > 36 || !pixels || w * h != pixels->integers.count || bool(w) != bool(h))
            return EDITOR_INVALID;
        void *bitmap = nullptr;
        if (w) {
            std::vector<unsigned char> bytes;
            for (uint32_t i = 0; i < pixels->integers.count; ++i) {
                auto v = pixels->integers.data[i];
                if (v > UINT32_MAX)
                    return EDITOR_INVALID;
                for (unsigned j = 0; j < 4; ++j)
                    bytes.push_back(uint8_t(v >> (8 * j)));
            }
            VxImageDescEx image;
            image.Width = int(w);
            image.Height = int(h);
            image.BitsPerPixel = 32;
            image.BytesPerLine = int(w * 4);
            image.RedMask = 0xff0000;
            image.GreenMask = 0xff00;
            image.BlueMask = 0xff;
            image.AlphaMask = 0xff000000;
            image.Image = bytes.data();
            bitmap = VxCreateBitmap(image);
            if (!bitmap)
                return EDITOR_ERROR;
            d->KeepBitmap(bitmap);
        }
        return SetScriptSnapshot(*d, Object(*r, "target").object, bitmap) ? EDITOR_OK : EDITOR_NO_CHANGE;
    }
};
SchematicScriptCommands::SchematicScriptCommands(std::function<Document *()> current)
    : m_Data(std::make_unique<Data>()) {
    m_Data->current = std::move(current);
}
SchematicScriptCommands::~SchematicScriptCommands() = default;
const std::vector<ProviderChoice> &SchematicScriptCommands::DefaultProviders() {
    static const std::vector<ProviderChoice> v = [] {
        std::vector<ProviderChoice> v;
        for (const auto *id : {"schematic.script-header", "schematic.breakpoint-target", "schematic.breakpoints",
                               "schematic.breakpoint-detail", "schematic.breakpoint-parameter"})
            v.push_back({"query", id, "builtin.schematic-scripts"});
        for (const auto *id :
             {"schematic.toggle-breakpoint", "schematic.configure-breakpoint", "schematic.remove-breakpoints",
              "schematic.rebuild-script", "schematic.set-script-snapshot"})
            v.push_back({"command", id, "builtin.schematic-scripts"});
        return v;
    }();
    return v;
}
bool SchematicScriptCommands::Register(EditorApplication &editor, std::string &error) {
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT},
        position{EDITOR_TEXT("position"), EDITOR_RECORD}, breakpoint{EDITOR_TEXT("breakpoint"), EDITOR_RECORD},
        breakpoints{EDITOR_TEXT("breakpoints"), EDITOR_RECORD};
    const EditorParameter headerFields[] = {{EDITOR_TEXT("owner"), EDITOR_OBJECT},
                                            {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("readonly"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("load-failed"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE},
                                            {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE}};
    const EditorParameter targetFields[] = {
        breakpoint, {EDITOR_TEXT("present"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("readonly"), EDITOR_INTEGER_VALUE}};
    const EditorParameter detailFields[] = {
        breakpoint, {EDITOR_TEXT("parameters"), EDITOR_RECORD}, {EDITOR_TEXT("conditions"), EDITOR_RECORD}};
    const EditorParameter parameterArgs[] = {{EDITOR_TEXT("parameter"), EDITOR_OBJECT},
                                             {EDITOR_TEXT("compared"), EDITOR_OBJECT, 1, 1}};
    const EditorParameter parameterFields[] = {
        {EDITOR_TEXT("object"), EDITOR_OBJECT},         {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("ordered"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("compatible"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("focus-target"), EDITOR_OBJECT},   {EDITOR_TEXT("target"), EDITOR_OBJECT}};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.script-header"), EDITOR_TEXT("builtin.schematic-scripts"),
         &target, 1, Data::Header, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, headerFields, 7},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.breakpoint-target"), EDITOR_TEXT("builtin.schematic-scripts"),
         &position, 1, Data::Target, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, targetFields, 3},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.breakpoints"), EDITOR_TEXT("builtin.schematic-scripts"),
         nullptr, 0, Data::List, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, &breakpoints, 1},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.breakpoint-detail"), EDITOR_TEXT("builtin.schematic-scripts"),
         &breakpoint, 1, Data::Detail, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, detailFields, 3},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("schematic.breakpoint-parameter"),
         EDITOR_TEXT("builtin.schematic-scripts"), parameterArgs, 2, Data::Parameter, m_Data.get(),
         EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, parameterFields, 6}};
    const EditorParameter configure[] = {breakpoint, {EDITOR_TEXT("conditions"), EDITOR_RECORD}};
    const EditorParameter rebuild[] = {
        target, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE}};
    const EditorParameter picture[] = {rebuild[0],
                                       rebuild[1],
                                       rebuild[2],
                                       {EDITOR_TEXT("width"), EDITOR_INTEGER_VALUE},
                                       {EDITOR_TEXT("height"), EDITOR_INTEGER_VALUE},
                                       {EDITOR_TEXT("pixels"), EDITOR_INTEGER_LIST}};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.toggle-breakpoint"),
         EDITOR_TEXT("builtin.schematic-scripts"), EDITOR_TEXT("Breakpoint"), &position, 1, Data::Toggle, m_Data.get(),
         EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.configure-breakpoint"),
         EDITOR_TEXT("builtin.schematic-scripts"), EDITOR_TEXT("Configure Breakpoint"), configure, 2, Data::Configure,
         m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.remove-breakpoints"),
         EDITOR_TEXT("builtin.schematic-scripts"), EDITOR_TEXT("Remove Breakpoints"), &breakpoints, 1, Data::Remove,
         m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.rebuild-script"), EDITOR_TEXT("builtin.schematic-scripts"),
         EDITOR_TEXT("Rebuild Layout from Runtime"), rebuild, 3, Data::Rebuild, m_Data.get(), EDITOR_DOCUMENT,
         EDITOR_DOCUMENT_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.set-script-snapshot"),
         EDITOR_TEXT("builtin.schematic-scripts"), EDITOR_TEXT("Set Script Snapshot"), picture, 6, Data::Picture,
         m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT}};
    EditorModuleDesc m{};
    m.size = sizeof(m);
    m.version = EDITOR_VERSION;
    m.id = EDITOR_TEXT("schematic.scripts");
    m.queries = queries;
    m.queryCount = uint32_t(std::size(queries));
    m.commands = commands;
    m.commandCount = uint32_t(std::size(commands));
    return editor.Register(m, error);
}
} // namespace vdev
