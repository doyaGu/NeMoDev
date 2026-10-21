#include "tests/ui/UiTests.h"
#include "ui/render/HtmlRenderer.h"
#include "ui/render/UiRenderer.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace vdevuitest {
class HtmlUiResources : public HtmlResources {
public:
    bool Read(const std::string &url, std::vector<unsigned char> &bytes, std::string &error) override {
        if (url != "book://tests/checker.bmp") {
            error = "Not found";
            return false;
        }
        bytes.assign(70, 0);
        bytes[0] = 'B';
        bytes[1] = 'M';
        bytes[2] = 70;
        bytes[10] = 54;
        bytes[14] = 40;
        bytes[18] = 2;
        bytes[22] = 2;
        bytes[26] = 1;
        bytes[28] = 24;
        bytes[34] = 16;
        const unsigned char pixels[] = {255, 0, 0, 255, 255, 255, 0, 0, 0, 0, 255, 0, 255, 0, 0, 0};
        std::copy(std::begin(pixels), std::end(pixels), bytes.begin() + 54);
        return true;
    }
};
struct HtmlUiFixture {
    HtmlUiResources resources;
    vdev::HtmlRenderer renderer;
    HtmlPoint circle, image;
    HtmlPoint origin, head, first, second, third;
    float scale = 1;
    std::string error;
    bool rendered = false, detailed = false;
    HtmlUiFixture() : renderer({ImGui::GetIO().FontDefault, ImGui::GetIO().FontDefault, ImGui::GetIO().FontDefault}) {}
    void Draw() {
        ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_Appearing);
        const float width = detailed ? std::max(900.0f, 500 * ImGui::GetFontSize() / 14 * scale + 32) : 900.0f;
        ImGui::SetNextWindowSize(ImVec2(width, 700), ImGuiCond_Always);
        if (ImGui::Begin("HTML Renderer")) {
            const auto start = ImGui::GetCursorScreenPos();
            origin = {start.x, start.y};
            const auto available = ImGui::GetContentRegionAvail();
            const float pixels = ImGui::GetFontSize() / 14.0f * scale;
            auto *draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(start, ImVec2(start.x + available.x, start.y + available.y), 0xffffffff);
            // Destroying the document before frame submission must not retire an
            // image still referenced by the draw commands emitted below.
            HtmlDocument document(resources, renderer);
            const std::string basic =
                "<!doctype "
                "html><style>body{margin:0}#circle{width:100px;height:100px;background:red;border-"
                "radius:50px}</style>"
                "<div id=circle></div><img id=picture src=checker.bmp width=64 height=64>"
                "<span style='font-size:0'>Hidden</span>";
            const std::string details =
                "<!doctype html><style>body{margin:0}#border{position:absolute;left:0;top:0;width:80px;height:80px;"
                "box-sizing:border-box;border:12px solid red;border-radius:40px;background:blue}"
                "#clip{position:absolute;left:100px;top:0;width:32px;height:32px;overflow:hidden}"
                "#clip div{margin-left:40px;width:32px;height:32px;background:lime}"
                "#repeat{position:absolute;left:100px;top:50px;width:64px;height:32px;"
                "background:url(checker.bmp) repeat;background-size:16px 16px}"
                "#dash,#dot{position:absolute;top:0;width:120px;height:120px;box-sizing:border-box;"
                "border:8px dashed red;border-radius:60px;background:blue}"
                "#dash{left:200px}#dot{left:340px;border-style:dotted}"
                "table{position:absolute;left:0;top:100px;width:180px;border-collapse:collapse}"
                "td{height:20px;padding:0;border:2px solid black}#head{background:red}#first{background:lime}"
                "#second{background:blue}#third{background:yellow}</style>"
                "<div id=border></div><div id=clip><div></div></div><div id=repeat></div>"
                "<div id=dash></div><div id=dot></div>"
                "<table><tr><td id=head colspan=2></td></tr><tr><td id=first rowspan=2></td><td id=second></td></tr>"
                "<tr><td id=third></td></tr></table>";
            rendered = document.Load(detailed ? details : basic, "book://tests/index.html", error) &&
                       document.Layout({available.x, available.y}, pixels, error);
            if (rendered) {
                renderer.Begin(draw);
                try {
                    document.Draw(origin, {0, 0, available.x, available.y});
                    if (detailed) {
                        rendered = document.Diagnostics().empty() && document.FindAnchor("head", head) &&
                                   document.FindAnchor("first", first) && document.FindAnchor("second", second) &&
                                   document.FindAnchor("third", third);
                    } else {
                        rendered = document.Diagnostics().empty() && document.FindAnchor("circle", circle) &&
                                   document.FindAnchor("picture", image);
                        const auto hidden = renderer.CreateFont({"sans-serif", 0, 400, false, 0});
                        rendered =
                            rendered && hidden && hidden->Width("Hidden") == 0 && hidden->Metrics().height == 0 &&
                            std::none_of(renderer.Drawings().begin(), renderer.Drawings().end(), [](const auto &item) {
                                return item.kind == vdev::HtmlRenderer::Drawing::Text && item.text == "Hidden";
                            });
                        circle.x += origin.x;
                        circle.y += origin.y;
                        image.x += origin.x;
                        image.y += origin.y;
                    }
                } catch (const std::exception &failure) {
                    error = failure.what();
                    rendered = false;
                }
                ImGui::Dummy(ImVec2(document.ContentSize().width, document.ContentSize().height));
            }
        }
        ImGui::End();
    }
};
static std::unique_ptr<HtmlUiFixture> g_HtmlFixture;
static bool g_HtmlVisible = false;
struct HtmlUiLifetime {
    ~HtmlUiLifetime() {
        g_HtmlVisible = false;
        g_HtmlFixture.reset();
    }
};
void RegisterHtmlTests(ImGuiTestEngine *engine) {
    auto *test = IM_REGISTER_TEST(engine, "html", "rounded_clip_image_pixels_and_resource_lifetime");
    test->SetVarsDataType<HtmlUiLifetime>();
    test->GuiFunc = [](ImGuiTestContext *ctx) {
        MainThreadGui(ctx);
        if (g_HtmlVisible) {
            if (!g_HtmlFixture)
                g_HtmlFixture = std::make_unique<HtmlUiFixture>();
            g_HtmlFixture->Draw();
        }
    };
    test->TestFunc = [](ImGuiTestContext *ctx) {
        OnMainThread(ctx, [] { g_HtmlVisible = true; });
        for (float zoom : {1.0f, 1.5f}) {
            OnMainThread(ctx, [=] { g_HtmlFixture->scale = zoom; });
            ctx->Yield(5);
            ctx->WindowFocus("//HTML Renderer");
            ctx->Yield(3);
            IM_CHECK(g_HtmlFixture->rendered);
            IM_CHECK(g_HtmlFixture->error.empty());
            const auto failure = Read<std::string>(ctx, [] {
                std::vector<unsigned char> rgba;
                int width = 0, height = 0;
                if (!vdev::MainRenderer().ReadFrame(rgba, width, height))
                    return std::string("Frame readback failed");
                const ImVec2 density = ImGui::GetIO().DisplayFramebufferScale;
                const auto pixel = [&](HtmlPoint origin, float x, float y, unsigned char r, unsigned char g,
                                       unsigned char b) {
                    const float scale = ImGui::GetFontSize() / 14.0f * g_HtmlFixture->scale;
                    const int px = int((origin.x + x * scale) * density.x);
                    const int py = int((origin.y + y * scale) * density.y);
                    if (px < 0 || py < 0 || px >= width || py >= height)
                        return false;
                    const size_t at = (size_t(py) * width + px) * 4;
                    return std::abs(int(rgba[at]) - r) < 15 && std::abs(int(rgba[at + 1]) - g) < 15 &&
                           std::abs(int(rgba[at + 2]) - b) < 15;
                };
                if (!pixel(g_HtmlFixture->circle, 50, 50, 255, 0, 0))
                    return std::string("Rounded background center is not red");
                if (!pixel(g_HtmlFixture->circle, 2, 2, 255, 255, 255))
                    return std::string("Rounded clipping painted outside the corner");
                if (!pixel(g_HtmlFixture->image, 8, 8, 255, 0, 0) || !pixel(g_HtmlFixture->image, 56, 8, 0, 255, 0) ||
                    !pixel(g_HtmlFixture->image, 8, 56, 0, 0, 255) ||
                    !pixel(g_HtmlFixture->image, 56, 56, 255, 255, 255))
                    return std::string("Image pixels or their submission lifetime are wrong");
                return std::string();
            });
            if (!failure.empty())
                ctx->LogError("%s", failure.c_str());
            IM_CHECK(failure.empty());
        }
    };
    test = IM_REGISTER_TEST(engine, "html", "table_border_background_and_overflow_pixels");
    test->SetVarsDataType<HtmlUiLifetime>();
    test->GuiFunc = [](ImGuiTestContext *ctx) {
        MainThreadGui(ctx);
        if (g_HtmlVisible) {
            if (!g_HtmlFixture)
                g_HtmlFixture = std::make_unique<HtmlUiFixture>();
            g_HtmlFixture->Draw();
        }
    };
    test->TestFunc = [](ImGuiTestContext *ctx) {
        OnMainThread(ctx, [] {
            g_HtmlVisible = true;
            g_HtmlFixture = std::make_unique<HtmlUiFixture>();
            g_HtmlFixture->detailed = true;
        });
        for (float zoom : {1.0f, 1.5f}) {
            OnMainThread(ctx, [=] { g_HtmlFixture->scale = zoom; });
            ctx->Yield(5);
            ctx->WindowFocus("//HTML Renderer");
            ctx->Yield(3);
            IM_CHECK(g_HtmlFixture->rendered);
            const auto failure = Read<std::string>(ctx, [] {
                std::vector<unsigned char> rgba;
                int width = 0, height = 0;
                if (!vdev::MainRenderer().ReadFrame(rgba, width, height))
                    return std::string("Frame readback failed");
                auto bgra = rgba;
                for (size_t at = 0; at < bgra.size(); at += 4)
                    std::swap(bgra[at], bgra[at + 2]);
                const auto folder = std::filesystem::u8path(SDL_GetBasePath()) / "test-help";
                std::filesystem::create_directories(folder);
                if (!vdev::WriteBgraPng((folder / "html-details.png").u8string(), width, height, bgra.data()))
                    return std::string("HTML pixel diagnostic could not be written");
                const float scale = ImGui::GetFontSize() / 14.0f * g_HtmlFixture->scale;
                const ImVec2 density = ImGui::GetIO().DisplayFramebufferScale;
                const auto pixel = [&](float x, float y, unsigned char r, unsigned char g, unsigned char b) {
                    const int px = int((g_HtmlFixture->origin.x + x * scale) * density.x);
                    const int py = int((g_HtmlFixture->origin.y + y * scale) * density.y);
                    if (px < 0 || py < 0 || px >= width || py >= height)
                        return false;
                    const size_t at = (size_t(py) * width + px) * 4;
                    return std::abs(int(rgba[at]) - r) < 15 && std::abs(int(rgba[at + 1]) - g) < 15 &&
                           std::abs(int(rgba[at + 2]) - b) < 15;
                };
                if (!pixel(40, 40, 0, 0, 255) || !pixel(2, 2, 255, 255, 255) || !pixel(18, 18, 255, 0, 0))
                    return std::string("Rounded border inner or outer curve is wrong");
                for (float left : {200.0f, 340.0f}) {
                    if (!pixel(left + 23, 19, 255, 0, 0) || !pixel(left + 40, 8, 0, 0, 255) ||
                        !pixel(left + 60, 60, 0, 0, 255) || !pixel(left + 2, 2, 255, 255, 255))
                        return std::string("Rounded dashes or dots lost their curves, gaps or clipping");
                }
                if (!pixel(150, 16, 255, 255, 255))
                    return std::string("Nested backgrounds escaped the overflow clip");
                if (!pixel(102, 52, 255, 0, 0) || !pixel(114, 52, 0, 255, 0) || !pixel(118, 52, 255, 0, 0))
                    return std::string("Repeated background changed tile phase or scale");
                const auto cell = [&](HtmlPoint point, unsigned char r, unsigned char g, unsigned char b) {
                    return pixel(point.x / scale + 4, point.y / scale + 4, r, g, b);
                };
                if (!cell(g_HtmlFixture->head, 255, 0, 0))
                    return std::string("Colspan table cell background is wrong");
                if (!cell(g_HtmlFixture->first, 0, 255, 0))
                    return std::string("Rowspan table cell background is wrong");
                if (!cell(g_HtmlFixture->second, 0, 0, 255) || !cell(g_HtmlFixture->third, 255, 255, 0))
                    return std::string("Table cells next to a rowspan background are wrong");
                return std::string();
            });
            if (!failure.empty())
                ctx->LogError("%s", failure.c_str());
            IM_CHECK(failure.empty());
        }
    };
    test = IM_REGISTER_TEST(engine, "html", "computed_cursor_presentation");
    test->GuiFunc = MainThreadGui;
    test->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK(Read<bool>(ctx, [] {
            HtmlUiResources resources;
            vdev::HtmlRenderer renderer(
                {ImGui::GetIO().FontDefault, ImGui::GetIO().FontDefault, ImGui::GetIO().FontDefault});
            HtmlDocument document(resources, renderer);
            const std::pair<const char *, ImGuiMouseCursor> cases[] = {{"pointer", ImGuiMouseCursor_Hand},
                                                                       {"text", ImGuiMouseCursor_TextInput},
                                                                       {"move", ImGuiMouseCursor_ResizeAll},
                                                                       {"ew-resize", ImGuiMouseCursor_ResizeEW},
                                                                       {"ns-resize", ImGuiMouseCursor_ResizeNS},
                                                                       {"nesw-resize", ImGuiMouseCursor_ResizeNESW},
                                                                       {"nwse-resize", ImGuiMouseCursor_ResizeNWSE},
                                                                       {"not-allowed", ImGuiMouseCursor_NotAllowed},
                                                                       {"wait", ImGuiMouseCursor_Wait},
                                                                       {"progress", ImGuiMouseCursor_Progress},
                                                                       {"none", ImGuiMouseCursor_None},
                                                                       {"default", ImGuiMouseCursor_Arrow},
                                                                       {"crosshair", ImGuiMouseCursor_Arrow}};
            for (const auto &item : cases) {
                std::string error;
                const std::string html =
                    "<!doctype html><a href=next style='cursor:" + std::string(item.first) + "'>Cursor</a>";
                if (!document.Load(html, "https://docs.example.test/index.html", error) ||
                    !document.Layout({400, 300}, 1, error) || document.Links().empty())
                    return false;
                const auto box = document.Links().front().bounds.front();
                renderer.SetCursor(document.HitTest({box.x + 2, box.y + 2}).cursor);
                if (ImGui::GetMouseCursor() != item.second)
                    return false;
            }
            return true;
        }));
    };
}
} // namespace vdevuitest
