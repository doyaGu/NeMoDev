#include "ui/help/HelpLayout.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace vdev {
bool HelpPageLayout::Anchor(const std::string &name, ImVec2 &position) const {
    const auto exact = anchors.find(name);
    if (exact != anchors.end()) { position = exact->second; return true; }
    // The original CHM has case mismatches. Accept a unique legacy spelling,
    // while retaining exact, case-sensitive matches and rejecting ambiguity.
    const auto lower = [](std::string value) {
        for (char &c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return value;
    };
    const std::string key = lower(name);
    bool found = false;
    for (const auto &anchor : anchors) if (lower(anchor.first) == key) {
        if (found) return false;
        position = anchor.second; found = true;
    }
    return found;
}
ImFont *HelpLayout::Font(const HelpAppearance &style) const {
    if ((style.flags & HelpAppearance::Monospace) && m_Fonts.mono) return m_Fonts.mono;
    if ((style.flags & HelpAppearance::Bold) && m_Fonts.bold) return m_Fonts.bold;
    return m_Fonts.normal;
}
float HelpLayout::Length(float value, float available) const {
    return value < 0.0f ? -value * available / 100.0f : value * m_Scale;
}
void HelpLayout::Borders(const HelpAppearance &style, ImVec2 origin, ImVec2 size, HelpPageLayout &page) {
    const ImVec2 starts[] = {origin, ImVec2(origin.x + size.x, origin.y),
        ImVec2(origin.x, origin.y + size.y), origin};
    const ImVec2 lengths[] = {ImVec2(size.x, 0), ImVec2(0, size.y), ImVec2(size.x, 0), ImVec2(0, size.y)};
    for (int side = 0; side < 4; ++side) if (style.border[side] > 0.0f) {
        HelpDrawing line; line.kind = HelpDrawing::Line; line.position = starts[side]; line.size = lengths[side];
        line.color = style.borderColor; line.thickness = style.border[side] * m_Scale;
        page.drawings.push_back(std::move(line));
    }
}
HelpPageLayout HelpLayout::Build(const HelpTopic &topic, float width) {
    HelpPageLayout page;
    const auto &style = topic.appearance;
    const ImVec2 inset(style.margin[3] * m_Scale, style.margin[0] * m_Scale);
    const ImVec2 content = Flow(topic.blocks, inset, std::max(1.0f, width - inset.x - style.margin[1] * m_Scale), page);
    page.size = ImVec2(std::max(width, inset.x + content.x + style.margin[1] * m_Scale),
        inset.y + content.y + style.margin[2] * m_Scale);
    return page;
}
ImVec2 HelpLayout::Flow(const std::vector<HelpBlock> &blocks, ImVec2 origin, float width, HelpPageLayout &page) {
    float y = 0.0f, right = 0.0f, previousMargin = 0.0f;
    for (const auto &block : blocks) {
        const auto &style = block.appearance;
        y += std::max(previousMargin, style.margin[0] * m_Scale);
        const float left = style.margin[3] * m_Scale, marginRight = style.margin[1] * m_Scale;
        float boxWidth = std::max(1.0f, width - left - marginRight);
        if (style.width != 0.0f) boxWidth = Length(style.width, boxWidth);
        const float padLeft = style.padding[3] * m_Scale, padRight = style.padding[1] * m_Scale;
        ImVec2 start(origin.x + left, origin.y + y);
        const ImVec2 inner(start.x + padLeft, start.y + style.padding[0] * m_Scale);
        if (block.kind == HelpBlock::Group || block.kind == HelpBlock::ListItem || block.kind == HelpBlock::Table || block.kind == HelpBlock::Rule)
            for (const auto &span : block.spans) if (!span.anchor.empty()) page.anchors.emplace(span.anchor, inner);
        const size_t background = page.drawings.size();
        if (style.background) {
            HelpDrawing fill; fill.kind = HelpDrawing::Fill; fill.position = start; fill.color = style.background;
            page.drawings.push_back(fill);
        }
        HelpPageLayout table;
        ImVec2 size;
        if (block.kind == HelpBlock::Table) size = Table(block, inner, boxWidth - padLeft - padRight, table);
        else if (block.kind == HelpBlock::Group) size = Flow(block.children, inner, boxWidth - padLeft - padRight, page);
        else if (block.kind == HelpBlock::ListItem) {
            const std::string label = block.value ? std::to_string(block.value) + "." : u8"\u2022";
            HelpDrawing marker; marker.text = label; marker.font = Font(style); marker.fontSize = style.size * m_Scale;
            marker.color = style.color; marker.position = ImVec2(inner.x - marker.font->CalcTextSizeA(marker.fontSize, FLT_MAX, 0, label.c_str()).x - 5 * m_Scale, inner.y);
            marker.size = marker.font->CalcTextSizeA(marker.fontSize, FLT_MAX, 0, label.c_str());
            page.drawings.push_back(std::move(marker));
            size = Flow(block.children, inner, boxWidth - padLeft - padRight, page);
        } else if (block.kind == HelpBlock::Rule) {
            HelpDrawing line; line.kind = HelpDrawing::Line; line.position = inner; line.size = ImVec2(boxWidth, 0);
            line.color = 0xff999999; line.thickness = m_Scale; page.drawings.push_back(line); size = ImVec2(boxWidth, m_Scale * 4);
        } else size = Paragraph(block, inner, std::max(1.0f, boxWidth - padLeft - padRight), page);
        size.x += padLeft + padRight;
        if (block.kind == HelpBlock::Table) {
            const float spare = std::max(0.0f, boxWidth - size.x);
            const float shift = style.align == 1 ? spare / 2 : style.align == 2 ? spare : 0;
            for (auto &item : table.drawings) { item.position.x += shift; page.drawings.push_back(std::move(item)); }
            for (auto &anchor : table.anchors) { anchor.second.x += shift; page.anchors.emplace(anchor); }
            start.x += shift;
            boxWidth = size.x;
            if (style.background) page.drawings[background].position.x += shift;
        }
        size.y = std::max(size.y + (style.padding[0] + style.padding[2]) * m_Scale, Length(style.height, 0));
        if (style.background) page.drawings[background].size = ImVec2(std::max(boxWidth, size.x), size.y);
        Borders(style, start, ImVec2(std::max(boxWidth, size.x), size.y), page);
        right = std::max(right, left + std::max(size.x, style.background ? boxWidth : 0.0f) + marginRight);
        y += size.y;
        previousMargin = style.margin[2] * m_Scale;
    }
    return ImVec2(right, y + previousMargin);
}
ImVec2 HelpLayout::Paragraph(const HelpBlock &block, ImVec2 origin, float width, HelpPageLayout &page) {
    struct Float { float bottom, width; unsigned side; };
    std::vector<Float> floats;
    std::map<size_t, HelpAppearance> imageStyles;
    std::map<size_t, std::vector<std::string>> lineAnchors;
    std::vector<std::string> pendingAnchors;
    std::vector<size_t> line;
    float x = 0, y = 0, lineHeight = block.appearance.size * m_Scale * 1.2f;
    float extent = 0, bottom = 0;
    bool pendingSpace = false;
    auto bounds = [&] {
        ImVec2 range(0, width);
        for (const auto &item : floats) if (item.bottom > y + 0.1f) {
            if (item.side == 1) range.x += item.width; else range.y -= item.width;
        }
        return range;
    };
    auto flush = [&] {
        const ImVec2 range = bounds();
        const float spare = std::max(0.0f, range.y - range.x - x);
        const float offset = block.appearance.align == 1 ? spare * 0.5f : block.appearance.align == 2 ? spare : 0;
        for (size_t index : line) {
            auto &item = page.drawings[index];
            item.position.x += origin.x + range.x + offset;
            item.position.y += origin.y + y;
            if (item.kind == HelpDrawing::Text) item.position.y += std::max(0.0f, lineHeight / 1.2f - item.fontSize) * 0.8f;
            const auto image = imageStyles.find(index);
            if (image != imageStyles.end()) {
                if (image->second.valign == 1) item.position.y += (lineHeight - item.size.y) / 2;
                else if (image->second.valign == 2) item.position.y += lineHeight - item.size.y;
            }
            extent = std::max(extent, item.position.x - origin.x + item.size.x);
            const auto names = lineAnchors.find(index);
            if (names != lineAnchors.end()) for (const auto &name : names->second) page.anchors.emplace(name, item.position);
            if (image != imageStyles.end()) { const ImVec2 at = item.position, size = item.size; Borders(image->second, at, size, page); }
        }
        y += lineHeight; x = 0; line.clear(); pendingSpace = false;
        lineHeight = block.appearance.size * m_Scale * 1.2f;
    };
    auto add = [&](HelpDrawing item) {
        item.position = ImVec2(x, 0); x += item.size.x;
        lineHeight = std::max(lineHeight, item.kind == HelpDrawing::Text ? item.fontSize * 1.2f : item.size.y);
        if (!pendingAnchors.empty()) { lineAnchors.emplace(page.drawings.size(), std::move(pendingAnchors)); pendingAnchors.clear(); }
        line.push_back(page.drawings.size()); page.drawings.push_back(std::move(item));
    };
    const auto placeAnchors = [&](ImVec2 position) {
        for (const auto &name : pendingAnchors) page.anchors.emplace(name, position);
        pendingAnchors.clear();
    };
    for (const auto &span : block.spans) {
        const auto &style = span.appearance;
        if (!span.anchor.empty()) pendingAnchors.push_back(span.anchor);
        if (!span.image.empty()) {
            HelpDrawing image; image.kind = HelpDrawing::Image; image.image = span.image; image.link = span.link;
            float w = style.width ? Length(style.width, width) : span.imageWidth * m_Scale;
            float h = style.height ? Length(style.height, 0) : span.imageHeight * m_Scale;
            if (style.width && !style.height && span.imageWidth) h = w * span.imageHeight / span.imageWidth;
            if (style.height && !style.width && span.imageHeight) w = h * span.imageWidth / span.imageHeight;
            image.size = ImVec2(std::max(1.0f, w), std::max(1.0f, h));
            if (style.floating) {
                const ImVec2 range = bounds();
                image.position = ImVec2(origin.x + (style.floating == 1 ? range.x : range.y - image.size.x), origin.y + y);
                placeAnchors(image.position);
                floats.push_back({y + image.size.y, image.size.x + 3 * m_Scale, style.floating});
                bottom = std::max(bottom, y + image.size.y);
                extent = std::max(extent, image.position.x - origin.x + image.size.x);
                const ImVec2 at = image.position, size = image.size;
                page.drawings.push_back(std::move(image)); Borders(style, at, size, page);
            } else {
                const ImVec2 range = bounds();
                if (x && x + image.size.x > range.y - range.x) flush();
                imageStyles[page.drawings.size()] = style;
                add(std::move(image));
            }
            continue;
        }
        ImFont *font = Font(style);
        const float size = style.size * m_Scale;
        const bool pre = (style.flags & HelpAppearance::PreserveWhitespace) || block.kind == HelpBlock::Preformatted;
        const char *at = span.text.data(), *end = at + span.text.size();
        while (at < end) {
            if (*at == '\n') { placeAnchors(ImVec2(origin.x + bounds().x + x, origin.y + y)); flush(); ++at; continue; }
            if (!pre && (*at == ' ' || *at == '\t' || *at == '\r')) { pendingSpace = x > 0; ++at; continue; }
            if (pre && *at == '\t') {
                const float tab = std::max(1.0f, font->CalcTextSizeA(size, FLT_MAX, 0, " ").x * 8);
                x += tab - std::fmod(x, tab); ++at; continue;
            }
            const char *next = at + 1;
            while (next < end && *next != ' ' && *next != '\n' && *next != '\t' && *next != '\r') ++next;
            float w = font->CalcTextSizeA(size, FLT_MAX, 0, at, next).x;
            const float space = pendingSpace ? font->CalcTextSizeA(size, FLT_MAX, 0, " ").x : 0;
            ImVec2 range = bounds();
            if (!pre && x && x + space + w > range.y - range.x) { flush(); range = bounds(); }
            else x += space;
            pendingSpace = false;
            if (!pre && w > std::max(1.0f, range.y - range.x - x)) {
                next = at;
                do {
                    const char *character = next + 1;
                    while (character < end && (static_cast<unsigned char>(*character) & 0xc0) == 0x80) ++character;
                    if (next > at && font->CalcTextSizeA(size, FLT_MAX, 0, at, character).x > std::max(1.0f, range.y - range.x - x)) break;
                    next = character;
                } while (next < end && *next != ' ' && *next != '\n' && *next != '\t' && *next != '\r');
                w = font->CalcTextSizeA(size, FLT_MAX, 0, at, next).x;
            }
            HelpDrawing text; text.text.assign(at, next); text.link = span.link; text.font = font; text.fontSize = size;
            text.size = ImVec2(w, size * 1.2f); text.color = style.color; text.background = style.background; text.flags = style.flags;
            text.hoverColor = style.hoverColor; text.visitedColor = style.visitedColor;
            text.hoverFlags = style.hoverFlags; text.visitedFlags = style.visitedFlags;
            add(std::move(text)); at = next;
        }
    }
    placeAnchors(ImVec2(origin.x + bounds().x + x, origin.y + y));
    if (!line.empty() || x > 0) flush();
    return ImVec2(extent, std::max(y, bottom));
}
float HelpLayout::Preferred(const std::vector<HelpBlock> &blocks) const {
    float width = 0;
    for (const auto &block : blocks) {
        float row = 0, widest = 0;
        for (const auto &span : block.spans) {
            if (!span.image.empty()) row += span.appearance.width > 0 ? span.appearance.width * m_Scale : span.imageWidth * m_Scale;
            else {
                const char *at = span.text.c_str(), *end = at + span.text.size();
                while (at < end) {
                    const char *next = std::find(at, end, '\n');
                    row += Font(span.appearance)->CalcTextSizeA(span.appearance.size * m_Scale, FLT_MAX, 0, at, next).x;
                    widest = std::max(widest, row);
                    at = next < end ? next + 1 : end;
                    if (next < end) row = 0;
                }
            }
        }
        width = std::max(width, std::max({row, widest, Preferred(block.children)}) + (block.appearance.padding[1] + block.appearance.padding[3]) * m_Scale);
    }
    return width;
}
ImVec2 HelpLayout::Table(const HelpBlock &block, ImVec2 origin, float width, HelpPageLayout &page) {
    struct Cell { const HelpBlock *block; size_t row, column, rows, columns; HelpPageLayout content; ImVec2 size; };
    const size_t columns = std::max(1u, block.value), rows = block.children.size();
    std::vector<Cell> cells;
    std::vector<size_t> occupied(columns, 0);
    std::vector<float> widths(columns, 0), preferred(columns, 0), heights(rows, 0);
    const float spacing = block.appearance.collapseBorders ? 0 : block.appearance.cellSpacing * m_Scale;
    for (size_t r = 0; r < rows; ++r) {
        size_t column = 0;
        for (const auto &cell : block.children[r].children) {
            while (column < columns && occupied[column] > r) ++column;
            if (column == columns) break;
            const size_t span = std::min<size_t>(std::max(1u, cell.value), columns - column);
            const size_t rowSpan = std::min<size_t>(cell.appearance.rowSpan, rows - r);
            cells.push_back({&cell, r, column, rowSpan, span, {}, {}});
            for (size_t c = column; c < column + span; ++c) {
                occupied[c] = r + rowSpan;
                preferred[c] = std::max(preferred[c], (Preferred(cell.children) +
                    (cell.appearance.padding[1] + cell.appearance.padding[3]) * m_Scale) / span);
            }
            column += span;
        }
    }
    float tableWidth = width;
    if (!block.appearance.width) {
        float total = spacing * (columns + 1);
        for (float value : preferred) total += value;
        tableWidth = std::min(width, std::max(1.0f, total));
    }
    const float usable = std::max(1.0f, tableWidth - spacing * (columns + 1));
    for (const auto &cell : cells) if (cell.block->appearance.width) {
        const float wanted = Length(cell.block->appearance.width, usable) / cell.columns;
        for (size_t c = cell.column; c < cell.column + cell.columns; ++c) widths[c] = std::max(widths[c], wanted);
    }
    float specified = 0, weight = 0;
    for (size_t c = 0; c < columns; ++c) { specified += widths[c]; if (!widths[c]) weight += std::max(1.0f, preferred[c]); }
    for (size_t c = 0; c < columns; ++c) if (!widths[c]) widths[c] = std::max(1.0f, usable - specified) * std::max(1.0f, preferred[c]) / std::max(1.0f, weight);
    const auto measure = [&](Cell &cell) {
        float cellWidth = spacing * (cell.columns - 1);
        for (size_t c = cell.column; c < cell.column + cell.columns; ++c) cellWidth += widths[c];
        const auto &style = cell.block->appearance;
        const float left = style.padding[3] * m_Scale, top = style.padding[0] * m_Scale;
        const ImVec2 content = Flow(cell.block->children, ImVec2(left, top), std::max(1.0f, cellWidth - left - style.padding[1] * m_Scale), cell.content);
        cell.size = ImVec2(cellWidth, std::max(content.y + top + style.padding[2] * m_Scale, Length(style.height, 0)));
        return content.x + left + style.padding[1] * m_Scale;
    };
    // Declared percentages do not override an image's or a preformatted line's
    // minimum width. Measure cells first, widen their columns, then arrange
    // again at the final widths so adjacent cells cannot cover each other.
    for (auto &cell : cells) {
        const float required = measure(cell);
        const float extra = std::max(0.0f, required - cell.size.x) / cell.columns;
        for (size_t c = cell.column; c < cell.column + cell.columns; ++c) widths[c] += extra;
        cell.content = {};
    }
    float actualWidth = spacing * (columns + 1);
    for (float value : widths) actualWidth += value;
    for (auto &cell : cells) {
        measure(cell);
        if (cell.rows == 1) heights[cell.row] = std::max(heights[cell.row], cell.size.y);
    }
    for (const auto &cell : cells) if (cell.rows > 1) {
        float available = spacing * (cell.rows - 1);
        for (size_t r = cell.row; r < cell.row + cell.rows; ++r) available += heights[r];
        if (cell.size.y > available) {
            const float extra = (cell.size.y - available) / cell.rows;
            for (size_t r = cell.row; r < cell.row + cell.rows; ++r) heights[r] += extra;
        }
    }
    float rowY = origin.y + spacing;
    for (size_t r = 0; r < rows; ++r) {
        for (const auto &span : block.children[r].spans) if (!span.anchor.empty()) page.anchors.emplace(span.anchor, ImVec2(origin.x, rowY));
        if (block.children[r].appearance.background) {
            HelpDrawing fill; fill.kind = HelpDrawing::Fill; fill.position = ImVec2(origin.x + spacing, rowY);
            fill.size = ImVec2(actualWidth - 2 * spacing, heights[r]); fill.color = block.children[r].appearance.background;
            page.drawings.push_back(fill);
        }
        rowY += heights[r] + spacing;
    }
    for (auto &cell : cells) {
        ImVec2 at(origin.x + spacing, origin.y + spacing);
        for (size_t c = 0; c < cell.column; ++c) at.x += widths[c] + spacing;
        for (size_t r = 0; r < cell.row; ++r) at.y += heights[r] + spacing;
        float cellHeight = spacing * (cell.rows - 1);
        for (size_t r = cell.row; r < cell.row + cell.rows; ++r) cellHeight += heights[r];
        const auto &style = cell.block->appearance;
        if (style.background) {
            HelpDrawing fill; fill.kind = HelpDrawing::Fill; fill.position = at; fill.size = ImVec2(cell.size.x, cellHeight); fill.color = style.background;
            page.drawings.push_back(fill);
        }
        const float vertical = style.valign == 1 ? (cellHeight - cell.size.y) / 2 : style.valign == 2 ? cellHeight - cell.size.y : 0;
        for (const auto &span : cell.block->spans) if (!span.anchor.empty()) page.anchors.emplace(span.anchor, at);
        for (auto &item : cell.content.drawings) { item.position.x += at.x; item.position.y += at.y + vertical; page.drawings.push_back(std::move(item)); }
        for (const auto &anchor : cell.content.anchors) page.anchors.emplace(anchor.first, ImVec2(anchor.second.x + at.x, anchor.second.y + at.y + vertical));
        HelpAppearance border = style;
        if (block.appearance.border[0] > 0) for (float &edge : border.border) edge = std::max(edge, block.appearance.border[0]);
        Borders(border, at, ImVec2(cell.size.x, cellHeight), page);
    }
    float totalHeight = spacing * (rows + 1);
    for (float value : heights) totalHeight += value;
    return ImVec2(actualWidth, totalHeight);
}
}
