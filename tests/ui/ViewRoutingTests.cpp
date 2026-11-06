#include "tests/ui/UiTests.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/TopologyCommands.h"
#include <cstring>

namespace vdevuitest {
namespace {
void Choose(vdev::EditorConfiguration &config, const char *kind, const char *id, const char *provider) {
    for (auto &choice : config.providers)
        if (choice.kind == kind && choice.id == id) { choice.provider = provider; return; }
    config.providers.push_back({kind, id, provider});
}
vdev::ObjectRef Shown(EditorView view) {
    const auto data = vdev::EditorClient(App().WorkspaceForTests().Application().Host(), view).Inspect(view);
    const auto *target = vdev::EditorClient::Find(data, "target");
    return target ? target->object : vdev::ObjectRef{};
}
}
void RegisterViewRoutingTests(ImGuiTestEngine *engine) {
    auto *test = IM_REGISTER_TEST(engine, "viewrouting", "schematic_replaced_view_receives_script_show");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto &editor = workspace.Application();
            auto &doc = *App().State().document;
            const auto created = vdev::CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(), "Routed Script");
            IM_CHECK(created.ok);
            const auto target = doc.Reference(created.created);
            const auto token = doc.Token();
            const auto revision = doc.ChangeRevision();
            const auto history = doc.History().UndoCount();
            const auto selected = App().State().objectSelection.References();
            const auto original = editor.Configuration();
            auto config = original;
            Choose(config, "view", "script.schematic", "routing.schematic");
            std::string error;
            IM_CHECK(workspace.Configure(config, error));
            const auto view = workspace.Role("main.schematic");
            IM_CHECK(view);
            const vdev::EditorClient client(editor.Host(), view);
            IM_CHECK(client.Command("script.navigate", {vdev::ObjectArgument("target", target)}).status == EDITOR_OK);
            IM_CHECK(client.Command("script.show", {vdev::ObjectArgument("target", target),
                                                    vdev::IntegerArgument("focus", 1)}).status == EDITOR_OK);
            IM_CHECK(Shown(view) == target);
            IM_CHECK(doc.Token() == token && doc.ChangeRevision() == revision &&
                     doc.History().UndoCount() == history && App().State().objectSelection.References() == selected);
            IM_CHECK(workspace.Configure(original, error));
        });
    };
    test = IM_REGISTER_TEST(engine, "viewrouting", "layout_foreign_source_keeps_active_camera");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto &doc = *App().State().document;
            const auto foreign = workspace.Open("object.summary");
            IM_CHECK(foreign);
            auto *camera = doc.Context()->CreateObject(CKCID_CAMERA, "Routing camera");
            IM_CHECK(camera);
            doc.Include(camera->GetID());
            auto &layout = App().Layout();
            const auto previous = layout.View().Camera()->GetID();
            const auto history = doc.History().UndoCount();
            const auto revision = doc.ChangeRevision();
            const vdev::EditorClient client(workspace.Application().Host(), foreign);
            const auto result = client.Command("layout.set-camera", {vdev::ObjectArgument("target", doc.Reference(camera->GetID()))});
            IM_CHECK(result.status == EDITOR_STALE || result.status == EDITOR_REFUSED);
            IM_CHECK(layout.View().Camera()->GetID() == previous &&
                     doc.History().UndoCount() == history && doc.ChangeRevision() == revision);
            IM_CHECK(workspace.Close(foreign));
        });
    };
    test = IM_REGISTER_TEST(engine, "viewrouting", "schematic_replaced_instances_keep_navigation_targets");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        EditorView first = 0, second = 0;
        vdev::ObjectRef a{}, b{};
        vdev::EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto &doc = *App().State().document;
            const auto level = doc.Context()->GetCurrentLevel()->GetID();
            const auto one = vdev::CreateScript(doc, level, "First navigation");
            const auto two = vdev::CreateScript(doc, level, "Second navigation");
            IM_CHECK(one.ok && two.ok);
            a = doc.Reference(one.created); b = doc.Reference(two.created);
            original = workspace.Application().Configuration();
            auto config = original;
            Choose(config, "view", "script.schematic", "routing.schematic");
            std::string error;
            IM_CHECK(workspace.Configure(config, error));
            first = workspace.Role("main.schematic");
            second = workspace.Open("script.schematic", "test.routing.second");
            IM_CHECK(first && second && first != second && workspace.Focus(second));
        });
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto &editor = workspace.Application();
            const auto &doc = *App().State().document;
            const auto revision = doc.ChangeRevision();
            const auto history = doc.History().UndoCount();
            IM_CHECK(vdev::EditorClient(editor.Host(), first).Command("script.navigate", {vdev::ObjectArgument("target", a)}).status == EDITOR_OK);
            IM_CHECK(vdev::EditorClient(editor.Host(), second).Command("script.show", {
                vdev::ObjectArgument("target", b), vdev::IntegerArgument("focus", 1)}).status == EDITOR_OK);
            IM_CHECK(Shown(first) == a && Shown(second) == b);
            IM_CHECK(vdev::EditorClient(editor.Host(), 0).Command("script.navigate", {vdev::ObjectArgument("target", a)}).status == EDITOR_OK);
            IM_CHECK(Shown(first) == a && Shown(second) == a);
            const auto foreign = workspace.Open("object.summary");
            const auto refused = vdev::EditorClient(editor.Host(), foreign).Command("script.navigate", {vdev::ObjectArgument("target", b)});
            IM_CHECK(refused.status == EDITOR_STALE || refused.status == EDITOR_INVALID || refused.status == EDITOR_REFUSED);
            IM_CHECK(Shown(first) == a && Shown(second) == a);
            IM_CHECK(workspace.Close(foreign) && workspace.Close(second));
            const EditorRequest old{{doc.Token(), second}, nullptr, 0};
            const auto args = std::vector<EditorValue>{vdev::ObjectArgument("target", b)};
            auto stale = old; stale.values = args.data(); stale.count = uint32_t(args.size());
            IM_CHECK(editor.Execute("script.show", stale).status == EDITOR_STALE);
            IM_CHECK(Shown(first) == a && doc.ChangeRevision() == revision && doc.History().UndoCount() == history);
            std::string error;
            IM_CHECK(workspace.Configure(original, error));
        });
    };
    test = IM_REGISTER_TEST(engine, "viewrouting", "layout_replaced_instances_use_selected_command_provider");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto &editor = workspace.Application();
            auto &doc = *App().State().document;
            auto *camera = doc.Context()->CreateObject(CKCID_CAMERA, "External camera");
            IM_CHECK(camera);
            doc.Include(camera->GetID());
            const auto object = doc.Reference(camera->GetID());
            const auto revision = doc.ChangeRevision();
            const auto history = doc.History().UndoCount();
            const auto selected = App().State().objectSelection.References();
            const auto original = editor.Configuration();
            const auto old = workspace.Role("main.layout");
            auto config = original;
            Choose(config, "view", "layout.viewport", "routing.layout");
            Choose(config, "command", "layout.set-camera", "routing.layout");
            std::string error;
            IM_CHECK(workspace.Configure(config, error));
            const auto first = workspace.Role("main.layout");
            const auto second = workspace.Open("layout.viewport", "test.routing.layout");
            IM_CHECK(first && second && first != second && first != old);
            const vdev::EditorClient a(editor.Host(), first), b(editor.Host(), second);
            const auto result = b.Command("layout.set-camera", {vdev::ObjectArgument("target", object)});
            IM_CHECK(result.status == EDITOR_OK && vdev::EditorClient::Integer(result, "view") == second);
            const auto readCamera = [](const vdev::EditorClient &client) {
                const auto data = client.Query("routing.layout-camera");
                const auto *field = vdev::EditorClient::Find(data, "camera");
                return field ? field->object : vdev::ObjectRef{};
            };
            IM_CHECK(!readCamera(a) && readCamera(b) == object);
            IM_CHECK(workspace.Focus(second));
            const auto defaultTarget = vdev::EditorClient(editor.Host(), 0).Command("layout.set-camera", {
                vdev::ObjectArgument("target", object)});
            IM_CHECK(defaultTarget.status == EDITOR_OK && vdev::EditorClient::Integer(defaultTarget, "view") == second);
            const EditorValue target = vdev::ObjectArgument("target", object);
            const EditorRequest stale{{doc.Token(), old}, &target, 1};
            IM_CHECK(editor.Execute("layout.set-camera", stale).status == EDITOR_STALE);
            const EditorRequest otherDocument{{{doc.Token().value + 1}, 0}, &target, 1};
            IM_CHECK(editor.CommandState("layout.set-camera", otherDocument) == EDITOR_STALE &&
                     editor.Execute("layout.set-camera", otherDocument).status == EDITOR_STALE);
            IM_CHECK(vdev::EditorClient(editor.Host(), old).Command("layout.set-camera", {
                vdev::ObjectArgument("target", object)}).status == EDITOR_STALE);
            const EditorRequest request{b.Scope(), &target, 1, EDITOR_TEXT("builtin.layout")};
            IM_CHECK(editor.Execute("layout.set-camera", request).status == EDITOR_STALE);
            const auto snapshot = b.Command("snapshot.take");
            IM_CHECK(snapshot.status == EDITOR_STALE || snapshot.status == EDITOR_REFUSED);
            IM_CHECK(!readCamera(a) && readCamera(b) == object &&
                     doc.ChangeRevision() == revision && doc.History().UndoCount() == history &&
                     App().State().objectSelection.References() == selected);
            IM_CHECK(workspace.Close(second) && workspace.Configure(original, error));
        });
        ctx->Yield(4);
        IM_CHECK(Read<bool>(ctx, [] { return App().Layout().UsesGpuViewport(); }));
    };
    test = IM_REGISTER_TEST(engine, "viewrouting", "schematic_replaced_view_receives_parameter_navigation");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto &editor = workspace.Application();
            auto &doc = *App().State().document;
            const auto script = vdev::CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(), "Parameter navigation");
            IM_CHECK(script.ok);
            const auto parameter = vdev::AddLocalParameter(doc, script.created, script.created, "Parameter", CKPGUID_INT, {20, 20});
            IM_CHECK(parameter.ok);
            const auto original = editor.Configuration();
            auto config = original;
            Choose(config, "view", "script.schematic", "routing.schematic");
            std::string error;
            IM_CHECK(workspace.Configure(config, error));
            const auto view = workspace.Role("main.schematic");
            const auto revision = doc.ChangeRevision();
            const auto history = doc.History().UndoCount();
            const auto selected = App().State().objectSelection.References();
            const vdev::EditorClient client(editor.Host(), view);
            const auto args = std::vector<EditorValue>{vdev::ObjectArgument("target", doc.Reference(parameter.created))};
            const auto source = client.Query("schematic.shortcuts", {
                args.front(), vdev::IntegerArgument("mode", 0)});
            const auto *rows = vdev::EditorClient::Find(source, "rows");
            IM_CHECK(source.status == EDITOR_OK && rows && !rows->fields.empty());
            IM_CHECK(client.Command("script.show-parameter", args).status == EDITOR_OK);
            IM_CHECK(Shown(view) == doc.Reference(script.created));
            IM_CHECK(doc.ChangeRevision() == revision && doc.History().UndoCount() == history &&
                     App().State().objectSelection.References() == selected);
            IM_CHECK(workspace.Configure(original, error));
        });
    };
    test = IM_REGISTER_TEST(engine, "viewrouting", "layout_invalid_sources_cannot_query_edit_or_capture");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto &editor = workspace.Application();
            auto &doc = *App().State().document;
            auto &layout = App().Layout();
            const auto valid = workspace.Role("main.layout");
            const auto foreign = workspace.Open("object.summary");
            const auto anonymous = editor.OpenScope();
            IM_CHECK(valid && foreign && anonymous && layout.UsesGpuViewport());
            const vdev::EditorClient native(editor.Host(), valid);
            const auto camera = doc.Reference(layout.View().Camera()->GetID());
            const auto revision = doc.ChangeRevision();
            const auto topology = doc.TopologyRevision();
            const auto history = doc.History().UndoCount();
            const auto redo = doc.History().CanRedo();
            const auto dirty = doc.Dirty();
            const auto selected = App().State().objectSelection.References();
            const auto locked = App().State().objectSelection.Locked();
            const auto readbacks = layout.View().ReadbackFrames();
            const auto uploads = layout.ViewportUploadsForTests();
            const auto *texture = layout.GpuViewportTextureForTests();
            for (const auto *query : {"layout.input", "layout.curve-tool", "layout.creation", "layout.presentation"}) {
                IM_CHECK(native.Query(query).status == EDITOR_OK);
                for (const auto source : {foreign, anonymous}) {
                    const auto response = vdev::EditorClient(editor.Host(), source).Query(query);
                    IM_CHECK(response.status == EDITOR_STALE || response.status == EDITOR_REFUSED);
                }
            }
            for (const auto source : {foreign, anonymous}) {
                const vdev::EditorClient client(editor.Host(), source);
                const auto tool = client.Command("layout.set-curve-tool", {
                    vdev::ObjectArgument("target", {}), vdev::IntegerArgument("tool", 0), vdev::IntegerArgument("plane", 0)});
                IM_CHECK(tool.status == EDITOR_STALE || tool.status == EDITOR_REFUSED);
                const auto highlight = client.Command("layout.highlight-drop", {vdev::ObjectArgument("target", {})});
                IM_CHECK(highlight.status == EDITOR_STALE || highlight.status == EDITOR_REFUSED);
                const auto snapshot = client.Command("snapshot.take");
                IM_CHECK(snapshot.status == EDITOR_STALE || snapshot.status == EDITOR_REFUSED);
                const auto args = std::vector<EditorValue>{vdev::ObjectArgument("target", camera)};
                const EditorRequest request{client.Scope(), args.data(), uint32_t(args.size())};
                IM_CHECK(editor.CommandState("layout.set-camera", request) != EDITOR_OK);
            }
            IM_CHECK(doc.Reference(layout.View().Camera()->GetID()) == camera &&
                     doc.ChangeRevision() == revision && doc.TopologyRevision() == topology &&
                     doc.History().UndoCount() == history && doc.History().CanRedo() == redo && doc.Dirty() == dirty &&
                     App().State().objectSelection.References() == selected && App().State().objectSelection.Locked() == locked);
            IM_CHECK(layout.View().ReadbackFrames() == readbacks && layout.ViewportUploadsForTests() == uploads &&
                     layout.GpuViewportTextureForTests() == texture && layout.UsesGpuViewport());
            editor.CloseScope(anonymous);
            IM_CHECK(workspace.Close(foreign));
        });
    };
    test = IM_REGISTER_TEST(engine, "viewrouting", "layout_default_gesture_keeps_initial_view_after_focus_change");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        EditorView second = 0;
        OnMainThread(ctx, [&] {
            second = App().WorkspaceForTests().Open("layout.viewport", "routing.gesture");
            IM_CHECK(second);
        });
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto &editor = workspace.Application();
            auto &doc = *App().State().document;
            auto &first = App().Layout();
            auto &other = *App().Layout(second);
            const auto a = first.View().Camera()->GetWorldMatrix();
            const auto b = other.View().Camera()->GetWorldMatrix();
            const auto revision = doc.ChangeRevision();
            const auto history = doc.History().UndoCount();
            const auto motion = [](double x, double y) {
                vdev::EditorField position{"position", EDITOR_REAL_LIST};
                position.reals = {x, y};
                vdev::EditorField value{"value", EDITOR_RECORD};
                value.fields = {position, {"move", EDITOR_INTEGER_VALUE, {}, {}, 1}};
                return value;
            };
            const auto start = motion(100, 100);
            vdev::EditorField origin{"start", EDITOR_REAL_LIST};
            origin.reals = {100, 100};
            const std::vector<vdev::EditorField> fields{
                {"target", EDITOR_OBJECT, doc.Reference(other.View().Camera()->GetID())},
                {"kind", EDITOR_INTEGER_VALUE}, {"action", EDITOR_INTEGER_VALUE, {}, {}, 2},
                {"objects", EDITOR_OBJECT_LIST}, origin, {"options", EDITOR_RECORD}, start};
            const auto values = vdev::BorrowEditorFields(fields);
            const vdev::EditorClient client(editor.Host(), 0);
            IM_CHECK(workspace.Focus(second));
            EditorEdit edit = 0;
            IM_CHECK(client.BeginCommand("layout.edit-gesture", {values.data(), values.data() + values.size()}, edit) == EDITOR_OK && edit);
            IM_CHECK(workspace.Focus(workspace.Role("main.layout")));
            const auto moved = std::vector<vdev::EditorField>{motion(150, 130)};
            const auto update = vdev::BorrowEditorFields(moved);
            IM_CHECK(editor.UpdateEdit(edit, update.data()[0]) == EDITOR_OK);
            IM_CHECK(!std::memcmp(&a, &first.View().Camera()->GetWorldMatrix(), sizeof(a)) &&
                     std::memcmp(&b, &other.View().Camera()->GetWorldMatrix(), sizeof(b)));
            IM_CHECK(editor.FinishEdit(edit, false) == EDITOR_CANCELLED);
            IM_CHECK(!std::memcmp(&a, &first.View().Camera()->GetWorldMatrix(), sizeof(a)) &&
                     !std::memcmp(&b, &other.View().Camera()->GetWorldMatrix(), sizeof(b)) &&
                     doc.ChangeRevision() == revision && doc.History().UndoCount() == history);
            IM_CHECK(workspace.Close(second));
        });
    };
    test = IM_REGISTER_TEST(engine, "viewrouting", "level_navigation_uses_replaced_schematic");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        vdev::EditorConfiguration original;
        EditorView schematic = 0;
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            original = workspace.Application().Configuration();
            auto config = original;
            Choose(config, "view", "script.schematic", "routing.schematic");
            std::string error;
            IM_CHECK(workspace.Configure(config, error));
            schematic = workspace.Role("main.schematic");
        });
        ctx->Yield(4);
        ctx->WindowFocus(SceneRef("level", "").c_str());
        ctx->Yield(3);
        const auto row = ClickLevelRow(ctx, CKCID_BEHAVIOR, true);
        IM_CHECK(row.has_value());
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            IM_CHECK(Shown(schematic) == App().State().document->Reference(row->object));
            IM_CHECK(workspace.Focused() == schematic);
            std::string error;
            IM_CHECK(workspace.Configure(original, error));
        });
    };
}
} // namespace vdevuitest
