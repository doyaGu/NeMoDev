#pragma once

#include <string>
#include "CKAll.h"

namespace vdev {

// ------------------------------------------------------- Behavior Information
// A behavior file's description (Dev's Behavior Information, Dev.exe
// sub_581A40, sub_581CB0, sub_581D30): beside the .nms, its name without
// the last 3 letters and "dsc"; the class the behavior applies to (int32)
// and the version (float, -1 without one), then the behavior's name and
// the author each on a line, the description the rest of the file (Dev
// writes a newline after it and drops the last byte when reading; its
// lines end with "\r\n", here with "\n"). Texts ANSI.
struct BehaviorDescription {
    CK_CLASSID cid = 0;
    float version = -1.0f;
    std::string name, author, description;
};
std::string BehaviorDescriptionPath(const std::string &nms);
bool ReadBehaviorDescription(const std::string &nms, BehaviorDescription &description);
bool WriteBehaviorDescription(const std::string &nms, const BehaviorDescription &description);

} // namespace vdev
