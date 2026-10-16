// "hierarchy": the Hierarchy Manager (Editors > Hierarchy Manager) on the
// sample opened from the command line: a row clicked (the 3D Layout's
// selection follows) and Down to the next, renamed in place (F2), its Render
// Priority edited (a double click on it), an entity dragged onto another (its
// new parent), each undone with Ctrl+Z; the toolbar's Show/Hide Meshes,
// Materials and Textures, Expand and Collapse Selected; the shortcut menu's
// Setup, Set and Remove Initial Conditions on Hierarchy (undone).

#include <algorithm>
#include <cstring>

#include "tests/ui/UiTests.h"

namespace vdevuitest {

namespace {

using Row = vdev::ManagerWindows::ShownRow;

// The Hierarchy Manager shown (its Editors menu item checks it) and in front.
bool ShowHierarchy(ImGuiTestContext *ctx) {
    Settle(ctx);
    if (!App().State().showHierarchy) ctx->MenuClick("//##MainMenuBar/Editors/Hierarchy Manager");
    ctx->Yield(2);
    ctx->WindowFocus("//Hierarchy Manager");
    ctx->Yield(3);
    return !App().Managers().ShownHierarchyRows().empty();
}

// Every row opened (the closed ones' arrows, the shallowest first), then
// the rows of objects of class cid (exact: of that class), in their order.
std::vector<Row> RowsOf(ImGuiTestContext *ctx, CK_CLASSID cid, bool exact) {
    for (int pass = 0; pass < 20; ++pass) {
        const std::vector<Row> rows = App().Managers().ShownHierarchyRows();
        const Row *closed = nullptr;
        for (const Row &row : rows)
            if (row.children && !row.open && (!closed || row.depth < closed->depth)) closed = &row;
        if (!closed) break;
        ctx->MouseMoveToPos(Center(closed->arrowMin, closed->arrowMax));
        ctx->MouseClick(0);
        ctx->Yield(2);
    }
    const std::vector<Row> rows = App().Managers().ShownHierarchyRows();
    std::vector<Row> found;
    OnMainThread(ctx, [&] {
        for (const Row &row : rows)
            if (row.object && row.cid && (exact ? row.cid == cid : CKIsChildClassOf(row.cid, cid) != 0)) found.push_back(row);
    });
    return found;
}

std::optional<Row> RowOf(CK_ID object) {
    for (const Row &row : App().Managers().ShownHierarchyRows())
        if (row.object == object) return row;
    return std::nullopt;
}

// A row's name: right of its arrow and its icon.
ImVec2 NameAt(const Row &row) {
    return ImVec2(row.arrowMax.x > 0.0f ? row.arrowMax.x + 40.0f : row.min.x + 80.0f, (row.min.y + row.max.y) * 0.5f);
}

void ClickRow(ImGuiTestContext *ctx, const Row &row) {
    ctx->LogInfo("row '%s'", row.name.c_str());
    ctx->MouseMoveToPos(NameAt(row));
    ctx->MouseClick(0);
    ctx->Yield(3);
}

// A right click on the row's name: the row selected, the shortcut menu.
void OpenRowMenu(ImGuiTestContext *ctx, const Row &row) {
    ctx->MouseMoveToPos(NameAt(row));
    ctx->MouseClick(1);
    ctx->Yield(3);
}

int ZOrder(ImGuiTestContext *ctx, CK_ID id) {
    return Read<int>(ctx, [id] {
        CKRenderObject *object = Object<CKRenderObject>(id);
        return object ? object->GetZOrder() : 0;
    });
}

CK_ID ParentOf(ImGuiTestContext *ctx, CK_ID id) {
    return Read<CK_ID>(ctx, [id] {
        CK3dEntity *entity = Object<CK3dEntity>(id);
        return entity && entity->GetParent() ? entity->GetParent()->GetID() : CK_ID(0);
    });
}

} // namespace

void RegisterHierarchyTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    // A click on a 3D entity's row selects it, in the 3D Layout too; Down
    // selects the next row's object.
    t = IM_REGISTER_TEST(engine, "hierarchy", "select_row_keys");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(ShowHierarchy(ctx));
        const std::vector<Row> entities = RowsOf(ctx, CKCID_3DENTITY, false);
        IM_CHECK_SILENT(!entities.empty());
        const Row &row = entities.front();
        ClickRow(ctx, row);
        const vdev::ObjectSelection &objects = App().State().objectSelection;
        IM_CHECK_EQ(objects.objects.size(), 1u);
        IM_CHECK(objects.Contains(row.object));
        std::optional<Row> shown = RowOf(row.object);
        IM_CHECK(shown && shown->selected);
        // The next row with an object (the roots are passed over).
        const std::vector<Row> rows = App().Managers().ShownHierarchyRows();
        CK_ID next = 0;
        for (size_t i = 0; i < rows.size() && !next; ++i)
            if (rows[i].object == row.object)
                for (size_t k = i + 1; k < rows.size() && !next; ++k) next = rows[k].object;
        IM_CHECK_SILENT(next != 0);
        ctx->KeyPress(ImGuiKey_DownArrow);
        ctx->Yield(3);
        shown = RowOf(next);
        IM_CHECK(shown && shown->selected);
        IM_CHECK(!RowOf(row.object)->selected);
    };

    // F2 renames the selected row in place (Enter); Ctrl+Z gives the old name back.
    t = IM_REGISTER_TEST(engine, "hierarchy", "rename_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(ShowHierarchy(ctx));
        const std::vector<Row> entities = RowsOf(ctx, CKCID_3DENTITY, false);
        IM_CHECK_SILENT(!entities.empty());
        const Row row = entities.front();
        const std::string before = ObjectName(ctx, row.object);
        ClickRow(ctx, row);
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(2);
        IM_CHECK_SILENT(ctx->UiContext->ActiveId != 0);
        ctx->KeyCharsReplaceEnter("UI Test Hierarchy");
        ctx->Yield(3);
        IM_CHECK_STR_EQ(ObjectName(ctx, row.object).c_str(), "UI Test Hierarchy");
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK_STR_EQ(ObjectName(ctx, row.object).c_str(), before.c_str());
        IM_CHECK(!App().State().document->Dirty());
    };

    // A double click on a render object's Render Priority edits it (Enter);
    // Ctrl+Z gives the old one back.
    t = IM_REGISTER_TEST(engine, "hierarchy", "render_priority_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(ShowHierarchy(ctx));
        std::vector<Row> entities = RowsOf(ctx, CKCID_3DENTITY, false);
        entities.erase(std::remove_if(entities.begin(), entities.end(),
                                      [](const Row &row) { return row.priorityMax.x <= row.priorityMin.x; }),
                       entities.end());
        IM_CHECK_SILENT(!entities.empty());
        const Row row = entities.front();
        const int before = ZOrder(ctx, row.object);
        const int value = before == 7 ? 8 : 7;
        ctx->MouseMoveToPos(Center(row.priorityMin, row.priorityMax));
        ctx->MouseDoubleClick(0);
        ctx->Yield(2);
        IM_CHECK_SILENT(ctx->UiContext->ActiveId != 0);
        ctx->KeyCharsReplaceEnter(std::to_string(value).c_str());
        ctx->Yield(3);
        IM_CHECK_EQ(ZOrder(ctx, row.object), value);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK_EQ(ZOrder(ctx, row.object), before);
        IM_CHECK(!App().State().document->Dirty());
    };

    // A 3D entity dragged onto another becomes its child; Ctrl+Z gives it
    // its parent back.
    t = IM_REGISTER_TEST(engine, "hierarchy", "set_parent_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(ShowHierarchy(ctx));
        std::vector<Row> entities = RowsOf(ctx, CKCID_3DOBJECT, true);
        if (entities.size() < 2) entities = RowsOf(ctx, CKCID_3DENTITY, false);
        IM_CHECK_SILENT(entities.size() >= 2);
        // Two rows of the same depth: neither is below the other.
        const Row *child = nullptr, *parent = nullptr;
        for (size_t i = 0; i < entities.size() && !parent; ++i)
            for (size_t k = i + 1; k < entities.size() && !parent; ++k)
                if (entities[i].depth == entities[k].depth) {
                    child = &entities[i];
                    parent = &entities[k];
                }
        IM_CHECK_SILENT(parent != nullptr);
        const Row moved = *child, target = *parent;
        ctx->LogInfo("'%s' onto '%s'", moved.name.c_str(), target.name.c_str());
        const CK_ID before = ParentOf(ctx, moved.object);
        IM_CHECK_SILENT(before != target.object);
        const ImVec2 from = NameAt(moved), to = NameAt(target);
        ctx->MouseMoveToPos(from);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos(ImVec2(from.x + 10.0f, (from.y + to.y) * 0.5f));
        ctx->MouseMoveToPos(to);
        ctx->Yield(2);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(ParentOf(ctx, moved.object), target.object);
        IM_CHECK(App().State().History().CanUndo());
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK_EQ(ParentOf(ctx, moved.object), before);
        IM_CHECK(!App().State().document->Dirty());
    };

    // The toolbar's Show/Hide Meshes: the meshes' rows under their
    // entities; Show/Hide Materials, the materials' under the meshes; both
    // off again, neither.
    t = IM_REGISTER_TEST(engine, "hierarchy", "show_meshes_materials");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(ShowHierarchy(ctx));
        IM_CHECK(RowsOf(ctx, CKCID_MESH, false).empty());
        ctx->ItemClick("//Hierarchy Manager/meshes/##bitmap");
        ctx->Yield(2);
        IM_CHECK(!RowsOf(ctx, CKCID_MESH, false).empty());
        IM_CHECK(RowsOf(ctx, CKCID_MATERIAL, false).empty());
        ctx->ItemClick("//Hierarchy Manager/materials/##bitmap");
        ctx->Yield(2);
        IM_CHECK(!RowsOf(ctx, CKCID_MATERIAL, false).empty());
        ctx->ItemClick("//Hierarchy Manager/materials/##bitmap");
        ctx->ItemClick("//Hierarchy Manager/meshes/##bitmap");
        ctx->Yield(2);
        IM_CHECK(RowsOf(ctx, CKCID_MESH, false).empty());
        IM_CHECK(RowsOf(ctx, CKCID_MATERIAL, false).empty());
    };

    // A right click on a 3D entity's row: the shortcut menu, its Setup opens
    // the entity's Setup.
    t = IM_REGISTER_TEST(engine, "hierarchy", "menu_setup");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(ShowHierarchy(ctx));
        const std::vector<Row> entities = RowsOf(ctx, CKCID_3DENTITY, false);
        IM_CHECK_SILENT(!entities.empty());
        const Row row = entities.front();
        OpenRowMenu(ctx, row);
        IM_CHECK(RowOf(row.object) && RowOf(row.object)->selected);
        ctx->ItemClick("//$FOCUSED/Setup");
        ctx->Yield(3);
        IM_CHECK(ShowsObject(row.object));
    };

    // The shortcut menu's Set Initial Conditions on Hierarchy on an entity
    // without them: it has them; Remove Initial Conditions on Hierarchy:
    // none; Ctrl+Z twice comes back to none, unmodified.
    t = IM_REGISTER_TEST(engine, "hierarchy", "menu_initial_conditions_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(ShowHierarchy(ctx));
        std::optional<Row> row;
        for (const Row &entity : RowsOf(ctx, CKCID_3DENTITY, false))
            if (!row && !HasInitialConditions(ctx, entity.object)) row = entity;
        IM_CHECK_SILENT(row.has_value());
        ctx->LogInfo("row '%s'", row->name.c_str());
        OpenRowMenu(ctx, *row);
        ctx->ItemClick("//$FOCUSED/Set Initial Conditions on Hierarchy");
        ctx->Yield(3);
        IM_CHECK(HasInitialConditions(ctx, row->object));
        IM_CHECK(App().State().History().CanUndo());
        IM_CHECK(App().State().document->Dirty());
        OpenRowMenu(ctx, *row);
        ctx->ItemClick("//$FOCUSED/Remove Initial Conditions on Hierarchy");
        ctx->Yield(3);
        IM_CHECK(!HasInitialConditions(ctx, row->object));
        Undo(ctx);
        IM_CHECK(HasInitialConditions(ctx, row->object));
        Undo(ctx);
        IM_CHECK(!HasInitialConditions(ctx, row->object));
        IM_CHECK(!App().State().document->Dirty());
    };

    // Show/Hide Textures with the meshes and materials: the textures' rows;
    // a 3D entity's row selected, Collapse Selected closes it (its mesh's row
    // gone), Expand Selected opens it again. The three off again.
    t = IM_REGISTER_TEST(engine, "hierarchy", "textures_expand_collapse");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(ShowHierarchy(ctx));
        const char *const buttons[] = {"//Hierarchy Manager/meshes/##bitmap", "//Hierarchy Manager/materials/##bitmap",
                                       "//Hierarchy Manager/textures/##bitmap"};
        for (const char *button : buttons) ctx->ItemClick(button);
        ctx->Yield(2);
        IM_CHECK(!RowsOf(ctx, CKCID_TEXTURE, false).empty());
        // An entity whose row holds its mesh.
        std::optional<Row> entity;
        for (const Row &row : RowsOf(ctx, CKCID_3DENTITY, false))
            if (!entity && row.children && row.open) entity = row;
        IM_CHECK_SILENT(entity.has_value());
        ctx->LogInfo("row '%s'", entity->name.c_str());
        ClickRow(ctx, *entity);
        const size_t shown = App().Managers().ShownHierarchyRows().size();
        ctx->ItemClick("//Hierarchy Manager/collapse/##bitmap");
        ctx->Yield(2);
        IM_CHECK(RowOf(entity->object) && !RowOf(entity->object)->open);
        IM_CHECK_LT(App().Managers().ShownHierarchyRows().size(), shown);
        ctx->ItemClick("//Hierarchy Manager/expand/##bitmap");
        ctx->Yield(2);
        IM_CHECK(RowOf(entity->object) && RowOf(entity->object)->open);
        IM_CHECK_EQ(App().Managers().ShownHierarchyRows().size(), shown);
        for (const char *button : buttons) ctx->ItemClick(button);
        ctx->Yield(2);
        IM_CHECK(RowsOf(ctx, CKCID_MESH, false).empty());
    };
}

} // namespace vdevuitest
