#pragma once
#include "core/graph/ParameterValues.h"
namespace vdev {
// Copy a compatible parameter or an object's position, orientation, bounds or
// object reference into a dialog's dynamic working parameter.
bool PickParameterDraft(const Document &, CKParameter *, CKObject *, bool relative, CK_CLASSID filterClass = 0,
                        bool derived = true, const std::string &filter = {});
} // namespace vdev
