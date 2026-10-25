#include "VirtoolsDevTestSupport.h"

namespace vdevtest {

class InitialConditionTestObject : public CKSceneObject {
public:
    int value = 0;
    explicit InitialConditionTestObject(CKContext *context) : CKSceneObject(context, "Initial conditions regression") {}
    CKStateChunk *Save(CKFile *, CKDWORD) override {
        CKStateChunk *state = CreateCKStateChunk(CKCID_SCENEOBJECT, nullptr);
        state->StartWrite();
        state->WriteIdentifier(0x7162);
        state->WriteInt(value);
        state->CloseChunk();
        return state;
    }
    CKERROR Load(CKStateChunk *state, CKFile *) override {
        state->StartRead();
        if (!state->SeekIdentifier(0x7162)) return CKERR_INVALIDPARAMETER;
        value = state->ReadInt();
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
    int Initial(CKScene *scene) {
        CKStateChunk *state = scene->GetObjectInitialValue(object);
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

} // namespace vdevtest
