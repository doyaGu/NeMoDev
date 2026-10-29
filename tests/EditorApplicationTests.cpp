#include "application/EditorApplication.h"
#include "features/objects/ObjectCommands.h"
#include "editor/ImGuiDrawing.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace vdev;
void TestWorkspace(vdev::CkRuntimeHost &host);
void TestDocumentCommands(vdev::CkRuntimeHost &host);
void TestDocumentTransitions(vdev::CkRuntimeHost &host);
void TestExecutionCommands(vdev::CkRuntimeHost &host, const std::string &modulePath);
void TestResourceCommands(vdev::CkRuntimeHost &host, const std::string &modulePath);
void TestObjectCommands(vdev::CkRuntimeHost &host, const std::string &modulePath);
namespace {
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
uint32_t Draw(void *, const EditorHost *, const EditorTextInput *) { return 0; }
EditorModuleDesc TextModule() {
    static const EditorTextEditorDesc editor{sizeof(editor),
                                             EDITOR_IMGUI_VERSION,
                                             EDITOR_TEXT("text.single-line"),
                                             EDITOR_TEXT("test.text"),
                                             EDITOR_TEXT("Text"),
                                             Draw,
                                             nullptr};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EditorString EDITOR_TEXT("test.editors");
    module.editors = &editor;
    module.editorCount = 1;
    return module;
}
EditorStatus NoChange(void *, const EditorHost *, const EditorRequest *, EditorSink) { return EDITOR_NO_CHANGE; }
void VersionedQueries() {
    struct Values {
        uint64_t revision = 1, value = 7;
        unsigned reads = 0, mutations = 0;
        bool fail = false;
        EditorStatus attempted = EDITOR_OK;
    } values;
    const EditorParameter field{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE};
    EditorQueryDesc query{sizeof(EditorQueryDesc), EDITOR_TEXT("test.published"), EDITOR_TEXT("test.queries"),
        nullptr, 0,
        [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            auto &values = *static_cast<Values *>(data);
            ++values.reads;
            const EditorValue result{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE, {}, {}, values.value};
            return sink.write(sink.data, &result, 1);
        }, &values, EDITOR_QUERY_VERSIONED, EDITOR_WORKSPACE, &field, 1,
        [](void *data, const EditorHost *host) -> uint64_t {
            auto &values = *static_cast<Values *>(data);
            const EditorRequest request{host->scope(host->data, 0), nullptr, 0};
            values.attempted = host->execute(host->data, EditorString EDITOR_TEXT("test.increment"), &request, {});
            if (values.fail) throw std::runtime_error("version failure");
            return values.revision;
        }};
    const EditorCommandDesc increment{sizeof(EditorCommandDesc), EDITOR_TEXT("test.increment"), EDITOR_TEXT("test.queries"),
        EDITOR_TEXT("Increment"), nullptr, 0,
        [](void *data, const EditorHost *, const EditorRequest *, EditorSink) {
            ++static_cast<Values *>(data)->mutations;
            return EDITOR_OK;
        }, &values, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE};
    auto module = TextModule();
    module.queries = &query; module.queryCount = 1;
    module.commands = &increment; module.commandCount = 1;
    EditorApplication app({});
    std::string error;
    const auto revision = query.revision;
    query.revision = nullptr;
    Check(!app.Register(module, error), "versioned query accepted no version reader");
    query.revision = revision;
    query.refresh = EDITOR_QUERY_EACH_READ;
    Check(!app.Register(module, error), "uncached query accepted a version reader");
    query.refresh = EDITOR_QUERY_VERSIONED;
    Check(app.Register(module, error) && app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    const EditorRequest request{app.Scope(), nullptr, 0};
    const auto *first = &app.ReadQuery("test.published", request);
    const auto *same = &app.ReadQuery("test.published", request);
    Check(first == same && same->status == EDITOR_OK && same->fields[0].integer == 7 && values.reads == 1,
          "unchanged published query was rebuilt or copied");
    const auto copied = app.Query("test.published", request);
    ++values.value; ++values.revision;
    const auto &next = app.ReadQuery("test.published", request);
    Check(next.status == EDITOR_OK && next.fields[0].integer == 8 && copied.fields[0].integer == 7 && values.reads == 2,
          "independent version did not refresh or changed an owned projection");
    Check(values.attempted == EDITOR_REFUSED && values.mutations == 0, "version reader mutated editor state");
    values.fail = true;
    Check(app.ReadQuery("test.published", request).status == EDITOR_ERROR, "version failure was hidden by the cache");
    values.fail = false;
    app.NativeAvailable(false);
    ++values.value; ++values.revision;
    Check(app.ReadQuery("test.published", request).fields[0].integer == 9, "published workspace query required native access");
    const auto scope = app.OpenScope();
    Check(app.ReadQuery("test.published", {app.Scope(scope), nullptr, 0}).status == EDITOR_OK, "view query failed");
    app.CloseScope(scope);
    Check(app.ReadQuery("test.published", {{}, nullptr, 0}).status == EDITOR_OK, "application query lost its scope");
    Check(app.ReadQuery("test.published", {{{}, scope}, nullptr, 0}).status == EDITOR_STALE,
          "closed view retained cached access");
    EditorStatus wrongThread = EDITOR_OK;
    std::thread worker([&] { wrongThread = app.ReadQuery("test.published", request).status; });
    worker.join();
    Check(wrongThread == EDITOR_REFUSED, "worker read a borrowed projection");
}
void Registration() {
    EditorApplication app({});
    std::string error;
    auto module = TextModule();
    auto invalid = module;
    invalid.size = 0;
    Check(!app.Register(invalid, error), "wrong size accepted");
    auto duplicate = module;
    const EditorTextEditorDesc editors[] = {module.editors[0], module.editors[0]};
    duplicate.editors = editors;
    duplicate.editorCount = 2;
    Check(!app.Register(duplicate, error), "partial duplicate registration accepted");
    Check(app.Register(module, error), "failed registration left partial entries");
    Check(!app.Register(module, error), "duplicate module accepted");
    auto drawing = EditorDrawingVersion EDITOR_IMGUI_VERSION;
    ++drawing.ioSize;
    Check(!app.Activate(drawing, error), "wrong drawing layout accepted");
    Check(app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    Check(!app.Register(module, error), "late registration accepted");
    auto config = app.Configuration();
    config.providers.push_back({"editor", "text.single-line", "missing.editor"});
    Check(!app.Configure(config, error), "missing editor accepted");
    Check(app.Configuration().providers.empty(), "failed configuration changed active choices");
    config = {};
    config.menus.push_back({"missing.item", "missing.location", 0});
    Check(!app.Configure(config, error), "unknown menu accepted");
    EditorApplication cyclic({});
    auto a = TextModule(), b = a;
    const EditorString da = EDITOR_TEXT("loop.b"), db = EDITOR_TEXT("loop.a");
    a.id = EditorString EDITOR_TEXT("loop.a");
    a.dependencies = &da;
    a.dependencyCount = 1;
    b = {};
    b.size = sizeof(b);
    b.version = EDITOR_VERSION;
    b.id = EditorString EDITOR_TEXT("loop.b");
    b.dependencies = &db;
    b.dependencyCount = 1;
    Check(cyclic.Register(a, error) && cyclic.Register(b, error), "cycle descriptors rejected too early");
    Check(!cyclic.Activate(EDITOR_IMGUI_VERSION, error), "dependency cycle activated");
    EditorApplication schema({});
    EditorCommandDesc commands[] = {{sizeof(EditorCommandDesc), EDITOR_TEXT("test.command"), EDITOR_TEXT("test.one"),
                                     EDITOR_TEXT("Test"), nullptr, 0, NoChange, nullptr},
                                    {sizeof(EditorCommandDesc), EDITOR_TEXT("test.command"), EDITOR_TEXT("test.two"),
                                     EDITOR_TEXT("Test"), nullptr, 0, NoChange, nullptr}};
    const EditorParameter argument{EDITOR_TEXT("target"), EDITOR_OBJECT};
    commands[1].parameters = &argument;
    commands[1].count = 1;
    auto inconsistent = TextModule();
    inconsistent.commands = commands;
    inconsistent.commandCount = 2;
    Check(!schema.Register(inconsistent, error), "inconsistent command arguments accepted");

    std::vector<int> stopped;
    struct ModuleData {
        std::vector<int> *stopped;
        int id;
    } base{&stopped, 1}, dependent{&stopped, 2};
    EditorApplication stopping({});
    EditorModuleDesc first{};
    first.size = sizeof(first);
    first.version = EDITOR_VERSION;
    first.id = EditorString EDITOR_TEXT("lifetime.base");
    first.data = &base;
    first.stop = [](void *p) {
        const auto &data = *static_cast<ModuleData *>(p);
        data.stopped->push_back(data.id);
    };
    auto second = first;
    second.id = EditorString EDITOR_TEXT("lifetime.dependent");
    second.dependencies = &first.id;
    second.dependencyCount = 1;
    second.data = &dependent;
    Check(stopping.Register(second, error) && stopping.Register(first, error) &&
              stopping.Activate(EDITOR_IMGUI_VERSION, error),
          error.c_str());
    stopping.Stop();
    stopping.Stop();
    Check(stopped == std::vector<int>({2, 1}), "modules did not stop exactly once in reverse dependency order");
}
EditorStatus Count(void *p, const EditorHost *, const EditorRequest *, EditorSink sink) {
    ++*static_cast<int *>(p);
    const EditorValue name{EDITOR_TEXT("name"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("cached")};
    return sink.write(sink.data, &name, 1);
}
void Edits(CkRuntimeHost &runtime, const char *path) {
    Document document(runtime);
    std::string error;
    Check(document.New(&error), error.c_str());
    const auto created = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(created.ok, "cannot create target");
    const auto object = document.Reference(created.created);
    auto name = [&] { return std::string(document.Resolve(object)->GetName()); };
    const auto original = name();
    document.History().Clear();
    document.MarkSaved();
    document.History().MarkSavePoint();
    EditorApplication app([&] { return &document; });
    ObjectCommands objects([&] { return &document; });
    Check(objects.Register(app, error) && app.Register(TextModule(), error), error.c_str());
    int reads = 0;
    const EditorParameter fields[] = {{EDITOR_TEXT("name"), EDITOR_TEXT_VALUE}};
    const EditorQueryDesc count{sizeof(count), EDITOR_TEXT("test.cached"), EDITOR_TEXT("test.query"), nullptr, 0, Count,
                                &reads,        EDITOR_QUERY_DOCUMENT,      EDITOR_DOCUMENT,           fields,  1};
    EditorModuleDesc queries{};
    queries.size = sizeof(queries);
    queries.version = EDITOR_VERSION;
    queries.id = EditorString EDITOR_TEXT("test.queries");
    queries.queries = &count;
    queries.queryCount = 1;
    Check(app.Register(queries, error), error.c_str());
    Check(app.Load(path, error), error.c_str());
    Check(!app.Load(path, error), "duplicate loaded module accepted");
    EditorConfiguration config;
    config.providers = {{"editor", "text.single-line", "test.text"}, {"command", "object.rename", "builtin.objects"},
                        {"command", "object.create-frame3d", "builtin.objects"}};
    Check(app.Activate(EDITOR_IMGUI_VERSION, error, config), error.c_str());
    auto invalidConfig = config;
    invalidConfig.bindings.emplace_back("object.name", "Ctrl+Unknown");
    Check(!app.Configure(invalidConfig, error) && app.Configuration().bindings.empty(),
          "invalid shortcut replaced active configuration");
    auto attach = [&] { app.Attach(document.Token(), document.TopologyRevision(), true, true); };
    attach();
    const auto owner = app.OpenScope();
    const auto scope = app.Scope(owner);
    const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, object, {}};
    const EditorRequest query{scope, &target, 1};
    const EditorRequest cached{scope, nullptr, 0};
    Check(app.Query("object.details", query).status == EDITOR_OK, "query refused");
    Check(app.Query("test.cached", cached).status == EDITOR_OK &&
              app.Query("test.cached", cached).status == EDITOR_OK && reads == 1,
          "stable query rebuilt native data");
    EditorEdit edit = 0;
    Check(app.BeginEdit("object.name", scope, object, edit) == EDITOR_OK, "edit refused");
    Check(app.UpdateEdit(edit, "Draft") == EDITOR_OK && name() == original, "draft changed document");
    Check(!app.Configure(config, error), "editor changed under an active draft");
    app.NativeAvailable(false);
    Check(app.FinishEdit(edit, true) == EDITOR_REFUSED, "busy edit was submitted");
    std::string draft;
    EditorTextEditorDesc chosen{};
    Check(app.EditState(edit, draft, chosen) && draft == "Draft", "refused commit lost its draft");
    app.NativeAvailable(true);
    Check(app.FinishEdit(edit, false) == EDITOR_CANCELLED && !document.Dirty(), "cancel wrote document");
    Check(app.BeginEdit("object.name", scope, object, edit) == EDITOR_OK, "second edit refused");
    Check(app.UpdateEdit(edit, "Changed") == EDITOR_OK, "draft update refused");
    Check(app.FinishEdit(edit, true) == EDITOR_QUEUED && name() == original, "write escaped end of frame");
    Check(app.BeginEdit("object.name", scope, object, edit) == EDITOR_REFUSED, "draft overtook pending command");
    app.EndFrame();
    Check(name() == "Changed" && document.History().UndoCount() == 1, "commit count differs");
    const EditorValue args[] = {target, {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("Module")}};
    const EditorRequest request{scope, args, 2};
    {
        Edit gesture(document, "Existing property edit");
        Check(app.CommandState("example.rename", request) == EDITOR_REFUSED &&
                  app.CanEdit("object.name", scope, object) == EDITOR_REFUSED &&
                  app.Execute("example.rename", request).status == EDITOR_REFUSED,
              "open core edit has different admission and execution rules");
    }
    Check(app.Execute("example.rename", request).status == EDITOR_OK && name() == "External Module",
          "loaded command did not use host command");
    Check(document.History().UndoCount() == 2, "nested command added multiple undo steps");
    Check(document.History().Undo(), "cannot prepare redo test");
    attach();
    const EditorValue unchanged[] = {target, {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("Changed")}};
    const EditorRequest noChange{scope, unchanged, 2};
    Check(app.Execute("object.rename", noChange).status == EDITOR_NO_CHANGE && document.History().CanRedo(),
          "unchanged command discarded redo");
    Check(document.History().Redo(), "redo after unchanged command failed");
    attach();
    Check(app.Execute("example.rename-twice", request).status == EDITOR_OK && name() == "Second name",
          "composite command failed");
    Check(document.History().UndoCount() == 3 && document.History().Undo(), "composite command was not one undo step");
    Check(name() == "External Module" && document.History().Redo(), "composite undo differs");
    attach();
    document.MarkSaved();
    document.History().MarkSavePoint();
    const auto history = document.History().UndoCount();
    for (auto id : {"example.rename-fail", "example.rename-throw"}) {
        const auto status = app.Execute(id, request).status;
        Check(status == EDITOR_REFUSED || status == EDITOR_ERROR, "failed command reported success");
        Check(name() == "Second name" && document.History().UndoCount() == history && !document.EditOpen() &&
                  !document.Dirty(),
              "failed command left partial changes");
    }
    for (auto &choice : config.providers)
        if (choice.kind == "command" && choice.id == "object.rename") choice.provider = "example.objects";
    Check(app.Configure(config, error), error.c_str());
    Check(app.BeginEdit("object.name", scope, object, edit) == EDITOR_OK, "replacement command edit refused");
    app.UpdateEdit(edit, "Selected");
    app.FinishEdit(edit, true);
    app.EndFrame();
    Check(name() == "External Selected" && document.History().UndoCount() == history + 1,
          "configured command did not handle property editing");
    const auto refusedOutput = [](void *, const EditorValue *, uint32_t) { return EDITOR_REFUSED; };
    Check(app.Host()->execute(app.Host()->data, EditorString EDITOR_TEXT("example.rename"), &request,
                              {nullptr, refusedOutput}) == EDITOR_OK &&
              name() == "External Module",
          "diagnostic delivery changed the result of an applied command");
    auto wrong = request;
    wrong.count = 1;
    Check(app.Execute("example.rename", wrong).status == EDITOR_INVALID, "wrong arguments accepted");
    EditorStatus worker = EDITOR_OK;
    std::thread thread([&] {
        worker = app.Host()->execute(app.Host()->data, EditorString EDITOR_TEXT("example.rename"), &request, {});
    });
    thread.join();
    Check(worker == EDITOR_REFUSED, "wrong-thread execution accepted");
    Check(app.BeginEdit("object.name", scope, object, edit) == EDITOR_OK, "third edit refused");
    app.UpdateEdit(edit, "Closed view");
    app.FinishEdit(edit, true);
    app.CloseScope(owner);
    Check(!app.HasEdits(), "closed view kept queued commands");
    app.EndFrame();
    Check(name() == "External Module", "closed view command was executed");
    Check(app.FinishEdit(edit, true) == EDITOR_STALE, "closed owner submitted draft");
    Check(app.Execute("example.rename", request).status == EDITOR_STALE, "closed owner wrote document");
    Check(document.New(&error), error.c_str());
    attach();
    Check(app.Execute("example.rename", request).status == EDITOR_STALE, "old document callback wrote new document");
    const auto nextOwner = app.OpenScope();
    struct Notices {
        int count = 0;
        EditorScope scope{};
    } notices;
    EditorSubscription subscription = 0;
    auto changed = [](void *data, const EditorHost *, const EditorChange *change) {
        auto &n = *static_cast<Notices *>(data);
        ++n.count;
        n.scope = change->scope;
    };
    Check(app.Host()->watch(app.Host()->data, nextOwner, changed, &notices, &subscription) == EDITOR_OK,
          "watch refused");
    app.EndFrame();
    const auto delivered = notices.count;
    app.EndFrame();
    Check(notices.count == delivered, "stable document published repeatedly");
    Check(document.New(&error), error.c_str());
    attach();
    app.EndFrame();
    Check(notices.count == delivered + 1 && notices.scope.document == document.Token(),
          "view missed document replacement");
    app.CloseScope(nextOwner);
    Check(document.New(&error), error.c_str());
    attach();
    app.EndFrame();
    Check(notices.count == delivered + 1, "closed view received a callback");
    app.Stop();
    Check(app.Query("object.details", query).status != EDITOR_OK, "callback survived module shutdown");
}
} // namespace
int main(int argc, char **argv) {
    try {
        Check(argc == 2, "module path is required");
        Registration();
        VersionedQueries();
        CkRuntimeHost runtime;
        std::string error;
        const auto *path = std::getenv("VIRTOOLS_PATH");
        Check(runtime.Start(path ? path : ".", {}, &error), error.c_str());
        Edits(runtime, argv[1]);
        TestDocumentCommands(runtime);
        TestDocumentTransitions(runtime);
        TestWorkspace(runtime);
        TestObjectCommands(runtime, argv[1]);
        TestResourceCommands(runtime, argv[1]);
        TestExecutionCommands(runtime, argv[1]);
        std::cout << "Module registration, object editing, undo and view lifetime checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
