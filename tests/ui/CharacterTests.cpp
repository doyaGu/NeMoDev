// "character": the Character Setup on the sample opened from the command
// line (Character Controller.cmo; the samples of the library's repository
// have no character): Pickable, undone; a body part's row double-clicked
// opens its Setup.

#include <string>

#include "tests/ui/UiTests.h"
#include "ui/controls/ObjectText.h"

namespace vdevuitest {

void RegisterCharacterTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    // The first character's Setup: Pickable; Ctrl+Z gives it back,
    // unmodified.
    t = IM_REGISTER_TEST(engine, "character", "pickable_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CK_ID character = OpenSetupOf(ctx, CKCID_CHARACTER, "//###setupcharacter");
        IM_CHECK_SILENT(character != 0);
        auto pickable = [character] { return Object<CKCharacter>(character)->IsPickable() != 0; };
        const bool before = Read<bool>(ctx, pickable);
        ctx->ItemClick("**/Pickable");
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, pickable) != before);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, pickable) == before);
        IM_CHECK(!App().State().document->Dirty());
    };

    // A double click on the first body part's row in Character Bodyparts:
    // the body part's Setup.
    t = IM_REGISTER_TEST(engine, "character", "body_part_setup");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CK_ID character = OpenSetupOf(ctx, CKCID_CHARACTER, "//###setupcharacter");
        IM_CHECK_SILENT(character != 0);
        CK_ID part = 0;
        std::string label;
        OnMainThread(ctx, [&] {
            CKCharacter *owner = Object<CKCharacter>(character);
            CKBodyPart *first = owner->GetBodyPartCount() ? owner->GetBodyPart(0) : nullptr;
            if (!first) return;
            part = first->GetID();
            // CharacterPage's row: the name, "  (Root)" after the root's.
            label = vdev::DisplayName(first, "-- None --") + (first == owner->GetRootBodyPart() ? "  (Root)" : "");
        });
        IM_CHECK_SILENT(part != 0);
        ctx->LogInfo("body part '%s'", label.c_str());
        ctx->ItemDoubleClick(("**/" + label).c_str());
        ctx->Yield(3);
        IM_CHECK(ShowsObject(part));
    };
}

} // namespace vdevuitest
