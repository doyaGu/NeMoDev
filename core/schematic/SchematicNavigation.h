#pragma once
#include "core/schematic/ElementAddress.h"
#include <algorithm>
#include <set>
#include <vector>

namespace vdev {
// Selection is confined to one Script at a time, but Script headers: with
// a header selected, Ctrl or Shift and a click on others' select them too
// (headers; Dev acts on "one or several scripts": Expand, Hide Selected,
// colors, Save As, Copy, Delete, initial conditions, Execute).
struct Selection {
    Selection() = default;
    Selection(const Selection &) = default;
    Selection &operator=(const Selection &);
    void Bind(Document *document) { m_Document = document; }
    void Validate();
    void Rebase();
    // Accept a copied selection already checked by a document query.
    void Assign(const std::vector<ElementAddress> &);
    const std::vector<ElementAddress> &Addresses() const { return m_Addresses; }
    std::uint64_t Revision() const { return m_Revision; }

    uint32_t script = 0;
    std::vector<ElementRef> items;
    std::vector<uint32_t> headers; // the other Scripts whose header is selected

    static ElementRef HeaderOf(uint32_t scriptId) { return {ElementKind::Script, scriptId, scriptId}; }
    bool Empty() const { return items.empty(); }
    // Only Script headers are selected.
    bool OnlyHeaders() const {
        if (items.empty()) return false;
        for (const ElementRef &item : items)
            if (item.kind != ElementKind::Script) return false;
        return true;
    }
    bool Contains(uint32_t inScript, const ElementRef &ref) const {
        if (ref.kind == ElementKind::Script && std::find(headers.begin(), headers.end(), inScript) != headers.end()) return true;
        return inScript == script && std::find(items.begin(), items.end(), ref) != items.end();
    }
    // The Scripts whose header is selected, the first one first.
    std::vector<uint32_t> Scripts() const {
        std::vector<uint32_t> scripts;
        for (const ElementRef &item : items)
            if (item.kind == ElementKind::Script && script) {
                scripts.push_back(script);
                break;
            }
        for (uint32_t other : headers)
            if (std::find(scripts.begin(), scripts.end(), other) == scripts.end()) scripts.push_back(other);
        return scripts;
    }
    void Clear() {
        script = 0;
        items.clear();
        headers.clear();
        m_Addresses.clear();
        ++m_Revision;
    }
    void Set(uint32_t inScript, const ElementRef &ref) {
        script = inScript;
        items.assign(1, ref);
        headers.clear();
        Rebase();
    }
    void Add(uint32_t inScript, const ElementRef &ref) {
        if (inScript != script && ref.kind == ElementKind::Script && OnlyHeaders()) {
            if (std::find(headers.begin(), headers.end(), inScript) == headers.end()) headers.push_back(inScript);
            Rebase();
            return;
        }
        if (inScript != script) Set(inScript, ref);
        else if (!Contains(inScript, ref)) { items.push_back(ref); Rebase(); }
    }
    void Toggle(uint32_t inScript, const ElementRef &ref) {
        if (inScript != script && ref.kind == ElementKind::Script && OnlyHeaders()) {
            auto found = std::find(headers.begin(), headers.end(), inScript);
            if (found != headers.end()) headers.erase(found);
            else headers.push_back(inScript);
            Rebase();
            return;
        }
        if (inScript != script) {
            Set(inScript, ref);
            return;
        }
        auto found = std::find(items.begin(), items.end(), ref);
        if (found != items.end()) items.erase(found);
        else items.push_back(ref);
        if (items.empty()) {
            script = 0;
            // Another header selected takes its place.
            if (!headers.empty()) {
                script = headers.front();
                headers.erase(headers.begin());
                items.assign(1, HeaderOf(script));
            }
        }
        Rebase();
    }

private:
    std::uint64_t m_Revision = 0;
    Document *m_Document = nullptr;
    std::vector<ElementAddress> m_Addresses;
};

// Dev Schematic toolbar toggles (UI reference section 8.1).
struct ViewOptions {
    bool showLocalParams = true;
    bool showLinkInfo = true;
    bool showControlPoints = true;
    bool showGrid = false; // Dev's canvas is plain
    bool showAllScripts = true; // Dev shows every Script, in Header::scriptIndex order
    bool showPriorities = false;
    bool showHeaders = true;
    // Dev's Level / Scene script filter (L / S button): with S only Scripts of
    // the active scene are shown.
    bool sceneFilter = false;
    bool zoomMode = false; // Dev Zoom Mode: drag up/down to zoom
    // Dev Link Mode: with auto-link (A on, Z off) a click on an I/O starts a
    // link; L starts one while auto-link is off. Ctrl+click selects instead.
    bool autoLink = true;
};

struct FocusRequest {
    ElementAddress address;
    ObjectRef behavior{};
    bool pending = false;
};


// Navigation belongs to one Schematic view, including Script visibility and
// open/closed bands. It never changes the document's stored display data.
struct SchematicNavigation {
    Selection selection;
    ViewOptions options;
    // The visible stack is a projection rebuilt from the document's Scripts.
    std::vector<uint32_t> visibleScripts;
    std::vector<ObjectRef> scriptOrder;
    std::set<ObjectRef> hiddenScripts;
    std::set<ObjectRef> closedScripts;
    ObjectRef stackScript{};
    FocusRequest focus;
    std::set<uint32_t> HiddenIds(const Document &) const;
};
} // namespace vdev
