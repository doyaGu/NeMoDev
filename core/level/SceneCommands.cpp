#include "core/level/SceneCommands.h"

#include <memory>
#include <string>

#include "core/runtime/CkRuntimeHost.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"

namespace vdev {

namespace {

// An owned copy of a state chunk (CKScene takes ownership of what it is given).
struct Chunk {
    CKStateChunk *chunk = nullptr;
    Chunk() = default;
    explicit Chunk(CKStateChunk *source) {
        if (source) chunk = CreateCKStateChunk(source);
    }
    Chunk(const Chunk &) = delete;
    Chunk &operator=(const Chunk &) = delete;
    ~Chunk() {
        if (chunk) DeleteCKStateChunk(chunk);
    }
    CKStateChunk *Copy() const { return chunk ? CreateCKStateChunk(chunk) : nullptr; }
};

struct Entry {
    CK_ID object = 0;
    std::unique_ptr<Chunk> before; // initial value (Set/Remove) or object state (Restore) before
    std::unique_ptr<Chunk> after;
};

std::string Label(InitialConditions action) {
    return action == InitialConditions::Set      ? "Set Initial Conditions"
           : action == InitialConditions::Remove ? "Remove Initial Conditions"
                                                 : "Restore Initial Conditions";
}

// Scene data and possibly restored values: the models and the dirty state follow.
class InitialConditionsAction : public Action {
public:
    InitialConditionsAction(CKScene *scene, InitialConditions action, std::vector<Entry> entries)
        : m_Scene(scene->GetID()), m_SceneInstance(scene), m_Action(action), m_Entries(std::move(entries)) {}
    bool Redo(Document &document) override { return Apply(document, false); }
    bool Undo(Document &document) override { return Apply(document, true); }

private:
    bool Apply(Document &document, bool undo) {
        CKContext *context = document.Context();
        CKScene *scene = CKScene::Cast(context->GetObject(m_Scene));
        if (!scene || scene != m_SceneInstance) return false;
        bool ok = true;
        for (const auto &entry : m_Entries) {
            CKObject *object = context->GetObject(entry.object);
            CKSceneObject *sceneObject = CKSceneObject::Cast(object);
            if (!sceneObject) {
                ok = false;
                continue;
            }
            const Chunk &state = undo ? *entry.before : *entry.after;
            GuardedCall(
                [&] {
                    if (m_Action == InitialConditions::Restore) {
                        if (state.chunk) CKReadObjectState(object, state.chunk);
                    } else {
                        scene->SetObjectInitialValue(sceneObject, state.Copy());
                    }
                },
                &document.Host(), "initial conditions");
        }
        return ok;
    }

    CK_ID m_Scene;
    CKScene *m_SceneInstance;
    InitialConditions m_Action;
    std::vector<Entry> m_Entries;
};

class SceneFlagsAction : public Action {
public:
    SceneFlagsAction(CK_ID scene, std::vector<std::pair<CK_ID, CKDWORD>> before, CKDWORD add, CKDWORD remove)
        : m_Scene(scene), m_Before(std::move(before)), m_Add(add), m_Remove(remove) {}
    bool Redo(Document &document) override { return Apply(document, false); }
    bool Undo(Document &document) override { return Apply(document, true); }

private:
    bool Apply(Document &document, bool undo) {
        CKScene *scene = CKScene::Cast(document.Context()->GetObject(m_Scene));
        if (!scene) return false;
        for (const auto &entry : m_Before)
            if (CKSceneObject *object = CKSceneObject::Cast(document.Context()->GetObject(entry.first))) {
                if (undo) scene->SetObjectFlags(object, static_cast<CK_SCENEOBJECT_FLAGS>(entry.second));
                else scene->ModifyObjectFlags(object, m_Add, m_Remove);
            }
        return true;
    }
    CK_ID m_Scene;
    std::vector<std::pair<CK_ID, CKDWORD>> m_Before;
    CKDWORD m_Add, m_Remove;
};

} // namespace

EditResult ModifySceneFlags(Document &document, const std::vector<CK_ID> &objects,
                            CKDWORD add, CKDWORD remove, const std::string &label, CK_ID sceneId) {
    CKContext *context = document.Context();
    CKScene *scene = sceneId ? CKScene::Cast(context->GetObject(sceneId)) : InitialConditionsScene(context);
    if (!scene) {
        return EditResult::Refused("No level scene");
    }
    std::vector<std::pair<CK_ID, CKDWORD>> before;
    for (CK_ID id : objects) {
        CKObject *object = context->GetObject(id);
        CKSceneObject *sceneObject = CKSceneObject::Cast(object);
        if (!sceneObject || !scene->IsObjectHere(object)) continue;
        const CKDWORD flags = scene->GetObjectFlags(sceneObject);
        if (((flags | add) & ~remove) == flags) continue;
        before.emplace_back(id, flags);
    }
    if (before.empty()) {
        return EditResult::NoChange("Nothing to change in the scene");
    }
    Edit edit(document, label);
    if (!edit.Do(std::make_unique<SceneFlagsAction>(scene->GetID(), std::move(before), add, remove)))
        return edit.Fail("The scene could not be changed");
    return edit.Commit();
}

CKScene *InitialConditionsScene(CKContext *context) {
    CKLevel *level = context ? context->GetCurrentLevel() : nullptr;
    if (!level) return nullptr;
    CKScene *scene = level->GetCurrentScene();
    return scene ? scene : level->GetLevelScene();
}

EditResult ApplyInitialConditions(Document &document, const std::vector<CK_ID> &objects,
                                  InitialConditions action) {
    CKContext *context = document.Context();
    CKScene *scene = InitialConditionsScene(context);
    if (!scene) {
        return EditResult::Refused("No level scene");
    }
    std::vector<Entry> entries;
    for (CK_ID id : objects) {
        CKObject *object = context->GetObject(id);
        CKSceneObject *sceneObject = CKSceneObject::Cast(object);
        if (!sceneObject || !scene->IsObjectHere(object)) continue;
        CKStateChunk *initial = scene->GetObjectInitialValue(sceneObject);
        Entry entry;
        entry.object = id;
        if (action == InitialConditions::Restore) {
            if (!initial) continue;
            CKStateChunk *state = CKSaveObjectState(object);
            entry.before = std::make_unique<Chunk>(state);
            if (state) DeleteCKStateChunk(state);
            entry.after = std::make_unique<Chunk>(initial);
        } else if (action == InitialConditions::Remove) {
            if (!initial) continue;
            entry.before = std::make_unique<Chunk>(initial);
            entry.after = std::make_unique<Chunk>();
        } else {
            entry.before = std::make_unique<Chunk>(initial);
            CKStateChunk *state = CKSaveObjectState(object);
            entry.after = std::make_unique<Chunk>(state);
            if (state) DeleteCKStateChunk(state);
        }
        entries.push_back(std::move(entry));
    }
    if (entries.empty()) {
        return EditResult::Refused(action == InitialConditions::Set ? "No selected object is in the current scene"
                                                          : "No selected object has initial conditions");
    }
    const size_t count = entries.size();
    Edit edit(document, Label(action));
    if (!edit.Do(std::make_unique<InitialConditionsAction>(scene, action, std::move(entries))))
        return edit.Fail(Label(action) + " failed: an object is no longer in the scene");
    return edit.Commit(std::to_string(count) + " object(s)");
}

} // namespace vdev
