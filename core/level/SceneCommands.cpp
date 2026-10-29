#include "core/level/SceneCommands.h"

#include <memory>
#include <set>
#include <string>

#include "core/runtime/CkRuntimeHost.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"

namespace vdev {

// An owned copy of a state chunk (CKScene takes ownership of what it is given).
struct InitialConditionChunk {
    CKStateChunk *chunk = nullptr;
    InitialConditionChunk() = default;
    explicit InitialConditionChunk(CKStateChunk *source) {
        if (source) chunk = CreateCKStateChunk(source);
    }
    InitialConditionChunk(const InitialConditionChunk &) = delete;
    InitialConditionChunk &operator=(const InitialConditionChunk &) = delete;
    ~InitialConditionChunk() {
        if (chunk) DeleteCKStateChunk(chunk);
    }
    CKStateChunk *Copy() const { return chunk ? CreateCKStateChunk(chunk) : nullptr; }
};

struct InitialConditionEntry {
    ObjectRef object{};
    std::unique_ptr<InitialConditionChunk> before; // initial value (Set/Remove) or object state (Restore) before
    std::unique_ptr<InitialConditionChunk> after;
};

static std::string Label(InitialConditions action) {
    return action == InitialConditions::Set      ? "Set Initial Conditions"
           : action == InitialConditions::Remove ? "Remove Initial Conditions"
                                                 : "Restore Initial Conditions";
}

// Scene data and possibly restored values: the models and the dirty state follow.
class InitialConditionsAction : public Action {
public:
    InitialConditionsAction(Document &document, CKScene *scene, InitialConditions action,
                            std::vector<InitialConditionEntry> entries)
        : m_Scene(document.Reference(scene->GetID())), m_Action(action), m_Entries(std::move(entries)) {}
    bool Redo(Document &document) override { return Apply(document, false); }
    bool Undo(Document &document) override { return Apply(document, true); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        if (auto id = document.NativeId(m_Scene, Document::ObjectScope::Existing)) objects.insert(id);
        for (const auto &entry : m_Entries)
            if (auto id = document.NativeId(entry.object, Document::ObjectScope::Existing)) objects.insert(id);
    }

private:
    CKScene *Scene(Document &document) const {
        return CKScene::Cast(document.Resolve(m_Scene, CKCID_SCENE, Document::ObjectScope::Existing));
    }
    bool Valid(Document &document, const InitialConditionEntry &entry) const {
        CKScene *scene = Scene(document);
        auto *object = document.Resolve(entry.object, CKCID_SCENEOBJECT);
        return scene && object && scene->IsObjectHere(object);
    }
    bool Write(Document &document, InitialConditionEntry &entry, bool undo) {
        bool written = false;
        std::unique_ptr<CKStateChunk, decltype(&DeleteCKStateChunk)> copy(nullptr, &DeleteCKStateChunk);
        return GuardedCall([&] {
            if (!Valid(document, entry)) return;
            CKScene *scene = Scene(document);
            auto *object = static_cast<CKSceneObject *>(document.Resolve(entry.object, CKCID_SCENEOBJECT));
            const InitialConditionChunk &state = undo ? *entry.before : *entry.after;
            if (m_Action == InitialConditions::Restore) {
                written = state.chunk && CKReadObjectState(object, state.chunk) == CK_OK;
            } else {
                copy.reset(state.Copy());
                if (state.chunk && !copy) return;
                CKStateChunk *expected = copy.get();
                if (!scene->SetObjectInitialValue(object, expected)) return;
                copy.release(); // The scene owns the installed copy, including a null initial value.
                if (!Valid(document, entry)) return;
                written = scene->GetObjectInitialValue(object) == expected;
            }
            written = written && Valid(document, entry);
        }, &document.Host(), "Apply initial conditions") && written;
    }
    bool Apply(Document &document, bool undo) {
        // Validate the complete selection before the first write.
        for (const auto &entry : m_Entries)
            if (!Valid(document, entry)) return false;
        for (size_t i = 0; i < m_Entries.size(); ++i)
            if (!Write(document, m_Entries[i], undo)) {
                bool restored = true;
                // This action owns every partial write, including the current
                // object. Edit only rolls back other actions in the undo step.
                for (size_t j = i + 1; j-- > 0;)
                    restored = Write(document, m_Entries[j], !undo) && restored;
                if (!restored) {
                    document.Host().Log(LogLevel::Error, "Initial condition recovery was incomplete: the undo history is cleared");
                    if (document.Token() == m_Scene.document) document.InvalidateHistory();
                }
                return false;
            }
        return true;
    }
    ObjectRef m_Scene;
    InitialConditions m_Action;
    std::vector<InitialConditionEntry> m_Entries;
};

class SceneFlagsAction : public Action {
public:
    SceneFlagsAction(ObjectRef scene, std::vector<std::pair<ObjectRef, CKDWORD>> before, CKDWORD add, CKDWORD remove)
        : m_Scene(scene), m_Before(std::move(before)), m_Add(add), m_Remove(remove) {}
    bool Redo(Document &document) override { return Apply(document, false); }
    bool Undo(Document &document) override { return Apply(document, true); }
    void RetainObjects(const Document &document, std::set<CK_ID> &objects) const override {
        if (auto id = document.NativeId(m_Scene, Document::ObjectScope::Existing)) objects.insert(id);
        for (const auto &entry : m_Before)
            if (auto id = document.NativeId(entry.first, Document::ObjectScope::Existing)) objects.insert(id);
    }

private:
    bool Apply(Document &document, bool undo) {
        CKScene *scene = CKScene::Cast(document.Resolve(m_Scene, CKCID_SCENE, Document::ObjectScope::Existing));
        if (!scene) return false;
        for (const auto &entry : m_Before)
            if (!document.Resolve(entry.first, CKCID_SCENEOBJECT, Document::ObjectScope::Existing)) return false;
        for (const auto &entry : m_Before)
            if (CKSceneObject *object = CKSceneObject::Cast(document.Resolve(entry.first, CKCID_SCENEOBJECT, Document::ObjectScope::Existing))) {
                if (undo) scene->SetObjectFlags(object, static_cast<CK_SCENEOBJECT_FLAGS>(entry.second));
                else scene->ModifyObjectFlags(object, m_Add, m_Remove);
            }
        return true;
    }
    ObjectRef m_Scene;
    std::vector<std::pair<ObjectRef, CKDWORD>> m_Before;
    CKDWORD m_Add, m_Remove;
};

EditResult ModifySceneFlags(Document &document, const std::vector<CK_ID> &objects,
                            CKDWORD add, CKDWORD remove, const std::string &label, CK_ID sceneId) {
    CKContext *context = document.Context();
    CKScene *scene = sceneId ? CKScene::Cast(context->GetObject(sceneId)) : InitialConditionsScene(context);
    if (!scene) {
        return EditResult::Refused("No level scene");
    }
    std::vector<std::pair<ObjectRef, CKDWORD>> before;
    for (CK_ID id : objects) {
        CKObject *object = context->GetObject(id);
        CKSceneObject *sceneObject = CKSceneObject::Cast(object);
        if (!sceneObject || !scene->IsObjectHere(object)) continue;
        const CKDWORD flags = scene->GetObjectFlags(sceneObject);
        if (((flags | add) & ~remove) == flags) continue;
        before.emplace_back(document.Reference(id), flags);
    }
    if (before.empty()) {
        return EditResult::NoChange("Nothing to change in the scene");
    }
    Edit edit(document, label);
    if (!edit.Do(std::make_unique<SceneFlagsAction>(document.Reference(scene->GetID()), std::move(before), add, remove)))
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
    std::vector<InitialConditionEntry> entries;
    std::set<CK_ID> seen;
    const auto sceneRef = document.Reference(scene->GetID());
    for (CK_ID id : objects) {
        CKObject *object = context->GetObject(id);
        CKSceneObject *sceneObject = CKSceneObject::Cast(object);
        if (!sceneObject || !document.IsLive(id) || !scene->IsObjectHere(object) || !seen.insert(id).second) continue;
        InitialConditionEntry entry;
        entry.object = document.Reference(id);
        bool captured = false, skipped = false;
        std::unique_ptr<CKStateChunk, decltype(&DeleteCKStateChunk)> state(nullptr, &DeleteCKStateChunk);
        const bool called = GuardedCall([&] {
            CKStateChunk *initial = scene->GetObjectInitialValue(sceneObject);
            if (!initial && action != InitialConditions::Set) { skipped = true; return; }
            // Copy the scene's existing value before an object's Save callback
            // can replace it. Absence is valid; failure to copy a value is not.
            auto previous = std::make_unique<InitialConditionChunk>(initial);
            if (initial && !previous->chunk) return;
            if (action == InitialConditions::Remove) {
                entry.before = std::move(previous);
                entry.after = std::make_unique<InitialConditionChunk>();
            } else {
                state.reset(CKSaveObjectState(object));
                if (!state) return;
                auto current = std::make_unique<InitialConditionChunk>(state.get());
                if (!current->chunk) return;
                entry.before = action == InitialConditions::Restore ? std::move(current) : std::move(previous);
                entry.after = action == InitialConditions::Restore ? std::move(previous) : std::move(current);
            }
            captured = document.Resolve(entry.object) == object &&
                       document.Resolve(sceneRef, CKCID_SCENE, Document::ObjectScope::Existing) == scene && scene->IsObjectHere(object);
        }, &document.Host(), "Capture initial conditions");
        if (called && skipped) continue;
        if (!called || !captured) return EditResult::Error("Cannot capture the objects' initial conditions");
        entries.push_back(std::move(entry));
    }
    if (entries.empty()) {
        return EditResult::Refused(action == InitialConditions::Set ? "No selected object is in the current scene"
                                                          : "No selected object has initial conditions");
    }
    const size_t count = entries.size();
    Edit edit(document, Label(action));
    if (!edit.Do(std::make_unique<InitialConditionsAction>(document, scene, action, std::move(entries)))) {
        edit.Fail(Label(action) + " failed");
        return EditResult::Error(Label(action) + " failed; check the runtime log for recovery failures");
    }
    return edit.Commit(std::to_string(count) + " object(s)");
}

} // namespace vdev
