// "setup": the 3D Object Setup on the sample opened from the command line,
// opened from the Level Manager (a 3D object's row, Enter): the Position
// typed in (Ctrl+Z puts it back), Set IC / Remove IC (each undone), another
// object chosen in the Name combo; the Camera, Light, Material and Curve
// Setups (a value typed in, a choice made, each undone); a Script's Setup's
// Show in Schematic.

#include <cmath>
#include <cstring>

#include "core/level/ObjectProperties.h"
#include "core/level/SceneCommands.h"
#include "core/level/SetupCommands.h"
#include "core/runtime/GridCompat.h"
#include "core/runtime/TextEncoding.h"
#include "tests/ui/UiTests.h"

namespace vdev {
struct SetupEditTests {
    static void ChooseGridLayers(EditorApp &app, CK_ID grid) {
        auto &setups = const_cast<SetupWindows &>(app.Setups());
        for (auto &setup : setups.m_Doc.setups)
            if (setup.object == grid)
                setup.layers = {0, 1};
    }
    static bool BeginRenameAndPick(EditorApp &app, ObjectRef target) {
        auto &windows = const_cast<SetupWindows &>(app.Setups());
        for (auto &setup : windows.m_Doc.setups)
            if (setup.reference == target) {
                windows.m_PickSetup = target;
                windows.m_PickSetupSlot = setup.slot;
                setup.meshRows.insert({target.document, 12345, 1});
                return windows.m_Workspace.Begin("object.name", target, setup.renameEdit, setup.nameOwner);
            }
        return false;
    }
    static bool CleanTarget(EditorApp &app, ObjectRef target) {
        auto &windows = const_cast<SetupWindows &>(app.Setups());
        for (const auto &setup : windows.m_Doc.setups)
            if (setup.reference == target)
                return !setup.renameEdit && setup.meshRows.empty() && !windows.m_PickSetup;
        return false;
    }
    static bool NoPicker(EditorApp &app) {
        const auto &windows = app.Setups();
        return !windows.m_PickSetup && !windows.m_PortalPlace;
    }
};
} // namespace vdev

namespace vdevuitest {

namespace {

constexpr const char *SETUP = "//###setup3d"; // SetupWindows::DrawSetup's id of the 3D Object Setup

bool SetupShown(ImGuiTestContext *ctx) {
    ImGuiWindow *window = ctx->GetWindowByRef(SETUP);
    return window && window->WasActive;
}

// The Level Manager in front, a 3D object's row clicked and Enter: its Setup.
std::optional<LevelRow> OpenSetup(ImGuiTestContext *ctx) {
    Settle(ctx, SceneRef("level", "").c_str());
    std::optional<LevelRow> row = ClickLevelRow(ctx, CKCID_3DOBJECT, true);
    if (!row) return row;
    ctx->KeyPress(ImGuiKey_Enter);
    ctx->Yield(3);
    return row;
}

bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

} // namespace

void RegisterSetupTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    t = IM_REGISTER_TEST(engine, "setup", "window_reuse_duplicate_and_picker_cleanup");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State();
        vdev::ObjectRef first{}, second{}, third{};
        size_t steps = 0;
        uint64_t selection = 0;
        bool dirty = false;
        const bool showNew = state.preferences.Bool("3D Layout\\Show New Setups");
        const auto setNew = [&](bool enabled) {
            const vdev::EditorClient client(state.workspace->Application().Host(), 0);
            const auto source = client.Query("preferences.values");
            const EditorValue field{EDITOR_TEXT("3D Layout\\Show New Setups"), EDITOR_INTEGER_VALUE, {}, {}, enabled};
            EditorValue value{EDITOR_TEXT("value"), EDITOR_RECORD};
            value.fields = {&field, 1};
            const auto result = client.Command(
                "preferences.apply",
                {vdev::IntegerArgument("revision", vdev::EditorClient::Integer(source, "revision")), value});
            IM_CHECK_NO_RET(result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE);
        };
        OnMainThread(ctx, [&] {
            const_cast<vdev::SetupWindows &>(App().Setups()).CloseAll();
            const auto create = [&](const char *name) {
                auto *object = state.document->Context()->CreateObject(CKCID_3DOBJECT, name);
                state.document->Context()->GetCurrentLevel()->AddObject(object);
                state.document->Include(object->GetID());
                return state.document->Reference(object->GetID());
            };
            first = create("First Setup lifetime");
            second = create("Second Setup lifetime");
            third = create("Third Setup lifetime");
            steps = state.History().UndoCount();
            selection = state.objectSelection.Revision();
            dirty = state.document->Dirty();
            setNew(false);
            state.events.Post(vdev::ShowObjectSetup{first});
        });
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(vdev::SetupEditTests::BeginRenameAndPick(App(), first));
            state.events.Post(vdev::ShowObjectSetup{second});
        });
        ctx->Yield(3);
        IM_CHECK(!ShowsObject(first.object) && ShowsObject(second.object));
        IM_CHECK(Read<bool>(ctx, [&] {
            return vdev::SetupEditTests::CleanTarget(App(), second) && !state.workspace->Application().HasEdits();
        }));
        OnMainThread(ctx, [&] {
            setNew(true);
            state.events.Post(vdev::ShowObjectSetup{third});
        });
        ctx->Yield(3);
        IM_CHECK(ShowsObject(second.object) && ShowsObject(third.object));
        IM_CHECK(App().Setups().OpenSetups().size() == 2);
        OnMainThread(ctx, [&] { state.events.Post(vdev::ShowObjectSetup{third}); });
        ctx->Yield(3);
        IM_CHECK(App().Setups().OpenSetups().size() == 2);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(vdev::SetupEditTests::BeginRenameAndPick(App(), third));
            const_cast<vdev::SetupWindows &>(App().Setups()).CloseAll();
            IM_CHECK_NO_RET(vdev::SetupEditTests::NoPicker(App()) && !state.workspace->Application().HasEdits());
            IM_CHECK_NO_RET(state.History().UndoCount() == steps && state.objectSelection.Revision() == selection &&
                            state.document->Dirty() == dirty);
            setNew(showNew);
        });
    };

    t = IM_REGISTER_TEST(engine, "propertyedits", "position_drag_cancel_undo_redo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const auto row = OpenSetup(ctx);
        IM_CHECK_SILENT(row.has_value() && SetupShown(ctx));
        const VxVector before = Position(ctx, row->object);
        const size_t count = App().State().History().UndoCount();
        ctx->SetRef(SETUP);
        const ImVec2 at = ctx->ItemInfo("**/##v").RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos(ImVec2(at.x + 40.0f, at.y));
        IM_CHECK(!Near(Position(ctx, row->object).x, before.x));
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        const VxVector cancelled = Position(ctx, row->object);
        IM_CHECK(Near(cancelled.x, before.x) && Near(cancelled.y, before.y) && Near(cancelled.z, before.z));
        IM_CHECK(App().State().History().UndoCount() == count && !App().State().document->Dirty());
        ctx->ItemInputValue("**/##v", before.x + 12.5f);
        ctx->Yield(3);
        IM_CHECK(Near(Position(ctx, row->object).x, before.x + 12.5f));
        IM_CHECK(App().State().History().UndoCount() == count + 1);
        Undo(ctx);
        IM_CHECK(Near(Position(ctx, row->object).x, before.x) && !App().State().document->Dirty());
        Redo(ctx);
        IM_CHECK(Near(Position(ctx, row->object).x, before.x + 12.5f));
        Undo(ctx);
    };

    t = IM_REGISTER_TEST(engine, "propertyedits", "close_during_preview_restores_object");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const auto row = OpenSetup(ctx);
        IM_CHECK_SILENT(row.has_value() && SetupShown(ctx));
        const auto before = Position(ctx, row->object);
        const auto count = App().State().History().UndoCount();
        ctx->SetRef(SETUP);
        const auto at = ctx->ItemInfo("**/##v").RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 35, at.y});
        IM_CHECK(!Near(Position(ctx, row->object).x, before.x));
        OnMainThread(ctx, [] { const_cast<vdev::SetupWindows &>(App().Setups()).CloseAll(); });
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(Near(Position(ctx, row->object).x, before.x));
        IM_CHECK(App().State().History().UndoCount() == count && !App().State().document->Dirty());
        IM_CHECK(Read<bool>(ctx, [] { return !App().State().workspace->Application().HasEdits(); }));
    };
    t = IM_REGISTER_TEST(engine, "propertyedits", "switch_object_during_preview_restores_original");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const auto row = OpenSetup(ctx);
        IM_CHECK_SILENT(row.has_value() && SetupShown(ctx));
        const auto before = Position(ctx, row->object);
        const auto count = App().State().History().UndoCount();
        ctx->SetRef(SETUP);
        const auto at = ctx->ItemInfo("**/##v").RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 35, at.y});
        IM_CHECK(!Near(Position(ctx, row->object).x, before.x));
        const auto other = Read<CK_ID>(ctx, [] {
            auto &state = App().State();
            auto *object = state.document->Context()->CreateObject(CKCID_3DOBJECT, "Next Setup object");
            state.document->Context()->GetCurrentLevel()->AddObject(object);
            state.document->Include(object->GetID());
            state.preferences.SetBool("3D Layout\\Show New Setups", false);
            state.events.Post(vdev::ShowObjectSetup{state.document->Reference(object->GetID())});
            return object->GetID();
        });
        ctx->Yield(3);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(ShowsObject(other) && !ShowsObject(row->object));
        IM_CHECK(Near(Position(ctx, row->object).x, before.x));
        IM_CHECK(App().State().History().UndoCount() == count && !App().State().document->Dirty());
    };

    t = IM_REGISTER_TEST(engine, "propertyedits", "sprite_text_long_input_cancel_and_font_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const auto id = Read<CK_ID>(ctx, [] {
            auto &doc = *App().State().document;
            auto *text = CKSpriteText::Cast(doc.Context()->CreateObject(CKCID_SPRITETEXT, "Text editing"));
            if (!text || !text->Create(128, 64, 32, 0))
                return CK_ID(0);
            text->SetText("Original");
            text->SetFont("Arial", 12, 400, FALSE, FALSE);
            doc.Context()->GetCurrentLevel()->AddObject(text);
            doc.Include(text->GetID());
            return text->GetID();
        });
        IM_CHECK_SILENT(id && ShowSetup(ctx, id, "//###setupspritetext"));
        const auto value = [id] {
            auto *text = Object<CKSpriteText>(id);
            return std::string(text->GetText() ? text->GetText() : "");
        };
        const auto size = [id] {
            vdev::SpriteTextFont font;
            vdev::ReadSpriteTextFont(Object<CKSpriteText>(id), &font);
            return font.size;
        };
        const auto count = App().State().History().UndoCount();
        const std::string longText(4096, 'x');
        ctx->ItemClick("**/##text");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars(longText.c_str());
        ctx->Yield(2);
        IM_CHECK_EQ(Read<std::string>(ctx, value).size(), longText.size());
        IM_CHECK(App().State().History().UndoCount() == count);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(Read<std::string>(ctx, value) == "Original");
        IM_CHECK(App().State().History().UndoCount() == count);
        const auto before = Read<int>(ctx, size);
        ctx->ItemInputValue("**/Size", before + 8);
        ctx->Yield(3);
        IM_CHECK_EQ(Read<int>(ctx, size), before + 8);
        IM_CHECK(App().State().History().UndoCount() == count + 1);
        Undo(ctx);
        IM_CHECK_EQ(Read<int>(ctx, size), before);
    };
    t = IM_REGISTER_TEST(engine, "propertyedits", "level_environment_drag_cancel_and_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const auto level = Read<CK_ID>(ctx, [] {
            auto *level = App().State().document->Context()->GetCurrentLevel();
            level->GetLevelScene()->SetFogMode(VXFOG_LINEAR);
            return level->GetID();
        });
        IM_CHECK_SILENT(ShowSetup(ctx, level, "//###setupscene"));
        const auto fog = [] {
            return App().State().document->Context()->GetCurrentLevel()->GetLevelScene()->GetFogStart();
        };
        const auto before = Read<float>(ctx, fog);
        const auto count = App().State().History().UndoCount();
        const auto at = ctx->ItemInfo("**/Fog Start:/##value").RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 40, at.y});
        ctx->Yield(3);
        IM_CHECK(!Near(Read<float>(ctx, fog), before));
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(Near(Read<float>(ctx, fog), before) && App().State().History().UndoCount() == count);
        ctx->ItemInputValue("**/Fog Start:/##value", before + 25);
        ctx->Yield(3);
        IM_CHECK(Near(Read<float>(ctx, fog), before + 25));
        IM_CHECK(App().State().History().UndoCount() == count + 1);
        Undo(ctx);
        IM_CHECK(Near(Read<float>(ctx, fog), before));
    };
    t = IM_REGISTER_TEST(engine, "propertyedits", "scene_color_text_cancel_has_no_late_write");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const auto level =
            Read<CK_ID>(ctx, [] { return App().State().document->Context()->GetCurrentLevel()->GetID(); });
        IM_CHECK_SILENT(ShowSetup(ctx, level, "//###setupscene"));
        const auto color = [] {
            return vdev::ReadSceneEnvironment(App().State().document->Context()->GetCurrentLevel()->GetLevelScene());
        };
        const auto before = Read<vdev::SceneEnvironment>(ctx, color);
        const auto count = App().State().History().UndoCount();
        const auto swatch = Read<ImGuiID>(ctx, [] {
            auto *root = ImGui::FindWindowByName("###setupscene");
            if (root)
                for (auto *child : root->DC.ChildWindows)
                    if (std::strstr(child->Name, "page"))
                        return ImHashStr("##swatch", 0, ImHashStr("Ambient", 0, child->IDStack[0]));
            return ImGuiID(0);
        });
        IM_CHECK(swatch != 0);
        ctx->ItemClick(swatch);
        ctx->Yield(2);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("**/##picker/##hex/##Text");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars("7F3344");
        ctx->Yield(2);
        IM_CHECK(!(Read<vdev::SceneEnvironment>(ctx, color) == before));
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(4);
        IM_CHECK(Read<vdev::SceneEnvironment>(ctx, color) == before);
        IM_CHECK(App().State().History().UndoCount() == count && !App().State().document->Dirty());
    };

    t = IM_REGISTER_TEST(engine, "propertyedits", "grid_multilayer_stroke_cancel_and_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const auto id = Read<CK_ID>(ctx, [] {
            auto &doc = *App().State().document;
            auto *grid = CKGrid::Cast(doc.Context()->CreateObject(CKCID_GRID, "Paint Grid"));
            if (!grid)
                return CK_ID(0);
            grid->SetDimensions(4, 4, 4, 4);
            doc.Context()->GetCurrentLevel()->AddObject(grid);
            doc.Include(grid->GetID());
            const auto first = vdev::RegisterGridLayerType(doc, "Paint first"),
                       second = vdev::RegisterGridLayerType(doc, "Paint second");
            if (!first.ok || !second.ok || !vdev::AddGridLayer(doc, grid->GetID(), int(first.created)).ok ||
                !vdev::AddGridLayer(doc, grid->GetID(), int(second.created)).ok)
                return CK_ID(0);
            return grid->GetID();
        });
        IM_CHECK_SILENT(id && ShowSetup(ctx, id, "//###setupgrid"));
        OnMainThread(ctx, [=] { vdev::SetupEditTests::ChooseGridLayers(App(), id); });
        ctx->Yield(2);
        const auto sum = [id] {
            auto *grid = Object<CKGrid>(id);
            int total = 0;
            for (int l = 0; l < grid->GetLayerCount(); ++l)
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x) {
                        int v = 0;
                        grid->GetLayerByIndex(l)->GetValue2(x, y, &v);
                        total += v;
                    }
            return total;
        };
        const auto count = App().State().History().UndoCount();
        const auto rect = ctx->ItemInfo("**/##squares").RectFull;
        const ImVec2 at(rect.Min.x + 30, rect.Min.y + 30);
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 105, at.y});
        ctx->Yield(3);
        IM_CHECK(Read<int>(ctx, sum) > 0);
        IM_CHECK(App().State().History().UndoCount() == count);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(Read<int>(ctx, sum), 0);
        IM_CHECK(App().State().History().UndoCount() == count);
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 105, at.y});
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(Read<int>(ctx, sum) > 0);
        IM_CHECK(App().State().History().UndoCount() == count + 1);
        Undo(ctx);
        IM_CHECK_EQ(Read<int>(ctx, sum), 0);
        Redo(ctx);
        IM_CHECK(Read<int>(ctx, sum) > 0);
        Undo(ctx);
    };

    t = IM_REGISTER_TEST(engine, "setup", "sprite_text_alignment_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const CK_ID text = Read<CK_ID>(ctx, [] {
            CKContext *context = App().State().host.Context();
            auto *sprite = CKSpriteText::Cast(context->CreateObject(CKCID_SPRITETEXT, (CKSTRING) "Alignment Test"));
            if (!sprite || !sprite->Create(128, 64, 32, 0)) return CK_ID(0);
            sprite->SetText((CKSTRING) "Alignment");
            sprite->SetAlign(static_cast<CKSPRITETEXT_ALIGNMENT>(CKSPRITETEXT_LEFT | CKSPRITETEXT_TOP));
            context->GetCurrentLevel()->AddObject(sprite);
            App().State().document->Include(sprite->GetID());
            return sprite->GetID();
        });
        IM_CHECK_SILENT(text && ShowSetup(ctx, text, "//###setupspritetext"));
        auto alignment = [text] { return vdev::properties::SpriteTextAlignment.get(Object<CKSpriteText>(text)); };
        IM_CHECK_EQ(Read<int>(ctx, alignment), CKSPRITETEXT_LEFT | CKSPRITETEXT_TOP);
        // ImGui's RadioButton does not report Checked to the test engine.
        // Clicking the selected choices must leave the value and history alone.
        ctx->ItemClick("**/Left");
        ctx->ItemClick("**/Top");
        ctx->Yield(3);
        IM_CHECK_EQ(Read<int>(ctx, alignment), CKSPRITETEXT_LEFT | CKSPRITETEXT_TOP);
        IM_CHECK(!App().State().History().CanUndo());
        ctx->ItemClick("**/Right");
        ctx->Yield(3);
        IM_CHECK_EQ(Read<int>(ctx, alignment), CKSPRITETEXT_RIGHT | CKSPRITETEXT_TOP);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK_EQ(Read<int>(ctx, alignment), CKSPRITETEXT_LEFT | CKSPRITETEXT_TOP);
        IM_CHECK(!App().State().document->Dirty());
        Redo(ctx);
        ctx->ItemClick("**/Bottom");
        ctx->Yield(3);
        IM_CHECK_EQ(Read<int>(ctx, alignment), CKSPRITETEXT_RIGHT | CKSPRITETEXT_BOTTOM);
        Undo(ctx);
        IM_CHECK_EQ(Read<int>(ctx, alignment), CKSPRITETEXT_RIGHT | CKSPRITETEXT_TOP);
        Undo(ctx);
        IM_CHECK(!App().State().document->Dirty());
    };

    // Enter on a 3D object's Level Manager row opens its 3D Object Setup.
    t = IM_REGISTER_TEST(engine, "setup", "open_from_level_manager");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const std::optional<LevelRow> row = OpenSetup(ctx);
        IM_CHECK_SILENT(row.has_value());
        IM_CHECK(SetupShown(ctx));
        IM_CHECK(ShowsObject(row->object));
    };

    // The Position's X typed in: the object moves there, one undo step, the
    // composition modified; Ctrl+Z puts it back, unmodified.
    t = IM_REGISTER_TEST(engine, "setup", "position_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const std::optional<LevelRow> row = OpenSetup(ctx);
        IM_CHECK_SILENT(row.has_value() && SetupShown(ctx));
        vdev::EditorState &state = App().State();
        const VxVector before = Position(ctx, row->object);
        const float x = before.x + 12.5f;
        ctx->SetRef(SETUP);
        // The page's first "##v": the Position's X (Entity3DPage).
        ctx->ItemInputValue("**/##v", x);
        ctx->Yield(3);
        const VxVector typed = Position(ctx, row->object);
        IM_CHECK(std::fabs(typed.x - x) < 1e-3f);
        IM_CHECK(std::fabs(typed.y - before.y) < 1e-3f && std::fabs(typed.z - before.z) < 1e-3f);
        IM_CHECK(state.History().CanUndo());
        IM_CHECK(state.document->Dirty());
        Undo(ctx);
        IM_CHECK(std::fabs(Position(ctx, row->object).x - before.x) < 1e-3f);
        IM_CHECK(!state.document->Dirty());
    };

    // Set IC keeps the object's initial conditions in the scene, Remove IC
    // drops them; Ctrl+Z twice comes back to what the file had.
    t = IM_REGISTER_TEST(engine, "setup", "initial_conditions_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const std::optional<LevelRow> row = OpenSetup(ctx);
        IM_CHECK_SILENT(row.has_value() && SetupShown(ctx));
        const bool before = HasInitialConditions(ctx, row->object);
        ctx->SetRef(SETUP);
        ctx->ItemClick("**/Set IC");
        ctx->Yield(3);
        IM_CHECK(HasInitialConditions(ctx, row->object));
        ctx->ItemClick("**/Remove IC");
        ctx->Yield(3);
        IM_CHECK(!HasInitialConditions(ctx, row->object));
        Undo(ctx);
        IM_CHECK(HasInitialConditions(ctx, row->object));
        Undo(ctx);
        IM_CHECK(HasInitialConditions(ctx, row->object) == before);
        IM_CHECK(!App().State().document->Dirty());
    };

    // The Name combo lists the objects of the Setup's class: another one
    // chosen, the Setup shows it. The class: the first of these with two
    // objects of their own (the combo's list, ObjectsOf), the Setup opened
    // as the Level Manager's Enter does (ShowObjectSetup).
    t = IM_REGISTER_TEST(engine, "setup", "name_combo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        static const std::pair<CK_CLASSID, const char *> CLASSES[] = {{CKCID_MATERIAL, "//###setupmaterial"},
                                                                      {CKCID_MESH, "//###setupmesh"},
                                                                      {CKCID_TEXTURE, "//###setuptexture"},
                                                                      {CKCID_3DOBJECT, SETUP}};
        CK_ID first = 0, other = 0;
        std::string name;
        const char *window = nullptr;
        OnMainThread(ctx, [&] {
            CKContext *context = App().State().host.Context();
            for (const auto &cls : CLASSES) {
                first = other = 0;
                const XObjectPointerArray &list = context->GetObjectListByType(cls.first, FALSE);
                for (int i = 0; i < list.Size() && !other; ++i) {
                    CKObject *object = list[i];
                    // A plain name the combo shows as it is (no '/', which a path splits on).
                    if (!object || object->IsPrivate() || object->IsDynamic() || !object->GetName() || !*object->GetName() ||
                        std::strchr(object->GetName(), '/') || App().State().document->IsDiscarded(object->GetID()))
                        continue;
                    if (!first) {
                        first = object->GetID();
                    } else if (std::strcmp(object->GetName(), context->GetObject(first)->GetName()) != 0) {
                        other = object->GetID();
                        name = vdev::AnsiToUtf8(object->GetName());
                    }
                }
                if (other) {
                    window = cls.second;
                    App().State().events.Post(vdev::ShowObjectSetup{App().State().document->Reference(first)});
                    return;
                }
            }
        });
        IM_CHECK_SILENT(other != 0);
        ctx->Yield(3);
        ImGuiWindow *setup = ctx->GetWindowByRef(window);
        IM_CHECK_SILENT(setup && setup->WasActive);
        IM_CHECK(ShowsObject(first));
        ctx->LogInfo("%s: choosing '%s'", window, name.c_str());
        ctx->SetRef(window);
        ctx->ComboClick(("##name/" + name).c_str());
        ctx->Yield(3);
        IM_CHECK(ShowsObject(other));
        IM_CHECK(!ShowsObject(first));
    };

    // The Camera Setup: the Field Of View typed in (degrees), then the
    // projection made Orthographic; Ctrl+Z gives each back, unmodified.
    t = IM_REGISTER_TEST(engine, "setup", "camera_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CK_ID camera = OpenSetupOf(ctx, CKCID_CAMERA, "//###setupcamera");
        IM_CHECK_SILENT(camera != 0);
        auto fov = [camera] { return Object<CKCamera>(camera)->GetFov() * 57.29577951f; }; // degrees, as the page
        auto projection = [camera] { return Object<CKCamera>(camera)->GetProjectionType(); };
        const float before = Read<float>(ctx, fov);
        IM_CHECK_SILENT(Read<int>(ctx, projection) == CK_PERSPECTIVEPROJECTION);
        const float typed = before > 60.0f ? 40.0f : 70.0f;
        ctx->ItemInputValue("**/##fov", typed);
        ctx->Yield(3);
        IM_CHECK(std::fabs(Read<float>(ctx, fov) - typed) < 0.01f);
        IM_CHECK(App().State().History().CanUndo());
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK(std::fabs(Read<float>(ctx, fov) - before) < 0.01f);
        IM_CHECK(!App().State().document->Dirty());
        ctx->ItemClick("**/Orthographic");
        ctx->Yield(3);
        IM_CHECK_EQ(Read<int>(ctx, projection), (int)CK_ORTHOGRAPHICPROJECTION);
        Undo(ctx);
        IM_CHECK_EQ(Read<int>(ctx, projection), (int)CK_PERSPECTIVEPROJECTION);
        IM_CHECK(!App().State().document->Dirty());
    };

    // The Light Setup: the Range typed in, then the Type made Spot (another
    // when it is one); Ctrl+Z gives each back, unmodified.
    t = IM_REGISTER_TEST(engine, "setup", "light_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CK_ID light = OpenSetupOf(ctx, CKCID_LIGHT, "//###setuplight");
        IM_CHECK_SILENT(light != 0);
        auto range = [light] { return Object<CKLight>(light)->GetRange(); };
        auto type = [light] { return static_cast<int>(Object<CKLight>(light)->GetType()); };
        const float before = Read<float>(ctx, range);
        const float typed = before + 25.0f;
        ctx->ItemInputValue("**/##range", typed);
        ctx->Yield(3);
        IM_CHECK(std::fabs(Read<float>(ctx, range) - typed) < 0.05f);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK(std::fabs(Read<float>(ctx, range) - before) < 0.05f);
        IM_CHECK(!App().State().document->Dirty());
        const int kind = Read<int>(ctx, type);
        const bool spot = kind == VX_LIGHTSPOT;
        ChooseInCombo(ctx, "//###setuplight", "##type", spot ? "Point" : "Spot");
        IM_CHECK_EQ(Read<int>(ctx, type), spot ? (int)VX_LIGHTPOINT : (int)VX_LIGHTSPOT);
        Undo(ctx);
        IM_CHECK_EQ(Read<int>(ctx, type), kind);
        IM_CHECK(!App().State().document->Dirty());
    };

    // The Material Setup: the Power typed in, then the Mode made
    // Transparent (Opaque when it is); Ctrl+Z gives each back, unmodified.
    t = IM_REGISTER_TEST(engine, "setup", "material_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CK_ID material = OpenSetupOf(ctx, CKCID_MATERIAL, "//###setupmaterial");
        IM_CHECK_SILENT(material != 0);
        auto power = [material] { return Object<CKMaterial>(material)->GetPower(); };
        auto blend = [material] { return Object<CKMaterial>(material)->AlphaBlendEnabled() != 0; };
        const float before = Read<float>(ctx, power);
        const float typed = before > 50.0f ? 10.0f : 60.0f;
        ctx->ItemInputValue("**/##power", typed);
        ctx->Yield(3);
        IM_CHECK(Near(Read<float>(ctx, power), typed));
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK(Near(Read<float>(ctx, power), before));
        IM_CHECK(!App().State().document->Dirty());
        const bool blended = Read<bool>(ctx, blend);
        ChooseInCombo(ctx, "//###setupmaterial", "##mode", blended ? "Opaque" : "Transparent");
        IM_CHECK(Read<bool>(ctx, blend) != blended);
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, blend) == blended);
        IM_CHECK(!App().State().document->Dirty());
    };

    // The Curve Setup: Close (Open when it is closed), then Linear (Spline
    // when it is); Ctrl+Z gives each back, unmodified.
    t = IM_REGISTER_TEST(engine, "setup", "curve_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CK_ID curve = OpenSetupOf(ctx, CKCID_CURVE, "//###setupcurve");
        IM_CHECK_SILENT(curve != 0);
        auto open = [curve] { return Object<CKCurve>(curve)->IsOpen() != 0; };
        auto linear = [curve] {
            CKCurve *c = Object<CKCurve>(curve);
            for (int i = 0; i < c->GetControlPointCount(); ++i)
                if (CKCurvePoint *point = c->GetControlPoint(i))
                    if (!point->IsLinear()) return false;
            return c->GetControlPointCount() > 0;
        };
        const bool wasOpen = Read<bool>(ctx, open);
        ctx->ItemClick(wasOpen ? "**/Close" : "**/Open");
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, open) != wasOpen);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, open) == wasOpen);
        IM_CHECK(!App().State().document->Dirty());
        const bool wasLinear = Read<bool>(ctx, linear);
        ctx->ItemClick(wasLinear ? "**/Spline" : "**/Linear");
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, linear) != wasLinear);
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, linear) == wasLinear);
        IM_CHECK(!App().State().document->Dirty());
    };

    // The Mesh Setup: the Lit Mode changed (Lit and Prelit), then the
    // Texture Wrap; Ctrl+Z gives each back, unmodified.
    t = IM_REGISTER_TEST(engine, "setup", "mesh_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const char *ref = "//###setupmesh";
        const CK_ID mesh = OpenSetupOf(ctx, CKCID_MESH, ref);
        IM_CHECK_SILENT(mesh != 0);
        auto lit = [mesh] { return static_cast<int>(Object<CKMesh>(mesh)->GetLitMode()); };
        auto wrap = [mesh] { return static_cast<int>(Object<CKMesh>(mesh)->GetWrapMode()); };
        const int litBefore = Read<int>(ctx, lit);
        ChooseInCombo(ctx, ref, "Lit Mode", litBefore == VX_LITMESH ? "Prelit" : "Lit");
        IM_CHECK(Read<int>(ctx, lit) != litBefore);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK_EQ(Read<int>(ctx, lit), litBefore);
        IM_CHECK(!App().State().document->Dirty());
        const int wrapBefore = Read<int>(ctx, wrap);
        ChooseInCombo(ctx, ref, "Texture Wrap", wrapBefore == VXTEXTUREWRAP_U ? "V" : "U");
        IM_CHECK(Read<int>(ctx, wrap) != wrapBefore);
        Undo(ctx);
        IM_CHECK_EQ(Read<int>(ctx, wrap), wrapBefore);
        IM_CHECK(!App().State().document->Dirty());
    };

    // The header's picker (Dev.exe sub_4436E0 20): a new 3D object in the
    // level; the picker clicked, then that object's row in the Level
    // Manager: the 3D Object Setup shows it.
    t = IM_REGISTER_TEST(engine, "setup", "header_picker");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CK_ID entity = OpenSetupOf(ctx, CKCID_3DOBJECT, SETUP);
        IM_CHECK_SILENT(entity != 0);
        CK_ID target = 0;
        OnMainThread(ctx, [&] {
            CKContext *context = App().State().host.Context();
            CK3dObject *made = CK3dObject::Cast(context->CreateObject(CKCID_3DOBJECT, (CKSTRING) "Setup Pick Target"));
            if (CKLevel *level = context->GetCurrentLevel()) level->AddObject(made);
            target = made->GetID();
            App().State().document->MarkTopologyChanged();
        });
        ctx->Yield(3);
        // The target's row shown first (its folders opened: a click on an
        // object's arrow while picking would pick that object).
        Settle(ctx, SceneRef("level", "").c_str());
        IM_CHECK_SILENT(FindLevelRow(ctx, CKCID_3DOBJECT).has_value());
        auto targetRow = [&] {
            std::optional<LevelRow> found;
            for (const LevelRow &shown : App().LevelRows())
                if (shown.object == target) found = shown;
            return found;
        };
        IM_CHECK_SILENT(targetRow().has_value());
        Settle(ctx, SETUP);
        ctx->ItemClick((std::string(SETUP) + "/picker/##bitmap").c_str());
        ctx->Yield(2);
        Settle(ctx, SceneRef("level", "").c_str());
        ctx->Yield(2);
        const std::optional<LevelRow> row = targetRow();
        IM_CHECK_SILENT(row.has_value());
        ctx->MouseMoveToPos(ImVec2(row->arrowMax.x > 0.0f ? row->arrowMax.x + 40.0f : row->min.x + 120.0f,
                                   (row->min.y + row->max.y) * 0.5f));
        ctx->MouseClick(0);
        ctx->Yield(3);
        bool shown = false, kept = false;
        for (const vdev::SetupWindows::OpenSetup &setup : App().Setups().OpenSetups()) {
            shown = shown || setup.object.object == target;
            kept = kept || setup.object.object == entity;
        }
        IM_CHECK(shown && !kept);
        OnMainThread(ctx, [&] {
            auto&document=*App().State().document;
            IM_CHECK_NO_RET(vdev::DestroyLevelObjects(document,{target}).ok);
            document.MarkSaved();document.History().MarkSavePoint();
        });
        ctx->Yield(3);
    };

    // The type-ahead in a Setup's list: three meshes whose names start with
    // Q added to a 3D object; the third one clicked in Object Meshes (it is
    // selected, as in Dev the current mesh unchanged); Q typed: the highlight
    // on the first one (wrapping); B after it ("QB"): on the second one;
    // Space (a letter while typing) chooses nothing; Enter selects it; its
    // menu's Set As Current: it is current (one undo step); Q, Escape:
    // nothing changes. Remove Mesh(es): it is off the object (one undo step).
    // Ctrl+Z gives each back.
    t = IM_REGISTER_TEST(engine, "setup", "mesh_list_type_ahead");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CK_ID entity = OpenSetupOf(ctx, CKCID_3DOBJECT, SETUP);
        IM_CHECK_SILENT(entity != 0);
        const bool dirty = App().State().document->Dirty();
        CK_ID before = 0, first = 0, second = 0, third = 0;
        OnMainThread(ctx, [&] {
            CKContext *context = App().State().host.Context();
            CK3dEntity *object = Object<CK3dEntity>(entity);
            before = object->GetCurrentMesh() ? object->GetCurrentMesh()->GetID() : 0;
            CKMesh *a = CKMesh::Cast(context->CreateObject(CKCID_MESH, (CKSTRING) "Qa TypeAhead"));
            CKMesh *b = CKMesh::Cast(context->CreateObject(CKCID_MESH, (CKSTRING) "Qb TypeAhead"));
            CKMesh *c = CKMesh::Cast(context->CreateObject(CKCID_MESH, (CKSTRING) "Qc TypeAhead"));
            object->AddMesh(a);
            object->AddMesh(b);
            object->AddMesh(c);
            first = a->GetID();
            second = b->GetID();
            third = c->GetID();
        });
        auto current = [entity] {
            CKMesh *mesh = Object<CK3dEntity>(entity)->GetCurrentMesh();
            return mesh ? mesh->GetID() : 0;
        };
        ctx->Yield(3);
        // The highlight: the item with the keyboard focus, shown.
        auto highlighted = [ctx](const char *name) {
            ImGuiContext &g = *ctx->UiContext;
            const ImGuiTestItemInfo info = ctx->ItemInfo((std::string("**/") + name + " (Mesh)").c_str());
            return info.ID != 0 && g.NavId == info.ID && g.NavCursorVisible;
        };
        const size_t steps = App().State().History().UndoCount();
        ctx->ItemClick("**/Qc TypeAhead (Mesh)");
        ctx->Yield(3);
        IM_CHECK_EQ(Read<CK_ID>(ctx, current), before);
        ctx->KeyChars("q");
        ctx->Yield(3);
        IM_CHECK(highlighted("Qa TypeAhead"));
        ctx->KeyChars("b");
        ctx->Yield(3);
        IM_CHECK(highlighted("Qb TypeAhead"));
        // Space while typing: one of the letters, not a choice.
        ctx->KeyPress(ImGuiKey_Space);
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), steps);
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(3);
        IM_CHECK_EQ(Read<CK_ID>(ctx, current), before);
        IM_CHECK_EQ(App().State().History().UndoCount(), steps);
        // The menu of the mesh selected: Set As Current.
        ctx->ItemClick("**/Qb TypeAhead (Mesh)", ImGuiMouseButton_Right);
        ctx->Yield(2);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("Set As Current");
        ctx->SetRef(SETUP);
        ctx->Yield(3);
        IM_CHECK_EQ(Read<CK_ID>(ctx, current), second);
        IM_CHECK_EQ(App().State().History().UndoCount(), steps + 1);
        ctx->ItemClick("**/Qc TypeAhead (Mesh)");
        ctx->KeyChars("q");
        ctx->Yield(3);
        IM_CHECK(highlighted("Qa TypeAhead"));
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK_EQ(Read<CK_ID>(ctx, current), second);
        IM_CHECK_EQ(App().State().History().UndoCount(), steps + 1);
        // Remove Mesh(es) on the one selected (Qc).
        auto holds = [entity](CK_ID mesh) {
            CK3dEntity *object = Object<CK3dEntity>(entity);
            for (int i = 0; i < object->GetMeshCount(); ++i)
                if (object->GetMesh(i) && object->GetMesh(i)->GetID() == mesh) return true;
            return false;
        };
        ctx->ItemClick("**/Qc TypeAhead (Mesh)", ImGuiMouseButton_Right);
        ctx->Yield(2);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("Remove Mesh(es)");
        ctx->SetRef(SETUP);
        ctx->Yield(3);
        IM_CHECK(!Read<bool>(ctx, [&] { return holds(third); }));
        IM_CHECK_EQ(App().State().History().UndoCount(), steps + 2);
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, [&] { return holds(third); }));
        Undo(ctx);
        IM_CHECK_EQ(Read<CK_ID>(ctx, current), before);
        IM_CHECK(App().State().document->Dirty() == dirty);
        OnMainThread(ctx, [&] {
            CKContext *context = App().State().host.Context();
            CK3dEntity *object = Object<CK3dEntity>(entity);
            for (CK_ID id : {first, second, third}) {
                object->RemoveMesh(Object<CKMesh>(id));
                context->DestroyObject(id);
            }
        });
    };

    // The Texture Setup: Mip Levels changed (None and Automatic), then
    // Color Key Transparency; Ctrl+Z gives each back, unmodified.
    t = IM_REGISTER_TEST(engine, "setup", "texture_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const char *ref = "//###setuptexture";
        const CK_ID texture = OpenSetupOf(ctx, CKCID_TEXTURE, ref);
        IM_CHECK_SILENT(texture != 0);
        auto mip = [texture] { return Object<CKTexture>(texture)->GetMipmapCount() != 0; };
        auto key = [texture] { return Object<CKTexture>(texture)->IsTransparent() != 0; };
        const bool mipBefore = Read<bool>(ctx, mip);
        ChooseInCombo(ctx, ref, "Mip Levels", mipBefore ? "None" : "Automatic");
        IM_CHECK(Read<bool>(ctx, mip) != mipBefore);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, mip) == mipBefore);
        IM_CHECK(!App().State().document->Dirty());
        const bool keyBefore = Read<bool>(ctx, key);
        ctx->ItemClick("**/Color Key Transparency");
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, key) != keyBefore);
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, key) == keyBefore);
        IM_CHECK(!App().State().document->Dirty());
    };

    // The Texture Setup's Image Format: the save option, then a format that
    // saves (Dev's Image Saver: its lines drawn); Ctrl+Z gives each back.
    t = IM_REGISTER_TEST(engine, "setup", "texture_image_format");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const char *ref = "//###setuptexture";
        const CK_ID texture = OpenSetupOf(ctx, CKCID_TEXTURE, ref);
        IM_CHECK_SILENT(texture != 0);
        auto option = [texture] { return static_cast<int>(Object<CKTexture>(texture)->GetSaveOptions()); };
        auto format = [texture] {
            CKBitmapProperties *properties = Object<CKTexture>(texture)->GetSaveFormat();
            return std::string(properties ? properties->m_Ext.m_Data : "");
        };
        const int before = Read<int>(ctx, option);
        if (before != CKTEXTURE_IMAGEFORMAT) {
            ChooseInCombo(ctx, ref, "Save Options", "Image Format");
            IM_CHECK_EQ(Read<int>(ctx, option), (int)CKTEXTURE_IMAGEFORMAT);
        }
        // The first reader that saves, as the combo names it.
        std::string label, extension;
        OnMainThread(ctx, [&] {
            CKPluginManager *plugins = CKGetPluginManager();
            for (int i = 0; plugins && i < plugins->GetPluginCount(CKPLUGIN_BITMAP_READER) && label.empty(); ++i) {
                CKPluginEntry *reader = plugins->GetPluginInfo(CKPLUGIN_BITMAP_READER, i);
                if (!reader || !reader->m_PluginInfo.m_Extension.m_Data[0]) continue;
                if (reader->m_ReadersInfo && !(reader->m_ReadersInfo->m_ReaderFlags & CK_DATAREADER_FILESAVE)) continue;
                extension = reader->m_PluginInfo.m_Extension.m_Data;
                label = std::string(reader->m_PluginInfo.m_Summary.CStr() ? reader->m_PluginInfo.m_Summary.CStr() : "") + " (" +
                        extension + ")";
            }
        });
        IM_CHECK_SILENT(!label.empty());
        const std::string formatBefore = Read<std::string>(ctx, format);
        const auto formatSize = Read<int>(ctx, [texture] {
            auto *f = Object<CKTexture>(texture)->GetSaveFormat();
            return f ? f->m_Size : 0;
        });
        ctx->LogInfo("Save format before edit: %s, size %d (native %d)", formatBefore.c_str(), formatSize,
                     int(sizeof(CKBitmapProperties)));
        if (_stricmp(formatBefore.c_str(), extension.c_str()) != 0) {
            ChooseInCombo(ctx, ref, "Format", label.c_str());
            ctx->Yield(5);
            if (_stricmp(Read<std::string>(ctx, format).c_str(), extension.c_str()) != 0)
                LogEditor(ctx, 0);
            IM_CHECK(_stricmp(Read<std::string>(ctx, format).c_str(), extension.c_str()) == 0);
            Undo(ctx);
            IM_CHECK(Read<std::string>(ctx, format) == formatBefore);
        }
        ctx->Yield(5);
        if (before != CKTEXTURE_IMAGEFORMAT) {
            Undo(ctx);
            IM_CHECK_EQ(Read<int>(ctx, option), before);
        }
    };

    t = IM_REGISTER_TEST(engine, "setup", "image_format_options_cancel_and_cleanup");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto &state = App().State();
        vdev::ObjectRef target{};
        vdev::BitmapFormat original;
        int parameters = 0;
        size_t steps = 0;
        bool dirty = false;
        const auto count = [&] { return state.document->Context()->GetObjectsCountByClassID(CKCID_PARAMETEROUT); };
        OnMainThread(ctx, [&] {
            auto *texture = CKTexture::Cast(state.document->Context()->CreateObject(CKCID_TEXTURE, "Image options"));
            IM_CHECK_NO_RET(texture && texture->Create(2, 2, 32));
            state.document->Include(texture->GetID());
            auto format = vdev::MakeSaveFormat(state.document->Context(), "bmp");
            IM_CHECK_NO_RET(!format.empty());
            texture->SetSaveFormat(reinterpret_cast<CKBitmapProperties *>(format.data()));
            texture->SetSaveOptions(CKTEXTURE_IMAGEFORMAT);
            target = state.document->Reference(texture->GetID());
            IM_CHECK_NO_RET(vdev::ReadBitmapFormat(texture, original));
            const_cast<vdev::SetupWindows &>(App().Setups()).CloseAll();
            state.events.Post(vdev::ShowObjectSetup{target});
            steps = state.History().UndoCount();
            dirty = state.document->Dirty();
            parameters = count();
        });
        ctx->Yield(4);
        ctx->SetRef("//###setuptexture");
        ctx->ItemClick("**/Format Options...");
        ctx->Yield(3);
        IM_CHECK(Read<int>(ctx, count) > parameters);
        ctx->SetRef("//Format Options###formatoptions");
        ctx->ItemClick("**/Cancel/##dialogbutton");
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            vdev::BitmapFormat after;
            IM_CHECK_NO_RET(vdev::ReadBitmapFormat(state.document->Resolve(target), after));
            IM_CHECK_NO_RET(after.bytes == original.bytes);
            IM_CHECK_NO_RET(count() == parameters);
            IM_CHECK_NO_RET(state.History().UndoCount() == steps && state.document->Dirty() == dirty);
        });
        ctx->SetRef("//###setuptexture");
        ctx->ItemClick("**/Format Options...");
        ctx->Yield(3);
        ctx->SetRef("//Format Options###formatoptions");
        ctx->ItemClick("**/OK/##dialogbutton");
        ctx->Yield(4);
        OnMainThread(ctx, [&] {
            vdev::BitmapFormat after;
            IM_CHECK_NO_RET(vdev::ReadBitmapFormat(state.document->Resolve(target), after));
            IM_CHECK_NO_RET(after.bytes == original.bytes);
            IM_CHECK_NO_RET(count() == parameters);
            IM_CHECK_NO_RET(state.History().UndoCount() == steps && state.document->Dirty() == dirty);
        });
        // A delayed accept must neither overwrite a new format nor keep private options alive.
        ctx->SetRef("//###setuptexture");
        ctx->ItemClick("**/Format Options...");
        ctx->Yield(3);
        OnMainThread(ctx, [&] { vdev::RenameLevelObject(*state.document, target, "Changed while options open"); });
        ctx->SetRef("//Format Options###formatoptions");
        ctx->ItemClick("**/OK/##dialogbutton");
        ctx->Yield(4);
        IM_CHECK_EQ(Read<int>(ctx, count), parameters);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(state.History().Undo());
            const_cast<vdev::SetupWindows &>(App().Setups()).CloseAll();
        });
    };

    // The 2D Entity Setup (a 2D entity, not a sprite): Pickable, then the
    // Z Order typed in; Ctrl+Z gives each back, unmodified.
    t = IM_REGISTER_TEST(engine, "setup", "entity2d_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CK_ID entity = OpenSetupOf(ctx, CKCID_2DENTITY, "//###setup2d", true);
        IM_CHECK_SILENT(entity != 0);
        auto pickable = [entity] { return Object<CK2dEntity>(entity)->IsPickable() != 0; };
        auto z = [entity] { return Object<CK2dEntity>(entity)->GetZOrder(); };
        const bool pickBefore = Read<bool>(ctx, pickable);
        ctx->ItemClick("**/Pickable");
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, pickable) != pickBefore);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, pickable) == pickBefore);
        IM_CHECK(!App().State().document->Dirty());
        const int zBefore = Read<int>(ctx, z);
        ctx->ItemInputValue("**/Z Order", zBefore + 5);
        ctx->Yield(3);
        IM_CHECK_EQ(Read<int>(ctx, z), zBefore + 5);
        Undo(ctx);
        IM_CHECK_EQ(Read<int>(ctx, z), zBefore);
        IM_CHECK(!App().State().document->Dirty());
    };

    // The level's Scene Setup (its level scene): the Fog Type changed
    // (Linear, None when it is), then Use settings at Scene Start; Ctrl+Z
    // gives each back, unmodified.
    t = IM_REGISTER_TEST(engine, "setup", "scene_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const char *ref = "//###setupscene";
        IM_CHECK_SILENT(OpenSetupOf(ctx, CKCID_LEVEL, ref) != 0);
        auto scene = [] { return App().State().host.Context()->GetCurrentLevel()->GetLevelScene(); };
        auto fog = [&] { return static_cast<int>(scene()->GetFogMode()); };
        auto use = [&] { return scene()->EnvironmentSettings() != FALSE; };
        const int fogBefore = Read<int>(ctx, fog);
        ChooseInCombo(ctx, ref, "##fogtype", fogBefore == VXFOG_LINEAR ? "None" : "Linear");
        IM_CHECK(Read<int>(ctx, fog) != fogBefore);
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK_EQ(Read<int>(ctx, fog), fogBefore);
        IM_CHECK(!App().State().document->Dirty());
        const bool useBefore = Read<bool>(ctx, use);
        ctx->ItemClick("**/Use settings at Scene Start");
        ctx->Yield(3);
        IM_CHECK(Read<bool>(ctx, use) != useBefore);
        Undo(ctx);
        IM_CHECK(Read<bool>(ctx, use) == useBefore);
        IM_CHECK(!App().State().document->Dirty());
    };

    // A Script's Setup (the Behavior Setup), Show in Schematic: the
    // Schematic in front (the Setups share its tab bar), its stack made for
    // that Script, showing it, the Script focused (the stack may have been
    // made for it already: the sample's first Script).
    t = IM_REGISTER_TEST(engine, "setup", "show_in_schematic");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const std::vector<vdev::ScriptInfo> &scripts = App().State().document->Scripts();
        IM_CHECK_SILENT(!scripts.empty());
        const CK_ID script = scripts.back().id;
        IM_CHECK_SILENT(ShowSetup(ctx, script, "//###setupbehavior"));
        OnMainThread(ctx, [] { App().State().Schematic().focus = vdev::FocusRequest(); });
        ctx->ItemClick("**/Show in Schematic");
        ctx->Yield(3);
        IM_CHECK(InFront(ctx, SchematicRef().c_str()));
        IM_CHECK_EQ(App().State().Schematic().stackScript, App().State().document->Reference(script));
        IM_CHECK_EQ(App().State().Schematic().focus.address.script, App().State().document->Reference(script));
        bool shown = false;
        for (const vdev::SchematicCanvas::ShownScript &each : App().Canvas().ShownScripts()) shown |= each.script == script;
        IM_CHECK(shown);
    };
}

} // namespace vdevuitest
