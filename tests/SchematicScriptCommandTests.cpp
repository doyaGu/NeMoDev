#include "features/schematic/SchematicGraphCommands.h"
#include "features/schematic/SchematicScriptData.h"
#include "workspace/EditorClient.h"
#include "core/schematic/SchematicEdits.h"
#include "features/schematic/SchematicPosition.h"
#include "core/document/Document.h"
#include "core/document/Clipboard.h"
#include "core/document/ScriptSession.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/TopologyCommands.h"
#include "core/graph/LinkCommands.h"
#include "core/graph/Debugger.h"
#include "core/graph/RuntimeGraph.h"
#include "application/DocumentCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/ImageFiles.h"
#include "editor/ImGuiDrawing.h"
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace {
using namespace vdev;
void Check(bool ok, const std::string &text) {
    if (!ok)
        throw std::runtime_error(text);
}
const EditorField &Field(const EditorResponse &r, const char *key) {
    const auto *f = EditorClient::Find(r, key);
    Check(f, key);
    return *f;
}
EditorField Number(const char *key, uint64_t n) { return {key, EDITOR_INTEGER_VALUE, {}, {}, n}; }
EditorResponse Call(const EditorClient &client, const char *id, const std::vector<EditorField> &fields,
                    bool query = false) {
    const auto values = BorrowEditorFields(fields);
    return query ? client.Query(id, {values.data(), values.data() + values.size()})
                 : client.Command(id, {values.data(), values.data() + values.size()});
}
struct Saved {
    Document &doc;
    EditorApplication &app;
    size_t undo;
    bool redo, dirty;
    uint64_t revision, topology;
    ObjectRef selection;
    DocumentToken token;
    Saved(Document &d, EditorApplication &a)
        : doc(d), app(a), undo(d.History().UndoCount()), redo(d.History().CanRedo()), dirty(d.Dirty()),
          revision(d.ChangeRevision()), topology(d.TopologyRevision()), selection(a.Selection()), token(d.Token()) {}
    void Unchanged() const {
        Check(doc.Token() == token && app.Selection() == selection && undo == doc.History().UndoCount() &&
                  redo == doc.History().CanRedo() && dirty == doc.Dirty() && revision == doc.ChangeRevision() &&
                  topology == doc.TopologyRevision(),
              "refused Script command changed document, selection or history");
    }
};
struct Views : DocumentFiles::Observer, DocumentCommands::Presentation {
    void ReplacingDocument(bool) override { throw std::runtime_error("Snapshot must preserve the document"); }
    void ReplacementCancelled() override {}
    void DocumentReplaced() override {}
    void Log(LogLevel, const std::string &) override {}
    LayoutInterfaceData CaptureLayout() const override { return {}; }
    void FileFinished(const DocumentCommands::Result &) override {}
    void HistoryChanged() override {}
    void Quit() override {}
};
int ThrowingText(CKParameter *, char *, CKBOOL) { throw std::runtime_error("Injected breakpoint conversion failure"); }
} // namespace
void TestSchematicScriptCommands(vdev::CkRuntimeHost &host) {
    using namespace vdev;
    Document doc(host);
    std::string error;
    Check(doc.New(&error), error);
    auto *context = doc.Context();
    const auto script = CreateScript(doc, context->GetCurrentLevel()->GetID(), "Script tools");
    Check(script.ok, "Script fixture");
    const auto graph = CreateBehaviorGraph(doc, script.created, script.created, "Breakpoint graph", {200, 120});
    Check(graph.ok, "graph fixture");
    auto *native = CKBehavior::Cast(context->GetObject(graph.created));
    native->CreateInput("In");
    auto *input = native->CreateInputParameter("Input", CKPGUID_INT);
    Check(input, "input fixture");
    const auto local = AddLocalParameter(doc, script.created, graph.created, "Same name", CKPGUID_INT, {240, 200});
    Check(local.ok, "local fixture");
    const auto another = AddLocalParameter(doc, script.created, graph.created, "Same name", CKPGUID_INT, {300, 200});
    Check(another.ok, "reference fixture");
    Check(LinkPairs(doc, script.created,
                    {{LinkEndpoint(CKBehavior::Cast(context->GetObject(script.created))->GetInput(0)->GetID(), 0,
                                   ENDPOINT_START_BIN),
                      LinkEndpoint(graph.created, 0, ENDPOINT_BIN)}},
                    true) == 1,
          "link fixture");
    Clipboard clipboard;
    SchematicGraphCommands scripts([&] { return &doc; }, [] { return true; }, clipboard);
    bool failBefore = false, failAfter = false;
    RuntimeOperation runtime({}, [&](const std::string &, const std::function<void()> &work) {
        if (failBefore)
            throw std::runtime_error("before snapshot");
        work();
        if (failAfter)
            throw std::runtime_error("after snapshot");
    });
    DocumentCommands *coordinator = nullptr;
    EditorApplication app([&] { return &doc; },
                          [&] { return !runtime.Busy() && (!coordinator || !coordinator->Busy()); });
    Views views;
    DocumentFiles files([&] { return &doc; }, runtime, views);
    DocumentCommands commands(app, files, runtime, [&] { return &doc; }, views);
    coordinator = &commands;
    Check(scripts.Register(app, error) && commands.Register(error) && app.Activate(EDITOR_IMGUI_VERSION, error), error);
    Check(std::any_of(app.Menus().begin(), app.Menus().end(),
                      [](const MenuEntry &entry) {
                          return entry.action == "history.undo" && entry.location == "file.history";
                      }),
          "snapshot file command hid history menu contributions");
    Check(std::none_of(app.Menus().begin(), app.Menus().end(),
                       [](const MenuEntry &entry) { return entry.action == "schematic.load-snapshot"; }),
          "context snapshot command leaked into the main menu");

    app.Attach(doc.Token(), doc.TopologyRevision(), true, true);
    const auto view = app.OpenScope();
    const EditorClient client(app.Host(), view), global(app.Host(), 0);
    app.SetSelection(doc.Reference(local.created));
    doc.History().Clear();
    doc.MarkSaved();
    doc.History().MarkSavePoint();
    const auto target = doc.Reference(script.created);
    const auto position = [&](ElementRef ref) {
        return SchematicPosition(ElementAddress::Capture(doc, script.created, ref), "position");
    };
    const auto behavior = [&] { return position({ElementKind::Behavior, script.created, graph.created}); };
    const auto selection = [&] {
        const auto result = Call(client, "schematic.breakpoint-target", {behavior()}, true);
        Check(result.status == EDITOR_OK, "breakpoint projection failed");
        SchematicBreakpoint b;
        Check(ReadBreakpoint(Field(result, "breakpoint"), b), "breakpoint target decode");
        return b;
    };
    Saved reads(doc, app);
    const auto header = client.Query("schematic.script-header", {ObjectArgument("target", target)});
    Check(header.status == EDITOR_OK &&
              Field(header, "owner").object == doc.Reference(context->GetCurrentLevel()->GetID()),
          "header owner projection failed");
    auto b = selection();
    Check(b.object == doc.Reference(graph.created), "wrong breakpoint object");
    reads.Unchanged();
    Check(Call(client, "schematic.toggle-breakpoint", {behavior()}).status == EDITOR_OK &&
              doc.History().UndoCount() == 1,
          "toggle was not one Undo step");
    b = selection();
    const auto detail = Call(client, "schematic.breakpoint-detail", {BreakpointField(b)}, true);
    Check(detail.status == EDITOR_OK && Field(detail, "parameters").fields.size() == 3,
          "breakpoint candidates missing");
    EditorField condition{"0", EDITOR_RECORD};
    condition.fields = {{"parameter", EDITOR_OBJECT, doc.Reference(local.created)},
                        {"reference", EDITOR_OBJECT, doc.Reference(another.created)},
                        Number("comparison", 2),
                        Number("logical", 0),
                        {"value", EDITOR_TEXT_VALUE, {}, "Same name"}};
    EditorField conditions{"conditions", EDITOR_RECORD};
    conditions.fields = {condition};
    Check(Call(client, "schematic.configure-breakpoint", {BreakpointField(b), conditions}).status == EDITOR_OK &&
              doc.History().UndoCount() == 2,
          "configure was not one Undo step");
    const auto *record = FindBreakpoint(doc.Session(script.created)->Data(), EXTRA_DATA_BEHAVIOR, graph.created);
    Check(record && record->subData.size() == 1 && record->subData[0].id2 == another.created,
          "reference parameter was matched by name");
    Check(doc.History().Undo(), "Undo conditions");
    b = selection();
    // A failed second row must not clear or partially replace any conditions.
    auto bad = condition;
    bad.key = "1";
    bad.fields[2].integer = 99;
    bad.fields.back().text = "not an integer";
    conditions.fields = {condition, bad};
    Saved invalid(doc, app);
    Check(Call(client, "schematic.configure-breakpoint", {BreakpointField(b), conditions}).status == EDITOR_INVALID,
          "invalid condition accepted");
    invalid.Unchanged();
    auto malformed = condition;
    malformed.fields[0].type = EDITOR_INTEGER_VALUE;
    conditions.fields = {malformed};
    Check(Call(client, "schematic.configure-breakpoint", {BreakpointField(b), conditions}).status == EDITOR_INVALID,
          "wrong condition type accepted");
    invalid.Unchanged();
    conditions.fields = {condition};
    Check(Call(client, "schematic.configure-breakpoint", {BreakpointField(b), conditions}).status == EDITOR_OK,
          "valid condition refused after failure");
    Check(doc.History().Undo(), "restore condition fixture");
    Saved stale(doc, app);
    Check(Call(client, "schematic.configure-breakpoint", {BreakpointField(b), conditions}).status == EDITOR_STALE,
          "configuration from before Undo accepted");
    stale.Unchanged();
    // Pin and link targets follow the same position projection as behaviors.
    const auto pin = position({ElementKind::Pin, graph.created, graph.created, 0, -1, PinKind::ParamIn});
    Check(Call(client, "schematic.toggle-breakpoint", {pin}).status == EDITOR_OK, "pin breakpoint command failed");
    const auto *layout = FindRecord(doc.Session(script.created)->Data(), script.created);
    Check(layout && !layout->links.empty(), "link record missing");
    const auto &link = layout->links.front();
    const auto linkPosition = position({ElementKind::Link, script.created, link.id, 0});
    Check(Call(client, "schematic.toggle-breakpoint", {linkPosition}).status == EDITOR_OK,
          "link breakpoint command failed");
    const auto list = client.Query("schematic.breakpoints");
    Check(list.status == EDITOR_OK && Field(list, "breakpoints").fields.size() == 3, "breakpoint list lost entries");
    auto removed = Field(list, "breakpoints");
    const auto steps = doc.History().UndoCount();
    Check(Call(client, "schematic.remove-breakpoints", {removed}).status == EDITOR_OK &&
              doc.History().UndoCount() == steps + 1,
          "batch breakpoint removal used multiple Undo steps");
    Check(doc.History().Undo(), "restore removed breakpoints");
    Saved oldList(doc, app);
    Check(Call(client, "schematic.remove-breakpoints", {removed}).status == EDITOR_STALE,
          "old list removed breakpoints after Undo");
    oldList.Unchanged();
    // Older files can contain a breakpoint whose native object has disappeared.
    Check(EditScriptData(doc, script.created, "Orphan breakpoint",
                         [](InterfaceData &d) {
                             ExtraData b;
                             b.type = EXTRA_DATA_BEHAVIOR;
                             b.id1 = 0xfffffffe;
                             ExtraSubData c;
                             c.id1 = 0xfffffffd;
                             c.buffer.resize(sizeof(int));
                             b.subData.push_back(c);
                             d.extraData.push_back(b);
                         }),
          "orphan fixture");
    const auto orphans = client.Query("schematic.breakpoints");
    EditorField orphan{"breakpoints", EDITOR_RECORD};
    for (const auto &row : Field(orphans, "breakpoints").fields) {
        SchematicBreakpoint b;
        if (ReadBreakpoint(row, b) && !b.object) {
            auto value = row;
            value.key = "0";
            orphan.fields.push_back(std::move(value));
        }
    }
    Check(orphan.fields.size() == 1, "missing-object breakpoint was hidden");
    auto orphanTarget = orphan.fields.front();
    orphanTarget.key = "breakpoint";
    const auto orphanDetail = Call(client, "schematic.breakpoint-detail", {orphanTarget}, true);
    Check(orphanDetail.status == EDITOR_OK && Field(orphanDetail, "conditions").fields.size() == 1,
          "missing-parameter condition was hidden");
    Saved orphanUnchanged(doc, app);
    Check(Call(client, "schematic.configure-breakpoint", {orphanTarget, Field(orphanDetail, "conditions")}).status ==
              EDITOR_STALE,
          "missing-parameter condition was silently dropped");
    orphanUnchanged.Unchanged();

    Check(Call(client, "schematic.remove-breakpoints", {orphan}).status == EDITOR_OK &&
              Field(client.Query("schematic.breakpoints"), "breakpoints").fields.size() == 3,
          "missing-object breakpoint could not be removed");
    Check(doc.History().Undo() && doc.History().Undo(), "restore orphan fixture");
    const auto pinIdentity = ElementAddress::Capture(
        doc, script.created, {ElementKind::Pin, graph.created, graph.created, 0, -1, PinKind::ParamIn});
    auto *old = native->RemoveInputParameter(0);
    context->DestroyObject(old, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID);
    Check(native->CreateInputParameter("Replacement", CKPGUID_INT), "replacement input fixture");
    Saved oldPin(doc, app);
    Check(Call(client, "schematic.toggle-breakpoint", {SchematicPosition(pinIdentity, "position")}).status ==
              EDITOR_STALE,
          "reused pin identity accepted");
    oldPin.Unchanged();
    // A read-only editor scope refuses mutations; a healthy layout is not reconstructed.
    auto *session = doc.Session(script.created);
    app.Attach(doc.Token(), doc.TopologyRevision(), true, false);
    Saved readOnly(doc, app);
    Check(Call(client, "schematic.toggle-breakpoint", {behavior()}).status == EDITOR_REFUSED, "read-only scope edited");
    readOnly.Unchanged();
    app.Attach(doc.Token(), doc.TopologyRevision(), true, true);
    Saved rebuild(doc, app);
    Check(client.Command("schematic.rebuild-script",
                         {ObjectArgument("target", target), IntegerArgument("revision", session->Revision()),
                          IntegerArgument("topology", doc.TopologyRevision())})
                  .status == EDITOR_NO_CHANGE,
          "healthy layout reconstructed");
    rebuild.Unchanged();
    // The public command uses the existing file coordinator, including late replies and failures.
    const auto folder = std::filesystem::temp_directory_path() / "nemodev-script-snapshot-tests";
    std::filesystem::create_directories(folder);
    struct Clean {
        std::filesystem::path path;
        ~Clean() {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } clean{folder};
    const auto path = (folder / "快照.png").u8string();
    std::vector<unsigned char> pixels(100 * 60 * 4, 80);
    for (size_t i = 3; i < pixels.size(); i += 4)
        pixels[i] = 255;
    Check(WriteBgraPng(path, 100, 60, pixels.data()), "write snapshot fixture");
    doc.History().Clear();
    doc.MarkSaved();
    doc.History().MarkSavePoint();
    const auto snapshot = [&](const std::string &path) {
        return Call(client, "schematic.load-snapshot",
                    {{"target", EDITOR_OBJECT, target},
                     Number("revision", session->Revision()),
                     Number("topology", doc.TopologyRevision()),
                     {"path", EDITOR_TEXT_VALUE, {}, path}});
    };
    const auto drain = [&](const EditorResponse &operation) {
        Check(operation.status == EDITOR_QUEUED, "snapshot command missing operation");
        app.EndFrame();
        runtime.Finish();
        app.Attach(doc.Token(), doc.TopologyRevision(), true, true);
        commands.Poll();
        return global.Completion(operation.operation);
    };
    for (int mode = 0; mode < 2; ++mode) {
        Saved failure(doc, app);
        failBefore = mode == 1;
        const auto result = drain(snapshot(mode ? path : (folder / "missing.png").u8string()));
        failBefore = false;
        Check(result.status != EDITOR_OK && result.status != EDITOR_QUEUED, "failed snapshot load succeeded");
        failure.Unchanged();
    }
    failAfter = true;
    const auto loaded = drain(snapshot(path));
    failAfter = false;
    Check(loaded.status == EDITOR_OK && !loaded.message.empty() && doc.History().UndoCount() == 1 &&
              session->Data().header.snapshot,
          "committed snapshot lost after runtime failure");
    VxImageDescEx image;
    std::unique_ptr<XBYTE[]> imageBytes(VxConvertBitmap(session->Data().header.snapshot, image));
    Check(imageBytes && image.Width <= 48 && image.Height <= 36, "snapshot image scaling changed");
    Check(doc.History().Undo() && !doc.Dirty() && !session->Data().header.snapshot,
          "snapshot Undo did not restore save point");
    // Native conversion exceptions release the scratch parameter and do not leak SDK objects.
    const CKGUID custom(0x19821, 0x99a7);
    CKParameterTypeDesc type{};
    type.Guid = custom;
    type.TypeName = "Throwing breakpoint value";
    type.DefaultSize = sizeof(int);
    type.StringFunction = ThrowingText;
    Check(context->GetParameterManager()->RegisterParameterType(&type) == CK_OK, "register throwing converter");
    const auto customIndex = native->GetLocalParameterCount();
    auto *customParameter = native->CreateLocalParameter("Throwing condition", custom);
    Check(customParameter, "create throwing condition");
    auto parserCondition = condition;
    parserCondition.fields[0].object = doc.Reference(customParameter->GetID());
    parserCondition.fields[1].object = {};
    parserCondition.fields[2].integer = 0;
    parserCondition.fields.back().text = "throw";
    conditions.fields = {parserCondition};
    b = selection();
    Saved conversion(doc, app);
    const auto countBeforeCommand = context->GetObjectsCountByClassID(CKCID_PARAMETEROUT);
    Check(Call(client, "schematic.configure-breakpoint", {BreakpointField(b), conditions}).status == EDITOR_ERROR,
          "converter exception escaped command handling");
    conversion.Unchanged();
    Check(context->GetObjectsCountByClassID(CKCID_PARAMETEROUT) == countBeforeCommand,
          "failed command leaked a working parameter");
    Saved beforeLate(doc, app);
    const auto waiting = snapshot("");
    Check(waiting.status == EDITOR_QUEUED && commands.PendingInput() &&
              commands.PendingInput()->file == DocumentCommands::Input::File::Image,
          "snapshot file input missing");
    const auto inputId = commands.PendingInput()->id;
    app.CloseScope(view);
    Check(commands.Respond(inputId, "accept", path) == EDITOR_CANCELLED, "closed view accepted snapshot reply");
    beforeLate.Unchanged();
    const auto count = context->GetObjectsCountByClassID(CKCID_PARAMETEROUT);
    bool threw = false;
    try {
        ParameterBytesFromText(context, custom, "throw");
    } catch (...) {
        threw = true;
    }
    Check(threw && context->GetObjectsCountByClassID(CKCID_PARAMETEROUT) == count,
          "conversion failure leaked scratch parameter");
    int n = 1;
    std::vector<CKBYTE> bytes(sizeof(n));
    std::memcpy(bytes.data(), &n, sizeof(n));
    threw = false;
    try {
        ParameterTextFromBytes(context, custom, bytes);
    } catch (...) {
        threw = true;
    }
    Check(threw && context->GetObjectsCountByClassID(CKCID_PARAMETEROUT) == count,
          "text projection failure leaked scratch parameter");
    context->DestroyObject(native->RemoveLocalParameter(customIndex));
    context->GetParameterManager()->UnRegisterParameterType(custom);
}
