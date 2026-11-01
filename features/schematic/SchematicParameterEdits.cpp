#include "features/schematic/SchematicParameterEdits.h"
#include "features/schematic/SchematicPosition.h"
#include "features/values/ParameterCommands.h"
#include "core/document/Document.h"
#include "core/document/ScriptSession.h"
#include "core/graph/BehaviorCommands.h"
#include "core/graph/ParameterValues.h"
#include "core/graph/ParameterPicking.h"
#include "core/graph/RuntimeGraph.h"
#include "core/graph/TopologyCommands.h"
#include "core/graph/ValueCommands.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
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
ObjectRef Object(const EditorRequest &r, const char *key) {
    const auto *v = Arg(r, key);
    return v ? v->object : ObjectRef{};
}
std::string Text(const EditorRequest &r, const char *key) {
    const auto *v = Arg(r, key);
    return v ? std::string(v->text.data ? v->text.data : "", v->text.size) : std::string();
}
uint64_t Type(CKGUID g) { return uint64_t(g.d1) << 32 | g.d2; }
CKGUID Guid(uint64_t id) { return CKGUID(CKDWORD(id >> 32), CKDWORD(id)); }
EditorField Integer(const char *key, uint64_t value) { return {key, EDITOR_INTEGER_VALUE, {}, {}, value}; }
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto v = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, v.data(), uint32_t(v.size())) : EDITOR_INVALID;
}
EditorStatus Result(const EditResult &r, EditorSink sink) {
    Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(r.message)}});
    return r.ok                                ? EDITOR_OK
           : r.Unchanged()                     ? EDITOR_NO_CHANGE
           : r.Status() == EditStatus::Refused ? EDITOR_REFUSED
                                               : EDITOR_ERROR;
}
std::string Name(CKObject *o) { return AnsiToUtf8(o && o->GetName() ? o->GetName() : ""); }
} // namespace
struct SchematicParameterEdits::Data {
    std::function<Document *()> current;
    EditorApplication *editor = nullptr;
    struct Row {
        ObjectRef target{}, work{};
        CKGUID type;
        std::string label;
        int setting = -1;
        bool typed = false;
        ParameterValue remembered;
        PropertyBinding property;
    };
    struct Draft {
        Data *owner;
        CKContext *context = nullptr;
        EditorScope scope{};
        ObjectRef script{}, object{};
        uint64_t revision = 0, topology = 0, documentRevision = 0, mode = 0;
        std::string name, title;
        CKGUID type;
        bool nameEditable = false, typeEditable = false, apply = false, removeLinks = false;
        std::vector<Row> rows;
        PropertyBinding nameProperty;
        std::string copyProvider;
        explicit Draft(Data *owner) : owner(owner) {}
        ~Draft() {
            auto *doc = owner->current();
            if (doc && doc->Token() == scope.document && doc->Context() == context)
                for (auto &row : rows)
                    if (doc->Resolve(row.work, CKCID_PARAMETER, Document::ObjectScope::Existing))
                        context->DestroyObject(row.work.object);
            if (owner->active == this)
                owner->active = nullptr;
        }
    };
    Draft *active = nullptr; // borrowed from the command host's preview state
    explicit Data(std::function<Document *()> current) : current(std::move(current)) {}
    Document *DocumentOf(const EditorRequest &r) const {
        auto *doc = current();
        return doc && doc->IsOpen() && doc->Token() == r.scope.document ? doc : nullptr;
    }
    bool Current(const Draft &d) const {
        auto *doc = current();
        if (!doc || doc->Token() != d.scope.document || doc->Context() != d.context || !doc->Resolve(d.object) ||
            !doc->Resolve(d.script) || doc->ChangeRevision() != d.documentRevision ||
            doc->TopologyRevision() != d.topology)
            return false;
        auto *session = doc->Session(d.script.object);
        if (!session || session->ReadOnly() || session->Revision() != d.revision)
            return false;
        for (const auto &row : d.rows) {
            auto *target = CKParameter::Cast(doc->Resolve(row.target));
            if (!target || target->GetGUID() != row.type)
                return false;
        }
        return true;
    }
    EditorStatus Prepare(const EditorRequest &r, Draft &d, bool versions, bool copies) {
        auto *doc = DocumentOf(r);
        auto *script = doc ? CKBehavior::Cast(doc->Resolve(Object(r, "target"))) : nullptr;
        auto *object = doc ? doc->Resolve(Object(r, "object")) : nullptr;
        auto *session = script && doc->FindScript(script->GetID()) ? doc->Session(script->GetID()) : nullptr;
        if (!session || !object)
            return EDITOR_STALE;
        if (session->ReadOnly())
            return EDITOR_REFUSED;
        if (versions &&
            (session->Revision() != Number(r, "revision") || doc->TopologyRevision() != Number(r, "topology")))
            return EDITOR_STALE;
        const auto mode = Number(r, "mode");
        if (mode > 2)
            return EDITOR_INVALID;
        auto *owning = OwningScript(object);
        if (mode != 0 && (!owning || owning != script))
            return EDITOR_STALE;
        d.scope = r.scope;
        d.context = doc->Context();
        d.script = Object(r, "target");
        d.object = Object(r, "object");
        d.revision = session->Revision();
        d.topology = doc->TopologyRevision();
        d.documentRevision = doc->ChangeRevision();
        d.mode = mode;
        d.name = Name(object);
        d.copyProvider = editor->CommandProvider("parameter.copy-value");
        if (const auto *property = editor->Property("object.name"))
            d.nameProperty = *property;
        const auto add = [&](CKParameter *target, const std::string &label, int setting = -1) -> bool {
            if (!target)
                return false;
            Row row;
            row.target = doc->Reference(target->GetID());
            row.type = target->GetGUID();
            row.label = AnsiToUtf8(label);
            row.setting = setting;
            ParameterValueDescription description;
            row.typed = DescribeParameterValue(*doc, target, description);
            if (row.typed) {
                row.remembered = description.value;
                if (const auto *id = ParameterProperty(description.value.kind))
                    if (const auto *property = editor->Property(id))
                        row.property = *property;
            }
            d.rows.push_back(std::move(row));
            if (copies) {
                auto *work = d.context->CreateCKParameterOut((CKSTRING)label.c_str(), target->GetGUID(), TRUE);
                if (!work)
                    return false;
                d.rows.back().work = doc->Reference(work->GetID());
                if (work->CopyValue(target, FALSE) != CK_OK)
                    return false;
            }
            return true;
        };
        const auto input = [&](CKParameterIn *value) -> bool {
            if (!value || (value->GetObjectFlags() & CK_PARAMETERIN_DISABLED))
                return true;
            auto *holder = EditableValueOf(value);
            return !holder || holder->IsMyselfParameter() || add(holder, holder->GetName() ? holder->GetName() : "");
        };
        if (mode == 0) {
            auto *in = CKParameterIn::Cast(object);
            auto *value = CKParameter::Cast(object);
            if (!in && !value)
                return EDITOR_INVALID;
            auto *holder = in ? EditableValueOf(in) : CKParameterLocal::Cast(value);
            const bool self = holder && holder->IsMyselfParameter();
            d.type = in ? in->GetGUID() : value->GetGUID();
            d.nameEditable = !self;
            d.typeEditable = !self && !(in && CKParameterOperation::Cast(in->GetOwner())) &&
                             !(CKParameterOut::Cast(value) && CKParameterOperation::Cast(value->GetOwner()));
            d.title = "Edit Parameter";
            if (holder && !self && !add(holder, object->GetName() ? object->GetName() : ""))
                return EDITOR_ERROR;
        } else if (mode == 1) {
            d.title = "Edit Parameters: ";
            if (auto *b = CKBehavior::Cast(object)) {
                std::vector<CKBehavior *> path;
                for (auto *at = b; at; at = at->GetParent())
                    path.insert(path.begin(), at);
                for (auto *at : path) {
                    if (d.title != "Edit Parameters: ")
                        d.title += " / ";
                    d.title += Name(at);
                }
                d.apply = true;
                if (b->IsUsingTarget() && !input(b->GetTargetParameter()))
                    return EDITOR_ERROR;
                for (int i = 0; i < b->GetInputParameterCount(); ++i)
                    if (!input(b->GetInputParameter(i)))
                        return EDITOR_ERROR;
            } else if (auto *op = CKParameterOperation::Cast(object)) {
                d.title = d.name + " - Edit Parameters";
                if (!input(op->GetInParameter1()) || !input(op->GetInParameter2()))
                    return EDITOR_ERROR;
            } else
                return EDITOR_INVALID;
        } else {
            auto *b = CKBehavior::Cast(object);
            if (!b)
                return EDITOR_INVALID;
            d.title = d.name + " - Edit Settings";
            for (const auto &setting : BehaviorSettings(b))
                if (!add(b->GetLocalParameter(setting.slot), setting.name, setting.slot))
                    return EDITOR_ERROR;
        }
        return EDITOR_OK;
    }
    EditorField Value(const Draft &d) const {
        EditorField value{"value", EDITOR_RECORD}, rows{"rows", EDITOR_RECORD};
        auto *doc = current();
        for (size_t i = 0; i < d.rows.size(); ++i) {
            const auto &row = d.rows[i];
            auto *p = CKParameter::Cast(
                doc->Resolve(row.work ? row.work : row.target, CKCID_PARAMETER, Document::ObjectScope::Existing));
            ParameterValueDescription description;
            EditorField field{std::to_string(i), EDITOR_TEXT_VALUE, {}, p ? AnsiToUtf8(ParameterText(p)) : ""};
            if (row.typed && DescribeParameterValue(*doc, p, description))
                field = ParameterValueField(ParameterDraftValue(description.value, row.remembered), std::to_string(i));
            rows.fields.push_back(std::move(field));
        }
        value.fields = {{"name", EDITOR_TEXT_VALUE, {}, d.name},
                        Integer("type", Type(d.type)),
                        std::move(rows),
                        Integer("delete-links", d.removeLinks)};
        return value;
    }
    std::vector<EditorField> Arguments(const Draft &d) const {
        return {{"target", EDITOR_OBJECT, d.script}, {"object", EDITOR_OBJECT, d.object}, Integer("mode", d.mode),
                Integer("revision", d.revision),     Integer("topology", d.topology),     Value(d)};
    }
    EditorField RowFields(CKParameter *p, const std::string &label, const PropertyBinding &property,
                          const std::string &key, unsigned depth = 0) const {
        auto &doc = *current();
        ParameterValueDescription description;
        const bool described = p && DescribeParameterValue(doc, p, description);
        const auto layout = p ? ParameterUIOf(doc.Context()->GetParameterManager(), p->GetType()) : ParameterUIInfo{};
        const auto *type =
            p ? doc.Context()->GetParameterManager()->GetParameterTypeDescription(p->GetType()) : nullptr;
        const bool textEditable = !described && type && type->StringFunction;
        static const char *layouts[] = {"none",         "number",    "angle",      "time",   "key",       "bool",
                                        "string",       "vector",    "quaternion", "euler",  "vector2",   "matrix",
                                        "color",        "box",       "rect",       "object", "script",    "message",
                                        "attribute",    "animation", "synchro",    "enum",   "structure", "flags",
                                        "dependencies", "objects",   "curve",      "lod"};
        EditorField fields{"fields", EDITOR_RECORD}, members{"members", EDITOR_RECORD},
            classes{"classes", EDITOR_RECORD};
        if (described)
            fields.fields = ParameterFields(description);
        else if (p)
            fields.fields = {Integer("type", Type(p->GetGUID())),
                             {"display", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(ParameterText(p))}};
        if (described && description.value.kind == ParameterValueKind::Structure && depth < 32) {
            for (size_t i = 0; i < description.members.size(); ++i) {
                const auto &member = description.members[i];
                auto *value =
                    CKParameter::Cast(doc.Resolve(member.parameter, CKCID_PARAMETER, Document::ObjectScope::Existing));
                const auto *id = ParameterProperty(member.value.kind);
                const auto *binding = id ? editor->Property(id) : nullptr;
                members.fields.push_back(RowFields(value, member.name, binding ? *binding : PropertyBinding{},
                                                   std::to_string(i), depth + 1));
            }
        }
        if (layout.ui == ParameterUI::Object)
            for (const auto &entry : DerivedClasses(layout.pickClass)) {
                EditorField item{std::to_string(classes.fields.size()), EDITOR_RECORD};
                item.fields = {Integer("id", entry.first), {"name", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(entry.second)}};
                classes.fields.push_back(std::move(item));
            }
        EditorField item{key, EDITOR_RECORD};
        item.fields = {{"label", EDITOR_TEXT_VALUE, {}, label},
                       {"target", EDITOR_OBJECT, doc.Reference(p ? p->GetID() : 0)},
                       {"property", EDITOR_TEXT_VALUE, {}, property.id},
                       {"editor", EDITOR_TEXT_VALUE, {}, textEditable ? "text.single-line" : ""},
                       {"layout", EDITOR_TEXT_VALUE, {}, textEditable ? "text" : layouts[unsigned(layout.ui)]},
                       Integer("width", textEditable ? 220 : unsigned(std::max(0, layout.width))),
                       Integer("height", textEditable ? 14 : unsigned(std::max(0, layout.height))),
                       Integer("pick", unsigned(layout.pick)),
                       Integer("pick-class", unsigned(layout.pickClass)),
                       Integer("expand", layout.expand),
                       Integer("extended", layout.ui == ParameterUI::Object && (layout.pickClass == CKCID_OBJECT ||
                                                                                layout.pickClass == CKCID_BEOBJECT)),
                       std::move(fields),
                       std::move(members),
                       std::move(classes)};
        return item;
    }
    bool UsesWorkingParameter(const Draft &draft, const ParameterValue &value) const {
        const auto working = [&](ObjectRef object) {
            if (!object)
                return false;
            auto *parameter =
                CKParameter::Cast(current()->Resolve(object, CKCID_PARAMETER, Document::ObjectScope::Existing));
            for (auto *owner = parameter; owner; owner = CKParameter::Cast(owner->GetOwner()))
                for (const auto &row : draft.rows)
                    if (owner->GetID() == row.work.object)
                        return true;
            return false;
        };
        if (working(value.object) || std::any_of(value.objects.begin(), value.objects.end(), working))
            return true;
        return std::any_of(value.members.begin(), value.members.end(),
                           [&](const auto &member) { return UsesWorkingParameter(draft, member); });
    }
    std::vector<EditorField> Projection(const Draft &d) const {
        auto *doc = current();
        EditorField rows{"rows", EDITOR_RECORD}, arguments{"arguments", EDITOR_RECORD};
        arguments.fields = Arguments(d);
        for (size_t i = 0; i < d.rows.size(); ++i) {
            const auto &row = d.rows[i];
            auto *p = CKParameter::Cast(
                doc->Resolve(row.work ? row.work : row.target, CKCID_PARAMETER, Document::ObjectScope::Existing));
            rows.fields.push_back(RowFields(p, row.label, row.property, std::to_string(i)));
        }
        auto *pm = d.context->GetParameterManager();
        const auto native = pm->ParameterGuidToType(d.type);
        const auto *typeName = native >= 0 ? pm->ParameterTypeToName(native) : nullptr;
        auto *object = doc->Resolve(d.object);
        auto *p = CKParameter::Cast(object);
        if (auto *input = CKParameterIn::Cast(object))
            p = input->GetRealSource();
        EditorField invalid{"invalid-links", EDITOR_RECORD};
        auto *input = CKParameterIn::Cast(object);
        const auto originalType = input ? input->GetGUID() : p ? p->GetGUID() : CKGUID();
        if (d.mode == 0 && d.typeEditable && d.type != originalType)
            for (const auto &ref : IncompatibleParameterLinks(*doc, d.script.object, d.object.object, d.type))
                invalid.fields.push_back(SchematicPosition(ElementAddress::Capture(*doc, d.script.object, ref),
                                                           std::to_string(invalid.fields.size()).c_str()));
        return {std::move(arguments),
                std::move(rows),
                {"title", EDITOR_TEXT_VALUE, {}, d.title},
                {"type-name", EDITOR_TEXT_VALUE, {}, typeName ? AnsiToUtf8(typeName) : "Unknown Type"},
                {"display", EDITOR_TEXT_VALUE, {}, p ? AnsiToUtf8(ParameterText(p)) : "NA"},
                Integer("name-editable", d.nameEditable),
                Integer("type-editable", d.typeEditable),
                Integer("apply", d.apply),
                std::move(invalid)};
    }
    EditorStatus Update(Draft &d, const EditorValue &v) {
        auto *doc = current();
        if (!Current(d))
            return EDITOR_STALE;
        if (v.type != EDITOR_RECORD)
            return EDITOR_INVALID;
        EditorRequest r{{}, v.fields.data, v.fields.count};
        if ((r.count != 4 && r.count != 5) || !Arg(r, "name") || !Arg(r, "type") || !Arg(r, "rows") ||
            !Arg(r, "delete-links"))
            return EDITOR_INVALID;
        for (uint32_t i = 0; i < r.count; ++i) {
            const auto &f = r.values[i];
            const std::string key(f.key.data, f.key.size);
            if ((key == "name"                            ? f.type != EDITOR_TEXT_VALUE
                 : key == "type" || key == "delete-links" ? f.type != EDITOR_INTEGER_VALUE
                 : key == "rows" || key == "pick"         ? f.type != EDITOR_RECORD
                                                          : true))
                return EDITOR_INVALID;
        }
        const auto name = Text(r, "name");
        if (name.find('\0') != std::string::npos || Number(r, "delete-links") > 1)
            return EDITOR_INVALID;
        const auto type = Guid(Number(r, "type"));
        if ((name != d.name && (!d.nameEditable || d.mode != 0)) ||
            (type != d.type && (!d.typeEditable || d.mode != 0)))
            return EDITOR_REFUSED;
        const auto *rows = Arg(r, "rows");
        if (!rows)
            return EDITOR_INVALID;
        if (type != d.type) {
            if (d.context->GetParameterManager()->ParameterGuidToType(type) < 0 || rows->fields.count)
                return EDITOR_INVALID;
            for (auto &row : d.rows) {
                auto *work = CKParameter::Cast(doc->Resolve(row.work));
                if (!work)
                    return EDITOR_ERROR;
                work->SetGUID(type);
                if (work->GetGUID() != type)
                    return EDITOR_ERROR;
                ParameterValueDescription description;
                row.typed = DescribeParameterValue(*doc, work, description);
                row.remembered = description.value;
                row.property = {};
                if (const auto *id = ParameterProperty(description.value.kind))
                    if (const auto *property = editor->Property(id))
                        row.property = *property;
            }
            d.type = type;
        } else {
            if (rows->fields.count != d.rows.size())
                return EDITOR_INVALID;
            std::vector<ParameterValue> values;
            for (size_t i = 0; i < d.rows.size(); ++i) {
                const auto &row = d.rows[i];
                auto *p = CKParameter::Cast(doc->Resolve(row.work));
                const auto *value =
                    Arg(EditorRequest{{}, rows->fields.data, rows->fields.count}, std::to_string(i).c_str());
                ParameterValueDescription description;
                ParameterValue parsed;
                if (!p || !value)
                    return EDITOR_STALE;
                if (row.typed) {
                    if (!DescribeParameterValue(*doc, p, description) ||
                        !ParameterValueFromEditor(description, *value, parsed) ||
                        !ValidateParameterValue(*doc, description, parsed) || UsesWorkingParameter(d, parsed))
                        return EDITOR_INVALID;
                } else {
                    if (value->type != EDITOR_TEXT_VALUE)
                        return EDITOR_INVALID;
                    parsed.text = std::string(value->text.data ? value->text.data : "", value->text.size);
                    if (parsed.text.find('\0') != std::string::npos)
                        return EDITOR_INVALID;
                    const auto *description =
                        d.context->GetParameterManager()->GetParameterTypeDescription(p->GetType());
                    if ((!description || !description->StringFunction) && parsed.text != AnsiToUtf8(ParameterText(p)))
                        return EDITOR_REFUSED;
                }
                values.push_back(std::move(parsed));
            }
            for (size_t i = 0; i < d.rows.size(); ++i)
                if (d.rows[i].typed) {
                    auto *p = CKParameter::Cast(doc->Resolve(d.rows[i].work));
                    if (!WriteParameterDraft(*doc, p, values[i]))
                        return EDITOR_ERROR;
                    d.rows[i].remembered = std::move(values[i]);
                } else {
                    auto *p = CKParameter::Cast(doc->Resolve(d.rows[i].work));
                    if (values[i].text != AnsiToUtf8(ParameterText(p)) &&
                        p->SetStringValue(Utf8ToAnsi(values[i].text).c_str()) != CK_OK)
                        return EDITOR_ERROR;
                }
        }
        if (const auto *pick = Arg(r, "pick")) {
            EditorRequest request{{}, pick->fields.data, pick->fields.count};
            const auto row = Number(request, "row");
            const auto *path = Arg(request, "path");
            if (row >= d.rows.size() || !path || path->type != EDITOR_INTEGER_LIST || path->integers.count > 32)
                return EDITOR_INVALID;
            auto *work = CKParameter::Cast(doc->Resolve(d.rows[row].work));
            for (uint32_t i = 0; work && i < path->integers.count; ++i) {
                std::vector<ParameterMember> members;
                if (!ParameterMembers(*doc, work, members) || path->integers.data[i] >= members.size())
                    return EDITOR_STALE;
                work = CKParameter::Cast(doc->Resolve(members[size_t(path->integers.data[i])].parameter));
            }
            auto *picked = doc->Resolve(Object(request, "object"), CKCID_OBJECT, Document::ObjectScope::Existing);
            const auto options = Number(request, "options");
            if (options > 3 || Number(request, "class") > INT32_MAX ||
                !PickParameterDraft(*doc, work, picked, options & 1, CK_CLASSID(Number(request, "class")),
                                    !(options & 2), Utf8ToAnsi(Text(request, "filter"))))
                return EDITOR_OK;
            auto *root = CKParameter::Cast(doc->Resolve(d.rows[row].work));
            ParameterValueDescription description;
            if (DescribeParameterValue(*doc, root, description))
                d.rows[row].remembered = std::move(description.value);
        }
        d.name = name;
        d.removeLinks = Number(r, "delete-links") != 0;
        return EDITOR_OK;
    }
    static EditorStatus Read(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        if (self.active && self.active->scope.view == r->scope.view && self.active->script == Object(*r, "target") &&
            self.active->object == Object(*r, "object") && self.active->mode == Number(*r, "mode")) {
            if (!self.Current(*self.active))
                return EDITOR_STALE;
            return Write(sink, self.Projection(*self.active));
        }
        Draft draft(&self);
        const auto status = self.Prepare(*r, draft, false, false);
        return status == EDITOR_OK ? Write(sink, self.Projection(draft)) : status;
    }
    static EditorStatus Check(void *data, const EditorHost *, const EditorRequest *r) {
        auto &self = *static_cast<Data *>(data);
        if (self.active)
            return self.Current(*self.active) && self.active->object == Object(*r, "object") &&
                           self.active->script == Object(*r, "target") && self.active->scope.view == r->scope.view &&
                           self.active->mode == Number(*r, "mode") && self.active->revision == Number(*r, "revision") &&
                           self.active->topology == Number(*r, "topology")
                       ? EDITOR_OK
                       : EDITOR_STALE;
        Draft draft(&self);
        return self.Prepare(*r, draft, true, false);
    }
    static EditorStatus Preview(void *data, const EditorHost *, const EditorRequest *r, EditorPreviewAction action,
                                void **state) {
        auto &self = *static_cast<Data *>(data);
        if (action == EDITOR_PREVIEW_CANCEL || action == EDITOR_PREVIEW_RELEASE) {
            std::unique_ptr<Draft> owned(static_cast<Draft *>(*state));
            *state = nullptr;
            return EDITOR_OK;
        }
        if (action != EDITOR_PREVIEW_UPDATE)
            return EDITOR_INVALID;
        if (!*state) {
            if (self.active)
                return EDITOR_REFUSED;
            auto draft = std::make_unique<Draft>(&self);
            const auto status = self.Prepare(*r, *draft, true, true);
            if (status != EDITOR_OK)
                return status;
            self.active = draft.get();
            *state = draft.release();
        }
        const auto *v = Arg(*r, "value");
        return v ? self.Update(*static_cast<Draft *>(*state), *v) : EDITOR_INVALID;
    }
    EditorStatus Commit(Draft &d, EditorSink sink) {
        if (!Current(d))
            return EDITOR_STALE;
        auto *doc = current();
        auto *object = doc->Resolve(d.object);
        if (d.mode == 2) {
            std::map<int, std::string> values;
            for (const auto &row : d.rows) {
                auto *work = CKParameter::Cast(doc->Resolve(row.work));
                if (!work)
                    return EDITOR_STALE;
                values[row.setting] = ParameterText(work);
            }
            return Result(EditBehaviorSettings(*doc, d.script.object, d.object.object, values), sink);
        }
        if (d.mode == 0) {
            auto links = IncompatibleParameterLinks(*doc, d.script.object, d.object.object, d.type);
            if (!links.empty() && !d.removeLinks) {
                Write(sink, {{"message",
                              EDITOR_TEXT_VALUE,
                              {},
                              "The new type requires confirmation to delete incompatible links"}});
                return EDITOR_REFUSED;
            }
            if (!links.empty()) {
                const auto removed = DeleteElements(*doc, d.script.object, links);
                if (!removed.ok && !removed.Unchanged())
                    return Result(removed, sink);
            }
            if (d.nameEditable && d.name != Name(object)) {
                const EditorValue name{EDITOR_TEXT("name"), EDITOR_TEXT_VALUE, {}, EditorText(d.name)};
                const auto renamed = editor->ApplyEdit(d.nameProperty, d.scope, d.object, name, {});
                if (renamed.status != EDITOR_OK && renamed.status != EDITOR_NO_CHANGE)
                    return renamed.status;
            }
            auto *input = CKParameterIn::Cast(object);
            auto *parameter = CKParameter::Cast(object);
            const auto original = input ? input->GetGUID() : parameter->GetGUID();
            if (d.typeEditable && d.type != original) {
                const auto changed = SetParameterType(*doc, d.object.object, d.type);
                if (!changed.ok && !changed.Unchanged())
                    return Result(changed, sink);
            }
            if (input && !d.rows.empty()) {
                auto *holder = EditableValueOf(input);
                if (!holder)
                    return EDITOR_OK;
                d.rows.front().target = doc->Reference(holder->GetID());
            }
        }
        for (auto &row : d.rows) {
            auto *target = CKParameter::Cast(doc->Resolve(row.target));
            auto *work = CKParameter::Cast(doc->Resolve(row.work));
            if (!target || !work || target->GetGUID() != work->GetGUID())
                return EDITOR_STALE;
            ParameterValueDescription description;
            if (!DescribeParameterValue(*doc, work, description)) {
                if (row.typed)
                    return EDITOR_STALE;
                if (d.copyProvider.empty())
                    return EDITOR_REFUSED;
                const EditorValue arguments[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, row.target},
                                                 {EDITOR_TEXT("source"), EDITOR_OBJECT, row.work}};
                const auto applied =
                    editor->ApplyEdit("parameter.copy-value", {d.scope, arguments, 2, EditorText(d.copyProvider)});
                if (applied.status != EDITOR_OK && applied.status != EDITOR_NO_CHANGE)
                    return applied.status;
                continue;
            }
            auto value = ParameterValueField(ParameterDraftValue(description.value, row.remembered));
            const std::vector<EditorField> ownedValue{value};
            const auto borrowed = BorrowEditorFields(ownedValue);
            ParameterValueDescription actual;
            if (!DescribeParameterValue(*doc, target, actual))
                return EDITOR_STALE;
            auto fields = ParameterFields(actual);
            std::vector<EditorField> arguments{Integer("type", Type(work->GetGUID()))};
            for (const auto &field : fields)
                if (field.key == "schema")
                    arguments.push_back(field);
            const auto applied = editor->ApplyEdit(row.property, d.scope, row.target, *borrowed.data(), arguments);
            if (applied.status != EDITOR_OK && applied.status != EDITOR_NO_CHANGE)
                return applied.status;
        }
        return EDITOR_OK;
    }
    static EditorStatus CommitPreview(void *data, const EditorHost *, const EditorRequest *, void *state,
                                      EditorSink sink) {
        return state ? static_cast<Data *>(data)->Commit(*static_cast<Draft *>(state), sink) : EDITOR_INVALID;
    }
    static EditorStatus Run(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        Draft d(&self);
        auto status = self.Prepare(*r, d, true, true);
        if (status != EDITOR_OK)
            return status;
        const auto *value = Arg(*r, "value");
        status = value ? self.Update(d, *value) : EDITOR_INVALID;
        return status == EDITOR_OK ? self.Commit(d, sink) : status;
    }
};
SchematicParameterEdits::SchematicParameterEdits(std::function<Document *()> current)
    : m_Data(std::make_unique<Data>(std::move(current))) {}
SchematicParameterEdits::~SchematicParameterEdits() = default;
const std::vector<ProviderChoice> &SchematicParameterEdits::DefaultProviders() {
    static const std::vector<ProviderChoice> choices = {
        {"query", "schematic.parameter-editor", "builtin.schematic-parameters"},
        {"command", "schematic.edit-parameters", "builtin.schematic-parameters"}};
    return choices;
}
bool SchematicParameterEdits::Register(EditorApplication &editor, std::string &error) {
    m_Data->editor = &editor;
    const EditorParameter queryArgs[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT},
                                         {EDITOR_TEXT("object"), EDITOR_OBJECT},
                                         {EDITOR_TEXT("mode"), EDITOR_INTEGER_VALUE}};
    const EditorParameter fields[] = {{EDITOR_TEXT("arguments"), EDITOR_RECORD},
                                      {EDITOR_TEXT("rows"), EDITOR_RECORD},
                                      {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE},
                                      {EDITOR_TEXT("type-name"), EDITOR_TEXT_VALUE},
                                      {EDITOR_TEXT("display"), EDITOR_TEXT_VALUE},
                                      {EDITOR_TEXT("name-editable"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("type-editable"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("apply"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("invalid-links"), EDITOR_RECORD}};
    const EditorQueryDesc query{sizeof(query),
                                EDITOR_TEXT("schematic.parameter-editor"),
                                EDITOR_TEXT("builtin.schematic-parameters"),
                                queryArgs,
                                3,
                                Data::Read,
                                m_Data.get(),
                                EDITOR_QUERY_EACH_READ,
                                EDITOR_DOCUMENT,
                                fields,
                                9};
    const EditorParameter args[] = {queryArgs[0],
                                    queryArgs[1],
                                    queryArgs[2],
                                    {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE},
                                    {EDITOR_TEXT("value"), EDITOR_RECORD}};
    const EditorCommandDesc command{sizeof(command),
                                    EDITOR_TEXT("schematic.edit-parameters"),
                                    EDITOR_TEXT("builtin.schematic-parameters"),
                                    EDITOR_TEXT("Edit Parameters"),
                                    args,
                                    6,
                                    Data::Run,
                                    m_Data.get(),
                                    EDITOR_DOCUMENT,
                                    EDITOR_DOCUMENT_EDIT,
                                    Data::Check,
                                    Data::Preview,
                                    Data::CommitPreview};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("schematic.parameters");
    module.queries = &query;
    module.queryCount = 1;
    module.commands = &command;
    module.commandCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev
