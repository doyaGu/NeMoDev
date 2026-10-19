#include "html/detail/HtmlPainting.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

static HtmlColor HtmlPaintColor(litehtml::web_color color) {
    return unsigned(color.red) | unsigned(color.green) << 8 | unsigned(color.blue) << 16 | unsigned(color.alpha) << 24;
}
static std::vector<HtmlPoint> HtmlRoundedRectangle(const litehtml::position &p, const litehtml::border_radiuses &r,
                                                   float scale) {
    if (!r.top_left_x && !r.top_left_y && !r.top_right_x && !r.top_right_y && !r.bottom_right_x && !r.bottom_right_y &&
        !r.bottom_left_x && !r.bottom_left_y)
        return {{p.x, p.y}, {p.right(), p.y}, {p.right(), p.bottom()}, {p.x, p.bottom()}};
    const std::array<float, 4> xs = {std::max(0.0f, r.top_left_x), std::max(0.0f, r.top_right_x),
                                     std::max(0.0f, r.bottom_right_x), std::max(0.0f, r.bottom_left_x)};
    const std::array<float, 4> ys = {std::max(0.0f, r.top_left_y), std::max(0.0f, r.top_right_y),
                                     std::max(0.0f, r.bottom_right_y), std::max(0.0f, r.bottom_left_y)};
    float factor = 1;
    const auto fit = [&](float size, float sum) {
        if (sum > 0)
            factor = std::min(factor, std::max(0.0f, size) / sum);
    };
    fit(p.width, xs[0] + xs[1]);
    fit(p.width, xs[2] + xs[3]);
    fit(p.height, ys[0] + ys[3]);
    fit(p.height, ys[1] + ys[2]);
    std::vector<HtmlPoint> result;
    for (int i = 0; i < 4; ++i) {
        const float rx = xs[i] * factor, ry = ys[i] * factor;
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
static std::vector<HtmlPoint> HtmlClipPolygon(std::vector<HtmlPoint> polygon, const std::vector<HtmlPoint> &clip) {
    for (size_t i = 0; i < clip.size() && !polygon.empty(); ++i) {
        const HtmlPoint a = clip[i], b = clip[(i + 1) % clip.size()];
        const auto distance = [&](HtmlPoint p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); };
        std::vector<HtmlPoint> output;
        HtmlPoint previous = polygon.back();
        float before = distance(previous);
        for (const HtmlPoint current : polygon) {
            const float after = distance(current);
            if ((before >= 0) != (after >= 0)) {
                const float t = before / (before - after);
                output.push_back(
                    {previous.x + (current.x - previous.x) * t, previous.y + (current.y - previous.y) * t});
            }
            if (after >= 0)
                output.push_back(current);
            previous = current;
            before = after;
        }
        polygon = std::move(output);
    }
    return polygon;
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
    if (p.width > 0 && p.height > 0) {
        const bool rx =
            layer.repeat == litehtml::background_repeat_repeat || layer.repeat == litehtml::background_repeat_repeat_x;
        const bool ry =
            layer.repeat == litehtml::background_repeat_repeat || layer.repeat == litehtml::background_repeat_repeat_y;
        float left = p.x, top = p.y;
        if (rx)
            left += std::floor((layer.clip_box.x - left) / p.width) * p.width;
        if (ry)
            top += std::floor((layer.clip_box.y - top) / p.height) * p.height;
        for (float y = top; y < (ry ? layer.clip_box.bottom() : top + p.height); y += p.height)
            for (float x = left; x < (rx ? layer.clip_box.right() : left + p.width); x += p.width)
                m_Device.Image(
                    image, {m_Origin.x + x * m_Scale, m_Origin.y + y * m_Scale, p.width * m_Scale, p.height * m_Scale});
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
            Polygon({mix(outside[i], inside[i], begin), mix(outside[(i + 1) % 4], inside[(i + 1) % 4], begin),
                     mix(outside[(i + 1) % 4], inside[(i + 1) % 4], end), mix(outside[i], inside[i], end)},
                    color);
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
            const HtmlPoint a = mix(outside[i], inside[i], .5f),
                            z = mix(outside[(i + 1) % 4], inside[(i + 1) % 4], .5f);
            const float length = std::hypot(z.x - a.x, z.y - a.y),
                        dash = side.width * (side.style == litehtml::border_style_dashed ? 3 : 1);
            for (float start = 0; start < length; start += dash * 2) {
                const float begin = start / length, end = std::min(length, start + dash) / length;
                if (side.style == litehtml::border_style_dotted) {
                    const auto c = mix(a, z, (begin + end) / 2);
                    Marker({c.x - side.width / 2, c.y - side.width / 2, side.width, side.width}, false, false,
                           side.color);
                } else
                    Polygon({mix(outside[i], outside[(i + 1) % 4], begin), mix(outside[i], outside[(i + 1) % 4], end),
                             mix(inside[i], inside[(i + 1) % 4], end), mix(inside[i], inside[(i + 1) % 4], begin)},
                            side.color);
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
    float left = m_Clips.back()[0].x, top = m_Clips.back()[0].y, right = left, bottom = top;
    for (const auto &p : m_Clips.back()) {
        left = std::min(left, p.x);
        right = std::max(right, p.x);
        top = std::min(top, p.y);
        bottom = std::max(bottom, p.y);
    }
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
