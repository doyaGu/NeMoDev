#pragma once

#include "core/interface/InterfaceData.h"

// Internal geometry for Dev 2.5 Behavior UI type 22, not Script UI type 23.
namespace BehaviorDisplayGeometry {

bool HasRuntimeState(const BehaviorData &behavior);
bool HasTargetParameter(const BehaviorData &behavior);
bool HasExpandedSize(const BehaviorData &behavior);

// Relative UI id2 content bounds. Missing expanded sizes use the stored rect
// as a compatibility estimate. Invalid geometry leaves bounds unchanged.
bool TryGetContentBounds(const BehaviorData &behavior, Rect &bounds);

} // namespace BehaviorDisplayGeometry
