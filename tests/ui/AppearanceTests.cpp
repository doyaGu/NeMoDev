#include "tests/ui/UiTests.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/DevControls.h"
#include "ui/setup/SetupLayout.h"
#include "ui/render/UiRenderer.h"
#include "imgui_internal.h"
#include "CKSprite3D.h"
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
