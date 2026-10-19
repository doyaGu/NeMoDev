#pragma once
#include "html/HtmlDocument.h"
#include "imgui.h"
#include <memory>

namespace vdev {
struct HtmlFonts {
    ImFont *normal = nullptr, *bold = nullptr, *monospace = nullptr;
};

// The ImGui/SDL graphics adapter. HTML parsing, CSS and layout stay in html/.
class HtmlRenderer : public HtmlRenderDevice {
public:
    explicit HtmlRenderer(HtmlFonts fonts);
    ~HtmlRenderer();
    void Begin(ImDrawList *draw);
    // Apply the supported native cursor shape; unknown CSS shapes use Arrow.
    void SetCursor(std::string_view cursor);
    std::shared_ptr<HtmlFont> CreateFont(const HtmlFontDescription &description) override;
    std::shared_ptr<HtmlImage> DecodeImage(const std::string &url, const std::vector<unsigned char> &bytes) override;
    std::shared_ptr<HtmlImage> CreateImage(unsigned width, unsigned height, const unsigned char *rgba) override;
    void SetClip(const std::vector<HtmlPoint> &polygon) override;
    void Text(const HtmlFont &font, std::string_view text, HtmlPoint position, HtmlColor color) override;
    void Image(const HtmlImage &image, HtmlRect destination) override;
    void Fill(const std::vector<HtmlPoint> &polygon, HtmlColor color) override;
#ifdef VIRTOOLSDEV_UI_TESTS
    struct Drawing {
        enum Kind { Text, Image, Fill } kind;
        ImVec2 position, size;
        std::string text, image;
        unsigned color = 0;
        HtmlFontDescription style;
        ImFont *font = nullptr;
    };
    const std::vector<Drawing> &Drawings() const;
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
} // namespace vdev
