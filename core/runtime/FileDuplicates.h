#pragma once

#include <string>

#include "CKAll.h"

namespace vdev {

// Dev's Duplicate Names Found (Dev.exe sub_59B3A0, CK2 CKFile::
// CheckDuplicateNames): an object of a Virtools file named as an object of
// its class in the context, and what loading it does: Ignore loads it with
// the same name, Rename adds a number to its name, Use Current uses the
// existing object without loading its data. LoadFileChoosingDuplicates
// (SdkCompat) applies these choices through the SDK's load callback.
enum class DuplicateAction { Ignore, Rename, UseCurrent };
struct DuplicateName {
    int fileIndex = -1; // the object's index in the file (CKFile::m_FileObjects)
    CK_CLASSID cid = 0;
    std::string name; // ANSI
    DuplicateAction action = DuplicateAction::Ignore;
};
} // namespace vdev
