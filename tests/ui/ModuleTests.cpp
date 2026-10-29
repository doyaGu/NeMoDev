#include "tests/ui/UiTests.h"
#include "features/objects/ObjectViews.h"

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
    auto *test = IM_REGISTER_TEST(engine, "modules", "rename_cancel_undo_configuration_and_loaded_view");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        CK_ID object = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
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
        ctx->WindowFocus("//Level Manager");
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
            state.objectSelection.Set(*state.document, {object});
            first = workspace.Open("object.properties");
            second = workspace.Open("object.summary");
            external = workspace.Open("example.object");
            IM_CHECK_NO_RET(first && second && external && first != second && second != external);
            IM_CHECK_NO_RET(LiveViews() == 1);
            auto config = vdev::DefaultObjectConfiguration();
            config.providers.front().provider = "example.text";
            config.bindings.emplace_back("object.name", "Ctrl+R");
            config.menus.push_back({"object.rename.level", "main.edit", 0});
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        ctx->Yield(4);
        ctx->WindowFocus("//Level Manager");
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
            IM_CHECK_NO_RET(workspace.Configure(vdev::DefaultObjectConfiguration(), error));
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
            state.objectSelection.Set(*state.document, {object});
            state.showHierarchy = true;
            state.History().Clear();
            state.document->MarkSaved();
            state.History().MarkSavePoint();
        });
        ctx->Yield(4);
        ctx->WindowFocus("//Hierarchy Manager");
        for (int pass = 0; pass < 20; ++pass) {
            const auto rows = App().Managers().ShownHierarchyRows();
            const vdev::ManagerWindows::ShownRow *closed = nullptr;
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
        for (const auto &row : App().Managers().ShownHierarchyRows()) {
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
        ctx->WindowFocus("//Hierarchy Manager");
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        ctx->WindowClose("//Hierarchy Manager");
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
            state.objectSelection.Set(*state.document, {object});
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
}
} // namespace vdevuitest
