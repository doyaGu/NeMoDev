#pragma once

#include <string>

namespace vdev {
// UTF-8 fields shared by the resource description editor and file request view.
// This form neither reads objects nor writes files.
struct BehaviorInfoFields {
    std::string name, type, author, version, description;
    bool hide = false;
};
bool DrawBehaviorInfoFields(BehaviorInfoFields &fields, bool hideRepresentation);
} // namespace vdev
