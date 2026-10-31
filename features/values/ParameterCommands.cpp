#include "features/values/ParameterCommands.h"
#include "core/graph/ParameterValues.h"
#include "core/graph/ValueCommands.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Clipboard.h"
#include "core/runtime/TextEncoding.h"
#include <cmath>
#include <algorithm>
#include <iterator>

namespace vdev {
namespace {
struct Kind {
    ParameterValueKind kind;
    const char *property, *editor;
    EditorValueType type;
};
const Kind kinds[] = {
    {ParameterValueKind::Boolean, "parameter.boolean", "value.boolean", EDITOR_INTEGER_VALUE},
    {ParameterValueKind::Integer, "parameter.integer", "value.integer", EDITOR_INTEGER_VALUE},
    {ParameterValueKind::Real, "parameter.real", "value.real", EDITOR_REAL_VALUE},
    {ParameterValueKind::Enumeration, "parameter.enumeration", "value.enumeration", EDITOR_INTEGER_VALUE},
    {ParameterValueKind::Object, "parameter.object", "value.object", EDITOR_OBJECT},
    {ParameterValueKind::Angle, "parameter.angle", "value.angle", EDITOR_REAL_VALUE},
    {ParameterValueKind::Vector2, "parameter.vector2", "value.vector2", EDITOR_REAL_LIST},
    {ParameterValueKind::Vector3, "parameter.vector3", "value.vector3", EDITOR_REAL_LIST},
    {ParameterValueKind::Quaternion, "parameter.quaternion", "value.quaternion", EDITOR_REAL_LIST},
    {ParameterValueKind::EulerAngles, "parameter.euler", "value.euler", EDITOR_REAL_LIST},
    {ParameterValueKind::Matrix, "parameter.matrix", "value.matrix", EDITOR_REAL_LIST},
    {ParameterValueKind::Color, "parameter.color", "value.color", EDITOR_REAL_LIST},
    {ParameterValueKind::Box, "parameter.box", "value.box", EDITOR_REAL_LIST},
    {ParameterValueKind::Rect, "parameter.rect", "value.rect", EDITOR_REAL_LIST},
    {ParameterValueKind::Text, "parameter.string", "value.string", EDITOR_TEXT_VALUE},
    {ParameterValueKind::Structure, "parameter.structure", "value.structure", EDITOR_RECORD},
    {ParameterValueKind::ObjectArray, "parameter.object-array", "value.object-array", EDITOR_OBJECT_LIST},
    {ParameterValueKind::Flags, "parameter.flags", "value.flags", EDITOR_INTEGER_VALUE},
    {ParameterValueKind::Curve2D, "parameter.curve2d", "value.curve2d", EDITOR_RECORD},
    {ParameterValueKind::Dependencies, "parameter.dependencies", "value.dependencies", EDITOR_RECORD},
    {ParameterValueKind::LodOptions, "parameter.lod-options", "value.lod-options", EDITOR_RECORD},
    {ParameterValueKind::Time, "parameter.time", "value.time", EDITOR_REAL_VALUE},
    {ParameterValueKind::Key, "parameter.key", "value.key", EDITOR_INTEGER_VALUE},
    {ParameterValueKind::Script, "parameter.script", "value.script", EDITOR_OBJECT},
    {ParameterValueKind::Animation, "parameter.animation", "value.animation", EDITOR_OBJECT},
    {ParameterValueKind::Message, "parameter.message", "value.message", EDITOR_RECORD},
    {ParameterValueKind::Attribute, "parameter.attribute", "value.attribute", EDITOR_INTEGER_VALUE},
    {ParameterValueKind::Synchro, "parameter.synchro", "value.synchro", EDITOR_RECORD}};
const EditorValue *Find(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key) return &request.values[i];
    return nullptr;
}
uint64_t TypeId(CKGUID type) { return (uint64_t(type.d1) << 32) | type.d2; }
const char *ChoicesFor(ParameterValueKind kind) {
    if (kind == ParameterValueKind::Object || kind == ParameterValueKind::ObjectArray) return "parameter.object-choices";
    if (kind == ParameterValueKind::Script || kind == ParameterValueKind::Animation || kind == ParameterValueKind::Message ||
        kind == ParameterValueKind::Attribute || kind == ParameterValueKind::Synchro) return "parameter.selection-choices";
    return "";
}
CKBehavior *OwnerScript(CKParameter *parameter, CKObject *&owner) {
    owner = parameter->GetOwner();
    if (auto *operation = CKParameterOperation::Cast(owner)) owner = operation->GetOwner();
    auto *behavior = CKBehavior::Cast(owner);
    if (!behavior) return nullptr;
    owner = behavior->GetOwner();
    return (behavior->GetType() & CKBEHAVIORTYPE_SCRIPT) ? behavior : behavior->GetOwnerScript();
}
EditorStatus ParameterPage(Document &document, const std::vector<CK_ID> &ids, const EditorRequest &request, EditorSink sink) {
    const auto *offset = Find(request, "first");
    const auto first = offset ? offset->integer : 0;
    if (first > ids.size()) return EDITOR_INVALID;
    const auto end = std::min(ids.size(), size_t(first) + 128);
    std::vector<ObjectRef> objects;
    for (size_t i = size_t(first); i < end; ++i)
        if (const auto object = document.Reference(ids[i]); document.Resolve(object)) objects.push_back(object);
    const EditorValue fields[] = {
        {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {objects.data(), uint32_t(objects.size())}},
        {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE, {}, {}, ids.size()},
        {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE, {}, {}, end}};
    return sink.write ? sink.write(sink.data, fields, 3) : EDITOR_INVALID;
}
} // namespace
const char *ParameterProperty(ParameterValueKind kind) {
    for (const auto &entry : kinds) if (entry.kind == kind) return entry.property;
    return nullptr;
}
EditorField ParameterValueField(const ParameterValue &value, const std::string &key) {
    EditorField result; result.key = key;
    for (const auto &entry : kinds) if (entry.kind == value.kind) result.type = entry.type;
    if (value.kind == ParameterValueKind::Message) {
        result.fields = {{"id", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(value.integer))}, {"name", EDITOR_TEXT_VALUE, {}, value.text}};
        return result;
    }
    if (value.kind == ParameterValueKind::Synchro) {
        result.fields = {{"object", EDITOR_OBJECT, value.object}, {"name", EDITOR_TEXT_VALUE, {}, value.text}};
        if (value.waiters) result.fields.push_back({"users", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(*value.waiters))});
        if (value.setWaiters) result.fields.push_back({"set-users", EDITOR_INTEGER_VALUE, {}, {}, 1});
        return result;
    }
    if (value.kind == ParameterValueKind::Curve2D) {
        result.fields = {{"fitting", EDITOR_REAL_VALUE}, {"points", EDITOR_REAL_LIST}, {"flags", EDITOR_INTEGER_LIST}};
        result.fields[0].real = value.curve.fitting;
        for (const auto &point : value.curve.points) {
            result.fields[1].reals.insert(result.fields[1].reals.end(), point.values.begin(), point.values.end());
            result.fields[2].integers.push_back(point.flags);
        }
        return result;
    }
    if (value.kind == ParameterValueKind::Dependencies) {
        result.fields = {{"mode", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(value.integer))}, {"options", EDITOR_INTEGER_LIST}};
        result.fields[1].integers.assign(value.options.begin(), value.options.end()); return result;
    }
    if (value.kind == ParameterValueKind::LodOptions) {
        result.fields = {{"flags", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(value.integer))}, {"proportions", EDITOR_REAL_LIST}};
        result.fields[1].reals = value.components; return result;
    }
    result.integer = uint64_t(int64_t(value.integer)); result.real = value.real; result.object = value.object;
    result.reals = value.components; result.text = value.text; result.objects = value.objects;
    for (size_t i = 0; i < value.members.size(); ++i) result.fields.push_back(ParameterValueField(value.members[i], std::to_string(i)));
    return result;
}
std::vector<EditorField> ParameterFields(const ParameterValueDescription &description) {
    std::vector<EditorField> fields{ParameterValueField(description.value),
        {"display", EDITOR_TEXT_VALUE, {}, description.display}, {"type", EDITOR_INTEGER_VALUE, {}, {}, TypeId(description.type)},
        {"labels", EDITOR_TEXT_LIST}, {"values", EDITOR_INTEGER_LIST}, {"objects", EDITOR_OBJECT_LIST},
        {"scale", EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, description.displayScale},
        {"rows", EDITOR_INTEGER_VALUE, {}, {}, description.rows}, {"columns", EDITOR_INTEGER_VALUE, {}, {}, description.columns},
        {"row-labels", EDITOR_TEXT_LIST}};
    for (const auto &choice : description.choices) { fields[3].texts.push_back(choice.second); fields[4].integers.push_back(uint64_t(int64_t(choice.first))); }
    if (!description.componentLabels.empty()) fields[3].texts = description.componentLabels;
    fields[9].texts = description.rowLabels;
    if (description.value.kind == ParameterValueKind::Structure) {
        EditorField schema{"schema", EDITOR_RECORD};
        for (size_t i = 0; i < description.members.size(); ++i) {
            const auto &child = description.members[i];
            EditorField member{std::to_string(i), EDITOR_RECORD}; member.fields = ParameterFields(child);
            member.fields.erase(member.fields.begin());
            const char *editor = "";
            for (const auto &kind : kinds) if (kind.kind == child.value.kind) editor = kind.editor;
            member.fields.push_back({"name", EDITOR_TEXT_VALUE, {}, child.name});
            member.fields.push_back({"editor", EDITOR_TEXT_VALUE, {}, editor});
            member.fields.push_back({"target", EDITOR_OBJECT, child.parameter});
            member.fields.push_back({"choices", EDITOR_TEXT_VALUE, {}, ChoicesFor(child.value.kind)});
            schema.fields.push_back(std::move(member));
        }
        fields.push_back(std::move(schema));
    }
    if (description.value.kind == ParameterValueKind::Dependencies) {
        EditorField schema{"schema", EDITOR_RECORD};
        for (const auto &entry : description.dependencyClasses) {
            EditorField item{std::to_string(entry.id), EDITOR_RECORD};
            item.fields = {{"id", EDITOR_INTEGER_VALUE, {}, {}, entry.id}, {"depth", EDITOR_INTEGER_VALUE, {}, {}, entry.depth},
                {"name", EDITOR_TEXT_VALUE, {}, entry.name}, {"labels", EDITOR_TEXT_LIST}};
            item.fields[3].texts = entry.options; schema.fields.push_back(std::move(item));
        }
        fields.push_back(std::move(schema));
    }
    return fields;
}
namespace {
const EditorValue *Member(const EditorValue &record, const std::string &key) {
    if (record.type != EDITOR_RECORD || (record.fields.count && !record.fields.data) || record.fields.count > 4096) return nullptr;
    for (uint32_t i = 0; i < record.fields.count; ++i) {
        const auto &value = record.fields.data[i];
        if (value.key.data && std::string(value.key.data, value.key.size) == key) return &value;
    }
    return nullptr;
}
bool SchemaMatches(const ParameterValueDescription &description, const EditorValue *schema) {
    if (!schema || schema->type != EDITOR_RECORD || schema->fields.count != description.members.size()) return false;
    for (size_t i = 0; i < description.members.size(); ++i) {
        const auto &child = description.members[i];
        const auto *member = Member(*schema, std::to_string(i));
        const auto *type = member ? Member(*member, "type") : nullptr;
        const auto *target = member ? Member(*member, "target") : nullptr;
        if (!type || type->type != EDITOR_INTEGER_VALUE || type->integer != TypeId(child.type) || !target || target->type != EDITOR_OBJECT || target->object != child.parameter) return false;
        if (child.value.kind == ParameterValueKind::Structure && !SchemaMatches(child, Member(*member, "schema"))) return false;
    }
    return true;
}
}
bool ParameterValueFromEditor(const ParameterValueDescription &description, const EditorValue &value, ParameterValue &out) {
    if (description.value.kind != ParameterValueKind::Structure) return ParameterValueFromEditor(description.value.kind, value, out);
    if (value.type != EDITOR_RECORD || value.fields.count != description.members.size()) return false;
    ParameterValue result; result.kind = ParameterValueKind::Structure;
    for (size_t i = 0; i < description.members.size(); ++i) {
        const auto *member = Member(value, std::to_string(i));
        ParameterValue parsed;
        if (!member || !ParameterValueFromEditor(description.members[i], *member, parsed)) return false;
        result.members.push_back(std::move(parsed));
    }
    out = std::move(result); return true;
}
bool ParameterValueFromEditor(ParameterValueKind kind, const EditorValue &value, ParameterValue &out) {
    if (kind == ParameterValueKind::None) return false;
    if (kind == ParameterValueKind::Message || kind == ParameterValueKind::Synchro) {
        if (value.type != EDITOR_RECORD) return false;
        ParameterValue parsed; parsed.kind = kind;
        const auto *name = Member(value, "name");
        if (!name || name->type != EDITOR_TEXT_VALUE || name->text.size > 1024 * 1024 || (name->text.size && !name->text.data)) return false;
        parsed.text.assign(name->text.data ? name->text.data : "", name->text.size);
        const auto integer = [](const EditorValue *field, int32_t &out) {
            if (!field || field->type != EDITOR_INTEGER_VALUE || int64_t(field->integer) < INT32_MIN || int64_t(field->integer) > INT32_MAX) return false;
            out = int32_t(int64_t(field->integer)); return true;
        };
        if (kind == ParameterValueKind::Message) {
            if (value.fields.count != 2 || !integer(Member(value, "id"), parsed.integer)) return false;
        } else {
            const auto *object = Member(value, "object"), *users = Member(value, "users"), *setUsers = Member(value, "set-users"); int32_t count = 0;
            if (!object || object->type != EDITOR_OBJECT || value.fields.count != 2u + (users ? 1u : 0u) + (setUsers ? 1u : 0u) || (users && !integer(users, count)) ||
                (setUsers && (setUsers->type != EDITOR_INTEGER_VALUE || setUsers->integer != 1 || !users))) return false;
            parsed.object = object->object; if (users) parsed.waiters = count; parsed.setWaiters = setUsers != nullptr;
        }
        out = std::move(parsed); return true;
    }
    if (kind == ParameterValueKind::Curve2D || kind == ParameterValueKind::Dependencies || kind == ParameterValueKind::LodOptions) {
        if (value.type != EDITOR_RECORD) return false;
        ParameterValue parsed; parsed.kind = kind;
        if (kind == ParameterValueKind::Curve2D) {
            const auto *fitting = Member(value, "fitting"), *points = Member(value, "points"), *flags = Member(value, "flags");
            if (value.fields.count != 3 || !fitting || fitting->type != EDITOR_REAL_VALUE || !points || points->type != EDITOR_REAL_LIST ||
                !flags || flags->type != EDITOR_INTEGER_LIST || flags->integers.count > 4096 || points->reals.count != flags->integers.count * 9 ||
                (points->reals.count && (!points->reals.data || !flags->integers.data))) return false;
            parsed.curve.fitting = fitting->real;
            for (uint32_t i = 0; i < flags->integers.count; ++i) {
                if (flags->integers.data[i] > UINT32_MAX) return false;
                ParameterCurvePoint point; point.flags = uint32_t(flags->integers.data[i]);
                std::copy_n(points->reals.data + i * 9, 9, point.values.begin()); parsed.curve.points.push_back(point);
            }
        } else {
            const bool dependencies = kind == ParameterValueKind::Dependencies;
            const auto *flags = Member(value, dependencies ? "mode" : "flags"), *list = Member(value, dependencies ? "options" : "proportions");
            if (value.fields.count != 2 || !flags || flags->type != EDITOR_INTEGER_VALUE || int64_t(flags->integer) < INT32_MIN ||
                int64_t(flags->integer) > INT32_MAX || !list) return false;
            parsed.integer = int32_t(int64_t(flags->integer));
            if (dependencies) {
                if (list->type != EDITOR_INTEGER_LIST || list->integers.count > 65536 || (list->integers.count && !list->integers.data)) return false;
                for (uint32_t i = 0; i < list->integers.count; ++i) {
                    if (list->integers.data[i] > UINT32_MAX) return false;
                    parsed.options.push_back(uint32_t(list->integers.data[i]));
                }
            } else {
                if (list->type != EDITOR_REAL_LIST || list->reals.count != 6 || !list->reals.data) return false;
                parsed.components.assign(list->reals.data, list->reals.data + 6);
            }
        }
        out = std::move(parsed); return true;
    }
    const auto count = ParameterComponentCount(kind);
    const auto expected = count ? EDITOR_REAL_LIST : ParameterRealValue(kind) ?
        EDITOR_REAL_VALUE : ParameterObjectValue(kind) ? EDITOR_OBJECT : kind == ParameterValueKind::Text ? EDITOR_TEXT_VALUE :
        kind == ParameterValueKind::ObjectArray ? EDITOR_OBJECT_LIST : EDITOR_INTEGER_VALUE;
    if (value.type != expected) return false;
    ParameterValue parsed;
    parsed.kind = kind;
    if (expected == EDITOR_REAL_LIST) {
        if (value.reals.count != count || !value.reals.data) return false;
        parsed.components.assign(value.reals.data, value.reals.data + count);
    }
    if (expected == EDITOR_TEXT_VALUE) {
        if (value.text.size > 1024 * 1024 || (value.text.size && !value.text.data)) return false;
        parsed.text.assign(value.text.data ? value.text.data : "", value.text.size);
    }
    if (expected == EDITOR_OBJECT_LIST) {
        if (value.objects.count > 65536 || (value.objects.count && !value.objects.data)) return false;
        if (value.objects.count) parsed.objects.assign(value.objects.data, value.objects.data + value.objects.count);
    }
    if (expected == EDITOR_REAL_VALUE) parsed.real = value.real;
    else if (expected == EDITOR_OBJECT) parsed.object = value.object;
    if (expected == EDITOR_INTEGER_VALUE) {
        const auto integer = int64_t(value.integer);
        if (integer < INT32_MIN || integer > INT32_MAX) return false;
        parsed.integer = int32_t(integer);
    }
    out = parsed;
    return true;
}
EditorStatus ParameterCommands::Check(void *data, const EditorHost *, const EditorRequest *request) {
    const auto &entry = *static_cast<Entry *>(data);
    auto *document = entry.owner->m_Current();
    const auto *target = Find(*request, "target"), *value = Find(*request, "value"), *type = Find(*request, "type");
    if (!document || document->Token() != request->scope.document || !target || !value || !type) return EDITOR_STALE;
    auto *parameter = CKParameter::Cast(document->Resolve(target->object, CKCID_PARAMETER));
    if (!parameter || TypeId(parameter->GetGUID()) != type->integer) return EDITOR_STALE;
    ParameterValueDescription description;
    ParameterValue parsed;
    if (!DescribeParameterValue(*document, parameter, description) || description.value.kind != kinds[entry.kind].kind ||
        !ParameterValueFromEditor(description, *value, parsed)) return EDITOR_INVALID;
    if (description.value.kind == ParameterValueKind::Structure && !SchemaMatches(description, Find(*request, "schema"))) return EDITOR_STALE;
    return ValidateParameterValue(*document, description, parsed) ? EDITOR_OK : EDITOR_REFUSED;
}
EditorStatus ParameterCommands::Describe(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto *document = static_cast<ParameterCommands *>(data)->m_Current();
    const auto *target = Find(*request, "target");
    if (!document || document->Token() != request->scope.document || !target) return EDITOR_STALE;
    auto *parameter = CKParameter::Cast(document->Resolve(target->object, CKCID_PARAMETER));
    ParameterValueDescription description;
    const char *property = DescribeParameterValue(*document, parameter, description) ? ParameterProperty(description.value.kind) : "";
    const EditorValue value{EDITOR_TEXT("property"), EDITOR_TEXT_VALUE, {}, EditorLiteral(property)};
    return sink.write ? sink.write(sink.data, &value, 1) : EDITOR_INVALID;
}
EditorStatus ParameterCommands::Watch(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto *document = static_cast<ParameterCommands *>(data)->m_Current();
    const auto *target = Find(*request, "target");
    if (!document || document->Token() != request->scope.document || !target) return EDITOR_STALE;
    auto *object = document->Resolve(target->object);
    if (!object) return EDITOR_STALE;
    auto *parameter = CKParameter::Cast(object);
    if (!parameter) return EDITOR_INVALID;
    if (!CKParameterLocal::Cast(parameter) && !CKParameterOut::Cast(parameter)) return EDITOR_INVALID;
    CKObject *owner = nullptr;
    auto *script = OwnerScript(parameter, owner);
    ParameterValueDescription description;
    const auto *property = DescribeParameterValue(*document, parameter, description) ? ParameterProperty(description.value.kind) : nullptr;
    const auto name = [](CKObject *object) { return AnsiToUtf8(object && object->GetName() ? object->GetName() : ""); };
    const std::vector<EditorField> fields{
        {"name", EDITOR_TEXT_VALUE, {}, CKParameterOperation::Cast(parameter->GetOwner()) ? "Out" : name(parameter)},
        {"display", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(ParameterText(parameter))},
        {"property", EDITOR_TEXT_VALUE, {}, property ? property : ""},
        {"type", EDITOR_INTEGER_VALUE, {}, {}, TypeId(parameter->GetGUID())},
        {"owner", EDITOR_OBJECT, document->Reference(owner ? owner->GetID() : 0)},
        {"owner-name", EDITOR_TEXT_VALUE, {}, name(owner)},
        {"script", EDITOR_OBJECT, document->Reference(script ? script->GetID() : 0)},
        {"script-name", EDITOR_TEXT_VALUE, {}, name(script)},
        {"editable", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(script && CKParameterLocal::Cast(parameter))}};
    const auto values = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
}
EditorStatus ParameterCommands::ClipboardParameters(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    const auto &self = *static_cast<ParameterCommands *>(data);
    auto *document = self.m_Current();
    if (!document || document->Token() != request->scope.document) return EDITOR_STALE;
    const std::vector<CK_ID> empty;
    return ParameterPage(*document, self.m_Clipboard && self.m_Clipboard->BelongsTo(*document) ? self.m_Clipboard->parameters : empty, *request, sink);
}
EditorStatus ParameterCommands::ScriptLocals(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto *document = static_cast<ParameterCommands *>(data)->m_Current();
    const auto *target = Find(*request, "target");
    if (!document || document->Token() != request->scope.document || !target) return EDITOR_STALE;
    auto *script = CKBehavior::Cast(document->Resolve(target->object, CKCID_BEHAVIOR));
    if (!script || !(script->GetType() & CKBEHAVIORTYPE_SCRIPT)) return EDITOR_INVALID;
    std::vector<CK_ID> locals;
    for (int i = 0; i < script->GetLocalParameterCount(); ++i)
        if (auto *local = script->GetLocalParameter(i); local && !local->IsMyselfParameter()) locals.push_back(local->GetID());
    return ParameterPage(*document, locals, *request, sink);
}
EditorStatus ParameterCommands::WriteText(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
    auto *document = static_cast<ParameterCommands *>(data)->m_Current();
    const auto *target = Find(*request, "target"), *type = Find(*request, "type"), *text = Find(*request, "text");
    if (!document || document->Token() != request->scope.document || !target || !type || !text) return EDITOR_STALE;
    auto *parameter = CKParameterLocal::Cast(document->Resolve(target->object, CKCID_PARAMETERLOCAL));
    if (!parameter || TypeId(parameter->GetGUID()) != type->integer) return EDITOR_STALE;
    CKObject *owner = nullptr;
    auto *script = OwnerScript(parameter, owner);
    if (!script) return EDITOR_REFUSED;
    const auto result = SetParameterText(*document, script->GetID(), parameter->GetID(),
        Utf8ToAnsi(std::string(text->text.data ? text->text.data : "", text->text.size)));
    return result.ok ? EDITOR_OK : result.Unchanged() ? EDITOR_NO_CHANGE : EDITOR_REFUSED;
}
EditorStatus ParameterCommands::Read(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    const auto &entry = *static_cast<Entry *>(data);
    auto *document = entry.owner->m_Current();
    const auto *target = Find(*request, "target");
    if (!document || document->Token() != request->scope.document || !target) return EDITOR_STALE;
    auto *parameter = CKParameter::Cast(document->Resolve(target->object, CKCID_PARAMETER));
    ParameterValueDescription description;
    const auto &kind = kinds[entry.kind];
    if (!DescribeParameterValue(*document, parameter, description) || description.value.kind != kind.kind) return EDITOR_INVALID;
    const auto fields = ParameterFields(description);
    const auto values = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
}
EditorStatus ParameterCommands::Choices(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto *document = static_cast<ParameterCommands *>(data)->m_Current();
    const auto *target = Find(*request, "target");
    if (!document || !target || document->Token() != request->scope.document) return EDITOR_STALE;
    auto *parameter = CKParameter::Cast(document->Resolve(target->object, CKCID_PARAMETER));
    ParameterValueDescription description;
    if (!DescribeParameterValue(*document, parameter, description) || (description.value.kind != ParameterValueKind::Object && description.value.kind != ParameterValueKind::ObjectArray)) return EDITOR_INVALID;
    const auto *classId = Find(*request, "class"), *derived = Find(*request, "derived"), *filter = Find(*request, "filter");
    const auto cid = classId && classId->integer ? CK_CLASSID(classId->integer) : description.objectClass;
    if (derived && derived->integer > 2) return EDITOR_INVALID;
    if (classId && classId->integer > INT32_MAX) return EDITOR_INVALID;
    if (!CKIsChildClassOf(cid, description.objectClass)) return EDITOR_INVALID;
    auto choices = ObjectsOfClass(*document, cid, !derived || derived->integer != 2,
        filter ? Utf8ToAnsi(std::string(filter->text.data ? filter->text.data : "", filter->text.size)) : std::string());
    std::vector<EditorString> labels;
    std::vector<ObjectRef> objects;
    for (auto &choice : choices) { choice.second = AnsiToUtf8(choice.second); objects.push_back(document->Reference(choice.first)); }
    for (const auto &choice : choices) labels.push_back(EditorText(choice.second));
    const EditorValue fields[] = {
        {EDITOR_TEXT("labels"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {labels.data(), uint32_t(labels.size())}},
        {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {objects.data(), uint32_t(objects.size())}}};
    return sink.write ? sink.write(sink.data, fields, 2) : EDITOR_INVALID;
}
EditorStatus ParameterCommands::Selections(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto *document = static_cast<ParameterCommands *>(data)->m_Current(); const auto *target = Find(*request, "target");
    if (!document || !target || document->Token() != request->scope.document) return EDITOR_STALE;
    auto *parameter = CKParameter::Cast(document->Resolve(target->object, CKCID_PARAMETER));
    ParameterValueDescription description;
    if (!DescribeParameterValue(*document, parameter, description)) return EDITOR_STALE;
    std::vector<EditorField> fields{{"labels", EDITOR_TEXT_LIST}, {"objects", EDITOR_OBJECT_LIST}, {"values", EDITOR_INTEGER_LIST},
        {"categories", EDITOR_TEXT_LIST}, {"classes", EDITOR_INTEGER_LIST}, {"class-labels", EDITOR_TEXT_LIST},
        {"owners", EDITOR_OBJECT_LIST}, {"owner-labels", EDITOR_TEXT_LIST}, {"owner", EDITOR_OBJECT}, {"class", EDITOR_INTEGER_VALUE},
        {"category", EDITOR_TEXT_VALUE}, {"users", EDITOR_INTEGER_LIST}, {"has-users", EDITOR_INTEGER_VALUE}};
    const EditorValue defaults{EDITOR_TEXT("filters"),EDITOR_RECORD};
    const auto *filters = Find(*request, "filters");
    if (!filters) filters=&defaults;
    if (filters->type != EDITOR_RECORD || filters->fields.count > 3) return EDITOR_INVALID;
    const auto *classId = Member(*filters,"class"), *ownerId = Member(*filters,"owner"), *categoryFilter = Member(*filters,"category");
    if ((classId && classId->type != EDITOR_INTEGER_VALUE) || (ownerId && ownerId->type != EDITOR_OBJECT) ||
        (categoryFilter && categoryFilter->type != EDITOR_TEXT_VALUE) ||
        filters->fields.count != unsigned(bool(classId)) + unsigned(bool(ownerId)) + unsigned(bool(categoryFilter))) return EDITOR_INVALID;
    auto *context = document->Context(); const auto kind = description.value.kind;
    const auto objects = [&](const ObjectChoices &choices, unsigned labels = 0, unsigned refs = 1) {
        for (const auto &choice : choices) {
            fields[labels].texts.push_back(AnsiToUtf8(choice.second)); fields[refs].objects.push_back(document->Reference(choice.first));
            if (refs == 1) {
                auto *synchro = CKSynchroObject::Cast(context->GetObject(choice.first));
                fields[11].integers.push_back(uint64_t(int64_t(synchro ? synchro->GetRendezVousNumberOfWaiters() : 0)));
            }
        }
    };
    if (kind == ParameterValueKind::Script) {
        auto *script = CKBehavior::Cast(document->Resolve(description.value.object)); auto *owner = script ? script->GetOwner() : nullptr;
        auto ref = owner ? document->Reference(owner->GetID()) : ObjectRef{};
        auto cid = owner ? owner->GetClassID() : CKCID_BEOBJECT;
        if (classId && classId->integer) {
            if (classId->integer > INT32_MAX || !CKIsChildClassOf(CK_CLASSID(classId->integer), CKCID_BEOBJECT)) return EDITOR_INVALID;
            cid = CK_CLASSID(classId->integer);
            if (owner && !CKIsChildClassOf(owner, cid)) ref = {};
        }
        if (ownerId) {
            if (ownerId->object && !document->Resolve(ownerId->object, cid)) return EDITOR_STALE;
            ref = ownerId->object;
        }
        fields[8].object = ref; fields[9].integer = cid;
        for (const auto &entry : DerivedClasses(CKCID_BEOBJECT)) { fields[4].integers.push_back(entry.first); fields[5].texts.push_back(AnsiToUtf8(entry.second)); }
        auto owners = ScriptOwners(*document, cid); owners.emplace_back(0, "NULL");
        std::stable_sort(owners.begin(), owners.end(), [](const auto &a, const auto &b) { return _stricmp(a.second.c_str(), b.second.c_str()) < 0; });
        objects(owners, 7, 6);
        objects(ScriptsOf(*document, document->NativeId(ref)));
    } else if (kind == ParameterValueKind::Animation || kind == ParameterValueKind::Synchro) {
        objects(ObjectsOfClass(*document, description.objectClass, true, ""));
        fields[9].integer = description.objectClass; fields[12].integer = description.objectClass == CKCID_SYNCHRO;
    } else if (kind == ParameterValueKind::Message) {
        auto *messages = context->GetMessageManager();
        if (!messages || messages->GetMessageTypeCount() > 65536) return EDITOR_INVALID;
        std::vector<std::pair<std::string, int>> names;
        for (int i = 0; i < messages->GetMessageTypeCount(); ++i) {
            const auto *name = messages->GetMessageTypeName(i);
            if (name && *name) names.emplace_back(AnsiToUtf8(name), i);
        }
        std::stable_sort(names.begin(), names.end(), [](const auto &a, const auto &b) { return _stricmp(a.first.c_str(), b.first.c_str()) < 0; });
        for (const auto &entry : names) { fields[0].texts.push_back(entry.first); fields[2].integers.push_back(uint64_t(int64_t(entry.second))); }
    } else if (kind == ParameterValueKind::Attribute) {
        auto *attributes = context->GetAttributeManager();
        if (!attributes || attributes->GetAttributeCount() > 65536 || attributes->GetCategoriesCount() > 65536) return EDITOR_INVALID;
        const auto *category = categoryFilter;
        const auto *native = description.value.integer >= 0 ? attributes->GetAttributeCategory(description.value.integer) : nullptr;
        const auto selected = category ? std::string(category->text.data ? category->text.data : "", category->text.size) : AnsiToUtf8(native ? native : "");
        fields[10].text = selected;
        for (int i = 0; i < attributes->GetCategoriesCount(); ++i) { const auto *name = attributes->GetCategoryName(i); fields[3].texts.push_back(AnsiToUtf8(name ? name : "")); }
        for (int i = 0; i < attributes->GetAttributeCount(); ++i) {
            if (!attributes->IsAttributeIndexValid(i)) continue;
            const auto *category = attributes->GetAttributeCategory(i), *name = attributes->GetAttributeNameByType(i);
            if (!selected.empty() && selected != AnsiToUtf8(category ? category : "")) continue;
            fields[0].texts.push_back(AnsiToUtf8(name ? name : "")); fields[2].integers.push_back(uint64_t(int64_t(i)));
        }
    } else return EDITOR_INVALID;
    const auto borrowed = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, borrowed.data(), uint32_t(borrowed.size())) : EDITOR_INVALID;
}
EditorStatus ParameterCommands::Write(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    const auto &entry = *static_cast<Entry *>(data);
    auto *document = entry.owner->m_Current();
    const auto *target = Find(*request, "target"), *value = Find(*request, "value"), *type = Find(*request, "type");
    if (!document || document->Token() != request->scope.document || !target || !value || !type) return EDITOR_STALE;
    const auto guid = CKGUID(CKDWORD(type->integer >> 32), CKDWORD(type->integer));
    const auto &kind = kinds[entry.kind];
    ParameterValue parsed;
    auto *parameter = CKParameter::Cast(document->Resolve(target->object, CKCID_PARAMETER));
    ParameterValueDescription description;
    if (!DescribeParameterValue(*document, parameter, description) || description.value.kind != kind.kind ||
        !ParameterValueFromEditor(description, *value, parsed)) return EDITOR_INVALID;
    if (kind.kind == ParameterValueKind::Structure && !SchemaMatches(description, Find(*request, "schema"))) return EDITOR_STALE;
    const auto result = SetParameterValue(*document, target->object, guid, parsed);
    if (!result.message.empty() && !result.ok && !result.Unchanged()) {
        const auto message = AnsiToUtf8(result.message);
        const EditorValue field{EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, {}, EditorText(message)};
        if (sink.write) sink.write(sink.data, &field, 1);
    }
    return result.ok ? EDITOR_OK : result.Unchanged() ? EDITOR_NO_CHANGE : EDITOR_REFUSED;
}
EditorStatus ParameterCommands::Copy(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
    auto *document = static_cast<ParameterCommands *>(data)->m_Current();
    const auto *target = Find(*request, "target"), *source = Find(*request, "source");
    if (!document || document->Token() != request->scope.document || !target || !source) return EDITOR_STALE;
    const auto result = CopyParameterValue(*document, target->object, source->object);
    return result.ok ? EDITOR_OK : result.Unchanged() ? EDITOR_NO_CHANGE : EDITOR_REFUSED;
}
EditorStatus ParameterCommands::CurvePreview(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto *document = static_cast<ParameterCommands *>(data)->m_Current();
    const auto *value = Find(*request, "value"); ParameterValue parsed;
    if (!document || document->Token() != request->scope.document) return EDITOR_STALE;
    std::vector<double> points, tangents;
    if (!value || !ParameterValueFromEditor(ParameterValueKind::Curve2D, *value, parsed) ||
        !ParameterCurvePreview(parsed.curve, points, tangents)) return EDITOR_INVALID;
    const EditorValue fields[] = {{EDITOR_TEXT("points"), EDITOR_REAL_LIST, {}, {}, 0, {}, {}, {}, 0, {points.data(), uint32_t(points.size())}},
        {EDITOR_TEXT("tangents"), EDITOR_REAL_LIST, {}, {}, 0, {}, {}, {}, 0, {tangents.data(), uint32_t(tangents.size())}}};
    return sink.write ? sink.write(sink.data, fields, 2) : EDITOR_INVALID;
}
bool ParameterCommands::Register(EditorApplication &editor, std::string &error) {
    std::vector<EditorCommandDesc> commands;
    std::vector<EditorQueryDesc> queries;
    std::vector<EditorPropertyDesc> properties;
    constexpr auto count = std::size(kinds);
    EditorParameter arguments[count][4]{}, fields[count][11]{};
    m_Entries.resize(count);
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT};
    const EditorParameter first{EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1};
    const EditorParameter pageFields[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE}};
    const EditorParameter localArgs[] = {target, first};
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("parameter.clipboard"), EDITOR_TEXT("builtin.parameters"),
        &first, 1, ClipboardParameters, this, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, pageFields, 3});
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("parameter.script-locals"), EDITOR_TEXT("builtin.parameters"),
        localArgs, 2, ScriptLocals, this, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, pageFields, 3});
    const EditorParameter watchFields[] = {{EDITOR_TEXT("name"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("display"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("property"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("owner"), EDITOR_OBJECT}, {EDITOR_TEXT("owner-name"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("script"), EDITOR_OBJECT}, {EDITOR_TEXT("script-name"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("editable"), EDITOR_INTEGER_VALUE}};
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("parameter.watch"), EDITOR_TEXT("builtin.parameters"),
        &target, 1, Watch, this, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, watchFields, uint32_t(std::size(watchFields))});
    const EditorParameter textArgs[] = {target, {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("text"), EDITOR_TEXT_VALUE}};
    commands.push_back({sizeof(EditorCommandDesc), EDITOR_TEXT("parameter.set-text"), EDITOR_TEXT("builtin.parameters"),
        EDITOR_TEXT("Set Value"), textArgs, 3, WriteText, this, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT});
    std::string queryIds[count], commandIds[count];
    for (unsigned i = 0; i < count; ++i) {
        const auto &kind = kinds[i];
        m_Entries[i] = {this, i};
        queryIds[i] = std::string(kind.property) + "-value";
        commandIds[i] = std::string(kind.property) + "-set";
        arguments[i][0] = target;
        arguments[i][1] = {EDITOR_TEXT("value"), kind.type, 0, kind.type == EDITOR_OBJECT ? 1u : 0u};
        arguments[i][2] = {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE};
        fields[i][0] = arguments[i][1];
        fields[i][1] = {EDITOR_TEXT("display"), EDITOR_TEXT_VALUE};
        fields[i][2] = arguments[i][2];
        fields[i][3] = {EDITOR_TEXT("labels"), EDITOR_TEXT_LIST};
        fields[i][4] = {EDITOR_TEXT("values"), EDITOR_INTEGER_LIST};
        fields[i][5] = {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST};
        fields[i][6] = {EDITOR_TEXT("scale"), EDITOR_REAL_VALUE};
        fields[i][7] = {EDITOR_TEXT("rows"), EDITOR_INTEGER_VALUE};
        fields[i][8] = {EDITOR_TEXT("columns"), EDITOR_INTEGER_VALUE};
        fields[i][9] = {EDITOR_TEXT("row-labels"), EDITOR_TEXT_LIST};
        arguments[i][3] = {EDITOR_TEXT("schema"), EDITOR_RECORD};
        fields[i][10] = arguments[i][3];
        const bool structure = kind.kind == ParameterValueKind::Structure;
        const bool schema = structure || kind.kind == ParameterValueKind::Dependencies;
        commands.push_back({sizeof(EditorCommandDesc), EditorText(commandIds[i]), EDITOR_TEXT("builtin.parameters"),
            EDITOR_TEXT("Set Value"), arguments[i], structure ? 4u : 3u, Write, &m_Entries[i], EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT, Check});
        queries.push_back({sizeof(EditorQueryDesc), EditorText(queryIds[i]), EDITOR_TEXT("builtin.parameters"),
            &target, 1, Read, &m_Entries[i], EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, fields[i], schema ? 11u : 10u});
        properties.push_back({sizeof(EditorPropertyDesc), EditorLiteral(kind.property), EDITOR_TEXT("Value"),
            EditorText(queryIds[i]), EDITOR_TEXT("value"), EditorText(commandIds[i]), EDITOR_TEXT("value"), EditorLiteral(kind.editor),
            EditorLiteral(ChoicesFor(kind.kind))});
    }
    const EditorParameter copyArgs[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT}, {EDITOR_TEXT("source"), EDITOR_OBJECT}};
    commands.push_back({sizeof(EditorCommandDesc), EDITOR_TEXT("parameter.copy-value"), EDITOR_TEXT("builtin.parameters"),
        EDITOR_TEXT("Set Value"), copyArgs, 2, Copy, this, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT});
    const EditorParameter property{EDITOR_TEXT("property"), EDITOR_TEXT_VALUE};
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("parameter.description"), EDITOR_TEXT("builtin.parameters"),
        &target, 1, Describe, this, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, &property, 1});
    const EditorParameter curveValue{EDITOR_TEXT("value"), EDITOR_RECORD};
    const EditorParameter curveFields[] = {{EDITOR_TEXT("points"), EDITOR_REAL_LIST}, {EDITOR_TEXT("tangents"), EDITOR_REAL_LIST}};
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("parameter.curve2d-preview"), EDITOR_TEXT("builtin.parameters"),
        &curveValue, 1, CurvePreview, this, EDITOR_QUERY_DOCUMENT, EDITOR_DOCUMENT, curveFields, 2});
    const EditorParameter choiceArgs[] = {target, {EDITOR_TEXT("class"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("derived"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("filter"), EDITOR_TEXT_VALUE, 1}};
    const EditorParameter choiceFields[] = {{EDITOR_TEXT("labels"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}};
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("parameter.object-choices"), EDITOR_TEXT("builtin.parameters"),
        choiceArgs, 4, Choices, this, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, choiceFields, 2});
    const EditorParameter selectionArgs[] = {target, {EDITOR_TEXT("filters"), EDITOR_RECORD, 1}};
    const EditorParameter selectionFields[] = {{EDITOR_TEXT("labels"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("values"), EDITOR_INTEGER_LIST}, {EDITOR_TEXT("categories"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST}, {EDITOR_TEXT("class-labels"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("owners"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("owner-labels"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("owner"), EDITOR_OBJECT}, {EDITOR_TEXT("class"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("category"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("users"), EDITOR_INTEGER_LIST}, {EDITOR_TEXT("has-users"), EDITOR_INTEGER_VALUE}};
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("parameter.selection-choices"), EDITOR_TEXT("builtin.parameters"),
        selectionArgs, 2, Selections, this, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, selectionFields, uint32_t(std::size(selectionFields))});
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("parameters.editing");
    module.commands = commands.data(); module.commandCount = uint32_t(commands.size());
    module.queries = queries.data(); module.queryCount = uint32_t(queries.size());
    module.properties = properties.data(); module.propertyCount = uint32_t(properties.size());
    return editor.Register(module, error);
}
} // namespace vdev
