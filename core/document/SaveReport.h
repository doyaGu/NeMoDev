#pragma once

#include <string>
#include <vector>

namespace vdev {
struct SaveReport {
    // True only after the verified composition was published at the requested path.
    bool saved = false;
    // A normal save also confirmed that the current document still matches
    // what was written, and established its save point. False for exports.
    bool documentSaved = false;
    int chunksWritten = 0;
    int scriptsVerified = 0;
    int parametersVerified = 0;
    int spriteTextsVerified = 0;
    std::vector<std::string> errors;
    std::vector<std::string> notes;
};
} // namespace vdev
