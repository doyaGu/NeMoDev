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
