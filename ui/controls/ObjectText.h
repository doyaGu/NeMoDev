#pragma once

// The texts the windows show for objects and parameter values, in UTF-8
// (CK's are in the system code page).

#include <string>

#include "CKAll.h"

#include "core/runtime/TextEncoding.h"

namespace vdev {

// An object's name: none without an object, "<Unnamed>" without a name.
inline std::string DisplayName(CKObject *object, const char *none, const char *unnamed = "<Unnamed>") {
    if (!object) return none;
    return object->GetName() && *object->GetName() ? AnsiToUtf8(object->GetName()) : std::string(unnamed);
}

// A parameter's value as its type writes it, UTF-8.
std::string ParameterValueText(CKParameter *parameter); // ValueEditor.cpp

} // namespace vdev
