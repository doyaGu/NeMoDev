#include "html/HtmlDocument.h"
#include "html/detail/HtmlPainting.h"
#include "html/detail/HtmlStyledElement.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <litehtml.h>
#include <litehtml/el_body.h>
#include <litehtml/el_div.h>
#include <litehtml/el_font.h>
#include <litehtml/el_image.h>
#include <litehtml/el_para.h>
#include <litehtml/el_table.h>
#include <litehtml/el_td.h>
#include <litehtml/el_tr.h>
#include <litehtml/render_item.h>
#include <litehtml/url.h>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

static bool HtmlContains(HtmlRect rect, HtmlPoint point) {
    return point.x >= rect.x && point.y >= rect.y && point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}
static std::string HtmlResolve(const std::string &base, const std::string &reference) {
    const litehtml::url relative(reference);
    if (relative.has_scheme() && !relative.has_authority() && !relative.path().empty() &&
        relative.path().front() != '/')
        return reference; // opaque schemes are navigation targets, never file paths
    const auto resolved = litehtml::resolve(litehtml::url(base), relative);
    // RFC 3986 5.2.4. litehtml deliberately leaves dot segments to its host.
    std::string input = resolved.path(), output;
    while (!input.empty()) {
        if (input.compare(0, 3, "../") == 0)
            input.erase(0, 3);
        else if (input.compare(0, 2, "./") == 0)
            input.erase(0, 2);
        else if (input.compare(0, 3, "/./") == 0 || input == "/.")
            input.replace(0, std::min(size_t(3), input.size()), "/");
        else if (input.compare(0, 4, "/../") == 0 || input == "/..") {
            input.replace(0, std::min(size_t(4), input.size()), "/");
            const auto slash = output.find_last_of('/');
            output.erase(slash == std::string::npos ? 0 : slash);
        } else if (input == "." || input == "..")
            input.clear();
        else {
            const auto end = input.find('/', input.front() == '/' ? 1 : 0);
            const size_t count = end == std::string::npos ? input.size() : end;
            output.append(input, 0, count);
            input.erase(0, count);
        }
    }
    const auto hasQuery = [](const std::string &url) {
        const auto question = url.find('?'), fragment = url.find('#');
        return question != std::string::npos && (fragment == std::string::npos || question < fragment);
    };
    const bool referenceQuery = hasQuery(reference);
    const bool queryPresent = referenceQuery || (!relative.has_scheme() && !relative.has_authority() &&
                                                 !relative.has_path() && hasQuery(base));
    std::string result = litehtml::url(resolved.scheme(), resolved.authority(), output,
                                       referenceQuery ? relative.query() : resolved.query(), resolved.fragment())
                             .str();
    if (queryPresent && (referenceQuery ? relative.query() : resolved.query()).empty()) {
        const auto fragment = result.find('#');
        result.insert(fragment == std::string::npos ? result.size() : fragment, 1, '?');
    }
    if (reference.find('#') != std::string::npos && relative.fragment().empty())
        result += '#';
    std::string escaped;
    const char hex[] = "0123456789ABCDEF";
    for (unsigned char byte : result) {
        if (byte <= 32 || byte >= 127) {
            escaped += '%';
            escaped += hex[byte >> 4];
            escaped += hex[byte & 15];
        } else
            escaped += char(byte);
    }
    return escaped;
}
static std::string HtmlFragment(const std::string &fragment) {
    const auto digit = [](unsigned char value) {
        if (value >= '0' && value <= '9')
            return int(value - '0');
        if (value >= 'a' && value <= 'f')
            return int(value - 'a' + 10);
        if (value >= 'A' && value <= 'F')
            return int(value - 'A' + 10);
        return -1;
    };
    std::string result;
    for (size_t i = 0; i < fragment.size(); ++i) {
        if (fragment[i] == '%' && i + 2 < fragment.size()) {
            const int high = digit(fragment[i + 1]), low = digit(fragment[i + 2]);
            if (high >= 0 && low >= 0) {
                result += char(high * 16 + low);
                i += 2;
                continue;
            }
        }
        result += fragment[i];
    }
    return result;
}
static std::atomic<std::uint64_t> HtmlDocumentSequence{1};

static bool HtmlGenerated(const litehtml::element::ptr &node) {
    return node->tag() == litehtml::__tag_before_ || node->tag() == litehtml::__tag_after_;
}

static void HtmlPlainText(const litehtml::element::ptr &node, std::string &text) {
    if (node->css().get_display() == litehtml::display_none)
        return;
    if (node->is_text())
        node->get_text(text);
    else if (node->tag() == litehtml::_img_)
        text += node->get_attr("alt", "");
    else if (node->is_break())
        text += '\n';
    else {
        for (const auto &child : node->children())
            HtmlPlainText(child, text);
        if (node->is_block_box() && !text.empty() && text.back() != '\n')
            text += '\n';
    }
}

struct HtmlReadingPosition::Data {
    std::shared_ptr<const std::string> source;
    std::string base;
    std::vector<size_t> path;
    HtmlPoint offset;
};
HtmlReadingPosition::HtmlReadingPosition() = default;
HtmlReadingPosition::~HtmlReadingPosition() = default;
HtmlReadingPosition::HtmlReadingPosition(const HtmlReadingPosition &) = default;
HtmlReadingPosition &HtmlReadingPosition::operator=(const HtmlReadingPosition &) = default;

struct HtmlDocument::Impl : litehtml::document_container {
    HtmlResources &resources;
    HtmlRenderDevice &device;
    HtmlPainting painting;
    std::string sourceBase, base, title, text;
    std::shared_ptr<const std::string> source;
    HtmlSize viewport;
    float scale = 1;
    std::vector<std::string> diagnostics;
    struct Resource {
        std::vector<unsigned char> bytes;
        std::string error;
        bool loaded = false;
    };
    std::map<std::string, std::shared_ptr<const Resource>> loadedResources;
    std::map<std::string, std::set<std::string>> imports;
    std::vector<std::string> visited;
    bool laidOut = false;
    std::uint64_t pressedLink = 0;
    std::map<litehtml::uint_ptr, std::shared_ptr<HtmlFont>> fonts;
    std::map<std::string, std::shared_ptr<HtmlImage>> images;
    std::vector<HtmlLink> links;
    std::map<const litehtml::element *, std::uint64_t> linkElements;
    std::map<std::pair<size_t, size_t>, std::uint64_t> linkIds;
    struct Area {
        const litehtml::element *image = nullptr;
        const litehtml::element *element = nullptr;
        HtmlRect picture, bounds;
        std::uint64_t link = 0;
        enum Shape { Rectangle, Circle, Polygon, Default } shape = Rectangle;
        std::vector<float> coordinates;
        bool Contains(HtmlPoint point, float scale) const {
            if (!HtmlContains(picture, point) || !HtmlContains(bounds, point))
                return false;
            const float x = (point.x - picture.x) / scale, y = (point.y - picture.y) / scale;
            if (shape == Circle) {
                const float dx = x - coordinates[0], dy = y - coordinates[1];
                return dx * dx + dy * dy <= coordinates[2] * coordinates[2];
            }
            if (shape != Polygon)
                return true;
            bool inside = false;
            for (size_t i = 0, j = coordinates.size() - 2; i < coordinates.size(); j = i, i += 2) {
                const float ax = coordinates[j], ay = coordinates[j + 1], bx = coordinates[i], by = coordinates[i + 1];
                const float cross = (x - ax) * (by - ay) - (y - ay) * (bx - ax);
                if (std::fabs(cross) < .0001f && x >= std::min(ax, bx) && x <= std::max(ax, bx) &&
                    y >= std::min(ay, by) && y <= std::max(ay, by))
                    return true;
                if ((ay > y) != (by > y) && x < (bx - ax) * (y - ay) / (by - ay) + ax)
                    inside = !inside;
            }
            return inside;
        }
    };
    std::vector<Area> areas;
    litehtml::document::ptr document; // destroyed before its container resources

    struct CellElement : HtmlStyledElement<litehtml::el_td> {
        using HtmlStyledElement::HtmlStyledElement;
        void parse_attributes() override {
            for (auto node = parent(); node; node = node->parent()) {
                if (node->tag() == litehtml::_table_) {
                    if (const char *padding = node->get_attr("cellpadding"))
                        map_to_pixel_length_property(litehtml::_padding_, padding);
                    break;
                }
            }
            HtmlStyledElement::parse_attributes();
        }
        void compute_styles(bool recursive = true) override {
            HtmlStyledElement::compute_styles(recursive);
            // CSS 2.1 table height: the cell's specified height is a minimum.
            // The engine intentionally gives cells an auto containing height;
            // use its minimum-height path after the author cascade is resolved.
            const auto &height = css().get_height();
            const auto &minimum = css().get_min_height();
            if (!height.is_predefined() && height.units() != litehtml::css_units_percentage &&
                (minimum.is_predefined() || minimum.units() != litehtml::css_units_percentage)) {
                const auto doc = get_document();
                const auto metrics = css().get_font_metrics();
                const float value = doc->to_pixels(height, metrics, 0);
                const float prior = minimum.is_predefined() ? 0 : doc->to_pixels(minimum, metrics, 0);
                if (value > prior)
                    css_w().set_min_height(litehtml::css_length(value));
            }
        }
    };

    struct ImageElement : HtmlStyledElement<litehtml::el_image> {
        Impl &owner;
        ImageElement(const litehtml::document::ptr &doc, Impl &container) : HtmlStyledElement(doc), owner(container) {}
        void compute_styles(bool recursive = true) override {
            HtmlStyledElement::compute_styles(recursive);
            if (get_document()->mode() == litehtml::no_quirks_mode ||
                css().get_vertical_align() != litehtml::va_baseline)
                return;
            auto cell = parent();
            while (cell && cell->tag() != litehtml::_td_ && cell->tag() != litehtml::_th_)
                cell = cell->parent();
            if (!cell)
                return;
            bool text = false;
            const auto inspect = [&](const auto &self, const litehtml::element::ptr &node) -> void {
                if (node->tag() == litehtml::_img_ || node->tag() == litehtml::_map_ ||
                    node->tag() == litehtml::_script_)
                    return;
                if (node->is_text()) {
                    std::string value;
                    node->get_text(value);
                    if (value.find_first_not_of(" \t\r\n\f") != std::string::npos)
                        text = true;
                }
                for (const auto &child : node->children())
                    self(self, child);
            };
            inspect(inspect, cell);
            // HTML's table-cell line-height quirk removes baseline descent from
            // image-only cells. Retain author alignment and normal text cells.
            if (!text)
                css_w().set_vertical_align(litehtml::va_text_bottom);
        }
        void get_content_size(litehtml::size &size, float maximumWidth) override {
            if (owner.Image(get_attr("src", ""), nullptr)) {
                litehtml::el_image::get_content_size(size, maximumWidth);
                return;
            }
            const char *alt = get_attr("alt");
            if (alt && !*alt) {
                size = {0, 0};
                return;
            }
            const auto font = owner.fonts.find(css().get_font());
            const float width = font != owner.fonts.end() && alt ? font->second->Width(alt) / owner.scale : 0;
            size = {16 + width, std::max(16.0f, css().get_font_metrics().height)};
        }
        void draw(litehtml::uint_ptr dc, float x, float y, const litehtml::position *clip,
                  const std::shared_ptr<litehtml::render_item> &render) override {
            if (owner.Image(get_attr("src", ""), nullptr)) {
                litehtml::el_image::draw(dc, x, y, clip, render);
                return;
            }
            litehtml::html_tag::draw(dc, x, y, clip, render);
            auto p = render->pos();
            p.x += x;
            p.y += y;
            if (p.width <= 0 || p.height <= 0 || !p.does_intersect(clip))
                return;
            const auto font = owner.fonts.find(css().get_font());
            owner.painting.BrokenImage(p, font == owner.fonts.end() ? nullptr : font->second.get(), get_attr("alt", ""),
                                       css().get_color());
        }
    };

    Impl(HtmlResources &r, HtmlRenderDevice &d) : resources(r), device(d), painting(d) {}
    void Issue(const std::string &message) {
        if (std::find(diagnostics.begin(), diagnostics.end(), message) == diagnostics.end())
            diagnostics.push_back(message);
    }
    const Resource &Read(const std::string &url) {
        const auto found = loadedResources.find(url);
        if (found != loadedResources.end())
            return *found->second;
        auto result = std::make_shared<Resource>();
        result->loaded = resources.Read(url, result->bytes, result->error);
        loadedResources.emplace(url, result);
        return *result;
    }
    bool Imports(const std::string &from, const std::string &to, std::set<std::string> &checked) const {
        if (from == to)
            return true;
        if (!checked.insert(from).second)
            return false;
        const auto found = imports.find(from);
        if (found != imports.end())
            for (const auto &child : found->second)
                if (Imports(child, to, checked))
                    return true;
        return false;
    }
    void ApplyVisited() {
        const std::set<std::string> known(visited.begin(), visited.end());
        for (const auto &node : document->root()->select_all("a[href]")) {
            const bool found = known.count(HtmlResolve(base, node->get_attr("href", ""))) != 0;
            node->set_pseudo_class(litehtml::_id("visited"), found);
            node->set_pseudo_class(litehtml::_link_, !found);
        }
        document->root()->refresh_styles();
        document->root()->compute_styles();
    }
    void Render() {
        document->render(viewport.width / scale);
        std::string readable;
        HtmlPlainText(document->root(), readable);
        CollectLinks();
        text = std::move(readable);
        laidOut = true;
    }
    template <class Change> bool UpdateInteraction(Change change) {
        if (!laidOut)
            return false;
        try {
            const bool changed = change();
            if (changed)
                Render();
            return changed;
        } catch (...) {
            // Style calculation may have changed native nodes before failing.
            // Geometry becomes usable only after a complete Layout rebuild.
            laidOut = false;
            pressedLink = 0;
            links.clear();
            linkElements.clear();
            areas.clear();
            text.clear();
            throw;
        }
    }
    HtmlRect Rect(const litehtml::position &p) const {
        return {p.x * scale, p.y * scale, p.width * scale, p.height * scale};
    }
    std::shared_ptr<HtmlImage> Image(const char *name, const char *relativeBase) {
        const std::string url =
            HtmlResolve(relativeBase && *relativeBase ? HtmlResolve(base, relativeBase) : base, name ? name : "");
        const auto found = images.find(url);
        if (found != images.end())
            return found->second;
        std::shared_ptr<HtmlImage> image;
        const auto &resource = Read(url);
        if (resource.loaded)
            image = device.DecodeImage(url, resource.bytes);
        if (!image)
            Issue("Image unavailable: " + url + (resource.error.empty() ? "" : " (" + resource.error + ")"));
        images.emplace(url, image);
        return image;
    }
    litehtml::uint_ptr create_font(const litehtml::font_description &description, const litehtml::document *,
                                   litehtml::font_metrics *metrics) override {
        HtmlFontDescription request{description.family, description.size * scale, description.weight,
                                    description.style == litehtml::font_style_italic};
        if (description.decoration_line & litehtml::text_decoration_line_underline)
            request.decoration |= HtmlDecorationUnderline;
        if (description.decoration_line & litehtml::text_decoration_line_overline)
            request.decoration |= HtmlDecorationOverline;
        if (description.decoration_line & litehtml::text_decoration_line_line_through)
            request.decoration |= HtmlDecorationLineThrough;
        auto font = device.CreateFont(request);
        if (!font)
            throw std::runtime_error("Font could not be created: " + description.family);
        const HtmlFontMetrics measured = font->Metrics();
        metrics->font_size = description.size;
        metrics->ascent = measured.ascent / scale;
        metrics->descent = measured.descent / scale;
        metrics->height = measured.height / scale;
        metrics->x_height = measured.xHeight / scale;
        metrics->ch_width = font->Width("0") / scale;
        metrics->sub_shift = description.size * .2f;
        metrics->super_shift = description.size * .35f;
        const auto handle = reinterpret_cast<litehtml::uint_ptr>(font.get());
        fonts.emplace(handle, std::move(font));
        return handle;
    }
    void delete_font(litehtml::uint_ptr font) override { fonts.erase(font); }
    float text_width(const char *value, litehtml::uint_ptr font) override {
        return fonts.at(font)->Width(value) / scale;
    }
    void draw_text(litehtml::uint_ptr, const char *value, litehtml::uint_ptr font, litehtml::web_color color,
                   const litehtml::position &p) override {
        painting.Text(*fonts.at(font), value, p, color);
    }
    float pt_to_px(float pt) const override { return pt * 96.0f / 72.0f; }
    float get_default_font_size() const override { return 16; }
    const char *get_default_font_name() const override { return "sans-serif"; }
    void load_image(const char *name, const char *relativeBase, bool) override { Image(name, relativeBase); }
    void get_image_size(const char *name, const char *relativeBase, litehtml::size &size) override {
        if (auto image = Image(name, relativeBase)) {
            const auto s = image->Size();
            size = {s.width, s.height};
        } else
            size = {16, 16};
    }
    void draw_image(litehtml::uint_ptr, const litehtml::background_layer &layer, const std::string &name,
                    const std::string &relativeBase) override {
        if (auto image = Image(name.c_str(), relativeBase.c_str()))
            painting.Image(*image, layer);
    }
    void draw_solid_fill(litehtml::uint_ptr, const litehtml::background_layer &layer,
                         const litehtml::web_color &color) override {
        painting.Fill(layer, color);
    }
    void draw_linear_gradient(litehtml::uint_ptr, const litehtml::background_layer &layer,
                              const litehtml::background_layer::linear_gradient &gradient) override {
        painting.LinearGradient(layer, gradient);
    }
    void draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer &layer,
                              const litehtml::background_layer::radial_gradient &gradient) override {
        painting.RadialGradient(layer, gradient);
    }
    void draw_conic_gradient(litehtml::uint_ptr, const litehtml::background_layer &layer,
                             const litehtml::background_layer::conic_gradient &gradient) override {
        painting.ConicGradient(layer, gradient);
    }
    void draw_borders(litehtml::uint_ptr, const litehtml::borders &b, const litehtml::position &p, bool) override {
        painting.Borders(p, b);
    }
    void draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker &marker) override {
        if (!marker.image.empty()) {
            if (auto image = Image(marker.image.c_str(), marker.baseurl)) {
                litehtml::background_layer layer;
                layer.origin_box = layer.border_box = layer.clip_box = marker.pos;
                layer.repeat = litehtml::background_repeat_no_repeat;
                painting.Image(*image, layer);
            }
            return;
        }
        painting.Marker(marker.pos, marker.marker_type == litehtml::list_style_type_circle,
                        marker.marker_type == litehtml::list_style_type_square, marker.color);
    }
    void set_caption(const char *value) override { title = value ? value : ""; }
    void set_base_url(const char *value) override {
        if (value && *value)
            base = HtmlResolve(base, value);
    }
    void link(const litehtml::document::ptr &doc, const litehtml::element::ptr &element) override {
        // rel is a set of ASCII case-insensitive tokens. The engine handles the
        // single lowercase token itself and delegates other forms to its host.
        std::istringstream tokens(element->get_attr("rel", ""));
        std::set<std::string> relations;
        std::string token;
        while (tokens >> token) {
            for (char &c : token)
                if (c >= 'A' && c <= 'Z')
                    c += 'a' - 'A';
            relations.insert(token);
        }
        if (!relations.count("stylesheet") || relations.count("alternate"))
            return;
        const char *href = element->get_attr("href", "");
        if (!*href)
            return;
        std::string css, relativeBase;
        import_css(css, href, relativeBase);
        if (!css.empty())
            doc->add_stylesheet(css.c_str(), relativeBase.c_str(), element->get_attr("media"));
    }
    void on_anchor_click(const char *, const litehtml::element::ptr &) override {}
    void on_mouse_event(const litehtml::element::ptr &, litehtml::mouse_event) override {}
    void set_cursor(const char *) override {} // HitTest reads the actual element's computed cursor.
    void transform_text(std::string &value, litehtml::text_transform transform) override {
        bool start = true;
        for (char &c : value) {
            const auto byte = static_cast<unsigned char>(c);
            if (byte < 128) {
                if (transform == litehtml::text_transform_uppercase ||
                    (transform == litehtml::text_transform_capitalize && start))
                    c = char(std::toupper(byte));
                else if (transform == litehtml::text_transform_lowercase)
                    c = char(std::tolower(byte));
                start = std::isspace(byte) != 0;
            } else
                start = false;
        }
    }
    void import_css(std::string &css, const std::string &url, std::string &relativeBase) override {
        const auto parent = relativeBase.empty() ? base : HtmlResolve(base, relativeBase);
        const auto resolved = HtmlResolve(parent, url);
        std::set<std::string> checked;
        if (Imports(resolved, parent, checked)) {
            css.clear();
            Issue("Cyclic stylesheet import ignored: " + resolved);
            return;
        }
        imports[parent].insert(resolved);
        const auto &resource = Read(resolved);
        if (resource.loaded)
            css.assign(resource.bytes.begin(), resource.bytes.end());
        else {
            css.clear();
            Issue("Stylesheet unavailable: " + resolved + (resource.error.empty() ? "" : " (" + resource.error + ")"));
        }
        relativeBase = resolved;
    }
    void set_clip(const litehtml::position &p, const litehtml::border_radiuses &r) override { painting.PushClip(p, r); }
    void del_clip() override { painting.PopClip(); }
    void get_viewport(litehtml::position &p) const override {
        p = {0, 0, viewport.width / scale, viewport.height / scale};
    }
    litehtml::element::ptr create_element(const char *name, const litehtml::string_map &,
                                          const litehtml::document::ptr &doc) override {
        if (std::string_view(name) == "img")
            return std::make_shared<ImageElement>(doc, *this);
        const std::string_view tag(name);
        if (tag == "body")
            return std::make_shared<HtmlStyledElement<litehtml::el_body>>(doc);
        if (tag == "div")
            return std::make_shared<HtmlStyledElement<litehtml::el_div>>(doc);
        if (tag == "p")
            return std::make_shared<HtmlStyledElement<litehtml::el_para>>(doc);
        if (tag == "font")
            return std::make_shared<HtmlStyledElement<litehtml::el_font>>(doc);
        if (tag == "table")
            return std::make_shared<HtmlStyledElement<litehtml::el_table>>(doc);
        if (tag == "td" || tag == "th")
            return std::make_shared<CellElement>(doc);
        if (tag == "tr")
            return std::make_shared<HtmlStyledElement<litehtml::el_tr>>(doc);
        return {};
    }
    void get_media_features(litehtml::media_features &media) const override {
        media.type = litehtml::media_type_screen;
        media.width = viewport.width / scale;
        media.height = viewport.height / scale;
        media.device_width = media.width;
        media.device_height = media.height;
        media.color = 8;
        media.resolution = 96;
    }
    void get_language(std::string &language, std::string &culture) const override {
        language.clear();
        culture.clear();
    }

    std::uint64_t AddLink(const litehtml::element::ptr &element, std::vector<HtmlRect> bounds,
                          std::pair<size_t, size_t> key) {
        const char *href = element->get_attr("href");
        if (!href || bounds.empty())
            return 0;
        auto id = linkIds.find(key);
        if (id == linkIds.end())
            id = linkIds.emplace(key, HtmlDocumentSequence.fetch_add(1)).first;
        HtmlLink link;
        link.id = id->second;
        link.navigation.url = HtmlResolve(base, href);
        link.bounds = std::move(bounds);
        link.navigation.target = element->get_attr("target", "");
        if (const char *download = element->get_attr("download")) {
            link.navigation.download = true;
            link.navigation.downloadName = download;
        }
        link.title = element->get_attr("title", "");
        link.label = element->get_attr("alt", "");
        if (link.label.empty())
            element->get_text(link.label);
        if (link.label.empty())
            link.label = link.navigation.url;
        link.tabIndex = std::atoi(element->get_attr("tabindex", "0"));
        links.push_back(std::move(link));
        return id->second;
    }
    void CollectLinks() {
        links.clear();
        linkElements.clear();
        areas.clear();
        if (!document)
            return;
        std::map<const litehtml::element *, size_t> order;
        size_t ordinal = 0;
        const auto assign = [&](const auto &self, const litehtml::element::ptr &node) -> void {
            if (HtmlGenerated(node))
                return;
            if (node->tag() == litehtml::_a_ || node->tag() == litehtml::_area_ || node->tag() == litehtml::_img_)
                order[node.get()] = ordinal++;
            for (const auto &child : node->children())
                self(self, child);
        };
        assign(assign, document->root());
        for (const auto &element : document->root()->select_all("a[href]")) {
            litehtml::position::vector boxes;
            element->run_on_renderers([&](const auto &render) {
                if (render->is_visible()) {
                    litehtml::position::vector fragment;
                    render->get_rendering_boxes(fragment);
                    boxes.insert(boxes.end(), fragment.begin(), fragment.end());
                }
                return true;
            });
            std::vector<HtmlRect> bounds;
            for (const auto &box : boxes)
                if (box.width > 0 && box.height > 0)
                    bounds.push_back(Rect(box));
            const auto id = AddLink(element, std::move(bounds), {order[element.get()], 0});
            if (id)
                linkElements[element.get()] = id;
        }
        for (const auto &image : document->root()->select_all("img[usemap]")) {
            const std::string reference = image->get_attr("usemap", "");
            if (reference.empty() || reference[0] != '#')
                continue;
            litehtml::element::ptr map;
            for (const auto &candidate : document->root()->select_all("map"))
                if (reference.substr(1) == candidate->get_attr("name", "")) {
                    map = candidate;
                    break;
                }
            if (!map)
                continue;
            const HtmlRect picture = Rect(image->get_placement());
            if (picture.width <= 0 || picture.height <= 0)
                continue;
            for (const auto &element : map->select_all("area")) {
                Area area;
                area.image = image.get();
                area.element = element.get();
                area.picture = picture;
                std::string shape = element->get_attr("shape", "rect");
                for (char &ch : shape)
                    ch = char(std::tolower(static_cast<unsigned char>(ch)));
                if (shape == "circle" || shape == "circ")
                    area.shape = Area::Circle;
                else if (shape == "poly" || shape == "polygon")
                    area.shape = Area::Polygon;
                else if (shape == "default")
                    area.shape = Area::Default;
                const char *input = element->get_attr("coords", "");
                while (*input) {
                    while (*input && (*input == ',' || std::isspace(static_cast<unsigned char>(*input))))
                        ++input;
                    if (!*input)
                        break;
                    char *end = nullptr;
                    const float value = std::strtof(input, &end);
                    if (input == end || !std::isfinite(value))
                        break;
                    area.coordinates.push_back(value);
                    input = end;
                }
                auto &c = area.coordinates;
                float left = 0, top = 0, right = picture.width / scale, bottom = picture.height / scale;
                if (area.shape == Area::Rectangle) {
                    if (c.size() < 4)
                        continue;
                    c.resize(4);
                    left = std::min(c[0], c[2]);
                    right = std::max(c[0], c[2]);
                    top = std::min(c[1], c[3]);
                    bottom = std::max(c[1], c[3]);
                } else if (area.shape == Area::Circle) {
                    if (c.size() < 3 || c[2] <= 0)
                        continue;
                    c.resize(3);
                    left = c[0] - c[2];
                    right = c[0] + c[2];
                    top = c[1] - c[2];
                    bottom = c[1] + c[2];
                } else if (area.shape == Area::Polygon) {
                    if (c.size() < 6)
                        continue;
                    c.resize(c.size() / 2 * 2);
                    left = right = c[0];
                    top = bottom = c[1];
                    for (size_t i = 2; i < c.size(); i += 2) {
                        left = std::min(left, c[i]);
                        right = std::max(right, c[i]);
                        top = std::min(top, c[i + 1]);
                        bottom = std::max(bottom, c[i + 1]);
                    }
                }
                left = std::max(0.0f, left);
                top = std::max(0.0f, top);
                right = std::min(picture.width / scale, right);
                bottom = std::min(picture.height / scale, bottom);
                if (right <= left || bottom <= top)
                    continue;
                area.bounds = {picture.x + left * scale, picture.y + top * scale, (right - left) * scale,
                               (bottom - top) * scale};
                if (!element->get_attr("nohref"))
                    area.link = AddLink(element, {area.bounds}, {order[element.get()], order[image.get()] + 1});
                areas.push_back(std::move(area));
            }
        }
        std::stable_sort(links.begin(), links.end(), [&](const HtmlLink &a, const HtmlLink &b) {
            const int ai = a.tabIndex > 0 ? a.tabIndex : std::numeric_limits<int>::max();
            const int bi = b.tabIndex > 0 ? b.tabIndex : std::numeric_limits<int>::max();
            if (ai != bi)
                return ai < bi;
            const auto key = [&](std::uint64_t id) {
                for (const auto &entry : linkIds)
                    if (entry.second == id)
                        return entry.first;
                return std::pair<size_t, size_t>{};
            };
            return key(a.id) < key(b.id);
        });
    }
};

HtmlDocument::HtmlDocument(HtmlResources &resources, HtmlRenderDevice &device) : m_Impl(new Impl(resources, device)) {}
HtmlDocument::~HtmlDocument() = default;
bool HtmlDocument::Load(const std::string &html, const std::string &baseUrl, std::string &error) {
    try {
        auto next = std::make_unique<Impl>(m_Impl->resources, m_Impl->device);
        next->sourceBase = next->base = baseUrl;
        next->viewport = m_Impl->viewport;
        next->scale = m_Impl->scale;
        next->source = std::make_shared<const std::string>(html);
        next->document =
            litehtml::document::createFromString(litehtml::estring(html, litehtml::encoding::utf_8), next.get());
        if (!next->document) {
            error = "HTML could not be parsed";
            return false;
        }
        HtmlPlainText(next->document->root(), next->text);
        m_Impl = std::move(next);
        error.clear();
        return true;
    } catch (const std::exception &failure) {
        error = failure.what();
        return false;
    }
}
bool HtmlDocument::Layout(HtmlSize viewport, float scale, std::string &error) {
    if (!m_Impl->document) {
        error = "No HTML document is loaded";
        return false;
    }
    if (!std::isfinite(viewport.width) || !std::isfinite(viewport.height) || !std::isfinite(scale) ||
        viewport.width <= 0 || viewport.height <= 0 || scale <= 0) {
        error = "Invalid HTML viewport or scale";
        return false;
    }
    if (m_Impl->laidOut && m_Impl->viewport.width == viewport.width && m_Impl->viewport.height == viewport.height &&
        m_Impl->scale == scale) {
        error.clear();
        return true;
    }
    try {
        auto next = std::make_unique<Impl>(m_Impl->resources, m_Impl->device);
        next->sourceBase = next->base = m_Impl->sourceBase;
        next->source = m_Impl->source;
        next->viewport = viewport;
        next->scale = scale;
        next->linkIds = m_Impl->linkIds;
        next->loadedResources = m_Impl->loadedResources;
        next->images = m_Impl->images;
        next->imports = m_Impl->imports;
        next->visited = m_Impl->visited;
        next->diagnostics = m_Impl->diagnostics;
        next->document = litehtml::document::createFromString(
            litehtml::estring(*next->source, litehtml::encoding::utf_8), next.get());
        next->ApplyVisited();
        next->Render();
        m_Impl = std::move(next);
        error.clear();
        return true;
    } catch (const std::exception &failure) {
        error = failure.what();
        return false;
    }
}
void HtmlDocument::Draw(HtmlPoint origin, HtmlRect visible) {
    if (!m_Impl->laidOut)
        throw std::logic_error("HTML drawing requires a successful layout");
    litehtml::position clip{visible.x / m_Impl->scale, visible.y / m_Impl->scale, visible.width / m_Impl->scale,
                            visible.height / m_Impl->scale};
    m_Impl->painting.Begin(origin, m_Impl->scale, clip);
    try {
        m_Impl->document->draw(0, 0, 0, &clip);
    } catch (...) {
        m_Impl->painting.End();
        throw;
    }
    m_Impl->painting.End();
}
HtmlSize HtmlDocument::ContentSize() const {
    return m_Impl->laidOut
               ? HtmlSize{m_Impl->document->width() * m_Impl->scale, m_Impl->document->height() * m_Impl->scale}
               : HtmlSize{};
}
const std::string &HtmlDocument::Title() const { return m_Impl->title; }
const std::string &HtmlDocument::Text() const { return m_Impl->text; }
const std::vector<std::string> &HtmlDocument::Diagnostics() const { return m_Impl->diagnostics; }
const std::vector<HtmlLink> &HtmlDocument::Links() const { return m_Impl->links; }
HtmlHit HtmlDocument::HitTest(HtmlPoint point) const {
    if (!m_Impl->laidOut)
        return {};
    const float x = point.x / m_Impl->scale, y = point.y / m_Impl->scale;
    auto element = m_Impl->document->root_render()->get_element_by_point(x, y, x, y, [](const auto &) { return true; });
    HtmlHit hit;
    if (element)
        hit.cursor = element->css().get_cursor();
    for (const auto &area : m_Impl->areas)
        if (area.image == element.get() && area.Contains(point, m_Impl->scale)) {
            hit.title = area.element->get_attr("title", "");
            const auto &cursor = area.element->css().get_cursor();
            if (cursor != "auto")
                hit.cursor = cursor;
            for (const auto &link : m_Impl->links)
                if (link.id == area.link) {
                    if (hit.cursor == "auto")
                        hit.cursor = "pointer";
                    return {link.id, link.navigation, hit.title, hit.cursor};
                }
            return hit; // An unlinked area masks linked shapes underneath it.
        }
    for (auto node = element; node; node = node->parent()) {
        if (hit.title.empty())
            hit.title = node->get_attr("title", "");
        const auto found = m_Impl->linkElements.find(node.get());
        if (found != m_Impl->linkElements.end()) {
            for (const auto &link : m_Impl->links)
                if (link.id == found->second) {
                    hit.link = link.id;
                    hit.navigation = link.navigation;
                    return hit;
                }
        }
    }
    return hit;
}
bool HtmlDocument::FindAnchor(const std::string &name, HtmlPoint &point) const {
    if (!m_Impl->laidOut)
        return false;
    const std::string decoded = HtmlFragment(name);
    for (const char *selector : {"[id]", "a[name]"}) {
        for (const auto &node : m_Impl->document->root()->select_all(selector)) {
            const char *value = node->get_attr(selector[0] == '[' ? "id" : "name", "");
            if (decoded == value) {
                const auto p = node->get_placement();
                point = {p.x * m_Impl->scale, p.y * m_Impl->scale};
                return true;
            }
        }
    }
    std::string special = decoded;
    for (char &byte : special)
        byte = char(std::tolower(static_cast<unsigned char>(byte)));
    if (decoded.empty() || special == "top") {
        point = {};
        return true;
    }
    return false;
}
bool HtmlDocument::Hover(HtmlPoint point) {
    if (!m_Impl->laidOut)
        return false;
    return m_Impl->UpdateInteraction([&] {
        litehtml::position::vector changed;
        const float x = point.x / m_Impl->scale, y = point.y / m_Impl->scale;
        return m_Impl->document->on_mouse_over(x, y, x, y, changed);
    });
}
void HtmlDocument::Leave() {
    m_Impl->UpdateInteraction([&] {
        litehtml::position::vector changed;
        return m_Impl->document->on_mouse_leave(changed);
    });
}
void HtmlDocument::PointerDown(HtmlPoint point) {
    if (!m_Impl->laidOut)
        return;
    Hover(point);
    m_Impl->pressedLink = HitTest(point).link;
    m_Impl->UpdateInteraction([&] {
        litehtml::position::vector changed;
        const float x = point.x / m_Impl->scale, y = point.y / m_Impl->scale;
        return m_Impl->document->on_lbutton_down(x, y, x, y, changed);
    });
}
bool HtmlDocument::PointerUp(HtmlPoint point, HtmlNavigation &navigation) {
    navigation = {};
    if (!m_Impl->laidOut)
        return false;
    Hover(point);
    const auto released = HitTest(point).link, pressed = m_Impl->pressedLink;
    m_Impl->pressedLink = 0;
    m_Impl->UpdateInteraction([&] {
        litehtml::position::vector changed;
        const float x = point.x / m_Impl->scale, y = point.y / m_Impl->scale;
        return m_Impl->document->on_lbutton_up(x, y, x, y, changed);
    });
    return pressed && pressed == released && Activate(pressed, navigation);
}
void HtmlDocument::CancelPointer() {
    m_Impl->pressedLink = 0;
    m_Impl->UpdateInteraction([&] {
        litehtml::position::vector changed;
        return m_Impl->document->on_button_cancel(changed);
    });
}
bool HtmlDocument::Activate(std::uint64_t id, HtmlNavigation &navigation) const {
    for (const auto &link : m_Impl->links)
        if (link.id == id) {
            navigation = link.navigation;
            return true;
        }
    navigation = {};
    return false;
}
void HtmlDocument::SetVisited(const std::vector<std::string> &urls) {
    if (m_Impl->visited == urls)
        return;
    m_Impl->visited = urls;
    m_Impl->UpdateInteraction([&] {
        m_Impl->ApplyVisited();
        return true;
    });
}
HtmlReadingPosition HtmlDocument::CapturePosition(HtmlPoint point) const {
    HtmlReadingPosition result;
    if (!m_Impl->laidOut)
        return result;
    auto data = std::make_shared<HtmlReadingPosition::Data>();
    data->source = m_Impl->source;
    data->base = m_Impl->base;
    std::vector<size_t> path;
    float nearest = std::numeric_limits<float>::max();
    const auto visit = [&](const auto &self, const litehtml::element::ptr &node) -> void {
        if (node->css().get_display() == litehtml::display_none)
            return;
        if (node->children().empty() && node->css().get_visibility() == litehtml::visibility_visible) {
            const auto r = m_Impl->Rect(node->get_placement());
            if (r.width > 0 && r.height > 0) {
                const float dy = point.y < r.y               ? r.y - point.y
                                 : point.y >= r.y + r.height ? point.y - r.y - r.height
                                                             : 0;
                const float dx = point.x < r.x ? r.x - point.x : point.x >= r.x + r.width ? point.x - r.x - r.width : 0;
                const float distance = dy * 100000 + dx;
                if (distance < nearest) {
                    nearest = distance;
                    data->path = path;
                    data->offset = {(point.x - r.x) / m_Impl->scale, (point.y - r.y) / m_Impl->scale};
                }
            }
        }
        size_t i = 0;
        for (const auto &child : node->children()) {
            if (HtmlGenerated(child))
                continue;
            path.push_back(i++);
            self(self, child);
            path.pop_back();
        }
    };
    visit(visit, m_Impl->document->root());
    if (nearest < std::numeric_limits<float>::max())
        result.m_Data = std::move(data);
    return result;
}
bool HtmlDocument::RestorePosition(const HtmlReadingPosition &position, HtmlPoint &point) const {
    if (!position.m_Data || !m_Impl->laidOut || position.m_Data->base != m_Impl->base ||
        *position.m_Data->source != *m_Impl->source)
        return false;
    auto node = m_Impl->document->root();
    for (size_t index : position.m_Data->path) {
        litehtml::element::ptr found;
        size_t i = 0;
        for (const auto &child : node->children()) {
            if (!HtmlGenerated(child) && i++ == index) {
                found = child;
                break;
            }
        }
        if (!found)
            return false;
        node = std::move(found);
    }
    const auto r = node->get_placement();
    if (r.width <= 0 || r.height <= 0 || node->css().get_visibility() != litehtml::visibility_visible)
        return false;
    point = {(r.x + position.m_Data->offset.x) * m_Impl->scale, (r.y + position.m_Data->offset.y) * m_Impl->scale};
    return true;
}
