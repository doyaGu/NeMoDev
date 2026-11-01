#include "tests/ui/UiTests.h"
#include "features/objects/ObjectViews.h"
#include "core/graph/ScriptCommands.h"
#include "core/document/BehaviorDescription.h"
#include "core/document/FileCommands.h"
#include "core/document/LinkValidation.h"
#include "ui/render/UiRenderer.h"
#include "ui/schematic/Snapshots.h"
#include "core/document/ToolCommands.h"
#include "core/schematic/SchematicEdits.h"
#include "features/layout/LayoutSceneData.h"
#include <fstream>
#include <chrono>
#include <filesystem>

namespace vdevuitest {
namespace {
int LiveViews() {
    auto &app = App().State().workspace->Application();
    const EditorRequest request{app.Scope(), nullptr, 0};
    const auto result = app.Query("example.views", request);
    return result.status == EDITOR_OK && !result.fields.empty() ? std::stoi(result.fields.front().text) : -1;
}
} // namespace
void RegisterModuleTests(ImGuiTestEngine *engine) {
    auto *test = IM_REGISTER_TEST(engine, "modules", "canvas_queries_and_commands");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        auto &state = App().State(); auto &workspace = *state.workspace; auto &editor = workspace.Application();
        vdev::EditorConfiguration initial; vdev::ObjectRef script{}, graph{}, camera{};
        EditorView schematic = 0, diagnostic = 0;
        size_t steps = 0; bool dirty = false; uint64_t selection = 0;
        OnMainThread(ctx, [&] {
            initial = editor.Configuration(); steps = state.History().UndoCount(); dirty = state.document->Dirty();
            selection = state.objectSelection.Revision();
            auto config = initial;
            for (auto &choice : config.providers)
                if ((choice.kind == "query" && choice.id == "layout.scene") ||
                    (choice.kind == "command" && (choice.id == "layout.set-camera" || choice.id == "schematic.toggle-fold" ||
                                                   choice.id == "schematic.refresh-invalid-links"))) choice.provider = "example.canvas";
            std::string error; IM_CHECK_NO_RET(workspace.Configure(config, error));
            const vdev::EditorClient client(editor.Host(), 0);
            vdev::LayoutSceneData data;
            IM_CHECK_NO_RET(vdev::ReadLayoutScene(client, "cameras", data) && !data.rows.empty());
            IM_CHECK_NO_RET(data.rows.front().name.rfind("External ", 0) == 0);
            camera = data.camera;
        });
        const auto count = [&](const char *key) { return Read<uint64_t>(ctx, [&] {
            return vdev::EditorClient::Integer(editor.Query("example.canvas-counts", {editor.Scope(), nullptr, 0}), key);
        }); };
        const auto reads = count("reads"), cameras = count("cameras"), folds = count("folds"), refreshes = count("refreshes");
        ImVec2 min, max;
        IM_CHECK(Read<bool>(ctx, [&] { return App().Layout().ShownView(min, max); }));
        ctx->WindowFocus("//3D Layout"); ctx->MouseMoveToPos(Center(min, max)); ctx->KeyPress(ImGuiKey_N); ctx->Yield(4);
        IM_CHECK_EQ(count("cameras"), cameras + 1); IM_CHECK(count("reads") > reads);
        IM_CHECK(Read<bool>(ctx, [&] { return App().Layout().View().Camera()->GetID() != camera.object &&
            state.History().UndoCount() == steps && state.document->Dirty() == dirty && state.objectSelection.Revision() == selection; }));
        OnMainThread(ctx, [&] {
            const auto made = vdev::CreateScript(*state.document, state.document->Context()->GetCurrentLevel()->GetID(), "External canvas");
            IM_CHECK_NO_RET(made.ok); script = state.document->Reference(made.created);
            const auto child = vdev::CreateBehaviorGraph(*state.document, made.created, made.created, "External fold", Point(80, 80));
            IM_CHECK_NO_RET(child.ok); graph = state.document->Reference(child.created);
            schematic = workspace.Open("script.schematic"); IM_CHECK_NO_RET(schematic);
            App().Canvas(schematic).ShowScript(script.object);
            App().Canvas(schematic).ToggleFold(script.object, graph.object);
        });
        ctx->Yield(4); IM_CHECK_EQ(count("folds"), folds + 1);
        IM_CHECK(Read<bool>(ctx, [&] { return state.History().UndoCount() == steps + 3 &&
            vdev::FindRecord(state.document->Session(script.object)->Data(), graph.object)->folded; }));
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, [&] { return !vdev::FindRecord(state.document->Session(script.object)->Data(), graph.object)->folded; }));
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), schematic);
            IM_CHECK_NO_RET(client.Command("schematic.open-invalid-links").status == EDITOR_OK);
            diagnostic = workspace.Role("main.invalid-links"); IM_CHECK_NO_RET(diagnostic);
            const auto inspected = workspace.Inspect(diagnostic);
            IM_CHECK_NO_RET(inspected.status == EDITOR_OK && vdev::EditorClient::Integer(inspected, "schematic") == schematic);
        });
        ctx->Yield(4);
        const auto window = "//" + workspace.WindowName(diagnostic);
        ctx->WindowFocus(window.c_str()); ctx->ItemClick((window + "/Refresh/##dialogbutton").c_str()); ctx->Yield(4);
        IM_CHECK_EQ(count("refreshes"), refreshes + 1);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(state.History().UndoCount() == steps + 2);
            IM_CHECK_NO_RET(workspace.Close(diagnostic) && workspace.Close(schematic));
            std::string error; IM_CHECK_NO_RET(workspace.Configure(initial, error));
            const vdev::EditorClient client(editor.Host(), 0);
            IM_CHECK_NO_RET(client.Command("layout.set-camera", {vdev::ObjectArgument("target", camera)}).status == EDITOR_OK);
            IM_CHECK_NO_RET(state.History().Undo() && state.History().Undo());
        });
        ctx->Yield(3);
    };

    test = IM_REGISTER_TEST(engine, "modules", "schematic_diagnostics_keep_their_source_view");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State(); auto &workspace = *state.workspace; auto &editor = workspace.Application();
        EditorView first = 0, second = 0, messages = 0, invalid = 0;
        CK_ID script = 0, senderId = 0; size_t steps = 0;
        OnMainThread(ctx, [&] {
            steps = state.History().UndoCount(); first = workspace.Role("main.schematic");
            const auto made = vdev::CreateScript(*state.document, state.document->Context()->GetCurrentLevel()->GetID(), "Canvas diagnostics");
            IM_CHECK_NO_RET(made.ok); script = made.created;
            const auto child = vdev::CreateBehaviorGraph(*state.document, script, script, "Canvas sender", Point(80, 80));
            IM_CHECK_NO_RET(child.ok); senderId = child.created;
            auto data = state.document->Session(script)->Data(); auto *root = Object<CKBehavior>(script);
            auto *chunk = GenerateInterfaceChunk(data, root); IM_CHECK_NO_RET(chunk); root->SetInterfaceChunk(chunk);
            auto *sender = Object<CKBehavior>(senderId); sender->ModifyFlags(CKBEHAVIOR_MESSAGESENDER, 0);
            const auto type = state.document->Context()->GetMessageManager()->AddMessageType("Canvas diagnostic message");
            auto *input = sender->CreateInputParameter("Message", CKPGUID_MESSAGE);
            auto *value = sender->CreateLocalParameter("Message value", CKPGUID_MESSAGE);
            IM_CHECK_NO_RET(input && value && value->SetValue(&type, sizeof(type)) == CK_OK && input->SetDirectSource(value) == CK_OK);
            state.document->MarkTopologyChanged(); state.document->RefreshScripts();
            IM_CHECK_NO_RET(vdev::EditScriptData(*state.document, script, "Invalid diagnostic fixture", [=](InterfaceData &data) {
                Link link; link.start = LinkEndpoint(script, 0, ENDPOINT_BIN); link.end = LinkEndpoint(UINT32_MAX - 1, 0, ENDPOINT_BIN);
                data.rootBehavior.links.push_back(link);
            }));
            second = workspace.Open("script.schematic"); IM_CHECK_NO_RET(second && second != first);
            App().Canvas(second).ShowScript(script); workspace.Focus(second);
            const vdev::EditorClient client(editor.Host(), second);
            IM_CHECK_NO_RET(client.Command("schematic.refresh-invalid-links").status == EDITOR_OK);
            IM_CHECK_NO_RET(client.Command("schematic.open-invalid-links").status == EDITOR_OK &&
                client.Command("schematic.open-messages").status == EDITOR_OK);
            invalid = workspace.Role("main.invalid-links"); messages = workspace.Role("main.messages");
            IM_CHECK_NO_RET(invalid && messages);
        }); ctx->Yield(6);
        OnMainThread(ctx, [&] {
            const auto a = workspace.Inspect(messages), b = workspace.Inspect(invalid);
            const auto *rows = vdev::EditorClient::Find(b, "rows");
            IM_CHECK_NO_RET(a.status == EDITOR_OK && b.status == EDITOR_OK && rows && rows->fields.size() == 1 &&
                vdev::EditorClient::Integer(a, "schematic") == second && vdev::EditorClient::Integer(b, "schematic") == second);
        });
        const auto messageWindow = "//" + workspace.WindowName(messages), invalidWindow = "//" + workspace.WindowName(invalid);
        ctx->WindowFocus(messageWindow.c_str());
        ctx->ItemClick((messageWindow + "/**/Canvas diagnostic message  (1)").c_str()); ctx->Yield(3);
        ctx->ItemClick((messageWindow + "/**/Canvas sender   (Canvas diagnostics)").c_str()); ctx->Yield(3);
        auto firstSelection = Read<uint64_t>(ctx, [&] { return App().Canvas(first).Navigation().selection.Revision(); });
        ctx->ItemClick((messageWindow + "/Goto").c_str()); ctx->Yield(4);
        IM_CHECK(Read<bool>(ctx, [&] { return App().Canvas(second).Navigation().selection.script == script &&
            App().Canvas(second).Navigation().selection.items.front().object == senderId &&
            App().Canvas(first).Navigation().selection.Revision() == firstSelection; }));
        ctx->WindowFocus(invalidWindow.c_str()); ctx->ItemClick((invalidWindow + "/Delete All").c_str()); ctx->Yield(4);
        IM_CHECK(Read<bool>(ctx, [&] { return state.document->Session(script)->Data().rootBehavior.links.empty() &&
            state.History().UndoCount() == steps + 4; }));
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, [&] { return state.document->Session(script)->Data().rootBehavior.links.size() == 1; }));
        firstSelection = Read<uint64_t>(ctx, [&] { return App().Canvas(first).Navigation().selection.Revision(); });
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(workspace.Close(second)); }); ctx->Yield(3);
        ctx->WindowFocus(messageWindow.c_str()); ctx->ItemClick((messageWindow + "/Goto").c_str()); ctx->Yield(4);
        IM_CHECK(Read<bool>(ctx, [&] { return !vdev::EditorClient::Text(workspace.Inspect(messages), "message").empty(); }));
        IM_CHECK_EQ(Read<uint64_t>(ctx, [&] { return App().Canvas(first).Navigation().selection.Revision(); }), firstSelection);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(workspace.Close(messages) && workspace.Close(invalid));
            IM_CHECK_NO_RET(state.History().Undo() && state.History().Undo() && state.History().Undo());
        }); ctx->Yield(3);
    };

    test = IM_REGISTER_TEST(engine, "modules", "schematic_changes_reach_external_views");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State();
        auto &workspace = *state.workspace;
        auto &editor = workspace.Application();
        vdev::EditorConfiguration initial;
        vdev::SchematicNavigation navigation;
        CK_ID script = 0, graph = 0;
        EditorView first = 0, second = 0, observer = 0;
        size_t steps = 0;
        CKDWORD before = 0;
        OnMainThread(ctx, [&] {
            initial = editor.Configuration();
            first = workspace.Role("main.schematic");
            navigation = App().Canvas(first).Navigation();
            steps = state.History().UndoCount();
            const auto created = vdev::CreateScript(*state.document, state.document->Context()->GetCurrentLevel()->GetID(),
                                                    "Notification view");
            IM_CHECK_NO_RET(created.ok);
            script = created.created;
            const auto child = vdev::CreateBehaviorGraph(*state.document, script, script, "Child graph", Point(80, 80));
            IM_CHECK_NO_RET(child.ok);
            graph = child.created;
            before = state.document->Session(script)->Data().header.color;
            second = workspace.Open("script.schematic");
            IM_CHECK_NO_RET(first && second && first != second);
            App().Canvas(first).ShowScript(script);
            App().Canvas(second).ShowScript(script);
            workspace.Focus(first);
            auto config = initial;
            for (auto &choice : config.providers)
                if (choice.kind == "command" && choice.id == "schematic.script-color-set")
                    choice.provider = "example.schematic";
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
            observer = editor.OpenScope();
            const EditorValue fields[] = {
                {EDITOR_TEXT("target"), EDITOR_OBJECT, state.document->Reference(script)},
                {EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, {}, {}, observer}};
            IM_CHECK_NO_RET(editor.Execute("example.schematic-watch", {editor.Scope(observer), fields, 2}).status == EDITOR_OK);
        });
        ctx->Yield(5);
        const auto count = [&](const char *key) {
            return Read<uint64_t>(ctx, [&] {
                const auto result = editor.Query("example.schematic-state", {editor.Scope(observer), nullptr, 0});
                for (const auto &field : result.fields) if (field.key == key) return field.integer;
                return UINT64_MAX;
            });
        };
        const auto notices = count("notices"), writes = count("writes");
        const auto topology = Read<uint64_t>(ctx, [&] { return state.document->TopologyRevision(); });
        OnMainThread(ctx, [&] { App().Canvas(first).ToggleFold(script, graph); });
        ctx->Yield(4);
        IM_CHECK_EQ(count("notices"), notices + 1);
        IM_CHECK_EQ(Read<uint64_t>(ctx, [&] { return state.document->TopologyRevision(); }), topology);
        IM_CHECK(Read<bool>(ctx, [&] { return vdev::FindRecord(state.document->Session(script)->Data(), graph)->folded; }));
        Undo(ctx);
        IM_CHECK_EQ(count("notices"), notices + 2);
        Redo(ctx);
        IM_CHECK_EQ(count("notices"), notices + 3);
        Undo(ctx);
        IM_CHECK_EQ(count("notices"), notices + 4);

        // The ordinary color dialog must use the selected external command;
        // drafting and cancelling publish no document modification.
        ctx->WindowFocus(("//" + App().SchematicWindow(first)).c_str());
        ctx->Yield(3);
        const auto shown = Read<std::vector<vdev::SchematicCanvas::ShownScript>>(ctx,
            [&] { return App().Canvas(first).ShownScripts(); });
        const auto found = std::find_if(shown.begin(), shown.end(), [&](const auto &item) { return item.script == script; });
        IM_CHECK(found != shown.end());
        const auto open = [&] {
            ctx->MouseMoveToPos(Center(found->headerMin, found->headerMax));
            ctx->MouseClick(1);
            ctx->Yield(3);
            ctx->ItemClick("//$FOCUSED/Set Color");
            ctx->Yield(3);
            ctx->ItemClick("//Script Color/**/##color/##hex/##Text");
            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
            ctx->KeyChars("FF00AA");
            ctx->Yield(3);
        };
        open();
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK_EQ(count("notices"), notices + 4);
        IM_CHECK_EQ(count("writes"), writes);
        open();
        ctx->ItemClick("//Script Color/OK");
        ctx->Yield(4);
        IM_CHECK_EQ(count("writes"), writes + 1);
        IM_CHECK_EQ(count("notices"), notices + 5);
        IM_CHECK_EQ(count("color"), 0xAA00FFu);
        IM_CHECK_EQ(state.History().UndoCount(), steps + 3);
        IM_CHECK_EQ(Read<uint64_t>(ctx, [&] { return state.document->TopologyRevision(); }), topology);
        Undo(ctx);
        IM_CHECK_EQ(count("notices"), notices + 6);
        IM_CHECK_EQ(count("color"), before);
        OnMainThread(ctx, [&] {
            editor.CloseScope(observer);
            IM_CHECK_NO_RET(workspace.Close(second));
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(initial, error));
            IM_CHECK_NO_RET(state.History().Undo() && state.History().Undo());
            App().Canvas(first).Navigation() = navigation;
            App().Canvas(first).RefreshStack();
            workspace.Focus(first);
        });
        ctx->Yield(3);
        IM_CHECK_EQ(state.History().UndoCount(), steps);
    };

    test = IM_REGISTER_TEST(engine, "modules", "rename_cancel_undo_configuration_and_loaded_view");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, SceneRef("level", "").c_str());
        CK_ID object = 0;
        vdev::EditorConfiguration initial;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            initial = state.workspace->Application().Configuration();
            const auto created = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(created.ok);
            object = created.created;
            state.History().Clear();
            state.document->MarkSaved();
            state.History().MarkSavePoint();
        });
        ctx->Yield(3);
        const auto row = ClickLevelRow(ctx, CKCID_3DENTITY);
        IM_CHECK(row && row->object == object);
        const auto before = ObjectName(ctx, object);
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        IM_CHECK(ctx->ItemExists("//Rename##level/##name"));
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == before);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 0);
        ctx->WindowFocus(SceneRef("level", "").c_str());
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        ctx->ItemInputValue("//Rename##level/##name", "Unified Name");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == "Unified Name");
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        Undo(ctx);
        IM_CHECK(ObjectName(ctx, object) == before);
        Redo(ctx);
        IM_CHECK(ObjectName(ctx, object) == "Unified Name");
        EditorView first = 0, second = 0, external = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &workspace = *state.workspace;
            state.objectSelection.Restore(*state.document, {object});
            first = workspace.Open("object.properties");
            second = workspace.Open("object.summary");
            external = workspace.Open("example.object");
            IM_CHECK_NO_RET(first && second && external && first != second && second != external);
            IM_CHECK_NO_RET(LiveViews() == 1);
            auto config = initial;
            for (auto &provider : config.providers)
                if (provider.kind == "editor" && provider.id == "text.single-line") provider.provider = "example.text";
            config.bindings.emplace_back("object.name", "Ctrl+R");
            config.menus.push_back({"object.rename.level", "main.edit", 0});
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        ctx->Yield(4);
        ctx->WindowFocus(SceneRef("level", "").c_str());
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(2);
        IM_CHECK(!ctx->ItemExists("//Rename##level/##name"));
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_R);
        ctx->Yield(3);
        IM_CHECK(ctx->ItemExists("//Rename##level/##name"));
        ctx->ItemInputValue("//Rename##level/##name", "External Editor");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == "External Editor");
        // Moving a menu entry changes its presentation only; it opens the same
        // property editor and submits the same rename command.
        ctx->MenuClick("//##MainMenuBar/Views/Rename###object.rename.level");
        ctx->Yield(3);
        IM_CHECK(ctx->ItemExists("//Rename##workspace/##name"));
        ctx->ItemInputValue("//Rename##workspace/##name", "Moved Menu");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == "Moved Menu");
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &app = state.workspace->Application();
            const auto reference = state.document->Reference(object);
            const EditorValue args[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, reference, {}},
                                        {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("Command")}};
            const EditorRequest request{app.Scope(), args, 2};
            const auto count = state.History().UndoCount();
            IM_CHECK_NO_RET(app.Execute("example.rename", request).status == EDITOR_OK);
            IM_CHECK_NO_RET(state.History().UndoCount() == count + 1);
        });
        ctx->Yield(4);
        IM_CHECK(ObjectName(ctx, object) == "External Command");
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &workspace = *state.workspace;
            workspace.Close(external);
            IM_CHECK_NO_RET(LiveViews() == 0);
            const auto context = state.document->Context();
            const auto token = state.document->Token();
            const auto count = state.History().UndoCount();
            workspace.Close(first);
            workspace.Close(second);
            IM_CHECK_NO_RET(state.document->Context() == context && state.document->Token() == token &&
                            state.History().UndoCount() == count);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(initial, error));
        });
    };
    test = IM_REGISTER_TEST(engine, "modules", "hierarchy_setup_and_property_view_share_edits");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        CK_ID object = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            const auto created = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(created.ok);
            object = created.created;
            state.objectSelection.Restore(*state.document, {object});
            App().OpenSceneView("hierarchy");
            state.History().Clear();
            state.document->MarkSaved();
            state.History().MarkSavePoint();
        });
        ctx->Yield(4);
        ctx->WindowFocus(SceneRef("hierarchy", "").c_str());
        for (int pass = 0; pass < 20; ++pass) {
            const auto rows = App().HierarchyRows();
            const vdev::SceneShownRow *closed = nullptr;
            for (const auto &row : rows)
                if (row.children && !row.open && (!closed || row.depth < closed->depth))
                    closed = &row;
            if (!closed)
                break;
            ctx->MouseMoveToPos(Center(closed->arrowMin, closed->arrowMax));
            ctx->MouseClick(0);
            ctx->Yield(2);
        }
        bool found = false;
        for (const auto &row : App().HierarchyRows()) {
            if (row.object != object)
                continue;
            ctx->MouseMoveToPos(ImVec2(row.arrowMax.x > 0.0f ? row.arrowMax.x + 40.0f : row.min.x + 80.0f,
                                       (row.min.y + row.max.y) * 0.5f));
            ctx->MouseClick(0);
            found = true;
            break;
        }
        IM_CHECK(found);
        const auto original = ObjectName(ctx, object);
        ctx->Yield(3);
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, [] { return App().State().workspace->Application().HasEdits(); }));
        ctx->KeyCharsReplaceEnter("Hierarchy Name");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == "Hierarchy Name");
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        Undo(ctx);
        IM_CHECK(ObjectName(ctx, object) == original);
        ctx->WindowFocus(SceneRef("hierarchy", "").c_str());
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        ctx->WindowClose(SceneRef("hierarchy", "").c_str());
        ctx->Yield(3);
        IM_CHECK(!Read<bool>(ctx, [] { return App().State().workspace->Application().HasEdits(); }));
        IM_CHECK(ShowSetup(ctx, object, "//###setup3d"));
        ctx->WindowFocus("//###setup3d");
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        IM_CHECK(ctx->ItemExists("//###setuprename/##name"));
        ctx->ItemInputValue("//###setuprename/##name", "Setup Name");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == "Setup Name");
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        Undo(ctx);
        IM_CHECK(ObjectName(ctx, object) == original);
        ctx->WindowClose("//###setup3d");
        EditorView view = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            state.objectSelection.Restore(*state.document, {object});
            view = state.workspace->Open("object.properties");
            IM_CHECK_NO_RET(view);
        });
        ctx->Yield(4);
        const auto window = "//###editor-view-" + std::to_string(view);
        ctx->ItemClick((window + "/Rename").c_str());
        ctx->ItemInputValue((window + "/##name").c_str(), "Property Name");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == "Property Name");
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        Undo(ctx);
        ctx->ItemClick((window + "/Rename").c_str());
        ctx->Yield(2);
        ctx->WindowClose(window.c_str());
        ctx->Yield(3);
        IM_CHECK(!Read<bool>(ctx, [] { return App().State().workspace->Application().HasEdits(); }));
        IM_CHECK(ObjectName(ctx, object) == original);
    };
    test = IM_REGISTER_TEST(engine, "modules", "stale_native_target_cannot_be_renamed");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        CK_ID id = 0;
        size_t history = 0;
        EditorView owner = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &document = *state.document;
            auto &workspace = *state.workspace;
            auto *object = document.Context()->CreateObject(CKCID_OBJECT, "Original target");
            IM_CHECK_NO_RET(object);
            id = object->GetID();
            const auto ref = document.Reference(id);
            EditorEdit edit = 0;
            IM_CHECK_NO_RET(workspace.Begin("object.name", ref, edit, owner));
            IM_CHECK_NO_RET(workspace.Application().UpdateEdit(edit, "Wrong target") == EDITOR_OK);
            IM_CHECK_NO_RET(workspace.Finish(edit, true));
            history = state.History().UndoCount();
            IM_CHECK_NO_RET(document.Context()->DestroyObject(id, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK);
            auto *replacement = document.Context()->CreateObject(CKCID_OBJECT, "Replacement target");
            IM_CHECK_NO_RET(replacement && replacement->GetID() == id);
            IM_CHECK_NO_RET(!document.Resolve(ref));
            // Leave the owner alive so rejection depends on native lifetime.
        });
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, id) == "Replacement target");
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == history);
        OnMainThread(ctx, [&] {
            App().State().workspace->CloseOwner(owner);
            IM_CHECK_NO_RET(App().State().document->Context()->DestroyObject(id) == CK_OK);
        });
    };
    test = IM_REGISTER_TEST(engine, "modules", "external_file_dialog_and_failed_replacement");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        vdev::EditorConfiguration original;
        vdev::DocumentToken document{};
        size_t history = 0, views = 0;
        EditorOperation operation = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &workspace = *state.workspace;
            auto &editor = workspace.Application();
            original = editor.Configuration();
            document = state.document->Token(); history = state.History().UndoCount(); views = workspace.ViewCount();
            auto choose = original;
            choose.providers.erase(std::remove_if(choose.providers.begin(), choose.providers.end(), [](const auto &p) {
                return p.kind == "view" && p.id == "document.file-dialogs";
            }), choose.providers.end());
            choose.providers.push_back({"view", "document.file-dialogs", "example.files"});
            const auto flag = [&](uint64_t value) {
                const EditorValue argument{vdev::EditorLiteral("refuse"), EDITOR_INTEGER_VALUE, {}, {}, value};
                return editor.Execute("example.file-view-state", {editor.Scope(), &argument, 1}).status;
            };
            IM_CHECK_NO_RET(flag(1) == EDITOR_OK);
            std::string error;
            IM_CHECK_NO_RET(!workspace.Configure(choose, error));
            IM_CHECK_NO_RET(workspace.ViewCount() == views);
            IM_CHECK_NO_RET(std::any_of(editor.Configuration().providers.begin(), editor.Configuration().providers.end(), [](const auto &p) {
                return p.kind == "view" && p.id == "document.file-dialogs" && p.provider == "builtin.file-dialogs";
            }));
            IM_CHECK_NO_RET(flag(0) == EDITOR_OK);
            IM_CHECK_NO_RET(workspace.Configure(choose, error));
            IM_CHECK_NO_RET(workspace.ViewCount() == views);
            const auto result = editor.Execute("document.open", {editor.Scope(), nullptr, 0});
            IM_CHECK_NO_RET(result.status == EDITOR_QUEUED && result.operation);
            operation = result.operation;
        });
        ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = state.workspace->Application();
            const EditorValue id{vdev::EditorLiteral("operation"), EDITOR_INTEGER_VALUE, {}, {}, operation};
            const auto result = editor.Query("application.operation", {editor.Scope(), &id, 1});
            IM_CHECK_NO_RET(result.status == EDITOR_OK);
            IM_CHECK_NO_RET(std::any_of(result.fields.begin(), result.fields.end(), [](const auto &f) {
                return f.key == "status" && f.integer == EDITOR_CANCELLED;
            }));
            IM_CHECK_NO_RET(state.document->Token() == document && state.History().UndoCount() == history);
            std::string error;
            IM_CHECK_NO_RET(state.workspace->Configure(original, error));
            IM_CHECK_NO_RET(state.workspace->ViewCount() == views);
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "history_command_menu_and_shortcut_configuration");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        vdev::EditorConfiguration previous;
        CK_ID object = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &workspace = *state.workspace;
            state.History().Clear();
            state.document->MarkSaved(); state.History().MarkSavePoint();
            const auto result = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(result.ok);
            object = result.created;
            previous = workspace.Application().Configuration();
            auto config = previous;
            config.bindings.emplace_back("history.undo", "Alt+U");
            config.menus.push_back({"menu.history.undo", "main.edit", 0});
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        ctx->Yield(3);
        ctx->WindowFocus("//3D Layout");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        ctx->Yield(2);
        IM_CHECK(Read<bool>(ctx, [&] { return App().State().document->IsLive(object); }));
        ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_U);
        ctx->Yield(2);
        IM_CHECK(Read<bool>(ctx, [&] { return !App().State().document->IsLive(object); }));
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
        ctx->Yield(2);
        IM_CHECK(Read<bool>(ctx, [&] { return App().State().document->IsLive(object); }));
        ctx->MenuClick("//##MainMenuBar/Views/Undo (3d Layout\\/Schematic only)");
        ctx->Yield(2);
        IM_CHECK(Read<bool>(ctx, [&] { return !App().State().document->IsLive(object); }));
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            std::string error;
            IM_CHECK_NO_RET(state.workspace->Configure(previous, error));
            IM_CHECK_NO_RET(state.History().Redo());
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "registered_creation_selection_and_stale_delete");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        vdev::EditorConfiguration previous;
        bool showSetup = false;
        CK_ID object = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            previous = state.workspace->Application().Configuration();
            showSetup = state.preferences.Bool("3D Layout\\Auto Show Setup");
            state.preferences.SetBool("3D Layout\\Auto Show Setup", false);
            auto configured = previous;
            for (auto &choice : configured.providers)
                if (choice.kind == "command" && choice.id == "object.create-frame3d") choice.provider = "example.objects";
            std::string error;
            IM_CHECK_NO_RET(state.workspace->Configure(configured, error));
            state.History().Clear();
        });
        ctx->WindowFocus("//3D Layout");
        ctx->KeyPress(ImGuiKey_3);
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto *created = state.document->Context()->GetObjectByName("External Frame");
            IM_CHECK_NO_RET(created != nullptr);
            object = created->GetID();
            IM_CHECK_NO_RET(state.History().UndoCount() == 1);
            state.objectSelection.Restore(*state.document, {object});
        });
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        ctx->Yield(2);
        IM_CHECK(Read<bool>(ctx, [&] { return !App().State().document->IsLive(object); }));
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
        ctx->Yield(2);
        OnMainThread(ctx, [&] { App().State().objectSelection.Restore(*App().State().document, {object}); });
        ctx->KeyPress(ImGuiKey_Space);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_D);
        ctx->Yield(2);
        IM_CHECK(Read<bool>(ctx, [&] { return App().State().objectSelection.Locked() && App().State().objectSelection.Contains(object); }));
        ctx->KeyPress(ImGuiKey_Space);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_D);
        ctx->Yield(2);
        IM_CHECK(Read<bool>(ctx, [] { return App().State().objectSelection.Objects().empty() && App().State().History().UndoCount() == 1; }));
        OnMainThread(ctx, [&] { App().State().objectSelection.Restore(*App().State().document, {object}); });
        ctx->KeyPress(ImGuiKey_Delete);
        ctx->Yield(3);
        IM_CHECK(ctx->GetWindowByRef("//###DeleteOptions") != nullptr);
        OnMainThread(ctx, [&] {
            auto &document = *App().State().document;
            const auto old = document.Reference(object);
            auto *render = App().Layout().View().RenderContext();
            auto *renderObject = CKRenderObject::Cast(document.Resolve(old));
            // NONOTIFY skips the render manager. Keep this fixture's render
            // list valid while testing a command target's native lifetime.
            if (render && renderObject) render->RemoveObject(renderObject);
            IM_CHECK_NO_RET(!render || !renderObject || !render->IsObjectAttached(renderObject));
            IM_CHECK_NO_RET(document.Context()->DestroyObject(object, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK);
            auto *replacement = document.Context()->CreateObject(CKCID_3DENTITY, "Delete replacement");
            IM_CHECK_NO_RET(replacement && replacement->GetID() == object && document.Reference(object) != old);
        });
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, object) == "Delete replacement");
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            std::string error;
            IM_CHECK_NO_RET(state.workspace->Configure(previous, error));
            state.preferences.SetBool("3D Layout\\Auto Show Setup", showSetup);
            state.History().Clear();
            state.document->MarkSaved(); state.History().MarkSavePoint();
            const auto oldDocument = state.document->Token();
            auto &editor = state.workspace->Application();
            IM_CHECK_NO_RET(editor.Execute("document.new", {editor.Scope(), nullptr, 0}).status == EDITOR_QUEUED);
            state.FinishRuntimeOperations();
            IM_CHECK_NO_RET(state.document->IsOpen() && state.document->Token() != oldDocument && !state.runtimeOperation.Busy());
        });
    };
    test = IM_REGISTER_TEST(engine, "modules", "resource_and_export_commands_use_registered_file_view");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        namespace fs = std::filesystem;
        const auto root = fs::temp_directory_path() /
            ("resource-ui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        IM_CHECK(fs::create_directory(root));
        struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
        const auto nmo = (root / "object.nmo").u8string(), nms = (root / "script.nms").u8string();
        CK_ID object = 0, script = 0;
        EditorOperation nmoOperation = 0, nmsOperation = 0, loadOperation = 0;
        size_t history = 0;
        vdev::DocumentToken token{};
        std::vector<vdev::ObjectRef> selection;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = state.workspace->Application();
            const auto created = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(created.ok);
            object = created.created;
            const auto createdScript = vdev::CreateScript(*state.document, object, "Exported Script");
            IM_CHECK_NO_RET(createdScript.ok);
            script = createdScript.created;
            state.objectSelection.Restore(*state.document, {object});
            selection = state.objectSelection.References();
            history = state.History().UndoCount();
            token = state.document->Token();
            const auto ref = state.document->Reference(object);
            const EditorValue args[] = {
                {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {&ref, 1}},
                {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, vdev::EditorText(nmo)},
                {EDITOR_TEXT("ask-options"), EDITOR_INTEGER_VALUE, {}, {}, 1}};
            const auto result = editor.Execute("object.export", {editor.Scope(), args, 3});
            IM_CHECK_NO_RET(result.status == EDITOR_QUEUED);
            nmoOperation = result.operation;
        });
        ctx->Yield(4);
        IM_CHECK(ctx->ItemExists("//###LevelSaveAs/Save..."));
        ctx->ItemClick("//###LevelSaveAs/Hide script representation in Schematic view");
        ctx->ItemClick("//###LevelSaveAs/Save...");
        ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(fs::exists(nmo) && state.document->Token() == token && state.History().UndoCount() == history &&
                            state.objectSelection.References() == selection);
            auto &editor = state.workspace->Application();
            const auto ref = state.document->Reference(script);
            const EditorValue args[] = {
                {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {&ref, 1}},
                {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, vdev::EditorText(nms)},
                {EDITOR_TEXT("ask-description"), EDITOR_INTEGER_VALUE, {}, {}, 1}};
            const auto result = editor.Execute("script.export", {editor.Scope(), args, 3});
            IM_CHECK_NO_RET(result.status == EDITOR_QUEUED);
            nmsOperation = result.operation;
        });
        ctx->Yield(5);
        IM_CHECK(ctx->ItemExists("//###behaviorinfo/##author"));
        ctx->ItemInputValue("//###behaviorinfo/##author", "UI Author");
        ctx->ItemInputValue("//###behaviorinfo/##version", "2.5");
        ctx->ItemClick("//###behaviorinfo/Hide script representation in schematic view");
        ctx->ItemClick("//###behaviorinfo/OK");
        ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = state.workspace->Application();
            vdev::BehaviorDescription info;
            IM_CHECK_NO_RET(fs::exists(nms) && vdev::ReadBehaviorDescription(nms, info) && info.author == "UI Author" && info.version == 2.5f);
            IM_CHECK_NO_RET(state.History().UndoCount() == history && state.objectSelection.References() == selection);
            const auto ref = state.document->Reference(object);
            const auto file = vdev::EditorText(nms);
            const uint64_t cid = CKCID_BEHAVIOR;
            const EditorValue args[] = {
                {EDITOR_TEXT("paths"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {&file, 1}},
                {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {&cid, 1}},
                {EDITOR_TEXT("targets"), EDITOR_OBJECT_LIST, {}, {}, 0, {&ref, 1}}};
            const auto result = editor.Execute("example.load-resources", {editor.Scope(), args, 3});
            IM_CHECK_NO_RET(result.status == EDITOR_QUEUED);
            loadOperation = result.operation;
        });
        ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = state.workspace->Application();
            IM_CHECK_NO_RET(Object<CKBeObject>(object)->GetScriptCount() == 2 && state.History().UndoCount() == history + 1 &&
                            state.objectSelection.References() == selection);
            for (const auto operation : {nmoOperation, nmsOperation, loadOperation}) {
                const EditorValue argument{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, operation};
                const auto result = editor.Query("application.operation", {editor.Scope(), &argument, 1});
                IM_CHECK_NO_RET(result.status == EDITOR_OK && std::any_of(result.fields.begin(), result.fields.end(), [](const auto &f) {
                    return f.key == "status" && f.integer == EDITOR_OK;
                }));
            }
            IM_CHECK_NO_RET(state.History().Undo() && Object<CKBeObject>(object)->GetScriptCount() == 1);
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "runtime_controls_share_commands_providers_and_bindings");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        vdev::EditorConfiguration previous;
        std::vector<vdev::EditorField> time;
        size_t history = 0;
        uint64_t plays = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = state.workspace->Application();
            previous = editor.Configuration();
            auto selected = previous;
            for (auto &provider : selected.providers)
                if (provider.kind == "command" && provider.id == "runtime.play") provider.provider = "example.execution";
            selected.bindings.emplace_back("runtime.toggle-trace", "Alt+T");
            std::string error;
            IM_CHECK_NO_RET(state.workspace->Configure(selected, error));
            editor.Execute("runtime.open-debugger", {editor.Scope(), nullptr, 0});
            history = state.History().UndoCount();
            time = editor.Query("runtime.time", {editor.Scope(), nullptr, 0}).fields;
            const EditorValue value{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE, {}, {}, 1};
            IM_CHECK_NO_RET(editor.Execute("runtime.set-max-frames", {editor.Scope(), &value, 1}).status == EDITOR_OK);
            plays = editor.Query("example.play-count", {editor.Scope(), nullptr, 0}).fields.front().integer;
        });
        ctx->Yield(4);
        ctx->ItemClick("//##StatusBar/play/##bitmap");
        ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = state.workspace->Application();
            IM_CHECK_NO_RET(state.debugger->Frames() == 1 && state.debugger->GetState() == vdev::Debugger::State::Paused);
            IM_CHECK_NO_RET(editor.Query("example.play-count", {editor.Scope(), nullptr, 0}).fields.front().integer == plays + 1);
        });
        ctx->ItemClick("//##StatusBar/step/##bitmap");
        ctx->Yield(3);
        IM_CHECK(Read<int>(ctx, [] { return App().State().debugger->Frames(); }) == 2);
        ctx->ItemClick("//##StatusBar/rewind/##bitmap");
        ctx->Yield(3);
        IM_CHECK(Read<int>(ctx, [] { return App().State().debugger->Frames(); }) == 0);
        ctx->WindowFocus(RuntimeRef("debugger", "").c_str());
        ctx->ItemClick(RuntimeRef("debugger", "/Debug Inside Frames").c_str());
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, [] { return App().State().debugger->trace && App().State().debugger->insideFrames; }));
        ctx->WindowFocus(SchematicRef().c_str());
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_T);
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, [] { return App().State().debugger->trace; }));
        ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_T);
        ctx->Yield(3);
        IM_CHECK(!Read<bool>(ctx, [] { return App().State().debugger->trace; }));
        ctx->ItemClick("//##StatusBar/play/##bitmap", 1);
        ctx->Yield(3);
        IM_CHECK(ctx->ItemExists("//###TimeSettings/##scale"));
        ctx->ItemInput("//###TimeSettings/##scale");
        ctx->KeyCharsReplace("1.5");
        ctx->ItemClick("//###TimeSettings/OK/##dialogbutton");
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = state.workspace->Application();
            const auto settings = editor.Query("runtime.time", {editor.Scope(), nullptr, 0});
            IM_CHECK_NO_RET(settings.status == EDITOR_OK && std::any_of(settings.fields.begin(), settings.fields.end(), [](const auto &f) {
                return f.key == "scale" && f.real == 1.5;
            }));
            IM_CHECK_NO_RET(state.History().UndoCount() == history);
            std::vector<EditorValue> restore;
            for (const auto &field : time)
                restore.push_back({vdev::EditorText(field.key), field.type, {}, {}, field.integer, {}, {}, {}, field.real});
            IM_CHECK_NO_RET(editor.Execute("runtime.set-time", {editor.Scope(), restore.data(), uint32_t(restore.size())}).status == EDITOR_OK);
            const EditorValue disabled{EDITOR_TEXT("enabled"), EDITOR_INTEGER_VALUE, {}, {}, 0};
            editor.Execute("runtime.set-inside-frames", {editor.Scope(), &disabled, 1});
            const EditorValue frames{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE, {}, {}, 0};
            editor.Execute("runtime.set-max-frames", {editor.Scope(), &frames, 1});
            state.workspace->Close(state.workspace->Role("main.debugger"));
            std::string error;
            IM_CHECK_NO_RET(state.workspace->Configure(previous, error));
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "web_print_inputs_and_owned_schematic_pages");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        namespace fs = std::filesystem;
        Settle(ctx);
        const auto root = fs::temp_directory_path() / ("output-ui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        IM_CHECK(fs::create_directory(root));
        struct Cleanup { fs::path root; ~Cleanup() { std::error_code ec; fs::remove_all(root, ec); } } cleanup{root};
        const auto html = (root / "page.htm").u8string(), pages = (root / "pages").u8string();
        vdev::EditorConfiguration previous;
        EditorOperation web = 0, print = 0;
        size_t history = 0;
        bool dirty = false;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &editor = state.workspace->Application();
            state.document->MarkSaved(); state.History().MarkSavePoint();
            IM_CHECK_NO_RET(state.workspace->Application().Execute("document.new", {state.workspace->Application().Scope(), nullptr, 0}).status == EDITOR_QUEUED);
            state.FinishRuntimeOperations();
            const auto frame = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(frame.ok);
            const auto script = vdev::CreateScript(*state.document, frame.created, "Printed Script");
            IM_CHECK_NO_RET(script.ok);
            const unsigned char green[] = {0, 200, 0, 255, 0, 200, 0, 255, 0, 200, 0, 255, 0, 200, 0, 255};
            void *bitmap = vdev::BitmapFromPixels(green, 2, 2);
            IM_CHECK_NO_RET(bitmap);
            state.document->KeepBitmap(bitmap);
            IM_CHECK_NO_RET(vdev::SetScriptSnapshot(*state.document, script.created, bitmap));
            previous = editor.Configuration();
            history = state.History().UndoCount(); dirty = state.document->Dirty();
        });
        ctx->Yield(6);
        OnMainThread(ctx, [&] {
            auto &editor = App().State().workspace->Application();
            const EditorValue values[] = {{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, vdev::EditorText(html)},
                {EDITOR_TEXT("ask-options"), EDITOR_INTEGER_VALUE, {}, {}, 1}};
            const auto result = editor.Execute("example.export-web", {editor.Scope(), values, 2});
            IM_CHECK_NO_RET(result.status == EDITOR_QUEUED); web = result.operation;
        });
        ctx->Yield(3);
        IM_CHECK(ctx->ItemExists("//###CreateWebPage/##title"));
        ctx->ItemInput("//###CreateWebPage/##title"); ctx->KeyCharsReplace("Registered Web Page");
        ctx->ItemInput("//###CreateWebPage/##width"); ctx->KeyCharsReplace("640");
        ctx->ItemInput("//###CreateWebPage/##height"); ctx->KeyCharsReplace("480");
        ctx->ItemClick("//###CreateWebPage/##cmo");
        ctx->ItemClick("//###CreateWebPage/OK/##dialogbutton");
        ctx->Yield(4); OnMainThread(ctx, [] {});
        IM_CHECK(fs::exists(fs::u8path(html)) && fs::exists(root / "page.cmo"));
        std::ifstream in(fs::u8path(html), std::ios::binary);
        const std::string page{std::istreambuf_iterator<char>(in), {}};
        IM_CHECK(page.find("<TITLE>Registered Web Page</TITLE>") != std::string::npos);
        IM_CHECK(page.find("WIDTH=\"640\" HEIGHT=\"480\"") != std::string::npos);
        IM_CHECK(!vdev::IsPlayerFile((root / "page.cmo").u8string()));
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &editor = state.workspace->Application();
            IM_CHECK_NO_RET(state.History().UndoCount() == history && state.document->Dirty() == dirty);
            const EditorValue value{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, vdev::EditorText(pages)};
            const auto result = editor.Execute("example.print", {editor.Scope(), &value, 1});
            IM_CHECK_NO_RET(result.status == EDITOR_QUEUED); print = result.operation;
        });
        ctx->Yield(3);
        IM_CHECK(ctx->ItemExists("//###PrintParameters/##pagesH"));
        ctx->KeyPress(ImGuiKey_Escape); ctx->Yield(3);
        IM_CHECK(!fs::exists(root / "pages" / "page1.png"));
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &editor = state.workspace->Application();
            const EditorValue id{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, print};
            const auto result = editor.Query("application.operation", {editor.Scope(), &id, 1});
            IM_CHECK_NO_RET(std::any_of(result.fields.begin(), result.fields.end(), [](const auto &f) { return f.key == "status" && f.integer == EDITOR_CANCELLED; }));
            const EditorValue value{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, vdev::EditorText(pages)};
            const auto started = editor.Execute("example.print", {editor.Scope(), &value, 1});
            IM_CHECK_NO_RET(started.status == EDITOR_QUEUED); print = started.operation;
        });
        ctx->Yield(3);
        ctx->ItemInput("//###PrintParameters/##pagesH"); ctx->KeyCharsReplace("2");
        ctx->ItemInput("//###PrintParameters/##pagesV"); ctx->KeyCharsReplace("2");
        ctx->ItemClick("//###PrintParameters/##fillShapes");
        ctx->ItemClick("//###PrintParameters/OK/##dialogbutton"); ctx->Yield(4);
        for (int i = 1; i <= 4; ++i) {
            std::vector<unsigned char> pixels; int w = 0, h = 0;
            IM_CHECK(vdev::ReadImageRgba((root / "pages" / ("page" + std::to_string(i) + ".png")).u8string(), pixels, w, h));
            IM_CHECK(w > 0 && h > 0 && pixels.size() == size_t(w) * h * 4);
            bool opaque = true; for (size_t at = 3; at < pixels.size(); at += 4) opaque &= pixels[at] == 255;
            IM_CHECK(opaque);
        }
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &editor = state.workspace->Application();
            IM_CHECK_NO_RET(state.History().UndoCount() == history && state.document->Dirty() == dirty);
            for (const auto operation : {web, print}) {
                const EditorValue id{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, operation};
                const auto result = editor.Query("application.operation", {editor.Scope(), &id, 1});
                IM_CHECK_NO_RET(std::any_of(result.fields.begin(), result.fields.end(), [](const auto &f) { return f.key == "status" && f.integer == EDITOR_OK; }));
            }
            auto selected = previous;
            for (auto &provider : selected.providers)
                if (provider.kind == "view" && provider.id == "document.output-dialogs") provider.provider = "example.outputs";
            std::string error;
            IM_CHECK_NO_RET(state.workspace->Configure(selected, error));
            IM_CHECK_NO_RET(editor.Execute("document.export-web", {editor.Scope(), nullptr, 0}).status == EDITOR_QUEUED);
        });
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &editor = state.workspace->Application();
            IM_CHECK_NO_RET(editor.Query("output.input", {editor.Scope(), nullptr, 0}).status == EDITOR_NO_CHANGE);
            std::string error; IM_CHECK_NO_RET(state.workspace->Configure(previous, error));
        });
        // The actual print renderer retains models and cloned bitmaps after
        // the original document/view has disappeared.
        std::unique_ptr<vdev::SchematicPrintContent> snapshot;
        OnMainThread(ctx, [&] { snapshot = App().Canvas().CapturePrint(); IM_CHECK_NO_RET(bool(snapshot)); });
        OnMainThread(ctx, [&] {
            auto &state = App().State(); state.document->MarkSaved(); state.History().MarkSavePoint();
            IM_CHECK_NO_RET(state.workspace->Application().Execute("document.new", {state.workspace->Application().Scope(), nullptr, 0}).status == EDITOR_QUEUED);
            state.FinishRuntimeOperations();
            std::vector<unsigned char> pixels; std::string error;
            vdev::SchematicPrint options; options.bitmaps = true;
            IM_CHECK_NO_RET(snapshot->Render(0, 0, 600, 500, 1, options, pixels, error));
            IM_CHECK_NO_RET(pixels.size() == 600u * 500u * 4);
            size_t ink = 0, green = 0;
            for (size_t i = 0; i < pixels.size(); i += 4) {
                if (pixels[i] < 240 || pixels[i+1] < 240 || pixels[i+2] < 240) ++ink;
                if (int(pixels[i+1]) > int(pixels[i])+40 && int(pixels[i+1]) > int(pixels[i+2])+40) ++green;
            }
            IM_CHECK_NO_RET(ink > 100 && green > 0);
            snapshot.reset();
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "document_switch_failure_restores_live_views_and_selections");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &editor = state.workspace->Application();
            const auto frame = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(frame.ok);
            const auto script = vdev::CreateScript(*state.document, frame.created, "Retained Script");
            IM_CHECK_NO_RET(script.ok);
            state.document->MarkSaved(); state.History().MarkSavePoint();
            IM_CHECK_NO_RET(vdev::RenameLevelObject(*state.document, state.document->Reference(frame.created), "Redo after failed switch").ok);
            IM_CHECK_NO_RET(state.History().Undo());
            state.objectSelection.Restore(*state.document, {frame.created}, true);
            state.Schematic().selection.Set(script.created, vdev::Selection::HeaderOf(script.created));
        });
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &editor = state.workspace->Application();
            const auto context = state.document->Context(); const auto token = state.document->Token();
            const auto objectSelection = state.objectSelection;
            const auto selection = state.Schematic().selection;
            const auto undo = state.History().UndoCount(); const auto redo = state.History().RedoLabel();
            const auto revision = state.document->TopologyRevision(); const auto dirty = state.document->Dirty();
            IM_CHECK_NO_RET(editor.Execute("runtime.play", {editor.Scope(), nullptr, 0}).status == EDITOR_OK);
            struct Views final : vdev::DocumentFiles::Views {
                vdev::EditorState &state;
                vdev::DocumentFiles::Views *original;
                int mode = 0, detached = 0;
                bool released = false;
                explicit Views(vdev::EditorState &state) : state(state), original(state.documentViews) { state.documentViews = this; }
                ~Views() { state.documentViews = original; }
                bool DocumentInteractionActive() const override { return mode == 3 || (mode == 2 && released) || original->DocumentInteractionActive(); }
                void DetachDocument(bool replaceContext) override {
                    ++detached; original->DetachDocument(replaceContext); released = true;
                    if (mode != 2) throw std::runtime_error("Injected failure after releasing actual views");
                }
                void RestoreDocument() override {
                    released = false; original->RestoreDocument();
                    if (mode == 1) throw std::runtime_error("Injected view recovery report");
                }
            } views(state);
            for (int mode : {0, 1, 2, 3}) {
                views.mode = mode;
                const auto status = editor.CommandState("document.new", {editor.Scope(), nullptr, 0});
                const auto result = editor.Execute("document.new", {editor.Scope(), nullptr, 0});
                if (mode == 3) IM_CHECK_NO_RET(status == EDITOR_REFUSED && result.status == EDITOR_REFUSED);
                else {
                    IM_CHECK_NO_RET(status == EDITOR_OK && result.status == EDITOR_QUEUED);
                    state.FinishRuntimeOperations();
                    const EditorValue operation{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, result.operation};
                    const auto completed = editor.Query("application.operation", {editor.Scope(), &operation, 1});
                    IM_CHECK_NO_RET(std::any_of(completed.fields.begin(), completed.fields.end(), [](const auto &f) { return f.key == "status" && f.integer == EDITOR_ERROR; }));
                }
                IM_CHECK_NO_RET(state.document->Context() == context && state.document->Token() == token);
                IM_CHECK_NO_RET(state.document->TopologyRevision() == revision && state.document->Dirty() == dirty);
                IM_CHECK_NO_RET(state.History().UndoCount() == undo && state.History().CanRedo() && state.History().RedoLabel() == redo);
                IM_CHECK_NO_RET(state.Schematic().selection.script == selection.script && state.Schematic().selection.items == selection.items);
                IM_CHECK_NO_RET(state.objectSelection.References() == objectSelection.References() && state.objectSelection.Locked() == objectSelection.Locked() && state.objectSelection.Revision() == objectSelection.Revision());
                IM_CHECK_NO_RET(state.debugger->GetState() == vdev::Debugger::State::Running);
            }
            IM_CHECK_NO_RET(views.detached == 3);
            editor.Execute("runtime.pause", {editor.Scope(), nullptr, 0});
        });
        ctx->Yield(4);
        IM_CHECK(Read<bool>(ctx, [] { return App().Layout().UsesGpuViewport(); }));
    };

}
} // namespace vdevuitest
