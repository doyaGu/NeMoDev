#include <filesystem>
#include "core/document/Document.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/document/Edit.h"
#include "core/document/SpriteTextPersistence.h"
#include "core/runtime/CkArrays.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/SdkCompat.h"

namespace vdev {

namespace {

std::string NameOf(CKObject *object) {
    return object && object->GetName() ? object->GetName() : std::string();
}

// The objects a load brought, each once and none of listed: the file's in
// file order (the array CKFile::Load returns omits some: Find Path.cmo, a
// group referenced only by an object parameter value; saving it alone would
// drop them), then the array's (objects: null, none).
void CollectLoaded(CKFile *file, CKObjectArray *objects, std::set<CK_ID> &listed, std::vector<CK_ID> &loaded) {
    for (int i = 0; file && i < file->m_FileObjects.Size(); ++i)
        if (CKObject *object = file->m_FileObjects[i].ObjPtr)
            if (listed.insert(object->GetID()).second) loaded.push_back(object->GetID());
    if (!objects) return;
    for (objects->Reset(); !objects->EndOfList(); objects->Next())
        if (listed.insert(objects->GetDataId()).second) loaded.push_back(objects->GetDataId());
}

// What holds a behavior (its parent graph), a parameter, an operation or a
// behavior I/O; null for the rest (a Script is a root).
CKObject *HolderOf(CKObject *object) {
    if (CKBehavior *behavior = CKBehavior::Cast(object)) return behavior->GetParent();
    if (CKParameterIn *input = CKParameterIn::Cast(object)) return input->GetOwner();
    if (CKParameter *parameter = CKParameter::Cast(object)) return parameter->GetOwner();
    if (CKParameterOperation *operation = CKParameterOperation::Cast(object)) return operation->GetOwner();
    if (CKBehaviorIO *io = CKBehaviorIO::Cast(object)) return io->GetOwner();
    return nullptr;
}

// The Script a behavior, a parameter or a behavior I/O belongs to (0: none).
CK_ID OwningScript(CKObject *object) {
    CKBehavior *top = nullptr;
    for (int depth = 0; object && depth < 256; ++depth, object = HolderOf(object))
        if (CKBehavior *behavior = CKBehavior::Cast(object)) top = behavior;
    return top && (top->GetType() & CKBEHAVIORTYPE_SCRIPT) ? top->GetID() : 0;
}

} // namespace

SceneEnvironment DefaultNewSceneEnvironment() {
    SceneEnvironment environment;
    environment.ambient = 0x0F0F0F;
    environment.background = 0xFF808080;
    environment.fogStart = 1.0f;
    environment.fogEnd = 100.0f;
    environment.fogDensity = 1.0f;
    return environment;
}

Document::~Document() {
    Close();
}

CKContext *Document::Context() const {
    return m_Host.Context();
}

Document::PreparedOpen::PreparedOpen(Document &target) : m_Target(target) {}
Document::PreparedOpen::~PreparedOpen() = default;

void Document::PreparedOpen::Commit() {
    if (m_Document) m_Target.CommitOpen(*this);
}

std::unique_ptr<Document::PreparedOpen> Document::PrepareOpen(const std::string &path, std::string *error,
                                                            const SceneEnvironment &environment) {
    if (EditOpen()) {
        if (error) *error = "Finish the current edit before opening a composition";
        return {};
    }
    auto prepared = std::unique_ptr<PreparedOpen>(new PreparedOpen(*this));
    prepared->m_Host = m_Host.CreateDocumentHost(error);
    if (!prepared->m_Host) return {};
    prepared->m_Document = std::make_unique<Document>(*prepared->m_Host);
    bool loaded = false;
    const bool completed = GuardedCall([&] {
        loaded = prepared->m_Document->Open(path, error, environment);
    }, &m_Host, "Prepare composition");
    if (!completed || !loaded) {
        if (!completed && error) *error = "Cannot prepare " + path + ": the runtime raised an exception";
        return {};
    }
    return prepared;
}

void Document::CommitOpen(PreparedOpen &prepared) {
    Close();
    m_Host.ExchangeContext(*prepared.m_Host);
    Document &candidate = *prepared.m_Document;
    m_Open = candidate.m_Open;
    m_SourcePath = std::move(candidate.m_SourcePath);
    m_LoadedObjects = std::move(candidate.m_LoadedObjects);
    m_Scripts = std::move(candidate.m_Scripts);
    m_Sessions = std::move(candidate.m_Sessions);
    m_Graveyard = std::move(candidate.m_Graveyard);
    m_CreatedObjects = std::move(candidate.m_CreatedObjects);
    m_Bitmaps = std::move(candidate.m_Bitmaps);
    m_TopologyRevision = candidate.m_TopologyRevision;
    m_SavedTopologyRevision = candidate.m_SavedTopologyRevision;
    m_ChangesOutsideUndo = candidate.m_ChangesOutsideUndo;
    ++m_ScriptsRevision;
    candidate.m_Open = false; // Its context now belongs to this document.
    prepared.m_Document.reset();
}

bool Document::Open(const std::string &path, std::string *error, const SceneEnvironment &environment) {
    // NMO files are object libraries, even when they contain a saved level.
    // Opening one is the editor's shortcut for New Composition followed by
    // Import: retain the new scene's settings and save the result as a new
    // composition rather than overwriting the source object library.
    const std::string extension = std::filesystem::u8path(path).extension().u8string();
    if (_stricmp(extension.c_str(), ".nmo") == 0) {
        if (!New(error, environment)) return false;
        if (Import(path, ImportMode::File, error, nullptr, nullptr, environment)) return true;
        Close();
        return false;
    }
    Close();
    CKContext *context = Context();
    if (!context) {
        if (error) *error = "Virtools runtime is not started";
        return false;
    }

    CKObjectArray *objects = CreateCKObjectArray();
    CKFile *file = context->CreateCKFile();
    CKERROR result = CKERR_INVALIDFILE;
    const bool completed = GuardedCall(
        [&] {
            result = file->Load(const_cast<CKSTRING>(std::filesystem::u8path(path).string().c_str()), objects, CK_LOAD_DEFAULT);
            if (result == CK_OK) RestoreSpriteTextAlignments(*file);
        },
        &m_Host, "CKFile::Load");
    if (!completed || result != CK_OK) {
        context->DeleteCKFile(file);
        DeleteCKObjectArray(objects);
        m_Host.ClearAll();
        if (error) *error = "Cannot load " + path + " (CKERROR " + std::to_string(result) + ")";
        return false;
    }
    // Every object of the file (CollectLoaded).
    std::set<CK_ID> listed;
    CollectLoaded(file, objects, listed, m_LoadedObjects);
    context->DeleteCKFile(file);
    DeleteCKObjectArray(objects);

    m_SourcePath = path;
    m_Open = true;
    RefreshScripts();
    m_Host.Log(LogLevel::Info, "Opened " + path + ": " + std::to_string(m_LoadedObjects.size()) + " objects, " +
                                   std::to_string(m_Scripts.size()) + " Scripts");
    return true;
}

namespace {

// Defaults are supplied by the document's caller, including imported legacy
// preferences. The core does not access process settings or the registry.
void ApplyNewSceneEnvironment(CKScene *scene, CkRuntimeHost &host,
                              const SceneEnvironment &environment, bool apply = true) {
    ApplySceneEnvironment(host.Context(), scene, environment);
    if (apply) GuardedCall([&] { scene->ApplyEnvironmentSettings(); }, &host, "CKScene::ApplyEnvironmentSettings");
}

bool IsVirtoolsFile(const std::string &path) {
    const size_t dot = path.find_last_of('.');
    const std::string extension = dot == std::string::npos ? std::string() : path.substr(dot + 1);
    return _stricmp(extension.c_str(), "nmo") == 0 || _stricmp(extension.c_str(), "cmo") == 0 ||
           _stricmp(extension.c_str(), "vmo") == 0 || _stricmp(extension.c_str(), "nms") == 0;
}

// GetFileTitle without the extension (Dev names the new place or scene with
// GetFileTitleA, which drops it when Explorer hides extensions).
std::string FileTitle(const std::string &path) {
    const size_t slash = path.find_last_of("\\/");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

} // namespace

bool Document::New(std::string *error, const SceneEnvironment &environment) {
    Close();
    CKContext *context = Context();
    if (!context) {
        if (error) *error = "Virtools runtime is not started";
        return false;
    }
    CKLevel *level = CKLevel::Cast(context->CreateObject(CKCID_LEVEL, (CKSTRING) "Level"));
    if (!level) {
        if (error) *error = "Cannot create the level";
        return false;
    }
    context->SetCurrentLevel(level);
    // Dev adds its 3D Layout render context; the editor has one only if the host made it.
    CKRenderManager *renderer = context->GetRenderManager();
    if (renderer && renderer->GetRenderContextCount() > 0) level->AddRenderContext(renderer->GetRenderContext(0), TRUE);
    if (CKScene *scene = level->GetLevelScene()) ApplyNewSceneEnvironment(scene, m_Host, environment);
    m_LoadedObjects.push_back(level->GetID());
    m_Open = true;
    RefreshScripts();
    m_Host.Log(LogLevel::Info, "New composition");
    return true;
}

bool Document::Merge(const std::string &path, bool asScene, std::string *error, std::vector<CK_ID> *merged,
                     const std::vector<DuplicateName> *duplicates) {
    CKContext *context = Context();
    CKLevel *current = context ? context->GetCurrentLevel() : nullptr;
    if (!m_Open || !current) {
        if (error) *error = "No composition is open";
        return false;
    }
    // Dev loads with CK_LOAD_GEOMETRY | CK_LOAD_DODIALOG: the duplicate names
    // are asked about first (FindDuplicateNames), the choices applied here.
    CKObjectArray *objects = CreateCKObjectArray();
    CKFile *file = context->CreateCKFile();
    CKERROR result = CKERR_INVALIDFILE;
    const bool completed = GuardedCall(
        [&] {
            result = LoadFileChoosingDuplicates(context, file, path, objects, CK_LOAD_DEFAULT, duplicates);
            if (result == CK_OK) RestoreSpriteTextAlignments(*file, duplicates);
        },
        &m_Host, "CKFile::Load");
    std::vector<CK_ID> loaded;
    std::set<CK_ID> listed(m_LoadedObjects.begin(), m_LoadedObjects.end());
    if (completed && result == CK_OK) CollectLoaded(file, objects, listed, loaded);
    context->DeleteCKFile(file);
    DeleteCKObjectArray(objects);
    if (!completed || result != CK_OK) {
        if (error) *error = "Cannot load " + path + " (CKERROR " + std::to_string(result) + ")";
        return false;
    }
    // The loaded level: its scenes and objects join the current level, then
    // it goes (Dev destroys it after CKLevel::Merge).
    std::vector<CK_ID> levels;
    for (CK_ID id : loaded) {
        CKLevel *level = CKLevel::Cast(context->GetObject(id));
        if (!level || level == current) continue;
        // As a new scene (CK2 CKLevel::Merge): the level's Scripts move to its
        // level scene, which becomes the new scene "Merged Scene"; but
        // CKScene::Merge, merging that scene into the current level scene,
        // moves them on to the level scene, where they are lost when the file
        // is loaded again. They are given back to the new scene.
        std::vector<CK_ID> scripts;
        CKScene *scene = asScene ? level->GetLevelScene() : nullptr;
        for (int i = 0; scene && i < level->GetScriptCount(); ++i)
            if (CKBehavior *script = level->GetScript(i)) scripts.push_back(script->GetID());
        GuardedCall([&] { current->Merge(level, asScene); }, &m_Host, "CKLevel::Merge");
        CKScene *levelScene = current->GetLevelScene();
        for (CK_ID scriptId : scripts) {
            CKBehavior *script = CKBehavior::Cast(context->GetObject(scriptId));
            if (!script || !scene || !levelScene || script->GetOwner() != levelScene) continue;
            levelScene->RemoveScript(scriptId);
            AddScriptLast(scene, script);
        }
        levels.push_back(id);
    }
    for (CK_ID id : levels)
        if (CKObject *level = context->GetObject(id))
            GuardedCall([&] { context->DestroyObject(level); }, &m_Host, "CKContext::DestroyObject");
    for (CK_ID id : loaded)
        if (context->GetObject(id)) {
            m_LoadedObjects.push_back(id);
            if (merged) merged->push_back(id);
        }
    RefreshScripts();
    MarkTopologyChanged();
    m_Host.Log(LogLevel::Info, "Merged " + path + (asScene ? " as a new scene: " : ": ") +
                                   std::to_string(loaded.size() - levels.size()) + " objects");
    return true;
}

bool Document::Import(const std::string &path, ImportMode mode, std::string *error,
                      std::vector<CK_ID> *imported, const std::vector<DuplicateName> *duplicates,
                      const SceneEnvironment &environment) {
    CKContext *context = Context();
    CKLevel *current = context ? context->GetCurrentLevel() : nullptr;
    if (!m_Open || !current) {
        if (error) *error = "No composition is open";
        return false;
    }
    // Dev: CK_LOAD_ANIMATION | CK_LOAD_GEOMETRY | CK_LOAD_DODIALOG, plus
    // CK_LOAD_ASCHARACTER for a character; the duplicate names' choices are
    // given (FindDuplicateNames) or the duplicates kept.
    const CK_LOAD_FLAGS flags =
        static_cast<CK_LOAD_FLAGS>(CK_LOAD_DEFAULT | (mode == ImportMode::Character ? CK_LOAD_ASCHARACTER : 0));
    CKObjectArray *objects = CreateCKObjectArray();
    CKERROR result = CKERR_INVALIDFILE;
    std::vector<CK_ID> loaded;
    std::set<CK_ID> listed(m_LoadedObjects.begin(), m_LoadedObjects.end());
    bool completed = false;
    if (IsVirtoolsFile(path)) {
        // As Open and Merge: the file's object list also has the objects the
        // returned array omits.
        CKFile *file = context->CreateCKFile();
        completed = GuardedCall(
            [&] {
                result = LoadFileChoosingDuplicates(context, file, path, objects, flags, duplicates);
                if (result == CK_OK) RestoreSpriteTextAlignments(*file, duplicates);
            },
            &m_Host, "CKFile::Load");
        if (completed && result == CK_OK) CollectLoaded(file, nullptr, listed, loaded); // the array below
        context->DeleteCKFile(file);
    } else {
        // Other formats go through their model reader.
        completed = GuardedCall(
            [&] { result = context->Load(const_cast<CKSTRING>(std::filesystem::u8path(path).string().c_str()), objects, flags); }, &m_Host,
            "CKContext::Load");
    }
    std::vector<CK_ID> array;
    if (completed && result == CK_OK)
        for (objects->Reset(); !objects->EndOfList(); objects->Next()) {
            array.push_back(objects->GetDataId());
            if (listed.insert(objects->GetDataId()).second) loaded.push_back(objects->GetDataId());
        }
    DeleteCKObjectArray(objects);
    if (!completed || result != CK_OK) {
        if (error) *error = "Cannot load " + path + " (CKERROR " + std::to_string(result) + ")";
        return false;
    }
    // Dev leaves a level of the file in the context; it would be saved as a
    // second level, so it goes (its objects stay and join the current level).
    std::vector<CK_ID> levels;
    for (CK_ID id : loaded)
        if (CKLevel *level = CKLevel::Cast(context->GetObject(id)))
            if (level != current) levels.push_back(id);
    const std::string title = FileTitle(path);
    if (mode == ImportMode::Place) {
        // A place named after the file, parent of the loaded 3D entities
        // (CK3dEntity::AddChildren keeps their own hierarchy).
        CKPlace *place = CKPlace::Cast(context->CreateObject(CKCID_PLACE, const_cast<CKSTRING>(title.c_str())));
        if (place) {
            current->AddPlace(place);
            XObjectPointerArray entities;
            for (CK_ID id : array)
                if (CKObject *object = context->GetObject(id))
                    if (CKIsChildClassOf(object, CKCID_3DENTITY)) entities.PushBack(object);
            GuardedCall([&] { place->AddChildren(entities, TRUE); }, &m_Host, "CK3dEntity::AddChildren");
            loaded.push_back(place->GetID());
        }
    } else if (mode == ImportMode::Scene) {
        // A scene named after the file with Dev's new-scene environment,
        // holding the loaded scene objects.
        CKScene *scene = CKScene::Cast(context->CreateObject(CKCID_SCENE, const_cast<CKSTRING>(title.c_str())));
        if (scene) {
            ApplyNewSceneEnvironment(scene, m_Host, environment, false);
            current->AddScene(scene);
            for (CK_ID id : array)
                if (CKSceneObject *object = CKSceneObject::Cast(context->GetObject(id))) scene->AddObjectToScene(object, TRUE);
            loaded.push_back(scene->GetID());
        }
    }
    // Every loaded scene, place, object and animation joins the level.
    current->BeginAddSequence(TRUE);
    for (CK_ID id : array) {
        CKObject *object = context->GetObject(id);
        if (!object || object == current || CKIsChildClassOf(object, CKCID_LEVEL)) continue;
        if (CKScene *scene = CKScene::Cast(object)) current->AddScene(scene);
        else if (CKPlace *place = CKPlace::Cast(object)) current->AddPlace(place);
        else if (CKIsChildClassOf(object, CKCID_BEOBJECT) || CKIsChildClassOf(object, CKCID_ANIMATION))
            current->AddObject(object);
    }
    current->BeginAddSequence(FALSE);
    for (CK_ID id : levels)
        if (CKObject *level = context->GetObject(id))
            GuardedCall([&] { context->DestroyObject(level); }, &m_Host, "CKContext::DestroyObject");
    int count = 0;
    for (CK_ID id : loaded)
        if (context->GetObject(id)) {
            m_LoadedObjects.push_back(id);
            if (imported) imported->push_back(id);
            ++count;
        }
    RefreshScripts();
    MarkTopologyChanged();
    static const char *const modes[] = {"", " as a character", " as a place", " as a scene"};
    m_Host.Log(LogLevel::Info, "Imported " + path + modes[static_cast<int>(mode)] + ": " + std::to_string(count) +
                                   " objects");
    return true;
}

void Document::AdoptObjects(const std::vector<CK_ID> &objects) {
    std::set<CK_ID> listed(m_LoadedObjects.begin(), m_LoadedObjects.end());
    for (CK_ID id : objects)
        if (listed.insert(id).second) m_LoadedObjects.push_back(id);
    RefreshScripts();
    MarkTopologyChanged();
}

void Document::KeepBitmap(void *bitmap) {
    if (bitmap) m_Bitmaps.emplace_back(bitmap, [](void *handle) { VxDeleteBitmap(handle); });
}

void Document::Close() {
    // The edits being built and the steps go before the objects they hold
    // (the graveyard is destroyed below).
    for (Edit *edit = m_OpenEdit; edit;) {
        Edit *outer = edit->m_Parent;
        edit->Abandon();
        edit = outer;
    }
    m_OpenEdit = nullptr;
    m_History.Clear();
    // Soft-deleted objects are detached but still name their former parent
    // and owner. Destroy them together while those still exist, before
    // ClearAll (a batch lets CK resolve references among them).
    if (m_Open && Context() && !m_Graveyard.empty()) {
        CKContext *context = Context();
        std::vector<CK_ID> doomed;
        for (CK_ID id : m_Graveyard)
            if (context->GetObject(id)) doomed.push_back(id);
        if (!doomed.empty() &&
            !GuardedCall([&] { context->DestroyObjects(doomed.data(), static_cast<int>(doomed.size())); }, &m_Host,
                         "CKContext::DestroyObjects"))
            m_Host.Log(LogLevel::Warning, "Discarding deleted Schematic objects raised an exception");
    }
    for (const auto &entry : m_Sessions) m_ChangesOutsideUndo += entry.second->ChangesOutsideUndo();
    m_Sessions.clear();
    m_Bitmaps.clear(); // after the sessions borrowing them
    m_Scripts.clear();
    m_LoadedObjects.clear();
    m_Graveyard.clear();
    m_CreatedObjects.clear();
    m_SourcePath.clear();
    m_TopologyRevision = m_SavedTopologyRevision = 0;
    if (m_Open && Context()) m_Host.ClearAll();
    m_Open = false;
}

bool Document::IsLive(CK_ID id) const {
    CKContext *context = Context();
    return context && context->GetObject(id) && !IsDiscarded(id);
}

std::vector<CKObject *> Document::LiveObjects(CK_CLASSID cid, bool derived) const {
    std::vector<CKObject *> objects;
    CKContext *context = Context();
    if (!context) return objects;
    const XObjectPointerArray &list = context->GetObjectListByType(cid, derived ? TRUE : FALSE);
    for (int i = 0; i < list.Size(); ++i)
        if (CKObject *object = list[i])
            if (!IsDiscarded(object->GetID())) objects.push_back(object);
    return objects;
}

bool Document::IsDiscarded(CK_ID id) const {
    if (m_Graveyard.count(id)) return true;
    if (m_Graveyard.empty()) return false;
    CKContext *context = Context();
    CKObject *object = context ? context->GetObject(id) : nullptr;
    for (int depth = 0; object && depth < 256; ++depth, object = HolderOf(object))
        if (m_Graveyard.count(object->GetID())) return true;
    return false;
}

bool Document::InvalidateHistory() {
    const bool any = m_History.CanUndo() || m_History.CanRedo();
    m_History.Clear();
    MarkTopologyChanged();
    return any;
}

void Document::RefreshScripts() {
    const std::vector<ScriptInfo> before = std::move(m_Scripts);
    m_Scripts.clear();
    ListScripts();
    auto same = [](const ScriptInfo &a, const ScriptInfo &b) {
        return a.id == b.id && a.ownerId == b.ownerId && a.name == b.name && a.ownerName == b.ownerName;
    };
    if (!std::equal(before.begin(), before.end(), m_Scripts.begin(), m_Scripts.end(), same)) ++m_ScriptsRevision;
}

void Document::ListScripts() {
    CKContext *context = Context();
    if (!context) return;
    // Destroyed Scripts (Level Manager Delete) leave with their sessions.
    for (auto it = m_Sessions.begin(); it != m_Sessions.end();) {
        if (context->GetObject(it->first)) {
            ++it;
            continue;
        }
        m_ChangesOutsideUndo += it->second->ChangesOutsideUndo(); // the count does not go back
        it = m_Sessions.erase(it);
    }
    // Group by owner in load order, then by the owner's own Script order as
    // the Level Manager lists them.
    std::vector<CK_ID> owners;
    std::vector<CK_ID> orphanScripts;
    const int count = context->GetObjectsCountByClassID(CKCID_BEHAVIOR);
    const CK_ID *ids = context->GetObjectsListByClassID(CKCID_BEHAVIOR);
    for (int i = 0; i < count; ++i) {
        CKBehavior *behavior = CKBehavior::Cast(context->GetObject(ids[i]));
        if (!behavior || !(behavior->GetType() & CKBEHAVIORTYPE_SCRIPT)) continue;
        if (m_Graveyard.count(behavior->GetID())) continue;
        CKBeObject *owner = behavior->GetOwner();
        if (!owner) {
            orphanScripts.push_back(behavior->GetID());
        } else if (std::find(owners.begin(), owners.end(), owner->GetID()) == owners.end()) {
            owners.push_back(owner->GetID());
        }
    }
    auto add = [&](CKBehavior *script, CKBeObject *owner) {
        ScriptInfo info;
        info.id = script->GetID();
        info.name = NameOf(script);
        info.ownerId = owner ? owner->GetID() : 0;
        info.ownerName = NameOf(owner);
        info.hasInterfaceChunk = script->GetInterfaceChunk() != nullptr;
        m_Scripts.push_back(info);
    };
    for (CK_ID ownerId : owners) {
        CKBeObject *owner = CKBeObject::Cast(context->GetObject(ownerId));
        for (int i = 0; owner && i < owner->GetScriptCount(); ++i) {
            CKBehavior *script = owner->GetScript(i);
            if (script && !m_Graveyard.count(script->GetID())) add(script, owner);
        }
    }
    for (CK_ID id : orphanScripts) add(CKBehavior::Cast(context->GetObject(id)), nullptr);
}

const ScriptInfo *Document::FindScript(CK_ID id) const {
    for (const auto &script : m_Scripts)
        if (script.id == id) return &script;
    return nullptr;
}

ScriptSession *Document::ExistingSession(CK_ID scriptId) {
    auto found = m_Sessions.find(scriptId);
    return found != m_Sessions.end() ? found->second.get() : nullptr;
}

ScriptSession *Document::Session(CK_ID scriptId) {
    if (ScriptSession *existing = ExistingSession(scriptId)) return existing;
    if (!FindScript(scriptId)) return nullptr;
    auto session = std::make_unique<ScriptSession>();
    // Read-only, or rebuilt from the runtime graph instead of a cleared or
    // unreadable InterfaceChunk: the log says why.
    session->Open(Context(), scriptId);
    if (!session->LoadMessage().empty())
        m_Host.Log(LogLevel::Warning, FindScript(scriptId)->name + ": " + session->LoadMessage());
    ScriptSession *result = session.get();
    m_Sessions.emplace(scriptId, std::move(session));
    return result;
}

bool Document::Dirty() const {
    if (TopologyDirty()) return true;
    for (const auto &entry : m_Sessions)
        if (entry.second->Dirty()) return true;
    return false;
}

std::uint64_t Document::ChangesOutsideUndo() const {
    std::uint64_t changes = m_ChangesOutsideUndo;
    for (const auto &entry : m_Sessions) changes += entry.second->ChangesOutsideUndo();
    return changes;
}

void Document::MarkSaved() {
    for (auto &entry : m_Sessions) entry.second->MarkSaved();
    m_SavedTopologyRevision = m_TopologyRevision;
}

std::vector<DuplicateName> Document::FindDuplicateNames(const std::string &path) const {
    std::vector<DuplicateName> found;
    CKContext *context = Context();
    if (!context || !IsVirtoolsFile(path)) return found;
    CKFile *file = context->CreateCKFile();
    CKERROR result = CKERR_INVALIDFILE;
    // The headers only, then the duplicates as CK2 2.5's
    // CKFile::CheckDuplicateNames finds them (the 2.1 runtime lists none): a
    // file object of a class checking names, named as an object of its class
    // in the context, not one used automatically.
    GuardedCall([&] { result = file->OpenFile(const_cast<CKSTRING>(std::filesystem::u8path(path).string().c_str()), CK_LOAD_DEFAULT); }, &m_Host,
                "CKFile::OpenFile");
    if (result == CK_OK || result == CKERR_PLUGINSMISSING)
        for (int index = 0; index < file->m_FileObjects.Size(); ++index) {
            const CKFileObject &object = file->m_FileObjects[index];
            const CKClassDesc *description = CKGetClassDesc(object.ObjectCid);
            if (!object.Name || !*object.Name || !description ||
                (description->DefaultOptions & (CK_GENERALOPTIONS_NODUPLICATENAMECHECK | CK_GENERALOPTIONS_AUTOMATICUSECURRENT)))
                continue;
            if (!context->GetObjectByNameAndClass(object.Name, object.ObjectCid, nullptr)) continue;
            DuplicateName duplicate;
            duplicate.fileIndex = index;
            duplicate.cid = object.ObjectCid;
            duplicate.name = object.Name;
            found.push_back(duplicate);
        }
    context->DeleteCKFile(file);
    return found;
}

} // namespace vdev
