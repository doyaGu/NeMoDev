#pragma once

#include <string>

#include "ui/app/EditorState.h"

namespace vdev {
struct PropertyBinding;
struct ParameterValue;

// Value of a parameter as UTF-8 text (CKParameter::GetStringValue).
std::string ParameterValueText(CKParameter *parameter);

uint32_t ParameterDraftEditor(EditorState &, CKParameter *, const char *label, float width, float height,
                              CK_CLASSID filterClass = 0, bool derived = true, const std::string &filter = {},
                              ParameterValue *remembered = nullptr, const PropertyBinding *property = nullptr,
                              const EditorValueEditors *editors = nullptr, unsigned representation = 0,
                              ParameterValue *beforeInteraction = nullptr, EditorDrawMembers drawMembers = nullptr, void *membersData = nullptr);

} // namespace vdev
