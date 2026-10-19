#include "html/HtmlDocument.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

static void Check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
class TestHtmlResources : public HtmlResources {
public:
    std::map<std::string, std::string> files;
    std::vector<std::string> requests;
    bool Read(const std::string &url, std::vector<unsigned char> &bytes, std::string &error) override {
        requests.push_back(url);
        const auto file = files.find(url);
        if (file == files.end()) {
            error = "Not found";
            return false;
        }
        bytes.assign(file->second.begin(), file->second.end());
        return true;
    }
};
class TestHtmlFont : public HtmlFont {
public:
    HtmlFontDescription style;
    explicit TestHtmlFont(HtmlFontDescription description) : style(std::move(description)) {}
    HtmlFontMetrics Metrics() const override {
        return {style.size * .8f, style.size * .2f, style.size, style.size * .5f};
    }
    float Width(std::string_view text) const override {
        return text.size() * style.size * (style.weight >= 600 ? .6f : .5f);
    }
};
class TestHtmlImage : public HtmlImage {
public:
    HtmlSize dimensions{100, 80};
    std::vector<unsigned char> rgba;
    HtmlSize Size() const override { return dimensions; }
};
class TestHtmlDevice : public HtmlRenderDevice {
public:
    struct TextDraw {
        std::string text;
        HtmlPoint point;
        HtmlColor color;
        HtmlFontDescription font;
    };
    std::vector<TextDraw> text;
    std::vector<HtmlRect> images;
    std::vector<HtmlPoint> clip;
    std::vector<std::shared_ptr<TestHtmlImage>> generated;
    bool failFont = false;
    std::shared_ptr<HtmlFont> CreateFont(const HtmlFontDescription &description) override {
        return failFont ? nullptr : std::make_shared<TestHtmlFont>(description);
    }
    std::shared_ptr<HtmlImage> DecodeImage(const std::string &, const std::vector<unsigned char> &bytes) override {
        return bytes.empty() ? nullptr : std::make_shared<TestHtmlImage>();
    }
    std::shared_ptr<HtmlImage> CreateImage(unsigned width, unsigned height, const unsigned char *rgba) override {
        auto image = std::make_shared<TestHtmlImage>();
        image->dimensions = {float(width), float(height)};
        image->rgba.assign(rgba, rgba + size_t(width) * height * 4);
        generated.push_back(image);
        return image;
    }
    void SetClip(const std::vector<HtmlPoint> &value) override { clip = value; }
    void Text(const HtmlFont &font, std::string_view value, HtmlPoint point, HtmlColor color) override {
        text.push_back({std::string(value), point, color, static_cast<const TestHtmlFont &>(font).style});
    }
    void Image(const HtmlImage &, HtmlRect rect) override { images.push_back(rect); }
    void Fill(const std::vector<HtmlPoint> &, HtmlColor) override {}
};

static void Load(HtmlDocument &document, const std::string &html, float width = 400, float scale = 1) {
    std::string error;
    Check(document.Load(html, "book://manual/topics/page.html", error), error.c_str());
    Check(document.Layout({width, 300}, scale, error), error.c_str());
}
static void CascadeAndResources() {
    TestHtmlResources resources;
    resources.files["book://manual/styles/main.css"] =
        "p {color:blue} .mark {color:green} #chosen {color:red} p {color:blue}";
    resources.files["book://manual/images/picture.png"] = "image";
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><link rel=stylesheet href='../styles/main.css'><title>Example</title>"
                   "<p class=mark id=chosen>Priority</p><img src='../images/picture.png' width=50 height=40>");
    document.Draw({}, {0, 0, 400, 300});
    const auto word =
        std::find_if(device.text.begin(), device.text.end(), [](const auto &item) { return item.text == "Priority"; });
    if (word == device.text.end() || word->color != 0xff0000ff) {
        for (const auto &request : resources.requests)
            std::cerr << "Read: " << request << '\n';
        for (const auto &draw : device.text)
            std::cerr << "Text: " << draw.text << " color " << std::hex << draw.color << std::dec << '\n';
        for (const auto &issue : document.Diagnostics())
            std::cerr << issue << '\n';
    }
    Check(word != device.text.end() && word->color == 0xff0000ff, "CSS specificity did not beat stylesheet order");
    Check(document.Title() == "Example", "Document title was lost");
    Check(document.Text().find("Example") == std::string::npos && document.Text().find("Priority") != std::string::npos,
          "Readable text includes the document head or lost the body");
    Check(resources.requests.size() == 2, "Stylesheets and images did not share resource loading");
    Check(device.images.size() == 1 && device.images[0].width == 50 && device.images[0].height == 40,
          "Image attributes did not set CSS dimensions");
    Check(document.Diagnostics().empty(), "Valid resources produced diagnostics");
    Check(device.clip.empty(), "Drawing left the backend clipped");
}
static void LinksAndAnchors() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><a href='../other.html#Target'>A link</a><div style='height:400px'></div><h3 "
                   "id=Target>Destination</h3>");
    Check(document.Links().size() == 1, "A link was not exposed for keyboard navigation");
    const auto &link = document.Links()[0];
    Check(link.navigation.url == "book://manual/other.html#Target" && !link.bounds.empty(),
          "Relative URL or link geometry was lost");
    const auto box = link.bounds.front();
    const auto hit = document.HitTest({box.x + box.width / 2, box.y + box.height / 2});
    Check(hit.link == link.id && hit.navigation.url == link.navigation.url,
          "Pointer hit testing disagrees with link geometry");
    HtmlPoint point;
    Check(document.FindAnchor("Target", point) && point.y > 400, "ID did not locate its actual heading");
    Check(!document.FindAnchor("target", point), "Anchor IDs became case insensitive");
    HtmlNavigation navigation;
    Check(document.Activate(link.id, navigation) && navigation.url == link.navigation.url,
          "Keyboard link activation lost the target");
    Check(!document.Activate(0, navigation) && navigation.url.empty(), "A missing link appeared valid");
    const auto oldId = link.id;
    Load(document, "<a href='replacement.html'>Replacement</a>");
    Check(!document.Activate(oldId, navigation), "A different document accepted a stale link ID");
}
static void UrlsAndFragmentEncoding() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><a href='../a/./b/../target.html?#'>Empty components</a>"
                   "<a href='a b.html#A+B'>Space</a><a href='?'>Query</a>"
                   "<div style='height:100px'></div><p id='A+B'>Plus</p><p id='a b'>Space</p>"
                   "<p id='top'>Real top</p><p id='caf&#233;'>Accent</p>");
    const auto &links = document.Links();
    Check(links.size() == 3 && links[0].navigation.url == "book://manual/a/target.html?#",
          "URL dot segments or explicit empty query/fragment were lost");
    Check(links[1].navigation.url == "book://manual/topics/a%20b.html#A+B" &&
              links[2].navigation.url == "book://manual/topics/page.html?",
          "URL escaping or empty query failed");
    HtmlPoint point;
    Check(document.FindAnchor("A+B", point) && point.y > 100, "Fragment plus became a space");
    Check(document.FindAnchor("a%20b", point) && point.y > 100, "Escaped fragment was not decoded");
    Check(document.FindAnchor("caf%C3%A9", point), "UTF-8 fragment bytes were changed");
    Check(document.FindAnchor("top", point) && point.y > 100, "Top fallback replaced an actual ID");
    Check(document.FindAnchor("TOP", point) && point.y == 0, "HTML top fallback failed");
}
static void BaseElementAndZoom() {
    TestHtmlResources resources;
    resources.files["book://manual/topics/sub/main.css"] = "p {color:red}";
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><base href='sub/'><link rel=stylesheet href='main.css'><p>Base</p><a "
                   "href='next.html'>Next</a>");
    Check(document.Links().size() == 1 && document.Links()[0].navigation.url == "book://manual/topics/sub/next.html",
          "The base element was not applied");
    std::string error;
    Check(document.Layout({400, 300}, 2, error), error.c_str());
    Check(document.Diagnostics().empty() && document.Links()[0].navigation.url == "book://manual/topics/sub/next.html",
          "Zoom applied the base element twice");
}
static void TablesAndReflow() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    const std::string html = "<!doctype html><table style='width:300px;border-collapse:collapse'><tr><td rowspan=2 "
                             "id=first>A<td id=second>B<tr><td id=third>C</table>"
                             "<p>one two three four five six seven eight nine ten eleven twelve thirteen fourteen "
                             "fifteen sixteen seventeen eighteen</p>";
    Load(document, html);
    HtmlPoint a, b, c;
    Check(document.FindAnchor("first", a) && document.FindAnchor("second", b) && document.FindAnchor("third", c),
          "Table IDs were lost");
    Check(b.x > a.x && c.x == b.x && c.y > b.y, "Rowspan changed the table's column occupancy");
    const auto before = document.ContentSize();
    const auto bookmark = document.CapturePosition({10, before.height - 10});
    std::string error;
    Check(document.Layout({200, 300}, 2, error), error.c_str());
    HtmlPoint restored;
    Check(document.RestorePosition(bookmark, restored), "Reflow invalidated the reading position");
    Check(document.ContentSize().height > before.height && std::isfinite(restored.y),
          "Zoom did not reflow the document");
    Check(document.Load(html, "book://manual/topics/page.html", error), error.c_str());
    Check(document.Layout({400, 300}, 1, error) && document.RestorePosition(bookmark, restored),
          "Reloading the same source lost its reading position");
    Check(document.Load("<p>Different</p>", "book://manual/topics/page.html", error), error.c_str());
    Check(!document.RestorePosition(bookmark, restored), "A different document accepted a stale reading position");
}
static void FailuresKeepTheDocument() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><p id=original>Original</p>");
    device.failFont = true;
    std::string error;
    Check(!document.Load("<p>Replacement</p>", "book://manual/replacement.html", error) && !error.empty(),
          "A font failure appeared successful");
    HtmlPoint point;
    Check(document.FindAnchor("original", point), "Failed load destroyed the previous document");
    Check(!document.Layout({0, 300}, 1, error), "An invalid viewport was accepted");
    device.failFont = false;
    Load(document, "<link rel=stylesheet href='missing.css'><p>Readable<img src='missing.png'></p>");
    Check(document.Diagnostics().size() == 2, "Missing resources were silently discarded");
    document.Draw({}, {0, 0, 400, 300});
    Check(!device.text.empty() && device.clip.empty(), "Missing resources prevented drawing or left clipping active");
}
static void ImageMapShapesAndZoom() {
    TestHtmlResources resources;
    resources.files["book://manual/topics/diagram.png"] = "image";
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document,
         "<!doctype html><style>body{margin:0}</style><img src=diagram.png width=200 height=160 usemap='#map'>"
         "<map name=map><area shape=poly coords='526,98,480,73' href='invalid'>"
         "<area shape=rect coords='5,5,10,10'><area shape=rect coords='40,40,0,0' href='rectangle' alt=Rectangle>"
         "<area shape=circle coords='60,20,10,100' href='circle' alt=Circle>"
         "<area shape=poly coords='80,40,100,60,80,80,999' href='polygon' alt=Polygon>"
         "<area shape=rect coords='100,100,140,140,999,999' href='extra' alt=Extra>"
         "<area shape=default href='fallback' alt=Default></map>");
    Check(document.Links().size() == 5, "Image maps discarded valid areas or activated an empty shape");
    const auto rectangle = std::find_if(document.Links().begin(), document.Links().end(),
                                        [](const auto &link) { return link.label == "Rectangle"; });
    Check(rectangle != document.Links().end() && rectangle->bounds[0].width == 40,
          "Stretching an image rescaled CSS-pixel map coordinates");
    const auto id = rectangle->id;
    Check(document.HitTest({20, 20}).navigation.url == "book://manual/topics/rectangle",
          "Reversed rectangle coordinates were not normalized");
    Check(document.HitTest({7, 7}).link == 0, "A dead area did not mask the link below it");
    Check(document.HitTest({60, 20}).navigation.url == "book://manual/topics/circle", "Circle map hit testing failed");
    Check(document.HitTest({85, 60}).navigation.url == "book://manual/topics/polygon",
          "Polygon map hit testing failed");
    Check(document.HitTest({97, 42}).navigation.url == "book://manual/topics/fallback",
          "Polygon hit testing used its bounding rectangle");
    Check(document.HitTest({120, 120}).navigation.url == "book://manual/topics/extra",
          "Excess rectangle coordinates were not ignored");
    std::string error;
    Check(document.Layout({400, 300}, 2, error), error.c_str());
    Check(document.HitTest({40, 40}).link == id, "Zoom changed map link IDs or failed to scale hit testing");
}
static void StylesheetCyclesAndFailedReflow() {
    TestHtmlResources resources;
    resources.files["book://manual/topics/a.css"] = "@import 'b.css';p{color:red}";
    resources.files["book://manual/topics/b.css"] = "@import 'a.css';p{font-weight:bold}";
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><link rel=stylesheet href='a.css'><p id=keep>Readable</p>");
    Check(resources.requests.size() == 2 && document.Diagnostics().size() == 1,
          "Stylesheet cycles were not bounded or reported");
    const HtmlSize previous = document.ContentSize();
    resources.files.clear();
    device.failFont = true;
    std::string error;
    Check(!document.Layout({200, 300}, 2, error), "Reflow font failure appeared successful");
    Check(document.ContentSize().width == previous.width && document.ContentSize().height == previous.height,
          "Failed reflow replaced the previous layout");
    device.failFont = false;
    Check(document.Layout({200, 300}, 2, error), error.c_str());
    Check(resources.requests.size() == 2, "Reflow reread the document's loaded resource contents");
}
static void PointerStatesAndVisitedLinks() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype "
                   "html><style>a:link{color:red}a:visited{color:#00ff00}a:hover{color:blue}a:active{color:#ffff00}</"
                   "style><a href=next.html>Link</a>");
    const auto id = document.Links()[0].id;
    const auto rect = document.Links()[0].bounds[0];
    const HtmlPoint point{rect.x + rect.width / 2, rect.y + rect.height / 2};
    const auto color = [&]() {
        device.text.clear();
        document.Draw({}, {0, 0, 400, 300});
        for (const auto &item : device.text)
            if (item.text == "Link")
                return item.color;
        return HtmlColor(0);
    };
    Check(color() == 0xff0000ff, "Default link style was lost");
    document.Hover(point);
    Check(color() == 0xffff0000, "Hover did not apply CSS pseudo-class styling");
    document.PointerDown(point);
    Check(color() == 0xff00ffff, "Pointer press did not apply active styling");
    HtmlNavigation navigation;
    Check(document.PointerUp(point, navigation) && navigation.url == "book://manual/topics/next.html",
          "Pointer release did not return the completed navigation");
    document.PointerDown(point);
    document.CancelPointer();
    Check(!document.PointerUp(point, navigation), "Cancelled input activated a link");
    document.PointerDown(point);
    Check(!document.PointerUp({390, 290}, navigation), "Releasing outside a link activated it");
    document.Leave();
    document.SetVisited({"book://manual/topics/next.html"});
    Check(color() == 0xff00ff00, "Visited links did not apply CSS styling");
    std::string error;
    Check(document.Layout({300, 300}, 1.5f, error), error.c_str());
    Check(document.Links()[0].id == id && color() == 0xff00ff00, "Reflow lost visited styling or stable link IDs");
}
static void GradientPixels() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><style>body{margin:0}div{width:100px;height:20px;background:linear-gradient(to "
                   "right,red,blue)}</style><div></div>");
    document.Draw({}, {0, 0, 100, 20});
    Check(device.generated.size() == 1, "A gradient did not produce an image");
    const auto &image = *device.generated.front();
    Check(image.dimensions.width == 100 && image.dimensions.height == 20, "Gradient raster dimensions are wrong");
    Check(image.rgba[0] > 245 && image.rgba[2] < 10, "Gradient start color is wrong");
    const size_t last = (100 * 20 - 1) * 4;
    Check(image.rgba[last] < 10 && image.rgba[last + 2] > 245, "Gradient end color is wrong");
    const size_t middle = 50 * 4;
    Check(image.rgba[middle] > 110 && image.rgba[middle] < 145 && image.rgba[middle + 2] > 110 &&
              image.rgba[middle + 2] < 145,
          "Gradient did not interpolate its actual pixels");
}
int main() {
    try {
        CascadeAndResources();
        LinksAndAnchors();
        UrlsAndFragmentEncoding();
        BaseElementAndZoom();
        TablesAndReflow();
        FailuresKeepTheDocument();
        ImageMapShapesAndZoom();
        StylesheetCyclesAndFailedReflow();
        PointerStatesAndVisitedLinks();
        GradientPixels();
        std::cout << "HTML parsing, CSS cascade, resources, links, tables, reflow and failures passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
