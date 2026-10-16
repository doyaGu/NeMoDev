#pragma once

#include "imgui.h"

#include "CKAll.h"

namespace vdev {

// Schematic palette, modeled on Dev 2.5's Schematic View (a screenshot of
// Dev with 2D Text.cmo): a plain gray canvas, Script bands with a colored
// left column, raised light-gray Building Blocks, black behavior links,
// dotted parameter links, white Comments. Evidence: the Script colors
// (stored COLORREF, e.g. 0x96C8FA orange) and the link / behavior state
// colors from Dev's preferences (Preferences.h, ApplyColors). The gray
// levels are sampled from the screenshot (canvas 176, Building Block 200,
// band line ~236, collapsed local ~124, selected Script header 128).
struct SchematicStyle {
    ImU32 canvas = IM_COL32(176, 176, 176, 255);
    ImU32 grid = IM_COL32(164, 164, 164, 255);
    ImU32 bandLine = IM_COL32(236, 236, 236, 255);       // between Script bands
    ImU32 headerEdge = IM_COL32(64, 64, 64, 255);        // right edge of the header column
    ImU32 currentHeaderFill = IM_COL32(128, 128, 128, 255); // header of a selected Script
    ImU32 currentHeaderText = IM_COL32(236, 236, 236, 255);
    ImU32 headerTriangle = IM_COL32(96, 96, 96, 255);
    ImU32 thumbnailFill = IM_COL32(176, 176, 176, 255);  // Script snapshot place
    ImU32 thumbnailBorder = IM_COL32(40, 40, 40, 255);
    ImU32 text = IM_COL32(0, 0, 0, 255);
    ImU32 behaviorText = IM_COL32(40, 40, 40, 255);
    ImU32 mutedText = IM_COL32(40, 40, 40, 255);
    ImU32 behaviorFill = IM_COL32(200, 200, 200, 255);
    ImU32 buildingBlockFill = IM_COL32(200, 200, 200, 255);
    ImU32 graphFill = IM_COL32(192, 192, 192, 255);
    ImU32 bevelLight = IM_COL32(236, 236, 236, 255);
    ImU32 bevelDark = IM_COL32(120, 120, 120, 255);
    ImU32 openGraphFill = IM_COL32(184, 184, 184, 255);
    ImU32 openGraphTitle = IM_COL32(160, 160, 160, 255);
    // Expanded / folded behavior graphs (Dev, Tunnel.cmo screenshots).
    ImU32 openGraphEdge = IM_COL32(255, 255, 255, 255);
    ImU32 openGraphName = IM_COL32(255, 255, 255, 255);
    ImU32 graphName = IM_COL32(222, 222, 222, 255);
    ImU32 graphNameEdge = IM_COL32(120, 120, 120, 255);
    ImU32 behaviorBorder = IM_COL32(128, 128, 128, 255);
    ImU32 missingBorder = IM_COL32(255, 0, 0, 255);
    ImU32 behaviorLink = IM_COL32(0, 0, 0, 255);
    ImU32 parameterLink = IM_COL32(60, 50, 200, 255);
    ImU32 highlightLink = IM_COL32(0, 255, 255, 255);
    ImU32 operationLink = IM_COL32(0, 128, 64, 255);
    ImU32 validLink = IM_COL32(20, 160, 100, 255);
    ImU32 invalidLink = IM_COL32(255, 0, 0, 255);
    ImU32 obsoleteBorder = IM_COL32(50, 150, 125, 255);
    ImU32 noPrototypeBorder = IM_COL32(255, 128, 0, 255);
    ImU32 toReplaceBorder = IM_COL32(144, 179, 210, 255);
    ImU32 pinFill = IM_COL32(210, 210, 210, 255);
    ImU32 pinBorder = IM_COL32(24, 24, 24, 255);
    ImU32 targetFill = IM_COL32(128, 128, 128, 255);
    ImU32 localIcon = IM_COL32(0, 0, 0, 255);           // local shown by a shortcut: black, light arrow
    ImU32 localIconArrow = IM_COL32(200, 200, 200, 255);
    ImU32 shortcutIcon = IM_COL32(255, 255, 255, 255);  // shortcut: white, black arrow
    ImU32 plainLocalFill = IM_COL32(150, 150, 150, 255); // other locals: small gray square
    ImU32 collapsedFill = IM_COL32(124, 124, 124, 255);
    ImU32 parameterBorder = IM_COL32(20, 20, 20, 255);
    ImU32 operationFill = IM_COL32(188, 188, 188, 255);
    ImU32 operationBorder = IM_COL32(40, 40, 40, 255);
    ImU32 commentFill = IM_COL32(255, 255, 255, 255);
    ImU32 commentBorder = IM_COL32(150, 150, 150, 255);
    ImU32 startFill = IM_COL32(200, 200, 200, 255);
    ImU32 startBorder = IM_COL32(56, 56, 56, 255);
    ImU32 controlPoint = IM_COL32(0, 0, 0, 255);
    ImU32 selection = IM_COL32(0, 60, 220, 255);
    ImU32 hover = IM_COL32(255, 255, 255, 170);
    ImU32 rubberBand = IM_COL32(40, 80, 200, 40);
    ImU32 rubberBandBorder = IM_COL32(20, 40, 140, 220);
};

// Stored Script colors are GDI COLORREF (0x00BBGGRR), as Dev paints them
// (0x96C8FA is the orange of "Topic" Scripts, 0xFAC896 the light blue).
inline ImU32 ColorRefToImU32(CKDWORD color, int alpha = 255) {
    return IM_COL32(color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF, alpha);
}

} // namespace vdev
