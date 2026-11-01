#include "features/layout/LayoutCommands.h"
#include "features/objects/SelectionCommands.h"
#include "features/schematic/SchematicGraphCommands.h"
#include "features/schematic/SchematicPosition.h"
#include "workspace/EditorClient.h"
#include "application/ObjectSelection.h"
#include "core/document/Clipboard.h"
#include "core/document/ScriptSession.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/TopologyCommands.h"
#include "core/layout/LayoutView.h"
#include "core/layout/LayoutTools.h"
#include "core/level/LevelCommands.h"
#include "core/schematic/SchematicEdits.h"
#include "core/runtime/CkRuntimeHost.h"
#include "editor/ImGuiDrawing.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
const EditorField &Field(const EditorResponse &r, const char *key) {
    auto *f = EditorClient::Find(r, key);
    Check(f, "missing gesture field");
    return *f;
}
EditorField Number(const char *key, uint64_t n) { return {key, EDITOR_INTEGER_VALUE, {}, {}, n}; }
EditorField Reals(const char *key, std::initializer_list<double> n) {
    EditorField f{key, EDITOR_REAL_LIST};
    f.reals = n;
    return f;
}
struct Snapshot {
    Document &doc;
    size_t undo;
    bool redo, dirty;
    uint64_t revision, topology;
    std::vector<ObjectRef> selected;
    ObjectSelection &selection;
    Snapshot(Document &doc, ObjectSelection &selection)
        : doc(doc), undo(doc.History().UndoCount()), redo(doc.History().CanRedo()), dirty(doc.Dirty()),
          revision(doc.ChangeRevision()), topology(doc.TopologyRevision()), selected(selection.References()),
          selection(selection) {}
    void Unchanged(const char *message) const {
        Check(doc.History().UndoCount() == undo && doc.History().CanRedo() == redo && doc.Dirty() == dirty &&
                  doc.ChangeRevision() == revision && doc.TopologyRevision() == topology &&
                  selection.References() == selected,
              message);
    }
};
} // namespace
void TestGestureCommands(vdev::CkRuntimeHost &runtime, const std::string &modulePath) {
    using namespace vdev;
    Document doc(runtime);
    std::string error;
    Check(doc.New(&error), error.c_str());
    LayoutView view(runtime);
    view.CompositionChanged();
    const auto object = CreateLayoutObject(doc, LayoutObject::Frame3D),
               other = CreateLayoutObject(doc, LayoutObject::Frame3D);
    const auto script = CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(), "Gesture Script");
    const auto graph = CreateBehaviorGraph(doc, script.created, script.created, "Moved graph", Point(240, 120));
    Check(object.ok && other.ok && script.ok && graph.ok, "create gesture fixture");
    ObjectSelection selection;
    selection.Restore(doc, {object.created});
    Clipboard clipboard;
    bool available = true;
    EditorApplication app([&] { return &doc; }, [&] { return available; });
    LayoutCommands layout(app, [&] { return &doc; }, [&]() -> LayoutView & { return view; }, selection);
    SchematicGraphCommands schematic([&] { return &doc; }, [] { return true; }, clipboard);
    SelectionCommands selections([&] { return &doc; }, selection);
    EditorConfiguration builtins;
    builtins.providers = {{"command", "schematic.edit-geometry", "builtin.schematic-geometry"},
                          {"command", "layout.edit-gesture", "builtin.layout-gestures"}};
    Check(layout.Register(error) && schematic.Register(app, error) && selections.Register(app, error) &&
              app.Load(modulePath, error) && app.Activate(EDITOR_IMGUI_VERSION, error, builtins),
          error.c_str());
    auto sync = [&] { app.Attach(doc.Token(), doc.TopologyRevision(), true, true); };
    sync();
    const auto scope = app.OpenScope();
    const EditorClient client(app.Host(), scope);
    void *outsidePreview = nullptr;
    const EditorRequest outsideRequest{client.Scope()};
    Check(app.Host()->previewCommand(app.Host()->data, EDITOR_TEXT("layout.edit-gesture"), &outsideRequest,
                                      EDITOR_PREVIEW_UPDATE, &outsidePreview, {}) == EDITOR_REFUSED && !outsidePreview,
          "preview delegation escaped a command callback");
    auto run = [&](const char *id, const std::vector<EditorField> &fields) {
        const auto v = BorrowEditorFields(fields);
        return client.Command(id, {v.data(), v.data() + v.size()});
    };
    EditorEdit edit = 0;
    auto begin = [&](const char *id, const std::vector<EditorField> &fields) {
        const auto v = BorrowEditorFields(fields);
        return client.BeginCommand(id, {v.data(), v.data() + v.size()}, edit);
    };
    auto update = [&](EditorField value) {
        const std::vector<EditorField> fields{std::move(value)};
        const auto v = BorrowEditorFields(fields);
        return app.UpdateEdit(edit, *v.data());
    };
    auto accept = [&] {
        EditorOperation operation = 0;
        Check(app.FinishEdit(edit, true, &operation) == EDITOR_QUEUED && operation, "queue gesture commit");
        const EditorClient results(app.Host(), 0);
        Check(results.Completion(operation).status == EDITOR_QUEUED, "gesture result completed before dispatch");
        edit = 0;
        app.EndFrame();
        sync();
        const auto completed = results.Completion(operation);
        Check(completed.status != EDITOR_QUEUED && completed.documentRevision == doc.ChangeRevision(),
              "gesture result has wrong committed document revision");
        return completed;
    };
    auto cancel = [&] {
        Check(app.FinishEdit(edit, false) == EDITOR_CANCELLED, "cancel gesture");
        edit = 0;
    };
    auto saved = [&] {
        doc.History().Clear();
        doc.MarkSaved();
        doc.History().MarkSavePoint();
        sync();
    };
    saved();
    auto geometry = [&](unsigned mode = 0) {
        const auto address =
            ElementAddress::Capture(doc, script.created, {ElementKind::Behavior, script.created, graph.created});
        EditorField elements{"elements", EDITOR_RECORD};
        elements.fields.push_back(SchematicPosition(address, "0"));
        const std::vector<EditorField> args{{"target", EDITOR_OBJECT, address.script},
                                            Number("mode", mode),
                                            elements,
                                            SchematicPosition(address, "primary")};
        const auto v = BorrowEditorFields(args);
        const auto result = client.Query("schematic.geometry", {v.data(), v.data() + v.size()});
        Check(result.status == EDITOR_OK, "prepare geometry projection");
        return result.fields;
    };
    auto delta = [&](double x, double y) {
        EditorField value{"value", EDITOR_RECORD};
        value.fields = {Reals("delta", {x, y})};
        return value;
    };
    const Rect before = FindRecord(doc.Session(script.created)->Data(), graph.created)->rect;
    const Snapshot original(doc, selection);
    Check(begin("schematic.edit-geometry", geometry()) == EDITOR_OK, "begin Schematic move");
    for (int i = 1; i <= 4; ++i)
        Check(update(delta(i * 20, 40)) == EDITOR_OK, "preview Schematic move");
    Check(doc.Session(script.created)->HasDisplayPreview() &&
              FindRecord(doc.Session(script.created)->Data(), graph.created)->rect.hPos == before.hPos &&
              FindRecord(doc.Session(script.created)->DisplayData(), graph.created)->rect.hPos == before.hPos + 80,
          "Schematic preview changed authoritative layout");
    original.Unchanged("Schematic preview changed document, selection or history");
    cancel();
    Check(!doc.Session(script.created)->HasDisplayPreview(), "cancel retained Schematic display");
    original.Unchanged("Schematic cancel changed saved state");
    Check(begin("schematic.edit-geometry", geometry()) == EDITOR_OK && update(delta(60, 20)) == EDITOR_OK,
          "begin committed Schematic move");
    accept();
    Check(doc.History().UndoCount() == 1 && doc.Dirty() &&
              FindRecord(doc.Session(script.created)->Data(), graph.created)->rect.hPos == before.hPos + 60,
          "Schematic move did not commit once");
    Check(doc.History().Undo() && !doc.Dirty() &&
              FindRecord(doc.Session(script.created)->Data(), graph.created)->rect.hPos == before.hPos,
          "Schematic undo missed save point");
    Check(doc.History().Redo(), "redo Schematic move");
    Check(doc.History().Undo(), "restore Schematic move");
    sync();
    const Snapshot noMove(doc, selection);
    Check(begin("schematic.edit-geometry", geometry()) == EDITOR_OK && update(delta(0, 0)) == EDITOR_OK,
          "unchanged Schematic preview");
    accept();
    noMove.Unchanged("unchanged Schematic gesture added undo");
    Check(begin("schematic.edit-geometry", geometry()) == EDITOR_OK && update(delta(20, 0)) == EDITOR_OK,
          "queued cancellation preview");
    Check(app.FinishEdit(edit, true) == EDITOR_QUEUED, "queue scope close gesture");
    app.CloseScope(scope);
    app.EndFrame();
    Check(!doc.Session(script.created)->HasDisplayPreview(), "closed scope retained queued display");
    noMove.Unchanged("closed scope committed a queued gesture");
    const auto liveScope = app.OpenScope();
    const EditorClient live(app.Host(), liveScope);
    auto liveBegin = [&](const char *id, const std::vector<EditorField> &fields) {
        const auto v = BorrowEditorFields(fields);
        return live.BeginCommand(id, {v.data(), v.data() + v.size()}, edit);
    };
    // The independent module forwards every preview phase using only Editor.h.
    EditorConfiguration external;
    external.providers = {{"command", "schematic.edit-geometry", "example.gestures"},
                          {"command", "layout.edit-gesture", "example.gestures"}};
    Check(app.Configure(external, error), error.c_str());
    auto failure = [&](unsigned mode) {
        const EditorValue v{EDITOR_TEXT("mode"), EDITOR_INTEGER_VALUE, {}, {}, mode};
        Check(app.Execute("example.gesture-failure", {app.Scope(liveScope), &v, 1}).status == EDITOR_OK,
              "set gesture failure");
    };
    auto freshGeometry = [&] {
        const auto address =
            ElementAddress::Capture(doc, script.created, {ElementKind::Behavior, script.created, graph.created});
        EditorField elements{"elements", EDITOR_RECORD};
        elements.fields.push_back(SchematicPosition(address, "0"));
        const std::vector<EditorField> args{{"target", EDITOR_OBJECT, address.script},
                                            Number("mode", 0),
                                            elements,
                                            SchematicPosition(address, "primary")};
        const auto v = BorrowEditorFields(args);
        const auto result = live.Query("schematic.geometry", {v.data(), v.data() + v.size()});
        Check(result.status == EDITOR_OK, "prepare external geometry");
        return result.fields;
    };
    for (unsigned mode = 1; mode <= 4; ++mode) {
        failure(mode);
        const Snapshot unchanged(doc, selection);
        Check(liveBegin("schematic.edit-geometry", freshGeometry()) == EDITOR_OK, "begin external failed move");
        const auto status = update(delta(40, 20));
        if (mode == 1)
            Check(status == EDITOR_ERROR && !app.HasEdits(), "throwing external preview survived");
        else {
            Check(status == EDITOR_OK, "external preview failed early");
            accept();
        }
        Check(!doc.Session(script.created)->HasDisplayPreview() &&
                  FindRecord(doc.Session(script.created)->Data(), graph.created)->rect.hPos == before.hPos,
              "failed external move left layout changed");
        unchanged.Unchanged("failed external move changed document or history");
    }
    failure(0);
    Check(liveBegin("schematic.edit-geometry", freshGeometry()) == EDITOR_OK && update(delta(40, 0)) == EDITOR_OK,
          "external Schematic preview");
    Check(!app.Configure(app.Configuration(), error), "configuration replaced a live preview");
    Check(app.FinishEdit(edit, true) == EDITOR_QUEUED, "queue external preview");
    Check(!app.Configure(app.Configuration(), error), "configuration replaced queued live state");
    app.EndFrame();
    sync();
    Check(doc.History().UndoCount() == 1 && doc.History().Undo() && !doc.Dirty(),
          "external Schematic command differs from builtin");
    sync();
    Check(EditorClient::Integer(live.Query("example.gesture-state"), "releases") >= 1 &&
              EditorClient::Integer(live.Query("example.gesture-state"), "cancels") >= 3,
          "external phases were not forwarded");
    // Indexed elements may be kept only across their own completed geometry
    // command. A later edit or Undo must make that completion unusable.
    Check(EditScriptData(doc, script.created, "Comment fixture",
                         [](InterfaceData &data) {
                             Comment comment;
                             comment.hPos = 400;
                             comment.vPos = 320;
                             comment.width = 160;
                             comment.height = 60;
                             comment.text = "Moved comment";
                             data.rootBehavior.AddComment(comment);
                         }),
          "add indexed gesture fixture");
    saved();
    const ElementRef commentRef{ElementKind::Comment, script.created, 0, 0};
    const auto commentAddress = ElementAddress::Capture(doc, script.created, commentRef);
    const auto indexedGeometry = [&](uint64_t mode) {
        const auto address = ElementAddress::Capture(doc, script.created, commentRef);
        EditorField elements{"elements", EDITOR_RECORD};
        elements.fields = {SchematicPosition(address, "0")};
        const std::vector<EditorField> args{{"target", EDITOR_OBJECT, address.script},
                                            Number("mode", mode),
                                            elements,
                                            SchematicPosition(address, "primary")};
        const auto values = BorrowEditorFields(args);
        const auto projected = live.Query("schematic.geometry", {values.data(), values.data() + values.size()});
        Check(projected.status == EDITOR_OK, "prepare indexed geometry");
        return projected.fields;
    };
    const Snapshot commentBefore(doc, selection);
    Check(liveBegin("schematic.edit-geometry", indexedGeometry(0)) == EDITOR_OK && update(delta(40, 20)) == EDITOR_OK,
          "preview comment move");
    cancel();
    commentBefore.Unchanged("comment cancellation changed document or history");
    Check(doc.Session(script.created)->Data().rootBehavior.comments.front().hPos == 400,
          "comment cancellation retained preview");
    Check(liveBegin("schematic.edit-geometry", indexedGeometry(0)) == EDITOR_OK && update(delta(40, 20)) == EDITOR_OK,
          "commit comment move");
    const auto movedComment = accept();
    Check(movedComment.status == EDITOR_OK && doc.History().UndoCount() == 1, "comment move did not commit once");
    EditorField kept{"elements", EDITOR_RECORD};
    kept.fields = {SchematicPosition(commentAddress, "0")};
    const std::vector<EditorField> keepArgs{kept, Number("document-revision", movedComment.documentRevision)};
    const auto keepValues = BorrowEditorFields(keepArgs);
    const auto recaptured =
        live.Query("schematic.positions", {keepValues.data(), keepValues.data() + keepValues.size()});
    Check(recaptured.status == EDITOR_OK && Field(recaptured, "elements").fields.size() == 1,
          "committed comment lost selection address");
    const auto ownedPosition = BorrowEditorFields(Field(recaptured, "elements").fields);
    ElementAddress afterComment;
    CK_ID inScript = 0;
    ElementRef resolvedComment;
    Check(ReadSchematicPosition(*ownedPosition.data(), afterComment) &&
              afterComment.Resolve(doc, inScript, resolvedComment) && resolvedComment == commentRef,
          "rebased comment position did not resolve");
    Check(doc.History().Undo() && !doc.Dirty(), "undo indexed move lost save point");
    sync();
    Check(live.Query("schematic.positions", {keepValues.data(), keepValues.data() + keepValues.size()}).status ==
                  EDITOR_STALE &&
              !afterComment.Resolve(doc, inScript, resolvedComment),
          "completed geometry reinterpreted an old index after Undo");
    EditorField size{"value", EDITOR_RECORD};
    size.fields = {Reals("size", {220, 100})};
    Check(liveBegin("schematic.edit-geometry", indexedGeometry(3)) == EDITOR_OK && update(size) == EDITOR_OK,
          "preview comment resize");
    cancel();
    Check(doc.Session(script.created)->Data().rootBehavior.comments.front().width == 160 && !doc.Dirty(),
          "cancel resize lost comment size");
    Check(liveBegin("schematic.edit-geometry", indexedGeometry(3)) == EDITOR_OK && update(size) == EDITOR_OK,
          "commit comment resize");
    accept();
    Check(doc.History().UndoCount() == 1 && doc.History().Undo() && !doc.Dirty() &&
              doc.Session(script.created)->Data().rootBehavior.comments.front().width == 160,
          "comment resize did not restore original size with one Undo");
    sync();
    auto *entity = CK3dEntity::Cast(doc.Context()->GetObject(object.created));
    const VxMatrix matrix = entity->GetWorldMatrix();
    auto motion = [&](int x, int y) {
        EditorField value{"value", EDITOR_RECORD};
        value.fields = {Reals("position", {double(x), double(y)}), Number("move", 1)};
        return value;
    };
    auto transform = [&](unsigned kind = 1, unsigned action = 0) {
        const auto input = live.Query("layout.input");
        Check(input.status == EDITOR_OK, "read Layout input state");
        EditorField options{"options", EDITOR_RECORD};
        options.fields = {Number("axes", kind == 2 ? 3 : 5), Number("referential", 1), Number("translate-sticky", 0)};
        EditorField objects{"objects", EDITOR_OBJECT_LIST};
        objects.objects = selection.References();
        return std::vector<EditorField>{Field(input, "target"),
                                        Number("kind", kind),
                                        Number("action", action),
                                        objects,
                                        Reals("start", {100, 100}),
                                        options,
                                        motion(100, 100)};
    };
    saved();
    const Snapshot layoutBefore(doc, selection);
    Check(liveBegin("layout.edit-gesture", transform()) == EDITOR_OK && update(motion(160, 130)) == EDITOR_OK,
          "preview Layout transform");
    Check(std::memcmp(&matrix, &entity->GetWorldMatrix(), sizeof(matrix)) != 0, "Layout preview did not move object");
    layoutBefore.Unchanged("Layout preview recorded document edits");
    cancel();
    Check(std::memcmp(&matrix, &entity->GetWorldMatrix(), sizeof(matrix)) == 0, "Layout cancel lost matrix");
    layoutBefore.Unchanged("Layout cancellation changed document");
    Check(liveBegin("layout.edit-gesture", transform()) == EDITOR_OK, "begin Layout commit");
    for (int x = 110; x <= 180; x += 10)
        Check(update(motion(x, 130)) == EDITOR_OK, "update Layout commit");
    accept();
    Check(doc.History().UndoCount() == 1 && doc.Dirty(), "Layout drag was not one undo step");
    Check(doc.History().Undo() && !doc.Dirty() && std::memcmp(&matrix, &entity->GetWorldMatrix(), sizeof(matrix)) == 0,
          "Layout undo lost save point or matrix");
    sync();
    for (unsigned mode = 1; mode <= 4; ++mode) {
        failure(mode);
        const Snapshot unchanged(doc, selection);
        Check(liveBegin("layout.edit-gesture", transform()) == EDITOR_OK, "begin failed Layout gesture");
        const auto status = update(motion(180, 130));
        if (mode == 1)
            Check(status == EDITOR_ERROR && !app.HasEdits(), "failed Layout update survived");
        else {
            Check(status == EDITOR_OK, "Layout failure ran before commit");
            accept();
        }
        Check(std::memcmp(&matrix, &entity->GetWorldMatrix(), sizeof(matrix)) == 0,
              "failed Layout gesture did not restore matrix");
        unchanged.Unchanged("failed Layout command changed selection, history or document");
    }
    failure(0);
    for (auto action : {TransformAction::Rotate, TransformAction::Scale}) {
        const auto before = entity->GetWorldMatrix();
        const auto steps = doc.History().UndoCount();
        Check(liveBegin("layout.edit-gesture", transform(1, unsigned(action))) == EDITOR_OK &&
                  update(motion(180, 140)) == EDITOR_OK,
              "preview rotation or scale");
        Check(std::memcmp(&before, &entity->GetWorldMatrix(), sizeof(before)) != 0,
              "rotation or scale did not change matrix");
        accept();
        Check(doc.History().UndoCount() == steps + 1 && doc.History().Undo(),
              "rotation or scale did not produce one undo");
        Check(std::memcmp(&before, &entity->GetWorldMatrix(), sizeof(before)) == 0,
              "rotation or scale undo lost matrix");
        sync();
    }
    const auto sprite = CreateLayoutObject(doc, LayoutObject::Frame2D);
    Check(sprite.ok, "create sprite gesture fixture");
    selection.Restore(doc, {sprite.created});
    saved();
    auto *spriteObject = CK2dEntity::Cast(doc.Context()->GetObject(sprite.created));
    Vx2DVector originalPosition;
    spriteObject->GetPosition(originalPosition);
    auto spriteFields = transform(2);
    spriteFields.push_back({"grabbed", EDITOR_OBJECT, doc.Reference(sprite.created)});
    Check(liveBegin("layout.edit-gesture", spriteFields) == EDITOR_OK && update(motion(180, 130)) == EDITOR_OK,
          "preview 2D transform");
    Vx2DVector movedPosition;
    spriteObject->GetPosition(movedPosition);
    Check(movedPosition.x != originalPosition.x || movedPosition.y != originalPosition.y,
          "2D preview did not move sprite");
    cancel();
    spriteObject->GetPosition(movedPosition);
    Check(movedPosition.x == originalPosition.x && movedPosition.y == originalPosition.y && !doc.Dirty(),
          "2D cancellation lost original position");
    Check(liveBegin("layout.edit-gesture", spriteFields) == EDITOR_OK && update(motion(180, 130)) == EDITOR_OK,
          "commit 2D transform");
    accept();
    Check(doc.History().UndoCount() == 1 && doc.History().Undo() && !doc.Dirty(),
          "2D transform did not undo to save point");
    sync();
    selection.Restore(doc, {object.created});
    saved();
    const auto flightMatrix = view.Camera()->GetWorldMatrix();
    auto flight = transform(3);
    Check(liveBegin("layout.edit-gesture", flight) == EDITOR_OK, "begin flight");
    auto flying = motion(180, 150);
    EditorField time{"time", EDITOR_REAL_VALUE};
    time.real = 1;
    flying.fields.push_back(time);
    Check(update(flying) == EDITOR_OK &&
              std::memcmp(&flightMatrix, &view.Camera()->GetWorldMatrix(), sizeof(flightMatrix)) != 0,
          "flight did not move camera");
    cancel();
    Check(std::memcmp(&flightMatrix, &view.Camera()->GetWorldMatrix(), sizeof(flightMatrix)) == 0 && !doc.Dirty(),
          "flight cancellation lost original camera");
    // An orthographic default camera switches to the free view on Orbit,
    // then cancellation restores the original camera and its full state.
    const auto top = view.ViewCamera(LayoutViewCamera::Top);
    view.SetCamera(top);
    const Snapshot cameraBefore(doc, selection);
    Check(liveBegin("layout.edit-gesture", transform(0, unsigned(CameraAction::Orbit))) == EDITOR_OK &&
              update(motion(140, 120)) == EDITOR_OK,
          "begin camera orbit");
    Check(view.Camera()->GetID() != top, "camera gesture did not select free view");
    cancel();
    Check(view.Camera()->GetID() == top, "camera cancel lost original view");
    cameraBefore.Unchanged("camera cancel changed document history");
    // Wrong thread and busy native work cannot begin or update a gesture.
    EditorStatus worker = EDITOR_OK;
    const auto requestFields = transform();
    const auto requestValues = BorrowEditorFields(requestFields);
    std::thread thread([&] {
        EditorEdit token = 0;
        worker = live.BeginCommand("layout.edit-gesture",
                                   {requestValues.data(), requestValues.data() + requestValues.size()}, token);
    });
    thread.join();
    Check(worker != EDITOR_OK, "worker began native gesture");
    available = false;
    app.NativeAvailable(false);
    Check(liveBegin("layout.edit-gesture", requestFields) == EDITOR_REFUSED, "busy context began gesture");
    available = true;
    app.NativeAvailable(true);
    const Snapshot closing(doc, selection);
    const auto closingMatrix = entity->GetWorldMatrix();
    Check(liveBegin("layout.edit-gesture", transform()) == EDITOR_OK && update(motion(180, 140)) == EDITOR_OK,
          "preview closing Layout scope");
    Check(app.FinishEdit(edit, true) == EDITOR_QUEUED, "queue closing Layout gesture");
    app.CloseScope(liveScope);
    app.EndFrame();
    Check(std::memcmp(&closingMatrix, &entity->GetWorldMatrix(), sizeof(closingMatrix)) == 0 && !app.HasEdits(),
          "closed scope retained queued Layout transform");
    closing.Unchanged("scope close changed Layout history");
    const EditorClient stopping(app.Host(), app.OpenScope());
    const auto stoppingValues = BorrowEditorFields(requestFields);
    Check(stopping.BeginCommand("layout.edit-gesture",
                                {stoppingValues.data(), stoppingValues.data() + stoppingValues.size()},
                                edit) == EDITOR_OK &&
              update(motion(180, 140)) == EDITOR_OK,
          "preview stopped Layout gesture");
    app.Stop();
    Check(std::memcmp(&closingMatrix, &entity->GetWorldMatrix(), sizeof(closingMatrix)) == 0 && !app.HasEdits(),
          "stopping modules retained native preview");
    closing.Unchanged("module stop changed saved document or selection");
}
