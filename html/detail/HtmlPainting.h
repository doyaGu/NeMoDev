#pragma once

#include "html/HtmlDocument.h"
#include <functional>
#include <litehtml/background.h>

// The HTML engine's paint descriptions stop here. Neither these types nor
// engine geometry are exposed to the application or its graphics backend.
class HtmlPainting {
public:
    explicit HtmlPainting(HtmlRenderDevice &device) : m_Device(device) {}
    void Begin(HtmlPoint origin, float scale, const litehtml::position &visible);
    void End();
    void PushClip(const litehtml::position &rectangle, const litehtml::border_radiuses &radii = {});
    void PopClip();
    void Text(const HtmlFont &font, std::string_view text, const litehtml::position &rectangle,
              litehtml::web_color color);
    void Image(const HtmlImage &image, const litehtml::background_layer &layer);
    void Fill(const litehtml::background_layer &layer, litehtml::web_color color);
    void Borders(const litehtml::position &rectangle, const litehtml::borders &borders);
    void Marker(const litehtml::position &rectangle, bool hollow, bool square, litehtml::web_color color);
    void BrokenImage(const litehtml::position &rectangle, const HtmlFont *font, std::string_view alt,
                     litehtml::web_color color);
    void LinearGradient(const litehtml::background_layer &layer,
                        const litehtml::background_layer::linear_gradient &gradient);
    void RadialGradient(const litehtml::background_layer &layer,
                        const litehtml::background_layer::radial_gradient &gradient);
    void ConicGradient(const litehtml::background_layer &layer,
                       const litehtml::background_layer::conic_gradient &gradient);

private:
    HtmlRenderDevice &m_Device;
    HtmlPoint m_Origin;
    float m_Scale = 1;
    std::vector<std::vector<HtmlPoint>> m_Clips;
    std::vector<std::shared_ptr<HtmlImage>> m_Gradients;
    void ApplyClip();
    void Polygon(const std::vector<HtmlPoint> &points, litehtml::web_color color);
    void Gradient(const litehtml::background_layer &layer, const litehtml::background_layer::gradient_base &gradient,
                  const std::function<float(float, float)> &offset);
};
