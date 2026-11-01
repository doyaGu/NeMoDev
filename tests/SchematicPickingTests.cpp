#include "features/schematic/SchematicGraphCommands.h"
#include "features/schematic/SchematicModelData.h"
#include "features/schematic/SchematicPosition.h"
#include "workspace/EditorClient.h"
#include "core/document/Document.h"
#include "core/document/Clipboard.h"
#include "core/document/ScriptSession.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/TopologyCommands.h"
#include "core/schematic/SchematicEdits.h"
#include "core/schematic/SchematicModelBuilder.h"
#include "core/runtime/CkRuntimeHost.h"
#include "editor/ImGuiDrawing.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
const EditorField &Field(const EditorResponse &result, const char *key) {
    const auto *field = EditorClient::Find(result, key);
    Check(field, key);
    return *field;
}
struct Saved {
    Document &doc;
    EditorApplication &app;
    DocumentToken token;
    ObjectRef selection;
    uint64_t revision, topology;
    size_t undo;
    bool redo, dirty;
    Saved(Document &doc, EditorApplication &app)
        : doc(doc), app(app), token(doc.Token()), selection(app.Selection()), revision(doc.ChangeRevision()),
          topology(doc.TopologyRevision()), undo(doc.History().UndoCount()), redo(doc.History().CanRedo()),
          dirty(doc.Dirty()) {}
    void Unchanged() const {
        Check(doc.Token() == token && app.Selection() == selection && doc.ChangeRevision() == revision &&
                  doc.TopologyRevision() == topology && doc.History().UndoCount() == undo &&
                  doc.History().CanRedo() == redo && doc.Dirty() == dirty,
              "picking or presentation changed document, selection or history");
    }
};
EditorResponse Pick(const EditorClient &client, ObjectRef target, const ElementAddress &address = {}) {
    std::vector<EditorField> fields{{"target", EDITOR_OBJECT, target}};
    if (address)
        fields.push_back(SchematicPosition(address));
    const auto args = BorrowEditorFields(fields);
    return client.Query("schematic.pick-objects", {args.data(), args.data() + args.size()});
}
struct PickProbe {
    std::vector<EditorField> fields;
    ObjectRef script{};
    EditorStatus status = EDITOR_OK, mutation = EDITOR_OK;
    unsigned calls = 0;
    bool throwing = false;
    uint32_t objectClass = 0;
    static EditorStatus Read(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<PickProbe *>(data);
        ++self.calls;
        Check(request->count == (self.objectClass ? 3u : 2u) && request->values[0].type == EDITOR_REAL_VALUE &&
                  request->values[0].real == 24 && request->values[1].real == 48,
              "view pick lost screen coordinates");
        if (self.objectClass)
            Check(request->values[2].type == EDITOR_INTEGER_VALUE && request->values[2].integer == self.objectClass,
                  "view pick lost object class filter");
        const EditorRequest edit{request->scope, nullptr, 0};
        self.mutation = host->execute(host->data, EditorString EDITOR_TEXT("schematic.refresh"), &edit, {});
        // Read-only view callbacks may use the same registered document queries.
        const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, self.script};
        const EditorRequest nested{request->scope, &target, 1};
        const EditorSink discard{nullptr, [](void *, const EditorValue *, uint32_t) { return EDITOR_OK; }};
        Check(host->query(host->data, EditorString EDITOR_TEXT("schematic.script-header"), &nested, discard) ==
                  EDITOR_OK,
              "view picking could not read a projection");
        if (self.throwing)
            throw std::runtime_error("injected pick failure");
        const auto values = BorrowEditorFields(self.fields);
        const auto written = sink.write(sink.data, values.data(), uint32_t(values.size()));
        return written == EDITOR_OK ? self.status : written;
    }
};
void ViewPicking(EditorApplication &app, Document &doc, ObjectRef script) {
    PickProbe probe{{{"objects", EDITOR_OBJECT_LIST, {}, {}, 0, {script}}}, script};
    EditorViewDesc view{};
    view.size = sizeof(view);
    view.pick = PickProbe::Read;
    const auto scope = app.OpenScope();
    const Saved before(doc, app);
    const auto pick = [&] { return app.PickView(view, &probe, app.Scope(scope), 24, 48); };
    Check(pick().status == EDITOR_OK && probe.calls == 1 && probe.mutation == EDITOR_REFUSED,
          "instance picker bypassed read-only execution");
    probe.objectClass = CKCID_MATERIAL;
    Check(app.PickView(view, &probe, app.Scope(scope), 24, 48, probe.objectClass).status == EDITOR_OK,
          "class-aware view pick failed");
    probe.objectClass = 0;
    probe.throwing = true;
    Check(pick().status == EDITOR_ERROR, "pick exception escaped");
    probe.throwing = false;
    probe.status = EDITOR_ERROR;
    Check(pick().status == EDITOR_ERROR && pick().fields.empty(), "failed pick kept partial candidates");
    probe.status = EDITOR_OK;
    probe.fields.front().key = "unexpected";
    Check(pick().status == EDITOR_INVALID, "view picker accepted a wrong result schema");
    probe.fields.front().key = "objects";
    probe.fields.front().objects.clear();
    Check(pick().status == EDITOR_OK && pick().fields.front().objects.empty(), "empty pick was treated as a failure");
    probe.fields.front().objects = {script};
    ++probe.fields.front().objects.front().lifetime;
    Check(pick().status == EDITOR_STALE, "view picker accepted an old object lifetime");
    probe.fields.front().objects = {script};
    probe.fields.front().objects.front().document.value += 1;
    Check(pick().status == EDITOR_STALE, "view picker accepted another document");
    probe.fields.front().objects = {script};
    const auto calls = probe.calls;
    Check(app.PickView(view, &probe, app.Scope(scope), std::numeric_limits<float>::infinity(), 48).status ==
                  EDITOR_INVALID &&
              probe.calls == calls,
          "view picker accepted invalid coordinates");
    app.NativeAvailable(false);
    Check(pick().status == EDITOR_REFUSED && probe.calls == calls, "view picker ran during native work");
    app.NativeAvailable(true);
    EditorStatus threadStatus = EDITOR_OK;
    std::thread thread([&] { threadStatus = pick().status; });
    thread.join();
    Check(threadStatus == EDITOR_REFUSED && probe.calls == calls, "view picker ran on another thread");
    Check(pick().status == EDITOR_OK, "pick failure left callback guards active");
    app.CloseScope(scope);
    Check(pick().status == EDITOR_STALE, "closed view still picked");
    before.Unchanged();
}
} // namespace

void TestSchematicPicking(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    Document doc(runtime);
    std::string error;
    Check(doc.New(&error), error);
    auto *context = doc.Context();
    const auto script = CreateScript(doc, context->GetCurrentLevel()->GetID(), "Picking");
    const auto graph = CreateBehaviorGraph(doc, script.created, script.created, "Element", {240, 160});
    Check(script.ok && graph.ok, "pick graph fixture");
    auto *root = CKBehavior::Cast(context->GetObject(script.created));
    auto *native = CKBehavior::Cast(context->GetObject(graph.created));
    auto *held = context->CreateObject(CKCID_3DENTITY, "Held object");
    Check(held, "held object fixture");
    const auto heldId = held->GetID();
    const auto local =
        AddLocalParameter(doc, script.created, script.created, "Object value", CKPGUID_3DENTITY, {300, 280});
    Check(local.ok, "object local fixture");
    auto *value = CKParameter::Cast(context->GetObject(local.created));
    Check(value->SetValue(&heldId, sizeof(heldId)) == CK_OK, "object local value");
    auto *input = native->CreateInputParameter("Direct", CKPGUID_3DENTITY);
    auto *shared = native->CreateInputParameter("Shared", CKPGUID_3DENTITY);
    auto *parent = root->CreateInputParameter("Parent", CKPGUID_3DENTITY);
    auto *output = native->CreateOutputParameter("Result", CKPGUID_3DENTITY);
    Check(input && shared && parent && output && input->SetDirectSource(value) == CK_OK &&
              parent->SetDirectSource(value) == CK_OK && shared->ShareSourceWith(parent) == CK_OK &&
              output->SetValue(&heldId, sizeof(heldId)) == CK_OK,
          "object pin fixtures");
    native->SetAsTargetable(TRUE);
    Check(native->UseTarget(TRUE) == CK_OK && native->GetTargetParameter()->SetDirectSource(value) == CK_OK,
          "target fixture");
    Check(EditScriptData(doc, script.created, "Shortcut",
                         [&](InterfaceData &data) {
                             data.rootBehavior.sharedParams.push_back(data.rootBehavior.localParams.front());
                         }),
          "shortcut fixture");
    const auto operation = AddParameterOperation(doc, script.created, script.created,
                                                 context->GetParameterManager()->OperationNameToGuid("Addition"),
                                                 CKPGUID_INT, CKPGUID_INT, CKPGUID_INT, {400, 300});
    Check(operation.ok, "operation fixture");
    auto *op = CKParameterOperation::Cast(context->GetObject(operation.created));
    auto *number = root->CreateLocalParameter("Number", CKPGUID_INT);
    Check(number && op->GetInParameter1()->SetDirectSource(number) == CK_OK, "operation input fixture");
    const auto link =
        CreateBehaviorLink(doc, script.created, LinkEndpoint(root->GetInput(0)->GetID(), 0, ENDPOINT_START_BIN),
                           LinkEndpoint(graph.created, 0, ENDPOINT_BIN));
    Check(link.ok && link.created, "active link fixture");
    doc.MarkTopologyChanged();
    doc.History().Clear();
    Check(RenameObject(doc, script.created, graph.created, "Temporary pick name").ok && doc.History().Undo(),
          "pick Redo fixture");
    doc.MarkSaved();
    doc.History().MarkSavePoint();
    EditorApplication app([&] { return &doc; });
    Clipboard clipboard;
    SchematicGraphCommands commands([&] { return &doc; }, [] { return true; }, clipboard);
    Check(commands.Register(app, error), error);
    EditorViewDesc declared{};
    declared.size = sizeof(declared);
    declared.imguiVersion = EDITOR_IMGUI_VERSION;
    declared.id = EDITOR_TEXT("test.pick-view");
    declared.provider = EDITOR_TEXT("test.picking");
    declared.title = EDITOR_TEXT("Picking");
    declared.create = [](void *data, const EditorHost *, EditorView) { return data; };
    declared.destroy = [](void *) {};
    declared.draw = [](void *, const EditorHost *, EditorView, EditorViewFrame *) {};
    declared.pick = PickProbe::Read;
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("test.picking");
    module.views = &declared;
    module.viewCount = 1;
    declared.kind = EDITOR_OVERLAY_VIEW;
    Check(!app.Register(module, error), "overlay registered an object picker");
    declared.kind = EDITOR_WINDOW_VIEW;
    declared.size -= sizeof(void *);
    Check(!app.Register(module, error), "old view layout was accepted");
    declared.size = sizeof(declared);
    module.version = 17;
    Check(!app.Register(module, error), "old module version was accepted");
    module.version = EDITOR_VERSION;
    Check(app.Register(module, error), error);
    module.id = EDITOR_TEXT("test.pick-replacement");
    declared.provider = EDITOR_TEXT("test.other-picking");
    declared.pick = nullptr;
    Check(!app.Register(module, error), "replacement view silently dropped picking support");
    Check(app.Activate(EDITOR_IMGUI_VERSION, error), error);
    app.Attach(doc.Token(), doc.ChangeRevision(), true, true);
    const auto view = app.OpenScope();
    const EditorClient client(app.Host(), view);
    const auto target = doc.Reference(script.created), owner = doc.Reference(context->GetCurrentLevel()->GetID()),
               element = doc.Reference(graph.created), object = doc.Reference(heldId);
    app.SetSelection(element);
    const Saved before(doc, app);
    Check(doc.History().CanRedo(), "picking must preserve an existing Redo entry");
    const auto expect = [&](ElementRef ref, std::vector<ObjectRef> expected) {
        const auto address = ElementAddress::Capture(doc, script.created, ref);
        Check(bool(address), "pick position fixture");
        const auto result = Pick(client, target, address);
        Check(result.status == EDITOR_OK && Field(result, "objects").objects == expected,
              "pick order differs from parameter, element, Script, owner");
        before.Unchanged();
    };
    Check(Field(Pick(client, target), "objects").objects == std::vector<ObjectRef>{target, owner},
          "background pick order");
    expect({ElementKind::Behavior, script.created, graph.created}, {element, target, owner});
    expect({ElementKind::LocalParam, script.created, local.created, 0},
           {object, doc.Reference(local.created), target, owner});
    expect({ElementKind::Shortcut, script.created, local.created, 0},
           {object, doc.Reference(local.created), target, owner});
    for (int index : {0, 1})
        expect({ElementKind::Pin, graph.created, graph.created, index, -1, PinKind::ParamIn},
               {object, element, target, owner});
    expect({ElementKind::Pin, graph.created, graph.created, 0, -1, PinKind::ParamOut},
           {object, element, target, owner});
    expect({ElementKind::Pin, graph.created, graph.created, -2, -1, PinKind::Target}, {object, element, target, owner});
    expect({ElementKind::Pin, operation.created, operation.created, 0, -1, PinKind::ParamIn},
           {doc.Reference(operation.created), target, owner});
    expect({ElementKind::Pin, operation.created, operation.created, 0, -1, PinKind::ParamOut},
           {doc.Reference(operation.created), target, owner});
    const auto address = ElementAddress::Capture(
        doc, script.created, {ElementKind::Pin, graph.created, graph.created, 0, -1, PinKind::ParamIn});
    ElementAddress withFocus;
    Check(ReadSchematicPosition(SchematicPosition(address, "address", element), withFocus) &&
              withFocus.script == address.script && withFocus.object == address.object &&
              withFocus.endpoint == address.endpoint && withFocus.element == address.element,
          "presentation position could not decode its optional navigation focus");
    auto wrongScript = address;
    wrongScript.script = element;
    Check(Pick(client, target, wrongScript).status == EDITOR_STALE, "picker accepted another Script's position");
    auto oldPosition = address;
    ++oldPosition.revision;
    Check(Pick(client, target, oldPosition).status == EDITOR_STALE, "picker accepted a stale slot");
    auto wrongKind = address;
    wrongKind.element.kind = ElementKind(99);
    Check(Pick(client, target, wrongKind).status == EDITOR_INVALID, "picker accepted malformed position data");
    before.Unchanged();
    ViewPicking(app, doc, target);

    const auto modelPage =
        client.Query("schematic.model", {ObjectArgument("target", target), IntegerArgument("open", 1)});
    SchematicModel model;
    Check(modelPage.status == EDITOR_OK && ReadSchematicModel(Field(modelPage, "model"), model) &&
              model.script.ownerKind == SchematicOwnerKind::Level,
          "model did not copy its owner's category");
    auto malformedModel = Field(modelPage, "model");
    for (auto &section : malformedModel.fields)
        if (section.key == "script")
            for (auto &field : section.fields)
                if (field.key == "ownerKind")
                    field.integer = 99;
    Check(!ReadSchematicModel(malformedModel, model) && model.script.ownerKind == SchematicOwnerKind::Level,
          "malformed owner category replaced the copied model");

    native->Activate(TRUE, FALSE);
    root->Activate(TRUE, FALSE);
    root->GetInput(0)->Activate(TRUE);
    const auto activity = client.Query("schematic.activity", {ObjectArgument("target", target)});
    Check(activity.status == EDITOR_OK && Field(activity, "running").objects == std::vector<ObjectRef>{element},
          "runtime activity lost full behavior references");
    Check(Field(activity, "firing").objects == std::vector<ObjectRef>{doc.Reference(link.created)},
          "runtime activity lost the firing link reference");
    ElementAddress source;
    source.script = target;
    source.revision = doc.Session(script.created)->Revision();
    source.topology = doc.TopologyRevision();
    const auto position =
        ElementAddress::Capture(doc, script.created, {ElementKind::Behavior, script.created, graph.created});
    const auto &links = doc.Session(script.created)->Data().rootBehavior.links;
    const auto linkRecord =
        std::find_if(links.begin(), links.end(), [&](const auto &entry) { return entry.id == link.created; });
    Check(linkRecord != links.end(), "firing link display fixture");
    const auto linkPosition = ElementAddress::Capture(
        doc, script.created, {ElementKind::Link, script.created, link.created, int(linkRecord - links.begin())});
    std::map<ElementRef, ElementAddress> positions{{position.element, position}, {linkPosition.element, linkPosition}};
    EditorResponse execution{EDITOR_OK};
    execution.fields = {{"state", EDITOR_INTEGER_VALUE, {}, {}, EDITOR_EXECUTION_PAUSED},
                        {"trace", EDITOR_INTEGER_VALUE, {}, {}, 1},
                        {"behavior", EDITOR_OBJECT, element}};
    const auto marks = ReadSchematicActivity(execution, activity, source, positions);
    Check(marks.trace && marks.active, "runtime presentation lost Script trace state");
    Check(marks.current == position.element && marks.running.count(position.element),
          "runtime presentation lost behavior marks");
    Check(marks.firing.count(linkPosition.element), "runtime presentation lost a firing link");
    auto retained = positions;
    ++retained[position.element].object.lifetime;
    const auto reused = ReadSchematicActivity(execution, activity, source, retained);
    Check(!reused.current.IsValid() && reused.running.empty(), "runtime marks matched only an object number");
    retained = positions;
    ++retained[position.element].object.document.value;
    Check(ReadSchematicActivity(execution, activity, source, retained).running.empty(),
          "runtime marks crossed documents");
    ++source.revision;
    Check(ReadSchematicActivity(execution, activity, source, positions).running.empty(),
          "new activity marked an older model");
    --source.revision;
    auto failed = activity;
    failed.status = EDITOR_ERROR;
    Check(!ReadSchematicActivity(execution, failed, source, positions).trace, "failed activity retained trace marks");
    execution.fields.front().integer = EDITOR_EXECUTION_STOPPED;
    Check(ReadSchematicActivity(execution, activity, source, positions).running.empty(),
          "stopped runtime retained marks");
    execution.fields.front().integer = EDITOR_EXECUTION_PAUSED;
    execution.fields[1].integer = 0;
    const auto noTrace = ReadSchematicActivity(execution, activity, source, positions);
    Check(!noTrace.trace && noTrace.firing.empty() && noTrace.running.count(position.element),
          "trace-off changed debugger marks");
    before.Unchanged();

    // Replacing a pin at the same slot cannot retarget a previously drawn position.
    const auto oldPin = doc.Reference(input->GetID());
    Check(native->RemoveInputParameter(0) == input, "remove pin fixture");
    Check(context->DestroyObject(oldPin.object, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK, "destroy old pin");
    auto *replacement = native->CreateInputParameter("Replacement", CKPGUID_3DENTITY);
    Check(replacement && replacement->GetID() == oldPin.object && doc.Reference(replacement->GetID()) != oldPin,
          "reused pin fixture");
    Check(Pick(client, target, address).status == EDITOR_STALE, "picker rebound a drawn pin after native ID reuse");
    before.Unchanged();
    app.NativeAvailable(false);
    Check(Pick(client, target).status == EDITOR_REFUSED, "picker ran while the native runtime was busy");
    app.NativeAvailable(true);
    app.CloseScope(view);
    Check(Pick(client, target).status == EDITOR_STALE, "picker used a closed view");
}
