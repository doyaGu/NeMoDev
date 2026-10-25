#include "VirtoolsDevTestSupport.h"
#include <stdexcept>

namespace vdevtest {

class InitialConditionTestObject : public CKSceneObject {
public:
    int value = 0;
    int saves = 0, loads = 0, failSave = 0, throwSave = 0, failLoad = 0;
    bool rejectLoads = false;
    explicit InitialConditionTestObject(CKContext *context) : CKSceneObject(context, "Initial conditions regression") {}
    CKStateChunk *Save(CKFile *, CKDWORD) override {
        ++saves;
        if (saves == throwSave) throw std::runtime_error("Initial condition capture failed");
        if (saves == failSave) return nullptr;
        CKStateChunk *state = CreateCKStateChunk(CKCID_SCENEOBJECT, nullptr);
        state->StartWrite();
        state->WriteIdentifier(0x7162);
        state->WriteInt(value);
        state->CloseChunk();
        return state;
    }
    CKERROR Load(CKStateChunk *state, CKFile *) override {
        ++loads;
        state->StartRead();
        if (!state->SeekIdentifier(0x7162)) return CKERR_INVALIDPARAMETER;
        value = state->ReadInt();
        if (loads == failLoad || rejectLoads) {
            value = -100;
            return CKERR_INVALIDPARAMETER;
        }
        return CK_OK;
    }
};

struct InitialConditionsFixture {
    Document document;
    CKLevel *level;
    CKScene *first, *other;
    InitialConditionTestObject *object;
    explicit InitialConditionsFixture(CkRuntimeHost &host) : document(host) {
        CHECK(document.New(nullptr));
        auto *context = document.Context();
        level = context->GetCurrentLevel();
        first = level->GetLevelScene();
        other = CKScene::Cast(context->CreateObject(CKCID_SCENE, "Other scene"));
        CHECK(other && level->AddScene(other) == CK_OK);
        object = new InitialConditionTestObject(context);
        CHECK(level->AddObject(object) == CK_OK);
        other->AddObjectToScene(object, FALSE);
        document.Include(other->GetID());
        document.Include(object->GetID());
    }
    void Select(CKScene *scene) {
        CHECK(level->LaunchScene(scene, CK_SCENEOBJECTACTIVITY_SCENEDEFAULT, CK_SCENEOBJECTRESET_DONOTHING) == CK_OK);
        CHECK(InitialConditionsScene(document.Context()) == scene);
    }
    void SetInitial(CKScene *scene, int value) {
        const int previous = object->value;
        object->value = value;
        CKStateChunk *state = CKSaveObjectState(object);
        object->value = previous;
        CHECK(state && scene->SetObjectInitialValue(object, state));
    }
    int Initial(CKScene *scene, InitialConditionTestObject *target = nullptr) {
        CKStateChunk *state = scene->GetObjectInitialValue(target ? target : object);
        CHECK(state);
        state->StartRead();
        CHECK(state->SeekIdentifier(0x7162));
        return state->ReadInt();
    }
    void MarkSaved() {
        document.History().Clear();
        document.MarkSaved();
        document.History().MarkSavePoint();
    }
};

void InitialConditionsScenes(CkRuntimeHost &host) {
    InitialConditionsFixture fixture(host);
    auto &document = fixture.document;
    auto *object = fixture.object;
    for (auto action : {InitialConditions::Set, InitialConditions::Remove, InitialConditions::Restore}) {
        fixture.Select(fixture.first);
        if (action == InitialConditions::Set) CHECK(fixture.first->SetObjectInitialValue(object, nullptr));
        else fixture.SetInitial(fixture.first, 11);
        fixture.SetInitial(fixture.other, 22);
        object->value = 33;
        fixture.MarkSaved();
        CHECK(ApplyInitialConditions(document, {object->GetID()}, action).ok);
        CHECK(document.Dirty());
        if (action == InitialConditions::Set) CHECK(fixture.Initial(fixture.first) == 33);
        else if (action == InitialConditions::Remove) CHECK(!fixture.first->GetObjectInitialValue(object));
        else CHECK(object->value == 11);
        fixture.Select(fixture.other);
        CHECK(document.History().Undo() && !document.Dirty());
        if (action == InitialConditions::Set) CHECK(!fixture.first->GetObjectInitialValue(object));
        else CHECK(fixture.Initial(fixture.first) == 11);
        CHECK(fixture.Initial(fixture.other) == 22);
        CHECK(object->value == 33);
        CHECK(document.History().Redo() && document.Dirty());
        CHECK(InitialConditionsScene(document.Context()) == fixture.other);
        CHECK(fixture.Initial(fixture.other) == 22);
        if (action == InitialConditions::Set) CHECK(fixture.Initial(fixture.first) == 33);
        else if (action == InitialConditions::Remove) CHECK(!fixture.first->GetObjectInitialValue(object));
        else CHECK(object->value == 11);
    }
}

void InitialConditionsFailures(CkRuntimeHost &host) {
    InitialConditionsFixture fixture(host);
    auto &document = fixture.document;
    auto *first = fixture.object;
    auto *second = new InitialConditionTestObject(document.Context());
    CHECK(fixture.level->AddObject(second) == CK_OK);
    document.Include(second->GetID());
    const auto reset = [&] {
        fixture.Select(fixture.first);
        first->rejectLoads = second->rejectLoads = false;
        first->failSave = first->throwSave = first->failLoad = 0;
        second->failSave = second->throwSave = second->failLoad = 0;
        fixture.SetInitial(fixture.first, 11);
        second->value = 22;
        CHECK(fixture.first->SetObjectInitialValue(second, CKSaveObjectState(second)));
        first->value = 33;
        second->value = 44;
        fixture.MarkSaved();
    };
    const std::vector<CK_ID> objects{first->GetID(), second->GetID()};
    for (auto action : {InitialConditions::Set, InitialConditions::Restore})
        for (bool raised : {false, true})
            for (auto *failed : {first, second}) {
                reset();
                CHECK(ApplyInitialConditions(document, objects, InitialConditions::Set).ok);
                CHECK(document.History().Undo() && !document.Dirty());
                if (raised) failed->throwSave = failed->saves + 1;
                else failed->failSave = failed->saves + 1;
                CHECK(ApplyInitialConditions(document, objects, action).Status() == EditStatus::Error);
                CHECK(first->value == 33 && second->value == 44 && !document.Dirty());
                CHECK(fixture.Initial(fixture.first) == 11 && fixture.Initial(fixture.first, second) == 22);
                CHECK(!document.History().CanUndo() && document.History().CanRedo());
            }

    reset();
    second->failLoad = second->loads + 1;
    CHECK(ApplyInitialConditions(document, objects, InitialConditions::Restore).Status() == EditStatus::Error);
    CHECK(first->value == 33 && second->value == 44 && !document.Dirty());
    CHECK(!document.History().CanUndo());

    reset();
    second->rejectLoads = true;
    CHECK(ApplyInitialConditions(document, objects, InitialConditions::Restore).Status() == EditStatus::Error);
    CHECK(first->value == 33 && second->value == -100 && document.Dirty());
    CHECK(!document.History().CanUndo() && !document.History().CanRedo());

    for (bool undo : {true, false}) {
        reset();
        CHECK(ApplyInitialConditions(document, objects, InitialConditions::Restore).ok);
        if (!undo) CHECK(document.History().Undo() && !document.Dirty());
        second->failLoad = second->loads + 1;
        CHECK(!(undo ? document.History().Undo() : document.History().Redo()));
        CHECK(first->value == (undo ? 11 : 33) && second->value == (undo ? 22 : 44));
        CHECK(document.Dirty() && !document.History().CanUndo() && !document.History().CanRedo());
    }
    reset();
    {
        Edit edit(document, "Cancel initial conditions");
        CHECK(ApplyInitialConditions(document, objects, InitialConditions::Restore).ok);
        CHECK(first->value == 11 && second->value == 22);
        fixture.Select(fixture.other);
        CHECK(edit.Cancel());
    }
    CHECK(first->value == 33 && second->value == 44 && !document.Dirty());
    CHECK(!document.History().CanUndo());
}

} // namespace vdevtest
