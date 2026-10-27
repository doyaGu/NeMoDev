#include "VirtoolsDevTestSupport.h"

#include <SDL3/SDL_timer.h>

namespace vdevtest {
namespace {

void CheckGone(Document &document, const std::set<CK_ID> &ids) {
    for (CK_ID id : ids) {
        CHECK(!document.Context()->GetObject(id));
        CHECK(!document.IsDiscarded(id));
        CHECK(!document.CreatedObjects().count(id));
        CHECK(!document.ExistingSession(id));
        CHECK(std::find(document.LoadedObjects().begin(), document.LoadedObjects().end(), id) == document.LoadedObjects().end());
    }
}

void Advance(Document &document) {
    Edit edit(document, "Advance history", Edit::Marks::Scripts);
    edit.Done([] { return true; }, [] { return true; });
    CHECK(edit.Commit().ok);
}

// Undo actions may own state chunks that borrow runtime objects. Those
// captures must be released before collecting the objects themselves.
struct LifetimeProbe {
    LifetimeProbe(CKContext *context, CK_ID id, std::shared_ptr<bool> releasedWhileAlive)
        : context(context), id(id), releasedWhileAlive(std::move(releasedWhileAlive)) {}
    CKContext *context;
    CK_ID id;
    std::shared_ptr<bool> releasedWhileAlive;
    ~LifetimeProbe() { *releasedWhileAlive = context->GetObject(id) != nullptr; }
};

} // namespace

void GraveyardCollection(CkRuntimeHost &host) {
    Document document(host);
    std::string error;
    CHECK(document.New(&error));
    CKContext *context = document.Context();
    const CK_ID owner = context->GetCurrentLevel()->GetID();
    UndoHistory &history = document.History();

    // Evicting creation must keep the deleted Script for its remaining
    // deletion step. Evicting that step releases the whole Script graph.
    history.SetLimit(2);
    const EditResult script = CreateScript(document, owner, "Evicted Script");
    CHECK(script.ok && document.Session(script.created));
    const std::set<CK_ID> owned = CollectOwnedObjects(context, {script.created});
    CHECK(owned.size() > 1);
    CHECK(DeleteScript(document, script.created).ok);
    Advance(document);
    CHECK(context->GetObject(script.created) && document.IsDiscarded(script.created));
    CHECK(history.Undo() && history.Undo());
    CHECK(document.IsLive(script.created));
    CHECK(history.Redo() && history.Redo());
    Advance(document);
    CheckGone(document, owned);
    CHECK(document.IsLive(owner) && history.Undo() && history.Redo());

    // Nested steps retain their creations on Redo, even after reducing the
    // Undo limit to zero. Replacing Redo releases them and their pins.
    history.SetLimit(10);
    CK_ID nestedId = 0;
    {
        Edit group(document, "Nested Script and local");
        const EditResult nested = CreateScript(document, owner, "Redo Script");
        CHECK(nested.ok);
        nestedId = nested.created;
        CHECK(AddLocalParameter(document, nestedId, nestedId, "Value", CKPGUID_INT, Point(10, 10)).ok);
        CHECK(group.Commit().ok);
    }
    const std::set<CK_ID> nestedOwned = CollectOwnedObjects(context, {nestedId});
    CHECK(history.Undo() && document.IsDiscarded(nestedId));
    history.SetLimit(0);
    CHECK(context->GetObject(nestedId) && history.CanRedo());
    Advance(document);
    CheckGone(document, nestedOwned);
    CHECK(!history.CanUndo() && !history.CanRedo());

    // A retained Redo can still reach the save point after the Undo limit
    // becomes zero; its applied creation is live even without any history.
    history.SetLimit(10);
    const auto saved = CreateScript(document, owner, "Saved Script");
    CHECK(saved.ok);
    document.MarkSaved();
    history.MarkSavePoint();
    CHECK(history.Undo() && document.Dirty());
    history.SetLimit(0);
    CHECK(history.Redo() && document.IsLive(saved.created) && !document.Dirty());
    CHECK(!history.CanUndo() && DeleteScript(document, saved.created).ok);
    CheckGone(document, {saved.created});

    // Clearing both stacks releases discarded objects, and leaves a live
    // creation and the saved/modified state alone.
    history.SetLimit(10);
    const auto live = CreateScript(document, owner, "Live Script");
    const auto deleted = CreateScript(document, owner, "Deleted Script");
    CHECK(live.ok && deleted.ok && DeleteScript(document, deleted.created).ok);
    const auto deletedOwned = CollectOwnedObjects(context, {deleted.created});
    const auto undone = CreateScript(document, owner, "Undone Script");
    CHECK(undone.ok && history.Undo());
    const auto undoneOwned = CollectOwnedObjects(context, {undone.created});
    document.MarkSaved();
    const auto outside = document.ChangesOutsideUndo();
    history.Clear();
    CheckGone(document, deletedOwned);
    CheckGone(document, undoneOwned);
    CHECK(document.IsLive(live.created) && document.CreatedObjects().count(live.created));
    CHECK(!document.Dirty() && document.ChangesOutsideUndo() == outside);

    // Ungrouping moves children into the live graph. Reclaim the wrapper
    // and its own pins while keeping those moved children and their pins.
    const auto wrapper = CreateBehaviorGraph(document, live.created, live.created, "Wrapper", Point(100, 100));
    const auto child = CreateBehaviorGraph(document, live.created, wrapper.created, "Child", Point(120, 140));
    CHECK(wrapper.ok && child.ok);
    CHECK(AddGraphPin(document, live.created, wrapper.created, PinKind::ParamIn, "Wrapper input", CKPGUID_INT).ok);
    CHECK(AddGraphPin(document, live.created, child.created, PinKind::ParamIn, "Child input", CKPGUID_INT).ok);
    const CK_ID removedPin = CKBehavior::Cast(context->GetObject(wrapper.created))->GetInputParameter(0)->GetID();
    const CK_ID childPin = CKBehavior::Cast(context->GetObject(child.created))->GetInputParameter(0)->GetID();
    CHECK(UngroupGraph(document, live.created, wrapper.created).ok);
    history.SetLimit(1);
    CHECK(AddLocalParameter(document, live.created, live.created, "After ungroup", CKPGUID_INT, Point(10, 10)).ok);
    CHECK(!context->GetObject(wrapper.created));
    CHECK(!context->GetObject(removedPin));
    CHECK(document.IsLive(child.created) && document.IsLive(childPin));
    CHECK(history.Undo() && history.Redo());
    history.Clear();

    // Collection requested during an edit waits for its rollback, including
    // actions that still access the discarded Script.
    const auto deferred = CreateScript(document, owner, "Deferred Script");
    CHECK(deferred.ok && DeleteScript(document, deferred.created).ok);
    const auto deferredOwned = CollectOwnedObjects(context, {deferred.created});
    {
        Edit edit(document, "Active edit");
        history.SetLimit(0);
        CHECK(context->GetObject(deferred.created));
        edit.Done([] { return true; }, [&] {
            CHECK(context->GetObject(deferred.created));
            return true;
        });
        CHECK(edit.Cancel());
    }
    CheckGone(document, deferredOwned);

    // An empty commit must also flush a clear deferred by the open edit.
    history.SetLimit(10);
    const auto empty = CreateScript(document, owner, "Empty edit Script");
    CHECK(empty.ok && history.Undo());
    const auto emptyOwned = CollectOwnedObjects(context, {empty.created});
    {
        Edit edit(document, "Empty edit");
        history.Clear();
        CHECK(context->GetObject(empty.created));
        CHECK(edit.Commit().Unchanged());
    }
    CheckGone(document, emptyOwned);

    // Nested cancellation keeps objects until the outer edit also ends.
    document.MarkSaved();
    std::set<CK_ID> cancelledOwned;
    {
        Edit outer(document, "Outer cancelled");
        Edit inner(document, "Inner cancelled");
        const auto cancelled = CreateScript(document, owner, "Cancelled Script");
        CHECK(cancelled.ok);
        cancelledOwned = CollectOwnedObjects(context, {cancelled.created});
        CHECK(inner.Cancel() && context->GetObject(cancelled.created));
        CHECK(outer.Cancel());
    }
    CheckGone(document, cancelledOwned);
    CHECK(!document.Dirty());

    // A failed Undo/Redo clears history while its current step is outside
    // both stacks. Keep its captures/objects alive until replay has ended.
    for (bool undoFailure : {true, false}) {
        const auto victim = CreateScript(document, owner, "Failed replay Script");
        CHECK(victim.ok && DeleteScript(document, victim.created).ok);
        const auto victimOwned = CollectOwnedObjects(context, {victim.created});
        auto releasedWhileAlive = std::make_shared<bool>(false);
        auto probe = std::make_shared<LifetimeProbe>(context, victim.created, releasedWhileAlive);
        {
            Edit edit(document, "Fail replay");
            const auto fail = [&, probe] {
                history.Clear();
                CHECK(context->GetObject(victim.created));
                CHECK(!history.Undo() && !history.Redo());
                return false;
            };
            if (undoFailure) edit.Done([] { return true; }, fail);
            else edit.Done(fail, [] { return true; });
            CHECK(edit.Commit().ok);
        }
        probe.reset();
        if (undoFailure) CHECK(!history.Undo());
        else CHECK(history.Undo() && !history.Redo());
        CHECK(*releasedWhileAlive);
        CheckGone(document, victimOwned);
        CHECK(!history.CanUndo() && !history.CanRedo());
    }

    // Repeated create/delete cycles with a bounded history must keep the
    // runtime population bounded, rather than accumulate every old graph.
    history.SetLimit(1);
    const int liveBehaviors = context->GetObjectsCountByClassID(CKCID_BEHAVIOR);
    for (int i = 0; i < 20; ++i) {
        const auto cycle = CreateScript(document, owner, "Cycle Script");
        CHECK(cycle.ok && DeleteScript(document, cycle.created).ok);
        CHECK(context->GetObjectsCountByClassID(CKCID_BEHAVIOR) == liveBehaviors + 1);
    }
    history.Clear();
    CHECK(context->GetObjectsCountByClassID(CKCID_BEHAVIOR) == liveBehaviors && document.IsLive(live.created));

    // Shared rendering resources are still live when an undone skinned
    // entity is abandoned with its Redo branch.
    auto *bone = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Live bone"));
    auto *mesh = CKMesh::Cast(context->CreateObject(CKCID_MESH, (CKSTRING) "Shared mesh"));
    auto *skinned = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Discarded skin"));
    CHECK(bone && mesh && skinned);
    const CK_ID boneId = bone->GetID(), meshId = mesh->GetID(), skinnedId = skinned->GetID();
    skinned->SetCurrentMesh(mesh);
    CKSkin *skin = skinned->CreateSkin();
    CHECK(skin);
    skin->SetBoneCount(1);
    skin->GetBoneData(0)->SetBone(bone);
    {
        Edit edit(document, "Create skinned entity");
        edit.Created(skinnedId);
        CHECK(edit.Commit().ok);
    }
    CHECK(history.Undo());
    Advance(document);
    CheckGone(document, {skinnedId});
    CHECK(document.IsLive(boneId) && document.IsLive(meshId));

    // Loaded-object bookkeeping must lose collected descendants as well;
    // a subsequent save must not reference the old IDs.
    namespace fs = std::filesystem;
    const fs::path root = fs::current_path() / ("graveyard-" + std::to_string(SDL_GetTicksNS()));
    CHECK(fs::create_directory(root));
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code error; fs::remove_all(root, error); } } cleanup{root};
    SaveReport report;
    CHECK(SaveDocumentAs(document, (root / "before.cmo").u8string(), {}, report));
    CHECK(document.Open((root / "before.cmo").u8string(), &error));
    CHECK(!document.Scripts().empty());
    const CK_ID loadedScript = document.Scripts().front().id;
    const auto loadedOwned = CollectOwnedObjects(context, {loadedScript});
    CHECK(document.LoadedObjects().size() >= loadedOwned.size());
    history.SetLimit(0);
    CHECK(DeleteScript(document, loadedScript).ok);
    CheckGone(document, loadedOwned);
    CHECK(SaveDocumentAs(document, (root / "after.cmo").u8string(), {}, report));
}

void ObjectReferenceLifetimes(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    CKContext *context = document.Context();
    const CK_ID owner = context->GetCurrentLevel()->GetID();
    const auto script = CreateScript(document, owner, "Copied Script");
    CHECK(script.ok);
    const auto graph = CreateBehaviorGraph(document, script.created, script.created, "Copied graph", Point(100, 100));
    CHECK(graph.ok);
    const auto owned = CollectOwnedObjects(context, {script.created});
    auto references = document.RetainObjects({graph.created});
    CHECK(references && DeleteScript(document, script.created).ok);
    document.History().Clear();
    for (CK_ID id : owned) CHECK(context->GetObject(id));
    CHECK(document.IsDiscarded(script.created));
    const auto pasted = PasteScripts(document, {script.created}, owner, true);
    CHECK(pasted.ok && document.IsLive(pasted.created));
    auto second = document.RetainObjects({script.created});
    references.reset();
    CHECK(context->GetObject(script.created));
    second.reset();
    CheckGone(document, owned);
    CHECK(document.History().Undo() && document.History().Redo());
    CHECK(document.IsLive(pasted.created));

    // Releasing an old clipboard after Close must not collect objects in
    // the new composition, even when the runtime recycles object IDs.
    references = document.RetainObjects({pasted.created});
    document.Close();
    CHECK(document.New(nullptr));
    const auto next = CreateScript(document, context->GetCurrentLevel()->GetID(), "New document");
    CHECK(next.ok);
    references.reset();
    CHECK(document.IsLive(next.created));

    // History eviction and abandoning Redo also honor non-history owners.
    auto retained = document.RetainObjects({next.created});
    document.History().SetLimit(0);
    CHECK(DeleteScript(document, next.created).ok);
    CHECK(context->GetObject(next.created));
    retained.reset();
    CHECK(!context->GetObject(next.created));
    document.History().SetLimit(50);
    const auto undone = CreateScript(document, context->GetCurrentLevel()->GetID(), "Undone copy source");
    CHECK(undone.ok);
    retained = document.RetainObjects({undone.created});
    CHECK(document.History().Undo());
    Advance(document);
    CHECK(context->GetObject(undone.created));
    retained.reset();
    CHECK(!context->GetObject(undone.created));

    // A loaded Script can be copied from the Level Manager without ever
    // opening its Schematic session. Its retained chunk must still paste.
    namespace fs = std::filesystem;
    const auto folder = fs::current_path() / ("clipboard-" + std::to_string(SDL_GetTicksNS()));
    CHECK(fs::create_directory(folder));
    struct Cleanup { fs::path folder; ~Cleanup() { std::error_code error; fs::remove_all(folder, error); } } cleanup{folder};
    CHECK(CreateScript(document, context->GetCurrentLevel()->GetID(), "Unopened Script").ok);
    SaveReport report;
    const auto file = (folder / "source.cmo").u8string();
    CHECK(SaveDocumentAs(document, file, {}, report));
    CHECK(document.Open(file, nullptr));
    CHECK(!document.Scripts().empty());
    const auto loaded = document.Scripts().front().id;
    CHECK(!document.ExistingSession(loaded));
    retained = document.RetainObjects({loaded});
    CHECK(DeleteScript(document, loaded).ok);
    document.History().Clear();
    const auto copy = PasteScripts(document, {loaded}, context->GetCurrentLevel()->GetID(), true);
    CHECK(copy.ok && document.Session(copy.created));
    retained.reset();
    CHECK(!context->GetObject(loaded));
}

} // namespace vdevtest
