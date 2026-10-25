#include "core/graph/ValueCommands.h"

#include <memory>

#include "core/runtime/CkRuntimeHost.h"
#include "core/level/ObjectEditCommands.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/document/ScriptSession.h"
#include "core/graph/detail/TopologyInternal.h"

namespace vdev {

// A value is either raw bytes, or (for memory-managing types) its text.
struct ParameterValue {
    CKGUID type;
    std::uint64_t generation = 0;
    bool text = false;
    std::vector<unsigned char> bytes;
    std::string string;
};

std::vector<unsigned char> ParameterBytes(CKParameter *parameter) {
    std::vector<unsigned char> bytes;
    ParameterBytes(parameter, bytes);
    return bytes;
}

bool ParameterBytes(CKParameter *parameter, std::vector<unsigned char> &bytes) {
    bytes.clear();
    if (!parameter) return false;
    const int size = parameter->GetDataSize();
    if (size < 0) return false;
    if (!size) return true;
    bytes.resize(static_cast<size_t>(size));
    // Do not evaluate a parameter operation while reading a value.
    if (parameter->GetValue(bytes.data(), FALSE) == CK_OK) return true;
    bytes.clear();
    return false;
}

static CKParameter *ParameterInstance(Document &document, CK_ID id, CKParameter *instance, const ParameterValue &value) {
    if (document.Host().Generation() != value.generation) return nullptr;
    CKContext *context = document.Context();
    CKParameter *parameter = context ? CKParameter::Cast(context->GetObject(id)) : nullptr;
    return parameter && parameter == instance && parameter->GetGUID() == value.type ? parameter : nullptr;
}

static bool SameValue(const ParameterValue &a, const ParameterValue &b) {
    return a.generation == b.generation && a.type == b.type && a.text == b.text &&
           (a.text ? a.string == b.string : a.bytes == b.bytes);
}

static bool ReadParameterValue(Document &document, CK_ID id, CKParameter *instance, ParameterValue &value) {
    bool read = false;
    return GuardedCall([&] {
        CKParameter *parameter = ParameterInstance(document, id, instance, value);
        if (!parameter) return;
        read = value.text ? ParameterText(parameter, value.string) : ParameterBytes(parameter, value.bytes);
        read = read && ParameterInstance(document, id, instance, value);
    }, &document.Host(), "Read parameter value") && read;
}

static bool WriteParameterValue(Document &document, CK_ID id, CKParameter *instance, const ParameterValue &value) {
    CKERROR result = CKERR_INVALIDPARAMETER;
    const bool written = GuardedCall([&] {
        CKParameter *parameter = ParameterInstance(document, id, instance, value);
        if (!parameter) return;
        result = value.text ? parameter->SetStringValue(value.string.c_str())
                            : parameter->SetValue(value.bytes.data(), static_cast<int>(value.bytes.size()));
    }, &document.Host(), "Write parameter value");
    if (!written || result != CK_OK) return false;
    ParameterValue actual = value;
    return ReadParameterValue(document, id, instance, actual) && SameValue(actual, value);
}

class ParameterValueAction : public Action {
public:
    ParameterValueAction(CK_ID id, CKParameter *instance, ParameterValue before, ParameterValue after)
        : m_Id(id), m_Instance(instance), m_Before(std::move(before)), m_After(std::move(after)) {}
    bool Redo(Document &document) override { return Apply(document, false); }
    bool Undo(Document &document) override { return Apply(document, true); }
private:
    bool Apply(Document &document, bool undo) {
        if (WriteParameterValue(document, m_Id, m_Instance, undo ? m_Before : m_After)) return true;
        // A rejected setter may already have written. This action alone owns
        // recovery of its value, including a failing Undo or Redo.
        if (!WriteParameterValue(document, m_Id, m_Instance, undo ? m_After : m_Before)) {
            document.Host().Log(LogLevel::Error, "Parameter value recovery was incomplete: the undo history is cleared");
            if (document.Host().Generation() == m_Before.generation) document.InvalidateHistory();
        }
        return false;
    }
    CK_ID m_Id;
    CKParameter *m_Instance;
    ParameterValue m_Before, m_After;
};

static EditResult PushValue(Document &document, CK_ID script, CK_ID id, CKParameter *instance,
                            ParameterValue before, ParameterValue after) {
    if (SameValue(before, after)) return EditResult::NoChange("The value is unchanged");
    Edit edit(document, "Set Value");
    if (!edit.Do(std::make_unique<ParameterValueAction>(id, instance, std::move(before), std::move(after)))) {
        edit.Fail("The value could not be applied");
        return EditResult::Error("The value could not be applied; check the runtime log for recovery failures");
    }
    edit.Touches(script);
    return edit.Commit();
}

EditResult SetParameterBytes(Document &document, CK_ID script, CK_ID id,
                             const std::vector<unsigned char> &bytes) {
    CKParameter *parameter = CKParameter::Cast(document.Context()->GetObject(id));
    if (!parameter) return EditResult::Refused("Parameter not found");
    if (ManagesMemory(document.Context(), parameter->GetType()))
        return EditResult::Refused("Values of this type can only be set as text");
    ParameterValue before, after;
    before.type = after.type = parameter->GetGUID();
    before.generation = after.generation = document.Host().Generation();
    if (!ReadParameterValue(document, id, parameter, before)) return EditResult::Error("Cannot read the parameter value");
    after.bytes = bytes;
    return PushValue(document, script, id, parameter, std::move(before), std::move(after));
}

EditResult SetParameterText(Document &document, CK_ID script, CK_ID id, const std::string &text) {
    CKParameter *parameter = CKParameter::Cast(document.Context()->GetObject(id));
    if (!parameter) return EditResult::Refused("Parameter not found");
    ParameterValue before, after;
    before.type = after.type = parameter->GetGUID();
    before.generation = after.generation = document.Host().Generation();
    before.text = after.text = ManagesMemory(document.Context(), parameter->GetType());
    if (!ReadParameterValue(document, id, parameter, before)) return EditResult::Error("Cannot read the parameter value");
    // Parse with the type's own function, capture the normalized result, and
    // restore before recording an action. All temporary writes are owned here.
    CKERROR parsed = CKERR_INVALIDPARAMETER;
    const bool called = GuardedCall([&] { parsed = parameter->SetStringValue(text.c_str()); },
                                    &document.Host(), "CKParameter::SetStringValue");
    const bool read = called && parsed == CK_OK && ReadParameterValue(document, id, parameter, after);
    if (!WriteParameterValue(document, id, parameter, before)) {
        if (document.Host().Generation() == before.generation) document.InvalidateHistory();
        return EditResult::Error("The parameter value could not be restored: the undo history is cleared");
    }
    if (!called) return EditResult::Error("The parameter type raised an exception");
    if (parsed != CK_OK) return EditResult::Refused("The text is not a valid value of this type");
    if (!read) return EditResult::Error("Cannot read the parsed parameter value");
    return PushValue(document, script, id, parameter, std::move(before), std::move(after));
}

CKParameterLocal *EditableValueOf(CKParameterIn *input) {
    return input ? CKParameterLocal::Cast(input->GetDirectSource()) : nullptr;
}

EditResult SetParameterType(Document &document, CK_ID id, const CKGUID &type) {
    CKContext *context = document.Context();
    CKObject *object = context->GetObject(id);
    CKParameterManager *manager = context->GetParameterManager();
    if (!CKParameter::Cast(object) && !CKParameterIn::Cast(object)) return EditResult::Refused("Parameter not found");
    if (!manager || manager->ParameterGuidToType(type) < 0) return EditResult::Refused("Unknown parameter type");
    std::vector<CK_ID> objects(1, id);
    CKParameterIn *input = CKParameterIn::Cast(object);
    // Only a value holder follows an input (a linked output keeps its type).
    bool holder = false;
    if (input) {
        if (input->GetGUID() == type) return EditResult::NoChange("The type is unchanged");
        // Documentation "Behavior Building Block": "the linked local
        // parameter's type will be updated if this one is linked only once".
        CKParameterLocal *local = EditableValueOf(input);
        if (local && detail::OtherUseOf(document, local, input).empty()) {
            objects.push_back(local->GetID());
            holder = true;
        }
    } else if (CKParameter::Cast(object)->GetGUID() == type) {
        return EditResult::NoChange("The type is unchanged");
    }
    return EditObjectsState(document, objects, "Edit Parameter Type", [id, type, holder](CKObject *changed) {
        if (changed->GetID() != id) return;
        if (CKParameterIn *in = CKParameterIn::Cast(changed)) in->SetGUID(type, holder);
        else if (CKParameter *parameter = CKParameter::Cast(changed)) parameter->SetGUID(type);
    });
}

std::vector<ElementRef> IncompatibleParameterLinks(Document &document, CK_ID script, CK_ID parameter, const CKGUID &type) {
    std::vector<ElementRef> links;
    CKContext *context = document.Context();
    CKParameterManager *manager = context ? context->GetParameterManager() : nullptr;
    ScriptSession *session = document.Session(script);
    if (!manager || !session) return links;
    // The parameters taking the new type: it, and the value holder that follows an input.
    const bool predicted = type.d1 != 0 || type.d2 != 0;
    const CKParameterType retyped = predicted ? manager->ParameterGuidToType(type) : -1;
    CK_ID follower = 0;
    if (predicted)
        if (CKParameterIn *input = CKParameterIn::Cast(context->GetObject(parameter)))
            if (CKParameterLocal *local = EditableValueOf(input))
                if (detail::OtherUseOf(document, local, input).empty()) follower = local->GetID();
    auto typeOf = [&](CKObject *object) -> CKParameterType {
        if (predicted && retyped >= 0 && (object->GetID() == parameter || object->GetID() == follower)) return retyped;
        if (CKParameterIn *input = CKParameterIn::Cast(object)) return input->GetType();
        if (CKParameter *value = CKParameter::Cast(object)) return value->GetType();
        return -1;
    };
    InterfaceData data = session->Data();
    detail::ForEachRecord(data, [&](BehaviorData &record) {
        for (int i = 0; i < static_cast<int>(record.links.size()); ++i) {
            const Link &link = record.links[i];
            if (link.IsBehaviorLink()) continue;
            CKObject *from = ResolveEndpointObject(context, record, link.start);
            CKObject *to = ResolveEndpointObject(context, record, link.end);
            if (!from || !to || from == to || (from->GetID() != parameter && to->GetID() != parameter)) continue;
            const CKParameterType source = typeOf(from), destination = typeOf(to);
            if (source < 0 || destination < 0 || manager->IsTypeCompatible(source, destination)) continue;
            links.push_back(ElementRef{ElementKind::Link, record.id, link.id, i});
        }
    });
    return links;
}

} // namespace vdev
