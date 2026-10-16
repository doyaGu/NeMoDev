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

namespace {

// A value is either raw bytes, or (for memory-managing types) its text.
struct Value {
    bool text = false;
    std::vector<unsigned char> bytes;
    std::string string;
};

bool SetValue(Document &document, CK_ID id, const Value &value) {
    CKParameter *parameter = CKParameter::Cast(document.Context()->GetObject(id));
    if (!parameter) return false;
    CKERROR result = CKERR_INVALIDPARAMETER;
    GuardedCall(
        [&] {
            result = value.text ? parameter->SetStringValue(const_cast<CKSTRING>(value.string.c_str()))
                                : parameter->SetValue(value.bytes.data(), static_cast<int>(value.bytes.size()));
        },
        &document.Host(), "CKParameter value");
    return result == CK_OK;
}

} // namespace

std::vector<unsigned char> ParameterBytes(CKParameter *parameter) {
    std::vector<unsigned char> bytes;
    if (!parameter) return bytes;
    const int size = parameter->GetDataSize();
    if (size <= 0) return bytes;
    bytes.resize(static_cast<size_t>(size));
    // Do not evaluate a parameter operation while reading a value.
    if (parameter->GetValue(bytes.data(), FALSE) != CK_OK) bytes.clear();
    return bytes;
}

namespace {

// One undo step; the Script shows the value (its session is marked with the composition).
EditResult Push(Document &document, CK_ID script, CK_ID id, Value before, Value after) {
    if (before.text ? before.string == after.string : before.bytes == after.bytes)
        return EditResult::NoChange("The value is unchanged");
    Edit edit(document, "Set Value");
    if (!edit.Do([&document, id, after] { return SetValue(document, id, after); },
                 [&document, id, before] { return SetValue(document, id, before); }))
        return edit.Fail("The value was rejected");
    edit.Touches(script);
    return edit.Commit();
}

} // namespace

EditResult SetParameterBytes(Document &document, CK_ID script, CK_ID id,
                             const std::vector<unsigned char> &bytes) {
    CKParameter *parameter = CKParameter::Cast(document.Context()->GetObject(id));
    if (!parameter) return EditResult::Refused("Parameter not found");
    if (ManagesMemory(document.Context(), parameter->GetType()))
        return EditResult::Refused("Values of this type can only be set as text");
    Value before, after;
    before.bytes = ParameterBytes(parameter);
    after.bytes = bytes;
    return Push(document, script, id, std::move(before), std::move(after));
}

EditResult SetParameterText(Document &document, CK_ID script, CK_ID id,
                            const std::string &text) {
    CKParameter *parameter = CKParameter::Cast(document.Context()->GetObject(id));
    if (!parameter) return EditResult::Refused("Parameter not found");
    const bool managed = ManagesMemory(document.Context(), parameter->GetType());
    Value before, after;
    before.text = after.text = managed;
    if (managed) before.string = ParameterText(parameter);
    else before.bytes = ParameterBytes(parameter);
    // Parse with the type's own function, read the result, restore.
    CKERROR parsed = CKERR_INVALIDPARAMETER;
    const bool read = GuardedCall(
        [&] {
            parsed = parameter->SetStringValue(const_cast<CKSTRING>(text.c_str()));
            if (managed) after.string = ParameterText(parameter);
            else after.bytes = ParameterBytes(parameter);
        },
        &document.Host(), "CKParameter::SetStringValue");
    // The type's own functions run again to put the value back.
    const bool restored = GuardedCall(
        [&] {
            if (managed) parameter->SetStringValue(const_cast<CKSTRING>(before.string.c_str()));
            else parameter->SetValue(before.bytes.data(), static_cast<int>(before.bytes.size()));
        },
        &document.Host(), "CKParameter::SetStringValue");
    if (!read || !restored) return EditResult::Error("The parameter type raised an exception");
    if (parsed != CK_OK) return EditResult::Refused("The text is not a valid value of this type");
    return Push(document, script, id, std::move(before), std::move(after));
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
