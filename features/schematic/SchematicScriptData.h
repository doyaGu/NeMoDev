#pragma once
#include "application/EditorApplication.h"

namespace vdev {
struct SchematicBreakpoint {
    ObjectRef script{}, object{}, owner{}, focus{};
    uint32_t type = 0;
    int index = -1; // ExtraData record at this Script revision; -1 for a new breakpoint.
    uint64_t revision = 0, topology = 0;
    std::string label;
};
struct BreakpointCondition {
    ObjectRef parameter{}, reference{};
    bool missing = false;
    int comparison = 0, logical = 0;
    std::string value;
};
struct BreakpointParameter {
    ObjectRef object{};
    std::string name;
    bool ordered = false;
};
inline std::vector<EditorField> BreakpointFields(const SchematicBreakpoint &b) {
    return {{"target", EDITOR_OBJECT, b.script},
            {"object", EDITOR_OBJECT, b.object},
            {"owner", EDITOR_OBJECT, b.owner},
            {"type", EDITOR_INTEGER_VALUE, {}, {}, b.type},
            {"index", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(b.index))},
            {"revision", EDITOR_INTEGER_VALUE, {}, {}, b.revision},
            {"topology", EDITOR_INTEGER_VALUE, {}, {}, b.topology}};
}
inline bool ReadBreakpoint(const EditorField &field, SchematicBreakpoint &out) {
    if (field.type != EDITOR_RECORD)
        return false;
    for (size_t i = 0; i < field.fields.size(); ++i)
        for (size_t j = 0; j < i; ++j)
            if (field.fields[i].key == field.fields[j].key)
                return false;
    const auto find = [&](const char *key) -> const EditorField * {
        for (const auto &v : field.fields)
            if (v.key == key)
                return &v;
        return nullptr;
    };
    const auto *script = find("target"), *object = find("object"), *owner = find("owner"), *type = find("type"),
               *revision = find("revision"), *topology = find("topology"), *index = find("index");
    if (!script || !object || !owner || !type || !revision || !topology || !index ||
        index->type != EDITOR_INTEGER_VALUE || (index->integer != UINT64_MAX && index->integer > INT32_MAX) ||
        script->type != EDITOR_OBJECT || object->type != EDITOR_OBJECT || owner->type != EDITOR_OBJECT ||
        type->type != EDITOR_INTEGER_VALUE || revision->type != EDITOR_INTEGER_VALUE ||
        topology->type != EDITOR_INTEGER_VALUE || type->integer < 1 || type->integer > 3 || !script->object ||
        (!object->object && index->integer == UINT64_MAX))
        return false;
    SchematicBreakpoint b;
    b.script = script->object;
    b.object = object->object;
    b.owner = owner->object;
    b.type = uint32_t(type->integer);
    b.index = index->integer == UINT64_MAX ? -1 : int(index->integer);
    b.revision = revision->integer;
    b.topology = topology->integer;
    if (const auto *label = find("label")) {
        if (label->type != EDITOR_TEXT_VALUE)
            return false;
        b.label = label->text;
    }
    if (const auto *focus = find("focus-target")) {
        if (focus->type != EDITOR_OBJECT)
            return false;
        b.focus = focus->object;
    }
    out = std::move(b);
    return true;
}
inline EditorField BreakpointField(const SchematicBreakpoint &b, const char *key = "breakpoint") {
    EditorField field{key, EDITOR_RECORD};
    field.fields = BreakpointFields(b);
    return field;
}
} // namespace vdev
