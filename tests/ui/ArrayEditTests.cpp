#include "tests/ui/UiTests.h"
#include "features/arrays/ArrayTable.h"
#include "core/level/ArrayCommands.h"
#include "workspace/EditorClient.h"
#include <cstring>

namespace vdevuitest {
namespace {
struct Editing {
    vdev::ArrayTable table;
    EditorView owner = 0;
    CK_ID array = 0;
    std::vector<CK_ID> arrays;
    std::unique_ptr<vdev::EditorConfiguration> configuration;
    bool visible = false;
} editing;
const char *window = "DataArray editing";
void Gui(ImGuiTestContext *ctx) {
    MainThreadGui(ctx);
    if (!editing.visible)
        return;
    ImGui::SetNextWindowSize({760, 400}, ImGuiCond_Always);
    ImGui::Begin(window);
    auto &app = (&App().WorkspaceForTests())->Application();
    app.EnterDraw();
    editing.table.Draw(vdev::EditorClient(app.Host(), editing.owner), App().State().document->Reference(editing.array));
    app.LeaveDraw();
    ImGui::End();
}
void Fixture(ImGuiTestContext *ctx) {
    OnMainThread(ctx, [] {
        auto &state = App().State();
        auto &app = (&App().WorkspaceForTests())->Application();
        auto *a = CKDataArray::Cast(state.host.Context()->CreateObject(CKCID_DATAARRAY, "Interactive array"));
        state.document->Include(a->GetID());
        a->InsertColumn(-1, CKARRAYTYPE_STRING, "Name");
        a->InsertColumn(-1, CKARRAYTYPE_PARAMETER, "Value", CKPGUID_FLOAT);
        a->AddRow();
        a->AddRow();
        a->SetElementStringValue(0, 0, "A");
        a->SetElementStringValue(1, 0, "B");
        editing.array = a->GetID();
        editing.arrays.push_back(editing.array);
        editing.owner = app.OpenScope();
        editing.visible = true;
        state.History().Clear();
    });
    ctx->Yield(3);
    ctx->SetRef(window);
}
void Cleanup(ImGuiTestContext *ctx) {
    OnMainThread(ctx, [] {
        auto &state = App().State();
        auto &app = (&App().WorkspaceForTests())->Application();
        editing.table.Cancel(vdev::EditorClient(app.Host(), editing.owner));
        app.CloseScope(editing.owner);
        if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
            ImGui::ClosePopupToLevel(0, false);
        if (editing.configuration) {
            std::string error;
            (&App().WorkspaceForTests())->Configure(*editing.configuration, error);
        }
        state.History().Clear();
        for (auto id : editing.arrays)
            if (auto *a = state.host.Context()->GetObject(id))
                state.host.Context()->DestroyObject(a);
        editing = Editing{};
    });
}
struct Clean {
    ImGuiTestContext *ctx;
    ~Clean() { Cleanup(ctx); }
};
CKDataArray *Array() { return CKDataArray::Cast(App().State().host.Context()->GetObject(editing.array)); }
std::string Value(ImGuiTestContext *ctx, int r = 0, int c = 0) {
    return Read<std::string>(ctx, [=] {
        auto *a = Array();
        int size = a->GetElementStringValue(r, c, nullptr, 0);
        std::string text(std::max(size, 1), '\0');
        a->GetElementStringValue(r, c, text.data(), int(text.size()));
        text.resize(std::strlen(text.c_str()));
        return text;
    });
}
size_t UndoCount(ImGuiTestContext *ctx) {
    return Read<size_t>(ctx, [] { return App().State().History().UndoCount(); });
}
ImGuiID Cell(ImGuiTestContext *ctx, int r, int c, bool header = false) {
    return Read<ImGuiID>(ctx, [=] {
        auto *root = ImGui::FindWindowByName(window);
        if (!root)
            return ImGuiID(0);
        auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(ImHashStr("array", 0, root->ID));
        if (!table)
            return ImGuiID(0);
        const auto row = ImHashData(&r, sizeof(r), table->ID);
        if (header)
            return ImHashStr(std::to_string(r).c_str(), 0, row);
        const auto column = ImHashData(&c, sizeof(c), row);
        return ImHashStr("##cell", 0, column);
    });
}
void Type(ImGuiTestContext *ctx, ImGuiID id, const char *text) {
    ctx->ItemClick(id);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
    ctx->KeyChars(text);
    ctx->Yield(2);
}
void Context(ImGuiTestContext *ctx, ImGuiID id, const char *item) {
    ctx->ItemClick(id, ImGuiMouseButton_Right);
    ctx->Yield(2);
    ctx->ItemClick((std::string("//$FOCUSED/") + item).c_str());
    ctx->Yield(3);
}
} // namespace
void RegisterArrayEditTests(ImGuiTestEngine *engine) {
    auto *test = IM_REGISTER_TEST(engine, "modules", "array_cell_one_undo_cancel_and_long_text");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Clean clean{ctx};
        Fixture(ctx);
        const auto id = Cell(ctx, 0, 0);
        IM_CHECK(id != 0);
        Type(ctx, id, "cancelled");
        IM_CHECK(Value(ctx) == "A" && UndoCount(ctx) == 0);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(2);
        IM_CHECK(Value(ctx) == "A" && UndoCount(ctx) == 0);
        ctx->ItemInputValue(id, "accepted");
        ctx->Yield(3);
        IM_CHECK(Value(ctx) == "accepted" && UndoCount(ctx) == 1);
        Undo(ctx);
        IM_CHECK(Value(ctx) == "A");
        std::string longText(1200, 'x');
        ctx->ItemInputValue(id, longText.c_str());
        ctx->Yield(3);
        IM_CHECK(Value(ctx) == longText && UndoCount(ctx) == 1);
    };
    test = IM_REGISTER_TEST(engine, "modules", "array_multiple_draft_accept_cancel_and_stale_rows");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Clean clean{ctx};
        Fixture(ctx);
        ctx->ItemClick(Cell(ctx, 0, 0, true));
        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->ItemClick(Cell(ctx, 1, 0, true));
        ctx->KeyUp(ImGuiMod_Ctrl);
        Context(ctx, Cell(ctx, 0, 0), "Edit Multiple");
        Type(ctx, ctx->ItemInfo("//$FOCUSED/##value").ID, "both");
        IM_CHECK(Value(ctx) == "A" && Value(ctx, 1) == "B" && UndoCount(ctx) == 0);
        ctx->ItemClick("//$FOCUSED/Cancel");
        ctx->Yield(2);
        IM_CHECK(Value(ctx) == "A" && UndoCount(ctx) == 0);
        IM_CHECK(Read<size_t>(ctx, [] { return editing.table.selected.size(); }) == 2);
        Context(ctx, Cell(ctx, 0, 0), "Edit Multiple");
        ctx->ItemInputValue("//$FOCUSED/##value", "both");
        ctx->Yield(3);
        IM_CHECK(Value(ctx) == "both" && Value(ctx, 1) == "both" && UndoCount(ctx) == 1);
        Undo(ctx);
        Context(ctx, Cell(ctx, 0, 0), "Edit Multiple");
        OnMainThread(ctx, [] { vdev::MoveArrayRow(*App().State().document, editing.array, 0, 1); });
        ctx->Yield(3);
        IM_CHECK(Value(ctx) == "B" && Value(ctx, 1) == "A");
        IM_CHECK(Read<bool>(ctx, [] { return !editing.table.dialog; }));
    };
    test = IM_REGISTER_TEST(engine, "modules", "array_column_drafts_insert_delete_and_cancel");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Clean clean{ctx};
        Fixture(ctx);
        ctx->ItemClick("**/Add Column");
        ctx->Yield(3);
        Type(ctx, ctx->ItemInfo("//$FOCUSED/Name").ID, "cancelled column");
        ctx->ItemClick("//$FOCUSED/Type");
        const auto combo = std::string("//") + ctx->GetWindowByRef("//$FOCUSED")->Name + "/**/Float";
        ctx->ItemClick(combo.c_str());
        ctx->Yield(2);
        IM_CHECK(Read<int>(ctx, [] { return Array()->GetColumnCount(); }) == 2 && UndoCount(ctx) == 0);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(Read<int>(ctx, [] { return Array()->GetColumnCount(); }) == 2 && UndoCount(ctx) == 0);
        ctx->ItemClick("**/Add Column");
        ctx->Yield(2);
        ctx->ItemInputValue("//$FOCUSED/Name", "Added");
        ctx->Yield(3);
        IM_CHECK(Read<int>(ctx, [] { return Array()->GetColumnCount(); }) == 3 && UndoCount(ctx) == 1);
        Undo(ctx);
        ctx->ItemClick("**/0 : Name", ImGuiMouseButton_Right);
        ctx->Yield(2);
        ctx->ItemClick("//$FOCUSED/Insert Column");
        ctx->Yield(3);
        ctx->ItemInputValue("//$FOCUSED/Name", "First");
        ctx->Yield(3);
        IM_CHECK(Read<std::string>(ctx, [] { return std::string(Array()->GetColumnName(0)); }) == "First" &&
                 UndoCount(ctx) == 1);
        Undo(ctx);
        ctx->ItemClick("**/0 : Name", ImGuiMouseButton_Right);
        ctx->Yield(2);
        ctx->ItemClick("//$FOCUSED/Delete Column");
        ctx->Yield(3);
        ctx->ItemClick("//$FOCUSED/No");
        ctx->Yield(2);
        IM_CHECK(UndoCount(ctx) == 0);
        ctx->ItemClick("**/0 : Name", ImGuiMouseButton_Right);
        ctx->Yield(2);
        ctx->ItemClick("//$FOCUSED/Delete Column");
        ctx->Yield(3);
        ctx->ItemClick("//$FOCUSED/Yes");
        ctx->Yield(3);
        IM_CHECK(Read<int>(ctx, [] { return Array()->GetColumnCount(); }) == 1 && UndoCount(ctx) == 1);
        Undo(ctx);
        IM_CHECK(Value(ctx) == "A");
    };
    test = IM_REGISTER_TEST(engine, "modules", "array_row_commands_preserve_parameter_identity");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Clean clean{ctx};
        Fixture(ctx);
        const auto original = Read<vdev::ObjectRef>(ctx, [] {
            auto *p = reinterpret_cast<CKParameter *>(*Array()->GetElement(0, 1));
            return App().State().document->Reference(p->GetID());
        });
        Context(ctx, Cell(ctx, 0, 0), "Duplicate Row");
        IM_CHECK(Read<int>(ctx, [] { return Array()->GetRowCount(); }) == 3 && UndoCount(ctx) == 1);
        Undo(ctx);
        Context(ctx, Cell(ctx, 0, 0), "Move Row Down");
        IM_CHECK(Value(ctx) == "B" && UndoCount(ctx) == 1);
        Undo(ctx);
        Context(ctx, Cell(ctx, 0, 0), "Delete Row");
        IM_CHECK(Value(ctx) == "B" && UndoCount(ctx) == 1);
        Undo(ctx);
        IM_CHECK(Read<vdev::ObjectRef>(ctx, [] {
                     auto *p = reinterpret_cast<CKParameter *>(*Array()->GetElement(0, 1));
                     return App().State().document->Reference(p->GetID());
                 }) == original);
        ctx->ItemClick("**/Add Row");
        ctx->Yield(3);
        IM_CHECK(Read<int>(ctx, [] { return Array()->GetRowCount(); }) == 3 && UndoCount(ctx) == 1);
        Undo(ctx);
        Type(ctx, Cell(ctx, 0, 0), "close me");
        OnMainThread(ctx, [] {
            auto &app = (&App().WorkspaceForTests())->Application();
            app.CloseScope(editing.owner);
            editing.visible = false;
        });
        ctx->Yield(2);
        IM_CHECK(Value(ctx) == "A" && UndoCount(ctx) == 0);
    };
    test = IM_REGISTER_TEST(engine, "modules", "array_scrolled_and_retargeted_cells_cancel");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Clean clean{ctx};
        Fixture(ctx);
        OnMainThread(ctx, [] {
            for (int i = 0; i < 120; ++i)
                Array()->AddRow();
        });
        ctx->Yield(2);
        Type(ctx, Cell(ctx, 0, 0), "off screen");
        IM_CHECK(Value(ctx) == "A" && UndoCount(ctx) == 0);
        OnMainThread(ctx, [] {
            auto *root = ImGui::FindWindowByName(window);
            auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(ImHashStr("array", 0, root->ID));
            ImGui::SetScrollY(table->InnerWindow, table->InnerWindow->ScrollMax.y);
        });
        ctx->Yield(3);
        IM_CHECK(Value(ctx) == "A" && UndoCount(ctx) == 0);
        IM_CHECK(
            Read<bool>(ctx, [] { return !editing.table.cell && !(&App().WorkspaceForTests())->Application().HasEdits(); }));
        OnMainThread(ctx, [] {
            auto *root = ImGui::FindWindowByName(window);
            auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(ImHashStr("array", 0, root->ID));
            ImGui::SetScrollY(table->InnerWindow, 0);
        });
        ctx->Yield(3);
        Type(ctx, Cell(ctx, 0, 0), "old target");
        CK_ID old = Read<CK_ID>(ctx, [] { return editing.array; });
        OnMainThread(ctx, [] {
            auto &state = App().State();
            auto *a = CKDataArray::Cast(state.host.Context()->CreateObject(CKCID_DATAARRAY, "Other table"));
            a->InsertColumn(-1, CKARRAYTYPE_STRING, "Other");
            a->AddRow();
            a->SetElementStringValue(0, 0, "X");
            state.document->Include(a->GetID());
            editing.array = a->GetID();
            editing.arrays.push_back(editing.array);
        });
        ctx->Yield(3);
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(2);
        IM_CHECK(Value(ctx) == "X" && UndoCount(ctx) == 0);
        IM_CHECK(Read<std::string>(ctx, [=] {
                     char value[64]{};
                     Object<CKDataArray>(old)->GetElementStringValue(0, 0, value, 64);
                     return std::string(value);
                 }) == "A");
        ctx->ItemInputValue(Cell(ctx, 0, 0), "new target");
        ctx->Yield(3);
        IM_CHECK(Value(ctx) == "new target" && UndoCount(ctx) == 1);
        Undo(ctx);
        IM_CHECK(Value(ctx) == "X");
    };
    test = IM_REGISTER_TEST(engine, "modules", "array_external_queries_editors_and_modifiers");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Clean clean{ctx};
        Fixture(ctx);
        vdev::EditorConfiguration previous;
        OnMainThread(ctx, [&] {
            auto &workspace = *(&App().WorkspaceForTests());
            previous = workspace.Application().Configuration();
            editing.configuration = std::make_unique<vdev::EditorConfiguration>(previous);
            auto next = previous;
            for (auto &provider : next.providers)
                if (provider.id == "array.cells-value" || provider.id == "array.cells-set" ||
                    provider.id == "array.column-value" || provider.id == "array.column-set" ||
                    provider.id == "value.string" || provider.id == "value.array-column")
                    provider.provider = "example.arrays";
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(next, error));
        });
        ctx->Yield(3);
        const auto before = Read<std::vector<uint64_t>>(ctx, [] {
            auto &app = (&App().WorkspaceForTests())->Application();
            const auto q = app.Query("example.array-count", {app.Scope(editing.owner), nullptr, 0});
            return vdev::EditorClient::Find(q, "writes")->integers;
        });
        Type(ctx, ctx->ItemInfo("**/External cell").ID, "external value");
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(!(&App().WorkspaceForTests())->Configure(previous, error));
        });
        ctx->Yield(2);
        // Reconfiguration respects the existing open-draft check; the selected
        // query, editor and modifier finish the same interaction.
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        IM_CHECK(Value(ctx) == "external value" && UndoCount(ctx) == 1);
        Undo(ctx);
        OnMainThread(ctx, [&] {
            auto next = previous;
            for (auto &provider : next.providers)
                if (provider.id == "array.column-value" || provider.id == "array.column-set" ||
                    provider.id == "value.array-column")
                    provider.provider = "example.arrays";
            std::string error;
            IM_CHECK_NO_RET((&App().WorkspaceForTests())->Configure(next, error));
        });
        ctx->Yield(2);
        ctx->ItemClick("**/0 : Name", ImGuiMouseButton_Right);
        ctx->Yield(2);
        ctx->ItemClick("//$FOCUSED/Edit Column Properties");
        ctx->Yield(3);
        Type(ctx, ctx->ItemInfo("//$FOCUSED/External column").ID, "External name");
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(!(&App().WorkspaceForTests())->Configure(previous, error));
        });
        ctx->Yield(2);
        ctx->ItemClick("//$FOCUSED/OK");
        ctx->Yield(3);
        IM_CHECK(Read<std::string>(ctx, [] { return std::string(Array()->GetColumnName(0)); }) == "External name" &&
                 UndoCount(ctx) == 1);
        const auto after = Read<std::vector<uint64_t>>(ctx, [] {
            auto &app = (&App().WorkspaceForTests())->Application();
            const auto q = app.Query("example.array-count", {app.Scope(editing.owner), nullptr, 0});
            return vdev::EditorClient::Find(q, "writes")->integers;
        });
        IM_CHECK(after.size() == 2 && before.size() == 2 && after[0] == before[0] + 1 && after[1] == before[1] + 1);
        Undo(ctx);
        IM_CHECK(Value(ctx) == "A");
    };
}
} // namespace vdevuitest
