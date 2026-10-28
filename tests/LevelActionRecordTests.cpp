#include "VirtoolsDevTestSupport.h"
#include "core/level/LevelActionRecord.h"

#include <stdexcept>

namespace vdevtest {
namespace {
ActionObject Ref(CKObject *object) { return {object->GetID(), object->GetClassID()}; }
template <typename T = ObjectNameChange> LevelActionRecord Record(Document &document) { return {document.InstanceId(), document.Host().Generation(), std::vector<T>{}}; }
std::string Name(CKObject *object) { return object->GetName() ? object->GetName() : ""; }
int Visibility(CKObject *object) { return object->IsHierarchicallyHide() ? CKHIERARCHICALHIDE : object->IsVisible() ? CKSHOW : CKHIDE; }

// A real SDK object with an injectable virtual setter. The record interpreter
// and native lookup/verification/recovery all use production code.
class ReplayObject : public CKObject {
public:
    explicit ReplayObject(CKContext *context) : CKObject(context, "Record replay failure") { CKObject::Show(CKSHOW); }
    int writes = 0, throwAt = 0, rejectAt = 0, refuseAt = 0;
    void Show(CK_OBJECT_SHOWOPTION value) override {
        ++writes;
        if (writes == refuseAt) throw std::runtime_error("Recovery refused before writing");
        CKObject::Show(value);
        if (writes == throwAt) throw std::runtime_error("Setter failed after writing");
        if (writes == rejectAt) CKObject::Show(value == CKSHOW ? CKHIDE : CKSHOW);
    }
    void Reset() { writes = throwAt = rejectAt = refuseAt = 0; CKObject::Show(CKSHOW); }
};

LevelActionRecord VisibilityRecord(Document &document, CKObject *a, CKObject *b) {
    auto record = Record<ObjectIntegerChange>(document);
    for (auto *object : {a, b})
        record.Changes<ObjectIntegerChange>().emplace_back(ObjectIntegerChange{Ref(object), ObjectIntegerProperty::Visibility, CKSHOW, CKHIDE});
    return record;
}
void Bury(Document &document, const std::vector<CK_ID> &objects) {
    Edit edit(document, "Discard record fixtures");
    for (auto id : objects) edit.Bury(id);
    CHECK(edit.Commit().ok);
}
} // namespace

void LevelActionRecordReplay(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    auto *context = document.Context();
    const auto frame = CreateLayoutObject(document, LayoutObject::Frame3D);
    const auto parent = CreateLayoutObject(document, LayoutObject::Frame3D);
    const auto child2d = CreateLayoutObject(document, LayoutObject::Frame2D);
    const auto parent2d = CreateLayoutObject(document, LayoutObject::Frame2D);
    const auto texture = CreateLayoutObject(document, LayoutObject::Texture);
    CHECK(frame.ok && parent.ok && child2d.ok && parent2d.ok && texture.ok);
    auto *object = context->GetObject(frame.created);
    ExpectUndoRoundTrip(document, "Recorded rename", [&] { return RenameLevelObject(document, frame.created, "Recorded Frame"); });
    ExpectUndoRoundTrip(document, "Recorded visibility", [&] { return ShowObjectsAs(document, {frame.created}, CKHIERARCHICALHIDE); });
    ExpectUndoRoundTrip(document, "Recorded priority", [&] { return SetPriorities(document, {frame.created}, 17); });
    ExpectUndoRoundTrip(document, "Recorded render priority", [&] { return SetRenderPriority(document, {frame.created}, -18); });
    const int option = SaveOptionOf(context->GetObject(texture.created)) == 1 ? 2 : 1;
    ExpectUndoRoundTrip(document, "Recorded save options", [&] { return SetSaveOptions(document, {texture.created}, option); });
    ExpectUndoRoundTrip(document, "Recorded 3D parent", [&] { return SetHierarchyParent(document, frame.created, parent.created); }, {frame.created});
    CHECK(SetHierarchyParent(document, parent2d.created, 0, 1).ok);
    ExpectUndoRoundTrip(document, "Recorded 2D parent", [&] { return SetHierarchyParent(document, child2d.created, parent2d.created); });
    auto *scene = context->GetCurrentLevel()->GetLevelScene();
    auto environment = ReadSceneEnvironment(scene);
    environment.fogEnd += 33;
    environment.backgroundTexture = texture.created;
    ExpectUndoRoundTrip(document, "Recorded environment", [&] { return SetSceneEnvironment(document, scene->GetID(), environment, "Environment record"); });

    // Reconstruct from copied/moved values, with the builder and original
    // record gone. The interpreter has no closures pointing at either.
    auto payload = Record(document);
    const auto current = Name(object);
    payload.Changes<ObjectNameChange>().emplace_back(ObjectNameChange{Ref(object), current, "Recovered record"});
    const auto saved = payload;
    auto restored = RestoreLevelAction(payload);
    payload.Changes<ObjectNameChange>().clear();
    CHECK(restored->Redo(document) && Name(object) == "Recovered record");
    CHECK(restored->Undo(document) && Name(object) == current);
    auto invalid = saved;
    invalid.Changes<ObjectNameChange>().insert(invalid.Changes<ObjectNameChange>().begin(), ObjectNameChange{Ref(scene), Name(scene), "Must not be written"});
    invalid.Changes<ObjectNameChange>().back().object.id = 0xFFFFFFFF;
    const auto sceneName = Name(scene);
    CHECK(!RestoreLevelAction(invalid)->Redo(document) && Name(scene) == sceneName);
    invalid.Changes<ObjectNameChange>().back().object = {frame.created, CKCID_TEXTURE};
    CHECK(!RestoreLevelAction(invalid)->Redo(document) && Name(scene) == sceneName);
    auto badProperty = Record<ObjectIntegerChange>(document);
    badProperty.Changes<ObjectIntegerChange>().emplace_back(ObjectIntegerChange{Ref(object), static_cast<ObjectIntegerProperty>(-1), 0, 1});
    CHECK(!RestoreLevelAction(badProperty)->Redo(document));
    auto missingParent = Record<ObjectParentChange>(document);
    missingParent.Changes<ObjectParentChange>().emplace_back(ObjectParentChange{Ref(object), {Ref(context->GetObject(parent.created)), false}, {{0xFFFFFFFF, CKCID_3DENTITY}, false}});
    CHECK(!RestoreLevelAction(missingParent)->Redo(document));
    CHECK(ApplyLevelActionRecord(document, "Recovered data", saved).ok);
    CHECK(document.History().Undo() && Name(object) == current);
    CHECK(document.History().Redo() && Name(object) == "Recovered record");
    document.History().Clear();
    {
        // Core callers can wrap the same context in another Document.
        // Generation, IDs and classes are identical, but its history differs.
        Document other(host);
        auto foreign = saved;
        foreign.Changes<ObjectNameChange>().front().after = "Wrong document";
        const auto originalName = Name(object);
        CHECK(document.InstanceId() != other.InstanceId());
        CHECK(!RestoreLevelAction(foreign)->Redo(other));
        CHECK(Name(object) == originalName && !other.History().CanUndo());
        document.Close(); // Release the owning document before the wrapper.
    }
    CHECK(document.New(nullptr));
    const auto newName = Name(document.Context()->GetCurrentLevel());
    CHECK(!restored->Redo(document));
    auto stale = saved;
    stale.Changes<ObjectNameChange>().front().object = Ref(document.Context()->GetCurrentLevel());
    stale.Changes<ObjectNameChange>().front().after = "Wrong context";
    CHECK(stale.contextGeneration != document.Host().Generation());
    CHECK(!RestoreLevelAction(stale)->Redo(document));
    CHECK(Name(document.Context()->GetCurrentLevel()) == newName && !document.History().CanUndo());
    CHECK(saved.Changes<ObjectNameChange>().front().after == "Recovered record");
}

void LevelActionRecordFailures(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    auto *a = new ReplayObject(document.Context());
    auto *b = new ReplayObject(document.Context());
    document.Include(a->GetID()); document.Include(b->GetID());
    const auto payload = VisibilityRecord(document, a, b);
    auto &history = document.History();
    auto seed = [&] {
        history.Clear(); a->Reset(); b->Reset();
        document.MarkSaved(); history.MarkSavePoint();
        auto *level = document.Context()->GetCurrentLevel();
        CHECK(RenameLevelObject(document, level->GetID(), Name(level) + " seed").ok);
        CHECK(history.Undo() && !document.Dirty() && history.CanRedo());
    };
    seed();
    b->throwAt = 1;
    CHECK(ApplyLevelActionRecord(document, "Record partial failure", payload).Status() == EditStatus::Error);
    CHECK(Visibility(a) == CKSHOW && Visibility(b) == CKSHOW && !document.Dirty() && history.CanRedo());
    CHECK(history.Redo() && history.Undo());
    seed();
    b->rejectAt = 1;
    CHECK(!ApplyLevelActionRecord(document, "Record readback failure", payload).ok);
    CHECK(Visibility(a) == CKSHOW && Visibility(b) == CKSHOW && !document.Dirty() && history.CanRedo());
    seed();
    CHECK(ApplyLevelActionRecord(document, "Record Undo failure", payload).ok);
    a->throwAt = a->writes + 1;
    CHECK(!history.Undo());
    CHECK(Visibility(a) == CKHIDE && Visibility(b) == CKHIDE && document.Dirty());
    CHECK(!history.CanUndo() && !history.CanRedo());
    seed();
    CHECK(ApplyLevelActionRecord(document, "Record Redo failure", payload).ok && history.Undo());
    b->throwAt = b->writes + 1;
    CHECK(!history.Redo());
    CHECK(Visibility(a) == CKSHOW && Visibility(b) == CKSHOW && document.Dirty());
    CHECK(!history.CanUndo() && !history.CanRedo());
    seed();
    a->CKObject::Show(CKHIERARCHICALHIDE); b->CKObject::Show(CKHIERARCHICALHIDE);
    b->throwAt = 1;
    CHECK(!RestoreLevelAction(payload)->Redo(document));
    CHECK(Visibility(a) == CKHIERARCHICALHIDE && Visibility(b) == CKHIERARCHICALHIDE);
    seed();
    b->throwAt = 1; b->refuseAt = 2;
    CHECK(!ApplyLevelActionRecord(document, "Record incomplete recovery", payload).ok);
    CHECK(Visibility(a) == CKSHOW && Visibility(b) == CKHIDE && document.Dirty());
    CHECK(!history.CanUndo() && !history.CanRedo());
}

void LevelActionRecordDependencies(CkRuntimeHost &host) {
    Document document(host);
    auto &history = document.History();
    for (int release = 0; release < 3; ++release) {
        CHECK(document.New(nullptr));
        history.SetLimit(50);
        const auto frame = CreateLayoutObject(document, LayoutObject::Frame3D);
        CHECK(frame.ok);
        auto *object = document.Context()->GetObject(frame.created);
        auto payload = Record(document);
        payload.Changes<ObjectNameChange>().emplace_back(ObjectNameChange{Ref(object), Name(object), "Retained by record"});
        CHECK(history.Undo() && document.IsDiscarded(frame.created));
        {
            Edit outer(document, "Nested recovered record");
            CHECK(ApplyLevelActionRecord(document, "Record dependency", payload).ok);
            CHECK(outer.Commit().ok);
        }
        // Push discarded the creation's Redo. Only the nested data record
        // now keeps the graveyard object alive; it must not be recycled.
        CHECK(document.Context()->GetObject(frame.created) && document.IsDiscarded(frame.created));
        if (release == 0) history.Clear();
        if (release == 1) history.SetLimit(0);
        if (release == 2) {
            CHECK(history.Undo());
            auto *level = document.Context()->GetCurrentLevel();
            CHECK(RenameLevelObject(document, level->GetID(), "Replace record Redo").ok);
        }
        CHECK(!document.Context()->GetObject(frame.created));
    }
    CHECK(document.New(nullptr));
    history.SetLimit(50);
    const auto camera = CreateLayoutObject(document, LayoutObject::Camera);
    const auto texture = CreateLayoutObject(document, LayoutObject::Texture);
    CHECK(camera.ok && texture.ok);
    auto *scene = document.Context()->GetCurrentLevel()->GetLevelScene();
    auto before = ReadSceneEnvironment(scene);
    before.backgroundTexture = texture.created; before.startingCamera = camera.created;
    CHECK(SetSceneEnvironment(document, scene->GetID(), before, "Initial environment").ok);
    auto after = before;
    after.backgroundTexture = after.startingCamera = 0;
    auto payload = Record<SceneEnvironmentChange>(document);
    payload.Changes<SceneEnvironmentChange>().emplace_back(SceneEnvironmentChange{Ref(scene), before, after});
    Bury(document, {camera.created, texture.created});
    CHECK(ApplyLevelActionRecord(document, "Referenced environment", payload).ok);
    history.SetLimit(1); // Evicts creation, deletion and older environment steps.
    CHECK(document.Context()->GetObject(camera.created) && document.Context()->GetObject(texture.created));
    CHECK(history.Undo() && ReadSceneEnvironment(scene) == before);
    CHECK(history.Redo() && ReadSceneEnvironment(scene) == after);
    history.Clear();
    CHECK(!document.Context()->GetObject(camera.created) && !document.Context()->GetObject(texture.created));

    CHECK(document.New(nullptr));
    history.SetLimit(50);
    const auto a = CreateLayoutObject(document, LayoutObject::Frame3D);
    const auto b = CreateLayoutObject(document, LayoutObject::Frame3D);
    const auto child = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(a.ok && b.ok && child.ok && SetHierarchyParent(document, child.created, a.created).ok);
    auto parentRecord = Record<ObjectParentChange>(document);
    parentRecord.Changes<ObjectParentChange>().emplace_back(ObjectParentChange{
        Ref(document.Context()->GetObject(child.created)), {Ref(document.Context()->GetObject(a.created)), false},
        {Ref(document.Context()->GetObject(b.created)), false}});
    Bury(document, {a.created, b.created});
    CHECK(ApplyLevelActionRecord(document, "Retained parent references", parentRecord).ok);
    history.SetLimit(1);
    CHECK(document.Context()->GetObject(a.created) && document.Context()->GetObject(b.created));
    CHECK(history.Undo() && CK3dEntity::Cast(document.Context()->GetObject(child.created))->GetParent()->GetID() == a.created);
    CHECK(history.Redo() && CK3dEntity::Cast(document.Context()->GetObject(child.created))->GetParent()->GetID() == b.created);
    CHECK(SetHierarchyParent(document, child.created, 0).ok);
    history.Clear();
    CHECK(!document.Context()->GetObject(a.created) && !document.Context()->GetObject(b.created));
    CHECK(document.IsLive(child.created));
}
} // namespace vdevtest
