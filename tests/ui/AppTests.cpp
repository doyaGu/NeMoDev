// "app": the editor on its new composition: its windows, the File menu,
// the dialogs of the Options and Help menus and their shortcuts.

#include "tests/ui/UiTests.h"

namespace vdevuitest {

namespace {

bool WindowShown(ImGuiTestContext *ctx, const char *name) {
    ImGuiWindow *window = ctx->GetWindowByRef(name);
    return window && window->WasActive;
}

} // namespace

void RegisterAppTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    // Dev's main windows are there on a new composition.
    t = IM_REGISTER_TEST(engine, "app", "main_windows");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        IM_CHECK(App().State().HasDocument());
        for (const char *name : {"//3D Layout", "//Schematic", "//Level Manager", "//Building Blocks"})
            IM_CHECK_NO_RET(ctx->GetWindowByRef(name) != nullptr);
    };

    // File > New Composition: a new, unmodified composition without Scripts
    // and without undo steps.
    t = IM_REGISTER_TEST(engine, "app", "new_composition");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->MenuClick("//##MainMenuBar/File/New Composition");
        ctx->Yield(5);
        vdev::EditorState &state = App().State();
        IM_CHECK(state.HasDocument());
        IM_CHECK(!state.document->Dirty());
        IM_CHECK(state.document->Scripts().empty());
        IM_CHECK(!state.History().CanUndo());
    };

    // What waits for a composition ends with it: File > New Composition
    // closes the 3D Layout Explorer (without restoring the selection it
    // saved) and ends a flight without recording it.
    t = IM_REGISTER_TEST(engine, "app", "new_composition_ends_explorer_and_flight");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        // Y over the view flies; the toolbar's button (Dev command 41) opens the Explorer.
        ImVec2 min, max;
        IM_CHECK_SILENT(App().Layout().ShownView(min, max));
        ctx->MouseMoveToPos(ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f));
        ctx->KeyPress(ImGuiKey_Y);
        ctx->Yield(2);
        IM_CHECK_SILENT(App().Layout().Tool() == vdev::LayoutTool::Fly);
        ctx->ItemClick("//3D Layout/$$41/##tool");
        ctx->Yield(3);
        IM_CHECK_SILENT(WindowShown(ctx, "//3D Layout Explorer"));
        ctx->MenuClick("//##MainMenuBar/File/New Composition");
        ctx->Yield(5);
        IM_CHECK(!WindowShown(ctx, "//3D Layout Explorer"));
        IM_CHECK(App().Layout().Tool() == vdev::LayoutTool::Select);
        IM_CHECK(!App().State().History().CanUndo());
        IM_CHECK(!App().State().document->Dirty());
    };

    // Ctrl+P and Options > General Preferences open the General Preferences;
    // Cancel closes them.
    t = IM_REGISTER_TEST(engine, "app", "general_preferences");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_P);
        ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, "//General Preferences"));
        ctx->ItemClick("//General Preferences/Cancel");
        ctx->Yield(3);
        IM_CHECK(!WindowShown(ctx, "//General Preferences"));
        ctx->MenuClick("//##MainMenuBar/Options/General Preferences");
        ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, "//General Preferences"));
        ctx->ItemClick("//General Preferences/Cancel");
        ctx->Yield(3);
        IM_CHECK(!WindowShown(ctx, "//General Preferences"));
    };

    // Help > About Virtools..., closed with OK.
    t = IM_REGISTER_TEST(engine, "app", "about");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->MenuClick("//##MainMenuBar/Help/About Virtools...");
        ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, "//About Virtools"));
        ctx->ItemClick("//About Virtools/OK");
        ctx->Yield(3);
        IM_CHECK(!WindowShown(ctx, "//About Virtools"));
    };
}

} // namespace vdevuitest
