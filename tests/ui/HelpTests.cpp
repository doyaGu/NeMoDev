#include "core/runtime/Resources.h"
#include "imgui_internal.h"
#include "tests/ui/UiTests.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace vdevuitest {
// The complete-book matrix measures real ImGui fonts and decodes source images,
// but does not enqueue thousands of GPU textures in a single test frame.
class HelpLayoutProbe : public HtmlRenderDevice {
    class ImageData : public HtmlImage {
    public:
        HtmlSize size;
        std::string url;
        HtmlSize Size() const override { return size; }
    };
    vdev::HtmlRenderer m_Fonts;

public:
    std::vector<vdev::HtmlRenderer::Drawing> drawings;
    explicit HelpLayoutProbe(ImFont *font) : m_Fonts({font, font, font}) {}
    std::shared_ptr<HtmlFont> CreateFont(const HtmlFontDescription &style) override {
        return m_Fonts.CreateFont(style);
    }
    std::shared_ptr<HtmlImage> DecodeImage(const std::string &url, const std::vector<unsigned char> &bytes) override {
        std::vector<unsigned char> pixels;
        int width = 0, height = 0;
        if (!vdev::DecodeImageRgba(bytes, pixels, width, height))
            return {};
        auto image = std::make_shared<ImageData>();
        image->size = {float(width), float(height)};
        image->url = url;
        return image;
    }
    std::shared_ptr<HtmlImage> CreateImage(unsigned width, unsigned height, const unsigned char *) override {
        auto image = std::make_shared<ImageData>();
        image->size = {float(width), float(height)};
        return image;
    }
    void SetClip(const std::vector<HtmlPoint> &) override {}
    void Text(const HtmlFont &font, std::string_view text, HtmlPoint at, HtmlColor color) override {
        vdev::HtmlRenderer::Drawing draw;
        draw.kind = vdev::HtmlRenderer::Drawing::Text;
        draw.text = std::string(text);
        draw.position = ImVec2(at.x, at.y);
        draw.size = ImVec2(font.Width(text), font.Metrics().height);
        draw.color = color;
        drawings.push_back(std::move(draw));
    }
    void Image(const HtmlImage &image, HtmlRect rect) override {
        vdev::HtmlRenderer::Drawing draw;
        draw.kind = vdev::HtmlRenderer::Drawing::Image;
        draw.image = static_cast<const ImageData &>(image).url;
        draw.position = ImVec2(rect.x, rect.y);
        draw.size = ImVec2(rect.width, rect.height);
        drawings.push_back(std::move(draw));
    }
    void Fill(const std::vector<HtmlPoint> &points, HtmlColor) override {
        for (const auto &p : points)
            if (!std::isfinite(p.x) || !std::isfinite(p.y))
                throw std::runtime_error("Invalid HTML paint polygon");
    }
};
static ImGuiWindow *HelpArticle(ImGuiTestContext *ctx) {
    for (ImGuiWindow *child : ctx->GetWindowByRef("//Help")->DC.ChildWindows)
        if (std::strstr(child->Name, "/article_"))
            return child;
    return nullptr;
}
static bool HelpFrame(ImGuiTestContext *ctx, const char *name, bool checkStyles = false) {
    return Read<bool>(ctx, [=] {
        std::vector<unsigned char> pixels;
        int width = 0, height = 0;
        if (!vdev::MainRenderer().ReadFrame(pixels, width, height))
            return false;
        size_t white = 0, section = 0, purple = 0;
        for (size_t at = 0; at < pixels.size(); at += 4) {
            white += pixels[at] == 255 && pixels[at + 1] == 255 && pixels[at + 2] == 255;
            section += pixels[at] == 242 && pixels[at + 1] == 242 && pixels[at + 2] == 242;
            purple += pixels[at] == 102 && pixels[at + 1] == 0 && pixels[at + 2] == 102;
            std::swap(pixels[at], pixels[at + 2]);
        }
        const auto directory = std::filesystem::u8path(SDL_GetBasePath()) / "test-help";
        std::filesystem::create_directories(directory);
        return (!checkStyles || (white > 5000 && section > 1000 && purple > 5)) &&
               vdev::WriteBgraPng((directory / name).u8string(), width, height, pixels.data());
    });
}
void RegisterHelpTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = IM_REGISTER_TEST(engine, "help", "contents_search_history");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        const bool dirty = App().State().document->Dirty();
        const int history = App().State().History().UndoCount();
        ctx->MenuClick("//##MainMenuBar/Help/Online Reference");
        ctx->Yield(4);
        IM_CHECK(InFront(ctx, "//Help"));
        IM_CHECK(App().Help().CurrentTopic() == "welcome/0_welcome.html");
        ctx->ItemClick("//Help/**/Interface");
        ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "ui/0_interface_index.html");
        ctx->ItemOpen("//Help/**/Interface", ImGuiTestOpFlags_MoveToEdgeL);
        ctx->ItemClick("//Help/**/Building Blocks Resource");
        ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "ui/4_building_blocks_resource.html");
        ctx->ItemClick("//Help/Home");
        ctx->Yield(3);
        ctx->ItemInputValue("//Help/**/##search", "Key Event");
        ctx->ItemClick("//Help/**/Key Event BB");
        ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "behaviors/Controllers/Key Event.html");
        IM_CHECK(App().Help().ShownText().find("Activates one output") != std::string::npos);
        IM_CHECK(App().Help().ShownText().find("Key State:") != std::string::npos);
        IM_CHECK_GT(App().Help().ShownImages(), size_t(0));
        const ImRect copy = ctx->ItemInfo("//Help/**/Copy text").RectFull;
        const bool headerVisible = Read<bool>(ctx, [copy] {
            std::vector<unsigned char> pixels;
            int width = 0, height = 0;
            if (!vdev::MainRenderer().ReadFrame(pixels, width, height))
                return false;
            size_t ink = 0;
            for (int y = std::max(0, int(copy.Min.y) + 2); y < std::min(height, int(copy.Max.y) - 2); ++y)
                for (int x = std::max(0, int(copy.Min.x) + 2); x < std::min(width, int(copy.Max.x) - 2); ++x) {
                    const size_t at = (size_t(y) * width + x) * 4;
                    ink += pixels[at] < 100 && pixels[at + 1] < 100 && pixels[at + 2] < 100;
                }
            return ink > 10;
        });
        IM_CHECK(headerVisible);
        ctx->ItemClick("//Help/**/Copy text");
        IM_CHECK(Read<std::string>(ctx, [] {
                     char *text = SDL_GetClipboardText();
                     std::string copy = text ? text : "";
                     SDL_free(text);
                     return copy;
                 }).find("Key State:") != std::string::npos);
        const auto clipboardFailure = Read<std::string>(ctx, [] {
            char *text = SDL_GetClipboardText();
            std::string copied = text ? text : "";
            SDL_free(text);
            // SDL's Windows clipboard stores native CRLF line endings.
            for (size_t at = 0; (at = copied.find("\r\n", at)) != std::string::npos; ++at)
                copied.erase(at, 1);
            const auto &expected = App().Help().PageDocument().Text();
            size_t at = 0;
            while (at < copied.size() && at < expected.size() && copied[at] == expected[at])
                ++at;
            if (at == copied.size() && at == expected.size())
                return std::string();
            return "Clipboard differs at " + std::to_string(at) + ", copied " +
                   (at < copied.size() ? std::to_string(static_cast<unsigned char>(copied[at])) : "end") +
                   ", expected " +
                   (at < expected.size() ? std::to_string(static_cast<unsigned char>(expected[at])) : "end") +
                   ", lengths " + std::to_string(copied.size()) + "/" + std::to_string(expected.size());
        });
        if (!clipboardFailure.empty())
            ctx->LogError("%s", clipboardFailure.c_str());
        IM_CHECK(clipboardFailure.empty());
        ctx->ItemClick("//Help/**/BEOBJECT##help-link");
        ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "concepts/class_hierarchy.html");
        ctx->ItemClick("//Help/Back");
        ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "behaviors/Controllers/Key Event.html");
        ctx->ItemNavActivate("//Help/**/BEOBJECT##help-link");
        ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "concepts/class_hierarchy.html");
        ctx->ItemClick("//Help/Back");
        ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "behaviors/Controllers/Key Event.html");
        ctx->ItemClick("//Help/Back");
        ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "welcome/0_welcome.html");
        ctx->ItemClick("//Help/Forward");
        ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "behaviors/Controllers/Key Event.html");
        ctx->ItemClick("//Help/Home");
        ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "welcome/0_welcome.html");
        IM_CHECK(App().State().document->Dirty() == dirty);
        IM_CHECK_EQ(App().State().History().UndoCount(), history);
    };
    t = IM_REGISTER_TEST(engine, "help", "tables_anchors_scroll_and_close");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        OnMainThread(ctx, [] { App().State().events.Post(vdev::ShowHelpTopic{"concepts/8_parameters.html#top"}); });
        ctx->Yield(4);
        IM_CHECK(InFront(ctx, "//Help"));
        IM_CHECK_GT(App().Help().ShownImages(), size_t(3));
        IM_CHECK(App().Help().ShownText().find("Parameter Output, pOut") != std::string::npos);
        ImGuiWindow *article = HelpArticle(ctx);
        IM_CHECK(article);
        const float start = article->Scroll.y;
        ctx->MouseMoveToPos(article->InnerRect.GetCenter());
        ctx->MouseWheelY(-8.0f);
        ctx->Yield(4);
        const float scroll = article->Scroll.y;
        IM_CHECK_GT(scroll, start + 100.0f);
        ctx->ItemClick("//Help/Home");
        ctx->Yield(3);
        ctx->ItemClick("//Help/Back");
        ctx->Yield(4);
        IM_CHECK(std::fabs(article->Scroll.y - scroll) < 2.0f);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_F);
        ctx->Yield(2);
        IM_CHECK_EQ(ImGui::GetActiveID(), ctx->ItemInfo("//Help/**/##search").ID);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->WindowClose("//Help");
        ctx->Yield(3);
        IM_CHECK(!InFront(ctx, "//Help"));
        ctx->MenuClick("//##MainMenuBar/Help/Online Reference");
        ctx->Yield(4);
        IM_CHECK(InFront(ctx, "//Help"));
        IM_CHECK(App().Help().CurrentTopic() == "welcome/0_welcome.html");
        ctx->ItemClick("//Help/Back");
        ctx->Yield(4);
        IM_CHECK(App().Help().CurrentTopic() == "concepts/8_parameters.html#top");
        IM_CHECK(std::fabs(article->Scroll.y - scroll) < 2.0f);
    };
    t = IM_REGISTER_TEST(engine, "help", "anchor_revisit_and_horizontal_history");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        OnMainThread(ctx, [] { App().State().events.Post(vdev::ShowHelpTopic{"concepts/8_parameters.html#top"}); });
        ctx->Yield(4);
        ImGuiWindow *article = HelpArticle(ctx);
        IM_CHECK(article);
        const float top = article->Scroll.y;
        ctx->MouseMoveToPos(article->InnerRect.GetCenter());
        ctx->MouseWheelY(-8);
        ctx->Yield(4);
        IM_CHECK_GT(article->Scroll.y, top + 100);
        OnMainThread(ctx, [] { App().State().events.Post(vdev::ShowHelpTopic{"concepts/8_parameters.html#top"}); });
        ctx->Yield(4);
        IM_CHECK(std::fabs(article->Scroll.y - top) < 2);
        OnMainThread(ctx,
                     [] { App().State().events.Post(vdev::ShowHelpTopic{"vsl/CoreLanguageFeatures/1_Types.html"}); });
        ctx->Yield(4);
        ctx->WindowResize("//Help", ImVec2(760, 620));
        ctx->Yield(4);
        ctx->ScrollTo(article->ID, ImGuiAxis_X, 120, ImGuiTestOpFlags_NoFocusWindow);
        ctx->ScrollTo(article->ID, ImGuiAxis_Y, 300, ImGuiTestOpFlags_NoFocusWindow);
        ctx->Yield(3);
        const ImVec2 scroll = article->Scroll;
        IM_CHECK_GT(scroll.x, 100);
        ctx->ItemClick("//Help/Home");
        ctx->Yield(4);
        IM_CHECK_LT(article->Scroll.x, 1);
        ctx->ItemClick("//Help/Back");
        ctx->Yield(4);
        IM_CHECK(std::fabs(article->Scroll.x - scroll.x) < 2 && std::fabs(article->Scroll.y - scroll.y) < 2);
        OnMainThread(ctx, [] {
            App().State().events.Post(vdev::ShowHelpTopic{"scripts/script_body/behavior_building_block.html#editbb"});
        });
        ctx->Yield(4);
        HtmlPoint anchor;
        IM_CHECK(App().Help().PageDocument().FindAnchor("editbb", anchor));
        IM_CHECK_GT(anchor.y, 100);
        IM_CHECK_GE(article->Scroll.y, std::min(article->ScrollMax.y, anchor.y) - 2);
    };
    t = IM_REGISTER_TEST(engine, "help", "building_block_entry_points");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Building Blocks");
        ctx->ItemInputValue("//Building Blocks/##filter", "Rotate");
        for (int action = 0; action < 3; ++action) {
            ctx->WindowFocus("//Building Blocks");
            ctx->MouseMove("//Building Blocks/**/Rotate");
            if (action == 0)
                ctx->KeyPress(ImGuiKey_F1);
            else if (action == 1)
                ctx->ItemDoubleClick("//Building Blocks/**/Rotate");
            else {
                ctx->ItemClick("//Building Blocks/**/Rotate", ImGuiMouseButton_Right);
                ctx->ItemClick("//$FOCUSED/Html Help");
            }
            ctx->Yield(4);
            IM_CHECK(InFront(ctx, "//Help"));
            IM_CHECK(App().Help().CurrentTopic() == "behaviors/3D Transformations/Rotate.html");
            IM_CHECK(App().Help().ShownText().find("Rotate") != std::string::npos);
            ctx->ItemClick("//Help/Home");
            ctx->Yield(3);
        }
        OnMainThread(ctx, [] { App().State().events.Post(vdev::ShowHelpTopic{"missing-source.html"}); });
        ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "missing-source.html");
        ctx->ItemClick("//Help/Back");
        ctx->Yield(3);
        IM_CHECK(App().Help().CurrentTopic() == "welcome/0_welcome.html");
    };
    t = IM_REGISTER_TEST(engine, "help", "source_styles_and_table_geometry");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        OnMainThread(ctx,
                     [] { App().State().events.Post(vdev::ShowHelpTopic{"behaviors/Controllers/Key Event.html"}); });
        ctx->Yield(4);
        bool heading = false, input = false;
        for (const auto &item : App().Help().PagePaint().drawings) {
            if (item.kind == vdev::HtmlRenderer::Drawing::Text && item.text == "Description") {
                heading =
                    item.color == 0xff660066 && (item.style.weight >= 600) && item.font != ImGui::GetIO().FontDefault;
            }
            if (item.kind == vdev::HtmlRenderer::Drawing::Text && item.text == "On:")
                input = item.color == 0xff0000ff && (item.style.weight >= 600);
        }
        if (!heading || !input) {
            for (const auto &issue : App().Help().PageDocument().Diagnostics())
                ctx->LogError("HTML resource: %s", issue.c_str());
            for (const auto &item : App().Help().PagePaint().drawings)
                if (item.text == "Description" || item.text == "On:")
                    ctx->LogError("Style %s color=%x weight=%d face=%s", item.text.c_str(), item.color,
                                  item.style.weight, item.font->GetDebugName());
        }
        IM_CHECK(heading && input);
        IM_CHECK(HelpFrame(ctx, "building-block.png", true));
        OnMainThread(ctx, [] { App().State().events.Post(vdev::ShowHelpTopic{"ui/0_interface_index.html"}); });
        ctx->Yield(4);
        const vdev::HtmlRenderer::Drawing *first = nullptr, *last = nullptr, *map = nullptr;
        for (const auto &item : App().Help().PagePaint().drawings) {
            if (item.image.find("taboff_menu_bar.png") != std::string::npos)
                first = &item;
            if (item.image.find("tabon_status_bar.png") != std::string::npos)
                last = &item;
            if (item.image.find("map_index_interface.png") != std::string::npos)
                map = &item;
        }
        IM_CHECK(first && last && map);
        IM_CHECK(std::fabs(first->position.y - map->position.y) < 1.0f);
        IM_CHECK(std::fabs(map->size.x / first->size.x - 513.0f / 114.0f) < 0.01f);
        IM_CHECK(std::fabs(last->position.y + last->size.y - map->position.y - map->size.y) < 2.0f);
        IM_CHECK(HelpFrame(ctx, "interface-table.png"));
        OnMainThread(ctx,
                     [] { App().State().events.Post(vdev::ShowHelpTopic{"vsl/CoreLanguageFeatures/1_Types.html"}); });
        ctx->Yield(4);
        bool codeBackground = false, mono = false, scalable = false;
        float codeY = -1.0f, tableY = -1.0f;
        for (const auto &item : App().Help().PagePaint().drawings) {
            codeBackground |= item.kind == vdev::HtmlRenderer::Drawing::Fill && item.color == 0xfffaebd7;
            if (item.kind == vdev::HtmlRenderer::Drawing::Text && item.text == "struct") {
                mono = (item.style.families.find("Courier") != std::string::npos ||
                        item.style.families.find("monospace") != std::string::npos) &&
                       std::fabs(item.font->CalcTextSizeA(item.style.size, 10000, 0, "iiii").x -
                                 item.font->CalcTextSizeA(item.style.size, 10000, 0, "WWWW").x) < 0.1f;
                scalable = std::strstr(item.font->GetDebugName(), "ProggyForever") != nullptr;
                if (codeY < 0)
                    codeY = item.position.y;
            }
            if (item.kind == vdev::HtmlRenderer::Drawing::Text && item.text == "bool" && tableY < 0)
                tableY = item.position.y;
        }
        IM_CHECK(codeBackground && mono);
        IM_CHECK(scalable);
        ImGuiWindow *article = HelpArticle(ctx);
        IM_CHECK(article && codeY >= 0 && tableY >= 0);
        ctx->ScrollTo(article->ID, ImGuiAxis_Y, std::max(0.0f, tableY - 80.0f), ImGuiTestOpFlags_NoFocusWindow);
        ctx->Yield(4);
        IM_CHECK(HelpFrame(ctx, "types-table.png"));
        ctx->ScrollTo(article->ID, ImGuiAxis_Y, std::max(0.0f, codeY - 100.0f), ImGuiTestOpFlags_NoFocusWindow);
        ctx->Yield(4);
        IM_CHECK(HelpFrame(ctx, "code.png"));
        ctx->WindowResize("//Help", ImVec2(760, 620));
        ctx->Yield(4);
        IM_CHECK_GT(article->ScrollMax.x, 20.0f);
        ctx->ScrollTo(article->ID, ImGuiAxis_Y, std::max(0.0f, tableY - 80.0f), ImGuiTestOpFlags_NoFocusWindow);
        ctx->Yield(4);
        IM_CHECK(HelpFrame(ctx, "narrow-table.png"));
        OnMainThread(ctx,
                     [] { App().State().events.Post(vdev::ShowHelpTopic{"behaviors/Visuals/Volumetric Fog.html"}); });
        ctx->Yield(4);
        const vdev::HtmlRenderer::Drawing *sample = nullptr, *texture = nullptr, *cloud = nullptr;
        for (const auto &item : App().Help().PagePaint().drawings) {
            if (item.image.find("image000.jpg") != std::string::npos)
                sample = &item;
            if (item.image.find("image001.jpg") != std::string::npos)
                texture = &item;
            if (item.image.find("image002.jpg") != std::string::npos)
                cloud = &item;
        }
        IM_CHECK(sample && texture && cloud);
        IM_CHECK_LE(sample->position.x + sample->size.x, texture->position.x);
        IM_CHECK_LE(texture->position.x + texture->size.x, cloud->position.x);
        const float sampleY = sample->position.y;
        ctx->WindowResize("//Help", ImVec2(1280, 760));
        ctx->Yield(4);
        ctx->ScrollTo(article->ID, ImGuiAxis_Y, std::max(0.0f, sampleY - 120.0f), ImGuiTestOpFlags_NoFocusWindow);
        ctx->Yield(4);
        IM_CHECK(HelpFrame(ctx, "image-columns.png"));
    };
    t = IM_REGISTER_TEST(engine, "help", "image_map_navigation_and_scaling");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        OnMainThread(ctx, [] { App().State().events.Post(vdev::ShowHelpTopic{"concepts/class_hierarchy.html"}); });
        ctx->Yield(4);
        for (float zoom : {0.75f, 2.0f}) {
            ctx->ItemInputValue("//Help/Zoom", zoom);
            ctx->Yield(5);
            float imageWidth = 0, imageLeft = 0, imageTop = 0;
            for (const auto &item : App().Help().PagePaint().drawings)
                if (item.kind == vdev::HtmlRenderer::Drawing::Image &&
                    item.image.find("class_hierarchy") != std::string::npos) {
                    imageWidth = item.size.x;
                    imageLeft = item.position.x;
                    imageTop = item.position.y;
                }
            IM_CHECK_GT(imageWidth, 0);
            ImGuiWindow *article = HelpArticle(ctx);
            IM_CHECK(article);
            ctx->ScrollTo(article->ID, ImGuiAxis_X,
                          std::max(0.0f, imageLeft + imageWidth * 348.5f / 697.0f - article->InnerRect.GetWidth() / 2),
                          ImGuiTestOpFlags_NoFocusWindow);
            const float imageY = article->DC.CursorMaxPos.y - App().Help().PagePaint().size.y + imageTop;
            ctx->ScrollTo(article->ID, ImGuiAxis_Y,
                          std::max(0.0f, article->Scroll.y + imageY - article->InnerRect.Min.y - 100),
                          ImGuiTestOpFlags_NoFocusWindow);
            ctx->Yield(3);
            const auto area = ctx->ItemInfo("//Help/**/book:\\/\\/help\\/beobjects\\/1_ckbeobject.html##help-link");
            IM_CHECK(std::fabs(area.RectFull.GetWidth() / imageWidth - 95.0f / 697.0f) < 0.001f);
            if (zoom < 1)
                ctx->ItemClick(area.ID);
            else
                ctx->ItemNavActivate(area.ID);
            ctx->Yield(4);
            IM_CHECK(App().Help().CurrentTopic() == "beobjects/1_ckbeobject.html");
            ctx->ItemClick("//Help/Back");
            ctx->Yield(4);
            IM_CHECK(App().Help().CurrentTopic() == "concepts/class_hierarchy.html");
        }
        IM_CHECK(HelpFrame(ctx, "image-map-zoom.png"));
    };
    t = IM_REGISTER_TEST(engine, "help", "zoom_reading_position_and_history");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        OnMainThread(ctx,
                     [] { App().State().events.Post(vdev::ShowHelpTopic{"vsl/CoreLanguageFeatures/1_Types.html"}); });
        ctx->Yield(4);
        ctx->WindowResize("//Help", ImVec2(1280, 760));
        ctx->ItemInputValue("//Help/Zoom", 1.0f);
        ctx->Yield(5);
        ImGuiWindow *article = HelpArticle(ctx);
        IM_CHECK(article);
        const auto codeY = [&]() {
            for (const auto &item : App().Help().PagePaint().drawings)
                if (item.text == "struct")
                    return article->DC.CursorMaxPos.y - App().Help().PagePaint().size.y + item.position.y;
            return -100000.0f;
        };
        IM_CHECK_GT(codeY(), -10000);
        ctx->ScrollTo(article->ID, ImGuiAxis_Y, article->Scroll.y + codeY() - article->InnerRect.Min.y + 3,
                      ImGuiTestOpFlags_NoFocusWindow);
        ctx->Yield(3);
        IM_CHECK(std::fabs(codeY() - article->InnerRect.Min.y) < 6);
        ctx->ItemInputValue("//Help/Zoom", 2.0f);
        ctx->Yield(5);
        IM_CHECK(std::fabs(codeY() - article->InnerRect.Min.y) < 12);
        ctx->WindowResize("//Help", ImVec2(900, 680));
        ctx->Yield(5);
        IM_CHECK(std::fabs(codeY() - article->InnerRect.Min.y) < 12);
        ctx->ItemClick("//Help/Home");
        ctx->Yield(4);
        ctx->ItemInputValue("//Help/Zoom", .75f);
        ctx->Yield(5);
        ctx->ItemClick("//Help/Back");
        ctx->Yield(5);
        IM_CHECK(std::fabs(codeY() - article->InnerRect.Min.y) < 12);
        ctx->ItemClick("//Help/Forward");
        ctx->Yield(4);
        IM_CHECK(App().Help().CurrentTopic() == "welcome/0_welcome.html");
        ctx->ItemInputValue("//Help/Zoom", 1.5f);
        ctx->Yield(5);
        ctx->ItemClick("//Help/Back");
        ctx->Yield(5);
        IM_CHECK(std::fabs(codeY() - article->InnerRect.Min.y) < 12);
        IM_CHECK(HelpFrame(ctx, "zoom-reading-position.png"));
    };
    t = IM_REGISTER_TEST(engine, "help", "zoom_limits_and_scrollbar");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        OnMainThread(ctx, [] { App().State().events.Post(vdev::ShowHelpTopic{"concepts/class_hierarchy.html"}); });
        ctx->Yield(4);
        ctx->WindowResize("//Help", ImVec2(1280, 760));
        const auto imageWidth = [] {
            for (const auto &item : App().Help().PagePaint().drawings)
                if (item.image.find("class_hierarchy") != std::string::npos)
                    return item.size.x;
            return 0.0f;
        };
        ctx->ItemInputValue("//Help/Zoom", 2.0f);
        ctx->Yield(5);
        const float maximum = imageWidth();
        IM_CHECK_GT(maximum, 100);
        ctx->ItemInputValue("//Help/Zoom", 3.0f);
        ctx->Yield(5);
        IM_CHECK(std::fabs(imageWidth() - maximum) < 1);
        ctx->ItemInputValue("//Help/Zoom", .75f);
        ctx->Yield(5);
        const float minimum = imageWidth();
        ctx->ItemInputValue("//Help/Zoom", -1.0f);
        ctx->Yield(5);
        IM_CHECK(std::fabs(imageWidth() - minimum) < 1);
        ImGuiWindow *article = HelpArticle(ctx);
        IM_CHECK(article);
        const float extraWidth = std::max(0.0f, imageWidth() - article->InnerRect.GetWidth());
        ctx->WindowResize("//Help", ImVec2(1280 + extraWidth + 64, 760));
        ctx->Yield(5);
        IM_CHECK_LT(imageWidth(), article->InnerRect.GetWidth());
        IM_CHECK_LT(article->ScrollMax.x, 1);
        IM_CHECK(HelpFrame(ctx, "zoom-out-scrollbar.png"));
    };
    t = IM_REGISTER_TEST(engine, "help", "recovered_script_image_does_not_cover_text");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        OnMainThread(ctx, [] {
            App().State().events.Post(vdev::ShowHelpTopic{"ui/7_schematic/toolbar/2_schematic_explorer.html"});
        });
        ctx->Yield(4);
        ctx->WindowResize("//Help", ImVec2(1280, 760));
        ctx->ItemInputValue("//Help/Zoom", 1.0f);
        ctx->Yield(5);
        const vdev::HtmlRenderer::Drawing *script = nullptr;
        for (const auto &item : App().Help().PagePaint().drawings)
            if (item.image == "book://help/_recovered/filter_script.png")
                script = &item;
        IM_CHECK(script);
        ImGuiWindow *article = HelpArticle(ctx);
        IM_CHECK(article);
        const float imageY = article->DC.CursorMaxPos.y - App().Help().PagePaint().size.y + script->position.y;
        ctx->ScrollTo(article->ID, ImGuiAxis_Y, article->Scroll.y + imageY - article->InnerRect.Min.y - 80,
                      ImGuiTestOpFlags_NoFocusWindow);
        ctx->Yield(4);
        const float right = article->DC.CursorStartPos.x + script->position.x + script->size.x;
        const float top = article->DC.CursorMaxPos.y - App().Help().PagePaint().size.y + script->position.y;
        const float bottom = top + script->size.y;
        const float left = right - script->size.x;
        const bool glyph = Read<bool>(ctx, [=] {
            std::vector<unsigned char> pixels;
            int width = 0, height = 0;
            if (!vdev::MainRenderer().ReadFrame(pixels, width, height))
                return false;
            size_t black = 0, white = 0, red = 0;
            for (int y = std::max(0, int(std::ceil(top))); y < std::min(height, int(bottom)); ++y)
                for (int x = std::max(0, int(std::ceil(left))); x < std::min(width, int(right)); ++x) {
                    const size_t at = (size_t(y) * width + x) * 4;
                    black += pixels[at] < 80 && pixels[at + 1] < 80 && pixels[at + 2] < 80;
                    white += pixels[at] > 230 && pixels[at + 1] > 230 && pixels[at + 2] > 230;
                    red += pixels[at] == 160 && pixels[at + 1] == 0 && pixels[at + 2] == 0;
                }
            return black > 4 && white > 4 && red == 0;
        });
        IM_CHECK(glyph);
        const size_t overflow = Read<size_t>(ctx, [=] {
            std::vector<unsigned char> pixels;
            int width = 0, height = 0;
            if (!vdev::MainRenderer().ReadFrame(pixels, width, height))
                return size_t(-1);
            size_t red = 0;
            for (int y = std::max(0, int(std::floor(top))); y < std::min(height, int(std::ceil(bottom))); ++y)
                for (int x = std::max(0, int(std::ceil(right)) + 2); x < std::min(width, int(right) + 80); ++x) {
                    const size_t at = (size_t(y) * width + x) * 4;
                    red += pixels[at] == 160 && pixels[at + 1] == 0 && pixels[at + 2] == 0;
                }
            return red;
        });
        IM_CHECK_EQ(overflow, size_t(0));
        IM_CHECK(HelpFrame(ctx, "recovered-script-image.png"));
    };
    t = IM_REGISTER_TEST(engine, "help", "recovered_source_links_and_hotspots");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        OnMainThread(ctx, [] { App().State().events.Post(vdev::ShowHelpTopic{"beobjects/1_ckgroup.html"}); });
        ctx->Yield(4);
        ctx->WindowResize("//Help", ImVec2(1280, 760));
        ctx->ItemInputValue("//Help/Zoom", 1.0f);
        ctx->Yield(4);
        ImGuiWindow *article = HelpArticle(ctx);
        IM_CHECK(article);
        const HtmlLink *link = nullptr;
        for (const auto &item : App().Help().PageDocument().Links())
            if (item.navigation.url == "book://help/behaviors/Logics/_Cat_Logics.html#groups") {
                link = &item;
                break;
            }
        IM_CHECK(link && !link->bounds.empty());
        const float linkY = article->DC.CursorMaxPos.y - App().Help().PagePaint().size.y + link->bounds[0].y;
        ctx->ScrollTo(
            article->ID, ImGuiAxis_Y,
            std::clamp(article->Scroll.y + linkY - article->InnerRect.Min.y - 120, 0.0f, article->ScrollMax.y),
            ImGuiTestOpFlags_NoFocusWindow);
        ctx->Yield(4);
        ctx->MouseMove("//Help/**/Level\\/Groups##help-link");
        const float scroll = article->Scroll.y;
        ctx->MouseClick();
        ctx->Yield(4);
        IM_CHECK(App().Help().CurrentTopic() == "behaviors/Logics/_Cat_Logics.html#groups");
        HtmlPoint anchor;
        IM_CHECK(App().Help().PageDocument().FindAnchor("groups", anchor));
        IM_CHECK_GT(anchor.y, 500);
        IM_CHECK_GE(article->Scroll.y, std::min(article->ScrollMax.y, anchor.y) - 2);
        bool heading = false;
        for (const auto &item : App().Help().PagePaint().drawings)
            if (item.text == "Groups") {
                const float y = article->DC.CursorMaxPos.y - App().Help().PagePaint().size.y + item.position.y;
                heading = y >= article->InnerRect.Min.y - 2 && y < article->InnerRect.Min.y + 64;
            }
        IM_CHECK(heading);
        IM_CHECK(HelpFrame(ctx, "recovered-groups-anchor.png"));
        ctx->ItemClick("//Help/Back");
        ctx->Yield(4);
        IM_CHECK(App().Help().CurrentTopic() == "beobjects/1_ckgroup.html");
        IM_CHECK(std::fabs(article->Scroll.y - scroll) < 2);
        ctx->ItemClick("//Help/Forward");
        ctx->Yield(4);
        IM_CHECK(App().Help().CurrentTopic() == "behaviors/Logics/_Cat_Logics.html#groups");
        OnMainThread(ctx,
                     [] { App().State().events.Post(vdev::ShowHelpTopic{"ui/7_schematic/0_schematic_index.html"}); });
        ctx->Yield(4);
        // Three ordinary links and the original 35 valid hotspots. The broken
        // four-coordinate polygon must not become a clickable rectangle.
        IM_CHECK_EQ(App().Help().PageDocument().Links().size(), size_t(38));
        // The repaired label belongs to the existing toolbar rectangle. Activate
        // it through navigation as well as checking the native map's geometry.
        const vdev::HtmlRenderer::Drawing *diagram = nullptr;
        for (const auto &item : App().Help().PagePaint().drawings)
            if (item.image == "book://help/images/schematic_map.png")
                diagram = &item;
        IM_CHECK(diagram);
        ctx->ScrollTo(
            article->ID, ImGuiAxis_X,
            std::max(0.0f, diagram->position.x + diagram->size.x * 546.5f / 888 - article->InnerRect.GetWidth() / 2),
            ImGuiTestOpFlags_NoFocusWindow);
        const float diagramY = article->DC.CursorMaxPos.y - App().Help().PagePaint().size.y + diagram->position.y;
        ctx->ScrollTo(article->ID, ImGuiAxis_Y, article->Scroll.y + diagramY - article->InnerRect.Min.y - 80,
                      ImGuiTestOpFlags_NoFocusWindow);
        ctx->Yield(4);
        const auto hotspot = ctx->ItemInfo("//Help/**/Show\\/Hide Link Information##help-link");
        ctx->ItemNavActivate(hotspot.ID);
        ctx->Yield(4);
        IM_CHECK(App().Help().CurrentTopic() == "ui/7_schematic/toolbar/9_sh_links_informations.html");
        IM_CHECK(App().Help().ShownText().find("show/hide link delays") != std::string::npos);
    };
    t = IM_REGISTER_TEST(engine, "help", "local_resource_attachments");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        OnMainThread(ctx, [] { App().State().events.Post(vdev::ShowHelpTopic{"import/3dsmax/0_3dsmax_index.html"}); });
        ctx->Yield(4);
        ctx->ItemClick("//Help/**/Readme.txt for Exporter Plugin##help-link");
        ctx->Yield(4);
        IM_CHECK(App().Help().CurrentTopic() == "import/3dsmax/Max2Virtools.txt");
        IM_CHECK(App().Help().ShownText().find("Virtools Exporter for 3DS Max") != std::string::npos);
        ctx->ItemClick("//Help/Back");
        ctx->Yield(4);
        IM_CHECK(App().Help().CurrentTopic() == "import/3dsmax/0_3dsmax_index.html");
        ctx->ItemClick("//Help/Forward");
        ctx->Yield(4);
        IM_CHECK(App().Help().CurrentTopic() == "import/3dsmax/Max2Virtools.txt");
        OnMainThread(ctx, [] { App().State().events.Post(vdev::ShowHelpTopic{"images/vsl_display_instances.png"}); });
        ctx->Yield(4);
        IM_CHECK_EQ(App().Help().ShownImages(), size_t(1));
        IM_CHECK(App().Help().PageDocument().Diagnostics().empty());
        IM_CHECK(HelpFrame(ctx, "resource-attachment.png"));
    };
    t = IM_REGISTER_TEST(engine, "help", "reference_layout_at_multiple_widths");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        const std::string failure = Read<std::string>(ctx, [] {
            vdev::HelpBook book;
            std::string error;
            if (!book.Load(vdev::DataFilePath("help"), error))
                return error;
            std::set<std::string> topics;
            std::function<void(const std::vector<vdev::HelpContents> &)> collect = [&](const auto &items) {
                for (const auto &item : items) {
                    if (!item.topic.empty())
                        topics.insert(item.topic);
                    collect(item.children);
                }
            };
            collect(book.Contents());
            for (const char *attachment :
                 {"import/3dsmax/Max2Virtools.txt", "import/Lightwave/readme.txt", "images/vsl_display_instances.png"})
                topics.insert(attachment);
            ImFont *font = ImGui::GetIO().FontDefault;
            for (const auto &path : topics) {
                const auto *topic = book.Find(path);
                if (!topic)
                    return "Missing topic: " + path;
                for (float scale : {1.0f, 1.5f})
                    for (float width : {320.0f, 960.0f}) {
                        HelpLayoutProbe renderer(font);
                        HtmlDocument document(book, renderer);
                        if (!document.Load(topic->html, vdev::HelpBook::Url(topic->path), error) ||
                            !document.Layout({width, 800}, scale, error))
                            return path + ": " + error;
                        const auto size = document.ContentSize();
                        document.Draw({}, {0, 0, size.width, size.height});
                        vdev::HelpWindow::PagePainting page{std::move(renderer.drawings),
                                                            ImVec2(size.width, size.height)};
                        if (!std::isfinite(page.size.x) || !std::isfinite(page.size.y) || page.size.x < width ||
                            page.size.y < 0)
                            return "Invalid page size: " + path;
                        for (const auto &item : page.drawings) {
                            if (!std::isfinite(item.position.x) || !std::isfinite(item.position.y) ||
                                !std::isfinite(item.size.x) || !std::isfinite(item.size.y) || item.size.x < 0 ||
                                item.size.y < 0 || item.position.x + item.size.x > page.size.x + 2 ||
                                item.position.y + item.size.y > page.size.y + 2)
                                return "Content outside scrollable page: " + path + " [" + item.text + item.image +
                                       "] at " + std::to_string(item.position.x) + "," +
                                       std::to_string(item.position.y) + " size " + std::to_string(item.size.x) + "," +
                                       std::to_string(item.size.y) + " page " + std::to_string(page.size.x) + "," +
                                       std::to_string(page.size.y) + " width " + std::to_string(width) + " scale " +
                                       std::to_string(scale);
                        }
                    }
            }
            return std::string();
        });
        if (!failure.empty())
            ctx->LogError("%s", failure.c_str());
        IM_CHECK(failure.empty());
    };
}
} // namespace vdevuitest
