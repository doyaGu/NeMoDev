#pragma once

#include "imgui.h"

#include "CKAll.h"

namespace vdev {

// Small line icons after Dev's class bitmaps (Level Manager rows, Script
// header owners), drawn in a 10x10 cell whose top-left corner is at; scale
// is screen pixels per cell unit.
enum class DevIcon { Level, Folder, LevelScripts, Script, Entity2D, Entity3D, Material, Place, Object, Scene };

DevIcon IconForClass(CK_CLASSID cid);
void DrawDevIcon(ImDrawList *drawList, const ImVec2 &at, float scale, DevIcon icon, ImU32 ink, ImU32 paper);

} // namespace vdev
