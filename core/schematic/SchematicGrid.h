#pragma once

#include <cmath>

// Default Dev 2.5 Schematic View grid. These two operations differ:
// UI positions use 0x4D8400; parameter bounds centers use 0x4D84F0(scale=1).
namespace SchematicGrid {

constexpr float SPACING = 20.0f;

inline float SnapToGrid(float value, float spacing, float origin = 0.0f) {
    const float adjusted = value - origin - spacing * 0.5f;
    // 0x4D8400 fcom(0), fnstsw, mask 0x4100: add one only if >0.
    const int cell = static_cast<int>(adjusted / spacing) + (adjusted > 0.0f ? 1 : 0);
    return static_cast<float>(cell) * spacing + origin;
}

inline float SnapToGridCellCenter(float value) {
    // Truncation with a sign-dependent half-cell, NOT nearest-grid snapping.
    // A negative exact grid boundary belongs to the cell below it.
    const float cell = std::trunc(value / SPACING);
    return (cell + (value < 0.0f ? -0.5f : 0.5f)) * SPACING;
}

inline int LayoutColumnCount(float contentWidth) {
    if (contentWidth <= 0.0f) return 0;
    const int width = static_cast<int>(contentWidth);
    const int spacing = static_cast<int>(SPACING);
    return width / spacing + (width % spacing > 0 ? 1 : 0);
}

} // namespace SchematicGrid
