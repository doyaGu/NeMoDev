#pragma once
#include <map>
#include <string>
#include <vector>

namespace vdev {

// Resolved presentation in CSS pixels. Content owns these values; DPI, zoom
// and available width are inputs to layout, not changes to the stored topic.
struct HelpAppearance {
    enum TextFlag { Bold = 1, Italic = 2, Monospace = 4, Underline = 8, PreserveWhitespace = 16 };
    unsigned color = 0xff000000, background = 0, flags = 0;
    unsigned align = 0, valign = 0, floating = 0;
    float size = 10.0f, width = 0.0f, height = 0.0f;
    float margin[4] = {}, padding[4] = {}, border[4] = {};
    float cellPadding = 1.0f, cellSpacing = 2.0f;
    unsigned borderColor = 0xff000000, rowSpan = 1;
    bool collapseBorders = false;
    unsigned hoverColor = 0xff000000, visitedColor = 0xff000000;
    unsigned hoverFlags = 0, visitedFlags = 0;
};

struct HelpImageArea {
    enum Shape { Rectangle, Polygon };
    Shape shape = Rectangle;
    std::string link, title;
    std::vector<float> points; // normalized to the source image's dimensions
    bool Contains(float x, float y) const;
};
struct HelpSpan {
    std::string text, link, image, anchor;
    HelpAppearance appearance;
    unsigned imageWidth = 0, imageHeight = 0;
    std::vector<HelpImageArea> imageAreas;
};
struct HelpBlock {
    enum Kind { Paragraph, Heading, Preformatted, ListItem, Rule, Table, Row, Cell, Group };
    Kind kind = Paragraph;
    unsigned value = 0; // heading level, list number, table columns or cell span
    HelpAppearance appearance;
    std::vector<HelpSpan> spans;
    std::vector<HelpBlock> children;
};
struct HelpTopic {
    std::string path, title, text;
    HelpAppearance appearance;
    std::vector<HelpBlock> blocks;
};
struct HelpContents {
    std::string title, topic;
    std::vector<HelpContents> children;
};

// Owns the optional, compiled CHM content. All paths and text here are UTF-8
// help data; they never pass through CK's object strings or path interfaces.
class HelpBook {
public:
    bool Load(const std::string &directory, std::string &error);
    const std::string &Home() const { return m_Home; }
    const std::vector<HelpContents> &Contents() const { return m_Contents; }
    const HelpTopic *Find(const std::string &path) const;
    std::vector<const HelpTopic *> Search(const std::string &text) const;
    std::string ImagePath(const std::string &path) const;
private:
    std::string m_Directory, m_Home;
    std::vector<HelpContents> m_Contents;
    std::vector<HelpTopic> m_Topics;
    std::map<std::string, size_t> m_Index;
    std::vector<std::string> m_SearchText;
};
}
