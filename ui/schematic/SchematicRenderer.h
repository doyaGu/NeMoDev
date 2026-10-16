#pragma once

#include <set>

#include "imgui.h"

#include "core/schematic/SchematicModel.h"
#include "ui/schematic/SchematicStyle.h"

namespace vdev {

// Script-local pixels -> screen. offsetY places a Script in the stack.
struct ViewTransform {
    ImVec2 origin;     // screen position of world (panX, panY)
    float panX = 0.0f;
    float panY = 0.0f;
    float zoom = 1.0f;

    ImVec2 ToScreen(float x, float y) const {
        return ImVec2(origin.x + (x - panX) * zoom, origin.y + (y - panY) * zoom);
    }
    Point ToWorld(const ImVec2 &screen) const {
        return Point((screen.x - origin.x) / zoom + panX, (screen.y - origin.y) / zoom + panY);
    }
};

// Print Schematic's options (Dev.exe's Schematic Print Parameters, dialog
// 702, sub_5990C0): without them the print is black outlines on white.
struct SchematicPrint {
    bool fillHeader = false; // Fill Script Header
    bool fillBody = false;   // Fill Script Body (else white, the band lines black)
    bool fillShapes = false; // Fill Shapes (Building Blocks, operations, comments, Start)
    bool bitmaps = false;    // Print Bitmaps (the Script snapshots)
    bool colorLinks = false; // Use Color for Links (else black)
};

struct DrawOptions {
    const SchematicStyle *style = nullptr;
    ImFont *font = nullptr;
    ImFont *boldFont = nullptr; // expanded graph names (DevTheme BoldFont); font when null
    float fontSize = 13.0f; // world pixels at zoom 1
    bool showLocalParams = true;
    bool showLinkInfo = true;
    bool showControlPoints = true;
    bool showPriorities = false; // Dev toolbar [Pri]
    bool showHeaders = true;     // Dev toolbar [H]
    bool current = false;        // the current Script: dotted frame on its header
    float scriptZoom = 1.0f;     // the Script's own zoom, shown in its header when not 100%
    // Script Debugger: running behaviors (CKBehavior::IsActive) and the debug context's behavior.
    const std::set<CK_ID> *running = nullptr;
    CK_ID debugBehavior = 0;
    // Trace Mode (documentation: "all active scripts, behaviors and links
    // are drawn in red"): the behavior links firing, the Script active.
    bool trace = false;
    const std::set<CK_ID> *activeLinks = nullptr;
    bool scriptActive = false;
    const std::set<ElementRef> *selection = nullptr;
    // Links found invalid after loading and kept: Dev's Invalid Link Color.
    const std::set<ElementRef> *invalid = nullptr;
    ElementRef hovered;
    ImVec2 clipMin;
    ImVec2 clipMax;
    // Dev's print mode (Dev.exe sub_5380F0 sets dword_646D2C): the options
    // the drawing functions test.
    const SchematicPrint *print = nullptr;
};

// Draws one Script of the stack. Links are straight segments through the
// stored control points, as Dev's DrawLink (0x4DA2B0).
// frame places the band and its header; body draws the Script contents,
// clipped to the band right of the header (each Script body is its own
// canvas with its own zoom and scroll).
void DrawScript(ImDrawList *drawList, const SchematicModel &model, float offsetY, const ViewTransform &frame,
                const ViewTransform &body, const DrawOptions &options);

void DrawGrid(ImDrawList *drawList, const ViewTransform &view, const ImVec2 &min, const ImVec2 &max,
              const SchematicStyle &style);

} // namespace vdev
