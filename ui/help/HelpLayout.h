#pragma once
#include "ui/help/HelpBook.h"
#include "imgui.h"

namespace vdev {
// Borrowed from the ImGui atlas, which outlives the reader and its layouts.
struct HelpFonts { ImFont *normal = nullptr, *bold = nullptr, *mono = nullptr; };
struct HelpDrawing {
    enum Kind { Text, Image, Fill, Line };
    Kind kind = Text;
    ImVec2 position, size;
    std::string text, image, link;
    unsigned color = 0xff000000, background = 0, flags = 0;
    unsigned hoverColor = 0xff000000, visitedColor = 0xff000000;
    unsigned hoverFlags = 0, visitedFlags = 0;
    float fontSize = 0.0f, thickness = 1.0f;
    ImFont *font = nullptr;
    const HelpSpan *source = nullptr; // borrowed from the immutable topic
    size_t textOffset = 0;
};
// A location in immutable help content, with a CSS-pixel offset into its
// displayed line or image. The owning book must outlive saved positions.
struct HelpReadingPosition {
    const HelpSpan *source = nullptr;
    size_t textOffset = 0;
    ImVec2 distance, fallback;
};
struct HelpPageLayout {
    std::vector<HelpDrawing> drawings;
    std::map<std::string, ImVec2> anchors;
    ImVec2 size;
    float scale = 1.0f;
    bool Anchor(const std::string &name, ImVec2 &position) const;
    HelpReadingPosition CapturePosition(ImVec2 point) const;
    ImVec2 RestorePosition(const HelpReadingPosition &position) const;
};

// Computes one page from immutable content, font metrics, width and scale.
// No file access, GPU objects, ImGui widgets or navigation state enter layout.
class HelpLayout {
public:
    HelpLayout(HelpFonts fonts, float scale) : m_Fonts(fonts), m_Scale(scale) {}
    HelpPageLayout Build(const HelpTopic &topic, float width);
private:
    ImFont *Font(const HelpAppearance &style) const;
    float Length(float value, float available) const;
    ImVec2 Flow(const std::vector<HelpBlock> &blocks, ImVec2 origin, float width, HelpPageLayout &page);
    ImVec2 Paragraph(const HelpBlock &block, ImVec2 origin, float width, HelpPageLayout &page);
    ImVec2 Table(const HelpBlock &block, ImVec2 origin, float width, HelpPageLayout &page);
    float Preferred(const std::vector<HelpBlock> &blocks) const;
    void Borders(const HelpAppearance &style, ImVec2 origin, ImVec2 size, HelpPageLayout &page);
    HelpFonts m_Fonts;
    float m_Scale;
};
}
