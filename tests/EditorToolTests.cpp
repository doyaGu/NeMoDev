#include "application/EditorProfile.h"
#include "workspace/EditorWorkspace.h"
#include "workspace/EditorClient.h"
#include "features/schematic/SchematicGeometry.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/document/ScriptSession.h"
#include "core/graph/ScriptCommands.h"
#include "core/schematic/SchematicEdits.h"
#include "core/runtime/CkRuntimeHost.h"
#include "editor/ImGuiDrawing.h"
#include <limits>
#include <algorithm>
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
std::string Text(EditorString s) { return {s.data ? s.data : "", s.size}; }
EditorStatus Idle(void *, const EditorHost *, const EditorRequest *, const EditorToolInput *, uint32_t *flags) {
    *flags = 0;
    return EDITOR_OK;
}
bool Builtins(EditorApplication &app, std::string &error) {
    EditorViewDesc view{};
    view.size = sizeof(view);
    view.imguiVersion = EDITOR_IMGUI_VERSION;
    view.id = EDITOR_TEXT("test.viewport");
    view.provider = EDITOR_TEXT("builtin.test");
    view.title = EDITOR_TEXT("Test Viewport");
    view.area = EDITOR_WORKSPACE;
    view.create = [](void *, const EditorHost *, EditorView) -> void * { return new int; };
    view.draw = [](void *, const EditorHost *, EditorView, EditorViewFrame *) {};
    view.destroy = [](void *p) { delete static_cast<int *>(p); };
    view.defaultTool = EDITOR_TEXT("test.select");
    EditorViewDesc other = view;
    other.id = EDITOR_TEXT("test.other-view");
    other.defaultTool = {};
    const EditorViewDesc views[] = {view, other};
    EditorToolDesc tools[3]{};
    tools[0].size = sizeof(EditorToolDesc);
    tools[0].id = EDITOR_TEXT("test.select");
    tools[0].provider = EDITOR_TEXT("builtin.test");
    tools[0].title = EDITOR_TEXT("Select");
    tools[0].view = view.id;
    tools[0].input = Idle;
    tools[1] = tools[0];
    tools[1].id = EDITOR_TEXT("test.resize");
    tools[2] = tools[0];
    tools[2].id = EDITOR_TEXT("test.disabled");
    tools[2].state = [](void *, const EditorHost *, const EditorRequest *) { return EDITOR_REFUSED; };
    const EditorToolbarLocation locations[] = {{EDITOR_TEXT("test.tools"), EDITOR_TEXT("Tools"), view.id},
                                               {EDITOR_TEXT("test.other-tools"), EDITOR_TEXT("Other Tools"), other.id}};
    const EditorToolbarItemDesc item{sizeof(EditorToolbarItemDesc),
                                     {},
                                     EDITOR_TEXT("test.button"),
                                     EDITOR_TEXT("builtin.toolbar"),
                                     locations[0].id,
                                     EDITOR_TEXT("Select"),
                                     EDITOR_TEXT("test.select"),
                                     {},
                                     nullptr,
                                     0,
                                     5,
                                     1,
                                     55,
                                     19};
    const EditorCommandDesc command{
        sizeof(EditorCommandDesc),
        EDITOR_TEXT("test.other-action"),
        EDITOR_TEXT("builtin.test"),
        EDITOR_TEXT("Other View Action"),
        nullptr,
        0,
        [](void *, const EditorHost *, const EditorRequest *, EditorSink) { return EDITOR_OK; },
        nullptr,
        EDITOR_WORKSPACE,
        EDITOR_SESSION_CHANGE,
        nullptr,
        nullptr,
        nullptr,
        other.id};
    const EditorBindingDesc bindings[] = {
        {command.id, EDITOR_TEXT("R"), other.id},
        {EDITOR_TEXT("test.select"), EDITOR_TEXT("Space"), view.id, EDITOR_KEY_RELEASED},
        {EDITOR_TEXT("test.resize"), EDITOR_TEXT("R"), view.id}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("test.tools");
    module.views = views;
    module.viewCount = 2;
    module.tools = tools;
    module.toolCount = 3;
    module.toolbars = locations;
    module.toolbarCount = 2;
    module.toolbarItems = &item;
    module.toolbarItemCount = 1;
    module.bindings = bindings;
    module.bindingCount = 3;
    module.commands = &command;
    module.commandCount = 1;
    return app.Register(module, error);
}
} // namespace
void TestEditorTools(vdev::CkRuntimeHost &runtime, const std::string &modulePath) {
    using namespace vdev;
    Document document(runtime);
    std::string error;
    Check(document.New(&error), error.c_str());
    const auto script = CreateScript(document, document.Context()->GetCurrentLevel()->GetID(), "Tool Script");
    Check(script.ok, "create tool test Script");
    document.History().Clear();
    document.MarkSaved();
    document.History().MarkSavePoint();
    EditorApplication app([&] { return &document; });
    EditorWorkspace workspace(app);
    SchematicGeometry geometry([&] { return &document; });
    EditorConfiguration config;
    config.providers = {{"command", "test.resize", "external.tools"}, {"toolbar", "test.button", "external.toolbar"}};
    struct SessionAction {
        Document &doc;
        CK_ID script;
        EditorOperation child = 0;
    } action{document, script.created};
    const EditorCommandDesc actions[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("test.set-script-color"), EDITOR_TEXT("builtin.test"),
         EDITOR_TEXT("Set Script Color"), nullptr, 0,
         [](void *p, const EditorHost *, const EditorRequest *, EditorSink) {
             auto &action = *static_cast<SessionAction *>(p);
             return EditScriptData(action.doc, action.script, "Set Script Color",
                                   [](InterfaceData &data) { data.header.color ^= 0x112233; })
                        ? EDITOR_OK
                        : EDITOR_ERROR;
         },
         &action, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("test.color-action"), EDITOR_TEXT("builtin.test"),
         EDITOR_TEXT("Color Action"), nullptr, 0,
         [](void *p, const EditorHost *host, const EditorRequest *r, EditorSink) {
             return host->execute(host->data, EDITOR_TEXT("test.set-script-color"), r,
                                  {p, [](void *p, const EditorValue *values, uint32_t count) {
                                       for (uint32_t i = 0; i < count; ++i)
                                           if (Text(values[i].key) == "operation")
                                               static_cast<SessionAction *>(p)->child = values[i].integer;
                                       return EDITOR_OK;
                                   }});
         },
         &action, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE}};
    EditorModuleDesc actionModule{};
    actionModule.size = sizeof(actionModule);
    actionModule.version = EDITOR_VERSION;
    actionModule.id = EDITOR_TEXT("test.session-actions");
    actionModule.commands = actions;
    actionModule.commandCount = 2;
    Check(app.Register(actionModule, error), error.c_str());
    const EditorParameter blocked{EDITOR_TEXT("blocked"), EDITOR_INTEGER_VALUE};
    const EditorQueryDesc guarded{
        sizeof(EditorQueryDesc),
        EDITOR_TEXT("test.tool-query"),
        EDITOR_TEXT("builtin.test"),
        nullptr,
        0,
        [](void *, const EditorHost *host, const EditorRequest *r, EditorSink sink) {
            host->cancelTool(host->data, r->scope.view);
            const auto status = host->selectTool(host->data, r->scope, EDITOR_TEXT("test.select"));
            const EditorValue result{EDITOR_TEXT("blocked"), EDITOR_INTEGER_VALUE, {}, {}, status == EDITOR_REFUSED};
            return sink.write(sink.data, &result, 1);
        },
        nullptr,
        EDITOR_QUERY_EACH_READ,
        EDITOR_WORKSPACE,
        &blocked,
        1};
    EditorModuleDesc guardedModule{};
    guardedModule.size = sizeof(guardedModule);
    guardedModule.version = EDITOR_VERSION;
    guardedModule.id = EDITOR_TEXT("test.tool-query");
    guardedModule.queries = &guarded;
    guardedModule.queryCount = 1;
    Check(app.Register(guardedModule, error), error.c_str());
    Check(workspace.Register(error) && Builtins(app, error) && geometry.Register(app, error) &&
              app.Load(modulePath, error) && app.Activate(EDITOR_IMGUI_VERSION, error, config),
          error.c_str());
    app.Attach(document.Token(), document.TopologyRevision(), true, true);
    const auto target = document.Reference(script.created);
    app.SetSelection(target);
    const auto a = workspace.Open("test.viewport", "test.first"), b = workspace.Open("test.viewport", "test.second"),
               other = workspace.Open("test.other-view");
    Check(a && b && other && a != b, "open independent tool views");
    Check(Text(app.ActiveTool(a)->id) == "test.select" && app.SelectTool(app.Scope(b), "test.resize") == EDITOR_OK,
          "select registered external tool");
    Check(Text(app.ActiveTool(a)->id) == "test.select" && Text(app.ActiveTool(b)->provider) == "external.tools",
          "tool selection escaped its viewport");
    Check(app.Bindings().size() == 3 && app.BindingPhase("test.select") == EDITOR_KEY_RELEASED &&
              app.ActionView("test.resize") == "test.viewport" &&
              app.ActionView("test.other-action") == "test.other-view",
          "same chord did not remain scoped to different view types");
    Check(app.SelectTool(app.Scope(a), "test.disabled") == EDITOR_REFUSED &&
              app.Execute("test.disabled", {app.Scope(a)}).status == EDITOR_REFUSED &&
              Text(app.ActiveTool(a)->id) == "test.select",
          "tool selection bypassed the command's enabled state");
    EditorKey key;
    Check(ReadEditorKey("Ctrl+MouseLeft", key) && key.modifiers == 1 && ReadEditorKey("KeypadAdd", key) &&
              !ReadEditorKey("MouseWheel", key),
          "input chord names were not validated");
    app.RestoreTool(a, "missing.module-tool");
    Check(app.ToolChoice(a) == "missing.module-tool" && Text(app.ActiveTool(a)->id) == "test.select",
          "missing tool did not retain its choice and use the view default");
    app.SelectTool(app.Scope(a), "test.select");
    const auto source = app.Scope(b);
    auto input = [&](unsigned pressed, unsigned buttons, unsigned released, float y, bool focused = true) {
        EditorToolInput event{};
        event.size = sizeof(event);
        event.phase = EDITOR_TOOL_FRAME;
        event.focused = focused;
        event.hovered = focused;
        event.pressed = pressed;
        event.buttons = buttons;
        event.released = released;
        event.y = y;
        return app.ToolInput(source, event);
    };
    const EditorClient sessionClient(app.Host(), b);
    const auto sessionResult = sessionClient.Command("test.color-action");
    const auto documentResult = sessionClient.Completion(action.child);
    Check(sessionResult.status == EDITOR_OK && action.child && action.child != sessionResult.operation &&
              documentResult.status == EDITOR_OK && documentResult.documentRevision == document.ChangeRevision() &&
              document.History().UndoCount() == 1,
          "session action lost its independent document command completion");
    Check(document.History().Undo() && !document.Dirty(), "session action did not preserve one-step Undo");
    document.History().Clear();
    document.History().MarkSavePoint();
    app.Attach(document.Token(), document.TopologyRevision(), true, true);
    const auto before = document.Session(script.created)->Data().header.vSize;
    const auto unchanged = [&] {
        Check(!document.Session(script.created)->HasDisplayPreview() &&
                  document.Session(script.created)->Data().header.vSize == before &&
                  document.History().UndoCount() == 0 && !document.Dirty() && app.Selection() == target,
              "tool cancellation changed document, selection or history");
    };
    Check(input(1, 1, 0, 0) == EDITOR_OK && input(0, 1, 0, 80) == EDITOR_OK && app.ToolCaptured(b) &&
              !app.ToolCaptured(a) && document.Session(script.created)->HasDisplayPreview(),
          "external tool preview did not capture its viewport");
    Check(EditorClient::Integer(EditorClient(app.Host(), b).Query("test.tool-query"), "blocked") == 1 &&
              app.ToolCaptured(b) && document.Session(script.created)->HasDisplayPreview(),
          "read-only query canceled or replaced an active tool");
    auto invalid = config;
    invalid.bindings = {{"test.select", "R"}};
    Check(!workspace.Configure(invalid, error) && Text(app.ActiveTool(b)->provider) == "external.tools",
          "configuration replaced a captured tool");
    Check(input(0, 0, 0, 80, false) == EDITOR_NO_CHANGE && !app.ToolCaptured(b), "focus loss retained tool capture");
    unchanged();
    Check(input(1, 1, 0, 0) == EDITOR_OK && input(0, 1, 0, 60) == EDITOR_OK && input(0, 0, 1, 60) == EDITOR_OK,
          "complete external tool drag");
    app.EndFrame();
    Check(document.History().UndoCount() == 1 && document.Session(script.created)->Data().header.vSize == before + 60,
          "continuous external tool did not make one Undo step");
    Check(document.History().Undo() && !document.Dirty(), "Undo did not restore original Script");
    app.Attach(document.Token(), document.TopologyRevision(), true, true);
    unchanged();
    const EditorClient commands(app.Host(), b);
    for (unsigned failure : {1u, 2u, 4u}) {
        Check(commands.Command("test.tool-failure", {IntegerArgument("mode", failure)}).status == EDITOR_OK,
              "inject tool failure");
        const auto status = input(1, 1, 0, 0);
        Check(status != EDITOR_OK && status != EDITOR_NO_CHANGE && !app.ToolCaptured(b),
              "tool exception or invalid flags retained capture");
        unchanged();
    }
    commands.Command("test.tool-failure", {IntegerArgument("mode", 3)});
    Check(app.SelectTool(source, "test.alternate") == EDITOR_ERROR && Text(app.ActiveTool(b)->id) == "test.resize",
          "failed tool entry replaced the selected tool");
    commands.Command("test.tool-failure", {IntegerArgument("mode", 0)});
    Check(input(1, 1, 0, 0) == EDITOR_OK && input(0, 1, 0, 40) == EDITOR_OK, "start tool before switching");
    Check(app.SelectTool(source, "test.select") == EDITOR_OK, "switch to builtin tool");
    unchanged();
    Check(app.SelectTool(source, "test.resize") == EDITOR_OK && input(1, 1, 0, 0) == EDITOR_OK,
          "start tool before closing");
    Check(workspace.Close(b), "close captured viewport");
    unchanged();
    Check(app.ToolInput(source, EditorToolInput{sizeof(EditorToolInput), EDITOR_TOOL_FRAME}) == EDITOR_STALE,
          "closed scope accepted input");
    const auto reopened = workspace.Open("test.viewport", "test.second");
    Check(reopened && reopened != b && Text(app.ActiveTool(reopened)->id) == "test.resize",
          "named viewport lost its selected tool");
    WorkspaceState saved;
    Check(workspace.Save(saved, error), error.c_str());
    Check(std::any_of(saved.views.begin(), saved.views.end(),
                      [](const auto &v) { return v.role == "test.second" && v.tool == "test.resize"; }),
          "workspace did not save the tool choice");
    EditorStatus thread = EDITOR_OK;
    std::thread worker([&] {
        EditorToolInput event{};
        event.size = sizeof(event);
        event.phase = EDITOR_TOOL_FRAME;
        thread = app.ToolInput(app.Scope(a), event);
    });
    worker.join();
    Check(thread != EDITOR_OK, "wrong thread dispatched a tool");
    // An unrelated property/dialog draft with the same view owner is not a tool gesture.
    const auto setupClient = EditorClient(app.Host(), a);
    const auto projection =
        setupClient.Query("schematic.geometry", {ObjectArgument("target", target), IntegerArgument("mode", 1)});
    const auto args = BorrowEditorFields(projection.fields);
    EditorEdit unrelated = 0;
    Check(app.BeginCommand("schematic.edit-geometry", {app.Scope(a), args.data(), uint32_t(args.size())}, unrelated) ==
              EDITOR_OK,
          "begin non-tool draft");
    EditorField dimensions{"size", EDITOR_REAL_LIST};
    dimensions.reals = {0, before + 80};
    EditorField value{"value", EDITOR_RECORD};
    value.fields = {dimensions};
    const std::vector<EditorField> owned{value};
    const auto values = BorrowEditorFields(owned);
    Check(app.UpdateEdit(unrelated, *values.data()) == EDITOR_OK &&
              document.Session(script.created)->HasDisplayPreview(),
          "preview non-tool draft");
    app.CancelTool(a);
    Check(document.Session(script.created)->HasDisplayPreview() && app.HasEdits(),
          "tool cancellation also canceled a non-tool draft");
    Check(app.FinishEdit(unrelated, false) == EDITOR_CANCELLED, "cancel non-tool draft");
    unchanged();
    auto update = config;
    ToolbarPlacement moved;
    moved.id = "test.button";
    moved.fields = ToolbarLocation | ToolbarPosition | ToolbarAction | ToolbarTitle;
    moved.location = "test.other-tools";
    moved.x = 90;
    moved.y = 3;
    moved.action = "test.other-action";
    moved.title = "Moved";
    update.toolbarItems.push_back(moved);
    ToolbarPlacement added;
    added.id = "test.added";
    added.location = "test.tools";
    added.action = "test.select";
    added.x = 100;
    added.y = 1;
    added.width = 60;
    added.height = 19;
    added.fields = ToolbarLocation | ToolbarAction | ToolbarPosition | ToolbarSize;
    update.toolbarItems.push_back(added);
    update.bindings = {{"test.resize", "Ctrl+K"}};
    Check(workspace.Configure(update, error), error.c_str());
    Check(app.ToolbarItems().size() == 2 && app.ToolbarItems().back().location == "test.other-tools" &&
              app.ToolbarItems().back().action == "test.other-action",
          "toolbar add, move and replace did not apply");
    const auto valid = app.Configuration();
    for (unsigned bad = 0; bad < 5; ++bad) {
        auto broken = valid;
        if (bad == 0) broken.toolbarItems.back().action = "missing.action";
        if (bad == 1) broken.toolbarItems.back().width = std::numeric_limits<float>::quiet_NaN();
        if (bad == 2) broken.toolbarItems.back().location = "missing.toolbar";
        if (bad == 3) broken.toolbarItems.push_back(broken.toolbarItems.back());
        if (bad == 4) broken.bindings = {{"test.select", "Ctrl+K"}, {"test.resize", "Ctrl+K"}};
        Check(!workspace.Configure(broken, error) && app.ToolbarItems().size() == 2 &&
                  app.Configuration().bindings == valid.bindings,
              "invalid tool configuration replaced live settings");
    }
    workspace.ConfigurationWriter([](const auto &, const auto &, std::string &message) {
        message = "injected write failure";
        return false;
    });
    auto rejected = valid;
    rejected.toolbarItems[0].x += 100;
    Check(!workspace.Configure(rejected, error) && app.ToolbarItems().back().x == 90,
          "failed profile write retained toolbar changes");
    workspace.ConfigurationWriter({});
    auto hidden = valid;
    hidden.toolbarItems[0].fields |= ToolbarVisibility;
    hidden.toolbarItems[0].visible = false;
    Check(workspace.Configure(hidden, error) && app.ToolbarItems().size() == 1, "toolbar removal did not apply");
    auto alternate = valid;
    alternate.providers[0].provider = "builtin.test";
    alternate.providers[1].provider = "builtin.toolbar";
    Check(workspace.Configure(alternate, error) && Text(app.ActiveTool(reopened)->provider) == "builtin.test",
          "builtin and external tool providers did not share selection");
    EditorProfile profile;
    Check(profile.Capture(
              {}, valid, {}, [&](const auto &choice) { return app.ProviderModule(choice); }, error),
          error.c_str());
    std::string json;
    Check(profile.Encode(json, error), error.c_str());
    EditorProfile restored;
    Check(restored.Decode(json, "/tmp/tool-profile.json", error) && restored.Contributions().toolbarItems.size() == 2 &&
              restored.Contributions().bindings == valid.bindings,
          "toolbar and input settings did not round-trip");
    workspace.CloseAll();
    app.Stop();

    // A toolbar owned by another module remains usable when an optional
    // command module is disabled, and returns to the same action on startup.
    for (const bool enabled : {false, true}) {
        EditorApplication optional([&] { return &document; });
        const std::vector<EditorModuleSource> sources{
            {"optional command",
             [](auto &editor, auto &error) {
                 const EditorCommandDesc command{
                     sizeof(EditorCommandDesc),
                     EDITOR_TEXT("test.optional"),
                     EDITOR_TEXT("builtin.test"),
                     EDITOR_TEXT("Optional Action"),
                     nullptr,
                     0,
                     [](void *, const EditorHost *, const EditorRequest *, EditorSink) { return EDITOR_OK; },
                     nullptr,
                     EDITOR_WORKSPACE,
                     EDITOR_SESSION_CHANGE};
                 EditorModuleDesc module{};
                 module.size = sizeof(module);
                 module.version = EDITOR_VERSION;
                 module.id = EDITOR_TEXT("optional.actions");
                 module.commands = &command;
                 module.commandCount = 1;
                 return editor.Register(module, error);
             }},
            {"independent toolbar", [](auto &editor, auto &error) {
                 const EditorToolbarLocation location{EDITOR_TEXT("test.optional-tools"), EDITOR_TEXT("Tools"), {}};
                 EditorToolbarItemDesc item{};
                 item.size = sizeof(item);
                 item.id = EDITOR_TEXT("test.optional-button");
                 item.provider = EDITOR_TEXT("builtin.test");
                 item.location = location.id;
                 item.action = EDITOR_TEXT("test.optional");
                 item.width = 80;
                 item.height = 19;
                 EditorModuleDesc module{};
                 module.size = sizeof(module);
                 module.version = EDITOR_VERSION;
                 module.id = EDITOR_TEXT("independent.toolbar");
                 module.toolbars = &location;
                 module.toolbarCount = 1;
                 module.toolbarItems = &item;
                 module.toolbarItemCount = 1;
                 return editor.Register(module, error);
             }}};
        Check(optional.Assemble(sources, EDITOR_IMGUI_VERSION, error, {}, {{"optional.actions", enabled}}),
              error.c_str());
        optional.Attach(document.Token(), document.TopologyRevision(), true, true);
        Check(optional.ToolbarItems().size() == 1 &&
                  (optional.CommandState("test.optional", {optional.Scope()}) == EDITOR_OK) == enabled,
              "toolbar did not retain a disabled action or restore its enabled command");
    }
}
