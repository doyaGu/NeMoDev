#include "tests/ui/UiTests.h"
#include "core/runtime/Resources.h"
#include "ui/help/HelpWindow.h"
#include "workspace/EditorClient.h"
#include "core/level/LevelCommands.h"
#include <atomic>
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

}
} // namespace vdevuitest
