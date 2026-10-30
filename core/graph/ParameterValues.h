#pragma once
#include "core/catalog/ParameterUIs.h"
#include "core/document/EditResult.h"

namespace vdev {
enum class ParameterValueKind { None, Boolean, Integer, Real, Enumeration, Object };
struct ParameterValue {
    ParameterValueKind kind = ParameterValueKind::None;
    int32_t integer = 0;
    double real = 0;
    ObjectRef object{};
};
bool operator==(const ParameterValue &, const ParameterValue &);
inline bool operator!=(const ParameterValue &a, const ParameterValue &b) { return !(a == b); }
struct ParameterValueDescription {
    CKGUID type;
    ParameterValue value;
    std::string display;
    std::vector<std::pair<int32_t, std::string>> choices;
    CK_CLASSID objectClass = 0;
    double displayScale = 1;
};
// The same type selection and validation serve inline values and dialog copies.
// Complex types retain their existing specialized editors.
bool DescribeParameterValue(const Document &, CKParameter *, ParameterValueDescription &);
bool ParseParameterValue(const ParameterValueDescription &, const std::string &, ParameterValue &);
bool ValidateParameterValue(const Document &, const ParameterValueDescription &, const ParameterValue &);
// Only a dialog's owned, dynamic working parameter may be passed here.
bool WriteParameterDraft(const Document &, CKParameter *, const ParameterValue &);
EditResult SetParameterValue(Document &, ObjectRef target, const CKGUID &type, const ParameterValue &,
                              CK_ID script = 0);
} // namespace vdev
