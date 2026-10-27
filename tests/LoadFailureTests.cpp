#include "VirtoolsDevTestSupport.h"

#include <stdexcept>
#include <SDL3/SDL_timer.h>

namespace vdevtest {
namespace {

class FailingLoadManager : public CKBaseManager {
public:
    bool armed = false;
    bool failSequence = false, waitingForSequence = false;
    bool failDeletion = false, waitingForDeletion = false;
    bool failReader = false;
    bool failCapture = false;
    int persistent = 137;
    bool structured = false;
    CK_ID original = 0, parameter = 0, scene = 0;
    explicit FailingLoadManager(CKContext *context)
        : CKBaseManager(context, CKGUID(0x514aa687, 0x237da852), "Load rollback regression") {}
    CKDWORD GetValidFunctionsMask() override {
        return CKMANAGER_FUNC_PostLoad | CKMANAGER_FUNC_OnSequenceAddedToScene | CKMANAGER_FUNC_OnSequenceToBeDeleted;
    }
    CKStateChunk *SaveData(CKFile *file) override {
        if (failCapture) { failCapture = false; throw std::runtime_error("Injected checkpoint failure"); }
        auto *chunk = CreateCKStateChunk(CKCID_OBJECT, file);
        chunk->StartWrite();
        chunk->WriteIdentifier(0x72a1);
        chunk->WriteInt(persistent);
        chunk->CloseChunk();
        return chunk;
    }
    CKERROR LoadData(CKStateChunk *chunk, CKFile *) override {
        if (armed && failReader) {
            armed = false;
            Fail();
        }
        chunk->StartRead();
        if (!chunk->SeekIdentifier(0x72a1)) return CKERR_INVALIDFILE;
        persistent = chunk->ReadInt();
        return CK_OK;
    }
    CKERROR PostLoad() override {
        if (!armed) return CK_OK;
        armed = false;
        if (failDeletion) { waitingForDeletion = true; return CK_OK; }
        if (failSequence) { waitingForSequence = true; return CK_OK; }
        Fail();
        return CK_OK;
    }
    CKERROR SequenceToBeDeleted(CK_ID *ids, int count) override {
        if (waitingForDeletion)
            for (int i = 0; i < count; ++i)
                if (CKLevel::Cast(m_Context->GetObject(ids[i]))) {
                    waitingForDeletion = false;
                    Fail();
                }
        return CK_OK;
    }
    CKERROR SequenceAddedToScene(CKScene *, CK_ID *, int) override {
        if (waitingForSequence) {
            waitingForSequence = false;
            Fail();
        }
        return CK_OK;
    }
    void Fail() {
        persistent = 999;
        m_Context->GetTimeManager()->SetTimeScaleFactor(0.25f);
        m_Context->GetMessageManager()->AddMessageType((CKSTRING) "Failed-load message");
        m_Context->GetAttributeManager()->RegisterNewAttributeType((CKSTRING) "Failed-load attribute", CKPGUID_INT);
        m_Context->GetObject(original)->SetName((CKSTRING) "Changed during failed load");
        VxVector moved(17, 23, 41);
        CK3dEntity::Cast(m_Context->GetObject(original))->SetPosition(&moved);
        CKScene::Cast(m_Context->GetObject(scene))->SetFogEnd(777);
        int value = 999;
        CKParameter::Cast(m_Context->GetObject(parameter))->SetValue(&value, sizeof(value));
#if defined(_WIN32) && defined(_MSC_VER)
        if (structured) RaiseException(0xE0421067, 0, 0, nullptr);
#endif
        throw std::runtime_error("Injected late load failure");
    }
};

std::map<CK_ID, CKObject *> Instances(CKContext *context) {
    std::map<CK_ID, CKObject *> result;
    // Include objects whose deletion notification threw after the SDK had
    // already removed them from its per-class lists.
    for (int i = 1; i <= context->m_ObjectManager->m_ObjectCount; ++i)
        if (auto *object = context->GetObject(i)) result.emplace(object->GetID(), object);
    return result;
}

} // namespace

void LoadFailurePreservation(CkRuntimeHost &host) {
    namespace fs = std::filesystem;
    const fs::path root = fs::current_path() / ("load-failures-" + std::to_string(SDL_GetTicksNS()));
    CHECK(fs::create_directory(root));
    struct Cleanup {
        fs::path root;
        ~Cleanup() { std::error_code error; fs::remove_all(root, error); }
    } cleanup{root};
    Document document(host);
    std::string error;
    CHECK(document.New(&error));
    auto *manager = new FailingLoadManager(document.Context());
    CHECK(document.Context()->RegisterNewManager(manager) == CK_OK);
    auto made = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(made.ok);
    document.Context()->GetObject(made.created)->SetName((CKSTRING) "Incoming frame");
    const auto incomingScript = CreateScript(document, made.created, "Incoming Script");
    CHECK(incomingScript.ok);
    CHECK(AddLocalParameter(document, incomingScript.created, incomingScript.created, "Incoming value", CKPGUID_INT, Point(200, 60)).ok);
    const std::string nmo = (root / "incoming.nmo").u8string();
    const std::string cmo = (root / "incoming.cmo").u8string();
    CHECK(SaveObjectsAs(document, {made.created}, nmo, &error));
    SaveReport report;
    CHECK(SaveDocumentAs(document, cmo, {}, report));

    CHECK(document.New(&error));
    made = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(made.ok && RenameLevelObject(document, made.created, "Original frame").ok);
    const auto child = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(child.ok);
    document.Context()->GetObject(child.created)->SetName((CKSTRING) "Incoming frame");
    CHECK(CK3dEntity::Cast(document.Context()->GetObject(child.created))->SetParent(
        CK3dEntity::Cast(document.Context()->GetObject(made.created))));
    const auto script = CreateScript(document, made.created, "Original Script");
    CHECK(script.ok);
    const auto parameter = AddLocalParameter(document, script.created, script.created, "Original value", CKPGUID_INT, Point(200, 60));
    CHECK(parameter.ok && SetParameterText(document, script.created, parameter.created, "137").ok);
    const std::string target = (root / "original.cmo").u8string();
    CHECK(SaveDocumentAs(document, target, {}, report));
    CHECK(document.Open(target, &error));
    document.History().MarkSavePoint();
    CKContext *context = document.Context();
    CKObject *original = context->GetObjectByNameAndClass((CKSTRING) "Original frame", CKCID_3DENTITY);
    CHECK(original && document.Scripts().size() == 1);
    const CK_ID scriptId = document.Scripts().front().id;
    CKBehavior *behavior = CKBehavior::Cast(context->GetObject(scriptId));
    CKParameterLocal *value = behavior->GetLocalParameter(0);
    CHECK(value);
    ScriptSession *session = document.Session(scriptId);
    CHECK(session);
    const auto before = Capture(document, scriptId);
    const auto generation = host.Generation();
    const auto deleted = CreateScript(document, original->GetID(), "Retained for undo");
    CHECK(deleted.ok);
    ScriptSession *deletedSession = document.Session(deleted.created);
    CHECK(DeleteScript(document, deleted.created).ok);
    document.MarkSaved();
    document.History().MarkSavePoint();
    CHECK(RenameLevelObject(document, original->GetID(), "Redo name").ok);
    CHECK(document.History().Undo() && !document.Dirty());
    const size_t undo = document.History().UndoCount();
    const auto undoLabel = document.History().UndoLabel(), redoLabel = document.History().RedoLabel();
    const auto revision = document.TopologyRevision(), scriptsRevision = document.ScriptsRevision();
    manager->original = original->GetID();
    manager->parameter = value->GetID();
    manager->scene = context->GetCurrentLevel()->GetLevelScene()->GetID();
    CKScene *scene = context->GetCurrentLevel()->GetLevelScene();
    scene->SetFogMode(VXFOG_EXP);
    const float fog = scene->GetFogEnd();
    const auto instances = Instances(context);
    const auto loaded = document.LoadedObjects();
    const auto created = document.CreatedObjects();
    const auto sessionRevision = session->Revision();
    const auto outside = document.ChangesOutsideUndo();
    VxVector position;
    auto *entity = CK3dEntity::Cast(original);
    entity->GetPosition(&position);
    const int children = entity->GetChildrenCount();
    CK3dEntity *originalChild = entity->GetChild(0);
    CHECK(originalChild && originalChild->GetParent() == entity);
    const auto timeScale = context->GetTimeManager()->GetTimeScaleFactor();
    const int messages = context->GetMessageManager()->GetMessageTypeCount();
    const int attributes = context->GetAttributeManager()->GetAttributeCount();

    const int faultKinds =
#if defined(_WIN32) && defined(_MSC_VER)
        2;
#else
        1;
#endif
    for (int fault = 0; fault < faultKinds; ++fault)
    for (int stage = 0; stage < 4; ++stage)
    for (int operation = 0; operation < 6; ++operation) {
        manager->structured = fault != 0;
        manager->failSequence = stage == 1;
        manager->failDeletion = stage == 2;
        manager->failReader = stage == 3;
        manager->armed = true;
        std::vector<CK_ID> output{original->GetID()};
        bool ok = false;
        if (operation < 4) {
            const auto mode = static_cast<Document::ImportMode>(operation);
            ok = document.Import(stage == 2 ? cmo : nmo, mode, &error, &output);
        } else ok = document.Merge(cmo, operation == 5, &error, &output);
        CHECK(!ok && !manager->armed && !manager->waitingForSequence && !manager->waitingForDeletion && !error.empty());
        CHECK(document.Context() == context && host.Generation() == generation);
        CHECK(!context->m_InLoad && !context->m_ObjectManager->m_InLoadSession);
        CHECK(Instances(context) == instances);
        CHECK(std::string(original->GetName()) == "Original frame" && scene->GetFogEnd() == fog && scene->GetFogMode() == VXFOG_EXP);
        VxVector currentPosition;
        entity->GetPosition(&currentPosition);
        CHECK(currentPosition == position && entity->GetChildrenCount() == children);
        int actual = 0;
        CHECK(value->GetValue(&actual, FALSE) == CK_OK && actual == 137);
        CHECK(document.SourcePath() == target && !document.Dirty());
        CHECK(document.LoadedObjects() == loaded && document.CreatedObjects() == created);
        CHECK(document.TopologyRevision() == revision && document.ScriptsRevision() == scriptsRevision);
        CHECK(document.ChangesOutsideUndo() == outside && document.ExistingSession(scriptId) == session);
        CHECK(document.IsDiscarded(deleted.created) && !document.FindScript(deleted.created));
        CHECK(document.ExistingSession(deleted.created) == deletedSession);
        CHECK(session->Revision() == sessionRevision);
        CHECK(manager->persistent == 137 && context->GetTimeManager()->GetTimeScaleFactor() == timeScale);
        CHECK(context->GetMessageManager()->GetMessageTypeCount() == messages);
        CHECK(context->GetAttributeManager()->GetAttributeCount() == attributes);
        CHECK(output == std::vector<CK_ID>{original->GetID()});
        CHECK(document.History().UndoCount() == undo && document.History().CanRedo());
        CHECK(document.History().UndoLabel() == undoLabel && document.History().RedoLabel() == redoLabel);
        ExpectSame(before, Capture(document, scriptId), "failed load restored original Script");
    }
    manager->structured = manager->failSequence = manager->failDeletion = manager->failReader = false;
    for (const auto action : {DuplicateAction::Ignore, DuplicateAction::Rename, DuplicateAction::UseCurrent}) {
        const std::vector<DuplicateName> choices{{-1, CKCID_3DENTITY, "Incoming frame", action}};
        for (bool late : {false, true})
        for (bool merge : {false, true}) {
            manager->armed = true;
            manager->failDeletion = late;
            CHECK(!(merge ? document.Merge(cmo, false, &error, nullptr, &choices)
                          : document.Import(late ? cmo : nmo, late ? Document::ImportMode::Place : Document::ImportMode::File,
                                            &error, nullptr, &choices)));
            CHECK(Instances(context) == instances && !document.Dirty());
            CHECK(document.History().UndoCount() == undo && document.History().CanRedo());
            CHECK(entity->GetChildrenCount() == children && originalChild->GetParent() == entity);
        }
    }
    manager->failDeletion = false;
    CHECK(document.History().Redo() && std::string(original->GetName()) == "Redo name");
    manager->structured = false;
    manager->failSequence = false;
    manager->armed = true;
    CHECK(!document.Import(nmo, Document::ImportMode::File, &error));
    CHECK(document.Dirty() && std::string(original->GetName()) == "Redo name" && !document.History().CanRedo());
    CHECK(document.History().Undo() && std::string(original->GetName()) == "Original frame" && !document.Dirty());
    manager->failCapture = manager->armed = true;
    CHECK(!document.Import(nmo, Document::ImportMode::File, &error) && manager->armed);
    manager->armed = false;
    CHECK(Instances(context) == instances && !document.Dirty() && document.History().CanRedo());
    for (const auto &missing : {root / "missing.nmo", root / "missing.unsupported"}) {
        CHECK(!document.Import(missing.u8string(), Document::ImportMode::File, &error));
        CHECK(Instances(context) == instances && !document.Dirty() && document.History().CanRedo());
    }
    CHECK(document.Import(nmo, Document::ImportMode::File, &error));
    CHECK(document.Dirty() && Instances(context).size() > instances.size());
    std::cout << "Import and Merge failures preserve objects, sessions, revisions and Undo/Redo\n";
}
} // namespace vdevtest
