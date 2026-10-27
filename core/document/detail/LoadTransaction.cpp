#include "core/document/detail/LoadTransaction.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

#include "core/document/Document.h"
#include "core/level/ObjectProperties.h"
#include "core/runtime/CkRuntimeHost.h"

namespace vdev {

namespace {
// Keep access to the source SDK's message tables local to the checkpoint.
// Member pointers retain the base type; no downcast of a live manager occurs.
struct MessageTables : CKMessageManager {
    static auto Names() { return &MessageTables::m_RegisteredMessageTypes; }
    static auto Waits() { return &MessageTables::m_MsgWaitingList; }
};
struct SceneSequences : CKScene {
    static auto AddCount() { return &SceneSequences::m_AddObjectCount; }
    static auto RemoveCount() { return &SceneSequences::m_RemoveObjectCount; }
    static auto Added() { return &SceneSequences::m_AddObjectList; }
    static auto Removed() { return &SceneSequences::m_RemoveObjectList; }
};
}

struct LoadTransaction::State {
    using Chunk = std::unique_ptr<CKStateChunk, decltype(&DeleteCKStateChunk)>;
    struct Object {
        CKObject *instance = nullptr;
        std::string name;
        CKDWORD flags = 0;
        CKDWORD behaviorFlags = 0;
        CKSTRING sharedName = nullptr;
        UnkeptValues unkept;
        int addCount = 0, removeCount = 0;
        XObjectArray added, removed;
    };
    struct Manager {
        CKBaseManager *instance;
        Chunk chunk;
    };
    explicit State(CKContext *context)
        : context(context), callback(context->m_UserLoadCallBack), callbackData(context->m_UserLoadCallBackArgs),
          general(context->m_GeneralLoadMode), objects3d(context->m_3DObjectsLoadMode),
          meshes(context->m_MeshLoadMode), materials(context->m_MatTexturesLoadMode),
          inLoad(context->m_InLoad), currentManager(context->m_CurrentManager), currentLevel(context->m_CurrentLevel),
          lastFile(context->m_LastFileLoaded), lastCmo(context->m_LastCmoLoaded),
          productVersion(context->m_PVInformation), writeMode(context->GetFileWriteMode()) {}
    void RestoreCallback() {
        context->SetUserLoadCallback(callback, callbackData);
        context->SetAutomaticLoadMode(general, objects3d, meshes, materials);
    }
    CKContext *context;
    CK_USERLOADCALLBACK callback;
    void *callbackData;
    CK_LOADMODE general, objects3d, meshes, materials;
    CKBOOL inLoad;
    CKBaseManager *currentManager;
    CK_ID currentLevel;
    XString lastFile, lastCmo;
    CKDWORD productVersion;
    CK_FILE_WRITEMODE writeMode;
    // File-mode chunks preserve graph topology and Level membership; the
    // ordinary CKSaveObjectState format deliberately omits those fields.
    std::unique_ptr<CKFile> file;
    std::map<CK_ID, Object> objects;
    std::vector<Manager> managers;
    std::vector<std::unique_ptr<CKAttributeDesc>> attributes;
    std::vector<std::string> defaults, categories;
    std::vector<bool> hasDefault, hasCategory;
    std::vector<CKDWORD> categoryFlags;
    CKStringArray messages;
    std::vector<std::pair<XString, std::vector<XString>>> paths;

    void CaptureRegistries() {
        auto *manager = context->GetAttributeManager();
        for (int i = 0; i < manager->m_AttributeInfoCount; ++i) {
            auto *desc = manager->m_AttributeInfos[i];
            attributes.push_back(desc ? std::make_unique<CKAttributeDesc>(*desc) : nullptr);
            defaults.emplace_back(desc && desc->DefaultValue ? desc->DefaultValue : "");
            hasDefault.push_back(desc && desc->DefaultValue);
            if (attributes.back()) attributes.back()->DefaultValue = nullptr;
        }
        for (int i = 0; i < manager->m_AttributeCategoryCount; ++i) {
            auto *desc = manager->m_AttributeCategories[i];
            categories.emplace_back(desc ? desc->Name : "");
            hasCategory.push_back(desc != nullptr);
            categoryFlags.push_back(desc ? desc->Flags : 0);
        }
        messages = context->GetMessageManager()->*MessageTables::Names();
        auto *pathManager = context->GetPathManager();
        for (int i = 0; i < pathManager->GetCategoryCount(); ++i) {
            XString category;
            pathManager->GetCategoryName(i, category);
            paths.emplace_back(category, std::vector<XString>());
            for (int p = 0; p < pathManager->GetPathCount(i); ++p) {
                XString path;
                pathManager->GetPathName(i, p, path);
                paths.back().second.push_back(path);
            }
        }
    }

    void RestoreRegistries() {
        auto *manager = context->GetAttributeManager();
        // Added attributes may be attached to original objects. Remove them
        // through the SDK before restoring the original descriptor slots.
        for (int i = 0; i < manager->m_AttributeInfoCount; ++i)
            if (static_cast<size_t>(i) >= attributes.size() || !attributes[i]) manager->UnRegisterAttribute(i);
        auto **restoredAttributes = new CKAttributeDesc *[attributes.size()]{};
        for (size_t i = 0; i < attributes.size(); ++i) {
            if (!attributes[i]) continue;
            auto *desc = i < static_cast<size_t>(manager->m_AttributeInfoCount) ? manager->m_AttributeInfos[i] : nullptr;
            if (!desc) desc = new CKAttributeDesc(*attributes[i]);
            else delete[] desc->DefaultValue;
            *desc = *attributes[i];
            desc->DefaultValue = hasDefault[i] ? CKStrdup(defaults[i].c_str()) : nullptr;
            restoredAttributes[i] = desc;
        }
        delete[] manager->m_AttributeInfos;
        manager->m_AttributeInfos = restoredAttributes;
        manager->m_AttributeInfoCount = static_cast<int>(attributes.size());
        auto **restoredCategories = new CKAttributeCategoryDesc *[categories.size()]{};
        for (int i = 0; i < manager->m_AttributeCategoryCount; ++i) {
            auto *desc = manager->m_AttributeCategories[i];
            if (desc) { delete[] desc->Name; delete desc; }
        }
        for (size_t i = 0; i < categories.size(); ++i)
            if (hasCategory[i]) restoredCategories[i] = new CKAttributeCategoryDesc{CKStrdup(categories[i].c_str()), categoryFlags[i]};
        delete[] manager->m_AttributeCategories;
        manager->m_AttributeCategories = restoredCategories;
        manager->m_AttributeCategoryCount = static_cast<int>(categories.size());

        auto *messageManager = context->GetMessageManager();
        auto &names = messageManager->*MessageTables::Names();
        auto **waits = messageManager->*MessageTables::Waits();
        if (waits)
            for (int i = messages.Size(); i < names.Size(); ++i) { delete waits[i]; waits[i] = nullptr; }
        names = messages;
        auto *pathManager = context->GetPathManager();
        while (pathManager->GetCategoryCount()) pathManager->RemoveCategory(pathManager->GetCategoryCount() - 1);
        for (auto &category : paths) {
            const int index = pathManager->AddCategory(category.first);
            for (auto &path : category.second) pathManager->AddPath(index, path);
        }
    }
    std::vector<CK_ID> loaded;
    std::set<CK_ID> created, graveyard;
    std::map<CK_ID, std::pair<ScriptSession *, ScriptSession>> sessions;
    std::uint64_t topology = 0, savedTopology = 0, outside = 0, scriptsRevision = 0;
    std::vector<ScriptInfo> scripts;
};

LoadTransaction::LoadTransaction(Document &document) : m_Document(document) {}

LoadTransaction::~LoadTransaction() {
    if (m_Armed) Rollback(nullptr);
    if (m_State) {
        // CKFile's destructor resets load callbacks and modes too.
        m_State->file.reset();
        m_State->RestoreCallback();
    }
}

bool LoadTransaction::Capture(std::string *error) {
    if (m_Document.EditOpen()) {
        if (error) *error = "Finish the current edit before loading objects";
        return false;
    }
    if (m_Document.Context()->m_InLoad || m_Document.Context()->m_ObjectManager->m_InLoadSession) {
        if (error) *error = "Cannot start another object load while loading";
        return false;
    }
    m_State = std::make_unique<State>(m_Document.Context());
    State &state = *m_State;
    state.loaded = m_Document.m_LoadedObjects;
    state.created = m_Document.m_CreatedObjects;
    state.graveyard = m_Document.m_Graveyard;
    state.topology = m_Document.m_TopologyRevision;
    state.savedTopology = m_Document.m_SavedTopologyRevision;
    state.outside = m_Document.m_ChangesOutsideUndo;
    state.scriptsRevision = m_Document.m_ScriptsRevision;
    state.scripts = m_Document.m_Scripts;
    for (const auto &session : m_Document.m_Sessions)
        state.sessions.emplace(session.first, std::make_pair(session.second.get(), *session.second));
    bool captured = false;
    const bool completed = GuardedCall([&] {
        state.file = std::make_unique<CKFile>(state.context);
        state.file->m_SceneSaved = TRUE;
        state.file->m_ReadFileDataDone = TRUE;
        state.file->m_FileInfo = {};
        state.file->m_FileInfo.ProductVersion = state.context->m_VirtoolsVersion;
        state.file->m_FileInfo.ProductBuild = state.context->m_VirtoolsBuild;
        state.file->m_FileInfo.FileVersion = GetCurrentFileVersion();
        state.file->m_IndexByClassId.Resize(CKGetClassCount());
        state.CaptureRegistries();
        const auto &all = state.context->GetObjectListByType(CKCID_OBJECT, TRUE);
        for (int i = 0; i < all.Size(); ++i) {
            CKObject *object = all[i];
            if (!object) continue;
            State::Object saved;
            saved.instance = object;
            saved.name = object->GetName() ? object->GetName() : "";
            saved.flags = object->GetObjectFlags();
            if (saved.flags & CK_OBJECT_NAMESHARED) saved.sharedName = object->GetName();
            if (auto *behavior = CKBehavior::Cast(object)) saved.behaviorFlags = behavior->GetFlags();
            saved.unkept = ReadUnkeptValues(object);
            if (auto *scene = CKScene::Cast(object)) {
                saved.addCount = scene->*SceneSequences::AddCount();
                saved.removeCount = scene->*SceneSequences::RemoveCount();
                saved.added = scene->*SceneSequences::Added();
                saved.removed = scene->*SceneSequences::Removed();
            }
            state.objects.emplace(object->GetID(), std::move(saved));
            CKFileObject record;
            record.Object = record.CreatedObject = object->GetID();
            record.ObjectCid = object->GetClassID();
            record.ObjPtr = object;
            state.file->m_ObjectsHashTable.Insert(record.Object, state.file->m_FileObjects.Size());
            state.file->m_IndexByClassId[record.ObjectCid].PushBack(state.file->m_FileObjects.Size());
            state.file->m_FileObjects.PushBack(record);
        }
        state.context->SetFileWriteMode(static_cast<CK_FILE_WRITEMODE>(state.writeMode & ~CKFILE_FORVIEWER));
        for (int i = 0; i < state.file->m_FileObjects.Size(); ++i) {
            auto &record = state.file->m_FileObjects[i];
            if (CKIsChildClassOf(record.ObjPtr, CKCID_RENDERCONTEXT)) continue;
            record.Data = record.ObjPtr->Save(state.file.get(), CK_STATESAVE_ALL);
            if (!record.Data) return;
            record.Data->CloseChunk();
        }
        for (int i = 0; i < state.context->GetManagerCount(); ++i) {
            CKBaseManager *manager = state.context->GetManager(i);
            State::Chunk chunk(manager->SaveData(state.file.get()), &DeleteCKStateChunk);
            if (chunk) chunk->CloseChunk();
            state.managers.push_back({manager, std::move(chunk)});
        }
        captured = true;
    }, &m_Document.Host(), "Checkpoint before loading objects");
    state.context->SetFileWriteMode(state.writeMode);
    state.RestoreCallback();
    if (!completed || !captured) {
        if (error) *error = "Cannot capture the current composition; no objects were loaded";
        return false;
    }
    m_Armed = true;
    return true;
}

bool LoadTransaction::Rollback(std::string *error) {
    if (!m_Armed) return true;
    m_Armed = false;
    State &state = *m_State;
    bool restored = true;
    // Return to a normal runtime state before running object/manager readers.
    state.context->m_InLoad = state.inLoad;
    state.context->m_CurrentManager = state.currentManager;
    state.context->m_CurrentLevel = state.currentLevel;
    // FinishLoading normally ends this mapping session. An exception from
    // a manager/object reader can bypass that epilogue; checkpoint IDs are
    // already the original live IDs and must not go through its remapping.
    state.context->m_ObjectManager->EndLoadSession();
    const bool completed = GuardedCall([&] {
        std::vector<CK_ID> added;
        // Deletion callbacks run after removal from per-class lists. A load
        // failing there still leaves those instances in the object table.
        auto *objects = state.context->m_ObjectManager;
        for (int i = 1; i <= objects->m_ObjectCount; ++i)
            if (objects->m_Objects[i] && !state.objects.count(i)) added.push_back(i);
        if (!added.empty()) {
            // Delete exactly the new objects, without a dependency walk that
            // could pull shared original resources into the deletion set.
            state.context->m_ObjectManager->DeleteObjects(added.data(), static_cast<int>(added.size()), 0, 0);
        }
        state.RestoreRegistries();
        for (auto &manager : state.managers)
            if (manager.chunk && manager.instance->LoadData(manager.chunk.get(), state.file.get()) != CK_OK) restored = false;
        for (const auto &saved : state.objects) {
            if (state.context->GetObject(saved.first) != saved.second.instance) { restored = false; continue; }
            if (auto *scene = CKScene::Cast(saved.second.instance)) {
                // A failure can interrupt BeginAdd/RemoveSequence before its
                // matching end, or leave the notification list uncleared.
                scene->*SceneSequences::AddCount() = 0;
                scene->*SceneSequences::RemoveCount() = 0;
                (scene->*SceneSequences::Added()).Clear();
                (scene->*SceneSequences::Removed()).Clear();
            }
        }
        // Parameter and IO readers attach themselves to their owners. Restore
        // the owner's complete arrays afterwards, otherwise replay duplicates
        // those children in an already populated Script/graph.
        std::vector<int> order;
        for (int i = 0; i < state.file->m_FileObjects.Size(); ++i) order.push_back(i);
        auto rank = [&](int i) {
            CKObject *object = state.file->m_FileObjects[i].ObjPtr;
            if (CKLevel::Cast(object)) return 4;
            if (CKScene::Cast(object)) return 3;
            if (CKBeObject::Cast(object)) return 2;
            if (CKBehavior::Cast(object)) return 1;
            return 0;
        };
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return rank(a) < rank(b); });
        for (int i : order) {
            auto &record = state.file->m_FileObjects[i];
            if (state.context->GetObject(record.Object) != record.ObjPtr) { restored = false; continue; }
            // A load checkpoint includes the editor's runtime objects too.
            // Do not replay an unchanged texture or viewport hierarchy: file
            // readers initialize new objects and need not be idempotent on
            // an existing render object. Behavior arrays are rebuilt because
            // parameter/IO readers can attach to them while being restored.
            if (record.Data && !CKBehavior::Cast(record.ObjPtr)) {
                State::Chunk current(record.ObjPtr->Save(state.file.get(), CK_STATESAVE_ALL), &DeleteCKStateChunk);
                if (!current) { restored = false; continue; }
                current->CloseChunk();
                const int size = current->GetDataSize();
                if (size == record.Data->GetDataSize()) {
                    current->StartRead();
                    record.Data->StartRead();
                    if (!size || std::memcmp(current->LockReadBuffer(), record.Data->LockReadBuffer(), static_cast<size_t>(size)) == 0)
                        continue;
                }
            }
            if (auto *behavior = CKBehavior::Cast(record.ObjPtr)) {
                while (behavior->GetInputCount()) behavior->RemoveInput(behavior->GetInputCount() - 1);
                while (behavior->GetOutputCount()) behavior->RemoveOutput(behavior->GetOutputCount() - 1);
                while (behavior->GetInputParameterCount()) behavior->RemoveInputParameter(behavior->GetInputParameterCount() - 1);
                while (behavior->GetOutputParameterCount()) behavior->RemoveOutputParameter(behavior->GetOutputParameterCount() - 1);
                while (behavior->GetLocalParameterCount()) behavior->RemoveLocalParameter(behavior->GetLocalParameterCount() - 1);
                while (behavior->GetSubBehaviorLinkCount()) behavior->RemoveSubBehaviorLink(behavior->GetSubBehaviorLinkCount() - 1);
                while (behavior->GetParameterOperationCount()) behavior->RemoveParameterOperation(behavior->GetParameterOperationCount() - 1);
                while (behavior->GetSubBehaviorCount()) behavior->RemoveSubBehavior(behavior->GetSubBehaviorCount() - 1);
            }
            if (record.Data && record.ObjPtr->Load(record.Data, state.file.get()) != CK_OK) restored = false;
        }
        for (const auto &saved : state.objects) {
            CKObject *object = state.context->GetObject(saved.first);
            if (object != saved.second.instance) { restored = false; continue; }
            object->SetName(saved.second.sharedName ? saved.second.sharedName : const_cast<CKSTRING>(saved.second.name.c_str()),
                            saved.second.sharedName ? TRUE : FALSE);
            object->ModifyObjectFlags(saved.second.flags, ~saved.second.flags);
            if (auto *behavior = CKBehavior::Cast(object)) behavior->SetFlags(static_cast<CK_BEHAVIOR_FLAGS>(saved.second.behaviorFlags));
            if (auto *scene = CKScene::Cast(object)) {
                scene->*SceneSequences::AddCount() = saved.second.addCount;
                scene->*SceneSequences::RemoveCount() = saved.second.removeCount;
                scene->*SceneSequences::Added() = saved.second.added;
                scene->*SceneSequences::Removed() = saved.second.removed;
            }
            for (const auto &restore : saved.second.unkept.restore) restore(object);
        }
    }, &m_Document.Host(), "Roll back failed object load");
    state.context->m_InLoad = state.inLoad;
    state.context->m_CurrentManager = state.currentManager;
    state.context->m_CurrentLevel = state.currentLevel;
    state.context->m_LastFileLoaded = state.lastFile;
    state.context->m_LastCmoLoaded = state.lastCmo;
    state.context->m_PVInformation = state.productVersion;
    state.context->SetFileWriteMode(state.writeMode);
    state.RestoreCallback();
    m_Document.m_LoadedObjects = std::move(state.loaded);
    m_Document.m_CreatedObjects = std::move(state.created);
    m_Document.m_Graveyard = std::move(state.graveyard);
    m_Document.m_TopologyRevision = state.topology;
    m_Document.m_SavedTopologyRevision = state.savedTopology;
    m_Document.m_ChangesOutsideUndo = state.outside;
    m_Document.m_ScriptsRevision = state.scriptsRevision;
    m_Document.m_Scripts = std::move(state.scripts);
    for (auto it = m_Document.m_Sessions.begin(); it != m_Document.m_Sessions.end();)
        if (!state.sessions.count(it->first)) it = m_Document.m_Sessions.erase(it);
        else ++it;
    for (const auto &session : state.sessions) {
        auto found = m_Document.m_Sessions.find(session.first);
        if (found == m_Document.m_Sessions.end() || found->second.get() != session.second.first) restored = false;
        else *found->second = session.second.second;
    }
    if (!completed || !restored) {
        const std::string message = "Restoring the original composition was incomplete; see Event Log";
        if (error) *error += "; " + message;
        m_Document.Host().Log(LogLevel::Error, message);
        // A runtime refusing its own checkpoint must never leave a false
        // clean state or enable replay against a partly restored graph.
        m_Document.InvalidateHistory();
    }
    return completed && restored;
}

void LoadTransaction::Commit() { m_Armed = false; }

} // namespace vdev
