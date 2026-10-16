// "blocks": the Building Blocks panel on the sample opened from the command
// line (a Script shown, 3D entities in the 3D Layout): its filter, a
// Building Block dragged into the Schematic's open Script (Ctrl+Z takes it
// out) and onto a 3D entity of the 3D Layout (its Edit Parameters dialog:
// OK keeps it, one undo step; Cancel takes it out), and onto a selection
// (each selected 3D entity gets it).

#include <algorithm>
#include <cstring>
#include <optional>

#include "tests/ui/UiTests.h"

namespace vdevuitest {

namespace {

// Rotate (3D Transformations/Basic): applies to 3D entities, has inputs
// (the 3D Layout asks for them).
constexpr const char *BLOCK = "Rotate";
constexpr const char *BLOCK_ITEM = "**/Rotate"; // its item in the panel's list

using Shown = vdev::SchematicCanvas::ShownBehavior;

// The panel's filter set to text (typed in, then Enter).
void Filter(ImGuiTestContext *ctx, const char *text) {
    ctx->ItemInputValue("//Building Blocks/##filter", text);
    ctx->Yield(2);
}

// The filtered list's item of BLOCK pressed and dragged to the point, then
// released there.
void DragBlockTo(ImGuiTestContext *ctx, const ImVec2 &to) {
    ctx->SetRef("//Building Blocks");
    ctx->MouseMove(BLOCK_ITEM);
    ctx->MouseDown(0);
    const ImVec2 from = ctx->UiContext->IO.MousePos;
    ctx->MouseMoveToPos(ImVec2(from.x - 20.0f, from.y + 10.0f));
    ctx->MouseMoveToPos(to);
    ctx->Yield(3);
    ctx->MouseUp(0);
    ctx->Yield(3);
}

// The Building Blocks named BLOCK in the owner's Scripts (each graph's).
int BlocksOn(ImGuiTestContext *ctx, CK_ID owner) {
    int count = -1;
    OnMainThread(ctx, [&] {
        CKBeObject *object = Object<CKBeObject>(owner);
        if (!object) return;
        count = 0;
        std::vector<CKBehavior *> pending;
        for (int i = 0; i < object->GetScriptCount(); ++i) pending.push_back(object->GetScript(i));
        while (!pending.empty()) {
            CKBehavior *behavior = pending.back();
            pending.pop_back();
            if (!behavior) continue;
            if (!(behavior->GetType() & CKBEHAVIORTYPE_SCRIPT) && behavior->GetName() &&
                !std::strcmp(behavior->GetName(), BLOCK))
                ++count;
            for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i) pending.push_back(behavior->GetSubBehavior(i));
        }
    });
    return count;
}

int ShownNamed(CK_ID script, const std::string &name = BLOCK) {
    int count = 0;
    for (const Shown &shown : App().Canvas().ShownBehaviors())
        if (shown.script == script && shown.name == name) ++count;
    return count;
}

// A 3D entity of the 3D Layout selected (SelectEntity), the Building Block
// dragged onto it: its Edit Parameters dialog shown.
bool DropOnEntity(ImGuiTestContext *ctx, InView &found, int &before) {
    Settle(ctx, "//3D Layout");
    if (!SelectEntity(ctx, found)) return false;
    before = BlocksOn(ctx, found.entity);
    Filter(ctx, BLOCK);
    DragBlockTo(ctx, found.at);
    ImGuiWindow *dialog = ctx->GetWindowByRef("//###DropParameters");
    return dialog && dialog->WasActive;
}

} // namespace

void RegisterBlocksTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    // The filter lists the Building Blocks whose name or category has the
    // text (the closed category tree otherwise).
    t = IM_REGISTER_TEST(engine, "blocks", "filter");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->SetRef("//Building Blocks");
        IM_CHECK(!ctx->ItemExists(BLOCK_ITEM));
        Filter(ctx, BLOCK);
        IM_CHECK(ctx->ItemExists(BLOCK_ITEM));
        IM_CHECK(!ctx->ItemExists("**/Delayer"));
        Filter(ctx, "");
        IM_CHECK(!ctx->ItemExists(BLOCK_ITEM));
    };

    // Dragged into the Schematic's open Script where nothing is: the Building
    // Block is inserted there, selected, the composition modified; Ctrl+Z
    // takes it out, unmodified.
    t = IM_REGISTER_TEST(engine, "blocks", "drop_into_schematic_undo");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Schematic");
        vdev::EditorState &state = App().State();
        const std::optional<std::pair<CK_ID, ImVec2>> point = EmptySchematicPoint(0, ImVec2(200, 120));
        IM_CHECK_SILENT(point.has_value());
        const CK_ID script = point->first;
        const int before = ShownNamed(script);
        Filter(ctx, BLOCK);
        DragBlockTo(ctx, point->second);
        IM_CHECK_EQ(ShownNamed(script), before + 1);
        IM_CHECK_EQ(state.selection.items.size(), 1u);
        IM_CHECK(state.History().CanUndo());
        IM_CHECK(state.document->Dirty());
        Undo(ctx);
        IM_CHECK_EQ(ShownNamed(script), before);
        IM_CHECK(!state.document->Dirty());
    };

    // Two Building Blocks selected (click, Ctrl+click) and dragged into the
    // Schematic's open Script together (documentation "Building Blocks
    // Resource"): both inserted; Ctrl+Z twice takes them out, unmodified.
    t = IM_REGISTER_TEST(engine, "blocks", "multi_drag_into_schematic");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Schematic");
        vdev::EditorState &state = App().State();
        const std::optional<std::pair<CK_ID, ImVec2>> point = EmptySchematicPoint(0, ImVec2(200, 120));
        IM_CHECK_SILENT(point.has_value());
        const CK_ID script = point->first;
        Filter(ctx, BLOCK);
        // The first two the filter lists, in the catalog's order.
        std::vector<std::string> names;
        for (const vdev::BuildingBlockEntry &entry : state.catalog.Entries())
            if (entry.name.find(BLOCK) != std::string::npos &&
                std::find(names.begin(), names.end(), entry.name) == names.end() && names.size() < 2)
                names.push_back(entry.name);
        IM_CHECK_SILENT(names.size() == 2);
        ctx->LogInfo("dragging '%s' and '%s'", names[0].c_str(), names[1].c_str());
        const int before0 = ShownNamed(script, names[0]), before1 = ShownNamed(script, names[1]);
        ctx->SetRef("//Building Blocks");
        ctx->ItemClick(("**/" + names[0]).c_str());
        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->ItemClick(("**/" + names[1]).c_str());
        ctx->KeyUp(ImGuiMod_Ctrl);
        ctx->Yield(2);
        ctx->MouseMove(("**/" + names[0]).c_str());
        ctx->MouseDown(0);
        const ImVec2 from = ctx->UiContext->IO.MousePos;
        ctx->MouseMoveToPos(ImVec2(from.x - 20.0f, from.y + 10.0f));
        ctx->MouseMoveToPos(point->second);
        ctx->Yield(3);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(ShownNamed(script, names[0]), before0 + 1);
        IM_CHECK_EQ(ShownNamed(script, names[1]), before1 + 1);
        IM_CHECK(state.document->Dirty());
        Undo(ctx);
        Undo(ctx);
        IM_CHECK_EQ(ShownNamed(script, names[0]), before0);
        IM_CHECK_EQ(ShownNamed(script, names[1]), before1);
        IM_CHECK(!state.document->Dirty());
        // One selected again (the next drags take only it).
        ctx->SetRef("//Building Blocks");
        ctx->ItemClick(("**/" + names[0]).c_str());
        ctx->Yield(2);
    };

    // Dropped on a 3D entity of the 3D Layout: Edit Parameters, OK (Enter):
    // the Building Block in the entity's Script, one undo step; Ctrl+Z takes
    // it out, unmodified.
    t = IM_REGISTER_TEST(engine, "blocks", "drop_on_layout_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        InView found;
        int before = 0;
        IM_CHECK_SILENT(DropOnEntity(ctx, found, before));
        IM_CHECK_SILENT(before >= 0);
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        vdev::EditorState &state = App().State();
        IM_CHECK_EQ(BlocksOn(ctx, found.entity), before + 1);
        IM_CHECK(state.document->Dirty());
        // The Schematic's stack made for the entity's Script, which shows the
        // block; the Schematic in front once the dialog is closed.
        const vdev::ScriptInfo *shown = state.document->FindScript(state.stackScript);
        IM_CHECK(shown && shown->ownerId == found.entity);
        IM_CHECK(InFront(ctx, "//Schematic"));
        Undo(ctx);
        IM_CHECK_EQ(BlocksOn(ctx, found.entity), before);
        IM_CHECK(!state.document->Dirty());
    };

    // Dropped on a 3D entity, Edit Parameters cancelled (Escape): the drop
    // is undone.
    t = IM_REGISTER_TEST(engine, "blocks", "drop_on_layout_cancel");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        InView found;
        int before = 0;
        IM_CHECK_SILENT(DropOnEntity(ctx, found, before));
        IM_CHECK_SILENT(before >= 0);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK_EQ(BlocksOn(ctx, found.entity), before);
        IM_CHECK(!App().State().document->Dirty());
    };

    // Everything selected (Ctrl+A), the Building Block dropped on one of
    // the selected entities: it goes to each selected 3D entity (the class
    // it applies to), Edit Parameters for the last, OK; one undo step,
    // Ctrl+Z takes them all out, unmodified.
    t = IM_REGISTER_TEST(engine, "blocks", "drop_on_selection_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK_SILENT(SelectEntity(ctx, found));
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->Yield(2);
        std::vector<CK_ID> entities;
        OnMainThread(ctx, [&] {
            CKContext *context = App().State().host.Context();
            for (CK_ID id : App().State().objectSelection.objects)
                if (CK3dEntity::Cast(context->GetObject(id))) entities.push_back(id);
        });
        IM_CHECK_SILENT(entities.size() > 1);
        ctx->LogInfo("%d selected 3D entities", static_cast<int>(entities.size()));
        std::vector<int> before;
        for (CK_ID id : entities) before.push_back(BlocksOn(ctx, id));
        Filter(ctx, BLOCK);
        DragBlockTo(ctx, found.at);
        ImGuiWindow *dialog = ctx->GetWindowByRef("//###DropParameters");
        IM_CHECK_SILENT(dialog && dialog->WasActive);
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        for (size_t i = 0; i < entities.size(); ++i) IM_CHECK_EQ(BlocksOn(ctx, entities[i]), before[i] + 1);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        for (size_t i = 0; i < entities.size(); ++i) IM_CHECK_EQ(BlocksOn(ctx, entities[i]), before[i]);
        IM_CHECK(!App().State().document->Dirty());
    };
}

} // namespace vdevuitest
