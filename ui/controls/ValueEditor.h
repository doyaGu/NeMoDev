#pragma once

#include <string>

#include "ui/controls/ParameterCopies.h"

namespace vdev {
struct PropertyBinding;
struct ParameterValue;

uint32_t ParameterDraftEditor(ParameterCopies &, ObjectRef, const char *label, float width, float height,
                              int32_t filterClass = 0, bool derived = true, const std::string &filter = {},
                              ParameterValue *remembered = nullptr, const PropertyBinding *property = nullptr,
                              const EditorValueEditors *editors = nullptr, unsigned representation = 0,
                              ParameterValue *beforeInteraction = nullptr, EditorDrawMembers drawMembers = nullptr, void *membersData = nullptr);

} // namespace vdev
