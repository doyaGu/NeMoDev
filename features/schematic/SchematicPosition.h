#pragma once
#include "application/EditorApplication.h"
#include "core/schematic/ElementAddress.h"

namespace vdev {
inline EditorField SchematicPosition(const ElementAddress &address, const char *key = "address", ObjectRef focus = {}) {
    EditorField result{key, EDITOR_RECORD};
    result.fields = {{"target", EDITOR_OBJECT, address.script}, {"owner", EDITOR_OBJECT, address.owner},
        {"object", EDITOR_OBJECT, address.object}, {"endpoint", EDITOR_OBJECT, address.endpoint},
        {"kind", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(address.element.kind)},
        {"index", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(address.element.index))},
        {"sub", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(address.element.sub))},
        {"pin", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(address.element.pin)},
        {"revision", EDITOR_INTEGER_VALUE, {}, {}, address.revision}, {"topology", EDITOR_INTEGER_VALUE, {}, {}, address.topology}};
    if (focus) result.fields.push_back({"focus-target", EDITOR_OBJECT, focus});
    // Keep null optional identities out of navigation and modification arguments.
    for (auto it = result.fields.begin(); it != result.fields.end();)
        if (it->type == EDITOR_OBJECT && !it->object) it = result.fields.erase(it); else ++it;
    return result;
}
}
