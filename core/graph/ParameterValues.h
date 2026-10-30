#pragma once
#include "core/catalog/ParameterUIs.h"
#include "core/document/EditResult.h"

namespace vdev {
enum class ParameterValueKind { None, Boolean, Integer, Real, Enumeration, Object };
struct ParameterScalar {
    ParameterValueKind kind = ParameterValueKind::None;
    int32_t integer = 0;
    double real = 0;
    ObjectRef object{};
};
bool operator==(const ParameterScalar &, const ParameterScalar &);
inline bool operator!=(const ParameterScalar &a, const ParameterScalar &b) { return !(a == b); }
struct ParameterValueDescription {
    CKGUID type;
    ParameterScalar value;
    std::string display;
    std::vector<std::pair<int32_t, std::string>> choices;
    CK_CLASSID objectClass = 0;
    double displayScale = 1;
};
// The same type selection and validation serve inline values and dialog copies.
// Complex types retain their existing specialized editors.
bool DescribeParameterValue(const Document &, CKParameter *, ParameterValueDescription &);
bool ParseParameterScalar(const ParameterValueDescription &, const std::string &, ParameterScalar &);
bool ValidateParameterScalar(const Document &, const ParameterValueDescription &, const ParameterScalar &);
// Only a dialog's owned, dynamic working parameter may be passed here.
bool WriteParameterDraft(const Document &, CKParameter *, const ParameterScalar &);
EditResult SetParameterScalar(Document &, ObjectRef target, const CKGUID &type, const ParameterScalar &,
                              CK_ID script = 0);
} // namespace vdev
