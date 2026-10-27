#include <filesystem>
#include "core/level/LevelCommands.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>

#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/CkArrays.h"
#include "core/runtime/GridCompat.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Document.h"
#include "core/document/detail/NativeExport.h"
#include "core/level/detail/LevelCommandsDetail.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/TopologyCommands.h"
#include "core/graph/NmsCommands.h"
#include "core/schematic/SchematicEdits.h"
#include "core/document/UndoHistory.h"

namespace vdev {

using namespace level;

// Curve points: CurveCommands.cpp; Create, Replace Material, Duplicate: CreateCommands.cpp.

namespace {

// Scripts are in the scenes of their owner. CKBeObject::AddScript adds them
// (AddToSelfScenes, CK2) and RemoveScript removes them; any scene still
// missing one is completed here and undone with it.
std::vector<CK_ID> ScenesMissing(CKContext *context, CKBeObject *owner, CKBehavior *script) {
    std::vector<CK_ID> scenes;
    CKLevel *level = context->GetCurrentLevel();
    if (!level) return scenes;
    std::vector<CKScene *> all{level->GetLevelScene()};
    for (int i = 0; i < level->GetSceneCount(); ++i) all.push_back(level->GetScene(i));
    for (CKScene *scene : all)
        if (scene && scene->IsObjectHere(owner) && !scene->IsObjectHere(script)) scenes.push_back(scene->GetID());
    return scenes;
}

// Gives scripts to owner (and owner's scenes); the inverse takes them back.
DocumentFn GiveScripts(CK_ID ownerId, std::vector<CK_ID> scripts, std::shared_ptr<std::vector<std::pair<CK_ID, CK_ID>>> added) {
    return [ownerId, scripts, added](Document &document) {
        CKBeObject *owner = Get<CKBeObject>(document, ownerId);
        if (!owner) return false;
        added->clear();
        for (CK_ID id : scripts) {
            CKBehavior *script = Get<CKBehavior>(document, id);
            if (!script || AddScriptLast(owner, script) != CK_OK) return false;
            for (CK_ID scene : ScenesMissing(document.Context(), owner, script)) {
                Get<CKScene>(document, scene)->AddObjectToScene(script, FALSE);
                added->emplace_back(scene, id);
            }
        }
        return true;
    };
}

DocumentFn TakeScripts(CK_ID ownerId, std::vector<CK_ID> scripts, std::shared_ptr<std::vector<std::pair<CK_ID, CK_ID>>> added) {
    return [ownerId, scripts, added](Document &document) {
        CKBeObject *owner = Get<CKBeObject>(document, ownerId);
        if (!owner) return false;
        for (const auto &entry : *added)
            if (CKScene *scene = Get<CKScene>(document, entry.first))
                if (CKSceneObject *script = Get<CKSceneObject>(document, entry.second)) scene->RemoveObjectFromScene(script, FALSE);
        for (CK_ID id : scripts)
            if (CKBehavior *script = Get<CKBehavior>(document, id))
                if (script->GetOwner() == owner) owner->RemoveScript(id);
        return true;
    };
}

std::vector<CKSceneObject *> SceneObjects(Document &document, const std::vector<CK_ID> &ids) {
    std::vector<CKSceneObject *> objects;
    for (CK_ID id : ids)
        if (CKSceneObject *object = Get<CKSceneObject>(document, id))
            if (!CKLevel::Cast(object) && !CKScene::Cast(object)) objects.push_back(object);
    return objects;
}

} // namespace

EditResult CreateScript(Document &document, CK_ID ownerId, const std::string &name) {
    CKContext *context = document.Context();
    CKBeObject *owner = Get<CKBeObject>(document, ownerId);
    if (!owner || CKBehavior::Cast(owner) || CKScene::Cast(owner))
        return EditResult::Refused("Cannot create script, item is not an entity !");
    const std::string label = name.empty() ? std::string(owner->GetName() ? owner->GetName() : "") + " Script" : name;
    CKBehavior *script = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, const_cast<CKSTRING>(label.c_str())));
    if (!script) return EditResult::Refused("CKContext::CreateObject failed");
    script->UseGraph();
    script->SetType(CKBEHAVIORTYPE_SCRIPT);
    script->CreateInput((CKSTRING) "Start");
    // Loaded Scripts carry CKBEHAVIOR_TOPMOST (no behavior includes them).
    script->ModifyFlags(CKBEHAVIOR_TOPMOST, 0);
    const CK_ID id = script->GetID();
    auto added = std::make_shared<std::vector<std::pair<CK_ID, CK_ID>>>();
    return Apply(document, "Create Script", GiveScripts(ownerId, {id}, added), TakeScripts(ownerId, {id}, added),
                 {id}, id);
}

EditResult LoadScripts(Document &document, const std::string &path, CK_ID ownerId) {
    CKContext *context = document.Context();
    CKBeObject *owner = Get<CKBeObject>(document, ownerId);
    if (!owner || CKBehavior::Cast(owner) || CKScene::Cast(owner)) return EditResult::Refused("Can only load Scripts on objects !");
    CKObjectArray *array = CreateCKObjectArray();
    CKERROR loaded = CKERR_INVALIDPARAMETER;
    GuardedCall([&] { loaded = context->Load(const_cast<CKSTRING>(std::filesystem::u8path(path).string().c_str()), array, CK_LOAD_DEFAULT); },
                &document.Host(), "CKContext::Load");
    std::vector<CK_ID> objects;
    for (array->Reset(); !array->EndOfList(); array->Next()) objects.push_back(array->GetDataId());
    DeleteCKObjectArray(array);
    auto discard = [&] {
        XObjectArray all;
        for (CK_ID id : objects)
            if (context->GetObject(id)) all.PushBack(id);
        GuardedCall([&] { context->DestroyObjects(all.Begin(), all.Size()); }, &document.Host(), "DestroyObjects");
    };
    if (loaded != CK_OK) {
        discard();
        document.Host().Log(LogLevel::Warning, "Cannot load " + path + " (" + std::to_string(loaded) + ")");
        return EditResult::Refused("Cannot load Script file (" + std::to_string(loaded) + ")");
    }
    std::vector<CK_ID> scripts;
    for (CK_ID id : objects) {
        CKBehavior *behavior = CKBehavior::Cast(context->GetObject(id));
        if (behavior && (behavior->GetType() & CKBEHAVIORTYPE_SCRIPT) &&
            (!behavior->GetParent() || behavior->GetParent() == behavior))
            scripts.push_back(id);
    }
    if (scripts.empty()) {
        discard();
        return EditResult::Refused(std::string(NO_SCRIPT_IN_FILE) + " (Import Behavior Graph takes graphs and Building Blocks)");
    }
    for (CK_ID id : scripts) {
        // A Script saved alone still names the owner it had.
        CKBehavior *script = CKBehavior::Cast(context->GetObject(id));
        if (CKBeObject *previous = script->GetOwner())
            if (previous != owner) previous->RemoveScript(id);
    }
    auto added = std::make_shared<std::vector<std::pair<CK_ID, CK_ID>>>();
    const std::set<CK_ID> created(objects.begin(), objects.end());
    EditResult result = Apply(document, "Load Script", GiveScripts(ownerId, scripts, added),
                              TakeScripts(ownerId, scripts, added), created, scripts.front());
    if (!result.ok) {
        discard();
        return EditResult::Refused("The owner's class does not accept the Script");
    }
    result.message = std::to_string(scripts.size()) + " Script(s)";
    return result;
}

EditResult DropBuildingBlockOn(Document &document, CK_ID object, const CKGUID &guid, const std::set<CK_ID> &skip,
                               CK_ID *script, bool *insertRefused) {
    if (script) *script = 0;
    if (insertRefused) *insertRefused = false;
    CKContext *context = document.Context();
    CKBeObject *owner = CKBeObject::Cast(context->GetObject(object));
    if (!owner || CKBehavior::Cast(owner)) return EditResult::Refused("Not an object Scripts can be put on");
    CK_ID scriptId = 0;
    for (int i = 0; i < owner->GetScriptCount() && !scriptId; ++i)
        if (CKBehavior *each = owner->GetScript(i))
            if (!skip.count(each->GetID())) scriptId = each->GetID();
    Edit edit(document, "Drop Behavior");
    if (!scriptId) {
        const EditResult made = CreateScript(document, object, "");
        if (!made.ok) return made;
        scriptId = made.created;
    }
    const EditResult inserted = DropBuildingBlockInto(document, scriptId, guid);
    if (!inserted.ok) {
        if (insertRefused) *insertRefused = true;
        return inserted;
    }
    if (script) *script = scriptId;
    return edit.Commit(std::string(), inserted.created);
}

EditResult DropBuildingBlockInto(Document &document, CK_ID scriptId, const CKGUID &guid) {
    CKContext *context = document.Context();
    Point at(140.0f + 100.0f, 0.0f);
    if (ScriptSession *session = document.Session(scriptId)) {
        const InterfaceData &data = session->Data();
        at = Point(data.header.hStartPos + 100.0f, data.header.vStartPos);
        CKBehavior *graph = CKBehavior::Cast(context->GetObject(scriptId));
        bool any = false;
        float right = 0.0f, top = 0.0f;
        for (int i = 0; graph && i < graph->GetSubBehaviorCount(); ++i)
            if (const BehaviorData *record = FindRecord(data, graph->GetSubBehavior(i)->GetID())) {
                right = any ? std::max(right, record->rect.hPos + record->rect.hSize) : record->rect.hPos + record->rect.hSize;
                top = any ? std::min(top, record->rect.vPos) : record->rect.vPos;
                any = true;
            }
        if (any) at = Point(right + 60.0f, top);
    }
    return InsertBuildingBlock(document, scriptId, scriptId, guid, at);
}

EditResult CreateScene(Document &document, const std::string &name,
                       const std::vector<CK_ID> &objects) {
    CKContext *context = document.Context();
    CKLevel *level = context->GetCurrentLevel();
    if (!level) return EditResult::Refused("No current level");
    const std::string label = name.empty() ? "New Scene" : name;
    CKScene *scene = CKScene::Cast(context->CreateObject(CKCID_SCENE, const_cast<CKSTRING>(label.c_str())));
    if (!scene) return EditResult::Refused("CKContext::CreateObject failed");
    const CK_ID id = scene->GetID(), levelId = level->GetID();
    std::vector<CK_ID> members;
    for (CKSceneObject *object : SceneObjects(document, objects)) members.push_back(object->GetID());
    return Apply(
        document, "Create Scene",
        [id, levelId, members](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKScene *created = Get<CKScene>(doc, id);
            if (!owner || !created || owner->AddScene(created) != CK_OK) return false;
            for (CK_ID member : members)
                if (CKSceneObject *object = Get<CKSceneObject>(doc, member)) created->AddObjectToScene(object);
            return true;
        },
        [id, levelId, members](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKScene *created = Get<CKScene>(doc, id);
            if (!owner || !created) return false;
            for (CK_ID member : members)
                if (CKSceneObject *object = Get<CKSceneObject>(doc, member)) created->RemoveObjectFromScene(object);
            return owner->RemoveScene(created) == CK_OK;
        },
        {id}, id);
}

namespace {

std::set<CK_ID> SceneMembers(CKScene *scene) {
    std::set<CK_ID> members;
    for (CKSceneObjectIterator it = scene->GetObjectIterator(); !it.End(); it++) members.insert(it.GetObjectID());
    return members;
}

// What a scene took or lost in a step, and how each was in it.
struct SceneEntry {
    CK_ID object = 0;
    CK_SCENEOBJECT_FLAGS flags = CK_SCENEOBJECT_FLAGS(0);
    std::shared_ptr<CKStateChunk> initial;
};

std::shared_ptr<CKStateChunk> CopyChunk(CKStateChunk *chunk) {
    if (!chunk) return nullptr;
    return std::shared_ptr<CKStateChunk>(CreateCKStateChunk(chunk), [](CKStateChunk *c) { DeleteCKStateChunk(c); });
}

} // namespace

EditResult SendToScene(Document &document, const std::vector<CK_ID> &objects, CK_ID sceneId, bool groupMembers) {
    CKScene *scene = Get<CKScene>(document, sceneId);
    if (!scene) return EditResult::Refused("Choose a scene");
    std::vector<CK_ID> members;
    for (CKSceneObject *object : SceneObjects(document, objects))
        if (!scene->IsObjectHere(object)) members.push_back(object->GetID());
    if (members.empty()) return EditResult::Refused("The objects are already in the scene");
    auto added = std::make_shared<std::vector<CK_ID>>();
    return Apply(
        document, "Send To Scene",
        [sceneId, members, groupMembers, added](Document &doc) {
            CKScene *target = Get<CKScene>(doc, sceneId);
            if (!target) return false;
            const std::set<CK_ID> before = SceneMembers(target);
            for (CK_ID member : members)
                if (CKSceneObject *object = Get<CKSceneObject>(doc, member))
                    target->AddObjectToScene(object, CKGroup::Cast(object) ? groupMembers : TRUE);
            added->clear();
            for (CK_ID now : SceneMembers(target))
                if (!before.count(now)) added->push_back(now);
            return true;
        },
        [sceneId, added](Document &doc) {
            CKScene *target = Get<CKScene>(doc, sceneId);
            if (!target) return false;
            for (CK_ID member : *added)
                if (CKSceneObject *object = Get<CKSceneObject>(doc, member)) target->RemoveObjectFromScene(object, FALSE);
            return true;
        });
}

EditResult RemoveFromScene(Document &document, const std::vector<CK_ID> &objects, CK_ID sceneId, bool dependencies) {
    CKScene *scene = Get<CKScene>(document, sceneId);
    if (!scene) return EditResult::Refused("Choose a scene");
    std::vector<CK_ID> members;
    for (CKSceneObject *object : SceneObjects(document, objects))
        if (scene->IsObjectHere(object) && !CKScene::Cast(object) && !CKLevel::Cast(object)) members.push_back(object->GetID());
    if (members.empty()) return EditResult::Refused("The objects are not in the scene");
    auto removed = std::make_shared<std::vector<SceneEntry>>();
    return Apply(
        document, "Remove From Scene",
        [sceneId, members, dependencies, removed](Document &doc) {
            CKScene *target = Get<CKScene>(doc, sceneId);
            if (!target) return false;
            // How each was in the scene, for what leaves it.
            std::map<CK_ID, SceneEntry> was;
            for (CKSceneObjectIterator it = target->GetObjectIterator(); !it.End(); it++) {
                CKSceneObject *object = Get<CKSceneObject>(doc, it.GetObjectID());
                if (!object) continue;
                SceneEntry entry;
                entry.object = it.GetObjectID();
                entry.flags = target->GetObjectFlags(object);
                entry.initial = CopyChunk(target->GetObjectInitialValue(object));
                was[entry.object] = entry;
            }
            for (CK_ID member : members)
                if (CKSceneObject *object = Get<CKSceneObject>(doc, member)) target->RemoveObjectFromScene(object, dependencies);
            const std::set<CK_ID> after = SceneMembers(target);
            removed->clear();
            for (const auto &entry : was)
                if (!after.count(entry.first)) removed->push_back(entry.second);
            return true;
        },
        [sceneId, removed](Document &doc) {
            CKScene *target = Get<CKScene>(doc, sceneId);
            if (!target) return false;
            for (const SceneEntry &entry : *removed) {
                CKSceneObject *object = Get<CKSceneObject>(doc, entry.object);
                if (!object) continue;
                target->AddObjectToScene(object, FALSE);
                target->SetObjectFlags(object, entry.flags);
                if (entry.initial) target->SetObjectInitialValue(object, CreateCKStateChunk(entry.initial.get()));
            }
            return true;
        });
}

namespace {

std::vector<CK_ID> BeObjects(Document &document, const std::vector<CK_ID> &ids, CK_ID except = 0) {
    std::vector<CK_ID> members;
    for (CK_ID id : ids)
        if (CKBeObject *object = Get<CKBeObject>(document, id))
            if (id != except && !CKBehavior::Cast(object) && !CKLevel::Cast(object) && !CKScene::Cast(object))
                members.push_back(id);
    return members;
}

DocumentFn AddToGroup(CK_ID groupId, std::vector<CK_ID> members) {
    return [groupId, members](Document &doc) {
        CKGroup *group = Get<CKGroup>(doc, groupId);
        if (!group) return false;
        for (CK_ID member : members)
            if (CKBeObject *object = Get<CKBeObject>(doc, member)) group->AddObject(object);
        return true;
    };
}

DocumentFn RemoveFromGroup(CK_ID groupId, std::vector<CK_ID> members) {
    return [groupId, members](Document &doc) {
        CKGroup *group = Get<CKGroup>(doc, groupId);
        if (!group) return false;
        for (CK_ID member : members)
            if (CKBeObject *object = Get<CKBeObject>(doc, member)) group->RemoveObject(object);
        return true;
    };
}

} // namespace

EditResult CreateGroup(Document &document, const std::string &name,
                       const std::vector<CK_ID> &objects, CK_ID sceneId) {
    CKContext *context = document.Context();
    CKLevel *level = context->GetCurrentLevel();
    if (!level) return EditResult::Refused("No current level");
    if (sceneId && (!Get<CKScene>(document, sceneId) || sceneId == level->GetLevelScene()->GetID())) sceneId = 0;
    const std::string label = name.empty() ? "New Group" : name;
    CKGroup *group = CKGroup::Cast(context->CreateObject(CKCID_GROUP, const_cast<CKSTRING>(label.c_str())));
    if (!group) return EditResult::Refused("CKContext::CreateObject failed");
    const CK_ID id = group->GetID(), levelId = level->GetID();
    const std::vector<CK_ID> members = BeObjects(document, objects);
    const DocumentFn add = AddToGroup(id, members), remove = RemoveFromGroup(id, members);
    // What the group took into the scene with it (Dev: its dependencies).
    auto added = std::make_shared<std::vector<CK_ID>>();
    return Apply(
        document, "Create Group",
        [id, levelId, sceneId, add, added](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKObject *created = doc.Context()->GetObject(id);
            if (!owner || !created || owner->AddObject(created) != CK_OK || !add(doc)) return false;
            if (CKScene *scene = sceneId ? Get<CKScene>(doc, sceneId) : nullptr) {
                const std::set<CK_ID> before = SceneMembers(scene);
                scene->AddObjectToScene(CKSceneObject::Cast(created), TRUE);
                added->clear();
                for (CK_ID now : SceneMembers(scene))
                    if (!before.count(now)) added->push_back(now);
            }
            return true;
        },
        [id, levelId, sceneId, remove, added](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKObject *created = doc.Context()->GetObject(id);
            if (CKScene *scene = sceneId ? Get<CKScene>(doc, sceneId) : nullptr)
                for (CK_ID member : *added)
                    if (CKSceneObject *object = Get<CKSceneObject>(doc, member)) scene->RemoveObjectFromScene(object, FALSE);
            return owner && created && remove(doc) && owner->RemoveObject(created) == CK_OK;
        },
        {id}, id);
}

EditResult CreateSelectionGroup(Document &document, CK_ID sceneId, const std::string &name,
                                const std::vector<CK_ID> &objects) {
    CKContext *context = document.Context();
    CKLevel *level = context->GetCurrentLevel();
    if (!level || !Get<CKScene>(document, sceneId)) return EditResult::Refused("No current scene");
    const std::vector<CK_ID> members = BeObjects(document, objects);
    if (name.empty() || members.empty()) return EditResult::Refused("Select objects and type the group's name");
    CKGroup *group = CKGroup::Cast(context->CreateObject(CKCID_GROUP, const_cast<CKSTRING>(name.c_str())));
    if (!group) return EditResult::Refused("CKContext::CreateObject failed");
    group->ModifyObjectFlags(CK_OBJECT_INTERFACEOBJ | CK_OBJECT_PRIVATE | CK_OBJECT_NOTTOBESAVED, 0);
    const CK_ID id = group->GetID(), levelId = level->GetID();
    const DocumentFn add = AddToGroup(id, members), remove = RemoveFromGroup(id, members);
    return Apply(
        document, "Create Selection Group",
        [id, levelId, sceneId, add](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKScene *scene = Get<CKScene>(doc, sceneId);
            CKGroup *created = Get<CKGroup>(doc, id);
            if (!owner || !scene || !created || owner->AddObject(created) != CK_OK || !add(doc)) return false;
            scene->AddObjectToScene(created, TRUE);
            return true;
        },
        [id, levelId, sceneId, remove](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKScene *scene = Get<CKScene>(doc, sceneId);
            CKGroup *created = Get<CKGroup>(doc, id);
            if (!owner || !scene || !created || !remove(doc)) return false;
            scene->RemoveObjectFromScene(created, TRUE);
            return owner->RemoveObject(created) == CK_OK;
        },
        {id}, id);
}

EditResult SendToGroup(Document &document, const std::vector<CK_ID> &objects, CK_ID groupId) {
    CKGroup *group = Get<CKGroup>(document, groupId);
    if (!group) return EditResult::Refused("Choose a group");
    std::vector<CK_ID> members;
    for (CK_ID id : BeObjects(document, objects, groupId))
        if (!Get<CKBeObject>(document, id)->IsInGroup(group)) members.push_back(id);
    if (members.empty()) return EditResult::Refused("The objects are already in the group");
    return Apply(document, "Send To Group", AddToGroup(groupId, members), RemoveFromGroup(groupId, members));
}

EditResult RemoveGroupMembers(Document &document, CK_ID groupId, const std::vector<CK_ID> &objects) {
    CKGroup *group = Get<CKGroup>(document, groupId);
    if (!group) return EditResult::Refused("Choose a group");
    const std::set<CK_ID> chosen(objects.begin(), objects.end());
    std::vector<std::pair<int, CK_ID>> places; // the members' positions, ascending
    for (int i = 0; i < group->GetObjectCount(); ++i)
        if (CKBeObject *member = group->GetObject(i))
            if (chosen.count(member->GetID())) places.push_back({i, member->GetID()});
    if (places.empty()) return EditResult::Refused("The objects are not in the group");
    return Apply(
        document, "Remove From Group",
        [groupId, places](Document &doc) {
            CKGroup *target = Get<CKGroup>(doc, groupId);
            if (!target) return false;
            for (const auto &place : places)
                if (CKBeObject *object = Get<CKBeObject>(doc, place.second)) target->RemoveObject(object);
            return true;
        },
        [groupId, places](Document &doc) {
            CKGroup *target = Get<CKGroup>(doc, groupId);
            if (!target) return false;
            for (const auto &place : places)
                if (CKBeObject *object = Get<CKBeObject>(doc, place.second))
                    target->InsertObjectAt(object, std::min(place.first, target->GetObjectCount()));
            return true;
        });
}

namespace {

// Body parts into or out of a character; the root it had back with undo.
EditResult BodyParts(Document &document, CK_ID characterId, const std::vector<CK_ID> &parts, bool add) {
    CKCharacter *character = Get<CKCharacter>(document, characterId);
    if (!character) return EditResult::Refused("Choose a character");
    std::vector<CK_ID> chosen;
    for (CK_ID id : parts)
        if (CKBodyPart *part = Get<CKBodyPart>(document, id))
            if (add ? part->GetCharacter() == nullptr : part->GetCharacter() == character) chosen.push_back(id);
    if (chosen.empty()) return EditResult::Refused(add ? "The body parts belong to a character" : "Choose its body parts");
    const CK_ID root = character->GetRootBodyPart() ? character->GetRootBodyPart()->GetID() : 0;
    auto in = [characterId, chosen](Document &doc) {
        CKCharacter *target = Get<CKCharacter>(doc, characterId);
        if (!target) return false;
        for (CK_ID id : chosen)
            if (CKBodyPart *part = Get<CKBodyPart>(doc, id)) target->AddBodyPart(part);
        return true;
    };
    auto out = [characterId, chosen](Document &doc) {
        CKCharacter *target = Get<CKCharacter>(doc, characterId);
        if (!target) return false;
        for (CK_ID id : chosen)
            if (CKBodyPart *part = Get<CKBodyPart>(doc, id)) target->RemoveBodyPart(part);
        return true;
    };
    auto rooted = [characterId, root](const DocumentFn &change) {
        return [characterId, root, change](Document &doc) {
            if (!change(doc)) return false;
            CKCharacter *target = Get<CKCharacter>(doc, characterId);
            if (CKBodyPart *part = root ? Get<CKBodyPart>(doc, root) : nullptr)
                if (target && part->GetCharacter() == target) target->SetRootBodyPart(part);
            return true;
        };
    };
    return add ? Apply(document, "Add Body Part", in, rooted(out)) : Apply(document, "Remove Body Part", out, rooted(in));
}

} // namespace

EditResult AddBodyParts(Document &document, CK_ID character, const std::vector<CK_ID> &parts) {
    return BodyParts(document, character, parts, true);
}

EditResult RemoveBodyParts(Document &document, CK_ID character, const std::vector<CK_ID> &parts) {
    return BodyParts(document, character, parts, false);
}

EditResult MoveScripts(Document &document, const std::vector<CK_ID> &scripts, CK_ID targetId,
                       std::vector<std::string> *refused) {
    CKBeObject *target = Get<CKBeObject>(document, targetId);
    if (!target || CKBehavior::Cast(target)) return EditResult::Refused("Choose an object");
    std::vector<std::pair<CK_ID, CK_ID>> moves; // the Script, its owner
    for (CK_ID id : scripts) {
        CKBehavior *script = Get<CKBehavior>(document, id);
        if (!script || !(script->GetFlags() & CKBEHAVIOR_SCRIPT)) continue;
        CKBeObject *owner = script->GetOwner();
        if (!owner || owner == target) continue;
        if (!CKIsChildClassOf(target, script->GetCompatibleClassID())) {
            if (refused) refused->push_back(script->GetName() ? script->GetName() : "");
            continue;
        }
        moves.push_back({id, owner->GetID()});
    }
    if (moves.empty()) return EditResult::Refused("Incompatible class");
    return Apply(
        document, "Move Script",
        [targetId, moves](Document &doc) {
            CKBeObject *to = Get<CKBeObject>(doc, targetId);
            if (!to) return false;
            for (const auto &move : moves) {
                CKBehavior *script = Get<CKBehavior>(doc, move.first);
                CKBeObject *from = Get<CKBeObject>(doc, move.second);
                if (!script || !from || CKMoveScript(from, to, script) != CK_OK) return false;
            }
            return true;
        },
        [targetId, moves](Document &doc) {
            CKBeObject *from = Get<CKBeObject>(doc, targetId);
            if (!from) return false;
            for (auto it = moves.rbegin(); it != moves.rend(); ++it) {
                CKBehavior *script = Get<CKBehavior>(doc, it->first);
                CKBeObject *to = Get<CKBeObject>(doc, it->second);
                if (!script || !to || CKMoveScript(from, to, script) != CK_OK) return false;
            }
            return true;
        });
}

namespace {

// Entities' parents before a place move, restored by undo.
struct Parents {
    std::vector<std::pair<CK_ID, CK_ID>> entries; // entity, former parent (0: none)
};

DocumentFn SetParents(std::shared_ptr<Parents> parents, CK_ID place) {
    return [parents, place](Document &doc) {
        CK3dEntity *target = place ? Get<CK3dEntity>(doc, place) : nullptr;
        if (place && !target) return false;
        for (const auto &entry : parents->entries)
            if (CK3dEntity *entity = Get<CK3dEntity>(doc, entry.first))
                if (!entity->SetParent(target, TRUE)) return false;
        return true;
    };
}

DocumentFn RestoreParents(std::shared_ptr<Parents> parents) {
    return [parents](Document &doc) {
        for (const auto &entry : parents->entries)
            if (CK3dEntity *entity = Get<CK3dEntity>(doc, entry.first))
                entity->SetParent(entry.second ? Get<CK3dEntity>(doc, entry.second) : nullptr, TRUE);
        return true;
    };
}

std::shared_ptr<Parents> EntitiesOf(Document &document, const std::vector<CK_ID> &ids, CK_ID except) {
    auto parents = std::make_shared<Parents>();
    for (CK_ID id : ids) {
        CK3dEntity *entity = Get<CK3dEntity>(document, id);
        if (!entity || id == except || CKPlace::Cast(entity)) continue;
        parents->entries.emplace_back(id, entity->GetParent() ? entity->GetParent()->GetID() : 0);
    }
    return parents;
}

} // namespace

EditResult CreatePlace(Document &document, const std::string &name,
                       const std::vector<CK_ID> &objects) {
    CKContext *context = document.Context();
    CKLevel *level = context->GetCurrentLevel();
    if (!level) return EditResult::Refused("No current level");
    const std::string label = name.empty() ? "New Place" : name;
    CKPlace *place = CKPlace::Cast(context->CreateObject(CKCID_PLACE, const_cast<CKSTRING>(label.c_str())));
    if (!place) return EditResult::Refused("CKContext::CreateObject failed");
    const CK_ID id = place->GetID(), levelId = level->GetID();
    auto parents = EntitiesOf(document, objects, id);
    const DocumentFn move = SetParents(parents, id), back = RestoreParents(parents);
    return Apply(
        document, "Create Place",
        [id, levelId, move](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKPlace *created = Get<CKPlace>(doc, id);
            return owner && created && owner->AddPlace(created) == CK_OK && move(doc);
        },
        [id, levelId, back](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKPlace *created = Get<CKPlace>(doc, id);
            return owner && created && back(doc) && owner->RemovePlace(created) == CK_OK;
        },
        {id}, id);
}

EditResult SendToPlace(Document &document, const std::vector<CK_ID> &objects, CK_ID placeId) {
    if (!Get<CKPlace>(document, placeId)) return EditResult::Refused("Choose a place");
    auto parents = EntitiesOf(document, objects, placeId);
    if (parents->entries.empty())
        return EditResult::Refused("Cannot move object on place, can only add object derived from 3D Entity !");
    return Apply(document, "Send to Place", SetParents(parents, placeId), RestoreParents(parents));
}

EditResult RemoveFromPlace(Document &document, const std::vector<CK_ID> &objects) {
    auto parents = EntitiesOf(document, objects, 0);
    auto inPlace = std::make_shared<Parents>();
    for (const auto &entry : parents->entries)
        if (entry.second && Get<CKPlace>(document, entry.second)) inPlace->entries.push_back(entry);
    if (inPlace->entries.empty()) return EditResult::Refused("The objects are not in a place");
    return Apply(document, "Remove From Place", SetParents(inPlace, 0), RestoreParents(inPlace));
}

EditResult RenameLevelObject(Document &document, CK_ID id, const std::string &name) {
    CKObject *object = document.Context()->GetObject(id);
    if (!object) return EditResult::Refused("Select an object");
    const std::string before = object->GetName() ? object->GetName() : "";
    if (before == name) return EditResult::NoChange("The name is unchanged");
    auto rename = [id](const std::string &text) {
        return [id, text](Document &doc) {
            CKObject *target = doc.Context()->GetObject(id);
            if (!target) return false;
            target->SetName(const_cast<CKSTRING>(text.c_str()));
            return true;
        };
    };
    return Apply(document, "Rename", rename(name), rename(before));
}

EditResult CreateArray(Document &document, const std::string &name) {
    CKContext *context = document.Context();
    CKLevel *level = context->GetCurrentLevel();
    if (!level) return EditResult::Refused("No current level");
    const std::string label = name.empty() ? "New Array" : name;
    CKObject *array = context->CreateObject(CKCID_DATAARRAY, const_cast<CKSTRING>(label.c_str()));
    if (!array) return EditResult::Refused("CKContext::CreateObject failed");
    const CK_ID id = array->GetID(), levelId = level->GetID();
    return Apply(
        document, "Create Array",
        [id, levelId](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKObject *created = doc.Context()->GetObject(id);
            return owner && created && owner->AddObject(created) == CK_OK;
        },
        [id, levelId](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKObject *created = doc.Context()->GetObject(id);
            return owner && created && owner->RemoveObject(created) == CK_OK;
        },
        {id}, id);
}

EditResult CreateNamedObject(Document &document, CK_CLASSID cid, const std::string &name) {
    CKContext *context = document.Context();
    CKLevel *level = context->GetCurrentLevel();
    if (!level) return EditResult::Refused("No current level");
    if (name.empty()) return EditResult::Refused("The object needs a name");
    CKObject *object = context->CreateObject(cid, const_cast<CKSTRING>(name.c_str()));
    if (!object) return EditResult::Refused("CKContext::CreateObject failed");
    const CK_ID id = object->GetID(), levelId = level->GetID();
    // The level keeps scene objects (in its scene); the others, as synchro
    // objects, are the composition's by themselves.
    const bool inScene = CKIsChildClassOf(cid, CKCID_SCENEOBJECT) != FALSE;
    return Apply(
        document, "Create Object",
        [id, levelId, inScene](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKObject *created = doc.Context()->GetObject(id);
            return owner && created && (!inScene || owner->AddObject(created) == CK_OK);
        },
        [id, levelId, inScene](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKObject *created = doc.Context()->GetObject(id);
            return owner && created && (!inScene || owner->RemoveObject(created) == CK_OK);
        },
        {id}, id);
}

namespace {

CK_OBJECT_SHOWOPTION ShowStateOf(CKObject *object) {
    if (object->IsHierarchicallyHide()) return CKHIERARCHICALHIDE;
    return object->IsVisible() ? CKSHOW : CKHIDE;
}

} // namespace

EditResult ShowObjects(Document &document, const std::vector<CK_ID> &objects, bool show) {
    return ShowObjectsAs(document, objects, show ? CKSHOW : CKHIDE);
}

EditResult ShowObjectsAs(Document &document, const std::vector<CK_ID> &objects, CK_OBJECT_SHOWOPTION option) {
    std::vector<std::pair<CK_ID, CK_OBJECT_SHOWOPTION>> before;
    for (CK_ID id : objects)
        if (CKRenderObject *object = Get<CKRenderObject>(document, id)) before.emplace_back(id, ShowStateOf(object));
    if (before.empty()) return EditResult::Refused("Select 2D or 3D objects");
    const char *label = option == CKSHOW ? "Show in 3D Layout" : option == CKHIDE ? "Hide in 3D Layout" : "Hierarchical Hide";
    return Apply(
        document, label,
        [before, option](Document &doc) {
            for (const auto &entry : before)
                if (CKObject *object = doc.Context()->GetObject(entry.first)) object->Show(option);
            return true;
        },
        [before](Document &doc) {
            for (const auto &entry : before)
                if (CKObject *object = doc.Context()->GetObject(entry.first)) object->Show(entry.second);
            return true;
        });
}

EditResult SetActiveNow(Document &document, const std::vector<CK_ID> &objects, CK_ID sceneId, bool active) {
    CKContext *context = document.Context();
    CKScene *scene = Get<CKScene>(document, sceneId);
    CKLevel *level = context->GetCurrentLevel();
    if (!scene) return EditResult::Refused("No scene");
    std::vector<std::pair<CK_ID, bool>> before;
    for (CK_ID id : objects) {
        CKSceneObject *object = Get<CKSceneObject>(document, id);
        if (!object || object == scene || object == level || !scene->IsObjectHere(object)) continue;
        const bool was = (scene->GetObjectFlags(object) & CK_SCENEOBJECT_ACTIVE) != 0;
        if (was != active) before.emplace_back(id, was);
    }
    if (before.empty()) return EditResult::NoChange("Nothing to change in the scene");
    auto set = [sceneId](Document &doc, CK_ID id, bool on) {
        CKScene *in = Get<CKScene>(doc, sceneId);
        CKSceneObject *object = Get<CKSceneObject>(doc, id);
        if (!in || !object) return false;
        if (on) in->Activate(object, FALSE);
        else in->DeActivate(object);
        return true;
    };
    return Apply(
        document, active ? "Activate Now" : "Deactivate Now",
        [before, active, set](Document &doc) {
            for (const auto &entry : before)
                if (!set(doc, entry.first, active)) return false;
            return true;
        },
        [before, set](Document &doc) {
            for (const auto &entry : before) set(doc, entry.first, entry.second);
            return true;
        });
}

EditResult SetPriorities(Document &document, const std::vector<CK_ID> &objects, int priority) {
    auto priorityOf = [](CKObject *object) {
        if (CKBehavior *behavior = CKBehavior::Cast(object)) return behavior->GetPriority();
        return CKBeObject::Cast(object)->GetPriority();
    };
    std::vector<std::pair<CK_ID, int>> before;
    for (CK_ID id : objects)
        if (CKObject *object = Get<CKBeObject>(document, id) ? document.Context()->GetObject(id) : Get<CKBehavior>(document, id))
            if (priorityOf(object) != priority) before.emplace_back(id, priorityOf(object));
    if (before.empty()) return EditResult::NoChange("The priority is unchanged");
    auto set = [](Document &doc, CK_ID id, int value) {
        CKObject *object = doc.Context()->GetObject(id);
        if (CKBehavior *behavior = CKBehavior::Cast(object)) behavior->SetPriority(value);
        else if (CKBeObject *beObject = CKBeObject::Cast(object)) beObject->SetPriority(value);
        else return false;
        return true;
    };
    return Apply(
        document, "Set Priority",
        [before, priority, set](Document &doc) {
            for (const auto &entry : before)
                if (!set(doc, entry.first, priority)) return false;
            return true;
        },
        [before, set](Document &doc) {
            for (const auto &entry : before) set(doc, entry.first, entry.second);
            return true;
        });
}

int SaveOptionOf(CKObject *object) {
    if (CKTexture *texture = CKTexture::Cast(object)) return texture->GetSaveOptions();
    if (CKSprite *sprite = CKSprite::Cast(object)) return sprite->GetSaveOptions();
    if (CKSound *sound = CKSound::Cast(object)) return sound->GetSaveOptions();
    return -1;
}

namespace {

void ApplySaveOption(CKObject *object, int option) {
    if (CKTexture *texture = CKTexture::Cast(object)) texture->SetSaveOptions(static_cast<CK_BITMAP_SAVEOPTIONS>(option));
    else if (CKSprite *sprite = CKSprite::Cast(object)) sprite->SetSaveOptions(static_cast<CK_BITMAP_SAVEOPTIONS>(option));
    else if (CKSound *sound = CKSound::Cast(object)) sound->SetSaveOptions(static_cast<CK_SOUND_SAVEOPTIONS>(option));
}

} // namespace

EditResult SetSaveOptions(Document &document, const std::vector<CK_ID> &objects,
                          int option) {
    std::vector<std::pair<CK_ID, int>> before;
    for (CK_ID id : objects) {
        CKObject *object = document.Context()->GetObject(id);
        const int current = SaveOptionOf(object);
        if (current >= 0 && current != option) before.emplace_back(id, current);
    }
    if (before.empty()) return EditResult::Refused("Select textures, sprites or sounds with another save option");
    return Apply(
        document, "Set Save Options",
        [before, option](Document &doc) {
            for (const auto &entry : before) ApplySaveOption(doc.Context()->GetObject(entry.first), option);
            return true;
        },
        [before](Document &doc) {
            for (const auto &entry : before) ApplySaveOption(doc.Context()->GetObject(entry.first), entry.second);
            return true;
        });
}

namespace {

std::string TextOf(CKParameter *parameter) {
    std::string text;
    if (!parameter) return text;
    const int size = parameter->GetStringValue(nullptr, FALSE);
    if (size > 0 && size < 65536) {
        text.resize(static_cast<size_t>(size));
        parameter->GetStringValue(&text[0], FALSE);
        text.resize(strlen(text.c_str()));
    }
    return text;
}

// The attribute is set and, when given, takes a value text; its parameter
// joins the composition.
bool GiveAttribute(Document &doc, CK_ID id, CKAttributeType type, const std::string *text) {
    CKBeObject *object = Get<CKBeObject>(doc, id);
    if (!object) return false;
    if (!object->HasAttribute(type) && !object->SetAttribute(type)) return false;
    if (CKParameterOut *value = object->GetAttributeParameter(type)) {
        doc.Include(value->GetID());
        if (text) value->SetStringValue(const_cast<CKSTRING>(text->c_str()));
    }
    return true;
}

} // namespace

EditResult AddAttribute(Document &document, const std::vector<CK_ID> &objects,
                        CKAttributeType type, std::vector<std::pair<std::string, bool>> *each) {
    CKAttributeManager *attributes = document.Context()->GetAttributeManager();
    if (!attributes || !attributes->IsAttributeIndexValid(type)) return EditResult::Refused("Unknown attribute");
    const CK_CLASSID compatible = attributes->GetAttributeCompatibleClassId(type);
    std::vector<CK_ID> targets;
    for (CK_ID id : objects)
        if (CKBeObject *object = Get<CKBeObject>(document, id)) {
            if (object->HasAttribute(type)) continue;
            const bool takes = !compatible || CKIsChildClassOf(object, compatible);
            if (takes) targets.push_back(id);
            if (each) each->push_back({object->GetName() ? object->GetName() : "", takes});
        }
    if (targets.empty()) return EditResult::Refused("No selected object takes this attribute");
    return Apply(
        document, "Add Attributes",
        [targets, type](Document &doc) {
            for (CK_ID id : targets)
                if (!GiveAttribute(doc, id, type, nullptr)) return false;
            return true;
        },
        [targets, type](Document &doc) {
            for (CK_ID id : targets)
                if (CKBeObject *object = Get<CKBeObject>(doc, id)) object->RemoveAttribute(type);
            return true;
        });
}

EditResult RemoveAttribute(Document &document, CK_ID id, CKAttributeType type) {
    CKBeObject *object = Get<CKBeObject>(document, id);
    if (!object || !object->HasAttribute(type)) return EditResult::Refused("The object has no such attribute");
    const bool valued = object->GetAttributeParameter(type) != nullptr;
    const std::string before = TextOf(object->GetAttributeParameter(type));
    return Apply(
        document, "Remove Attribute",
        [id, type](Document &doc) {
            CKBeObject *target = Get<CKBeObject>(doc, id);
            return target && target->RemoveAttribute(type);
        },
        [id, type, before, valued](Document &doc) { return GiveAttribute(doc, id, type, valued ? &before : nullptr); });
}

EditResult RemoveAttributes(Document &document, CK_ID object, const std::vector<CKAttributeType> &types) {
    if (types.empty()) return EditResult::Refused("Select attributes");
    Edit edit(document, "Remove Attribute");
    for (CKAttributeType type : types) {
        const EditResult removed = RemoveAttribute(document, object, type);
        if (!removed.ok) return edit.Fail(removed.message);
    }
    return edit.Commit();
}

EditResult RemoveAttributeFromObjects(Document &document, const std::vector<CK_ID> &objects, CKAttributeType type) {
    if (objects.empty()) return EditResult::Refused("Select objects");
    Edit edit(document, "Remove Attribute from Objects");
    for (CK_ID object : objects) {
        const EditResult removed = RemoveAttribute(document, object, type);
        if (!removed.ok) return edit.Fail(removed.message);
    }
    return edit.Commit();
}

EditResult PasteAttributes(Document &document, CK_ID object, const std::vector<std::string> &names,
                           const std::vector<CK_ID> &values) {
    CKAttributeManager *attributes = document.Context()->GetAttributeManager();
    CKBeObject *target = Get<CKBeObject>(document, object);
    if (!attributes || !target) return EditResult::Refused("Not a behavioral object");
    Edit edit(document, "Paste Attributes");
    std::string refusal;
    for (size_t i = 0; i < names.size() && i < values.size(); ++i) {
        const CKAttributeType type = attributes->GetAttributeTypeByName(const_cast<CKSTRING>(names[i].c_str()));
        if (type < 0) continue;
        if (!target->HasAttribute(type)) {
            const EditResult added = AddAttribute(document, {object}, type);
            if (!added.ok) {
                refusal = added.message;
                continue;
            }
        }
        if (target->GetAttributeParameter(type) && document.Context()->GetObject(values[i]))
            PasteAttributeValue(document, values[i], {object}, type);
    }
    if (edit.Empty()) return edit.Fail(refusal.empty() ? std::string("Nothing to paste!") : refusal);
    return edit.Commit();
}

EditResult SetAttributeText(Document &document, CK_ID id, CKAttributeType type,
                            const std::string &text) {
    CKBeObject *object = Get<CKBeObject>(document, id);
    CKParameterOut *value = object ? object->GetAttributeParameter(type) : nullptr;
    if (!value) return EditResult::Refused("The attribute has no value");
    const std::string before = TextOf(value);
    if (before == text) return EditResult::NoChange("The value is unchanged");
    auto set = [id, type](const std::string &next) {
        return [id, type, next](Document &doc) {
            CKBeObject *target = Get<CKBeObject>(doc, id);
            CKParameterOut *parameter = target ? target->GetAttributeParameter(type) : nullptr;
            return parameter && parameter->SetStringValue(const_cast<CKSTRING>(next.c_str())) == CK_OK;
        };
    };
    return Apply(document, "Edit Attribute", set(text), set(before));
}

EditResult DropAttribute(Document &document, const std::vector<CK_ID> &objects, CKAttributeType type, CK_ID value,
                         std::vector<std::pair<std::string, bool>> *each) {
    CKAttributeManager *attributes = document.Context()->GetAttributeManager();
    if (!attributes || !attributes->IsAttributeIndexValid(type)) return EditResult::Refused("Unknown attribute");
    std::vector<CK_ID> targets;
    for (CK_ID id : objects)
        if (CKBeObject *object = Get<CKBeObject>(document, id))
            if (!CKBehavior::Cast(object) && std::find(targets.begin(), targets.end(), id) == targets.end()) targets.push_back(id);
    if (targets.empty()) return EditResult::Refused("Attributes go on objects");
    Edit edit(document, "Add Attribute");
    std::vector<CK_ID> without;
    for (CK_ID id : targets)
        if (!Get<CKBeObject>(document, id)->HasAttribute(type)) without.push_back(id);
    EditResult added = EditResult::NoChange("The objects have the attribute");
    if (!without.empty()) {
        added = AddAttribute(document, without, type, each);
        if (!added.ok && !value) return edit.Fail(added.message);
    }
    if (value && Get<CKParameter>(document, value)) {
        const EditResult pasted = PasteAttributeValue(document, value, targets, type);
        if (!pasted.ok && !pasted.Unchanged() && !added.ok) return edit.Fail(pasted.message);
        if (!pasted.ok && !pasted.Unchanged()) added.message = pasted.message;
    }
    if (edit.Empty()) return edit.Unchanged(added.message.empty() ? "The objects have the attribute" : added.message);
    return edit.Commit(added.ok ? added.message : std::string());
}

EditResult PasteAttributeValue(Document &document, CK_ID sourceId, const std::vector<CK_ID> &objects,
                               CKAttributeType type) {
    CKParameter *source = Get<CKParameter>(document, sourceId);
    if (!source) return EditResult::Refused("Can't paste values other than parameters!");
    const std::string text = TextOf(source);
    std::vector<std::pair<CK_ID, std::string>> before;
    bool mismatched = false;
    for (CK_ID id : objects) {
        CKBeObject *object = Get<CKBeObject>(document, id);
        CKParameterOut *value = object ? object->GetAttributeParameter(type) : nullptr;
        if (!value || value == source) continue;
        if (value->GetGUID() != source->GetGUID()) {
            mismatched = true;
            continue;
        }
        std::string old = TextOf(value);
        if (old != text) before.emplace_back(id, std::move(old));
    }
    if (before.empty())
        return mismatched ? EditResult::Refused("Parameters are not of same type!") : EditResult::NoChange("The values are unchanged");
    EditResult result = Apply(
        document, "Paste Attribute Value",
        [before, sourceId, text, type](Document &doc) {
            CKParameter *from = Get<CKParameter>(doc, sourceId);
            for (const auto &entry : before) {
                CKBeObject *target = Get<CKBeObject>(doc, entry.first);
                CKParameterOut *parameter = target ? target->GetAttributeParameter(type) : nullptr;
                if (!parameter) return false;
                const CKERROR copied = from && from->GetGUID() == parameter->GetGUID()
                                           ? parameter->CopyValue(from, FALSE)
                                           : parameter->SetStringValue(const_cast<CKSTRING>(text.c_str()));
                if (copied != CK_OK) return false;
            }
            return true;
        },
        [before, type](Document &doc) {
            bool ok = true;
            for (const auto &entry : before) {
                CKBeObject *target = Get<CKBeObject>(doc, entry.first);
                CKParameterOut *parameter = target ? target->GetAttributeParameter(type) : nullptr;
                ok = parameter && parameter->SetStringValue(const_cast<CKSTRING>(entry.second.c_str())) == CK_OK && ok;
            }
            return ok;
        });
    if (result.ok && mismatched) result.message = "Parameters are not of same type!";
    return result;
}

EditResult MoveInGroup(Document &document, CK_ID groupId, CK_ID objectId, int step) {
    CKGroup *group = Get<CKGroup>(document, groupId);
    CKBeObject *object = Get<CKBeObject>(document, objectId);
    if (!group || !object || !object->IsInGroup(group)) return EditResult::Refused("Select an object of a group");
    int at = -1;
    for (int i = 0; i < group->GetObjectCount(); ++i)
        if (group->GetObject(i) == object) at = i;
    if (at + step < 0 || at + step >= group->GetObjectCount()) return EditResult::Refused("The object cannot move further");
    auto move = [groupId, objectId](int direction) {
        return [groupId, objectId, direction](Document &doc) {
            CKGroup *g = Get<CKGroup>(doc, groupId);
            CKBeObject *o = Get<CKBeObject>(doc, objectId);
            if (!g || !o) return false;
            if (direction < 0) g->MoveObjectUp(o);
            else g->MoveObjectDown(o);
            return true;
        };
    };
    return Apply(document, step < 0 ? "Move Up" : "Move Down", move(step), move(-step));
}

bool SaveObjectsAs(Document &document, const std::vector<CK_ID> &objects, const std::string &path,
                   std::string *error, bool hideScripts) {
    return SaveNativeExport(document, objects, path, error, NativeExportKind::Objects, hideScripts);
}

namespace {

// attempted: CKContext::DestroyObjects was called (objects may be gone even
// when it raised an exception).
EditResult DestroyObjects(Document &document, const std::vector<CK_ID> &objects, CKDependencies *dependencies,
                          bool &attempted) {
    attempted = false;
    CKContext *context = document.Context();
    XObjectArray doomed;
    for (CK_ID id : objects) {
        CKObject *object = context->GetObject(id);
        if (!object || CKLevel::Cast(object) || CKScene::Cast(object) || CKBehavior::Cast(object) ||
            CKParameter::Cast(object) || CKParameterIn::Cast(object))
            continue;
        doomed.PushBack(id);
    }
    if (!doomed.Size()) return EditResult::Refused("Select objects to delete (Scripts are deleted from their header)");
    const int count = doomed.Size();
    attempted = true;
    const bool destroyed = GuardedCall([&] { context->DestroyObjects(doomed.Begin(), count, 0, dependencies); },
                                       &document.Host(), "CKContext::DestroyObjects");
    document.MarkTopologyChanged();
    if (!destroyed) return EditResult::Error("CKContext::DestroyObjects raised an exception");
    return EditResult::Applied(std::to_string(count) + " object(s)");
}

} // namespace

EditResult DestroyLevelObjects(Document &document, const std::vector<CK_ID> &objects,
                               CKDependencies *dependencies) {
    bool attempted = false;
    EditResult result = DestroyObjects(document, objects, dependencies, attempted);
    if (attempted) document.History().Clear();
    return result;
}

EditResult SetRenderPriority(Document &document, const std::vector<CK_ID> &objects, int priority) {
    using Priorities = std::vector<std::pair<CK_ID, int>>;
    Priorities before, after;
    for (CK_ID id : objects)
        if (CKRenderObject *object = CKRenderObject::Cast(document.Context()->GetObject(id)))
            if (object->GetZOrder() != priority) {
                before.emplace_back(id, object->GetZOrder());
                after.emplace_back(id, priority);
            }
    if (before.empty()) return EditResult::NoChange();
    auto set = [](const Priorities &values) {
        return [values](Document &doc) {
            for (const auto &value : values) {
                CKRenderObject *object = CKRenderObject::Cast(doc.Context()->GetObject(value.first));
                if (!object) return false;
                object->SetZOrder(value.second);
            }
            return true;
        };
    };
    return Apply(document, "Edit Render Priority", set(after), set(before));
}

// ---------------------------------------------------------------- Attribute Manager

EditResult CreateAttributeCategory(Document &document, const std::string &name) {
    CKAttributeManager *attributes = document.Context()->GetAttributeManager();
    if (!attributes) return EditResult::Refused("No attribute manager");
    if (name.empty()) return EditResult::Refused("Give a name to the category");
    if (attributes->GetCategoryByName(const_cast<CKSTRING>(name.c_str())) >= 0)
        return EditResult::Refused("A category with this name already exists");
    // Dev's categories may be renamed and deleted (Dev.exe sub_594600: flags 3).
    const CKAttributeCategory category =
        attributes->AddCategory(const_cast<CKSTRING>(name.c_str()), CK_ATTRIBUT_CAN_MODIFY | CK_ATTRIBUT_CAN_DELETE);
    if (category < 0) return EditResult::Refused("CKAttributeManager::AddCategory failed");
    document.MarkTopologyChanged();
    return EditResult::Applied();
}

EditResult CreateAttributeType(Document &document, const std::string &name, CKGUID parameter,
                               const std::string &category, CK_CLASSID compatible) {
    CKAttributeManager *attributes = document.Context()->GetAttributeManager();
    if (!attributes) return EditResult::Refused("No attribute manager");
    if (name.empty()) return EditResult::Refused("Give a name to the attribute");
    if (attributes->GetAttributeTypeByName(const_cast<CKSTRING>(name.c_str())) >= 0)
        return EditResult::Refused("An attribute with this name already exists");
    const CKAttributeType type = attributes->RegisterNewAttributeType(
        const_cast<CKSTRING>(name.c_str()), parameter, compatible ? compatible : CKCID_BEOBJECT,
        static_cast<CK_ATTRIBUT_FLAGS>(CK_ATTRIBUT_USER | CK_ATTRIBUT_CAN_MODIFY | CK_ATTRIBUT_CAN_DELETE));
    if (type < 0) return EditResult::Refused("CKAttributeManager::RegisterNewAttributeType failed");
    if (!category.empty()) {
        if (attributes->GetCategoryByName(const_cast<CKSTRING>(category.c_str())) < 0)
            attributes->AddCategory(const_cast<CKSTRING>(category.c_str()), CK_ATTRIBUT_CAN_MODIFY | CK_ATTRIBUT_CAN_DELETE);
        attributes->SetAttributeCategory(type, const_cast<CKSTRING>(category.c_str()));
    }
    document.MarkTopologyChanged();
    return EditResult::Applied(std::string(), static_cast<CK_ID>(type));
}

namespace {

// attempted: the attributes were being unregistered (some may be gone even
// when it raised an exception).
EditResult UnregisterUserAttributes(Document &document, bool &attempted) {
    attempted = false;
    CKAttributeManager *attributes = document.Context()->GetAttributeManager();
    if (!attributes) return EditResult::Refused("No attribute manager");
    std::vector<CKAttributeType> user;
    for (CKAttributeType type = 0; type < attributes->GetAttributeCount(); ++type)
        if (attributes->IsAttributeIndexValid(type) && (attributes->GetAttributeFlags(type) & CK_ATTRIBUT_USER))
            user.push_back(type);
    if (user.empty()) return EditResult::Refused("There is no user attribute");
    bool ok = true;
    attempted = true;
    if (!GuardedCall([&] {
            for (CKAttributeType type : user) attributes->UnRegisterAttribute(type);
        }, &document.Host(), "Remove All User Attributes"))
        ok = false;
    document.MarkTopologyChanged();
    return ok ? EditResult::Applied() : EditResult::Refused("UnRegisterAttribute raised an exception");
}

} // namespace

EditResult RemoveUserAttributes(Document &document) {
    bool attempted = false;
    EditResult result = UnregisterUserAttributes(document, attempted);
    if (attempted) document.History().Clear();
    return result;
}

EditResult RenameAttributeType(Document &document, CKAttributeType type, const std::string &name) {
    CKAttributeManager *attributes = document.Context()->GetAttributeManager();
    if (!attributes || !attributes->IsAttributeIndexValid(type)) return EditResult::Refused("No such attribute");
    if (!(attributes->GetAttributeFlags(type) & CK_ATTRIBUT_CAN_MODIFY)) return EditResult::Refused("This attribute cannot be renamed!");
    if (name.empty()) return EditResult::Refused("Give a name to the attribute");
    const char *current = attributes->GetAttributeNameByType(type);
    if (current && name == current) return EditResult::NoChange("The name is unchanged");
    if (attributes->GetAttributeTypeByName(const_cast<CKSTRING>(name.c_str())) >= 0)
        return EditResult::Refused("Name already used, please enter another name");
    attributes->SetAttributeNameByType(type, const_cast<CKSTRING>(name.c_str()));
    document.MarkTopologyChanged();
    return EditResult::Applied();
}

EditResult RenameAttributeCategory(Document &document, const std::string &category, const std::string &name) {
    CKAttributeManager *attributes = document.Context()->GetAttributeManager();
    const int index = attributes ? attributes->GetCategoryByName(const_cast<CKSTRING>(category.c_str())) : -1;
    if (index < 0) return EditResult::Refused("No such category");
    if (!(attributes->GetCategoryFlags(index) & CK_ATTRIBUT_CAN_MODIFY)) return EditResult::Refused("This category cannot be renamed!");
    if (name.empty()) return EditResult::Refused("Give a name to the category");
    if (name == category) return EditResult::NoChange("The name is unchanged");
    if (attributes->GetCategoryByName(const_cast<CKSTRING>(name.c_str())) >= 0)
        return EditResult::Refused("Name already used, please enter another name");
    attributes->SetCategoryName(index, const_cast<CKSTRING>(name.c_str()));
    document.MarkTopologyChanged();
    return EditResult::Applied();
}

EditResult DeleteAttributeType(Document &document, CKAttributeType type) {
    CKAttributeManager *attributes = document.Context()->GetAttributeManager();
    if (!attributes || !attributes->IsAttributeIndexValid(type)) return EditResult::Refused("No such attribute");
    if (!(attributes->GetAttributeFlags(type) & CK_ATTRIBUT_CAN_DELETE)) return EditResult::Refused("This attribute cannot be deleted!");
    const bool ok = GuardedCall([&] { attributes->UnRegisterAttribute(type); }, &document.Host(), "Delete Attribute Type");
    document.MarkTopologyChanged();
    document.History().Clear();
    return ok ? EditResult::Applied() : EditResult::Refused("UnRegisterAttribute raised an exception");
}

EditResult DeleteAttributeCategory(Document &document, const std::string &category) {
    CKAttributeManager *attributes = document.Context()->GetAttributeManager();
    const int index = attributes ? attributes->GetCategoryByName(const_cast<CKSTRING>(category.c_str())) : -1;
    if (index < 0) return EditResult::Refused("No such category");
    if (!(attributes->GetCategoryFlags(index) & CK_ATTRIBUT_CAN_DELETE)) return EditResult::Refused("This category cannot be deleted!");
    attributes->RemoveCategory(const_cast<CKSTRING>(category.c_str()));
    document.MarkTopologyChanged();
    return EditResult::Applied();
}

// ---------------------------------------------------------------- Hierarchy Manager

namespace {

// An entity's depth in its hierarchy (0: no parent).
int HierarchyDepth(CKObject *object) {
    int depth = 0;
    if (CK3dEntity *entity = CK3dEntity::Cast(object))
        for (CK3dEntity *up = entity->GetParent(); up; up = up->GetParent()) ++depth;
    else if (CK2dEntity *entity2d = CK2dEntity::Cast(object))
        for (CK2dEntity *up = entity2d->GetParent(); up; up = up->GetParent()) ++depth;
    return depth;
}

} // namespace

std::vector<EditResult> SetHierarchyParents(Document &document, const std::vector<CK_ID> &objects, CK_ID parent,
                                            int background) {
    CKContext *context = document.Context();
    for (size_t i = 1; i < objects.size(); ++i)
        if (HierarchyDepth(context->GetObject(objects[i])) != HierarchyDepth(context->GetObject(objects[0])))
            return std::vector<EditResult>(objects.size(),
                                           EditResult::Refused("Cannot move multiple entities with different depths!"));
    EditGroup group(document, "Set Parent");
    std::vector<EditResult> results;
    for (CK_ID object : objects) results.push_back(SetHierarchyParent(document, object, parent, background));
    return results;
}

std::vector<EditResult> CreateScriptsOn(Document &document, const std::vector<CK_ID> &owners) {
    EditGroup group(document, "Create Script");
    std::vector<EditResult> results;
    for (CK_ID owner : owners) results.push_back(CreateScript(document, owner, ""));
    return results;
}

EditResult SetHierarchyParent(Document &document, CK_ID id, CK_ID parentId, int background) {
    CKObject *object = document.Context()->GetObject(id);
    if (CKBodyPart::Cast(object)) return EditResult::Refused("Cannot modify character hierarchy!");
    if (CK3dEntity *entity = CK3dEntity::Cast(object)) {
        CK3dEntity *parent = parentId ? Get<CK3dEntity>(document, parentId) : nullptr;
        if (parentId && !parent) return EditResult::Refused("A 3D entity can only be the child of a 3D entity");
        for (CK3dEntity *up = parent; up; up = up->GetParent())
            if (up == entity) return EditResult::Refused("An object cannot be the child of its own child");
        const CK_ID former = entity->GetParent() ? entity->GetParent()->GetID() : 0;
        if (former == parentId) return EditResult::NoChange("The parent is unchanged");
        auto set = [id](CK_ID to) {
            return [id, to](Document &doc) {
                CK3dEntity *target = Get<CK3dEntity>(doc, id);
                return target && target->SetParent(to ? Get<CK3dEntity>(doc, to) : nullptr, TRUE) != FALSE;
            };
        };
        return Apply(document, "Set Parent", set(parentId), set(former));
    }
    if (CK2dEntity *entity = CK2dEntity::Cast(object)) {
        CK2dEntity *parent = parentId ? Get<CK2dEntity>(document, parentId) : nullptr;
        if (parentId && !parent) return EditResult::Refused("A 2D entity can only be the child of a 2D entity");
        for (CK2dEntity *up = parent; up; up = up->GetParent())
            if (up == entity) return EditResult::Refused("An object cannot be the child of its own child");
        const CK_ID former = entity->GetParent() ? entity->GetParent()->GetID() : 0;
        const bool wasBackground = entity->IsBackground() != FALSE;
        const bool toBackground = parent ? parent->IsBackground() != FALSE : background < 0 ? wasBackground : background != 0;
        if (former == parentId && wasBackground == toBackground) return EditResult::NoChange("The parent is unchanged");
        auto set = [id](CK_ID to, bool back) {
            return [id, to, back](Document &doc) {
                CK2dEntity *target = Get<CK2dEntity>(doc, id);
                if (!target) return false;
                if (!target->SetParent(to ? Get<CK2dEntity>(doc, to) : nullptr)) return false;
                target->SetBackground(back);
                return true;
            };
        };
        return Apply(document, "Set Parent", set(parentId, toBackground), set(former, wasBackground));
    }
    return EditResult::Refused("Only 2D and 3D entities have a hierarchy");
}

// ---------------------------------------------------------------- Level / Scene Setup

SceneEnvironment ReadSceneEnvironment(CKScene *scene) {
    SceneEnvironment environment;
    if (!scene) return environment;
    environment.ambient = scene->GetAmbientLight();
    environment.fogColor = scene->GetFogColor();
    environment.background = scene->GetBackgroundColor();
    environment.fogMode = static_cast<int>(scene->GetFogMode());
    environment.fogStart = scene->GetFogStart();
    environment.fogEnd = scene->GetFogEnd();
    environment.fogDensity = scene->GetFogDensity();
    environment.backgroundTexture = scene->GetBackgroundTexture() ? scene->GetBackgroundTexture()->GetID() : 0;
    environment.startingCamera = scene->GetStartingCamera() ? scene->GetStartingCamera()->GetID() : 0;
    environment.useAtStart = scene->EnvironmentSettings() != FALSE;
    return environment;
}

void ApplySceneEnvironment(CKContext *context, CKScene *scene, const SceneEnvironment &environment) {
    if (!scene) return;
    scene->SetAmbientLight(environment.ambient);
    scene->SetFogColor(environment.fogColor);
    scene->SetBackgroundColor(environment.background);
    scene->SetFogMode(static_cast<VXFOG_MODE>(environment.fogMode));
    scene->SetFogStart(environment.fogStart);
    scene->SetFogEnd(environment.fogEnd);
    scene->SetFogDensity(environment.fogDensity);
    scene->SetBackgroundTexture(CKTexture::Cast(context->GetObject(environment.backgroundTexture)));
    scene->SetStartingCamera(CKCamera::Cast(context->GetObject(environment.startingCamera)));
    scene->UseEnvironmentSettings(environment.useAtStart ? TRUE : FALSE);
}

bool operator==(const SceneEnvironment &a, const SceneEnvironment &b) {
    return a.ambient == b.ambient && a.fogColor == b.fogColor && a.background == b.background && a.fogMode == b.fogMode &&
           a.fogStart == b.fogStart && a.fogEnd == b.fogEnd && a.fogDensity == b.fogDensity &&
           a.backgroundTexture == b.backgroundTexture && a.startingCamera == b.startingCamera && a.useAtStart == b.useAtStart;
}

EditResult CommitSceneEnvironment(Document &document, CK_ID sceneId,
                                  const SceneEnvironment &before, const std::string &label) {
    CKScene *scene = Get<CKScene>(document, sceneId);
    if (!scene) return EditResult::Refused("The scene is gone");
    const SceneEnvironment after = ReadSceneEnvironment(scene);
    if (after == before) return EditResult::NoChange("No change");
    auto set = [sceneId](const SceneEnvironment &environment) {
        return [sceneId, environment](Document &doc) {
            CKScene *target = Get<CKScene>(doc, sceneId);
            if (!target) return false;
            ApplySceneEnvironment(doc.Context(), target, environment);
            return true;
        };
    };
    // Already applied: recorded as done.
    Edit edit(document, label);
    edit.Done([&document, redo = set(after)] { return redo(document); },
              [&document, undoAction = set(before)] { return undoAction(document); });
    return edit.Commit();
}

bool CancelSceneEnvironment(Document &document, CK_ID sceneId, const SceneEnvironment &before) {
    bool restored = false;
    const bool called = GuardedCall([&] {
        CKScene *scene = Get<CKScene>(document, sceneId);
        if (!scene) return;
        ApplySceneEnvironment(document.Context(), scene, before);
        restored = Get<CKScene>(document, sceneId) == scene && ReadSceneEnvironment(scene) == before;
    }, &document.Host(), "Cancel scene environment");
    if (!called || !restored) document.InvalidateHistory();
    document.RefreshScripts();
    return called && restored;
}

EditResult SetSceneEnvironment(Document &document, CK_ID sceneId,
                               const SceneEnvironment &environment, const std::string &label) {
    CKScene *scene = Get<CKScene>(document, sceneId);
    if (!scene) return EditResult::Refused("The scene is gone");
    const SceneEnvironment before = ReadSceneEnvironment(scene);
    ApplySceneEnvironment(document.Context(), scene, environment);
    return CommitSceneEnvironment(document, sceneId, before, label);
}

// ---------------------------------------------------------------- Place links

std::vector<PlaceLink> PlaceLinksOf(CKPlace *place) {
    std::vector<PlaceLink> links;
    for (int i = 0; place && i < place->GetPortalCount(); ++i) {
        CK3dEntity *portal = nullptr;
        CKPlace *other = place->GetPortal(i, &portal);
        links.push_back({other ? other->GetID() : 0, portal ? portal->GetID() : 0});
    }
    return links;
}

EditResult CreateAutomaticPortal(Document &document, CK_ID placeId, CK_ID otherId) {
    CKPlace *place = Get<CKPlace>(document, placeId);
    CKPlace *other = Get<CKPlace>(document, otherId);
    if (!place || !other) return EditResult::Refused("Pick a place");
    if (place == other) return EditResult::Refused("Pick another place");
    VxMatrix box;
    // The 2.1 runtime's ComputeBestFitBBox faults on a place without children.
    bool fit = false;
    if (place->GetChildrenCount() > 0 && other->GetChildrenCount() > 0)
        GuardedCall([&] { fit = place->ComputeBestFitBBox(other, box) != FALSE; }, &document.Host(),
                    "CKPlace::ComputeBestFitBBox");
    const std::string name = std::string("Portal ") + (place->GetName() ? place->GetName() : "") + "-" +
                             (other->GetName() ? other->GetName() : "");
    Edit edit(document, "Automatic Portal");
    const EditResult made = CreateLayoutObject(document, LayoutObject::Portal);
    CK3dEntity *portal = made.ok ? Get<CK3dEntity>(document, made.created) : nullptr;
    if (!portal) return edit.Fail(made.message.empty() ? std::string("The portal cannot be made") : made.message);
    portal->SetName(const_cast<CKSTRING>(name.c_str()));
    if (fit) portal->SetWorldMatrix(box);
    std::vector<PlaceLink> links = PlaceLinksOf(place);
    links.push_back({otherId, portal->GetID()});
    const EditResult linked = SetPlaceLinks(document, placeId, links, "Automatic Portal");
    if (!linked.ok) return edit.Fail(linked.message);
    return edit.Commit(std::string(), portal->GetID());
}

EditResult CreateAutomaticPortals(Document &document) {
    CKLevel *level = document.Context()->GetCurrentLevel();
    if (!level || level->GetPlaceCount() < 2) return EditResult::Refused("The level needs two places");
    std::vector<CK_ID> places;
    for (int i = 0; i < level->GetPlaceCount(); ++i)
        if (CKPlace *place = level->GetPlace(i)) places.push_back(place->GetID());
    Edit edit(document, "Automatic Portals");
    int made = 0;
    for (size_t i = 0; i < places.size(); ++i)
        for (size_t j = i + 1; j < places.size(); ++j) {
            CKPlace *a = Get<CKPlace>(document, places[i]), *b = Get<CKPlace>(document, places[j]);
            if (!a || !b || a->GetChildrenCount() == 0 || b->GetChildrenCount() == 0) continue;
            bool linked = false;
            for (const PlaceLink &link : PlaceLinksOf(a)) linked = linked || link.place == places[j];
            for (const PlaceLink &link : PlaceLinksOf(b)) linked = linked || link.place == places[i];
            if (linked) continue;
            VxMatrix box;
            bool fit = false;
            GuardedCall([&] { fit = a->ComputeBestFitBBox(b, box) != FALSE; }, &document.Host(), "CKPlace::ComputeBestFitBBox");
            if (!fit) continue;
            const EditResult portal = CreateAutomaticPortal(document, places[i], places[j]);
            if (portal.ok) ++made;
        }
    if (!made) return edit.Unchanged("No contiguous places without a portal");
    return edit.Commit(std::to_string(made) + " portal(s) created");
}

EditResult SetPlaceLinks(Document &document, CK_ID placeId, const std::vector<PlaceLink> &links,
                         const std::string &label) {
    CKPlace *place = Get<CKPlace>(document, placeId);
    if (!place) return EditResult::Refused("Select a place");
    const std::vector<PlaceLink> before = PlaceLinksOf(place);
    auto same = [](const std::vector<PlaceLink> &a, const std::vector<PlaceLink> &b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].place != b[i].place || a[i].portal != b[i].portal) return false;
        return true;
    };
    if (same(before, links)) return EditResult::NoChange("No change");
    auto set = [placeId](const std::vector<PlaceLink> &list) {
        return [placeId, list](Document &doc) {
            CKPlace *target = Get<CKPlace>(doc, placeId);
            if (!target) return false;
            for (const PlaceLink &link : PlaceLinksOf(target))
                target->RemovePortal(Get<CKPlace>(doc, link.place), Get<CK3dEntity>(doc, link.portal));
            for (const PlaceLink &link : list)
                if (CKPlace *other = Get<CKPlace>(doc, link.place)) target->AddPortal(other, Get<CK3dEntity>(doc, link.portal));
            return true;
        };
    };
    return Apply(document, label, set(links), set(before));
}

// ---------------------------------------------------------------- Sprite Text

bool ReadSpriteTextFont(CKStateChunk *state, SpriteTextFont *font) {
    if (!state || !font) return false;
    state->StartRead();
    if (!state->SeekIdentifier(CK_STATESAVE_SPRITEFONT)) return false;
    char *face = nullptr;
    state->ReadString(&face);
    font->face = face ? face : "";
    CKDeletePointer(face);
    font->size = state->ReadInt();
    font->weight = state->ReadInt();
    font->italic = state->ReadInt() != 0;
    font->underline = state->ReadInt() != 0;
    return true;
}

bool ReadSpriteTextFont(CKSpriteText *text, SpriteTextFont *font) {
    if (!text || !font) return false;
    CKStateChunk *state = CKSaveObjectState(text);
    const bool found = ReadSpriteTextFont(state, font);
    if (state) DeleteCKStateChunk(state);
    return found;
}


} // namespace vdev
