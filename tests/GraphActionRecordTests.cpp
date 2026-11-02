#include "VirtoolsDevTestSupport.h"
#include "core/graph/GraphActionRecord.h"
#include "core/graph/BehaviorCommands.h"
#include "core/graph/TopologyCommands.h"
#include "tests/TestActions.h"

#include <stdexcept>

namespace vdevtest {
namespace {
GraphActionRecord Names(Document &document, CK_ID object) {
    auto *target = document.Context()->GetObject(object);
    return {document.Token(), {{GraphText{document.Reference(object), "Recorded graph name", false}}},
            {{GraphText{document.Reference(object), target->GetName() ? target->GetName() : "", false}}}};
}
} // namespace
void GraphActionRecordReplay(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    auto *level = document.Context()->GetCurrentLevel();
    const auto id = level->GetID();
    const std::string before = level->GetName() ? level->GetName() : "";
    auto payload = Names(document, id);
    const auto copied = payload;
    auto action = RestoreGraphAction(std::move(payload));
    payload = {};
    CHECK(action->Redo(document) && std::string(level->GetName()) == "Recorded graph name");
    CHECK(action->Undo(document) && std::string(level->GetName()) == before);
    {
        Edit edit(document, "Recovered graph action");
        CHECK(edit.Do(RestoreGraphAction(copied)) && edit.Commit().ok);
    }
    CHECK(document.History().Undo() && std::string(level->GetName()) == before);
    CHECK(document.History().Redo() && std::string(level->GetName()) == "Recorded graph name");

    // An invalid later operand is rejected before an earlier write.
    auto invalid = copied;
    invalid.after.operations.push_back(GraphText{{document.Token(), id, UINT64_MAX}, "Wrong lifetime", false});
    const std::string name = level->GetName();
    CHECK(!RestoreGraphAction(invalid)->Redo(document) && std::string(level->GetName()) == name);
    auto wrong = copied;
    wrong.document.value = UINT64_MAX;
    CHECK(!RestoreGraphAction(wrong)->Redo(document));
    CHECK(document.New(nullptr));
    CHECK(!action->Redo(document));

    // An input is addressed by its owner's lifetime and slot. Its Target
    // may be destroyed and recreated without invalidating the saved source.
    auto *context = document.Context();
    auto *behavior = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Target record"));
    CHECK(behavior);
    behavior->ModifyFlags(CKBEHAVIOR_TARGETABLE, 0);
    CHECK(behavior->UseTarget(TRUE) == CK_OK);
    document.Include(behavior->GetID());
    auto *input = behavior->GetTargetParameter();
    CHECK(input);
    auto *source = context->CreateCKParameterOut((CKSTRING) "Source", input->GetGUID());
    CHECK(source);
    document.Include(source->GetID());
    const GraphParameter target{{}, document.Reference(behavior->GetID()), -2};
    GraphActionRecord sourceRecord{document.Token(), {{GraphSource{target, {document.Reference(source->GetID())}, {}}}},
                                   {{GraphSource{target, {}, {}}}}};
    auto sourceAction = RestoreGraphAction(sourceRecord);
    CHECK(sourceAction->Redo(document));
    CHECK(behavior->UseTarget(FALSE) == CK_OK && behavior->UseTarget(TRUE) == CK_OK);
    CHECK(sourceAction->Undo(document) && !behavior->GetTargetParameter()->GetDirectSource());
    CHECK(sourceAction->Redo(document) && behavior->GetTargetParameter()->GetDirectSource() == source);
    const auto oldOwner = document.Reference(behavior->GetID());
    context->DestroyObject(behavior);
    CHECK(!document.Resolve(oldOwner) && !sourceAction->Undo(document));

    // Construct destroys its output on Undo and recreates it on Redo. A
    // following link must name the output by owner/slot, just like an input
    // or Target, while still rejecting a replaced owner.
    CHECK(document.New(nullptr));
    const auto owner = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(owner.ok);
    const auto script = CreateScript(document, owner.created, "Dynamic output replay");
    CHECK(script.ok);
    const auto writer = CreateBehaviorGraph(document, script.created, script.created, "Writer", Point(100, 80));
    const auto reader = CreateBehaviorGraph(document, script.created, script.created, "Reader", Point(300, 80));
    CHECK(writer.ok && reader.ok);
    auto *writerObject = CKBehavior::Cast(document.Context()->GetObject(writer.created));
    auto *readerObject = CKBehavior::Cast(document.Context()->GetObject(reader.created));
    CHECK(writerObject && readerObject);
    writerObject->ModifyFlags(CKBEHAVIOR_BUILDINGBLOCK | CKBEHAVIOR_VARIABLEPARAMETEROUTPUTS, 0);
    CHECK(AddGraphPin(document, script.created, reader.created, PinKind::ParamIn, "In", CKPGUID_INT).ok);
    document.History().Clear();
    CHECK(AddBuildingBlockPin(document, script.created, writer.created, PinKind::ParamOut, "Out", CKPGUID_INT).ok);
    const auto oldOutput = document.Reference(writerObject->GetOutputParameter(0)->GetID());
    CHECK(CreateParameterLink(document, script.created, LinkEndpoint(writer.created, 0, ENDPOINT_POUT),
                              LinkEndpoint(reader.created, 0, ENDPOINT_PIN)).ok);
    CHECK(document.History().Undo() && document.History().Undo());
    CHECK(!document.Resolve(oldOutput) && writerObject->GetOutputParameterCount() == 0);
    CHECK(document.History().Redo() && document.History().Redo());
    CHECK(readerObject->GetInputParameter(0)->GetDirectSource() == writerObject->GetOutputParameter(0));
}

void GraphActionRecordDependencies(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    auto *context = document.Context();
    auto *object = context->CreateObject(CKCID_OBJECT, (CKSTRING) "History dependency");
    CHECK(object);
    const auto ref = document.Reference(object->GetID());
    document.Include(object->GetID());
    document.History().SetLimit(1);
    {
        Edit edit(document, "Bury dependency");
        edit.Bury(object->GetID());
        CHECK(edit.Commit().ok);
    }
    {
        Edit edit(document, "Recorded dependency");
        CHECK(edit.Do(RestoreGraphAction(Names(document, object->GetID()))));
        CHECK(edit.Commit().ok);
    }
    // The bury step has been evicted: the graph record alone retains it.
    CHECK(document.Resolve(ref, CKCID_OBJECT, Document::ObjectScope::Existing));
    CHECK(document.History().Undo());
    const auto level = document.Reference(context->GetCurrentLevel()->GetID());
    CHECK(RenameLevelObject(document, level, "Clear recorded redo").ok);
    CHECK(!document.Resolve(ref, CKCID_OBJECT, Document::ObjectScope::Existing));
    document.History().Clear();

    // Failed Do keeps the record available for explicit compensation.
    int value = 0;
    std::unique_ptr<Action> failing = TestAction(
        [&] {
            value = 7;
            throw std::runtime_error("Replay fault");
            return false;
        },
        [&] {
            value = 0;
            return true;
        });
    Edit edit(document, "Failed recorded action");
    CHECK(!edit.Do(std::move(failing)) && failing && value == 7);
    edit.Done(std::move(failing));
    CHECK(edit.Fail("Injected failure").Status() == EditStatus::Error && value == 0);

    // Typed actions and test actions share the protected history replay.
    {
        Edit recorded(document, "Undo exception");
        recorded.Done(TestAction([] { return true; }, []() -> bool { throw std::runtime_error("Undo fault"); }));
        CHECK(recorded.Commit().ok);
    }
    CHECK(!document.History().Undo() && !document.History().CanUndo() && !document.History().CanRedo());
}
} // namespace vdevtest
