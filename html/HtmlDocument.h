#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct HtmlPoint {
    float x = 0, y = 0;
};
struct HtmlSize {
    float width = 0, height = 0;
};
struct HtmlRect {
    float x = 0, y = 0, width = 0, height = 0;
};
using HtmlColor = std::uint32_t; // RGBA, with red in the least significant byte

struct HtmlFontDescription {
    std::string families;
    float size = 16;
    int weight = 400;
    bool italic = false;
    unsigned decoration = 0; // underline, overline, line-through
};
struct HtmlFontMetrics {
    float ascent = 0, descent = 0, height = 0, xHeight = 0;
};

// Backend resources own their native allocations. The document retains them;
// paint implementations must also retain resources until submitted work finishes.
class HtmlFont {
public:
    virtual ~HtmlFont() = default;
    virtual HtmlFontMetrics Metrics() const = 0;
    virtual float Width(std::string_view text) const = 0;
};
class HtmlImage {
public:
    virtual ~HtmlImage() = default;
    virtual HtmlSize Size() const = 0;
};

// Reads resolved URLs according to the application's resource policy. Stylesheets
// are UTF-8; image bytes retain their source encoding. No network or filesystem
// access is implicit in the renderer.
class HtmlResources {
public:
    virtual ~HtmlResources() = default;
    virtual bool Read(const std::string &url, std::vector<unsigned char> &bytes, std::string &error) = 0;
};

// Graphics and font measurement belong to the same device, so layout uses the
// font that will actually be painted. Coordinates and font sizes are in pixels.
// SetClip replaces the current convex clipping region; an empty region resets it.
class HtmlRenderDevice {
public:
    virtual ~HtmlRenderDevice() = default;
    virtual std::shared_ptr<HtmlFont> CreateFont(const HtmlFontDescription &description) = 0;
    virtual std::shared_ptr<HtmlImage> DecodeImage(const std::string &url, const std::vector<unsigned char> &bytes) = 0;
    virtual std::shared_ptr<HtmlImage> CreateImage(unsigned width, unsigned height, const unsigned char *rgba) = 0;
    virtual void SetClip(const std::vector<HtmlPoint> &polygon) = 0;
    virtual void Text(const HtmlFont &font, std::string_view text, HtmlPoint position, HtmlColor color) = 0;
    virtual void Image(const HtmlImage &image, HtmlRect destination) = 0;
    virtual void Fill(const std::vector<HtmlPoint> &polygon, HtmlColor color) = 0;
};

struct HtmlNavigation {
    std::string url, target, downloadName;
    bool download = false;
};
struct HtmlLink {
    std::uint64_t id = 0; // valid only for this loaded document
    HtmlNavigation navigation;
    std::string label, title;
    std::vector<HtmlRect> bounds;
    int tabIndex = 0; // negative values exclude a link from sequential navigation
};
struct HtmlHit {
    std::uint64_t link = 0;
    HtmlNavigation navigation;
    std::string title, cursor;
};

// A reading location belongs to one loaded document. Its contents are private;
// reflow does not invalidate it, and a different document rejects it.
class HtmlReadingPosition {
public:
    HtmlReadingPosition();
    ~HtmlReadingPosition();
    HtmlReadingPosition(const HtmlReadingPosition &);
    HtmlReadingPosition &operator=(const HtmlReadingPosition &);

private:
    struct Data;
    std::shared_ptr<const Data> m_Data;
    friend class HtmlDocument;
};

class HtmlDocument {
public:
    // Resources and device must outlive this document. HTML and stylesheets are
    // UTF-8. No script is executed, and following links is the caller's decision.
    HtmlDocument(HtmlResources &resources, HtmlRenderDevice &device);
    ~HtmlDocument();
    HtmlDocument(const HtmlDocument &) = delete;
    HtmlDocument &operator=(const HtmlDocument &) = delete;

    // Failure retains the previous document. Resource failures are available in
    // Diagnostics; they do not turn a readable document into a successful blank.
    bool Load(const std::string &html, const std::string &baseUrl, std::string &error);
    // Reflow is atomic and reuses the loaded resource contents. Failure retains
    // the previous layout and resources; changing HTML requires another Load.
    bool Layout(HtmlSize viewport, float pixelsPerCssPixel, std::string &error);
    void Draw(HtmlPoint origin, HtmlRect visibleDocumentArea);
    HtmlSize ContentSize() const;
    const std::string &Title() const;
    const std::string &Text() const;
    const std::vector<std::string> &Diagnostics() const;

    // Geometry, input and reading positions require a successful Layout. Before
    // that they return empty results. Coordinates use document pixels; link
    // views are refreshed by layout and interaction state changes.
    HtmlHit HitTest(HtmlPoint point) const;
    const std::vector<HtmlLink> &Links() const;
    bool FindAnchor(const std::string &name, HtmlPoint &position) const;
    // Interaction updates styles, layout, readable text and link geometry as one
    // operation. Backend failures throw; geometry then requires another Layout.
    bool Hover(HtmlPoint point);
    void Leave();
    void PointerDown(HtmlPoint point);
    bool PointerUp(HtmlPoint point, HtmlNavigation &navigation);
    void CancelPointer();
    bool Activate(std::uint64_t link, HtmlNavigation &navigation) const;
    // May be supplied before the first Layout; the next layout applies it.
    void SetVisited(const std::vector<std::string> &urls);
    HtmlReadingPosition CapturePosition(HtmlPoint point) const;
    bool RestorePosition(const HtmlReadingPosition &position, HtmlPoint &point) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
