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
#include <limits>
#include <thread>

using namespace vdev;
void TestWorkspace(vdev::CkRuntimeHost &host);
void TestEventLog();
void TestSceneCommands(vdev::CkRuntimeHost &host);
void TestBuildingBlockQueries(vdev::CkRuntimeHost &host);
void TestDataResources(vdev::CkRuntimeHost &host);
void TestParameterEdits(vdev::CkRuntimeHost &host);
void TestArrayEdits(vdev::CkRuntimeHost &host);
void TestSetupEdits(vdev::CkRuntimeHost &host);
void TestSchematicEdits(vdev::CkRuntimeHost &host);
void TestGraphChangeNotifications(vdev::CkRuntimeHost &host, const std::string &modulePath);
void TestDocumentCommands(vdev::CkRuntimeHost &host);
void TestDocumentTransitions(vdev::CkRuntimeHost &host);
void TestOutputCommands(vdev::CkRuntimeHost &host);
void TestExecutionCommands(vdev::CkRuntimeHost &host, const std::string &modulePath);
void TestRuntimeProjections(vdev::CkRuntimeHost &host);
void TestEnvironmentCommands(vdev::CkRuntimeHost &host, const std::string &fixture);
void TestResourceCommands(vdev::CkRuntimeHost &host, const std::string &modulePath);
void TestObjectCommands(vdev::CkRuntimeHost &host, const std::string &modulePath);
namespace {
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
uint32_t Draw(void *, const EditorHost *, const EditorValueInput *) { return 0; }
EditorModuleDesc TextModule() {
    static const EditorValueEditorDesc editor{sizeof(editor),
                                             EDITOR_IMGUI_VERSION,
                                             EDITOR_TEXT("text.single-line"),
                                             EDITOR_TEXT("test.text"),
                                             EDITOR_TEXT("Text"),
                                             Draw,
                                             nullptr, EDITOR_TEXT_VALUE};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EditorString EDITOR_TEXT("test.editors");
    module.editors = &editor;
    module.editorCount = 1;
    return module;
}
EditorStatus NoChange(void *, const EditorHost *, const EditorRequest *, EditorSink) { return EDITOR_NO_CHANGE; }
void OwnedValueDrawing() {
    struct Painter { unsigned calls = 0; bool recursive = false; } painter;
    const EditorValueEditorDesc valueEditor{sizeof(valueEditor), EDITOR_IMGUI_VERSION,
        EDITOR_TEXT("test.draft"), EDITOR_TEXT("test.draft"), EDITOR_TEXT("Draft value"),
        [](void *data, const EditorHost *host, const EditorValueInput *input) -> uint32_t {
            auto &painter = *static_cast<Painter *>(data); ++painter.calls;
            if (painter.recursive) return host->drawValue(host->data, EditorString EDITOR_TEXT("test.draft"), input);
            auto value = input->value; value.integer = 37;
            return input->update.write(input->update.data, &value, 1) == EDITOR_OK ? EDITOR_INPUT_CHANGED : EDITOR_INPUT_INVALID;
        }, &painter, EDITOR_INTEGER_VALUE};
    EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("test.draft"); module.editors = &valueEditor; module.editorCount = 1;
    EditorApplication app({}); std::string error;
    Check(app.Register(module, error) && app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    uint64_t draft = 4;
    EditorValueInput input{}; input.size = sizeof(input); input.label = EDITOR_TEXT("value");
    input.value = {EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE, {}, {}, draft};
    input.update = {&draft, [](void *data, const EditorValue *value, uint32_t count) {
        if (count != 1 || !value || value->type != EDITOR_INTEGER_VALUE) return EDITOR_INVALID;
        *static_cast<uint64_t *>(data) = value->integer; return EDITOR_OK;
    }};
    const auto draw = [&] { return app.Host()->drawValue(app.Host()->data, EditorString EDITOR_TEXT("test.draft"), &input); };
    Check(draw() == EDITOR_INPUT_INVALID && !painter.calls && draft == 4, "draft value editor ran outside drawing");
    app.EnterDraw();
    auto invalid = input; --invalid.size;
    Check(app.Host()->drawValue(app.Host()->data, EditorString EDITOR_TEXT("test.draft"), &invalid) == EDITOR_INPUT_INVALID,
          "draft value editor accepted an invalid descriptor");
    Check(draw() == EDITOR_INPUT_CHANGED && painter.calls == 1 && draft == 37 && !app.HasEdits(),
          "view-owned value editing did not update only its draft");
    painter.calls = 0; painter.recursive = true;
    Check(draw() == EDITOR_INPUT_CANCEL && painter.calls == 64, "draft value editor recursion was not bounded");
    painter.recursive = false; painter.calls = 0;
    uint32_t workerAction = EDITOR_INPUT_NONE;
    std::thread worker([&] { workerAction = draw(); }); worker.join();
    Check(workerAction == EDITOR_INPUT_INVALID && !painter.calls, "draft value editor ran on a worker thread");
    app.LeaveDraw();
}
void PreviewRegistration() {
    EditorApplication app({});
    const EditorCommandDesc command{
        sizeof(command),
        EDITOR_TEXT("test.bad-preview"),
        EDITOR_TEXT("test.preview"),
        EDITOR_TEXT("Bad preview"),
        nullptr,
        0,
        NoChange,
        nullptr,
        EDITOR_WORKSPACE,
        EDITOR_SESSION_CHANGE,
        nullptr,
        [](void *, const EditorHost *, const EditorRequest *, EditorPreviewAction, void **) { return EDITOR_OK; }};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("test.bad-preview");
    module.commands = &command;
    module.commandCount = 1;
    std::string error;
    Check(!app.Register(module, error), "non-document command accepted a native preview");
}
void MenuChecks() {
    for (int invalid = 0; invalid < 4; ++invalid) {
        EditorApplication app({});
        const EditorParameter field{EDITOR_TEXT("checked"), invalid == 2 ? EDITOR_TEXT_VALUE : EDITOR_INTEGER_VALUE};
        const EditorParameter argument{EDITOR_TEXT("required"), EDITOR_INTEGER_VALUE};
        const EditorQueryDesc query{sizeof(EditorQueryDesc), EDITOR_TEXT("test.checked"), EDITOR_TEXT("test.menus"),
            invalid == 3 ? &argument : nullptr, invalid == 3 ? 1u : 0u,
            [](void *, const EditorHost *, const EditorRequest *, EditorSink sink) {
                const EditorValue value{EDITOR_TEXT("checked"), EDITOR_INTEGER_VALUE, {}, {}, 1};
                return sink.write(sink.data, &value, 1);
            }, nullptr, EDITOR_QUERY_EACH_READ, EDITOR_WORKSPACE, &field, 1};
        const EditorCommandDesc command{sizeof(EditorCommandDesc), EDITOR_TEXT("test.toggle"), EDITOR_TEXT("test.menus"), EDITOR_TEXT("Toggle"),
            nullptr, 0, [](void *, const EditorHost *, const EditorRequest *, EditorSink) { return EDITOR_OK; }, nullptr, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE};
        const EditorMenuLocation location{EDITOR_TEXT("test.menu"), EDITOR_TEXT("Menu")};
        const EditorMenuDesc menu{EDITOR_TEXT("test.toggle.menu"), EDITOR_TEXT("test.toggle"), EDITOR_TEXT("test.menu"), EDITOR_TEXT("Toggle"), 0,
            invalid == 1 ? EditorString EDITOR_TEXT("test.missing") : EditorString EDITOR_TEXT("test.checked")};
        EditorModuleDesc module{};
        module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.menus");
        module.commands = &command; module.commandCount = 1; module.queries = &query; module.queryCount = 1;
        module.locations = &location; module.locationCount = 1; module.menus = &menu; module.menuCount = 1;
        std::string error;
        Check(app.Register(module, error), error.c_str());
        Check(app.Activate(EDITOR_IMGUI_VERSION, error) == (invalid == 0), "invalid menu check query was accepted");
        if (!invalid) {
            Check(app.Menus().front().checked == "test.checked" && app.Query("test.checked", {app.Scope(), nullptr, 0}).status == EDITOR_OK,
                "menu lost its registered check query");
        }
    }
}
void CommandStateReads() {
    struct Counts { unsigned writes = 0; EditorStatus nested = EDITOR_INVALID, mutation = EDITOR_OK; } counts;
    EditorApplication app({});
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("test.state-inner"), EDITOR_TEXT("test.states"), EDITOR_TEXT("Inner"), nullptr, 0,
         [](void *data, const EditorHost *, const EditorRequest *, EditorSink) { ++static_cast<Counts *>(data)->writes; return EDITOR_OK; },
         &counts, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("test.state-outer"), EDITOR_TEXT("test.states"), EDITOR_TEXT("Outer"), nullptr, 0,
         [](void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
             return host->execute(host->data, EditorString EDITOR_TEXT("test.state-inner"), request, sink);
         }, &counts, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE,
         [](void *data, const EditorHost *host, const EditorRequest *request) {
             auto &counts = *static_cast<Counts *>(data);
             counts.nested = host->commandState(host->data, EditorString EDITOR_TEXT("test.state-inner"), request);
             counts.mutation = host->execute(host->data, EditorString EDITOR_TEXT("test.state-inner"), request, {});
             return counts.nested;
         }}};
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.states");
    module.commands = commands; module.commandCount = 2;
    std::string error;
    Check(app.Register(module, error) && app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    const EditorRequest request{app.Scope(), nullptr, 0};
    Check(app.CommandState("test.state-outer", request) == EDITOR_OK && counts.nested == EDITOR_OK &&
          counts.mutation == EDITOR_REFUSED && counts.writes == 0, "command state rejected nested reads or allowed a write");
    Check(app.Execute("test.state-outer", request).status == EDITOR_OK && counts.writes == 1,
          "read-only state checks blocked normal nested execution");
}
void RealArrays() {
    double components[] = {1.25, -2, 3};
    EditorValue value{}; value.key = EDITOR_TEXT("value"); value.type = EDITOR_REAL_LIST; value.reals = {components, 3};
    value.real = std::numeric_limits<double>::quiet_NaN();
    std::vector<EditorField> copied;
    Check(CopyEditorFields(copied, &value, 1) == EDITOR_OK && copied.front().reals == std::vector<double>({1.25, -2, 3}),
          "real array copy read an inactive field or lost components");
    components[0] = 9;
    Check(copied.front().reals.front() == 1.25, "real array borrowed producer memory after copying");
    const auto borrowed = BorrowEditorFields(copied);
    Check(borrowed.size() == 1 && borrowed.values.front().reals.count == 3 && borrowed.values.front().reals.data[1] == -2,
          "real array was not delivered through the common field view");
    copied.clear(); value.reals = {nullptr, 1};
    Check(CopyEditorFields(copied, &value, 1) == EDITOR_INVALID, "null real array accepted");
    copied.clear(); value.reals = {components, UINT32_MAX};
    Check(CopyEditorFields(copied, &value, 1) == EDITOR_INVALID, "oversized real array accepted");
    copied.clear(); components[0] = std::numeric_limits<double>::infinity(); value.reals = {components, 3};
    Check(CopyEditorFields(copied, &value, 1) == EDITOR_INVALID, "non-finite real array accepted");
    components[0] = 1.25;
    EditorApplication app({});
    const EditorCommandDesc command{sizeof(EditorCommandDesc), EDITOR_TEXT("test.real-output"), EDITOR_TEXT("test.reals"),
        EDITOR_TEXT("Real output"), nullptr, 0,
        [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            EditorValue value{}; value.key = EDITOR_TEXT("components"); value.type = EDITOR_REAL_LIST;
            value.reals = {static_cast<double *>(data), 3};
            return sink.write(sink.data, &value, 1);
        }, components, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE};
    EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.reals");
    module.commands = &command; module.commandCount = 1;
    std::string error;
    Check(app.Register(module, error) && app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    const auto result = app.Execute("test.real-output", {app.Scope(), nullptr, 0});
    Check(result.status == EDITOR_OK && result.fields.front().reals.front() == 1.25, "command did not own its real-array output");
    components[0] = 90;
    const EditorValue arguments[] = {{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, result.operation},
        {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE, {}, {}, 0}};
    const auto output = app.Query("application.operation-output", {app.Scope(), arguments, 2});
    const auto reals = std::find_if(output.fields.begin(), output.fields.end(), [](const auto &field) { return field.key == "reals"; });
    Check(output.status == EDITOR_OK && reals != output.fields.end() && reals->reals == std::vector<double>({1.25, -2, 3}),
          "completed operation lost its copied real array");
}
void RecordValues() {
    double numbers[] = {1, 2, 3};
    EditorValue leaf{}; leaf.key = EDITOR_TEXT("leaf"); leaf.type = EDITOR_REAL_LIST; leaf.reals = {numbers, 3};
    EditorValue child{}; child.key = EDITOR_TEXT("child"); child.type = EDITOR_RECORD; child.fields = {&leaf, 1};
    EditorValue root{}; root.key = EDITOR_TEXT("value"); root.type = EDITOR_RECORD; root.fields = {&child, 1};
    std::vector<EditorField> copied;
    Check(CopyEditorFields(copied, &root, 1) == EDITOR_OK, "copy nested record");
    numbers[0] = 99;
    Check(copied.front().fields.front().fields.front().reals.front() == 1, "record retained producer memory");
    auto borrowed = BorrowEditorFields(copied);
    auto moved = std::move(borrowed);
    Check(moved.values.front().fields.data[0].fields.data[0].reals.data[0] == 1, "moving record view invalidated members");
    std::vector<EditorField> roundtrip;
    Check(CopyEditorFields(roundtrip, moved.data(), uint32_t(moved.size())) == EDITOR_OK && roundtrip == copied,
          "nested record roundtrip changed types or values");
    const auto before = copied;
    root.fields = {nullptr, 1};
    Check(CopyEditorFields(copied, &root, 1) == EDITOR_INVALID && copied == before, "invalid record modified collected fields");
    root.fields = {&root, 1};
    std::vector<EditorField> cyclic;
    Check(CopyEditorFields(cyclic, &root, 1) == EDITOR_INVALID && cyclic.empty(), "cyclic record was accepted");
    EditorValue duplicates[] = {leaf, leaf}; root.fields = {duplicates, 2};
    Check(CopyEditorFields(cyclic, &root, 1) == EDITOR_INVALID, "duplicate record keys were accepted");
    root.fields = {&child, 1}; leaf.reals.count = UINT32_MAX;
    Check(CopyEditorFields(cyclic, &root, 1) == EDITOR_INVALID, "nested array bypassed total byte limit");
    leaf.reals.count = 3; numbers[0] = std::numeric_limits<double>::quiet_NaN();
    Check(CopyEditorFields(cyclic, &root, 1) == EDITOR_INVALID, "non-finite nested component was accepted");
    numbers[0] = 1;
    EditorApplication app({});
    const EditorCommandDesc command{sizeof(EditorCommandDesc), EDITOR_TEXT("test.record-output"), EDITOR_TEXT("test.records"),
        EDITOR_TEXT("Record output"), nullptr, 0,
        [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            return sink.write(sink.data, static_cast<EditorValue *>(data), 1);
        }, &root, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE};
    const auto paint = [](void *, const EditorHost *, const EditorValueInput *) -> uint32_t { return EDITOR_INPUT_NONE; };
    const EditorValueEditorDesc editors[] = {
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("test.integer-array"), EDITOR_TEXT("test.records"), EDITOR_TEXT("Integers"), paint, nullptr, EDITOR_INTEGER_LIST},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("test.text-array"), EDITOR_TEXT("test.records"), EDITOR_TEXT("Texts"), paint, nullptr, EDITOR_TEXT_LIST}};
    EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.records");
    module.commands = &command; module.commandCount = 1; module.editors = editors; module.editorCount = 2;
    std::string error;
    Check(app.Register(module, error) && app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    const auto result = app.Execute("test.record-output", {app.Scope(), nullptr, 0});
    Check(result.status == EDITOR_OK && result.fields.front().fields.front().fields.front().reals.front() == 1, "record output was not copied");
    numbers[0] = 88;
    const EditorValue args[] = {{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, result.operation}, {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE, {}, {}, 0}};
    const auto output = app.Query("application.operation-output", {app.Scope(), args, 2});
    const auto field = std::find_if(output.fields.begin(), output.fields.end(), [](const auto &value) { return value.key == "fields"; });
    Check(output.status == EDITOR_OK && field != output.fields.end() && field->fields.front().fields.front().reals.front() == 1,
          "completed operation did not retain nested values");
    std::vector<EditorValue> deep(64);
    for (size_t i = 0; i < deep.size(); ++i) {
        deep[i].key = EDITOR_TEXT("member"); deep[i].type = EDITOR_RECORD;
        if (i + 1 < deep.size()) deep[i].fields = {&deep[i + 1], 1};
    }
    std::vector<EditorField> boundary;
    Check(CopyEditorFields(boundary, deep.data(), 1) == EDITOR_OK, "empty record at the nesting limit was refused");
    EditorValue extra{}; extra.key = EDITOR_TEXT("member"); extra.type = EDITOR_RECORD; deep.back().fields = {&extra, 1};
    std::vector<EditorField> beyond;
    Check(CopyEditorFields(beyond, deep.data(), 1) == EDITOR_INVALID && beyond.empty(), "record exceeded the nesting limit");
}
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
    const EditorValueEditorDesc editors[] = {module.editors[0], module.editors[0]};
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

    EditorApplication nullability({});
    auto nullableModule = TextModule();
    EditorParameter nullableArgument{EDITOR_TEXT("value"), EDITOR_OBJECT, 0, 1};
    auto nullableCommand = commands[0]; nullableCommand.parameters = &nullableArgument; nullableCommand.count = 1;
    nullableModule.commands = &nullableCommand; nullableModule.commandCount = 1;
    nullableArgument.type = EDITOR_INTEGER_VALUE;
    Check(!nullability.Register(nullableModule, error), "non-object nullable argument was accepted");
    nullableArgument.type = EDITOR_OBJECT; nullableArgument.nullable = 2;
    Check(!nullability.Register(nullableModule, error), "unknown nullable value was accepted");
    nullableArgument.nullable = 1;
    Check(nullability.Register(nullableModule, error), "required nullable object was refused");
    EditorApplication inconsistentNullability({});
    EditorParameter strictArgument = nullableArgument; strictArgument.nullable = 0;
    commands[0].parameters = &nullableArgument; commands[0].count = 1;
    commands[1].parameters = &strictArgument; commands[1].count = 1;
    Check(!inconsistentNullability.Register(inconsistent, error), "providers disagreeing on nullability were accepted");

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
    EditorValueEditorDesc chosen{};
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
        Check(argc == 3, "module and environment fixture paths are required");
        Registration();
        VersionedQueries();
        MenuChecks();
        PreviewRegistration();
        OwnedValueDrawing();
        CommandStateReads();
        RealArrays();
        RecordValues();
        CkRuntimeHost runtime;
        std::string error;
        const auto *path = std::getenv("VIRTOOLS_PATH");
        Check(runtime.Start(path ? path : ".", {}, &error), error.c_str());
        Edits(runtime, argv[1]);
        TestDocumentCommands(runtime);
        TestDocumentTransitions(runtime);
        TestOutputCommands(runtime);
        TestWorkspace(runtime);
        TestEventLog();
        TestSceneCommands(runtime);
        TestBuildingBlockQueries(runtime);
        TestDataResources(runtime);
        TestParameterEdits(runtime);
        TestArrayEdits(runtime);
        TestSetupEdits(runtime);
        TestSchematicEdits(runtime);
        TestGraphChangeNotifications(runtime, argv[1]);
        TestObjectCommands(runtime, argv[1]);
        TestResourceCommands(runtime, argv[1]);
        TestExecutionCommands(runtime, argv[1]);
        TestRuntimeProjections(runtime);
        TestEnvironmentCommands(runtime, argv[2]);
        std::cout << "Module registration, object editing, undo and view lifetime checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
