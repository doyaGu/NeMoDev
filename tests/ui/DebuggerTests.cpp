// "debugger": the Script Debugger on the sample opened from the command line
// (Scripts that run): opened from the Schematic's toolbar (Dbg); the status
// bar's Play (the 3D Layout in Player Mode), Pause, Step One Frame and
// Restore Initial Conditions; a breakpoint (F9) on a Building Block, Debug
// Inside Frames and Play stopping at it, the breakpoint taken off again; the
// Script Debugger's Add Breakpoint (a click in the Schematic) and its
// Breakpoint List (Remove Breakpoint, Ok), undone.

#include <cstring>
#include <optional>
#include <atomic>
#include <condition_variable>
#include <mutex>

#include "tests/ui/UiTests.h"
#include "workspace/EditorClient.h"

namespace vdevuitest {

namespace {

// The status bar's bitmap buttons (StatusBar.cpp: DevBitmapButton's ids).
constexpr const char *PLAY = "//##StatusBar/play/##bitmap";
constexpr const char *STEP = "//##StatusBar/step/##bitmap";
constexpr const char *REWIND = "//##StatusBar/rewind/##bitmap";

const vdev::Debugger &TheDebugger() { return *App().State().debugger; }

// The Schematic in front, its toolbar's Dbg: the Script Debugger shown.
bool OpenDebugger(ImGuiTestContext *ctx) {
    Settle(ctx, SchematicRef().c_str());
    if (!App().State().debugger) return false;
    if (!App().State().workspace->Role("main.debugger")) ctx->ItemClick(SchematicRef("/**/debugger/##bitmap").c_str());
    ctx->Yield(3);
    ImGuiWindow *window = ctx->GetWindowByRef(RuntimeRef("debugger").c_str());
    return window && window->WasActive;
}
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    std::atomic<bool> entered{false};
    bool released = false;
    void Release() { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
};
struct ReleaseGate { std::shared_ptr<Gate> gate; ~ReleaseGate() { gate->Release(); } };
std::function<void()> runtimeWork;
void RuntimeGui(ImGuiTestContext *ctx) {
    if (runtimeWork) { runtimeWork(); runtimeWork = {}; }
    MainThreadGui(ctx);
}
void OnRuntimeThread(ImGuiTestContext *ctx, std::function<void()> work) {
    runtimeWork = std::move(work);
    for (int i = 0; runtimeWork && i < 100; ++i) ctx->Yield();
    IM_CHECK_NO_RET(!runtimeWork);
    runtimeWork = {};
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
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(OpenDebugger(ctx));
        const vdev::Debugger &debugger = TheDebugger();
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
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(OpenDebugger(ctx));
        const vdev::Debugger &debugger = TheDebugger();
        const size_t before = debugger.Breakpoints().size();
        Settle(ctx, SchematicRef().c_str());
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
        if (!debugger.insideFrames) ctx->ItemClick(RuntimeRef("debugger", "/Debug Inside Frames").c_str());
        IM_CHECK(debugger.insideFrames && debugger.trace);
        ctx->ItemClick(PLAY);
        IM_CHECK(WaitFor(ctx, 300, [&] { return debugger.Reached() > reached; }));
        IM_CHECK(debugger.GetState() == vdev::Debugger::State::Paused);
        IM_CHECK_EQ(debugger.CurrentBehavior(), block->behavior);
        IM_CHECK(!debugger.ReachedBreakpoints().empty());
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &editor = state.workspace->Application();
            const EditorValue args[] = {vdev::IntegerArgument("index", 0), vdev::IntegerArgument("reached", uint64_t(debugger.Reached()))};
            const auto hit = editor.Query("runtime.reached-breakpoint", {editor.Scope(), args, 2});
            IM_CHECK_NO_RET(hit.status == EDITOR_OK);
            const auto *target = vdev::EditorClient::Find(hit, "object"), *behavior = vdev::EditorClient::Find(hit, "behavior");
            IM_CHECK_NO_RET(target && behavior && target->object == state.document->Reference(block->behavior) && behavior->object == target->object);
        });
        ctx->ItemClick(REWIND);
        ctx->ItemClick(RuntimeRef("debugger", "/Debug Inside Frames").c_str());
        ctx->Yield(2);
        IM_CHECK(!debugger.insideFrames);
        OnMainThread(ctx, [] {
            auto &editor = App().State().workspace->Application();
            const EditorValue value{EDITOR_TEXT("enabled"), EDITOR_INTEGER_VALUE, {}, {}, 0};
            editor.Execute("runtime.set-trace", {editor.Scope(), &value, 1});
        });
        Settle(ctx, SchematicRef().c_str());
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
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(OpenDebugger(ctx));
        const vdev::Debugger &debugger = TheDebugger();
        IM_CHECK_SILENT(debugger.Breakpoints().empty());
        const bool dirty = App().State().document->Dirty();
        const std::optional<vdev::SchematicCanvas::ShownBehavior> block = FirstBlock();
        IM_CHECK_SILENT(block.has_value());
        ctx->ItemClick(RuntimeRef("debugger", "/Add Breakpoint").c_str());
        ctx->Yield(2);
        ctx->MouseMoveToPos(Center(block->min, block->max));
        ctx->MouseClick(0);
        ctx->Yield(3);
        IM_CHECK_EQ(debugger.Breakpoints().size(), size_t(1));
        ctx->ItemClick(RuntimeRef("debugger", "/Breakpoint List").c_str());
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

    t = IM_REGISTER_TEST(engine, "debugger", "independent_registered_debugger_windows");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &workspace = *App().State().workspace;
        auto &editor = workspace.Application();
        EditorView first = 0, second = 0;
        size_t undo = 0; bool redo = false;
        vdev::ObjectRef selected{};
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(editor.Execute("runtime.close-tools", {editor.Scope(), nullptr, 0}).status == EDITOR_OK);
            IM_CHECK_NO_RET(editor.Execute("runtime.open-debugger", {editor.Scope(), nullptr, 0}).status == EDITOR_OK);
            first = workspace.Role("main.debugger"); second = workspace.Open("runtime.debugger");
            IM_CHECK_NO_RET(first && second && first != second);
            undo = App().State().History().UndoCount(); redo = App().State().History().CanRedo();
            selected = editor.Selection();
        });
        ctx->Yield(5);
        const auto log = [&](EditorView id) {
            return Read<uint64_t>(ctx, [&] { return vdev::EditorClient::Integer(workspace.Inspect(id), "log-visible"); });
        };
        IM_CHECK(log(first) && log(second));
        const auto window = "//" + workspace.WindowName(first);
        ctx->WindowFocus(window.c_str()); ctx->Yield(2);
        ctx->ItemClick((window + "/Debug Log").c_str()); ctx->Yield(3);
        IM_CHECK(!log(first) && log(second));
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(editor.Execute("runtime.close-tools", {editor.Scope(second), nullptr, 0}).status == EDITOR_OK);
            IM_CHECK_NO_RET(workspace.WindowName(first).empty() && workspace.WindowName(second).empty());
            IM_CHECK_NO_RET(App().State().History().UndoCount() == undo && App().State().History().CanRedo() == redo && editor.Selection() == selected);
        });
    };

    t = IM_REGISTER_TEST(engine, "debugger", "profiler_views_restore_original_setting");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State(); auto &workspace = *state.workspace; auto &editor = workspace.Application();
        EditorView first = 0, second = 0;
        bool originally = false;
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(editor.Execute("runtime.close-tools", {editor.Scope(), nullptr, 0}).status == EDITOR_OK);
            originally = state.document->Context()->IsProfilingEnable() != 0;
            state.document->Context()->EnableProfiling(FALSE);
            IM_CHECK_NO_RET(editor.Execute("runtime.open-profiler", {editor.Scope(), nullptr, 0}).status == EDITOR_OK);
            first = workspace.Role("main.profiler"); second = workspace.Open("runtime.profiler");
            IM_CHECK_NO_RET(first && second && first != second);
        });
        ctx->Yield(7);
        IM_CHECK(Read<bool>(ctx, [&] { return state.document->Context()->IsProfilingEnable() != 0; }));
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(workspace.Close(first)); }); ctx->Yield(4);
        IM_CHECK(Read<bool>(ctx, [&] { return state.document->Context()->IsProfilingEnable() != 0; }));
        ctx->WindowFocus(("//" + workspace.WindowName(second)).c_str()); ctx->Yield(2);
        ctx->ItemClick(("//" + workspace.WindowName(second) + "/##details").c_str()); ctx->Yield(3);
        OnMainThread(ctx, [&] {
            const auto value = vdev::IntegerArgument("details", 1);
            const auto profile = editor.Query("runtime.profile", {editor.Scope(second), &value, 1});
            IM_CHECK_NO_RET(profile.status == EDITOR_OK && vdev::EditorClient::Integer(profile, "row-count") > 0);
            IM_CHECK_NO_RET(workspace.Close(second));
        }); ctx->Yield(4);
        IM_CHECK(!Read<bool>(ctx, [&] { return state.document->Context()->IsProfilingEnable() != 0; }));
        OnMainThread(ctx, [&] {
            state.document->Context()->EnableProfiling(TRUE);
            editor.Execute("runtime.open-profiler", {editor.Scope(), nullptr, 0});
        }); ctx->Yield(5);
        OnMainThread(ctx, [&] { workspace.Close(workspace.Role("main.profiler")); }); ctx->Yield(4);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(state.document->Context()->IsProfilingEnable());
            state.document->Context()->EnableProfiling(originally ? TRUE : FALSE);
        });
    };

    t = IM_REGISTER_TEST(engine, "debugger", "published_runtime_views_during_file_work");
    t->GuiFunc = RuntimeGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State(); auto &workspace = *state.workspace; auto &editor = workspace.Application();
        auto gate = std::make_shared<Gate>(); ReleaseGate release{gate};
        EditorView profiler = 0, parameters = 0;
        std::vector<vdev::ObjectRef> watches;
        std::vector<std::string> values;
        uint64_t revision = 0;
        size_t undo = 0; bool redo = false;
        vdev::ObjectRef selected{};
        bool originally = false;
        OnMainThread(ctx, [&] {
            editor.Execute("runtime.close-tools", {editor.Scope(), nullptr, 0});
            originally = state.document->Context()->IsProfilingEnable() != 0;
            state.document->Context()->EnableProfiling(FALSE);
            editor.Execute("runtime.open-debugger", {editor.Scope(), nullptr, 0});
            editor.Execute("runtime.open-profiler", {editor.Scope(), nullptr, 0});
            profiler = workspace.Role("main.profiler");
            auto *parameter = state.document->Context()->CreateCKParameterLocal("Busy watch", CKPGUID_INT);
            IM_CHECK_NO_RET(parameter != nullptr);
            int value = 123; parameter->SetValue(&value);
            watches = {state.document->Reference(parameter->GetID())};
            const auto args = vdev::ObjectsArgument("objects", watches);
            IM_CHECK_NO_RET(editor.Execute("runtime.watch-parameters", {editor.Scope(), &args, 1}).status == EDITOR_OK);
            parameters = workspace.Role("main.parameters");
        }); ctx->Yield(6);
        OnMainThread(ctx, [&] {
            const auto profile = editor.Query("runtime.profile", {editor.Scope(), nullptr, 0});
            IM_CHECK_NO_RET(profile.status == EDITOR_OK && vdev::EditorClient::Integer(profile, "available"));
            revision = vdev::EditorClient::Integer(profile, "revision");
            const auto watched = vdev::EditorClient(editor.Host(), parameters).Inspect(parameters);
            IM_CHECK_NO_RET(watched.status == EDITOR_OK);
            values = vdev::EditorClient::Find(watched, "values")->texts;
            IM_CHECK_NO_RET(values == std::vector<std::string>{"123"});
            undo = state.History().UndoCount(); redo = state.History().CanRedo(); selected = editor.Selection();
            IM_CHECK_NO_RET(state.RunRuntime("Runtime projection regression", [gate] {
                std::unique_lock<std::mutex> lock(gate->mutex); gate->entered = true;
                gate->changed.wait(lock, [&] { return gate->released; });
            }));
        });
        IM_CHECK(WaitFor(ctx, 100, [&] { return gate->entered.load(); })); ctx->Yield(5);
        const auto *debuggerWindow = ctx->GetWindowByRef(RuntimeRef("debugger").c_str());
        const auto *profilerWindow = ctx->GetWindowByRef(RuntimeRef("profiler").c_str());
        const auto *parameterWindow = ctx->GetWindowByRef(RuntimeRef("parameters").c_str());
        IM_CHECK(debuggerWindow && profilerWindow && parameterWindow && debuggerWindow->WasActive && profilerWindow->WasActive && parameterWindow->WasActive);
        OnRuntimeThread(ctx, [&] {
            IM_CHECK_NO_RET(state.runtimeOperation.Busy());
            const auto profile = editor.Query("runtime.profile", {editor.Scope(), nullptr, 0});
            IM_CHECK_NO_RET(profile.status == EDITOR_OK && vdev::EditorClient::Integer(profile, "revision") == revision);
            IM_CHECK_NO_RET(editor.Execute("runtime.play", {editor.Scope(), nullptr, 0}).status == EDITOR_REFUSED);
            const vdev::EditorClient client(editor.Host(), parameters);
            const auto watched = client.Inspect(parameters);
            IM_CHECK_NO_RET(watched.status == EDITOR_OK && vdev::EditorClient::Find(watched, "objects")->objects == watches &&
                            vdev::EditorClient::Find(watched, "values")->texts == values);
            IM_CHECK_NO_RET(workspace.Close(parameters));
            IM_CHECK_NO_RET(workspace.Close(profiler));
        });
        gate->Release();
        IM_CHECK(WaitFor(ctx, 100, [&] { return !state.runtimeOperation.Busy(); })); ctx->Yield(5);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(!state.document->Context()->IsProfilingEnable());
            IM_CHECK_NO_RET(state.History().UndoCount() == undo && state.History().CanRedo() == redo && editor.Selection() == selected);
            editor.Execute("runtime.close-tools", {editor.Scope(), nullptr, 0});
            state.document->Context()->EnableProfiling(originally ? TRUE : FALSE);
        });
    };
    t = IM_REGISTER_TEST(engine, "debugger", "environment_views_during_file_work");
    t->GuiFunc = RuntimeGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State(); auto &workspace = *state.workspace; auto &editor = workspace.Application();
        auto gate = std::make_shared<Gate>(); ReleaseGate release{gate};
        vdev::EditorResponse plugins, paths;
        EditorView pluginView = 0, pathView = 0;
        size_t undo = 0; bool redo = false, dirty = false; vdev::ObjectRef selected{};
        OnMainThread(ctx, [&] {
            editor.Execute("environment.open-plugins", {editor.Scope(), nullptr, 0});
            editor.Execute("environment.open-paths", {editor.Scope(), nullptr, 0});
            pluginView = workspace.Role("main.plugins"); pathView = workspace.Role("main.paths");
        }); ctx->Yield(4);
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), pluginView);
            plugins = client.Query("plugins.list"); paths = client.Query("paths.list");
            IM_CHECK_NO_RET(plugins.status == EDITOR_OK && paths.status == EDITOR_OK);
            undo = state.History().UndoCount(); redo = state.History().CanRedo(); dirty = state.document->Dirty(); selected = editor.Selection();
            IM_CHECK_NO_RET(state.RunRuntime("Environment projection regression", [gate] {
                std::unique_lock<std::mutex> lock(gate->mutex); gate->entered = true;
                gate->changed.wait(lock, [&] { return gate->released; });
            }));
        });
        IM_CHECK(WaitFor(ctx, 100, [&] { return gate->entered.load(); })); ctx->Yield(4);
        const auto *pluginWindow = ctx->GetWindowByRef(EnvironmentRef("plugins").c_str());
        const auto *pathWindow = ctx->GetWindowByRef(EnvironmentRef("paths").c_str());
        IM_CHECK(pluginWindow && pathWindow && pluginWindow->WasActive && pathWindow->WasActive);
        OnRuntimeThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), pluginView);
            IM_CHECK_NO_RET(client.Query("plugins.list").fields == plugins.fields && client.Query("paths.list").fields == paths.fields);
            const auto *categories = vdev::EditorClient::Find(paths, "categories");
            IM_CHECK_NO_RET(categories != nullptr);
            const std::vector<vdev::EditorField> fields{*categories}; const auto args = vdev::BorrowEditorFields(fields);
            IM_CHECK_NO_RET(client.Command("paths.apply", {vdev::IntegerArgument("revision", vdev::EditorClient::Integer(paths, "revision")), args.values.front()}).status == EDITOR_REFUSED);
            IM_CHECK_NO_RET(workspace.Close(pluginView) && workspace.Close(pathView));
        });
        gate->Release(); IM_CHECK(WaitFor(ctx, 100, [&] { return !state.runtimeOperation.Busy(); })); ctx->Yield(4);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(state.History().UndoCount() == undo && state.History().CanRedo() == redo && state.document->Dirty() == dirty && editor.Selection() == selected);
            IM_CHECK_NO_RET(!workspace.Role("main.plugins") && !workspace.Role("main.paths"));
            IM_CHECK_NO_RET(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
        });
    };

    t = IM_REGISTER_TEST(engine, "debugger", "diagnostic_views_during_file_work");
    t->GuiFunc = RuntimeGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State(); auto &workspace = *state.workspace; auto &editor = workspace.Application();
        auto gate = std::make_shared<Gate>(); ReleaseGate release{gate};
        EditorView unused = 0, memory = 0, versions = 0, links = 0;
        OnMainThread(ctx, [&] {
            editor.Execute("diagnostics.open-unused", {editor.Scope(), nullptr, 0});
            editor.Execute("diagnostics.open-memory", {editor.Scope(), nullptr, 0});
            editor.Execute("diagnostics.open-versions", {editor.Scope(), nullptr, 0});
            unused = workspace.Role("main.unused"); memory = workspace.Role("main.memory"); versions = workspace.Role("main.versions");
        });
        bool ready = false;
        for (int i = 0; i < 150 && !ready; ++i) {
            ready = Read<bool>(ctx, [&] {
                const vdev::EditorClient client(editor.Host(), 0);
                return vdev::EditorClient::Integer(client.Inspect(unused), "revision") && vdev::EditorClient::Integer(client.Inspect(memory), "revision") &&
                    vdev::EditorClient::Integer(client.Inspect(versions), "revision");
            }); ctx->Yield();
        }
        IM_CHECK(ready);
        OnMainThread(ctx, [&] { editor.Execute("environment.open-link-options", {editor.Scope(), nullptr, 0}); links = workspace.Role("main.link-options"); }); ctx->Yield(3);
        vdev::EditorResponse beforeUnused, beforeMemory, beforeVersions, beforeLinks;
        size_t undo = 0; bool redo = false, dirty = false; vdev::ObjectRef selection{};
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), 0);
            beforeUnused = client.Inspect(unused); beforeMemory = client.Inspect(memory); beforeVersions = client.Inspect(versions); beforeLinks = client.Inspect(links);
            IM_CHECK_NO_RET(beforeUnused.status == EDITOR_OK && beforeMemory.status == EDITOR_OK && beforeVersions.status == EDITOR_OK && beforeLinks.status == EDITOR_OK);
            undo = state.History().UndoCount(); redo = state.History().CanRedo(); dirty = state.document->Dirty(); selection = editor.Selection();
            IM_CHECK_NO_RET(state.RunRuntime("Diagnostic projection regression", [gate] {
                std::unique_lock<std::mutex> lock(gate->mutex); gate->entered = true;
                gate->changed.wait(lock, [&] { return gate->released; });
            }));
        });
        IM_CHECK(WaitFor(ctx, 100, [&] { return gate->entered.load(); })); ctx->Yield(4);
        OnRuntimeThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), 0);
            IM_CHECK_NO_RET(client.Inspect(unused).fields == beforeUnused.fields && client.Inspect(memory).fields == beforeMemory.fields &&
                client.Inspect(versions).fields == beforeVersions.fields && client.Inspect(links).fields == beforeLinks.fields);
            IM_CHECK_NO_RET(vdev::EditorClient(editor.Host(), unused).Command("diagnostics.scan-unused").status == EDITOR_REFUSED);
            IM_CHECK_NO_RET(client.Command("diagnostics.close-tools").status == EDITOR_OK && client.Command("environment.close-managers").status == EDITOR_OK);
        });
        gate->Release(); IM_CHECK(WaitFor(ctx, 100, [&] { return !state.runtimeOperation.Busy(); })); ctx->Yield(3);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(state.History().UndoCount() == undo && state.History().CanRedo() == redo && state.document->Dirty() == dirty && editor.Selection() == selection);
            IM_CHECK_NO_RET(!workspace.Role("main.unused") && !workspace.Role("main.memory") && !workspace.Role("main.versions") && !workspace.Role("main.link-options"));
            IM_CHECK_NO_RET(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
        });
    };
    t = IM_REGISTER_TEST(engine, "debugger", "preferences_during_file_work");
    t->GuiFunc = RuntimeGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State(); auto &workspace = *state.workspace; auto &editor = workspace.Application();
        auto gate = std::make_shared<Gate>(); ReleaseGate release{gate};
        EditorView view = 0; vdev::EditorResponse beforeView, beforeValues, beforeCatalog;
        size_t undo = 0; bool redo = false, dirty = false; vdev::ObjectRef selection{};
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(editor.Execute("preferences.open", {editor.Scope(), nullptr, 0}).status == EDITOR_OK);
            view = workspace.Role("main.preferences");
        }); ctx->Yield(4);
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), 0);
            beforeView = client.Inspect(view); beforeValues = client.Query("preferences.values"); beforeCatalog = client.Query("preferences.catalog");
            IM_CHECK_NO_RET(beforeView.status == EDITOR_OK && beforeValues.status == EDITOR_OK && beforeCatalog.status == EDITOR_OK);
            undo = state.History().UndoCount(); redo = state.History().CanRedo(); dirty = state.document->Dirty(); selection = editor.Selection();
            IM_CHECK_NO_RET(state.RunRuntime("Preferences projection regression", [gate] {
                std::unique_lock<std::mutex> lock(gate->mutex); gate->entered = true;
                gate->changed.wait(lock, [&] { return gate->released; });
            }));
        });
        IM_CHECK(WaitFor(ctx, 100, [&] { return gate->entered.load(); })); ctx->Yield(4);
        OnRuntimeThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), 0);
            IM_CHECK_NO_RET(client.Inspect(view).fields == beforeView.fields && client.Query("preferences.values").fields == beforeValues.fields &&
                client.Query("preferences.catalog").fields == beforeCatalog.fields);
            const EditorValue args[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, vdev::EditorClient::Integer(beforeValues, "revision")},
                {EDITOR_TEXT("value"), EDITOR_RECORD}};
            IM_CHECK_NO_RET(editor.Execute("preferences.apply", {editor.Scope(view), args, 2}).status == EDITOR_REFUSED);
            IM_CHECK_NO_RET(client.Command("preferences.close").status == EDITOR_OK && !workspace.Role("main.preferences"));
            IM_CHECK_NO_RET(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
        });
        gate->Release(); IM_CHECK(WaitFor(ctx, 100, [&] { return !state.runtimeOperation.Busy(); })); ctx->Yield(3);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(state.History().UndoCount() == undo && state.History().CanRedo() == redo && state.document->Dirty() == dirty && editor.Selection() == selection);
        });
    };
}

} // namespace vdevuitest
