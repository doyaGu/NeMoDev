#include "tests/ui/UiTests.h"

#include "imgui_internal.h"

namespace vdevuitest {

void RegisterArchitectureBaselineTests(ImGuiTestEngine *engine) {
    auto *test = IM_REGISTER_TEST(engine, "app", "architecture_baseline_registrations");
    test->TestFunc = [](ImGuiTestContext *ctx) {
#define VIRTOOLSDEV_UI_CASE(category, name) IM_CHECK_NO_RET(ImGuiTestEngine_FindTestByName(ctx->Engine, category, name) != nullptr);
#include "tests/architecture/UiCases.def"
#undef VIRTOOLSDEV_UI_CASE
    };

    test = IM_REGISTER_TEST(engine, "app", "architecture_default_workspace_and_shortcut");
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->MenuClick("//##MainMenuBar/Options/Reset Interface");
        Settle(ctx, "//3D Layout");
        ImGuiWindow *layout = ctx->GetWindowByRef("//3D Layout");
        ImGuiWindow *blocks = ctx->GetWindowByRef(BlocksRef().c_str());
        ImGuiWindow *schematic = ctx->GetWindowByRef(SchematicRef().c_str());
        ImGuiWindow *level = ctx->GetWindowByRef(SceneRef("level", "").c_str());
        IM_CHECK(layout && blocks && schematic && level);
        IM_CHECK(layout->DockId && blocks->DockId && level->DockId);
        IM_CHECK(layout->DockId != blocks->DockId && layout->DockId != level->DockId);
        IM_CHECK(blocks->DockId != level->DockId && schematic->DockId == level->DockId);
        IM_CHECK(layout->Pos.x < blocks->Pos.x && layout->Pos.y < level->Pos.y);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_P);
        ctx->Yield(4);
        IM_CHECK(ctx->GetWindowByRef("//General Preferences") != nullptr);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
    };
}

} // namespace vdevuitest
