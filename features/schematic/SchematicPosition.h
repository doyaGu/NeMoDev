#pragma once
#include "application/EditorApplication.h"
#include "core/schematic/ElementAddress.h"
#include <climits>

namespace vdev {
inline bool ReadSchematicPosition(const EditorValue &value, ElementAddress &address) {
    if (value.type != EDITOR_RECORD)
        return false;
    address = {};
    EditorRequest request{};
    request.values = value.fields.data;
    request.count = value.fields.count;
    for (uint32_t i = 0; i < request.count; ++i) {
        const auto &field = request.values[i];
        const std::string key(field.key.data, field.key.size);
        if (key == "target" || key == "owner" || key == "object" || key == "endpoint") {
            if (field.type != EDITOR_OBJECT)
                return false;
            if (key == "target")
                address.script = field.object;
            if (key == "owner")
                address.owner = field.object;
            if (key == "object")
                address.object = field.object;
            if (key == "endpoint")
                address.endpoint = field.object;
        } else {
            if (field.type != EDITOR_INTEGER_VALUE)
                return false;
            const auto v = field.integer;
            if (key == "kind") {
                if (v > uint64_t(ElementKind::Start))
                    return false;
                address.element.kind = ElementKind(v);
            } else if (key == "pin") {
                if (v > uint64_t(PinKind::Target))
                    return false;
                address.element.pin = PinKind(v);
            } else if (key == "index" || key == "sub") {
                if (v != UINT64_MAX && v > INT_MAX)
                    return false;
                const auto index = v == UINT64_MAX ? -1 : int(v);
                if (key == "index")
                    address.element.index = index;
                else
                    address.element.sub = index;
            } else if (key == "revision")
                address.revision = v;
            else if (key == "topology")
                address.topology = v;
            else
                return false;
        }
    }
    address.element.owner = address.owner.object;
    address.element.object = address.object.object;
    return address.script && address.element.IsValid();
}

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
