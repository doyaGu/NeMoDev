#include "tests/ui/UiTests.h"

#include <algorithm>
#include <string_view>

namespace vdevuitest {
namespace {
// Count deliveries through the real module interface, including cached
// queries. A provider read counter alone would miss repeated result copies.
class QueryCalls {
public:
    explicit QueryCalls(ImGuiTestContext *ctx) : ctx(ctx) {
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            originalConfiguration = workspace.Application().Configuration();
            host = const_cast<EditorHost *>(workspace.Application().Host());
            query = host->query;
            current = this;
            host->query = [](void *data, EditorString id, const EditorRequest *request, EditorSink sink) {
                auto &self = *current;
                const std::string_view name(id.data, id.size);
                if (name == "runtime.state" && request && !request->scope.view) ++self.runtime;
                if (name == "layout.input") ++self.input;
                if (name == "runtime.state" && request && !request->scope.view) {
                    struct Relay { QueryCalls &self; EditorSink sink; } relay{self, sink};
                    return self.query(data, id, request, {&relay, [](void *p, const EditorValue *values, uint32_t count) {
                        auto &relay = *static_cast<Relay *>(p);
                        for (uint32_t i = 0; i < count; ++i)
                            if (std::string_view(values[i].key.data, values[i].key.size) == "log-revision")
                                relay.self.logRevision = values[i].integer;
                        return relay.sink.write ? relay.sink.write(relay.sink.data, values, count) : EDITOR_OK;
                    }});
                }
                return self.query(data, id, request, sink);
            };
        });
    }
    ~QueryCalls() {
        OnMainThread(ctx, [&] {
            host->query = query;
            current = nullptr;
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(originalConfiguration, error));
        });
    }
    struct Sample { int frame; uint64_t runtime, input, logRevision; };
    Sample Read() const {
        return vdevuitest::Read<Sample>(ctx, [&] { return Sample{ImGui::GetFrameCount(), runtime, input, logRevision}; });
    }
    void Configure(std::vector<vdev::ToolbarPlacement> overrides) {
        OnMainThread(ctx, [&] {
            auto next = originalConfiguration;
            for (auto &item : overrides) {
                next.toolbarItems.erase(std::remove_if(next.toolbarItems.begin(), next.toolbarItems.end(),
                    [&](const auto &old) { return old.id == item.id; }), next.toolbarItems.end());
                next.toolbarItems.push_back(std::move(item));
            }
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(next, error));
        });
        ctx->Yield(4);
    }
private:
    ImGuiTestContext *ctx;
    EditorHost *host = nullptr;
    decltype(EditorHost::query) query = nullptr;
    vdev::EditorConfiguration originalConfiguration;
    uint64_t runtime = 0, input = 0, logRevision = 0;
    static thread_local QueryCalls *current;
};
thread_local QueryCalls *QueryCalls::current = nullptr;
vdev::ToolbarPlacement Hide(const char *id) {
    vdev::ToolbarPlacement item;
    item.id = id; item.visible = false; item.fields = vdev::ToolbarVisibility;
    return item;
}
} // namespace

void RegisterFrameQueryTests(ImGuiTestEngine *engine) {
    auto *test = IM_REGISTER_TEST(engine, "framequeries", "status_controls_only_read_runtime_when_displaying_playback");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        QueryCalls calls(ctx);
        auto before = calls.Read(); ctx->Yield(12); auto after = calls.Read();
        IM_CHECK_LE(after.runtime - before.runtime, uint64_t(after.frame - before.frame));
        calls.Configure({Hide("status.play")});
        before = calls.Read(); ctx->Yield(12); after = calls.Read();
        IM_CHECK_EQ(after.runtime, before.runtime);
        // A configured replacement uses its actual action, including when a
        // different toolbar item has become the playback button.
        vdev::ToolbarPlacement replacement;
        replacement.id = "status.rewind"; replacement.action = "runtime.play-pause";
        replacement.fields = vdev::ToolbarAction;
        calls.Configure({Hide("status.play"), replacement});
        before = calls.Read(); ctx->Yield(12); after = calls.Read();
        IM_CHECK_LE(after.runtime - before.runtime, uint64_t(after.frame - before.frame));
        const auto logRevision = after.logRevision;
        OnMainThread(ctx, [&] {
            auto &editor = App().WorkspaceForTests().Application();
            IM_CHECK_NO_RET(editor.Execute("runtime.clear-log", {editor.Scope()}).status <= EDITOR_NO_CHANGE);
        });
        ctx->Yield(3);
        IM_CHECK_GT(calls.Read().logRevision, logRevision);
    };
    test = IM_REGISTER_TEST(engine, "framequeries", "idle_selection_status_does_not_read_gesture_input");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            IM_CHECK_NO_RET(workspace.Application().SelectTool(
                workspace.Application().Scope(workspace.Role("main.layout")), "layout.select") <= EDITOR_NO_CHANGE);
        });
        QueryCalls calls(ctx);
        // These two camera controls legitimately query their enabled state.
        calls.Configure({Hide("layout.fov"), Hide("layout.roll")});
        const auto before = calls.Read(); ctx->Yield(12); const auto after = calls.Read();
        IM_CHECK_EQ(after.input, before.input);
    };
}
} // namespace vdevuitest
