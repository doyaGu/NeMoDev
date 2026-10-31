#include "features/scene/SceneCommands.h"
#include "features/scene/SceneQueries.h"
#include "core/level/LevelCommands.h"
#include "core/level/ObjectEditCommands.h"
#include "core/level/AttributeQueries.h"
#include "core/level/ObjectQueries.h"
#include "core/level/SceneCommands.h"
#include "core/level/HierarchyTree.h"
#include "core/catalog/ParameterCatalog.h"
#include "core/graph/ScriptCommands.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <deque>
#include <limits>
#include <stdexcept>

namespace vdev {
namespace {
const EditorValue *Arg(const EditorRequest &r, const char *key) {
    for (uint32_t i = 0; i < r.count; ++i) {
        if (std::string(r.values[i].key.data, r.values[i].key.size) == key) {
            return &r.values[i];
        }
    }
    return nullptr;
}
uint64_t Number(const EditorRequest &r, const char *key) {
    const auto *v = Arg(r, key);
    return v ? v->integer : 0;
}
std::string Text(const EditorRequest &r, const char *key) {
    const auto *v = Arg(r, key);
    return v ? std::string(v->text.data ? v->text.data : "", v->text.size) : std::string();
}
ObjectRef Ref(const EditorRequest &r, const char *key) {
    const auto *v = Arg(r, key);
    return v ? v->object : ObjectRef{};
}
std::vector<ObjectRef> Refs(const EditorRequest &r, const char *key) {
    const auto *v = Arg(r, key);
    if (!v || !v->objects.count) {
        return {};
    }
    return {v->objects.data, v->objects.data + v->objects.count};
}
EditorStatus Result(const EditResult &result, Document &doc, EditorSink sink) {
    const auto message = AnsiToUtf8(result.message);
    const EditorValue fields[] = {{EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, {}, EditorText(message)},
                                  {EDITOR_TEXT("created"), EDITOR_OBJECT, doc.Reference(result.created)}};
    if (sink.write) {
        sink.write(sink.data, fields, 2);
    }
    return result.ok ? EDITOR_OK : result.Unchanged() ? EDITOR_NO_CHANGE : EDITOR_REFUSED;
}
} // namespace
namespace {
struct SceneMovePlan {
    std::vector<CK_ID> scripts, objects;
    CK_ID owner = 0, container = 0;
    bool groups = false;
    std::string message;
};
SceneMovePlan PlanSceneMove(Document &doc, const std::vector<CK_ID> &objects, CK_ID owner, CK_ID container) {
    SceneMovePlan plan;
    plan.owner = owner;
    plan.container = container;
    auto *context = doc.Context();
    auto *target = context->GetObject(owner);
    if (!target || CKBehavior::Cast(target)) {
        return plan;
    }
    if (CKScene::Cast(target) || CKLevel::Cast(target) || CKGroup::Cast(target) || CKPlace::Cast(target)) {
        plan.container = owner;
    }
    auto *destination = context->GetObject(plan.container);
    for (auto id : objects) {
        auto *object = context->GetObject(id);
        if (!object) {
            continue;
        }
        if (CKBehavior::Cast(object)) {
            plan.scripts.push_back(id);
            continue;
        }
        if (auto *scene = CKScene::Cast(destination)) {
            if (auto *entity = CK3dEntity::Cast(object);
                entity && entity->GetReferencePlace() && !CKPlace::Cast(object)) {
                plan.message = "Cannot add Object to scene, it belongs to a place !";
                break;
            }
            if (auto *so = CKSceneObject::Cast(object); so && !so->IsInScene(scene)) {
                plan.objects.push_back(id);
                if (auto *group = CKGroup::Cast(object)) {
                    plan.groups = plan.groups || group->GetObjectCount() > 0;
                }
            }
        } else if (CKLevel::Cast(destination)) {
            if (auto *entity = CK3dEntity::Cast(object);
                entity && entity->GetReferencePlace() && !CKPlace::Cast(object)) {
                plan.objects.push_back(id);
            }
        } else if (auto *place = CKPlace::Cast(destination)) {
            if (auto *entity = CK3dEntity::Cast(object);
                entity && !CKPlace::Cast(object) && entity->GetReferencePlace() != place) {
                plan.objects.push_back(id);
            }
        } else if (auto *group = CKGroup::Cast(destination)) {
            if (auto *be = CKBeObject::Cast(object);
                be && be != group && !CKLevel::Cast(be) && !CKScene::Cast(be) && !be->IsInGroup(group)) {
                plan.objects.push_back(id);
            }
        }
    }
    return plan;
}
} // namespace
struct SceneCommands::Data {
    EditorApplication &editor;
    std::function<Document *()> current;
    ObjectClipboard &objectClipboard;
    Clipboard &clipboard;
    std::function<SchematicNavigation &()> navigation;
    std::function<CKDependencies &()> copyOptions;
    uint64_t revision = 1, published = 0, nextWorkset = 1, catalogEpoch = 1;
    DocumentToken document{};
    int runtimeFrame = -1, runtimeState = -1;
    bool dynamic = false;
    std::vector<SceneWorkset> worksets;
    struct TreeCache {
        SceneTreeOptions options;
        bool hierarchy;
        SceneTree tree;
    };
    std::deque<TreeCache> trees;
    enum Action {
        Visibility,
        Priority,
        RenderPriority,
        SceneFlag,
        Active,
        Initial,
        SendScene,
        SendGroup,
        SendPlace,
        RemovePlace,
        GroupMove,
        ScriptMove,
        Hierarchy,
        Launch,
        SaveOptions,
        Copy,
        Paste,
        Move,
        WorksetCreate,
        WorksetRename,
        WorksetDelete,
        WorksetAdd,
        WorksetRemove,
        WorksetShow,
        AttributeAdd,
        AttributeRemove,
        AttributeText,
        AttributeCopy,
        AttributePaste,
        CategoryCreate,
        CategoryRename,
        CategoryDelete,
        TypeCreate,
        TypeRename,
        TypeDelete,
        RemoveUsers,
        Duplicate,
        Export,
        AttributeAddSelected,
        CopyDrag
    };
    struct Command {
        Data *owner;
        Action action;
    };
    std::deque<Command> commands;
    Document *DocumentOf(const EditorRequest &request) const {
        auto *doc = current();
        return doc && doc->Token() == request.scope.document && doc->IsOpen() ? doc : nullptr;
    }
    static uint64_t Version(void *data, const EditorHost *) { return static_cast<Data *>(data)->revision; }
    static EditorStatus Tree(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Catalog(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Attributes(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Parameters(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus CopyOptions(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus MovePlan(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus DeletePlan(void *, const EditorHost *, const EditorRequest *, EditorSink);
    uint64_t CatalogVersion(Document &doc) const;
    static EditorStatus Run(void *, const EditorHost *, const EditorRequest *, EditorSink);
    std::vector<CK_ID> Ids(Document &doc, const std::vector<ObjectRef> &refs) const {
        std::vector<CK_ID> ids;
        for (auto ref : refs) {
            auto id = doc.NativeId(ref);
            if (!id) {
                throw std::invalid_argument("Stale scene object");
            }
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
                ids.push_back(id);
            }
        }
        return ids;
    }
};
SceneCommands::SceneCommands(EditorApplication &editor, std::function<Document *()> current, ObjectClipboard &objects,
                             Clipboard &clipboard, std::function<SchematicNavigation &()> navigation, std::function<CKDependencies &()> options)
    : m_Data(new Data{editor, std::move(current), objects, clipboard, std::move(navigation), std::move(options)}) {
}
SceneCommands::~SceneCommands() = default;
void SceneCommands::Publish(uint64_t published, int frame, int state, bool dynamic) {
    auto &d = *m_Data;
    auto *doc = d.current();
    const auto token = doc && doc->IsOpen() ? doc->Token() : DocumentToken{};
    if (token != d.document) {
        d.worksets.clear();
        d.nextWorkset = 1;
        ++d.catalogEpoch;
    }
    if (token != d.document || published != d.published || frame != d.runtimeFrame || state != d.runtimeState ||
        dynamic != d.dynamic) {
        d.document = token;
        d.published = published;
        d.runtimeFrame = frame;
        d.runtimeState = state;
        d.dynamic = dynamic;
        ++d.revision;
        d.trees.clear();
    }
}
void SceneCommands::Stop() {
    m_Data->trees.clear();
    m_Data->worksets.clear();
}
void SceneCommands::RefreshCatalog() {
    ++m_Data->catalogEpoch;
    ++m_Data->revision;
    m_Data->trees.clear();
}
EditorStatus SceneCommands::Data::Tree(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto &d = *static_cast<Data *>(data);
    auto *doc = d.DocumentOf(*request);
    if (!doc) {
        return EDITOR_STALE;
    }
    if (const auto ref = Ref(*request, "context"); ref && !doc->NativeId(ref)) {
        return EDITOR_STALE;
    }
    SceneTreeOptions options;
    options.priority = Number(*request, "priority") != 0;
    options.reversed = Number(*request, "reversed") != 0;
    options.dynamic = d.dynamic;
    options.context = Ref(*request, "context");
    const bool hierarchy = Number(*request, "hierarchy") != 0;
    const auto resources = Number(*request, "resources");
    options.meshes = (resources & 1) != 0;
    options.materials = (resources & 2) != 0;
    options.textures = (resources & 4) != 0;
    auto found = std::find_if(d.trees.begin(), d.trees.end(), [&](const TreeCache &c) {
        return c.hierarchy == hierarchy && c.options.priority == options.priority &&
               c.options.reversed == options.reversed && c.options.context == options.context &&
               c.options.meshes == options.meshes && c.options.materials == options.materials &&
               c.options.textures == options.textures;
    });
    if (found == d.trees.end()) {
        if (d.trees.size() >= 4) {
            d.trees.pop_front();
        }
        d.trees.push_back(
            {options, hierarchy,
             hierarchy ? ReadHierarchyTree(*doc, options) : ReadLevelTree(*doc, options, d.worksets, d.navigation().HiddenIds(*doc))});
        found = std::prev(d.trees.end());
    }
    const auto &tree = found->tree;
    const auto first = Number(*request, "first");
    if (first > tree.rows.size()) {
        return EDITOR_INVALID;
    }
    const auto end = std::min(tree.rows.size(), size_t(first) + 256);
    std::vector<uint64_t> keys, parents, kinds, types, cells, classes, priorities, zorders, icons, worksets, roots,
        saves, expanded;
    std::vector<ObjectRef> objects, scenes, sections, groups, scripts;
    std::vector<EditorString> names, owners;
    for (size_t i = size_t(first); i < end; ++i) {
        const auto &r = tree.rows[i];
        keys.push_back(r.key);
        parents.push_back(r.parent);
        kinds.push_back(r.kind);
        types.push_back(r.types);
        cells.push_back(r.cells);
        classes.push_back(r.cid);
        priorities.push_back(uint64_t(int64_t(r.priority)));
        zorders.push_back(uint64_t(int64_t(r.renderPriority)));
        icons.push_back(r.icon);
        worksets.push_back(r.workset);
        roots.push_back(uint64_t(int64_t(r.root)));
        saves.push_back(uint64_t(int64_t(r.saveOption)));
        expanded.push_back(r.expanded);
        objects.push_back(r.object);
        scenes.push_back(r.scene);
        sections.push_back(r.section);
        groups.push_back(r.group);
        scripts.push_back(r.script);
        names.push_back(EditorText(r.name));
        owners.push_back(EditorText(r.owner));
    }
    std::vector<EditorValue> fields;
    auto ints = [&](const char *key, const std::vector<uint64_t> &v) {
        fields.push_back({EditorLiteral(key), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {v.data(), uint32_t(v.size())}});
    };
    auto refs = [&](const char *key, const std::vector<ObjectRef> &v) {
        fields.push_back({EditorLiteral(key), EDITOR_OBJECT_LIST, {}, {}, 0, {v.data(), uint32_t(v.size())}});
    };
    ints("keys", keys);
    ints("parents", parents);
    ints("kinds", kinds);
    ints("types", types);
    ints("cells", cells);
    ints("classes", classes);
    ints("priorities", priorities);
    ints("render-priorities", zorders);
    ints("icons", icons);
    ints("worksets", worksets);
    ints("roots", roots);
    ints("save-options", saves);
    ints("expanded", expanded);
    refs("objects", objects);
    refs("scenes", scenes);
    refs("sections", sections);
    refs("groups", groups);
    refs("scripts", scripts);
    fields.push_back(
        {EDITOR_TEXT("names"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {names.data(), uint32_t(names.size())}});
    fields.push_back(
        {EDITOR_TEXT("owners"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {owners.data(), uint32_t(owners.size())}});
    fields.push_back({EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE, {}, {}, tree.rows.size()});
    fields.push_back({EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, d.revision});
    fields.push_back({EDITOR_TEXT("level"), EDITOR_OBJECT, tree.level});
    fields.push_back({EDITOR_TEXT("level-scene"), EDITOR_OBJECT, tree.levelScene});
    fields.push_back({EDITOR_TEXT("current-scene"), EDITOR_OBJECT, tree.currentScene});
    return sink.write ? sink.write(sink.data, fields.data(), uint32_t(fields.size())) : EDITOR_INVALID;
}
namespace {
uint64_t CatalogHash(const AttributeCatalog &catalog) {
    uint64_t hash = 14695981039346656037ULL;
    const auto text = [&](const std::string &value) {
        for (unsigned char c : value) {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
        hash ^= 0xff;
        hash *= 1099511628211ULL;
    };
    for (const auto &type : catalog.types) {
        text(std::to_string(type.first));
        text(type.second.name);
        text(type.second.parameterName);
        text(type.second.compatibleName);
        text(std::to_string(type.second.flags));
    }
    for (const auto &category : catalog.categories) {
        text(category.name);
        text(std::to_string(category.flags));
        for (auto type : category.types) {
            text(std::to_string(type));
        }
    }
    return hash;
}
uint64_t AttributeFlags(CKDWORD flags) {
    return ((flags & CK_ATTRIBUT_CAN_MODIFY) ? 1u : 0u) | ((flags & CK_ATTRIBUT_CAN_DELETE) ? 2u : 0u) |
           ((flags & CK_ATTRIBUT_USER) ? 4u : 0u);
}
} // namespace
EditorStatus SceneCommands::Data::Catalog(void *data, const EditorHost *, const EditorRequest *request,
                                          EditorSink sink) {
    auto &d = *static_cast<Data *>(data);
    auto *doc = d.DocumentOf(*request);
    if (!doc) {
        return EDITOR_STALE;
    }
    for (auto ref : Refs(*request, "objects")) {
        if (!doc->NativeId(ref)) {
            return EDITOR_STALE;
        }
    }
    const auto targets = d.Ids(*doc, Refs(*request, "objects"));
    const auto catalog = ReadAttributeCatalog(*doc, targets);
    std::vector<uint64_t> ids, flags, categoryFlags, parents;
    std::vector<EditorString> names, parameters, compatible, categories;
    for (const auto &category : catalog.categories) {
        categories.push_back(EditorText(category.name));
        categoryFlags.push_back(AttributeFlags(category.flags));
    }
    std::vector<std::string> utfNames, utfParameters, utfCompatible, utfCategories;
    for (const auto &entry : catalog.types) {
        const auto &type = entry.second;
        ids.push_back(type.type);
        flags.push_back(AttributeFlags(type.flags));
        utfNames.push_back(AnsiToUtf8(type.name));
        utfParameters.push_back(AnsiToUtf8(type.parameterName));
        utfCompatible.push_back(AnsiToUtf8(type.compatibleName));
        uint64_t parent = 0;
        for (size_t i = 0; i < catalog.categories.size(); ++i) {
            if (std::find(catalog.categories[i].types.begin(), catalog.categories[i].types.end(), type.type) !=
                catalog.categories[i].types.end()) {
                parent = i + 1;
                break;
            }
        }
        parents.push_back(parent);
    }
    for (const auto &category : catalog.categories) {
        utfCategories.push_back(AnsiToUtf8(category.name));
    }
    categories.clear();
    for (const auto &value : utfNames) {
        names.push_back(EditorText(value));
    }
    for (const auto &value : utfParameters) {
        parameters.push_back(EditorText(value));
    }
    for (const auto &value : utfCompatible) {
        compatible.push_back(EditorText(value));
    }
    for (const auto &value : utfCategories) {
        categories.push_back(EditorText(value));
    }
    const auto hash = d.CatalogVersion(*doc);
    const EditorValue fields[] = {
        {EDITOR_TEXT("types"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {ids.data(), uint32_t(ids.size())}},
        {EDITOR_TEXT("flags"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {flags.data(), uint32_t(flags.size())}},
        {EDITOR_TEXT("parents"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {parents.data(), uint32_t(parents.size())}},
        {EDITOR_TEXT("category-flags"),
         EDITOR_INTEGER_LIST,
         {},
         {},
         0,
         {},
         {categoryFlags.data(), uint32_t(categoryFlags.size())}},
        {EDITOR_TEXT("names"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {names.data(), uint32_t(names.size())}},
        {EDITOR_TEXT("parameters"),
         EDITOR_TEXT_LIST,
         {},
         {},
         0,
         {},
         {},
         {parameters.data(), uint32_t(parameters.size())}},
        {EDITOR_TEXT("compatible"),
         EDITOR_TEXT_LIST,
         {},
         {},
         0,
         {},
         {},
         {compatible.data(), uint32_t(compatible.size())}},
        {EDITOR_TEXT("categories"),
         EDITOR_TEXT_LIST,
         {},
         {},
         0,
         {},
         {},
         {categories.data(), uint32_t(categories.size())}},
        {EDITOR_TEXT("catalog"), EDITOR_INTEGER_VALUE, {}, {}, hash},
        {EDITOR_TEXT("available"), EDITOR_INTEGER_VALUE, {}, {}, catalog.available}};
    return sink.write ? sink.write(sink.data, fields, 10) : EDITOR_INVALID;
}
EditorStatus SceneCommands::Data::Attributes(void *data, const EditorHost *, const EditorRequest *request,
                                             EditorSink sink) {
    auto &d = *static_cast<Data *>(data);
    auto *doc = d.DocumentOf(*request);
    if (!doc) {
        return EDITOR_STALE;
    }
    const auto hash = d.CatalogVersion(*doc);
    if (Number(*request, "catalog") != hash) {
        return EDITOR_STALE;
    }
    std::vector<CKAttributeType> types;
    const auto *list = Arg(*request, "types");
    for (uint32_t i = 0; list && i < list->integers.count; ++i) {
        if (list->integers.data[i] > uint64_t(std::numeric_limits<int>::max())) {
            return EDITOR_INVALID;
        }
        types.push_back(int(list->integers.data[i]));
    }
    for (auto ref : Refs(*request, "objects")) {
        if (!doc->NativeId(ref)) {
            return EDITOR_STALE;
        }
    }
    const auto all = ReadAttributeObjects(*doc, types, d.Ids(*doc, Refs(*request, "objects")));
    const auto first = Number(*request, "first");
    if (first > all.size()) {
        return EDITOR_INVALID;
    }
    std::vector<ObjectRef> objects, parameters;
    std::vector<uint64_t> rowTypes;
    std::vector<EditorString> names, values, parameterNames;
    std::vector<std::string> utfNames, utfValues, utfParameters;
    const auto end = std::min(all.size(), size_t(first) + 256);
    for (size_t i = size_t(first); i < end; ++i) {
        const auto &r = all[i];
        objects.push_back(r.reference);
        parameters.push_back(r.parameterReference);
        rowTypes.push_back(r.type);
        utfNames.push_back(AnsiToUtf8(r.name));
        utfValues.push_back(AnsiToUtf8(r.value));
        utfParameters.push_back(AnsiToUtf8(r.parameterName));
    }
    for (const auto &v : utfNames) {
        names.push_back(EditorText(v));
    }
    for (const auto &v : utfValues) {
        values.push_back(EditorText(v));
    }
    for (const auto &v : utfParameters) {
        parameterNames.push_back(EditorText(v));
    }
    const EditorValue fields[] = {
        {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {objects.data(), uint32_t(objects.size())}},
        {EDITOR_TEXT("parameters"), EDITOR_OBJECT_LIST, {}, {}, 0, {parameters.data(), uint32_t(parameters.size())}},
        {EDITOR_TEXT("types"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {rowTypes.data(), uint32_t(rowTypes.size())}},
        {EDITOR_TEXT("names"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {names.data(), uint32_t(names.size())}},
        {EDITOR_TEXT("values"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {values.data(), uint32_t(values.size())}},
        {EDITOR_TEXT("parameter-names"),
         EDITOR_TEXT_LIST,
         {},
         {},
         0,
         {},
         {},
         {parameterNames.data(), uint32_t(parameterNames.size())}},
        {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE, {}, {}, all.size()},
        {EDITOR_TEXT("catalog"), EDITOR_INTEGER_VALUE, {}, {}, hash}};
    return sink.write ? sink.write(sink.data, fields, 8) : EDITOR_INVALID;
}
EditorStatus SceneCommands::Data::Parameters(void *data, const EditorHost *, const EditorRequest *request,
                                             EditorSink sink) {
    auto &d = *static_cast<Data *>(data);
    auto *doc = d.DocumentOf(*request);
    if (!doc) {
        return EDITOR_STALE;
    }
    auto types = ListParameterTypes(*doc);
    std::stable_sort(types.begin(), types.end(),
                     [](const auto &a, const auto &b) { return _stricmp(a.name.c_str(), b.name.c_str()) < 0; });
    std::vector<uint64_t> guids;
    std::vector<std::string> strings;
    std::vector<EditorString> names;
    for (const auto &type : types) {
        guids.push_back((uint64_t(type.guid.d1) << 32) | type.guid.d2);
        strings.push_back(AnsiToUtf8(type.name));
    }
    for (const auto &name : strings) {
        names.push_back(EditorText(name));
    }
    const EditorValue fields[] = {
        {EDITOR_TEXT("guids"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {guids.data(), uint32_t(guids.size())}},
        {EDITOR_TEXT("names"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {names.data(), uint32_t(names.size())}}};
    return sink.write ? sink.write(sink.data, fields, 2) : EDITOR_INVALID;
}
EditorStatus SceneCommands::Data::Run(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    const auto &command = *static_cast<Command *>(data);
    auto &d = *command.owner;
    auto *doc = d.DocumentOf(*request);
    if (!doc) {
        return EDITOR_STALE;
    }
    auto *context = doc->Context();
    for (auto ref : Refs(*request, "objects")) {
        if (!doc->NativeId(ref)) {
            return EDITOR_STALE;
        }
    }
    const auto objects = d.Ids(*doc, Refs(*request, "objects"));
    const auto target = doc->NativeId(Ref(*request, "target"));
    const auto scene = doc->NativeId(Ref(*request, "scene"));
    for (auto ref : Refs(*request, "objects")) {
        if (!doc->NativeId(ref)) {
            return EDITOR_STALE;
        }
    }
    for (const auto *key : {"target", "scene", "source", "container"}) {
        if (const auto ref = Ref(*request, key); ref && !doc->NativeId(ref)) {
            return EDITOR_STALE;
        }
    }
    if (Arg(*request, "type") && Number(*request, "type") > INT32_MAX) {
        return EDITOR_INVALID;
    }
    const int value = int(int64_t(Number(*request, "value")));
    const auto mode = Number(*request, "mode");
    auto result = EditResult::NoChange();
    const auto merge = [&](const std::vector<EditResult> &results) {
        bool changed = false;
        std::string messages;
        CK_ID created = 0;
        for (const auto &r : results) {
            if (r.Status() == EditStatus::Error) {
                return r;
            }
            if (r.ok) {
                changed = true;
                created = r.created;
            }
            if (!r.Unchanged() && !r.message.empty()) {
                if (!messages.empty()) {
                    messages += '\n';
                }
                messages += r.message;
            }
        }
        return changed            ? EditResult::Applied(messages, created)
               : messages.empty() ? EditResult::NoChange()
                                  : EditResult::Refused(messages);
    };
    if (command.action >= AttributeAdd && command.action <= RemoveUsers || command.action == AttributeAddSelected) {
        if (command.action != CategoryCreate && command.action != TypeCreate &&
            Number(*request, "catalog") != d.CatalogVersion(*doc)) {
            return EDITOR_STALE;
        }
    }
    switch (command.action) {
    case Visibility:
        if (mode > 2) {
            return EDITOR_INVALID;
        }
        result = ShowObjectsAs(*doc, objects, mode == 0 ? CKSHOW : mode == 1 ? CKHIDE : CKHIERARCHICALHIDE);
        break;
    case Priority:
        result = SetPriorities(*doc, objects, value);
        break;
    case RenderPriority:
        result = SetRenderPriority(*doc, objects, std::clamp(value, -10000, 10000));
        break;
    case SceneFlag: {
        auto *owner = CKScene::Cast(context->GetObject(scene));
        auto *object = CKSceneObject::Cast(context->GetObject(target));
        if (!owner || !object || !owner->IsObjectHere(object)) {
            return EDITOR_STALE;
        }
        const auto flags = owner->GetObjectFlags(object);
        CKDWORD add = 0, remove = 0;
        if (mode == 0) {
            if (flags & CK_SCENEOBJECT_START_ACTIVATE) {
                add = CK_SCENEOBJECT_START_DEACTIVATE;
                remove = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_LEAVE;
            } else if (flags & CK_SCENEOBJECT_START_DEACTIVATE) {
                add = CK_SCENEOBJECT_START_LEAVE;
                remove = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_DEACTIVATE;
            } else if (flags & CK_SCENEOBJECT_START_LEAVE) {
                add = CK_SCENEOBJECT_START_ACTIVATE;
                remove = CK_SCENEOBJECT_START_DEACTIVATE | CK_SCENEOBJECT_START_LEAVE;
            } else {
                add = CK_SCENEOBJECT_START_DEACTIVATE;
                remove = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_LEAVE;
            }
        } else if (mode == 1) {
            if (flags & CK_SCENEOBJECT_START_RESET) {
                remove = CK_SCENEOBJECT_START_RESET;
            } else {
                add = CK_SCENEOBJECT_START_RESET;
            }
        } else if (mode == 2) {
            result = SetActiveNow(*doc, objects, scene, !(flags & CK_SCENEOBJECT_ACTIVE));
            break;
        } else if (mode == 3 || mode == 4) {
            add = mode == 3 ? CK_SCENEOBJECT_START_ACTIVATE : CK_SCENEOBJECT_START_DEACTIVATE;
            remove = (mode == 3 ? CK_SCENEOBJECT_START_DEACTIVATE : CK_SCENEOBJECT_START_ACTIVATE) |
                     CK_SCENEOBJECT_START_LEAVE;
        } else {
            return EDITOR_INVALID;
        }
        result = ModifySceneFlags(*doc, objects, add, remove, "Scene Flags", scene);
        break;
    }
    case Active:
        if (mode > 1) {
            return EDITOR_INVALID;
        }
        result = SetActiveNow(*doc, objects, scene, mode != 0);
        break;
    case Initial: {
        if (mode > 2) {
            return EDITOR_INVALID;
        }
        auto targets = objects;
        if (Number(*request, "hierarchy")) {
            targets.clear();
            for (auto id : objects) {
                const auto all = HierarchyOf(*doc, id);
                targets.insert(targets.end(), all.begin(), all.end());
            }
        }
        result = ApplyInitialConditions(*doc, targets,
                                        mode == 0   ? InitialConditions::Set
                                        : mode == 1 ? InitialConditions::Restore
                                                    : InitialConditions::Remove);
        break;
    }
    case SendScene:
        result = SendToScene(*doc, objects, target, Number(*request, "dependencies") != 0);
        break;
    case SendGroup:
        result = SendToGroup(*doc, objects, target);
        break;
    case SendPlace:
        result = SendToPlace(*doc, objects, target);
        break;
    case RemovePlace:
        result = RemoveFromPlace(*doc, objects);
        break;
    case GroupMove:
        result = MoveInGroup(*doc, target, objects.empty() ? 0 : objects.front(), value);
        break;
    case ScriptMove:
        result = MoveScripts(*doc, objects, target);
        break;
    case Hierarchy:
        result = merge(SetHierarchyParents(*doc, objects, target, int(int64_t(Number(*request, "background")))));
        break;
    case Launch: {
        auto *level = context->GetCurrentLevel();
        auto *chosen = CKScene::Cast(context->GetObject(target));
        if (!level || !chosen) {
            return EDITOR_STALE;
        }
        if (level->GetCurrentScene() == chosen) {
            return EDITOR_NO_CHANGE;
        }
        result = level->LaunchScene(chosen) == CK_OK ? EditResult::Applied("Scene launched")
                                                     : EditResult::Error("Launch Scene failed");
        break;
    }
    case SaveOptions:
        result = SetSaveOptions(*doc, objects, value);
        break;
    case Copy: {
        std::vector<CK_ID> ordinary, scripts;
        for (auto id : objects) {
            auto *o = context->GetObject(id);
            if (CKLevel::Cast(o)) {
                continue;
            }
            if (CKBehavior::Cast(o)) {
                scripts.push_back(id);
            } else {
                ordinary.push_back(id);
            }
        }
        if (!ordinary.empty()) {
            d.objectClipboard.SetObjects(*doc, ordinary);
        } else if (!scripts.empty()) {
            d.objectClipboard.SetScripts(*doc, scripts);
        }
        result =
            ordinary.empty() && scripts.empty()
                ? EditResult::NoChange()
                : EditResult::Applied("Copied " + std::to_string(ordinary.empty() ? scripts.size() : ordinary.size()) +
                                      (ordinary.empty() ? " Script(s)" : " object(s)"));
        break;
    }
    case Paste: {
        if (objects.empty()) {
            return EDITOR_REFUSED;
        }
        const auto &sources = d.objectClipboard.Objects(*doc);
        if (sources.empty()) {
            result = merge(PasteScriptsOn(*doc, d.objectClipboard.Scripts(*doc), objects, true));
            break;
        }
        std::vector<CK_ID> containers;
        bool copies = false;
        auto *level = context->GetCurrentLevel();
        for (auto id : objects) {
            auto *object = context->GetObject(id);
            if (CKScene::Cast(object) || CKGroup::Cast(object) || CKPlace::Cast(object)) {
                containers.push_back(id);
            } else if (scene && (!level || scene != level->GetLevelScene()->GetID())) {
                containers.push_back(scene);
            } else {
                copies = true;
            }
        }
        std::sort(containers.begin(), containers.end());
        containers.erase(std::unique(containers.begin(), containers.end()), containers.end());
        std::vector<CK_ID> detached;
        for (auto id : sources) {
            if (doc->IsDiscarded(id)) {
                detached.push_back(id);
            }
        }
        std::vector<std::pair<CK_ID, CK_ID>> pasted;
        std::vector<EditResult> results;
        if (copies || !detached.empty()) {
            results.push_back(DuplicateObjects(*doc, copies ? sources : detached, &pasted));
        }
        for (auto id : containers) {
            auto *container = context->GetObject(id);
            std::vector<CK_ID> chosen;
            for (auto source : sources) {
                auto object = source;
                if (doc->IsDiscarded(object)) {
                    const auto found =
                        std::find_if(pasted.begin(), pasted.end(), [&](const auto &p) { return p.first == source; });
                    if (found == pasted.end()) {
                        continue;
                    }
                    object = found->second;
                }
                auto *o = context->GetObject(object);
                if (auto *g = CKGroup::Cast(container)) {
                    auto *be = CKBeObject::Cast(o);
                    if (be && !CKGroup::Cast(o) && !CKBehavior::Cast(o) && !be->IsInGroup(g)) {
                        chosen.push_back(object);
                    }
                } else if (auto *p = CKPlace::Cast(container)) {
                    auto *entity = CK3dEntity::Cast(o);
                    if (entity && !CKPlace::Cast(o) && entity->GetReferencePlace() != p) {
                        chosen.push_back(object);
                    }
                } else if (auto *sc = CKScene::Cast(container)) {
                    auto *so = CKSceneObject::Cast(o);
                    if (so && !CKBehavior::Cast(o) && !so->IsInScene(sc)) {
                        chosen.push_back(object);
                    }
                }
            }
            if (chosen.empty()) {
                continue;
            }
            if (CKGroup::Cast(container)) {
                results.push_back(SendToGroup(*doc, chosen, id));
            } else if (CKPlace::Cast(container)) {
                results.push_back(SendToPlace(*doc, chosen, id));
            } else {
                results.push_back(SendToScene(*doc, chosen, id));
            }
        }
        result = merge(results);
        break;
    }
    case Move: {
        const auto plan = PlanSceneMove(*doc, objects, target, doc->NativeId(Ref(*request, "container")));
        std::vector<EditResult> results;
        if (!plan.scripts.empty()) {
            results.push_back(MoveScripts(*doc, plan.scripts, plan.owner));
        }
        auto *destination = context->GetObject(plan.container);
        if (!plan.objects.empty()) {
            if (CKScene::Cast(destination)) {
                results.push_back(
                    SendToScene(*doc, plan.objects, plan.container, Number(*request, "dependencies") != 0));
            } else if (CKPlace::Cast(destination)) {
                results.push_back(SendToPlace(*doc, plan.objects, plan.container));
            } else if (CKGroup::Cast(destination)) {
                results.push_back(SendToGroup(*doc, plan.objects, plan.container));
            } else if (CKLevel::Cast(destination)) {
                results.push_back(RemoveFromPlace(*doc, plan.objects));
            }
        }
        result = merge(results);
        break;
    }
    case WorksetCreate: {
        const auto parent = Number(*request, "parent");
        if (d.nextWorkset > INT32_MAX || d.worksets.size() >= 4096) {
            return EDITOR_REFUSED;
        }
        if (parent &&
            std::none_of(d.worksets.begin(), d.worksets.end(), [&](const auto &w) { return w.id == parent; })) {
            return EDITOR_STALE;
        }
        SceneWorkset workset;
        workset.id = d.nextWorkset++;
        workset.parent = Number(*request, "parent");
        workset.name = Text(*request, "name");
        if (workset.name.empty()) {
            workset.name = "Workset " + std::to_string(workset.id);
        }
        workset.members = Refs(*request, "objects");
        d.worksets.push_back(workset);
        ++d.revision;
        d.trees.clear();
        const EditorValue field{EDITOR_TEXT("workset"), EDITOR_INTEGER_VALUE, {}, {}, workset.id};
        if (sink.write) {
            sink.write(sink.data, &field, 1);
        }
        return EDITOR_OK;
    }
    case WorksetRename:
    case WorksetDelete:
    case WorksetAdd:
    case WorksetRemove:
    case WorksetShow: {
        const auto id = Number(*request, "workset");
        auto found = std::find_if(d.worksets.begin(), d.worksets.end(), [&](const auto &w) { return w.id == id; });
        if (found == d.worksets.end()) {
            return EDITOR_STALE;
        }
        if (command.action == WorksetRename) {
            const auto name = Text(*request, "name");
            found->name = name;
        } else if (command.action == WorksetAdd) {
            for (auto ref : Refs(*request, "objects")) {
                if (std::find(found->members.begin(), found->members.end(), ref) == found->members.end()) {
                    found->members.push_back(ref);
                }
            }
        } else if (command.action == WorksetRemove) {
            const auto references = Refs(*request, "objects");
            found->members.erase(std::remove_if(found->members.begin(), found->members.end(),
                                                [&](ObjectRef ref) {
                                                    return std::find(references.begin(), references.end(), ref) !=
                                                           references.end();
                                                }),
                                 found->members.end());
        } else if (command.action == WorksetDelete) {
            std::set<uint64_t> removed{id};
            bool more = true;
            while (more) {
                more = false;
                for (const auto &w : d.worksets) {
                    if (removed.count(w.parent) && removed.insert(w.id).second) {
                        more = true;
                    }
                }
            }
            d.worksets.erase(std::remove_if(d.worksets.begin(), d.worksets.end(),
                                            [&](const auto &w) { return removed.count(w.id) != 0; }),
                             d.worksets.end());
        } else {
            std::set<uint64_t> included{id};
            bool more = true;
            while (more) {
                more = false;
                for (const auto &w : d.worksets) {
                    if (included.count(w.parent) && included.insert(w.id).second) {
                        more = true;
                    }
                }
            }
            for (const auto &w : d.worksets) {
                if (included.count(w.id)) {
                    for (auto ref : w.members) {
                        auto *o = doc->Resolve(ref);
                        if (auto *script = CKBehavior::Cast(o)) {
                            if (!mode) {
                                d.navigation().hiddenScripts.erase(doc->Reference(script->GetID()));
                            } else {
                                d.navigation().hiddenScripts.insert(doc->Reference(script->GetID()));
                            }
                        } else if (auto *be = CKBeObject::Cast(o)) {
                            for (int i = 0; i < be->GetScriptCount(); ++i) {
                                if (auto *script = be->GetScript(i)) {
                                    if (!mode) {
                                        d.navigation().hiddenScripts.erase(doc->Reference(script->GetID()));
                                    } else {
                                        d.navigation().hiddenScripts.insert(doc->Reference(script->GetID()));
                                    }
                                }
                            }
                        }
                    }
                }
            }
            d.editor.Execute("script.refresh", {request->scope, nullptr, 0});
        }
        result = EditResult::Applied();
        break;
    }
    case AttributeAdd: {
        const auto type = int(Number(*request, "type"));
        result = DropAttribute(*doc, objects, type, doc->NativeId(Ref(*request, "source")));
        break;
    }
    case AttributeRemove: {
        std::vector<EditResult> results;
        const auto *types = Arg(*request, "types");
        if (types) {
            for (uint32_t i = 0; i < types->integers.count; ++i) {
                results.push_back(RemoveAttributeFromObjects(*doc, objects, int(types->integers.data[i])));
            }
        }
        result = merge(results);
        break;
    }
    case AttributeText:
        result = SetAttributeText(*doc, target, int(Number(*request, "type")), Utf8ToAnsi(Text(*request, "text")));
        break;
    case AttributeCopy: {
        std::vector<CK_ID> values;
        for (const auto &row : ReadAttributeObjects(*doc, {int(Number(*request, "type"))}, objects)) {
            if (row.parameter) {
                values.push_back(row.parameter);
            }
        }
        d.clipboard.SetParameters(*doc, std::move(values));
        result = EditResult::Applied();
        break;
    }
    case AttributePaste: {
        if (!d.clipboard.BelongsTo(*doc)) {
            return EDITOR_STALE;
        }
        const auto values = FilterObjects(*doc, d.clipboard.parameters, CKCID_PARAMETER, ObjectQueryScope::Existing);
        if (values.empty()) {
            result = EditResult::Refused("Nothing to paste!");
        } else if (values.size() != 1) {
            result = EditResult::Refused("Can't paste multiple values, please select a single parameter!");
        } else {
            result = PasteAttributeValue(*doc, values.front(), objects, int(Number(*request, "type")));
        }
        break;
    }
    case CategoryCreate:
        result = CreateAttributeCategory(*doc, Utf8ToAnsi(Text(*request, "name")));
        break;
    case CategoryRename:
        result =
            RenameAttributeCategory(*doc, Utf8ToAnsi(Text(*request, "category")), Utf8ToAnsi(Text(*request, "name")));
        break;
    case CategoryDelete:
        result = DeleteAttributeCategory(*doc, Utf8ToAnsi(Text(*request, "category")));
        break;
    case TypeCreate: {
        const auto guid = Number(*request, "parameter");
        result =
            CreateAttributeType(*doc, Utf8ToAnsi(Text(*request, "name")), CKGUID(CKDWORD(guid >> 32), CKDWORD(guid)),
                                Utf8ToAnsi(Text(*request, "category")));
        break;
    }
    case TypeRename:
        result = RenameAttributeType(*doc, int(Number(*request, "type")), Utf8ToAnsi(Text(*request, "name")));
        break;
    case TypeDelete:
        result = DeleteAttributeType(*doc, int(Number(*request, "type")));
        break;
    case RemoveUsers:
        result = RemoveUserAttributes(*doc);
        break;
    case CopyDrag: {
        auto *owner = context->GetObject(target);
        const auto container = doc->NativeId(Ref(*request, "container"));
        std::vector<CK_ID> scripts, ordinary;
        for (auto id : objects) {
            (CKBehavior::Cast(context->GetObject(id)) ? scripts : ordinary).push_back(id);
        }
        if (!scripts.empty() && CKBeObject::Cast(owner) && !CKScene::Cast(owner) && !CKGroup::Cast(owner) &&
            !CKPlace::Cast(owner)) {
            result = merge(PasteScriptsOn(*doc, scripts, {target}, true));
        }
        if (!ordinary.empty() && container) {
            const auto refs = doc->References(ordinary);
            const EditorValue values[] = {
                {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {refs.data(), uint32_t(refs.size())}},
                {EDITOR_TEXT("target"), EDITOR_OBJECT, doc->Reference(container)}};
            const EditorRequest options{request->scope, values, 2, {}, request->operation};
            return d.editor.Host()->execute(d.editor.Host()->data, EditorString EDITOR_TEXT("scene.copy-options"),
                                            &options, sink);
        }
        break;
    }
    case Duplicate: {
        CKDependencies options = d.copyOptions();
        if (Number(*request, "custom")) {
            const auto *values = Arg(*request, "options");
            if (!values || values->integers.count > 100000) {
                return EDITOR_INVALID;
            }
            options.Resize(int(values->integers.count));
            options.m_Flags = CK_DEPENDENCIES_FLAGS(Number(*request, "flags"));
            for (uint32_t i = 0; i < values->integers.count; ++i) {
                if (values->integers.data[i] > UINT32_MAX) {
                    return EDITOR_INVALID;
                }
                options[int(i)] = CKDWORD(values->integers.data[i]);
            }
        }
        std::vector<CK_ID> ordinary, scripts;
        for (auto id : objects) {
            (CKBehavior::Cast(context->GetObject(id)) ? scripts : ordinary).push_back(id);
        }
        std::vector<EditResult> results;
        std::vector<std::pair<CK_ID, CK_ID>> copies;
        if (!scripts.empty()) {
            results.push_back(PasteScripts(*doc, scripts, target, true));
        }
        if (!ordinary.empty()) {
            results.push_back(DuplicateObjects(*doc, ordinary, &copies, &options));
        }
        std::vector<CK_ID> fresh;
        for (const auto &pair : copies) {
            fresh.push_back(pair.second);
        }
        auto *container = context->GetObject(target);
        if (!fresh.empty()) {
            if (CKPlace::Cast(container)) {
                results.push_back(SendToPlace(*doc, fresh, target));
            } else if (CKScene::Cast(container)) {
                results.push_back(SendToScene(*doc, fresh, target));
            } else if (CKGroup::Cast(container)) {
                results.push_back(SendToGroup(*doc, fresh, target));
            }
        }
        result = merge(results);
        if (result.ok) {
            d.copyOptions() = options;
        }
        break;
    }
    case Export: {
        std::vector<CK_ID> saved;
        bool scripts = true;
        for (auto id : objects) {
            auto *o = context->GetObject(id);
            scripts = scripts && CKBehavior::Cast(o);
            auto *level = CKLevel::Cast(o);
            if (!level) {
                saved.push_back(id);
                continue;
            }
            for (int i = 0; i < level->GetScriptCount(); ++i) {
                if (auto *script = level->GetScript(i)) {
                    saved.push_back(script->GetID());
                }
            }
            for (int i = 0; i < level->GetSceneCount(); ++i) {
                if (auto *scene = level->GetScene(i)) {
                    saved.push_back(scene->GetID());
                }
            }
            if (auto *scene = level->GetLevelScene()) {
                for (CKSceneObjectIterator it = scene->GetObjectIterator(); !it.End(); it++) {
                    if (auto *held = context->GetObject(it.GetObjectID())) {
                        if (held != level && !held->IsDynamic()) {
                            saved.push_back(held->GetID());
                        }
                    }
                }
            }
        }
        std::sort(saved.begin(), saved.end());
        saved.erase(std::unique(saved.begin(), saved.end()), saved.end());
        const auto refs = doc->References(saved);
        const EditorValue value{
            EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {refs.data(), uint32_t(refs.size())}};
        const EditorRequest forwarded{request->scope, &value, 1, {}, request->operation};
        return d.editor.Host()->execute(d.editor.Host()->data,
                                        scripts ? EditorString EDITOR_TEXT("behavior.export")
                                                : EditorString EDITOR_TEXT("object.export"),
                                        &forwarded, sink);
    }
    case AttributeAddSelected: {
        const auto *types = Arg(*request, "types");
        std::vector<EditResult> results;
        for (uint32_t i = 0; types && i < types->integers.count; ++i) {
            if (types->integers.data[i] > INT32_MAX) {
                return EDITOR_INVALID;
            }
            results.push_back(AddAttribute(*doc, objects, int(types->integers.data[i])));
        }
        result = merge(results);
        break;
    }
    }
    if (result.ok) {
        ++d.revision;
        d.trees.clear();
        if (command.action >= CategoryCreate && command.action <= RemoveUsers) {
            ++d.catalogEpoch;
        }
    }
    if (command.action == TypeCreate) {
        const auto message = AnsiToUtf8(result.message);
        const EditorValue fields[] = {{EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE, {}, {}, result.created},
                                      {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, {}, EditorText(message)}};
        if (sink.write) {
            sink.write(sink.data, fields, 2);
        }
        return result.ok ? EDITOR_OK : EDITOR_REFUSED;
    }
    return Result(result, *doc, sink);
}
bool SceneCommands::Register(std::string &error) {
    auto &d = *m_Data;
    using Parameters = std::vector<EditorParameter>;
    const EditorParameter objects{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST};
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT};
    const EditorParameter mode{EDITOR_TEXT("mode"), EDITOR_INTEGER_VALUE};
    const EditorParameter value{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE};
    const EditorParameter name{EDITOR_TEXT("name"), EDITOR_TEXT_VALUE};
    const EditorParameter type{EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE};
    const EditorParameter catalog{EDITOR_TEXT("catalog"), EDITOR_INTEGER_VALUE};
    const EditorParameter scene{EDITOR_TEXT("scene"), EDITOR_OBJECT};
    const EditorParameter workset{EDITOR_TEXT("workset"), EDITOR_INTEGER_VALUE};
    std::deque<Parameters> schemas;
    std::vector<EditorCommandDesc> commands;
    auto add = [&](const char *id, const char *title, Data::Action action, Parameters parameters,
                   EditorEffect effect = EDITOR_DOCUMENT_EDIT) {
        schemas.push_back(std::move(parameters));
        d.commands.push_back({&d, action});
        commands.push_back({sizeof(EditorCommandDesc), EditorLiteral(id), EDITOR_TEXT("builtin.scene"),
                            EditorLiteral(title), schemas.back().data(), uint32_t(schemas.back().size()), Data::Run,
                            &d.commands.back(), EDITOR_DOCUMENT, effect});
    };
    add("scene.visibility", "Show/Hide Objects", Data::Visibility, {objects, mode});
    add("scene.priority", "Edit Priority", Data::Priority, {objects, value});
    add("scene.render-priority", "Edit Render Priority", Data::RenderPriority, {objects, value});
    add("scene.flags", "Scene Flags", Data::SceneFlag, {objects, target, scene, mode});
    add("scene.active", "Activate Objects", Data::Active, {objects, scene, mode});
    add("scene.initial-conditions", "Initial Conditions", Data::Initial,
        {objects, mode, {EDITOR_TEXT("hierarchy"), EDITOR_INTEGER_VALUE, 1}});
    add("scene.send-to-scene", "Send To Scene", Data::SendScene,
        {objects, target, {EDITOR_TEXT("dependencies"), EDITOR_INTEGER_VALUE, 1}});
    add("scene.send-to-group", "Send To Group", Data::SendGroup, {objects, target});
    add("scene.send-to-place", "Send To Place", Data::SendPlace, {objects, target});
    add("scene.remove-from-place", "Remove From Place", Data::RemovePlace, {objects});
    add("scene.group-order", "Move In Group", Data::GroupMove, {objects, target, value});
    add("scene.move-scripts", "Move Scripts", Data::ScriptMove, {objects, target});
    add("scene.hierarchy", "Set Hierarchy Parent", Data::Hierarchy,
        {objects, {EDITOR_TEXT("target"), EDITOR_OBJECT, 1}, {EDITOR_TEXT("background"), EDITOR_INTEGER_VALUE}});
    add("scene.launch", "Launch Scene", Data::Launch, {target}, EDITOR_RUNTIME_CONTROL);
    add("scene.save-options", "Set Save Options", Data::SaveOptions, {objects, value});
    add("scene.copy", "Copy Objects", Data::Copy, {objects}, EDITOR_SESSION_CHANGE);
    add("scene.paste", "Paste Objects", Data::Paste, {objects, {EDITOR_TEXT("scene"), EDITOR_OBJECT, 1}});
    add("scene.duplicate", "Copy Objects", Data::Duplicate,
        {objects,
         target,
         {EDITOR_TEXT("custom"), EDITOR_INTEGER_VALUE, 1},
         {EDITOR_TEXT("flags"), EDITOR_INTEGER_VALUE, 1},
         {EDITOR_TEXT("options"), EDITOR_INTEGER_LIST, 1}});
    add("scene.export", "Save Objects as...", Data::Export, {objects}, EDITOR_FILE_OPERATION);
    add("attribute.add-selected", "Add Attributes", Data::AttributeAddSelected,
        {objects, catalog, {EDITOR_TEXT("types"), EDITOR_INTEGER_LIST}});
    add("scene.move", "Move Objects", Data::Move,
        {objects,
         target,
         {EDITOR_TEXT("container"), EDITOR_OBJECT, 1},
         {EDITOR_TEXT("dependencies"), EDITOR_INTEGER_VALUE, 1}});
    add("scene.copy-drag", "Copy Objects", Data::CopyDrag,
        {objects, target, {EDITOR_TEXT("container"), EDITOR_OBJECT, 1}}, EDITOR_DOCUMENT_CHANGE);
    add("scene.workset-create", "Create Workset", Data::WorksetCreate,
        {objects, {EDITOR_TEXT("parent"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE, 1}},
        EDITOR_SESSION_CHANGE);
    add("scene.workset-rename", "Rename Workset", Data::WorksetRename, {workset, name}, EDITOR_SESSION_CHANGE);
    add("scene.workset-delete", "Delete Workset", Data::WorksetDelete, {workset}, EDITOR_SESSION_CHANGE);
    add("scene.workset-add", "Add To Workset", Data::WorksetAdd, {workset, objects}, EDITOR_SESSION_CHANGE);
    add("scene.workset-remove", "Remove From Workset", Data::WorksetRemove, {workset, objects}, EDITOR_SESSION_CHANGE);
    add("scene.workset-show", "Show/Hide Workset Scripts", Data::WorksetShow, {workset, mode}, EDITOR_SESSION_CHANGE);
    add("attribute.add", "Add Attribute", Data::AttributeAdd,
        {objects, type, catalog, {EDITOR_TEXT("source"), EDITOR_OBJECT, 1}});
    add("attribute.remove", "Remove Attributes", Data::AttributeRemove,
        {objects, {EDITOR_TEXT("types"), EDITOR_INTEGER_LIST}, catalog});
    add("attribute.set-text", "Edit Attribute Value", Data::AttributeText,
        {target, type, catalog, {EDITOR_TEXT("text"), EDITOR_TEXT_VALUE}});
    add("attribute.copy", "Copy Attribute Value", Data::AttributeCopy, {objects, type, catalog}, EDITOR_SESSION_CHANGE);
    add("attribute.paste", "Paste Attribute Value", Data::AttributePaste, {objects, type, catalog});
    add("attribute.create-category", "Create Attribute Category", Data::CategoryCreate, {name}, EDITOR_DOCUMENT_CHANGE);
    add("attribute.rename-category", "Rename Attribute Category", Data::CategoryRename,
        {{EDITOR_TEXT("category"), EDITOR_TEXT_VALUE}, name, catalog}, EDITOR_DOCUMENT_CHANGE);
    add("attribute.delete-category", "Delete Attribute Category", Data::CategoryDelete,
        {{EDITOR_TEXT("category"), EDITOR_TEXT_VALUE}, catalog}, EDITOR_DOCUMENT_CHANGE);
    add("attribute.create-type", "Create Attribute Type", Data::TypeCreate,
        {name, {EDITOR_TEXT("parameter"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("category"), EDITOR_TEXT_VALUE}},
        EDITOR_DOCUMENT_CHANGE);
    add("attribute.rename-type", "Rename Attribute Type", Data::TypeRename, {type, name, catalog},
        EDITOR_DOCUMENT_CHANGE);
    add("attribute.delete-type", "Delete Attribute Type", Data::TypeDelete, {type, catalog}, EDITOR_DOCUMENT_CHANGE);
    add("attribute.remove-user", "Remove User Attributes", Data::RemoveUsers, {catalog}, EDITOR_DOCUMENT_CHANGE);
    const Parameters treeArgs = {
        {EDITOR_TEXT("priority"), EDITOR_INTEGER_VALUE, 1},  {EDITOR_TEXT("reversed"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("context"), EDITOR_OBJECT, 1},          {EDITOR_TEXT("hierarchy"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("resources"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1}};
    Parameters treeFields;
    for (auto key : {"keys", "parents", "kinds", "types", "cells", "classes", "priorities", "render-priorities",
                     "icons", "worksets", "roots", "save-options", "expanded"}) {
        treeFields.push_back({EditorLiteral(key), EDITOR_INTEGER_LIST});
    }
    for (auto key : {"objects", "scenes", "sections", "groups", "scripts"}) {
        treeFields.push_back({EditorLiteral(key), EDITOR_OBJECT_LIST});
    }
    for (auto key : {"names", "owners"}) {
        treeFields.push_back({EditorLiteral(key), EDITOR_TEXT_LIST});
    }
    for (auto key : {"total", "revision"}) {
        treeFields.push_back({EditorLiteral(key), EDITOR_INTEGER_VALUE});
    }
    for (auto key : {"level", "level-scene", "current-scene"}) {
        treeFields.push_back({EditorLiteral(key), EDITOR_OBJECT});
    }
    const Parameters catalogFields = {
        {EDITOR_TEXT("types"), EDITOR_INTEGER_LIST},    {EDITOR_TEXT("flags"), EDITOR_INTEGER_LIST},
        {EDITOR_TEXT("parents"), EDITOR_INTEGER_LIST},  {EDITOR_TEXT("category-flags"), EDITOR_INTEGER_LIST},
        {EDITOR_TEXT("names"), EDITOR_TEXT_LIST},       {EDITOR_TEXT("parameters"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("compatible"), EDITOR_TEXT_LIST},  {EDITOR_TEXT("categories"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("catalog"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("available"), EDITOR_INTEGER_VALUE}};
    const Parameters attributeArgs = {{EDITOR_TEXT("types"), EDITOR_INTEGER_LIST},
                                      catalog,
                                      {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, 1},
                                      {EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1}};
    const Parameters attributeFields = {
        {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("parameters"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("types"), EDITOR_INTEGER_LIST},  {EDITOR_TEXT("names"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("values"), EDITOR_TEXT_LIST},    {EDITOR_TEXT("parameter-names"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE}, catalog};
    const EditorParameter parameterFields[] = {{EDITOR_TEXT("guids"), EDITOR_INTEGER_LIST},
                                               {EDITOR_TEXT("names"), EDITOR_TEXT_LIST}};
    const EditorParameter filter{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, 1};
    const EditorParameter copyFields[] = {{EDITOR_TEXT("flags"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("options"), EDITOR_INTEGER_LIST}};
    const EditorParameter moveArgs[] = {objects, target, {EDITOR_TEXT("container"), EDITOR_OBJECT, 1}};
    const EditorParameter moveFields[] = {{EDITOR_TEXT("groups"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE}};
    const EditorParameter deleteArgs[] = {
        objects, {EDITOR_TEXT("scene"), EDITOR_OBJECT, 1}, {EDITOR_TEXT("group"), EDITOR_OBJECT, 1}};
    const EditorParameter deleteFields[] = {
        {EDITOR_TEXT("scripts"), EDITOR_OBJECT_LIST},  {EDITOR_TEXT("leave"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("destroy"), EDITOR_OBJECT_LIST},  {EDITOR_TEXT("members"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("groups"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE}};
    EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("scene.tree"), EDITOR_TEXT("builtin.scene"), treeArgs.data(),
         uint32_t(treeArgs.size()), Data::Tree, &d, EDITOR_QUERY_VERSIONED, EDITOR_DOCUMENT, treeFields.data(),
         uint32_t(treeFields.size()), Data::Version},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("attribute.catalog"), EDITOR_TEXT("builtin.scene"), &filter, 1,
         Data::Catalog, &d, EDITOR_QUERY_VERSIONED, EDITOR_DOCUMENT, catalogFields.data(),
         uint32_t(catalogFields.size()), Data::Version},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("attribute.objects"), EDITOR_TEXT("builtin.scene"), attributeArgs.data(),
         uint32_t(attributeArgs.size()), Data::Attributes, &d, EDITOR_QUERY_VERSIONED, EDITOR_DOCUMENT,
         attributeFields.data(), uint32_t(attributeFields.size()), Data::Version},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("scene.parameter-types"), EDITOR_TEXT("builtin.scene"), nullptr, 0,
         Data::Parameters, &d, EDITOR_QUERY_VERSIONED, EDITOR_DOCUMENT, parameterFields, 2, Data::Version},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("scene.copy-options"), EDITOR_TEXT("builtin.scene"), nullptr, 0,
         Data::CopyOptions, &d, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, copyFields, 2},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("scene.delete-plan"), EDITOR_TEXT("builtin.scene"), deleteArgs, 3,
         Data::DeletePlan, &d, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, deleteFields, 6},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("scene.move-plan"), EDITOR_TEXT("builtin.scene"), moveArgs, 3,
         Data::MovePlan, &d, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, moveFields, 2}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("scene.editing");
    module.commands = commands.data();
    module.commandCount = uint32_t(commands.size());
    module.queries = queries;
    module.queryCount = 7;
    return d.editor.Register(module, error);
}
} // namespace vdev
namespace vdev {
uint64_t SceneCommands::Data::CatalogVersion(Document &doc) const {
    return CatalogHash(ReadAttributeCatalog(doc)) ^ (doc.Token().value * 0x9e3779b97f4a7c15ULL) ^ catalogEpoch;
}
EditorStatus SceneCommands::Data::CopyOptions(void *data, const EditorHost *, const EditorRequest *request,
                                              EditorSink sink) {
    auto &d = *static_cast<Data *>(data);
    if (!d.DocumentOf(*request)) {
        return EDITOR_STALE;
    }
    const auto &options = d.copyOptions();
    std::vector<uint64_t> values;
    for (int i = 0; i < options.Size(); ++i) {
        values.push_back(options[i]);
    }
    const EditorValue fields[] = {
        {EDITOR_TEXT("flags"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(options.m_Flags)},
        {EDITOR_TEXT("options"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {values.data(), uint32_t(values.size())}}};
    return sink.write ? sink.write(sink.data, fields, 2) : EDITOR_INVALID;
}
EditorStatus SceneCommands::Data::DeletePlan(void *data, const EditorHost *, const EditorRequest *request,
                                             EditorSink sink) {
    auto &d = *static_cast<Data *>(data);
    auto *doc = d.DocumentOf(*request);
    if (!doc) {
        return EDITOR_STALE;
    }
    auto *context = doc->Context();
    auto *scene = CKScene::Cast(doc->Resolve(Ref(*request, "scene")));
    auto *group = CKGroup::Cast(doc->Resolve(Ref(*request, "group")));
    auto *level = context->GetCurrentLevel();
    if (level && scene == level->GetLevelScene()) {
        scene = nullptr;
    }
    std::vector<ObjectRef> scripts, leave, destroy, members;
    bool groups = false;
    std::string message;
    if (scene && group) {
        message = "Cannot remove an entity from a group in a scene, please remove it from level !";
    } else {
        for (auto id : d.Ids(*doc, Refs(*request, "objects"))) {
            auto *object = context->GetObject(id);
            const auto ref = doc->Reference(id);
            if (auto *script = CKBehavior::Cast(object)) {
                if (!scene || CKScene::Cast(script->GetOwner())) {
                    scripts.push_back(ref);
                } else {
                    message = "Please delete object scripts in the level.";
                }
            } else if (CKLevel::Cast(object)) {
                continue;
            } else if (scene) {
                if (CKScene::Cast(object)) {
                    destroy.push_back(ref);
                } else if (CKSceneObject::Cast(object)) {
                    leave.push_back(ref);
                    if (auto *g = CKGroup::Cast(object)) {
                        groups = groups || g->GetObjectCount() > 0;
                    }
                }
            } else if (group) {
                members.push_back(ref);
            } else {
                destroy.push_back(ref);
            }
        }
    }
    const EditorValue fields[] = {
        {EDITOR_TEXT("scripts"), EDITOR_OBJECT_LIST, {}, {}, 0, {scripts.data(), uint32_t(scripts.size())}},
        {EDITOR_TEXT("leave"), EDITOR_OBJECT_LIST, {}, {}, 0, {leave.data(), uint32_t(leave.size())}},
        {EDITOR_TEXT("destroy"), EDITOR_OBJECT_LIST, {}, {}, 0, {destroy.data(), uint32_t(destroy.size())}},
        {EDITOR_TEXT("members"), EDITOR_OBJECT_LIST, {}, {}, 0, {members.data(), uint32_t(members.size())}},
        {EDITOR_TEXT("groups"), EDITOR_INTEGER_VALUE, {}, {}, groups},
        {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, {}, EditorText(message)}};
    return sink.write ? sink.write(sink.data, fields, 6) : EDITOR_INVALID;
}
} // namespace vdev

namespace vdev {
EditorStatus SceneCommands::Data::MovePlan(void *data, const EditorHost *, const EditorRequest *request,
                                           EditorSink sink) {
    auto &d = *static_cast<Data *>(data);
    auto *doc = d.DocumentOf(*request);
    if (!doc) {
        return EDITOR_STALE;
    }
    for (auto ref : Refs(*request, "objects")) {
        if (!doc->NativeId(ref)) {
            return EDITOR_STALE;
        }
    }
    for (auto key : {"target", "container"}) {
        if (auto ref = Ref(*request, key); ref && !doc->NativeId(ref)) {
            return EDITOR_STALE;
        }
    }
    const auto plan = PlanSceneMove(*doc, d.Ids(*doc, Refs(*request, "objects")),
                                    doc->NativeId(Ref(*request, "target")), doc->NativeId(Ref(*request, "container")));
    const auto message = AnsiToUtf8(plan.message);
    const EditorValue fields[] = {{EDITOR_TEXT("groups"), EDITOR_INTEGER_VALUE, {}, {}, plan.groups},
                                  {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, {}, EditorText(message)}};
    return sink.write ? sink.write(sink.data, fields, 2) : EDITOR_INVALID;
}
} // namespace vdev
