// "setup": the 3D Object Setup on the sample opened from the command line,
// opened from the Level Manager (a 3D object's row, Enter): the Position
// typed in (Ctrl+Z puts it back), Set IC / Remove IC (each undone), another
// object chosen in the Name combo; the Camera, Light, Material and Curve
// Setups (a value typed in, a choice made, each undone); a Script's Setup's
// Show in Schematic.

#include <cmath>
#include <cstring>

#include "core/runtime/TextEncoding.h"
#include "core/level/ObjectProperties.h"
#include "core/level/SceneCommands.h"
#include "tests/ui/UiTests.h"
#include "tests/ObjectEditTestObject.h"

namespace vdev {

// Exercise the same live-edit entry points as the widgets, using a CK
// object with failing Save/Load methods. No production failure switches.
struct SetupEditTests {
    static SetupWindows &Setups(EditorApp &app) { return const_cast<SetupWindows &>(app.Setups()); }
    static void Set(EditorApp &app, CK_ID id, int value, bool fail = false) {
        Setups(app).Live(id, "Property test", [value, fail](CKObject *object) {
            auto *test = static_cast<vdevtest::PropertyTestObject *>(object);
            ++test->writes;
            test->value = value;
            if (fail) throw std::runtime_error("Property setter failed");
        });
    }
    static bool Finish(EditorApp &app) { return Setups(app).FlushPending(); }
    static void Cancel(EditorApp &app) { Setups(app).CancelPending(); }
    static void Scene(EditorApp &app, CK_ID id, float fog, bool fail = false) {
        Setups(app).LiveScene(id, "Scene property test", [fog, fail](CKScene *scene) {
            scene->SetFogStart(fog);
            if (fail) throw std::runtime_error("Scene setter failed");
        });
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
    Settle(ctx, "//Level Manager");
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

    t = IM_REGISTER_TEST(engine, "propertyedits", "capture_and_recovery_failures");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        struct Results { bool before = false, after = false, exception = false, cancel = false, recovery = false; } results;
        OnMainThread(ctx, [&] {
            auto &app = App();
            auto &document = *app.State().document;
            auto &history = document.History();
            auto *object = new vdevtest::PropertyTestObject(document.Context());
            document.Include(object->GetID());
            document.MarkSaved();
            history.MarkSavePoint();
            const size_t count = history.UndoCount();
            object->failSave = object->saves + 1;
            vdev::SetupEditTests::Set(app, object->GetID(), 10);
            results.before = object->writes == 0 && object->value == 0 && !document.Dirty() &&
                             history.UndoCount() == count;
            object->failSave = object->saves + 2;
            vdev::SetupEditTests::Set(app, object->GetID(), 20);
            results.after = !vdev::SetupEditTests::Finish(app) && object->value == 0 && !document.Dirty() &&
                            history.UndoCount() == count;
            vdev::SetupEditTests::Set(app, object->GetID(), 30, true);
            results.exception = object->value == 0 && !document.Dirty() && history.UndoCount() == count;
            vdev::SetupEditTests::Set(app, object->GetID(), 10);
            const bool committed = vdev::SetupEditTests::Finish(app);
            vdev::SetupEditTests::Set(app, object->GetID(), 20);
            vdev::SetupEditTests::Cancel(app);
            results.cancel = committed && object->value == 10 && history.UndoCount() == count + 1 &&
                             history.Undo() && object->value == 0 && !document.Dirty() &&
                             history.Redo() && object->value == 10;
            vdev::SetupEditTests::Set(app, object->GetID(), 30);
            object->failLoad = object->loads + 1;
            vdev::SetupEditTests::Cancel(app);
            results.recovery = object->value == -100 && document.Dirty() && !history.CanUndo() && !history.CanRedo();
            object->value = 0;
            document.MarkSaved();
            history.MarkSavePoint();
        });
        IM_CHECK(results.before);
        IM_CHECK(results.after);
        IM_CHECK(results.exception);
        IM_CHECK(results.cancel);
        IM_CHECK(results.recovery);
    };

    t = IM_REGISTER_TEST(engine, "propertyedits", "scene_cancel_and_setter_failure");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        IM_CHECK(Read<bool>(ctx, [] {
            auto &app = App();
            auto &document = *app.State().document;
            CKScene *scene = vdev::InitialConditionsScene(document.Context());
            if (!scene) return false;
            const auto before = vdev::ReadSceneEnvironment(scene);
            auto restored = [&] {
                const auto now = vdev::ReadSceneEnvironment(scene);
                return now.ambient == before.ambient && now.fogColor == before.fogColor &&
                       now.background == before.background && now.fogMode == before.fogMode &&
                       now.fogStart == before.fogStart && now.fogEnd == before.fogEnd &&
                       now.fogDensity == before.fogDensity && now.backgroundTexture == before.backgroundTexture &&
                       now.startingCamera == before.startingCamera && now.useAtStart == before.useAtStart;
            };
            const size_t count = document.History().UndoCount();
            vdev::SetupEditTests::Scene(app, scene->GetID(), before.fogStart + 50.0f);
            vdev::SetupEditTests::Cancel(app);
            if (!restored() || document.History().UndoCount() != count) return false;
            vdev::SetupEditTests::Scene(app, scene->GetID(), before.fogStart + 80.0f, true);
            return restored() && document.History().UndoCount() == count && !document.Dirty();
        }));
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
                    App().State().events.Post(vdev::ShowObjectSetup{first});
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
        });
        ctx->Yield(3);
        // The target's row shown first (its folders opened: a click on an
        // object's arrow while picking would pick that object).
        Settle(ctx, "//Level Manager");
        IM_CHECK_SILENT(FindLevelRow(ctx, CKCID_3DOBJECT).has_value());
        auto targetRow = [&] {
            std::optional<LevelRow> found;
            for (const LevelRow &shown : App().Panels().ShownLevelRows())
                if (shown.object == target) found = shown;
            return found;
        };
        IM_CHECK_SILENT(targetRow().has_value());
        Settle(ctx, SETUP);
        ctx->ItemClick((std::string(SETUP) + "/picker/##bitmap").c_str());
        ctx->Yield(2);
        Settle(ctx, "//Level Manager");
        ctx->Yield(2);
        const std::optional<LevelRow> row = targetRow();
        IM_CHECK_SILENT(row.has_value());
        ctx->MouseMoveToPos(ImVec2(row->arrowMax.x > 0.0f ? row->arrowMax.x + 40.0f : row->min.x + 120.0f,
                                   (row->min.y + row->max.y) * 0.5f));
        ctx->MouseClick(0);
        ctx->Yield(3);
        bool shown = false, kept = false;
        for (const vdev::SetupWindows::OpenSetup &setup : App().Setups().OpenSetups()) {
            shown = shown || setup.object == target;
            kept = kept || setup.object == entity;
        }
        IM_CHECK(shown && !kept);
        OnMainThread(ctx, [&] { App().State().host.Context()->DestroyObject(target); });
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
        if (_stricmp(formatBefore.c_str(), extension.c_str()) != 0) {
            ChooseInCombo(ctx, ref, "Format", label.c_str());
            ctx->Yield(5);
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
        OnMainThread(ctx, [] { App().State().focus = vdev::FocusRequest(); });
        ctx->ItemClick("**/Show in Schematic");
        ctx->Yield(3);
        IM_CHECK(InFront(ctx, "//Schematic"));
        IM_CHECK_EQ(App().State().stackScript, script);
        IM_CHECK_EQ(App().State().focus.script, script);
        bool shown = false;
        for (const vdev::SchematicCanvas::ShownScript &each : App().Canvas().ShownScripts()) shown |= each.script == script;
        IM_CHECK(shown);
    };
}

} // namespace vdevuitest
