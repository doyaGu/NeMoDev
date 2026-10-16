// "level": the Level Manager on the sample opened from the command line:
// a 3D entity's row clicked (the 3D Layout's selection follows), a
// Script's row and Enter (the Schematic in front, showing it), the entity's
// Visible cell clicked and renamed (F2), each undone with Ctrl+Z, then
// deleted (Del, Select Delete Options), which clears the undo steps.

#include <optional>

#include "tests/ui/UiTests.h"

namespace vdevuitest {

namespace {

using Row = LevelRow;

std::optional<Row> RowOf(CK_ID object) {
    for (const Row &row : App().Panels().ShownLevelRows())
        if (row.object == object) return row;
    return std::nullopt;
}

bool Visible(ImGuiTestContext *ctx, CK_ID id) {
    return Read<bool>(ctx, [id] {
        CKObject *object = Object<CKObject>(id);
        return object && object->IsVisible();
    });
}

} // namespace

void RegisterLevelTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    // A click on a 3D entity's row selects it alone; the 3D Layout's
    // selection is that entity.
    t = IM_REGISTER_TEST(engine, "level", "select_row");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        const std::optional<Row> row = ClickLevelRow(ctx, CKCID_3DENTITY);
        IM_CHECK_SILENT(row.has_value());
        const std::set<CK_ID> &selection = App().Panels().LevelSelection();
        IM_CHECK_EQ(selection.size(), 1u);
        IM_CHECK(selection.count(row->object) == 1);
        const std::optional<Row> shown = RowOf(row->object);
        IM_CHECK(shown && shown->selected);
        ctx->Yield(2);
        const vdev::ObjectSelection &objects = App().State().objectSelection;
        IM_CHECK_EQ(objects.objects.size(), 1u);
        IM_CHECK(objects.Contains(row->object));
    };

    // A Script's row clicked, Enter: the Schematic in front, showing that
    // Script.
    t = IM_REGISTER_TEST(engine, "level", "enter_script_shows_schematic");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        const std::optional<Row> row = ClickLevelRow(ctx, CKCID_BEHAVIOR, true);
        IM_CHECK_SILENT(row.has_value());
        IM_CHECK_SILENT(InFront(ctx, "//Level Manager"));
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        IM_CHECK(InFront(ctx, "//Schematic"));
        IM_CHECK_EQ(App().State().stackScript, row->object);
        bool shown = false;
        for (const vdev::SchematicCanvas::ShownScript &each : App().Canvas().ShownScripts())
            shown |= each.script == row->object;
        IM_CHECK(shown);
    };

    // The Visible cell hides the entity (one undo step, modified); Ctrl+Z
    // shows it again, unmodified.
    t = IM_REGISTER_TEST(engine, "level", "visible_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        const std::optional<Row> row = FindLevelRow(ctx, CKCID_3DENTITY);
        IM_CHECK_SILENT(row.has_value());
        IM_CHECK_SILENT(row->eyeMax.x > row->eyeMin.x);
        vdev::EditorState &state = App().State();
        const bool before = Visible(ctx, row->object);
        ctx->MouseMoveToPos(Center(row->eyeMin, row->eyeMax));
        ctx->MouseClick(0);
        ctx->Yield(3);
        IM_CHECK(Visible(ctx, row->object) != before);
        IM_CHECK(state.document->Dirty());
        Undo(ctx);
        IM_CHECK(Visible(ctx, row->object) == before);
        IM_CHECK(!state.document->Dirty());
    };

    // F2 on the selected row: Rename, a new name entered; Ctrl+Z gives the
    // old one back.
    t = IM_REGISTER_TEST(engine, "level", "rename_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        const std::optional<Row> row = ClickLevelRow(ctx, CKCID_3DENTITY);
        IM_CHECK_SILENT(row.has_value());
        const std::string before = ObjectName(ctx, row->object);
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        ImGuiWindow *dialog = ctx->GetWindowByRef("//Rename##level");
        IM_CHECK_SILENT(dialog && dialog->WasActive);
        ctx->ItemInputValue("//Rename##level/##name", "UI Test Renamed");
        ctx->Yield(3);
        IM_CHECK_STR_EQ(ObjectName(ctx, row->object).c_str(), "UI Test Renamed");
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK_STR_EQ(ObjectName(ctx, row->object).c_str(), before.c_str());
    };

    // Dev's tree keys: Down selects the row below (a folder's: nothing
    // selected), Up the row clicked again; Space locks the 3D Layout's
    // selection, Space again unlocks it.
    t = IM_REGISTER_TEST(engine, "level", "keys_move_lock");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        const std::optional<Row> row = ClickLevelRow(ctx, CKCID_3DENTITY);
        IM_CHECK_SILENT(row.has_value());
        const std::vector<Row> rows = App().Panels().ShownLevelRows();
        size_t at = rows.size();
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].object == row->object && at == rows.size()) at = i;
        IM_CHECK_SILENT(at + 1 < rows.size());
        const CK_ID below = rows[at + 1].object;
        ctx->KeyPress(ImGuiKey_DownArrow);
        ctx->Yield(3);
        const std::set<CK_ID> &selection = App().Panels().LevelSelection();
        if (below) {
            IM_CHECK_EQ(selection.size(), 1u);
            IM_CHECK(selection.count(below) == 1);
        } else {
            IM_CHECK(selection.empty());
        }
        ctx->KeyPress(ImGuiKey_UpArrow);
        ctx->Yield(3);
        IM_CHECK_EQ(selection.size(), 1u);
        IM_CHECK(selection.count(row->object) == 1);
        ctx->Yield(2);
        vdev::ObjectSelection &objects = App().State().objectSelection;
        IM_CHECK(!objects.locked);
        ctx->KeyPress(ImGuiKey_Space);
        ctx->Yield(3);
        IM_CHECK(objects.locked);
        IM_CHECK(objects.Contains(row->object));
        ctx->KeyPress(ImGuiKey_Space);
        ctx->Yield(3);
        IM_CHECK(!objects.locked);
    };

    // The Activate At Scene Start cell goes A, D, Leave and A again (Dev.exe
    // sub_574C10), one undo step each; the Active Now cell deactivates the
    // entity now, Ctrl+Z activates it again.
    t = IM_REGISTER_TEST(engine, "level", "flag_cells_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        const std::optional<Row> row = FindLevelRow(ctx, CKCID_3DENTITY);
        IM_CHECK_SILENT(row.has_value() && row->activateMax.x > row->activateMin.x);
        const CK_ID id = row->object;
        auto flags = [&] {
            return Read<CKDWORD>(ctx, [id] {
                CKLevel *level = App().State().host.Context()->GetCurrentLevel();
                CKScene *scene = level ? level->GetLevelScene() : nullptr;
                CKSceneObject *object = Object<CKSceneObject>(id);
                return scene && object ? static_cast<CKDWORD>(scene->GetObjectFlags(object)) : 0u;
            });
        };
        const CKDWORD start = flags();
        // The row's cell (where it is drawn now).
        auto click = [&](bool activeNow) {
            const Row now = RowOf(id).value_or(*row);
            ctx->MouseMoveToPos(activeNow ? Center(now.activeMin, now.activeMax) : Center(now.activateMin, now.activateMax));
            ctx->MouseClick(0);
            ctx->Yield(3);
        };
        const CKDWORD starts = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_DEACTIVATE | CK_SCENEOBJECT_START_LEAVE;
        // From A (or nothing set): D, Leave, A.
        click(false);
        IM_CHECK((flags() & starts) == CK_SCENEOBJECT_START_DEACTIVATE);
        click(false);
        IM_CHECK((flags() & starts) == CK_SCENEOBJECT_START_LEAVE);
        click(false);
        IM_CHECK((flags() & starts) == CK_SCENEOBJECT_START_ACTIVATE);
        Undo(ctx);
        Undo(ctx);
        Undo(ctx);
        IM_CHECK_EQ(flags() & starts, start & starts);
        const bool active = (start & CK_SCENEOBJECT_ACTIVE) != 0;
        click(true);
        IM_CHECK(((flags() & CK_SCENEOBJECT_ACTIVE) != 0) != active);
        Undo(ctx);
        IM_CHECK(((flags() & CK_SCENEOBJECT_ACTIVE) != 0) == active);
    };

    // Del on the selected row: Select Delete Options; Escape keeps the
    // object, OK (Enter) destroys it, the row goes and, as in Dev ("Undo
    // stack will be cleared"), the undo steps with it. Last: the object is gone.
    t = IM_REGISTER_TEST(engine, "level", "delete");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        const std::optional<Row> row = ClickLevelRow(ctx, CKCID_3DENTITY);
        IM_CHECK_SILENT(row.has_value());
        vdev::EditorState &state = App().State();
        const size_t logged = state.log.size();
        ctx->KeyPress(ImGuiKey_Delete);
        ctx->Yield(3);
        ImGuiWindow *dialog = ctx->GetWindowByRef("//###DeleteOptions");
        IM_CHECK_SILENT(dialog && dialog->WasActive);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(!dialog->Active);
        IM_CHECK(ObjectExists(ctx, row->object));
        ctx->KeyPress(ImGuiKey_Delete);
        ctx->Yield(3);
        IM_CHECK_SILENT(dialog->WasActive);
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        LogEditor(ctx, logged);
        IM_CHECK(!ObjectExists(ctx, row->object));
        IM_CHECK(!RowOf(row->object).has_value());
        IM_CHECK(!state.History().CanUndo());
        IM_CHECK(state.document->Dirty());
    };
}

} // namespace vdevuitest
