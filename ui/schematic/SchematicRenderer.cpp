#include "ui/schematic/SchematicRenderer.h"
#include "ui/schematic/Snapshots.h"

#include "ui/controls/DevIcons.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace vdev {

namespace {

constexpr float MIN_TEXT_PIXELS = 5.5f;

// Offsets measured on Dev's 2D Text.cmo screenshot (zoom 100%), relative to
// the endpoint centers of LinkEndpointGeometry: a parameter input endpoint
// lies 10 px above its behavior's top edge, an output 10 px below the bottom
// edge, but Dev draws the pin glyphs against the edges.
constexpr float PIN_IN_GLYPH_TOP = 4.5f;     // input triangle: endpoint + 4.5 .. + 8
constexpr float PIN_OUT_GLYPH_TOP = -9.0f;   // output triangle: endpoint - 9 .. - 5
constexpr float PIN_GLYPH_HEIGHT = 3.5f;
constexpr float PIN_GLYPH_HALF_WIDTH = 3.0f;
constexpr float TARGET_PIN_OFFSET = 4.5f;    // Target square centers: endpoint + 4.5
constexpr float TARGET_MARK_OFFSET = 14.0f;  // and the square inside the box, endpoint + 14
constexpr float TARGET_HALF_SIZE = 3.0f;

class Painter {
public:
    Painter(ImDrawList *drawList, const ViewTransform &view, float offsetY, const DrawOptions &options)
        : dl(drawList), v(view), dy(offsetY), o(options), s(*options.style) {}

    ImVec2 P(float x, float y) const { return v.ToScreen(x, y + dy); }
    ImVec2 P(const Point &point) const { return P(point.h, point.v); }
    float Scale(float value) const { return value * v.zoom; }

    bool Visible(const Rect &rect, float margin = 4.0f) const {
        const ImVec2 a = P(rect.hPos, rect.vPos), b = P(rect.Right(), rect.Bottom());
        return b.x + margin >= o.clipMin.x && a.x - margin <= o.clipMax.x && b.y + margin >= o.clipMin.y &&
               a.y - margin <= o.clipMax.y;
    }
    bool Selected(const ElementRef &ref) const { return o.selection && o.selection->count(ref); }
    // A shape's fill, none when printing without Fill Shapes (Dev.exe
    // CUIKLink_Draw, sub_4F9940, sub_4FE990).
    ImU32 Fill(ImU32 color) const { return o.print && !o.print->fillShapes ? 0 : color; }
    // Where the canvas shows: white when printing without Fill Script Body.
    ImU32 Canvas() const { return o.print && !o.print->fillBody ? IM_COL32(255, 255, 255, 255) : s.canvas; }
    bool Invalid(const ElementRef &ref) const { return o.invalid && o.invalid->count(ref); }
    bool Hovered(const ElementRef &ref) const { return o.hovered.IsValid() && o.hovered == ref; }

    void Box(const Rect &rect, ImU32 fill, ImU32 border, float thickness = 1.0f) const {
        const ImVec2 a = P(rect.hPos, rect.vPos), b = P(rect.Right(), rect.Bottom());
        if (fill) dl->AddRectFilled(a, b, fill);
        if (border) dl->AddRect(a, b, border, 0.0f, 0, thickness);
    }
    // Windows-style raised edge: light top/left, dark bottom/right.
    void Raised(const Rect &rect, ImU32 fill, ImU32 border, float thickness = 1.0f) const {
        const ImVec2 a = P(rect.hPos, rect.vPos), b = P(rect.Right(), rect.Bottom());
        dl->AddRectFilled(a, b, fill);
        const float i = 1.0f;
        dl->AddLine(ImVec2(a.x + i, a.y + i), ImVec2(b.x - i, a.y + i), s.bevelLight);
        dl->AddLine(ImVec2(a.x + i, a.y + i), ImVec2(a.x + i, b.y - i), s.bevelLight);
        dl->AddLine(ImVec2(a.x + i, b.y - i - 1), ImVec2(b.x - i, b.y - i - 1), s.bevelDark);
        dl->AddLine(ImVec2(b.x - i - 1, a.y + i), ImVec2(b.x - i - 1, b.y - i), s.bevelDark);
        dl->AddRect(a, b, border, 0.0f, 0, thickness);
    }
    // Screen-space dotted rectangle (2 px dots, 2 px gaps).
    void DottedScreenRect(ImVec2 a, ImVec2 b, ImU32 color) const {
        for (float x = a.x; x < b.x; x += 4.0f) {
            dl->AddLine(ImVec2(x, a.y), ImVec2(std::min(x + 2.0f, b.x), a.y), color);
            dl->AddLine(ImVec2(x, b.y - 1), ImVec2(std::min(x + 2.0f, b.x), b.y - 1), color);
        }
        for (float y = a.y; y < b.y; y += 4.0f) {
            dl->AddLine(ImVec2(a.x, y), ImVec2(a.x, std::min(y + 2.0f, b.y)), color);
            dl->AddLine(ImVec2(b.x - 1, y), ImVec2(b.x - 1, std::min(y + 2.0f, b.y)), color);
        }
    }
    void DottedRect(const Rect &rect, ImU32 color) const {
        DottedScreenRect(P(rect.hPos, rect.vPos), P(rect.Right(), rect.Bottom()), color);
    }
    void Outline(const Rect &rect, const ElementRef &ref, float grow = 2.0f) const {
        if (Selected(ref)) {
            const ImVec2 a = P(rect.hPos, rect.vPos), b = P(rect.Right(), rect.Bottom());
            dl->AddRect(ImVec2(a.x - grow, a.y - grow), ImVec2(b.x + grow, b.y + grow), s.selection, 0.0f, 0, 2.0f);
        } else if (Hovered(ref)) {
            const ImVec2 a = P(rect.hPos, rect.vPos), b = P(rect.Right(), rect.Bottom());
            dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), s.hover, 0.0f, 0, 1.5f);
        }
    }

    float FontPixels(float scale = 1.0f) const { return std::floor(o.fontSize * scale * v.zoom); }
    // Text width in Script-local pixels.
    float TextWidth(const std::string &text, float scale = 1.0f) const {
        if (!o.font || text.empty()) return 0.0f;
        return o.font->CalcTextSizeA(o.fontSize * scale, FLT_MAX, 0.0f, text.c_str()).x;
    }

    // Text at a Script-local point, clipped to a Script-local rectangle.
    void Text(const Point &at, ImU32 color, const std::string &text, const Rect &clip, float scale = 1.0f,
              float wrap = 0.0f, bool bold = false) const {
        const float size = FontPixels(scale);
        if (text.empty() || size < MIN_TEXT_PIXELS || !o.font) return;
        const ImVec2 a = P(clip.hPos, clip.vPos), b = P(clip.Right(), clip.Bottom());
        const ImVec4 clipRect(std::max(a.x, o.clipMin.x), std::max(a.y, o.clipMin.y), std::min(b.x, o.clipMax.x),
                              std::min(b.y, o.clipMax.y));
        if (clipRect.z <= clipRect.x || clipRect.w <= clipRect.y) return;
        const ImVec2 at0 = P(at);
        dl->AddText(o.font, size, at0, color, text.c_str(), text.c_str() + text.size(), Scale(wrap), &clipRect);
        if (bold)
            dl->AddText(o.font, size, ImVec2(at0.x + 1.0f, at0.y), color, text.c_str(), text.c_str() + text.size(),
                        Scale(wrap), &clipRect);
    }
    // Text centered in a Script-local rectangle (clipped to it).
    void CenteredText(const Rect &box, ImU32 color, const std::string &text, float scale = 1.0f) const {
        const float width = TextWidth(text, scale);
        const float height = o.fontSize * scale;
        const Point at(box.hPos + std::max(2.0f, (box.hSize - width) * 0.5f), box.vPos + (box.vSize - height) * 0.5f);
        Text(at, color, text, box, scale);
    }

    void Polyline(const std::vector<Point> &path, ImU32 color, float thickness, bool dotted) const {
        for (size_t i = 1; i < path.size(); ++i) {
            const ImVec2 a = P(path[i - 1]), b = P(path[i]);
            if (!dotted) {
                dl->AddLine(a, b, color, thickness);
                continue;
            }
            // Dev's parameter links: short dashes.
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length < 0.5f) continue;
            const float dash = 2.0f, gap = 2.0f;
            for (float t = 0.0f; t < length; t += dash + gap) {
                const float e = std::min(length, t + dash);
                dl->AddLine(ImVec2(a.x + dx * t / length, a.y + dy * t / length),
                            ImVec2(a.x + dx * e / length, a.y + dy * e / length), color, thickness);
            }
        }
    }

    ImDrawList *dl;
    const ViewTransform &v;
    float dy;
    const DrawOptions &o;
    const SchematicStyle &s;
};

// Owner icon of the Script header (Dev's class bitmaps).
void DrawOwnerIcon(const Painter &p, const Point &at, CK_CLASSID owner, ImU32 color) {
    DrawDevIcon(p.dl, p.P(at), p.v.zoom, owner ? IconForClass(owner) : DevIcon::Object, color, p.s.bevelLight);
}

// The open/close triangle of a Script header: dark gray with a black edge.
void DrawHeaderTriangle(const Painter &p, const Point &center, bool open) {
    const ImVec2 c = p.P(center);
    const float r = std::max(2.0f, p.Scale(4.0f));
    ImVec2 a, b, d;
    if (open) {
        a = ImVec2(c.x - r, c.y - r * 0.55f);
        b = ImVec2(c.x + r, c.y - r * 0.55f);
        d = ImVec2(c.x, c.y + r * 0.65f);
    } else {
        a = ImVec2(c.x - r * 0.55f, c.y - r);
        b = ImVec2(c.x + r * 0.65f, c.y);
        d = ImVec2(c.x - r * 0.55f, c.y + r);
    }
    p.dl->AddTriangleFilled(a, b, d, p.s.headerTriangle);
    p.dl->AddTriangle(a, b, d, p.s.pinBorder);
}

// A Script band: the colored left column (name, owner, snapshot place) and
// a separator line across the whole canvas width. A closed Script is one row
// with the triangle and the name. Dev (screenshots, 2D Text.cmo): the
// current Script has a dotted frame; a selected Script header is dark gray.
void DrawScriptFrame(const Painter &p, const SchematicModel &model) {
    const ScriptItem &script = model.script;
    const Rect &band = script.bounds;
    const ImVec2 b = p.P(band.Right(), band.Bottom());
    // Printing without Fill Script Body, the lines are black (CUIKLink_RenderAllBehaviors).
    const ImU32 line = p.o.print && !p.o.print->fillBody ? IM_COL32(0, 0, 0, 255) : p.s.bandLine;
    p.dl->AddLine(ImVec2(p.o.clipMin.x, b.y - 1), ImVec2(p.o.clipMax.x, b.y - 1), line);
    if (!p.o.showHeaders) return;
    const Rect &header = script.header;
    const bool selected = p.Selected(script.ref);
    // sub_4F9E80: the header is filled unless printing without Fill Script Header.
    if (!p.o.print || p.o.print->fillHeader)
        p.Box(header, selected ? p.s.currentHeaderFill : ColorRefToImU32(script.color), 0);
    const ImVec2 h0 = p.P(header.Right(), header.vPos), h1 = p.P(header.Right(), header.Bottom());
    p.dl->AddLine(ImVec2(h0.x - 1, h0.y), ImVec2(h1.x - 1, h1.y - 1), p.s.headerEdge);
    const ImU32 text = selected ? p.s.currentHeaderText : p.s.text;
    DrawHeaderTriangle(p, Point(header.hPos + 9.0f, header.vPos + 10.0f), script.open);
    const Rect clip(header.hPos + 2, header.vPos, header.hSize - 4, header.vSize);
    p.Text(Point(header.hPos + 19, header.vPos + 3), p.o.trace && p.o.scriptActive ? IM_COL32(230, 0, 0, 255) : text, script.name,
           clip, 0.95f);
    if (script.open) {
        DrawOwnerIcon(p, Point(header.hPos + 20, header.vPos + 23), script.ownerClass, text);
        p.Text(Point(header.hPos + 35, header.vPos + 22), text, script.ownerName, clip, 0.9f);
        // Script snapshot (Dev "Load Snapshot for Script"), else its place (dotted frame).
        const Rect thumb(header.hPos + 20, header.vPos + 37, 49, 38);
        // Printing without Print Bitmaps, the bitmaps are left out (sub_4F9E80).
        if (thumb.Bottom() < header.Bottom() && (!p.o.print || p.o.print->bitmaps)) {
            ImVec2 size;
            const ImTextureID texture = script.snapshot ? SnapshotTexture(script.snapshot, &size) : ImTextureID();
            if (texture && size.x > 0 && size.y > 0) {
                const float scale = std::min({1.0f, thumb.hSize / size.x, thumb.vSize / size.y});
                const float w = size.x * scale, h = size.y * scale;
                const float x = thumb.hPos + (thumb.hSize - w) * 0.5f, y = thumb.vPos + (thumb.vSize - h) * 0.5f;
                p.dl->AddImage(texture, p.P(x, y), p.P(x + w, y + h));
            } else {
                p.Box(thumb, p.s.thumbnailFill, 0);
                p.DottedRect(thumb, p.s.thumbnailBorder);
            }
        }
        if (script.hidden) p.Text(Point(header.hPos + 20, header.Bottom() - 16), text, "(hidden)", clip, 0.8f);
        // Dev documentation (Script Header): "Priority = 0 - script priority,
        // if Show Priority is activated".
        if (p.o.showPriorities && header.vSize >= 96.0f)
            p.Text(Point(header.hPos + 20, header.vPos + 78), text, "Priority = " + std::to_string(script.priority), clip, 0.8f);
        // Dev documentation (Script Header): "IC - Initial Conditions, if set".
        if (script.initialConditions && header.vSize >= 60.0f)
            p.Text(Point(header.hPos + 75, header.vPos + 40), text, "IC", clip, 0.9f, 0.0f, true);
    }
    if (script.open && std::fabs(p.o.scriptZoom - 1.0f) > 0.001f) {
        // The Script's own zoom, so a zoomed body is never mistaken for moved contents.
        const std::string zoom = std::to_string(static_cast<int>(std::lround(p.o.scriptZoom * 100.0f))) + "%";
        const Rect badge(header.hPos + 4, header.Bottom() - 17, p.TextWidth(zoom, 0.85f) + 8, 14);
        if (badge.vPos > header.vPos + 40.0f) {
            p.Box(badge, p.s.text, 0);
            p.Text(Point(badge.hPos + 4, badge.vPos + 1), p.s.currentHeaderText, zoom, badge, 0.85f);
        }
    }
    if (p.o.current) {
        const ImVec2 a = p.P(header.hPos, header.vPos), e = p.P(header.Right(), header.Bottom());
        p.DottedScreenRect(ImVec2(a.x + 2, a.y + 2), ImVec2(e.x - 2, e.y - 2), p.s.pinBorder);
    }
}

// Dev's expanded behavior graph (Tunnel.cmo, "Tunnel Scrolling"): a frame
// filled with the canvas color (it hides what lies under it in the parent
// graph), a white 1 px edge, the graph's name as a large white watermark
// across the middle (under the contents) and a resize grip in the
// bottom-right corner. No title bar.
void DrawOpenGraph(const Painter &p, const BehaviorItem &behavior) {
    const Rect &box = behavior.box;
    const ImVec2 a = p.P(box.hPos, box.vPos), b = p.P(box.Right(), box.Bottom());
    p.dl->AddRectFilled(a, b, p.Canvas());
    // Watermark: letters about half the frame height (Tunnel.cmo), at most
    // 90% of its width, in the bold face drawn once (repeated offset draws
    // leave seams where large glyphs overlap).
    ImFont *font = p.o.boldFont ? p.o.boldFont : p.o.font;
    if (font && !behavior.name.empty()) {
        const float unit = font->CalcTextSizeA(100.0f, FLT_MAX, 0.0f, behavior.name.c_str()).x / 100.0f;
        float size = (b.y - a.y) * 0.75f;
        if (unit > 0.0f) size = std::min(size, (b.x - a.x) * 0.9f / unit);
        // Glyphs are baked per size: a few sizes only (zooming and resizing
        // would otherwise bake a new set every frame).
        size = std::min(240.0f, size >= 16.0f ? std::floor(size / 8.0f) * 8.0f : std::floor(size));
        if (size >= MIN_TEXT_PIXELS) {
            const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, behavior.name.c_str());
            const ImVec2 at(a.x + (b.x - a.x - extent.x) * 0.5f, a.y + (b.y - a.y - extent.y) * 0.5f);
            const ImVec4 clip(std::max(a.x, p.o.clipMin.x), std::max(a.y, p.o.clipMin.y), std::min(b.x, p.o.clipMax.x),
                              std::min(b.y, p.o.clipMax.y));
            p.dl->AddText(font, size, at, p.s.openGraphName, behavior.name.c_str(), nullptr, 0.0f, &clip);
        }
    }
    const ImU32 edge = behavior.missingRuntime ? p.s.missingBorder : p.s.openGraphEdge;
    p.dl->AddRect(a, b, edge);
    // Resize grip: three diagonal strokes in the corner.
    const float g = std::max(4.0f, p.Scale(14.0f));
    for (int k = 1; k <= 3; ++k) {
        const float d = g * k / 3.0f;
        p.dl->AddLine(ImVec2(b.x - d, b.y - 2), ImVec2(b.x - 2, b.y - d), p.s.openGraphEdge);
    }
    p.Outline(box, behavior.ref);
}

// Dev's Building Blocks are flat light-gray boxes with a gray 1 px edge and
// the name centered.
void DrawBehavior(const Painter &p, const BehaviorItem &behavior) {
    const Rect &box = behavior.box;
    const ImU32 fill = p.s.buildingBlockFill;
    // Dev preference colors for a behavior without CKObject or prototype,
    // an obsolete prototype, or a newer prototype version.
    const ImU32 border = behavior.missingRuntime     ? p.s.missingBorder
                         : behavior.prototypeMissing ? p.s.noPrototypeBorder
                         : behavior.obsolete         ? p.s.obsoleteBorder
                         : behavior.outdated         ? p.s.toReplaceBorder
                                                     : p.s.behaviorBorder;
    p.Box(box, p.Fill(fill), border, border != p.s.behaviorBorder ? 2.5f : 1.0f);
    if (behavior.expandable) {
        // Folded behavior graph (Dev, Tunnel.cmo): the name in light gray,
        // bold, with a dark edge around the glyphs.
        const float width = p.TextWidth(behavior.name, 1.15f) + 1.0f;
        const float height = p.o.fontSize * 1.15f;
        const Point at(box.hPos + std::max(2.0f, (box.hSize - width) * 0.5f), box.vPos + (box.vSize - height) * 0.5f);
        const float d = 1.0f / std::max(0.25f, p.v.zoom);
        for (const Point &o : {Point(-d, 0), Point(d, 0), Point(0, -d), Point(0, d)})
            p.Text(Point(at.h + o.h, at.v + o.v), p.s.graphNameEdge, behavior.name, box, 1.15f, 0.0f, true);
        p.Text(at, p.s.graphName, behavior.name, box, 1.15f, 0.0f, true);
    } else {
        p.CenteredText(box, p.s.behaviorText, behavior.name, 1.15f);
    }
    // Dev marks at the bottom-left corner: "v" variable I/O, "s" settings;
    // right of the Target column when the behavior uses its Target.
    std::string marks;
    if (behavior.customDialog) marks += "C";
    if (behavior.variable) marks += marks.empty() ? "v" : " v";
    if (behavior.settings) marks += marks.empty() ? "s" : " s";
    float markX = box.hPos + (behavior.usingTarget ? 14.0f : 5.0f);
    if (!marks.empty()) {
        p.Text(Point(markX, box.Bottom() - 12), p.s.text, marks, box, 0.8f, 0.0f, true);
        markX += p.TextWidth(marks, 0.8f) + 3.0f;
    }
    // The Sender and Receiver icons (documentation.chm sender.png, receiver.png: 11 x 11).
    static const char *const SENDER[11] = {".....#.....", ".....#.....", "...#.#.....", "...#.##....",
                                           "##.#..##...", "##.##..####", "##..##.....", "##...####..",
                                           "##.........", "#######....", "#######...."};
    static const char *const RECEIVER[11] = {"...........", "....####...", ".......##..", "..####..##.",
                                             ".....##..#.", ".###..##.#.", "...##..#.#.", "....##.#.#.",
                                             ".##..#.#...", "###..#.....", ".#........."};
    auto icon = [&](const char *const (&rows)[11]) {
        if (p.Scale(11.0f) < 4.0f) return;
        for (int y = 0; y < 11; ++y)
            for (int x = 0; x < 11; ++x)
                if (rows[y][x] == '#') p.Box(Rect(markX + x, box.Bottom() - 13 + y, 1, 1), p.s.text, 0);
        markX += 13.0f;
    };
    if (behavior.sender) icon(SENDER);
    if (behavior.receiver) icon(RECEIVER);
    if (p.o.showPriorities && behavior.priority != 0) {
        const std::string text = std::to_string(behavior.priority);
        p.Text(Point(box.Right() - 4 - p.TextWidth(text, 0.8f), box.Bottom() - 13), p.s.mutedText, text, box, 0.8f);
    }
    p.Outline(box, behavior.ref);
}

// Dev pins: hollow triangles; behavior I/O point right on the side edges,
// parameter I/O point down against the top/bottom edges; the Target is a
// gray square above the box plus one inside its top-left corner.
void DrawPin(const Painter &p, const PinItem &pin) {
    const ImVec2 c = p.P(pin.center);
    const float z = p.v.zoom;
    const bool emphasized = p.Selected(pin.ref) || p.Hovered(pin.ref);
    auto triangle = [&](ImVec2 a, ImVec2 b, ImVec2 d) {
        p.dl->AddTriangleFilled(a, b, d, p.s.pinFill);
        p.dl->AddTriangle(a, b, d, p.s.pinBorder);
    };
    auto square = [&](float cy) {
        const float r = std::max(1.5f, TARGET_HALF_SIZE * z);
        const ImVec2 a(c.x - r, cy - r), b(c.x + r, cy + r);
        p.dl->AddRectFilled(a, b, p.s.targetFill);
        p.dl->AddRect(a, b, p.s.pinBorder);
    };
    const float hw = std::max(1.5f, PIN_GLYPH_HALF_WIDTH * z), h = std::max(1.5f, PIN_GLYPH_HEIGHT * z);
    switch (pin.kind) {
    case PinKind::BehaviorIn:
    case PinKind::BehaviorOut: {
        const float r = std::max(2.0f, p.Scale(3.5f));
        triangle(ImVec2(c.x - r * 0.8f, c.y - r), ImVec2(c.x + r * 0.8f, c.y), ImVec2(c.x - r * 0.8f, c.y + r));
        break;
    }
    case PinKind::ParamIn: {
        const float top = c.y + PIN_IN_GLYPH_TOP * z;
        triangle(ImVec2(c.x - hw, top), ImVec2(c.x + hw, top), ImVec2(c.x, top + h));
        break;
    }
    case PinKind::ParamOut: {
        const float top = c.y + PIN_OUT_GLYPH_TOP * z;
        triangle(ImVec2(c.x - hw, top), ImVec2(c.x + hw, top), ImVec2(c.x, top + h));
        break;
    }
    case PinKind::Target:
        square(c.y + TARGET_PIN_OFFSET * z);
        if (!pin.onScript) square(c.y + TARGET_MARK_OFFSET * z);
        break;
    default:
        break;
    }
    if (emphasized) p.dl->AddCircle(c, std::max(4.0f, p.Scale(6.0f)), p.Selected(pin.ref) ? p.s.selection : p.s.hover, 0, 2.0f);
}

// The segment a link label goes on: the longest one.
void LongestSegment(const LinkItem &link, Point &a, Point &b) {
    size_t longest = 1;
    float best = -1.0f;
    for (size_t i = 1; i < link.path.size(); ++i) {
        const float dx = link.path[i].h - link.path[i - 1].h, dy = link.path[i].v - link.path[i - 1].v;
        const float length = dx * dx + dy * dy;
        if (length > best) {
            best = length;
            longest = i;
        }
    }
    a = link.path[longest - 1];
    b = link.path[longest];
}

void DrawLink(const Painter &p, const LinkItem &link) {
    if (link.hidden) return;
    if (link.path.size() < 2) {
        // Unresolved: a short stub at the resolved end, if any.
        if (link.path.size() == 1) {
            const ImVec2 a = p.P(link.path[0]);
            p.dl->AddLine(a, ImVec2(a.x + 12, a.y - 12), p.s.invalidLink, 2.0f);
        }
        return;
    }
    const bool selected = p.Selected(link.ref);
    ImU32 color = !link.resolved || p.Invalid(link.ref) ? p.s.invalidLink
                  : selected              ? p.s.selection
                  : link.behaviorLink     ? p.s.behaviorLink
                  : link.highlight        ? p.s.highlightLink
                  : link.operationLink    ? p.s.operationLink
                                          : p.s.parameterLink;
    // Printing without Use Color for Links, every link is black (CUIKLink::DrawLink).
    if (p.o.print && !p.o.print->colorLinks) color = IM_COL32(0, 0, 0, 255);
    if (p.o.activeLinks && p.o.activeLinks->count(link.ref.object)) color = IM_COL32(230, 0, 0, 255);
    const float thickness = 1.0f + (selected || p.Hovered(link.ref) ? 1.0f : 0.0f);
    p.Polyline(link.path, color, thickness, !link.behaviorLink);
    if (p.o.showControlPoints) {
        // Dev: small filled squares at the bends.
        for (size_t i = 1; i + 1 < link.path.size(); ++i) {
            const ImVec2 c = p.P(link.path[i]);
            const float r = std::max(1.5f, p.Scale(1.8f));
            p.dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r + 1, c.y + r + 1),
                                link.behaviorLink ? p.s.controlPoint : color);
        }
    }
    Point a, b;
    LongestSegment(link, a, b);
    if (p.o.showLinkInfo && link.behaviorLink) {
        // Dev writes every delay (also 0) above the longest segment.
        const std::string text = std::to_string(link.delay);
        const Point mid((a.h + b.h) * 0.5f - p.TextWidth(text, 0.85f) * 0.5f, (a.v + b.v) * 0.5f - 13.0f);
        p.Text(mid, p.s.text, text, Rect(mid.h - 20, mid.v - 2, 60, 18), 0.85f);
    }
    if (!link.conversion.empty()) {
        // The hidden conversion operation's name, in the link color ("Convert").
        const float width = p.TextWidth(link.conversion, 0.85f);
        const Point mid((a.h + b.h) * 0.5f - width * 0.5f, (a.v + b.v) * 0.5f - 12.0f);
        p.Text(mid, color, link.conversion, Rect(mid.h - 4, mid.v - 2, width + 8, 18), 0.85f);
    }
}

void DrawOperation(const Painter &p, const OperationItem &operation) {
    const Rect &b = operation.bounds;
    // Type 20 body row between the input row and the output row.
    const Rect body(b.hPos, b.vPos + 20.0f, b.hSize, 20.0f);
    p.Box(body, p.Fill(p.s.operationFill), p.s.operationBorder);
    p.CenteredText(body, p.s.text, operation.name, 0.9f);
    p.Outline(b, operation.ref);
}

// Local parameter icons (Dev, 2D Text.cmo): a local some shortcut shows is a
// black square with a light arrow; a shortcut a white square with a black
// arrow; any other local a small gray square.
void DrawLocalIcon(const Painter &p, const ParamItem &param, const Point &center) {
    const ImVec2 c = p.P(center);
    const float z = p.v.zoom;
    if (!param.shortcut && !param.shortcutSource) {
        const float r = std::max(1.5f, 3.0f * z);
        p.dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), p.s.plainLocalFill);
        p.dl->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), p.s.parameterBorder);
        return;
    }
    const float r = std::max(2.0f, 4.5f * z);
    const ImVec2 a(c.x - r, c.y - r), b(c.x + r, c.y + r);
    const float w = std::max(1.5f, 2.0f * z), head = 4.5f * z, inset = 1.5f * z;
    // Shortcut: white with a black arrow up-right; its local: black with a
    // light arrow down-right.
    const bool up = param.shortcut;
    p.dl->AddRectFilled(a, b, up ? p.s.shortcutIcon : p.s.localIcon);
    const ImU32 arrow = up ? p.s.localIcon : p.s.localIconArrow;
    const ImVec2 tip = up ? ImVec2(b.x - inset, a.y + inset) : ImVec2(b.x - inset, b.y - inset);
    const ImVec2 tail = up ? ImVec2(a.x + 2.0f * z, b.y - 1.0f * z) : ImVec2(a.x + 1.0f * z, a.y + 2.0f * z);
    const ImVec2 bend = up ? ImVec2(a.x + 2.5f * z, c.y) : ImVec2(c.x, a.y + 2.5f * z);
    const ImVec2 base = up ? ImVec2(tip.x - head * 0.55f, tip.y + head * 0.55f) : ImVec2(tip.x - head * 0.55f, tip.y - head * 0.55f);
    p.dl->AddBezierQuadratic(tail, bend, base, arrow, w);
    if (up)
        p.dl->AddTriangleFilled(tip, ImVec2(tip.x - head, tip.y), ImVec2(tip.x, tip.y + head), arrow);
    else
        p.dl->AddTriangleFilled(tip, ImVec2(tip.x, tip.y - head), ImVec2(tip.x - head, tip.y), arrow);
}

// Local parameters and shortcuts as Dev draws them: an icon, then the name
// and/or the value; collapsed ones are small gray bars at the top of their
// cell.
void DrawParam(const Painter &p, const ParamItem &param) {
    if (param.hidden) return;
    const Rect &b = param.display;
    const int mode = ParamDisplayMode(param.style);
    if (mode == PARAM_STYLE_COLLAPSED) {
        const Rect bar(param.center.h - 5.5f, param.center.v - 10.0f, 11.0f, 5.0f);
        p.Box(bar, p.Fill(p.s.collapsedFill), p.s.parameterBorder);
        p.Outline(bar, param.ref, 1.0f);
        return;
    }
    DrawLocalIcon(p, param, param.center);
    const Rect clip(b.hPos, b.vPos, std::max(b.hSize, 400.0f), b.vSize);
    const float textX = param.center.h + 9.0f, textY = param.center.v - p.o.fontSize * 0.45f;
    if (mode == PARAM_STYLE_VALUE) {
        p.Text(Point(textX, textY), p.s.text, param.value, clip, 0.9f);
    } else {
        p.Text(Point(textX, textY), p.s.text, param.name, clip, 0.9f);
        if (mode == PARAM_STYLE_NAMEVALUE)
            p.Text(Point(textX + p.TextWidth(param.name, 0.9f) + 18.0f, textY), p.s.text, param.value, clip, 0.9f);
    }
    p.Outline(b, param.ref);
}

void DrawComment(const Painter &p, const CommentItem &comment) {
    if (comment.collapsed) {
        const Rect icon(comment.bounds.hPos, comment.bounds.vPos, 20.0f, 20.0f);
        p.Box(icon, p.Fill(p.s.commentFill), p.s.commentBorder);
        p.Outline(icon, comment.ref);
        return;
    }
    p.Box(comment.bounds, p.Fill(p.s.commentFill), 0);
    p.Text(Point(comment.bounds.hPos + 3, comment.bounds.vPos + 2), p.s.text, comment.text, comment.bounds, 0.9f,
           comment.bounds.hSize - 6);
    p.Outline(comment.bounds, comment.ref);
}

// Dev's Start: a pentagon arrow pointing right.
void DrawStart(const Painter &p, const StartItem &start) {
    const Rect &b = start.bounds;
    const ImVec2 points[5] = {p.P(b.hPos, b.vPos), p.P(b.hPos + b.hSize * 0.55f, b.vPos),
                              p.P(b.Right(), b.CenterY()), p.P(b.hPos + b.hSize * 0.55f, b.Bottom()),
                              p.P(b.hPos, b.Bottom())};
    if (const ImU32 fill = p.Fill(p.s.startFill)) p.dl->AddConvexPolyFilled(points, 5, fill);
    p.dl->AddPolyline(points, 5, p.s.startBorder, ImDrawFlags_Closed, 1.0f);
    p.Outline(b, start.ref);
}

} // namespace

namespace {
void DrawScriptBody(ImDrawList *drawList, const SchematicModel &model, float offsetY, const ViewTransform &view,
                    const ViewTransform &frame, const DrawOptions &options);
} // namespace

void DrawGrid(ImDrawList *drawList, const ViewTransform &view, const ImVec2 &min, const ImVec2 &max,
              const SchematicStyle &style) {
    float step = 20.0f;
    while (step * view.zoom < 8.0f) step *= 5.0f;
    const Point a = view.ToWorld(min), b = view.ToWorld(max);
    for (float x = std::floor(a.h / step) * step; x <= b.h; x += step) {
        const float sx = view.ToScreen(x, 0).x;
        drawList->AddLine(ImVec2(sx, min.y), ImVec2(sx, max.y), style.grid);
    }
    for (float y = std::floor(a.v / step) * step; y <= b.v; y += step) {
        const float sy = view.ToScreen(0, y).y;
        drawList->AddLine(ImVec2(min.x, sy), ImVec2(max.x, sy), style.grid);
    }
}

void DrawScript(ImDrawList *drawList, const SchematicModel &model, float offsetY, const ViewTransform &frame,
                const ViewTransform &body, const DrawOptions &options) {
    Painter f(drawList, frame, offsetY, options);
    const Rect &bounds = model.script.bounds;
    const Rect band(-1e5f, bounds.vPos, 2e5f, bounds.vSize);
    if (!f.Visible(band, 40.0f)) return;
    if (model.script.open) {
        // The body: right of the header, inside the band.
        const float left = options.showHeaders ? bounds.hPos + model.script.header.hSize : bounds.hPos;
        const ImVec2 a = f.P(left, bounds.vPos), b = f.P(bounds.Right(), bounds.Bottom());
        DrawOptions clipped = options;
        clipped.clipMin = ImVec2(std::max(a.x, options.clipMin.x), std::max(a.y, options.clipMin.y));
        clipped.clipMax = ImVec2(options.clipMax.x, std::min(b.y, options.clipMax.y));
        if (clipped.clipMax.x > clipped.clipMin.x && clipped.clipMax.y > clipped.clipMin.y) {
            drawList->PushClipRect(clipped.clipMin, clipped.clipMax, true);
            DrawScriptBody(drawList, model, offsetY, body, frame, clipped);
            drawList->PopClipRect();
        }
    }
    DrawScriptFrame(f, model);
    // Start is attached to the header (frame): body zoom and scroll leave it in place.
    if (model.script.open && model.start.present) {
        const ImVec2 a = f.P(bounds.hPos, bounds.vPos), b = f.P(bounds.Right(), bounds.Bottom());
        drawList->PushClipRect(ImVec2(std::max(a.x, options.clipMin.x), std::max(a.y, options.clipMin.y)),
                               ImVec2(options.clipMax.x, std::min(b.y, options.clipMax.y)), true);
        DrawStart(f, model.start);
        drawList->PopClipRect();
    }
}

namespace {

void DrawScriptBody(ImDrawList *drawList, const SchematicModel &model, float offsetY, const ViewTransform &view,
                    const ViewTransform &frame, const DrawOptions &options) {
    Painter p(drawList, view, offsetY, options);
    // A Start end lies in frame coordinates: re-express it in body coordinates.
    auto fromFrame = [&](const Point &point) {
        const ImVec2 screen = frame.ToScreen(point.h, point.v + offsetY);
        const Point world = view.ToWorld(screen);
        return Point(world.h, world.v - offsetY);
    };

    // Dev draws graph by graph: a graph's own contents, then each expanded
    // child graph (its opaque frame over them) with its contents.
    std::map<CK_ID, CK_ID> behaviorParent, operationGraph;
    for (const auto &behavior : model.behaviors) behaviorParent[behavior.id] = behavior.parent;
    for (const auto &operation : model.operations) operationGraph[operation.id] = operation.owner;
    // The graph a pin is drawn with: an open graph's own pins go with its
    // frame; others with the graph holding their behavior or operation.
    auto pinGraph = [&](const PinItem &pin) -> CK_ID {
        if (model.IsOpenGraph(pin.owner)) return pin.owner;
        auto behavior = behaviorParent.find(pin.owner);
        if (behavior != behaviorParent.end()) return behavior->second;
        auto operation = operationGraph.find(pin.owner);
        return operation != operationGraph.end() ? operation->second : model.script.id;
    };
    std::map<CK_ID, std::vector<const BehaviorItem *>> openChildren;
    for (const auto &behavior : model.behaviors)
        if (model.IsOpenGraph(behavior.id) && behavior.id != model.script.id) openChildren[behavior.parent].push_back(&behavior);
    std::function<void(CK_ID)> level = [&](CK_ID graph) {
        for (const auto &comment : model.comments)
            if (comment.owner == graph && p.Visible(comment.bounds)) DrawComment(p, comment);
        // Dev draws links over the local parameter texts ("OnClick" in 2D Text.cmo).
        for (const auto &param : model.params)
            if (param.owner == graph && IsParamShown(param, options.showLocalParams) && p.Visible(param.display))
                DrawParam(p, param);
        for (const auto &link : model.links) {
            if (link.owner != graph || !IsLinkShown(link, options.showLocalParams)) continue;
            if (link.path.size() >= 2 && (link.start.type == ENDPOINT_START_BIN || link.end.type == ENDPOINT_START_BIN)) {
                LinkItem anchored = link;
                if (link.start.type == ENDPOINT_START_BIN) anchored.path.front() = fromFrame(link.path.front());
                if (link.end.type == ENDPOINT_START_BIN) anchored.path.back() = fromFrame(link.path.back());
                DrawLink(p, anchored);
            } else {
                DrawLink(p, link);
            }
        }
        for (const auto &operation : model.operations)
            if (operation.owner == graph && p.Visible(operation.bounds)) DrawOperation(p, operation);
        for (const auto &behavior : model.behaviors)
            if (behavior.parent == graph && !model.IsOpenGraph(behavior.id) && p.Visible(behavior.box)) DrawBehavior(p, behavior);
        for (const auto &pin : model.pins)
            if (pin.owner != graph && pinGraph(pin) == graph) DrawPin(p, pin);
        auto children = openChildren.find(graph);
        if (children != openChildren.end())
            for (const BehaviorItem *child : children->second) {
                if (p.Visible(child->box)) DrawOpenGraph(p, *child);
                level(child->id);
            }
        // The graph's own pins, on its frame.
        for (const auto &pin : model.pins)
            if (pin.owner == graph && graph != model.script.id) DrawPin(p, pin);
    };
    level(model.script.id);
    // Anything held by a graph that is not displayed (defensive): drawn last.
    std::set<CK_ID> drawn{model.script.id};
    for (const auto &behavior : model.behaviors)
        if (model.IsOpenGraph(behavior.id)) drawn.insert(behavior.id);
    for (const auto &link : model.links)
        if (!drawn.count(link.owner) && IsLinkShown(link, options.showLocalParams)) DrawLink(p, link);

    // Script Debugger marks (library drawing): running behaviors framed in
    // orange, the debug context's behavior in red; breakpoints as red dots.
    const ImU32 breakpointFill = IM_COL32(220, 30, 30, 255), breakpointEdge = IM_COL32(90, 0, 0, 255);
    auto dot = [&](const Point &at) {
        const float r = std::max(3.0f, p.Scale(4.5f));
        p.dl->AddCircleFilled(p.P(at), r, breakpointFill);
        p.dl->AddCircle(p.P(at), r, breakpointEdge, 0, 1.0f);
    };
    for (const auto &behavior : model.behaviors) {
        if (!p.Visible(behavior.box)) continue;
        const bool current = options.debugBehavior && options.debugBehavior == behavior.id;
        if (current || (options.running && options.running->count(behavior.id))) {
            const Rect &b = behavior.box;
            p.dl->AddRect(p.P(b.hPos - 2, b.vPos - 2), p.P(b.Right() + 2, b.Bottom() + 2),
                          current || options.trace ? IM_COL32(230, 0, 0, 255) : IM_COL32(255, 140, 0, 255), 0.0f, 0,
                          current ? 3.0f : 2.0f);
        }
        if (behavior.breakpoint) dot(Point(behavior.box.hPos + 6.0f, behavior.box.vPos + 6.0f));
    }
    for (const auto &link : model.links)
        if (link.breakpoint && link.path.size() >= 2 && IsLinkShown(link, options.showLocalParams)) {
            Point a, b;
            LongestSegment(link, a, b);
            dot(Point((a.h + b.h) * 0.5f, (a.v + b.v) * 0.5f));
        }
    for (const auto &param : model.params)
        if (param.breakpoint && IsParamShown(param, options.showLocalParams))
            dot(Point(param.display.hPos - 5.0f, param.display.vPos + param.display.vSize * 0.5f));
}

} // namespace

} // namespace vdev
