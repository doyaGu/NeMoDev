#include "tests/ui/UiTests.h"
#include "core/runtime/Resources.h"
#include "core/document/ToolCommands.h"
#include "workspace/ResourceDrag.h"
#include "ui/help/HelpWindow.h"
#include "workspace/EditorClient.h"
#include "features/scene/SceneViews.h"
#include "features/scene/SceneViewData.h"
#include "core/level/LevelCommands.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <condition_variable>
#include <fstream>
#include <mutex>

namespace vdevuitest {
namespace {
uint64_t Integer(const vdev::EditorResponse &response, const char *key) {
    for (const auto &field : response.fields) if (field.key == key) return field.integer;
    return 0;
}
std::vector<EditorView> Views(const char *type) {
    auto &app = App().State().workspace->Application();
    const auto response = app.Query("workspace.views", {app.Scope(), nullptr, 0});
    std::vector<EditorView> ids, result;
    std::vector<std::string> types;
    for (const auto &field : response.fields) {
        if (field.key == "views") ids = field.integers;
        if (field.key == "types") types = field.texts;
    }
    if (ids.size() != types.size()) return {};
    for (size_t i = 0; i < ids.size(); ++i) if (types[i] == type) result.push_back(ids[i]);
    return result;
}
vdev::EditorResponse Command(const char *id, const std::vector<EditorValue> &values = {}) {
    auto &editor = App().State().workspace->Application();
    return editor.Execute(id, {editor.Scope(), values.data(), uint32_t(values.size())});
}
std::string Window(EditorView view) { return "//" + App().State().workspace->WindowName(view); }

// The normal test callback waits for native work. This callback is deliberately
// restricted to workspace commands/queries while a controlled worker is active.
std::function<void()> workspaceWork;
void WorkspaceGui(ImGuiTestContext *ctx) {
    if (workspaceWork) { auto work = std::move(workspaceWork); workspaceWork = {}; work(); }
    else MainThreadGui(ctx);
}
void OnWorkspaceThread(ImGuiTestContext *ctx, std::function<void()> work) {
    workspaceWork = std::move(work);
    for (int i = 0; workspaceWork && i < 100; ++i) ctx->Yield();
    IM_CHECK_NO_RET(!workspaceWork);
}
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool released = false;
    std::atomic<bool> entered{false};
    void Release() { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
};
struct ReleaseGate { std::shared_ptr<Gate> gate; ~ReleaseGate() { gate->Release(); } };

// A complete, small help book exercises the real reader and renderer without
// depending on the optional original help archive. Existing bytes are restored.
class HelpSample {
  public:
    explicit HelpSample(bool buildingBlock = false) : path(std::filesystem::u8path(vdev::DataFilePath("help/help.ndh"))) {
        existed = std::filesystem::exists(path);
        directoryExisted = std::filesystem::exists(path.parent_path());
        if (existed) {
            std::ifstream input(path, std::ios::binary);
            original.assign(std::istreambuf_iterator<char>(input), {});
        }
        std::string bytes = "NDEVHLP5";
        const auto number = [&](uint32_t value) { for (unsigned i = 0; i < 4; ++i) bytes += char(value >> (8 * i)); };
        const auto text = [&](const std::string &value) { number(uint32_t(value.size())); bytes += value; };
        text("home.html");
        number(buildingBlock ? 3 : 2);
        text("Home"); text("home.html"); number(0);
        text("Long Topic"); text("topic.html"); number(0);
        if (buildingBlock) { text("Rotate"); text("behaviors/3D Transformations/Rotate.html"); number(0); }
        number(buildingBlock ? 3 : 2);
        text("home.html"); text("Home");
        text("<h1>Workspace help sample</h1><a href='topic.html'>Read the long topic</a>"); text("Workspace home");
        text("topic.html"); text("Long Topic");
        std::string article = "<h1>Long Topic</h1>";
        for (int i = 0; i < 100; ++i) article += "<p>Reading position paragraph " + std::to_string(i) + "</p>";
        text(article); text("Long Topic Reading position");
        if (buildingBlock) {
            text("behaviors/3D Transformations/Rotate.html"); text("Rotate");
            text("<h1>Rotate</h1><p>Building Block help.</p>"); text("Rotate Building Block help");
        }
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), std::streamsize(bytes.size()));
        if (!output) throw std::runtime_error("Could not write help sample");
    }
    ~HelpSample() {
        if (existed) {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(original.data(), std::streamsize(original.size()));
        } else {
            std::error_code error;
            std::filesystem::remove(path, error);
            if (!directoryExisted) std::filesystem::remove(path.parent_path(), error);
        }
    }
  private:
    std::filesystem::path path;
    bool existed = false, directoryExisted = false;
    std::string original;
};
class ResourceSample {
  public:
    std::filesystem::path root;
    std::string library, graphs, first, second;
    ResourceSample() {
        namespace fs = std::filesystem;
        root = fs::temp_directory_path() / ("resource-workspace-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
        library = (root / "Library.rsc").u8string();
        std::string error;
        if (!vdev::CreateDataResource(library, &error)) throw std::runtime_error(error);
        graphs = (root / "Library/Behavior Graphs").u8string();
        first = (fs::u8path(graphs) / "One.nms").u8string();
        second = (fs::u8path(graphs) / "Two.nms").u8string();
        std::ofstream(fs::u8path(first)) << "Description fixture";
        std::ofstream(fs::u8path(second)) << "Description fixture";
        vdev::BehaviorDescription info;
        info.name = "One"; info.author = "Initial"; info.version = 1.5f; info.cid = CKCID_3DENTITY;
        if (!vdev::WriteBehaviorDescription(first, info)) throw std::runtime_error("write resource description fixture");
    }
    ~ResourceSample() { std::error_code ec; std::filesystem::remove_all(root, ec); }
};

}
void RegisterWorkspaceTests(ImGuiTestEngine *engine) {
    auto *test = IM_REGISTER_TEST(engine, "modules", "workspace_focus_replacement_and_event_log");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        EditorView log = 0, first = 0, second = 0;
        vdev::EditorConfiguration initial;
        vdev::DocumentToken token;
        size_t undo = 0;
        bool redo = false;
        uint64_t selection = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &workspace = *state.workspace;
            initial = workspace.Application().Configuration();
            token = state.document->Token(); undo = state.History().UndoCount(); redo = state.History().CanRedo();
            selection = state.objectSelection.Revision();
            state.Log(vdev::LogLevel::Error, "Workspace error sample");
            log = Integer(Command("log.open"), "view");
            const std::vector<EditorValue> type{{EDITOR_TEXT("type"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("object.summary")}};
            first = Integer(Command("workspace.open-view", type), "view");
            second = Integer(Command("workspace.open-view", type), "view");
            IM_CHECK_NO_RET(log && first && second && first != second);
        });
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            ImGui::SetWindowPos(App().State().workspace->WindowName(log).c_str(), {400, 90});
            ImGui::SetWindowSize(App().State().workspace->WindowName(log).c_str(), {530, 410});
            IM_CHECK_NO_RET(Command("workspace.focus-view", {{EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, {}, {}, first}}).status == EDITOR_OK);
        });
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            auto &workspace = *App().State().workspace;
            IM_CHECK_NO_RET(workspace.Focused() == first);
            const auto oldScope = workspace.Application().Scope(log);
            auto replacement = initial;
            for (auto &choice : replacement.providers)
                if (choice.kind == "view" && choice.id == "application.event-log") choice.provider = "example.log";
            IM_CHECK_NO_RET(Command("example.file-view-state", {{EDITOR_TEXT("refuse"), EDITOR_INTEGER_VALUE, {}, {}, 1}}).status == EDITOR_OK);
            std::string error;
            IM_CHECK_NO_RET(!workspace.Configure(replacement, error));
            IM_CHECK_NO_RET(workspace.Application().Scope(log).view == oldScope.view);
            IM_CHECK_NO_RET(Command("example.file-view-state", {{EDITOR_TEXT("refuse"), EDITOR_INTEGER_VALUE, {}, {}, 0}}).status == EDITOR_OK);
            IM_CHECK_NO_RET(workspace.Configure(replacement, error));
            const auto views = Views("application.event-log");
            IM_CHECK_NO_RET(views.size() == 1 && views[0] != log);
            IM_CHECK_NO_RET(!workspace.Application().Scope(log).view);
            log = views[0];
        });
        ctx->Yield(4);
        ImGuiWindow *window = ctx->GetWindowByRef(Window(log).c_str());
        IM_CHECK(window && std::fabs(window->Pos.x - 400) < 2 && std::fabs(window->Size.x - 530) < 2);
        ctx->ItemClick((Window(log) + "/Clear").c_str());
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &workspace = *state.workspace;
            IM_CHECK_NO_RET(state.eventLog.Entries().size() == 1 && state.eventLog.Entries()[0].text == "Event Log Cleared");
            IM_CHECK_NO_RET(state.document->Token() == token && state.History().UndoCount() == undo &&
                state.History().CanRedo() == redo && state.objectSelection.Revision() == selection);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(initial, error));
            const auto views = Views("application.event-log");
            IM_CHECK_NO_RET(views.size() == 1 && views[0] != log);
            log = views[0];
            IM_CHECK_NO_RET(workspace.Close(first) && workspace.Close(second) && workspace.Close(log));
            IM_CHECK_NO_RET(state.eventLog.Entries().size() == 1);
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "help_history_and_workspace_views_during_native_work");
    test->GuiFunc = WorkspaceGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        HelpSample sample;
        EditorView help = 0, log = 0;
        vdev::DocumentToken token;
        size_t undo = 0;
        bool redo = false;
        uint64_t selection = 0;
        vdev::EditorConfiguration initial;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            initial = state.workspace->Application().Configuration();
            token = state.document->Token(); undo = state.History().UndoCount(); redo = state.History().CanRedo();
            selection = state.objectSelection.Revision();
            help = Integer(Command("help.open", {{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("topic.html")}}), "view");
            IM_CHECK_NO_RET(help);
        });
        ctx->Yield(5);
        IM_CHECK(App().Help().CurrentTopic() == "topic.html");
        ctx->WindowResize(Window(help).c_str(), {820, 540});
        ctx->Yield(3);
        const auto article = [&]() -> ImGuiWindow * {
            for (auto *child : ctx->GetWindowByRef(Window(help).c_str())->DC.ChildWindows)
                if (std::strstr(child->Name, "/article_")) return child;
            return nullptr;
        };
        auto *page = article();
        IM_CHECK(page);
        ctx->MouseMoveToPos(page->InnerRect.GetCenter());
        ctx->MouseWheelY(-8);
        ctx->Yield(3);
        const float scroll = page->Scroll.y;
        IM_CHECK(scroll > 100);
        ctx->WindowClose(Window(help).c_str());
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            const auto reopened = Integer(Command("help.open"), "view");
            IM_CHECK_NO_RET(reopened && reopened != help);
            help = reopened;
        });
        ctx->Yield(4);
        IM_CHECK(App().Help().CurrentTopic() == "home.html");
        ctx->ItemClick((Window(help) + "/Back").c_str());
        ctx->Yield(4);
        IM_CHECK(App().Help().CurrentTopic() == "topic.html" && std::fabs(article()->Scroll.y - scroll) < 2);
        OnMainThread(ctx, [&] {
            auto config = initial;
            config.bindings.emplace_back("help.open", "Alt+H");
            std::string error;
            IM_CHECK_NO_RET(App().State().workspace->Configure(config, error));
        });
        ctx->KeyPress(ImGuiKey_F1);
        ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "topic.html");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_F);
        ctx->Yield(3);
        IM_CHECK_EQ(ImGui::GetActiveID(), ctx->ItemInfo((Window(help) + "/**/##search").c_str()).ID);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(2);
        auto gate = std::make_shared<Gate>();
        ReleaseGate release{gate};
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(state.runtimeOperation.Queue("Workspace regression", [gate, &state] {
                state.RuntimeLog(vdev::LogLevel::Warning, "Native work is waiting");
                std::unique_lock<std::mutex> lock(gate->mutex);
                gate->entered = true;
                gate->changed.wait(lock, [&] { return gate->released; });
            }, [](const std::string &) {}));
        });
        for (int i = 0; !gate->entered && i < 100; ++i) ctx->Yield();
        IM_CHECK(gate->entered.load());
        ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_H);
        ctx->Yield(4);
        IM_CHECK(App().Help().CurrentTopic() == "home.html");
        ctx->ItemClick((Window(help) + "/Back").c_str());
        ctx->Yield(4);
        IM_CHECK(App().Help().CurrentTopic() == "topic.html");
        OnWorkspaceThread(ctx, [&] {
            auto &workspace = *App().State().workspace;
            log = Integer(Command("log.open"), "view");
            IM_CHECK_NO_RET(log && App().State().runtimeOperation.Busy());
            const auto hierarchy=workspace.Open("scene.hierarchy");
            IM_CHECK_NO_RET(hierarchy);
            IM_CHECK_NO_RET(workspace.Inspect(hierarchy).status==EDITOR_OK);
            IM_CHECK_NO_RET(Command("scene.priority",{vdev::ObjectsArgument("objects",{}),vdev::IntegerArgument("value",1)}).status==EDITOR_REFUSED);
            IM_CHECK_NO_RET(workspace.Close(hierarchy));
            const auto blocks = workspace.Open("resource.building-blocks");
            IM_CHECK_NO_RET(blocks);
            IM_CHECK_NO_RET(Command("example.blocks.navigate", {vdev::IntegerArgument("view", blocks),
                vdev::TextArgument("filter", std::string("Rotate"))}).status == EDITOR_OK);
            IM_CHECK_NO_RET(Integer(workspace.Inspect(blocks), "rows") > 0);
            IM_CHECK_NO_RET(workspace.Close(blocks));

            const auto entries = workspace.Application().Query("log.entries", {workspace.Application().Scope(), nullptr, 0});
            IM_CHECK_NO_RET(entries.status == EDITOR_OK);
            IM_CHECK_NO_RET(Command("log.clear").status == EDITOR_OK);
            IM_CHECK_NO_RET(App().State().eventLog.Entries().size() == 1);
        });
        ctx->Yield(4);
        IM_CHECK(ctx->GetWindowByRef(Window(log).c_str()) != nullptr);
        gate->Release();
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(!state.runtimeOperation.Busy() && state.document->Token() == token &&
                state.History().UndoCount() == undo && state.History().CanRedo() == redo &&
                state.objectSelection.Revision() == selection);
            std::string error;
            IM_CHECK_NO_RET(state.workspace->Configure(initial, error));
            IM_CHECK_NO_RET(state.workspace->Close(help) && state.workspace->Close(log));
        });
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, [] { return App().Layout().UsesGpuViewport(); }));
    };
    test = IM_REGISTER_TEST(engine, "modules", "registered_views_reset_docking");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        EditorView hierarchy = 0, blocks = 0;
        OnMainThread(ctx, [&] {
            hierarchy = App().State().workspace->Open("scene.hierarchy");
            blocks = App().State().workspace->Open("resource.building-blocks");
            IM_CHECK_NO_RET(hierarchy && blocks);
        });
        ctx->Yield(4);
        ctx->MenuClick("//##MainMenuBar/Options/Reset Interface"); ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &workspace = *App().State().workspace;
            IM_CHECK_NO_RET(workspace.Application().Scope(hierarchy).view && workspace.Application().Scope(blocks).view);
            auto *tree = ImGui::FindWindowByName(workspace.WindowName(hierarchy).c_str());
            auto *browser = ImGui::FindWindowByName(workspace.WindowName(blocks).c_str());
            IM_CHECK_NO_RET(tree && browser && tree->DockId && tree->DockId == browser->DockId);
            ImGui::SetWindowDock(browser, 0, ImGuiCond_Always);
        });
        ctx->Yield(3);
        ctx->MenuClick("//##MainMenuBar/Options/Reset Interface"); ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &workspace = *App().State().workspace;
            IM_CHECK_NO_RET(!workspace.Application().Scope(blocks).view);
            IM_CHECK_NO_RET(workspace.Application().Scope(hierarchy).view);
            IM_CHECK_NO_RET(workspace.Role("main.building-blocks") && workspace.Role("main.level"));
            IM_CHECK_NO_RET(workspace.Close(hierarchy));
        });
    };



    test = IM_REGISTER_TEST(engine, "modules", "workspace_layout_configuration_applies_and_rolls_back");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        vdev::WorkspaceConfiguration original;
        vdev::DocumentToken token{};
        size_t undo = 0;
        uint64_t selection = 0;
        EditorView extra = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &workspace = *state.workspace;
            original = workspace.Configuration();
            token = state.document->Token(); undo = state.History().UndoCount(); selection = state.objectSelection.Revision();
            auto layout = original;
            layout.splits[0].fraction = .6f;
            layout.splits[1].fraction = .35f;
            for (auto &view : layout.views) if (view.role == "main.level") view.region = "resources";
            layout.views.push_back({"object.summary", "test.summary", "editing", true});
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(layout, error));
            extra = workspace.Role("test.summary");
            IM_CHECK_NO_RET(extra);
        });
        ctx->Yield(6);
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &workspace = *state.workspace;
            const auto *level = ImGui::FindWindowByName(workspace.WindowName(workspace.Role("main.level")).c_str());
            const auto *summary = ImGui::FindWindowByName(workspace.WindowName(extra).c_str());
            IM_CHECK_NO_RET(level && summary && level->DockId == workspace.RegionDock("resources") && summary->DockId == workspace.RegionDock("editing"));
            const auto *layout = ImGui::FindWindowByName("3D Layout");
            const auto *resources = ImGui::DockBuilderGetNode(workspace.RegionDock("resources"));
            IM_CHECK_NO_RET(layout && resources && resources->Size.x < layout->Size.x);
            auto bad = workspace.Configuration();
            bad.views.push_back(bad.views.front());
            const auto primary = workspace.Role("main.level");
            const auto dock = level->DockId;
            std::string error;
            IM_CHECK_NO_RET(!workspace.Configure(bad, error) && workspace.Role("main.level") == primary && level->DockId == dock);
            IM_CHECK_NO_RET(workspace.Configure(original, error) && workspace.Close(extra));
            IM_CHECK_NO_RET(state.document->Token() == token && state.History().UndoCount() == undo && state.objectSelection.Revision() == selection);
        });
        ctx->Yield(6);
        OnMainThread(ctx, [&] {
            auto &workspace = *App().State().workspace;
            const auto *level = ImGui::FindWindowByName(workspace.WindowName(workspace.Role("main.level")).c_str());
            IM_CHECK_NO_RET(level && level->DockId == workspace.RegionDock("editing"));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport());
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "resource_views_navigation_drag_and_reset");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ResourceSample sample;
        EditorView first = 0, second = 0;
        vdev::DocumentToken token{};
        size_t undo = 0;
        uint64_t selection = 0;
        bool dirty = false;
        vdev::EditorResponse closedRootState;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            token = state.document->Token(); undo = state.History().UndoCount(); selection = state.objectSelection.Revision(); dirty = state.document->Dirty();
            first = Integer(Command("example.library.open", {vdev::TextArgument("path", sample.library)}), "view");
            second = state.workspace->Open("resource.data");
            IM_CHECK_NO_RET(first && second && second != first);
            IM_CHECK_NO_RET(Command("example.library.open", {vdev::IntegerArgument("view", second), vdev::TextArgument("path", sample.library)}).status == EDITOR_OK);
            IM_CHECK_NO_RET(Command("example.library.browse", {vdev::IntegerArgument("view", first), vdev::TextArgument("folder", sample.graphs)}).status == EDITOR_OK);
            IM_CHECK_NO_RET(Command("example.library.browse", {vdev::IntegerArgument("view", second), vdev::TextArgument("folder", std::string("Textures"))}).status == EDITOR_OK);
        });
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            auto &workspace = *App().State().workspace;
            IM_CHECK_NO_RET(vdev::EditorClient::Text(workspace.Inspect(first), "folder") == sample.graphs);
            IM_CHECK_NO_RET(vdev::EditorClient::Text(workspace.Inspect(second), "folder") != sample.graphs);
            IM_CHECK_NO_RET(Command("example.library.browse", {vdev::IntegerArgument("view", first), vdev::TextArgument("folder", std::string("missing"))}).status == EDITOR_INVALID);
            IM_CHECK_NO_RET(vdev::EditorClient::Text(workspace.Inspect(first), "folder") == sample.graphs);
            ImGui::SetWindowDock(ImGui::FindWindowByName(workspace.WindowName(first).c_str()), 0, ImGuiCond_Always);
        });
        ctx->Yield(2);
        ctx->MenuClick("//##MainMenuBar/Options/Reset Interface"); ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &workspace = *App().State().workspace;
            const auto *window = ImGui::FindWindowByName(workspace.WindowName(first).c_str());
            IM_CHECK_NO_RET(window && window->DockId && window->DockId == workspace.RegionDock("resources"));
            IM_CHECK_NO_RET(workspace.Focus(first));
        });
        ctx->Yield(3);
        ctx->ItemClick((Window(first) + "/**/One.nms").c_str());
        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->ItemClick((Window(first) + "/**/Two.nms").c_str());
        ctx->KeyUp(ImGuiMod_Ctrl);
        ctx->Yield(2);
        OnMainThread(ctx, [&] {
            const auto &view = App().State().workspace->Inspect(first);
            const auto *chosen = vdev::EditorClient::Find(view, "selection");
            IM_CHECK_NO_RET(chosen && chosen->texts.size() == 2);
        });
        ctx->MouseMove((Window(first) + "/**/One.nms").c_str());
        const auto from = ImGui::GetIO().MousePos;
        ctx->MouseDown(0); ctx->MouseMoveToPos({from.x - 20, from.y + 10}); ctx->Yield(2);
        OnMainThread(ctx, [&] {
            const auto *payload = ImGui::GetDragDropPayload();
            uint32_t source = 9; std::vector<vdev::ResourceFile> files;
            IM_CHECK_NO_RET(payload && payload->IsDataType(EDITOR_RESOURCE_FILE_PAYLOAD) &&
                vdev::ReadResourceDrag(payload->Data, size_t(payload->DataSize), source, files));
            IM_CHECK_NO_RET(files.size() == 2 && files[source].path == sample.first && files[0].path == sample.first && files[1].path == sample.second);
        });
        ctx->KeyPress(ImGuiKey_Escape); ctx->MouseUp(0); ctx->Yield(3);
        ctx->ItemClose((Window(first) + "/**/Library").c_str());
        ctx->Yield(2);
        OnMainThread(ctx, [&] { closedRootState = App().State().workspace->Inspect(first); });
        OnMainThread(ctx, [&] {
            auto &workspace = *App().State().workspace;
            const auto previous = workspace.Application().Configuration();
            auto alternative = previous;
            for (auto &choice : alternative.providers) if (choice.kind == "view" && choice.id == "resource.data") choice.provider = "example.resources";
            std::string error;
            IM_CHECK_NO_RET(Command("example.file-view-state", {vdev::IntegerArgument("refuse", 1)}).status == EDITOR_OK);
            IM_CHECK_NO_RET(!workspace.Configure(alternative, error) && workspace.Application().Scope(first).view && workspace.Application().Scope(second).view);
            IM_CHECK_NO_RET(Command("example.file-view-state", {vdev::IntegerArgument("refuse", 0)}).status == EDITOR_OK);
            IM_CHECK_NO_RET(workspace.Configure(alternative, error));
            IM_CHECK_NO_RET(!workspace.Application().Scope(first).view && !workspace.Application().Scope(second).view);
            for (auto id : Views("resource.data")) {
                const auto &view = workspace.Inspect(id);
                if (vdev::EditorClient::Text(view, "folder") == vdev::EditorClient::Text(closedRootState, "folder")) {
                    const auto *chosen = vdev::EditorClient::Find(view, "selection");
                    IM_CHECK_NO_RET(chosen && chosen->texts == vdev::EditorClient::Find(closedRootState, "selection")->texts);
                }
            }
            IM_CHECK_NO_RET(workspace.Configure(previous, error));
            first = second = 0;
            for (auto id : Views("resource.data")) {
                if (vdev::EditorClient::Text(workspace.Inspect(id), "folder") == vdev::EditorClient::Text(closedRootState, "folder")) first = id;
                else second = id;
            }
            IM_CHECK_NO_RET(first && second && first != second);
        });
        ctx->Yield(4);
        IM_CHECK(ctx->ItemExists((Window(first) + "/**/Library").c_str()));
        IM_CHECK((ctx->ItemInfo((Window(first) + "/**/Library").c_str()).StatusFlags & ImGuiItemStatusFlags_Opened) == 0);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(state.workspace->Close(first) && state.workspace->Close(second));
            IM_CHECK_NO_RET(state.document->Token() == token && state.History().UndoCount() == undo && state.objectSelection.Revision() == selection && state.document->Dirty() == dirty);
        });
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, [&] { const auto &paths = App().State().preferences.dataResources; return std::find(paths.begin(), paths.end(), sample.library) == paths.end(); }));
    };


    test = IM_REGISTER_TEST(engine, "modules", "resource_load_uses_primary_level_selection");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ResourceSample sample;
        EditorView view = 0;
        CK_ID owner = 0;
        EditorObject script{};
        vdev::EditorConfiguration original;
        size_t undo = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            const auto frame = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(frame.ok); owner = frame.created;
            const auto created = vdev::CreateScript(*state.document, owner, "Resource Target");
            IM_CHECK_NO_RET(created.ok); script = state.document->Reference(created.created);
            state.objectSelection.Reset();
        });
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            auto &workspace = *App().State().workspace;
            original = workspace.Application().Configuration();
            auto configuration = original;
            for (auto &choice : configuration.providers) if (choice.kind == "command" && choice.id == "resource.load") choice.provider = "example.resource-spy";
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(configuration, error));
            view = Integer(Command("example.library.open", {vdev::TextArgument("path", sample.library)}), "view");
            IM_CHECK_NO_RET(view && Command("example.library.browse", {vdev::IntegerArgument("view", view), vdev::TextArgument("folder", sample.graphs)}).status == EDITOR_OK);
            IM_CHECK_NO_RET(Command("example.scene.navigate", {vdev::TextArgument("type", std::string("scene.level")),
                vdev::IntegerArgument("view", workspace.Role("main.level")), vdev::ObjectArgument("target", script)}).status == EDITOR_OK);
            undo = App().State().History().UndoCount();
        });
        ctx->Yield(4);
        ctx->WindowFocus(Window(view).c_str());
        ctx->ItemDoubleClick((Window(view) + "/**/One.nms").c_str()); ctx->Yield(4);
        OnMainThread(ctx, [&] {
            auto &app = App().State().workspace->Application();
            const auto result = vdev::EditorClient(app.Host(), 0).Query("example.resource-target");
            const auto *target = vdev::EditorClient::Find(result, "target");
            IM_CHECK_NO_RET(
                result.status == EDITOR_OK && target && target->object == script &&
                vdev::SceneObjects(vdev::EditorClient(app.Host(), 0).Query("selection.objects"), "objects").empty());
            IM_CHECK_NO_RET(Command("selection.restore",
                                    {vdev::ObjectsArgument("objects", {App().State().document->Reference(owner)}),
                                     vdev::IntegerArgument("locked", 1)})
                                .status == EDITOR_OK);
            auto &workspace = *App().State().workspace;
            IM_CHECK_NO_RET(
                Command("example.scene.navigate", {vdev::TextArgument("type", std::string("scene.level")),
                                                   vdev::IntegerArgument("view", workspace.Role("main.level")),
                                                   vdev::ObjectArgument("target", script)})
                    .status == EDITOR_OK);
        });
        ctx->Yield(4);
        ctx->WindowFocus(Window(view).c_str());
        ctx->ItemDoubleClick((Window(view) + "/**/One.nms").c_str());
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &workspace = *state.workspace;
            const auto result = workspace.Application().Query("example.resource-target", {workspace.Application().Scope(), nullptr, 0});
            const auto *target = vdev::EditorClient::Find(result, "target");
            IM_CHECK_NO_RET(result.status == EDITOR_OK && target && target->object == script);
            const auto selection = vdev::EditorClient(workspace.Application().Host(), 0).Query("selection.objects");
            IM_CHECK_NO_RET(vdev::SceneObjects(selection, "objects") ==
                                std::vector<vdev::ObjectRef>{state.document->Reference(owner)} &&
                            vdev::EditorClient::Integer(selection, "locked") && state.History().UndoCount() == undo);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(original, error) && workspace.Close(view));
            state.objectSelection.Reset();
            IM_CHECK_NO_RET(vdev::DestroyLevelObjects(*state.document, {owner}).ok);
            state.History().Clear(); state.document->MarkSaved(); state.History().MarkSavePoint();
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "resource_behavior_information_uses_registered_input");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ResourceSample sample;
        EditorView view = 0;
        vdev::DocumentToken token{};
        size_t undo = 0;
        EditorOperation cancelled = 0;
        vdev::EditorConfiguration inputConfiguration;
        OnMainThread(ctx, [&] {
            view = Integer(Command("example.library.open", {vdev::TextArgument("path", sample.library)}), "view");
            token = App().State().document->Token(); undo = App().State().History().UndoCount();
            IM_CHECK_NO_RET(view && Command("resource.behavior-information", {vdev::TextArgument("path", sample.first)}).status == EDITOR_QUEUED);
            IM_CHECK_NO_RET(Command("document.save-as").status == EDITOR_REFUSED);
        });
        ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &workspace = *App().State().workspace;
            std::string error;
            IM_CHECK_NO_RET(!workspace.Configure(workspace.Configuration(), error));
        });
        IM_CHECK(ctx->ItemExists("//###behaviorinfo/##author"));
        IM_CHECK(!ctx->ItemExists("//###behaviorinfo/Hide script representation in schematic view"));
        ctx->ItemInputValue("//###behaviorinfo/##author", "Resource UI Author");
        ctx->ItemInputValue("//###behaviorinfo/##version", "4.5");
        ctx->ItemClick("//###behaviorinfo/OK"); ctx->Yield(5);
        OnMainThread(ctx, [&] {
            vdev::BehaviorDescription info;
            IM_CHECK_NO_RET(vdev::ReadBehaviorDescription(sample.first, info) && info.author == "Resource UI Author" && info.version == 4.5f);
            IM_CHECK_NO_RET(App().State().document->Token() == token && App().State().History().UndoCount() == undo);
            auto &workspace = *App().State().workspace;
            inputConfiguration = workspace.Application().Configuration();
            auto alternative = inputConfiguration;
            for (auto &choice : alternative.providers) if (choice.kind == "view" && choice.id == "resource.file-dialogs") choice.provider = "example.resource-inputs";
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(alternative, error));
            const auto result = Command("resource.behavior-information", {vdev::TextArgument("path", sample.first)});
            IM_CHECK_NO_RET(result.status == EDITOR_QUEUED); cancelled = result.operation;
        });
        ctx->Yield(6);
        OnMainThread(ctx, [&] {
            auto &workspace = *App().State().workspace;
            vdev::EditorClient client(workspace.Application().Host(), 0);
            IM_CHECK_NO_RET(client.Completion(cancelled).status == EDITOR_CANCELLED);
            vdev::BehaviorDescription info;
            IM_CHECK_NO_RET(vdev::ReadBehaviorDescription(sample.first, info) && info.author == "Resource UI Author");
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(inputConfiguration, error));
            IM_CHECK_NO_RET(workspace.Close(view));
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "resource_browsing_during_native_work");
    test->GuiFunc = WorkspaceGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ResourceSample sample;
        EditorView view = 0;
        auto gate = std::make_shared<Gate>(); ReleaseGate release{gate};
        OnMainThread(ctx, [&] {
            view = Integer(Command("example.library.open", {vdev::TextArgument("path", sample.library)}), "view");
            IM_CHECK_NO_RET(view && App().State().runtimeOperation.Queue("Resource browsing regression", [gate] {
                std::unique_lock<std::mutex> lock(gate->mutex); gate->entered = true; gate->changed.wait(lock, [&] { return gate->released; });
            }, [](const std::string &) {}));
        });
        for (int i = 0; !gate->entered && i < 100; ++i) ctx->Yield();
        IM_CHECK(gate->entered.load());
        OnWorkspaceThread(ctx, [&] {
            auto &workspace = *App().State().workspace;
            IM_CHECK_NO_RET(Command("example.library.browse", {vdev::IntegerArgument("view", view), vdev::TextArgument("folder", sample.graphs)}).status == EDITOR_OK);
            IM_CHECK_NO_RET(vdev::EditorClient::Text(workspace.Inspect(view), "folder") == sample.graphs);
            IM_CHECK_NO_RET(Command("example.library.open", {vdev::TextArgument("path", sample.library)}).status == EDITOR_REFUSED);
            std::string error;
            IM_CHECK_NO_RET(!workspace.Configure(workspace.Configuration(), error));
            IM_CHECK_NO_RET(workspace.Close(view));
        });
        gate->Release(); ctx->Yield(5); Settle(ctx);
    };

    test = IM_REGISTER_TEST(engine, "modules", "building_block_view_instances_and_navigation");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        HelpSample help(true);
        vdev::EditorConfiguration configuration;
        EditorView first = 0, second = 0;
        vdev::DocumentToken token{};
        size_t undo = 0;
        uint64_t selection = 0;
        bool dirty = false;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            configuration = state.workspace->Application().Configuration();
            token = state.document->Token(); undo = state.History().UndoCount();
            selection = state.objectSelection.Revision(); dirty = state.document->Dirty();
            first = Integer(Command("workspace.open-view", {vdev::TextArgument("type", std::string("resource.building-blocks"))}), "view");
            second = Integer(Command("workspace.open-view", {vdev::TextArgument("type", std::string("resource.building-blocks"))}), "view");
            IM_CHECK_NO_RET(first && second && first != second);
            IM_CHECK_NO_RET(Command("example.blocks.navigate", {vdev::IntegerArgument("view", first),
                vdev::TextArgument("filter", std::string("Rotate"))}).status == EDITOR_OK);
            IM_CHECK_NO_RET(Command("example.blocks.navigate", {vdev::IntegerArgument("view", second),
                vdev::TextArgument("filter", std::string("Delayer"))}).status == EDITOR_OK);
        });
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            for (auto id : {first, second}) {
                const auto name = App().State().workspace->WindowName(id);
                if (auto *window = ImGui::FindWindowByName(name.c_str())) ImGui::SetWindowDock(window, 0, ImGuiCond_Always);
                ImGui::SetWindowSize(name.c_str(), {650, 400});
            }
            auto &workspace = *App().State().workspace;
            IM_CHECK_NO_RET(vdev::EditorClient::Text(workspace.Inspect(first), "filter") == "Rotate");
            IM_CHECK_NO_RET(vdev::EditorClient::Text(workspace.Inspect(second), "filter") == "Delayer");
            IM_CHECK_NO_RET(Integer(workspace.Inspect(first), "rows") > 1 && Integer(workspace.Inspect(second), "rows") > 0);
            IM_CHECK_NO_RET(Command("example.blocks.navigate", {vdev::IntegerArgument("view", first),
                vdev::TextArgument("filter", std::string("bad state")),
                vdev::TextArgument("category", std::string("missing/category"))}).status == EDITOR_INVALID);
            IM_CHECK_NO_RET(vdev::EditorClient::Text(workspace.Inspect(first), "filter") == "Rotate");
            const uint64_t invalid = UINT64_MAX;
            IM_CHECK_NO_RET(Command("example.blocks.navigate", {vdev::IntegerArgument("view", first),
                {EDITOR_TEXT("selection"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {&invalid, 1}}}).status == EDITOR_INVALID);
        });
        OnMainThread(ctx, [&] {
            auto &workspace = *App().State().workspace;
            IM_CHECK_NO_RET(workspace.Focus(second) && workspace.Focus(first));
        });
        ctx->Yield(3);
        IM_CHECK(Read<EditorView>(ctx, [] { return App().State().workspace->Focused(); }) == first);
        ctx->WindowFocus(Window(first).c_str()); ctx->Yield(3);
        ctx->ItemClick((Window(first) + "/**/Rotate").c_str()); ctx->Yield(2);
        OnMainThread(ctx, [&] {
            auto next = configuration;
            next.bindings.emplace_back("help.open", "Alt+H");
            std::string error;
            IM_CHECK_NO_RET(App().State().workspace->Configure(next, error));
        });
        ctx->MouseMove((Window(first) + "/**/Rotate").c_str());
        ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_H); ctx->Yield(4);
        IM_CHECK(App().Help().CurrentTopic() == "behaviors/3D Transformations/Rotate.html");
        IM_CHECK(App().Help().ShownText().find("Building Block help") != std::string::npos);
        OnMainThread(ctx, [&] {
            auto &workspace = *App().State().workspace;
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(configuration, error));
            for (auto id : Views("help.reference")) IM_CHECK_NO_RET(workspace.Close(id));
            const auto *chosen = vdev::EditorClient::Find(workspace.Inspect(first), "selection");
            IM_CHECK_NO_RET(chosen && chosen->integers.size() == 1);
            const auto selected = chosen->integers[0];
            const auto *other = vdev::EditorClient::Find(workspace.Inspect(second), "selection");
            IM_CHECK_NO_RET(other && other->integers.empty());
            IM_CHECK_NO_RET(Command("example.blocks.navigate", {vdev::IntegerArgument("view", second),
                vdev::TextArgument("category", std::string("3D Transformations/Basic")),
                {EDITOR_TEXT("selection"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {&selected, 1}}}).status == EDITOR_OK);
            IM_CHECK_NO_RET(!workspace.Close(workspace.Role("main.building-blocks")));
            const auto closed = workspace.Application().Scope(first);
            IM_CHECK_NO_RET(workspace.Close(first));
            IM_CHECK_NO_RET(Command("example.blocks.navigate", {vdev::IntegerArgument("view", first),
                vdev::TextArgument("filter", std::string("Rotate"))}).status == EDITOR_STALE);
            IM_CHECK_NO_RET(workspace.Application().Host()->queryView(workspace.Application().Host()->data, closed.view, {}) == EDITOR_STALE);
            IM_CHECK_NO_RET(workspace.Close(second));
            auto &state = App().State();
            IM_CHECK_NO_RET(state.document->Token() == token && state.History().UndoCount() == undo &&
                state.objectSelection.Revision() == selection && state.document->Dirty() == dirty);
        });
    };

    test = IM_REGISTER_TEST(engine,"modules","scene_view_instances_queries_and_commands");
    test->GuiFunc=MainThreadGui;
    test->TestFunc=[](ImGuiTestContext *ctx){
        Settle(ctx);
        vdev::ObjectRef a{},b{},scriptA{},scriptB{};
        EditorView first=0,second=0,hierarchyA=0,hierarchyB=0,attributesA=0,attributesB=0;
        size_t undo=0;vdev::DocumentToken token{};
        int typeA=-1,typeB=-1;
        OnMainThread(ctx,[&]{
            auto &state=App().State();auto &workspace=*state.workspace;auto &document=*state.document;
            auto left=vdev::CreateLayoutObject(document,vdev::LayoutObject::Frame3D),right=vdev::CreateLayoutObject(document,vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(left.ok&&right.ok);a=document.Reference(left.created);b=document.Reference(right.created);
            const auto sa=vdev::CreateScript(document,left.created,"Instance Script A"),sb=vdev::CreateScript(document,right.created,"Instance Script B");
            IM_CHECK_NO_RET(sa.ok&&sb.ok);scriptA=document.Reference(sa.created);scriptB=document.Reference(sb.created);
            IM_CHECK_NO_RET(vdev::CreateAttributeCategory(document,"Instance Attributes").ok);
            const auto ta=vdev::CreateAttributeType(document,"Instance A",CKPGUID_INT,"Instance Attributes"),tb=vdev::CreateAttributeType(document,"Instance B",CKPGUID_INT,"Instance Attributes");
            IM_CHECK_NO_RET(ta.ok&&tb.ok);typeA=int(ta.created);typeB=int(tb.created);
            IM_CHECK_NO_RET(vdev::AddAttribute(document,{left.created,right.created},typeA).ok);
            IM_CHECK_NO_RET(vdev::AddAttribute(document,{left.created,right.created},typeB).ok);
            state.objectSelection.Restore(document,{});document.History().Clear();document.MarkSaved();document.History().MarkSavePoint();
            undo=document.History().UndoCount();token=document.Token();
            first=Integer(Command("workspace.open-view",{vdev::TextArgument("type",std::string("scene.level"))}),"view");
            second=Integer(Command("workspace.open-view",{vdev::TextArgument("type",std::string("scene.level"))}),"view");
            hierarchyA=workspace.Open("scene.hierarchy");hierarchyB=workspace.Open("scene.hierarchy");
            attributesA=workspace.Open("scene.attributes");attributesB=workspace.Open("scene.attributes");
            IM_CHECK_NO_RET(first&&second&&first!=second&&hierarchyA!=hierarchyB&&attributesA!=attributesB);

        });
        ctx->Yield(4);
        OnMainThread(ctx,[&]{
            for(auto id:{first,second,hierarchyA,hierarchyB,attributesA,attributesB}) {
                const auto name=App().State().workspace->WindowName(id);
                if(auto *window=ImGui::FindWindowByName(name.c_str()))ImGui::SetWindowDock(window,0,ImGuiCond_Always);
                ImGui::SetWindowSize(name.c_str(),{530,330});
            }
            for(auto pair:{std::pair<EditorView,vdev::ObjectRef>{first,scriptA},{second,scriptB}}){
                IM_CHECK_NO_RET(Command("example.scene.navigate",{vdev::TextArgument("type",std::string("scene.level")),vdev::IntegerArgument("view",pair.first),vdev::ObjectArgument("target",pair.second)}).status==EDITOR_OK);
            }
        });
        ctx->Yield(4);
        OnMainThread(ctx,[&]{
            auto &state=App().State();auto &workspace=*state.workspace;
            const auto selected=[](const vdev::EditorResponse&r){for(const auto&f:r.fields)if(f.key=="selection")return f.objects;return std::vector<vdev::ObjectRef>{};};
            IM_CHECK_NO_RET(selected(workspace.Inspect(first))==std::vector<vdev::ObjectRef>{scriptA});
            IM_CHECK_NO_RET(selected(workspace.Inspect(second))==std::vector<vdev::ObjectRef>{scriptB});
            IM_CHECK_NO_RET(state.document->Token()==token&&state.History().UndoCount()==undo&&!state.document->Dirty());
            IM_CHECK_NO_RET(Command("example.scene.navigate",{vdev::TextArgument("type",std::string("scene.level")),vdev::IntegerArgument("view",first),vdev::ObjectArgument("target",a)}).status==EDITOR_OK);
        });
        ctx->Yield(3);
        OnMainThread(ctx,[&]{
            IM_CHECK_NO_RET(Command("example.scene.hide",{vdev::IntegerArgument("view",first)}).status==EDITOR_OK);
            IM_CHECK_NO_RET(!App().State().document->Resolve(a)->IsVisible());
            IM_CHECK_NO_RET(App().State().History().UndoCount()==undo+1);
        });
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx,[&]{return App().State().document->Resolve(a)->IsVisible();}));
        for(auto pair:{std::pair<EditorView,const char*>{attributesA,"Instance A"},{attributesB,"Instance B"}}){
            ctx->WindowFocus(Window(pair.first).c_str());ctx->SetRef(Window(pair.first).c_str());ctx->Yield(3);
            ctx->ItemOpen("**/Instance Attributes");ctx->ItemClick((std::string("**/")+pair.second).c_str());ctx->Yield(3);
        }
        OnMainThread(ctx,[&]{
            auto&workspace=*App().State().workspace;
            const auto types=[](const vdev::EditorResponse&r){for(const auto&field:r.fields)if(field.key=="types")return field.integers;return std::vector<uint64_t>{};};
            IM_CHECK_NO_RET(types(workspace.Inspect(attributesA))==std::vector<uint64_t>{uint64_t(typeA)});
            IM_CHECK_NO_RET(types(workspace.Inspect(attributesB))==std::vector<uint64_t>{uint64_t(typeB)});
            IM_CHECK_NO_RET(!App().State().document->Dirty());
        });
        ctx->WindowFocus(Window(hierarchyA).c_str());ctx->Yield(3);
        ctx->ItemClick((Window(hierarchyA)+"/meshes/##bitmap").c_str());ctx->Yield(3);
        OnMainThread(ctx,[&]{
            auto &workspace=*App().State().workspace;
            IM_CHECK_NO_RET(Integer(workspace.Inspect(hierarchyA),"resources")==1);
            IM_CHECK_NO_RET(Integer(workspace.Inspect(hierarchyB),"resources")==0);
            const auto closed=workspace.Application().Scope(second);
            IM_CHECK_NO_RET(workspace.Close(second));
            IM_CHECK_NO_RET(!workspace.Application().Scope(second).view);
            IM_CHECK_NO_RET(workspace.Application().Host()->queryView(workspace.Application().Host()->data,closed.view,{})==EDITOR_STALE);
            IM_CHECK_NO_RET(!workspace.Close(workspace.Role("main.level")));
            for(auto id:{first,hierarchyA,hierarchyB,attributesA,attributesB})IM_CHECK_NO_RET(workspace.Close(id));
            App().State().objectSelection.Reset();
            IM_CHECK_NO_RET(vdev::DestroyLevelObjects(*App().State().document,{a.object,b.object}).ok);
            IM_CHECK_NO_RET(vdev::DeleteAttributeType(*App().State().document,typeA).ok);
            IM_CHECK_NO_RET(vdev::DeleteAttributeType(*App().State().document,typeB).ok);
            IM_CHECK_NO_RET(vdev::DeleteAttributeCategory(*App().State().document,"Instance Attributes").ok);
            App().State().document->MarkSaved();
        });
    };

    test = IM_REGISTER_TEST(engine, "modules", "cross_view_object_drags_keep_source_selection_and_cancel_safely");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State();
        auto &workspace = *state.workspace;
        const vdev::EditorClient client(workspace.Application().Host(), 0);
        vdev::ObjectRef a{}, b{}, c{}, frame{}, group{};
        OnMainThread(ctx, [&] {
            const auto create = [&](const char *name, bool group = false) {
                const auto made = Command(group ? "object.create-group" : "object.create-frame3d");
                const auto *created = vdev::EditorClient::Find(made, "created");
                IM_CHECK_NO_RET(made.status == EDITOR_OK && created && created->object);
                const auto object = created ? created->object : vdev::ObjectRef{};
                IM_CHECK_NO_RET(Command("object.rename", {vdev::ObjectArgument("target", object),
                                                          vdev::TextArgument("name", std::string(name))})
                                    .status == EDITOR_OK);
                return object;
            };
            a = create("00 Drop A");
            b = create("00 Drop B");
            c = create("00 Drop C");
            frame = create("00 Drop Parent");
            group = create("00 Drop Group", true);
        });
        const auto point = [&](EditorView view, vdev::ObjectRef object) {
            return Read<ImVec2>(ctx, [&] {
                for (const auto &row : vdev::SceneRowsForTests(view))
                    if (row.object == object.object)
                        return ImVec2(row.min.x + 180, (row.min.y + row.max.y) * .5f);
                return ImVec2(-1, -1);
            });
        };
        const auto choose = [&](EditorView view, vdev::ObjectRef second) {
            ctx->WindowFocus(Window(view).c_str());
            ctx->Yield(2);
            ctx->MouseMoveToPos(point(view, a));
            ctx->MouseClick(0);
            ctx->KeyDown(ImGuiMod_Ctrl);
            ctx->MouseMoveToPos(point(view, second));
            ctx->MouseClick(0);
            ctx->KeyUp(ImGuiMod_Ctrl);
            ctx->Yield(3);
        };
        enum { Accept, Escape, CloseSource, CloseReceiver, CloseUnrelated, DeleteMember };
        for (bool hierarchy : {true, false})
            for (bool locked : {true, false})
                for (int mode = Accept; mode <= DeleteMember; ++mode) {
                    ctx->LogInfo("hierarchy=%d locked=%d action=%d", hierarchy, locked, mode);
                    const char *type = hierarchy ? "scene.hierarchy" : "scene.level";
                    const auto target = hierarchy ? frame : group;
                    EditorView source = 0, receiver = 0, unrelated = 0;
                    size_t steps = 0;
                    bool dirty = false, redo = false;
                    std::vector<vdev::ObjectRef> shared;
                    OnMainThread(ctx, [&] {
                        const auto restored = Command("selection.restore", {vdev::ObjectsArgument("objects", {a}),
                                                                            vdev::IntegerArgument("locked", locked)});
                        IM_CHECK_NO_RET(restored.status == EDITOR_OK);
                        source = workspace.Open(type);
                        receiver = workspace.Open(type);
                        if (mode == CloseUnrelated)
                            unrelated = workspace.Open(type);
                        IM_CHECK_NO_RET(source && receiver && source != receiver);
                    });
                    ctx->Yield(4);
                    OnMainThread(ctx, [&] {
                        int index = 0;
                        for (auto view : {source, receiver}) {
                            const auto name = workspace.WindowName(view);
                            auto *window = ImGui::FindWindowByName(name.c_str());
                            IM_CHECK_NO_RET(window);
                            ImGui::SetWindowDock(window, 0, ImGuiCond_Always);
                            ImGui::SetWindowPos(name.c_str(), {20.0f + 780 * index++, 60});
                            ImGui::SetWindowSize(name.c_str(), {700, 650});
                            IM_CHECK_NO_RET(
                                Command("example.scene.navigate",
                                        {vdev::TextArgument("type", std::string(type)),
                                         vdev::IntegerArgument("view", view), vdev::ObjectArgument("target", a)})
                                    .status == EDITOR_OK);
                        }
                    });
                    ctx->Yield(4);
                    choose(source, b);
                    if (locked)
                        choose(receiver, c);
                    OnMainThread(ctx, [&] {
                        IM_CHECK_NO_RET(vdev::SceneObjects(workspace.Inspect(source), "selection") ==
                                        std::vector<vdev::ObjectRef>({a, b}));
                        if (locked)
                            IM_CHECK_NO_RET(vdev::SceneObjects(workspace.Inspect(receiver), "selection") ==
                                            std::vector<vdev::ObjectRef>({a, c}));
                        shared = vdev::SceneObjects(client.Query("selection.objects"), "objects");
                        steps = state.History().UndoCount();
                        dirty = state.document->Dirty();
                        redo = state.History().CanRedo();
                    });
                    ctx->WindowFocus(Window(source).c_str());
                    ctx->Yield(2);
                    const auto from = point(source, a), to = point(receiver, target);
                    IM_CHECK(from.x >= 0 && to.x >= 0);
                    ctx->MouseMoveToPos(from);
                    ctx->MouseDown(0);
                    ctx->MouseMoveToPos({from.x + 25, from.y + 5});
                    OnMainThread(ctx, [&] {
                        IM_CHECK_NO_RET(vdev::SceneObjects(workspace.Inspect(source), "selection") ==
                                        std::vector<vdev::ObjectRef>({a, b}));
                    });
                    ctx->MouseMoveToPos(to);
                    ctx->Yield(3);
                    if (mode == Escape)
                        ctx->KeyPress(ImGuiKey_Escape);
                    OnMainThread(ctx, [&] {
                        if (mode == CloseSource) {
                            // Focus changes may clear the active widget while
                            // ImGui still retains a drag awaiting mouse release.
                            if (!locked)
                                ImGui::ClearActiveID();
                            IM_CHECK_NO_RET(workspace.Close(source));
                            IM_CHECK_NO_RET(!ImGui::GetDragDropPayload());
                        }
                        if (mode == CloseReceiver)
                            IM_CHECK_NO_RET(workspace.Close(receiver));
                        if (mode == CloseUnrelated) {
                            IM_CHECK_NO_RET(workspace.Close(unrelated));
                            IM_CHECK_NO_RET(ImGui::GetDragDropPayload());
                        }
                        if (mode == DeleteMember) {
                            IM_CHECK_NO_RET(Command("object.destroy", {vdev::ObjectsArgument("objects", {b})}).status ==
                                            EDITOR_OK);
                            steps = state.History().UndoCount();
                            dirty = state.document->Dirty();
                            redo = state.History().CanRedo();
                            shared = vdev::SceneObjects(client.Query("selection.objects"), "objects");
                        }
                    });
                    if (mode == DeleteMember) {
                        ctx->Yield(2);
                        OnMainThread(
                            ctx, [&] { shared = vdev::SceneObjects(client.Query("selection.objects"), "objects"); });
                    }
                    ctx->MouseUp(0);
                    ctx->Yield(4);
                    OnMainThread(ctx, [&] {
                        vdev::SceneTree tree;
                        IM_CHECK_NO_RET(vdev::QuerySceneTree(client, {}, hierarchy, tree) == EDITOR_OK);
                        const auto member = [&](vdev::ObjectRef object) {
                            for (const auto &row : tree.rows)
                                if (row.object == object && row.parent && tree.rows[row.parent - 1].object == target)
                                    return true;
                            return false;
                        };
                        const bool accepted = mode == Accept || mode == CloseUnrelated;
                        IM_CHECK_NO_RET(member(a) == accepted && member(b) == accepted && !member(c));
                        IM_CHECK_NO_RET(vdev::SceneObjects(client.Query("selection.objects"), "objects") == shared);
                        IM_CHECK_NO_RET(
                            bool(vdev::EditorClient::Integer(client.Query("selection.objects"), "locked")) == locked);
                        IM_CHECK_NO_RET(state.History().UndoCount() == steps + unsigned(accepted));
                        if (accepted)
                            IM_CHECK_NO_RET(Command("history.undo").status == EDITOR_OK);
                        else
                            IM_CHECK_NO_RET(state.document->Dirty() == dirty && state.History().CanRedo() == redo);
                        for (auto view : {source, receiver})
                            if (workspace.Application().Scope(view).view)
                                IM_CHECK_NO_RET(workspace.Close(view));
                        if (mode == DeleteMember) {
                            // Permanent deletion retains its existing non-undoable
                            // behavior. The refused drop must preserve that state.
                            const auto made = Command("object.create-frame3d");
                            const auto *created = vdev::EditorClient::Find(made, "created");
                            IM_CHECK_NO_RET(made.status == EDITOR_OK && created && created->object);
                            b = created ? created->object : vdev::ObjectRef{};
                            IM_CHECK_NO_RET(
                                Command("object.rename", {vdev::ObjectArgument("target", b),
                                                          vdev::TextArgument("name", std::string("00 Drop B"))})
                                    .status == EDITOR_OK);
                        }
                    });
                    ctx->Yield(3);
                }
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(Command("selection.lock", {vdev::IntegerArgument("locked", 0)}).status == EDITOR_OK ||
                            !vdev::EditorClient::Integer(client.Query("selection.objects"), "locked"));
        });
    };
}
} // namespace vdevuitest
