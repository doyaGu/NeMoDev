// "schematic": the Schematic on the sample opened from the command line,
// driven with the mouse and the keys: a Building Block moved, deleted, and
// each undone and redone (Ctrl+Z, Ctrl+Y); its Target (T), its priority from
// its shortcut menu (Edit Priority), a comment (C), each undone; the
// Schematic Explorer (Ctrl+F, F3), a Script's owner shown in the Level
// Manager (its header's menu), a Shift+drag export refused, a link from a
// Script's Start by two clicks and the Message Explorer (H).

#include <cmath>
#include <optional>
#include <string>

#include "core/document/ScriptSession.h"
#include "core/graph/TopologyCommands.h"
#include "core/graph/ValueCommands.h"
#include "core/schematic/SchematicModel.h"
#include "core/schematic/SchematicSearch.h"
#include "tests/ui/UiTests.h"

namespace vdevuitest {

namespace {

using Shown = vdev::SchematicCanvas::ShownBehavior;

// The first Building Block the Schematic shows.
std::optional<Shown> FirstBlock() {
    for (const Shown &behavior : App().Canvas().ShownBehaviors())
        if (behavior.buildingBlock) return behavior;
    return std::nullopt;
}

// The behavior as the Schematic shows it now, if it does.
std::optional<Shown> Find(CK_ID behavior) {
    for (const Shown &shown : App().Canvas().ShownBehaviors())
        if (shown.behavior == behavior) return shown;
    return std::nullopt;
}

bool Near(float a, float b) { return std::fabs(a - b) < 0.5f; }

// The Building Block clicked alone (the Schematic in front).
std::optional<Shown> ClickFirstBlock(ImGuiTestContext *ctx) {
    Settle(ctx, "//Schematic");
    const std::optional<Shown> block = FirstBlock();
    if (!block) return block;
    ctx->LogInfo("block '%s'", block->name.c_str());
    ctx->MouseMoveToPos(Center(block->min, block->max));
    ctx->MouseClick(0);
    ctx->Yield(2);
    return block;
}

// The comments of a Script's layout (every graph's).
int CommentCount(ImGuiTestContext *ctx, CK_ID script) {
    return Read<int>(ctx, [script] {
        vdev::ScriptSession *session = App().State().document->Session(script);
        if (!session) return -1;
        const auto &data = session->Data();
        size_t count = data.rootBehavior.comments.size();
        for (const auto &record : data.behaviors) count += record.comments.size();
        return static_cast<int>(count);
    });
}

bool WindowShown(ImGuiTestContext *ctx, const char *ref) {
    ImGuiWindow *window = ctx->GetWindowByRef(ref);
    return window && window->WasActive;
}

} // namespace

void RegisterSchematicTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    // A Building Block dragged 60 pixels to the right: one undo step, the
    // composition modified; Ctrl+Z puts it back (unmodified again), Ctrl+Y
    // moves it again.
    t = IM_REGISTER_TEST(engine, "schematic", "move_block_undo");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Schematic");
        vdev::EditorState &state = App().State();
        IM_CHECK(state.HasDocument());
        const std::optional<Shown> block = FirstBlock();
        IM_CHECK_SILENT(block.has_value());
        ctx->LogInfo("dragging '%s'", block->name.c_str());
        const ImVec2 from = Center(block->min, block->max);
        ctx->MouseMoveToPos(from);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos(ImVec2(from.x + 60.0f, from.y));
        ctx->MouseUp(0);
        ctx->Yield(3);
        std::optional<Shown> moved = Find(block->behavior);
        IM_CHECK_SILENT(moved.has_value());
        const float dx = moved->min.x - block->min.x;
        IM_CHECK_GT(dx, 30.0f);
        IM_CHECK(Near(moved->min.y, block->min.y));
        IM_CHECK(state.History().CanUndo());
        IM_CHECK(state.document->Dirty());

        Undo(ctx);
        std::optional<Shown> back = Find(block->behavior);
        IM_CHECK_SILENT(back.has_value());
        IM_CHECK(Near(back->min.x, block->min.x) && Near(back->min.y, block->min.y));
        IM_CHECK(!state.document->Dirty());

        Redo(ctx);
        std::optional<Shown> again = Find(block->behavior);
        IM_CHECK_SILENT(again.has_value());
        IM_CHECK(Near(again->min.x, moved->min.x));
        Undo(ctx);
    };

    // Cancelling a live move restores the position and the save point, then
    // another move must still commit as its own undo step.
    t = IM_REGISTER_TEST(engine, "schematic", "move_block_cancel_and_reedit");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Schematic");
        const auto block = FirstBlock();
        IM_CHECK_SILENT(block.has_value());
        auto &state = App().State();
        const bool dirty = state.document->Dirty();
        const size_t steps = state.History().UndoCount();
        const bool redo = state.History().CanRedo();
        const ImVec2 from = Center(block->min, block->max);
        ctx->MouseMoveToPos(from); ctx->MouseDown(0);
        ctx->MouseMoveToPos(ImVec2(from.x + 60.0f, from.y + 20.0f)); ctx->Yield(3);
        const auto preview = Find(block->behavior);
        IM_CHECK_SILENT(preview.has_value());
        IM_CHECK_GT(preview->min.x - block->min.x, 30.0f);
        IM_CHECK(state.document->EditOpen());
        ctx->KeyPress(ImGuiKey_Escape); ctx->MouseUp(0); ctx->Yield(3);
        const auto cancelled = Find(block->behavior);
        IM_CHECK_SILENT(cancelled.has_value());
        IM_CHECK(Near(cancelled->min.x, block->min.x) && Near(cancelled->min.y, block->min.y));
        IM_CHECK(!state.document->EditOpen());
        IM_CHECK_EQ(state.History().UndoCount(), steps);
        IM_CHECK_EQ(state.History().CanRedo(), redo);
        IM_CHECK_EQ(state.document->Dirty(), dirty);

        ctx->MouseMoveToPos(from); ctx->MouseDown(0);
        ctx->MouseMoveToPos(ImVec2(from.x + 60.0f, from.y)); ctx->MouseUp(0); ctx->Yield(3);
        const auto committed = Find(block->behavior);
        IM_CHECK_SILENT(committed.has_value());
        IM_CHECK_GT(committed->min.x - block->min.x, 30.0f);
        IM_CHECK_EQ(state.History().UndoCount(), steps + 1);
        Undo(ctx);
        const auto undone = Find(block->behavior);
        IM_CHECK_SILENT(undone.has_value());
        IM_CHECK(Near(undone->min.x, block->min.x) && Near(undone->min.y, block->min.y));
        IM_CHECK_EQ(state.document->Dirty(), dirty);
        Redo(ctx);
        const auto redone = Find(block->behavior);
        IM_CHECK_SILENT(redone.has_value());
        IM_CHECK(Near(redone->min.x, committed->min.x) && Near(redone->min.y, committed->min.y));
        Undo(ctx);
    };

    // A Building Block clicked and deleted (Del): no longer shown; Ctrl+Z
    // brings it back where it was, Ctrl+Y deletes it again.
    t = IM_REGISTER_TEST(engine, "schematic", "delete_block_undo");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Schematic");
        vdev::EditorState &state = App().State();
        IM_CHECK(state.HasDocument());
        const std::optional<Shown> block = FirstBlock();
        IM_CHECK_SILENT(block.has_value());
        ctx->LogInfo("deleting '%s'", block->name.c_str());
        ctx->MouseMoveToPos(Center(block->min, block->max));
        ctx->MouseClick(0);
        ctx->Yield(2);
        IM_CHECK_EQ(state.selection.items.size(), 1u);
        ctx->KeyPress(ImGuiKey_Delete);
        ctx->Yield(3);
        IM_CHECK(!Find(block->behavior).has_value());
        IM_CHECK(state.document->Dirty());

        Undo(ctx);
        std::optional<Shown> back = Find(block->behavior);
        IM_CHECK_SILENT(back.has_value());
        IM_CHECK(Near(back->min.x, block->min.x) && Near(back->min.y, block->min.y));
        IM_CHECK(!state.document->Dirty());

        Redo(ctx);
        IM_CHECK(!Find(block->behavior).has_value());
        Undo(ctx);
        IM_CHECK(Find(block->behavior).has_value());
    };

    // T on the selected Building Block adds its Target parameter (or takes
    // it off); Ctrl+Z gives it back, unmodified.
    t = IM_REGISTER_TEST(engine, "schematic", "target_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const std::optional<Shown> block = ClickFirstBlock(ctx);
        IM_CHECK_SILENT(block.has_value());
        const CK_ID id = block->behavior;
        auto target = [id] { return Object<CKBehavior>(id)->IsUsingTarget() != 0; };
        const bool before = Read<bool>(ctx, target);
        ctx->KeyPress(ImGuiKey_T);
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, target) != before);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, target) == before);
        IM_CHECK(!App().State().document->Dirty());
    };

    // The Building Block's shortcut menu (a right click; Rename is a graph's
    // only): Edit Priority, a priority typed in the Priority dialog (Enter);
    // Ctrl+Z gives the old one back.
    t = IM_REGISTER_TEST(engine, "schematic", "menu_priority_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const std::optional<Shown> block = ClickFirstBlock(ctx);
        IM_CHECK_SILENT(block.has_value());
        const CK_ID id = block->behavior;
        auto priority = [id] { return Object<CKBehavior>(id)->GetPriority(); };
        const int before = Read<int>(ctx, priority);
        ctx->MouseClick(1);
        ctx->Yield(3);
        ImGuiWindow *menu = ctx->GetWindowByRef("//$FOCUSED");
        IM_CHECK_SILENT(menu && (menu->Flags & ImGuiWindowFlags_Popup));
        ctx->ItemClick("//$FOCUSED/Edit Priority");
        ctx->Yield(3);
        IM_CHECK_SILENT(WindowShown(ctx, "//Priority"));
        ctx->ItemInputValue("//Priority/Priority", before + 5);
        ctx->Yield(3);
        IM_CHECK_EQ(Read<int>(ctx, priority), before + 5);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK_EQ(Read<int>(ctx, priority), before);
        IM_CHECK(!App().State().document->Dirty());
    };

    // C over an empty place of an open Script: a comment there; Ctrl+Z
    // takes it out, unmodified.
    t = IM_REGISTER_TEST(engine, "schematic", "comment_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Schematic");
        const std::optional<std::pair<CK_ID, ImVec2>> point = EmptySchematicPoint();
        IM_CHECK_SILENT(point.has_value());
        const int before = CommentCount(ctx, point->first);
        IM_CHECK_SILENT(before >= 0);
        ctx->MouseMoveToPos(point->second);
        ctx->MouseClick(ImGuiMouseButton_Right);
        ctx->Yield(2);
        IM_CHECK(!ctx->ItemExists("//$FOCUSED/Import from Variable Manager"));
        for (const char *item : {"Add Building Block", "Add Local Parameter", "Add <This> Parameter",
                                 "Add Parameter Operation", "Add Comment"})
            IM_CHECK_NO_RET(ctx->ItemExists((std::string("//$FOCUSED/") + item).c_str()));
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(2);
        ctx->KeyPress(ImGuiKey_C);
        ctx->Yield(3);
        IM_CHECK_EQ(CommentCount(ctx, point->first), before + 1);
        IM_CHECK_STR_EQ(App().State().History().UndoLabel().c_str(), "Add Comment");
        // Its text editor opens on it (the shortcuts wait while it is open): Escape.
        IM_CHECK(WindowShown(ctx, "//Edit Comment"));
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        Undo(ctx);
        IM_CHECK_EQ(CommentCount(ctx, point->first), before);
        IM_CHECK(!App().State().document->Dirty());
    };

    // Ctrl+Shift+F: the Schematic Explorer with its text focused; the first
    // Building Block's name typed, Enter: results; F3 shows the first one's
    // Script.
    t = IM_REGISTER_TEST(engine, "schematic", "explorer_find");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Schematic");
        const std::optional<Shown> block = FirstBlock();
        IM_CHECK_SILENT(block.has_value());
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F);
        ctx->Yield(3);
        IM_CHECK_SILENT(WindowShown(ctx, "//Schematic Explorer"));
        // Dev lists the Scripts only at first: Building Blocks checked, the
        // name typed in the Text Filter (the list follows it).
        ctx->ItemClick("//Schematic Explorer/Building Blocks");
        ctx->ItemClick("//Schematic Explorer/##text");
        ctx->KeyCharsAppendEnter(block->name.c_str());
        ctx->Yield(3);
        const std::vector<vdev::SearchResult> results = App().Panels().SearchResults();
        IM_CHECK_SILENT(!results.empty());
        bool named = false;
        for (const vdev::SearchResult &result : results) named |= result.label.find(block->name) != std::string::npos;
        IM_CHECK(named);
        ctx->LogInfo("%d result(s), the first '%s' (%s)", static_cast<int>(results.size()), results[0].label.c_str(),
                     results[0].kind.c_str());
        // F3: the next result (the first) shown, its Script in the Schematic.
        ctx->KeyPress(ImGuiKey_F3);
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().stackScript, results[0].script);
    };

    // Ctrl+F in the Schematic: Find, the first Building Block's name, Find
    // Next: it is selected; F3 again: another element with the name, or past
    // the end (nothing selected).
    t = IM_REGISTER_TEST(engine, "schematic", "find_dialog");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Schematic");
        const std::optional<Shown> block = FirstBlock();
        IM_CHECK_SILENT(block.has_value());
        ctx->WindowFocus("//Schematic");
        App().State().selection.Clear();
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_F);
        ctx->Yield(3);
        IM_CHECK_SILENT(WindowShown(ctx, "//Find###SchematicFind"));
        // Enter: Find Next.
        ctx->ItemInputValue("//Find###SchematicFind/Find", block->name.c_str());
        ctx->Yield(5);
        const vdev::Selection &selection = App().State().selection;
        const vdev::ElementRef found = App().Canvas().Found();
        IM_CHECK_SILENT(found.IsValid());
        IM_CHECK_EQ(selection.items.size(), 1u);
        IM_CHECK(!selection.items.empty() && selection.items.front() == found);
        ctx->WindowFocus("//Schematic");
        ctx->KeyPress(ImGuiKey_F3);
        ctx->Yield(5);
        IM_CHECK(!(App().Canvas().Found() == found));
    };

    // A right click on the first shown Script's header, Show Object In
    // Level Manager: the Level Manager in front, the Script's owner's row
    // shown (the tree opened, scrolled to it).
    t = IM_REGISTER_TEST(engine, "schematic", "show_object_in_level_manager");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Schematic");
        const std::vector<vdev::SchematicCanvas::ShownScript> scripts = App().Canvas().ShownScripts();
        IM_CHECK_SILENT(!scripts.empty());
        const vdev::SchematicCanvas::ShownScript script = scripts.front();
        const CK_ID owner = Read<CK_ID>(ctx, [&] {
            CKBehavior *behavior = Object<CKBehavior>(script.script);
            return behavior && behavior->GetOwner() ? behavior->GetOwner()->GetID() : CK_ID(0);
        });
        IM_CHECK_SILENT(owner != 0);
        // The name row (Dev's closed Script height is 20).
        ctx->MouseMoveToPos(ImVec2(script.headerMin.x + 40.0f, script.headerMin.y + 8.0f));
        ctx->MouseClick(ImGuiMouseButton_Right);
        ctx->Yield(3);
        ctx->ItemClick("//$FOCUSED/Show Object In Level Manager");
        ctx->Yield(3);
        IM_CHECK(InFront(ctx, "//Level Manager"));
        ImGuiWindow *level = ctx->GetWindowByRef("//Level Manager");
        bool shown = false;
        for (const LevelRow &row : App().Panels().ShownLevelRows())
            shown |= row.object == owner && level && row.min.y >= level->InnerClipRect.Min.y &&
                     row.max.y <= level->InnerClipRect.Max.y;
        IM_CHECK(shown);
    };

    // Shift+drag of an input parameter of a behavior of the Script onto the
    // Script, its own graph: refused (a parameter leaves its graph for one
    // enclosing it), told in the Event Log, nothing recorded.
    t = IM_REGISTER_TEST(engine, "schematic", "shift_export_refused");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Schematic");
        std::optional<vdev::SchematicCanvas::ShownPin> found;
        for (const vdev::SchematicCanvas::ShownPin &pin : App().Canvas().ShownPins())
            if (!found && pin.kind == vdev::PinKind::ParamIn) {
                const CK_ID owner = pin.owner, script = pin.script;
                const bool inRoot = Read<bool>(ctx, [owner, script] {
                    CKBehavior *behavior = Object<CKBehavior>(owner);
                    return behavior && behavior->GetParent() && behavior->GetParent()->GetID() == script;
                });
                if (inRoot) found = pin;
            }
        IM_CHECK_SILENT(found.has_value());
        const std::optional<std::pair<CK_ID, ImVec2>> point = EmptySchematicPoint(found->script);
        IM_CHECK_SILENT(point.has_value());
        vdev::EditorState &state = App().State();
        const size_t steps = state.History().UndoCount(), logged = state.log.size();
        ctx->KeyDown(ImGuiMod_Shift);
        ctx->MouseMoveToPos(found->center);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos(ImVec2((found->center.x + point->second.x) * 0.5f, (found->center.y + point->second.y) * 0.5f));
        ctx->MouseMoveToPos(point->second);
        ctx->MouseUp(0);
        ctx->KeyUp(ImGuiMod_Shift);
        ctx->Yield(3);
        bool told = false;
        for (size_t i = logged; i < state.log.size(); ++i)
            told |= state.log[i].text.find("outside its behavior graph") != std::string::npos;
        LogEditor(ctx, logged);
        IM_CHECK(told);
        IM_CHECK_EQ(state.History().UndoCount(), steps);
        IM_CHECK(!state.document->Dirty());
    };

    // A click on a Script's Start (on its header), a click on a Building
    // Block's input of its root graph: a link from Start, one undo step;
    // Ctrl+Z takes it off.
    t = IM_REGISTER_TEST(engine, "schematic", "start_click_link_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Schematic");
        // Bring the complete stack into view before caching pin coordinates;
        // a short viewport can otherwise autoscroll during the two clicks.
        ctx->KeyPress(ImGuiKey_KeypadSubtract);
        ctx->KeyPress(ImGuiKey_KeypadSubtract);
        ctx->KeyPress(ImGuiKey_End);
        ctx->Yield(3);
        // Auto Link on (a click on an I/O begins a link), as it was after.
        const bool autoLink = App().State().view.autoLink;
        OnMainThread(ctx, [] { App().State().view.autoLink = true; });
        // Start's links to the owner's inputs (none wanted: a new one is made).
        auto startLinks = [](CK_ID script, CK_ID owner) {
            CKBehavior *graph = Object<CKBehavior>(script);
            CKBehaviorIO *in = graph && graph->GetInputCount() > 0 ? graph->GetInput(0) : nullptr;
            int count = 0;
            for (int i = 0; graph && in && i < graph->GetSubBehaviorLinkCount(); ++i) {
                CKBehaviorLink *link = graph->GetSubBehaviorLink(i);
                CKBehaviorIO *to = link ? link->GetOutBehaviorIO() : nullptr;
                if (link->GetInBehaviorIO() == in && to && to->GetOwner() && to->GetOwner()->GetID() == owner) ++count;
            }
            return count;
        };
        std::optional<vdev::SchematicCanvas::ShownScript> script;
        std::optional<vdev::SchematicCanvas::ShownPin> input;
        for (const vdev::SchematicCanvas::ShownScript &shown : App().Canvas().ShownScripts()) {
            if (script || !shown.hasStart) continue;
            for (const vdev::SchematicCanvas::ShownPin &pin : App().Canvas().ShownPins()) {
                if (pin.script != shown.script || pin.kind != vdev::PinKind::BehaviorIn) continue;
                const CK_ID owner = pin.owner, id = shown.script;
                const bool fits = Read<bool>(ctx, [&] {
                    CKBehavior *behavior = Object<CKBehavior>(owner);
                    return behavior && behavior->GetParent() && behavior->GetParent()->GetID() == id &&
                           startLinks(id, owner) == 0;
                });
                if (fits) {
                    script = shown;
                    input = pin;
                    break;
                }
            }
        }
        IM_CHECK_SILENT(script.has_value() && input.has_value());
        vdev::EditorState &state = App().State();
        const size_t steps = state.History().UndoCount();
        const CK_ID id = script->script, owner = input->owner;
        ctx->MouseMoveToPos(script->start);
        ctx->MouseClick(0);
        ctx->Yield(2);
        ctx->MouseMoveToPos(input->center);
        ctx->MouseClick(0);
        ctx->Yield(3);
        IM_CHECK_EQ(Read<int>(ctx, [&] { return startLinks(id, owner); }), 1);
        IM_CHECK_EQ(state.History().UndoCount(), steps + 1);
        IM_CHECK(state.document->Dirty());
        Undo(ctx);
        IM_CHECK_EQ(Read<int>(ctx, [&] { return startLinks(id, owner); }), 0);
        IM_CHECK(!state.document->Dirty());
        OnMainThread(ctx, [autoLink] { App().State().view.autoLink = autoLink; });
    };

    // H over the Schematic: the Message Explorer; H again closes it.
    t = IM_REGISTER_TEST(engine, "schematic", "message_explorer");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Schematic");
        const std::optional<std::pair<CK_ID, ImVec2>> point = EmptySchematicPoint();
        IM_CHECK_SILENT(point.has_value());
        ctx->MouseMoveToPos(point->second);
        ctx->KeyPress(ImGuiKey_H);
        ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, "//Message Explorer"));
        ctx->WindowFocus("//Schematic");
        ctx->MouseMoveToPos(point->second);
        ctx->KeyPress(ImGuiKey_H);
        ctx->Yield(3);
        IM_CHECK(!WindowShown(ctx, "//Message Explorer"));
    };

    // A Building Block grouped into a behavior graph, the graph deleted (Del):
    // "Confirm Graph(es) Deletion" first (Dev.exe sub_50E430); No keeps it,
    // Yes deletes it; Ctrl+Z brings it back.
    t = IM_REGISTER_TEST(engine, "schematic", "delete_graph_confirm");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const std::optional<Shown> block = ClickFirstBlock(ctx);
        IM_CHECK_SILENT(block.has_value());
        vdev::EditorState &state = App().State();
        // Grouped by the core command (the test is the deletion): the block's
        // parent is then the new graph, selected for Del.
        CK_ID graph = 0, parent = 0;
        OnMainThread(ctx, [&] {
            CKContext *context = state.document->Context();
            CKBehavior *behavior = CKBehavior::Cast(context->GetObject(block->behavior));
            parent = behavior && behavior->GetParent() ? behavior->GetParent()->GetID() : 0;
            if (!vdev::GroupBehaviors(*state.document, block->script, {block->behavior}).ok) return;
            graph = behavior->GetParent() ? behavior->GetParent()->GetID() : 0;
        });
        IM_CHECK_SILENT(graph != 0 && graph != parent);
        auto alive = [&] { return Read<bool>(ctx, [&] { return !state.document->IsDiscarded(graph) && state.document->Context()->GetObject(graph); }); };
        auto selectGraph = [&] {
            OnMainThread(ctx, [&] { state.selection.Set(block->script, vdev::ElementRef{vdev::ElementKind::Behavior, parent, graph}); });
            ctx->Yield(2);
        };
        const size_t steps = state.History().UndoCount();
        selectGraph();
        ctx->KeyPress(ImGuiKey_Delete);
        ctx->Yield(3);
        IM_CHECK_SILENT(WindowShown(ctx, "//Warning!###confirmgraphs"));
        ctx->ItemClick("//Warning!###confirmgraphs/No");
        ctx->Yield(3);
        IM_CHECK(alive());
        IM_CHECK_EQ(state.History().UndoCount(), steps);
        selectGraph();
        ctx->KeyPress(ImGuiKey_Delete);
        ctx->Yield(3);
        IM_CHECK_SILENT(WindowShown(ctx, "//Warning!###confirmgraphs"));
        ctx->ItemClick("//Warning!###confirmgraphs/Yes");
        ctx->Yield(3);
        IM_CHECK(!alive());
        IM_CHECK_EQ(state.History().UndoCount(), steps + 1);
        Undo(ctx);
        IM_CHECK(alive());
        Undo(ctx); // the grouping
        IM_CHECK(!state.document->Dirty());
    };

    // Keypad + and -: the Schematic's three zoom levels (Dev.exe sub_52C680:
    // "+" 75% below it, else 100%; "-" 50% at or below 75%, else 75%).
    t = IM_REGISTER_TEST(engine, "schematic", "zoom_levels");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Schematic");
        const std::optional<std::pair<CK_ID, ImVec2>> point = EmptySchematicPoint();
        IM_CHECK_SILENT(point.has_value());
        ctx->MouseMoveToPos(point->second);
        ctx->MouseClick(0);
        ctx->Yield(2);
        auto press = [&](ImGuiKey key) {
            ctx->KeyPress(key);
            ctx->Yield(3);
            return App().Canvas().GlobalZoom();
        };
        const float start = App().Canvas().GlobalZoom();
        IM_CHECK_EQ(press(ImGuiKey_KeypadAdd), start < 0.75f ? 0.75f : 1.0f);
        IM_CHECK_EQ(press(ImGuiKey_KeypadAdd), 1.0f);
        IM_CHECK_EQ(press(ImGuiKey_KeypadSubtract), 0.75f);
        IM_CHECK_EQ(press(ImGuiKey_KeypadSubtract), 0.5f);
        IM_CHECK_EQ(press(ImGuiKey_KeypadSubtract), 0.5f);
        IM_CHECK_EQ(press(ImGuiKey_KeypadAdd), 0.75f);
        press(ImGuiKey_KeypadAdd);
    };

    // A local feeding two inputs of the same type, one input's type changed
    // to String in Edit Parameter: Dev's "Invalid Parameter Link" (Dev.exe
    // sub_536670) asked first; OK deletes the link the new type no longer
    // fits, the type changes, all in one step that Ctrl+Z takes back.
    t = IM_REGISTER_TEST(engine, "schematic", "invalid_link_type_change");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Schematic");
        vdev::EditorState &state = App().State();
        // Two Logics Building Blocks with an Int input inserted in an open
        // Script, at an empty place (core commands; the test is the dialog).
        const std::optional<std::pair<CK_ID, ImVec2>> point = EmptySchematicPoint();
        IM_CHECK_SILENT(point.has_value());
        const size_t steps = state.History().UndoCount();
        CK_ID script = point->first, a = 0, b = 0, local = 0;
        OnMainThread(ctx, [&] {
            CKContext *context = state.document->Context();
            for (const vdev::BuildingBlockEntry &entry : state.catalog.Entries()) {
                if (entry.category.rfind("Logics", 0) != 0) continue;
                CKBehaviorPrototype *prototype = CKGetPrototypeFromGuid(entry.guid);
                if (!prototype || prototype->GetInputCount() < 1) continue;
                int index = -1;
                for (int i = 0; i < prototype->GetInParameterCount() && index < 0; ++i)
                    if (prototype->GetInParameterList()[i]->Guid == CKPGUID_INT) index = i;
                if (index < 0) continue;
                for (float y : {300.0f, 360.0f}) {
                    const vdev::EditResult made =
                        vdev::InsertBuildingBlock(*state.document, script, script, entry.guid, ::Point(600.0f, y));
                    CKBehavior *block = made.ok ? CKBehavior::Cast(context->GetObject(made.created)) : nullptr;
                    CKParameterIn *input = block ? block->GetInputParameter(index) : nullptr;
                    if (input) (a ? b : a) = input->GetID();
                }
                break;
            }
        });
        IM_CHECK_SILENT(a && b);
        CKGUID type;
        OnMainThread(ctx, [&] {
            CKContext *context = state.document->Context();
            CKParameterIn *first = CKParameterIn::Cast(context->GetObject(a));
            type = first->GetGUID();
            const vdev::EditResult made =
                vdev::AddLocalParameter(*state.document, script, script, "UI Two Readers", type, ::Point(40.0f, 40.0f));
            if (!made.ok) return;
            local = made.created;
            vdev::SchematicModel model;
            model.Build(*state.document->Session(script), context);
            int slot = -1;
            for (const auto &param : model.params)
                if (param.parameter == local) slot = param.slot;
            for (CK_ID input : {a, b}) {
                CKParameterIn *in = CKParameterIn::Cast(context->GetObject(input));
                CKBehavior *owner = CKBehavior::Cast(in->GetOwner());
                int index = -1;
                for (int i = 0; owner && i < owner->GetInputParameterCount(); ++i)
                    if (owner->GetInputParameter(i) == in) index = i;
                vdev::CreateParameterLink(*state.document, script, ::LinkEndpoint(script, slot, ENDPOINT_PLOCAL),
                                          ::LinkEndpoint(owner->GetID(), index, ENDPOINT_PIN));
            }
        });
        IM_CHECK_SILENT(local != 0);
        auto source = [&](CK_ID input) {
            return Read<CK_ID>(ctx, [&] {
                CKParameterIn *in = CKParameterIn::Cast(state.document->Context()->GetObject(input));
                return in && in->GetDirectSource() ? in->GetDirectSource()->GetID() : 0;
            });
        };
        IM_CHECK_SILENT(source(a) == local && source(b) == local);
        const size_t linked = state.History().UndoCount();
        // The input's pin right-clicked, Edit Parameter.
        CK_ID owner = 0;
        int index = -1;
        OnMainThread(ctx, [&] {
            CKParameterIn *in = CKParameterIn::Cast(state.document->Context()->GetObject(a));
            CKBehavior *behavior = CKBehavior::Cast(in->GetOwner());
            owner = behavior->GetID();
            for (int i = 0; i < behavior->GetInputParameterCount(); ++i)
                if (behavior->GetInputParameter(i) == in) index = i;
            // Scrolled to (the blocks are inserted out of the view).
            CKBehavior *graph = behavior->GetParent();
            state.events.Post(vdev::ShowElement{script, vdev::ElementRef{vdev::ElementKind::Behavior, graph ? graph->GetID() : script, owner}, owner});
        });
        ctx->Yield(6);
        std::optional<ImVec2> pin;
        for (const auto &shown : App().Canvas().ShownPins())
            if (shown.owner == owner && shown.kind == vdev::PinKind::ParamIn && shown.index == index) pin = shown.center;
        IM_CHECK_SILENT(pin.has_value());
        ctx->MouseMoveToPos(*pin);
        ctx->MouseClick(1);
        ctx->Yield(3);
        ctx->ItemClick("//$FOCUSED/Edit Parameter");
        ctx->Yield(3);
        IM_CHECK_SILENT(WindowShown(ctx, "//###EditParameter"));
        ctx->ItemClick("//###EditParameter/##type/##combo");
        ctx->Yield(2);
        // Typed (the list's type-ahead moves to it), Enter takes it - and is
        // not the dialog's OK as well: the dialog stays.
        ctx->KeyChars("String");
        ctx->Yield(3);
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        IM_CHECK_SILENT(WindowShown(ctx, "//###EditParameter"));
        IM_CHECK_EQ(state.History().UndoCount(), linked);
        ctx->ItemClick("//###EditParameter/OK/##dialogbutton");
        ctx->Yield(3);
        // Asked first: nothing changed yet.
        IM_CHECK_SILENT(WindowShown(ctx, "//Invalid Parameter Link"));
        IM_CHECK_EQ(state.History().UndoCount(), linked);
        ctx->ItemClick("//Invalid Parameter Link/OK");
        ctx->Yield(4);
        auto guidOf = [&](CK_ID input) {
            return Read<CKGUID>(ctx, [&] { return CKParameterIn::Cast(state.document->Context()->GetObject(input))->GetGUID(); });
        };
        IM_CHECK(guidOf(a) == CKPGUID_STRING);
        IM_CHECK(source(a) != local && source(b) == local);
        IM_CHECK_EQ(state.History().UndoCount(), linked + 1);
        Undo(ctx);
        IM_CHECK(guidOf(a) == type && source(a) == local);
        while (state.History().UndoCount() > steps) Undo(ctx);
        IM_CHECK(!state.document->Dirty());
    };
}

} // namespace vdevuitest
