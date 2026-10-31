#include "tests/ui/UiTests.h"
#include "workspace/EditorClient.h"
#include <optional>

namespace vdevuitest {
namespace {
using namespace vdev;
const EditorField &Field(const EditorResponse &response, const char *key) {
    const auto *field = EditorClient::Find(response, key); if (!field) throw std::runtime_error("Missing diagnostic view field"); return *field;
}
bool Ready(ImGuiTestContext *ctx, EditorView view) {
    for (int i = 0; i < 100; ++i) {
        if (Read<bool>(ctx, [&] {
            auto &editor = App().State().workspace->Application();
            return EditorClient::Integer(EditorClient(editor.Host(), 0).Inspect(view), "revision") != 0;
        })) return true;
        ctx->Yield();
    }
    return false;
}
EditorView Open(ImGuiTestContext *ctx, const char *name) {
    return Read<EditorView>(ctx, [&] {
        auto &workspace = *App().State().workspace; auto &editor = workspace.Application();
        const auto opened = editor.Execute(std::string("diagnostics.open-") + name, {editor.Scope(), nullptr, 0});
        IM_CHECK_NO_RET(opened.status == EDITOR_OK);
        return workspace.Role(std::string("main.") + name);
    });
}
struct Restore {
    ImGuiTestContext *ctx;
    std::optional<EditorConfiguration> configuration;
    EditorResponse links;
    ~Restore() {
        OnMainThread(ctx, [&] {
            auto &workspace = *App().State().workspace; auto &editor = workspace.Application();
            editor.Execute("diagnostics.close-tools", {editor.Scope(), nullptr, 0});
            editor.Execute("environment.close-managers", {editor.Scope(), nullptr, 0});
            if (configuration) { std::string error; IM_CHECK_NO_RET(workspace.Configure(*configuration, error)); }
            if (links.status == EDITOR_OK) {
                const EditorClient client(editor.Host(), 0); const auto current = client.Query("environment.link-options");
                IM_CHECK_NO_RET(client.Command("environment.set-link-options", {IntegerArgument("revision", EditorClient::Integer(current, "revision")),
                    IntegerArgument("delay", EditorClient::Integer(links, "delay")), IntegerArgument("prevent", EditorClient::Integer(links, "prevent")),
                    IntegerArgument("iterations", EditorClient::Integer(links, "iterations"))}).status <= EDITOR_NO_CHANGE);
            }
        });
    }
};
void OpenLinks(ImGuiTestContext *ctx) {
    OnMainThread(ctx, [] {
        auto &editor = App().State().workspace->Application(); editor.Execute("environment.open-link-options", {editor.Scope(), nullptr, 0});
    }); ctx->Yield(3);
}
void SetLinkDelay(ImGuiTestContext *ctx, const char *text) {
    const auto input = Read<ImGuiID>(ctx, [&] {
        auto &workspace = *App().State().workspace;
        auto *window = ImGui::FindWindowByName(workspace.WindowName(workspace.Role("main.link-options")).c_str());
        if (!window) return ImGuiID(0);
        const auto *table = ImGui::TableFindByID(ImHashStr("parameters", 0, window->ID));
        if (!table) return ImGuiID(0);
        const int row = 0;
        return ImHashStr("##value", 0, ImHashData(&row, sizeof(row), table->ID));
    });
    IM_CHECK(input != 0);
    ctx->ItemInputValue(input, text); ctx->Yield(2);
}
}
void RegisterDiagnosticTests(ImGuiTestEngine *engine) {
    auto *test = IM_REGISTER_TEST(engine, "schematic", "replace_block_command_completion");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, SchematicRef().c_str());
        auto &state = App().State();
        std::optional<SchematicCanvas::ShownBehavior> target;
        CKGUID prototype;
        std::vector<CKGUID> recent;
        OnMainThread(ctx, [&] {
            for (const auto &block : App().Canvas().ShownBehaviors())
                if (block.buildingBlock) { target = block; break; }
            if (!target) return;
            prototype = Object<CKBehavior>(target->behavior)->GetPrototypeGuid();
            recent = state.recentBlocks; state.recentBlocks = {prototype};
        });
        IM_CHECK(target.has_value());
        struct RestoreRecent {
            ImGuiTestContext *ctx; std::vector<CKGUID> value;
            ~RestoreRecent() { OnMainThread(ctx, [&] { App().State().recentBlocks = value; }); }
        } restore{ctx, recent};
        const auto steps = state.History().UndoCount();
        const auto dirty = state.document->Dirty();
        ctx->MouseMoveToPos(Center(target->min, target->max));
        ctx->KeyDown(ImGuiMod_Shift); ctx->MouseClick(ImGuiMouseButton_Right); ctx->KeyUp(ImGuiMod_Shift); ctx->Yield(2);
        ctx->ItemClick(("//$FOCUSED/" + target->name).c_str()); ctx->Yield(2);
        uint64_t logSequence = 0;
        OnMainThread(ctx, [&] {
            state.recentBlocks.clear();
            if (!state.eventLog.Entries().empty()) logSequence = state.eventLog.Entries().back().sequence;
        });
        ctx->ItemClick("//Replace Building Block/Yes"); ctx->Yield(5);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(state.History().UndoCount() == steps + 1 && state.document->IsDiscarded(target->behavior));
            IM_CHECK_NO_RET(!state.recentBlocks.empty() && state.recentBlocks.front() == prototype);
            bool logged = false;
            for (const auto &entry : state.eventLog.Entries())
                if (entry.sequence > logSequence && entry.text == "Replaced 1 Building Block(s)") logged = true;
            IM_CHECK_NO_RET(logged);
        });
        Undo(ctx);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(!state.document->IsDiscarded(target->behavior) && state.document->Dirty() == dirty); });
    };
    test = IM_REGISTER_TEST(engine, "tools", "diagnostic_instances_exclusions_and_memory_sort");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Restore restore{ctx};
        auto &state = App().State(); auto &workspace = *state.workspace; auto &editor = workspace.Application();
        size_t undo = 0; bool redo = false, dirty = false; ObjectRef selection{};
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(CreateLayoutObject(*state.document, LayoutObject::Material).ok);
            IM_CHECK_NO_RET(CreateLayoutObject(*state.document, LayoutObject::Material).ok);
            undo = state.History().UndoCount(); redo = state.History().CanRedo(); dirty = state.document->Dirty(); selection = editor.Selection();
        });
        const auto first = Open(ctx, "unused"); IM_CHECK(first && Ready(ctx, first));
        const auto firstWindow = "//" + workspace.WindowName(first);
        ctx->WindowFocus(firstWindow.c_str()); ctx->SetRef(firstWindow.c_str()); ctx->ItemClick("**/-"); ctx->SetRef(""); ctx->Yield(2);
        OnMainThread(ctx, [&] { IM_CHECK_NO_RET(Field(EditorClient(editor.Host(), 0).Inspect(first), "excluded").objects.size() == 1); });
        const auto second = Read<EditorView>(ctx, [&] { return workspace.Open("diagnostics.unused"); });
        IM_CHECK(second && second != first && Ready(ctx, second)); ctx->Yield(3);
        OnMainThread(ctx, [&] {
            const EditorClient client(editor.Host(), 0);
            IM_CHECK_NO_RET(Field(client.Inspect(first), "excluded").objects.size() == 1 && Field(client.Inspect(second), "excluded").objects.empty());
            IM_CHECK_NO_RET(workspace.Close(first) && workspace.Close(second));
        }); ctx->Yield(2);
        const auto memory = Open(ctx, "memory"); IM_CHECK(memory && Ready(ctx, memory));
        const auto memoryWindow = "//" + workspace.WindowName(memory);
        ctx->WindowFocus(memoryWindow.c_str()); ctx->SetRef(memoryWindow.c_str()); ctx->ItemClick("**/##row"); ctx->SetRef(""); ctx->Yield(2);
        std::vector<ObjectRef> selected;
        OnMainThread(ctx, [&] { selected = Field(EditorClient(editor.Host(), 0).Inspect(memory), "selected").objects; IM_CHECK_NO_RET(selected.size() == 1); });
        const auto header = Read<ImGuiID>(ctx, [&] {
            auto *window = ImGui::FindWindowByName(workspace.WindowName(memory).c_str());
            const auto list = ImHashStr("##objects", 0, window->ID);
            const int column = 2;
            return ImHashStr("##header", 0, ImHashData(&column, sizeof(column), list));
        });
        ctx->ItemClick(header); ctx->Yield(2);
        OnMainThread(ctx, [&] {
            const auto inspected = EditorClient(editor.Host(), 0).Inspect(memory);
            IM_CHECK_NO_RET(EditorClient::Integer(inspected, "sort") == 2 && Field(inspected, "selected").objects == selected);
        });
        const auto other = Read<EditorView>(ctx, [&] { return workspace.Open("diagnostics.memory"); });
        IM_CHECK(other && other != memory && Ready(ctx, other)); ctx->Yield(2);
        OnMainThread(ctx, [&] {
            const EditorClient client(editor.Host(), 0);
            IM_CHECK_NO_RET(Field(client.Inspect(memory), "selected").objects == selected && Field(client.Inspect(other), "selected").objects.empty());
            IM_CHECK_NO_RET(state.History().UndoCount() == undo && state.History().CanRedo() == redo && state.document->Dirty() == dirty && editor.Selection() == selection);
        });
    };
    test = IM_REGISTER_TEST(engine, "tools", "link_properties_draft_cancel_accept_and_stale");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, SchematicRef().c_str()); Restore restore{ctx};
        auto &state = App().State(); auto &workspace = *state.workspace; auto &editor = workspace.Application();
        size_t undo = 0; bool redo = false, dirty = false; ObjectRef selection{}; int parameters = 0;
        OnMainThread(ctx, [&] {
            restore.links = EditorClient(editor.Host(), 0).Query("environment.link-options");
            undo = state.History().UndoCount(); redo = state.History().CanRedo(); dirty = state.document->Dirty(); selection = editor.Selection();
            parameters = state.document->Context()->GetObjectsCountByClassID(CKCID_PARAMETEROUT);
        });
        ctx->ItemClick(SchematicRef("/**/linkprops/##bitmap").c_str()); ctx->Yield(3);
        IM_CHECK(workspace.Role("main.link-options") != 0);
        SetLinkDelay(ctx, "17");
        ctx->ItemClick(EnvironmentRef("link-options", "/Cancel").c_str()); ctx->Yield(3);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(EditorClient(editor.Host(), 0).Query("environment.link-options").fields == restore.links.fields &&
                state.document->Context()->GetObjectsCountByClassID(CKCID_PARAMETEROUT) == parameters);
        });
        OpenLinks(ctx);
        SetLinkDelay(ctx, "17");
        ctx->ItemClick(EnvironmentRef("link-options", "/OK").c_str()); ctx->Yield(4);
        IM_CHECK(!workspace.Role("main.link-options"));
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(state.preferences.defaultLinkDelay == 17 && state.History().UndoCount() == undo && state.History().CanRedo() == redo &&
                state.document->Dirty() == dirty && editor.Selection() == selection);
        });
        OpenLinks(ctx); ctx->KeyPress(ImGuiKey_Enter); ctx->Yield(3);
        IM_CHECK(!workspace.Role("main.link-options"));
        OpenLinks(ctx);
        OnMainThread(ctx, [&] {
            const EditorClient client(editor.Host(), 0); const auto current = client.Query("environment.link-options");
            IM_CHECK_NO_RET(client.Command("environment.set-link-options", {IntegerArgument("revision", EditorClient::Integer(current, "revision")),
                IntegerArgument("delay", 23), IntegerArgument("prevent", EditorClient::Integer(current, "prevent")),
                IntegerArgument("iterations", EditorClient::Integer(current, "iterations"))}).status == EDITOR_OK);
        });
        ctx->KeyPress(ImGuiKey_Enter); ctx->Yield(3);
        IM_CHECK(workspace.Role("main.link-options") != 0 && state.preferences.defaultLinkDelay == 23);
        ctx->ItemClick(EnvironmentRef("link-options", "/Cancel").c_str()); ctx->Yield(2);
    };
    test = IM_REGISTER_TEST(engine, "modules", "diagnostics_and_link_properties_use_external_commands");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Restore restore{ctx};
        auto &state = App().State(); auto &workspace = *state.workspace; auto &editor = workspace.Application();
        OnMainThread(ctx, [&] {
            restore.configuration = editor.Configuration(); restore.links = EditorClient(editor.Host(), 0).Query("environment.link-options");
            auto configuration = *restore.configuration;
            for (auto &provider : configuration.providers)
                if ((provider.kind == "command" && (provider.id == "diagnostics.scan-versions" || provider.id == "environment.set-link-options")) ||
                    (provider.kind == "query" && provider.id == "diagnostics.versions"))
                    provider.provider = "example.diagnostics";
                else if (provider.kind == "editor" && provider.id == "value.integer") provider.provider = "example.integer";
            std::string error; IM_CHECK_NO_RET(workspace.Configure(configuration, error));
        });
        const auto view = Open(ctx, "versions"); IM_CHECK(view && Ready(ctx, view));
        OnMainThread(ctx, [&] {
            const EditorClient client(editor.Host(), 0);
            IM_CHECK_NO_RET(EditorClient::Text(client.Inspect(view), "report") == "External version report" &&
                EditorClient::Integer(client.Query("example.diagnostic-counts"), "scans") == 1);
            editor.Execute("diagnostics.close-tools", {editor.Scope(), nullptr, 0});
        }); ctx->Yield(2);
        OpenLinks(ctx); SetLinkDelay(ctx, "19");
        ctx->ItemClick(EnvironmentRef("link-options", "/**/Increment").c_str()); ctx->Yield(2);
        ctx->ItemClick(EnvironmentRef("link-options", "/OK").c_str()); ctx->Yield(4);
        OnMainThread(ctx, [&] {
            const auto count = EditorClient(editor.Host(), 0).Query("example.diagnostic-counts");
            IM_CHECK_NO_RET(EditorClient::Integer(count, "links") == 1 && state.preferences.defaultLinkDelay == 26);
        });
    };
}
} // namespace vdevuitest
