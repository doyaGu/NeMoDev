#pragma once

#include <string>

#include "ui/app/EditorState.h"

namespace vdev {

// State of the value widget being edited. Drag and text widgets commit one
// undoable edit when they are deactivated, not on every change.
struct ValueEditState {
    CK_ID editing = 0;
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

// A Copy / Delete Dependencies value (CKPGUID_COPYDEPENDENCIES ...; Dev's
// Copy Options of Duplicate and Paste, Dev.exe sub_5BDD60): the mode (Custom,
// None, Full) and, in Custom, each class's own options (CKClassDesc
// DependsFct for mode). True when changed.
bool DependenciesEditor(CKDependencies &dependencies, CK_DEPENDENCIES_OPMODE mode, float height);

} // namespace vdev
