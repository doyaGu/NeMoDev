#include "application/KeyBinding.h"
#include <array>
namespace vdev {
bool ReadEditorKey(const std::string &text, EditorKey &key) {
    EditorKey result;
    size_t begin = 0, end = 0;
    while ((end = text.find('+', begin)) != std::string::npos) {
        const auto modifier = text.substr(begin, end - begin);
        const unsigned flag = modifier == "Ctrl"    ? 1u
                              : modifier == "Alt"   ? 2u
                              : modifier == "Shift" ? 4u
                              : modifier == "Super" ? 8u
                                                    : 0u;
        if (!flag || (result.modifiers & flag))
            return false;
        result.modifiers |= flag;
        begin = end + 1;
    }
    const auto name = text.substr(begin);
    if (name.size() == 1 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9')))
        result.key = name[0];
    else if (name.size() >= 2 && name.size() <= 3 && name[0] == 'F') {
        int number = 0;
        for (size_t i = 1; i < name.size(); ++i) {
            if (name[i] < '0' || name[i] > '9')
                return false;
            number = number * 10 + name[i] - '0';
        }
        if (number < 1 || number > 24 || name[1] == '0')
            return false;
        result.key = 256 + number;
    } else {
        const std::array<const char *, 23> names = {
            "Space",  "Enter",       "Tab",       "Escape",         "Delete",    "Insert",     "Home",       "End",
            "PageUp", "PageDown",    "Up",        "Down",           "Left",      "Right",      "Backspace",  "Equal",
            "Minus",  "KeypadEnter", "KeypadAdd", "KeypadSubtract", "MouseLeft", "MouseRight", "MouseMiddle"};
        for (size_t i = 0; i < names.size(); ++i)
            if (name == names[i])
                result.key = 512 + static_cast<int>(i);
    }
    if (!result.key)
        return false;
    key = result;
    return true;
}
} // namespace vdev
