#include "features/setup/SetupViewData.h"
#include "core/document/Document.h"
#include "core/level/SceneCommands.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

bool RegisterSetupProjectionTests(vdev::EditorApplication &app, std::string &error) {
    using namespace vdev;
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT};
    const EditorParameter args[] = {target, {EDITOR_TEXT("current-scene"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter fields[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
                                      {EDITOR_TEXT("names"), EDITOR_TEXT_LIST}};
    const EditorQueryDesc query{
        sizeof(EditorQueryDesc),
        EDITOR_TEXT("setup.objects"),
        EDITOR_TEXT("test.setup-lists"),
        args,
        2,
        [](void *, const EditorHost *, const EditorRequest *request, EditorSink sink) {
            const EditorValue output[] = {
                {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {&request->values[0].object, 1}},
                {EDITOR_TEXT("names"), EDITOR_TEXT_LIST}};
            return sink.write(sink.data, output, 2);
        },
        nullptr,
        EDITOR_QUERY_EACH_READ,
        EDITOR_DOCUMENT,
        fields,
        2};
    const EditorParameter transformFields[] = {{EDITOR_TEXT("world"), EDITOR_REAL_LIST},
                                               {EDITOR_TEXT("local"), EDITOR_REAL_LIST},
                                               {EDITOR_TEXT("box"), EDITOR_REAL_LIST}};
    const EditorQueryDesc transform{sizeof(EditorQueryDesc),
                                    EDITOR_TEXT("setup.transform"),
                                    EDITOR_TEXT("test.setup-lists"),
                                    &target,
                                    1,
                                    [](void *, const EditorHost *, const EditorRequest *, EditorSink sink) {
                                        const double matrix[16]{}, bounds[6]{};
                                        EditorValue output[] = {{EDITOR_TEXT("world"), EDITOR_REAL_LIST},
                                                                {EDITOR_TEXT("local"), EDITOR_REAL_LIST},
                                                                {EDITOR_TEXT("box"), EDITOR_REAL_LIST}};
                                        output[0].reals = {matrix, 15};
                                        output[1].reals = {matrix, 16};
                                        output[2].reals = {bounds, 6};
                                        return sink.write(sink.data, output, 3);
                                    },
                                    nullptr,
                                    EDITOR_QUERY_EACH_READ,
                                    EDITOR_DOCUMENT,
                                    transformFields,
                                    3};
    const EditorQueryDesc queries[] = {query, transform};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("test.setup-lists");
    module.queries = queries;
    module.queryCount = 2;
    return app.Register(module, error);
}

// Runs with the same registered Setup queries and property editors as the edit tests.
void TestSetupProjections(vdev::Document &doc, vdev::EditorApplication &app) {
    using namespace vdev;
    const EditorClient client(app.Host(), 0);
    const auto create = [&](CK_CLASSID cid, const char *name) {
        auto *object = doc.Context()->CreateObject(cid, name);
        Check(object != nullptr, "Setup projection fixture");
        doc.Context()->GetCurrentLevel()->AddObject(object);
        doc.Include(object->GetID());
        return object;
    };
    const auto page = [&](CKObject *object) {
        SetupPageData result;
        Check(ReadSetupPage(client, doc.Reference(object->GetID()), -1, result), "read Setup page");
        return result;
    };
    auto *first = CK3dEntity::Cast(create(CKCID_3DOBJECT, "Alpha"));
    auto *second = create(CKCID_3DOBJECT, "Zulu");
    auto *privateObject = create(CKCID_3DOBJECT, "Private");
    privateObject->ModifyObjectFlags(CK_OBJECT_PRIVATE, 0);
    const auto ref = doc.Reference(first->GetID());
    Check(page(first).kind == SetupKind::Entity3D && page(first).title == "3D Object Setup" &&
              page(first).name == "Alpha" && page(first).behavioral && page(first).attributeDrop,
          "3D Setup page changed");
    first->SetFlags(first->GetFlags() | CK_3DENTITY_FRAME);
    Check(page(first).title == "3D Frame Setup", "frame Setup title");
    first->SetFlags(first->GetFlags() | CK_3DENTITY_PORTAL);
    Check(page(first).title == "3D Frame Setup", "derived portal title changed");
    auto *portal = CK3dEntity::Cast(create(CKCID_3DENTITY, "Portal"));
    portal->SetFlags(portal->GetFlags() | CK_3DENTITY_PORTAL);
    Check(page(portal).title == "Portal Setup", "portal Setup title");
    auto *place = create(CKCID_PLACE, "Place");
    Check(page(place).title == "3D Frame Setup" && !page(place).initialVisible, "place Setup changed");
    auto *light = CKLight::Cast(create(CKCID_TARGETLIGHT, "Target light"));
    light->SetType(VX_LIGHTSPOT);
    Check(page(light).title == "Target Spot Light Setup" && page(light).kind == SetupKind::Light,
          "light Setup classification");
    auto *camera = CKTargetCamera::Cast(create(CKCID_TARGETCAMERA, "Target camera"));
    camera->SetTarget(first);
    first->SetFlags(first->GetFlags() | CK_3DENTITY_TARGETCAMERA);
    ObjectRef redirected{};
    int point = 99;
    Check(ReadSetupTarget(client, ref, redirected, point) && redirected == doc.Reference(camera->GetID()) &&
              point == -1,
          "target camera Setup redirection");
    auto *curve = CKCurve::Cast(create(CKCID_CURVE, "Curve"));
    auto *control = CKCurvePoint::Cast(create(CKCID_CURVEPOINT, "Control point"));
    curve->AddControlPoint(control);
    Check(ReadSetupTarget(client, doc.Reference(control->GetID()), redirected, point) &&
              redirected == doc.Reference(curve->GetID()) && point == 0,
          "curve point Setup redirection");
    SetupPageData curvePage;
    Check(ReadSetupPage(client, redirected, point, curvePage) && curvePage.point == doc.Reference(control->GetID()),
          "curve point not preserved in Setup data");
    Check(ReadSetupPage(client, redirected, 999, curvePage) && !curvePage.point, "obsolete curve index was used");

    std::vector<SetupObjectRow> rows;
    Check(ReadSetupObjects(client, ref, false, rows), "read Setup name list");
    const auto contains = [&](ObjectRef target) {
        return std::any_of(rows.begin(), rows.end(), [target](const auto &row) { return row.object == target; });
    };
    Check(contains(ref) && contains(doc.Reference(second->GetID())) &&
              !contains(doc.Reference(privateObject->GetID())) && !contains(doc.Reference(camera->GetID())),
          "Setup name list changed exact class or private filtering");
    Check(rows.front().name == "Alpha", "Setup names no longer sorted");
    auto *level = doc.Context()->GetCurrentLevel();
    auto *levelScene = level->GetLevelScene();
    auto *current = CKScene::Cast(create(CKCID_SCENE, "Filtered Setup scene"));
    current->AddObject(first);
    Check(level->AddScene(current) == CK_OK && level->LaunchScene(current) == CK_OK, "Setup scene filter fixture");
    Check(ReadSetupObjects(client, ref, true, rows) && contains(ref) && !contains(doc.Reference(second->GetID())),
          "Setup current scene filter was ignored");
    Check(ReadSetupObjects(client, ref, false, rows) && contains(doc.Reference(second->GetID())),
          "Setup level filter lost another scene's object");
    Check(page(second).sceneScoped && page(second).currentScene && !page(second).inCurrentScene,
          "Setup filter eligibility changed");
    Check(level->LaunchScene(levelScene) == CK_OK, "restore Setup scene filter fixture");
    auto copied = page(second);
    second->SetName("Renamed after query");
    Check(copied.name == "Zulu" && page(second).name == "Renamed after query", "Setup data retained native strings");
    VxVector position(11, 12, 13);
    first->SetPosition(&position);
    SetupTransformData transform;
    Check(ReadSetupTransform(client, ref, transform) && transform.world[12] == 11 && transform.world[13] == 12 &&
              transform.world[14] == 13,
          "Setup transform order changed");
    const auto undo = doc.History().UndoCount();
    const bool redo = doc.History().CanRedo();
    const bool dirty = doc.Dirty();
    Check(page(first).initialHere, "Setup initial condition presence");
    for (int i = 0; i < 3; ++i) {
        page(first);
        Check(ReadSetupObjects(client, ref, true, rows) && ReadSetupTransform(client, ref, transform),
              "repeated Setup read");
    }
    Check(doc.History().UndoCount() == undo && doc.History().CanRedo() == redo && doc.Dirty() == dirty,
          "Setup query changed the document or history");
    EditorStatus workerStatus = EDITOR_OK;
    std::thread worker([&] { workerStatus = client.Query("setup.page", {ObjectArgument("target", ref)}).status; });
    worker.join();
    Check(workerStatus == EDITOR_REFUSED, "Setup query accessed CK from a worker thread");

    // An independent provider can replace a common query. Malformed pages never
    // replace a view's last complete copy.
    std::string error;
    const auto initial = app.Configuration();
    auto configuration = initial;
    for (auto &choice : configuration.providers)
        if (choice.kind == "query" && (choice.id == "setup.objects" || choice.id == "setup.transform"))
            choice.provider = "test.setup-lists";
    Check(app.Configure(configuration, error), error.c_str());
    const auto savedRows = rows;
    Check(!ReadSetupObjects(client, ref, true, rows) && rows.size() == savedRows.size() &&
              rows.front().object == savedRows.front().object && rows.front().name == savedRows.front().name,
          "incomplete Setup list replaced the previous copy");
    const auto savedTransform = transform;
    Check(!ReadSetupTransform(client, ref, transform) && transform.world == savedTransform.world &&
              transform.local == savedTransform.local && transform.box == savedTransform.box,
          "incomplete Setup transform replaced the previous copy");
    Check(app.Configure(initial, error), error.c_str());

    const auto retired = doc.Reference(second->GetID());
    Check(doc.Context()->DestroyObject(second, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK,
          "retire Setup fixture");
    auto *replacement = create(CKCID_3DOBJECT, "Replacement lifetime");
    Check(replacement->GetID() == retired.object && doc.Reference(replacement->GetID()) != retired,
          "Setup ID reuse fixture");
    const auto savedPage = copied;
    Check(!ReadSetupPage(client, retired, -1, copied) && copied.target == savedPage.target &&
              copied.name == savedPage.name,
          "old Setup reference selected another lifetime");
    Check(page(replacement).name == "Replacement lifetime", "fresh Setup reference failed");
    auto foreign = ref;
    ++foreign.document.value;
    Check(!ReadSetupTarget(client, foreign, redirected, point), "Setup query accepted a foreign document");
    Check(doc.New(&error), error.c_str());
    app.Attach(doc.Token(), doc.TopologyRevision(), true, true);
    Check(!ReadSetupPage(client, ref, -1, copied) && copied.target == savedPage.target,
          "Setup query crossed document replacement");
}
