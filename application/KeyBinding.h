#pragma once
#include <string>
namespace vdev {
struct EditorKey {
    int key = 0;            // ASCII letters/digits; 256 + function-key number; named keys from 512.
    unsigned modifiers = 0; // Control, Alt, Shift, Super.
    bool operator==(const EditorKey &other) const { return key == other.key && modifiers == other.modifiers; }
};
bool ReadEditorKey(const std::string &text, EditorKey &key);
} // namespace vdev
