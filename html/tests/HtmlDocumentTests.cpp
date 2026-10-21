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
    std::vector<HtmlColor> fills;
    std::vector<std::vector<HtmlPoint>> fillClips;
    std::vector<std::vector<HtmlPoint>> fillPolygons;
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
    void Fill(const std::vector<HtmlPoint> &polygon, HtmlColor color) override {
        fills.push_back(color);
        fillClips.push_back(clip);
        fillPolygons.push_back(polygon);
    }
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
    Load(document, "<!doctype html><link rel='preload StyleSheet' href='../styles/main.css'><title>Example</title>"
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
static void NestedMultilineLinkGeometry() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><div style='margin:200px 50px;width:100px'><p><a href='nested.html'>"
                   "Multiple words wrapped across several lines</a></p></div>");
    Check(document.Links().size() == 1, "Nested link was lost");
    const auto link = document.Links().front();
    Check(link.bounds.size() > 1, "Wrapped link lost its line fragments");
    for (const auto &box : link.bounds) {
        Check(box.x >= 50 && box.y >= 200, "Link geometry stayed relative to an internal parent");
        Check(document.HitTest({box.x + box.width / 2, box.y + box.height / 2}).link == link.id,
              "Nested link's keyboard geometry disagrees with pointer hit testing");
    }
}
static void PresentationalHintsSurviveLinkStates() {
    TestHtmlResources resources;
    resources.files["book://manual/topics/image.png"] = "image";
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document,
         "<style>a:hover{color:red} td{color:green}</style><body bgcolor=white><table cellpadding=0 cellspacing=0>"
         "<tr><td id=first height=17><a href='other.html'><img src=image.png width=114 height=17></a></td>"
         "<td rowspan=3 height=66><img src=image.png width=513 height=66></td></tr>"
         "<tr><td height=32>&nbsp;</td></tr><tr><td id=last height=17><img src=image.png width=114 height=17></td></tr>"
         "</table><font color=red>Font</font></body>",
         900);
    HtmlPoint first, last;
    Check(document.FindAnchor("first", first) && document.FindAnchor("last", last) && last.y - first.y == 49,
          "Legacy table cell heights were not honored");
    const auto size = document.ContentSize();
    const auto box = document.Links().front().bounds.front();
    document.Hover({box.x + 2, box.y + 2});
    document.Leave();
    document.SetVisited({"book://manual/topics/other.html"});
    Check(document.FindAnchor("last", last) && last.y - first.y == 49 && document.ContentSize().height == size.height,
          "Link state refresh discarded HTML attribute styles");
    document.Draw({}, {0, 0, 900, 300});
    Check(device.images.size() == 3 && device.images[1].width == 513 && device.images[1].height == 66,
          "Link state refresh discarded image dimensions");
    const auto font =
        std::find_if(device.text.begin(), device.text.end(), [](const auto &draw) { return draw.text == "Font"; });
    Check(font != device.text.end() && font->color == 0xff0000ff, "Link state refresh discarded font color");
}
static void LegacyMarkupAndStyles() {
    TestHtmlResources resources;
    resources.files["book://manual/topics/a.png"] = "image";
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document,
         "<!doctype html><style>body{background:white;font-size:x-small} h3{color:#660066;background:#f2f2f2;"
         "margin-top:24pt;padding-left:5pt;font-size:medium}.pin{color:#4040ff;font-weight:bold}"
         ".code{background:#faebd7}</style><body><h3 id=heading>Inputs</h3><p><span class=pin>Key</span></p>"
         "<center><img id=diagram src=a.png width=120 height=40 border=1></center>"
         "<table id=table border=0 width='75%' cellpadding=0 cellspacing=0><tr id=row><td id=cell width='20%'>A"
         "<td rowspan=2 width='80%'>B<tr><td>C</table><pre class=code>\r\nfirst\r\n\tsecond</pre>"
         "<a id=new name=old>End</a><ul><li>one<li>two</ul><script>bad()</script></body>",
         800);
    document.Draw({}, {0, 0, 800, 1000});
    const auto text = [&](const char *word) -> const TestHtmlDevice::TextDraw & {
        const auto found =
            std::find_if(device.text.begin(), device.text.end(), [&](const auto &draw) { return draw.text == word; });
        Check(found != device.text.end(), "Styled text was not painted");
        return *found;
    };
    Check(text("Inputs").color == 0xff660066 && text("Inputs").font.size == 16 && text("Inputs").point.y >= 32 &&
              text("Inputs").point.x > 14,
          "Heading color, size, margin or padding was lost");
    Check(text("Key").color == 0xffff4040 && text("Key").font.weight >= 600, "Inline color or bold style was lost");
    Check(std::find(device.fills.begin(), device.fills.end(), 0xfff2f2f2) != device.fills.end() &&
              std::find(device.fills.begin(), device.fills.end(), 0xffd7ebfa) != device.fills.end(),
          "Heading or preformatted background was lost");
    Check(device.images.size() == 1 && device.images[0].width == 120 && device.images[0].height == 40 &&
              std::fabs(device.images[0].x + 60 - 400) < 2,
          "Centered image dimensions or alignment were lost");
    Check(document.Text().find("first\n\tsecond") != std::string::npos &&
              document.Text().find("bad()") == std::string::npos,
          "Preformatted whitespace or script exclusion failed");
    for (const char *name : {"heading", "table", "row", "cell", "diagram", "new", "old"}) {
        HtmlPoint position;
        Check(document.FindAnchor(name, position), "An element ID or named anchor was lost");
    }
    Load(document,
         "<!doctype html><style>a{text-decoration:none}a:link{color:red}a:hover{color:blue;text-decoration:underline}"
         "a:visited{color:purple}</style><body><p>Start&nbsp;here<h3>Title</h3><p><a href=page.html>Link"
         "<span style=color:green>fixed</span></a></body>");
    const auto box = document.Links()[0].bounds[0];
    document.Hover({box.x + 1, box.y + 1});
    device.text.clear();
    document.Draw({}, {0, 0, 400, 300});
    Check(text("Link").color == 0xffff0000 && (text("Link").font.decoration & HtmlDecorationUnderline) &&
              text("fixed").color == 0xff008000,
          "Hover lost link decoration or changed an explicitly styled descendant");
    document.Leave();
    document.SetVisited({"book://manual/topics/page.html"});
    device.text.clear();
    document.Draw({}, {0, 0, 400, 300});
    Check(text("Link").color == 0xff800080 && !(text("Link").font.decoration & HtmlDecorationUnderline) &&
              text("fixed").color == 0xff008000,
          "Visited state lost its cascade or retained hover decoration");
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
static void LayoutOwnsGeometryLifetime() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    std::string error;
    Check(document.Load("<!doctype html><style>a:visited{color:red}</style><a href=other.html>Visited</a>",
                        "book://manual/page.html", error),
          error.c_str());
    const auto invalid = document.CapturePosition({});
    HtmlPoint restored;
    Check(!document.Hover({1, 1}) && document.HitTest({1, 1}).link == 0 && !document.RestorePosition(invalid, restored),
          "An unlaid-out document invented geometry or accepted a reading position");
    document.SetVisited({"book://manual/other.html"});
    Check(document.Layout({400, 300}, 1, error), error.c_str());
    document.Draw({}, {0, 0, 400, 300});
    Check(!device.text.empty() && device.text.front().color == 0xff0000ff,
          "Visited URLs supplied before layout were lost");
    Check(!document.RestorePosition(invalid, restored) &&
              document.RestorePosition(document.CapturePosition({8, 8}), restored),
          "Layout did not establish reading-position lifetime");
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
                   "right in srgb,red,blue)}</style><div></div>");
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
static void GradientInterpolationPixels() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    // Reference midpoints for opaque red/blue in the three supported spaces.
    struct Sample {
        const char *space;
        int red, green, blue;
    };
    for (const auto sample : {Sample{"srgb", 128, 0, 128}, Sample{"srgb-linear", 188, 0, 188},
                              Sample{"oklab", 140, 83, 162}, Sample{"", 140, 83, 162}}) {
        const std::string interpolation = *sample.space ? std::string(" in ") + sample.space : "";
        for (const auto gradient : {"linear-gradient(to right" + interpolation + ",red,blue)",
                                    "radial-gradient(circle 100px at .5px .5px" + interpolation + ",red,blue)",
                                    "conic-gradient(from 0deg at 50.5px -50px" + interpolation + ",red,blue)"}) {
            Load(document, "<!doctype html><style>body{margin:0}div{width:101px;height:1px;background:" + gradient +
                               "}</style><div></div>");
            device.generated.clear();
            document.Draw({}, {0, 0, 101, 1});
            Check(device.generated.size() == 1, "A supported gradient did not render");
            const auto &rgba = device.generated.front()->rgba;
            const std::string failure = "Wrong interpolation midpoint for " + gradient + ": " +
                                        std::to_string(rgba[200]) + "," + std::to_string(rgba[201]) + "," +
                                        std::to_string(rgba[202]);
            Check(std::abs(int(rgba[200]) - sample.red) <= 2 && std::abs(int(rgba[201]) - sample.green) <= 2 &&
                      std::abs(int(rgba[202]) - sample.blue) <= 2,
                  failure.c_str());
        }
    }
}
static void UnsupportedGradientsPreserveFallbacks() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    for (const char *space : {"display-p3", "a98-rgb", "prophoto-rgb", "rec2020", "lab", "xyz", "xyz-d50", "xyz-d65",
                              "hsl", "hwb", "lch", "oklch"})
        for (const char *kind : {"linear", "radial", "conic"}) {
            const std::string gradient = std::string(kind) + "-gradient(in " + space + ",red,blue)";
            Load(document,
                 "<!doctype html><style>body{margin:0}div{width:80px;height:20px;background:lime}"
                 "#long{background-image:linear-gradient(in srgb,red,blue);background-image:" +
                     gradient + "}#short{background:" + gradient + "}#list{background-image:url(missing.png)," +
                     gradient +
                     "}</style><div id=long></div><div id=short></div><div id=list></div><a href=next>Readable</a>");
            device.generated.clear();
            device.fills.clear();
            device.fillPolygons.clear();
            document.Draw({}, {0, 0, 400, 300});
            Check(device.generated.size() == 1, "An unsupported gradient replaced a supported fallback declaration");
            Check(std::count(device.fills.begin(), device.fills.end(), 0xff00ff00) == 3,
                  "An unsupported background shorthand or image list erased the fallback color");
            Check(document.Links().size() == 1 && document.Text().find("Readable") != std::string::npos,
                  "An unsupported background prevented document interaction");
            Check(document.Diagnostics().empty(), "A discarded image declaration still requested its resources");
            Check(device.clip.empty(), "Unsupported CSS left the drawing clip active");
        }
}
static void HitQueriesUseComputedStyles() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><style>body{margin:0}a{cursor:crosshair}div{cursor:help}</style>"
                   "<a href=next>Link</a><div id=plain>Plain</div>");
    const auto box = document.Links().front().bounds.front();
    const HtmlPoint link{box.x + 2, box.y + 2};
    HtmlPoint plain;
    Check(document.FindAnchor("plain", plain), "Plain text position was lost");
    plain.x += 2;
    plain.y += 2;
    Check(document.HitTest(link).cursor == "crosshair", "Link hit testing ignored the author's cursor");
    Check(document.HitTest(plain).cursor == "help", "Cursor queries require an earlier hover event");
    document.Hover(link);
    Check(document.HitTest(plain).cursor == "help", "Cursor queries reused another element's hover state");
    Check(document.HitTest({-100, -100}).cursor.empty(), "An empty hit retained a previous cursor");
    resources.files["book://manual/topics/image.png"] = "image";
    Load(document, "<!doctype html><style>body{margin:0}img{cursor:help}</style><img src=image.png usemap='#map'>"
                   "<map name=map><area coords='0,0,20,20' style='cursor:wait' href=next>"
                   "<area coords='20,0,40,20' title=Unlinked><area coords='40,0,60,20' href=next></map>");
    Check(document.HitTest({2, 2}).cursor == "wait", "Image map hit testing ignored the area's cursor");
    const auto unlinked = document.HitTest({22, 2});
    Check(!unlinked.link && unlinked.title == "Unlinked" && unlinked.cursor == "help",
          "An unlinked image map area discarded its tooltip or image cursor");
    Check(document.HitTest({42, 2}).cursor == "help", "Image map hit testing replaced the image's author cursor");
}
static void InteractionRefreshesReadableText() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><style>span{display:none}a:hover span,a:visited span{display:inline}</style>"
                   "<a href=next>Link<span>Details</span></a>");
    const auto box = document.Links().front().bounds.front();
    Check(document.Text().find("Details") == std::string::npos, "Hidden text was exposed before interaction");
    document.Hover({box.x + 2, box.y + 2});
    Check(document.Text().find("Details") != std::string::npos, "Hover did not refresh readable text");
    document.Draw({}, {0, 0, 400, 300});
    Check(std::any_of(device.text.begin(), device.text.end(), [](const auto &draw) { return draw.text == "Details"; }),
          "A newly displayed hover span did not join the rendering tree");
    document.Leave();
    Check(document.Text().find("Details") == std::string::npos, "Leaving retained hidden hover text");
    document.SetVisited({"book://manual/topics/next"});
    Check(document.Text().find("Details") != std::string::npos, "Visited styles did not refresh readable text");
    document.SetVisited({});
    Check(document.Text().find("Details") == std::string::npos, "Clearing visited links retained their text");
}
static void InteractionFailuresRequireSuccessfulLayout() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><style>a:hover{font-size:30px}</style><a href=next>Link</a>");
    const auto box = document.Links().front().bounds.front();
    const HtmlPoint point{box.x + 2, box.y + 2};
    device.failFont = true;
    bool failed = false;
    try {
        document.Hover(point);
    } catch (const std::exception &) {
        failed = true;
    }
    Check(failed, "A failed interaction was silently accepted");
    Check(document.ContentSize().height == 0 && document.Links().empty() && !document.HitTest(point).link,
          "An interrupted style refresh exposed stale geometry");
    device.failFont = false;
    std::string error;
    Check(document.Layout({400, 300}, 1, error), error.c_str());
    document.Draw({}, {0, 0, 400, 300});
    Check(document.Links().size() == 1 && !device.text.empty(), "Layout could not recover failed interaction");
}
static void ReadingPositionsIgnoreGeneratedContent() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><style>body{margin:0}p{margin:0}"
                   "@media(min-width:300px){p:before{content:'Generated';display:block;height:80px}}</style>"
                   "<div style='height:100px'></div><p><span id=target>Original</span></p>");
    HtmlPoint target;
    Check(document.FindAnchor("target", target), "Reading target was lost");
    const auto position = document.CapturePosition({target.x + 2, target.y + 2});
    std::string error;
    Check(document.Layout({200, 300}, 1, error), error.c_str());
    HtmlPoint restored;
    Check(document.FindAnchor("target", target) && document.RestorePosition(position, restored) &&
              std::fabs(restored.x - target.x - 2) < .01f && std::fabs(restored.y - target.y - 2) < .01f,
          "Generated CSS nodes changed the identity of a reading position");
}
static void DeviceTextDecorations() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    std::string error;
    Check(document.Load("<!doctype html><p style='text-decoration:underline'>Under</p>"
                        "<p style='text-decoration:overline'>Over</p>"
                        "<p style='text-decoration:line-through'>Strike</p>"
                        "<p style='text-decoration:underline overline line-through'>Combined</p>",
                        "https://docs.example.test/index.html", error) &&
              document.Layout({400, 300}, 1, error),
          error.c_str());
    document.Draw({}, {0, 0, 400, 300});
    const std::map<std::string, unsigned> expected = {
        {"Under", HtmlDecorationUnderline},
        {"Over", HtmlDecorationOverline},
        {"Strike", HtmlDecorationLineThrough},
        {"Combined", HtmlDecorationUnderline | HtmlDecorationOverline | HtmlDecorationLineThrough}};
    for (const auto &item : expected) {
        const auto draw = std::find_if(device.text.begin(), device.text.end(),
                                       [&](const auto &draw) { return draw.text == item.first; });
        Check(draw != device.text.end() && draw->font.decoration == item.second,
              "CSS decoration did not reach the device's public flags");
    }
}
static void LinkIdentityIgnoresGeneratedImages() {
    TestHtmlResources resources;
    resources.files["book://manual/topics/icon.png"] = "image";
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><style>@media(min-width:300px){body:before{content:url(icon.png)}}</style>"
                   "<a href=first>First</a><a href=second>Second</a>");
    const auto links = document.Links();
    std::string error;
    Check(document.Layout({200, 300}, 1, error), error.c_str());
    Check(document.Links().size() == 2 && document.Links()[0].id == links[0].id &&
              document.Links()[1].id == links[1].id,
          "Generated images changed source link identities after reflow");
    HtmlNavigation navigation;
    Check(document.Activate(links[0].id, navigation) && navigation.url == links[0].navigation.url,
          "A retained keyboard link ID activated a different destination");
}
static void CascadeAcrossReflowAndInteraction() {
    TestHtmlResources resources;
    resources.files["book://manual/topics/first.css"] =
        "@import 'nested/second.css'; #target{color:red!important} p{font-size:12px}";
    resources.files["book://manual/topics/nested/second.css"] = "p{color:blue} #target{color:purple}";
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><link rel=stylesheet href=first.css><style>p{color:yellow}"
                   "a:hover{color:blue} @media(max-width:250px){p{font-size:20px}}</style>"
                   "<p id=target style='color:green'>Cascade</p><a href=next>Hover</a>");
    const auto verify = [&](float scale, float size) {
        device.text.clear();
        document.Draw({}, {0, 0, 800, 800});
        const auto text = std::find_if(device.text.begin(), device.text.end(),
                                       [](const auto &draw) { return draw.text == "Cascade"; });
        Check(text != device.text.end() && text->color == 0xff0000ff && text->font.size == size * scale,
              "Import order, inline priority, important or media query cascade changed");
    };
    verify(1, 12);
    const auto rect = document.Links().front().bounds.front();
    document.Hover({rect.x + 2, rect.y + 2});
    document.Leave();
    verify(1, 12);
    std::string error;
    Check(document.Layout({400, 300}, 2, error), error.c_str());
    verify(2, 20);
    Check(resources.requests.size() == 2, "Reflow reloaded imported stylesheets");
}
static void ImageSizingAndVisibility() {
    TestHtmlResources resources;
    resources.files["book://manual/topics/image.png"] = "image";
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><style>body{margin:0}#scaled{width:60px;height:auto}"
                   "#limited{max-width:50px;height:auto}</style>"
                   "<img id=scaled src=image.png width=120 height=50>"
                   "<img id=limited src=image.png><img src=image.png style='visibility:hidden'>");
    const auto verify = [&](float scale) {
        device.images.clear();
        document.Draw({}, {0, 0, 800, 800});
        Check(device.images.size() == 2, "An invisible image was painted");
        Check(std::fabs(device.images[0].width - 60 * scale) < .01f &&
                  std::fabs(device.images[0].height - 48 * scale) < .01f &&
                  std::fabs(device.images[1].width - 50 * scale) < .01f &&
                  std::fabs(device.images[1].height - 40 * scale) < .01f,
              "CSS image constraints lost the intrinsic aspect ratio");
    };
    verify(1);
    std::string error;
    Check(document.Layout({800, 600}, 2, error), error.c_str());
    verify(2);
}
static void HiddenAnchorsAndImageMaps() {
    TestHtmlResources resources;
    resources.files["book://manual/topics/image.png"] = "image";
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><div style='height:100px'></div>"
                   "<div style='display:none'><p id=hidden>Hidden</p>"
                   "<img src=image.png usemap='#hidden-map'></div>"
                   "<img src=image.png usemap='#hidden-map' style='visibility:hidden'>"
                   "<map name=hidden-map><area coords='0,0,50,50' href=next></map><p id=visible>Visible</p>");
    HtmlPoint point{19, 23};
    Check(!document.FindAnchor("hidden", point) && point.x == 19 && point.y == 23,
          "An anchor without layout supplied a false navigation position");
    Check(document.Links().empty(), "Hidden image maps remained keyboard activatable");
    Check(document.FindAnchor("visible", point) && point.y >= 100, "Visible anchor geometry was lost");
}
static void WrappedAnchorsStartAtFirstFragment() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><div style='margin:200px 50px;width:100px'><a id=wrapped href=next>"
                   "Several words wrapping across multiple lines</a></div>");
    const auto verify = [&] {
        const auto box = document.Links().front().bounds.front();
        HtmlPoint point;
        Check(document.FindAnchor("wrapped", point) && std::fabs(point.y - box.y) < .01f &&
                  std::fabs(point.x - box.x) < .01f,
              "A wrapped anchor navigated to its last line instead of its first fragment");
    };
    verify();
    std::string error;
    Check(document.Layout({600, 600}, 1.5f, error), error.c_str());
    verify();
}
static void EmptyClipsStayEmpty() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><style>body{margin:0}</style>"
                   "<div style='width:30px;height:30px;overflow:hidden'>"
                   "<div style='margin-left:40px;width:30px;height:30px;background:red'></div></div>");
    document.Draw({}, {0, 0, 300, 300});
    for (size_t i = 0; i < device.fills.size(); ++i) {
        if (device.fills[i] != 0xff0000ff)
            continue;
        const auto &clip = device.fillClips[i];
        Check(!clip.empty() && std::all_of(clip.begin(), clip.end(),
                                           [&](const auto &point) {
                                               return point.x == clip.front().x && point.y == clip.front().y;
                                           }),
              "A nested background expanded an empty overflow clip");
    }
}
static void RepeatedImagesOnlyPaintVisibleTiles() {
    TestHtmlResources resources;
    resources.files["book://manual/topics/tile.png"] = "image";
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><style>body{margin:0}div{width:100px;height:10000px;"
                   "background:url(tile.png) repeat;background-size:10px 10px}</style><div></div>");
    document.Draw({}, {0, 4000, 100, 80});
    Check(!device.images.empty() && device.images.size() <= 90,
          "A repeated background submitted tiles outside the visible document area");
    for (const auto &rect : device.images)
        Check(rect.y >= 4000 && rect.y < 4080, "Repeat alignment was not retained after clipping");
}
static void VisibilityPreservesVisibleDescendants() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><div style='visibility:hidden;height:40px'>Hidden"
                   "<span style='visibility:visible'>Visible</span>"
                   "<a href=next style='visibility:visible'>VisibleLink</a></div><p id=after>After</p>");
    document.Draw({}, {0, 0, 400, 300});
    Check(
        std::none_of(device.text.begin(), device.text.end(), [](const auto &draw) { return draw.text == "Hidden"; }) &&
            std::any_of(device.text.begin(), device.text.end(),
                        [](const auto &draw) { return draw.text == "Visible"; }),
        "Visibility inheritance suppressed an explicitly visible descendant");
    Check(document.Text().find("Hidden") == std::string::npos && document.Text().find("Visible") != std::string::npos,
          "Readable text did not follow inherited visibility");
    HtmlPoint after;
    Check(document.FindAnchor("after", after) && after.y >= 40, "Hidden content stopped reserving its layout height");
    Check(document.Links().size() == 1, "A visible link inside hidden content was lost");
    const auto box = document.Links().front().bounds.front();
    Check(document.HitTest({box.x + 2, box.y + 2}).link == document.Links().front().id,
          "A visible descendant was painted but could not be hit");
}
static HtmlRect FilledBounds(const TestHtmlDevice &device, HtmlColor color) {
    for (size_t i = 0; i < device.fills.size(); ++i) {
        if (device.fills[i] != color)
            continue;
        const auto &points = device.fillPolygons[i];
        Check(!points.empty(), "An expected background has no polygon");
        float left = points.front().x, right = left, top = points.front().y, bottom = top;
        for (const auto &point : points) {
            left = std::min(left, point.x);
            right = std::max(right, point.x);
            top = std::min(top, point.y);
            bottom = std::max(bottom, point.y);
        }
        return {left, top, right - left, bottom - top};
    }
    throw std::runtime_error("An expected background was not painted");
}
static void EmptyTableColumnsShareExtraWidth() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document, "<!doctype html><style>body{margin:0}table{width:180px;border-collapse:collapse}"
                   "td{height:20px;padding:0;border:2px solid black}#head{background:red}#left{background:lime}"
                   "#right{background:blue}#bottom{background:yellow}</style>"
                   "<table><tr><td id=head colspan=2></td></tr>"
                   "<tr><td id=left rowspan=2></td><td id=right></td></tr><tr><td id=bottom></td></tr></table>");
    for (float scale : {1.0f, 1.5f}) {
        std::string error;
        Check(document.Layout({600, 600}, scale, error), error.c_str());
        device.fills.clear();
        device.fillPolygons.clear();
        document.Draw({}, {0, 0, 600, 600});
        const auto left = FilledBounds(device, 0xff00ff00), right = FilledBounds(device, 0xffff0000);
        const auto head = FilledBounds(device, 0xff0000ff), bottom = FilledBounds(device, 0xff00ffff);
        Check(left.width / scale > 80 && std::fabs(left.width - right.width) < scale && head.width / scale >= 176,
              "Empty table columns assigned all extra width to the last column");
        Check(left.height > right.height && bottom.y >= right.y + 20 * scale && right.x > left.x + 80 * scale,
              "Rowspan or colspan lost its cell occupancy after resizing");
    }
}
static void PositionedBorderBoxConstraints() {
    TestHtmlResources resources;
    TestHtmlDevice device;
    HtmlDocument document(resources, device);
    Load(document,
         "<!doctype html><style>body{margin:0}div{position:absolute;top:0;width:80px;height:80px;"
         "border:12px solid red;box-sizing:border-box}#box{left:0;background:blue}"
         "#limited{left:120px;min-width:90px;max-width:100px;min-height:90px;max-height:100px;background:lime}"
         "</style><div id=box></div><div id=limited></div>");
    for (float scale : {1.0f, 1.5f}) {
        std::string error;
        Check(document.Layout({600, 600}, scale, error), error.c_str());
        device.fills.clear();
        device.fillPolygons.clear();
        document.Draw({}, {0, 0, 600, 600});
        const auto box = FilledBounds(device, 0xffff0000), limited = FilledBounds(device, 0xff00ff00);
        Check(std::fabs(box.width - 80 * scale) < .01f && std::fabs(box.height - 80 * scale) < .01f,
              "Positioned border-box sizing counted the borders twice");
        Check(std::fabs(limited.width - 90 * scale) < .01f && std::fabs(limited.height - 90 * scale) < .01f,
              "Positioned min/max constraints used the wrong box or minimum height");
    }
}
int main() {
    try {
        CascadeAndResources();
        LinksAndAnchors();
        UrlsAndFragmentEncoding();
        NestedMultilineLinkGeometry();
        PresentationalHintsSurviveLinkStates();
        LegacyMarkupAndStyles();
        BaseElementAndZoom();
        TablesAndReflow();
        FailuresKeepTheDocument();
        LayoutOwnsGeometryLifetime();
        ImageMapShapesAndZoom();
        StylesheetCyclesAndFailedReflow();
        PointerStatesAndVisitedLinks();
        HitQueriesUseComputedStyles();
        InteractionRefreshesReadableText();
        InteractionFailuresRequireSuccessfulLayout();
        ReadingPositionsIgnoreGeneratedContent();
        DeviceTextDecorations();
        LinkIdentityIgnoresGeneratedImages();
        CascadeAcrossReflowAndInteraction();
        ImageSizingAndVisibility();
        HiddenAnchorsAndImageMaps();
        WrappedAnchorsStartAtFirstFragment();
        EmptyClipsStayEmpty();
        RepeatedImagesOnlyPaintVisibleTiles();
        VisibilityPreservesVisibleDescendants();
        EmptyTableColumnsShareExtraWidth();
        PositionedBorderBoxConstraints();
        GradientPixels();
        GradientInterpolationPixels();
        UnsupportedGradientsPreserveFallbacks();
        std::cout << "HTML parsing, CSS cascade, resources, links, tables, reflow and failures passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
