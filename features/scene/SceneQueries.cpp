#include "features/scene/SceneQueries.h"
#include "core/catalog/ClassInfo.h"
#include "core/level/LevelTree.h"
#include "core/level/HierarchyTree.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <functional>

namespace vdev {
namespace {
uint64_t Key(uint64_t parent, uint32_t kind, uint64_t object, const std::string &tag = {}) {
    uint64_t key = (parent ^ (object + 0x9e3779b97f4a7c15ULL) ^ (uint64_t(kind) << 48)) * 1099511628211ULL;
    for (unsigned char byte : tag) {
        key ^= byte;
        key *= 1099511628211ULL;
    }
    return key ? key : 1;
}
std::string Name(CKObject *object, const char *fallback = "<Unnamed>") {
    return object && object->GetName() && *object->GetName() ? AnsiToUtf8(object->GetName()) : fallback;
}
int Priority(CKObject *object) {
    if (auto *be = CKBeObject::Cast(object)) {
        return be->GetPriority();
    }
    if (auto *script = CKBehavior::Cast(object)) {
        return script->GetPriority();
    }
    return -1;
}
void Describe(Document &document, SceneRow &row, CKObject *object, CKScene *scene, const std::set<CK_ID> &hidden) {
    if (!object) {
        return;
    }
    row.object = document.Reference(object->GetID());
    row.cid = object->GetClassID();
    row.name = Name(object);
    row.priority = Priority(object);
    row.saveOption = SaveOptionOf(object);
    auto *be = CKBeObject::Cast(object);
    auto *behavior = CKBehavior::Cast(object);
    if (be) {
        row.types |= EDITOR_OBJECT_BEHAVIORAL;
        if (be->GetScriptCount() && be->GetScript(0)) {
            row.script = document.Reference(be->GetScript(0)->GetID());
        }
    }
    if (auto *render = CKRenderObject::Cast(object)) {
        row.types |= EDITOR_OBJECT_RENDER;
        row.renderPriority = render->GetZOrder();
    }
    if (CK3dEntity::Cast(object)) {
        row.types |= EDITOR_OBJECT_ENTITY3D;
    }
    if (CK2dEntity::Cast(object)) {
        row.types |= EDITOR_OBJECT_ENTITY2D;
    }
    if (CKGroup::Cast(object)) {
        row.types |= EDITOR_OBJECT_GROUP;
    }
    if (CKPlace::Cast(object)) {
        row.types |= EDITOR_OBJECT_PLACE;
    }
    if (CKCharacter::Cast(object)) {
        row.types |= EDITOR_OBJECT_CHARACTER;
    }
    if (CKScene::Cast(object)) {
        row.types |= EDITOR_OBJECT_SCENE;
    }
    if (CKLevel::Cast(object)) {
        row.types |= EDITOR_OBJECT_LEVEL;
    }
    if (CKSound::Cast(object)) {
        row.types |= EDITOR_OBJECT_SOUND;
    }
    if (row.saveOption >= 0 && !CKSound::Cast(object)) {
        row.types |= EDITOR_OBJECT_BITMAP;
    }
    if (behavior) {
        row.types |= EDITOR_OBJECT_SCRIPT;
        row.script = row.object;
        row.owner = "Script Owner: " + Name(behavior->GetOwner(), "-");
        if (!hidden.count(object->GetID())) {
            row.cells |= EDITOR_CELL_SCRIPT_SHOWN;
        }
    }
    auto *sceneObject = CKSceneObject::Cast(object);
    if (!sceneObject) {
        return;
    }
    if (sceneObject->CanBeHide()) {
        row.cells |= EDITOR_CELL_HIDEABLE;
    }
    if (sceneObject->IsVisible()) {
        row.cells |= EDITOR_CELL_VISIBLE;
    }
    if (sceneObject->IsHierarchicallyHide()) {
        row.cells |= EDITOR_CELL_HIERARCHY_HIDDEN;
    }
    if (auto *entity = CK3dEntity::Cast(object)) {
        for (auto *parent = entity->GetParent(); parent; parent = parent->GetParent()) {
            if (parent->IsHierarchicallyHide()) {
                row.cells |= EDITOR_CELL_PARENT_HIDDEN;
                break;
            }
        }
    }
    if (auto *entity = CK2dEntity::Cast(object)) {
        for (auto *parent = entity->GetParent(); parent; parent = parent->GetParent()) {
            if (parent->IsHierarchicallyHide()) {
                row.cells |= EDITOR_CELL_PARENT_HIDDEN;
                break;
            }
        }
    }
    if (!scene) {
        return;
    }
    row.scene = document.Reference(scene->GetID());
    if (!scene->IsObjectHere(sceneObject)) {
        return;
    }
    if (scene->GetObjectInitialValue(sceneObject)) {
        row.cells |= EDITOR_CELL_INITIAL;
    }
    if (!be && !behavior) {
        return;
    }
    row.cells |= EDITOR_CELL_SCENE_FLAGS;
    const auto flags = scene->GetObjectFlags(sceneObject);
    if (flags & CK_SCENEOBJECT_START_ACTIVATE) {
        row.cells |= EDITOR_CELL_START_ACTIVATE;
    }
    if (flags & CK_SCENEOBJECT_START_DEACTIVATE) {
        row.cells |= EDITOR_CELL_START_DEACTIVATE;
    }
    if (flags & CK_SCENEOBJECT_START_LEAVE) {
        row.cells |= EDITOR_CELL_START_LEAVE;
    }
    if (flags & CK_SCENEOBJECT_START_RESET) {
        row.cells |= EDITOR_CELL_START_RESET;
    }
    if (flags & CK_SCENEOBJECT_ACTIVE) {
        row.cells |= EDITOR_CELL_ACTIVE;
    }
}
} // namespace
SceneTree ReadLevelTree(Document &document, const SceneTreeOptions &options, const std::vector<SceneWorkset> &worksets,
                        const std::set<CK_ID> &hidden) {
    SceneTree result;
    result.document = document.Token();
    auto *context = document.Context();
    auto *level = context ? context->GetCurrentLevel() : nullptr;
    if (!level) {
        return result;
    }
    auto *levelScene = level->GetLevelScene();
    auto *contextScene = CKScene::Cast(document.Resolve(options.context));
    if (!contextScene) {
        contextScene = levelScene;
    }
    result.level = document.Reference(level->GetID());
    result.levelScene = document.Reference(levelScene ? levelScene->GetID() : 0);
    result.currentScene = document.Reference(level->GetCurrentScene() ? level->GetCurrentScene()->GetID() : 0);
    const auto less = [&](CKObject *a, CKObject *b) {
        if (options.priority && Priority(a) != Priority(b)) {
            return options.reversed ? Priority(a) < Priority(b) : Priority(a) > Priority(b);
        }
        return options.priority || !options.reversed ? LevelNameLess(a, b) : LevelNameLess(b, a);
    };
    const auto add = [&](uint32_t parent, uint32_t kind, CKObject *object, CKScene *scene, const std::string &tag,
                         const std::string &label, int icon, bool expanded) {
        SceneRow row;
        row.parent = parent;
        row.depth = parent ? result.rows[parent - 1].depth + 1 : 0;
        row.kind = kind;
        row.expanded = expanded;
        row.icon = icon ? icon : object ? ClassIconImage(object->GetClassID()) : 0;
        row.scene = document.Reference(scene ? scene->GetID() : 0);
        Describe(document, row, object, scene, hidden);
        if (kind == EDITOR_SCENE_LEVEL || kind == EDITOR_SCENE_SCENE) {
            row.cells = 0;
        }
        if (!label.empty()) {
            row.name = label;
        }
        row.key = Key(parent ? result.rows[parent - 1].key : document.Token().value, kind, row.object.lifetime, tag);
        result.rows.push_back(std::move(row));
        return uint32_t(result.rows.size());
    };
    const auto folder = [&](uint32_t parent, const char *tag, const std::string &label, int icon = 0x188) {
        return add(parent, EDITOR_SCENE_FOLDER, nullptr, nullptr, tag, label, icon, true);
    };
    const auto script = [&](uint32_t parent, CKBehavior *object, CKScene *scene) {
        if (object) {
            add(parent, EDITOR_SCENE_SCRIPT, object, scene, {}, {}, 0, false);
        }
    };
    std::set<CK_ID> activeGroups;
    std::function<void(uint32_t, CKSceneObject *, CKScene *, CKGroup *)> object;
    object = [&](uint32_t parent, CKSceneObject *value, CKScene *scene, CKGroup *group) {
        if (!value || document.IsDiscarded(value->GetID())) {
            return;
        }
        const auto index = add(parent, EDITOR_SCENE_OBJECT, value, scene, {}, {}, 0, false);
        result.rows[index - 1].group = document.Reference(group ? group->GetID() : 0);
        auto *be = CKBeObject::Cast(value);
        std::vector<CKObject *> scripts;
        for (int i = 0; be && i < be->GetScriptCount(); ++i) {
            if (be->GetScript(i)) {
                scripts.push_back(be->GetScript(i));
            }
        }
        if (options.priority) {
            std::stable_sort(scripts.begin(), scripts.end(), less);
        }
        for (auto *each : scripts) {
            script(index, CKBehavior::Cast(each), scene);
        }
        if (auto *character = CKCharacter::Cast(value)) {
            if (character->GetAnimationCount()) {
                const auto animations = folder(index, "animations", "Animations");
                for (int i = 0; i < character->GetAnimationCount(); ++i) {
                    object(animations, character->GetAnimation(i), scene, nullptr);
                }
            }
            if (character->GetBodyPartCount()) {
                const auto parts = folder(index, "body-parts", "Body Parts");
                std::vector<CKObject *> values;
                for (int i = 0; i < character->GetBodyPartCount(); ++i) {
                    if (character->GetBodyPart(i)) {
                        values.push_back(character->GetBodyPart(i));
                    }
                }
                std::sort(values.begin(), values.end(), less);
                for (auto *part : values) {
                    object(parts, CKSceneObject::Cast(part), scene, nullptr);
                }
            }
        }
        if (auto *members = CKGroup::Cast(value)) {
            if (activeGroups.insert(value->GetID()).second) {
                for (int i = 0; i < members->GetObjectCount(); ++i) {
                    object(index, members->GetObject(i), scene, members);
                }
                activeGroups.erase(value->GetID());
            }
        }
    };
    const auto folders = [&](uint32_t parent, const LevelFolders &values, CKScene *scene) {
        for (const auto &entry : values) {
            const auto at = folder(parent, entry.first.c_str(), entry.first);
            auto items = entry.second;
            std::sort(items.begin(), items.end(), less);
            for (auto *item : items) {
                object(at, item, scene, nullptr);
            }
        }
    };
    const auto contents = [&](uint32_t parent, CKScene *source, CKScene *flags, bool global) {
        const auto all = ListSceneContents(context, source, level, options.dynamic);
        const auto globalIndex = folder(parent, global ? "global" : "entities", global ? "Global" : "Entities");
        folders(globalIndex, all.global, flags);
        const auto placesIndex = folder(parent, "places", "Places", 0xc5);
        auto places = all.places;
        std::sort(places.begin(), places.end(), less);
        for (auto *place : places) {
            const auto at = add(placesIndex, EDITOR_SCENE_OBJECT, place, flags, "place", {}, 0, false);
            for (int i = 0; i < place->GetScriptCount(); ++i) {
                script(at, place->GetScript(i), flags);
            }
            const auto found = all.placed.find(place->GetID());
            if (found != all.placed.end()) {
                folders(at, found->second, flags);
            }
        }
    };
    std::function<void(uint32_t, uint64_t, CKScene *)> addWorksets;
    addWorksets = [&](uint32_t parent, uint64_t owner, CKScene *scene) {
        for (const auto &workset : worksets) {
            if (workset.parent == owner) {
                const auto at = add(parent, EDITOR_SCENE_WORKSET, nullptr, scene, std::to_string(workset.id),
                                    workset.name, 0x3f3, true);
                result.rows[at - 1].workset = int(workset.id);
                addWorksets(at, workset.id, scene);
                for (const auto &member : workset.members) {
                    auto *value = document.Resolve(member);
                    if (auto *behavior = CKBehavior::Cast(value)) {
                        script(at, behavior, scene);
                    } else if (auto *sceneObject = CKSceneObject::Cast(value)) {
                        object(at, sceneObject, scene, nullptr);
                    }
                }
            }
        }
    };
    std::vector<CKObject *> levelScripts;
    for (int i = 0; i < level->GetScriptCount(); ++i) {
        if (level->GetScript(i)) {
            levelScripts.push_back(level->GetScript(i));
        }
    }
    std::sort(levelScripts.begin(), levelScripts.end(), less);
    const auto root = add(0, EDITOR_SCENE_LEVEL, level, contextScene, "level", Name(level, "<Unnamed Level>"), 0, true);
    const auto scripts = folder(root, "level-scripts", "Level Scripts", 0xca);
    for (auto *each : levelScripts) {
        script(scripts, CKBehavior::Cast(each), contextScene);
    }
    contents(root, levelScene, contextScene, true);
    addWorksets(folder(root, "worksets", "Worksets", 0x3f3), 0, contextScene);
    std::vector<CKObject *> scenes;
    for (int i = 0; i < level->GetSceneCount(); ++i) {
        if (level->GetScene(i)) {
            scenes.push_back(level->GetScene(i));
        }
    }
    std::stable_sort(scenes.begin(), scenes.end(), less);
    for (auto *value : scenes) {
        auto *scene = CKScene::Cast(value);
        const auto at =
            add(0, EDITOR_SCENE_SCENE, scene, scene, "scene",
                Name(scene, "<Unnamed Scene>") + (scene == level->GetCurrentScene() ? "  (current)" : ""), 0, false);
        if (scene == level->GetCurrentScene()) {
            result.rows[at - 1].cells |= EDITOR_CELL_CURRENT_SCENE;
        }
        const auto scripts = folder(at, "level-scripts", "Level Scripts", 0xca);
        for (auto *each : levelScripts) {
            script(scripts, CKBehavior::Cast(each), scene);
        }
        const auto own = folder(at, "scene-scripts", "Scene Scripts", 0xca);
        std::vector<CKObject *> sceneScripts;
        for (int i = 0; i < scene->GetScriptCount(); ++i) {
            if (scene->GetScript(i)) {
                sceneScripts.push_back(scene->GetScript(i));
            }
        }
        if (options.priority) {
            std::stable_sort(sceneScripts.begin(), sceneScripts.end(), less);
        }
        for (auto *each : sceneScripts) {
            script(own, CKBehavior::Cast(each), scene);
        }
        contents(at, scene, scene, false);
        addWorksets(folder(at, "worksets", "Worksets", 0x3f3), 0, scene);
    }
    for (auto &row : result.rows) {
        row.section = row.parent                       ? result.rows[row.parent - 1].section
                      : row.kind == EDITOR_SCENE_SCENE ? row.object
                                                       : ObjectRef{};
    }
    return result;
}
SceneTree ReadHierarchyTree(Document &document, const SceneTreeOptions &options) {
    SceneTree result;
    result.document = document.Token();
    const auto roots = BuildHierarchy(document, {options.meshes, options.materials, options.textures});
    std::function<void(const HierarchyItem &, uint32_t)> add = [&](const HierarchyItem &item, uint32_t parent) {
        SceneRow row;
        row.parent = parent;
        row.depth = parent ? result.rows[parent - 1].depth + 1 : 0;
        row.root = item.root;
        row.kind = item.object ? EDITOR_SCENE_OBJECT : EDITOR_SCENE_ROOT;
        row.cid = item.cid;
        row.expanded = !item.object;
        Describe(document, row, document.Context()->GetObject(item.object), nullptr, {});
        row.renderPriority = item.renderPriority;
        row.name = AnsiToUtf8(item.name);
        if (item.renderObject) {
            row.types |= EDITOR_OBJECT_RENDER;
        }
        row.key = Key(parent ? result.rows[parent - 1].key : document.Token().value, row.kind,
                      row.object ? row.object.lifetime : uint64_t(item.root + 1));
        result.rows.push_back(std::move(row));
        const auto index = uint32_t(result.rows.size());
        for (const auto &child : item.children) {
            add(child, index);
        }
    };
    for (const auto &root : roots) {
        add(root, 0);
    }
    return result;
}
} // namespace vdev
