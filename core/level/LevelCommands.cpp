#include <filesystem>
#include "core/level/LevelCommands.h"
#include "core/level/HierarchyTree.h"
#include "core/level/LevelActionRecord.h"

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

ObjectRef RecordedObject(Document &document, CKObject *object) {
    return object ? document.Reference(object->GetID()) : ObjectRef{};
}

template <typename T> LevelActionRecord NewRecord(Document &document) { return {document.Token(), std::vector<T>{}}; }

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
class ScriptOwnershipAction final : public Action {
public:
    ScriptOwnershipAction(Document &document, CK_ID owner, const std::vector<CK_ID> &scripts)
        : m_Owner(document.Reference(owner)), m_Scripts(RecordObjects(document, scripts)) {}
    bool Redo(Document &document) override {
        auto *owner = Get<CKBeObject>(document, m_Owner);
        if (!owner) return false;
        m_Added.clear();
        for (auto ref : m_Scripts) {
            auto *script = Get<CKBehavior>(document, ref);
            if (!script || AddScriptLast(owner, script) != CK_OK) return false;
            for (auto sceneId : ScenesMissing(document.Context(), owner, script)) {
                Get<CKScene>(document, sceneId)->AddObjectToScene(script, FALSE);
                m_Added.emplace_back(document.Reference(sceneId), ref);
            }
        }
        return true;
    }
    bool Undo(Document &document) override {
        auto *owner = Get<CKBeObject>(document, m_Owner);
        if (!owner) return false;
        for (const auto &entry : m_Added)
            if (auto *scene = Get<CKScene>(document, entry.first))
                if (auto *script = Get<CKSceneObject>(document, entry.second))
                    scene->RemoveObjectFromScene(script, FALSE);
        for (auto ref : m_Scripts)
            if (auto *script = Get<CKBehavior>(document, ref))
                if (script->GetOwner() == owner)
                    owner->RemoveScript(script->GetID());
        return true;
    }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        KeepObjects(document, objects, m_Owner);
        KeepObjects(document, objects, m_Scripts);
        for (const auto &entry : m_Added) {
            KeepObjects(document, objects, entry.first);
            KeepObjects(document, objects, entry.second);
        }
    }

private:
    ObjectRef m_Owner{};
    std::vector<ObjectRef> m_Scripts;
    std::vector<std::pair<ObjectRef, ObjectRef>> m_Added;
};

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
    return Apply(document, "Create Script",
                 std::make_unique<ScriptOwnershipAction>(document, ownerId, std::vector<CK_ID>{id}), {id}, id);
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
    const std::set<CK_ID> created(objects.begin(), objects.end());
    EditResult result =
        Apply(document, "Load Script", std::make_unique<ScriptOwnershipAction>(document, ownerId, scripts), created,
              scripts.front());
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

namespace {
class CreateSceneAction final : public Action {
public:
    CreateSceneAction(Document &document, CK_ID id, CK_ID levelId, const std::vector<CK_ID> &members)
        : m_Document(document.Token()), m_Object(document.Reference(id)), m_Level(document.Reference(levelId)),
          m_Members(RecordObjects(document, members)) {}
    bool Redo(Document &doc) override {
        if (doc.Token() != m_Document)
            return false;
        const auto id = doc.NativeId(m_Object, Document::ObjectScope::Existing);
        if (m_Object != ObjectRef{} && !id)
            return false;
        const auto levelId = doc.NativeId(m_Level, Document::ObjectScope::Existing);
        if (m_Level != ObjectRef{} && !levelId)
            return false;
        std::vector<CK_ID> members;
        if (!ResolveObjects(doc, m_Members, members))
            return false;
        CKLevel *owner = Get<CKLevel>(doc, levelId);
        CKScene *created = Get<CKScene>(doc, id);
        if (!owner || !created || owner->AddScene(created) != CK_OK)
            return false;
        for (CK_ID member : members)
            if (CKSceneObject *object = Get<CKSceneObject>(doc, member))
                created->AddObjectToScene(object);
        return true;
    }
    bool Undo(Document &doc) override {
        if (doc.Token() != m_Document)
            return false;
        const auto id = doc.NativeId(m_Object, Document::ObjectScope::Existing);
        if (m_Object != ObjectRef{} && !id)
            return false;
        const auto levelId = doc.NativeId(m_Level, Document::ObjectScope::Existing);
        if (m_Level != ObjectRef{} && !levelId)
            return false;
        std::vector<CK_ID> members;
        if (!ResolveObjects(doc, m_Members, members))
            return false;
        CKLevel *owner = Get<CKLevel>(doc, levelId);
        CKScene *created = Get<CKScene>(doc, id);
        if (!owner || !created)
            return false;
        for (CK_ID member : members)
            if (CKSceneObject *object = Get<CKSceneObject>(doc, member))
                created->RemoveObjectFromScene(object);
        return owner->RemoveScene(created) == CK_OK;
    }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        KeepObjects(document, objects, m_Object);
        KeepObjects(document, objects, m_Level);
        KeepObjects(document, objects, m_Members);
    }

private:
    DocumentToken m_Document{};
    ObjectRef m_Object{};
    ObjectRef m_Level{};
    std::vector<ObjectRef> m_Members;
};
} // namespace

EditResult CreateScene(Document &document, const std::string &name, const std::vector<CK_ID> &objects) {
    CKContext *context = document.Context();
    CKLevel *level = context->GetCurrentLevel();
    if (!level)
        return EditResult::Refused("No current level");
    const std::string label = name.empty() ? "New Scene" : name;
    CKScene *scene = CKScene::Cast(context->CreateObject(CKCID_SCENE, const_cast<CKSTRING>(label.c_str())));
    if (!scene)
        return EditResult::Refused("CKContext::CreateObject failed");
    const CK_ID id = scene->GetID(), levelId = level->GetID();
    std::vector<CK_ID> members;
    for (CKSceneObject *object : SceneObjects(document, objects))
        members.push_back(object->GetID());
    return Apply(document, "Create Scene", std::make_unique<CreateSceneAction>(document, id, levelId, members), {id},
                 id);
}

namespace {

std::set<CK_ID> SceneMembers(CKScene *scene) {
    std::set<CK_ID> members;
    for (CKSceneObjectIterator it = scene->GetObjectIterator(); !it.End(); it++) members.insert(it.GetObjectID());
    return members;
}

// What a scene took or lost in a step, and how each was in it.
struct SceneEntry {
    ObjectRef object{};
    CK_SCENEOBJECT_FLAGS flags = CK_SCENEOBJECT_FLAGS(0);
    std::shared_ptr<CKStateChunk> initial;
};

std::shared_ptr<CKStateChunk> CopyChunk(CKStateChunk *chunk) {
    if (!chunk) return nullptr;
    return std::shared_ptr<CKStateChunk>(CreateCKStateChunk(chunk), [](CKStateChunk *c) { DeleteCKStateChunk(c); });
}

class SceneMembershipAction final : public Action {
public:
    SceneMembershipAction(Document &document, CK_ID scene, const std::vector<CK_ID> &members, bool add,
                          bool dependencies)
        : m_Scene(document.Reference(scene)), m_Members(RecordObjects(document, members)), m_Add(add),
          m_Dependencies(dependencies) {}
    bool Redo(Document &document) override {
        auto *target = Get<CKScene>(document, m_Scene);
        if (!target)
            return false;
        std::vector<CK_ID> members;
        if (!ResolveObjects(document, m_Members, members))
            return false;
        if (m_Add) {
            const auto before = SceneMembers(target);
            m_Added.clear();
            for (auto id : members)
                if (auto *object = Get<CKSceneObject>(document, id))
                    target->AddObjectToScene(object, CKGroup::Cast(object) ? m_Dependencies : TRUE);
            for (auto id : SceneMembers(target))
                if (!before.count(id))
                    m_Added.push_back(document.Reference(id));
        } else {
            std::map<CK_ID, SceneEntry> before;
            for (auto id : SceneMembers(target)) {
                auto *object = Get<CKSceneObject>(document, id);
                if (object)
                    before.emplace(id, SceneEntry{document.Reference(id), target->GetObjectFlags(object),
                                                  CopyChunk(target->GetObjectInitialValue(object))});
            }
            m_Removed.clear();
            for (auto id : members)
                if (auto *object = Get<CKSceneObject>(document, id))
                    target->RemoveObjectFromScene(object, m_Dependencies);
            const auto after = SceneMembers(target);
            for (const auto &entry : before)
                if (!after.count(entry.first))
                    m_Removed.push_back(entry.second);
        }
        return true;
    }
    bool Undo(Document &document) override {
        auto *target = Get<CKScene>(document, m_Scene);
        if (!target)
            return false;
        if (m_Add) {
            for (auto ref : m_Added)
                if (auto *object = Get<CKSceneObject>(document, ref))
                    target->RemoveObjectFromScene(object, FALSE);
        } else {
            for (const auto &entry : m_Removed) {
                auto *object = Get<CKSceneObject>(document, entry.object);
                if (!object)
                    continue;
                target->AddObjectToScene(object, FALSE);
                target->SetObjectFlags(object, entry.flags);
                if (entry.initial)
                    target->SetObjectInitialValue(object, CreateCKStateChunk(entry.initial.get()));
            }
        }
        return true;
    }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        KeepObjects(document, objects, m_Scene);
        KeepObjects(document, objects, m_Members);
        KeepObjects(document, objects, m_Added);
        for (const auto &entry : m_Removed)
            KeepObjects(document, objects, entry.object);
    }

private:
    ObjectRef m_Scene{};
    std::vector<ObjectRef> m_Members, m_Added;
    std::vector<SceneEntry> m_Removed;
    bool m_Add, m_Dependencies;
};

} // namespace

EditResult SendToScene(Document &document, const std::vector<CK_ID> &objects, CK_ID sceneId, bool groupMembers) {
    CKScene *scene = Get<CKScene>(document, sceneId);
    if (!scene) return EditResult::Refused("Choose a scene");
    std::vector<CK_ID> members;
    for (CKSceneObject *object : SceneObjects(document, objects))
        if (!scene->IsObjectHere(object)) members.push_back(object->GetID());
    if (members.empty()) return EditResult::Refused("The objects are already in the scene");
    return Apply(document, "Send To Scene",
                 std::make_unique<SceneMembershipAction>(document, sceneId, members, true, groupMembers));
}

EditResult RemoveFromScene(Document &document, const std::vector<CK_ID> &objects, CK_ID sceneId, bool dependencies) {
    CKScene *scene = Get<CKScene>(document, sceneId);
    if (!scene) return EditResult::Refused("Choose a scene");
    std::vector<CK_ID> members;
    for (CKSceneObject *object : SceneObjects(document, objects))
        if (scene->IsObjectHere(object) && !CKScene::Cast(object) && !CKLevel::Cast(object)) members.push_back(object->GetID());
    if (members.empty()) return EditResult::Refused("The objects are not in the scene");
    return Apply(document, "Remove From Scene",
                 std::make_unique<SceneMembershipAction>(document, sceneId, members, false, dependencies));
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

class GroupMembersAction final : public Action {
public:
    GroupMembersAction(Document &document, CK_ID group, const std::vector<CK_ID> &members)
        : m_Group(document.Reference(group)), m_Members(RecordObjects(document, members)) {}
    bool Redo(Document &document) override { return Apply(document, true); }
    bool Undo(Document &document) override { return Apply(document, false); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        KeepObjects(document, objects, m_Group);
        KeepObjects(document, objects, m_Members);
    }

private:
    bool Apply(Document &document, bool add) {
        auto *group = Get<CKGroup>(document, m_Group);
        if (!group) return false;
        for (auto ref : m_Members) {
            auto *object = Get<CKBeObject>(document, ref);
            if (!object)
                return false;
            if (add)
                group->AddObject(object);
            else
                group->RemoveObject(object);
        }
        return true;
    }
    ObjectRef m_Group{};
    std::vector<ObjectRef> m_Members;
};
class CreateGroupAction final : public Action {
public:
    CreateGroupAction(Document &document, CK_ID level, CK_ID group, CK_ID scene, const std::vector<CK_ID> &members,
                      bool selection)
        : m_Level(document.Reference(level)), m_Group(document.Reference(group)), m_Scene(document.Reference(scene)),
          m_Members(document, group, members), m_Selection(selection) {}
    bool Redo(Document &document) override {
        auto *owner = Get<CKLevel>(document, m_Level);
        auto *group = Get<CKGroup>(document, m_Group);
        if (!owner || !group || owner->AddObject(group) != CK_OK || !m_Members.Redo(document))
            return false;
        if (auto *scene = Get<CKScene>(document, m_Scene)) {
            const auto before = SceneMembers(scene);
            scene->AddObjectToScene(group, TRUE);
            m_Added.clear();
            for (auto id : SceneMembers(scene))
                if (!before.count(id))
                    m_Added.push_back(document.Reference(id));
        } else if (m_Selection || m_Scene)
            return false;
        return true;
    }
    bool Undo(Document &document) override {
        auto *owner = Get<CKLevel>(document, m_Level);
        auto *group = Get<CKGroup>(document, m_Group);
        if (!owner || !group)
            return false;
        auto *scene = Get<CKScene>(document, m_Scene);
        // Selection groups historically remove their scene dependencies.
        // Ordinary group creation removes only the members it introduced.
        if (m_Selection) {
            if (!scene || !m_Members.Undo(document))
                return false;
            scene->RemoveObjectFromScene(group, TRUE);
        } else {
            if (scene)
                for (auto ref : m_Added)
                    if (auto *object = Get<CKSceneObject>(document, ref))
                        scene->RemoveObjectFromScene(object, FALSE);
            if (!m_Members.Undo(document))
                return false;
        }
        return owner->RemoveObject(group) == CK_OK;
    }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        for (auto ref : {m_Level, m_Group, m_Scene})
            KeepObjects(document, objects, ref);
        KeepObjects(document, objects, m_Added);
        m_Members.RetainObjects(document, objects);
    }

private:
    ObjectRef m_Level{}, m_Group{}, m_Scene{};
    GroupMembersAction m_Members;
    std::vector<ObjectRef> m_Added;
    bool m_Selection;
};

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
    return Apply(document, "Create Group",
                 std::make_unique<CreateGroupAction>(document, levelId, id, sceneId, members, false), {id}, id);
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
    return Apply(document, "Create Selection Group",
                 std::make_unique<CreateGroupAction>(document, levelId, id, sceneId, members, true), {id}, id);
}

EditResult SendToGroup(Document &document, const std::vector<CK_ID> &objects, CK_ID groupId) {
    CKGroup *group = Get<CKGroup>(document, groupId);
    if (!group) return EditResult::Refused("Choose a group");
    std::vector<CK_ID> members;
    for (CK_ID id : BeObjects(document, objects, groupId))
        if (!Get<CKBeObject>(document, id)->IsInGroup(group)) members.push_back(id);
    if (members.empty()) return EditResult::Refused("The objects are already in the group");
    return Apply(document, "Send To Group", std::make_unique<GroupMembersAction>(document, groupId, members));
}

namespace {
class RemoveGroupMembersAction final : public Action {
public:
    RemoveGroupMembersAction(Document &document, CK_ID groupId, const std::vector<std::pair<int, CK_ID>> &places)
        : m_Document(document.Token()), m_Group(document.Reference(groupId)) {
        for (const auto &entry : places)
            m_Places.emplace_back(entry.first, document.Reference(entry.second));
    }
    bool Redo(Document &doc) override {
        if (doc.Token() != m_Document)
            return false;
        const auto groupId = doc.NativeId(m_Group, Document::ObjectScope::Existing);
        if (m_Group != ObjectRef{} && !groupId)
            return false;
        std::vector<std::pair<int, CK_ID>> places;
        for (const auto &entry : m_Places) {
            const auto id = doc.NativeId(entry.second, Document::ObjectScope::Existing);
            if (entry.second != ObjectRef{} && !id)
                return false;
            places.emplace_back(entry.first, id);
        }
        CKGroup *target = Get<CKGroup>(doc, groupId);
        if (!target)
            return false;
        for (const auto &place : places)
            if (CKBeObject *object = Get<CKBeObject>(doc, place.second))
                target->RemoveObject(object);
        return true;
    }
    bool Undo(Document &doc) override {
        if (doc.Token() != m_Document)
            return false;
        const auto groupId = doc.NativeId(m_Group, Document::ObjectScope::Existing);
        if (m_Group != ObjectRef{} && !groupId)
            return false;
        std::vector<std::pair<int, CK_ID>> places;
        for (const auto &entry : m_Places) {
            const auto id = doc.NativeId(entry.second, Document::ObjectScope::Existing);
            if (entry.second != ObjectRef{} && !id)
                return false;
            places.emplace_back(entry.first, id);
        }
        CKGroup *target = Get<CKGroup>(doc, groupId);
        if (!target)
            return false;
        for (const auto &place : places)
            if (CKBeObject *object = Get<CKBeObject>(doc, place.second))
                target->InsertObjectAt(object, std::min(place.first, target->GetObjectCount()));
        return true;
    }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        KeepObjects(document, objects, m_Group);
        for (const auto &entry : m_Places)
            KeepObjects(document, objects, entry.second);
    }

private:
    DocumentToken m_Document{};
    ObjectRef m_Group{};
    std::vector<std::pair<int, ObjectRef>> m_Places;
};
} // namespace

EditResult RemoveGroupMembers(Document &document, CK_ID groupId, const std::vector<CK_ID> &objects) {
    CKGroup *group = Get<CKGroup>(document, groupId);
    if (!group)
        return EditResult::Refused("Choose a group");
    const std::set<CK_ID> chosen(objects.begin(), objects.end());
    std::vector<std::pair<int, CK_ID>> places; // the members' positions, ascending
    for (int i = 0; i < group->GetObjectCount(); ++i)
        if (CKBeObject *member = group->GetObject(i))
            if (chosen.count(member->GetID()))
                places.push_back({i, member->GetID()});
    if (places.empty())
        return EditResult::Refused("The objects are not in the group");
    return Apply(document, "Remove From Group", std::make_unique<RemoveGroupMembersAction>(document, groupId, places));
}

namespace {

class BodyPartsAction final : public Action {
public:
    BodyPartsAction(Document &document, CK_ID character, const std::vector<CK_ID> &parts, CK_ID root, bool add)
        : m_Character(document.Reference(character)), m_Parts(RecordObjects(document, parts)),
          m_Root(document.Reference(root)), m_Add(add) {}
    bool Redo(Document &document) override { return Apply(document, m_Add, false); }
    bool Undo(Document &document) override { return Apply(document, !m_Add, true); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        KeepObjects(document, objects, m_Character);
        KeepObjects(document, objects, m_Root);
        KeepObjects(document, objects, m_Parts);
    }

private:
    bool Apply(Document &document, bool add, bool restoreRoot) {
        auto *target = Get<CKCharacter>(document, m_Character);
        if (!target)
            return false;
        for (auto ref : m_Parts) {
            auto *part = Get<CKBodyPart>(document, ref);
            if (!part)
                return false;
            if (add)
                target->AddBodyPart(part);
            else
                target->RemoveBodyPart(part);
        }
        if (restoreRoot)
            if (auto *root = Get<CKBodyPart>(document, m_Root))
                if (root->GetCharacter() == target)
                    target->SetRootBodyPart(root);
        return true;
    }
    ObjectRef m_Character{}, m_Root{};
    std::vector<ObjectRef> m_Parts;
    bool m_Add;
};

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
    return Apply(document, add ? "Add Body Part" : "Remove Body Part",
                 std::make_unique<BodyPartsAction>(document, characterId, chosen, root, add));
}

} // namespace

EditResult AddBodyParts(Document &document, CK_ID character, const std::vector<CK_ID> &parts) {
    return BodyParts(document, character, parts, true);
}

EditResult RemoveBodyParts(Document &document, CK_ID character, const std::vector<CK_ID> &parts) {
    return BodyParts(document, character, parts, false);
}

namespace {
class MoveScriptsAction final : public Action {
public:
    MoveScriptsAction(Document &document, CK_ID targetId, const std::vector<std::pair<CK_ID, CK_ID>> &moves)
        : m_Document(document.Token()), m_Target(document.Reference(targetId)) {
        for (const auto &entry : moves)
            m_Moves.emplace_back(document.Reference(entry.first), document.Reference(entry.second));
    }
    bool Redo(Document &doc) override {
        if (doc.Token() != m_Document)
            return false;
        const auto targetId = doc.NativeId(m_Target, Document::ObjectScope::Existing);
        if (m_Target != ObjectRef{} && !targetId)
            return false;
        std::vector<std::pair<CK_ID, CK_ID>> moves;
        for (const auto &entry : m_Moves) {
            const auto first = doc.NativeId(entry.first, Document::ObjectScope::Existing),
                       second = doc.NativeId(entry.second, Document::ObjectScope::Existing);
            if (!first || !second)
                return false;
            moves.emplace_back(first, second);
        }
        CKBeObject *to = Get<CKBeObject>(doc, targetId);
        if (!to)
            return false;
        for (const auto &move : moves) {
            CKBehavior *script = Get<CKBehavior>(doc, move.first);
            CKBeObject *from = Get<CKBeObject>(doc, move.second);
            if (!script || !from || CKMoveScript(from, to, script) != CK_OK)
                return false;
        }
        return true;
    }
    bool Undo(Document &doc) override {
        if (doc.Token() != m_Document)
            return false;
        const auto targetId = doc.NativeId(m_Target, Document::ObjectScope::Existing);
        if (m_Target != ObjectRef{} && !targetId)
            return false;
        std::vector<std::pair<CK_ID, CK_ID>> moves;
        for (const auto &entry : m_Moves) {
            const auto first = doc.NativeId(entry.first, Document::ObjectScope::Existing),
                       second = doc.NativeId(entry.second, Document::ObjectScope::Existing);
            if (!first || !second)
                return false;
            moves.emplace_back(first, second);
        }
        CKBeObject *from = Get<CKBeObject>(doc, targetId);
        if (!from)
            return false;
        for (auto it = moves.rbegin(); it != moves.rend(); ++it) {
            CKBehavior *script = Get<CKBehavior>(doc, it->first);
            CKBeObject *to = Get<CKBeObject>(doc, it->second);
            if (!script || !to || CKMoveScript(from, to, script) != CK_OK)
                return false;
        }
        return true;
    }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        KeepObjects(document, objects, m_Target);
        for (const auto &entry : m_Moves) {
            KeepObjects(document, objects, entry.first);
            KeepObjects(document, objects, entry.second);
        }
    }

private:
    DocumentToken m_Document{};
    ObjectRef m_Target{};
    std::vector<std::pair<ObjectRef, ObjectRef>> m_Moves;
};
} // namespace

EditResult MoveScripts(Document &document, const std::vector<CK_ID> &scripts, CK_ID targetId,
                       std::vector<std::string> *refused) {
    CKBeObject *target = Get<CKBeObject>(document, targetId);
    if (!target || CKBehavior::Cast(target))
        return EditResult::Refused("Choose an object");
    std::vector<std::pair<CK_ID, CK_ID>> moves; // the Script, its owner
    for (CK_ID id : scripts) {
        CKBehavior *script = Get<CKBehavior>(document, id);
        if (!script || !(script->GetFlags() & CKBEHAVIOR_SCRIPT))
            continue;
        CKBeObject *owner = script->GetOwner();
        if (!owner || owner == target)
            continue;
        if (!CKIsChildClassOf(target, script->GetCompatibleClassID())) {
            if (refused)
                refused->push_back(script->GetName() ? script->GetName() : "");
            continue;
        }
        moves.push_back({id, owner->GetID()});
    }
    if (moves.empty())
        return EditResult::Refused("Incompatible class");
    return Apply(document, "Move Script", std::make_unique<MoveScriptsAction>(document, targetId, moves));
}

namespace {

std::vector<ObjectParentChange> EntitiesOf(Document &document, const std::vector<CK_ID> &ids, CK_ID except) {
    std::vector<ObjectParentChange> parents;
    for (auto id : ids) {
        auto *entity = Get<CK3dEntity>(document, id);
        if (!entity || id == except || CKPlace::Cast(entity)) continue;
        const ParentState parent{entity->GetParent() ? document.Reference(entity->GetParent()->GetID()) : ObjectRef{},
                                 false};
        parents.push_back({document.Reference(id), parent, {}});
    }
    return parents;
}
EditResult PlaceObjects(Document &document, const std::string &label, std::vector<ObjectParentChange> parents,
                        CK_ID place) {
    const auto target = document.Reference(place);
    for (auto &entry : parents)
        entry.after.parent = target;
    return ApplyLevelActionRecord(document, label, {document.Token(), std::move(parents)});
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
    Edit edit(document, "Create Place");
    const auto added =
        Apply(document, "Create Place",
              std::make_unique<LevelObjectAction>(document, levelId, id, LevelCollection::Places), {id}, id);
    if (!added.ok)
        return added;
    if (!parents.empty()) {
        const auto moved = PlaceObjects(document, "Send to Place", std::move(parents), id);
        if (!moved.ok)
            return edit.Fail(moved.message);
    }
    return edit.Commit({}, id);
}

EditResult SendToPlace(Document &document, const std::vector<CK_ID> &objects, CK_ID placeId) {
    if (!Get<CKPlace>(document, placeId)) return EditResult::Refused("Choose a place");
    auto parents = EntitiesOf(document, objects, placeId);
    if (parents.empty())
        return EditResult::Refused("Cannot move object on place, can only add object derived from 3D Entity !");
    return PlaceObjects(document, "Send to Place", std::move(parents), placeId);
}

EditResult RemoveFromPlace(Document &document, const std::vector<CK_ID> &objects) {
    auto parents = EntitiesOf(document, objects, 0);
    std::vector<ObjectParentChange> inPlace;
    for (const auto &entry : parents)
        if (entry.before.parent && Get<CKPlace>(document, entry.before.parent))
            inPlace.push_back(entry);
    if (inPlace.empty())
        return EditResult::Refused("The objects are not in a place");
    return PlaceObjects(document, "Remove From Place", std::move(inPlace), 0);
}

EditResult RenameLevelObject(Document &document, ObjectRef reference, const std::string &name) {
    CKObject *object = document.Resolve(reference);
    if (!object) return EditResult::Refused("Select an object");
    const std::string before = object->GetName() ? object->GetName() : "";
    if (before == name) return EditResult::NoChange("The name is unchanged");
    auto record = NewRecord<ObjectNameChange>(document);
    record.Changes<ObjectNameChange>().emplace_back(ObjectNameChange{RecordedObject(document, object), before, name});
    Edit edit(document, "Rename");
    const auto result = ApplyLevelActionRecord(document, "Rename", std::move(record));
    if (!result.ok) return result;
    if (auto *script = OwningScript(object)) edit.Touches(script->GetID());
    return edit.Commit();
}

EditResult CreateArray(Document &document, const std::string &name) {
    CKContext *context = document.Context();
    CKLevel *level = context->GetCurrentLevel();
    if (!level) return EditResult::Refused("No current level");
    const std::string label = name.empty() ? "New Array" : name;
    CKObject *array = context->CreateObject(CKCID_DATAARRAY, const_cast<CKSTRING>(label.c_str()));
    if (!array) return EditResult::Refused("CKContext::CreateObject failed");
    const CK_ID id = array->GetID(), levelId = level->GetID();
    return Apply(document, "Create Array", std::make_unique<LevelObjectAction>(document, levelId, id), {id}, id);
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
    return Apply(document, "Create Object",
                 std::make_unique<LevelObjectAction>(document, levelId, id, LevelCollection::Objects, inScene), {id},
                 id);
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
    auto record = NewRecord<ObjectIntegerChange>(document);
    for (CK_ID id : objects)
        if (auto *object = Get<CKRenderObject>(document, id))
            record.Changes<ObjectIntegerChange>().emplace_back(ObjectIntegerChange{RecordedObject(document, object), ObjectIntegerProperty::Visibility,
                                                            ShowStateOf(object), option});
    if (record.Changes<ObjectIntegerChange>().empty()) return EditResult::Refused("Select 2D or 3D objects");
    const char *label = option == CKSHOW ? "Show in 3D Layout" : option == CKHIDE ? "Hide in 3D Layout" : "Hierarchical Hide";
    return ApplyLevelActionRecord(document, label, std::move(record));
}

namespace {
bool SetSceneActive(Document &doc, CK_ID scene, CK_ID id, bool active) {
    auto *in = Get<CKScene>(doc, scene);
    auto *object = Get<CKSceneObject>(doc, id);
    if (!in || !object)
        return false;
    if (active)
        in->Activate(object, FALSE);
    else
        in->DeActivate(object);
    return true;
}
} // namespace

namespace {
class SceneActivationAction final : public Action {
public:
    SceneActivationAction(Document &document, CK_ID sceneId, const std::vector<std::pair<CK_ID, bool>> &before,
                          const bool &active)
        : m_Document(document.Token()), m_SceneId(document.Reference(sceneId)), m_Active(active) {
        for (const auto &entry : before)
            m_Before.emplace_back(document.Reference(entry.first), entry.second);
    }
    bool Redo(Document &doc) override {
        if (doc.Token() != m_Document)
            return false;
        const auto sceneId = doc.NativeId(m_SceneId, Document::ObjectScope::Existing);
        if (m_SceneId != ObjectRef{} && !sceneId)
            return false;
        std::vector<std::pair<CK_ID, bool>> before;
        for (const auto &entry : m_Before) {
            const auto id = doc.NativeId(entry.first, Document::ObjectScope::Existing);
            if (entry.first != ObjectRef{} && !id)
                return false;
            before.emplace_back(id, entry.second);
        }
        const auto &active = m_Active;
        for (const auto &entry : before)
            if (!SetSceneActive(doc, sceneId, entry.first, active))
                return false;
        return true;
    }
    bool Undo(Document &doc) override {
        if (doc.Token() != m_Document)
            return false;
        const auto sceneId = doc.NativeId(m_SceneId, Document::ObjectScope::Existing);
        if (m_SceneId != ObjectRef{} && !sceneId)
            return false;
        std::vector<std::pair<CK_ID, bool>> before;
        for (const auto &entry : m_Before) {
            const auto id = doc.NativeId(entry.first, Document::ObjectScope::Existing);
            if (entry.first != ObjectRef{} && !id)
                return false;
            before.emplace_back(id, entry.second);
        }
        for (const auto &entry : before)
            SetSceneActive(doc, sceneId, entry.first, entry.second);
        return true;
    }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        KeepObjects(document, objects, m_SceneId);
        for (const auto &entry : m_Before)
            KeepObjects(document, objects, entry.first);
    }

private:
    DocumentToken m_Document{};
    ObjectRef m_SceneId{};
    std::vector<std::pair<ObjectRef, bool>> m_Before;
    bool m_Active;
};
} // namespace

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
    return Apply(document, active ? "Activate Now" : "Deactivate Now",
                 std::make_unique<SceneActivationAction>(document, sceneId, before, active));
}

EditResult SetPriorities(Document &document, const std::vector<CK_ID> &objects, int priority) {
    auto priorityOf = [](CKObject *object) {
        if (CKBehavior *behavior = CKBehavior::Cast(object)) return behavior->GetPriority();
        return CKBeObject::Cast(object)->GetPriority();
    };
    auto record = NewRecord<ObjectIntegerChange>(document);
    for (CK_ID id : objects)
        if (CKObject *object = Get<CKBeObject>(document, id) ? document.Context()->GetObject(id) : Get<CKBehavior>(document, id))
            if (priorityOf(object) != priority)
                record.Changes<ObjectIntegerChange>().emplace_back(ObjectIntegerChange{RecordedObject(document, object), ObjectIntegerProperty::Priority,
                                                                priorityOf(object), priority});
    if (record.Changes<ObjectIntegerChange>().empty()) return EditResult::NoChange("The priority is unchanged");
    return ApplyLevelActionRecord(document, "Set Priority", std::move(record));
}

int SaveOptionOf(CKObject *object) {
    if (CKTexture *texture = CKTexture::Cast(object)) return texture->GetSaveOptions();
    if (CKSprite *sprite = CKSprite::Cast(object)) return sprite->GetSaveOptions();
    if (CKSound *sound = CKSound::Cast(object)) return sound->GetSaveOptions();
    return -1;
}

EditResult SetSaveOptions(Document &document, const std::vector<CK_ID> &objects,
                          int option) {
    auto record = NewRecord<ObjectIntegerChange>(document);
    for (CK_ID id : objects) {
        auto *object = document.Context()->GetObject(id);
        const int current = SaveOptionOf(object);
        if (current >= 0 && current != option)
            record.Changes<ObjectIntegerChange>().emplace_back(ObjectIntegerChange{RecordedObject(document, object), ObjectIntegerProperty::SaveOptions, current, option});
    }
    if (record.Changes<ObjectIntegerChange>().empty()) return EditResult::Refused("Select textures, sprites or sounds with another save option");
    return ApplyLevelActionRecord(document, "Set Save Options", std::move(record));
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

namespace {
class AddAttributeAction final : public Action {
public:
    AddAttributeAction(Document &document, const std::vector<CK_ID> &targets, const CKAttributeType &type)
        : m_Document(document.Token()), m_Targets(RecordObjects(document, targets)), m_Type(type) {}
    bool Redo(Document &doc) override {
        if (doc.Token() != m_Document)
            return false;
        std::vector<CK_ID> targets;
        if (!ResolveObjects(doc, m_Targets, targets))
            return false;
        const auto &type = m_Type;
        for (CK_ID id : targets)
            if (!GiveAttribute(doc, id, type, nullptr))
                return false;
        return true;
    }
    bool Undo(Document &doc) override {
        if (doc.Token() != m_Document)
            return false;
        std::vector<CK_ID> targets;
        if (!ResolveObjects(doc, m_Targets, targets))
            return false;
        const auto &type = m_Type;
        for (CK_ID id : targets)
            if (CKBeObject *object = Get<CKBeObject>(doc, id))
                object->RemoveAttribute(type);
        return true;
    }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        KeepObjects(document, objects, m_Targets);
    }

private:
    DocumentToken m_Document{};
    std::vector<ObjectRef> m_Targets;
    CKAttributeType m_Type;
};
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
    return Apply(document, "Add Attributes", std::make_unique<AddAttributeAction>(document, targets, type));
}

namespace {
class RemoveAttributeAction final : public Action {
public:
    RemoveAttributeAction(Document &document, CK_ID id, const CKAttributeType &type, const std::string &before,
                          const bool &valued)
        : m_Document(document.Token()), m_Object(document.Reference(id)), m_Type(type), m_Before(before),
          m_Valued(valued) {}
    bool Redo(Document &doc) override {
        if (doc.Token() != m_Document)
            return false;
        const auto id = doc.NativeId(m_Object, Document::ObjectScope::Existing);
        if (m_Object != ObjectRef{} && !id)
            return false;
        const auto &type = m_Type;
        CKBeObject *target = Get<CKBeObject>(doc, id);
        return target && target->RemoveAttribute(type);
    }
    bool Undo(Document &doc) override {
        if (doc.Token() != m_Document)
            return false;
        const auto id = doc.NativeId(m_Object, Document::ObjectScope::Existing);
        if (m_Object != ObjectRef{} && !id)
            return false;
        const auto &type = m_Type;
        const auto &before = m_Before;
        const auto &valued = m_Valued;
        return GiveAttribute(doc, id, type, valued ? &before : nullptr);
    }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        KeepObjects(document, objects, m_Object);
    }

private:
    DocumentToken m_Document{};
    ObjectRef m_Object{};
    CKAttributeType m_Type;
    std::string m_Before;
    bool m_Valued;
};
} // namespace

EditResult RemoveAttribute(Document &document, CK_ID id, CKAttributeType type) {
    CKBeObject *object = Get<CKBeObject>(document, id);
    if (!object || !object->HasAttribute(type)) return EditResult::Refused("The object has no such attribute");
    const bool valued = object->GetAttributeParameter(type) != nullptr;
    const std::string before = TextOf(object->GetAttributeParameter(type));
    return Apply(document, "Remove Attribute",
                 std::make_unique<RemoveAttributeAction>(document, id, type, before, valued));
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

namespace {
class AttributeTextAction final : public Action {
public:
    AttributeTextAction(Document &document, CK_ID object, CKAttributeType type, std::string before, std::string after)
        : m_Object(document.Reference(object)), m_Type(type), m_Before(std::move(before)), m_After(std::move(after)) {}
    bool Redo(Document &document) override { return Apply(document, m_After); }
    bool Undo(Document &document) override { return Apply(document, m_Before); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        KeepObjects(document, objects, m_Object);
    }

private:
    bool Apply(Document &document, const std::string &text) {
        auto *owner = Get<CKBeObject>(document, m_Object);
        auto *parameter = owner ? owner->GetAttributeParameter(m_Type) : nullptr;
        return parameter && parameter->SetStringValue(const_cast<CKSTRING>(text.c_str())) == CK_OK;
    }
    ObjectRef m_Object{};
    CKAttributeType m_Type;
    std::string m_Before, m_After;
};
} // namespace

EditResult SetAttributeText(Document &document, CK_ID id, CKAttributeType type,
                            const std::string &text) {
    CKBeObject *object = Get<CKBeObject>(document, id);
    CKParameterOut *value = object ? object->GetAttributeParameter(type) : nullptr;
    if (!value) return EditResult::Refused("The attribute has no value");
    const std::string before = TextOf(value);
    if (before == text) return EditResult::NoChange("The value is unchanged");
    return Apply(document, "Edit Attribute", std::make_unique<AttributeTextAction>(document, id, type, before, text));
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

namespace {
class PasteAttributeValueAction final : public Action {
public:
    PasteAttributeValueAction(Document &document, const std::vector<std::pair<CK_ID, std::string>> &before,
                              CK_ID sourceId, const std::string &text, const CKAttributeType &type)
        : m_Document(document.Token()), m_Source(document.Reference(sourceId)), m_Text(text), m_Type(type) {
        for (const auto &entry : before)
            m_Before.emplace_back(document.Reference(entry.first), entry.second);
    }
    bool Redo(Document &doc) override {
        if (doc.Token() != m_Document)
            return false;
        std::vector<std::pair<CK_ID, std::string>> before;
        for (const auto &entry : m_Before) {
            const auto id = doc.NativeId(entry.first, Document::ObjectScope::Existing);
            if (entry.first != ObjectRef{} && !id)
                return false;
            before.emplace_back(id, entry.second);
        }
        const auto sourceId = doc.NativeId(m_Source, Document::ObjectScope::Existing);
        const auto &text = m_Text;
        const auto &type = m_Type;
        CKParameter *from = Get<CKParameter>(doc, sourceId);
        for (const auto &entry : before) {
            CKBeObject *target = Get<CKBeObject>(doc, entry.first);
            CKParameterOut *parameter = target ? target->GetAttributeParameter(type) : nullptr;
            if (!parameter)
                return false;
            const CKERROR copied = from && from->GetGUID() == parameter->GetGUID()
                                       ? parameter->CopyValue(from, FALSE)
                                       : parameter->SetStringValue(const_cast<CKSTRING>(text.c_str()));
            if (copied != CK_OK)
                return false;
        }
        return true;
    }
    bool Undo(Document &doc) override {
        if (doc.Token() != m_Document)
            return false;
        std::vector<std::pair<CK_ID, std::string>> before;
        for (const auto &entry : m_Before) {
            const auto id = doc.NativeId(entry.first, Document::ObjectScope::Existing);
            if (entry.first != ObjectRef{} && !id)
                return false;
            before.emplace_back(id, entry.second);
        }
        const auto &type = m_Type;
        bool ok = true;
        for (const auto &entry : before) {
            CKBeObject *target = Get<CKBeObject>(doc, entry.first);
            CKParameterOut *parameter = target ? target->GetAttributeParameter(type) : nullptr;
            ok = parameter && parameter->SetStringValue(const_cast<CKSTRING>(entry.second.c_str())) == CK_OK && ok;
        }
        return ok;
    }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        for (const auto &entry : m_Before)
            KeepObjects(document, objects, entry.first);
        KeepObjects(document, objects, m_Source);
    }

private:
    DocumentToken m_Document{};
    std::vector<std::pair<ObjectRef, std::string>> m_Before;
    ObjectRef m_Source{};
    std::string m_Text;
    CKAttributeType m_Type;
};
} // namespace

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
    EditResult result = Apply(document, "Paste Attribute Value",
                              std::make_unique<PasteAttributeValueAction>(document, before, sourceId, text, type));
    if (result.ok && mismatched) result.message = "Parameters are not of same type!";
    return result;
}

namespace {
class GroupOrderAction final : public Action {
public:
    GroupOrderAction(Document &document, CK_ID group, CK_ID object, int step)
        : m_Group(document.Reference(group)), m_Object(document.Reference(object)), m_Step(step) {}
    bool Redo(Document &document) override { return Apply(document, m_Step); }
    bool Undo(Document &document) override { return Apply(document, -m_Step); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        KeepObjects(document, objects, m_Group);
        KeepObjects(document, objects, m_Object);
    }

private:
    bool Apply(Document &document, int direction) {
        auto *group = Get<CKGroup>(document, m_Group);
        auto *object = Get<CKBeObject>(document, m_Object);
        if (!group || !object)
            return false;
        if (direction < 0)
            group->MoveObjectUp(object);
        else
            group->MoveObjectDown(object);
        return true;
    }
    ObjectRef m_Group{}, m_Object{};
    int m_Step;
};
} // namespace

EditResult MoveInGroup(Document &document, CK_ID groupId, CK_ID objectId, int step) {
    CKGroup *group = Get<CKGroup>(document, groupId);
    CKBeObject *object = Get<CKBeObject>(document, objectId);
    if (!group || !object || !object->IsInGroup(group)) return EditResult::Refused("Select an object of a group");
    int at = -1;
    for (int i = 0; i < group->GetObjectCount(); ++i)
        if (group->GetObject(i) == object) at = i;
    if (at + step < 0 || at + step >= group->GetObjectCount()) return EditResult::Refused("The object cannot move further");
    return Apply(document, step < 0 ? "Move Up" : "Move Down",
                 std::make_unique<GroupOrderAction>(document, groupId, objectId, step));
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
    auto record = NewRecord<ObjectIntegerChange>(document);
    for (CK_ID id : objects)
        if (auto *object = Get<CKRenderObject>(document, id))
            if (object->GetZOrder() != priority)
                record.Changes<ObjectIntegerChange>().emplace_back(ObjectIntegerChange{RecordedObject(document, object), ObjectIntegerProperty::RenderPriority,
                                                                object->GetZOrder(), priority});
    if (record.Changes<ObjectIntegerChange>().empty()) return EditResult::NoChange();
    return ApplyLevelActionRecord(document, "Edit Render Priority", std::move(record));
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

std::vector<EditResult> SetHierarchyParents(Document &document, const std::vector<CK_ID> &objects, CK_ID parent,
                                            int background) {
    const auto error = HierarchyDragError(document, objects);
    if (!error.empty()) return std::vector<EditResult>(objects.size(), EditResult::Refused(error));
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
        auto record = NewRecord<ObjectParentChange>(document);
        record.Changes<ObjectParentChange>().emplace_back(ObjectParentChange{RecordedObject(document, object), {RecordedObject(document, entity->GetParent()), false},
                                                       {RecordedObject(document, parent), false}});
        return ApplyLevelActionRecord(document, "Set Parent", std::move(record));
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
        auto record = NewRecord<ObjectParentChange>(document);
        record.Changes<ObjectParentChange>().emplace_back(ObjectParentChange{RecordedObject(document, object), {RecordedObject(document, entity->GetParent()), wasBackground},
                                                       {RecordedObject(document, parent), toBackground}});
        return ApplyLevelActionRecord(document, "Set Parent", std::move(record));
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
    auto record = NewRecord<SceneEnvironmentChange>(document);
    record.Changes<SceneEnvironmentChange>().emplace_back(SceneEnvironmentChange{RecordedObject(document, scene), RecordSceneEnvironment(document, before), RecordSceneEnvironment(document, after)});
    // The gesture has already applied these settings.
    Edit edit(document, label);
    edit.Done(RestoreLevelAction(std::move(record)));
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

EditResult CreateAutomaticPortals(Document &document, std::vector<CK_ID> *created) {
    CKLevel *level = document.Context()->GetCurrentLevel();
    if (!level || level->GetPlaceCount() < 2) return EditResult::Refused("The level needs two places");
    std::vector<CK_ID> places;
    for (int i = 0; i < level->GetPlaceCount(); ++i)
        if (CKPlace *place = level->GetPlace(i)) places.push_back(place->GetID());
    Edit edit(document, "Automatic Portals");
    int made = 0;
    std::vector<CK_ID> portals;
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
            if (portal.ok) { ++made; if (created) portals.push_back(portal.created); }
        }
    if (!made) return edit.Unchanged("No contiguous places without a portal");
    const auto result = edit.Commit(std::to_string(made) + " portal(s) created");
    if (result.ok && created) *created = std::move(portals);
    return result;
}

namespace {
class PlaceLinksAction final : public Action {
public:
    PlaceLinksAction(Document &document, CK_ID place, const std::vector<PlaceLink> &before,
                     const std::vector<PlaceLink> &after)
        : m_Place(document.Reference(place)) {
        for (const auto &link : before)
            m_Before.emplace_back(document.Reference(link.place), document.Reference(link.portal));
        for (const auto &link : after)
            m_After.emplace_back(document.Reference(link.place), document.Reference(link.portal));
    }
    bool Redo(Document &document) override { return Apply(document, m_After); }
    bool Undo(Document &document) override { return Apply(document, m_Before); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        KeepObjects(document, objects, m_Place);
        for (const auto *links : {&m_Before, &m_After})
            for (const auto &link : *links) {
                KeepObjects(document, objects, link.first);
                KeepObjects(document, objects, link.second);
            }
    }

private:
    bool Apply(Document &document, const std::vector<std::pair<ObjectRef, ObjectRef>> &links) {
        auto *target = Get<CKPlace>(document, m_Place);
        if (!target)
            return false;
        for (const auto &link : links) {
            if (!Get<CKPlace>(document, link.first) || (link.second && !Get<CK3dEntity>(document, link.second)))
                return false;
        }
        for (const auto &link : PlaceLinksOf(target))
            target->RemovePortal(Get<CKPlace>(document, link.place), Get<CK3dEntity>(document, link.portal));
        for (const auto &link : links)
            target->AddPortal(Get<CKPlace>(document, link.first), Get<CK3dEntity>(document, link.second));
        return true;
    }
    ObjectRef m_Place{};
    std::vector<std::pair<ObjectRef, ObjectRef>> m_Before, m_After;
};
} // namespace

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
    return Apply(document, label, std::make_unique<PlaceLinksAction>(document, placeId, before, links));
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
