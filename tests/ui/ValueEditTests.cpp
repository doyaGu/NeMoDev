#include "tests/ui/UiTests.h"
#include "core/graph/ParameterValues.h"
#include "ui/controls/ParameterBox.h"
#include "workspace/EditorClient.h"
#include <memory>
#include <cmath>

namespace vdevuitest {
namespace {
struct Editing {
    std::unique_ptr<vdev::ParameterBox> box;
    std::vector<CK_ID> parameters;
    std::unique_ptr<vdev::EditorConfiguration> configuration;
    CKGUID enumType{0, 0};
    std::vector<CKGUID> structures;
    bool open = false, popup = false, inlineValue = false;
    const char *property = "parameter.integer";
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
        ImGui::SetNextWindowSize(ImVec2(420, 180), ImGuiCond_Always);
        ImGui::Begin("Typed inline");
        auto &app = App().State().workspace->Application();
        app.EnterDraw();
        app.DrawProperty(editing.property, app.Scope(editing.owner), editing.target, editing.edit, vdev::EditorLiteral("##integer"));
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
        if (!ImGui::GetCurrentContext()->OpenPopupStack.empty()) ImGui::ClosePopupToLevel(0, false);
        if (editing.configuration) { std::string error; state.workspace->Configure(*editing.configuration, error); }
        state.History().Clear();
        for (auto id : editing.parameters) if (state.host.Context()->GetObject(id)) state.host.Context()->DestroyObject(id);
        for (auto type = editing.structures.rbegin(); type != editing.structures.rend(); ++type) state.host.Context()->GetParameterManager()->UnRegisterParameterType(*type);
        if (editing.enumType.d1 || editing.enumType.d2) state.host.Context()->GetParameterManager()->UnRegisterParameterType(editing.enumType);
        editing = Editing{};
    });
}
struct Clean { ImGuiTestContext *ctx; ~Clean() { Cleanup(ctx); } };
int IntegerValue(ImGuiTestContext *ctx, CK_ID id) {
    return Read<int>(ctx, [=] { int value = -99; if (auto *p = Object<CKParameter>(id)) p->GetValue(&value, FALSE); return value; });
}
std::vector<float> Components(ImGuiTestContext *ctx, CK_ID id) {
    return Read<std::vector<float>>(ctx, [=] {
        auto *parameter = Object<CKParameter>(id);
        std::vector<float> values(parameter ? size_t(parameter->GetDataSize()) / sizeof(float) : 0);
        if (parameter) parameter->GetValue(values.data(), FALSE);
        return values;
    });
}
std::vector<float> WorkingComponents(ImGuiTestContext *ctx, size_t row) {
    return Read<std::vector<float>>(ctx, [=] {
        auto *parameter = editing.box ? editing.box->Work(row) : nullptr;
        std::vector<float> values(parameter ? size_t(parameter->GetDataSize()) / sizeof(float) : 0);
        if (parameter) parameter->GetValue(values.data(), FALSE);
        return values;
    });
}
vdev::ParameterValue Value(ImGuiTestContext *ctx, CK_ID id) {
    return Read<vdev::ParameterValue>(ctx, [=] {
        vdev::ParameterValueDescription value;
        IM_CHECK_NO_RET(vdev::DescribeParameterValue(*App().State().document, Object<CKParameter>(id), value));
        return value.value;
    });
}
vdev::ParameterValue WorkingValue(ImGuiTestContext *ctx) {
    return Read<vdev::ParameterValue>(ctx, [] {
        vdev::ParameterValueDescription value;
        IM_CHECK_NO_RET(editing.box && vdev::DescribeParameterValue(*App().State().document, editing.box->Work(0), value));
        return value.value;
    });
}
void ArrayItem(ImGuiTestContext *ctx) {
    const auto at = Read<ImVec2>(ctx, [] {
        for (auto *window : ImGui::GetCurrentContext()->Windows)
            if (window->WasActive && window->ParentWindow && std::string(window->Name).find("##items") != std::string::npos &&
                std::string(window->ParentWindow->Name).find("PrimitiveValues") != std::string::npos)
                return ImVec2(window->DC.CursorStartPos.x + 12, window->DC.CursorStartPos.y + 7);
        return ImVec2(-1, -1);
    });
    IM_CHECK_NO_RET(at.x >= 0 && at.y >= 0);
    ctx->MouseMoveToPos(at); ctx->MouseClick(0);
}
void NestedFixture(bool dialog = true) {
    auto &state = App().State(); auto *context = state.host.Context(); auto *manager = context->GetParameterManager();
    const CKGUID innerType(0x6e76616c, 0x00000430), outerType(0x6e76616c, 0x00000431);
    XArray<CKGUID> inner; for (auto type : {CKPGUID_INT, CKPGUID_COLOR, CKPGUID_OBJECTARRAY, CKPGUID_STRING}) inner.PushBack(type);
    IM_CHECK_NO_RET(manager->RegisterNewStructure(innerType, "UI nested inner", "Count,Color,Objects,Text", inner) == CK_OK);
    editing.structures.push_back(innerType);
    XArray<CKGUID> outer; outer.PushBack(innerType); outer.PushBack(CKPGUID_VECTOR);
    IM_CHECK_NO_RET(manager->RegisterNewStructure(outerType, "UI nested outer", "Inner,Position", outer) == CK_OK);
    editing.structures.push_back(outerType);
    auto *parameter = context->CreateCKParameterLocal("Nested editor", outerType);
    IM_CHECK_NO_RET(parameter != nullptr);
    editing.parameters = {parameter->GetID()}; editing.target = state.document->Reference(parameter->GetID());
    if (dialog) { editing.box = std::make_unique<vdev::ParameterBox>(state); IM_CHECK_NO_RET(editing.box->Add(parameter, "Nested")); editing.open = true; }
    else { editing.property = "parameter.structure"; editing.owner = state.workspace->Application().OpenScope(); editing.inlineValue = true; }
    state.History().Clear(); state.document->MarkSaved(); state.History().MarkSavePoint();
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
            IM_CHECK_NO_RET(vdev::SetParameterValue(*state.document, state.document->Reference(a->GetID()), description.type, description.value).ok);
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

    test = IM_REGISTER_TEST(engine, "modules", "compound_parameter_dialog_one_undo_and_cancel");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Clean cleanup{ctx};
        OnMainThread(ctx, [] {
            auto &state = App().State();
            editing.box = std::make_unique<vdev::ParameterBox>(state);
            const CKGUID types[] = {CKPGUID_VECTOR, CKPGUID_MATRIX, CKPGUID_COLOR, CKPGUID_RECT, CKPGUID_ANGLE, CKPGUID_EULERANGLES, CKPGUID_QUATERNION, CKPGUID_BOX};
            for (unsigned i = 0; i < 8; ++i) {
                auto *parameter = state.host.Context()->CreateCKParameterLocal("Compound edit", types[i]);
                IM_CHECK_NO_RET(parameter != nullptr);
                if (i == 1) { VxMatrix matrix; matrix.SetIdentity(); parameter->SetValue(&matrix, sizeof(matrix)); }
                if (i == 2) { const VxColor color(.1f, .2f, .3f, 1.0f); parameter->SetValue(&color, sizeof(color)); }
                if (i == 3) { const VxRect rect(1, 2, 3, 4); parameter->SetValue(&rect, sizeof(rect)); }
                editing.parameters.push_back(parameter->GetID());
                IM_CHECK_NO_RET(editing.box->Add(parameter, "Compound"));
            }
            state.History().Clear(); state.document->MarkSaved(); state.History().MarkSavePoint(); editing.open = true;
        });
        ctx->Yield(3); ctx->SetRef("//Value editing###PrimitiveValues");
        const auto vector = Components(ctx, editing.parameters[0]);
        const auto matrix = Components(ctx, editing.parameters[1]);
        const auto color = Components(ctx, editing.parameters[2]);
        const auto rect = Components(ctx, editing.parameters[3]);
        std::vector<std::vector<float>> additional;
        for (size_t i = 4; i < 8; ++i) additional.push_back(Components(ctx, editing.parameters[i]));
        Input(ctx, "**/$$0/##components/##x", "12");
        Input(ctx, "**/$$0/##components/##y", "14");
        Input(ctx, "**/$$1/##components/$$0/##m", "3.5");
        Input(ctx, "**/$$2/##components/##r", "128");
        Input(ctx, "**/$$3/##components/$$0/##r", "5");
        Input(ctx, "**/$$4/##components/$$0/##angle/##turn", "1");
        Input(ctx, "**/$$4/##components/$$0/##angle/##degree", "90");
        Input(ctx, "**/$$5/##components/$$1/##euler/##degree", "90");
        Input(ctx, "**/$$6/##components/##w", ".5");
        Input(ctx, "**/$$7/##components/$$5/##b", "5");
        IM_CHECK(Components(ctx, editing.parameters[0]) == vector && Components(ctx, editing.parameters[1]) == matrix);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 0);
        ctx->ItemClick("**/Apply/##dialogbutton"); ctx->Yield(3);
        IM_CHECK(Components(ctx, editing.parameters[0])[0] == 12 && Components(ctx, editing.parameters[0])[1] == 14);
        IM_CHECK(Components(ctx, editing.parameters[1])[0] == 3.5f);
        IM_CHECK(std::abs(Components(ctx, editing.parameters[2])[0] - 128.0f / 255) < .00001f);
        IM_CHECK(Components(ctx, editing.parameters[3])[0] == 5);
        IM_CHECK(std::abs(Components(ctx, editing.parameters[4])[0] - 7.853981634f) < .00001f);
        IM_CHECK(std::abs(Components(ctx, editing.parameters[5])[1] - 1.570796327f) < .00001f);
        IM_CHECK(Components(ctx, editing.parameters[6])[3] == .5f && Components(ctx, editing.parameters[7])[5] == 5);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        ctx->ItemClick("**/Apply/##dialogbutton"); ctx->Yield(2);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        const auto angleBeforeCancel = WorkingComponents(ctx, 4);
        const auto dial = ctx->ItemInfo("**/$$4/##components/$$0/##angle/##dial").RectFull.GetCenter();
        ctx->MouseMoveToPos(dial); ctx->MouseDown(0);
        ctx->MouseMoveToPos(ImVec2(dial.x - 5, dial.y - 8)); ctx->Yield(2);
        IM_CHECK(WorkingComponents(ctx, 4) != angleBeforeCancel);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseMoveToPos(ImVec2(dial.x + 8, dial.y - 4)); ctx->Yield(2); ctx->MouseUp(0); ctx->Yield(2);
        IM_CHECK(WorkingComponents(ctx, 4) == angleBeforeCancel);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        Input(ctx, "**/$$0/##components/##x", "nan");
        IM_CHECK(Read<bool>(ctx, [] { return !editing.box->Valid(); }));
        ctx->KeyPress(ImGuiKey_Enter); ctx->Yield(2);
        IM_CHECK(Components(ctx, editing.parameters[0])[0] == 12);
        ctx->ItemClick("**/Cancel/##dialogbutton"); ctx->Yield(2);
        IM_CHECK(Components(ctx, editing.parameters[0])[0] == 12);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK(Components(ctx, editing.parameters[0]) == vector && Components(ctx, editing.parameters[1]) == matrix &&
                 Components(ctx, editing.parameters[2]) == color && Components(ctx, editing.parameters[3]) == rect);
        for (size_t i = 4; i < 8; ++i) IM_CHECK(Components(ctx, editing.parameters[i]) == additional[i - 4]);
        IM_CHECK(Read<bool>(ctx, [] { return !App().State().document->Dirty() && App().State().History().CanRedo(); }));
    };
    test = IM_REGISTER_TEST(engine, "modules", "color_picker_cancel_restores_working_value");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Clean cleanup{ctx};
        OnMainThread(ctx, [] {
            auto &state = App().State();
            auto *parameter = state.host.Context()->CreateCKParameterLocal("Picker edit", CKPGUID_COLOR);
            const VxColor color(.2f, .3f, .4f, 1.0f); parameter->SetValue(&color, sizeof(color));
            editing.parameters = {parameter->GetID()};
            editing.box = std::make_unique<vdev::ParameterBox>(state); IM_CHECK_NO_RET(editing.box->Add(parameter, "Color"));
            state.History().Clear(); state.document->MarkSaved(); state.History().MarkSavePoint(); editing.open = true;
        });
        ctx->Yield(3); ctx->SetRef("//Value editing###PrimitiveValues");
        const auto before = WorkingComponents(ctx, 0);
        ctx->ItemClick("**/##swatch"); ctx->Yield(2); ctx->SetRef("//$FOCUSED");
        const auto alpha = ctx->ItemInfo("**/##picker/alpha").RectFull;
        ctx->MouseMoveToPos(ImVec2(alpha.GetCenter().x, alpha.Min.y + alpha.GetHeight() * .65f)); ctx->MouseClick(0); ctx->Yield(2);
        IM_CHECK(WorkingComponents(ctx, 0) != before);
        IM_CHECK(Components(ctx, editing.parameters[0]) == before);
        ctx->ItemClick("Cancel"); ctx->Yield(2);
        IM_CHECK(WorkingComponents(ctx, 0) == before);
        IM_CHECK(Read<bool>(ctx, [] { return editing.open && !App().State().document->Dirty() && App().State().History().UndoCount() == 0; }));
        ctx->SetRef("//Value editing###PrimitiveValues");
        ctx->ItemClick("**/##swatch"); ctx->Yield(2); ctx->SetRef("//$FOCUSED");
        const auto second = ctx->ItemInfo("**/##picker/alpha").RectFull;
        ctx->MouseMoveToPos(ImVec2(second.GetCenter().x, second.Min.y + second.GetHeight() * .4f)); ctx->MouseClick(0); ctx->Yield(2);
        ctx->KeyPress(ImGuiKey_Escape); ctx->Yield(2);
        IM_CHECK(WorkingComponents(ctx, 0) == before);
        ctx->SetRef("//Value editing###PrimitiveValues"); ctx->ItemClick("**/Cancel/##dialogbutton");
        IM_CHECK(Components(ctx, editing.parameters[0]) == before);
    };
    test = IM_REGISTER_TEST(engine, "modules", "compound_inline_drag_cancel_and_undo");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Clean cleanup{ctx};
        OnMainThread(ctx, [] {
            auto &state = App().State(); auto &app = state.workspace->Application();
            auto *parameter = state.host.Context()->CreateCKParameterLocal("Inline vector", CKPGUID_VECTOR);
            editing.parameters = {parameter->GetID()}; editing.target = state.document->Reference(parameter->GetID());
            editing.property = "parameter.vector3"; editing.owner = app.OpenScope(); editing.inlineValue = true;
            state.History().Clear(); state.document->MarkSaved(); state.History().MarkSavePoint();
        });
        ctx->Yield(3); ctx->SetRef("//Typed inline");
        const auto before = Components(ctx, editing.parameters[0]);
        const auto at = ctx->ItemInfo("**/$$0/##x").RectFull.GetCenter();
        ctx->MouseMoveToPos(at); ctx->MouseDown(0);
        for (int i = 1; i < 8; ++i) { ctx->MouseMoveToPos(ImVec2(at.x + 8 * i, at.y)); ctx->Yield(2); }
        IM_CHECK(Components(ctx, editing.parameters[0]) == before);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 0);
        ctx->MouseUp(0); ctx->Yield(3);
        IM_CHECK(Components(ctx, editing.parameters[0]) != before);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        ctx->MouseMoveToPos(at); ctx->MouseDown(0); ctx->MouseMoveToPos(ImVec2(at.x + 80, at.y)); ctx->Yield(3);
        ctx->KeyPress(ImGuiKey_Escape); ctx->MouseUp(0); ctx->Yield(3);
        IM_CHECK(Components(ctx, editing.parameters[0]) == before);
        IM_CHECK(Read<bool>(ctx, [] { return App().State().History().UndoCount() == 0 && App().State().History().CanRedo() && !App().State().document->Dirty(); }));
    };
    test = IM_REGISTER_TEST(engine, "modules", "external_vector_editor_shared_by_inline_and_dialog");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Clean cleanup{ctx};
        vdev::EditorConfiguration previous;
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &app = state.workspace->Application();
            previous = app.Configuration(); editing.configuration = std::make_unique<vdev::EditorConfiguration>(previous);
            auto config = previous;
            for (auto &choice : config.providers)
                if ((choice.kind == "command" && choice.id == "parameter.vector3-set") ||
                    (choice.kind == "query" && choice.id == "parameter.vector3-value")) choice.provider = "example.vector";
            std::string error; IM_CHECK_NO_RET(state.workspace->Configure(config, error));
            auto *parameter = state.host.Context()->CreateCKParameterLocal("External vector", CKPGUID_VECTOR);
            editing.parameters = {parameter->GetID()}; editing.target = state.document->Reference(parameter->GetID());
            editing.property = "parameter.vector3"; editing.owner = app.OpenScope(); editing.inlineValue = true; state.History().Clear();
        });
        ctx->Yield(3); ctx->MenuClick("//##MainMenuBar/Views/Value Editors/External vector");
        ctx->Yield(2); ctx->SetRef("//Typed inline"); ctx->ItemClick("Offset vector"); ctx->Yield(3);
        IM_CHECK(Components(ctx, editing.parameters[0]) == std::vector<float>({1, 2, 3}));
        OnMainThread(ctx, [] {
            editing.inlineValue = false; auto &app = App().State().workspace->Application();
            const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, editing.target};
            IM_CHECK_NO_RET(app.Execute("parameter.edit", {app.Scope(), &target, 1}).status == EDITOR_OK);
        });
        ctx->Yield(3); ctx->SetRef("//Edit Parameter###AttributeManagerValue");
        IM_CHECK(ctx->ItemExists("**/Offset vector"));
        const auto reads = [&] {
            return Read<uint64_t>(ctx, [] {
                auto &app = App().State().workspace->Application();
                const auto result = app.Query("example.vector-count", {app.Scope(), nullptr, 0});
                return vdev::EditorClient::Integer(result, "reads");
            });
        };
        const auto beforeSwitch = reads();
        OnMainThread(ctx, [&] { std::string error; IM_CHECK_NO_RET(App().State().workspace->Configure(previous, error)); });
        ctx->Yield(2);
        IM_CHECK(reads() > beforeSwitch);
        ctx->ItemClick("**/Offset vector");
        IM_CHECK(Components(ctx, editing.parameters[0]) == std::vector<float>({1, 2, 3}));
        ctx->ItemClick("**/OK/##dialogbutton"); ctx->Yield(3);
        IM_CHECK(Components(ctx, editing.parameters[0]) == std::vector<float>({2, 4, 6}));
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 2);
        IM_CHECK(Read<uint64_t>(ctx, [] {
            auto &app = App().State().workspace->Application();
            const auto result = app.Query("example.vector-count", {app.Scope(), nullptr, 0});
            return result.status == EDITOR_OK ? result.fields.front().integer : 0;
        }) == 2);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK(Components(ctx, editing.parameters[0]) == std::vector<float>({1, 2, 3}));
    };

    test = IM_REGISTER_TEST(engine, "modules", "angle_dial_escape_stops_writes");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Clean cleanup{ctx};
        OnMainThread(ctx, [] {
            auto &state = App().State();
            auto *parameter = state.host.Context()->CreateCKParameterLocal("Dial cancel", CKPGUID_ANGLE);
            const float value = 7.853981634f; parameter->SetValue(&value, sizeof(value));
            editing.parameters = {parameter->GetID()};
            editing.box = std::make_unique<vdev::ParameterBox>(state); IM_CHECK_NO_RET(editing.box->Add(parameter, "Angle"));
            state.History().Clear(); state.document->MarkSaved(); state.History().MarkSavePoint(); editing.open = true;
        });
        ctx->Yield(3); ctx->SetRef("//Value editing###PrimitiveValues");
        const auto before = WorkingComponents(ctx, 0);
        const auto dial = ctx->ItemInfo("**/##dial").RectFull.GetCenter();
        ctx->MouseMoveToPos(dial); ctx->MouseDown(0); ctx->MouseMoveToPos(ImVec2(dial.x - 5, dial.y - 8)); ctx->Yield(2);
        IM_CHECK(WorkingComponents(ctx, 0) != before);
        ctx->KeyPress(ImGuiKey_Escape);
        IM_CHECK(Read<bool>(ctx, [] { return editing.open; }));
        ctx->MouseMoveToPos(ImVec2(dial.x + 8, dial.y - 4)); ctx->Yield(2); ctx->MouseUp(0); ctx->Yield(2);
        IM_CHECK(WorkingComponents(ctx, 0) == before);
        IM_CHECK(Components(ctx, editing.parameters[0]) == before);
        IM_CHECK(Read<bool>(ctx, [] { return App().State().History().UndoCount() == 0 && !App().State().document->Dirty(); }));
    };

    test = IM_REGISTER_TEST(engine, "modules", "nested_parameter_dialog_one_undo_and_cancel");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Clean cleanup{ctx}; OnMainThread(ctx, [] { NestedFixture(); });
        ctx->Yield(3); ctx->SetRef("//Value editing###PrimitiveValues");
        const auto before = Value(ctx, editing.parameters[0]);
        Input(ctx, "**/##number", "12");
        Input(ctx, "**/##string", "Nested text");
        Input(ctx, "**/##r", "128");
        Input(ctx, "**/##x", "7");
        IM_CHECK(Value(ctx, editing.parameters[0]) == before);
        ctx->ItemClick("**/Apply/##dialogbutton"); ctx->Yield(3);
        const auto applied = Value(ctx, editing.parameters[0]);
        IM_CHECK(applied.members[0].members[0].integer == 12 && applied.members[0].members[3].text == "Nested text");
        IM_CHECK(std::abs(applied.members[0].members[1].components[0] - 128.0 / 255) < .00001 && applied.members[1].components[0] == 7);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        ctx->ItemClick("**/Apply/##dialogbutton"); ctx->Yield(2);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        Input(ctx, "**/##r", "200"); ctx->KeyPress(ImGuiKey_Escape); ctx->Yield(2);
        IM_CHECK(Read<bool>(ctx, [] { return editing.open; }));
        IM_CHECK(WorkingValue(ctx) == applied);
        Input(ctx, "**/##string", "Cancelled text"); ctx->KeyPress(ImGuiKey_Escape); ctx->Yield(2);
        IM_CHECK(WorkingValue(ctx) == applied && Read<bool>(ctx, [] { return editing.open; }));
        ctx->ItemClick("**/Cancel/##dialogbutton"); ctx->Yield(2);
        IM_CHECK(Value(ctx, editing.parameters[0]) == applied);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK(Value(ctx, editing.parameters[0]) == before);
        IM_CHECK(Read<bool>(ctx, [] { return !App().State().document->Dirty() && App().State().History().CanRedo(); }));
    };
    test = IM_REGISTER_TEST(engine, "modules", "object_array_dialog_operations_and_cancel");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Clean cleanup{ctx};
        OnMainThread(ctx, [] {
            auto &state = App().State(); auto *context = state.host.Context();
            auto *parameter = context->CreateCKParameterLocal("Array editor", CKPGUID_OBJECTARRAY);
            auto *a = context->CreateObject(CKCID_3DENTITY, "Array A"), *b = context->CreateObject(CKCID_3DENTITY, "Array B");
            XObjectArray list; list.PushBack(a->GetID()); list.PushBack(b->GetID());
            IM_CHECK_NO_RET(vdev::SetHeldValue(parameter, &list));
            editing.parameters = {parameter->GetID(), a->GetID(), b->GetID()};
            editing.box = std::make_unique<vdev::ParameterBox>(state); IM_CHECK_NO_RET(editing.box->Add(parameter, "Objects")); editing.open = true;
            state.History().Clear(); state.document->MarkSaved(); state.History().MarkSavePoint();
        });
        ctx->Yield(3); ctx->SetRef("//Value editing###PrimitiveValues");
        const auto before = Value(ctx, editing.parameters[0]);
        ArrayItem(ctx); ctx->Yield(2); ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_C);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_V); ctx->Yield(2);
        IM_CHECK(WorkingValue(ctx).objects.size() == 3 && Value(ctx, editing.parameters[0]) == before);
        ArrayItem(ctx); ctx->Yield(2); ctx->KeyPress(ImGuiKey_KeypadAdd); ctx->Yield(2);
        IM_CHECK(WorkingValue(ctx).objects[0] == before.objects[1]);
        ctx->KeyPress(ImGuiKey_Delete); ctx->Yield(2);
        IM_CHECK(WorkingValue(ctx).objects.size() == 2);
        ctx->ItemClick("**/OK/##dialogbutton"); ctx->Yield(2);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        OnMainThread(ctx, [] {
            auto &state = App().State(); IM_CHECK_NO_RET(state.History().Undo());
            editing.box = std::make_unique<vdev::ParameterBox>(state); IM_CHECK_NO_RET(editing.box->Add(Object<CKParameter>(editing.parameters[0]), "Objects"));
            editing.open = true; editing.popup = false;
        });
        ctx->Yield(3); ctx->SetRef("//Value editing###PrimitiveValues");
        ArrayItem(ctx); ctx->Yield(2); ctx->KeyPress(ImGuiKey_Delete); ctx->Yield(2);
        IM_CHECK(WorkingValue(ctx).objects.size() == 1);
        ctx->ItemClick("**/Cancel/##dialogbutton"); ctx->Yield(2);
        IM_CHECK(Value(ctx, editing.parameters[0]) == before);
        IM_CHECK(Read<bool>(ctx, [] { return App().State().History().UndoCount() == 0 && App().State().History().CanRedo() && !App().State().document->Dirty(); }));
    };
    test = IM_REGISTER_TEST(engine, "modules", "object_array_inline_operations_and_cancel");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Clean cleanup{ctx};
        OnMainThread(ctx, [] {
            auto &state = App().State(); auto *context = state.host.Context();
            auto *parameter = context->CreateCKParameterLocal("Inline array", CKPGUID_OBJECTARRAY);
            auto *first = context->CreateObject(CKCID_3DENTITY, "Inline array first");
            auto *second = context->CreateObject(CKCID_3DENTITY, "Inline array second");
            IM_CHECK_NO_RET(parameter && first && second);
            editing.parameters = {parameter->GetID(), first->GetID(), second->GetID()};
            editing.target = state.document->Reference(parameter->GetID());
            vdev::ParameterValueDescription value;
            IM_CHECK_NO_RET(vdev::DescribeParameterValue(*state.document, parameter, value));
            value.value.objects = {state.document->Reference(first->GetID()), state.document->Reference(second->GetID())};
            IM_CHECK_NO_RET(vdev::SetParameterValue(*state.document, editing.target, value.type, value.value).ok);
            editing.property = "parameter.object-array"; editing.owner = state.workspace->Application().OpenScope(); editing.inlineValue = true;
            state.History().Clear(); state.document->MarkSaved(); state.History().MarkSavePoint();
        });
        ctx->Yield(3); ctx->SetRef("//Typed inline");
        const auto before = Value(ctx, editing.parameters[0]);
        ctx->ItemClick("**/$$0/Down"); ctx->Yield(2);
        IM_CHECK(Value(ctx, editing.parameters[0]).objects == std::vector<vdev::ObjectRef>({before.objects[1], before.objects[0]}));
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        ctx->ItemClick("**/$$0/Remove"); ctx->Yield(2);
        IM_CHECK(Value(ctx, editing.parameters[0]).objects == std::vector<vdev::ObjectRef>({before.objects[0]}));
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 2);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo() && App().State().History().Undo()); });
        ctx->Yield(2);
        const auto add = Read<ImGuiID>(ctx, [] {
            auto *window = ImGui::FindWindowByName("Typed inline");
            return ImHashStr("Add Object", 0, ImHashStr("##integer", 0, window->ID));
        });
        ctx->ItemClick(add); ctx->Yield(2); ctx->KeyPress(ImGuiKey_Escape); ctx->Yield(2);
        IM_CHECK(Value(ctx, editing.parameters[0]) == before);
        IM_CHECK(Read<bool>(ctx, [] { return App().State().History().UndoCount() == 0 && App().State().History().CanRedo() && !App().State().document->Dirty(); }));
    };
    test = IM_REGISTER_TEST(engine, "modules", "nested_inline_commit_and_cancel");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Clean cleanup{ctx}; OnMainThread(ctx, [] { NestedFixture(false); });
        ctx->Yield(3); ctx->SetRef("//Typed inline");
        const auto before = Value(ctx, editing.parameters[0]);
        const auto at = ctx->ItemInfo("**/##Count").RectFull.GetCenter();
        ctx->MouseMoveToPos(at); ctx->MouseDown(0);
        for (int i = 1; i <= 8; ++i) { ctx->MouseMoveToPos(ImVec2(at.x + 8 * i, at.y)); ctx->Yield(2); }
        IM_CHECK(Value(ctx, editing.parameters[0]) == before);
        ctx->MouseUp(0); ctx->Yield(3);
        IM_CHECK(Value(ctx, editing.parameters[0]).members[0].members[0].integer != before.members[0].members[0].integer);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 1);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        ctx->MouseMoveToPos(at); ctx->MouseDown(0); ctx->MouseMoveToPos(ImVec2(at.x + 72, at.y)); ctx->Yield(2);
        ctx->KeyPress(ImGuiKey_Escape); ctx->MouseUp(0); ctx->Yield(2);
        IM_CHECK(Value(ctx, editing.parameters[0]) == before);
        IM_CHECK(Read<bool>(ctx, [] { return App().State().History().UndoCount() == 0 && App().State().History().CanRedo() && !App().State().document->Dirty(); }));
    };

    test = IM_REGISTER_TEST(engine, "modules", "external_structure_editor_and_nested_provider");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Clean cleanup{ctx};
        vdev::EditorConfiguration previous;
        OnMainThread(ctx, [&] {
            auto &state = App().State(); auto &app = state.workspace->Application(); previous = app.Configuration();
            editing.configuration = std::make_unique<vdev::EditorConfiguration>(previous);
            auto config = previous;
            for (auto &choice : config.providers) {
                if (choice.kind == "editor" && choice.id == "value.structure") choice.provider = "example.structure";
                if (choice.kind == "editor" && choice.id == "value.vector3") choice.provider = "example.vector";
                if ((choice.kind == "command" && choice.id == "parameter.structure-set") ||
                    (choice.kind == "query" && choice.id == "parameter.structure-value")) choice.provider = "example.structure";
            }
            std::string error; IM_CHECK_NO_RET(state.workspace->Configure(config, error)); NestedFixture(false);
        });
        ctx->Yield(3); ctx->SetRef("//Typed inline");
        ctx->ItemClick("Offset nested count"); ctx->Yield(2);
        ctx->ItemClick("Offset vector"); ctx->Yield(2);
        IM_CHECK(Value(ctx, editing.parameters[0]).members[0].members[0].integer == 1);
        IM_CHECK(Value(ctx, editing.parameters[0]).members[1].components == std::vector<double>({1, 2, 3}));
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 2);
        const auto inlineValue = Value(ctx, editing.parameters[0]);
        OnMainThread(ctx, [] {
            editing.inlineValue = false; auto &app = App().State().workspace->Application();
            const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, editing.target};
            IM_CHECK_NO_RET(app.Execute("parameter.edit", {app.Scope(), &target, 1}).status == EDITOR_OK);
        });
        ctx->Yield(3); ctx->SetRef("//Edit Parameter###AttributeManagerValue");
        const auto reads = [&] {
            return Read<uint64_t>(ctx, [] { auto &app = App().State().workspace->Application(); const auto result = app.Query("example.structure-count", {app.Scope(), nullptr, 0}); return vdev::EditorClient::Integer(result, "reads"); });
        };
        const auto beforeSwitch = reads();
        OnMainThread(ctx, [&] { std::string error; IM_CHECK_NO_RET(App().State().workspace->Configure(previous, error)); });
        ctx->Yield(2);
        IM_CHECK(reads() > beforeSwitch && ctx->ItemExists("**/Offset nested count") && ctx->ItemExists("**/Offset vector"));
        ctx->ItemClick("**/Offset nested count"); ctx->ItemClick("**/Offset vector"); ctx->Yield(2);
        IM_CHECK(Value(ctx, editing.parameters[0]) == inlineValue);
        ctx->ItemClick("**/OK/##dialogbutton"); ctx->Yield(3);
        const auto applied = Value(ctx, editing.parameters[0]);
        IM_CHECK(applied.members[0].members[0].integer == 2 && applied.members[1].components == std::vector<double>({2, 4, 6}));
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 3);
        IM_CHECK(Read<uint64_t>(ctx, [] { auto &app = App().State().workspace->Application(); const auto result = app.Query("example.structure-count", {app.Scope(), nullptr, 0}); return vdev::EditorClient::Integer(result, "count"); }) == 3);
        OnMainThread(ctx, [] { IM_CHECK_NO_RET(App().State().History().Undo()); });
        IM_CHECK(Value(ctx, editing.parameters[0]) == inlineValue);
    };

    test = IM_REGISTER_TEST(engine, "modules", "nested_member_reuse_is_rejected");
    test->GuiFunc = Gui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx); Clean cleanup{ctx}; OnMainThread(ctx, [] { NestedFixture(); });
        ctx->Yield(3); ctx->SetRef("//Value editing###PrimitiveValues");
        Input(ctx, "**/##number", "12");
        OnMainThread(ctx, [] {
            auto &state = App().State(); vdev::ParameterValueDescription description;
            IM_CHECK_NO_RET(vdev::DescribeParameterValue(*state.document, Object<CKParameter>(editing.parameters[0]), description));
            const auto old = description.members[0].members[0].parameter;
            IM_CHECK_NO_RET(state.host.Context()->DestroyObject(old.object, CK_DESTROY_FREEID | CK_DESTROY_NONOTIFY) == CK_OK);
            auto *replacement = state.host.Context()->CreateCKParameterOut("Replacement member", CKPGUID_INT);
            IM_CHECK_NO_RET(replacement && replacement->GetID() == old.object && state.document->Reference(old.object) != old);
            const int value = 77; replacement->SetValue(&value, sizeof(value));
        });
        ctx->ItemClick("**/Apply/##dialogbutton"); ctx->Yield(2);
        IM_CHECK(Value(ctx, editing.parameters[0]).members[0].members[0].integer == 77);
        IM_CHECK(Read<size_t>(ctx, [] { return App().State().History().UndoCount(); }) == 0);
        ctx->ItemClick("**/Cancel/##dialogbutton");
    };

}
} // namespace vdevuitest
