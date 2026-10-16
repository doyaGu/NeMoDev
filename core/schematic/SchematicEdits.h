#pragma once

#include <set>
#include <vector>

#include "core/schematic/ElementRef.h"
#include "core/interface/InterfaceData.h"

namespace vdev {

// Geometry/display edits on InterfaceData only; no CK topology changes.

// Moves the selected elements by rawDelta (Script-local pixels) from their
// positions in original, writing the result into data. The primary element
// is snapped to the 20px grid (SchematicGrid::SnapToGrid, Dev 0x4D8400) and
// every other element follows by the same snapped delta. Moving a behavior
// graph translates its flattened subtree: descendant rects, operations,
// Comments and the control points of links stored in those graphs.
// Parameter cells are window-relative and move with their owner; Script
// parameters keep the col>=9 / row>=0 clamp.
void MoveElements(InterfaceData &data, const InterfaceData &original, bool rootIsScript,
                  const std::vector<ElementRef> &items, const ElementRef &primary, const Point &rawDelta);

// Expands or folds a behavior graph (UI flag 0x200). When a graph without
// stored expanded size is expanded, the size is derived from its contents
// plus one grid row/column of margin (library choice; Dev's sizing on
// expansion is not recovered).
void SetBehaviorFolded(InterfaceData &data, CK_ID behavior, bool folded);

// Opens or closes a Script (root flag 0x200; 0x4F9C60 hides the body).
void SetScriptOpen(InterfaceData &data, bool open);

// Ids of a graph and all of its descendants, using BehaviorData::parentId.
std::set<CK_ID> CollectSubtree(const InterfaceData &data, CK_ID graph);

// Graph record storing elements, or null.
BehaviorData *FindRecord(InterfaceData &data, CK_ID id);
const BehaviorData *FindRecord(const InterfaceData &data, CK_ID id);

} // namespace vdev
