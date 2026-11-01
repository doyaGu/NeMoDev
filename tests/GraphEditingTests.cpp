#include "features/schematic/SchematicGraphCommands.h"
#include "features/schematic/SchematicPosition.h"
#include "workspace/EditorClient.h"
#include "core/document/Clipboard.h"
#include "core/document/ScriptSession.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/TopologyCommands.h"
#include "core/schematic/SchematicModel.h"
#include "core/schematic/SchematicEdits.h"
#include "core/runtime/CkRuntimeHost.h"
#include "editor/ImGuiDrawing.h"
#include <stdexcept>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
EditorField Number(const char *key, uint64_t n) { return {key, EDITOR_INTEGER_VALUE, {}, {}, n}; }
EditorField At(double x, double y) {
    EditorField f{"at", EDITOR_REAL_LIST};
    f.reals = {x, y};
    return f;
}
struct Snapshot {
    Document &doc;
    size_t undo;
    bool redo, dirty;
    uint64_t revision, topology;
    explicit Snapshot(Document &doc)
        : doc(doc), undo(doc.History().UndoCount()), redo(doc.History().CanRedo()), dirty(doc.Dirty()),
          revision(doc.ChangeRevision()), topology(doc.TopologyRevision()) {}
    void Unchanged(const char *message) const {
        Check(doc.History().UndoCount() == undo && doc.History().CanRedo() == redo && doc.Dirty() == dirty &&
                  doc.ChangeRevision() == revision && doc.TopologyRevision() == topology,
              message);
    }
};
} // namespace
void TestGraphEditing(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    Document doc(runtime);
    std::string error;
    Check(doc.New(&error), error.c_str());
    const auto script = CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(), "Graph commands"),
               other = CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(), "Destination");
    const auto a = CreateBehaviorGraph(doc, script.created, script.created, "A", Point(240, 100)),
               b = CreateBehaviorGraph(doc, script.created, script.created, "B", Point(560, 100));
    Check(script.ok && other.ok && a.ok && b.ok, "create graph fixture");
    Clipboard clipboard;
    EditorApplication app([&] { return &doc; });
    SchematicGraphCommands graphs([&] { return &doc; }, [] { return true; }, clipboard);
    Check(graphs.Register(app, error) && app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    const auto sync = [&] { app.Attach(doc.Token(), doc.TopologyRevision(), true, true); };
    sync();
    const EditorClient client(app.Host(), 0);
    const auto saved = [&] {
        doc.History().Clear();
        doc.MarkSaved();
        doc.History().MarkSavePoint();
        sync();
    };
    const auto base = [&](CK_ID id) {
        return std::vector<EditorField>{{"target", EDITOR_OBJECT, doc.Reference(id)},
                                        Number("revision", doc.Session(id)->Revision()),
                                        Number("topology", doc.TopologyRevision())};
    };
    const auto request = [&](const char *id, const std::vector<EditorField> &f, bool query = false) {
        const auto v = BorrowEditorFields(f);
        return query ? client.Query(id, {v.data(), v.data() + v.size()})
                     : client.Command(id, {v.data(), v.data() + v.size()});
    };
    const auto address = [&](ElementRef ref) { return ElementAddress::Capture(doc, script.created, ref); };
    const auto pin = [&](CK_ID id, PinKind kind, int index) {
        return address({ElementKind::Pin, id, id, index, -1, kind});
    };
    const auto local = [&](CK_ID id) {
        SchematicModel model;
        model.Build(*doc.Session(script.created), doc.Context(), {}, true);
        for (const auto &p : model.params)
            if (p.parameter == id && p.ref.kind == ElementKind::LocalParam)
                return address(p.ref);
        return ElementAddress{};
    };
    const auto connection = [&](const ElementAddress &from, const ElementAddress &to) {
        auto f = base(script.created);
        f.push_back(SchematicPosition(from, "from"));
        f.push_back(SchematicPosition(to, "to"));
        f.push_back(Number("auto-loop", 1));
        return f;
    };
    const auto root = [&]() -> CKBehavior * { return CKBehavior::Cast(doc.Context()->GetObject(script.created)); };
    const auto graphA = [&]() -> CKBehavior * { return CKBehavior::Cast(doc.Context()->GetObject(a.created)); };
    const auto graphB = [&]() -> CKBehavior * { return CKBehavior::Cast(doc.Context()->GetObject(b.created)); };
    saved();
    auto start = address({ElementKind::Start, script.created, script.created});
    auto connect = connection(start, pin(a.created, PinKind::BehaviorIn, 0));
    EditorField points{"points", EDITOR_REAL_LIST};
    points.reals = {180, 60, 220, 60};
    connect.push_back(points);
    const Snapshot beforeRead(doc);
    Check(request("schematic.connection", connect, true).status == EDITOR_OK, "read connection options");
    beforeRead.Unchanged("connection query changed document");
    Check(request("schematic.connect", connect).status == EDITOR_OK && doc.History().UndoCount() == 1,
          "single connection was not one undo step");
    Check(doc.Session(script.created)->Data().rootBehavior.links.size() == 1 &&
              doc.Session(script.created)->Data().rootBehavior.links[0].points.size() == 2,
          "link lost explicit control points");
    Check(doc.History().Undo() && !doc.Dirty() && doc.Session(script.created)->Data().rootBehavior.links.empty(),
          "link undo lost save point");
    Check(doc.History().Redo(), "redo link");
    saved();
    auto routed = base(script.created);
    EditorField routedLinks{"elements", EDITOR_RECORD};
    routedLinks.fields = {
        SchematicPosition(address({ElementKind::Link, script.created,
                                   doc.Session(script.created)->Data().rootBehavior.links.front().id, 0}),
                          "0")};
    routed.push_back(routedLinks);
    Check(request("schematic.route-links", routed).status == EDITOR_OK && doc.History().UndoCount() == 1 &&
              doc.Session(script.created)->Data().rootBehavior.links.front().points.empty(),
          "routing existing points was not one edit");
    Check(doc.History().Undo() && !doc.Dirty() &&
              doc.Session(script.created)->Data().rootBehavior.links.front().points.size() == 2,
          "routing undo lost original points or save point");
    sync();
    const Snapshot staleRoute(doc);
    Check(request("schematic.route-links", routed).status == EDITOR_STALE, "old route positions survived Undo");
    staleRoute.Unchanged("stale route changed document");
    auto insertion = base(script.created);
    insertion.push_back(
        SchematicPosition(address({ElementKind::Link, script.created,
                                   doc.Session(script.created)->Data().rootBehavior.links.front().id, 0}),
                          "link"));
    insertion.push_back(At(211, 121));
    insertion.push_back(Number("index", 1));
    Check(request("schematic.insert-control-point", insertion).status == EDITOR_OK && doc.History().UndoCount() == 1,
          "control point insertion was not one edit");
    const auto &newPoints = doc.Session(script.created)->Data().rootBehavior.links.front().points;
    Check(newPoints.size() == 3 && newPoints[1].h == 220 && newPoints[1].v == 120,
          "control point insertion lost ordering or grid snapping");
    Check(doc.History().Undo() && !doc.Dirty() &&
              doc.Session(script.created)->Data().rootBehavior.links.front().points.size() == 2,
          "control point undo lost original link");
    sync();
    auto invalidPoint = base(script.created);
    invalidPoint.push_back(
        SchematicPosition(address({ElementKind::Link, script.created,
                                   doc.Session(script.created)->Data().rootBehavior.links.front().id, 0}),
                          "link"));
    invalidPoint.push_back(At(200, 120));
    invalidPoint.push_back(Number("index", 99));
    const Snapshot invalidInsertion(doc);
    Check(request("schematic.insert-control-point", invalidPoint).status == EDITOR_INVALID,
          "invalid control point index was accepted");
    invalidInsertion.Unchanged("invalid control point changed history");
    const auto oldLink = doc.Session(script.created)->Data().rootBehavior.links.front().id;
    auto relink = connection(address({ElementKind::Start, script.created, script.created}),
                             pin(b.created, PinKind::BehaviorIn, 0));
    relink.push_back(SchematicPosition(address({ElementKind::Link, script.created, oldLink, 0}), "link"));
    Check(request("schematic.connect", relink).status == EDITOR_OK && doc.History().UndoCount() == 1,
          "relink was not one undo step");
    Check(doc.Session(script.created)->Data().rootBehavior.links.front().end.id == b.created,
          "relink missed destination");
    Check(doc.History().Undo() && !doc.Dirty() &&
              doc.Session(script.created)->Data().rootBehavior.links.front().id == oldLink,
          "relink undo lost original link");
    sync();
    const Snapshot staleLink(doc);
    Check(request("schematic.connect", relink).status == EDITOR_STALE, "old relink request survived Undo");
    staleLink.Unchanged("stale relink changed document");
    Check(AddGraphPin(doc, script.created, a.created, PinKind::ParamIn, "Input 1", CKPGUID_INT).ok &&
              AddGraphPin(doc, script.created, a.created, PinKind::ParamIn, "Input 2", CKPGUID_INT).ok,
          "add parameter pins");
    const auto first = AddLocalParameter(doc, script.created, script.created, "First", CKPGUID_INT, Point(200, 300)),
               second = AddLocalParameter(doc, script.created, script.created, "Second", CKPGUID_INT, Point(280, 300)),
               third = AddLocalParameter(doc, script.created, script.created, "Third", CKPGUID_INT, Point(360, 300));
    Check(first.ok && second.ok && third.ok, "create local fixture");
    auto initial = connection(local(first.created), pin(a.created, PinKind::ParamIn, 0));
    Check(request("schematic.connect", initial).status == EDITOR_OK, "create original parameter link");
    saved();
    auto replacement = connection(local(second.created), pin(a.created, PinKind::ParamIn, 0));
    const auto replaceState = request("schematic.connection", replacement, true);
    Check(replaceState.status == EDITOR_OK && EditorClient::Integer(replaceState, "replace"),
          "existing input source was not projected");
    const Snapshot original(doc);
    Check(request("schematic.connect", replacement).status == EDITOR_REFUSED, "source replaced without confirmation");
    original.Unchanged("refused source replacement changed document");
    Check(graphA()->GetInputParameter(0)->GetDirectSource() == doc.Context()->GetObject(first.created),
          "refused replacement lost original source");
    replacement.push_back(Number("replace", 1));
    Check(request("schematic.connect", replacement).status == EDITOR_OK && doc.History().UndoCount() == 1,
          "replace source was not one undo");
    Check(graphA()->GetInputParameter(0)->GetDirectSource() == doc.Context()->GetObject(second.created) &&
              doc.IsDiscarded(first.created),
          "replace source retained unique old local");
    Check(doc.History().Undo() && !doc.Dirty() &&
              graphA()->GetInputParameter(0)->GetDirectSource() == doc.Context()->GetObject(first.created),
          "source replacement undo lost old local");
    sync();
    auto batch = connection(local(second.created), pin(a.created, PinKind::ParamIn, 0));
    EditorField selected{"selected", EDITOR_RECORD};
    selected.fields = {SchematicPosition(local(second.created), "0"), SchematicPosition(local(third.created), "1")};
    batch.push_back(selected);
    auto stale = batch;
    for (auto &f : stale.back().fields.back().fields)
        if (f.key == "revision")
            --f.integer;
    const Snapshot beforeBatch(doc);
    Check(request("schematic.connect", stale).status == EDITOR_STALE, "partly stale batch was accepted");
    beforeBatch.Unchanged("stale batch created a partial link");
    Check(request("schematic.connect", batch).status == EDITOR_OK && doc.History().UndoCount() == beforeBatch.undo + 1,
          "multi link was not one undo");
    Check(graphA()->GetInputParameter(0)->GetDirectSource() == doc.Context()->GetObject(second.created) &&
              graphA()->GetInputParameter(1)->GetDirectSource() == doc.Context()->GetObject(third.created),
          "multi link lost pin ordering");
    Check(doc.History().Undo(), "undo multi link");
    sync();
    auto automatic = connection(local(second.created), address({ElementKind::Behavior, script.created, b.created}));
    selected.fields = {SchematicPosition(local(second.created), "0"), SchematicPosition(local(third.created), "1")};
    automatic.push_back(selected);
    const auto inputCount = graphB()->GetInputParameterCount();
    const auto undoCount = doc.History().UndoCount();
    Check(request("schematic.connect", automatic).status == EDITOR_OK &&
              graphB()->GetInputParameterCount() == inputCount + 2 && doc.History().UndoCount() == undoCount + 1,
          "automatic pins were not created as one edit");
    Check(doc.History().Undo() && graphB()->GetInputParameterCount() == inputCount,
          "automatic pins undo left new inputs");
    sync();
    const auto inner = CreateBehaviorGraph(doc, script.created, a.created, "Inner", Point(280, 160));
    Check(inner.ok &&
              AddGraphPin(doc, script.created, inner.created, PinKind::ParamIn, "Exported input", CKPGUID_INT).ok,
          "create export fixture");
    auto exporting = base(script.created);
    EditorField pins{"elements", EDITOR_RECORD};
    pins.fields = {SchematicPosition(pin(inner.created, PinKind::ParamIn, 0), "0")};
    exporting.push_back(pins);
    exporting.push_back(At(900, 400));
    const auto parentInputs = graphA()->GetInputParameterCount();
    const auto exportSteps = doc.History().UndoCount();
    Check(request("schematic.export-pins", exporting).status == EDITOR_OK &&
              graphA()->GetInputParameterCount() == parentInputs + 1 && doc.History().UndoCount() == exportSteps + 1,
          "pin export did not use one native edit");
    Check(doc.History().Undo() && graphA()->GetInputParameterCount() == parentInputs,
          "pin export undo lost original inputs");
    sync();
    const bool folded = FindRecord(doc.Session(script.created)->Data(), a.created)->folded;
    if (!folded) {
        auto fold = base(script.created);
        fold.push_back({"object", EDITOR_OBJECT, doc.Reference(a.created)});
        Check(request("schematic.toggle-fold", fold).status == EDITOR_OK, "fold copied element's parent");
    }
    SchematicModel foldedModel;
    foldedModel.Build(*doc.Session(script.created), doc.Context(), {}, true);
    Check(!foldedModel.FindBehavior(inner.created), "copy fixture is still visible");
    auto hiddenCopy = base(script.created);
    EditorField hiddenElements{"elements", EDITOR_RECORD};
    hiddenElements.fields = {SchematicPosition(address({ElementKind::Behavior, a.created, inner.created}), "0")};
    hiddenCopy.push_back(hiddenElements);
    const Snapshot hiddenBefore(doc);
    Check(request("schematic.copy", hiddenCopy).status == EDITOR_OK &&
              clipboard.behaviors == std::vector<CK_ID>{inner.created},
          "copy required a visible element");
    hiddenBefore.Unchanged("copying a hidden element changed document or history");
    if (!folded) {
        Check(doc.History().Undo(), "restore copied element's parent");
        sync();
    }
    auto copying = base(script.created);
    EditorField elements{"elements", EDITOR_RECORD};
    elements.fields = {SchematicPosition(address({ElementKind::Behavior, script.created, a.created}), "0")};
    copying.push_back(elements);
    const Snapshot beforeCopy(doc);
    Check(request("schematic.copy", copying).status == EDITOR_OK && clipboard.behaviors.size() == 1,
          "copy command lost graph");
    beforeCopy.Unchanged("copy command changed document or history");
    auto paste = base(other.created);
    paste.push_back(At(300, 80));
    paste.push_back(Number("clipboard-revision", clipboard.Revision()));
    const auto otherCount = CKBehavior::Cast(doc.Context()->GetObject(other.created))->GetSubBehaviorCount();
    const auto pasted = request("schematic.paste", paste);
    Check(pasted.status == EDITOR_OK && EditorClient::Find(pasted, "created")->object &&
              CKBehavior::Cast(doc.Context()->GetObject(other.created))->GetSubBehaviorCount() == otherCount + 1,
          "paste command lost graph copy");
    Check(doc.History().Undo() &&
              CKBehavior::Cast(doc.Context()->GetObject(other.created))->GetSubBehaviorCount() == otherCount,
          "paste undo retained graph");
    sync();
    auto oldPaste = base(other.created);
    oldPaste.push_back(At(300, 80));
    oldPaste.push_back(Number("clipboard-revision", clipboard.Revision()));
    clipboard.SetParameters(doc, {second.created});
    const Snapshot changedClipboard(doc);
    Check(request("schematic.paste", oldPaste).status == EDITOR_STALE, "old clipboard selection was pasted");
    changedClipboard.Unchanged("stale clipboard paste changed document");
    auto drag = base(script.created);
    elements.fields = {SchematicPosition(address({ElementKind::Behavior, script.created, a.created}), "0")};
    drag.push_back(elements);
    drag.push_back(At(500, 160));
    drag.push_back({"destination", EDITOR_OBJECT, doc.Reference(other.created)});
    drag.push_back(Number("destination-revision", doc.Session(other.created)->Revision()));
    const auto clipboardRevision = clipboard.Revision();
    const auto dragCount = doc.History().UndoCount();
    Check(request("schematic.copy-elements", drag).status == EDITOR_OK && clipboard.Revision() == clipboardRevision &&
              doc.History().UndoCount() == dragCount + 1,
          "drag copy changed clipboard or split history");
    Check(doc.History().Undo(), "undo drag copy");
    sync();
    auto group = base(script.created);
    EditorField objects{"objects", EDITOR_OBJECT_LIST};
    objects.objects = {doc.Reference(a.created), doc.Reference(b.created)};
    group.push_back(objects);
    const auto grouped = request("schematic.group", group);
    Check(grouped.status == EDITOR_OK && EditorClient::Find(grouped, "created")->object, "group command failed");
    Check(graphA()->GetParent()->GetID() == EditorClient::Find(grouped, "created")->object.object,
          "group did not own selected graph");
    Check(doc.History().Undo() && graphA()->GetParent() == root(), "group undo lost graph parent");
    sync();
    auto ungroup = base(script.created);
    objects.objects = {doc.Reference(a.created)};
    ungroup.push_back(objects);
    const auto ungroupCount = doc.History().UndoCount();
    Check(request("schematic.ungroup", ungroup).status == EDITOR_OK && doc.IsDiscarded(a.created) &&
              doc.History().UndoCount() == ungroupCount + 1,
          "ungroup command did not use one edit");
    Check(doc.History().Undo() && !doc.IsDiscarded(a.created) && graphA()->GetParent() == root(),
          "ungroup undo lost original graph");
    SchematicModel forRectangle;
    forRectangle.Build(*doc.Session(script.created), doc.Context(), {}, true);
    const auto *rectangleMember = forRectangle.FindBehavior(b.created);
    Check(rectangleMember, "find rectangle member");
    const Rect rectangle = rectangleMember->box;
    auto draw = base(script.created);
    EditorField rect{"rect", EDITOR_REAL_LIST};
    rect.reals = {rectangle.hPos - 1, rectangle.vPos - 1, rectangle.hSize + 2, rectangle.vSize + 2};
    draw.push_back(rect);
    const auto drawnSteps = doc.History().UndoCount();
    Check(request("schematic.draw-graph", draw).status == EDITOR_OK && doc.History().UndoCount() == drawnSteps + 1 &&
              graphB()->GetParent() != root(),
          "drawn graph did not group the enclosed behavior in one edit");
    Check(doc.History().Undo() && graphB()->GetParent() == root(), "drawn graph undo lost original parent");
    sync();
    auto refusedExport = base(script.created);
    pins.fields = {SchematicPosition(pin(a.created, PinKind::ParamIn, 0), "0")};
    refusedExport.push_back(pins);
    refusedExport.push_back(At(900, 400));
    const Snapshot refusedState(doc);
    app.EnterDraw();
    const auto queued = request("schematic.export-pins", refusedExport);
    app.LeaveDraw();
    Check(queued.status == EDITOR_QUEUED && queued.operation, "queue refused export");
    app.EndFrame();
    const auto refusedResult = client.Completion(queued.operation);
    Check(refusedResult.status == EDITOR_NO_CHANGE &&
              refusedResult.message.find("outside its behavior graph") != std::string::npos,
          "deferred export lost refusal feedback");
    refusedState.Unchanged("refused export changed saved state or undo history");
    app.Stop();
}
