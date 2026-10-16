#include "core/catalog/ParameterCatalog.h"

#include <algorithm>

namespace vdev {

namespace {

std::string TypeNameOf(CKParameterManager *manager, const CKGUID &guid) {
    if (guid == CKGUID(0)) return "-";
    CKParameterTypeDesc *description = manager->GetParameterTypeDescription(guid);
    return description && description->TypeName.CStr() ? description->TypeName.CStr() : "?";
}

} // namespace

std::vector<ParameterTypeEntry> ListParameterTypes(CKContext *context) {
    std::vector<ParameterTypeEntry> types;
    CKParameterManager *manager = context ? context->GetParameterManager() : nullptr;
    if (!manager) return types;
    for (int i = 0; i < manager->GetParameterTypesCount(); ++i) {
        CKParameterTypeDesc *description = manager->GetParameterTypeDescription(i);
        if (!description || !description->Valid || (description->dwFlags & CKPARAMETERTYPE_HIDDEN)) continue;
        const char *name = description->TypeName.CStr();
        if (!name || !*name) continue;
        types.push_back({description->Guid, name});
    }
    std::sort(types.begin(), types.end(),
              [](const ParameterTypeEntry &a, const ParameterTypeEntry &b) { return a.name < b.name; });
    return types;
}

std::vector<OperationEntry> ListParameterOperations(CKContext *context) {
    std::vector<OperationEntry> operations;
    CKParameterManager *manager = context ? context->GetParameterManager() : nullptr;
    if (!manager) return operations;
    for (int i = 0; i < manager->GetParameterOperationCount(); ++i) {
        const char *name = manager->OperationCodeToName(i);
        if (!name || !*name) continue;
        operations.push_back({manager->OperationCodeToGuid(i), name});
    }
    std::sort(operations.begin(), operations.end(),
              [](const OperationEntry &a, const OperationEntry &b) { return a.name < b.name; });
    return operations;
}

std::vector<OperationSignature> ListOperationSignatures(CKContext *context, const CKGUID &operation) {
    std::vector<OperationSignature> signatures;
    CKParameterManager *manager = context ? context->GetParameterManager() : nullptr;
    if (!manager) return signatures;
    const int count = manager->GetAvailableOperationsDesc(operation, nullptr, nullptr, nullptr, nullptr);
    if (count <= 0) return signatures;
    std::vector<CKOperationDesc> descriptions(static_cast<size_t>(count));
    manager->GetAvailableOperationsDesc(operation, nullptr, nullptr, nullptr, descriptions.data());
    const char *name = manager->OperationGuidToName(operation);
    for (const auto &description : descriptions) {
        OperationSignature signature;
        signature.operation = operation;
        signature.result = description.ResGuid;
        signature.input1 = description.P1Guid;
        signature.input2 = description.P2Guid;
        signature.label = TypeNameOf(manager, description.ResGuid) + " = " + (name ? name : "") + "(" +
                          TypeNameOf(manager, description.P1Guid) + ", " + TypeNameOf(manager, description.P2Guid) +
                          ")";
        signatures.push_back(signature);
    }
    std::sort(signatures.begin(), signatures.end(),
              [](const OperationSignature &a, const OperationSignature &b) { return a.label < b.label; });
    return signatures;
}

std::vector<OperationSignature> AvailableOperationSignatures(CKContext *context, const CKGUID *operation,
                                                             const CKGUID *result, const CKGUID *input1,
                                                             const CKGUID *input2, bool swap) {
    std::vector<OperationSignature> signatures;
    CKParameterManager *manager = context ? context->GetParameterManager() : nullptr;
    if (!manager) return signatures;
    auto ask = [&](const CKGUID *first, const CKGUID *second) {
        CKParameterIn *p1 = first ? context->CreateCKParameterIn((CKSTRING) "pin1", *first, TRUE) : nullptr;
        CKParameterIn *p2 = second ? context->CreateCKParameterIn((CKSTRING) "pin2", *second, TRUE) : nullptr;
        CKParameterOut *out = result ? context->CreateCKParameterOut((CKSTRING) "pout", *result, TRUE) : nullptr;
        CKGUID guid = operation ? *operation : CKGUID();
        const int count = manager->GetAvailableOperationsDesc(guid, out, p1, p2, nullptr);
        if (count > 0) {
            std::vector<CKOperationDesc> descriptions(static_cast<size_t>(count));
            manager->GetAvailableOperationsDesc(guid, out, p1, p2, descriptions.data());
            for (const CKOperationDesc &description : descriptions) {
                OperationSignature signature;
                signature.operation = description.OpGuid;
                signature.result = description.ResGuid;
                signature.input1 = description.P1Guid;
                signature.input2 = description.P2Guid;
                const char *name = manager->OperationGuidToName(description.OpGuid);
                signature.label = TypeNameOf(manager, description.ResGuid) + " = " + (name ? name : "") + "(" +
                                  TypeNameOf(manager, description.P1Guid) + ", " + TypeNameOf(manager, description.P2Guid) +
                                  ")";
                signatures.push_back(signature);
            }
        }
        for (CKObject *temporary : {static_cast<CKObject *>(p1), static_cast<CKObject *>(p2), static_cast<CKObject *>(out)})
            if (temporary) context->DestroyObject(temporary, CK_DESTROY_NONOTIFY);
    };
    ask(input1, input2);
    if (swap && signatures.empty()) ask(input2, input1);
    return signatures;
}

} // namespace vdev
