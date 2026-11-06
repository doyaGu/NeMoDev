#include "application/detail/EditorValidation.h"

namespace vdev::detail {
namespace {
constexpr uint32_t Limit = 1024 * 1024;
}
bool ValueType(EditorValueType type) {
    return type >= EDITOR_OBJECT && type <= EDITOR_RECORD;
}
std::string String(EditorString value) {
    if (value.size > Limit || (value.size && !value.data)) throw std::invalid_argument("Invalid text span");
    std::string result(value.data ? value.data : "", value.size);
    if (result.find('\0') != std::string::npos) throw std::invalid_argument("Embedded zero in text");
    // Strict UTF-8: no overlong encodings, surrogate values or partial scalars.
    for (size_t i = 0; i < result.size();) {
        const auto c = static_cast<unsigned char>(result[i++]);
        if (c < 128) continue;
        int n = c >= 0xC2 && c <= 0xDF ? 1 : c >= 0xE0 && c <= 0xEF ? 2 : c >= 0xF0 && c <= 0xF4 ? 3 : -1;
        if (n < 0 || i + n > result.size()) throw std::invalid_argument("Invalid UTF-8");
        const auto first = static_cast<unsigned char>(result[i]);
        if ((c == 0xE0 && first < 0xA0) || (c == 0xED && first >= 0xA0) || (c == 0xF0 && first < 0x90) ||
            (c == 0xF4 && first >= 0x90))
            throw std::invalid_argument("Invalid UTF-8");
        while (n--)
            if ((static_cast<unsigned char>(result[i++]) & 0xC0) != 0x80) throw std::invalid_argument("Invalid UTF-8");
    }
    return result;
}
std::string_view Identifier(EditorString value) {
    if (!value.data || !value.size || value.size > 256) throw std::invalid_argument("Expected a namespaced identifier");
    const std::string_view id(value.data, value.size);
    if (id.find('.') == std::string_view::npos ||
        id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") !=
            std::string_view::npos)
        throw std::invalid_argument("Expected a namespaced identifier");
    return id;
}
std::string Id(EditorString value) {
    return std::string(Identifier(value));
}
bool SameText(const std::string &text, EditorString registered) {
    // Registration has already copied and validated descriptor text.
    return text.size() == registered.size &&
           text.compare(0, text.size(), registered.data ? registered.data : "", registered.size) == 0;
}
} // namespace vdev::detail
