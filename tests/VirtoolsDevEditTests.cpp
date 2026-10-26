// Edits and the undo history (core/document/Edit.h), undo round trips of each kind of command, and
// the CK arrays the commands change (core/runtime/CkArrays.h).
#include "VirtoolsDevTestSupport.h"

namespace vdevtest {

namespace {

// An edit of a plain value: redo sets to, undo puts back what it was.
bool SetValue(Edit &edit, int &value, int to) {
    const int from = value;
    return edit.Do([&value, to] { value = to; return true; }, [&value, from] { value = from; return true; });
}

CKBehavior *NewScript(CKContext *context, const char *name) {
    auto *script = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) name));
    script->UseGraph();
    script->SetType(CKBEHAVIORTYPE_SCRIPT);
    script->CreateInput((CKSTRING) "Start");
    return script;
}

} // namespace

void Edits(CkRuntimeHost &host) {
    CKContext *context = host.Context();
    Document document(host);
    document.RefreshScripts();
    UndoHistory &history = document.History();
    history.SetLimit(100);
    int value = 0;

    // Committed: one step, undone and redone; the edit's label.
    {
        Edit edit(document, "Set 1");
        CHECK(edit.Open() && !edit.Nested() && document.EditOpen());
        CHECK(SetValue(edit, value, 1) && value == 1);
        CHECK(edit.Commit().ok && !edit.Open() && !document.EditOpen());
    }
    CHECK(history.UndoCount() == 1 && history.UndoLabel() == "Set 1");
    CHECK(history.Undo() && value == 0);
    CHECK(history.Redo() && value == 1);

    // Ended without Commit: taken back, nothing recorded.
    {
        Edit edit(document, "Dropped");
        CHECK(SetValue(edit, value, 2) && value == 2);
    }
    CHECK(value == 1 && history.UndoCount() == 1 && !document.EditOpen());
    {
        Edit edit(document, "Cancelled");
        CHECK(SetValue(edit, value, 3));
        CHECK(edit.Cancel() && value == 1 && !edit.Open());
        CHECK(edit.Commit().Status() == EditStatus::Error); // ended
    }

    // A failed action is not recorded; Fail takes back the others.
    {
        Edit edit(document, "Refused");
        CHECK(SetValue(edit, value, 4));
        CHECK(!edit.Do([] { return false; }, [] { return true; }));
        const EditResult refused = edit.Fail("no");
        CHECK(!refused.ok && refused.Status() == EditStatus::Refused && value == 1);
        Edit unchanged(document, "Unchanged");
        CHECK(unchanged.Unchanged("same").Unchanged());
        Edit empty(document, "Empty");
        CHECK(empty.Commit().Unchanged() && history.UndoCount() == 1);
    }

    // Nested edits: one step, all or nothing.
    {
        Edit outer(document, "Outer");
        CHECK(SetValue(outer, value, 10));
        {
            Edit inner(document, "Inner");
            CHECK(inner.Nested());
            CHECK(SetValue(inner, value, 11));
            CHECK(inner.Commit().ok && history.UndoCount() == 1); // not in the history yet
        }
        {
            Edit inner(document, "Second");
            CHECK(SetValue(inner, value, 12) && inner.Commit().ok);
        }
        CHECK(!history.Undo() && value == 12); // the edit is open
        CHECK(outer.Commit().ok);
    }
    CHECK(history.UndoCount() == 2 && history.UndoLabel() == "Outer");
    CHECK(history.Undo() && value == 1);
    CHECK(history.Redo() && value == 12);
    {
        Edit outer(document, "Outer dropped");
        Edit inner(document, "Inner");
        CHECK(SetValue(inner, value, 20) && inner.Commit().ok);
        CHECK(SetValue(outer, value, 21));
        CHECK(outer.Cancel() && value == 12 && history.UndoCount() == 2);
    }

    // A command run while an edit is open is part of it.
    {
        Edit edit(document, "Joined");
        CHECK(RecordValue(document, "Inner", value, 30));
        CHECK(value == 30 && history.UndoCount() == 2);
        CHECK(edit.Commit().ok);
    }
    CHECK(history.UndoCount() == 3 && history.UndoLabel() == "Joined");
    CHECK(history.Undo() && value == 12 && history.Redo() && value == 30);

    // The save point; taking an edit back leaves the composition unmodified;
    // an edit marking only the Scripts it shows (none here) does not modify it.
    document.MarkSaved();
    history.MarkSavePoint();
    CHECK(!document.Dirty());
    {
        Edit edit(document, "Modify");
        CHECK(SetValue(edit, value, 31) && edit.Commit().ok && document.Dirty());
    }
    CHECK(history.Undo() && !document.Dirty());
    {
        Edit outer(document, "Taken back");
        Edit inner(document, "Inner");
        CHECK(SetValue(inner, value, 32) && inner.Commit().ok);
        CHECK(outer.Cancel() && !document.Dirty());
    }
    {
        Edit edit(document, "Camera", Edit::Marks::Scripts);
        CHECK(SetValue(edit, value, 33) && edit.Commit().ok && !document.Dirty());
    }
    CHECK(history.Undo() && !document.Dirty() && value == 30);

    // Objects made and removed: saved and in the graveyard as the step is applied.
    {
        CKObject *made = context->CreateObject(CKCID_DATAARRAY, (CKSTRING) "Edit Made");
        CKObject *removed = context->CreateObject(CKCID_DATAARRAY, (CKSTRING) "Edit Removed");
        const CK_ID madeId = made->GetID(), removedId = removed->GetID();
        {
            Edit edit(document, "Make and remove");
            edit.Created(madeId);
            edit.Bury(removedId);
            CHECK(document.IsDiscarded(removedId));
            CHECK(edit.Commit().ok);
        }
        CHECK(document.CreatedObjects().count(madeId) && !document.IsDiscarded(madeId));
        CHECK(document.IsDiscarded(removedId));
        CHECK(history.Undo() && document.IsDiscarded(madeId) && !document.IsDiscarded(removedId));
        CHECK(history.Redo() && !document.IsDiscarded(madeId) && document.IsDiscarded(removedId));
        // Taken back: never part of the composition.
        CKObject *dropped = context->CreateObject(CKCID_DATAARRAY, (CKSTRING) "Edit Dropped");
        const CK_ID droppedId = dropped->GetID();
        {
            Edit edit(document, "Dropped");
            edit.Created(droppedId);
            edit.Bury(madeId);
        }
        CHECK(!context->GetObject(droppedId) && !document.IsDiscarded(madeId));
    }

    // What cannot be taken back clears the history, keeps the objects made,
    // and the edits it is in record nothing.
    {
        CKObject *made = context->CreateObject(CKCID_DATAARRAY, (CKSTRING) "Edit Kept");
        CHECK(history.CanUndo());
        {
            Edit edit(document, "Stuck");
            edit.Created(made->GetID());
            edit.Done([] { return true; }, [] { return false; });
            CHECK(!edit.Cancel());
        }
        CHECK(!history.CanUndo() && !history.CanRedo() && !document.IsDiscarded(made->GetID()));
        {
            Edit outer(document, "Outer stuck");
            CHECK(SetValue(outer, value, 40));
            {
                Edit inner(document, "Inner stuck");
                inner.Done([] { return true; }, [] { return false; });
                CHECK(inner.Fail("stuck").Status() == EditStatus::Error);
            }
            CHECK(outer.Commit().Status() == EditStatus::Error);
        }
        CHECK(!history.CanUndo() && value == 30);
    }

    // A Script's display: kept once from its first change, put back after the runtime.
    {
        CKBehavior *script = NewScript(context, "Edit Display");
        const CK_ID id = script->GetID();
        document.RefreshScripts();
        CHECK(document.Session(id));
        const State before = Capture(document, id);
        {
            Edit edit(document, "Resize Script");
            // A gesture resizing the Script: the data changes in place, one change kept.
            InterfaceData *data = edit.ScriptData(id);
            CHECK(data);
            const float height = data->header.vSize;
            data->header.vSize = height + 40.0f;
            CHECK(edit.ScriptData(id) == data);
            data->header.vSize = height + 80.0f;
            CHECK(edit.Commit().ok);
        }
        const State folded = Capture(document, id);
        CHECK(!Same(before, folded));
        CHECK(history.Undo());
        ExpectSame(before, Capture(document, id), "undo display");
        CHECK(history.Redo());
        ExpectSame(folded, Capture(document, id), "redo display");
        {
            Edit edit(document, "Resize back");
            InterfaceData smaller = document.Session(id)->Data();
            smaller.header.vSize -= 80.0f;
            CHECK(edit.ShowScript(id, smaller));
            CHECK(edit.Cancel());
        }
        ExpectSame(folded, Capture(document, id), "display taken back");
        // A gesture taken back (Escape) leaves an unmodified composition unmodified.
        document.MarkSaved();
        {
            Edit edit(document, "Resize and back", Edit::Marks::Scripts);
            InterfaceData *data = edit.ScriptData(id);
            CHECK(data && document.Dirty());
            data->header.vSize += 20.0f;
        }
        CHECK(!document.Dirty());
        ExpectSame(folded, Capture(document, id), "gesture taken back");
    }

    // The document closed with an edit open: it ends, with nothing to take back.
    {
        Document other(host);
        auto edit = std::make_unique<Edit>(other, "Left open");
        CHECK(SetValue(*edit, value, 50));
        other.Close();
        CHECK(!edit->Open() && !other.EditOpen());
        edit.reset();
        CHECK(value == 50);
    }
    std::cout << "Edits passed\n";
}

// One command of each way an undo step is recorded, through the round trip.
void UndoRoundTrips(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes) {
    CKContext *context = host.Context();
    auto *owner = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Round Trip Owner"));
    CHECK(owner);
    if (CKLevel *level = context->GetCurrentLevel()) level->AddObject(owner);
    CKBehavior *script = NewScript(context, "Round Trip Script");
    owner->AddScript(script);
    const CK_ID id = script->GetID(), ownerId = owner->GetID();
    Document document(host);
    document.RefreshScripts();
    document.History().SetLimit(100);
    UndoHistory &undo = document.History();
    CHECK(document.Session(id));

    // The Script's display (EditScriptData).
    ExpectUndoRoundTrip(document, "EditScriptData", [&] {
        return EditScriptData(document, id, "Open Script", [](InterfaceData &data) { SetScriptOpen(data, true); })
                   ? EditResult::Applied()
                   : EditResult::Refused("read-only");
    });
    // Topology (TopologyEdit).
    const EditResult block = ExpectUndoRoundTrip(document, "InsertBuildingBlock", [&] {
        return InsertBuildingBlock(document, id, id, prototypes[0], Point(200, 100));
    });
    // A parameter's value (ParameterValueCommand).
    CKBehavior *inserted = CKBehavior::Cast(context->GetObject(block.created));
    CHECK(inserted);
    const EditResult local = ExpectUndoRoundTrip(document, "AddLocalParameter", [&] {
        return AddLocalParameter(document, id, id, "Round Trip Value", CKPGUID_INT, Point(100, 300));
    });
    ExpectUndoRoundTrip(document, "SetParameterText",
                        [&] { return SetParameterText(document, id, local.created, "42"); }, {local.created});
    // Plain scalar copiers do not own external memory. Undo must preserve the
    // original float bits even when the type's display text rounds the value.
    CKParameterOut *precise = context->CreateCKParameterOut((CKSTRING) "Precise Float", CKPGUID_FLOAT);
    CHECK(precise);
    const float original = 1.0f / 3.0f;
    CHECK(precise->SetValue(&original, sizeof(original)) == CK_OK);
    const auto originalBytes = ParameterBytes(precise);
    CHECK(SetParameterText(document, 0, precise->GetID(), "2.5").ok);
    CHECK(undo.Undo() && ParameterBytes(precise) == originalBytes);
    CHECK(undo.Redo());
    for (const CKGUID &guid : {CKPGUID_2DVECTOR, CKPGUID_VECTOR, CKPGUID_COLOR}) {
        CKParameterOut *value = context->CreateCKParameterOut((CKSTRING) "Vector Text", guid);
        CHECK(value);
        const bool color = guid == CKPGUID_COLOR;
        const float values[] = {color ? 1.0f : 1.25f, color ? 0.0f : 2.5f, color ? 1.0f : 3.75f, 0.0f};
        CHECK(value->SetValue(values, value->GetDataSize()) == CK_OK);
        const auto bytes = ParameterBytes(value);
        const std::string text = ParameterText(value);
        CHECK(!text.empty() && value->SetStringValue(const_cast<CKSTRING>(text.c_str())) == CK_OK);
        CHECK(ParameterBytes(value) == bytes);
    }
    // Level objects (level::Apply).
    const EditResult array = ExpectUndoRoundTrip(document, "CreateArray", [&] { return CreateArray(document, "Round Trip Array"); });
    ExpectUndoRoundTrip(document, "RenameLevelObject",
                        [&] { return RenameLevelObject(document, array.created, "Round Trip Renamed"); });
    // An object's state (ObjectStateCommand). Its position, not its state's
    // bytes: moving a 3D entity sets the engine's moveable flags
    // (VX_MOVEABLE_HASMOVED, INVERSEWORLDMATVALID) and clears the
    // WORLDALIGNED hint, which reading the state back does not undo.
    auto position = [&] {
        VxVector at;
        owner->GetPosition(&at);
        return at;
    };
    ExpectUndoRoundTrip(document, "EditObjectState", [&] {
        return EditObjectState(document, ownerId, "Move", [](CKObject *object) {
            const VxVector to(1.0f, 2.0f, 3.0f);
            CK3dEntity::Cast(object)->SetPosition(&to);
        });
    });
    CHECK(position() == VxVector(1.0f, 2.0f, 3.0f));
    CHECK(undo.Undo() && position() == VxVector(0.0f, 0.0f, 0.0f));
    CHECK(undo.Redo());
    // Scripts given and taken away (level::Apply, ScriptOwnershipCommand).
    const EditResult created = ExpectUndoRoundTrip(document, "CreateScript",
                                                   [&] { return CreateScript(document, ownerId, "Round Trip Second"); });
    ExpectUndoRoundTrip(document, "DeleteScript", [&] { return DeleteScript(document, created.created); });
    // A deleted object is not the composition's: not live, not offered (the
    // Script UI's list), back with undo.
    auto offered = [&](CK_ID scriptId) {
        const ObjectChoices scripts = ScriptsOf(document, 0);
        return std::any_of(scripts.begin(), scripts.end(), [&](const auto &choice) { return choice.first == scriptId; });
    };
    CHECK(document.IsDiscarded(created.created) && !document.IsLive(created.created) && !offered(created.created));
    const std::vector<CKObject *> behaviors = document.LiveObjects(CKCID_BEHAVIOR);
    CHECK(std::find(behaviors.begin(), behaviors.end(), context->GetObject(created.created)) == behaviors.end());
    CHECK(undo.Undo() && document.IsLive(created.created) && offered(created.created));
    CHECK(undo.Redo() && !offered(created.created));
    ExpectUndoRoundTrip(document, "PasteScripts", [&] { return PasteScripts(document, {id}, ownerId); });
    // The scene's data (actions of their own).
    ExpectUndoRoundTrip(document, "ApplyInitialConditions",
                        [&] { return ApplyInitialConditions(document, {ownerId}, InitialConditions::Set); });
    ExpectUndoRoundTrip(document, "ModifySceneFlags", [&] {
        return ModifySceneFlags(document, {ownerId}, CK_SCENEOBJECT_START_DEACTIVATE, 0, "Deactivate at Start");
    });
    // The Active Now cell: deactivated now in its scene, undone.
    if (CKScene *scene = InitialConditionsScene(context)) {
        CKSceneObject *owner = CKSceneObject::Cast(context->GetObject(ownerId));
        if (owner && scene->IsObjectHere(owner)) {
            const bool active = (scene->GetObjectFlags(owner) & CK_SCENEOBJECT_ACTIVE) != 0;
            ExpectUndoRoundTrip(document, "SetActiveNow", [&] { return SetActiveNow(document, {ownerId}, scene->GetID(), !active); });
            CHECK(((scene->GetObjectFlags(owner) & CK_SCENEOBJECT_ACTIVE) != 0) != active); // redone
        }
    }
    // Several commands as one: an EditGroup, and an Edit around them (each
    // command a nested edit).
    ExpectUndoRoundTrip(document, "EditGroup", [&] {
        EditGroup group(document, "Two Blocks");
        const bool ok = InsertBuildingBlock(document, id, id, prototypes[0], Point(400, 100)).ok &&
                        InsertBuildingBlock(document, id, id, prototypes[1], Point(400, 200)).ok;
        return ok ? EditResult::Applied() : EditResult::Refused("insert");
    });
    ExpectUndoRoundTrip(document, "Edit around commands", [&] {
        Edit edit(document, "Block and Array");
        if (!InsertBuildingBlock(document, id, id, prototypes[1], Point(600, 100)).ok) return edit.Fail("insert");
        if (!CreateArray(document, "Round Trip Joined").ok) return edit.Fail("array");
        return edit.Commit();
    });
    std::cout << "Undo round trips passed\n";
}

// The commands over several Scripts or objects (Dev's all-Scripts and
// selection commands): one undo step each, all or nothing where Dev is.
void BatchCommands(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes) {
    CKContext *context = host.Context();
    CKLevel *level = context->GetCurrentLevel();
    auto entity = [&](const char *name) {
        CK3dEntity *made = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) name));
        if (level) level->AddObject(made);
        return made;
    };
    CK3dEntity *owner = entity("Batch Owner"), *other = entity("Batch Other"), *parent = entity("Batch Parent");
    CKBehavior *first = NewScript(context, "Batch First"), *second = NewScript(context, "Batch Second");
    owner->AddScript(first);
    owner->AddScript(second);
    const CK_ID firstId = first->GetID(), secondId = second->GetID();
    Document document(host);
    document.RefreshScripts();
    document.History().SetLimit(100);
    UndoHistory &undo = document.History();
    CHECK(document.Session(firstId) && document.Session(secondId));

    // A Building Block with an Int input in each Script, the same prototype.
    CKGUID withInt;
    CK_ID blocks[2] = {}, inputs[2] = {};
    int input = -1;
    for (const CKGUID &guid : prototypes) {
        const EditResult inserted = InsertBuildingBlock(document, firstId, firstId, guid, Point(300.0f, 100.0f));
        CKBehavior *block = CKBehavior::Cast(context->GetObject(inserted.created));
        for (int i = 0; block && i < block->GetInputParameterCount(); ++i)
            if (block->GetInputParameter(i)->GetGUID() == CKPGUID_INT) input = i;
        if (input >= 0) {
            withInt = guid;
            blocks[0] = inserted.created;
            break;
        }
    }
    if (input < 0) {
        std::cout << "No logic Building Block with an Int input: batch commands skipped\n";
        return;
    }
    blocks[1] = InsertBuildingBlock(document, secondId, secondId, withInt, Point(300.0f, 100.0f)).created;
    CHECK(blocks[1]);

    // A Building Block grouped whose output another Script reads through a
    // shortcut (corpus: Open File.cmo): that Script's input keeps the output,
    // its display stays true - before, it was moved to the new graph's output
    // and the other Script's shortcut no longer matched CK.
    {
        const size_t steps = undo.UndoCount();
        CK_ID producer = 0;
        int output = -1;
        for (const CKGUID &guid : prototypes) {
            const EditResult inserted = InsertBuildingBlock(document, firstId, firstId, guid, Point(520.0f, 500.0f));
            CKBehavior *block = inserted.ok ? CKBehavior::Cast(context->GetObject(inserted.created)) : nullptr;
            for (int o = 0; block && o < block->GetOutputParameterCount() && output < 0; ++o)
                if (block->GetOutputParameter(o)->GetGUID() == CKPGUID_INT) output = o;
            if (output >= 0) {
                producer = inserted.created;
                break;
            }
            if (inserted.ok) undo.Undo();
        }
        if (!producer) {
            std::cout << "Group with a shortcut elsewhere skipped: no Building Block with an Int output\n";
        } else {
            CKParameterOut *out = CKBehavior::Cast(context->GetObject(producer))->GetOutputParameter(output);
            // The second Script: a local like the output, read by its block's Int input, made its shortcut.
            const EditResult local =
                AddLocalParameter(document, secondId, secondId, out->GetName() ? out->GetName() : "", CKPGUID_INT, Point(100.0f, 560.0f));
            CHECK(local.ok);
            CHECK(SetParameterBytes(document, secondId, local.created, ParameterBytes(out)).ok ||
                  ParameterBytes(CKParameter::Cast(context->GetObject(local.created))) == ParameterBytes(out));
            int slot = -1;
            for (const auto &param : BuildModel(document, secondId).params)
                if (param.parameter == local.created) slot = param.slot;
            CHECK(CreateParameterLink(document, secondId, LinkEndpoint(secondId, slot, ENDPOINT_PLOCAL),
                                      LinkEndpoint(blocks[1], input, ENDPOINT_PIN))
                      .ok);
            // A local like it in the first Script made the source, then the shortcut set to the output.
            const EditResult same = AddLocalParameter(document, firstId, firstId, out->GetName() ? out->GetName() : "",
                                                      CKPGUID_INT, Point(100.0f, 600.0f));
            CHECK(same.ok);
            SetParameterBytes(document, firstId, same.created, ParameterBytes(CKParameter::Cast(context->GetObject(local.created))));
            CHECK(ShortcutizeLocals(document, secondId, same.created).ok);
            CHECK(RetargetShortcuts(document, secondId, same.created, out->GetID()).ok);
            CKParameterIn *reader = CKBehavior::Cast(context->GetObject(blocks[1]))->GetInputParameter(input);
            CHECK(reader->GetDirectSource() == out);
            CHECK(GroupBehaviors(document, firstId, {producer}).ok);
            CHECK(reader->GetDirectSource() == out);
            CHECK(RuntimeConsistencyIssues(context, document.Session(secondId)->Data(),
                                           CKBehavior::Cast(context->GetObject(secondId)))
                      .empty());
            std::cout << "Group with a shortcut elsewhere passed\n";
        }
        while (undo.UndoCount() > steps) CHECK(undo.Undo());
    }

    // A local deleted that another Script reads through a shortcut (corpus:
    // Frustum Object Intersection.cmo): that Script's shortcut and its link
    // leave with it, and come back with the undo - before, its display kept
    // a link CK no longer had.
    {
        const size_t steps = undo.UndoCount();
        const EditResult source = AddLocalParameter(document, firstId, firstId, "Shared Int", CKPGUID_INT, Point(100.0f, 640.0f));
        CHECK(source.ok);
        const EditResult local = AddLocalParameter(document, secondId, secondId, "Shared Int", CKPGUID_INT, Point(100.0f, 640.0f));
        CHECK(local.ok);
        int slot = -1;
        for (const auto &param : BuildModel(document, secondId).params)
            if (param.parameter == local.created) slot = param.slot;
        CHECK(CreateParameterLink(document, secondId, LinkEndpoint(secondId, slot, ENDPOINT_PLOCAL),
                                  LinkEndpoint(blocks[1], input, ENDPOINT_PIN))
                  .ok);
        CHECK(ShortcutizeLocals(document, secondId, source.created).ok);
        CKParameterIn *reader = CKBehavior::Cast(context->GetObject(blocks[1]))->GetInputParameter(input);
        CKBehavior *second = CKBehavior::Cast(context->GetObject(secondId));
        CHECK(reader->GetDirectSource() == context->GetObject(source.created));
        auto shortcuts = [&] {
            const InterfaceData &data = document.Session(secondId)->Data();
            size_t count = data.rootBehavior.sharedParams.size();
            for (const BehaviorData &record : data.behaviors) count += record.sharedParams.size();
            return count;
        };
        const size_t shown = shortcuts();
        CHECK(shown > 0);
        int sourceSlot = -1;
        CKBehavior *first = CKBehavior::Cast(context->GetObject(firstId));
        for (int i = 0; i < first->GetLocalParameterCount(); ++i)
            if (first->GetLocalParameter(i)->GetID() == source.created) sourceSlot = i;
        ElementRef ref;
        ref.kind = ElementKind::LocalParam;
        ref.owner = firstId;
        ref.object = source.created;
        ref.index = sourceSlot;
        CHECK(DeleteElements(document, firstId, {ref}).ok);
        CHECK(reader->GetDirectSource() == nullptr);
        CHECK(shortcuts() == shown - 1);
        CHECK(RuntimeConsistencyIssues(context, document.Session(secondId)->Data(), second).empty());
        CHECK(undo.Undo());
        CHECK(reader->GetDirectSource() == context->GetObject(source.created) && shortcuts() == shown);
        CHECK(RuntimeConsistencyIssues(context, document.Session(secondId)->Data(), second).empty());
        CHECK(undo.Redo() && shortcuts() == shown - 1);
        std::cout << "Deleting a local read elsewhere passed\n";
        while (undo.UndoCount() > steps) CHECK(undo.Undo());
    }

    // A graph ungrouped whose output reaches a Building Block outside through
    // a hidden conversion (corpus: Set Pickable.cmo): the conversion stays in
    // the parent's list and the link is shown through it - before, the link
    // ended at the conversion's own pin and Commit took the conversion out of
    // the graph, the display still linking to it.
    {
        const size_t steps = undo.UndoCount();
        std::vector<CK_ID> made;
        for (const CKGUID &guid : prototypes) {
            const EditResult inserted =
                InsertBuildingBlock(document, firstId, firstId, guid, Point(520.0f, 100.0f + 70.0f * static_cast<float>(made.size())));
            if (inserted.ok) made.push_back(inserted.created);
        }
        CK_ID from = 0, to = 0;
        int in = -1;
        for (CK_ID a : made)
            for (CK_ID b : made) {
                if (a == b || from) continue;
                CKBehavior *source = CKBehavior::Cast(context->GetObject(a));
                CKBehavior *target = CKBehavior::Cast(context->GetObject(b));
                for (int o = 0; o < source->GetOutputParameterCount() && !from; ++o)
                    for (int i = 0; i < target->GetInputParameterCount() && !from; ++i) {
                        const LinkEndpoint out(a, o, ENDPOINT_POUT), pin(b, i, ENDPOINT_PIN);
                        const auto conversions = ParameterLinkConversions(document, firstId, out, pin);
                        if (conversions.empty() || !CreateParameterLink(document, firstId, out, pin, &conversions.front()).ok) continue;
                        from = a;
                        to = b;
                        in = i;
                    }
            }
        if (!from) {
            std::cout << "Ungroup through a conversion skipped: no two Building Blocks needing a conversion\n";
        } else {
            CKParameterIn *input = CKBehavior::Cast(context->GetObject(to))->GetInputParameter(in);
            CKParameterOperation *conversion =
                input->GetDirectSource() ? CKParameterOperation::Cast(input->GetDirectSource()->GetOwner()) : nullptr;
            CHECK(conversion != nullptr);
            CHECK(HiddenConversions(context, document.Session(firstId)->Data()).count(conversion->GetID()) == 1);
            CHECK(GroupBehaviors(document, firstId, {from}).ok);
            CKBehavior *graph = CKBehavior::Cast(context->GetObject(from))->GetParent();
            CHECK(graph && graph->GetID() != firstId);
            CHECK(UngroupGraph(document, firstId, graph->GetID()).ok);
            CKBehavior *script = CKBehavior::Cast(context->GetObject(firstId));
            bool listed = false;
            for (int i = 0; i < script->GetParameterOperationCount(); ++i) listed = listed || script->GetParameterOperation(i) == conversion;
            CHECK(listed && conversion->GetOwner() == script);
            CHECK(HiddenConversions(context, document.Session(firstId)->Data()).count(conversion->GetID()) == 1);
            CHECK(RuntimeConsistencyIssues(context, document.Session(firstId)->Data(), script).empty());
            std::cout << "Ungroup through a conversion passed\n";
            // The conversion's source deleted (corpus: Set Pickable.cmo): the
            // conversion leaves the graph and its reader goes without, not on
            // the output of an operation no longer in the composition.
            CHECK(DeleteElements(document, firstId,
                                 {ElementRef{ElementKind::Behavior, CKBehavior::Cast(context->GetObject(from))->GetParent()->GetID(), from}})
                      .ok);
            bool still = false;
            for (int i = 0; i < script->GetParameterOperationCount(); ++i) still = still || script->GetParameterOperation(i) == conversion;
            CHECK(!still && input->GetDirectSource() == nullptr);
            CHECK(undo.Undo() && input->GetDirectSource() == conversion->GetOutParameter());
            std::cout << "Deleting a conversion's source passed\n";
        }
        while (undo.UndoCount() > steps) CHECK(undo.Undo());
    }

    // Edit Parameter's type on an input whose local another input reads: the
    // local keeps its type, and the link no longer fits (Dev's "Invalid
    // Parameter Link"); deleting it cuts the input from the local.
    {
        const size_t steps = undo.UndoCount();
        const CK_ID third = InsertBuildingBlock(document, firstId, firstId, withInt, Point(300.0f, 300.0f)).created;
        const EditResult local = AddLocalParameter(document, firstId, firstId, "Two Readers", CKPGUID_INT, Point(100.0f, 600.0f));
        CHECK(third && local.ok);
        int slot = -1;
        for (const auto &param : BuildModel(document, firstId).params)
            if (param.parameter == local.created) slot = param.slot;
        for (CK_ID block : {blocks[0], third})
            CHECK(CreateParameterLink(document, firstId, LinkEndpoint(firstId, slot, ENDPOINT_PLOCAL),
                                      LinkEndpoint(block, input, ENDPOINT_PIN))
                      .ok);
        CKParameterIn *retyped = CKBehavior::Cast(context->GetObject(blocks[0]))->GetInputParameter(input);
        CKParameterLocal *shared = CKParameterLocal::Cast(context->GetObject(local.created));
        // Foreseen with the new type (the links fit until then), deleted
        // first, then the type: undoing gives the type back, then the link.
        CHECK(IncompatibleParameterLinks(document, firstId, retyped->GetID()).empty());
        const std::vector<ElementRef> invalid =
            IncompatibleParameterLinks(document, firstId, retyped->GetID(), CKPGUID_STRING);
        CHECK(invalid.size() == 1);
        // The same type foreseen: nothing.
        CHECK(IncompatibleParameterLinks(document, firstId, retyped->GetID(), CKPGUID_INT).empty());
        {
            EditGroup group(document, "Edit Parameter");
            CHECK(DeleteElements(document, firstId, invalid).ok && retyped->GetDirectSource() != shared);
            CHECK(SetParameterType(document, retyped->GetID(), CKPGUID_STRING).ok);
        }
        CHECK(shared->GetGUID() == CKPGUID_INT && retyped->GetGUID() == CKPGUID_STRING);
        CHECK(IncompatibleParameterLinks(document, firstId, retyped->GetID()).empty());
        CHECK(undo.Undo() && retyped->GetGUID() == CKPGUID_INT && retyped->GetDirectSource() == shared);
        while (undo.UndoCount() > steps) CHECK(undo.Undo());
        CHECK(document.IsDiscarded(third) && document.IsDiscarded(local.created));
    }
    // In each Script a local "Shared" feeding the input; in the first the
    // source to be (another "Shared"), an Int and a Float local.
    const CK_ID scripts[2] = {firstId, secondId};
    for (int s = 0; s < 2; ++s) {
        const EditResult shared = AddLocalParameter(document, scripts[s], scripts[s], "Shared", CKPGUID_INT, Point(100.0f, 440.0f));
        CHECK(shared.ok);
        int slot = -1;
        for (const auto &param : BuildModel(document, scripts[s]).params)
            if (param.parameter == shared.created) slot = param.slot;
        CHECK(CreateParameterLink(document, scripts[s], LinkEndpoint(scripts[s], slot, ENDPOINT_PLOCAL),
                                  LinkEndpoint(blocks[s], input, ENDPOINT_PIN))
                  .ok);
        inputs[s] = CKBehavior::Cast(context->GetObject(blocks[s]))->GetInputParameter(input)->GetID();
    }
    const EditResult source = AddLocalParameter(document, firstId, firstId, "Shared", CKPGUID_INT, Point(100.0f, 400.0f));
    const EditResult retargeted = AddLocalParameter(document, firstId, firstId, "Other", CKPGUID_INT, Point(100.0f, 480.0f));
    const EditResult floating = AddLocalParameter(document, firstId, firstId, "Float", CKPGUID_FLOAT, Point(100.0f, 520.0f));
    CHECK(source.ok && retargeted.ok && floating.ok);
    auto fedBy = [&](int s, CK_ID parameter) {
        CKParameterIn *in = CKParameterIn::Cast(context->GetObject(inputs[s]));
        return in && in->GetDirectSource() == context->GetObject(parameter);
    };

    // In the selected Scripts only (Ctrl + right-click): the second one, not the first.
    {
        const EditResult one = ShortcutizeLocalsInScripts(document, {secondId}, source.created);
        CHECK(one.ok && one.message == "1 Script(s)" && !fedBy(0, source.created) && fedBy(1, source.created));
        CHECK(undo.Undo() && !fedBy(1, source.created));
    }
    // Convert to Shortcut Source in every Script, then Set Shortcut Source.
    const EditResult converted = ExpectUndoRoundTrip(document, "ShortcutizeLocalsInAllScripts",
                                                     [&] { return ShortcutizeLocalsInAllScripts(document, source.created); });
    CHECK(converted.message == "2 Script(s)" && fedBy(0, source.created) && fedBy(1, source.created));
    const EditResult moved = ExpectUndoRoundTrip(document, "RetargetShortcutsInAllScripts", [&] {
        return RetargetShortcutsInAllScripts(document, source.created, retargeted.created);
    });
    CHECK(moved.message == "2 Script(s)" && fedBy(0, retargeted.created) && fedBy(1, retargeted.created));
    // Refused (a Float does not feed an Int input): nothing changes, nothing is recorded.
    {
        const size_t steps = undo.UndoCount();
        const CompositionState before = CaptureComposition(document);
        const EditResult refused = RetargetShortcutsInAllScripts(document, retargeted.created, floating.created);
        std::cout << "Set Shortcut Source to a Float: " << (refused.ok ? "applied" : refused.message) << '\n';
        CHECK(!refused.ok && !refused.Unchanged() && undo.UndoCount() == steps);
        ExpectSameComposition(before, CaptureComposition(document), "a refused Set Shortcut Source");
        CHECK(fedBy(0, retargeted.created) && fedBy(1, retargeted.created));
    }

    // Apply to All: the same-named Building Blocks of both Scripts.
    const std::vector<std::pair<CK_ID, CK_ID>> targets = SameNamedBehaviors(document, blocks[0]);
    CHECK(targets.size() >= 2);
    ExpectUndoRoundTrip(document, "ReplaceBuildingBlocks", [&] {
        bool all = true;
        for (const EditResult &result : ReplaceBuildingBlocks(document, targets, withInt)) all = all && result.ok;
        return all ? EditResult::Applied() : EditResult::Refused("a Building Block was not replaced");
    });

    // The selection's commands.
    ExpectUndoRoundTrip(document, "CreateScriptsOn", [&] {
        const std::vector<EditResult> results = CreateScriptsOn(document, {owner->GetID(), other->GetID()});
        return results.size() == 2 && results[0].ok && results[1].ok ? EditResult::Applied() : EditResult::Refused("not made");
    });
    ExpectUndoRoundTrip(document, "PasteScriptsOn", [&] {
        const std::vector<EditResult> results = PasteScriptsOn(document, {firstId}, {owner->GetID(), other->GetID()});
        return results.size() == 2 && results[0].ok && results[1].ok ? EditResult::Applied() : EditResult::Refused("not pasted");
    });
    ExpectUndoRoundTrip(
        document, "SetHierarchyParents",
        [&] {
            const std::vector<EditResult> results = SetHierarchyParents(document, {owner->GetID(), other->GetID()}, parent->GetID());
            return results.size() == 2 && results[0].ok && results[1].ok ? EditResult::Applied() : EditResult::Refused("not moved");
        },
        {owner->GetID(), other->GetID()});
    CHECK(owner->GetParent() == parent && other->GetParent() == parent);
    {
        Debugger debugger(document);
        const size_t before = debugger.Breakpoints().size();
        CHECK(ToggleBreakpoint(document, firstId, EXTRA_DATA_BEHAVIOR, blocks[0]));
        CHECK(ToggleBreakpoint(document, secondId, EXTRA_DATA_BEHAVIOR, blocks[1]));
        CHECK(debugger.Breakpoints().size() == before + 2);
        ExpectUndoRoundTrip(document, "RemoveBreakpoints", [&] {
            RemoveBreakpoints(document, debugger.Breakpoints());
            return debugger.Breakpoints().empty() ? EditResult::Applied() : EditResult::Refused("breakpoints left");
        });
        CHECK(debugger.Breakpoints().empty());
    }
    std::cout << "Batch commands passed\n";
}


// Every property the Setups edit (ObjectProperties), through the undo their
// edits take (the object's state before and after, as SetupWindows::Live):
// set to another value, undone back to its own, redone.
void ObjectPropertyUndo(CkRuntimeHost &host) {
    CKContext *context = host.Context();
    CKLevel *level = context->GetCurrentLevel();
    auto make = [&](CK_CLASSID cid) {
        CKObject *object = context->CreateObject(cid, (CKSTRING) "Property Object");
        if (level && CKSceneObject::Cast(object)) level->AddObject(CKSceneObject::Cast(object));
        return object;
    };
    // An object of each class the properties are of, as the Setups have
    // them: bitmaps with an image and two slots, a mesh with a face, a curve
    // with two points, a grid's layer, a sound of each kind, a keyed
    // animation with an object animation, a sprite text made.
    std::map<CK_CLASSID, CKObject *> objects;
    for (const ObjectPropertyCheck &property : ObjectPropertyChecks()) {
        const CK_CLASSID cid = property.cid;
        if (objects.count(cid)) continue;
        CKObject *object = nullptr;
        switch (cid) {
        case CKCID_SPRITE: {
            CKSprite *sprite = CKSprite::Cast(make(cid));
            CHECK(sprite && sprite->Create(16, 16, 32, 0));
            sprite->SetSlotCount(2);
            object = sprite;
            break;
        }
        case CKCID_TEXTURE: {
            CKTexture *texture = CKTexture::Cast(make(cid));
            CHECK(texture && texture->Create(16, 16, 32, 0));
            texture->SetSlotCount(2);
            object = texture;
            break;
        }
        case CKCID_MESH: {
            CKMesh *mesh = CKMesh::Cast(make(cid));
            CHECK(mesh);
            mesh->SetVertexCount(3);
            VxVector vertices[] = {VxVector(0.0f, 0.0f, 0.0f), VxVector(1.0f, 0.0f, 0.0f), VxVector(0.0f, 1.0f, 0.0f)};
            for (int vertex = 0; vertex < 3; ++vertex) mesh->SetVertexPosition(vertex, &vertices[vertex]);
            mesh->SetFaceCount(1);
            mesh->SetFaceVertexIndex(0, 0, 1, 2);
            object = mesh;
            break;
        }
        case CKCID_CURVE:
        case CKCID_CURVEPOINT: {
            CKCurve *curve = CKCurve::Cast(make(CKCID_CURVE));
            CHECK(curve);
            CKCurvePoint *first = CKCurvePoint::Cast(make(CKCID_CURVEPOINT));
            CKCurvePoint *second = CKCurvePoint::Cast(make(CKCID_CURVEPOINT));
            CHECK(first && second && curve->AddControlPoint(first) == CK_OK && curve->AddControlPoint(second) == CK_OK);
            second->SetPosition3f(10.0f, 0.0f, 0.0f);
            curve->Update();
            objects[CKCID_CURVE] = curve;
            objects[CKCID_CURVEPOINT] = first;
            continue;
        }
        case CKCID_LAYER: {
            CKGrid *grid = CKGrid::Cast(make(CKCID_GRID));
            CHECK(grid);
            object = GridAddLayer(grid, 1);
            break;
        }
        case CKCID_SOUND: object = make(CKCID_MIDISOUND); break;
        case CKCID_WAVESOUND: {
            // A point sound: its cone and distances apply.
            CKWaveSound *sound = CKWaveSound::Cast(make(cid));
            CHECK(sound);
            sound->SetType(CK_WAVESOUND_POINT);
            object = sound;
            break;
        }
        case CKCID_ANIMATION: {
            CKKeyedAnimation *keyed = CKKeyedAnimation::Cast(make(CKCID_KEYEDANIMATION));
            CKObjectAnimation *part = CKObjectAnimation::Cast(make(CKCID_OBJECTANIMATION));
            CHECK(keyed && part && keyed->AddAnimation(part) == CK_OK);
            keyed->SetLength(30.0f);
            object = keyed;
            break;
        }
        case CKCID_SPRITETEXT: {
            CKSpriteText *text = CKSpriteText::Cast(make(cid));
            CHECK(text && text->Create(64, 16, 32, 0));
            text->SetText((CKSTRING) "Text");
            object = text;
            break;
        }
        default: object = make(cid); break;
        }
        CHECK(object);
        objects[cid] = object;
    }
    Document document(host);
    document.RefreshScripts();
    UndoHistory &undo = document.History();
    undo.SetLimit(1000);
    int checked = 0;
    std::vector<std::string> failed;
    for (const ObjectPropertyCheck &property : ObjectPropertyChecks()) {
        CKObject *object = objects[property.cid];
        const std::string name = std::string(CKClassIDToString(property.cid)) + " #" + std::to_string(checked) + " " + property.label;
        CHECK(object);
        const std::string before = property.text(object);
        const EditResult edit = EditObjectState(document, object->GetID(), property.label, property.setOther);
        const std::string after = property.text(object);
        std::string undone = "-", redone = "-";
        bool ok = edit.ok && after != before;
        if (ok && undo.Undo()) undone = property.text(object);
        if (ok && undo.Redo()) redone = property.text(object);
        ok = ok && property.same(undone, before) && property.same(redone, after);
        if (!ok)
            failed.push_back(name + ": " + before + " -> " + after + ", undone " + undone +
                             ", redone " + redone + (edit.ok ? "" : " (" + edit.message + ")"));
        ++checked;
    }
    for (const std::string &failure : failed) std::cerr << "property undo: " << failure << '\n';
    std::cout << checked << " properties through undo, " << failed.size() << " failed\n";
    CHECK(checked > 0 && failed.empty());
}

// CkArrays: an input, an output and parameters inserted at a slot (there,
// owned by the behavior; refused out of range or of the other type);
// sub-behaviors and scripts of equal priority in the order asked for as they
// are added and as a priority changes.
void CkArrays(CkRuntimeHost &host) {
    CKContext *context = host.Context();
    std::vector<CKObject *> made;
    auto keep = [&made](CKObject *object) {
        CHECK(object);
        made.push_back(object);
        return object;
    };
    auto named = [](CKObject *object) { return std::string(object && object->GetName() ? object->GetName() : ""); };
    CKBehavior *graph = NewScript(context, "Arrays Graph"); // its input Start
    keep(graph);

    // Inputs and outputs.
    graph->CreateInput((CKSTRING) "In B");
    auto *in = CKBehaviorIO::Cast(keep(context->CreateObject(CKCID_BEHAVIORIO, (CKSTRING) "In A")));
    in->SetType(CK_BEHAVIORIO_IN);
    CHECK(!InsertBehaviorIo(graph, 1, in, false)); // an input as an output
    CHECK(!InsertBehaviorIo(graph, 3, in, true));  // past the end
    CHECK(graph->GetInputCount() == 2);
    CHECK(InsertBehaviorIo(graph, 1, in, true));
    CHECK(graph->GetInputCount() == 3 && graph->GetInput(1) == in && named(graph->GetInput(2)) == "In B");
    CHECK(in->GetOwner() == graph);
    CHECK(!InsertBehaviorIo(graph, 0, in, true) && graph->GetInputCount() == 3 && graph->GetInput(1) == in);
    CKBehavior *otherGraph = NewScript(context, "Other Arrays Graph");
    keep(otherGraph);
    CHECK(!InsertBehaviorIo(otherGraph, 0, in, true) && otherGraph->GetInputCount() == 1);
    CHECK(graph->RemoveInput(1) == in && in->GetOwner() == graph);
    CHECK(InsertBehaviorIo(otherGraph, 0, in, true) && in->GetOwner() == otherGraph);
    CHECK(otherGraph->RemoveInput(0) == in);
    CHECK(InsertBehaviorIo(graph, 1, in, true) && graph->GetInput(1) == in);
    CHECK(in->GetOwner() == graph);
    graph->CreateOutput((CKSTRING) "Out B");
    auto *out = CKBehaviorIO::Cast(keep(context->CreateObject(CKCID_BEHAVIORIO, (CKSTRING) "Out A")));
    out->SetType(CK_BEHAVIORIO_OUT);
    CHECK(InsertBehaviorIo(graph, 0, out, false));
    CHECK(graph->GetOutputCount() == 2 && graph->GetOutput(0) == out && named(graph->GetOutput(1)) == "Out B");

    // Parameters: an input in the middle, an output first, a local last.
    graph->CreateInputParameter((CKSTRING) "P0", CKPGUID_INT);
    graph->CreateInputParameter((CKSTRING) "P1", CKPGUID_INT);
    CKParameterIn *pin = context->CreateCKParameterIn((CKSTRING) "P", CKPGUID_INT);
    keep(pin);
    CHECK(!InsertInputParameter(graph, 3, pin));
    CHECK(InsertInputParameter(graph, 1, pin));
    CHECK(graph->GetInputParameterCount() == 3 && graph->GetInputParameter(1) == pin);
    CHECK(named(graph->GetInputParameter(2)) == "P1" && pin->GetOwner() == graph);
    graph->CreateOutputParameter((CKSTRING) "O0", CKPGUID_INT);
    CKParameterOut *pout = context->CreateCKParameterOut((CKSTRING) "O", CKPGUID_INT);
    keep(pout);
    CHECK(InsertOutputParameter(graph, 0, pout));
    CHECK(graph->GetOutputParameterCount() == 2 && graph->GetOutputParameter(0) == pout && pout->GetOwner() == graph);
    graph->CreateLocalParameter((CKSTRING) "L0", CKPGUID_INT);
    CKParameterLocal *local = context->CreateCKParameterLocal((CKSTRING) "L", CKPGUID_INT);
    keep(local);
    CHECK(InsertLocalParameter(graph, 1, local));
    CHECK(graph->GetLocalParameterCount() == 2 && graph->GetLocalParameter(1) == local && local->GetOwner() == graph);
    CHECK(!InsertInputParameter(graph, 0, pin) && graph->GetInputParameterCount() == 3);
    CHECK(!InsertOutputParameter(graph, 0, pout) && graph->GetOutputParameterCount() == 2);
    CHECK(!InsertLocalParameter(otherGraph, 0, local) && otherGraph->GetLocalParameterCount() == 0);
    CHECK(local->GetOwner() == graph && graph->GetLocalParameter(1) == local);
    CHECK(graph->RemoveLocalParameter(1) == local);
    CHECK(local->GetOwner() == graph); // CK removal leaves the prior owner metadata.
    CHECK(InsertLocalParameter(otherGraph, 0, local) && otherGraph->GetLocalParameter(0) == local);
    CHECK(local->GetOwner() == otherGraph);
    CHECK(otherGraph->RemoveLocalParameter(0) == local);
    CHECK(InsertLocalParameter(graph, 1, local) && local->GetOwner() == graph);

    // Sub-behaviors of equal priority where asked, the end out of range; a
    // priority changed: CK's order, the ties as they were.
    auto child = [&](const char *name) {
        return CKBehavior::Cast(keep(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) name)));
    };
    CKBehavior *a = child("A"), *b = child("B"), *c = child("C"), *d = child("D"), *e = child("E");
    CHECK(AddSubBehaviorAt(graph, a, 0) && AddSubBehaviorAt(graph, b, 1) && AddSubBehaviorAt(graph, c, 2));
    CHECK(AddSubBehaviorAt(graph, d, 1) && AddSubBehaviorAt(graph, e, -1));
    auto subs = [&] {
        std::string order;
        for (int i = 0; i < graph->GetSubBehaviorCount(); ++i) order += named(graph->GetSubBehavior(i));
        return order;
    };
    CHECK(subs() == "ADBCE");
    CHECK(!AddSubBehaviorAt(graph, a, 1) && subs() == "ADBCE");
    SetPriorityKeepingTies(b, 10);
    const bool firstHigh = subs() == "BADCE"; // CK puts the higher priority first, or last
    CHECK(firstHigh || subs() == "ADCEB");
    SetPriorityKeepingTies(c, 10);
    CHECK(subs() == (firstHigh ? "BCADE" : "ADEBC"));

    // Scripts: each after its equal priorities; one's priority changed.
    auto *owner = CKBeObject::Cast(keep(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Arrays Owner")));
    CKBehavior *s1 = NewScript(context, "S1"), *s2 = NewScript(context, "S2"), *s3 = NewScript(context, "S3");
    for (CKBehavior *script : {s1, s2, s3}) keep(script);
    CHECK(AddScriptLast(owner, s1) == CK_OK && AddScriptLast(owner, s2) == CK_OK && AddScriptLast(owner, s3) == CK_OK);
    CHECK(AddScriptLast(owner, nullptr) == CKERR_INVALIDPARAMETER);
    auto scripts = [&] {
        std::string order;
        for (int i = 0; i < owner->GetScriptCount(); ++i) order += named(owner->GetScript(i));
        return order;
    };
    CHECK(scripts() == "S1S2S3");
    SetPriorityKeepingTies(s2, 10);
    CHECK(scripts() == (firstHigh ? "S2S1S3" : "S1S3S2"));
    std::cout << "sub-behaviors " << subs() << ", scripts " << scripts() << '\n';

    for (CKObject *object : made) context->DestroyObject(object);
}

} // namespace vdevtest
