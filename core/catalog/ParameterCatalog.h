#pragma once

#include <string>
#include <vector>

#include "CKAll.h"

namespace vdev {

// Parameter types and parameter operations registered with the
// CKParameterManager, for the local parameter and operation pickers.
// Names are ANSI as registered.

struct ParameterTypeEntry {
    CKGUID guid;
    std::string name;
};

struct OperationSignature {
    CKGUID operation;
    CKGUID result;
    CKGUID input1;
    CKGUID input2;
    std::string label; // "Result = Name(Input1, Input2)"
};

struct OperationEntry {
    CKGUID guid;
    std::string name;
};

// Valid types not flagged CKPARAMETERTYPE_HIDDEN, sorted by name.
std::vector<ParameterTypeEntry> ListParameterTypes(CKContext *context);
// Registered operations, sorted by name.
std::vector<OperationEntry> ListParameterOperations(CKContext *context);
// Type combinations registered for one operation.
std::vector<OperationSignature> ListOperationSignatures(CKContext *context, const CKGUID &operation);

// Dev.exe sub_5977D0 / sub_5969F0 (Add Parameter Operation): the type
// combinations CK finds for the parts given (null: any), asked through
// temporary parameters of those types as Dev does, so that derived types are
// accepted (CKParameterManager::GetAvailableOperationsDesc); with swap the
// two inputs are also tried the other way round. The signatures carry the
// declared types.
std::vector<OperationSignature> AvailableOperationSignatures(CKContext *context, const CKGUID *operation,
                                                             const CKGUID *result, const CKGUID *input1,
                                                             const CKGUID *input2, bool swap = true);

} // namespace vdev
