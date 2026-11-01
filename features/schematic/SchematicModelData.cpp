#include "features/schematic/SchematicModelData.h"
#include <cmath>
#include <limits>
#include <type_traits>
namespace vdev {
namespace {
template <class A, class T> void Fields(A &, T &);
template <class A> void Fields(A &a, Point &v) {
    a("h", v.h);
    a("v", v.v);
}
template <class A> void Fields(A &a, Rect &v) {
    a("hPos", v.hPos);
    a("vPos", v.vPos);
    a("hSize", v.hSize);
    a("vSize", v.vSize);
}
template <class A> void Fields(A &a, ElementRef &v) {
    a("kind", v.kind);
    a("owner", v.owner);
    a("object", v.object);
    a("index", v.index);
    a("sub", v.sub);
    a("pin", v.pin);
}
template <class A> void Fields(A &a, LinkEndpoint &v) {
    a("id", v.id);
    a("index", v.index);
    a("type", v.type);
    a("hasLegacyTypeCode", v.hasLegacyTypeCode);
}
template <class A> void Fields(A &a, ScriptItem &v) {
    a("ref", v.ref);
    a("id", v.id);
    a("name", v.name);
    a("ownerName", v.ownerName);
    a("ownerKind", v.ownerKind);
    a("isScript", v.isScript);
    a("open", v.open);
    a("hidden", v.hidden);
    a("initialConditions", v.initialConditions);
    a("color", v.color);
    a("priority", v.priority);
    a("header", v.header);
    a("bounds", v.bounds);
    a("contentHeight", v.contentHeight);
    a("snapshot", v.snapshot);
}
template <class A> void Fields(A &a, BehaviorItem &v) {
    a("ref", v.ref);
    a("id", v.id);
    a("parent", v.parent);
    a("depth", v.depth);
    a("name", v.name);
    a("prototype", v.prototype);
    a("graph", v.graph);
    a("expandable", v.expandable);
    a("folded", v.folded);
    a("buildingBlock", v.buildingBlock);
    a("usingTarget", v.usingTarget);
    a("missingRuntime", v.missingRuntime);
    a("priority", v.priority);
    a("settings", v.settings);
    a("variable", v.variable);
    a("customDialog", v.customDialog);
    a("sender", v.sender);
    a("receiver", v.receiver);
    a("prototypeMissing", v.prototypeMissing);
    a("obsolete", v.obsolete);
    a("outdated", v.outdated);
    a("breakpoint", v.breakpoint);
    a("box", v.box);
}
template <class A> void Fields(A &a, PinItem &v) {
    a("ref", v.ref);
    a("owner", v.owner);
    a("kind", v.kind);
    a("index", v.index);
    a("endpoint", v.endpoint);
    a("center", v.center);
    a("name", v.name);
    a("typeName", v.typeName);
    a("onOperation", v.onOperation);
    a("onScript", v.onScript);
}
template <class A> void Fields(A &a, LinkItem &v) {
    a("ref", v.ref);
    a("owner", v.owner);
    a("slot", v.slot);
    a("behaviorLink", v.behaviorLink);
    a("highlight", v.highlight);
    a("operationLink", v.operationLink);
    a("resolved", v.resolved);
    a("hidden", v.hidden);
    a("conversion", v.conversion);
    a("fromDirectLocal", v.fromDirectLocal);
    a("delay", v.delay);
    a("breakpoint", v.breakpoint);
    a("start", v.start);
    a("end", v.end);
    a("path", v.path);
}
template <class A> void Fields(A &a, OperationItem &v) {
    a("ref", v.ref);
    a("owner", v.owner);
    a("id", v.id);
    a("name", v.name);
    a("bounds", v.bounds);
}
template <class A> void Fields(A &a, ParamItem &v) {
    a("ref", v.ref);
    a("owner", v.owner);
    a("slot", v.slot);
    a("shortcut", v.shortcut);
    a("shortcutSource", v.shortcutSource);
    a("hidden", v.hidden);
    a("directInput", v.directInput);
    a("parameter", v.parameter);
    a("style", v.style);
    a("breakpoint", v.breakpoint);
    a("name", v.name);
    a("value", v.value);
    a("typeName", v.typeName);
    a("bounds", v.bounds);
    a("display", v.display);
    a("center", v.center);
}
template <class A> void Fields(A &a, CommentItem &v) {
    a("ref", v.ref);
    a("owner", v.owner);
    a("slot", v.slot);
    a("text", v.text);
    a("collapsed", v.collapsed);
    a("bounds", v.bounds);
}
template <class A> void Fields(A &a, StartItem &v) {
    a("ref", v.ref);
    a("endpoint", v.endpoint);
    a("present", v.present);
    a("bounds", v.bounds);
    a("center", v.center);
}
template <class T> struct IsVector : std::false_type {};
template <class T> struct IsVector<std::vector<T>> : std::true_type {};
struct Output {
    std::vector<EditorField> fields;
    template <class T> void operator()(const char *key, const T &v) { fields.push_back(Make(key, v)); }
    template <class T> static EditorField Make(const std::string &key, const T &v) {
        EditorField f;
        f.key = key;
        if constexpr (std::is_same_v<T, std::string>) {
            f.type = EDITOR_TEXT_VALUE;
            f.text = v;
        } else if constexpr (std::is_floating_point_v<T>) {
            f.type = EDITOR_REAL_VALUE;
            f.real = v;
        } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
            f.type = EDITOR_INTEGER_VALUE;
            f.integer = uint64_t(v);
        } else if constexpr (IsVector<T>::value) {
            f.type = EDITOR_RECORD;
            for (size_t i = 0; i < v.size(); ++i)
                f.fields.push_back(Make(std::to_string(i), v[i]));
        } else if constexpr (std::is_same_v<T, std::shared_ptr<const SchematicSnapshot>>) {
            f.type = EDITOR_RECORD;
            if (v) {
                f.fields = {Make("width", v->width), Make("height", v->height)};
            }
        } else {
            Output a;
            Fields(a, const_cast<T &>(v));
            f.type = EDITOR_RECORD;
            f.fields = std::move(a.fields);
        }
        return f;
    }
};
struct Input {
    const std::vector<EditorField> &fields;
    bool valid = true;
    template <class T> void operator()(const char *key, T &v) {
        const EditorField *found = nullptr;
        for (const auto &f : fields)
            if (f.key == key) {
                if (found) {
                    valid = false;
                    return;
                }
                found = &f;
            }
        valid = valid && found && Read(*found, v);
    }
    template <class T> static bool Read(const EditorField &f, T &v) {
        if constexpr (std::is_same_v<T, std::string>) {
            if (f.type != EDITOR_TEXT_VALUE)
                return false;
            v = f.text;
        } else if constexpr (std::is_floating_point_v<T>) {
            if (f.type != EDITOR_REAL_VALUE || !std::isfinite(f.real) ||
                std::abs(f.real) > std::numeric_limits<T>::max())
                return false;
            v = T(f.real);
        } else if constexpr (std::is_same_v<T, bool>) {
            if (f.type != EDITOR_INTEGER_VALUE || f.integer > 1)
                return false;
            v = bool(f.integer);
        } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
            if (f.type != EDITOR_INTEGER_VALUE)
                return false;
            if constexpr (std::is_enum_v<T> || std::is_signed_v<T>) {
                const auto n = int64_t(f.integer);
                if (n < INT32_MIN || n > INT32_MAX)
                    return false;
            } else if (f.integer > std::numeric_limits<T>::max())
                return false;
            v = T(f.integer);
        } else if constexpr (IsVector<T>::value) {
            if (f.type != EDITOR_RECORD || f.fields.size() > 1000000)
                return false;
            v.clear();
            for (const auto &item : f.fields) {
                typename T::value_type t{};
                if (!Read(item, t))
                    return false;
                v.push_back(std::move(t));
            }
        } else if constexpr (std::is_same_v<T, std::shared_ptr<const SchematicSnapshot>>) {
            if (f.type != EDITOR_RECORD)
                return false;
            v.reset();
            if (f.fields.empty())
                return true;
            auto picture = std::make_shared<SchematicSnapshot>();
            Input a{f.fields};
            a("width", picture->width);
            a("height", picture->height);
            if (!a.valid || !picture->width || !picture->height || picture->width > 8192 || picture->height > 8192)
                return false;
            v = std::move(picture);
        } else {
            if (f.type != EDITOR_RECORD)
                return false;
            Input a{f.fields};
            Fields(a, v);
            return a.valid;
        }
        return true;
    }
};
} // namespace
size_t SchematicModelSize(const SchematicModel &m) {
    return m.behaviors.size() + m.pins.size() + m.links.size() + m.operations.size() + m.params.size() +
           m.comments.size();
}
EditorField SchematicModelFields(const SchematicModel &m, size_t first, size_t count) {
    Output out;
    out("script", m.script);
    out("start", m.start);
    out("bounds", m.contentBounds);
    out("unresolved", m.unresolvedEndpoints);
    EditorField items{"items", EDITOR_RECORD};
    size_t index = 0;
    const auto append = [&](const auto &values, unsigned category) {
        for (const auto &value : values) {
            if (index >= first && index - first < count) {
                EditorField item{std::to_string(index), EDITOR_RECORD};
                item.fields = {Output::Make("category", category), Output::Make("value", value)};
                items.fields.push_back(std::move(item));
            }
            ++index;
        }
    };
    append(m.behaviors, 1);
    append(m.pins, 2);
    append(m.links, 3);
    append(m.operations, 4);
    append(m.params, 5);
    append(m.comments, 6);
    out.fields.push_back(std::move(items));
    EditorField result{"model", EDITOR_RECORD};
    result.fields = std::move(out.fields);
    return result;
}
bool ReadSchematicModel(const EditorField &field, SchematicModel &model) {
    if (field.type != EDITOR_RECORD)
        return false;
    SchematicModel next;
    Input in{field.fields};
    in("script", next.script);
    in("start", next.start);
    in("bounds", next.contentBounds);
    in("unresolved", next.unresolvedEndpoints);
    const EditorField *items = nullptr;
    for (const auto &f : field.fields)
        if (f.key == "items") {
            if (items)
                return false;
            items = &f;
        }
    if (!in.valid || !items || items->type != EDITOR_RECORD || next.script.ownerKind < SchematicOwnerKind::Object ||
        next.script.ownerKind > SchematicOwnerKind::Material)
        return false;
    const auto append = [&](auto &list, const EditorField &v) {
        typename std::decay_t<decltype(list)>::value_type item{};
        if (!Input::Read(v, item))
            return false;
        list.push_back(std::move(item));
        return true;
    };
    for (const auto &entry : items->fields) {
        if (entry.type != EDITOR_RECORD)
            return false;
        unsigned category = 0;
        Input a{entry.fields};
        a("category", category);
        const EditorField *v = nullptr;
        for (const auto &f : entry.fields)
            if (f.key == "value") {
                if (v)
                    return false;
                v = &f;
            }
        if (!a.valid || !v)
            return false;
        const bool ok = category == 1   ? append(next.behaviors, *v)
                        : category == 2 ? append(next.pins, *v)
                        : category == 3 ? append(next.links, *v)
                        : category == 4 ? append(next.operations, *v)
                        : category == 5 ? append(next.params, *v)
                        : category == 6 ? append(next.comments, *v)
                                        : false;
        if (!ok)
            return false;
    }
    next.Reindex();
    model = std::move(next);
    return true;
}
SchematicActivity ReadSchematicActivity(const EditorResponse &execution, const EditorResponse &activity,
                                        const ElementAddress &script,
                                        const std::map<ElementRef, ElementAddress> &positions) {
    SchematicActivity result;
    if (!script || execution.status != EDITOR_OK || activity.status != EDITOR_OK)
        return result;
    const auto find = [](const EditorResponse &r, const char *key, EditorValueType type) -> const EditorField * {
        const EditorField *found = nullptr;
        for (const auto &f : r.fields)
            if (f.key == key) {
                if (found || f.type != type)
                    return nullptr;
                found = &f;
            }
        return found;
    };
    const auto *state = find(execution, "state", EDITOR_INTEGER_VALUE),
               *trace = find(execution, "trace", EDITOR_INTEGER_VALUE),
               *current = find(execution, "behavior", EDITOR_OBJECT),
               *revision = find(activity, "revision", EDITOR_INTEGER_VALUE),
               *topology = find(activity, "topology", EDITOR_INTEGER_VALUE),
               *active = find(activity, "active", EDITOR_INTEGER_VALUE),
               *running = find(activity, "running", EDITOR_OBJECT_LIST),
               *firing = find(activity, "firing", EDITOR_OBJECT_LIST);
    if (!state || state->integer == EDITOR_EXECUTION_STOPPED || state->integer > EDITOR_EXECUTION_PAUSED || !trace ||
        trace->integer > 1 || !current || !revision || revision->integer != script.revision || !topology ||
        topology->integer != script.topology || !active || active->integer > 1 || !running || !firing)
        return result;
    result.trace = trace->integer != 0;
    result.active = active->integer != 0;
    const std::set<ObjectRef> runningObjects(running->objects.begin(), running->objects.end()),
        firingObjects(firing->objects.begin(), firing->objects.end());
    for (const auto &entry : positions) {
        const auto &a = entry.second;
        if (a.script != script.script || !a.object || a.revision != script.revision || a.topology != script.topology)
            continue;
        if (a.element.kind == ElementKind::Behavior) {
            if (runningObjects.count(a.object))
                result.running.insert(a.element);
            if (a.object == current->object)
                result.current = a.element;
        } else if (result.trace && a.element.kind == ElementKind::Link && firingObjects.count(a.object))
            result.firing.insert(a.element);
    }
    return result;
}
} // namespace vdev
