// "app": the editor on its new composition: its windows, the File menu,
// the dialogs of the Options and Help menus and their shortcuts.

#include "core/level/LevelCommands.h"
#include "imgui_internal.h"
#include "tests/ui/UiTests.h"
#include "ui/app/DesktopFiles.h"
#include "ui/controls/DevTheme.h"
#include <cmath>

namespace vdevuitest {

namespace {

bool WindowShown(ImGuiTestContext *ctx, const char *name) {
    ImGuiWindow *window = ctx->GetWindowByRef(name);
    return window && window->WasActive;
}

} // namespace

void RegisterAppTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    t = IM_REGISTER_TEST(engine, "app", "display_scale_rebuilds_theme_sizes");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        ImGuiStyle base, scaled;
        vdev::ApplyDevTheme(base);
        for (int pass = 0; pass < 8; ++pass) {
            for (float scale : {.75f, 1.25f, 1.5f, 2.0f, 1.0f}) {
                vdev::ApplyDevTheme(scaled, scale);
                ImGuiStyle expected = base;
                expected.ScaleAllSizes(scale);
                IM_CHECK_EQ(scaled.FramePadding.x, expected.FramePadding.x);
                IM_CHECK_EQ(scaled.WindowPadding.y, expected.WindowPadding.y);
                IM_CHECK_EQ(scaled.ItemSpacing.x, expected.ItemSpacing.x);
                IM_CHECK_EQ(scaled.ScrollbarSize, expected.ScrollbarSize);
                IM_CHECK_EQ(scaled.FontScaleDpi, scale);
                IM_CHECK_EQ(scaled.FontSizeBase, 14.0f);
            }
        }
        for (float invalid : {0.0f, -1.0f, NAN, INFINITY}) {
            vdev::ApplyDevTheme(scaled, invalid);
            IM_CHECK_EQ(scaled.FontScaleDpi, 1.0f);
            IM_CHECK_EQ(scaled.FramePadding.x, base.FramePadding.x);
        }
    };

    // Explicit status fonts keep their original proportions at every DPI.
    t = IM_REGISTER_TEST(engine, "app", "status_bar_text_scales_once");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        const ImGuiStyle saved = ImGui::GetStyle();
        OnMainThread(ctx, [] { App().State().Log(vdev::LogLevel::Info, "Status text"); });
        for (float dpi : {1.0f, 1.5f, 2.0f, 1.0f}) {
            ctx->LogInfo("Status bar DPI scale %.2f", dpi);
            OnMainThread(ctx, [dpi] { ImGui::GetStyle().FontScaleDpi = dpi; });
            ctx->Yield(3);
            OnMainThread(ctx, [&] {
                ImGuiWindow *bar = ImGui::FindWindowByName("##StatusBar");
                IM_CHECK(bar && bar->WasActive);
                const float scale = ImGui::GetFontSize() / 13.0f;
                const ImU32 color = IM_COL32(176, 176, 176, 255);
                auto check = [&](float left, float right, float top, float bottom, const char *text, bool bold) {
                    const ImVec2 min(IM_ROUND(bar->Pos.x + bar->Size.x - left * scale),
                                     IM_ROUND(bar->Pos.y + top * scale));
                    const ImVec2 max(IM_ROUND(bar->Pos.x + bar->Size.x - right * scale),
                                     IM_ROUND(bar->Pos.y + bottom * scale));
                    // Plain labels use GPU clipping; ellipsis labels also clip
                    // glyph quads on the CPU. Compare their visible geometry.
                    auto visible = [&](ImVec2 point) {
                        return ImVec2(ImClamp(point.x, min.x, max.x), ImClamp(point.y, min.y, max.y));
                    };
                    ImDrawList expected(ImGui::GetDrawListSharedData());
                    expected._ResetForNewFrame();
                    expected.PushClipRectFullScreen();
                    expected.PushTexture(ImGui::GetIO().Fonts->TexRef);
                    const float height = vdev::VIFontHeight(bold);
                    vdev::DrawVIText(&expected, min, color, text, bold,
                                    ImGui::GetRoundedFontSize(height * scale) / height, &max);
                    std::vector<ImVec2> actual;
                    for (const ImDrawVert &vertex : bar->DrawList->VtxBuffer)
                        if (vertex.col == color && vertex.pos.x >= min.x - 1 && vertex.pos.x <= max.x + 1 &&
                            vertex.pos.y >= min.y - 1 && vertex.pos.y <= max.y + 1)
                            actual.push_back(visible(vertex.pos));
                    IM_CHECK(expected.VtxBuffer.Size > 0);
                    IM_CHECK_EQ(actual.size(), static_cast<size_t>(expected.VtxBuffer.Size));
                    for (size_t i = 0; i < actual.size(); ++i) {
                        const ImVec2 point = visible(expected.VtxBuffer[static_cast<int>(i)].pos);
                        IM_CHECK(std::fabs(actual[i].x - point.x) < .1f);
                        IM_CHECK(std::fabs(actual[i].y - point.y) < .1f);
                    }
                };
                check(154, 124, 4, 17, "FPS", true);
                check(185, 155, 7, 19, "NA", false);
                check(407, 207, 6, 18, "Status text", false);
            });
        }
        OnMainThread(ctx, [&] { ImGui::GetStyle() = saved; });
    };

    // Dev's main windows are there on a new composition.
    t = IM_REGISTER_TEST(engine, "app", "main_windows");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        IM_CHECK(App().State().HasDocument());
        for (const char *name : {"//3D Layout", "//Schematic", "//Level Manager", "//Building Blocks"})
            IM_CHECK_NO_RET(ctx->GetWindowByRef(name) != nullptr);
    };

    // File > New Composition: a new, unmodified composition without Scripts
    // and without undo steps.
    t = IM_REGISTER_TEST(engine, "app", "new_composition");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->MenuClick("//##MainMenuBar/File/New Composition");
        ctx->Yield(5);
        vdev::EditorState &state = App().State();
        IM_CHECK(state.HasDocument());
        IM_CHECK(!state.document->Dirty());
        IM_CHECK(state.document->Scripts().empty());
        IM_CHECK(!state.History().CanUndo());
    };

    // What waits for a composition ends with it: File > New Composition
    // closes the 3D Layout Explorer (without restoring the selection it
    // saved) and ends a flight without recording it.
    t = IM_REGISTER_TEST(engine, "app", "new_composition_ends_explorer_and_flight");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//3D Layout");
        // Y over the view flies; the toolbar's button (Dev command 41) opens the Explorer.
        ImVec2 min, max;
        IM_CHECK_SILENT(App().Layout().ShownView(min, max));
        ctx->MouseMoveToPos(ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f));
        ctx->KeyPress(ImGuiKey_Y);
        ctx->Yield(2);
        IM_CHECK_SILENT(App().Layout().Tool() == vdev::LayoutTool::Fly);
        ctx->ItemClick("//3D Layout/$$41/##tool");
        ctx->Yield(3);
        IM_CHECK_SILENT(WindowShown(ctx, "//3D Layout Explorer"));
        ctx->MenuClick("//##MainMenuBar/File/New Composition");
        ctx->Yield(5);
        IM_CHECK(!WindowShown(ctx, "//3D Layout Explorer"));
        IM_CHECK(App().Layout().Tool() == vdev::LayoutTool::Select);
        IM_CHECK(!App().State().History().CanUndo());
        IM_CHECK(!App().State().document->Dirty());
    };

    // Ctrl+P and Options > General Preferences open the General Preferences;
    // Cancel closes them.
    t = IM_REGISTER_TEST(engine, "app", "general_preferences");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_P);
        ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, "//General Preferences"));
        ctx->ItemClick("//General Preferences/Cancel");
        ctx->Yield(3);
        IM_CHECK(!WindowShown(ctx, "//General Preferences"));
        ctx->MenuClick("//##MainMenuBar/Options/General Preferences");
        ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, "//General Preferences"));
        ctx->ItemClick("//General Preferences/Cancel");
        ctx->Yield(3);
        IM_CHECK(!WindowShown(ctx, "//General Preferences"));
    };

    t = IM_REGISTER_TEST(engine, "app", "duplicate_names_cancel");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        auto loaded = std::make_shared<bool>(false);
        CK_ID object = 0;
        size_t historyCount = 0, scriptCount = 0;
        std::set<CK_ID> created;
        std::vector<CK_ID> loadedObjects;
        std::string sourcePath;
        bool dirty = false;
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            const auto made = vdev::CreateLayoutObject(*state.document, vdev::LayoutObject::Frame3D);
            IM_CHECK_NO_RET(made.ok);
            object = made.created;
            historyCount = state.History().UndoCount();
            created = state.document->CreatedObjects();
            loadedObjects = state.document->LoadedObjects();
            scriptCount = state.document->Scripts().size();
            sourcePath = state.document->SourcePath();
            dirty = state.document->Dirty();
            vdev::EditorState::DuplicateNamesRequest request;
            request.path = "cancelled.cmo";
            request.serial = ++state.duplicateNamesAsked;
            request.duplicates.push_back({0, CKCID_3DENTITY, "Existing object", vdev::DuplicateAction::Ignore});
            request.load = [loaded](const std::vector<vdev::DuplicateName> *) { *loaded = true; };
            state.duplicateNames.push_back(std::move(request));
        });
        ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, "//###DuplicateNames"));
        ctx->ItemClick("//###DuplicateNames/Cancel/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK(!*loaded && App().State().duplicateNames.empty());
        IM_CHECK(!WindowShown(ctx, "//###DuplicateNames"));
        OnMainThread(ctx, [&] {
            auto &state = App().State();
            IM_CHECK_NO_RET(state.document->IsLive(object));
            IM_CHECK_NO_RET(state.document->CreatedObjects() == created);
            IM_CHECK_NO_RET(state.document->LoadedObjects() == loadedObjects);
            IM_CHECK_EQ_NO_RET(state.document->Scripts().size(), scriptCount);
            IM_CHECK_NO_RET(state.document->SourcePath() == sourcePath);
            IM_CHECK_NO_RET(state.document->Dirty() == dirty);
            IM_CHECK_EQ_NO_RET(state.History().UndoCount(), historyCount);
            IM_CHECK_NO_RET(state.History().Undo());
            IM_CHECK_NO_RET(!state.document->IsLive(object));
            IM_CHECK_NO_RET(state.History().Redo());
            IM_CHECK_NO_RET(state.document->IsLive(object));
        });
    };

    // Local help is optional. Missing help disables its menus and makes
    // the reference and Building Block shortcuts quiet.
    t = IM_REGISTER_TEST(engine, "app", "optional_help");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Building Blocks");
        const bool available = vdev::HelpAvailable();
        ctx->LogInfo("Local help: %s", available ? "installed" : "absent");
        ctx->MenuClick("//##MainMenuBar/Help");
        IM_CHECK(((ctx->ItemInfo("//$FOCUSED/Online Reference").ItemFlags & ImGuiItemFlags_Disabled) == 0) == available);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->ItemInputValue("//Building Blocks/##filter", "Rotate");
        ctx->ItemClick("//Building Blocks/**/Rotate", ImGuiMouseButton_Right);
        IM_CHECK(((ctx->ItemInfo("//$FOCUSED/Html Help").ItemFlags & ImGuiItemFlags_Disabled) == 0) == available);
        ctx->KeyPress(ImGuiKey_Escape);
        if (!available) {
            const auto &state = App().State();
            const size_t logCount = state.log.size();
            const int repeats = logCount ? state.log.back().repeats : 0;
            const bool dirty = state.document->Dirty();
            ctx->WindowFocus("//Level Manager");
            ctx->MouseMoveToPos(ImVec2(10, 10));
            ctx->KeyPress(ImGuiKey_F1);
            ctx->MouseMove("//Building Blocks/**/Rotate");
            ctx->KeyPress(ImGuiKey_F1);
            ctx->ItemDoubleClick("//Building Blocks/**/Rotate");
            ctx->Yield(3);
            IM_CHECK_EQ(state.log.size(), logCount);
            if (logCount) IM_CHECK_EQ(state.log.back().repeats, repeats);
            IM_CHECK(state.document->Dirty() == dirty);
        }
    };

    t = IM_REGISTER_TEST(engine, "app", "editors_menu");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->MenuClick("//##MainMenuBar/Editors");
        IM_CHECK(!ctx->ItemExists("//$FOCUSED/Variable Manager"));
        IM_CHECK(!ctx->ItemExists("//$FOCUSED/VSL Script Manager"));
        for (const char *item : {"Attributes Manager", "Hierarchy Manager", "Path Manager", "Parameter Debugger",
                                 "Close All Setups and Managers"})
            IM_CHECK_NO_RET(ctx->ItemExists((std::string("//$FOCUSED/") + item).c_str()));
        ctx->KeyPress(ImGuiKey_Escape);
    };

    // Help > About Virtools..., closed with OK.
    t = IM_REGISTER_TEST(engine, "app", "about");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        ctx->MenuClick("//##MainMenuBar/Help/About Virtools...");
        ctx->Yield(3);
        IM_CHECK(WindowShown(ctx, "//About Virtools"));
        ctx->ItemClick("//About Virtools/OK");
        ctx->Yield(3);
        IM_CHECK(!WindowShown(ctx, "//About Virtools"));
    };
}

} // namespace vdevuitest
