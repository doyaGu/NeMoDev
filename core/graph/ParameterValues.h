#pragma once
#include "core/catalog/ParameterUIs.h"
#include "core/document/EditResult.h"
#include "core/graph/SpecializedParameterValues.h"
#include "core/graph/ParameterValueData.h"

namespace vdev {
struct ParameterValueDescription {
    CKGUID type;
    ParameterValue value;
    std::string display;
    std::vector<std::pair<int32_t, std::string>> choices;
    CK_CLASSID objectClass = 0;
    double displayScale = 1;
    unsigned rows = 1, columns = 1;
    std::vector<std::string> componentLabels, rowLabels;
    ObjectRef parameter{};
    std::string name;
    std::vector<ParameterValueDescription> members;
    struct DependencyClass {
        uint32_t id = 0, depth = 0;
        std::string name;
        std::vector<std::string> options;
    };
    std::vector<DependencyClass> dependencyClasses;
};
struct ParameterMember {
    ObjectRef parameter;
    CKGUID type;
    std::string name;
};
bool ParameterMembers(const Document &, CKParameter *, std::vector<ParameterMember> &);
ParameterValue ParameterDraftValue(ParameterValue, const ParameterValue &);
// The same type selection and validation serve inline values and dialog copies.
// Native data stays in the core; public editors receive typed values and metadata.
bool DescribeParameterValue(const Document &, CKParameter *, ParameterValueDescription &);
bool ParseParameterValue(const ParameterValueDescription &, const std::string &, ParameterValue &);
bool ValidateParameterValue(const Document &, const ParameterValueDescription &, const ParameterValue &);
// Only a dialog's owned, dynamic working parameter may be passed here.
bool WriteParameterDraft(const Document &, CKParameter *, const ParameterValue &);
EditResult SetParameterValue(Document &, ObjectRef target, const CKGUID &type, const ParameterValue &,
                              CK_ID script = 0);
EditResult CopyParameterValue(Document &, ObjectRef target, ObjectRef source, CK_ID script = 0);
} // namespace vdev
