#include "tests/ui/UiTests.h"
#include "ui/render/UiRenderer.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>

namespace vdevuitest {
namespace {
std::filesystem::path Output(const char *name) {
    return std::filesystem::u8path(SDL_GetBasePath()) / name;
}
void ClearPages(const std::filesystem::path &folder) {
    // These four files belong to this test. Never remove a directory tree.
    for (int page = 1; page <= 4; ++page)
        std::filesystem::remove(folder / ("page" + std::to_string(page) + ".png"));
}
bool DialogShown(ImGuiTestContext *ctx) {
    ImGuiWindow *window = ctx->GetWindowByRef("//###PrintParameters");
    return window && window->WasActive;
}
}

void RegisterPrintTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = IM_REGISTER_TEST(engine, "printing", "cancel_pages");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        IM_CHECK(DialogShown(ctx));
        const auto folder = Output("test-print-cancel");
        ClearPages(folder);
        const bool dirty = App().State().document->Dirty();
        const auto steps = App().State().History().UndoCount();
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(!DialogShown(ctx));
        IM_CHECK_EQ(App().State().document->Dirty(), dirty);
        IM_CHECK_EQ(App().State().History().UndoCount(), steps);
        IM_CHECK(!std::filesystem::exists(folder / "page1.png"));
        bool cancelled = false;
        for (const auto &entry : App().State().log) cancelled |= entry.text == "Print cancelled";
        IM_CHECK(cancelled);
    };
    t = IM_REGISTER_TEST(engine, "printing", "four_pages");
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx);
        IM_CHECK(DialogShown(ctx));
        const auto folder = Output("test-print-pages");
        ClearPages(folder);
        const bool dirty = App().State().document->Dirty();
        const auto steps = App().State().History().UndoCount();
        ctx->ItemInput("//###PrintParameters/##pagesH");
        ctx->KeyCharsReplace("2");
        ctx->ItemInput("//###PrintParameters/##pagesV");
        ctx->KeyCharsReplace("2");
        ctx->ItemClick("//###PrintParameters/OK/##dialogbutton");
        ctx->Yield(3);
        IM_CHECK(!DialogShown(ctx));
        int visiblePages = 0;
        int pageWidth = 0, pageHeight = 0;
        for (int page = 1; page <= 4; ++page) {
            std::vector<unsigned char> pixels;
            int width = 0, height = 0;
            IM_CHECK(vdev::ReadImageRgba((folder / ("page" + std::to_string(page) + ".png")).u8string(), pixels, width, height));
            IM_CHECK(width > 0 && height > 0);
            if (page == 1) { pageWidth = width; pageHeight = height; }
            IM_CHECK_EQ(width, pageWidth);
            IM_CHECK_EQ(height, pageHeight);
            IM_CHECK_EQ(pixels.size(), static_cast<size_t>(width) * height * 4);
            size_t ink = 0;
            bool opaque = true;
            for (size_t i = 0; i < pixels.size(); i += 4) {
                opaque &= pixels[i+3] == 255;
                ink += pixels[i] < 240 || pixels[i+1] < 240 || pixels[i+2] < 240;
            }
            IM_CHECK(opaque);
            if (ink > 100) ++visiblePages;
        }
        IM_CHECK(visiblePages >= 2);
        IM_CHECK(!std::filesystem::exists(folder / "page5.png"));
        IM_CHECK_EQ(App().State().document->Dirty(), dirty);
        IM_CHECK_EQ(App().State().History().UndoCount(), steps);
    };
}
}
