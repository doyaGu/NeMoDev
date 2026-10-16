#include "core/schematic/BehaviorDisplayGeometry.h"
#include "core/schematic/SchematicGrid.h"

#include <cmath>
#include <limits>

namespace BehaviorDisplayGeometry {

bool HasRuntimeState(const BehaviorData &behavior) {
    return behavior.runtimeInputCount >= 0 && behavior.runtimeOutputCount >= 0;
}

bool HasTargetParameter(const BehaviorData &behavior) {
    // 0x4CBE90 queries GetTargetParameter(), independently of IsUsingTarget.
    return HasRuntimeState(behavior) ? behavior.runtimeHasTargetParameter : behavior.isUsingTarget;
}

bool HasExpandedSize(const BehaviorData &behavior) {
    return !behavior.folded && behavior.hExpandSize > 0.0f && behavior.vExpandSize > 0.0f;
}

bool TryGetContentBounds(const BehaviorData &behavior, Rect &bounds) {
    if (!std::isfinite(behavior.rect.hPos) || !std::isfinite(behavior.rect.vPos) ||
        (!behavior.folded && (!std::isfinite(behavior.hExpandSize) ||
                             !std::isfinite(behavior.vExpandSize)))) return false;
    // 0x4D3310 selects folded +200/+204 or expanded +192/+196 dimensions.
    const bool expanded = HasExpandedSize(behavior);
    const float right = expanded ? behavior.hExpandSize : behavior.rect.hSize;
    const float bottom = expanded ? behavior.vExpandSize : behavior.rect.vSize;
    // 0x4CBE90 extends only the content left edge by .75H for Target.
    const float left = HasTargetParameter(behavior) ? -0.75f * SchematicGrid::SPACING : 0.0f;
    const float width = right - left;
    const float maxCoordinate = static_cast<float>(std::numeric_limits<int>::max() - 1024);
    if (!std::isfinite(right) || !std::isfinite(bottom) || !std::isfinite(width) ||
        right <= 0.0f || bottom < 0.0f || width > maxCoordinate || bottom > maxCoordinate)
        return false;
    bounds = Rect(left, 0.0f, width, bottom);
    return true;
}

} // namespace BehaviorDisplayGeometry
