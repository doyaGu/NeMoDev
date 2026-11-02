#pragma once

#include "imgui.h"
#include "core/catalog/ClassIcon.h"


namespace vdev {

// Small line icons after Dev's class bitmaps (Level Manager rows, Script
// header owners), drawn in a 10x10 cell whose top-left corner is at; scale
// is screen pixels per cell unit.
using DevIcon = ClassIconKind;

void DrawDevIcon(ImDrawList *drawList, const ImVec2 &at, float scale, DevIcon icon, ImU32 ink, ImU32 paper);

} // namespace vdev
