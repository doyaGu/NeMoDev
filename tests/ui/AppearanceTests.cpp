#include "tests/ui/UiTests.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/DevControls.h"
#include "ui/setup/SetupLayout.h"
#include "ui/render/UiRenderer.h"
#include "imgui_internal.h"
#include "CKSprite3D.h"
#include "CK3dObject.h"
#include "CKMesh.h"
#include "CKTexture.h"
#include "CKMaterial.h"
#include "CKPatchMesh.h"
#include "CKGrid.h"
#include "CKSprite.h"
#include "CKCurve.h"
#include "CKCurvePoint.h"
#include "CKCharacter.h"
#include "CKBodyPart.h"
#include "CKAnimation.h"
#include <algorithm>
#include <utility>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace vdevuitest {
namespace {
struct PageExtentProbe {
    bool visible = false;
};
void Select(vdev::EditorConfiguration &config, const char *kind, const char *id, const char *provider) {
    for (auto &row : config.providers)
        if (row.kind == kind && row.id == id) {
            row.provider = provider;
            return;
        }
    config.providers.push_back({kind, id, provider});
}
void Alternate(vdev::EditorConfiguration &config) {
    Select(config, "theme", "editor.theme", "example.appearance");
    Select(config, "font", "ui.font.body", "example.appearance");
    Select(config, "image", "ui.bitmap.777", "example.appearance");
}
void CheckAppearance(ImGuiTestContext *ctx, bool alternate) {
    OnMainThread(ctx, [&] {
        const auto color = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        IM_CHECK_NO_RET(ImGui::ColorConvertFloat4ToU32(color) ==
                        (alternate ? IM_COL32(36, 54, 74, 255) : IM_COL32(176, 176, 176, 255)));
        IM_CHECK_NO_RET(ImGui::GetStyle().FontSizeBase == (alternate ? 16 : 14));
        const auto *image = vdev::DevBitmapForTests(777);
        IM_CHECK_NO_RET(image && image->NativeTexture());
        std::vector<unsigned char> pixels;
        IM_CHECK_NO_RET(vdev::MainRenderer().ReadTextureForTests(*image, pixels));
        if (alternate)
            IM_CHECK_NO_RET(pixels.size() == 17 * 19 * 4 && pixels[0] == 201 && pixels[1] == 37 && pixels[2] == 175);
        IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                        App().Layout().ViewportUploadsForTests() == 0);
    });
}
ImGuiWindow *Article(ImGuiTestContext *ctx) {
    const auto name = "//" + App().WorkspaceForTests().WindowName(vdev::HelpViewForTests());
    for (auto *child : ctx->GetWindowByRef(name.c_str())->DC.ChildWindows)
        if (std::string(child->Name).find("/article_") != std::string::npos) return child;
    return nullptr;
}
ImVec2 ReadingLine(ImGuiWindow *article) {
    for (const auto &item : App().Help().PagePaint().drawings)
        if (item.text == "struct")
            return {item.size.x, article->DC.CursorMaxPos.y - App().Help().PagePaint().size.y + item.position.y -
                                     article->InnerRect.Min.y};
    return {};
}
size_t Colored(ImGuiWindow *window, ImU32 color, bool children = false) {
    size_t count = 0;
    for (const auto &vertex : window->DrawList->VtxBuffer) count += vertex.col == color;
    if (children)
        for (auto *child : window->DC.ChildWindows) count += Colored(child, color, true);
    return count;
}
std::string SetupRef(EditorView view) {
    return "//" + App().WorkspaceForTests().WindowName(view);
}
ImGuiWindow *SetupPage(ImGuiTestContext *ctx, EditorView view) {
    for (auto *child : ctx->GetWindowByRef(SetupRef(view).c_str())->DC.ChildWindows)
        if (std::string(child->Name).find("/page_") != std::string::npos) return child;
    return nullptr;
}
void ResizeSetup(ImGuiTestContext *ctx, EditorView view, float width, bool focus = true) {
    OnMainThread(ctx, [&] {
        const auto name = App().WorkspaceForTests().WindowName(view);
        ImGui::SetWindowDock(ImGui::FindWindowByName(name.c_str()), 0, ImGuiCond_Always);
        ImGui::SetWindowPos(name.c_str(), {10, 60});
        ImGui::SetWindowSize(name.c_str(), {width, 680});
    });
    if (focus) ctx->WindowFocus(SetupRef(view).c_str());
    ctx->Yield(4);
}
ImGuiTable *SetupTable(ImGuiTestContext *ctx, EditorView view, const char *id) {
    auto *page = SetupPage(ctx, view);
    return page ? ctx->UiContext->Tables.GetByKey(page->GetID(id)) : nullptr;
}
void ShowSetupTable(ImGuiTestContext *ctx, EditorView view, const char *id) {
    auto *page = SetupPage(ctx, view);
    ctx->ScrollToBottom(page->ID);
    ctx->Yield(3);
    if (auto *table = SetupTable(ctx, view, id)) {
        ctx->ScrollTo(page->ID, ImGuiAxis_Y, page->Scroll.y + table->OuterRect.Min.y - page->InnerRect.Min.y);
        ctx->Yield(3);
    }
}
} // namespace
void RegisterAppearanceTests(ImGuiTestEngine *engine) {
    auto *edit = IM_REGISTER_TEST(engine, "appearance", "theme_font_and_image_switch_at_frame_boundary");
    edit->GuiFunc = MainThreadGui;
    edit->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(8);
        OnMainThread(ctx, [] {
            auto &editor = App().WorkspaceForTests().Application();
            const EditorValue topic{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("appearance.html")};
            const auto result = editor.Execute("help.open", {editor.Scope(), &topic, 1});
            IM_CHECK_NO_RET(result.status == EDITOR_OK || result.status == EDITOR_QUEUED);
        });
        ctx->Yield(4);
        auto *article = Article(ctx);
        IM_CHECK(article);
        const float lineWidth = ReadingLine(article).x;
        IM_CHECK_GT(lineWidth, 0);
        ctx->ScrollTo(article->ID, ImGuiAxis_Y, article->Scroll.y + ReadingLine(article).y + 3,
                      ImGuiTestOpFlags_NoFocusWindow);
        ctx->Yield(3);
        IM_CHECK(std::fabs(ReadingLine(article).y) < 6);
        vdev::EditorConfiguration original;
        ImFont *font = nullptr;
        ImTextureID image = 0;
        size_t history = 0;
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            original = workspace.Application().Configuration();
            font = ImGui::GetIO().FontDefault;
            image = vdev::DevBitmap(777);
            history = App().State().History().UndoCount();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
            // Drawing already submitted in this frame still uses the old resources.
            IM_CHECK_NO_RET(ImGui::GetIO().FontDefault == font && vdev::DevBitmap(777) == image);
        });
        ctx->Yield(5);
        CheckAppearance(ctx, true);
        IM_CHECK_GT(ReadingLine(article).x, lineWidth);
        IM_CHECK(std::fabs(ReadingLine(article).y) < 12);
        int fonts = 0;
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            IM_CHECK_NO_RET(ImGui::GetIO().FontDefault != font && vdev::DevBitmap(777) != image);
            IM_CHECK_NO_RET(font->OwnerAtlas == ImGui::GetIO().Fonts);
            fonts = ImGui::GetIO().Fonts->Fonts.Size;
            image = vdev::DevBitmap(777);
            auto invalid = workspace.Application().Configuration();
            Select(invalid, "font", "ui.font.body", "example.invalid-font");
            std::string error;
            const auto active = ImGui::GetIO().FontDefault;
            IM_CHECK_NO_RET(!workspace.Configure(invalid, error) &&
                            error.find("Cannot load font") != std::string::npos);
            IM_CHECK_NO_RET(ImGui::GetIO().FontDefault == active && vdev::DevBitmap(777) == image);
            // Refuse publication after resources have been successfully prepared.
            const char *name = std::getenv("NEMODEV_APPEARANCE_PROFILE");
            IM_CHECK_NO_RET(name);
            const auto path = std::filesystem::u8path(name),
                       backup = std::filesystem::u8path(std::string(name) + ".retained");
            std::filesystem::rename(path, backup);
            struct Restore {
                std::filesystem::path path, backup;
                ~Restore() {
                    std::error_code e;
                    std::filesystem::remove(path, e);
                    std::filesystem::rename(backup, path, e);
                }
            } restore{path, backup};
            std::filesystem::create_directory(path);
            IM_CHECK_NO_RET(!workspace.Configure(original, error));
            IM_CHECK_NO_RET(ImGui::GetIO().FontDefault == active && vdev::DevBitmap(777) == image);
            IM_CHECK_NO_RET(App().State().History().UndoCount() == history);
        });
        ctx->Yield(12);
        CheckAppearance(ctx, true);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(ImGui::GetIO().Fonts->Fonts.Size == fonts && vdev::DevBitmap(777) == image);
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
        });
        ctx->Yield(4);
        CheckAppearance(ctx, false);
        IM_CHECK(std::fabs(ReadingLine(article).x - lineWidth) < 1);
        IM_CHECK(std::fabs(ReadingLine(article).y) < 12);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(ImGui::GetIO().Fonts->Fonts.Size == fonts && ImGui::GetIO().FontDefault == font);
            const auto scale = ImGui::GetStyle().FontScaleDpi;
            std::string error;
            IM_CHECK_NO_RET(!App().PrepareUiFrame(scale * 2, error));
            IM_CHECK_NO_RET(ImGui::GetStyle().FontScaleDpi == scale);
            IM_CHECK_NO_RET(ImGui::GetIO().FontDefault == font && ImGui::GetIO().Fonts->Fonts.Size == fonts);
            IM_CHECK_NO_RET(App().State().History().UndoCount() == history);
        });
    };
    auto *pages = IM_REGISTER_TEST(engine, "appearance", "page_colors_geometry_and_edit_state");
    pages->GuiFunc = MainThreadGui;
    pages->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        vdev::EditorConfiguration original;
        vdev::DocumentToken token;
        std::vector<vdev::ObjectRef> selection;
        size_t history = 0;
        bool dirty = false;
        EditorView setup = 0, log = 0;
        OnMainThread(ctx, [&] {
            using namespace vdev;
            auto &workspace = App().WorkspaceForTests();
            auto client = EditorCommands();
            const auto made = client.Command("object.create-light");
            IM_CHECK_NO_RET(made.status == EDITOR_OK && EditorClient::Find(made, "created"));
            const auto light = EditorClient::Find(made, "created")->object;
            IM_CHECK_NO_RET(client.Command("object.create-frame3d").status == EDITOR_OK);
            IM_CHECK_NO_RET(client.Command("history.undo").status == EDITOR_OK);
            IM_CHECK_NO_RET(client.Command("selection.replace", {ObjectsArgument("objects", {light})}).status <=
                            EDITOR_NO_CHANGE);
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", light)}).status <=
                            EDITOR_NO_CHANGE);
            setup = workspace.Role("setuplight");
            log = EditorClient::Integer(client.Command("log.open"), "view");
            IM_CHECK_NO_RET(setup && log);
            IM_CHECK_NO_RET(EditorCommands(workspace.Role("main.layout")).Command("layout.snapshots").status <=
                            EDITOR_NO_CHANGE);
            original = workspace.Application().Configuration();
            token = App().State().document->Token();
            history = App().State().History().UndoCount();
            selection = App().State().objectSelection.References();
            dirty = App().State().document->Dirty();
            IM_CHECK_NO_RET(App().State().History().CanRedo());
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        ctx->Yield(4);
        ctx->WindowFocus(SchematicRef().c_str());
        ctx->Yield(3);
        IM_CHECK_GT(Colored(ctx->GetWindowByRef(SchematicRef().c_str()), IM_COL32(83, 100, 117, 255)), 0);
        const auto setupRef = "//" + App().WorkspaceForTests().WindowName(setup);
        ctx->WindowFocus(setupRef.c_str());
        ctx->Yield(3);
        IM_CHECK_GT(Colored(ctx->GetWindowByRef(setupRef.c_str()), IM_COL32(213, 234, 255, 255), true), 0);
        IM_CHECK_GT(Colored(ctx->GetWindowByRef(setupRef.c_str()), IM_COL32(201, 33, 123, 255), true), 0);
        ctx->WindowFocus("//Snapshot Buffer");
        ctx->Yield(3);
        IM_CHECK_GT(Colored(ctx->GetWindowByRef("//Snapshot Buffer"), IM_COL32(50, 67, 84, 255)), 0);
        const auto logRef = "//" + App().WorkspaceForTests().WindowName(log);
        ctx->WindowFocus(logRef.c_str());
        ctx->Yield(3);
        const float scale = ImGui::GetFontSize() / 13;
        IM_CHECK(std::fabs(ctx->ItemInfo((logRef + "/##caption").c_str()).RectFull.GetHeight() - (31 * scale + 1)) < 1);
        IM_CHECK(std::fabs(ctx->GetWindowByRef(logRef.c_str())->WindowRounding - 10 * scale) < 1);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(state.document->Token() == token && state.History().UndoCount() == history &&
                            state.History().CanRedo() && state.document->Dirty() == dirty &&
                            state.objectSelection.References() == selection);
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
        });
        ctx->WindowFocus(SchematicRef().c_str());
        ctx->Yield(4);
        IM_CHECK_GT(Colored(ctx->GetWindowByRef(SchematicRef().c_str()), IM_COL32(176, 176, 176, 255)), 0);
        ctx->WindowFocus(setupRef.c_str());
        ctx->Yield(3);
        IM_CHECK_GT(Colored(ctx->GetWindowByRef(setupRef.c_str()), IM_COL32(255, 0, 0, 255), true), 0);
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(state.document->Token() == token && state.History().UndoCount() == history &&
                            state.History().CanRedo() && state.document->Dirty() == dirty &&
                            state.objectSelection.References() == selection);
        });
    };
    auto *layout = IM_REGISTER_TEST(engine, "appearance", "setup_reflows_and_keeps_edits");
    layout->SetVarsDataType<PageExtentProbe>();
    layout->GuiFunc = [](ImGuiTestContext *ctx) {
        MainThreadGui(ctx);
        if (!ctx->GetVars<PageExtentProbe>().visible) return;
        ImGui::SetNextWindowSize({260, 160}, ImGuiCond_Always);
        if (ImGui::Begin("Setup extent probe", nullptr,
                         ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
            vdev::setup::DevPage page;
            page.Label(0, 0, 20, 20,
                       "A long object name must remain reachable even when it exceeds the original page width");
            page.End(20, 20);
        }
        ImGui::End();
    };
    layout->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        EditorView sceneView = 0, frameView = 0, spriteView = 0;
        ObjectRef scene;
        EditorConfiguration original;
        size_t history = 0;
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto client = EditorCommands();
            const auto tree = client.Query("scene.tree");
            const auto *levelScene = EditorClient::Find(tree, "level-scene");
            IM_CHECK_NO_RET(levelScene && levelScene->object);
            scene = levelScene->object;
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", scene)}).status <=
                            EDITOR_NO_CHANGE);
            sceneView = workspace.Role("setupscene");
            const auto made = client.Command("object.create-frame3d");
            const auto *frame = EditorClient::Find(made, "created");
            IM_CHECK_NO_RET(frame && frame->object);
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", frame->object)}).status <=
                            EDITOR_NO_CHANGE);
            frameView = workspace.Role("setup3d");
            // Test content unavailable from the built-in creation menu.
            auto &document = *App().State().document;
            auto *sprite = document.Context()->CreateObject(CKCID_SPRITE3D, "Setup layout sprite");
            IM_CHECK_NO_RET(sprite);
            document.Include(sprite->GetID());
            IM_CHECK_NO_RET(
                client.Command("object.open-setup", {ObjectArgument("target", document.Reference(sprite->GetID()))})
                    .status <= EDITOR_NO_CHANGE);
            spriteView = workspace.Role("setupsprite3d");
            IM_CHECK_NO_RET(sceneView && frameView && spriteView);
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
            history = App().State().History().UndoCount();
        });
        const auto sceneName = App().WorkspaceForTests().WindowName(sceneView);
        const auto ref = "//" + sceneName;
        ctx->Yield(4);
        ImGuiID fogId = 0;
        ImGuiID fogStartId = 0;
        for (float width : {1350.f, 690.f, 450.f, 1350.f}) {
            OnMainThread(ctx, [&] {
                ImGui::SetWindowDock(ImGui::FindWindowByName(sceneName.c_str()), 0, ImGuiCond_Always);
                ImGui::SetWindowPos(sceneName.c_str(), {10, 60});
                ImGui::SetWindowSize(sceneName.c_str(), {width, 680});
            });
            ctx->WindowFocus(ref.c_str());
            ctx->Yield(4);
            ImGuiWindow *page = nullptr;
            for (auto *child : ctx->GetWindowByRef(ref.c_str())->DC.ChildWindows)
                if (std::string(child->Name).find("/page_") != std::string::npos) page = child;
            IM_CHECK(page);
            const auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(page->GetID("##scene-sections"));
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            const auto background = ctx->ItemInfo(page->GetID("##background")).RectFull;
            const auto &column = table->Columns[3 % table->ColumnsCount];
            IM_CHECK(background.Min.x >= column.WorkMinX - 1 && background.Max.x <= column.WorkMaxX + 1);
            const auto fog = ctx->ItemInfo(page->GetID("##fogtype")).ID;
            IM_CHECK(!fogId || fog == fogId);
            fogId = fog;
            fogStartId = ImHashStr("##value", 0, ImHashStr("Fog Start:", 0, page->ID));
            if (width == 450) IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        for (const auto view : {frameView, spriteView}) {
            const auto name = App().WorkspaceForTests().WindowName(view), windowRef = "//" + name;
            OnMainThread(ctx, [&] {
                ImGui::SetWindowDock(ImGui::FindWindowByName(name.c_str()), 0, ImGuiCond_Always);
                ImGui::SetWindowSize(name.c_str(), {490, 680});
            });
            ctx->WindowFocus(windowRef.c_str());
            ctx->Yield(4);
            ImGuiWindow *page = nullptr;
            for (auto *child : ctx->GetWindowByRef(windowRef.c_str())->DC.ChildWindows)
                if (std::string(child->Name).find("/page_") != std::string::npos) page = child;
            IM_CHECK(page);
            const int axis = 0;
            const auto component = ImHashData(&axis, sizeof(axis), ImHashStr("Position", 0, page->ID));
            IM_CHECK_GT(
                ctx->ItemInfo(ImHashStr(view == frameView ? "##world" : "##v", 0, component)).RectFull.GetWidth(), 1);
        }
        ctx->WindowFocus(ref.c_str());
        ctx->Yield(3);
        const auto mode = Read<uint64_t>(ctx, [&] {
            SetupDetails data;
            IM_CHECK_NO_RET(ReadSetupProperties(EditorCommands(), scene, {"scene.fog-mode"}, data));
            return data.Values().Integer("scene.fog-mode");
        });
        ChooseInCombo(ctx, ref.c_str(), "##fogtype", mode == 3 ? "Exponential" : "Linear");
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        const auto start = [&] {
            SetupDetails data;
            ReadSetupProperties(EditorCommands(), scene, {"scene.fog-start"}, data);
            return data.Values().Real("scene.fog-start");
        };
        const auto before = Read<double>(ctx, start);
        ctx->SetRef(ref.c_str());
        const auto at = ctx->ItemInfo(fogStartId).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 40, at.y});
        ctx->Yield(3);
        IM_CHECK(std::fabs(Read<double>(ctx, start) - before) > .001);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(std::fabs(Read<double>(ctx, start) - before) < .001);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->ItemInputValue(fogStartId, float(before + 25));
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 2);
        Undo(ctx);
        IM_CHECK(std::fabs(Read<double>(ctx, start) - before) < .001);
        ctx->GetVars<PageExtentProbe>().visible = true;
        ctx->Yield(4);
        IM_CHECK_GT(ctx->GetWindowByRef("//Setup extent probe")->ScrollMax.x, 100);
        ctx->GetVars<PageExtentProbe>().visible = false;
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *render = IM_REGISTER_TEST(engine, "appearance", "render_setup_reflows_and_keeps_edits");
    render->GuiFunc = MainThreadGui;
    render->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        struct Page {
            const char *create, *role, *table, *input, *property;
            float typed, divisor;
            ObjectRef object{};
            EditorView view = 0;
            ImGuiID inputId = 0;
        } pages[] = {
            {"object.create-camera", "setupcamera", "##camera-settings", "##fov", "camera.fov", 70,
             (180.f / 3.14159265358979323846f)},
            {"object.create-light", "setuplight", "##light-settings", "##range", "light.range", 325, 1},
            {"object.create-material", "setupmaterial", "##material-settings", "##power", "material.power", 60, 1}};
        EditorConfiguration original;
        DocumentToken token;
        std::vector<ObjectRef> selection;
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto client = EditorCommands();
            for (auto &page : pages) {
                const auto made = client.Command(page.create);
                const auto *created = EditorClient::Find(made, "created");
                IM_CHECK_NO_RET(made.status == EDITOR_OK && created && created->object);
                page.object = created->object;
                IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", page.object)}).status <=
                                EDITOR_NO_CHANGE);
                page.view = workspace.Role(page.role);
                IM_CHECK_NO_RET(page.view);
            }
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
            token = App().State().document->Token();
            selection = App().State().objectSelection.References();
        });
        ctx->Yield(4);
        const auto windowName = [](const Page &page) { return App().WorkspaceForTests().WindowName(page.view); };
        const auto pageWindow = [&](const Page &page) -> ImGuiWindow * {
            const auto ref = "//" + windowName(page);
            for (auto *child : ctx->GetWindowByRef(ref.c_str())->DC.ChildWindows)
                if (std::string(child->Name).find("/page_") != std::string::npos) return child;
            return nullptr;
        };
        const auto resize = [&](const Page &page, float width) {
            OnMainThread(ctx, [&] {
                const auto name = windowName(page);
                ImGui::SetWindowDock(ImGui::FindWindowByName(name.c_str()), 0, ImGuiCond_Always);
                ImGui::SetWindowPos(name.c_str(), {10, 60});
                ImGui::SetWindowSize(name.c_str(), {width, 680});
            });
            ctx->Yield(4);
        };
        const auto number = [&](const Page &page) {
            return Read<double>(ctx, [&] {
                SetupDetails data;
                IM_CHECK_NO_RET(ReadSetupProperties(EditorCommands(), page.object, {page.property}, data));
                return data.Values().Real(page.property);
            });
        };
        const auto history = App().State().History().UndoCount();
        for (auto &page : pages) {
            const auto ref = "//" + windowName(page);
            ctx->WindowFocus(ref.c_str());
            for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
                resize(page, width);
                auto *window = pageWindow(page);
                IM_CHECK(window);
                auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(window->GetID(page.table));
                IM_CHECK(table);
                IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
                const auto id = window->GetID(page.input);
                IM_CHECK(!page.inputId || page.inputId == id);
                page.inputId = id;
                const auto field = ctx->ItemInfo(id).RectFull;
                IM_CHECK_GT(field.GetWidth(), 1);
                bool inside = false;
                for (int i = 0; i < table->ColumnsCount; ++i)
                    inside |=
                        field.Min.x >= table->Columns[i].WorkMinX - 1 && field.Max.x <= table->Columns[i].WorkMaxX + 1;
                IM_CHECK(inside);
                if (width == 450) IM_CHECK_GT(window->ScrollMax.x, 0);
            }
            ctx->WindowFocus(ref.c_str());
            ctx->Yield(3);
            ctx->SetRef(ref.c_str());
            const auto before = number(page);
            ctx->ScrollToItemY(page.inputId);
            ctx->Yield(3);
            const auto at = ctx->ItemInfo(page.inputId).RectFull.GetCenter();
            ctx->MouseMoveToPos(at);
            ctx->MouseDown(0);
            ctx->MouseMoveToPos({at.x + 35, at.y});
            ctx->Yield(3);
            IM_CHECK(std::fabs(number(page) - before) > .0001);
            resize(page, 690);
            IM_CHECK_EQ(pageWindow(page)->GetID(page.input), page.inputId);
            IM_CHECK_EQ(App().State().History().UndoCount(), history);
            ctx->KeyPress(ImGuiKey_Escape);
            ctx->MouseUp(0);
            ctx->Yield(3);
            IM_CHECK(std::fabs(number(page) - before) < .0001);
            IM_CHECK_EQ(App().State().History().UndoCount(), history);
            resize(page, 1500);
            ctx->ItemInputValue(page.inputId, page.typed);
            ctx->Yield(3);
            IM_CHECK(std::fabs(number(page) * page.divisor - page.typed) < .01);
            IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
            Undo(ctx);
            IM_CHECK(std::fabs(number(page) - before) < .0001);
            IM_CHECK_EQ(App().State().History().UndoCount(), history);
        }
        // Directional lights keep range and attenuation unavailable; spot angles stay in degrees on screen.
        const auto lightRef = "//" + windowName(pages[1]);
        ctx->WindowFocus(lightRef.c_str());
        ctx->Yield(3);
        ChooseInCombo(ctx, lightRef.c_str(), "##type", "Directional");
        ctx->Yield(3);
        IM_CHECK(ctx->ItemInfo(pageWindow(pages[1])->GetID("##range")).ItemFlags & ImGuiItemFlags_Disabled);
        IM_CHECK(ctx->ItemInfo(pageWindow(pages[1])->GetID("##constant")).ItemFlags & ImGuiItemFlags_Disabled);
        Undo(ctx);
        ChooseInCombo(ctx, lightRef.c_str(), "##type", "Spot");
        ctx->Yield(3);
        const auto hotSpot = pageWindow(pages[1])->GetID("##hotspot");
        IM_CHECK(!(ctx->ItemInfo(hotSpot).ItemFlags & ImGuiItemFlags_Disabled));
        ctx->ItemInputValue(hotSpot, 45.f);
        ctx->Yield(3);
        const auto angle = Read<double>(ctx, [&] {
            SetupDetails data;
            IM_CHECK_NO_RET(ReadSetupProperties(EditorCommands(), pages[1].object, {"light.hot-spot"}, data));
            return data.Values().Real("light.hot-spot");
        });
        IM_CHECK(std::fabs(angle * (180.f / 3.14159265358979323846f) - 45) < .01);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 2);
        Undo(ctx);
        Undo(ctx);
        // Custom is only a page choice; depth and texture controls remain reachable after reflow.
        const auto materialRef = "//" + windowName(pages[2]);
        ctx->WindowFocus(materialRef.c_str());
        ctx->Yield(3);
        ChooseInCombo(ctx, materialRef.c_str(), "##mode", "Custom");
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        auto *materialPage = pageWindow(pages[2]);
        IM_CHECK_GT(ctx->ItemInfo(materialPage->GetID("##zfunc")).RectFull.GetWidth(), 1);
        ChooseInCombo(ctx, materialRef.c_str(), "##addressmode", "Border");
        ctx->Yield(3);
        const auto border = ImHashStr("Border Color", 0, ImHashStr("Border Color", 0, materialPage->ID));
        IM_CHECK_GT(ctx->ItemInfo(border).RectFull.GetWidth(), 1);
        Undo(ctx);
        const auto diffuse = [&] {
            return Read<std::vector<double>>(ctx, [&] {
                SetupDetails data;
                IM_CHECK_NO_RET(ReadSetupProperties(EditorCommands(), pages[2].object, {"material.diffuse"}, data));
                return data.Values().Reals("material.diffuse");
            });
        };
        const auto color = diffuse();
        const auto swatch = ImHashStr("Diffuse", 0, ImHashStr("Diffuse", 0, materialPage->ID));
        ctx->ItemClick(swatch);
        ctx->Yield(3);
        resize(pages[2], 690);
        const auto popup = ImHashStr("picker", 0, ImHashStr("Diffuse", 0, materialPage->ID));
        IM_CHECK(ImGui::IsPopupOpen(popup, ImGuiPopupFlags_AnyPopupLevel));
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("**/##picker/##hex/##Text");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
        ctx->KeyChars("7F3344");
        ctx->Yield(3);
        IM_CHECK(diffuse() != color);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(4);
        IM_CHECK(diffuse() == color);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().State().document->Token() == token &&
                            App().State().objectSelection.References() == selection);
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *header = IM_REGISTER_TEST(engine, "appearance", "setup_header_reflow_navigation_and_rename");
    header->GuiFunc = MainThreadGui;
    header->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        EditorView view = 0;
        ObjectRef texture, other;
        EditorConfiguration original;
        std::string otherName, filename = "/tmp/";
        for (int i = 0; i < 80; ++i) filename += "long_header_component/";
        filename += "image.bmp";
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            auto client = EditorCommands();
            for (auto *object : {&texture, &other}) {
                const auto made = client.Command("object.create-texture");
                const auto *created = EditorClient::Find(made, "created");
                IM_CHECK_NO_RET(created && created->object);
                *object = created->object;
            }
            // Image content and a filename longer than the header; no filesystem access is needed.
            auto *image = Object<CKTexture>(texture.object);
            IM_CHECK_NO_RET(image && image->Create(2, 2, 32) && image->SetSlotFileName(0, filename.data()));
            SetupPageData page;
            IM_CHECK_NO_RET(ReadSetupPage(client, other, -1, page));
            otherName = page.name;
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", texture)}).status <=
                            EDITOR_NO_CHANGE);
            view = workspace.Role("setuptexture");
            IM_CHECK_NO_RET(view);
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        const auto token = App().State().document->Token();
        const auto selection = App().State().objectSelection.References();
        ImGuiID nameId = 0, replaceId = 0;
        for (const float width : {1500.f, 450.f, 260.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            auto *root = ctx->GetWindowByRef(SetupRef(view).c_str());
            auto *table = ctx->UiContext->Tables.GetByKey(root->GetID("##setup-header"));
            IM_CHECK(table);
            IM_CHECK(table->OuterRect.Min.x >= root->WorkRect.Min.x - 1 &&
                     table->OuterRect.Max.x <= root->WorkRect.Max.x + 1);
            IM_CHECK_LT(table->OuterRect.GetHeight(), 7 * ImGui::GetFrameHeight());
            IM_CHECK(!nameId || nameId == root->GetID("##name"));
            IM_CHECK(!replaceId || replaceId == root->GetID("...##texturefile"));
            nameId = root->GetID("##name");
            replaceId = root->GetID("...##texturefile");
            for (const auto id : {nameId, replaceId, ImHashStr("##bitmap", 0, root->GetID("picker")),
                                  ImHashStr("##bitmap", 0, root->GetID("scenefilter"))}) {
                const auto rect = ctx->ItemInfo(id).RectFull;
                IM_CHECK_GT(rect.GetWidth(), 1);
                IM_CHECK(rect.Min.x >= root->WorkRect.Min.x - 1 && rect.Max.x <= root->WorkRect.Max.x + 1);
                IM_CHECK(rect.Min.y >= table->OuterRect.Min.y && rect.Max.y <= table->OuterRect.Max.y + 1);
            }
            IM_CHECK(SetupPage(ctx, view)->Pos.y >= table->OuterRect.Max.y);
            IM_CHECK_EQ(App().State().History().UndoCount(), history);
        }
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemClick("scenefilter/##bitmap");
        ctx->ItemClick("scenefilter/##bitmap");
        ctx->ItemClick(nameId);
        ctx->Yield(3);
        IM_CHECK(!ctx->UiContext->OpenPopupStack.empty());
        const auto popup = ctx->UiContext->OpenPopupStack.back().PopupId;
        ResizeSetup(ctx, view, 450, false);
        IM_CHECK(!ctx->UiContext->OpenPopupStack.empty() && ctx->UiContext->OpenPopupStack.back().PopupId == popup);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->WindowFocus(SetupRef(view).c_str());
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ComboClick(("##name/" + otherName).c_str());
        ctx->Yield(3);
        OnMainThread(ctx, [&] {
            const auto info = EditorCommands().Inspect(view);
            const auto *target = EditorClient::Find(info, "target");
            IM_CHECK_NO_RET(target && target->object == other);
        });
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        const auto beforeName = Read<std::string>(ctx, [&] {
            SetupPageData page;
            ReadSetupPage(EditorCommands(), other, -1, page);
            return page.name;
        });
        ctx->WindowFocus(SetupRef(view).c_str());
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        ctx->ItemInput("//###setuprename/##name");
        ctx->KeyCharsReplace("Cancelled header name");
        ResizeSetup(ctx, view, 260, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, other.object) == beforeName);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ctx->WindowFocus(SetupRef(view).c_str());
        ctx->KeyPress(ImGuiKey_F2);
        ctx->Yield(3);
        ctx->ItemInputValue("//###setuprename/##name", "Accepted header name");
        ctx->Yield(3);
        IM_CHECK(ObjectName(ctx, other.object) == "Accepted header name");
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(ObjectName(ctx, other.object) == beforeName);
        ctx->WindowFocus(SetupRef(view).c_str());
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemClick("picker/##bitmap");
        OnMainThread(ctx, [&] {
            auto &workspace = App().WorkspaceForTests();
            IM_CHECK_NO_RET(workspace.Close(view));
            IM_CHECK_NO_RET(EditorCommands().Command("object.open-setup", {ObjectArgument("target", texture)}).status <=
                            EDITOR_NO_CHANGE);
            view = workspace.Role("setuptexture");
            IM_CHECK_NO_RET(view && !workspace.Application().HasEdits());
            SetupPageData page;
            IM_CHECK_NO_RET(ReadSetupPage(EditorCommands(), texture, -1, page) && page.filename == filename);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(original, error));
            IM_CHECK_NO_RET(App().State().document->Token() == token &&
                            App().State().objectSelection.References() == selection);
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *entity = IM_REGISTER_TEST(engine, "appearance", "entity3d_reflow_cancel_and_mesh_selection");
    entity->GuiFunc = MainThreadGui;
    entity->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef object, first, second;
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto &document = *App().State().document;
            auto *context = document.Context();
            // A mesh-bearing object is not available from the built-in creation menu.
            auto *made = CK3dObject::Cast(context->CreateObject(CKCID_3DOBJECT, "Entity layout object"));
            auto *a = CKMesh::Cast(context->CreateObject(CKCID_MESH, "Entity layout first mesh"));
            auto *b = CKMesh::Cast(context->CreateObject(CKCID_MESH, "Entity layout second mesh"));
            IM_CHECK_NO_RET(made && a && b);
            for (auto *value : {static_cast<CKObject *>(made), static_cast<CKObject *>(a), static_cast<CKObject *>(b)})
                document.Include(value->GetID());
            made->AddMesh(a);
            made->AddMesh(b);
            made->SetCurrentMesh(a);
            made->SetPosition3f(10, 20, 30);
            object = document.Reference(made->GetID());
            first = document.Reference(a->GetID());
            second = document.Reference(b->GetID());
            auto &workspace = App().WorkspaceForTests();
            IM_CHECK_NO_RET(EditorCommands().Command("object.open-setup", {ObjectArgument("target", object)}).status <=
                            EDITOR_NO_CHANGE);
            view = workspace.Role("setup3d");
            IM_CHECK_NO_RET(view);
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID input = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            auto *page = SetupPage(ctx, view);
            IM_CHECK(page);
            for (const auto *id : {"##entity-transform", "##entity-details"}) {
                const auto *table = ctx->UiContext->Tables.GetByKey(page->GetID(id));
                IM_CHECK(table);
                IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            }
            const int axis = 0;
            const auto id = ImHashStr("##v", 0, ImHashData(&axis, sizeof(axis), page->GetID("Position")));
            IM_CHECK(!input || input == id);
            input = id;
            IM_CHECK_GT(ctx->ItemInfo(id).RectFull.GetWidth(), 1);
            if (width == 450) IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto position = [&] {
            return Read<std::vector<double>>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), object, {"entity3d.world-position"}, data);
                return data.Values().Reals("entity3d.world-position");
            });
        };
        const auto before = position();
        IM_CHECK_EQ(before.size(), 3);
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ScrollToItemY(input);
        const auto at = ctx->ItemInfo(input).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 40, at.y});
        ctx->Yield(3);
        IM_CHECK(position() != before);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(position() == before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemInputValue(input, float(before[0] + 12.5));
        ctx->Yield(3);
        const auto after = position();
        IM_CHECK(std::fabs(after[0] - before[0] - 12.5) < .001 && after[1] == before[1] && after[2] == before[2]);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(position() == before);
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemClick("**/Entity layout first mesh (Mesh)");
        ctx->KeyDown(ImGuiMod_Ctrl);
        ctx->ItemClick("**/Entity layout second mesh (Mesh)");
        ctx->KeyUp(ImGuiMod_Ctrl);
        ResizeSetup(ctx, view, 690);
        ctx->ScrollToBottom(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemClick("**/Entity layout second mesh (Mesh)", ImGuiMouseButton_Right);
        ctx->Yield(2);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("Remove Mesh(es)");
        ctx->Yield(3);
        const auto meshes = [&] {
            return Read<std::vector<ObjectRef>>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), object, {"entity3d.meshes"}, data);
                return data.Values().Objects("entity3d.meshes");
            });
        };
        IM_CHECK(meshes().empty());
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        const auto restored = meshes();
        IM_CHECK(restored.size() == 2 && std::find(restored.begin(), restored.end(), first) != restored.end() &&
                 std::find(restored.begin(), restored.end(), second) != restored.end());
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *entity2d = IM_REGISTER_TEST(engine, "appearance", "entity2d_reflow_cancel_and_object_choices");
    entity2d->GuiFunc = MainThreadGui;
    entity2d->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef object, parent, material;
        EditorView view = 0;
        EditorConfiguration original;
        std::string parentName, materialName, ownName;
        OnMainThread(ctx, [&] {
            auto client = EditorCommands();
            for (const auto &entry :
                 {std::make_pair("object.create-frame2d", &parent), std::make_pair("object.create-material", &material),
                  std::make_pair("object.create-frame2d", &object)}) {
                const auto made = client.Command(entry.first);
                const auto *created = EditorClient::Find(made, "created");
                IM_CHECK_NO_RET(created && created->object);
                *entry.second = created->object;
            }
            SetupPageData page;
            IM_CHECK_NO_RET(ReadSetupPage(client, parent, -1, page));
            parentName = page.name;
            IM_CHECK_NO_RET(ReadSetupPage(client, material, -1, page));
            materialName = page.name;
            IM_CHECK_NO_RET(ReadSetupPage(client, object, -1, page));
            ownName = page.name;
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", object)}).status <=
                            EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setup2d");
            IM_CHECK_NO_RET(view);
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID input = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            auto *page = SetupPage(ctx, view);
            const auto *table = ctx->UiContext->Tables.GetByKey(page->GetID("##entity2d-settings"));
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            const int axis = 0;
            const auto id = ImHashData(&axis, sizeof(axis), page->GetID("X / Y"));
            IM_CHECK(!input || input == id);
            input = id;
            for (const auto &field : {std::make_pair("X / Y", 2), std::make_pair("Width / Height", 2),
                                      std::make_pair("Z Order", 1), std::make_pair("L / T / R / B", 4)}) {
                for (int component = 0; component < field.second; ++component) {
                    const auto id = field.second == 1
                                        ? page->GetID(field.first)
                                        : ImHashData(&component, sizeof(component), page->GetID(field.first));
                    const auto rect = ctx->ItemInfo(id).RectFull;
                    bool inside = false;
                    for (int i = 0; i < table->ColumnsCount; ++i)
                        inside |= rect.Min.x >= table->Columns[i].WorkMinX - 1 &&
                                  rect.Max.x <= table->Columns[i].WorkMaxX + 1;
                    IM_CHECK(inside);
                }
            }
            if (width == 450) IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto position = [&] {
            return Read<std::vector<double>>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), object, {"entity2d.position"}, data);
                return data.Values().Reals("entity2d.position");
            });
        };
        const auto before = position();
        IM_CHECK_EQ(before.size(), 2);
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ScrollToItemY(input);
        const auto at = ctx->ItemInfo(input).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 40, at.y});
        ctx->Yield(3);
        IM_CHECK(position() != before);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(position() == before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemInputValue(input, float(before[0] + .25));
        ctx->Yield(3);
        const auto after = position();
        IM_CHECK(std::fabs(after[0] - before[0] - .25) < .001 && after[1] == before[1]);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(position() == before);
        const auto choice = [&](const char *property) {
            return Read<ObjectRef>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), object, {property}, data);
                return data.Values().Object(property);
            });
        };
        const auto originalMaterial = choice("entity2d.material"), originalParent = choice("entity2d.parent");
        const auto ref = SetupRef(view);
        ChooseInCombo(ctx, ref.c_str(), "##material", materialName.c_str());
        ctx->Yield(3);
        IM_CHECK(choice("entity2d.material") == material);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(choice("entity2d.material") == originalMaterial);
        ChooseInCombo(ctx, ref.c_str(), "##parent", ownName.c_str());
        ctx->Yield(3);
        IM_CHECK(choice("entity2d.parent") == originalParent);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ChooseInCombo(ctx, ref.c_str(), "##parent", parentName.c_str());
        ctx->Yield(3);
        IM_CHECK(choice("entity2d.parent") == parent);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(choice("entity2d.parent") == originalParent);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *texture = IM_REGISTER_TEST(engine, "appearance", "texture_reflow_slots_previews_cancel_and_undo");
    texture->GuiFunc = MainThreadGui;
    texture->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target;
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto client = EditorCommands();
            const auto made = client.Command("object.create-texture");
            IM_CHECK_NO_RET(EditorClient::Find(made, "created"));
            target = EditorClient::Find(made, "created")->object;
            auto *image = Object<CKTexture>(target.object);
            IM_CHECK_NO_RET(image && image->Create(8, 8, 32, 0) && image->Create(8, 8, 32, 1));
            for (int slot = 0; slot < 2; ++slot) {
                image->SetCurrentSlot(slot);
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        image->SetPixel(x, y, slot ? 0x7f123456 : 0xffabcdef);
            }
            image->SetCurrentSlot(0);
            image->SetPickThreshold(0);
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                            EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setuptexture");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID threshold = 0, preview = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ctx->ScrollToBottom(SetupPage(ctx, view)->ID);
            ctx->Yield(3);
            auto *page = SetupPage(ctx, view);
            for (const auto &row :
                 {std::make_pair("##bitmap-info", 3), std::make_pair("##texture-settings", 2),
                  std::make_pair("##texture-previews", 2)}) {
                auto *table = SetupTable(ctx, view, row.first);
                IM_CHECK(table);
                IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? row.second : 1);
            }
            const auto id = page->GetID("##threshold");
            IM_CHECK(!threshold || threshold == id);
            threshold = id;
            const auto imageId = ImHashStr("##preview", 0, page->GetID("image"));
            IM_CHECK(!preview || preview == imageId);
            preview = imageId;
            const auto *table = SetupTable(ctx, view, "##texture-previews");
            for (const char *name : {"image", "alpha"}) {
                const auto rect = ctx->ItemInfo(ImHashStr("##preview", 0, page->GetID(name))).RectFull;
                bool inside = false;
                for (int i = 0; i < table->ColumnsCount; ++i)
                    inside |= rect.Min.x >= table->Columns[i].WorkMinX - 1 &&
                              rect.Max.x <= table->Columns[i].WorkMaxX + 1;
                IM_CHECK(inside && rect.GetWidth() > 1);
            }
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto value = [&](const char *property) {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {property}, data);
                return int(data.Values().Integer(property));
            });
        };
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ScrollToItemY(threshold);
        ctx->Yield(3);
        const int before = value("texture.pick-threshold");
        const auto at = ctx->ItemInfo(threshold).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 35, at.y});
        ctx->Yield(3);
        IM_CHECK(value("texture.pick-threshold") != before);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(value("texture.pick-threshold"), before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->SetRef(SetupRef(view).c_str());
        const int typed = before < 128 ? 200 : 40;
        ctx->ItemInputValue(threshold, typed);
        ctx->Yield(3);
        IM_CHECK_EQ(value("texture.pick-threshold"), typed);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(value("texture.pick-threshold"), before);
        ctx->ScrollToTop(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        ctx->ItemClick("**/Next");
        ctx->Yield(3);
        IM_CHECK_EQ(value("texture.current-slot"), 1);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(value("texture.current-slot"), 0);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *mesh = IM_REGISTER_TEST(engine, "appearance", "mesh_reflow_channels_patch_cancel_and_undo");
    mesh->GuiFunc = MainThreadGui;
    mesh->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target, patch;
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *made = CKMesh::Cast(doc.Context()->CreateObject(CKCID_MESH, "Layout channels mesh"));
            auto *subdivided =
                CKPatchMesh::Cast(doc.Context()->CreateObject(CKCID_PATCHMESH, "Layout patch mesh"));
            auto *a = CKMaterial::Cast(doc.Context()->CreateObject(CKCID_MATERIAL, "Layout channel first"));
            auto *b = CKMaterial::Cast(doc.Context()->CreateObject(CKCID_MATERIAL, "Layout channel second"));
            IM_CHECK_NO_RET(made && subdivided && a && b);
            subdivided->SetIterationCount(0);
            for (CKObject *value : {static_cast<CKObject *>(made), static_cast<CKObject *>(subdivided),
                                    static_cast<CKObject *>(a), static_cast<CKObject *>(b)})
                doc.Include(value->GetID());
            IM_CHECK_NO_RET(made->AddChannel(a) >= 0 && made->AddChannel(b) >= 0);
            target = doc.Reference(made->GetID());
            patch = doc.Reference(subdivided->GetID());
            auto &workspace = App().WorkspaceForTests();
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                EDITOR_NO_CHANGE);
            view = workspace.Role("setupmesh");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID channelsId = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            for (const char *name : {"##mesh-settings", "##mesh-lists"}) {
                auto *table = SetupTable(ctx, view, name);
                IM_CHECK(table);
                IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            }
            ShowSetupTable(ctx, view, "channels");
            auto *table = SetupTable(ctx, view, "channels");
            IM_CHECK(table && table->InnerWindow);
            IM_CHECK(!channelsId || channelsId == table->ID);
            channelsId = table->ID;
            IM_CHECK_GT(table->InnerWindow->ScrollMax.x, 0);
            const auto *outer = SetupTable(ctx, view, "##mesh-lists");
            bool inside = false;
            for (int i = 0; i < outer->ColumnsCount; ++i)
                inside |= table->OuterRect.Min.x >= outer->Columns[i].WorkMinX - 1 &&
                          table->OuterRect.Max.x <= outer->Columns[i].WorkMaxX + 1;
            IM_CHECK(inside);
        }
        const auto channelValue = [&](const char *property) {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {property}, data, {0});
                return int(data.Values().Integer(property));
            });
        };
        auto *table = SetupTable(ctx, view, "channels");
        ctx->ScrollToX(table->InnerWindow->ID, 0);
        ctx->Yield(3);
        const int row = 0;
        const auto seed = ImHashData(&row, sizeof(row), table->ID);
        const auto active = ImHashStr("##active", 0, seed);
        ctx->SetRef(SetupRef(view).c_str());
        const int wasActive = channelValue("mesh.channel-active");
        ctx->ItemClick(active);
        ctx->Yield(3);
        IM_CHECK_EQ(channelValue("mesh.channel-active"), !wasActive);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(channelValue("mesh.channel-active"), wasActive);
        table = SetupTable(ctx, view, "channels");
        ctx->ScrollToX(table->InnerWindow->ID, table->InnerWindow->ScrollMax.x);
        ctx->Yield(3);
        const int blend = channelValue("mesh.channel-dest-blend"), desired = blend == 2 ? 1 : 2;
        ctx->ItemClick(ImHashStr("##dst", 0, seed));
        ctx->Yield(2);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick(desired == 1 ? "**/Zero" : "**/One");
        ctx->Yield(3);
        IM_CHECK_EQ(channelValue("mesh.channel-dest-blend"), desired);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK_EQ(channelValue("mesh.channel-dest-blend"), blend);
        table = SetupTable(ctx, view, "channels");
        ctx->ScrollToX(table->InnerWindow->ID, 0);
        ctx->Yield(3);
        const auto selectChannel = [&](int index, const char *name, ImGuiMouseButton button = 0) {
            const auto rowId = ImHashData(&index, sizeof(index), channelsId);
            ctx->ItemClick(ImHashStr(name, 0, rowId), button);
        };
        selectChannel(0, "Layout channel first");
        ctx->KeyDown(ImGuiMod_Ctrl);
        selectChannel(1, "Layout channel second");
        ctx->KeyUp(ImGuiMod_Ctrl);
        ResizeSetup(ctx, view, 690);
        ShowSetupTable(ctx, view, "channels");
        selectChannel(1, "Layout channel second", ImGuiMouseButton_Right);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("**/Remove Channel(s)");
        ctx->Yield(3);
        const auto channelCount = [&] {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "mesh", data);
                return int(data.Values().Integer("channel-count"));
            });
        };
        IM_CHECK_EQ(channelCount(), 0);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK_EQ(channelCount(), 2);
        IM_CHECK_EQ(channelValue("mesh.channel-dest-blend"), blend);
        OnMainThread(ctx, [&] {
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", patch)}).status <=
                EDITOR_NO_CHANGE);
        });
        ResizeSetup(ctx, view, 1500);
        auto *page = SetupPage(ctx, view);
        const auto input = page->GetID("Iterations");
        ctx->ScrollToTop(page->ID);
        ctx->Yield(3);
        const auto iterations = [&] {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), patch, {"patchmesh.iterations"}, data);
                return int(data.Values().Integer("patchmesh.iterations"));
            });
        };
        const int before = iterations();
        const auto at = ctx->ItemInfo(input).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 35, at.y});
        ctx->Yield(3);
        IM_CHECK(iterations() != before);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(iterations(), before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        const int typed = before < 5 ? 7 : 2;
        ctx->ItemInputValue(input, typed);
        ctx->Yield(3);
        IM_CHECK_EQ(iterations(), typed);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(iterations(), before);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *grid = IM_REGISTER_TEST(engine, "appearance", "grid_reflow_multilayer_strokes_cancel_and_undo");
    grid->GuiFunc = MainThreadGui;
    grid->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target;
        EditorView view = 0;
        EditorConfiguration original;
        int first = -1, second = -1;
        OnMainThread(ctx, [&] {
            auto client = EditorCommands();
            const auto made = client.Command("object.create-grid");
            IM_CHECK_NO_RET(EditorClient::Find(made, "created"));
            target = EditorClient::Find(made, "created")->object;
            for (const char *name : {"Layout grid first", "Layout grid second"}) {
                SetupDetails data;
                IM_CHECK_NO_RET(ReadSetupDetails(client, target, "grid", data));
                const auto registered =
                    client.Command("setup.grid-register-layer",
                                   {ObjectArgument("target", target), TextArgument("name", name),
                                    IntegerArgument("document-revision", data.documentRevision)});
                const auto *created = EditorClient::Find(registered, "created");
                IM_CHECK_NO_RET(registered.status == EDITOR_OK && created);
                const auto type = created->integer;
                IM_CHECK_NO_RET(ReadSetupDetails(client, target, "grid", data));
                IM_CHECK_NO_RET(client
                                    .Command("setup.grid-add-layer",
                                             {ObjectArgument("target", target), IntegerArgument("type", type),
                                              IntegerArgument("document-revision", data.documentRevision)})
                                    .status == EDITOR_OK);
                IM_CHECK_NO_RET(ReadSetupDetails(client, target, "grid", data));
                const auto &layers = data.Values().Rows("layers");
                for (size_t i = 0; i < layers.size(); ++i)
                    if (SetupFields(layers[i].fields).Integer("type") == type)
                        (first < 0 ? first : second) = int(i);
            }
            IM_CHECK_NO_RET(first >= 0 && second >= 0 && first != second);
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                            EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupgrid");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID squares = 0;
        const auto canvas = [&]() -> ImGuiWindow * {
            auto *page = SetupPage(ctx, view);
            for (auto *child : page->DC.ChildWindows)
                if (child->ChildId == page->GetID("##grid-canvas"))
                    return child;
            return nullptr;
        };
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ctx->ScrollToTop(SetupPage(ctx, view)->ID);
            ctx->Yield(3);
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##grid-sections");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            const auto id = page->GetID("##squares");
            IM_CHECK(!squares || squares == id);
            squares = id;
            auto *child = canvas();
            IM_CHECK(child);
            IM_CHECK(child->Pos.x >= table->Columns[0].WorkMinX - 1 &&
                     child->Pos.x + child->Size.x <= table->Columns[0].WorkMaxX + 1);
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        ctx->SetRef(SetupRef(view).c_str());
        ShowSetupTable(ctx, view, "layers");
        const auto selectLayer = [&](int row, const char *name) {
            const auto seed = ImHashData(&row, sizeof(row), SetupTable(ctx, view, "layers")->ID);
            ctx->ItemClick(ImHashStr(name, 0, seed));
        };
        selectLayer(first, "Layout grid first");
        ctx->KeyDown(ImGuiMod_Ctrl);
        selectLayer(second, "Layout grid second");
        ctx->KeyUp(ImGuiMod_Ctrl);
        ctx->ScrollToTop(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        const auto cells = [&] {
            return Read<std::vector<std::vector<uint64_t>>>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "grid", data);
                std::vector<std::vector<uint64_t>> result;
                for (const auto &layer : data.Values().Rows("layers"))
                    result.push_back(SetupFields(layer.fields).Integers("squares"));
                return result;
            });
        };
        const auto before = cells();
        const auto beginStroke = [&] {
            const auto rect = ctx->ItemInfo(squares).RectFull;
            const ImVec2 at{rect.Min.x + rect.GetWidth() * .05f, rect.Min.y + rect.GetHeight() * .05f};
            ctx->MouseMoveToPos(at);
            ctx->MouseDown(0);
            ctx->MouseMoveToPos({at.x + rect.GetWidth() * .15f, at.y});
            ctx->Yield(3);
        };
        beginStroke();
        const auto preview = cells();
        IM_CHECK(preview != before && preview[size_t(first)] == preview[size_t(second)]);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 690, false);
        IM_CHECK(cells() == preview);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(cells() == before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ScrollToTop(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        beginStroke();
        ctx->MouseUp(0);
        ctx->Yield(3);
        const auto after = cells();
        IM_CHECK(after != before && after[size_t(first)] == after[size_t(second)]);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(cells() == before);
        Redo(ctx);
        IM_CHECK(cells() == after);
        Undo(ctx);
        IM_CHECK(cells() == before);
        const auto priority = [&] {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {"grid.priority"}, data);
                return int(data.Values().Integer("grid.priority"));
            });
        };
        const auto input = SetupPage(ctx, view)->GetID("Priority");
        const int oldPriority = priority();
        ctx->ItemInputValue(input, oldPriority + 3);
        ctx->Yield(3);
        IM_CHECK_EQ(priority(), oldPriority + 3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(priority(), oldPriority);
        const auto length = [&] {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "grid", data);
                return int(data.Values().Integer("length"));
            });
        };
        const int oldLength = length();
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Add Row (Top)"));
        ctx->Yield(3);
        IM_CHECK_EQ(length(), oldLength + 1);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(length(), oldLength);
        IM_CHECK(cells() == before);
        ctx->ScrollToTop(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        ctx->ItemClick("**/3D Properties");
        ctx->Yield(3);
        IM_CHECK(SetupTable(ctx, view, "##entity-transform") && SetupTable(ctx, view, "##entity-details"));
        ctx->ItemClick("**/Grid Properties");
        ctx->Yield(3);
        IM_CHECK(cells() == before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] { Object<CKGrid>(target.object)->SetDimensions(256, 256, 256, 256); });
        ResizeSetup(ctx, view, 450);
        auto *child = canvas();
        IM_CHECK(child);
        IM_CHECK_GT(child->ScrollMax.x, 0);
        IM_CHECK_GT(child->ScrollMax.y, 0);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *sprite = IM_REGISTER_TEST(engine, "appearance", "sprite_reflow_picking_slots_cancel_and_undo");
    sprite->GuiFunc = MainThreadGui;
    sprite->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target;
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *image = CKSprite::Cast(doc.Context()->CreateObject(CKCID_SPRITE, "Reflow sprite"));
            IM_CHECK_NO_RET(image && image->Create(16, 16, 32, 0) && image->Create(16, 16, 32, 1));
            doc.Include(image->GetID());
            for (int slot = 0; slot < 2; ++slot) {
                image->SetCurrentSlot(slot);
                for (int y = 0; y < 16; ++y)
                    for (int x = 0; x < 16; ++x)
                        image->SetPixel(x, y,
                                        0xff000000u | uint32_t(x * 16) << 16 | uint32_t(y * 16) << 8 |
                                            uint32_t(slot * 32));
            }
            image->SetCurrentSlot(0);
            image->SetSaveOptions(CKTEXTURE_RAWDATA);
            image->SetTransparentColor(0xff010203);
            target = doc.Reference(image->GetID());
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupsprite2d");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID preview = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##sprite-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##sprite-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            const auto id = ImHashStr("##preview", 0, page->GetID("image"));
            IM_CHECK(!preview || preview == id);
            preview = id;
            const auto rect = ctx->ItemInfo(preview).RectFull;
            const int column = table->ColumnsCount - 1;
            IM_CHECK(rect.Min.x >= table->Columns[column].WorkMinX - 1 &&
                     rect.Max.x <= table->Columns[column].WorkMaxX + 1);
            IM_CHECK(SetupTable(ctx, view, "##entity2d-settings"));
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto integer = [&](const char *property) {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {property}, data);
                return int(data.Values().Integer(property));
            });
        };
        const auto color = [&] {
            return Read<std::vector<double>>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {"sprite.transparent-color"}, data);
                return data.Values().Reals("sprite.transparent-color");
            });
        };
        const auto ref = SetupRef(view);
        ChooseInCombo(ctx, ref.c_str(), "##saveoptions", "External");
        ctx->Yield(3);
        IM_CHECK_EQ(integer("sprite.save-options"), CKTEXTURE_EXTERNAL);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(ref.c_str());
        Undo(ctx);
        IM_CHECK_EQ(integer("sprite.save-options"), CKTEXTURE_RAWDATA);
        const auto before = color();
        const auto beginPicking = [&] {
            ctx->SetRef(ref.c_str());
            ShowSetupTable(ctx, view, "##sprite-settings");
            ctx->ItemClick("**/Pick");
            ctx->Yield(3);
            ctx->ScrollToItemY(preview);
            ctx->Yield(3);
            const auto rect = ctx->ItemInfo(preview).RectFull;
            const auto at = rect.GetCenter();
            ctx->MouseMoveToPos(at);
            ctx->MouseDown(0);
            ctx->MouseMoveToPos({at.x + 25, at.y});
            ctx->Yield(3);
        };
        beginPicking();
        const auto picked = color();
        IM_CHECK(picked != before);
        const auto mouse = ctx->UiContext->IO.MousePos;
        IM_CHECK_EQ(ctx->UiContext->ActiveId, preview);
        ResizeSetup(ctx, view, 1300, false);
        IM_CHECK_EQ(ctx->UiContext->ActiveId, preview);
        IM_CHECK(ctx->UiContext->IO.MousePos.x == mouse.x && ctx->UiContext->IO.MousePos.y == mouse.y);
        IM_CHECK(ctx->UiContext->IO.MouseDelta.x == 0 && ctx->UiContext->IO.MouseDelta.y == 0);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        IM_CHECK(color() == picked);
        ctx->MouseMoveToPos({mouse.x - 25, mouse.y + 30});
        ctx->Yield(3);
        IM_CHECK(color() != picked);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK(color() == before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        beginPicking();
        ctx->MouseUp(0);
        ctx->Yield(3);
        const auto after = color();
        IM_CHECK(after != before);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK(color() == before);
        Redo(ctx);
        IM_CHECK(color() == after);
        Undo(ctx);
        ctx->KeyPress(ImGuiKey_N);
        ctx->Yield(3);
        IM_CHECK_EQ(integer("sprite.current-slot"), 1);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(integer("sprite.current-slot"), 0);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *curve = IM_REGISTER_TEST(engine, "appearance", "curve_reflow_points_tools_cancel_and_undo");
    curve->GuiFunc = MainThreadGui;
    curve->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target, points[3];
        EditorView view = 0;
        EditorConfiguration original;
        OnMainThread(ctx, [&] {
            auto client = EditorCommands();
            const auto result = client.Command("object.create-curve");
            IM_CHECK_NO_RET(EditorClient::Find(result, "created"));
            target = EditorClient::Find(result, "created")->object;
            auto &doc = *App().State().document;
            auto *made = Object<CKCurve>(target.object);
            IM_CHECK_NO_RET(made);
            for (int i = 0; i < 3; ++i) {
                auto *point = CKCurvePoint::Cast(doc.Context()->CreateObject(
                    CKCID_CURVEPOINT, ("Reflow point " + std::to_string(i)).c_str()));
                IM_CHECK_NO_RET(point);
                doc.Include(point->GetID());
                points[i] = doc.Reference(point->GetID());
                const VxVector position(float(i), float(i % 2), 0.f);
                point->SetPosition(&position);
                point->SetBias(0);
                point->SetTension(0);
                point->SetContinuity(0);
                point->SetLinear(FALSE);
                IM_CHECK_NO_RET(made->AddControlPoint(point) == CK_OK);
            }
            made->SetStepCount(10);
            made->SetFittingCoeff(0);
            made->Update();
            IM_CHECK_NO_RET(client.Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                            EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupcurve");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID bias = 0, fitting = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "##curve-settings");
            auto *page = SetupPage(ctx, view);
            auto *table = SetupTable(ctx, view, "##curve-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 3 : 1);
            IM_CHECK(!bias || bias == page->GetID("Bias"));
            bias = page->GetID("Bias");
            IM_CHECK(!fitting || fitting == page->GetID("Fitting Coef"));
            fitting = page->GetID("Fitting Coef");
            for (const auto name : {"Bias", "Tension", "Continuity", "Fitting Coef", "Step count",
                                    "Add Points", "##referential"}) {
                const auto rect = ctx->ItemInfo(page->GetID(name)).RectFull;
                bool inside = false;
                for (int i = 0; i < table->ColumnsCount; ++i)
                    inside |= rect.Min.x >= table->Columns[i].WorkMinX - 1 &&
                              rect.Max.x <= table->Columns[i].WorkMaxX + 1;
                IM_CHECK(inside);
            }
            if (width == 450)
                IM_CHECK_GT(page->ScrollMax.x, 0);
        }
        const auto value = [&](ObjectRef object, const char *property) {
            return Read<double>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), object, {property}, data);
                return data.Values().Real(property);
            });
        };
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ScrollToItemY(fitting);
        ctx->Yield(3);
        auto at = ctx->ItemInfo(fitting).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 25, at.y});
        ctx->Yield(3);
        IM_CHECK(std::fabs(value(target, "curve.fitting")) > .01);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(value(target, "curve.fitting"), 0.0);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ctx->ItemInputValue(fitting, .25f);
        ctx->Yield(3);
        IM_CHECK(std::fabs(value(target, "curve.fitting") - .25) < .001);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(value(target, "curve.fitting"), 0.0);
        ctx->ScrollToItemY(bias);
        ctx->Yield(3);
        at = ctx->ItemInfo(bias).RectFull.GetCenter();
        ctx->MouseMoveToPos(at);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({at.x + 25, at.y});
        ctx->Yield(3);
        IM_CHECK(std::fabs(value(points[0], "curvepoint.bias")) > .01);
        ResizeSetup(ctx, view, 690, false);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->MouseUp(0);
        ctx->Yield(3);
        IM_CHECK_EQ(value(points[0], "curvepoint.bias"), 0.0);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ShowSetupTable(ctx, view, "##curve-settings");
        ctx->ItemClick(SetupPage(ctx, view)->GetID(">"));
        ctx->Yield(3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        for (const auto &field :
             {std::make_pair("Bias", "curvepoint.bias"), std::make_pair("Tension", "curvepoint.tension"),
              std::make_pair("Continuity", "curvepoint.continuity")}) {
            ctx->ItemInputValue(SetupPage(ctx, view)->GetID(field.first), .5f);
            ctx->Yield(3);
            IM_CHECK(std::fabs(value(points[1], field.second) - .5) < .001);
            IM_CHECK_EQ(value(points[0], field.second), 0.0);
            IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
            Undo(ctx);
            IM_CHECK_EQ(value(points[1], field.second), 0.0);
        }
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Add Points"));
        ctx->Yield(3);
        const auto tool = [&] {
            return Read<int>(ctx, [&] {
                return int(EditorClient::Integer(EditorCommands().Query("layout.curve-tool"), "tool"));
            });
        };
        IM_CHECK_EQ(tool(), 128);
        ResizeSetup(ctx, view, 690);
        ShowSetupTable(ctx, view, "##curve-settings");
        ChooseInCombo(ctx, SetupRef(view).c_str(), "##referential", "View Plane");
        IM_CHECK_EQ(Read<int>(ctx,
                              [&] {
                                  return int(EditorClient::Integer(
                                      EditorCommands().Query("layout.curve-tool"), "plane"));
                              }),
                    1);
        ctx->SetRef(SetupRef(view).c_str());
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Add Points"));
        ctx->Yield(3);
        IM_CHECK_EQ(tool(), 0);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *character =
        IM_REGISTER_TEST(engine, "appearance", "character_reflow_bodyparts_navigation_and_undo");
    character->GuiFunc = MainThreadGui;
    character->TestFunc = [](ImGuiTestContext *ctx) {
        using namespace vdev;
        Settle(ctx);
        ObjectRef target, root, available, animation;
        EditorView view = 0;
        EditorConfiguration original;
        std::vector<std::string> labels;
        OnMainThread(ctx, [&] {
            auto &doc = *App().State().document;
            auto *context = doc.Context();
            auto *made = CKCharacter::Cast(context->CreateObject(CKCID_CHARACTER, "Reflow character"));
            IM_CHECK_NO_RET(made);
            doc.Include(made->GetID());
            target = doc.Reference(made->GetID());
            for (int i = 0; i < 3; ++i) {
                auto *part = CKBodyPart::Cast(
                    context->CreateObject(CKCID_BODYPART, ("Reflow body " + std::to_string(i)).c_str()));
                IM_CHECK_NO_RET(part);
                doc.Include(part->GetID());
                if (i < 2) {
                    IM_CHECK_NO_RET(made->AddBodyPart(part) == CK_OK);
                    if (i == 0) {
                        root = doc.Reference(part->GetID());
                        IM_CHECK_NO_RET(made->SetRootBodyPart(part) == CK_OK);
                    }
                } else
                    available = doc.Reference(part->GetID());
            }
            auto *walk = CKAnimation::Cast(context->CreateObject(CKCID_ANIMATION, "Reflow character walk"));
            auto *active = CKAnimation::Cast(context->CreateObject(
                CKCID_ANIMATION, ("Active animation " + std::string(150, 'A')).c_str()));
            IM_CHECK_NO_RET(walk && active);
            doc.Include(walk->GetID());
            doc.Include(active->GetID());
            animation = doc.Reference(walk->GetID());
            IM_CHECK_NO_RET(made->AddAnimation(walk) == CK_OK && made->AddAnimation(active) == CK_OK);
            IM_CHECK_NO_RET(made->SetActiveAnimation(active) == CK_OK);
            SetupDetails data;
            IM_CHECK_NO_RET(ReadSetupDetails(EditorCommands(), target, "character", data));
            root = data.Values().Object("root");
            for (const auto &row : data.Values().Rows("bodies")) {
                const SetupFields part(row.fields);
                labels.push_back(part.Text("name") + (part.Object("target") == root ? "  (Root)" : ""));
            }
            IM_CHECK_NO_RET(labels.size() == 2);
            IM_CHECK_NO_RET(
                EditorCommands().Command("object.open-setup", {ObjectArgument("target", target)}).status <=
                EDITOR_NO_CHANGE);
            auto &workspace = App().WorkspaceForTests();
            view = workspace.Role("setupcharacter");
            original = workspace.Application().Configuration();
            auto config = original;
            Alternate(config);
            std::string error;
            IM_CHECK_NO_RET(view && workspace.Configure(config, error));
        });
        ctx->Yield(4);
        const auto history = App().State().History().UndoCount();
        ImGuiID bodyparts = 0;
        for (const float width : {1500.f, 690.f, 450.f, 1500.f}) {
            ResizeSetup(ctx, view, width);
            ShowSetupTable(ctx, view, "bodyparts");
            auto *table = SetupTable(ctx, view, "##character-settings");
            IM_CHECK(table);
            IM_CHECK_EQ(table->ColumnsCount, width > 1000 ? 2 : 1);
            auto *list = SetupTable(ctx, view, "bodyparts");
            IM_CHECK(list && list->InnerWindow);
            IM_CHECK(!bodyparts || bodyparts == list->ID);
            bodyparts = list->ID;
            IM_CHECK_GT(list->InnerWindow->ScrollMax.x, 0);
            IM_CHECK(list->Flags & ImGuiTableFlags_Resizable);
            IM_CHECK(list->OuterRect.Min.x >= table->Columns[0].WorkMinX - 1 &&
                     list->OuterRect.Max.x <= table->Columns[0].WorkMaxX + 1);
            if (width == 450)
                IM_CHECK_GT(SetupPage(ctx, view)->ScrollMax.x, 0);
        }
        auto *table = SetupTable(ctx, view, "bodyparts");
        ctx->ScrollToX(table->InnerWindow->ID, 0);
        ctx->Yield(3);
        const float oldWidth = table->Columns[0].WidthGiven;
        const ImVec2 border{table->Columns[0].MaxX, table->InnerWindow->InnerRect.Min.y + 10};
        ctx->MouseMoveToPos(border);
        ctx->MouseDown(0);
        ctx->MouseMoveToPos({border.x + 35, border.y});
        ctx->MouseUp(0);
        ctx->Yield(3);
        table = SetupTable(ctx, view, "bodyparts");
        const float changedWidth = table->Columns[0].WidthGiven;
        IM_CHECK(std::fabs(changedWidth - oldWidth) > 10);
        ResizeSetup(ctx, view, 690);
        ShowSetupTable(ctx, view, "bodyparts");
        IM_CHECK(std::fabs(SetupTable(ctx, view, "bodyparts")->Columns[0].WidthGiven - changedWidth) < 3);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        ResizeSetup(ctx, view, 1500);
        ShowSetupTable(ctx, view, "bodyparts");
        ctx->SetRef(SetupRef(view).c_str());
        const auto pickable = [&] {
            return Read<int>(ctx, [&] {
                SetupDetails data;
                ReadSetupProperties(EditorCommands(), target, {"entity3d.pickable"}, data);
                return int(data.Values().Integer("entity3d.pickable"));
            });
        };
        const int beforePick = pickable();
        ctx->ItemClick(SetupPage(ctx, view)->GetID("Pickable"));
        ctx->Yield(3);
        IM_CHECK_EQ(pickable(), !beforePick);
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        Undo(ctx);
        IM_CHECK_EQ(pickable(), beforePick);
        const auto select = [&](int row, ImGuiMouseButton button = 0) {
            const auto seed = ImHashData(&row, sizeof(row), bodyparts);
            ctx->ItemClick(ImHashStr(labels[size_t(row)].c_str(), 0, seed), button, ImGuiTestOpFlags_MoveToEdgeL);
        };
        ShowSetupTable(ctx, view, "bodyparts");
        select(0);
        ctx->KeyDown(ImGuiMod_Ctrl);
        select(1);
        ctx->KeyUp(ImGuiMod_Ctrl);
        ResizeSetup(ctx, view, 690);
        ShowSetupTable(ctx, view, "bodyparts");
        select(1, ImGuiMouseButton_Right);
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("**/Remove Body Part");
        ctx->Yield(3);
        const auto bodies = [&] {
            return Read<std::vector<ObjectRef>>(ctx, [&] {
                SetupDetails data;
                ReadSetupDetails(EditorCommands(), target, "character", data);
                std::vector<ObjectRef> result;
                for (const auto &row : data.Values().Rows("bodies"))
                    result.push_back(SetupFields(row.fields).Object("target"));
                return result;
            });
        };
        IM_CHECK(bodies().empty());
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK_EQ(bodies().size(), 2);
        IM_CHECK(Read<ObjectRef>(ctx, [&] {
                     SetupDetails data;
                     ReadSetupDetails(EditorCommands(), target, "character", data);
                     return data.Values().Object("root");
                 }) == root);
        const auto openAdd = [&] {
            ctx->SetRef(SetupRef(view).c_str());
            ShowSetupTable(ctx, view, "bodyparts");
            auto *list = SetupTable(ctx, view, "bodyparts");
            ctx->MouseMoveToPos({list->OuterRect.Min.x + 15, list->OuterRect.Max.y - 35});
            ctx->MouseClick(ImGuiMouseButton_Right);
            ctx->Yield(3);
            ctx->SetRef("//$FOCUSED");
            ctx->ItemClick("**/Add Body Part");
            ctx->Yield(3);
        };
        openAdd();
        const auto popup = ctx->UiContext->OpenPopupStack.back().PopupId;
        ResizeSetup(ctx, view, 1500, false);
        IM_CHECK(!ctx->UiContext->OpenPopupStack.empty() &&
                 ctx->UiContext->OpenPopupStack.back().PopupId == popup);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK_EQ(bodies().size(), 2);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        openAdd();
        ctx->SetRef("//$FOCUSED");
        ctx->ItemClick("**/Reflow body 2");
        ctx->Yield(3);
        const auto added = bodies();
        IM_CHECK_EQ(added.size(), 3);
        IM_CHECK(std::find(added.begin(), added.end(), available) != added.end());
        IM_CHECK_EQ(App().State().History().UndoCount(), history + 1);
        ctx->SetRef(SetupRef(view).c_str());
        Undo(ctx);
        IM_CHECK_EQ(bodies().size(), 2);
        ctx->ScrollToBottom(SetupPage(ctx, view)->ID);
        ctx->Yield(3);
        ctx->ItemDoubleClick("**/Reflow character walk (Animation)");
        ctx->Yield(3);
        IM_CHECK(ShowsObject(ctx, animation.object));
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
        OnMainThread(ctx, [&] {
            std::string error;
            IM_CHECK_NO_RET(App().WorkspaceForTests().Configure(original, error));
            IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && App().Layout().View().ReadbackFrames() == 0 &&
                            App().Layout().ViewportUploadsForTests() == 0);
        });
    };
    auto *startup = IM_REGISTER_TEST(engine, "appearancestartup", "appearance_startup_state");
    startup->GuiFunc = MainThreadGui;
    startup->TestFunc = [](ImGuiTestContext *ctx) {
        ctx->Yield(8);
        const auto *phase = std::getenv("NEMODEV_APPEARANCE_PHASE");
        IM_CHECK(phase);
        if (std::string(phase) == "basic") {
            OnMainThread(ctx, [&] {
                IM_CHECK_NO_RET(App().WorkspaceForTests().Application().Themes().empty());
                IM_CHECK_NO_RET(vdev::DevBitmap(777) == ImTextureID());
                IM_CHECK_NO_RET(App().Layout().UsesGpuViewport() && ImGui::GetIO().Fonts->Fonts.Size != 0);
            });
            return;
        }
        const bool missing = std::string(phase) == "missing";
        if (std::string(phase) == "save") {
            OnMainThread(ctx, [&] {
                auto &workspace = App().WorkspaceForTests();
                auto config = workspace.Application().Configuration();
                Alternate(config);
                std::string error;
                IM_CHECK_NO_RET(workspace.Configure(config, error));
            });
            ctx->Yield(4);
        }
        CheckAppearance(ctx, !missing);
    };
}
} // namespace vdevuitest
