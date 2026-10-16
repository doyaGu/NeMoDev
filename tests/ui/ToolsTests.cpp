// "tools": the Editors and Options menus' windows on the sample opened from
// the command line: Path Manager, Parameter Debugger, Installed Plugins and
// Building Block Version Test shown; last, Unused Object Explorer listing a
// material made in the 3D Layout (7) and its Delete destroying it (the undo
// steps cleared, as the Level Manager's Delete).

#include <set>

#include "tests/ui/UiTests.h"

namespace vdevuitest {

namespace {

bool WindowShown(ImGuiTestContext *ctx, const char *ref) {
    ImGuiWindow *window = ctx->GetWindowByRef(ref);
    return window && window->WasActive;
}

// The menu item clicked (a path of the main menu bar), its window shown.
bool OpenFromMenu(ImGuiTestContext *ctx, const char *menu, const char *window) {
    Settle(ctx);
    ctx->MenuClick((std::string("//##MainMenuBar/") + menu).c_str());
    ctx->Yield(3);
    const bool shown = WindowShown(ctx, window);
    ctx->LogInfo("%s: %s", window, shown ? "shown" : "not shown");
    return shown;
}

std::set<CK_ID> Materials(ImGuiTestContext *ctx) {
    return Read<std::set<CK_ID>>(ctx, [] {
        std::set<CK_ID> ids;
        const XObjectPointerArray &list = App().State().host.Context()->GetObjectListByType(CKCID_MATERIAL, TRUE);
        for (int i = 0; i < list.Size(); ++i)
            if (list[i] && !App().State().document->IsDiscarded(list[i]->GetID())) ids.insert(list[i]->GetID());
        return ids;
    });
}

} // namespace

void RegisterToolsTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    // Editors > Path Manager: its dialog (Escape closes it).
    t = IM_REGISTER_TEST(engine, "tools", "path_manager");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK(OpenFromMenu(ctx, "Editors/Path Manager", "//Path Manager"));
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
    };

    // Editors > Parameter Debugger: its window; the menu item again hides it.
    t = IM_REGISTER_TEST(engine, "tools", "parameter_debugger");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK(OpenFromMenu(ctx, "Editors/Parameter Debugger", "//Parameter Debugger"));
        IM_CHECK(!OpenFromMenu(ctx, "Editors/Parameter Debugger", "//Parameter Debugger"));
    };

    // Options > Installed Plugins: its window.
    t = IM_REGISTER_TEST(engine, "tools", "installed_plugins");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK(OpenFromMenu(ctx, "Options/Installed Plugins", "//Installed Plugins"));
        ctx->WindowClose("//Installed Plugins");
        ctx->Yield(2);
    };

    // Options > Building Block Version Test: its report; No (or Escape) closes it.
    t = IM_REGISTER_TEST(engine, "tools", "version_test");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK(OpenFromMenu(ctx, "Options/Building Block Version Test", "//Building Block Version Test"));
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(!WindowShown(ctx, "//Building Block Version Test"));
    };

    // Ctrl+Alt+F12: All CK Object In Memory; Enter (OK) closes it.
    t = IM_REGISTER_TEST(engine, "tools", "all_objects");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_F12);
        ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, "//###AllObjects"));
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        IM_CHECK(!WindowShown(ctx, "//###AllObjects"));
    };

    // Last (no undo): a material made in the 3D Layout (7) is unused;
    // Options > Unused Object Explorer, Delete: the material destroyed, the
    // undo steps cleared.
    t = IM_REGISTER_TEST(engine, "tools", "unused_objects_delete");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        ImVec2 min, max;
        IM_CHECK_SILENT(App().Layout().ShownView(min, max));
        ctx->MouseMoveToPos(Center(min, max));
        const std::set<CK_ID> before = Materials(ctx);
        ctx->KeyPress(ImGuiKey_7);
        ctx->Yield(3);
        CK_ID material = 0;
        for (CK_ID id : Materials(ctx))
            if (!before.count(id)) material = id;
        IM_CHECK_SILENT(material != 0);
        ctx->LogInfo("material '%s'", ObjectName(ctx, material).c_str());
        IM_CHECK_SILENT(OpenFromMenu(ctx, "Options/Unused Object Explorer", "//Unused Object Explorer"));
        ctx->ItemClick("//Unused Object Explorer/**/Delete");
        ctx->Yield(3);
        IM_CHECK(!ObjectExists(ctx, material));
        IM_CHECK(!App().State().History().CanUndo());
    };
}

} // namespace vdevuitest
