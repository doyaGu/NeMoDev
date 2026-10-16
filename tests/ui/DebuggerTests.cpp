// "debugger": the Script Debugger on the sample opened from the command line
// (Scripts that run): opened from the Schematic's toolbar (Dbg); the status
// bar's Play (the 3D Layout in Player Mode), Pause, Step One Frame and
// Restore Initial Conditions; a breakpoint (F9) on a Building Block, Debug
// Inside Frames and Play stopping at it, the breakpoint taken off again; the
// Script Debugger's Add Breakpoint (a click in the Schematic) and its
// Breakpoint List (Remove Breakpoint, Ok), undone.

#include <cstring>
#include <optional>

#include "tests/ui/UiTests.h"

namespace vdevuitest {

namespace {

constexpr const char *DEBUGGER = "//Script Debugger";
// The status bar's bitmap buttons (StatusBar.cpp: DevBitmapButton's ids).
constexpr const char *PLAY = "//##StatusBar/play/##bitmap";
constexpr const char *STEP = "//##StatusBar/step/##bitmap";
constexpr const char *REWIND = "//##StatusBar/rewind/##bitmap";

vdev::Debugger &TheDebugger() { return *App().State().debugger; }

// The Schematic in front, its toolbar's Dbg: the Script Debugger shown.
bool OpenDebugger(ImGuiTestContext *ctx) {
    Settle(ctx, "//Schematic");
    if (!App().State().debugger) return false;
    if (!App().State().showDebugger) ctx->ItemClick("//Schematic/**/debugger/##bitmap");
    ctx->Yield(3);
    ImGuiWindow *window = ctx->GetWindowByRef(DEBUGGER);
    return window && window->WasActive;
}

// Frames of the UI until done or the count runs out.
template <typename F> bool WaitFor(ImGuiTestContext *ctx, int frames, F done) {
    for (int i = 0; i < frames && !done(); ++i) ctx->Yield(1);
    return done();
}

// The first Building Block the Schematic shows.
std::optional<vdev::SchematicCanvas::ShownBehavior> FirstBlock() {
    for (const vdev::SchematicCanvas::ShownBehavior &shown : App().Canvas().ShownBehaviors())
        if (shown.buildingBlock) return shown;
    return std::nullopt;
}

} // namespace

void RegisterDebuggerTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    // Play: running, CK frames counted, the 3D Layout in Player Mode; Pause:
    // no frame more, Author Mode; Step One Frame: one frame; Restore Initial
    // Conditions: stopped, no frame counted.
    t = IM_REGISTER_TEST(engine, "debugger", "play_pause_step_reset");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(OpenDebugger(ctx));
        vdev::Debugger &debugger = TheDebugger();
        ctx->ItemClick(PLAY);
        IM_CHECK(WaitFor(ctx, 60, [&] { return debugger.Frames() > 3; }));
        IM_CHECK(debugger.GetState() == vdev::Debugger::State::Running);
        IM_CHECK(App().Layout().PlayerMode());
        ctx->ItemClick(PLAY);
        ctx->Yield(2);
        IM_CHECK(debugger.GetState() == vdev::Debugger::State::Paused);
        IM_CHECK(!App().Layout().PlayerMode());
        const int paused = debugger.Frames();
        ctx->Yield(10);
        IM_CHECK_EQ(debugger.Frames(), paused);
        ctx->ItemClick(STEP);
        ctx->Yield(2);
        IM_CHECK_EQ(debugger.Frames(), paused + 1);
        ctx->ItemClick(REWIND);
        ctx->Yield(2);
        IM_CHECK(debugger.GetState() == vdev::Debugger::State::Stopped);
        IM_CHECK_EQ(debugger.Frames(), 0);
    };

    // The first Building Block selected, F9: a breakpoint on it (one element
    // at a time, as Dev); Debug Inside Frames, Play: the run stops there
    // (paused, the Debug Context on it, Goto enabled). Restore, F9 again: the
    // breakpoint taken off.
    t = IM_REGISTER_TEST(engine, "debugger", "breakpoint_trace");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(OpenDebugger(ctx));
        vdev::Debugger &debugger = TheDebugger();
        const size_t before = debugger.Breakpoints().size();
        Settle(ctx, "//Schematic");
        const std::optional<vdev::SchematicCanvas::ShownBehavior> block = FirstBlock();
        IM_CHECK_SILENT(block.has_value());
        ctx->LogInfo("block '%s'", block->name.c_str());
        const ImVec2 at = Center(block->min, block->max);
        ctx->MouseMoveToPos(at);
        ctx->MouseClick(0);
        ctx->Yield(2);
        ctx->KeyPress(ImGuiKey_F9);
        ctx->Yield(3);
        IM_CHECK_SILENT(debugger.Breakpoints().size() == before + 1);
        const int reached = debugger.Reached();
        if (!debugger.insideFrames) ctx->ItemClick("//Script Debugger/Debug Inside Frames");
        IM_CHECK(debugger.insideFrames && debugger.trace);
        ctx->ItemClick(PLAY);
        IM_CHECK(WaitFor(ctx, 300, [&] { return debugger.Reached() > reached; }));
        IM_CHECK(debugger.GetState() == vdev::Debugger::State::Paused);
        IM_CHECK_EQ(debugger.CurrentBehavior(), block->behavior);
        IM_CHECK(!debugger.ReachedBreakpoints().empty());
        ctx->ItemClick(REWIND);
        ctx->ItemClick("//Script Debugger/Debug Inside Frames");
        ctx->Yield(2);
        IM_CHECK(!debugger.insideFrames);
        debugger.trace = false;
        Settle(ctx, "//Schematic");
        ctx->MouseMoveToPos(at);
        ctx->MouseClick(0);
        ctx->Yield(2);
        ctx->KeyPress(ImGuiKey_F9);
        ctx->Yield(3);
        IM_CHECK_EQ(debugger.Breakpoints().size(), before);
    };

    // Add Breakpoint, then a click on the first Building Block: a breakpoint
    // on it (one undo step). Breakpoint List: it is listed; picked, Remove
    // Breakpoint, Ok: none left (one undo step); Ctrl+Z gives it back, Ctrl+Z
    // again takes it off, the composition unmodified.
    t = IM_REGISTER_TEST(engine, "debugger", "add_breakpoint_and_list");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(OpenDebugger(ctx));
        vdev::Debugger &debugger = TheDebugger();
        IM_CHECK_SILENT(debugger.Breakpoints().empty());
        const bool dirty = App().State().document->Dirty();
        const std::optional<vdev::SchematicCanvas::ShownBehavior> block = FirstBlock();
        IM_CHECK_SILENT(block.has_value());
        ctx->ItemClick("//Script Debugger/Add Breakpoint");
        ctx->Yield(2);
        ctx->MouseMoveToPos(Center(block->min, block->max));
        ctx->MouseClick(0);
        ctx->Yield(3);
        IM_CHECK_EQ(debugger.Breakpoints().size(), size_t(1));
        ctx->ItemClick("//Script Debugger/Breakpoint List");
        ctx->Yield(3);
        ImGuiWindow *list = ctx->GetWindowByRef("//$FOCUSED");
        IM_CHECK_SILENT(list != nullptr && std::strstr(list->Name, "BreakpointList"));
        // The list's rows are in its child window (vi::ListView).
        ImGuiWindow *rows = nullptr;
        for (ImGuiWindow *window : ctx->UiContext->Windows)
            if (window->ParentWindow == list && std::strstr(window->Name, "##breakpoints")) rows = window;
        IM_CHECK_SILENT(rows != nullptr);
        ctx->SetRef(rows);
        ctx->ItemClick("$$0/##row");
        ctx->SetRef(list);
        ctx->ItemClick("Remove Breakpoint");
        ctx->ItemClick("Ok/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK(debugger.Breakpoints().empty());
        Undo(ctx);
        IM_CHECK_EQ(debugger.Breakpoints().size(), size_t(1));
        Undo(ctx);
        IM_CHECK(debugger.Breakpoints().empty());
        IM_CHECK(App().State().document->Dirty() == dirty);
    };
}

} // namespace vdevuitest
