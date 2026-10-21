#include "html/detail/HtmlPainting.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

static HtmlColor HtmlPaintColor(litehtml::web_color color) {
    return unsigned(color.red) | unsigned(color.green) << 8 | unsigned(color.blue) << 16 | unsigned(color.alpha) << 24;
}
static litehtml::border_radiuses HtmlUsedRadii(const litehtml::position &p, litehtml::border_radiuses r) {
    float *values[] = {&r.top_left_x,     &r.top_left_y,     &r.top_right_x,   &r.top_right_y,
                       &r.bottom_right_x, &r.bottom_right_y, &r.bottom_left_x, &r.bottom_left_y};
    for (auto *value : values)
        *value = std::max(0.0f, *value);
    float factor = 1;
    const auto fit = [&](float size, float sum) {
        if (sum > 0)
            factor = std::min(factor, std::max(0.0f, size) / sum);
    };
    fit(p.width, r.top_left_x + r.top_right_x);
    fit(p.width, r.bottom_left_x + r.bottom_right_x);
    fit(p.height, r.top_left_y + r.bottom_left_y);
    fit(p.height, r.top_right_y + r.bottom_right_y);
    for (auto *value : values)
        *value *= factor;
    return r;
}
static std::vector<HtmlPoint> HtmlRoundedRectangle(const litehtml::position &p, const litehtml::border_radiuses &r,
                                                   float scale, int firstCorner = 0, int cornerCount = 4) {
    const auto used = HtmlUsedRadii(p, r);
    if (!r.top_left_x && !r.top_left_y && !r.top_right_x && !r.top_right_y && !r.bottom_right_x && !r.bottom_right_y &&
        !r.bottom_left_x && !r.bottom_left_y) {
        std::vector<HtmlPoint> corners{{p.x, p.y}, {p.right(), p.y}, {p.right(), p.bottom()}, {p.x, p.bottom()}};
        std::rotate(corners.begin(), corners.begin() + firstCorner, corners.end());
        corners.resize(cornerCount);
        return corners;
    }
    const std::array<float, 4> xs = {used.top_left_x, used.top_right_x, used.bottom_right_x, used.bottom_left_x};
    const std::array<float, 4> ys = {used.top_left_y, used.top_right_y, used.bottom_right_y, used.bottom_left_y};
    std::vector<HtmlPoint> result;
    for (int corner = 0; corner < cornerCount; ++corner) {
        const int i = (firstCorner + corner) % 4;
        const float rx = xs[i], ry = ys[i];
        const float cx = i == 0 || i == 3 ? p.x + rx : p.right() - rx;
        const float cy = i < 2 ? p.y + ry : p.bottom() - ry;
        const int segments =
            rx == 0 || ry == 0 ? 1 : std::clamp(int(std::ceil(std::sqrt(std::max(rx, ry) * scale) * 2)), 4, 64);
        for (int n = 0; n <= segments; ++n) {
            const float angle = (float(i) + 2 + float(n) / segments) * 1.5707963268f;
            result.push_back({cx + rx * std::cos(angle), cy + ry * std::sin(angle)});
        }
    }
    return result;
}
static std::vector<HtmlPoint> HtmlBorderArc(const litehtml::position &box, const litehtml::borders &border, int side,
                                            float depth, float scale) {
    const auto radii = HtmlUsedRadii(box, border.radius);
    const float left = border.left.width * depth, right = border.right.width * depth;
    const float top = border.top.width * depth, bottom = border.bottom.width * depth;
    const litehtml::position inset{box.x + left, box.y + top, std::max(0.0f, box.width - left - right),
                                   std::max(0.0f, box.height - top - bottom)};
    auto inner = radii;
    inner.top_left_x -= left;
    inner.top_left_y -= top;
    inner.top_right_x -= right;
    inner.top_right_y -= top;
    inner.bottom_right_x -= right;
    inner.bottom_right_y -= bottom;
    inner.bottom_left_x -= left;
    inner.bottom_left_y -= bottom;
    const auto contour = HtmlRoundedRectangle(inset, inner, scale, side, 2);
    const HtmlPoint outer[] = {
        {box.x, box.y}, {box.right(), box.y}, {box.right(), box.bottom()}, {box.x, box.bottom()}};
    const HtmlPoint padding[] = {{box.x + border.left.width, box.y + border.top.width},
                                 {box.right() - border.right.width, box.y + border.top.width},
                                 {box.right() - border.right.width, box.bottom() - border.bottom.width},
                                 {box.x + border.left.width, box.bottom() - border.bottom.width}};
    std::vector<HtmlPoint> arc;
    const auto append = [&](HtmlPoint point) {
        if (arc.empty() || std::hypot(arc.back().x - point.x, arc.back().y - point.y) > .0001f)
            arc.push_back(point);
    };
    for (size_t i = 1; i < contour.size(); ++i) {
        const auto a = contour[i - 1], z = contour[i];
        float begin = 0, end = 1;
        for (int edge = 0; edge < 2; ++edge) {
            const int corner = (side + edge) % 4;
            const auto distance = [&](HtmlPoint point) {
                const auto o = outer[corner], p = padding[corner];
                return ((p.x - o.x) * (point.y - o.y) - (p.y - o.y) * (point.x - o.x)) * (edge ? 1 : -1);
            };
            const float before = distance(a), after = distance(z);
            if (before < 0 && after < 0) {
                end = -1;
                break;
            }
            if ((before < 0) != (after < 0)) {
                const float t = before / (before - after);
                if (before < 0)
                    begin = std::max(begin, t);
                else
                    end = std::min(end, t);
            }
        }
        if (end >= begin) {
            append({a.x + (z.x - a.x) * begin, a.y + (z.y - a.y) * begin});
            append({a.x + (z.x - a.x) * end, a.y + (z.y - a.y) * end});
        }
    }
    return arc;
}
static std::vector<HtmlPoint> HtmlClipEdge(const std::vector<HtmlPoint> &polygon, HtmlPoint a, HtmlPoint b) {
    if (polygon.empty())
        return {};
    const auto distance = [&](HtmlPoint p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); };
    std::vector<HtmlPoint> output;
    HtmlPoint previous = polygon.back();
    float before = distance(previous);
    for (const HtmlPoint current : polygon) {
        const float after = distance(current);
        if ((before >= 0) != (after >= 0)) {
            const float t = before / (before - after);
            output.push_back({previous.x + (current.x - previous.x) * t, previous.y + (current.y - previous.y) * t});
        }
        if (after >= 0)
            output.push_back(current);
        previous = current;
        before = after;
    }
    return output;
}
static std::vector<HtmlPoint> HtmlClipPolygon(std::vector<HtmlPoint> polygon, const std::vector<HtmlPoint> &clip) {
    if (clip.empty())
        return {};
    for (size_t i = 0; i < clip.size() && !polygon.empty(); ++i)
        polygon = HtmlClipEdge(polygon, clip[i], clip[(i + 1) % clip.size()]);
    return polygon;
}
static float HtmlArcLength(const std::vector<HtmlPoint> &arc) {
    float length = 0;
    for (size_t i = 1; i < arc.size(); ++i)
        length += std::hypot(arc[i].x - arc[i - 1].x, arc[i].y - arc[i - 1].y);
    return length;
}
struct HtmlArcSample {
    HtmlPoint point, direction;
};
static HtmlArcSample HtmlSampleArc(const std::vector<HtmlPoint> &arc, float fraction) {
    const float target = fraction * HtmlArcLength(arc);
    float travelled = 0;
    for (size_t i = 1; i < arc.size(); ++i) {
        const auto a = arc[i - 1], z = arc[i];
        const HtmlPoint direction{z.x - a.x, z.y - a.y};
        const float segment = std::hypot(direction.x, direction.y);
        if (segment > 0 && (travelled + segment >= target || i + 1 == arc.size())) {
            const float t = std::clamp((target - travelled) / segment, 0.0f, 1.0f);
            return {{a.x + direction.x * t, a.y + direction.y * t}, direction};
        }
        travelled += segment;
    }
    return {arc.empty() ? HtmlPoint{} : arc.front(), {}};
}
static litehtml::position HtmlPolygonBounds(const std::vector<HtmlPoint> &polygon) {
    if (polygon.empty())
        return {};
    float left = polygon[0].x, right = left, top = polygon[0].y, bottom = top;
    for (const auto &point : polygon) {
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    return {left, top, right - left, bottom - top};
}
void HtmlPainting::Begin(HtmlPoint origin, float scale, const litehtml::position &visible) {
    m_Origin = origin;
    m_Scale = scale;
    m_Clips.clear();
    m_Gradients.clear();
    PushClip(visible);
}
void HtmlPainting::End() {
    m_Clips.clear();
    m_Device.SetClip({});
}
void HtmlPainting::PushClip(const litehtml::position &p, const litehtml::border_radiuses &r) {
    auto polygon = HtmlRoundedRectangle(p, r, m_Scale);
    if (!m_Clips.empty())
        polygon = HtmlClipPolygon(std::move(polygon), m_Clips.back());
    m_Clips.push_back(std::move(polygon));
    ApplyClip();
}
void HtmlPainting::PopClip() {
    if (!m_Clips.empty())
        m_Clips.pop_back();
    ApplyClip();
}
void HtmlPainting::ApplyClip() {
    if (m_Clips.empty()) {
        m_Device.SetClip({});
        return;
    }
    std::vector<HtmlPoint> points;
    for (const auto &p : m_Clips.back())
        points.push_back({m_Origin.x + p.x * m_Scale, m_Origin.y + p.y * m_Scale});
    if (points.empty())
        points.assign(3, m_Origin); // empty region, not a reset
    m_Device.SetClip(points);
}
void HtmlPainting::Polygon(const std::vector<HtmlPoint> &points, litehtml::web_color color) {
    if (!color.alpha)
        return;
    std::vector<HtmlPoint> output;
    for (const auto &p : points)
        output.push_back({m_Origin.x + p.x * m_Scale, m_Origin.y + p.y * m_Scale});
    m_Device.Fill(output, HtmlPaintColor(color));
}
void HtmlPainting::Text(const HtmlFont &font, std::string_view text, const litehtml::position &p,
                        litehtml::web_color color) {
    m_Device.Text(font, text, {m_Origin.x + p.x * m_Scale, m_Origin.y + p.y * m_Scale}, HtmlPaintColor(color));
}
void HtmlPainting::Image(const HtmlImage &image, const litehtml::background_layer &layer) {
    PushClip(layer.border_box, layer.border_radius);
    PushClip(layer.clip_box);
    const auto &p = layer.origin_box;
    const auto visible = HtmlPolygonBounds(m_Clips.back());
    if (p.width > 0 && p.height > 0 && visible.width > 0 && visible.height > 0) {
        const bool rx =
            layer.repeat == litehtml::background_repeat_repeat || layer.repeat == litehtml::background_repeat_repeat_x;
        const bool ry =
            layer.repeat == litehtml::background_repeat_repeat || layer.repeat == litehtml::background_repeat_repeat_y;
        float left = p.x, top = p.y;
        if (rx)
            left += std::floor((visible.x - left) / p.width) * p.width;
        if (ry)
            top += std::floor((visible.y - top) / p.height) * p.height;
        const float right = rx ? visible.right() : left + p.width;
        const float bottom = ry ? visible.bottom() : top + p.height;
        for (float y = top; y < bottom; y += p.height) {
            if (y + p.height <= y)
                throw std::runtime_error("Background tile height cannot advance at this position");
            for (float x = left; x < right; x += p.width) {
                if (x + p.width <= x)
                    throw std::runtime_error("Background tile width cannot advance at this position");
                m_Device.Image(
                    image, {m_Origin.x + x * m_Scale, m_Origin.y + y * m_Scale, p.width * m_Scale, p.height * m_Scale});
            }
        }
    }
    PopClip();
    PopClip();
}
void HtmlPainting::Fill(const litehtml::background_layer &layer, litehtml::web_color color) {
    PushClip(layer.border_box, layer.border_radius);
    PushClip(layer.clip_box);
    const auto &p = layer.border_box;
    Polygon({{p.x, p.y}, {p.right(), p.y}, {p.right(), p.bottom()}, {p.x, p.bottom()}}, color);
    PopClip();
    PopClip();
}
void HtmlPainting::BrokenImage(const litehtml::position &p, const HtmlFont *font, std::string_view alt,
                               litehtml::web_color color) {
    PushClip(p);
    Polygon({{p.x, p.y}, {p.right(), p.y}, {p.right(), p.bottom()}, {p.x, p.bottom()}}, {238, 238, 238});
    const float icon = std::min({16.0f, p.width, p.height});
    Polygon({{p.x, p.y}, {p.x + 1, p.y}, {p.x + icon, p.y + icon - 1}, {p.x + icon, p.y + icon}}, {160, 0, 0});
    Polygon({{p.x + icon - 1, p.y}, {p.x + icon, p.y}, {p.x + 1, p.y + icon}, {p.x, p.y + icon}}, {160, 0, 0});
    if (font && p.width > icon) {
        auto position = p;
        position.x += icon;
        position.y += (p.height - font->Metrics().height / m_Scale) / 2;
        Text(*font, alt, position, color);
    }
    PopClip();
}
void HtmlPainting::Marker(const litehtml::position &p, bool hollow, bool square, litehtml::web_color color) {
    if (square) {
        Polygon({{p.x, p.y}, {p.right(), p.y}, {p.right(), p.bottom()}, {p.x, p.bottom()}}, color);
        return;
    }
    std::vector<HtmlPoint> points;
    for (int i = 0; i < 32; ++i) {
        const float a = float(i) * 6.283185307f / 32;
        points.push_back({p.x + p.width * (.5f + .5f * std::cos(a)), p.y + p.height * (.5f + .5f * std::sin(a))});
    }
    if (!hollow)
        Polygon(points, color);
    else
        for (size_t i = 0; i < points.size(); ++i) {
            const auto a = points[i], b = points[(i + 1) % points.size()];
            const float cx = p.x + p.width / 2, cy = p.y + p.height / 2;
            const float fraction = std::max(0.0f, 1 - 2 / std::max(1.0f, p.width));
            Polygon({a,
                     b,
                     {cx + (b.x - cx) * fraction, cy + (b.y - cy) * fraction},
                     {cx + (a.x - cx) * fraction, cy + (a.y - cy) * fraction}},
                    color);
        }
}
void HtmlPainting::Borders(const litehtml::position &p, const litehtml::borders &b) {
    PushClip(p, b.radius);
    const auto &radius = b.radius;
    const bool rounded = radius.top_left_x || radius.top_left_y || radius.top_right_x || radius.top_right_y ||
                         radius.bottom_right_x || radius.bottom_right_y || radius.bottom_left_x || radius.bottom_left_y;
    const litehtml::border sides[] = {b.top, b.right, b.bottom, b.left};
    const HtmlPoint outside[] = {{p.x, p.y}, {p.right(), p.y}, {p.right(), p.bottom()}, {p.x, p.bottom()}};
    const HtmlPoint inside[] = {{p.x + b.left.width, p.y + b.top.width},
                                {p.right() - b.right.width, p.y + b.top.width},
                                {p.right() - b.right.width, p.bottom() - b.bottom.width},
                                {p.x + b.left.width, p.bottom() - b.bottom.width}};
    const auto mix = [](HtmlPoint a, HtmlPoint z, float t) {
        return HtmlPoint{a.x + (z.x - a.x) * t, a.y + (z.y - a.y) * t};
    };
    const auto shade = [](litehtml::web_color c, bool light) {
        const auto channel = [&](unsigned char v) {
            return static_cast<unsigned char>(light ? v + (255 - v) * .45f : v * .55f);
        };
        return litehtml::web_color(channel(c.red), channel(c.green), channel(c.blue), c.alpha);
    };
    for (int i = 0; i < 4; ++i) {
        const auto &side = sides[i];
        if (side.width <= 0 || side.style == litehtml::border_style_none || side.style == litehtml::border_style_hidden)
            continue;
        const auto band = [&](float begin, float end, litehtml::web_color color) {
            if (!rounded) {
                Polygon({mix(outside[i], inside[i], begin), mix(outside[(i + 1) % 4], inside[(i + 1) % 4], begin),
                         mix(outside[(i + 1) % 4], inside[(i + 1) % 4], end), mix(outside[i], inside[i], end)},
                        color);
                return;
            }
            auto outer = HtmlBorderArc(p, b, i, begin, m_Scale);
            const auto inner = HtmlBorderArc(p, b, i, end, m_Scale);
            outer.insert(outer.end(), inner.rbegin(), inner.rend());
            if (outer.size() >= 3)
                Polygon(outer, color);
        };
        if (side.style == litehtml::border_style_double && side.width >= 3) {
            band(0, 1.0f / 3, side.color);
            band(2.0f / 3, 1, side.color);
        } else if (side.style == litehtml::border_style_inset || side.style == litehtml::border_style_outset)
            band(0, 1, shade(side.color, (i == 1 || i == 2) == (side.style == litehtml::border_style_inset)));
        else if (side.style == litehtml::border_style_groove || side.style == litehtml::border_style_ridge) {
            const bool light = (i == 1 || i == 2) == (side.style == litehtml::border_style_groove);
            band(0, .5f, shade(side.color, light));
            band(.5f, 1, shade(side.color, !light));
        } else if (side.style == litehtml::border_style_dashed || side.style == litehtml::border_style_dotted) {
            const auto contour = [&](float depth) {
                return rounded ? HtmlBorderArc(p, b, i, depth, m_Scale)
                               : std::vector<HtmlPoint>{mix(outside[i], inside[i], depth),
                                                        mix(outside[(i + 1) % 4], inside[(i + 1) % 4], depth)};
            };
            const auto center = contour(.5f);
            const float length = HtmlArcLength(center);
            if (length <= 0)
                continue;
            auto band = contour(0);
            const auto inner = contour(1);
            band.insert(band.end(), inner.rbegin(), inner.rend());
            const float dash = side.width * (side.style == litehtml::border_style_dashed ? 3 : 1);
            const int count = std::max(1, int(std::ceil(length / (dash * 2))));
            for (int n = 0; n < count; ++n) {
                const float begin = float(n) / count, end = (n + .5f) / count;
                if (side.style == litehtml::border_style_dotted) {
                    const float middle = (begin + end) / 2;
                    const auto c = HtmlSampleArc(center, middle).point;
                    Marker({c.x - side.width / 2, c.y - side.width / 2, side.width, side.width}, false, false,
                           side.color);
                } else {
                    // Square ends are perpendicular to the centerline, including
                    // the straight portions between unequal corner radii.
                    const auto a = HtmlSampleArc(center, begin), z = HtmlSampleArc(center, end);
                    auto polygon = HtmlClipEdge(band, a.point, {a.point.x + a.direction.y, a.point.y - a.direction.x});
                    polygon = HtmlClipEdge(polygon, z.point, {z.point.x - z.direction.y, z.point.y + z.direction.x});
                    if (polygon.size() >= 3)
                        Polygon(polygon, side.color);
                }
            }
        } else
            band(0, 1, side.color);
    }
    PopClip();
}

static float HtmlLinearChannel(float value) {
    return value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
}
static float HtmlSrgbChannel(float value) {
    return value <= .0031308f ? 12.92f * value : 1.055f * std::pow(value, 1 / 2.4f) - .055f;
}
static std::array<float, 3> HtmlGradientComponents(litehtml::web_color color, litehtml::color_space_t space) {
    std::array<float, 3> value = {color.red / 255.0f, color.green / 255.0f, color.blue / 255.0f};
    if (space == litehtml::color_space_none || space == litehtml::color_space_srgb)
        return value;
    for (float &v : value)
        v = HtmlLinearChannel(v);
    if (space == litehtml::color_space_srgb_linear)
        return value;
    const float l = std::cbrt(.4122214708f * value[0] + .5363325363f * value[1] + .0514459929f * value[2]);
    const float m = std::cbrt(.2119034982f * value[0] + .6806995451f * value[1] + .1073969566f * value[2]);
    const float s = std::cbrt(.0883024619f * value[0] + .2817188376f * value[1] + .6299787005f * value[2]);
    return {.2104542553f * l + .7936177850f * m - .0040720468f * s,
            1.9779984951f * l - 2.4285922050f * m + .4505937099f * s,
            .0259040371f * l + .7827717662f * m - .8086757660f * s};
}
static std::array<float, 3> HtmlGradientSrgb(std::array<float, 3> value, litehtml::color_space_t space) {
    if (space == litehtml::color_space_none || space == litehtml::color_space_srgb)
        return value;
    if (space == litehtml::color_space_oklab) {
        const float l = std::pow(value[0] + .3963377774f * value[1] + .2158037573f * value[2], 3);
        const float m = std::pow(value[0] - .1055613458f * value[1] - .0638541728f * value[2], 3);
        const float s = std::pow(value[0] - .0894841775f * value[1] - 1.2914855480f * value[2], 3);
        value = {4.0767416621f * l - 3.3077115913f * m + .2309699292f * s,
                 -1.2684380046f * l + 2.6097574011f * m - .3413193965f * s,
                 -.0041960863f * l - .7034186147f * m + 1.7076147010f * s};
    }
    for (float &v : value)
        v = HtmlSrgbChannel(v);
    return value;
}
static std::array<unsigned char, 4> HtmlGradientSample(const litehtml::background_layer::gradient_base &gradient,
                                                       float offset) {
    const auto &points = gradient.color_points;
    if (points.empty())
        return {};
    size_t right = 0;
    while (right < points.size() && points[right].offset <= offset)
        ++right;
    if (right == 0) {
        const auto c = points.front().color;
        return {c.red, c.green, c.blue, c.alpha};
    }
    if (right == points.size()) {
        const auto c = points.back().color;
        return {c.red, c.green, c.blue, c.alpha};
    }
    const auto &a = points[right - 1], &b = points[right];
    float t = (offset - a.offset) / (b.offset - a.offset);
    if (a.hint) {
        const float midpoint = (*a.hint - a.offset) / (b.offset - a.offset);
        if (midpoint <= 0)
            t = 1;
        else if (midpoint >= 1)
            t = 0;
        else
            t = std::pow(t, std::log(.5f) / std::log(midpoint));
    }
    const float aa = a.color.alpha / 255.0f, ba = b.color.alpha / 255.0f, alpha = aa + (ba - aa) * t;
    const auto av = HtmlGradientComponents(a.color, gradient.color_space),
               bv = HtmlGradientComponents(b.color, gradient.color_space);
    std::array<float, 3> value{};
    if (alpha > 0)
        for (int i = 0; i < 3; ++i)
            value[i] = (av[i] * aa * (1 - t) + bv[i] * ba * t) / alpha;
    value = HtmlGradientSrgb(value, gradient.color_space);
    const auto byte = [](float v) { return static_cast<unsigned char>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    return {byte(value[0]), byte(value[1]), byte(value[2]), byte(alpha)};
}
void HtmlPainting::Gradient(const litehtml::background_layer &layer,
                            const litehtml::background_layer::gradient_base &gradient,
                            const std::function<float(float, float)> &offset) {
    if (gradient.color_space != litehtml::color_space_none && gradient.color_space != litehtml::color_space_srgb &&
        gradient.color_space != litehtml::color_space_srgb_linear &&
        gradient.color_space != litehtml::color_space_oklab)
        throw std::runtime_error("This gradient's interpolation color space is not supported");
    PushClip(layer.border_box, layer.border_radius);
    PushClip(layer.clip_box);
    if (m_Clips.back().empty()) {
        PopClip();
        PopClip();
        return;
    }
    const auto visible = HtmlPolygonBounds(m_Clips.back());
    float left = visible.x, top = visible.y, right = visible.right(), bottom = visible.bottom();
    const auto &box = layer.origin_box;
    const bool rx =
        layer.repeat == litehtml::background_repeat_repeat || layer.repeat == litehtml::background_repeat_repeat_x;
    const bool ry =
        layer.repeat == litehtml::background_repeat_repeat || layer.repeat == litehtml::background_repeat_repeat_y;
    if (!rx) {
        left = std::max(left, box.x);
        right = std::min(right, box.right());
    }
    if (!ry) {
        top = std::max(top, box.y);
        bottom = std::min(bottom, box.bottom());
    }
    if (right > left && bottom > top && box.width > 0 && box.height > 0) {
        const auto width = static_cast<unsigned>(std::ceil((right - left) * m_Scale));
        const auto height = static_cast<unsigned>(std::ceil((bottom - top) * m_Scale));
        if (width > 16384 || height > 16384 || std::uint64_t(width) * height > 67108864)
            throw std::runtime_error("Gradient viewport is too large");
        std::vector<unsigned char> rgba(size_t(width) * height * 4);
        const auto repeat = [](float value, float origin, float size) {
            return origin + value - origin - std::floor((value - origin) / size) * size;
        };
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                float px = left + (x + .5f) * (right - left) / width, py = top + (y + .5f) * (bottom - top) / height;
                if (rx)
                    px = repeat(px, box.x, box.width);
                if (ry)
                    py = repeat(py, box.y, box.height);
                const auto color = HtmlGradientSample(gradient, offset(px, py));
                std::copy(color.begin(), color.end(), rgba.begin() + (size_t(y) * width + x) * 4);
            }
        auto image = m_Device.CreateImage(width, height, rgba.data());
        if (!image)
            throw std::runtime_error("Gradient image could not be created");
        m_Device.Image(*image, {m_Origin.x + left * m_Scale, m_Origin.y + top * m_Scale, (right - left) * m_Scale,
                                (bottom - top) * m_Scale});
        m_Gradients.push_back(std::move(image));
    }
    PopClip();
    PopClip();
}
void HtmlPainting::LinearGradient(const litehtml::background_layer &layer,
                                  const litehtml::background_layer::linear_gradient &gradient) {
    const float dx = gradient.end.x - gradient.start.x, dy = gradient.end.y - gradient.start.y,
                length = dx * dx + dy * dy;
    Gradient(layer, gradient, [&](float x, float y) {
        return length > 0 ? ((x - gradient.start.x) * dx + (y - gradient.start.y) * dy) / length : 1;
    });
}
void HtmlPainting::RadialGradient(const litehtml::background_layer &layer,
                                  const litehtml::background_layer::radial_gradient &gradient) {
    Gradient(layer, gradient, [&](float x, float y) {
        if (gradient.radius.x <= 0 || gradient.radius.y <= 0)
            return 1.0f;
        return std::hypot((x - gradient.position.x) / gradient.radius.x, (y - gradient.position.y) / gradient.radius.y);
    });
}
void HtmlPainting::ConicGradient(const litehtml::background_layer &layer,
                                 const litehtml::background_layer::conic_gradient &gradient) {
    Gradient(layer, gradient, [&](float x, float y) {
        const float angle =
            std::atan2(x - gradient.position.x, -(y - gradient.position.y)) / (6.283185307f) - gradient.angle / 360;
        return angle - std::floor(angle);
    });
}
