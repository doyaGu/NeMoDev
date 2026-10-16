// "layout": the 3D Layout on the sample opened from the command line (3D
// entities in view after Zoom on Scene, W): its tool keys, Ctrl+G (General
// Preferences) and camera keys,
// a click selecting an entity (and its Level Manager row), Ctrl+A and
// Ctrl+D, an entity translated, rotated and scaled with the mouse; the
// shortcut menu (a right click): the entity's Setup, Create Script on, Set /
// Restore / Remove Initial Conditions, Create Scripts on Selection, Show
// Script, Select Camera, Duplicate; deleted through the Delete Options
// dialog; each undone.

#include <cmath>

#include "tests/ui/UiTests.h"

namespace vdevuitest {

namespace {

bool Same(const VxVector &a, const VxVector &b) {
    return std::fabs(a.x - b.x) < 1e-3f && std::fabs(a.y - b.y) < 1e-3f && std::fabs(a.z - b.z) < 1e-3f;
}

bool Same(const VxMatrix &a, const VxMatrix &b) {
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            if (std::fabs(a[row][column] - b[row][column]) > 1e-4f) return false;
    return true;
}

CK_ID CurrentCamera(ImGuiTestContext *ctx) {
    return Read<CK_ID>(ctx, [] {
        CKCamera *camera = App().Layout().View().Camera();
        return camera ? camera->GetID() : CK_ID(0);
    });
}

// A 3D entity's world matrix and its scale.
VxMatrix WorldMatrix(ImGuiTestContext *ctx, CK_ID entity) {
    return Read<VxMatrix>(ctx, [entity] {
        VxMatrix matrix;
        matrix.SetIdentity();
        if (CK3dEntity *object = Object<CK3dEntity>(entity)) matrix = object->GetWorldMatrix();
        return matrix;
    });
}

VxVector Scale(ImGuiTestContext *ctx, CK_ID entity) {
    return Read<VxVector>(ctx, [entity] {
        VxVector scale(1.0f, 1.0f, 1.0f);
        if (CK3dEntity *object = Object<CK3dEntity>(entity)) object->GetScale(&scale);
        return scale;
    });
}

// The selected entity dragged with the left button from where it was
// found, by (dx, dy) in two steps.
void DragFrom(ImGuiTestContext *ctx, const ImVec2 &at, float dx, float dy) {
    ctx->MouseMoveToPos(at);
    ctx->MouseDown(0);
    ctx->MouseMoveToPos(ImVec2(at.x + dx * 0.5f, at.y + dy * 0.5f));
    ctx->MouseMoveToPos(ImVec2(at.x + dx, at.y + dy));
    ctx->MouseUp(0);
    ctx->Yield(3);
}

// The 3D Layout's shortcut menu opened on the entity (a right click): the
// labels of its items for that entity, the first of the menu's objects
// (LayoutWindow::ContextMenuPopup: "%s Setup (%s)", "%s (%s)").
struct EntityMenu {
    std::string setup, script;
};
EntityMenu OpenMenuOn(ImGuiTestContext *ctx, const InView &found) {
    EntityMenu menu;
    OnMainThread(ctx, [&] {
        CKObject *object = App().State().host.Context()->GetObject(found.entity);
        if (!object) return;
        const std::string cls = vdev::DevClassName(object), name = vdev::DevObjectName(object);
        menu.setup = cls + " Setup (" + name + ")##setup0";
        menu.script = name + " (" + cls + ")##script0";
    });
    ctx->MouseMoveToPos(found.at);
    ctx->MouseClick(1);
    ctx->Yield(3);
    return menu;
}

// A camera's item in Select Camera: "name", "name (class)" for the
// composition's cameras.
std::string CameraLabel(ImGuiTestContext *ctx, CK_ID id) {
    return Read<std::string>(ctx, [id] {
        CKObject *camera = Object<CKObject>(id);
        if (!camera) return std::string();
        std::string label = vdev::DevObjectName(camera);
        if (!App().Layout().View().IsViewCamera(id)) label += " (" + vdev::DevClassName(camera) + ")";
        return label;
    });
}

// A camera of Select Camera other than the current one.
CK_ID OtherCamera(ImGuiTestContext *ctx) {
    return Read<CK_ID>(ctx, [] {
        const vdev::LayoutView &view = App().Layout().View();
        for (CK_ID id : view.Cameras())
            if (CKObject *camera = Object<CKObject>(id))
                if (camera != view.Camera()) return id;
        return CK_ID(0);
    });
}

// The shortcut menu opened at the point, Select Camera > the camera.
void SelectCamera(ImGuiTestContext *ctx, const ImVec2 &at, CK_ID camera) {
    const std::string label = CameraLabel(ctx, camera);
    ctx->LogInfo("camera '%s'", label.c_str());
    ctx->MouseMoveToPos(at);
    ctx->MouseClick(1);
    ctx->Yield(3);
    ctx->SetRef("//$FOCUSED");
    ctx->MenuClick("Select Camera");
    // The submenu (ImGui's first menu level, "###Menu_00"); its items are
    // pushed with their camera's id.
    ctx->ItemClick(("//###Menu_00/**/" + label).c_str());
    ctx->Yield(3);
}

// The selected 3D entities other than entity: the copies Duplicate selects.
std::vector<CK_ID> OthersSelected(CK_ID entity) {
    std::vector<CK_ID> others;
    for (CK_ID id : App().State().objectSelection.objects)
        if (id != entity) others.push_back(id);
    return others;
}

} // namespace

void RegisterLayoutTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    // Dev's tool keys: X Translate, C Rotate, V Scale, O Orbit, A Select.
    t = IM_REGISTER_TEST(engine, "layout", "tool_keys");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        ImVec2 min, max;
        IM_CHECK(App().Layout().ShownView(min, max));
        ctx->MouseMoveToPos(Center(min, max));
        const std::pair<ImGuiKey, vdev::LayoutTool> keys[] = {
            {ImGuiKey_X, vdev::LayoutTool::Translate}, {ImGuiKey_C, vdev::LayoutTool::Rotate},
            {ImGuiKey_V, vdev::LayoutTool::Scale},     {ImGuiKey_O, vdev::LayoutTool::Orbit},
            {ImGuiKey_A, vdev::LayoutTool::Select}};
        for (const auto &key : keys) {
            ctx->KeyPress(key.first);
            ctx->Yield(2);
            IM_CHECK_EQ(static_cast<int>(App().Layout().Tool()), static_cast<int>(key.second));
        }
    };

    // Ctrl+G over the view: General Preferences (the Reference Guide's
    // page); Cancel closes it.
    t = IM_REGISTER_TEST(engine, "layout", "preferences_key");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        ImVec2 min, max;
        IM_CHECK(App().Layout().ShownView(min, max));
        ctx->MouseMoveToPos(Center(min, max));
        IM_CHECK_SILENT(!InFront(ctx, "//General Preferences"));
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_G);
        ctx->Yield(3);
        IM_CHECK(InFront(ctx, "//General Preferences"));
        ctx->ItemClick("//General Preferences/Cancel");
        ctx->Yield(2);
        IM_CHECK(!InFront(ctx, "//General Preferences"));
    };

    // N shows the next camera, Shift+N the previous one again.
    t = IM_REGISTER_TEST(engine, "layout", "camera_keys");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        ImVec2 min, max;
        IM_CHECK(App().Layout().ShownView(min, max));
        ctx->MouseMoveToPos(Center(min, max));
        const CK_ID before = CurrentCamera(ctx);
        IM_CHECK(before != 0);
        ctx->KeyPress(ImGuiKey_N);
        ctx->Yield(2);
        IM_CHECK(CurrentCamera(ctx) != before);
        ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_N);
        ctx->Yield(2);
        IM_CHECK_EQ(CurrentCamera(ctx), before);
    };

    // A click selects the entity under it alone, the Level Manager's rows
    // follow; Ctrl+D clears the selection, Ctrl+A selects everything again.
    t = IM_REGISTER_TEST(engine, "layout", "click_select");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK_SILENT(SelectEntity(ctx, found));
        vdev::EditorState &state = App().State();
        IM_CHECK_EQ(state.objectSelection.objects.size(), 1u);
        IM_CHECK(state.objectSelection.Contains(found.entity));
        IM_CHECK(App().Panels().LevelSelection().count(found.entity) == 1);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_D);
        ctx->Yield(2);
        IM_CHECK(state.objectSelection.objects.empty());
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->Yield(2);
        IM_CHECK(state.objectSelection.Contains(found.entity));
        IM_CHECK_GT(state.objectSelection.objects.size(), 0u);
    };

    // Translate (X): the entity dragged 50 pixels moves, one undo step, the
    // composition modified; Ctrl+Z puts it back, unmodified, Ctrl+Y moves it again.
    t = IM_REGISTER_TEST(engine, "layout", "translate_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK_SILENT(SelectEntity(ctx, found));
        vdev::EditorState &state = App().State();
        const VxVector before = Position(ctx, found.entity);
        ctx->KeyPress(ImGuiKey_X);
        ctx->Yield(2);
        ctx->MouseMoveToPos(found.at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos(ImVec2(found.at.x + 25.0f, found.at.y));
        ctx->MouseMoveToPos(ImVec2(found.at.x + 50.0f, found.at.y));
        ctx->MouseUp(0);
        ctx->Yield(3);
        const VxVector moved = Position(ctx, found.entity);
        IM_CHECK(!Same(moved, before));
        IM_CHECK(state.History().CanUndo());
        IM_CHECK(state.document->Dirty());

        Undo(ctx);
        IM_CHECK(Same(Position(ctx, found.entity), before));
        IM_CHECK(!state.document->Dirty());
        Redo(ctx);
        IM_CHECK(Same(Position(ctx, found.entity), moved));
        Undo(ctx);
    };

    // Rotate (C): the entity dragged turns (its world matrix changes), one
    // undo step; Ctrl+Z gives its matrix back, unmodified.
    t = IM_REGISTER_TEST(engine, "layout", "rotate_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK_SILENT(SelectEntity(ctx, found));
        vdev::EditorState &state = App().State();
        const VxMatrix before = WorldMatrix(ctx, found.entity);
        ctx->KeyPress(ImGuiKey_C);
        ctx->Yield(2);
        DragFrom(ctx, found.at, 40.0f, 30.0f);
        IM_CHECK(!Same(WorldMatrix(ctx, found.entity), before));
        IM_CHECK(state.History().CanUndo());
        IM_CHECK(state.document->Dirty());
        Undo(ctx);
        IM_CHECK(Same(WorldMatrix(ctx, found.entity), before));
        IM_CHECK(!state.document->Dirty());
    };

    // Scale (V): the entity dragged gets another scale, one undo step;
    // Ctrl+Z gives it back, unmodified.
    t = IM_REGISTER_TEST(engine, "layout", "scale_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK_SILENT(SelectEntity(ctx, found));
        vdev::EditorState &state = App().State();
        const VxVector before = Scale(ctx, found.entity);
        ctx->KeyPress(ImGuiKey_V);
        ctx->Yield(2);
        DragFrom(ctx, found.at, 40.0f, -40.0f);
        IM_CHECK(!Same(Scale(ctx, found.entity), before));
        IM_CHECK(state.History().CanUndo());
        IM_CHECK(state.document->Dirty());
        Undo(ctx);
        IM_CHECK(Same(Scale(ctx, found.entity), before));
        IM_CHECK(!state.document->Dirty());
    };

    // A right click on the entity: the shortcut menu, its "<class> Setup
    // (<name>)" opens the entity's Setup.
    t = IM_REGISTER_TEST(engine, "layout", "menu_setup");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK_SILENT(SelectEntity(ctx, found));
        const EntityMenu menu = OpenMenuOn(ctx, found);
        IM_CHECK_SILENT(!menu.setup.empty());
        ctx->ItemClick(("//$FOCUSED/" + menu.setup).c_str());
        ctx->Yield(3);
        IM_CHECK(ShowsObject(found.entity));
    };

    // The shortcut menu's Create Script on > the entity: a new Script on
    // it, one undo step; Ctrl+Z takes it off, unmodified.
    t = IM_REGISTER_TEST(engine, "layout", "menu_create_script_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK_SILENT(SelectEntity(ctx, found));
        const int before = ScriptCount(ctx, found.entity);
        IM_CHECK_SILENT(before >= 0);
        const EntityMenu menu = OpenMenuOn(ctx, found);
        IM_CHECK_SILENT(!menu.script.empty());
        ctx->SetRef("//$FOCUSED");
        ctx->MenuClick(("Create Script on/" + menu.script).c_str());
        ctx->Yield(3);
        IM_CHECK_EQ(ScriptCount(ctx, found.entity), before + 1);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK_EQ(ScriptCount(ctx, found.entity), before);
        IM_CHECK(!App().State().document->Dirty());
    };

    // The shortcut menu's Set Initial Conditions, then (the menu again)
    // Remove Initial Conditions on the entity; Ctrl+Z twice comes back to
    // what the file had, unmodified.
    t = IM_REGISTER_TEST(engine, "layout", "menu_initial_conditions_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK_SILENT(SelectEntity(ctx, found));
        const bool before = HasInitialConditions(ctx, found.entity);
        OpenMenuOn(ctx, found);
        ctx->ItemClick("//$FOCUSED/Set Initial Conditions");
        ctx->Yield(3);
        IM_CHECK(HasInitialConditions(ctx, found.entity));
        OpenMenuOn(ctx, found);
        ctx->ItemClick("//$FOCUSED/Remove Initial Conditions");
        ctx->Yield(3);
        IM_CHECK(!HasInitialConditions(ctx, found.entity));
        Undo(ctx);
        IM_CHECK(HasInitialConditions(ctx, found.entity));
        Undo(ctx);
        IM_CHECK(HasInitialConditions(ctx, found.entity) == before);
        IM_CHECK(!App().State().document->Dirty());
    };

    // Set Initial Conditions, the entity translated (X), the shortcut menu's
    // Restore Initial Conditions: back where it was set; Ctrl+Z three times
    // comes back to what the file had, unmodified.
    t = IM_REGISTER_TEST(engine, "layout", "menu_restore_initial_conditions_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK_SILENT(SelectEntity(ctx, found));
        const bool had = HasInitialConditions(ctx, found.entity);
        const VxVector before = Position(ctx, found.entity);
        OpenMenuOn(ctx, found);
        ctx->ItemClick("//$FOCUSED/Set Initial Conditions");
        ctx->Yield(3);
        ctx->KeyPress(ImGuiKey_X);
        ctx->Yield(2);
        DragFrom(ctx, found.at, 50.0f, 0.0f);
        const VxVector moved = Position(ctx, found.entity);
        IM_CHECK_SILENT(!Same(moved, before));
        ctx->KeyPress(ImGuiKey_A);
        ctx->Yield(2);
        // The entity is under the mouse where it was dragged to.
        const InView there = FindEntity(ctx, found.entity);
        IM_CHECK_SILENT(there.entity == found.entity);
        OpenMenuOn(ctx, there);
        ctx->ItemClick("//$FOCUSED/Restore Initial Conditions");
        ctx->Yield(3);
        IM_CHECK(Same(Position(ctx, found.entity), before));
        Undo(ctx);
        IM_CHECK(Same(Position(ctx, found.entity), moved));
        Undo(ctx);
        IM_CHECK(Same(Position(ctx, found.entity), before));
        Undo(ctx);
        IM_CHECK(HasInitialConditions(ctx, found.entity) == had);
        IM_CHECK(!App().State().document->Dirty());
    };

    // Everything selected (Ctrl+A), the shortcut menu's Create Scripts on
    // Selection: a new Script on each selected object that takes one, one
    // undo step; Ctrl+Z takes them all off, unmodified.
    t = IM_REGISTER_TEST(engine, "layout", "menu_create_scripts_on_selection_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK_SILENT(SelectEntity(ctx, found));
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->Yield(2);
        const std::vector<CK_ID> selected = App().State().objectSelection.objects;
        IM_CHECK_SILENT(selected.size() > 1);
        auto scripts = [&] {
            std::vector<int> counts;
            for (CK_ID id : selected) counts.push_back(ScriptCount(ctx, id));
            return counts;
        };
        const std::vector<int> before = scripts();
        OpenMenuOn(ctx, found);
        ctx->ItemClick("//$FOCUSED/Create Scripts on Selection");
        ctx->Yield(3);
        const std::vector<int> after = scripts();
        for (size_t i = 0; i < selected.size(); ++i)
            if (before[i] >= 0) IM_CHECK_EQ(after[i], before[i] + 1);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK(scripts() == before);
        IM_CHECK(!App().State().document->Dirty());
    };

    // A Script on the entity (Create Script on), the Schematic hidden
    // behind another tab; the shortcut menu's Show Script > the Script:
    // the Schematic in front, the Script shown. Ctrl+Z takes it off.
    t = IM_REGISTER_TEST(engine, "layout", "menu_show_script");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK_SILENT(SelectEntity(ctx, found));
        IM_CHECK_SILENT(ScriptCount(ctx, found.entity) == 0);
        EntityMenu menu = OpenMenuOn(ctx, found);
        ctx->SetRef("//$FOCUSED");
        ctx->MenuClick(("Create Script on/" + menu.script).c_str());
        ctx->Yield(3);
        CK_ID script = 0;
        std::string label;
        OnMainThread(ctx, [&] {
            CKBeObject *owner = Object<CKBeObject>(found.entity);
            CKBehavior *made = owner && owner->GetScriptCount() ? owner->GetScript(0) : nullptr;
            if (!made) return;
            script = made->GetID();
            // ContextMenuPopup: "'%s' on %s (%s)##show%d".
            label = "'" + vdev::DevObjectName(made) + "' on " + vdev::DevObjectName(owner) + " (" +
                    vdev::DevClassName(owner) + ")##show0";
        });
        IM_CHECK_SILENT(script != 0);
        // The Level Manager in front of the Schematic (their shared tab bar).
        ctx->WindowFocus("//Level Manager");
        ctx->Yield(3);
        IM_CHECK(App().Canvas().ShownScripts().empty());
        menu = OpenMenuOn(ctx, found);
        ctx->SetRef("//$FOCUSED");
        ctx->MenuClick("Show Script");
        ctx->ItemClick(("//###Menu_00/**/" + label).c_str());
        ctx->Yield(5);
        bool shown = false;
        for (const vdev::SchematicCanvas::ShownScript &each : App().Canvas().ShownScripts()) shown |= each.script == script;
        IM_CHECK(shown);
        Undo(ctx);
        IM_CHECK_EQ(ScriptCount(ctx, found.entity), 0);
        IM_CHECK(!App().State().document->Dirty());
    };

    // The shortcut menu's Select Camera > another camera: the view shows
    // it; the camera it had chosen again the same way.
    t = IM_REGISTER_TEST(engine, "layout", "menu_select_camera");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        ImVec2 min, max;
        IM_CHECK_SILENT(App().Layout().ShownView(min, max));
        const CK_ID before = CurrentCamera(ctx);
        const CK_ID other = OtherCamera(ctx);
        IM_CHECK_SILENT(before != 0 && other != 0);
        SelectCamera(ctx, Center(min, max), other);
        IM_CHECK_EQ(CurrentCamera(ctx), other);
        SelectCamera(ctx, Center(min, max), before);
        IM_CHECK_EQ(CurrentCamera(ctx), before);
    };

    // The shortcut menu's Duplicate: Dev's copy options (Edit Parameter),
    // OK (Enter): a copy of the entity, selected alone; Ctrl+Z takes it out,
    // unmodified.
    t = IM_REGISTER_TEST(engine, "layout", "menu_duplicate_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK_SILENT(SelectEntity(ctx, found));
        OpenMenuOn(ctx, found);
        ctx->ItemClick("//$FOCUSED/Duplicate");
        ctx->Yield(3);
        ImGuiWindow *dialog = ctx->GetWindowByRef("//###CopyOptions");
        IM_CHECK_SILENT(dialog && dialog->WasActive);
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        const std::vector<CK_ID> copies = OthersSelected(found.entity);
        IM_CHECK_EQ(copies.size(), 1u);
        IM_CHECK_EQ(App().State().objectSelection.objects.size(), 1u);
        IM_CHECK_SILENT(!copies.empty());
        const CK_ID copy = copies.front();
        // CK's copies are named after their original ("Ground.Copy").
        IM_CHECK(ObjectName(ctx, copy).rfind(found.name, 0) == 0);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK(!ObjectExists(ctx, copy) || App().State().document->IsDiscarded(copy));
        IM_CHECK(!App().State().document->Dirty());
    };

    // Del on the selected entity: the Level Manager's Select Delete Options,
    // OK (Enter) destroys it, out of the selection, and clears the undo
    // steps (Dev). Last: the entity is gone.
    t = IM_REGISTER_TEST(engine, "layout", "delete");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        InView found;
        IM_CHECK_SILENT(SelectEntity(ctx, found));
        vdev::EditorState &state = App().State();
        IM_CHECK_EQ(state.objectSelection.objects.size(), 1u);
        const size_t logged = state.log.size();
        ctx->KeyPress(ImGuiKey_Delete);
        ctx->Yield(3);
        ImGuiWindow *dialog = ctx->GetWindowByRef("//###DeleteOptions");
        IM_CHECK_SILENT(dialog && dialog->WasActive);
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        LogEditor(ctx, logged);
        IM_CHECK(!ObjectExists(ctx, found.entity));
        IM_CHECK(!state.objectSelection.Contains(found.entity));
        IM_CHECK(!state.History().CanUndo());
        IM_CHECK(state.document->Dirty());
    };
}

} // namespace vdevuitest
