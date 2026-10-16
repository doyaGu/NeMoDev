#pragma once

#include <string>

#include "CKAll.h"
#include "core/runtime/SdkVersion.h"

namespace vdev {

// Dev's Duplicate Names Found (Dev.exe sub_59B3A0, CK2 CKFile::
// CheckDuplicateNames): an object of a Virtools file named as an object of
// its class in the context, and what loading it does (Dev.exe sub_59BCA0
// sets the file object's options; CK2 FinishLoading creates it with
// CK_OBJECTCREATION_RENAME, REPLACE or USECURRENT): Ignore loads it with the
// same name, Rename adds a number to its name, Use Current uses the existing
// object instead (its data is not loaded), Replace loads the file's data
// into the existing object. LoadFileChoosingDuplicates (SdkCompat) loads with
// the choices.
enum class DuplicateAction { Ignore, Rename, UseCurrent, Replace };
struct DuplicateName {
    int fileIndex = -1; // the object's index in the file (CKFile::m_FileObjects)
    CK_CLASSID cid = 0;
    std::string name; // ANSI
    DuplicateAction action = DuplicateAction::Ignore;
};
// Replace needs CK2 2.5 (the file object's CK_FO_REPLACEOBJECT): the 2.1
// runtime loads a file's objects without their options and creates a new
// object for a duplicate it is told to replace (CKContext::CreateObject); its
// choices are Ignore, Rename and Use Current, as Dev 2.1's rename dialog.
constexpr bool DUPLICATE_REPLACE_AVAILABLE = SDK_25;

} // namespace vdev
