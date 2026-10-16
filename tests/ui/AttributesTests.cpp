// "attributes": attribute types and attributes on the sample opened from the
// command line: a type created in the Attribute Manager's Add Attribute
// dialog (Create Attribute, More: a name, a parameter type), added to a 3D
// object from the Level Manager's shortcut menu (Add Attributes > Add
// Attribute...), its value edited in the Attribute Manager, copied to
// another object (the toolbar's Copy and Paste), each undone; last, Remove
// All User Attributes. Creating a type is not undone (a change outside undo): the composition
// stays modified from then on, the tests check that an undo gives back the
// state before it.

#include <cmath>

#include "core/level/LevelCommands.h"
#include "tests/ui/UiTests.h"

namespace vdevuitest {

namespace {

constexpr const char *TYPE_NAME = "UI Test Attribute";
constexpr const char *CATEGORY = "UI Tests"; // its category, its alone (its row shown at once)
constexpr const char *CATEGORY_ITEM = "**/UI Tests";
constexpr const char *DIALOG = "//Add Attribute";
constexpr const char *MANAGER = "//Attribute Manager";

CKAttributeType TestType(ImGuiTestContext *ctx) {
    return Read<CKAttributeType>(ctx, [] {
        CKAttributeManager *attributes = App().State().host.Context()->GetAttributeManager();
        return attributes ? attributes->GetAttributeTypeByName(const_cast<CKSTRING>(TYPE_NAME)) : -1;
    });
}

bool HasAttribute(ImGuiTestContext *ctx, CK_ID id, CKAttributeType type) {
    return Read<bool>(ctx, [id, type] {
        CKBeObject *object = Object<CKBeObject>(id);
        return object && object->HasAttribute(type);
    });
}

// The attribute's value (a Float), NAN without it.
float AttributeValue(ImGuiTestContext *ctx, CK_ID id, CKAttributeType type) {
    return Read<float>(ctx, [id, type] {
        float value = NAN;
        CKBeObject *object = Object<CKBeObject>(id);
        if (CKParameterOut *parameter = object ? object->GetAttributeParameter(type) : nullptr) parameter->GetValue(&value);
        return value;
    });
}

// The Attribute Manager shown (Editors > Attributes Manager, a toggle) and in front.
void ShowManager(ImGuiTestContext *ctx) {
    Settle(ctx);
    if (!App().State().showAttributeManager) ctx->MenuClick("//##MainMenuBar/Editors/Attributes Manager");
    ctx->Yield(2);
    ctx->WindowFocus(MANAGER);
    ctx->Yield(2);
}

// The Level Manager's 3D object (Ground) clicked, then its shortcut menu's
// Add Attributes > Add Attribute...: the test type chosen (its category
// opened), Add Selected. The object.
CK_ID AddTestAttribute(ImGuiTestContext *ctx) {
    Settle(ctx, "//Level Manager");
    const std::optional<LevelRow> row = ClickLevelRow(ctx, CKCID_3DOBJECT, true);
    if (!row) return 0;
    ctx->MouseClick(1);
    ctx->Yield(3);
    ctx->SetRef("//$FOCUSED");
    ctx->MenuClick("Add Attributes/Add Attribute...");
    ctx->Yield(3);
    ctx->SetRef(DIALOG);
    ctx->ItemOpen(CATEGORY_ITEM); // ImGui keeps it open from the last time
    ctx->ItemClick((std::string("**/") + TYPE_NAME).c_str());
    ctx->ItemClick("Add Selected");
    ctx->Yield(3);
    return row->object;
}

} // namespace

void RegisterAttributesTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    // The Attribute Manager's Create Attribute: the Add Attribute dialog
    // with its More panel; a new category (the new type's category then), a
    // name, Float, Create: the type, of floats, in that category.
    t = IM_REGISTER_TEST(engine, "attributes", "create_type");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ShowManager(ctx);
        IM_CHECK_SILENT(TestType(ctx) < 0);
        ctx->ItemClick("//Attribute Manager/create");
        ctx->Yield(3);
        ctx->SetRef(DIALOG);
        ctx->ItemInputValue("##categoryname", CATEGORY);
        ctx->ItemClick("Create##category");
        ctx->ItemInputValue("##typename", TYPE_NAME);
        ctx->ComboClick("##typeparameter/Float");
        ctx->ItemClick("Create##type");
        ctx->Yield(2);
        const CKAttributeType type = TestType(ctx);
        IM_CHECK(type >= 0);
        bool floats = false;
        std::string category;
        OnMainThread(ctx, [&] {
            CKAttributeManager *attributes = App().State().host.Context()->GetAttributeManager();
            floats = attributes->GetAttributeParameterGUID(type) == CKPGUID_FLOAT;
            const char *name = attributes->GetAttributeCategory(type);
            category = name ? name : "";
        });
        IM_CHECK(floats);
        IM_CHECK_STR_EQ(category.c_str(), CATEGORY);
        ctx->ItemClick("Close");
        ctx->Yield(2);
        IM_CHECK(!ctx->GetWindowByRef(DIALOG) || !ctx->GetWindowByRef(DIALOG)->WasActive);
    };

    // The type added to Ground from the Level Manager: Ground has it, one
    // undo step; Ctrl+Z takes it off.
    t = IM_REGISTER_TEST(engine, "attributes", "add_to_object_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CKAttributeType type = TestType(ctx);
        IM_CHECK_SILENT(type >= 0);
        const bool dirty = App().State().document->Dirty();
        const CK_ID object = AddTestAttribute(ctx);
        IM_CHECK_SILENT(object != 0);
        IM_CHECK(HasAttribute(ctx, object, type));
        IM_CHECK(App().State().History().CanUndo());
        Undo(ctx);
        IM_CHECK(!HasAttribute(ctx, object, type));
        IM_CHECK(App().State().document->Dirty() == dirty);
    };

    // The type selected in the Attribute Manager lists Ground with it, its
    // value typed in (2.5); Ctrl+Z gives the value back, the attribute stays.
    t = IM_REGISTER_TEST(engine, "attributes", "edit_value_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CKAttributeType type = TestType(ctx);
        IM_CHECK_SILENT(type >= 0);
        const CK_ID object = AddTestAttribute(ctx);
        IM_CHECK_SILENT(object != 0 && HasAttribute(ctx, object, type));
        const float before = AttributeValue(ctx, object, type);
        ShowManager(ctx);
        ctx->SetRef(MANAGER);
        ctx->ItemOpen(CATEGORY_ITEM);
        // The type's row: "##attr" under its type, the wildcard matches the
        // label the row shows.
        ctx->ItemClick((std::string("**/") + TYPE_NAME).c_str());
        ctx->Yield(2);
        ctx->ItemInputValue(("**/$$" + std::to_string(object) + "/##value").c_str(), "2.5");
        ctx->Yield(3);
        IM_CHECK(std::fabs(AttributeValue(ctx, object, type) - 2.5f) < 1e-5f);
        Undo(ctx);
        IM_CHECK(HasAttribute(ctx, object, type));
        IM_CHECK(std::fabs(AttributeValue(ctx, object, type) - before) < 1e-5f);
    };

    // Ground with the type (2.5) and another 3D entity with it: Ground's row
    // selected, Copy; the other's, Paste: 2.5 there, one undo step; Ctrl+Z
    // gives its value back.
    t = IM_REGISTER_TEST(engine, "attributes", "copy_paste_value_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CKAttributeType type = TestType(ctx);
        IM_CHECK_SILENT(type >= 0);
        const CK_ID ground = AddTestAttribute(ctx);
        IM_CHECK_SILENT(ground != 0 && HasAttribute(ctx, ground, type));
        CK_ID other = 0;
        OnMainThread(ctx, [&] {
            CKContext *context = App().State().host.Context();
            const XObjectPointerArray &entities = context->GetObjectListByType(CKCID_3DENTITY, TRUE);
            for (int i = 0; i < entities.Size() && !other; ++i)
                if (entities[i]->GetID() != ground && !entities[i]->IsDynamic()) other = entities[i]->GetID();
            if (other) vdev::AddAttribute(*App().State().document, {other}, type);
        });
        IM_CHECK_SILENT(other != 0 && HasAttribute(ctx, other, type));
        const float before = AttributeValue(ctx, other, type);
        ShowManager(ctx);
        ctx->SetRef(MANAGER);
        ctx->ItemOpen(CATEGORY_ITEM);
        ctx->ItemClick((std::string("**/") + TYPE_NAME).c_str());
        ctx->Yield(2);
        ctx->ItemInputValue(("**/$$" + std::to_string(ground) + "/##value").c_str(), "2.5");
        ctx->Yield(3);
        IM_CHECK_SILENT(std::fabs(AttributeValue(ctx, ground, type) - 2.5f) < 1e-5f);
        ctx->ItemClick(("**/$$" + std::to_string(ground) + "/###row").c_str());
        ctx->ItemClick("copy/##bitmap");
        // One clipboard: the value copied is the parameters' (Paste As
        // Shortcut, the Parameter Debugger).
        IM_CHECK_EQ(App().State().clipboard.parameters.size(), 1u);
        ctx->ItemClick(("**/$$" + std::to_string(other) + "/###row").c_str());
        ctx->ItemClick("paste/##bitmap");
        ctx->Yield(3);
        IM_CHECK(std::fabs(AttributeValue(ctx, other, type) - 2.5f) < 1e-5f);
        Undo(ctx);
        IM_CHECK(std::fabs(AttributeValue(ctx, other, type) - before) < 1e-5f);
        IM_CHECK(std::fabs(AttributeValue(ctx, ground, type) - 2.5f) < 1e-5f);
    };

    // The test type dragged from the Attribute Manager's list onto an entity
    // in the 3D Layout (without it first): the entity has it; Ctrl+Z takes
    // it away.
    t = IM_REGISTER_TEST(engine, "attributes", "drag_to_layout_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CKAttributeType type = TestType(ctx);
        IM_CHECK_SILENT(type >= 0);
        Settle(ctx, "//3D Layout");
        const InView found = FindEntity(ctx);
        IM_CHECK_SILENT(found.entity != 0);
        if (HasAttribute(ctx, found.entity, type))
            OnMainThread(ctx, [&] { vdev::RemoveAttribute(*App().State().document, found.entity, type); });
        IM_CHECK_SILENT(!HasAttribute(ctx, found.entity, type));
        ShowManager(ctx);
        ctx->SetRef(MANAGER);
        ctx->ItemOpen(CATEGORY_ITEM);
        ctx->MouseMove((std::string("**/") + TYPE_NAME).c_str());
        ctx->MouseDown(0);
        const ImVec2 from = ctx->UiContext->IO.MousePos;
        ctx->MouseMoveToPos(ImVec2(from.x + 20.0f, from.y + 10.0f));
        ctx->MouseMoveToPos(found.at);
        ctx->Yield(3);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(HasAttribute(ctx, found.entity, type));
        Undo(ctx);
        IM_CHECK(!HasAttribute(ctx, found.entity, type));
    };

    // Last (no undo): Remove All User Attributes, OK in its confirmation:
    // the type gone, Ground without it, the undo steps cleared.
    t = IM_REGISTER_TEST(engine, "attributes", "remove_user_attributes");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CKAttributeType type = TestType(ctx);
        IM_CHECK_SILENT(type >= 0);
        ShowManager(ctx);
        ctx->ItemClick("//Attribute Manager/removeuser");
        ctx->Yield(3);
        ctx->ItemClick("//Remove All User Attributes/OK");
        ctx->Yield(3);
        IM_CHECK_EQ(TestType(ctx), -1);
        IM_CHECK(!App().State().History().CanUndo());
        IM_CHECK(!App().State().History().CanRedo());
    };
}

} // namespace vdevuitest
