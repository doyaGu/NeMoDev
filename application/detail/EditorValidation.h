#pragma once

#include "application/EditorApplication.h"
#include <stdexcept>
#include <string_view>

namespace vdev::detail {
bool ValueType(EditorValueType);
std::string String(EditorString);
std::string_view Identifier(EditorString);
std::string Id(EditorString);
bool SameText(const std::string &, EditorString);
template <class T> void Array(const T *data, uint32_t count) {
    if (count > 4096 || (count && !data)) throw std::invalid_argument("Invalid descriptor array");
}
template <class Predicate> bool EveryField(const std::vector<EditorField> &fields, const Predicate &test) {
    for (const auto &field : fields)
        if (!test(field) || (field.type == EDITOR_RECORD && !EveryField(field.fields, test))) return false;
    return true;
}
} // namespace vdev::detail
