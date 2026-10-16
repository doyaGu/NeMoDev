#pragma once

#include <map>
#include <ostream>
#include <string>

#include "CKAll.h"

namespace ProjectParameterTypes {

struct State {
    CK_CLASSID classId = 0;
    std::string name;
    CKGUID guid;
    bool hasObjectValue = false;
    CK_ID objectValue = 0;
    bool hasEnumValue = false;
    int enumValue = 0;
    std::string enumName;
};

using Snapshot = std::map<CK_ID, State>;

// Before saving, keys are runtime CK_IDs. After independently loading the
// output, CKFileObject::Object maps each new runtime ID to that saved ID.
// This avoids assuming an ID offset or matching duplicate parameter names.
inline bool Capture(CKFile& file, bool useSavedIds, Snapshot& result, std::ostream& errors) {
    result.clear();
    std::map<CK_ID, CK_ID> savedIds;
    for (int i = 0; i < file.m_FileObjects.Size(); ++i) {
        const auto& entry = file.m_FileObjects[i];
        if (entry.ObjPtr) savedIds.emplace(entry.ObjPtr->GetID(), entry.Object);
    }
    for (int i = 0; i < file.m_FileObjects.Size(); ++i) {
        const auto& entry = file.m_FileObjects[i];
        CKObject* object = entry.ObjPtr;
        if (!object) {
            if (CKIsChildClassOf(entry.ObjectCid, CKCID_PARAMETER) ||
                CKIsChildClassOf(entry.ObjectCid, CKCID_PARAMETERIN)) {
                errors << "Parameter object not loaded: " << entry.Object << '\n';
                return false;
            }
            continue;
        }
        auto* parameter = CKParameter::Cast(object);
        auto* input = CKParameterIn::Cast(object);
        if (!parameter && !input) continue;
        State state;
        state.classId = object->GetClassID();
        state.name = object->GetName() ? object->GetName() : "";
        state.guid = parameter ? parameter->GetGUID() : input->GetGUID();
        auto* description = file.m_Context->GetParameterManager()->GetParameterTypeDescription(state.guid);
        if (!description) {
            errors << "Unregistered parameter type: " << state.name
                   << " id=" << object->GetID() << '\n';
            return false;
        }
        if (parameter && description->Cid != 0) {
            state.hasObjectValue = true;
            // Do not evaluate a parameter operation while checking a file.
            if (parameter->GetDataSize() != sizeof(state.objectValue) ||
                parameter->GetValue(&state.objectValue, FALSE) != CK_OK) {
                errors << "Cannot read object parameter: " << state.name << '\n';
                return false;
            }
            if (useSavedIds && state.objectValue != 0) {
                const auto found = savedIds.find(state.objectValue);
                if (found == savedIds.end()) {
                    errors << "Object parameter reference absent from output: " << state.name << '\n';
                    return false;
                }
                state.objectValue = found->second;
            }
        }
        if (parameter && (description->dwFlags & CKPARAMETERTYPE_ENUMS)) {
            state.hasEnumValue = true;
            if (parameter->GetDataSize() != sizeof(state.enumValue) ||
                parameter->GetValue(&state.enumValue, FALSE) != CK_OK) {
                errors << "Cannot read enum parameter: " << state.name << '\n';
                return false;
            }
            auto* enumeration = file.m_Context->GetParameterManager()->GetEnumDescByType(parameter->GetType());
            if (enumeration) {
                for (int index = 0; index < enumeration->GetNumEnums(); ++index) {
                    if (enumeration->GetEnumValue(index) == state.enumValue) {
                        const char* label = enumeration->GetEnumDescription(index);
                        state.enumName = label ? label : "";
                        break;
                    }
                }
                // Dynamic enums (for example System Font Name) remap integer
                // values between contexts. Only a unique nonempty label may
                // be used for comparison; ambiguous labels require exact ints.
                int matches = 0;
                for (int index = 0; index < enumeration->GetNumEnums(); ++index) {
                    const char* label = enumeration->GetEnumDescription(index);
                    if (label && state.enumName == label) ++matches;
                }
                if (matches != 1) state.enumName.clear();
            }
        }
        const CK_ID id = useSavedIds ? file.m_FileObjects[i].Object : object->GetID();
        if (!result.emplace(id, state).second) {
            errors << "Duplicate parameter identity: " << id << '\n';
            return false;
        }
    }
    return true;
}

inline bool Compare(const Snapshot& expected, const Snapshot& actual, std::ostream& errors) {
    if (expected.size() != actual.size()) {
        errors << "Parameter count changed: " << expected.size() << " -> " << actual.size() << '\n';
        return false;
    }
    for (const auto& item : expected) {
        const auto found = actual.find(item.first);
        const State& a = item.second;
        if (found == actual.end()) {
            errors << "Parameter missing from output: " << a.name << " id=" << item.first << '\n';
            return false;
        }
        const State& b = found->second;
        if (a.classId != b.classId || a.name != b.name || a.guid != b.guid ||
            a.hasObjectValue != b.hasObjectValue || a.objectValue != b.objectValue ||
            a.hasEnumValue != b.hasEnumValue || a.enumName != b.enumName ||
            (a.hasEnumValue && a.enumName.empty() && a.enumValue != b.enumValue)) {
            errors << "Parameter type, object reference or enum value changed: " << a.name
                   << " id=" << item.first << '\n';
            return false;
        }
    }
    return true;
}

} // namespace ProjectParameterTypes
