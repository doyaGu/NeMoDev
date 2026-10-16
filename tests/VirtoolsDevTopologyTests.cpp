// Topology edits, undo and redo, the synthetic fuzz and a sample's random edits.
#include "VirtoolsDevTestSupport.h"
#include "core/runtime/SdkCompat.h"

namespace vdevtest {

void DeterministicEdits(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes) {
    CKContext *context = host.Context();
    auto *script = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Command Script"));
    script->UseGraph();
    script->SetType(CKBEHAVIORTYPE_SCRIPT);
    script->CreateInput((CKSTRING) "Start");
    const CK_ID id = script->GetID();

    Document document(host);
    document.RefreshScripts();
    UndoHistory &undo = document.History();
    undo.SetLimit(1000);
    CHECK(document.Session(id) && EditScriptData(document, id, "Open Script",
                                                  [](InterfaceData &data) { SetScriptOpen(data, true); }));
    Recorder recorder(document, id);

    EditResult a = InsertBuildingBlock(document, id, id, prototypes[0], Point(300, 100));
    recorder.After(a, "insert A");
    CHECK(a.ok && a.created);
    // Every input parameter gets a value local of the Script, linked to it.
    CKBehavior *blockA = CKBehavior::Cast(context->GetObject(a.created));
    for (int i = 0; i < blockA->GetInputParameterCount(); ++i) {
        CKParameterLocal *value = CKParameterLocal::Cast(blockA->GetInputParameter(i)->GetDirectSource());
        CHECK(value && script->GetLocalParameterPosition(value) >= 0);
    }
    EditResult b = InsertBuildingBlock(document, id, id, prototypes[1], Point(500, 100));
    recorder.After(b, "insert B");
    CHECK(b.ok);
    // Inserting into a Building Block is refused.
    recorder.After(InsertBuildingBlock(document, id, a.created, prototypes[0], Point(0, 0)), "insert into BB");

    SchematicModel model = BuildModel(document, id);
    const PinItem *aIn = FindPin(model, a.created, PinKind::BehaviorIn);
    const PinItem *aOut = FindPin(model, a.created, PinKind::BehaviorOut);
    const PinItem *bIn = FindPin(model, b.created, PinKind::BehaviorIn);
    CHECK(aIn && aOut && bIn);
    // Either drag direction makes the same link.
    EditResult startLink = CreateBehaviorLink(document, id, aIn->endpoint, StartEndpoint(document, id));
    recorder.After(startLink, "Start -> A");
    CHECK(startLink.ok);
    EditResult ab = CreateBehaviorLink(document, id, aOut->endpoint, bIn->endpoint);
    recorder.After(ab, "A -> B");
    CHECK(ab.ok);
    recorder.After(CreateBehaviorLink(document, id, aOut->endpoint, bIn->endpoint), "duplicate link");
    recorder.After(CreateBehaviorLink(document, id, aIn->endpoint, bIn->endpoint), "input to input");
    auto *runtimeLink = CKBehaviorLink::Cast(context->GetObject(ab.created));
    CHECK(runtimeLink && runtimeLink->GetInitialActivationDelay() == 0);
    recorder.After(SetLinkDelay(document, id, ab.created, 3), "delay");
    CHECK(runtimeLink->GetInitialActivationDelay() == 3);

    // local(int) -> Addition.in1; Addition.out -> local2 (destination).
    EditResult local = AddLocalParameter(document, id, id, "Counter", IntGuid(), Point(300, 300));
    recorder.After(local, "add local");
    EditResult local2 = AddLocalParameter(document, id, id, "Sum", IntGuid(), Point(300, 340));
    recorder.After(local2, "add local 2");
    // Dev Alt+T: a <This> local typed as the owner's class (refused without an owner).
    EditResult self = AddThisParameter(document, id, id, Point(300, 380));
    recorder.After(self, "add <This>");
    if (self.ok) {
        auto *myself = CKParameterLocal::Cast(context->GetObject(self.created));
        CHECK(myself && myself->IsMyselfParameter());
    }
    CKParameterManager *manager = context->GetParameterManager();
    const CKGUID addition = manager->OperationNameToGuid((CKSTRING) "Addition");
    EditResult op = AddParameterOperation(document, id, id, addition, IntGuid(), IntGuid(), IntGuid(),
                                          Point(500, 300));
    recorder.After(op, "add operation");
    // Values: text parsed by the type, raw bytes, refused text, no-op.
    EditResult value = SetParameterText(document, id, local.created, "42");
    recorder.After(value, "set value");
    int counterValue = 0;
    CKParameter::Cast(context->GetObject(local.created))->GetValue(&counterValue, FALSE);
    CHECK(value.ok && counterValue == 42);
    std::vector<unsigned char> seven(sizeof(int));
    const int sevenValue = 7;
    std::memcpy(seven.data(), &sevenValue, sizeof(int));
    recorder.After(SetParameterBytes(document, id, local2.created, seven), "set bytes");
    recorder.After(SetParameterText(document, id, local.created, "42"), "unchanged value");
    for (int i = 0; i < blockA->GetInputParameterCount(); ++i)
        if (CKParameterLocal *holder = EditableValueOf(blockA->GetInputParameter(i)))
            recorder.After(SetParameterText(document, id, holder->GetID(), "1"), "input value");
    CHECK(local.ok && local2.ok && op.ok);
    auto slotOf = [&](CK_ID local) {
        return script->GetLocalParameterPosition(CKParameterLocal::Cast(context->GetObject(local)));
    };
    EditResult toOp = CreateParameterLink(document, id, LinkEndpoint(id, slotOf(local.created), ENDPOINT_PLOCAL),
                                          LinkEndpoint(op.created, 0, ENDPOINT_PIN));
    recorder.After(toOp, "local -> op");
    CHECK(toOp.ok);
    auto *operation = CKParameterOperation::Cast(context->GetObject(op.created));
    CHECK(operation->GetInParameter1()->GetDirectSource() == context->GetObject(local.created));
    EditResult fromOp = CreateParameterLink(document, id, LinkEndpoint(id, slotOf(local2.created), ENDPOINT_PLOCAL),
                                            LinkEndpoint(op.created, 0, ENDPOINT_POUT));
    recorder.After(fromOp, "op -> local2");
    CHECK(fromOp.ok && operation->GetOutParameter()->GetDestinationCount() == 1);
    // Replacing a source prunes the old stored link.
    EditResult replace = CreateParameterLink(document, id, LinkEndpoint(id, slotOf(local2.created), ENDPOINT_PLOCAL),
                                             LinkEndpoint(op.created, 0, ENDPOINT_PIN));
    recorder.After(replace, "replace source");
    CHECK(replace.ok);
    size_t toOpLinks = 0;
    for (const auto &link : document.Session(id)->Data().rootBehavior.links)
        if (link.end.id == op.created && link.end.type == ENDPOINT_PIN && link.end.index == 0) ++toOpLinks;
    CHECK(toOpLinks == 1);

    recorder.After(RenameObject(document, id, a.created, "Renamed"), "rename");
    for (CK_ID behavior : {a.created, b.created})
        if (CKBehavior::Cast(context->GetObject(behavior))->IsTargetable())
            recorder.After(SetTargetUsed(document, id, behavior, true), "use target");

    // Deleting A removes both of its links; deleting the local removes its
    // link and renumbers the other local's endpoints.
    SchematicModel current = BuildModel(document, id);
    const BehaviorItem *aItem = current.FindBehavior(a.created);
    CHECK(aItem);
    EditResult deleted = DeleteElements(document, id, {aItem->ref});
    recorder.After(deleted, "delete A");
    CHECK(deleted.ok && document.IsDiscarded(a.created));
    for (const auto &link : document.Session(id)->Data().rootBehavior.links)
        CHECK(link.start.id != a.created && link.end.id != a.created);
    const int locals = script->GetLocalParameterCount();
    EditResult deletedLocal = DeleteElements(
        document, id, {ElementRef{ElementKind::LocalParam, id, local.created, slotOf(local.created)}});
    recorder.After(deletedLocal, "delete local");
    CHECK(deletedLocal.ok && script->GetLocalParameterCount() == locals - 1);
    EditResult deletedOp =
        DeleteElements(document, id, {ElementRef{ElementKind::Operation, id, op.created}});
    recorder.After(deletedOp, "delete operation");
    CHECK(deletedOp.ok && script->GetParameterOperationCount() == 0);

    // A behavior graph with its own pins, linked inside and outside.
    EditResult graph = CreateBehaviorGraph(document, id, id, "Group", Point(300, 400));
    recorder.After(graph, "create graph");
    CHECK(graph.ok);
    recorder.After(AddGraphPin(document, id, graph.created, PinKind::BehaviorIn, "In 1"), "graph bIn");
    recorder.After(AddGraphPin(document, id, graph.created, PinKind::ParamIn, "Value", IntGuid()), "graph pIn");
    recorder.After(AddGraphPin(document, id, graph.created, PinKind::ParamOut, "Result", IntGuid()), "graph pOut");
    recorder.After(AddGraphPin(document, id, id, PinKind::BehaviorOut, "Out"), "Script output (refused)");
    CKBehavior *group = CKBehavior::Cast(context->GetObject(graph.created));
    CHECK(group->GetInputCount() == 2 && group->GetInputParameterCount() == 1 && group->GetOutputParameterCount() == 1);
    EditResult inner = InsertBuildingBlock(document, id, graph.created, prototypes[0], Point(340, 440));
    recorder.After(inner, "insert inside graph");
    CHECK(inner.ok);
    recorder.After(CreateBehaviorLink(document, id, StartEndpoint(document, id),
                                      LinkEndpoint(graph.created, 0, ENDPOINT_BIN)),
                   "Start -> graph");
    EditResult inside = CreateBehaviorLink(document, id, LinkEndpoint(graph.created, 1, ENDPOINT_BIN),
                                           LinkEndpoint(inner.created, 0, ENDPOINT_BIN));
    recorder.After(inside, "graph bIn 1 -> inner");
    CHECK(inside.ok);
    recorder.After(CreateBehaviorLink(document, id, LinkEndpoint(inner.created, 0, ENDPOINT_BOUT),
                                      LinkEndpoint(graph.created, 0, ENDPOINT_BOUT)),
                   "inner -> graph bOut");
    EditResult shared = CreateParameterLink(document, id, LinkEndpoint(graph.created, 0, ENDPOINT_PIN),
                                            LinkEndpoint(inner.created, 0, ENDPOINT_PIN));
    recorder.After(shared, "graph pIn -> inner pIn");
    // Deleting graph pins renumbers nothing here but removes their links.
    EditResult deletedIn = DeleteElements(
        document, id, {ElementRef{ElementKind::Pin, graph.created, graph.created, 0, -1, PinKind::BehaviorIn}});
    recorder.After(deletedIn, "delete graph bIn 0");
    CHECK(deletedIn.ok && group->GetInputCount() == 1);
    for (const auto &record : document.Session(id)->Data().behaviors)
        for (const auto &link : record.links)
            if (link.IsBehaviorLink() && link.start.id == graph.created && link.start.type == ENDPOINT_BIN)
                CHECK(link.start.index == 0); // former bIn 1
    EditResult deletedPin = DeleteElements(
        document, id, {ElementRef{ElementKind::Pin, graph.created, graph.created, 0, -1, PinKind::ParamIn}});
    recorder.After(deletedPin, "delete graph pIn");
    CHECK(deletedPin.ok && group->GetInputParameterCount() == 0);

    // Ungroup: links through the graph become direct, shared inputs take
    // the graph parameter's source, locals and children move up.
    {
        EditResult outer = CreateBehaviorGraph(document, id, id, "Outer", Point(300, 600));
        recorder.After(outer, "create outer graph");
        CHECK(outer.ok);
        EditResult child = InsertBuildingBlock(document, id, outer.created, prototypes[1], Point(340, 640));
        recorder.After(child, "insert into outer");
        CHECK(child.ok);
        CKBehavior *childBehavior = CKBehavior::Cast(context->GetObject(child.created));
        CKBehavior *outerBehavior = CKBehavior::Cast(context->GetObject(outer.created));
        recorder.After(CreateBehaviorLink(document, id, StartEndpoint(document, id),
                                          LinkEndpoint(outer.created, 0, ENDPOINT_BIN)),
                       "Start -> outer");
        recorder.After(CreateBehaviorLink(document, id, LinkEndpoint(outer.created, 0, ENDPOINT_BIN),
                                          LinkEndpoint(child.created, 0, ENDPOINT_BIN)),
                       "outer in -> child");
        if (childBehavior->GetInputParameterCount() > 0) {
            const CKGUID type = childBehavior->GetInputParameter(0)->GetGUID();
            recorder.After(AddGraphPin(document, id, outer.created, PinKind::ParamIn, "Shared", type), "outer pIn");
            recorder.After(CreateParameterLink(document, id, LinkEndpoint(outer.created, 0, ENDPOINT_PIN),
                                               LinkEndpoint(child.created, 0, ENDPOINT_PIN)),
                           "outer pIn -> child pIn");
        }
        const int linksBefore = script->GetSubBehaviorLinkCount();
        EditResult ungrouped = UngroupGraph(document, id, outer.created);
        recorder.After(ungrouped, "ungroup");
        CHECK(ungrouped.ok);
        CHECK(childBehavior->GetParent() == script && document.IsDiscarded(outer.created));
        bool startToChild = false;
        for (int i = 0; i < script->GetSubBehaviorLinkCount(); ++i) {
            CKBehaviorLink *link = script->GetSubBehaviorLink(i);
            startToChild = startToChild || (link->GetInBehaviorIO() == script->GetInput(0) &&
                                             link->GetOutBehaviorIO() == childBehavior->GetInput(0));
        }
        CHECK(startToChild && script->GetSubBehaviorLinkCount() == linksBefore);
        (void)outerBehavior;

        // Group the child and B again: Start now reaches the child through
        // a graph input; ungrouping restores a direct link.
        EditResult grouped = GroupBehaviors(document, id, {child.created, b.created});
        recorder.After(grouped, "group");
        CHECK(grouped.ok);
        CKBehavior *newGraph = CKBehavior::Cast(context->GetObject(grouped.created));
        CHECK(newGraph && childBehavior->GetParent() == newGraph && newGraph->GetInputCount() >= 1);
        recorder.After(UngroupGraph(document, id, grouped.created), "ungroup again");
        CHECK(childBehavior->GetParent() == script);
    }

    // Copy/paste: the graph (with its child, pins and inner links) and B.
    const InterfaceData clipboard = document.Session(id)->Data();
    const int before = script->GetSubBehaviorCount();
    EditResult pasted = PasteBehaviors(document, clipboard, {graph.created, b.created}, {}, id, id, Point(700, 400));
    recorder.After(pasted, "paste");
    CHECK(pasted.ok && script->GetSubBehaviorCount() == before + 2);
    CKBehavior *pastedGraph = nullptr;
    for (int i = before; i < script->GetSubBehaviorCount(); ++i)
        if (script->GetSubBehavior(i)->GetType() != CKBEHAVIORTYPE_BASE) pastedGraph = script->GetSubBehavior(i);
    CHECK(pastedGraph && pastedGraph->GetSubBehaviorCount() == group->GetSubBehaviorCount() &&
           pastedGraph->GetSubBehaviorLinkCount() == group->GetSubBehaviorLinkCount());
    // Pasting into a Building Block is refused.
    recorder.After(PasteBehaviors(document, clipboard, {b.created}, {}, id, b.created, Point(0, 0)), "paste into BB");

    recorder.UndoAllRedoAll();
    std::cout << "Deterministic topology edits passed (" << recorder.Applied() << " commands)\n";

    // Moving a link end: delete + create joined into one undo step.
    {
        const State before = Capture(document, id);
        const size_t depth = undo.UndoCount();
        EditResult x = InsertBuildingBlock(document, id, id, prototypes[0], Point(900, 100));
        EditResult y = InsertBuildingBlock(document, id, id, prototypes[1], Point(900, 200));
        CHECK(x.ok && y.ok);
        EditResult linked = CreateBehaviorLink(document, id, StartEndpoint(document, id),
                                               LinkEndpoint(x.created, 0, ENDPOINT_BIN));
        CHECK(linked.ok);
        SchematicModel model = BuildModel(document, id);
        ElementRef linkRef;
        for (const auto &link : model.links)
            if (link.ref.object == linked.created) linkRef = link.ref;
        {
            Edit edit(document, "Move Link");
            CHECK(DeleteElements(document, id, {linkRef}).ok);
            CHECK(CreateBehaviorLink(document, id, StartEndpoint(document, id),
                                      LinkEndpoint(y.created, 0, ENDPOINT_BIN)).ok);
            CHECK(edit.Commit().ok);
        }
        const State moved = Capture(document, id);
        CHECK(undo.UndoLabel() == "Move Link");
        CHECK(undo.Undo());
        CHECK(undo.UndoCount() == depth + 3);
        CHECK(undo.Redo());
        ExpectSame(moved, Capture(document, id), "redo merged move");
        while (undo.UndoCount() > depth) CHECK(undo.Undo());
        ExpectSame(before, Capture(document, id), "undo merged move");
        std::cout << "Merged undo step passed\n";
    }

    // The depth counts undo steps: an edit holds any number of commands as
    // one, and the oldest steps go past the depth.
    {
        // The history kept by the document, made 3 steps deep for the check.
        UndoHistory &small = document.History();
        small.Clear();
        small.SetLimit(3);
        int value = 0;
        for (int i = 1; i <= 3; ++i) CHECK(RecordValue(document, "Set", value, i));
        {
            Edit all(document, "Set All");
            for (int i = 4; i <= 9; ++i) CHECK(RecordValue(document, "Set", value, i));
            CHECK(small.UndoCount() == 3 && all.Commit().ok);
        }
        CHECK(small.UndoCount() == 3 && small.UndoLabel() == "Set All"); // the first evicted
        CHECK(small.Undo() && value == 3);
        CHECK(small.Redo() && value == 9);
        {
            Edit dropped(document, "Dropped");
            for (int i = 10; i <= 15; ++i) CHECK(RecordValue(document, "Set", value, i));
        }
        CHECK(value == 9 && small.UndoLabel() == "Set All");
        while (small.Undo()) {
        }
        CHECK(value == 1); // the first step was evicted
        small.Clear();
        small.SetLimit(1000);
        std::cout << "Undo depth passed\n";
    }

    // A deleted Building Block's I/Os and parameters are discarded with it:
    // they still name the graph, but the scans deciding what is used skip them.
    {
        const size_t depth = undo.UndoCount();
        EditResult z = InsertBuildingBlock(document, id, id, prototypes[0], Point(1300, 100));
        CHECK(z.ok);
        CKBehavior *block = CKBehavior::Cast(context->GetObject(z.created));
        const CK_ID io = block->GetInput(0)->GetID();
        const CK_ID pin = block->GetInputParameterCount() > 0 ? block->GetInputParameter(0)->GetID() : 0;
        CHECK(!document.IsDiscarded(z.created) && !document.IsDiscarded(io) && !document.IsDiscarded(pin));
        const SchematicModel model = BuildModel(document, id);
        const BehaviorItem *item = model.FindBehavior(z.created);
        CHECK(item && DeleteElements(document, id, {item->ref}).ok);
        CHECK(document.IsDiscarded(z.created) && document.IsDiscarded(io) && (!pin || document.IsDiscarded(pin)));
        CHECK(!document.IsDiscarded(id));
        CHECK(undo.Undo() && !document.IsDiscarded(io));
        while (undo.UndoCount() > depth) CHECK(undo.Undo());
        std::cout << "Discarded objects passed\n";
    }

    // Dev "Auto Loop Link Delay": a link closing a zero-delay loop gets 1.
    {
        const size_t depth = undo.UndoCount();
        EditResult x = InsertBuildingBlock(document, id, id, prototypes[0], Point(1100, 100));
        EditResult y = InsertBuildingBlock(document, id, id, prototypes[1], Point(1100, 200));
        CHECK(x.ok && y.ok);
        EditResult forward = CreateBehaviorLink(document, id, LinkEndpoint(x.created, 0, ENDPOINT_BOUT),
                                                LinkEndpoint(y.created, 0, ENDPOINT_BIN));
        EditResult back = CreateBehaviorLink(document, id, LinkEndpoint(y.created, 0, ENDPOINT_BOUT),
                                             LinkEndpoint(x.created, 0, ENDPOINT_BIN));
        CHECK(forward.ok && back.ok);
        CHECK(CKBehaviorLink::Cast(context->GetObject(forward.created))->GetInitialActivationDelay() == 0);
        CHECK(CKBehaviorLink::Cast(context->GetObject(back.created))->GetInitialActivationDelay() == 1);
        undo.Undo();
        EditResult plain = CreateBehaviorLink(document, id, LinkEndpoint(y.created, 0, ENDPOINT_BOUT),
                                              LinkEndpoint(x.created, 0, ENDPOINT_BIN), 0, false);
        CHECK(plain.ok && CKBehaviorLink::Cast(context->GetObject(plain.created))->GetInitialActivationDelay() == 0);
        while (undo.UndoCount() > depth) CHECK(undo.Undo());
        std::cout << "Auto loop link delay passed\n";
    }
    undo.Clear();
    document.Close();
}
// Dev "Add <This> Parameter": a myself local of the owner's class type.
void ThisParameter(CkRuntimeHost &host) {
    CKContext *context = host.Context();
    CKLevel *level = context->GetCurrentLevel();
    auto *script = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Level Script"));
    script->UseGraph();
    script->SetType(CKBEHAVIORTYPE_SCRIPT);
    script->CreateInput((CKSTRING) "Start");
    CHECK(level && level->AddScript(script) == CK_OK && script->GetOwner() == level);
    const CK_ID id = script->GetID();
    {
        Document document(host);
        document.RefreshScripts();
        UndoHistory &undo = document.History();
        undo.SetLimit(100);
        CHECK(document.Session(id) && EditScriptData(document, id, "Open Script",
                                                      [](InterfaceData &data) { SetScriptOpen(data, true); }));
        Recorder recorder(document, id);
        EditResult self = AddThisParameter(document, id, id, Point(300, 100));
        recorder.After(self, "add <This>");
        auto *myself = CKParameterLocal::Cast(context->GetObject(self.created));
        CHECK(self.ok && myself && myself->IsMyselfParameter());
        CHECK(myself->GetGUID() == context->GetParameterManager()->ClassIDToGuid(CKCID_LEVEL));
        CHECK(undo.Undo() && script->GetLocalParameterCount() == 0);
        CHECK(undo.Redo() && script->GetLocalParameterCount() == 1 && script->GetLocalParameter(0) == myself);

        // Initial conditions in the level scene: Set, Remove and their undo.
        CKScene *scene = InitialConditionsScene(context);
        if (scene && scene->IsObjectHere(script)) {
            CHECK(!scene->GetObjectInitialValue(script));
            EditResult set = ApplyInitialConditions(document, {id}, InitialConditions::Set);
            CHECK(set.ok && scene->GetObjectInitialValue(script));
            CHECK(undo.Undo() && !scene->GetObjectInitialValue(script));
            CHECK(undo.Redo() && scene->GetObjectInitialValue(script));
            EditResult restore = ApplyInitialConditions(document, {id}, InitialConditions::Restore);
            CHECK(restore.ok && undo.Undo() && undo.Redo());
            EditResult removed = ApplyInitialConditions(document, {id}, InitialConditions::Remove);
            CHECK(removed.ok && !scene->GetObjectInitialValue(script));
            CHECK(undo.Undo() && scene->GetObjectInitialValue(script));
            CHECK(!ApplyInitialConditions(document, {0}, InitialConditions::Set).ok);
            std::cout << "Initial conditions passed\n";

            // Copy / paste a Script onto the level, then delete the copy.
            const int scripts = level->GetScriptCount();
            CKStateChunk *originalChunk = script->GetInterfaceChunk();
            EditResult pasted = PasteScripts(document, {id}, level->GetID());
    CHECK(pasted.ok && level->GetScriptCount() == scripts + 1);
            CKBehavior *copy = CKBehavior::Cast(context->GetObject(pasted.created));
            CHECK(copy && copy != script && script->GetInterfaceChunk() == originalChunk);
            ScriptSession *copied = document.Session(copy->GetID());
            CHECK(copied && copied->Origin() != SessionOrigin::LoadFailed);
            CHECK(RuntimeConsistencyIssues(context, copied->Data(), copy).empty());
            CHECK(GenerateInterfaceChunk(document.Session(id)->Data(), script));
            EditResult deleted = DeleteScript(document, copy->GetID());
            CHECK(deleted.ok && level->GetScriptCount() == scripts);
            CHECK(undo.Undo() && level->GetScriptCount() == scripts + 1);
            CHECK(undo.Undo() && level->GetScriptCount() == scripts);
            CHECK(undo.Redo() && level->GetScriptCount() == scripts + 1);
            CHECK(undo.Undo() && level->GetScriptCount() == scripts);
            std::cout << "Script copy/delete passed\n";
        } else {
            std::cout << "Initial conditions skipped (the Script is not in a scene)\n";
        }
    }
    level->RemoveScript(id);
    std::cout << "<This> parameter passed\n";
}

// A Script whose InterfaceChunk was cleared (no data) or cannot be parsed
// opens with its layout rebuilt from the runtime graph, editable; its chunk
// stays as it was until the layout is edited.
void UnreadableChunks(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes) {
    CKContext *context = host.Context();
    CKLevel *level = context->GetCurrentLevel();
    auto *script = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Cleared Script"));
    script->UseGraph();
    script->SetType(CKBEHAVIORTYPE_SCRIPT);
    script->CreateInput((CKSTRING) "Start");
    CHECK(level && level->AddScript(script) == CK_OK);
    if (!prototypes.empty())
        if (auto *block = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Block"))) {
            block->InitFromGuid(prototypes.front());
            script->AddSubBehavior(block);
        }
    const CK_ID id = script->GetID();
    auto chunkWith = [](const std::function<void(CKStateChunk *)> &write) {
        CKStateChunk *chunk = CreateCKStateChunk(static_cast<CK_CLASSID>(0), nullptr);
        chunk->StartWrite();
        write(chunk);
        chunk->CloseChunk();
        return chunk;
    };
    const struct {
        const char *what;
        std::function<void(CKStateChunk *)> write;
        const char *message;
    } cases[] = {
        {"cleared", [](CKStateChunk *) {}, "cleared"},
        {"unreadable",
         [](CKStateChunk *chunk) {
             chunk->WriteIdentifier(0x12345678);
             chunk->WriteInt(7);
         },
         "unreadable"},
    };
    for (const auto &c : cases) {
        CKStateChunk *chunk = chunkWith(c.write);
        script->SetInterfaceChunk(chunk);
        Document document(host);
        document.RefreshScripts();
        UndoHistory &undo = document.History();
        undo.SetLimit(100);
        ScriptSession *session = document.Session(id);
        CHECK(session && !session->ReadOnly() && session->Origin() == SessionOrigin::Reconstructed);
        CHECK(session->LoadMessage().find(c.message) != std::string::npos);
        CHECK(session->Data().behaviors.size() == static_cast<size_t>(script->GetSubBehaviorCount()));
        CHECK(!session->Dirty() && script->GetInterfaceChunk() == chunk); // not a change: the chunk is kept
        CHECK(EditScriptData(document, id, "Open Script", [](InterfaceData &data) { SetScriptOpen(data, true); }));
        CHECK(session->Dirty());
        std::cout << "InterfaceChunk " << c.what << ": layout rebuilt\n";
    }
    script->SetInterfaceChunk(nullptr);
    level->RemoveScript(id);
    std::cout << "Unreadable InterfaceChunks passed\n";
}

// Dev exported parameters and pins: Script > Outer > Inner > Building Block.
// Parameter links between graphs: the link ends at a shortcut, as Dev saves
// them (a shortcut of the source in the input's graph; a shortcut of the
// written parameter in the output's graph).
void CrossGraphLinks(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes) {
    CKContext *context = host.Context();
    auto *script = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Cross Graph Script"));
    script->UseGraph();
    script->SetType(CKBEHAVIORTYPE_SCRIPT);
    script->CreateInput((CKSTRING) "Start");
    const CK_ID id = script->GetID();
    Document document(host);
    document.RefreshScripts();
    UndoHistory &undo = document.History();
    undo.SetLimit(1000);
    CHECK(EditScriptData(document, id, "Open Script", [](InterfaceData &data) { SetScriptOpen(data, true); }));
    Recorder recorder(document, id);
    EditResult inner = CreateBehaviorGraph(document, id, id, "Inner", Point(300, 100));
    recorder.After(inner, "inner graph");
    CHECK(inner.ok);
    CK_ID block = 0;
    for (const CKGUID &guid : prototypes) {
        EditResult inserted = InsertBuildingBlock(document, id, inner.created, guid, Point(340, 160));
        if (!inserted.ok) continue;
        CKBehavior *behavior = CKBehavior::Cast(context->GetObject(inserted.created));
        if (behavior->GetInputParameterCount() > 0 && behavior->GetOutputParameterCount() > 0) {
            recorder.After(inserted, "insert block");
            block = inserted.created;
            break;
        }
        undo.Undo();
    }
    if (!block) {
        std::cout << "Cross-graph links skipped (no Building Block with input and output parameters)\n";
        return;
    }
    CKBehavior *bb = CKBehavior::Cast(context->GetObject(block));
    CKParameterIn *x = bb->GetInputParameter(0);
    CKParameterOut *y = bb->GetOutputParameter(0);
    const int unresolvedBefore = BuildModel(document, id).unresolvedEndpoints;
    auto shortcutOf = [&](CK_ID graph, CK_ID shown) {
        const BehaviorData *record = FindRecord(document.Session(id)->Data(), graph);
        for (int i = 0; record && i < static_cast<int>(record->sharedParams.size()); ++i)
            if (record->sharedParams[i].sourceId == shown) return i;
        return -1;
    };
    auto linked = [&](CK_ID graph, const LinkEndpoint &start, const LinkEndpoint &end) {
        const BehaviorData *record = FindRecord(document.Session(id)->Data(), graph);
        auto same = [](const LinkEndpoint &p, const LinkEndpoint &q) {
            return p.id == q.id && p.index == q.index && p.type == q.type;
        };
        for (const Link &link : record ? record->links : std::vector<Link>())
            if (same(link.start, start) && same(link.end, end)) return true;
        return false;
    };
    // A local of the Script feeds the block's input inside the graph.
    EditResult outside = AddLocalParameter(document, id, id, "Outside", x->GetGUID(), Point(120, 60));
    recorder.After(outside, "Script local");
    CKParameterLocal *local = CKParameterLocal::Cast(context->GetObject(outside.created));
    CHECK(outside.ok && local);
    const LinkEndpoint localEnd(id, script->GetLocalParameterPosition(local), ENDPOINT_PLOCAL);
    EditResult in = CreateParameterLink(document, id, localEnd, LinkEndpoint(block, 0, ENDPOINT_PIN));
    recorder.After(in, "Script local -> input in a graph");
    const int inShortcut = shortcutOf(inner.created, local->GetID());
    CHECK(in.ok && x->GetDirectSource() == local && inShortcut >= 0 &&
           linked(inner.created, LinkEndpoint(inner.created, inShortcut, ENDPOINT_POUT_SHORTCUT), LinkEndpoint(block, 0, ENDPOINT_PIN)));
    // The block's output writes a local of the Script.
    EditResult written = AddLocalParameter(document, id, id, "Written", y->GetGUID(), Point(120, 200));
    recorder.After(written, "second Script local");
    CKParameterLocal *target = CKParameterLocal::Cast(context->GetObject(written.created));
    CHECK(written.ok && target);
    const LinkEndpoint targetEnd(id, script->GetLocalParameterPosition(target), ENDPOINT_PLOCAL);
    EditResult out = CreateParameterLink(document, id, LinkEndpoint(block, 0, ENDPOINT_POUT), targetEnd);
    recorder.After(out, "output in a graph -> Script local");
    const int outShortcut = shortcutOf(inner.created, target->GetID());
    bool writes = false;
    for (int i = 0; i < y->GetDestinationCount(); ++i) writes = writes || y->GetDestination(i) == target;
    CHECK(out.ok && writes && outShortcut >= 0 &&
           linked(inner.created, LinkEndpoint(block, 0, ENDPOINT_POUT), LinkEndpoint(inner.created, outShortcut, ENDPOINT_POUT_SHORTCUT)));
    recorder.After(CreateParameterLink(document, id, LinkEndpoint(block, 0, ENDPOINT_POUT), targetEnd), "same link again");
    CHECK(BuildModel(document, id).unresolvedEndpoints == unresolvedBefore);
    recorder.UndoAllRedoAll();
    std::cout << "Cross-graph parameter links passed\n";
}

void ExportedParameters(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes) {
    CKContext *context = host.Context();
    auto *script = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Export Script"));
    script->UseGraph();
    script->SetType(CKBEHAVIORTYPE_SCRIPT);
    script->CreateInput((CKSTRING) "Start");
    const CK_ID id = script->GetID();
    Document document(host);
    document.RefreshScripts();
    UndoHistory &undo = document.History();
    undo.SetLimit(1000);
    CHECK(EditScriptData(document, id, "Open Script", [](InterfaceData &data) { SetScriptOpen(data, true); }));
    Recorder recorder(document, id);
    EditResult outer = CreateBehaviorGraph(document, id, id, "Outer", Point(300, 100));
    recorder.After(outer, "outer graph");
    EditResult inner = CreateBehaviorGraph(document, id, outer.created, "Inner", Point(320, 140));
    recorder.After(inner, "inner graph");
    CHECK(outer.ok && inner.ok);
    EditScriptData(document, id, "Sizes", [&](InterfaceData &data) {
        FindRecord(data, outer.created)->hExpandSize = 400;
        FindRecord(data, outer.created)->vExpandSize = 300;
    });
    recorder.After(EditResult{true}, "sizes");
    // A Building Block with an input and an output parameter.
    CK_ID block = 0;
    for (const CKGUID &guid : prototypes) {
        EditResult inserted = InsertBuildingBlock(document, id, inner.created, guid, Point(360, 180));
        if (!inserted.ok) continue;
        CKBehavior *behavior = CKBehavior::Cast(context->GetObject(inserted.created));
        if (behavior->GetInputParameterCount() > 0 && behavior->GetOutputParameterCount() > 0 &&
            behavior->GetInputCount() > 0 && behavior->GetOutputCount() > 0) {
            recorder.After(inserted, "insert block");
            block = inserted.created;
            break;
        }
        undo.Undo();
    }
    if (!block) {
        std::cout << "Exported parameters skipped (no Building Block with input and output parameters)\n";
        return;
    }
    CKBehavior *bb = CKBehavior::Cast(context->GetObject(block));
    CKBehavior *outerGraph = CKBehavior::Cast(context->GetObject(outer.created));
    CKBehavior *innerGraph = CKBehavior::Cast(context->GetObject(inner.created));
    CKParameterIn *x = bb->GetInputParameter(0);
    CKParameterOut *y = bb->GetOutputParameter(0);
    CKParameterLocal *holder = CKParameterLocal::Cast(x->GetDirectSource());
    CHECK(holder && innerGraph->GetLocalParameterPosition(holder) >= 0);
    auto unresolved = [&] { return BuildModel(document, id).unresolvedEndpoints; };
    const int unresolvedBefore = unresolved();

    // Export the input through both graphs: the same instance, its owner
    // kept; the value holder moves to the Script.
    recorder.After(ExportParameter(document, id, LinkEndpoint(block, 0, ENDPOINT_PIN), inner.created),
                   "export pIn into its own graph (refused)");
    EditResult exported = ExportParameter(document, id, LinkEndpoint(block, 0, ENDPOINT_PIN), id);
    recorder.After(exported, "export pIn");
    if (!exported.ok) std::cerr << "export: " << exported.message << std::endl;
    CHECK(exported.ok);
    CHECK(innerGraph->GetInputParameterPosition(x) == 0 && outerGraph->GetInputParameterPosition(x) == 0);
    CHECK(x->GetOwner() == bb && x->GetDirectSource() == holder);
    CHECK(script->GetLocalParameterPosition(holder) >= 0 && innerGraph->GetLocalParameterPosition(holder) < 0);
    CHECK(unresolved() == unresolvedBefore);
    int highlighted = 0;
    for (const auto &record : document.Session(id)->Data().behaviors)
        for (const auto &link : record.links) highlighted += link.highlight ? 1 : 0;
    CHECK(highlighted == 2);
    recorder.After(ExportParameter(document, id, LinkEndpoint(block, 0, ENDPOINT_PIN), id), "export again (refused)");
    // Re-import from the middle is refused; from the top, down into Inner.
    recorder.After(ReimportParameter(document, id, inner.created, PinKind::ParamIn, 0, inner.created),
                   "re-import inner level (refused)");
    EditResult reimported = ReimportParameter(document, id, outer.created, PinKind::ParamIn, 0, inner.created);
    recorder.After(reimported, "re-import pIn");
    CHECK(reimported.ok && outerGraph->GetInputParameterCount() == 0 && innerGraph->GetInputParameterCount() == 0);
    CHECK(innerGraph->GetLocalParameterPosition(holder) >= 0 && x->GetDirectSource() == holder);
    // A local value holder dragged up exports its input; dragged back in, re-imports it.
    const int slot = innerGraph->GetLocalParameterPosition(holder);
    EditResult up = MoveLocalAcrossGraphs(document, id, inner.created, slot, outer.created, Point(400, 120));
    recorder.After(up, "export local");
    CHECK(up.ok && outerGraph->GetLocalParameterPosition(holder) >= 0 && innerGraph->GetInputParameterPosition(x) == 0);
    EditResult down = MoveLocalAcrossGraphs(document, id, outer.created,
                                            outerGraph->GetLocalParameterPosition(holder), inner.created, Point(360, 160));
    recorder.After(down, "re-import local");
    CHECK(down.ok && innerGraph->GetLocalParameterPosition(holder) >= 0 && innerGraph->GetInputParameterCount() == 0);
    // The output: exported to the Script, re-imported one level.
    EditResult out = ExportParameter(document, id, LinkEndpoint(block, 0, ENDPOINT_POUT), id);
    recorder.After(out, "export pOut");
    CHECK(out.ok && outerGraph->GetOutputParameterPosition(y) == 0 && y->GetOwner() == bb);
    EditResult outBack = ReimportParameter(document, id, outer.created, PinKind::ParamOut, 0, outer.created);
    recorder.After(outBack, "re-import pOut one level");
    CHECK(outBack.ok && outerGraph->GetOutputParameterCount() == 0 && innerGraph->GetOutputParameterPosition(y) == 0);
    // Behavior pins: a new graph input/output per level, linked.
    EditResult bin = ExportBehaviorIo(document, id, LinkEndpoint(block, 0, ENDPOINT_BIN), id);
    recorder.After(bin, "export bIn");
    EditResult bout = ExportBehaviorIo(document, id, LinkEndpoint(block, 0, ENDPOINT_BOUT), id);
    recorder.After(bout, "export bOut");
    CHECK(bin.ok && bout.ok && outerGraph->GetInputCount() == 2 && innerGraph->GetOutputCount() == 2);
    CHECK(unresolved() == unresolvedBefore);
    // Border position of a graph parameter (display only).
    CHECK(MoveGraphParameter(document, id, inner.created, PinKind::ParamOut, 0, 3, true));
    recorder.After(EditResult{true}, "move graph pOut");
    CHECK(GenerateInterfaceChunk(document.Session(id)->Data(), script));
    recorder.UndoAllRedoAll();
    std::cout << "Exported parameters passed\n";
}

// Dev "Replacing BBs and BGs": links kept by name/slot and type.
void ReplaceBehaviors(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes) {
    CKContext *context = host.Context();
    auto *script = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Replace Script"));
    script->UseGraph();
    script->SetType(CKBEHAVIORTYPE_SCRIPT);
    script->CreateInput((CKSTRING) "Start");
    const CK_ID id = script->GetID();
    Document document(host);
    document.RefreshScripts();
    UndoHistory &undo = document.History();
    undo.SetLimit(1000);
    CHECK(EditScriptData(document, id, "Open Script", [](InterfaceData &data) { SetScriptOpen(data, true); }));
    Recorder recorder(document, id);
    EditResult a = InsertBuildingBlock(document, id, id, prototypes[0], Point(300, 100));
    recorder.After(a, "insert A");
    EditResult b = InsertBuildingBlock(document, id, id, prototypes[1], Point(500, 100));
    recorder.After(b, "insert B");
    CHECK(a.ok && b.ok);
    CKBehavior *blockA = CKBehavior::Cast(context->GetObject(a.created));
    CKBehavior *blockB = CKBehavior::Cast(context->GetObject(b.created));
    recorder.After(CreateBehaviorLink(document, id, StartEndpoint(document, id), LinkEndpoint(a.created, 0, ENDPOINT_BIN)),
                   "Start -> A");
    if (blockA->GetOutputCount() > 0 && blockB->GetInputCount() > 0)
        recorder.After(CreateBehaviorLink(document, id, LinkEndpoint(a.created, 0, ENDPOINT_BOUT),
                                          LinkEndpoint(b.created, 0, ENDPOINT_BIN)),
                       "A -> B");
    const int links = script->GetSubBehaviorLinkCount();
    const int locals = script->GetLocalParameterCount();
    std::vector<CKParameter *> sources;
    for (int i = 0; i < blockA->GetInputParameterCount(); ++i) sources.push_back(blockA->GetInputParameter(i)->GetDirectSource());
    // The same prototype: every pin and link is kept, the values too.
    EditResult same = ReplaceBuildingBlock(document, id, a.created, blockA->GetPrototypeGuid());
    recorder.After(same, "replace A by itself");
    if (!same.ok) std::cerr << "replace: " << same.message << std::endl;
    CHECK(same.ok && document.IsDiscarded(a.created));
    CKBehavior *fresh = CKBehavior::Cast(context->GetObject(same.created));
    CHECK(fresh->GetParent() == script && script->GetSubBehaviorLinkCount() == links &&
           script->GetLocalParameterCount() == locals);
    for (int i = 0; i < fresh->GetInputParameterCount(); ++i)
        CHECK(fresh->GetInputParameter(i)->GetDirectSource() == sources[i]);
    // Another prototype: what cannot be kept goes, with its value holders.
    EditResult other = ReplaceBuildingBlock(document, id, same.created, prototypes[1]);
    recorder.After(other, "replace by another Building Block");
    CHECK(other.ok);
    recorder.After(ReplaceBuildingBlock(document, id, id, prototypes[1]), "replace the Script (refused)");
    CHECK(SameNamedBehaviors(document, b.created).size() >= 1);

    // Graphs: a copy of another graph replaces one, keeping its links.
    EditResult g1 = CreateBehaviorGraph(document, id, id, "First", Point(300, 300));
    recorder.After(g1, "graph 1");
    EditResult g2 = CreateBehaviorGraph(document, id, id, "Second", Point(600, 300));
    recorder.After(g2, "graph 2");
    recorder.After(AddGraphPin(document, id, g1.created, PinKind::ParamIn, "Value", IntGuid()), "g1 pIn");
    recorder.After(AddGraphPin(document, id, g2.created, PinKind::ParamIn, "Value", IntGuid()), "g2 pIn");
    recorder.After(CreateBehaviorLink(document, id, StartEndpoint(document, id), LinkEndpoint(g1.created, 0, ENDPOINT_BIN)),
                   "Start -> g1");
    EditResult value = AddLocalParameter(document, id, id, "Number", IntGuid(), Point(300, 260));
    recorder.After(value, "local");
    recorder.After(CreateParameterLink(document, id,
                                       LinkEndpoint(id, script->GetLocalParameterPosition(
                                                            CKParameterLocal::Cast(context->GetObject(value.created))),
                                                    ENDPOINT_PLOCAL),
                                       LinkEndpoint(g1.created, 0, ENDPOINT_PIN)),
                   "local -> g1 pIn");
    EditResult replaced = ReplaceBehaviorGraph(document, id, g1.created, g2.created);
    recorder.After(replaced, "replace graph");
    if (!replaced.ok) std::cerr << "replace graph: " << replaced.message << std::endl;
    CHECK(replaced.ok && document.IsDiscarded(g1.created));
    CKBehavior *copy = CKBehavior::Cast(context->GetObject(replaced.created));
    CHECK(copy && copy->GetParent() == script && copy->GetInputParameter(0)->GetDirectSource() == context->GetObject(value.created));
    bool started = false;
    for (int i = 0; i < script->GetSubBehaviorLinkCount(); ++i)
        started = started || script->GetSubBehaviorLink(i)->GetOutBehaviorIO() == copy->GetInput(0);
    CHECK(started);
    recorder.UndoAllRedoAll();
    std::cout << "Replace behaviors passed\n";

    // A Script snapshot is saved in the InterfaceChunk and read back.
#ifdef _WIN32
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 8;
    info.bmiHeader.biHeight = -6;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    CHECK(bitmap && bits);
    for (int i = 0; i < 8 * 6; ++i) static_cast<CKDWORD *>(bits)[i] = 0xFF000000u | (i * 5);
#else
    CKDWORD values[8 * 6];
    for (int i = 0; i < 8 * 6; ++i) values[i] = 0xFF000000u | (i * 5);
    VxImageDescEx image;
    image.Width = 8; image.Height = 6; image.BitsPerPixel = 32; image.BytesPerLine = 8 * 4;
    image.RedMask = 0x00FF0000; image.GreenMask = 0x0000FF00;
    image.BlueMask = 0x000000FF; image.AlphaMask = 0xFF000000;
    image.Image = reinterpret_cast<XBYTE *>(values);
    BITMAP_HANDLE bitmap = VxCreateBitmap(image);
    CHECK(bitmap);
#endif
    document.KeepBitmap(bitmap);
    CHECK(SetScriptSnapshot(document, id, bitmap));
    CKStateChunk *chunk = GenerateInterfaceChunk(document.Session(id)->Data(), script);
    CHECK(chunk);
    InterfaceData reloaded;
    CHECK(reloaded.LoadFromChunk(script, chunk) == CK_OK && reloaded.header.snapshot);
#ifdef _WIN32
    BITMAP description = {};
    CHECK(GetObject(static_cast<HBITMAP>(reloaded.header.snapshot), sizeof(description), &description) &&
           description.bmWidth == 8 && std::abs(description.bmHeight) == 6);
#else
    VxImageDescEx description;
    CHECK(VxConvertBitmap(reloaded.header.snapshot, description) && description.Image &&
          description.Width == 8 && description.Height == 6);
    for (int i = 0; i < 8 * 6; ++i)
        CHECK(reinterpret_cast<CKDWORD *>(description.Image)[i] == (0xFF000000u | (i * 5)));
    delete[] description.Image;
#endif
    DeleteCKStateChunk(chunk);
    CHECK(undo.Undo() && !document.Session(id)->Data().header.snapshot);
    std::cout << "Script snapshot passed\n";
}

// The Schematic's operation commands (OperationCommands): Modify Parameter
// Operation takes over the links, the one to a disabled input (Dev does not
// draw it) too; Add Parameter Operation in Link and Edit Parameter Operation
// Link; each one undo step, undone and redone.
void OperationCommands(CkRuntimeHost &host) {
    CKContext *context = host.Context();
    auto *script = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Operation Script"));
    script->UseGraph();
    script->SetType(CKBEHAVIORTYPE_SCRIPT);
    script->CreateInput((CKSTRING) "Start");
    const CK_ID id = script->GetID();
    Document document(host);
    document.RefreshScripts();
    document.History().SetLimit(100);
    CHECK(document.Session(id) && EditScriptData(document, id, "Open Script",
                                                  [](InterfaceData &data) { SetScriptOpen(data, true); }));
    CKParameterManager *manager = context->GetParameterManager();
    const CKGUID addition = manager->OperationNameToGuid((CKSTRING) "Addition");
    const CKGUID subtraction = manager->OperationNameToGuid((CKSTRING) "Subtraction");
    auto slotOf = [&](CK_ID local) {
        return script->GetLocalParameterPosition(CKParameterLocal::Cast(context->GetObject(local)));
    };

    // Counter -> A.in1; A.out -> B.in1, B's input disabled: the link is hidden.
    const EditResult counter = AddLocalParameter(document, id, id, "Counter", IntGuid(), Point(100, 300));
    const EditResult a = AddParameterOperation(document, id, id, addition, IntGuid(), IntGuid(), IntGuid(), Point(300, 300));
    const EditResult b = AddParameterOperation(document, id, id, addition, IntGuid(), IntGuid(), IntGuid(), Point(500, 300));
    CHECK(counter.ok && a.ok && b.ok);
    const LinkEndpoint counterEnd(id, slotOf(counter.created), ENDPOINT_PLOCAL);
    CHECK(CreateParameterLink(document, id, counterEnd, LinkEndpoint(a.created, 0, ENDPOINT_PIN)).ok);
    CHECK(CreateParameterLink(document, id, LinkEndpoint(a.created, 0, ENDPOINT_POUT), LinkEndpoint(b.created, 0, ENDPOINT_PIN)).ok);
    auto *opB = CKParameterOperation::Cast(context->GetObject(b.created));
    opB->GetInParameter1()->Enable(FALSE);
    bool hidden = false;
    for (const LinkItem &link : BuildModel(document, id).links) hidden |= link.hidden && link.end.id == b.created;
    CHECK(hidden);

    // Modify A into a Subtraction: both links taken over, the hidden one too.
    const OperationSignature minus{subtraction, IntGuid(), IntGuid(), IntGuid(), "Subtraction"};
    int dropped = -1;
    CK_ID made = 0;
    ExpectUndoRoundTrip(document, "modify operation", [&] {
        const EditResult result = ReplaceParameterOperation(document, id, a.created, minus, &dropped);
        made = result.created;
        return result;
    });
    auto *replaced = CKParameterOperation::Cast(context->GetObject(made));
    CHECK(dropped == 0 && replaced && replaced->GetOperationGuid() == subtraction && document.IsDiscarded(a.created));
    CHECK(replaced->GetInParameter1()->GetDirectSource() == context->GetObject(counter.created));
    CHECK(opB->GetInParameter1()->GetDirectSource() == replaced->GetOutParameter()); // the hidden link
    CHECK(!opB->GetInParameter1()->IsEnabled());

    // An operation in the link Counter -> Subtraction.in1, linked on both sides.
    ElementRef counterLink;
    for (const LinkItem &link : BuildModel(document, id).links)
        if (!link.behaviorLink && link.end.id == made && link.end.type == ENDPOINT_PIN && link.end.index == 0)
            counterLink = link.ref;
    CHECK(counterLink.IsValid());
    const OperationSignature plus{addition, IntGuid(), IntGuid(), IntGuid(), "Addition"};
    CK_ID inserted = 0;
    ExpectUndoRoundTrip(document, "operation in link", [&] {
        const EditResult result =
            InsertOperationInLink(document, id, counterLink, counterEnd, LinkEndpoint(made, 0, ENDPOINT_PIN), plus);
        inserted = result.created;
        return result;
    });
    auto *between = CKParameterOperation::Cast(context->GetObject(inserted));
    CHECK(between && between->GetInParameter1()->GetDirectSource() == context->GetObject(counter.created));
    CHECK(replaced->GetInParameter1()->GetDirectSource() == between->GetOutParameter());

    // A Float local -> Subtraction.in2 through a conversion; then another conversion.
    const EditResult ratio = AddLocalParameter(document, id, id, "Ratio", CKPGUID_FLOAT, Point(100, 400));
    CHECK(ratio.ok);
    const LinkEndpoint ratioEnd(id, slotOf(ratio.created), ENDPOINT_PLOCAL), second(made, 1, ENDPOINT_PIN);
    const std::vector<OperationSignature> conversions = ParameterLinkConversions(document, id, ratioEnd, second);
    CHECK(!conversions.empty());
    CHECK(CreateParameterLink(document, id, ratioEnd, second, &conversions.front()).ok);
    ElementRef ratioLink;
    for (const LinkItem &link : BuildModel(document, id).links)
        if (!link.behaviorLink && link.end.id == made && link.end.type == ENDPOINT_PIN && link.end.index == 1)
            ratioLink = link.ref;
    CHECK(ratioLink.IsValid());
    ExpectUndoRoundTrip(document, "edit operation link", [&] {
        return ReplaceLinkConversion(document, id, ratioLink, ratioEnd, second, conversions.back());
    });
    std::cout << "operation commands: " << conversions.size() << " conversion(s) Float -> Int\n";
}

// The Schematic's linking from several pins and exports (ExportCommands,
// LinkCommands) and the 3D Layout's Building Block drop
// (DropBuildingBlockOn): each one undo step, undone and redone.
void LinkAndExportCommands(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes) {
    CKContext *context = host.Context();
    auto *script = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Export Script"));
    script->UseGraph();
    script->SetType(CKBEHAVIORTYPE_SCRIPT);
    script->CreateInput((CKSTRING) "Start");
    const CK_ID id = script->GetID();
    Document document(host);
    document.RefreshScripts();
    document.History().SetLimit(100);
    CHECK(document.Session(id) && EditScriptData(document, id, "Open Script",
                                                  [](InterfaceData &data) { SetScriptOpen(data, true); }));
    // A Building Block with an input parameter.
    CKGUID withInput;
    for (const CKGUID &guid : prototypes) {
        CKBehaviorPrototype *prototype = CKGetPrototypeFromGuid(guid);
        if (prototype && prototype->GetInParameterCount() > 0) {
            withInput = guid;
            break;
        }
    }
    CHECK(withInput.IsValid());
    const EditResult graph = CreateBehaviorGraph(document, id, id, "Group", Point(400, 300));
    CHECK(graph.ok);
    CKBehavior *group = CKBehavior::Cast(context->GetObject(graph.created));
    const EditResult inner = InsertBuildingBlock(document, id, graph.created, withInput, Point(420, 340));
    const EditResult other = InsertBuildingBlock(document, id, graph.created, withInput, Point(500, 340));
    CHECK(inner.ok && other.ok);
    const BehaviorItem *box = BuildModel(document, id).FindBehavior(graph.created);
    CHECK(box);
    const Rect groupBox = box->box;
    const Point outside(groupBox.hPos - 150.0f, groupBox.vPos + 20.0f);
    const Point within(groupBox.hPos + groupBox.hSize * 0.5f, groupBox.vPos + groupBox.vSize * 0.5f);
    auto pinOf = [&](CK_ID owner, PinKind kind, int index) {
        for (const PinItem &pin : BuildModel(document, id).pins)
            if (pin.owner == owner && pin.kind == kind && pin.index == index) return pin;
        return PinItem();
    };

    // The other block's value local dragged out of the graph: its input is exported.
    CKBehavior *otherBlock = CKBehavior::Cast(context->GetObject(other.created));
    CKParameterLocal *value = CKParameterLocal::Cast(otherBlock->GetInputParameter(0)->GetDirectSource());
    CHECK(value && group->GetLocalParameterPosition(value) >= 0);
    ExpectUndoRoundTrip(document, "local out of the graph", [&] {
        size_t moved = 0;
        std::string refusal;
        MoveLocalsAcrossGraphs(document, id, graph.created, {{value->GetID(), outside}}, id, moved, refusal);
        return moved == 1 ? EditResult::Applied() : EditResult::Refused(refusal);
    });
    CHECK(group->GetInputParameterCount() == 1);

    // Shift+drag of the inner block's input out of the graph: exported; of
    // the graph's new input into it: re-imported.
    const PinItem input = pinOf(inner.created, PinKind::ParamIn, 0);
    CHECK(input.owner == inner.created);
    ExpectUndoRoundTrip(document, "export", [&] {
        const std::vector<EditResult> refused = ExportPinsAt(document, id, {input}, outside);
        return refused.empty() ? EditResult::Applied() : refused.front();
    });
    CHECK(group->GetInputParameterCount() == 2);
    const PinItem exported = pinOf(graph.created, PinKind::ParamIn, 1);
    CHECK(exported.owner == graph.created);
    ExpectUndoRoundTrip(document, "re-import", [&] {
        const std::vector<EditResult> refused = ExportPinsAt(document, id, {exported}, within);
        return refused.empty() ? EditResult::Applied() : refused.front();
    });
    CHECK(group->GetInputParameterCount() == 1);
    // Dropped in its own graph: refused, nothing recorded.
    const size_t steps = document.History().UndoCount();
    CHECK(ExportPinsAt(document, id, {pinOf(inner.created, PinKind::ParamIn, 0)}, within).size() == 1);
    CHECK(document.History().UndoCount() == steps);

    // Links from several pins: the allowed ones, one step.
    const EditResult first = AddLocalParameter(document, id, id, "First", IntGuid(), Point(100, 100));
    const EditResult second = AddLocalParameter(document, id, id, "Second", IntGuid(), Point(100, 140));
    const EditResult addition =
        AddParameterOperation(document, id, id, context->GetParameterManager()->OperationNameToGuid((CKSTRING) "Addition"),
                              IntGuid(), IntGuid(), IntGuid(), Point(250, 120));
    CHECK(first.ok && second.ok && addition.ok);
    auto localEnd = [&](CK_ID local) {
        return LinkEndpoint(id, script->GetLocalParameterPosition(CKParameterLocal::Cast(context->GetObject(local))),
                            ENDPOINT_PLOCAL);
    };
    int made = 0;
    ExpectUndoRoundTrip(document, "links", [&] {
        made = LinkPairs(document, id,
                         {{localEnd(first.created), LinkEndpoint(addition.created, 0, ENDPOINT_PIN)},
                          {localEnd(second.created), LinkEndpoint(addition.created, 1, ENDPOINT_PIN)},
                          {localEnd(first.created), localEnd(second.created)}}, // a local to a local: not a link
                         false);
        return made ? EditResult::Applied() : EditResult::Refused("no link");
    });
    CHECK(made == 2);

    // The two locals dropped on the graph: two new inputs, linked.
    const int inputs = group->GetInputParameterCount();
    ExpectUndoRoundTrip(document, "links to new pins", [&] {
        made = LinkToNewPins(document, id,
                             {{localEnd(first.created), PinKind::ParamOut, "First", IntGuid()},
                              {localEnd(second.created), PinKind::ParamOut, "Second", IntGuid()}},
                             graph.created, false, 0, true);
        return made ? EditResult::Applied() : EditResult::Refused("nothing made");
    });
    CHECK(made == 4 && group->GetInputParameterCount() == inputs + 2);
    CHECK(group->GetInputParameter(inputs)->GetDirectSource() == context->GetObject(first.created));

    // A Building Block dropped on a 3D entity: a new Script with it; with
    // that Script skipped (hidden in the Schematic), another one.
    auto *entity = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Drop Entity"));
    CHECK(entity);
    if (CKLevel *level = context->GetCurrentLevel()) level->AddObject(entity);
    CKGUID onEntity;
    for (int i = 0; i < CKGetPrototypeDeclarationCount() && !onEntity.IsValid(); ++i) {
        CKObjectDeclaration *declaration = CKGetPrototypeDeclaration(i);
        if (declaration && declaration->GetType() == CKDLL_BEHAVIORPROTOTYPE &&
            CKIsChildClassOf(CKCID_3DENTITY, declaration->GetCompatibleClassId()))
            onEntity = declaration->GetGuid();
    }
    CHECK(onEntity.IsValid());
    CK_ID dropScript = 0;
    ExpectUndoRoundTrip(document, "drop on an entity",
                        [&] { return DropBuildingBlockOn(document, entity->GetID(), onEntity, {}, &dropScript); });
    CHECK(entity->GetScriptCount() == 1 && entity->GetScript(0)->GetID() == dropScript);
    CHECK(DropBuildingBlockOn(document, entity->GetID(), onEntity, {}).ok && entity->GetScriptCount() == 1);
    CHECK(DropBuildingBlockOn(document, entity->GetID(), onEntity, {dropScript}).ok && entity->GetScriptCount() == 2);
    CHECK(!DropBuildingBlockOn(document, id, onEntity, {}).ok); // a behavior
    std::cout << "link and export commands: " << group->GetInputParameterCount() << " graph inputs\n";
}

// Random edits over the pins, links and parameters displayed by the Script.
// Diagnostic: references from living objects to parameters of soft-deleted ones.
void ReportDangling(Document &document, const std::string &what) {
    CKContext *context = document.Context();
    auto buried = [&](CKObject *object) {
        if (!object) return false;
        if (document.IsDiscarded(object->GetID())) return true;
        CKObject *owner = nullptr;
        if (CKParameter *parameter = CKParameter::Cast(object)) owner = parameter->GetOwner();
        if (CKParameterIn *input = CKParameterIn::Cast(object)) owner = input->GetOwner();
        return owner && document.IsDiscarded(owner->GetID());
    };
    const int count = context->GetObjectsCountByClassID(CKCID_BEHAVIOR);
    CK_ID *ids = context->GetObjectsListByClassID(CKCID_BEHAVIOR);
    for (int i = 0; i < count; ++i) {
        CKBehavior *behavior = CKBehavior::Cast(context->GetObject(ids[i]));
        if (!behavior || buried(behavior) || document.IsDiscarded(behavior->GetID())) continue;
        for (int k = 0; k < behavior->GetInputParameterCount(); ++k) {
            CKParameterIn *input = behavior->GetInputParameter(k);
            if (buried(input)) std::cout << what << ": " << behavior->GetID() << " lists buried pIn " << input->GetID() << std::endl;
            else if (input && buried(input->GetDirectSource()))
                std::cout << what << ": pIn " << input->GetID() << " reads buried " << input->GetDirectSource()->GetID() << std::endl;
        }
        for (int k = 0; k < behavior->GetOutputParameterCount(); ++k) {
            CKParameterOut *output = behavior->GetOutputParameter(k);
            if (buried(output))
                std::cout << what << ": " << behavior->GetID() << " lists buried pOut " << output->GetID() << std::endl;
            else if (output)
                for (int d = 0; d < output->GetDestinationCount(); ++d)
                    if (buried(output->GetDestination(d)))
                        std::cout << what << ": pOut " << output->GetID() << " of " << behavior->GetID()
                                  << " writes buried " << output->GetDestination(d)->GetID() << std::endl;
        }
        for (int k = 0; k < behavior->GetParameterOperationCount(); ++k) {
            CKParameterOperation *operation = behavior->GetParameterOperation(k);
            for (CKParameterIn *input : {operation->GetInParameter1(), operation->GetInParameter2()})
                if (input && buried(input->GetDirectSource()))
                    std::cout << what << ": operation pIn " << input->GetID() << " reads buried " << input->GetDirectSource()->GetID() << std::endl;
        }
    }
}

int RandomEdits(Document &document, CK_ID script, const std::vector<CKGUID> &prototypes,
                int steps, unsigned seed) {
    std::mt19937 random(seed);
    auto pick = [&](size_t count) { return static_cast<size_t>(random() % count); };
    Recorder recorder(document, script);
    CKContext *context = document.Context();
    const CKGUID addition = context->GetParameterManager()->OperationNameToGuid((CKSTRING) "Addition");
    std::map<std::string, std::pair<int, int>> stats; // applied, refused
    for (int step = 0; step < steps; ++step) {
        const SchematicModel model = BuildModel(document, script);
        const Rect &area = model.script.bounds;
        const Point where(area.hPos + 200.0f + static_cast<float>(pick(30)) * 20.0f,
                          area.vPos + static_cast<float>(pick(20)) * 20.0f);
        std::vector<LinkEndpoint> behaviorPins, parameterPins;
        for (const auto &pin : model.pins)
            (pin.kind == PinKind::BehaviorIn || pin.kind == PinKind::BehaviorOut ? behaviorPins : parameterPins)
                .push_back(pin.endpoint);
        if (model.start.present) behaviorPins.push_back(StartEndpoint(document, script));
        for (const auto &param : model.params)
            parameterPins.push_back(LinkEndpoint(param.owner, param.slot,
                                                 param.shortcut ? ENDPOINT_POUT_SHORTCUT : ENDPOINT_PLOCAL));
        std::vector<CK_ID> graphs{script};
        for (const auto &item : model.behaviors)
            if (item.expandable && !item.folded) graphs.push_back(item.id);
        const CK_ID graph = graphs[pick(graphs.size())];
        std::vector<ElementRef> deletable;
        for (const auto &pin : model.pins)
            if (std::find(graphs.begin(), graphs.end(), pin.owner) != graphs.end() && pin.kind != PinKind::Target)
                deletable.push_back(pin.ref);
        for (const auto &item : model.behaviors) deletable.push_back(item.ref);
        for (const auto &item : model.links) deletable.push_back(item.ref);
        for (const auto &item : model.operations) deletable.push_back(item.ref);
        for (const auto &item : model.params) deletable.push_back(item.ref);

        const size_t action = pick(15);
        std::string what = "step " + std::to_string(step) + " ";
        EditResult result = EditResult::Refused(std::string());
        bool attempted = true;
        switch (action) {
        case 0:
        case 1:
            what += "insert";
            if (pick(4) == 0) {
                what += " graph";
                result = CreateBehaviorGraph(document, script, graph, "Fuzz Graph", where);
            } else {
                result = InsertBuildingBlock(document, script, graph, prototypes[pick(prototypes.size())], where);
            }
            break;
        case 2:
        case 3:
            what += "behavior link";
            if (behaviorPins.size() < 2) { attempted = false; break; }
            result = CreateBehaviorLink(document, script, behaviorPins[pick(behaviorPins.size())],
                                        behaviorPins[pick(behaviorPins.size())]);
            break;
        case 4:
        case 5:
            what += "parameter link";
            if (parameterPins.size() < 2) { attempted = false; break; }
            result = CreateParameterLink(document, script, parameterPins[pick(parameterPins.size())],
                                         parameterPins[pick(parameterPins.size())]);
            break;
        case 6:
            what += "local";
            if (pick(2) == 0) {
                result = AddLocalParameter(document, script, graph, "Local", CKPGUID_INT, where);
            } else {
                what += " graph pin";
                const PinKind kinds[] = {PinKind::BehaviorIn, PinKind::BehaviorOut, PinKind::ParamIn, PinKind::ParamOut};
                result = AddGraphPin(document, script, graph, kinds[pick(4)], "Fuzz Pin", CKPGUID_INT);
            }
            break;
        case 7:
            what += "operation";
            result = AddParameterOperation(document, script, graph, addition, CKPGUID_INT, CKPGUID_INT,
                                           CKPGUID_INT, where);
            break;
        case 8: {
            what += "target/rename/delay";
            if (model.behaviors.empty()) { attempted = false; break; }
            const BehaviorItem &behavior = model.behaviors[pick(model.behaviors.size())];
            CKBehavior *runtime = CKBehavior::Cast(context->GetObject(behavior.id));
            const size_t kind = pick(3);
            if (kind == 0 && runtime)
                result = SetTargetUsed(document, script, behavior.id, !runtime->IsUsingTarget());
            else if (kind == 1)
                result = RenameObject(document, script, behavior.id, "Fuzz " + std::to_string(step));
            else if (!model.links.empty() && model.links[0].behaviorLink)
                result = SetLinkDelay(document, script, model.links[0].ref.object, static_cast<int>(pick(4)));
            else
                attempted = false;
            break;
        }
        case 9:
            what += "delete";
            if (deletable.empty()) { attempted = false; break; }
            {
                const ElementRef target = deletable[pick(deletable.size())];
                what += " kind " + std::to_string(static_cast<int>(target.kind)) + " object " +
                        std::to_string(target.object) + " owner " + std::to_string(target.owner) + " index " +
                        std::to_string(target.index);
                result = DeleteElements(document, script, {target});
            }
            break;
        case 14: {
            what += "group";
            if (model.behaviors.empty()) { attempted = false; break; }
            const BehaviorItem &first = model.behaviors[pick(model.behaviors.size())];
            std::vector<CK_ID> chosen{first.id};
            for (const auto &item : model.behaviors)
                if (item.id != first.id && item.parent == first.parent && pick(3) == 0) chosen.push_back(item.id);
            result = GroupBehaviors(document, script, chosen);
            break;
        }
        case 13: {
            what += "ungroup";
            std::vector<CK_ID> expandable;
            for (const auto &item : model.behaviors)
                if (item.expandable) expandable.push_back(item.id);
            if (expandable.empty()) { attempted = false; break; }
            result = UngroupGraph(document, script, expandable[pick(expandable.size())]);
            break;
        }
        case 12: {
            what += "paste";
            if (model.behaviors.empty()) { attempted = false; break; }
            std::vector<CK_ID> chosen{model.behaviors[pick(model.behaviors.size())].id};
            if (pick(2)) chosen.push_back(model.behaviors[pick(model.behaviors.size())].id);
            const InterfaceData clipboard = document.Session(script)->Data();
            result = PasteBehaviors(document, clipboard, chosen, {}, script, graph, where);
            break;
        }
        case 10:
            recorder.Undo();
            attempted = false;
            break;
        default:
            recorder.Redo();
            attempted = false;
            break;
        }
        if (attempted) {
            if (std::getenv("SCHEMATIC_FUZZ_TRACE"))
            {
                CKBehavior *root = CKBehavior::Cast(context->GetObject(script));
                std::cout << what << (result.ok ? " ok" : " refused: " + result.message) << " subs";
                for (int i = 0; root && i < root->GetSubBehaviorCount(); ++i) std::cout << " " << root->GetSubBehavior(i)->GetID();
                std::cout << std::endl;
            }
            recorder.After(result, what);
            if (std::getenv("SCHEMATIC_DANGLING_TRACE")) ReportDangling(document, what);
            const std::string kind = what.substr(what.find(' ', 5) + 1);
            (result.ok ? stats[kind].first : stats[kind].second)++;
        }
    }
    recorder.UndoAllRedoAll();
    if (std::getenv("SCHEMATIC_FUZZ_STATS"))
        for (const auto &entry : stats)
            std::cout << "  " << entry.first << ": " << entry.second.first << " applied, " << entry.second.second
                      << " refused" << std::endl;
    return recorder.Applied();
}

void SyntheticFuzz(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes, unsigned seed, int steps) {
    CKContext *context = host.Context();
    auto *script = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Fuzz Script"));
    script->UseGraph();
    script->SetType(CKBEHAVIORTYPE_SCRIPT);
    script->CreateInput((CKSTRING) "Start");
    Document document(host);
    document.RefreshScripts();
    UndoHistory &undo = document.History();
    undo.SetLimit(1000);
    CHECK(EditScriptData(document, script->GetID(), "Open Script",
                          [](InterfaceData &data) { SetScriptOpen(data, true); }));
    const int applied = RandomEdits(document, script->GetID(), prototypes, steps, seed);
    CHECK(applied > steps / 5);
    std::cout << "Synthetic fuzz passed (seed " << seed << ", " << steps << " steps, " << applied
              << " commands applied)\n";
    undo.Clear();
    document.Close();
}

std::vector<unsigned char> ChunkBytes(CKStateChunk *chunk) {
    std::vector<unsigned char> bytes;
    if (!chunk) return bytes;
    bytes.resize(static_cast<size_t>(chunk->ConvertToBuffer(nullptr)));
    if (!bytes.empty()) chunk->ConvertToBuffer(bytes.data());
    return bytes;
}

// Reloads a file written from the document into a fresh CKContext and
// compares every Script's InterfaceChunk with the document's, byte for byte.
// Chunks embed runtime CK_IDs: the reloaded copy is remapped to the saved
// IDs (CKFileObject::Object, the document's runtime IDs) first. Returns the
// number of Scripts compared, or -1 on a difference.
int CompareReloadedChunks(Document &document, const std::string &path) {
    CKContext *context = nullptr;
    if (CreateCKContext(&context, nullptr) != CK_OK) return -1;
    context->SetInterfaceMode(TRUE, nullptr, nullptr);
    CKFile *file = context->CreateCKFile();
    CKObjectArray *objects = CreateCKObjectArray();
    int compared = -1;
    if (file->Load(const_cast<CKSTRING>(path.c_str()), objects, CK_LOAD_DEFAULT) == CK_OK) {
        compared = 0;
        const int count = file->m_FileObjects.Size();
        for (int i = 0; i < count && compared >= 0; ++i) {
            CKBehavior *reloaded = CKBehavior::Cast(file->m_FileObjects[i].ObjPtr);
            if (!reloaded || !(reloaded->GetType() & CKBEHAVIORTYPE_SCRIPT)) continue;
            CKBehavior *source = CKBehavior::Cast(document.Context()->GetObject(file->m_FileObjects[i].Object));
            if (!source) {
                compared = -1;
                break;
            }
            CKStateChunk *chunk = reloaded->GetInterfaceChunk();
            if (!chunk != !source->GetInterfaceChunk()) {
                compared = -1;
                break;
            }
            if (!chunk) continue;
            CKStateChunk *copy = CreateCKStateChunk(chunk);
            // Two passes so a new ID equal to another object's saved ID is
            // not remapped twice.
            for (int j = 0; j < count; ++j)
                if (file->m_FileObjects[j].ObjPtr)
                    copy->RemapObject(file->m_FileObjects[j].ObjPtr->GetID(), 0x40000000 + j);
            for (int j = 0; j < count; ++j)
                if (file->m_FileObjects[j].ObjPtr) copy->RemapObject(0x40000000 + j, file->m_FileObjects[j].Object);
            const bool same = ChunkBytes(copy) == ChunkBytes(source->GetInterfaceChunk());
            DeleteCKStateChunk(copy);
            if (!same) {
                std::cerr << "InterfaceChunk of " << (source->GetName() ? source->GetName() : "") << " differs\n";
                compared = -1;
            } else {
                ++compared;
            }
        }
    }
    context->DeleteCKFile(file);
    DeleteCKObjectArray(objects);
    context->SetInterfaceMode(FALSE, nullptr, nullptr);
    CKCloseContext(context);
    return compared;
}

int CountMismatchedLinks(Document &document) {
    int mismatched = 0;
    for (const auto &info : document.Scripts()) {
        ScriptSession *session = document.Session(info.id);
        if (!session || session->ReadOnly()) continue;
        auto check = [&](const BehaviorData &record) {
            for (const auto &link : record.links)
                if (!LinkMatchesRuntime(document.Context(), record, link)) ++mismatched;
        };
        check(session->Data().rootBehavior);
        for (const auto &record : session->Data().behaviors) check(record);
    }
    return mismatched;
}

// SCHEMATIC_FUZZ_TRACE: progress on stderr (unbuffered) to locate crashes.
void Phase(const char *what) {
    if (std::getenv("SCHEMATIC_FUZZ_TRACE")) std::cerr << "phase: " << what << std::endl;
}

int SampleFile(CkRuntimeHost &host, const std::vector<CKGUID> &prototypes, const std::string &path,
               const std::string &outputDirectory) {
    namespace fs = std::filesystem;
    fs::create_directories(outputDirectory);
    Document document(host);
    std::string error;
    if (!document.Open(path, &error)) {
        std::cerr << error << '\n';
        return 2;
    }
    Phase("opened");
    // 1. No edit: every InterfaceChunk is written back byte for byte.
    const std::string unchanged = (fs::path(outputDirectory) / "unchanged.cmo").string();
    SaveOptions options;
    options.allowOverwrite = true;
    SaveReport report;
    if (!SaveDocumentAs(document, unchanged, options, report) || report.chunksWritten != 0) {
        for (const auto &message : report.errors) std::cerr << message << '\n';
        return 1;
    }
    Phase("saved unchanged");
    const int unchangedChunks = CompareReloadedChunks(document, unchanged);
    Phase("compared");
    if (unchangedChunks <= 0) {
        std::cerr << "An unedited InterfaceChunk changed in " << unchanged << '\n';
        return 1;
    }
    std::cout << "No-edit save kept " << unchangedChunks << " InterfaceChunks byte-identical\n";

    // 2. Random edits on the first Script with a 2.5 chunk, then save.
    CHECK(document.Open(path, &error));
    CK_ID script = 0;
    for (const auto &info : document.Scripts()) {
        ScriptSession *session = document.Session(info.id);
        if (session && !session->ReadOnly() && session->Origin() == SessionOrigin::LoadedChunk &&
            session->Data().version == 0x16) {
            script = info.id;
            break;
        }
    }
    if (!script) {
        // Pre-2.5 sectioned chunks accept display edits only.
        std::cout << "No Dev 2.5 Script to edit in " << path << "; random edits skipped\n";
        document.Close();
        return 0;
    }
    UndoHistory &undo = document.History();
    undo.SetLimit(1000);
    CHECK(EditScriptData(document, script, "Open Script", [](InterfaceData &data) { SetScriptOpen(data, true); }));
    if (!std::getenv("SCHEMATIC_SKIP_REPLACE")) {
        // Every Building Block of the Dev-saved Script replaced by its own
        // prototype keeps the topology (fingerprint modulo ids is not
        // compared; links and consistency are), then undone.
        // The 2.5 Script with the most behaviors.
        CK_ID target = script;
        int most = -1;
        for (const auto &info : document.Scripts()) {
            ScriptSession *session = document.Session(info.id);
            CKBehavior *candidate = CKBehavior::Cast(host.Context()->GetObject(info.id));
            if (session && candidate && !session->ReadOnly() && session->Data().version == 0x16 &&
                candidate->GetSubBehaviorCount() > most) {
                most = candidate->GetSubBehaviorCount();
                target = info.id;
            }
        }
        const size_t base = undo.UndoCount();
        Recorder recorder(document, target);
        int replaced = 0, refused = 0;
        std::vector<CK_ID> blocks;
        std::vector<CKBehavior *> pending{CKBehavior::Cast(host.Context()->GetObject(target))};
        while (!pending.empty()) {
            CKBehavior *behavior = pending.back();
            pending.pop_back();
            for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i) pending.push_back(behavior->GetSubBehavior(i));
            if (behavior->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK) blocks.push_back(behavior->GetID());
        }
        for (CK_ID id : blocks) {
            CKBehavior *behavior = CKBehavior::Cast(host.Context()->GetObject(id));
            CKBehavior *parent = behavior->GetParent();
            const int links = parent ? parent->GetSubBehaviorLinkCount() : 0;
            const int ins = behavior->GetInputCount(), outs = behavior->GetOutputCount();
            const std::string replacedName = behavior->GetName() ? behavior->GetName() : "";
            const EditResult result = ReplaceBuildingBlock(document, target, id, behavior->GetPrototypeGuid());
            recorder.After(result, "replace by itself");
            if (result.ok) {
                ++replaced;
                if (parent && parent->GetSubBehaviorLinkCount() != links) {
                    // The new one's IOs: a runtime whose prototype makes fewer (LightTrail.cmo's
                    // SphericalParticleSystem has none on the 2.1 runtime) cannot keep their links.
                    CKBehavior *fresh = CKBehavior::Cast(host.Context()->GetObject(result.created));
                    std::cerr << "replacing " << replacedName << " lost behavior links (" << links << " -> "
                              << parent->GetSubBehaviorLinkCount() << "; " << ins << " in / " << outs << " out, now "
                              << (fresh ? fresh->GetInputCount() : -1) << " / " << (fresh ? fresh->GetOutputCount() : -1) << ")"
                              << std::endl;
                    Fail("the check written above", __FILE__, __LINE__);
                }
            } else {
                ++refused;
                if (std::getenv("SCHEMATIC_REPLACE_TRACE")) std::cout << "refused: " << result.message << "\n";
            }
        }
        recorder.UndoAllRedoAll();
        while (undo.UndoCount() > base) undo.Undo();
        undo.Clear(); // the random edits below start without these redo steps
        std::cout << "Replaced " << replaced << " Building Blocks by themselves (" << refused << " refused)\n";
    }
    const int applied = RandomEdits(document, script, prototypes, 200, 7u);
    Phase("random edits done");
    if (std::getenv("SCHEMATIC_DANGLING_TRACE")) ReportDangling(document, "after random edits");
    // A loaded Script copied onto its owner: the original keeps its chunk
    // (CopyObjects may share it), the copy loads its own display.
    if (!std::getenv("SCHEMATIC_SKIP_PASTE")) {
        CKBehavior *original = CKBehavior::Cast(host.Context()->GetObject(script));
        CKStateChunk *chunk = original ? original->GetInterfaceChunk() : nullptr;
        const EditResult pasted = original && original->GetOwner()
                                      ? PasteScripts(document, {script}, original->GetOwner()->GetID())
                                      : EditResult::Refused("not attempted");
        if (pasted.ok) {
            InterfaceData reloaded;
            CHECK(original->GetInterfaceChunk() == chunk && reloaded.LoadFromChunk(original, chunk) == CK_OK);
            CKBehavior *copy = CKBehavior::Cast(host.Context()->GetObject(pasted.created));
            ScriptSession *copied = document.Session(pasted.created);
            CHECK(copy && copied && copied->Origin() == SessionOrigin::LoadedChunk);
            const std::set<std::string> issues = RuntimeConsistencyIssues(host.Context(), copied->Data(), copy);
            for (const std::string &issue : issues) std::cout << "pasted Script: " << issue << "\n";
            CHECK(issues.empty());
            std::cout << "Script paste on a loaded composition passed\n";
        } else {
            std::cout << "Script paste skipped: " << pasted.message << "\n";
        }
    }
    const std::string edited = (fs::path(outputDirectory) / "edited.cmo").string();
    report = SaveReport();
    Phase("saving edited");
    const bool saved = SaveDocumentAs(document, edited, options, report);
    Phase("saved edited");
    for (const auto &message : report.errors) std::cerr << message << '\n';
    Phase("before undo clear");
    undo.Clear();
    Phase("after undo clear");
    if (!saved) return 1;
    Phase("reopening edited");
    CHECK(document.Open(edited, &error));
    Phase("reopened edited");
    const int mismatched = CountMismatchedLinks(document);
    if (mismatched)
        for (const ScriptInfo &info : document.Scripts())
            if (ScriptSession *session = document.Session(info.id))
                for (const std::string &issue :
                     RuntimeConsistencyIssues(host.Context(), session->Data(), CKBehavior::Cast(host.Context()->GetObject(info.id))))
                    std::cout << "reopened " << info.name << ": " << issue << "\n";
    std::cout << "Sample fuzz: " << applied << " commands applied, saved " << edited << " ("
              << report.chunksWritten << " chunks, " << report.scriptsVerified << " verified), reopened with "
              << mismatched << " links without a CK relation\n";
    document.Close();
    return mismatched == 0 ? 0 : 1;
}

} // namespace vdevtest
