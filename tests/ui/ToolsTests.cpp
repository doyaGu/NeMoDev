// "tools": the Editors and Options menus' windows on the sample opened from
// the command line: Path Manager, Parameter Debugger, Installed Plugins and
// Building Block Version Test shown; last, Unused Object Explorer listing a
// material made in the 3D Layout (7) and its Delete destroying it (the undo
// steps cleared, as the Level Manager's Delete).

#include <set>

#include "tests/ui/UiTests.h"
#include "workspace/EditorClient.h"
#include "core/graph/TopologyCommands.h"
#include "ui/controls/ValueEditor.h"

namespace vdevuitest {

namespace {

bool WindowShown(ImGuiTestContext *ctx, const char *ref) {
    ImGuiWindow *window = ctx->GetWindowByRef(ref);
    return window && window->WasActive;
}

// The menu item clicked (a path of the main menu bar), its window shown.
bool OpenFromMenu(ImGuiTestContext *ctx, const char *menu, const char *window) {
    Settle(ctx);
    ctx->MenuClick((std::string("//##MainMenuBar/") + menu).c_str());
    ctx->Yield(3);
    const bool shown = WindowShown(ctx, window);
    ctx->LogInfo("%s: %s", window, shown ? "shown" : "not shown");
    return shown;
}

std::set<CK_ID> Materials(ImGuiTestContext *ctx) {
    return Read<std::set<CK_ID>>(ctx, [] {
        std::set<CK_ID> ids;
        const XObjectPointerArray &list = App().State().host.Context()->GetObjectListByType(CKCID_MATERIAL, TRUE);
        for (int i = 0; i < list.Size(); ++i)
            if (list[i] && !App().State().document->IsDiscarded(list[i]->GetID())) ids.insert(list[i]->GetID());
        return ids;
    });
}

} // namespace

void RegisterToolsTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    // Editors > Path Manager: its dialog (Escape closes it).
    t = IM_REGISTER_TEST(engine, "tools", "path_manager");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->MenuClick("//##MainMenuBar/Editors/Path Manager"); ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, EnvironmentRef("paths").c_str()));
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
    };

    // Editors > Parameter Debugger: its window; the menu item again hides it.
    t = IM_REGISTER_TEST(engine, "tools", "parameter_debugger");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->MenuClick("//##MainMenuBar/Editors/Parameter Debugger");
        ctx->Yield(3);
        const auto window = RuntimeRef("parameters");
        IM_CHECK(WindowShown(ctx, window.c_str()));
        IM_CHECK(!OpenFromMenu(ctx, "Editors/Parameter Debugger", window.c_str()));
    };

    t = IM_REGISTER_TEST(engine, "tools", "parameter_watches_instances_edit_and_lifetime");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State(); auto &workspace = *state.workspace; auto &editor = workspace.Application();
        vdev::ObjectRef local{}, script{}, output{}, input{};
        EditorView first = 0, second = 0;
        OnMainThread(ctx, [&] {
            editor.Execute("runtime.close-tools", {editor.Scope(), nullptr, 0});
            auto *owner = state.document->Context()->CreateObject(CKCID_BEOBJECT, "Watch UI owner");
            IM_CHECK_NO_RET(owner != nullptr);
            state.document->AdoptObjects({owner->GetID()});
            const auto made = vdev::CreateScript(*state.document, owner->GetID(), "Watch UI Script");
            IM_CHECK_NO_RET(made.ok);
            script = state.document->Reference(made.created);
            const auto parameter = vdev::AddLocalParameter(*state.document, made.created, made.created, "Watch UI text", CKPGUID_STRING, Point(80, 80));
            IM_CHECK_NO_RET(parameter.ok);
            local = state.document->Reference(parameter.created);
            auto *behavior = CKBehavior::Cast(state.document->Resolve(script));
            output = state.document->Reference(behavior->CreateOutputParameter("Watch UI output", CKPGUID_INT)->GetID());
            input = state.document->Reference(behavior->CreateInputParameter("Watch UI input", CKPGUID_INT)->GetID());
            auto *value = CKParameter::Cast(state.document->Resolve(local));
            value->SetStringValue("before");
            state.clipboard.SetParameters(*state.document, {local.object, output.object});
            const std::vector<vdev::ObjectRef> objects{local, local, output, input};
            const auto args = vdev::ObjectsArgument("objects", objects);
            IM_CHECK_NO_RET(editor.Execute("runtime.watch-parameters", {editor.Scope(), &args, 1}).status == EDITOR_OK);
            first = workspace.Role("main.parameters");
            second = workspace.Open("runtime.parameters");
            IM_CHECK_NO_RET(first && second && first != second);
            const std::vector<vdev::ObjectRef> other{output};
            const EditorValue more[] = {vdev::ObjectsArgument("objects", other), vdev::IntegerArgument("view", second)};
            IM_CHECK_NO_RET(editor.Execute("runtime.watch-parameters", {editor.Scope(), more, 2}).status == EDITOR_OK);
            const vdev::EditorClient client(editor.Host(), first);
            IM_CHECK_NO_RET(vdev::EditorClient::Find(client.Inspect(first), "objects")->objects.size() == 2 &&
                            vdev::EditorClient::Find(client.Inspect(second), "objects")->objects == other);
            state.History().Clear(); state.document->MarkSaved(); state.History().MarkSavePoint();
            workspace.Focus(first);
        }); ctx->Yield(5);
        const auto window = "//" + workspace.WindowName(first);
        const auto widget = window + "/**/##value";
        ctx->WindowFocus(window.c_str());
        ctx->ItemInputValue(widget.c_str(), "accepted"); ctx->Yield(3);
        OnMainThread(ctx, [&] {
            auto *value = CKParameter::Cast(state.document->Resolve(local));
            IM_CHECK_NO_RET(vdev::ParameterValueText(value) == "accepted" && state.History().UndoCount() == 1);
            IM_CHECK_NO_RET(state.History().Undo() && vdev::ParameterValueText(value) == "before");
        }); ctx->Yield(3);
        ctx->ItemClick(widget.c_str()); ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A); ctx->KeyChars("cancelled");
        ctx->KeyPress(ImGuiKey_Escape); ctx->Yield(3);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(vdev::ParameterValueText(CKParameter::Cast(state.document->Resolve(local))) == "before" &&
                            !state.History().UndoCount() && state.History().CanRedo() && !state.document->Dirty());
            const auto target = vdev::ObjectArgument("target", local);
            IM_CHECK_NO_RET(editor.Execute("script.show-parameter", {editor.Scope(first), &target, 1}).status == EDITOR_OK);
        }); ctx->Yield(3);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(App().Canvas().Navigation().selection.script == script.object &&
                !App().Canvas().Navigation().selection.items.empty() && App().Canvas().Navigation().selection.items.front().object == local.object);
            const auto view = vdev::IntegerArgument("view", second);
            IM_CHECK_NO_RET(editor.Execute("runtime.clear-watches", {editor.Scope(first), &view, 1}).status == EDITOR_OK);
            const vdev::EditorClient client(editor.Host(), first);
            IM_CHECK_NO_RET(vdev::EditorClient::Find(client.Inspect(first), "objects")->objects.size() == 2 &&
                            vdev::EditorClient::Find(client.Inspect(second), "objects")->objects.empty());
            workspace.Focus(second);
        }); ctx->Yield(3);
        const auto secondWindow = "//" + workspace.WindowName(second);
        ctx->WindowFocus(secondWindow.c_str()); ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_V); ctx->Yield(3);
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), second);
            const auto pasted = client.Inspect(second);
            const std::vector<vdev::ObjectRef> expected{local, output};
            IM_CHECK_NO_RET(pasted.status == EDITOR_OK && vdev::EditorClient::Find(pasted, "objects")->objects == expected);
            const EditorValue remove[] = {vdev::ObjectArgument("remove", local), vdev::IntegerArgument("view", second)};
            IM_CHECK_NO_RET(editor.Execute("runtime.unwatch-parameter", {editor.Scope(second), remove, 2}).status == EDITOR_OK);
            IM_CHECK_NO_RET(vdev::EditorClient::Find(client.Inspect(first), "objects")->objects == expected &&
                            vdev::EditorClient::Find(client.Inspect(second), "objects")->objects == std::vector<vdev::ObjectRef>{output});
            const auto view = vdev::IntegerArgument("view", second);
            IM_CHECK_NO_RET(editor.Execute("runtime.clear-watches", {editor.Scope(second), &view, 1}).status == EDITOR_OK);
            state.clipboard = {};
            state.History().Clear();
            auto *behavior = CKBehavior::Cast(state.document->Resolve(script));
            const auto oldId = local.object;
            for (int i = 0; i < behavior->GetLocalParameterCount(); ++i)
                if (behavior->GetLocalParameter(i)->GetID() == oldId) { behavior->RemoveLocalParameter(i); break; }
            state.document->Context()->DestroyObject(oldId, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID);
            CKParameterLocal *replacement = nullptr;
            for (int i = 0; i < 256 && !replacement; ++i) {
                auto *value = state.document->Context()->CreateCKParameterLocal("Watch replacement", CKPGUID_STRING);
                if (value->GetID() == oldId) replacement = value;
            }
            IM_CHECK_NO_RET(replacement != nullptr && !state.document->Resolve(local));
        }); ctx->Yield(3);
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), first);
            const auto watched = client.Inspect(first);
            IM_CHECK_NO_RET(vdev::EditorClient::Find(watched, "objects")->objects == std::vector<vdev::ObjectRef>{output});
            const auto stale = vdev::ObjectArgument("target", local);
            IM_CHECK_NO_RET(editor.Execute("script.show-parameter", {editor.Scope(first), &stale, 1}).status == EDITOR_STALE);
            state.document->MarkSaved();
            IM_CHECK_NO_RET(editor.Execute("document.new", {editor.Scope(), nullptr, 0}).status == EDITOR_QUEUED);
            state.FinishRuntimeOperations();
        }); ctx->Yield(6);
        OnMainThread(ctx, [&] {
            const vdev::EditorClient client(editor.Host(), first);
            IM_CHECK_NO_RET(state.document->Token() != local.document &&
                vdev::EditorClient::Find(client.Inspect(first), "objects")->objects.empty() &&
                vdev::EditorClient::Find(client.Inspect(second), "objects")->objects.empty());
            editor.Execute("runtime.close-tools", {editor.Scope(), nullptr, 0});
            IM_CHECK_NO_RET(!workspace.Role("main.parameters") && workspace.WindowName(second).empty());
        });
    };

    // Options > Installed Plugins: its window.
    t = IM_REGISTER_TEST(engine, "tools", "installed_plugins");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->MenuClick("//##MainMenuBar/Options/Installed Plugins"); ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, EnvironmentRef("plugins").c_str()));
        ctx->WindowClose(EnvironmentRef("plugins").c_str());
        ctx->Yield(2);
    };

    // Options > Building Block Version Test: its report; No (or Escape) closes it.
    t = IM_REGISTER_TEST(engine, "tools", "version_test");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); ctx->MenuClick("//##MainMenuBar/Options/Building Block Version Test"); ctx->Yield(3);
        const auto window = DiagnosticRef("versions");
        IM_CHECK(WindowShown(ctx, window.c_str()));
        ctx->WindowFocus(window.c_str());
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(!WindowShown(ctx, window.c_str()));
    };

    // Ctrl+Alt+F12: All CK Object In Memory; Enter (OK) closes it.
    t = IM_REGISTER_TEST(engine, "tools", "all_objects");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_F12);
        ctx->Yield(3);
        const auto window = DiagnosticRef("memory");
        IM_CHECK(WindowShown(ctx, window.c_str()));
        ctx->WindowFocus(window.c_str());
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        IM_CHECK(!WindowShown(ctx, window.c_str()));
    };

    // Last (no undo): a material made in the 3D Layout (7) is unused;
    // Options > Unused Object Explorer, Delete: the material destroyed, the
    // undo steps cleared.
    t = IM_REGISTER_TEST(engine, "tools", "unused_objects_delete");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        ImVec2 min, max;
        IM_CHECK_SILENT(App().Layout().ShownView(min, max));
        ctx->MouseMoveToPos(Center(min, max));
        const std::set<CK_ID> before = Materials(ctx);
        ctx->KeyPress(ImGuiKey_7);
        ctx->Yield(3);
        CK_ID material = 0;
        for (CK_ID id : Materials(ctx))
            if (!before.count(id)) material = id;
        IM_CHECK_SILENT(material != 0);
        ctx->LogInfo("material '%s'", ObjectName(ctx, material).c_str());
        Settle(ctx); ctx->MenuClick("//##MainMenuBar/Options/Unused Object Explorer"); ctx->Yield(3);
        const auto window = DiagnosticRef("unused");
        IM_CHECK_SILENT(WindowShown(ctx, window.c_str()));
        bool ready = false;
        for (int i = 0; i < 100 && !ready; ++i) {
            ready = Read<bool>(ctx, [] {
                auto &workspace = *App().State().workspace;
                const auto snapshot = vdev::EditorClient(workspace.Application().Host(), 0).Inspect(workspace.Role("main.unused"));
                return snapshot.status == EDITOR_OK && vdev::EditorClient::Integer(snapshot, "revision") != 0;
            }); ctx->Yield();
        }
        IM_CHECK(ready);
        ctx->ItemClick((window + "/**/Delete").c_str());
        ctx->Yield(3);
        IM_CHECK(!ObjectExists(ctx, material));
        IM_CHECK(!App().State().History().CanUndo());
    };
}

} // namespace vdevuitest
