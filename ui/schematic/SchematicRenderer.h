#pragma once

#include <set>

#include "imgui.h"
#include "application/Printing.h"

#include "core/schematic/SchematicModel.h"
#include "ui/controls/SchematicStyle.h"

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
    // Runtime marks matched to this model's retained element positions.
    const SchematicActivity *activity = nullptr;
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
