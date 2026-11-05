#include "tests/ui/UiTests.h"
#include "workspace/EditorClient.h"
#include <cmath>
#include <optional>

namespace vdevuitest {
namespace {
using namespace vdev;
const EditorField &Field(const EditorResponse &result, const char *key) {
    const auto *field = EditorClient::Find(result, key); if (!field) throw std::runtime_error("Missing preference field"); return *field;
}
const EditorField &Field(const EditorField &row, const char *key) {
    for (const auto &field : row.fields) if (field.key == key) return field;
    throw std::runtime_error("Missing preference value");
}
EditorResponse Current() {
    auto &editor = (&App().WorkspaceForTests())->Application(); return EditorClient(editor.Host(), 0).Query("preferences.values");
}
EditorResponse Apply(const std::vector<EditorField> &fields) {
    auto &editor = (&App().WorkspaceForTests())->Application(); const auto current = Current();
    EditorField value{"value", EDITOR_RECORD}; value.fields = fields;
    const std::vector<EditorField> owned{value}; const auto borrowed = BorrowEditorFields(owned);
    return EditorClient(editor.Host(), 0).Command("preferences.apply", {IntegerArgument("revision", Field(current, "revision").integer), borrowed.values.front()});
}
struct Restore {
    ImGuiTestContext *ctx;
    EditorResponse original;
    std::optional<EditorConfiguration> configuration;
    ~Restore() {
        OnMainThread(ctx, [&] {
            auto &workspace = *(&App().WorkspaceForTests()); auto &editor = workspace.Application();
            editor.Execute("preferences.close", {editor.Scope(), nullptr, 0});
            if (configuration) { std::string error; IM_CHECK_NO_RET(workspace.Configure(*configuration, error)); }
            if (original.status == EDITOR_OK) IM_CHECK_NO_RET(Apply(Field(original, "value").fields).status <= EDITOR_NO_CHANGE);
        });
    }
};
EditorView Open(ImGuiTestContext *ctx, const char *page = "") {
    const auto id = Read<EditorView>(ctx, [&] {
        auto &workspace = *(&App().WorkspaceForTests()); auto &editor = workspace.Application();
        const EditorValue value{EDITOR_TEXT("page"), EDITOR_TEXT_VALUE, {}, EditorLiteral(page)};
        const auto result = editor.Execute("preferences.open", {editor.Scope(), &value, 1});
        IM_CHECK_NO_RET(result.status == EDITOR_OK); return workspace.Role("main.preferences");
    }); ctx->Yield(4); return id;
}
EditorResponse Inspect(EditorView view) {
    return EditorClient((&App().WorkspaceForTests())->Application().Host(), 0).Inspect(view);
}
void Cancel(ImGuiTestContext *ctx) { ctx->ItemClick(PreferencesRef("/Cancel").c_str()); ctx->Yield(3); }
}
void RegisterPreferencesTests(ImGuiTestEngine *engine) {
    auto *stable = IM_REGISTER_TEST(engine, "app", "layout_preferences_reuse_published_query_version");
    stable->GuiFunc = MainThreadGui;
    stable->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        const auto reads = Read<uint64_t>(ctx, [&] { return App().Layout().PreferencesReadsForTests(); });
        IM_CHECK(reads > 0);
        ctx->Yield(12);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(App().Layout().PreferencesReadsForTests() == reads); });
    };
    auto *test = IM_REGISTER_TEST(engine, "tools", "preferences_pages_draft_defaults_and_cancel");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Restore restore{ctx};
        auto &state = App().State(); auto &workspace = *(&App().WorkspaceForTests()); auto &editor = workspace.Application();
        size_t undo = 0; bool redo = false, dirty = false; ObjectRef selection{};
        OnMainThread(ctx, [&] {
            restore.original = Current(); undo = state.History().UndoCount(); redo = state.History().CanRedo();
            dirty = state.document->Dirty(); selection = editor.Selection();
        });
        EditorView first = 0;
        for (const auto &page : Preferences::Pages()) {
            const auto id = Open(ctx, page.id); if (!first) first = id;
            IM_CHECK(id == first);
            OnMainThread(ctx, [&] {
                IM_CHECK_NO_RET(EditorClient::Text(Inspect(id), "page") == page.id && EditorClient::Integer(Inspect(id), "revision"));
                IM_CHECK_NO_RET(workspace.Open("preferences.general") == id && Current().fields == restore.original.fields);
            });
        }
        Open(ctx, "actions");
        ctx->ItemInputValue(PreferencesRef("/**/##value").c_str(), "4.25"); ctx->Yield(2);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(std::abs(Field(Field(Inspect(first), "value"), "3D Layout\\Actions\\Object Translate Step").real - 4.25) < .001);
            IM_CHECK_NO_RET(Current().fields == restore.original.fields);
        });
        ctx->ItemClick(PreferencesRef("/Default").c_str()); ctx->Yield(3);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(std::abs(Field(Field(Inspect(first), "value"), "3D Layout\\Actions\\Object Translate Step").real - .1) < .001);
            IM_CHECK_NO_RET(Current().fields == restore.original.fields);
        });
        Cancel(ctx);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(!workspace.Role("main.preferences") && Current().fields == restore.original.fields &&
                state.History().UndoCount() == undo && state.History().CanRedo() == redo && state.document->Dirty() == dirty && editor.Selection() == selection);
        });
    };
    test = IM_REGISTER_TEST(engine, "tools", "preferences_apply_stale_and_view_state");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Restore restore{ctx};
        auto &state = App().State(); auto &workspace = *(&App().WorkspaceForTests()); auto &editor = workspace.Application();
        size_t undo = 0; bool redo = false, dirty = false, automatic = false; ObjectRef selection{}; CKContext *context = nullptr;
        OnMainThread(ctx, [&] {
            restore.original = Current(); undo = state.History().UndoCount(); redo = state.History().CanRedo(); dirty = state.document->Dirty();
            automatic = App().SettingsForTests().automaticLink; selection = editor.Selection(); context = state.document->Context();
        });
        Open(ctx, "schematic"); ctx->ItemClick(PreferencesRef("/**/Automatic Link").c_str());
        ctx->ItemInputValue(PreferencesRef("/**/##maxrecent").c_str(), 27); ctx->Yield(2);
        ctx->ItemClick(PreferencesRef("/OK").c_str()); ctx->Yield(5);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(!workspace.Role("main.preferences") && App().SettingsForTests().maxRecentBlocks == 27 && App().SettingsForTests().automaticLink != automatic);
            IM_CHECK_NO_RET(state.document->Context() == context && state.History().UndoCount() == undo && state.History().CanRedo() == redo &&
                state.document->Dirty() == dirty && editor.Selection() == selection);
        });
        Open(ctx, "miscellaneous"); Cancel(ctx);
        const auto reopened = Open(ctx);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(EditorClient::Text(Inspect(reopened), "page") == "miscellaneous");
            IM_CHECK_NO_RET(Apply({{"Schematic\\Max Recent Building Block Count", EDITOR_INTEGER_VALUE, {}, {}, 28}}).status == EDITOR_OK);
        });
        ctx->ItemClick(PreferencesRef("/OK").c_str()); ctx->Yield(4);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(workspace.Role("main.preferences") && !EditorClient::Text(Inspect(reopened), "message").empty() && App().SettingsForTests().maxRecentBlocks == 28);
        });
        Cancel(ctx);
        OnMainThread(ctx, [&] {
            const EditorValue invalid{EDITOR_TEXT("page"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("missing")};
            IM_CHECK_NO_RET(editor.Execute("preferences.open", {editor.Scope(), &invalid, 1}).status == EDITOR_INVALID && !workspace.Role("main.preferences"));
        });
    };
    test = IM_REGISTER_TEST(engine, "modules", "preferences_use_external_queries_commands_editors_and_views");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Restore restore{ctx};
        auto &state = App().State(); auto &workspace = *(&App().WorkspaceForTests()); auto &editor = workspace.Application();
        OnMainThread(ctx, [&] {
            restore.original = Current(); restore.configuration = editor.Configuration();
            auto configuration = *restore.configuration;
            for (auto &choice : configuration.providers) {
                if ((choice.kind == "query" && choice.id == "preferences.values") || (choice.kind == "command" && choice.id == "preferences.apply")) choice.provider = "example.preferences";
                if (choice.kind == "editor" && choice.id == "value.integer") choice.provider = "example.integer";
            }
            std::string error; IM_CHECK_NO_RET(workspace.Configure(configuration, error));
        });
        const auto view = Open(ctx, "snap-align");
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(Field(Field(Inspect(view), "value"), "Schematic\\Max Recent Building Block Count").integer == 37); });
        ctx->ItemInputValue(PreferencesRef("/**/##radius").c_str(), 11); ctx->Yield(2);
        ctx->ItemClick(PreferencesRef("/**/Increment").c_str()); ctx->Yield(2);
        ctx->ItemClick(PreferencesRef("/OK").c_str()); ctx->Yield(5);
        OnMainThread(ctx, [&] {
            const auto counts = EditorClient(editor.Host(), 0).Query("example.preference-counts");
            IM_CHECK_NO_RET(EditorClient::Integer(counts, "reads") && EditorClient::Integer(counts, "writes") == 1 &&
                App().SettingsForTests().maxRecentBlocks == 37 && App().SettingsForTests().Int("3D Layout\\Snap\\Translation Influence (3D Entities)") == 18);
        });
        const bool dynamic = Read<bool>(ctx, [&] { return App().SettingsForTests().Bool("3D Layout\\Show Dynamic Objects"); });
        ctx->MenuClick(dynamic ? "//##MainMenuBar/Options/Hide Dynamic Objects" : "//##MainMenuBar/Options/Show Dynamic Objects"); ctx->Yield(4);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(App().SettingsForTests().Bool("3D Layout\\Show Dynamic Objects") != dynamic &&
                EditorClient::Integer(EditorClient(editor.Host(), 0).Query("example.preference-counts"), "writes") == 2);
        });
        Open(ctx, "reference-guide");
        OnMainThread(ctx, [&] {
            auto configuration = editor.Configuration();
            for (auto &choice : configuration.providers) if (choice.kind == "view" && choice.id == "preferences.general") choice.provider = "example.preferences";
            std::string error; IM_CHECK_NO_RET(workspace.Configure(configuration, error));
        }); ctx->Yield(4);
        OnMainThread(ctx, [&] {
            const auto replaced = workspace.Role("main.preferences");
            IM_CHECK_NO_RET(replaced && EditorClient::Text(Inspect(replaced), "page") == "reference-guide" &&
                EditorClient::Text(Inspect(replaced), "message") == "External preferences view");
            IM_CHECK_NO_RET(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
            editor.Execute("preferences.close", {editor.Scope(), nullptr, 0});
            std::string error; IM_CHECK_NO_RET(workspace.Configure(*restore.configuration, error));
        }); ctx->Yield(3);
        const auto restored = Open(ctx);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(EditorClient::Text(Inspect(restored), "page") == "reference-guide"); });
        Cancel(ctx);
    };
}
} // namespace vdevuitest
