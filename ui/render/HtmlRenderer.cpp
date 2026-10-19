#include "ui/render/HtmlRenderer.h"
#include "imgui_internal.h"
#include "ui/render/UiRenderer.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace vdev {
class UiHtmlFont : public HtmlFont {
public:
    ImFont *font;
    HtmlFontDescription style;
    bool syntheticBold;
    UiHtmlFont(ImFont *face, HtmlFontDescription description, bool strong)
        : font(face), style(std::move(description)), syntheticBold(strong) {}
    HtmlFontMetrics Metrics() const override {
        if (style.size == 0)
            return {};
        auto *baked = font->GetFontBaked(style.size);
        const auto *glyph = baked->FindGlyph('x');
        return {baked->Ascent, -baked->Descent, baked->Ascent - baked->Descent,
                glyph ? glyph->Y1 - glyph->Y0 : style.size / 2};
    }
    float Width(std::string_view text) const override {
        if (text.empty() || style.size == 0)
            return 0;
        return font->CalcTextSizeA(style.size, FLT_MAX, 0, text.data(), text.data() + text.size()).x;
    }
};
class UiHtmlImage : public HtmlImage {
public:
    std::shared_ptr<UiTexture> texture;
    std::string url;
    HtmlSize Size() const override { return {float(texture->Width()), float(texture->Height())}; }
};
struct HtmlRenderer::Impl {
    HtmlFonts fonts;
    ImDrawList *draw = nullptr;
    std::unique_ptr<ImDrawList> scratch;
    std::vector<HtmlPoint> clip;
#ifdef VIRTOOLSDEV_UI_TESTS
    std::vector<Drawing> drawings;
#endif
    explicit Impl(HtmlFonts value) : fonts(value) {}
    static ImDrawVert Interpolate(const ImDrawVert &a, const ImDrawVert &b, float t) {
        ImDrawVert out;
        out.pos = ImVec2(a.pos.x + (b.pos.x - a.pos.x) * t, a.pos.y + (b.pos.y - a.pos.y) * t);
        out.uv = ImVec2(a.uv.x + (b.uv.x - a.uv.x) * t, a.uv.y + (b.uv.y - a.uv.y) * t);
        out.col = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            const float av = float((a.col >> shift) & 255), bv = float((b.col >> shift) & 255);
            out.col |= unsigned(std::lround(std::clamp(av + (bv - av) * t, 0.0f, 255.0f))) << shift;
        }
        return out;
    }
    void Submit() {
        for (const auto &command : scratch->CmdBuffer) {
            if (!command.ElemCount)
                continue;
            draw->PushTexture(command.TexRef);
            for (unsigned at = command.IdxOffset; at < command.IdxOffset + command.ElemCount; at += 3) {
                std::vector<ImDrawVert> polygon;
                for (unsigned i = 0; i < 3; ++i)
                    polygon.push_back(scratch->VtxBuffer[command.VtxOffset + scratch->IdxBuffer[at + i]]);
                for (size_t edge = 0; edge < clip.size() && !polygon.empty(); ++edge) {
                    const auto a = clip[edge], b = clip[(edge + 1) % clip.size()];
                    const auto distance = [&](const ImDrawVert &v) {
                        return (b.x - a.x) * (v.pos.y - a.y) - (b.y - a.y) * (v.pos.x - a.x);
                    };
                    std::vector<ImDrawVert> output;
                    auto previous = polygon.back();
                    float before = distance(previous);
                    for (const auto &current : polygon) {
                        const float after = distance(current);
                        if ((before >= 0) != (after >= 0))
                            output.push_back(Interpolate(previous, current, before / (before - after)));
                        if (after >= 0)
                            output.push_back(current);
                        previous = current;
                        before = after;
                    }
                    polygon = std::move(output);
                }
                for (size_t i = 1; i + 1 < polygon.size(); ++i) {
                    draw->PrimReserve(3, 3);
                    const ImDrawIdx first = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);
                    draw->PrimWriteIdx(first);
                    draw->PrimWriteIdx(ImDrawIdx(first + 1));
                    draw->PrimWriteIdx(ImDrawIdx(first + 2));
                    for (const auto *vertex : {&polygon[0], &polygon[i], &polygon[i + 1]})
                        draw->PrimWriteVtx(vertex->pos, vertex->uv, vertex->col);
                }
            }
            draw->PopTexture();
        }
    }
    void Paint(const std::function<void(ImDrawList *)> &paint) {
        if (!draw) {
#ifdef VIRTOOLSDEV_UI_TESTS
            return;
#else
            throw std::logic_error("HTML graphics require a draw list");
#endif
        }
        if (clip.empty()) {
            paint(draw);
            return;
        }
        float left = clip[0].x, right = left, top = clip[0].y, bottom = top;
        for (const auto &p : clip) {
            left = std::min(left, p.x);
            right = std::max(right, p.x);
            top = std::min(top, p.y);
            bottom = std::max(bottom, p.y);
        }
        if (right <= left || bottom <= top)
            return;
        const bool rectangle = clip.size() == 4 && std::all_of(clip.begin(), clip.end(), [&](const auto &p) {
                                   return (std::fabs(p.x - left) < .001f || std::fabs(p.x - right) < .001f) &&
                                          (std::fabs(p.y - top) < .001f || std::fabs(p.y - bottom) < .001f);
                               });
        if (rectangle) {
            draw->PushClipRect(ImVec2(left, top), ImVec2(right, bottom), true);
            paint(draw);
            draw->PopClipRect();
            return;
        }
        if (!scratch)
            scratch = std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData());
        scratch->_ResetForNewFrame();
        scratch->Flags = draw->Flags;
        scratch->PushClipRect(draw->GetClipRectMin(), draw->GetClipRectMax());
        scratch->PushTexture(ImGui::GetIO().Fonts->TexRef);
        paint(scratch.get());
        Submit();
    }
};
HtmlRenderer::HtmlRenderer(HtmlFonts fonts) : m_Impl(new Impl(fonts)) {}
HtmlRenderer::~HtmlRenderer() = default;
void HtmlRenderer::Begin(ImDrawList *draw) {
    m_Impl->draw = draw;
    m_Impl->clip.clear();
#ifdef VIRTOOLSDEV_UI_TESTS
    m_Impl->drawings.clear();
#endif
}
void HtmlRenderer::SetCursor(std::string_view cursor) {
    ImGuiMouseCursor shape = ImGuiMouseCursor_Arrow;
    if (cursor == "none")
        shape = ImGuiMouseCursor_None;
    else if (cursor == "pointer")
        shape = ImGuiMouseCursor_Hand;
    else if (cursor == "text" || cursor == "vertical-text")
        shape = ImGuiMouseCursor_TextInput;
    else if (cursor == "move" || cursor == "all-scroll")
        shape = ImGuiMouseCursor_ResizeAll;
    else if (cursor == "wait")
        shape = ImGuiMouseCursor_Wait;
    else if (cursor == "progress")
        shape = ImGuiMouseCursor_Progress;
    else if (cursor == "not-allowed" || cursor == "no-drop")
        shape = ImGuiMouseCursor_NotAllowed;
    else if (cursor == "e-resize" || cursor == "w-resize" || cursor == "ew-resize" || cursor == "col-resize")
        shape = ImGuiMouseCursor_ResizeEW;
    else if (cursor == "n-resize" || cursor == "s-resize" || cursor == "ns-resize" || cursor == "row-resize")
        shape = ImGuiMouseCursor_ResizeNS;
    else if (cursor == "ne-resize" || cursor == "sw-resize" || cursor == "nesw-resize")
        shape = ImGuiMouseCursor_ResizeNESW;
    else if (cursor == "nw-resize" || cursor == "se-resize" || cursor == "nwse-resize")
        shape = ImGuiMouseCursor_ResizeNWSE;
    ImGui::SetMouseCursor(shape);
}
std::shared_ptr<HtmlFont> HtmlRenderer::CreateFont(const HtmlFontDescription &description) {
    if (!std::isfinite(description.size) || description.size < 0)
        return {};
    std::string names = description.families;
    std::transform(names.begin(), names.end(), names.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    const bool mono = names.find("monospace") != std::string::npos || names.find("courier") != std::string::npos ||
                      names.find("consolas") != std::string::npos;
    ImFont *face = mono                        ? m_Impl->fonts.monospace
                   : description.weight >= 600 ? m_Impl->fonts.bold
                                               : m_Impl->fonts.normal;
    if (!face)
        return {};
    return std::make_shared<UiHtmlFont>(face, description, mono && description.weight >= 600);
}
std::shared_ptr<HtmlImage> HtmlRenderer::DecodeImage(const std::string &url, const std::vector<unsigned char> &bytes) {
    std::vector<unsigned char> rgba;
    int width = 0, height = 0;
    if (!DecodeImageRgba(bytes, rgba, width, height))
        return {};
    auto result = CreateImage(unsigned(width), unsigned(height), rgba.data());
    if (result)
        static_cast<UiHtmlImage &>(*result).url = url;
    return result;
}
std::shared_ptr<HtmlImage> HtmlRenderer::CreateImage(unsigned width, unsigned height, const unsigned char *rgba) {
    if (width > unsigned(std::numeric_limits<int>::max()) || height > unsigned(std::numeric_limits<int>::max()))
        return {};
    auto texture = MainRenderer().CreateTexture(int(width), int(height), rgba);
    if (!texture)
        return {};
    auto result = std::make_shared<UiHtmlImage>();
    result->texture = std::move(texture);
    return result;
}
void HtmlRenderer::SetClip(const std::vector<HtmlPoint> &polygon) { m_Impl->clip = polygon; }
void HtmlRenderer::Text(const HtmlFont &font, std::string_view text, HtmlPoint position, HtmlColor color) {
    const auto &native = static_cast<const UiHtmlFont &>(font);
    if (native.style.size == 0)
        return;
    m_Impl->Paint([&](ImDrawList *draw) {
        const int start = draw->VtxBuffer.Size;
        draw->AddText(native.font, native.style.size, ImVec2(position.x, position.y), color, text.data(),
                      text.data() + text.size());
        if (native.syntheticBold)
            draw->AddText(native.font, native.style.size, ImVec2(position.x + .5f, position.y), color, text.data(),
                          text.data() + text.size());
        if (native.style.italic) {
            const float baseline = position.y + native.Metrics().ascent;
            for (int i = start; i < draw->VtxBuffer.Size; ++i)
                draw->VtxBuffer[i].pos.x += (baseline - draw->VtxBuffer[i].pos.y) * .2f;
        }
        const auto metrics = native.Metrics();
        const float width = native.Width(text), thickness = std::max(1.0f, native.style.size / 16);
        if (native.style.decoration & HtmlDecorationUnderline)
            draw->AddLine(ImVec2(position.x, position.y + metrics.ascent + thickness),
                          ImVec2(position.x + width, position.y + metrics.ascent + thickness), color, thickness);
        if (native.style.decoration & HtmlDecorationOverline)
            draw->AddLine(ImVec2(position.x, position.y), ImVec2(position.x + width, position.y), color, thickness);
        if (native.style.decoration & HtmlDecorationLineThrough)
            draw->AddLine(ImVec2(position.x, position.y + metrics.ascent * .6f),
                          ImVec2(position.x + width, position.y + metrics.ascent * .6f), color, thickness);
    });
#ifdef VIRTOOLSDEV_UI_TESTS
    Drawing item;
    item.kind = Drawing::Text;
    item.position = ImVec2(position.x, position.y);
    item.size = ImVec2(native.Width(text), native.Metrics().height);
    item.text = std::string(text);
    item.color = color;
    item.style = native.style;
    item.font = native.font;
    m_Impl->drawings.push_back(std::move(item));
#endif
}
void HtmlRenderer::Image(const HtmlImage &image, HtmlRect rect) {
    const auto &native = static_cast<const UiHtmlImage &>(image);
    MainRenderer().RetainTextureForFrame(native.texture);
    m_Impl->Paint([&](ImDrawList *draw) {
        draw->AddImage(native.texture->Id(), ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.width, rect.y + rect.height));
    });
#ifdef VIRTOOLSDEV_UI_TESTS
    Drawing item;
    item.kind = Drawing::Image;
    item.position = ImVec2(rect.x, rect.y);
    item.size = ImVec2(rect.width, rect.height);
    item.image = native.url;
    m_Impl->drawings.push_back(std::move(item));
#endif
}
void HtmlRenderer::Fill(const std::vector<HtmlPoint> &polygon, HtmlColor color) {
    if (polygon.size() < 3)
        return;
    std::vector<ImVec2> points;
    for (const auto &p : polygon)
        points.emplace_back(p.x, p.y);
    m_Impl->Paint([&](ImDrawList *draw) { draw->AddConcavePolyFilled(points.data(), int(points.size()), color); });
#ifdef VIRTOOLSDEV_UI_TESTS
    Drawing item;
    item.kind = Drawing::Fill;
    item.color = color;
    float left = polygon[0].x, right = left, top = polygon[0].y, bottom = top;
    for (const auto &p : polygon) {
        left = std::min(left, p.x);
        right = std::max(right, p.x);
        top = std::min(top, p.y);
        bottom = std::max(bottom, p.y);
    }
    item.position = ImVec2(left, top);
    item.size = ImVec2(right - left, bottom - top);
    m_Impl->drawings.push_back(std::move(item));
#endif
}
#ifdef VIRTOOLSDEV_UI_TESTS
const std::vector<HtmlRenderer::Drawing> &HtmlRenderer::Drawings() const { return m_Impl->drawings; }
#endif
} // namespace vdev
