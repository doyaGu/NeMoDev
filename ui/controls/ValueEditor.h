#pragma once

#include <string>

#include "ui/app/EditorState.h"

namespace vdev {
struct PropertyBinding;
struct ParameterValue;

// State of the value widget being edited. Drag and text widgets commit one
// undoable edit when they are deactivated, not on every change.
struct ValueEditState {
    CK_ID editing = 0;
    EditorEdit draft = 0;
    EditorView owner = 0;
    std::string text;
    int integer = 0;
    float real = 0.0f;
};

// Value of a parameter as UTF-8 text (CKParameter::GetStringValue).
std::string ParameterValueText(CKParameter *parameter);

// Draws the value of parameter in the current ImGui item width: read-only
// text, or an editor (bool, int, float, enum and flags widgets; the type's
// own string conversion otherwise) committing through ValueCommands.
void ParameterValueEditor(EditorState &state, ValueEditState &edit, CK_ID script, CKParameter *parameter,
                          bool editable);

void CancelParameterValueEdit(EditorState &, ValueEditState &);
uint32_t ParameterDraftEditor(EditorState &, CKParameter *, const char *label, float width, float height,
                              CK_CLASSID filterClass = 0, bool derived = true, const std::string &filter = {},
                              ParameterValue *remembered = nullptr, const PropertyBinding *property = nullptr,
                              const EditorValueEditors *editors = nullptr, unsigned representation = 0,
                              ParameterValue *beforeInteraction = nullptr, EditorDrawMembers drawMembers = nullptr, void *membersData = nullptr);

} // namespace vdev
