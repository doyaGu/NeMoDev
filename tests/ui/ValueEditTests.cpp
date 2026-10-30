#include "tests/ui/UiTests.h"
#include "core/graph/ParameterValues.h"
#include "ui/controls/ParameterBox.h"
#include "workspace/EditorClient.h"
#include <memory>

namespace vdevuitest {
namespace {
struct Editing {
    std::unique_ptr<vdev::ParameterBox> box;
    std::vector<CK_ID> parameters;
    std::unique_ptr<vdev::EditorConfiguration> configuration;
    CKGUID enumType{0, 0};
    bool open = false, popup = false, inlineValue = false;
    vdev::ObjectRef target{};
    EditorView owner = 0;
    EditorEdit edit = 0;
} editing;
const char *dialog = "Value editing###PrimitiveValues";
void Gui(ImGuiTestContext *ctx) {
    MainThreadGui(ctx);
    if (editing.open && editing.box) {
        if (!editing.popup) { ImGui::OpenPopup(dialog); editing.popup = true; }
        const auto action = vdev::EditParametersDialog(dialog, "Value editing", *editing.box, true);
        if (action == vdev::DialogAction::Apply || action == vdev::DialogAction::Ok) editing.box->Apply(0);
        if (action == vdev::DialogAction::Ok || action == vdev::DialogAction::Cancel) {
            editing.box->Clear(); editing.open = false;
        }
    }
    if (editing.inlineValue) {
        ImGui::Begin("Typed inline");
        auto &app = App().State().workspace->Application();
        app.EnterDraw();
        app.DrawProperty("parameter.integer", app.Scope(editing.owner), editing.target, editing.edit, vdev::EditorLiteral("##integer"));
        app.LeaveDraw();
        ImGui::End();
    }
}
void Cleanup(ImGuiTestContext *ctx) {
    OnMainThread(ctx, [] {
        auto &state = App().State();
        auto &app = state.workspace->Application();
        if (editing.edit) app.FinishEdit(editing.edit, false);
        if (editing.owner) app.CloseScope(editing.owner);
        editing.box.reset();
        if (editing.configuration) { std::string error; state.workspace->Configure(*editing.configuration, error); }
        state.History().Clear();
        for (auto id : editing.parameters) if (state.host.Context()->GetObject(id)) state.host.Context()->DestroyObject(id);
        if (editing.enumType.d1 || editing.enumType.d2) state.host.Context()->GetParameterManager()->UnRegisterParameterType(editing.enumType);
        editing = Editing{};
    });
}
struct Clean { ImGuiTestContext *ctx; ~Clean() { Cleanup(ctx); } };
int IntegerValue(ImGuiTestContext *ctx, CK_ID id) {
    return Read<int>(ctx, [=] { int value = -99; if (auto *p = Object<CKParameter>(id)) p->GetValue(&value, FALSE); return value; });
}
void Input(ImGuiTestContext *ctx, const char *path, const char *text) {
    ctx->ItemClick(path);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
    ctx->KeyChars(text);
    ctx->Yield(2);
}
} // namespace
void RegisterValueEditTests(ImGuiTestEngine *engine) {
    auto *test = IM_REGISTER_TEST(engine, "modules", "primitive_parameter_dialog_one_undo_and_cancel");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        Clean cleanup{ctx};
        OnMainThread(ctx, [] {
            auto &state = App().State();
            auto *context = state.host.Context();
            editing.enumType = CKGUID(0x6e76616c, 0x00000402);
            IM_CHECK_NO_RET(context->GetParameterManager()->RegisterNewEnum(editing.enumType, "Primitive UI enum", "First=2,Last=90") == CK_OK);
            const CKGUID types[] = {CKPGUID_INT, CKPGUID_FLOAT, CKPGUID_BOOL, editing.enumType};
            editing.box = std::make_unique<vdev::ParameterBox>(state);
            for (auto type : types) {
                auto *parameter = context->CreateCKParameterLocal("Primitive edit", type);
                IM_CHECK_NO_RET(parameter != nullptr);
                if (type == editing.enumType) { int value = 2; parameter->SetValue(&value, sizeof(value)); }
                editing.parameters.push_back(parameter->GetID());
                IM_CHECK_NO_RET(editing.box->Add(parameter, "Value"));
            }
            state.History().Clear();
            state.document->MarkSaved(); state.History().MarkSavePoint();
            editing.open = true;
        });
        ctx->Yield(3); ctx->SetRef("//Value editing###PrimitiveValues");
        IM_CHECK(ctx->ItemExists("**/$$0/##number"));
        Input(ctx, "**/$$0/##number", "12");
        Input(ctx, "**/$$1/##number", "2.5");
        ctx->ItemClick("**/$$2/##bool");
        ctx->ItemClick("**/$$3/##enum/##combo");
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("**/Last");
        ctx->SetRef("//Value editing###PrimitiveValues");
        IM_CHECK(IntegerValue(ctx, editing.parameters[0]) == 0);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 0);
        ctx->ItemClick("**/Apply/##dialogbutton"); ctx->Yield(3);
        IM_CHECK(IntegerValue(ctx, editing.parameters[0]) == 12);
        IM_CHECK(IntegerValue(ctx, editing.parameters[2]) == 1);
        IM_CHECK(IntegerValue(ctx, editing.parameters[3]) == 90);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        ctx->ItemClick("**/Apply/##dialogbutton"); ctx->Yield(2);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        Input(ctx, "**/$$0/##number", "2147483648");
        IM_CHECK(Read<bool>(ctx, [] { return !editing.box->Valid(); }));
        ctx->KeyPress(ImGuiKey_Enter); ctx->Yield(2);
        IM_CHECK(IntegerValue(ctx, editing.parameters[0]) == 12);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        Input(ctx, "**/$$0/##number", "123");
        ctx->ItemClick("**/Cancel/##dialogbutton"); ctx->Yield(2);
        IM_CHECK(IntegerValue(ctx, editing.parameters[0]) == 12);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK(IntegerValue(ctx, editing.parameters[0]) == 0 && IntegerValue(ctx, editing.parameters[2]) == 0 && IntegerValue(ctx, editing.parameters[3]) == 2);
        IM_CHECK(Read<bool>(ctx, [] { return !App().State().document->Dirty(); }));
    };
    test = IM_REGISTER_TEST(engine, "modules", "parameter_batch_failure_preserves_history_and_values");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Clean cleanup{ctx};
        OnMainThread(ctx, [] {
            auto &state = App().State();
            auto *context = state.host.Context();
            auto *a = context->CreateCKParameterLocal("Batch A", CKPGUID_INT);
            auto *b = context->CreateCKParameterLocal("Batch B", CKPGUID_INT);
            IM_CHECK_NO_RET(a && b);
            editing.parameters = {a->GetID()};
            editing.box = std::make_unique<vdev::ParameterBox>(state);
            IM_CHECK_NO_RET(editing.box->Add(a, "A") && editing.box->Add(b, "B"));
            state.History().Clear(); state.document->MarkSaved(); state.History().MarkSavePoint();
            vdev::ParameterValueDescription description;
            IM_CHECK_NO_RET(vdev::DescribeParameterValue(*state.document, a, description));
            description.value.integer = 4;
            IM_CHECK_NO_RET(vdev::SetParameterScalar(*state.document, state.document->Reference(a->GetID()), description.type, description.value).ok);
            IM_CHECK_NO_RET(state.History().Undo() && state.History().CanRedo());
            description.value.integer = 33;
            IM_CHECK_NO_RET(vdev::WriteParameterDraft(*state.document, editing.box->Work(0), description.value));
            const auto selected = state.workspace->Application().Selection();
            context->DestroyObject(b);
            IM_CHECK_NO_RET(editing.box->Apply(0) == 0);
            int value = -1; a->GetValue(&value, FALSE);
            IM_CHECK_NO_RET(value == 0 && state.History().UndoCount() == 0 && state.History().CanRedo() && !state.document->Dirty());
            IM_CHECK_NO_RET(state.workspace->Application().Selection() == selected);
        });
    };
    test = IM_REGISTER_TEST(engine, "modules", "external_integer_editor_shared_by_inline_and_dialog");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Clean cleanup{ctx};
        vdev::EditorConfiguration previous;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            auto &app = state.workspace->Application();
            previous = app.Configuration();
            editing.configuration = std::make_unique<vdev::EditorConfiguration>(previous);
            auto selected = previous;
            for (auto &choice : selected.providers)
                if (choice.kind == "command" && choice.id == "parameter.integer-set") choice.provider = "example.integer";
            std::string error;
            IM_CHECK_NO_RET(state.workspace->Configure(selected, error));
            auto *parameter = state.host.Context()->CreateCKParameterLocal("External integer", CKPGUID_INT);
            IM_CHECK_NO_RET(parameter != nullptr);
            editing.parameters = {parameter->GetID()}; editing.target = state.document->Reference(parameter->GetID());
            editing.owner = app.OpenScope();
            state.History().Clear();
            editing.inlineValue = true;
        });
        ctx->Yield(3);
        ctx->MenuClick("//##MainMenuBar/Views/Value Editors/External integer");
        ctx->Yield(2); ctx->SetRef("//Typed inline");
        ctx->ItemClick("Increment"); ctx->Yield(3);
        IM_CHECK(IntegerValue(ctx, editing.parameters[0]) == 7);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        OnMainThread(ctx, [] {
            editing.inlineValue = false;
            auto &state = App().State();
            auto &app = state.workspace->Application();
            const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, editing.target};
            IM_CHECK_NO_RET(app.Execute("parameter.edit", {app.Scope(), &target, 1}).status == EDITOR_OK);
        });
        ctx->Yield(3); ctx->SetRef("//Edit Parameter###AttributeManagerValue");
        IM_CHECK(ctx->ItemExists("**/Increment"));
        OnMainThread(ctx, [&] {
            auto changed = previous;
            std::string error;
            IM_CHECK_NO_RET(App().State().workspace->Configure(changed, error));
        });
        // An open dialog keeps the editor and modifier it started with.
        ctx->Yield(2);
        IM_CHECK(ctx->ItemExists("**/Increment"));
        ctx->ItemClick("**/Increment");
        IM_CHECK(IntegerValue(ctx, editing.parameters[0]) == 7);
        ctx->ItemClick("**/OK/##dialogbutton"); ctx->Yield(3);
        IM_CHECK(IntegerValue(ctx, editing.parameters[0]) == 14);
        IM_CHECK(Read<uint64_t>(ctx, [] {
            auto &app = App().State().workspace->Application();
            const auto result = app.Query("example.integer-count", {app.Scope(), nullptr, 0});
            return result.status == EDITOR_OK ? result.fields.front().integer : 0;
        }) == 2);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 2);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(App().State().History().Undo());
            std::string error;
            IM_CHECK_NO_RET(App().State().workspace->Configure(previous, error));
        });
        IM_CHECK(IntegerValue(ctx, editing.parameters[0]) == 7);
    };
    test = IM_REGISTER_TEST(engine, "modules", "object_parameter_choices_apply_cancel_and_undo");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Clean cleanup{ctx};
        OnMainThread(ctx, [] {
            auto &state = App().State();
            auto *parameter = state.host.Context()->CreateCKParameterLocal("Object value", CKPGUID_3DENTITY);
            auto *object = state.host.Context()->CreateObject(CKCID_3DENTITY, "Value chooser target");
            IM_CHECK_NO_RET(parameter && object);
            editing.parameters = {parameter->GetID(), object->GetID()};
            editing.target = state.document->Reference(parameter->GetID());
            state.History().Clear();
            auto &app = state.workspace->Application();
            const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, editing.target};
            IM_CHECK_NO_RET(app.Execute("parameter.edit", {app.Scope(), &target, 1}).status == EDITOR_OK);
        });
        ctx->Yield(3); ctx->SetRef("//Edit Parameter###AttributeManagerValue");
        ctx->ItemClick("**/##object/##combo");
        ctx->SetRef("//$FOCUSED"); ctx->ItemClick("**/Value chooser target");
        ctx->SetRef("//Edit Parameter###AttributeManagerValue");
        IM_CHECK(IntegerValue(ctx, editing.parameters[0]) == 0);
        ctx->ItemClick("**/Cancel/##dialogbutton"); ctx->Yield(3);
        IM_CHECK(IntegerValue(ctx, editing.parameters[0]) == 0);
        OnMainThread(ctx, [] {
            auto &app = App().State().workspace->Application();
            const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, editing.target};
            IM_CHECK_NO_RET(app.Execute("parameter.edit", {app.Scope(), &target, 1}).status == EDITOR_OK);
        });
        ctx->Yield(3); ctx->SetRef("//Edit Parameter###AttributeManagerValue");
        ctx->ItemClick("**/##object/##combo");
        ctx->SetRef("//$FOCUSED"); ctx->ItemClick("**/Value chooser target");
        ctx->SetRef("//Edit Parameter###AttributeManagerValue");
        ctx->ItemClick("**/OK/##dialogbutton"); ctx->Yield(3);
        IM_CHECK(IntegerValue(ctx, editing.parameters[0]) == int(editing.parameters[1]));
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK(IntegerValue(ctx, editing.parameters[0]) == 0);
    };

}
} // namespace vdevuitest
