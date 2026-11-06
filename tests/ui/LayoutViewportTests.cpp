#include "CKInputManager.h"
#include "core/level/LevelCommands.h"
#include "features/layout/SnapshotData.h"
#include "imgui_internal.h"
#include "tests/ui/UiTests.h"
#include "ui/render/UiRenderer.h"
#include <cmath>
#include <cstring>

namespace vdevuitest {
namespace {
vdev::EditorClient Client(EditorView view = 0) {
    return vdev::EditorClient(App().WorkspaceForTests().Application().Host(), view);
}
struct Pair {
    EditorView first = 0, second = 0;
    std::string name;
    ImVec2 position{}, size{};
    ImGuiID dock = 0;
    void Open(ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            first = workspace.Role("main.layout");
            const auto opened =
                Client().Command("workspace.open-view", {vdev::TextArgument("type", "layout.viewport")});
            IM_CHECK_NO_RET(opened.status == EDITOR_OK);
            second = vdev::EditorClient::Integer(opened, "view");
            name = workspace.WindowName(second);
            IM_CHECK_NO_RET(first && second && first != second && name != workspace.WindowName(first));
            IM_CHECK_NO_RET(workspace.WindowName(first) == "3D Layout###3D Layout");
            auto *window = ImGui::FindWindowByName("3D Layout");
            IM_CHECK_NO_RET(window);
            position = window->Pos;
            size = window->Size;
            dock = window->DockId;
            ImGui::SetWindowDock(window, 0, ImGuiCond_Always);
            ImGui::SetWindowPos(window, ImVec2(20, 100));
            ImGui::SetWindowSize(window, ImVec2(550, 440));
        });
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            auto *window = ImGui::FindWindowByName(name.c_str());
            IM_CHECK_NO_RET(window);
            ImGui::SetWindowDock(window, 0, ImGuiCond_Always);
            ImGui::SetWindowPos(window, ImVec2(600, 100));
            ImGui::SetWindowSize(window, ImVec2(550, 440));
        });
        ctx->Yield(5);
    }
    void Close(ImGuiTestContext *ctx) {
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(Client().Command("workspace.close-view", {vdev::IntegerArgument("view", second)}).status ==
                            EDITOR_OK);
            auto *window = ImGui::FindWindowByName("3D Layout");
            IM_CHECK_NO_RET(window);
            ImGui::SetWindowPos(window, position);
            ImGui::SetWindowSize(window, size);
            ImGui::SetWindowDock(window, dock, ImGuiCond_Always);
            App().WorkspaceForTests().Focus(first);
        });
        ctx->Yield(4);
    }
};
bool Same(const VxMatrix &a, const VxMatrix &b) {
    return !std::memcmp(&a, &b, sizeof(VxMatrix));
}
void CheckInput(vdev::LayoutWindow &window) {
    ImVec2 min, max;
    IM_CHECK_NO_RET(window.ShownView(min, max));
    const auto at = Center(min, max);
    const auto *main = ImGui::GetMainViewport();
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = SDL_WindowID(reinterpret_cast<uintptr_t>(main->PlatformHandle));
    event.motion.x = at.x - main->Pos.x;
    event.motion.y = at.y - main->Pos.y;
    auto *input =
        static_cast<CKInputManager *>(App().State().document->Context()->GetManagerByGuid(INPUT_MANAGER_GUID));
    IM_CHECK_NO_RET(input);
    // Process the queued native event as a running composition would, while
    // keeping the test document and its scripts stopped.
    input->Pause(FALSE);
    const bool pushed = SDL_PushEvent(&event);
    const auto processed = input->PreProcess();
    Vx2DVector mouse;
    input->GetMousePosition(mouse, FALSE);
    input->Pause(TRUE);
    IM_CHECK_NO_RET(pushed && processed == CK_OK);
    IM_CHECK_NO_RET(std::abs(mouse.x - window.View().Width() * .5f) < 2 &&
                    std::abs(mouse.y - window.View().Height() * .5f) < 2);
}
} // namespace

void RegisterLayoutViewportTests(ImGuiTestEngine *engine) {
    auto *rollback = IM_REGISTER_TEST(engine, "documentviews", "failed_new_keeps_both_viewports_and_setup");
    rollback->GuiFunc = MainThreadGui;
    rollback->TestFunc = [](ImGuiTestContext *ctx) {
        Pair pair; pair.Open(ctx);
        CK_ID firstCamera = 0, secondCamera = 0;
        VxMatrix firstMatrix, secondMatrix;
        EditorView setup = 0;
        size_t history = 0;
        uint64_t firstReads = 0, secondReads = 0, firstUploads = 0, secondUploads = 0;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &workspace = App().WorkspaceForTests();
            auto &a = App().Layout(), &b = *App().Layout(pair.second);
            firstCamera = a.View().ViewCamera(vdev::LayoutViewCamera::Top);
            secondCamera = b.View().ViewCamera(vdev::LayoutViewCamera::Front);
            a.View().SetCamera(firstCamera); b.View().SetCamera(secondCamera);
            a.View().Camera()->SetPosition3f(11, 23, 41); b.View().Camera()->SetPosition3f(59, 71, 89);
            firstMatrix = a.View().Camera()->GetWorldMatrix(); secondMatrix = b.View().Camera()->GetWorldMatrix();
            const auto made = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(made.ok);
            const auto object = state.document->Reference(made.created);
            state.objectSelection.Restore(*state.document, {made.created});
            const auto opened = Client().Command("object.open-setup", {vdev::ObjectArgument("target", object)});
            IM_CHECK_NO_RET(opened.status == EDITOR_OK);
            setup = vdev::EditorClient::Integer(opened, "view");
            IM_CHECK_NO_RET(setup && workspace.Application().Scope(setup).view == setup);
            state.document->MarkSaved(); state.History().MarkSavePoint(); history = state.History().UndoCount();
            firstReads = a.View().ReadbackFrames(); secondReads = b.View().ReadbackFrames();
            firstUploads = a.ViewportUploadsForTests(); secondUploads = b.ViewportUploadsForTests();
            const auto token = state.document->Token();
            struct FailingViews : vdev::DocumentViews {
                vdev::EditorSession &session;
                vdev::DocumentViews &original;
                FailingViews(vdev::EditorSession &session) : session(session), original(*session.documentViews) { session.documentViews = this; }
                ~FailingViews() { session.documentViews = &original; }
                bool DocumentInteractionActive() const override { return original.DocumentInteractionActive(); }
                void DetachDocument(bool context) override { original.DetachDocument(context); throw std::runtime_error("Injected multi-viewport detach failure"); }
                void RestoreDocument() override { original.RestoreDocument(); }
                void DocumentReplaced(vdev::DocumentToken document, uint64_t revision, bool open) override { original.DocumentReplaced(document, revision, open); }
            } failure(state);
            const auto result = Client().Command("document.new");
            IM_CHECK_NO_RET(result.status == EDITOR_QUEUED);
            state.FinishRuntimeOperations();
            IM_CHECK_NO_RET(Client().Completion(result.operation).status == EDITOR_ERROR);
            IM_CHECK_NO_RET(state.document->Token() == token && state.objectSelection.Contains(made.created) &&
                            !state.document->Dirty() && state.History().UndoCount() == history);
            IM_CHECK_NO_RET(workspace.Application().Scope(setup).view == setup);
        });
        ctx->Yield(12);
        OnMainThread(ctx, [&] {
            auto &a = App().Layout(), &b = *App().Layout(pair.second);
            IM_CHECK_NO_RET(a.UsesGpuViewport() && b.UsesGpuViewport());
            IM_CHECK_NO_RET(a.View().Camera()->GetID() == firstCamera && b.View().Camera()->GetID() == secondCamera &&
                            Same(a.View().Camera()->GetWorldMatrix(), firstMatrix) && Same(b.View().Camera()->GetWorldMatrix(), secondMatrix));
            IM_CHECK_NO_RET(a.View().ReadbackFrames() == firstReads && b.View().ReadbackFrames() == secondReads &&
                            a.ViewportUploadsForTests() == firstUploads && b.ViewportUploadsForTests() == secondUploads);
            IM_CHECK_NO_RET(App().State().History().UndoCount() == history);
            IM_CHECK_NO_RET(Client().Command("workspace.close-view", {vdev::IntegerArgument("view", setup)}).status == EDITOR_OK);
        });
        pair.Close(ctx);
    };
    auto *test =
        IM_REGISTER_TEST(engine, "layoutviews", "layout_viewports_keep_cameras_tools_and_gestures_independent");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Pair pair;
        pair.Open(ctx);
        VxMatrix firstMatrix, secondMatrix;
        size_t undo = 0;
        bool dirty = false;
        OnMainThread(ctx, [&] {
            auto &a = App().Layout();
            auto &b = *App().Layout(pair.second);
            IM_CHECK_NO_RET(a.View().RenderContext() != b.View().RenderContext());
            IM_CHECK_NO_RET(a.View().Camera() != b.View().Camera());
            firstMatrix = a.View().Camera()->GetWorldMatrix();
            secondMatrix = b.View().Camera()->GetWorldMatrix();
            undo = App().State().History().UndoCount();
            dirty = App().State().document->Dirty();
        });
        const auto second = "//" + pair.name;
        ctx->WindowFocus(second.c_str());
        ctx->ItemClick((second + "/$$28/##tool").c_str());
        IM_CHECK(App().Layout(pair.second)->Tool() == vdev::LayoutTool::Pan);
        IM_CHECK(App().Layout().Tool() == vdev::LayoutTool::Select);
        ImVec2 min, max;
        IM_CHECK(App().Layout(pair.second)->ShownView(min, max));
        auto at = Center(min, max);
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos(ImVec2(at.x + 45, at.y + 25));
        ctx->Yield(2);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(!Same(secondMatrix, App().Layout(pair.second)->View().Camera()->GetWorldMatrix()));
            IM_CHECK_NO_RET(Same(firstMatrix, App().Layout().View().Camera()->GetWorldMatrix()));
            IM_CHECK_NO_RET(vdev::EditorClient::Integer(Client(pair.second).Query("layout.input"), "active"));
            IM_CHECK_NO_RET(!vdev::EditorClient::Integer(Client(pair.first).Query("layout.input"), "active"));
        });
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(Same(secondMatrix, App().Layout(pair.second)->View().Camera()->GetWorldMatrix()));
            IM_CHECK_NO_RET(App().State().History().UndoCount() == undo && App().State().document->Dirty() == dirty);
            IM_CHECK_NO_RET(App().State().document->Context()->GetCurrentLevel()->GetRenderContext(0) ==
                            App().Layout(pair.second)->View().RenderContext());
            CheckInput(*App().Layout(pair.second));
        });
        // Drawing the second window later must not take input from the first.
        ctx->WindowFocus("//3D Layout");
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(App().State().document->Context()->GetCurrentLevel()->GetRenderContext(0) ==
                            App().Layout().View().RenderContext());
            CheckInput(App().Layout());
        });
        pair.Close(ctx);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(!App().Layout(pair.second) &&
                            Client(pair.second).Query("layout.input").status == EDITOR_STALE);
            IM_CHECK_NO_RET(App().Layout().View().InterfaceAlive() && App().Layout().UsesGpuViewport());
            CheckInput(App().Layout());
        });
    };

    test = IM_REGISTER_TEST(engine, "layoutviews", "layout_viewports_keep_gpu_targets_and_fallback_independent");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Pair pair;
        pair.Open(ctx);
        SDL_GPUTexture *aTexture = nullptr, *bTexture = nullptr;
        CKRasterizerContext *aContext = nullptr;
        uint64_t aReads = 0, bReads = 0, aUploads = 0, bUploads = 0, aCreates = 0, bCreates = 0, selections = 0;
        OnMainThread(ctx, [&] {
            auto &a = App().Layout();
            auto &b = *App().Layout(pair.second);
            IM_CHECK_NO_RET(a.UsesGpuViewport() && b.UsesGpuViewport());
            aTexture = a.GpuViewportTextureForTests()->NativeTexture();
            bTexture = b.GpuViewportTextureForTests()->NativeTexture();
            aContext = a.View().RasterizerContext();
            IM_CHECK_NO_RET(aTexture && bTexture && aTexture != bTexture && aContext != b.View().RasterizerContext());
            aReads = a.View().ReadbackFrames();
            bReads = b.View().ReadbackFrames();
            aUploads = a.ViewportUploadsForTests();
            bUploads = b.ViewportUploadsForTests();
            aCreates = a.GpuViewportCreationsForTests();
            bCreates = b.GpuViewportCreationsForTests();
            selections = b.GpuViewportSelectionsForTests();
        });
        ctx->Yield(12);
        OnMainThread(ctx, [&] {
            auto &a = App().Layout();
            auto &b = *App().Layout(pair.second);
            IM_CHECK_NO_RET(a.GpuViewportTextureForTests()->NativeTexture() == aTexture &&
                            b.GpuViewportTextureForTests()->NativeTexture() == bTexture);
            IM_CHECK_NO_RET(a.View().ReadbackFrames() == aReads && b.View().ReadbackFrames() == bReads &&
                            a.ViewportUploadsForTests() == aUploads && b.ViewportUploadsForTests() == bUploads);
            IM_CHECK_NO_RET(a.GpuViewportCreationsForTests() == aCreates &&
                            b.GpuViewportCreationsForTests() == bCreates &&
                            b.GpuViewportSelectionsForTests() == selections);
            auto *window = ImGui::FindWindowByName(pair.name.c_str());
            IM_CHECK_NO_RET(window);
            ImGui::SetWindowSize(window, ImVec2(580, 470));
        });
        ctx->Yield(5);
        OnMainThread(ctx, [&] {
            auto &a = App().Layout();
            auto &b = *App().Layout(pair.second);
            IM_CHECK_NO_RET(a.GpuViewportTextureForTests()->NativeTexture() == aTexture &&
                            a.View().RasterizerContext() == aContext);
            IM_CHECK_NO_RET(a.GpuViewportCreationsForTests() == aCreates &&
                            b.GpuViewportCreationsForTests() == bCreates + 1 &&
                            b.GpuViewportSelectionsForTests() == selections);
            b.FailNextGpuViewportForTests();
            IM_CHECK_NO_RET(b.RecreateGpuViewportForTests());
        });
        ctx->Yield(6);
        OnMainThread(ctx, [&] {
            auto &a = App().Layout();
            auto &b = *App().Layout(pair.second);
            IM_CHECK_NO_RET(a.UsesGpuViewport() && !b.UsesGpuViewport());
            IM_CHECK_NO_RET(a.View().ReadbackFrames() == aReads && a.ViewportUploadsForTests() == aUploads);
            IM_CHECK_NO_RET(b.View().ReadbackFrames() > bReads && b.ViewportUploadsForTests() > bUploads &&
                            b.GpuViewportSelectionsForTests() == selections);
        });
        pair.Close(ctx);
    };

    test = IM_REGISTER_TEST(engine, "layoutviews", "layout_snapshot_reads_only_the_originating_viewport");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Pair pair;
        pair.Open(ctx);
        OnMainThread(ctx, [&] {
            auto &a = App().Layout();
            auto &b = *App().Layout(pair.second);
            const auto aReads = a.View().ReadbackFrames(), bReads = b.View().ReadbackFrames();
            const auto undo = App().State().History().UndoCount();
            const auto taken = Client(pair.second).Command("snapshot.take");
            IM_CHECK_NO_RET(taken.status == EDITOR_OK && a.View().ReadbackFrames() == aReads &&
                            b.View().ReadbackFrames() == bReads + 1);
            vdev::SnapshotData data;
            IM_CHECK_NO_RET(vdev::ReadSnapshots(Client(), data));
            IM_CHECK_NO_RET(
                vdev::RemoveSnapshots(Client(), data, {int(vdev::EditorClient::Integer(taken, "slot"))}).status ==
                EDITOR_OK);
            IM_CHECK_NO_RET(App().State().History().UndoCount() == undo && a.UsesGpuViewport() && b.UsesGpuViewport());
        });
        pair.Close(ctx);
    };
    test = IM_REGISTER_TEST(engine, "layoutviews",
                            "registered_toolbar_actions_and_rebound_inputs_keep_viewport_ownership");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Pair pair;
        pair.Open(ctx);
        vdev::EditorConfiguration previous;
        uint64_t reads = 0, uploads = 0;
        size_t history = 0;
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto &app = workspace.Application();
            previous = app.Configuration();
            auto config = previous;
            config.bindings.emplace_back("layout.pan", "Ctrl+K");
            vdev::ToolbarPlacement button;
            button.id = "test.configured-pan";
            button.location = "layout.top";
            button.title = "Configured Pan";
            button.action = "layout.pan";
            button.x = 375;
            button.y = 1;
            button.width = 100;
            button.height = 19;
            button.fields = vdev::ToolbarLocation | vdev::ToolbarAction | vdev::ToolbarTitle | vdev::ToolbarPosition |
                            vdev::ToolbarSize;
            config.toolbarItems.push_back(button);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
            IM_CHECK_NO_RET(Client(pair.first).Command("layout.select").status <= EDITOR_NO_CHANGE);
            IM_CHECK_NO_RET(Client(pair.second).Command("layout.select").status <= EDITOR_NO_CHANGE);
            reads = App().Layout(pair.second)->View().ReadbackFrames();
            uploads = App().Layout(pair.second)->ViewportUploadsForTests();
            history = App().State().History().UndoCount();
        });
        ctx->Yield(3);
        const auto second = "//" + pair.name;
        ctx->WindowFocus(second.c_str());
        ctx->KeyPress(ImGuiKey_P);
        ctx->Yield(3);
        IM_CHECK(App().Layout(pair.second)->Tool() == vdev::LayoutTool::Select);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_K);
        ctx->Yield(3);
        IM_CHECK(App().Layout(pair.second)->Tool() == vdev::LayoutTool::Pan);
        IM_CHECK(App().Layout().Tool() == vdev::LayoutTool::Select);
        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->KeyDown(ImGuiMod_Shift);
        ctx->KeyDown(ImGuiKey_K);
        ctx->Yield(2);
        OnMainThread(ctx,
                     [&] { IM_CHECK_NO_RET(App().WorkspaceForTests().Binding("layout.pan", EDITOR_KEY_DOWN, false)); });
        ctx->KeyUp(ImGuiKey_K);
        ctx->KeyUp(ImGuiMod_Shift);
        ctx->KeyUp(ImGuiMod_Ctrl);
        ctx->WindowFocus("//3D Layout");
        ctx->ItemClick("//3D Layout/test.configured-pan/Configured Pan");
        ctx->Yield(3);
        IM_CHECK(App().Layout().Tool() == vdev::LayoutTool::Pan);
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto config = workspace.Application().Configuration();
            auto &button = config.toolbarItems.back();
            button.x = 405;
            button.y = 1;
            button.width = 110;
            button.action = "layout.translate";
            button.title = "Configured Move";
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        ctx->Yield(3);
        ctx->WindowFocus(second.c_str());
        ctx->ItemClick((second + "/test.configured-pan/Configured Move").c_str());
        ctx->Yield(3);
        IM_CHECK(App().Layout(pair.second)->Tool() == vdev::LayoutTool::Translate);
        IM_CHECK(App().Layout().Tool() == vdev::LayoutTool::Pan);
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto config = workspace.Application().Configuration();
            config.toolbarItems.back().fields |= vdev::ToolbarVisibility;
            config.toolbarItems.back().visible = false;
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        ctx->Yield(3);
        IM_CHECK(!ctx->ItemExists((second + "/test.configured-pan/Configured Move").c_str()));
        OnMainThread(ctx, [&] {
            auto &window = *App().Layout(pair.second);
            IM_CHECK_NO_RET(window.UsesGpuViewport() && window.View().ReadbackFrames() == reads &&
                            window.ViewportUploadsForTests() == uploads);
            IM_CHECK_NO_RET(App().State().History().UndoCount() == history);
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(previous, error));
            Client(pair.first).Command("layout.select");
        });
        pair.Close(ctx);
    };
}
} // namespace vdevuitest
