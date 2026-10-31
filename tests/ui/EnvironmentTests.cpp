#include "tests/ui/UiTests.h"
#include "workspace/EditorClient.h"
#include <algorithm>
#include <optional>

namespace vdevuitest {
namespace {
const vdev::EditorField &Field(const vdev::EditorResponse &response, const char *key) {
    const auto *field = vdev::EditorClient::Find(response, key);
    if (!field) throw std::runtime_error("Missing environment UI projection field");
    return *field;
}
struct RestorePaths {
    ImGuiTestContext *ctx;
    vdev::EditorResponse original;
    decltype(vdev::Preferences::paths) preferences;
    bool saved = false;
    std::optional<vdev::EditorConfiguration> configuration;
    ~RestorePaths() {
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &editor = state.workspace->Application();
            editor.Execute("environment.close-managers", {editor.Scope(), nullptr, 0});
            if (configuration) { std::string error; IM_CHECK_NO_RET(state.workspace->Configure(*configuration, error)); }
            const vdev::EditorClient client(editor.Host(), 0);
            const auto current = client.Query("paths.list");
            const std::vector<vdev::EditorField> fields{Field(original, "categories")};
            const auto values = vdev::BorrowEditorFields(fields);
            client.Command("paths.apply", {vdev::IntegerArgument("revision", Field(current, "revision").integer), values.values.front()});
            state.preferences.paths = preferences; state.preferences.pathsSaved = saved;
        });
    }
};
void OpenPaths(ImGuiTestContext *ctx) {
    OnMainThread(ctx, [] {
        auto &editor = App().State().workspace->Application();
        IM_CHECK_NO_RET(editor.Execute("environment.open-paths", {editor.Scope(), nullptr, 0}).status == EDITOR_OK);
    }); ctx->Yield(3);
}
void AddPath(ImGuiTestContext *ctx, const char *path) {
    ctx->ItemClick(EnvironmentRef("paths", "/Add Path").c_str()); ctx->Yield(2);
    ctx->KeyChars(path); ctx->KeyPress(ImGuiKey_Enter); ctx->Yield(2);
}
} // namespace
void RegisterEnvironmentTests(ImGuiTestEngine *engine) {
    auto *test = IM_REGISTER_TEST(engine, "tools", "path_draft_accept_cancel_and_reorder");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State(); auto &editor = state.workspace->Application();
        RestorePaths restore{ctx};
        size_t undo = 0; bool redo = false, dirty = false; vdev::ObjectRef selection{};
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), 0);
            restore.original = client.Query("paths.list"); restore.preferences = state.preferences.paths; restore.saved = state.preferences.pathsSaved;
            undo = state.History().UndoCount(); redo = state.History().CanRedo(); dirty = state.document->Dirty(); selection = editor.Selection();
        });
        OpenPaths(ctx);
        ctx->ItemClick(EnvironmentRef("paths", "/Add Path").c_str()); ctx->Yield(2);
        ctx->KeyChars("discarded path"); ctx->KeyPress(ImGuiKey_Escape); ctx->Yield(3);
        IM_CHECK(state.workspace->Role("main.paths") != 0);
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), 0);
            const auto draft = client.Inspect(state.workspace->Role("main.paths"));
            IM_CHECK_NO_RET(draft.status == EDITOR_OK && Field(draft, "categories").fields == Field(restore.original, "categories").fields);
        });
        AddPath(ctx, "Environment draft A"); AddPath(ctx, "Environment draft B");
        ctx->ItemClick(EnvironmentRef("paths", "/Up").c_str()); ctx->Yield(2);
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), 0);
            const auto draft = client.Inspect(state.workspace->Role("main.paths"));
            if (draft.status != EDITOR_OK || Field(draft, "categories").fields.empty()) { IM_CHECK_NO_RET(false); return; }
            const auto &paths = Field(draft, "categories").fields[0].fields[1].texts;
            IM_CHECK_NO_RET(paths.size() >= 2 && paths[paths.size() - 2] == "Environment draft B" && paths.back() == "Environment draft A");
            IM_CHECK_NO_RET(Field(client.Query("paths.list"), "categories").fields == Field(restore.original, "categories").fields && state.preferences.paths == restore.preferences);
        });
        ctx->ItemClick(EnvironmentRef("paths", "/Cancel").c_str()); ctx->Yield(3);
        IM_CHECK(!state.workspace->Role("main.paths"));
        OpenPaths(ctx); AddPath(ctx, "Environment committed A"); AddPath(ctx, "Environment committed B");
        ctx->ItemClick(EnvironmentRef("paths", "/Ok").c_str()); ctx->Yield(4);
        IM_CHECK(!state.workspace->Role("main.paths"));
        OnMainThread(ctx, [&] {
            const auto paths = vdev::EditorClient(editor.Host(), 0).Query("paths.list");
            const auto &values = Field(paths, "categories").fields[0].fields[1].texts;
            IM_CHECK_NO_RET(values.size() >= 2 && values[values.size() - 2] == "Environment committed A" && values.back() == "Environment committed B");
            IM_CHECK_NO_RET(state.preferences.pathsSaved && state.preferences.paths[0].second.back() == "Environment committed B");
            IM_CHECK_NO_RET(state.History().UndoCount() == undo && state.History().CanRedo() == redo && state.document->Dirty() == dirty && editor.Selection() == selection);
        });
    };
    test = IM_REGISTER_TEST(engine, "tools", "plugin_category_state_and_single_instance");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State(); auto &workspace = *state.workspace; auto &editor = workspace.Application();
        EditorView first = 0;
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(editor.Execute("environment.open-plugins", {editor.Scope(), nullptr, 0}).status == EDITOR_OK);
            first = workspace.Role("main.plugins");
            IM_CHECK_NO_RET(first && workspace.Open("environment.plugins") == first);
            const vdev::EditorClient client(editor.Host(), first);
            const auto blocks = client.Revision("catalog.building-blocks"), parameters = client.Revision("scene.parameter-types");
            const auto attributes = client.Query("attribute.catalog");
            IM_CHECK_NO_RET(blocks && parameters && attributes.status == EDITOR_OK);
            IM_CHECK_NO_RET(client.Command("environment.refresh").status == EDITOR_OK);
            IM_CHECK_NO_RET(client.Revision("catalog.building-blocks") != blocks && client.Revision("scene.parameter-types") != parameters &&
                Field(client.Query("attribute.catalog"), "catalog").integer != Field(attributes, "catalog").integer);
        }); ctx->Yield(4);
        auto window = EnvironmentRef("plugins");
        ctx->ItemClick((window + "/**/Bitmap Readers").c_str()); ctx->Yield(2);
        OnMainThread(ctx, [&] {
            const auto view = vdev::EditorClient(editor.Host(), first).Inspect(first);
            const auto &open = Field(view, "open-categories").texts;
            IM_CHECK_NO_RET(std::find(open.begin(), open.end(), "Bitmap Readers") == open.end());
        });
        ctx->WindowClose(window.c_str()); ctx->Yield(3);
        OnMainThread(ctx, [&] { editor.Execute("environment.open-plugins", {editor.Scope(), nullptr, 0}); }); ctx->Yield(3);
        window = EnvironmentRef("plugins");
        OnMainThread(ctx, [&] {
            const auto view = vdev::EditorClient(editor.Host(), 0).Inspect(workspace.Role("main.plugins"));
            const auto &open = Field(view, "open-categories").texts;
            IM_CHECK_NO_RET(std::find(open.begin(), open.end(), "Bitmap Readers") == open.end());
            editor.Execute("environment.close-managers", {editor.Scope(), nullptr, 0});
        });
    };
    test = IM_REGISTER_TEST(engine, "modules", "path_dialog_uses_external_folder_and_apply_commands");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State(); auto &workspace = *state.workspace; auto &editor = workspace.Application();
        RestorePaths restore{ctx};
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), 0);
            restore.original = client.Query("paths.list"); restore.preferences = state.preferences.paths; restore.saved = state.preferences.pathsSaved;
            restore.configuration = editor.Configuration(); auto selected = *restore.configuration;
            for (auto &provider : selected.providers)
                if (provider.kind == "command" && (provider.id == "paths.choose-folder" || provider.id == "paths.apply"))
                    provider.provider = "example.environment";
            std::string error; IM_CHECK_NO_RET(workspace.Configure(selected, error));
        });
        OpenPaths(ctx);
        ctx->ItemClick(EnvironmentRef("paths", "/Add Path").c_str()); ctx->Yield(2);
        ctx->ItemClick("**/..."); ctx->Yield(4);
        OnMainThread(ctx, [&] {
            const auto draft = vdev::EditorClient(editor.Host(), 0).Inspect(workspace.Role("main.paths"));
            IM_CHECK_NO_RET(Field(draft, "categories").fields == Field(restore.original, "categories").fields);
        });
        ctx->ItemClick(EnvironmentRef("paths", "/Add Path").c_str()); ctx->Yield(2);
        ctx->ItemClick("**/..."); ctx->Yield(4);
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), 0);
            const auto draft = client.Inspect(workspace.Role("main.paths"));
            IM_CHECK_NO_RET(Field(draft, "categories").fields[0].fields[1].texts.back() == "External chosen folder");
        });
        ctx->ItemClick(EnvironmentRef("paths", "/Ok").c_str()); ctx->Yield(4);
        IM_CHECK(!workspace.Role("main.paths"));
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), 0);
            const auto calls = client.Query("example.environment-counts");
            IM_CHECK_NO_RET(calls.status == EDITOR_OK && vdev::EditorClient::Integer(calls, "folders") == 2 && vdev::EditorClient::Integer(calls, "paths") == 1);
            IM_CHECK_NO_RET(state.preferences.paths[0].second.back() == "External chosen folder");
        });
    };
}
} // namespace vdevuitest
