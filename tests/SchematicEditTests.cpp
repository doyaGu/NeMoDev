#include "tests/TestActions.h"
#include "application/EditorApplication.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/document/ScriptSession.h"
#include "core/graph/TopologyCommands.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/schematic/SchematicEdits.h"
#include "editor/ImGuiDrawing.h"
#include "features/schematic/SchematicCommands.h"
#include "features/objects/ObjectCommands.h"
#include "features/setup/SetupCommands.h"
#include <stdexcept>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}

EditorField Integer(const char *key, uint64_t value) {
    return {key, EDITOR_INTEGER_VALUE, {}, {}, value};
}
EditorField Objects(const char *key, const std::vector<ObjectRef> &refs) {
    EditorField field{key, EDITOR_OBJECT_LIST};
    field.objects = refs;
    return field;
}
} // namespace
void TestSchematicEdits(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    Document doc(runtime);
    std::string error;
    Check(doc.New(&error), error.c_str());
    const auto level = doc.Context()->GetCurrentLevel()->GetID();
    const auto first = CreateScript(doc, level, "Schematic editing A");
    const auto second = CreateScript(doc, level, "Schematic editing B");
    Check(first.ok && second.ok, "create Schematic scripts");
    const auto a = doc.Reference(first.created), b = doc.Reference(second.created);
    Check(EditScriptData(doc, a.object, "Seed comments",
                         [&](InterfaceData &data) {
                             Comment comment;
                             comment.text = "Original";
                             data.rootBehavior.AddComment(comment);
                         }),
          "seed Schematic comment");
    doc.History().Clear();
    doc.MarkSaved();
    doc.History().MarkSavePoint();
    SchematicCommands schematic([&] { return &doc; });
    SetupCommands setup([&] { return &doc; });
    EditorApplication app([&] { return &doc; }, [] { return true; });
    const auto draw = [](void *, const EditorHost *, const EditorValueInput *) -> uint32_t { return 0; };
    const EditorValueEditorDesc editors[] = {
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.string"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Text"), draw, nullptr, EDITOR_TEXT_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.color"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Color"), draw, nullptr, EDITOR_REAL_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.integer"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Integer"), draw, nullptr, EDITOR_INTEGER_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.boolean"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Boolean"), draw, nullptr, EDITOR_INTEGER_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.real"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Real"), draw, nullptr, EDITOR_REAL_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.vector3"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Vector"), draw, nullptr, EDITOR_REAL_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.vector2"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Vector"), draw, nullptr, EDITOR_REAL_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.rect"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Rect"), draw, nullptr, EDITOR_REAL_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.object"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Object"), draw, nullptr, EDITOR_OBJECT},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.structure"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Structure"), draw, nullptr, EDITOR_RECORD},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.object-array"),
         EDITOR_TEXT("test.schematic-values"), EDITOR_TEXT("Objects"), draw, nullptr, EDITOR_OBJECT_LIST}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("test.schematic-values");
    module.editors = editors;
    module.editorCount = std::size(editors);
    Check(schematic.Register(app, error) && setup.Register(app, error) && app.Register(module, error) &&
              app.Activate(EDITOR_IMGUI_VERSION, error),
          error.c_str());
    const auto sync = [&] { app.Attach(doc.Token(), doc.TopologyRevision(), true, true); };
    sync();
    // The Schematic's direct display edit must notify an external view too.
    app.EndFrame();
    const auto watching = app.OpenScope();
    unsigned notices = 0;
    EditorSubscription subscription = 0;
    Check(app.Host()->watch(app.Host()->data, watching,
        [](void *data, const EditorHost *, const EditorChange *) { ++*static_cast<unsigned *>(data); },
        &notices, &subscription) == EDITOR_OK, "watch Schematic display edits");
    Check(EditScriptData(doc, a.object, "Change Script color", [](InterfaceData &data) {
        data.header.color ^= 1;
    }), "direct Schematic display edit");
    sync();
    app.EndFrame();
    Check(notices == 1, "direct Schematic display edit did not notify external view");
    Check(doc.History().Undo(), "undo direct display edit");
    app.CloseScope(watching);
    doc.History().Clear();
    doc.MarkSaved();
    doc.History().MarkSavePoint();
    sync();
    const auto owner = app.OpenScope();
    const auto begin = [&](const char *id, const std::vector<EditorField> &fields) {
        const auto values = BorrowEditorFields(fields);
        EditorEdit edit = 0;
        Check(app.BeginEdit(id, {app.Scope(owner), values.data(), uint32_t(values.size())}, edit) == EDITOR_OK,
              "begin Schematic property");
        return edit;
    };
    const auto update = [&](EditorEdit edit, const EditorField &field) {
        const std::vector<EditorField> fields{field};
        const auto value = BorrowEditorFields(fields);
        Check(app.UpdateEdit(edit, value.values.front()) == EDITOR_OK, "update Schematic property");
    };
    const auto finish = [&](EditorEdit edit) {
        Check(app.FinishEdit(edit, true) == EDITOR_QUEUED, "confirm Schematic property");
        app.EndFrame();
        sync();
    };
    const std::vector<EditorField> commentArgs{
        {"target", EDITOR_OBJECT, a}, {"owner", EDITOR_OBJECT, a}, Integer("index", 0)};
    const auto text = [&] { return doc.Session(a.object)->Data().rootBehavior.comments[0].text; };
    auto edit = begin("schematic.comment", commentArgs);
    for (int i = 0; i < 40; ++i)
        update(edit, {"value", EDITOR_TEXT_VALUE, {}, "Draft " + std::to_string(i)});
    Check(text() == "Original" && !doc.Dirty() && !doc.History().CanUndo(), "comment draft touched document");
    finish(edit);
    Check(text() == "Draft 39" && doc.History().UndoCount() == 1, "comment did not form one undo step");
    Check(doc.History().Undo() && text() == "Original" && !doc.Dirty(), "undo comment");
    sync();
    edit = begin("schematic.comment", commentArgs);
    update(edit, {"value", EDITOR_TEXT_VALUE, {}, "Cancelled"});
    Check(app.FinishEdit(edit, false) == EDITOR_CANCELLED && text() == "Original" && doc.History().CanRedo() &&
              !doc.Dirty(),
          "comment cancellation changed document or Redo");
    edit = begin("schematic.comment", commentArgs);
    finish(edit);
    Check(doc.History().UndoCount() == 0 && doc.History().CanRedo() && !doc.Dirty(), "unchanged comment cleared Redo");

    // A comment slot cannot be reused after another display edit.
    edit = begin("schematic.comment", commentArgs);
    update(edit, {"value", EDITOR_TEXT_VALUE, {}, "Stale"});
    Check(EditScriptData(doc, a.object, "Replace comment",
                         [&](InterfaceData &data) { data.rootBehavior.comments[0].text = "Replacement"; }),
          "replace comment slot");
    Check(app.FinishEdit(edit, true) == EDITOR_STALE && text() == "Replacement", "stale comment overwrote reused slot");
    Check(doc.History().Undo() && text() == "Original", "undo external comment change");
    sync();

    const auto beforeA = doc.Session(a.object)->Data().header.color;
    const auto beforeB = doc.Session(b.object)->Data().header.color;
    const auto steps = doc.History().UndoCount();
    edit = begin("schematic.script-color", {{"target", EDITOR_OBJECT, a}, Objects("targets", {a, b})});
    EditorField color{"value", EDITOR_REAL_LIST};
    color.reals = {.2, .4, .6, 1};
    for (int i = 0; i < 20; ++i)
        update(edit, color);
    Check(doc.Session(a.object)->Data().header.color == beforeA &&
              doc.Session(b.object)->Data().header.color == beforeB,
          "unconfirmed color changed document");
    finish(edit);
    Check(doc.History().UndoCount() == steps + 1 && doc.Session(a.object)->Data().header.color == 0x996633 &&
              doc.Session(b.object)->Data().header.color == 0x996633,
          "script color batch was not atomic");
    const std::vector<EditorField> resetFields{{"target", EDITOR_OBJECT, a}, Objects("targets", {a, b})};
    const auto resetValues = BorrowEditorFields(resetFields);
    Check(app.Execute("schematic.reset-color", {app.Scope(owner), resetValues.data(), uint32_t(resetValues.size())})
                  .status == EDITOR_OK,
          "reset script colors");
    Check(doc.History().UndoCount() == steps + 2 && doc.Session(a.object)->Data().header.color == beforeA &&
              doc.Session(b.object)->Data().header.color == beforeB,
          "reset colors was not one undo step");
    Check(doc.History().Undo() && doc.Session(a.object)->Data().header.color == 0x996633 &&
              doc.Session(b.object)->Data().header.color == 0x996633,
          "undo reset colors");
    sync();
    Check(doc.History().Undo() && doc.Session(a.object)->Data().header.color == beforeA &&
              doc.Session(b.object)->Data().header.color == beforeB,
          "undo script colors");
    sync();

    // An unchanged first target must not skip later targets in a batch.
    auto *behaviorA = CKBehavior::Cast(doc.Resolve(a)), *behaviorB = CKBehavior::Cast(doc.Resolve(b));
    const auto priority = behaviorA->GetPriority();
    behaviorB->SetPriority(priority + 3);
    edit = begin("setup.behavior.priority", {{"target", EDITOR_OBJECT, a}, Objects("targets", {a, b})});
    finish(edit);
    Check(behaviorA->GetPriority() == priority && behaviorB->GetPriority() == priority &&
              doc.History().UndoCount() == steps + 1,
          "unchanged first priority skipped batch");
    Check(doc.History().Undo() && behaviorB->GetPriority() == priority + 3, "undo priority batch");
    sync();
    auto *orphan = doc.Context()->CreateObject(CKCID_BEHAVIOR, "Unattached behavior");
    Check(orphan, "create orphan behavior");
    doc.Include(orphan->GetID());
    edit = begin("setup.behavior.priority",
                 {{"target", EDITOR_OBJECT, a}, Objects("targets", {a, doc.Reference(orphan->GetID())})});
    update(edit, Integer("value", priority + 5));
    finish(edit);
    Check(behaviorA->GetPriority() == priority && doc.History().UndoCount() == steps && doc.History().CanRedo(),
          "failed priority batch kept partial changes or cleared Redo");

    const auto closing = app.OpenScope();
    const auto args = BorrowEditorFields(commentArgs);
    Check(app.BeginEdit("schematic.comment", {app.Scope(closing), args.data(), uint32_t(args.size())}, edit) ==
              EDITOR_OK,
          "begin closing comment");
    update(edit, {"value", EDITOR_TEXT_VALUE, {}, "Closed"});
    app.CloseScope(closing);
    Check(app.FinishEdit(edit, true) == EDITOR_STALE && text() == "Original", "closing view committed comment");
    edit = begin("schematic.comment", commentArgs);
    Check(doc.New(&error), error.c_str());
    sync();
    Check(app.FinishEdit(edit, true) == EDITOR_STALE && !doc.History().CanUndo(),
          "old Schematic draft reached new document");
    app.Stop();
}

void TestGraphChangeNotifications(vdev::CkRuntimeHost &runtime, const std::string &modulePath) {
    using namespace vdev;
    Document doc(runtime);
    std::string error;
    Check(doc.New(&error), error.c_str());
    const auto made = CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(), "Graph notifications");
    Check(made.ok, "create notification Script");
    const auto script = doc.Reference(made.created);
    const auto openingRevision = doc.ChangeRevision();
    auto *session = doc.Session(script.object);
    Check(session && doc.ChangeRevision() == openingRevision, "opening a Script published an edit");
    const auto original = session->Data().header.color;
    doc.History().Clear();
    doc.MarkSaved();
    doc.History().MarkSavePoint();
    bool native = true;
    EditorApplication app([&] { return &doc; }, [&] { return native; });
    ObjectCommands objects([&] { return &doc; });
    SchematicCommands schematic([&] { return &doc; });
    const auto draw = [](void *, const EditorHost *, const EditorValueInput *) -> uint32_t { return 0; };
    const EditorValueEditorDesc editors[] = {
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.color"),
         EDITOR_TEXT("test.graph"), EDITOR_TEXT("Color"), draw, nullptr, EDITOR_REAL_LIST}};
    struct Projection {
        Document &doc;
        CK_ID script;
        unsigned reads = 0;
    } projection{doc, script.object};
    const EditorParameter colorField{EDITOR_TEXT("color"), EDITOR_INTEGER_VALUE};
    const EditorQueryDesc colorQuery{sizeof(EditorQueryDesc), EDITOR_TEXT("test.graph-color"),
        EDITOR_TEXT("test.graph"), nullptr, 0,
        [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            auto &projection = *static_cast<Projection *>(data);
            ++projection.reads;
            const EditorValue color{EDITOR_TEXT("color"), EDITOR_INTEGER_VALUE, {}, {},
                                   projection.doc.Session(projection.script)->Data().header.color};
            return sink.write(sink.data, &color, 1);
        }, &projection, EDITOR_QUERY_DOCUMENT, EDITOR_DOCUMENT, &colorField, 1};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("test.graph");
    module.editors = editors;
    module.editorCount = std::size(editors);
    module.queries = &colorQuery;
    module.queryCount = 1;
    Check(objects.Register(app, error) && schematic.Register(app, error) && app.Register(module, error) &&
              app.Load(modulePath, error), error.c_str());
    EditorConfiguration config;
    config.providers = SchematicCommands::DefaultProviders();
    config.providers.push_back({"command", "object.rename", "builtin.objects"});
    config.providers.push_back({"command", "object.create-frame3d", "builtin.objects"});
    Check(app.Activate(EDITOR_IMGUI_VERSION, error, config), error.c_str());
    const auto sync = [&] { app.Attach(doc.Token(), doc.TopologyRevision(), true, true); };
    sync();
    app.SetSelection(script);
    app.EndFrame();
    const auto view = app.OpenScope(), secondView = app.OpenScope();
    const auto execute = [&](const char *id, const std::vector<EditorField> &fields, const char *provider = "") {
        const auto values = BorrowEditorFields(fields);
        return app.Execute(id, {app.Scope(view), values.data(), uint32_t(values.size()),
                               {provider, uint32_t(std::char_traits<char>::length(provider))}});
    };
    const auto number = [&](const char *id, const char *key) {
        const auto result = app.Query(id, {app.Scope(view), nullptr, 0});
        Check(result.status == EDITOR_OK, "read notification projection");
        for (const auto &field : result.fields) if (field.key == key) return field.integer;
        throw std::runtime_error("missing notification field");
    };
    Check(execute("example.schematic-watch", {{"target", EDITOR_OBJECT, script}, Integer("view", view)}).status ==
              EDITOR_OK, "subscribe loaded module");
    struct Notice { unsigned count = 0; uint64_t revision = 0; } notice, secondNotice;
    const auto watch = [&](EditorView owner, Notice &value) {
        EditorSubscription id = 0;
        Check(app.Host()->watch(app.Host()->data, owner,
            [](void *data, const EditorHost *, const EditorChange *change) {
                auto &value = *static_cast<Notice *>(data);
                ++value.count;
                value.revision = change->revision;
            }, &value, &id) == EDITOR_OK, "subscribe graph view");
    };
    watch(view, notice);
    watch(secondView, secondNotice);
    unsigned publication = 0;
    const auto publish = [&](bool changed) {
        ++publication;
        const auto before = notice.count;
        app.EndFrame();
        if (notice.count != before + unsigned(changed) || notice.count != secondNotice.count ||
            notice.revision != secondNotice.revision || number("example.schematic-state", "notices") != notice.count)
            throw std::runtime_error("graph notification step " + std::to_string(publication) + ": expected " +
                std::to_string(before + unsigned(changed)) + ", received " + std::to_string(notice.count));
        if (changed)
            Check(number("example.schematic-state", "read-status") == EDITOR_OK &&
                      number("example.schematic-state", "color") == session->Data().header.color,
                  "loaded module observed stale state");
        app.EndFrame();
        Check(notice.count == before + unsigned(changed), "stable graph repeated a notification");
    };
    const auto colorFields = [&](CKDWORD color) {
        EditorField value{"value", EDITOR_REAL_LIST};
        value.reals = {double(color & 255) / 255, double((color >> 8) & 255) / 255,
                       double((color >> 16) & 255) / 255, 1};
        EditorField revisions{"revisions", EDITOR_INTEGER_LIST};
        revisions.integers = {session->Revision()};
        return std::vector<EditorField>{{"target", EDITOR_OBJECT, script}, value,
            {"owner", EDITOR_OBJECT}, Integer("index", 0), Objects("targets", {script}), revisions};
    };
    Check(number("test.graph-color", "color") == original && projection.reads == 1,
          "initial graph projection");

    // Direct Schematic edits, registered built-ins and a loaded provider must
    // affect exactly the same versions and save point.
    const CKDWORD next = original ^ 0x112233;
    uint64_t scriptDelta = 0;
    for (int route = 0; route < 3; ++route) {
        const auto topology = doc.TopologyRevision(), change = doc.ChangeRevision(), revision = session->Revision();
        if (!route) {
            Check(EditScriptData(doc, script.object, "Set Color", [&](InterfaceData &data) { data.header.color = next; }),
                  "direct graph display edit");
        } else {
            const auto result = execute("schematic.script-color-set", colorFields(next),
                                        route == 1 ? "builtin.schematic" : "example.schematic");
            Check(result.status == EDITOR_OK && result.fields.empty() && result.message.empty(),
                  "registered graph result differs");
        }
        if (!route) scriptDelta = session->Revision() - revision;
        Check(session->Data().header.color == next && doc.ChangeRevision() == change + 1 &&
                  doc.TopologyRevision() == topology && session->Revision() == revision + scriptDelta &&
                  doc.Dirty() && doc.History().UndoCount() == 1 && app.Selection() == script,
              "graph command route changed topology, history, selection or display semantics");
        // Reads before Attach/EndFrame must also discard old projections.
        Check(number("test.graph-color", "color") == next, "direct edit kept cached graph projection");
        sync();
        publish(true);
        const auto undoChange = doc.ChangeRevision();
        Check(doc.History().Undo() && session->Data().header.color == original && !doc.Dirty() &&
                  doc.TopologyRevision() == topology && doc.ChangeRevision() == undoChange + 1,
              "undo graph display edit");
        publish(true);
        Check(doc.History().Redo() && session->Data().header.color == next && doc.TopologyRevision() == topology,
              "redo graph display edit");
        publish(true);
        Check(doc.History().Undo() && !doc.Dirty(), "restore graph route baseline");
        publish(true);
    }
    Check(number("example.schematic-state", "writes") == 1, "loaded graph provider was bypassed");

    // A preview can update the built-in view without publishing a committed
    // edit. Cancellation and failed nested commands must preserve Redo.
    const auto change = doc.ChangeRevision(), topology = doc.TopologyRevision();
    const auto redo = doc.History().RedoLabel();
    Check(number("test.graph-color", "color") == original, "prime committed projection before preview");
    const auto reads = projection.reads;
    {
        Edit gesture(doc, "Move graph preview", Edit::Marks::Scripts);
        auto *data = gesture.ScriptData(script.object);
        Check(data, "open graph preview");
        data->header.color = next;
        publish(false);
        Check(number("test.graph-color", "color") == original && projection.reads == reads,
              "preview invalidated committed projection");
        Check(gesture.Cancel(), "cancel graph preview");
    }
    publish(false);
    for (const auto *id : {"example.schematic-color-refuse", "example.schematic-color-throw",
                           "example.schematic-color-cancel"}) {
        const auto result = execute(id, colorFields(next));
        const auto expected = std::string(id).find("throw") != std::string::npos ? EDITOR_ERROR :
                              std::string(id).find("cancel") != std::string::npos ? EDITOR_NO_CHANGE : EDITOR_REFUSED;
        Check(result.status == expected && session->Data().header.color == original &&
                  doc.ChangeRevision() == change && doc.TopologyRevision() == topology && !doc.Dirty() &&
                  doc.History().UndoCount() == 0 && doc.History().RedoLabel() == redo &&
                  !doc.EditOpen() && app.Selection() == script, "failed graph command did not roll back");
        publish(false);
    }
    Check(execute("schematic.script-color-set", colorFields(original), "example.schematic").status == EDITOR_NO_CHANGE,
          "unchanged external graph command");
    publish(false);

    // Queued execution fixes the selected provider at submission and produces
    // one committed edit and one notification at the frame boundary.
    auto selected = config;
    for (auto &choice : selected.providers)
        if (choice.kind == "command" && choice.id == "schematic.script-color-set") choice.provider = "example.schematic";
    Check(app.Configure(selected, error), error.c_str());
    publish(false); // command selection does not modify the document
    app.EnterDraw();
    const auto queued = execute("schematic.script-color-set", colorFields(next));
    app.LeaveDraw();
    Check(queued.status == EDITOR_QUEUED && queued.operation && session->Data().header.color == original,
          "graph drawing command ran immediately");
    publish(true);
    Check(doc.History().UndoCount() == 1 && doc.ChangeRevision() == change + 1 &&
              doc.TopologyRevision() == topology && session->Data().header.color == next,
          "queued external graph edit differs");
    Check(doc.History().Undo(), "undo queued graph edit");
    publish(true);

    // A topology operation already contains nested records of its own.
    // Forwarding it through the public command entry must add no extra mark.
    const std::string originalName = doc.Resolve(script)->GetName();
    for (int route = 0; route < 3; ++route) {
        const auto beforeTopology = doc.TopologyRevision(), beforeChange = doc.ChangeRevision();
        const auto revision = session->Revision();
        if (!route)
            Check(RenameLevelObject(doc, script, "External Renamed graph").ok, "direct graph rename");
        else {
            const auto result = execute(route == 1 ? "object.rename" : "example.rename",
                {{"target", EDITOR_OBJECT, script},
                 {"name", EDITOR_TEXT_VALUE, {}, route == 1 ? "External Renamed graph" : "Renamed graph"}},
                route == 1 ? "builtin.objects" : "");
            Check(result.status == EDITOR_OK && result.fields.empty() && result.message.empty(),
                  "registered topology result differs");
        }
        if (!route) scriptDelta = session->Revision() - revision;
        Check(doc.TopologyRevision() == beforeTopology + 1 && doc.ChangeRevision() == beforeChange + 1 &&
                  session->Revision() == revision + scriptDelta && doc.History().UndoCount() == 1 &&
                  std::string(doc.Resolve(script)->GetName()) == "External Renamed graph", "topology route marked twice");
        publish(true);
        Check(doc.History().Undo() && !doc.Dirty() && doc.TopologyRevision() == beforeTopology + 2 &&
                  std::string(doc.Resolve(script)->GetName()) == originalName, "topology Undo differs");
        publish(true);
        Check(doc.History().Redo() && doc.TopologyRevision() == beforeTopology + 3, "topology Redo differs");
        publish(true);
        Check(doc.History().Undo() && !doc.Dirty(), "restore topology baseline");
        publish(true);
    }

    // Outside-undo Script replacement is still a content change, but is not
    // a topology edit. Busy native state delays publication until it is safe.
    const auto displayTopology = doc.TopologyRevision();
    native = false;
    app.EndFrame();
    Check(notice.count == secondNotice.count, "busy publication diverged");
    native = true;
    InterfaceData replacement = session->Data();
    replacement.header.color = next;
    Check(session->Assign(replacement), "replace graph display data");
    native = false;
    publish(false);
    native = true;
    publish(true);
    Check(doc.TopologyRevision() == displayTopology, "display replacement marked topology");
    replacement.header.color = original;
    Check(session->Assign(replacement), "restore graph display data");
    publish(true);
    doc.MarkSaved();
    doc.History().MarkSavePoint();

    // Several core topology edits in one group mark topology only once, on
    // commit and replay, while retaining their ordinary graph algorithms.
    const auto groupedChange = doc.ChangeRevision(), groupedTopology = doc.TopologyRevision();
    CK_ID graph = 0;
    {
        EditGroup group(doc, "Create graph and parameter");
        Check(EditScriptData(doc, script.object, "Set Color", [&](InterfaceData &data) { data.header.color = next; }),
              "create grouped display change");
        const auto created = CreateBehaviorGraph(doc, script.object, script.object, "Nested graph", Point(80, 80));
        Check(created.ok, "create grouped graph");
        graph = created.created;
        Check(AddLocalParameter(doc, script.object, graph, "Value", CKPGUID_INT, Point(100, 100)).ok,
              "create grouped parameter");
        publish(false);
    }
    Check(doc.TopologyRevision() == groupedTopology + 1 && doc.ChangeRevision() == groupedChange + 1 &&
              doc.History().UndoCount() == 1 && FindRecord(session->Data(), graph), "grouped topology marked twice");
    publish(true);
    Check(doc.History().Undo() && !doc.Resolve(doc.Reference(graph)) && !doc.Dirty() &&
              doc.TopologyRevision() == groupedTopology + 2, "nested graph Undo marked topology twice");
    publish(true);
    Check(doc.History().Redo() && FindRecord(session->Data(), graph) &&
              doc.TopologyRevision() == groupedTopology + 3, "nested graph Redo marked topology twice");
    publish(true);
    Check(doc.History().Undo(), "restore nested graph");
    publish(true);
    // A replay failure changes the available history even when it cannot
    // restore display data. Subscribers must not keep the old history state.
    {
        Edit broken(doc, "Failed graph replay", Edit::Marks::Scripts);
        auto *data = broken.ScriptData(script.object);
        Check(data, "open failed graph replay");
        data->header.color = next;
        broken.Done(vdevtest::TestAction([] { return true; }, [] { return false; }));
        Check(broken.Commit().ok, "commit failing graph replay");
    }
    publish(true);
    const auto replayChange = doc.ChangeRevision();
    Check(!doc.History().Undo() && !doc.History().CanUndo() && !doc.History().CanRedo() &&
              doc.ChangeRevision() == replayChange + 1, "failed graph replay did not record the changed history");
    publish(true);
    app.CloseScope(secondView);
    app.CloseScope(view);
    app.Stop();

    // Session ownership follows an adopted composition; its temporary
    // Document must not remain the recipient of outside-undo changes.
    auto prepared = doc.PrepareNew(&error, {});
    Check(bool(prepared), error.c_str());
    auto &candidate = prepared->Candidate();
    const auto adopted = CreateScript(candidate, candidate.Context()->GetCurrentLevel()->GetID(), "Adopted Script");
    Check(adopted.ok && candidate.Session(adopted.created), "prepare adopted Script session");
    prepared->Commit();
    const auto adoptedRevision = doc.ChangeRevision();
    auto *adoptedSession = doc.Session(adopted.created);
    InterfaceData adoptedData = adoptedSession->Data();
    adoptedData.header.color ^= 1;
    Check(adoptedSession->Assign(adoptedData) && doc.ChangeRevision() == adoptedRevision + 1,
          "adopted Script session retained its temporary owner");
}
