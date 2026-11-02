#pragma once

#include "editor/Scene.h"
#include <algorithm>
#include <cstring>
#include <set>
#include <vector>

namespace vdev {
// The first reference is the grabbed row. The remaining references belong to
// its source selection, copied at drag start; the receiver never supplies them.
inline std::vector<unsigned char> WriteObjectDrag(ObjectRef source, const std::vector<ObjectRef> &selection) {
    if (!source || selection.size() > 65536)
        return {};
    std::vector<ObjectRef> objects{source};
    if (std::find(selection.begin(), selection.end(), source) != selection.end()) {
        std::set<ObjectRef> included{source};
        for (const auto object : selection) {
            if (!object || object.document != source.document)
                return {};
            if (included.insert(object).second)
                objects.push_back(object);
        }
    }
    std::vector<unsigned char> bytes(objects.size() * sizeof(ObjectRef));
    std::memcpy(bytes.data(), objects.data(), bytes.size());
    return bytes;
}

inline std::vector<ObjectRef> ReadObjectDrag(const void *data, size_t size) {
    if (!data || !size || size % sizeof(ObjectRef) || size / sizeof(ObjectRef) > 65536)
        return {};
    std::vector<ObjectRef> objects(size / sizeof(ObjectRef));
    std::memcpy(objects.data(), data, size);
    std::set<ObjectRef> included;
    for (const auto object : objects)
        if (!object || object.document != objects.front().document || !included.insert(object).second)
            return {};
    return objects;
}

// A single-object operation still uses the grabbed row, even for a multi-drag.
inline ObjectRef ObjectDragSource(const void *data, size_t size) {
    const auto objects = ReadObjectDrag(data, size);
    return objects.empty() ? ObjectRef{} : objects.front();
}
} // namespace vdev
