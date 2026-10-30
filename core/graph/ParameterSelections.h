#pragma once
#include "core/graph/ParameterValues.h"
namespace vdev {
// Native selectors read their choices here; drafts never create manager entries
// or change objects referenced by a parameter.
bool DescribeParameterSelection(const Document &, CKParameter *, ParameterValueDescription &);
bool ValidateParameterSelection(const Document &, const ParameterValueDescription &, const ParameterValue &);
bool PrepareParameterSelection(Document &, const ParameterValueDescription &, ParameterValue &);
} // namespace vdev
