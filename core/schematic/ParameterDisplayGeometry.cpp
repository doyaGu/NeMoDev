#include "core/schematic/ParameterDisplayGeometry.h"

#include "CKAll.h"
#include "core/schematic/SchematicGrid.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ParameterDisplayGeometry {
namespace {

float EstimateWidth(const Parameter& parameter, CKContext* context) {
    // 0x4E1470 selects an exact mode after masking flags, not bit precedence.
    // Unrecognized combinations are displayed as COLLAPSED without rewriting
    // the library's stored style flags. 0x800 is a glyph-only native mode.
    const int mode = static_cast<int>(parameter.style) & 0x3E00;
    switch (mode) {
    case PARAM_STYLE_NAME: {
        CKObject* object = context
            ? context->GetObject(parameter.sourceId ? parameter.sourceId : parameter.id) : nullptr;
        const char* name = object ? object->GetName() : nullptr;
        const float parentWidth = name
            ? std::ceil(std::min(500.0f, static_cast<float>(std::strlen(name)) * 8.0f + 40.0f)
                        / SchematicGrid::SPACING) * SchematicGrid::SPACING
            : 180.0f;
        // 0x4E0F40 places the NAME text child at H, with the parent's width.
        // Reserve that offset too. This remains an estimate: Dev uses its
        // selected font and GetTextExtentPoint32A (0x4FEEA0), not byte count.
        return parentWidth + SchematicGrid::SPACING;
    }
    case PARAM_STYLE_VALUE:
        // 0x4E10C0: child offset H + capped value width 1000.
        return 1020.0f;
    case PARAM_STYLE_NAMEVALUE:
        // Name width <=500; value offset=nameWidth+1, value width <=1000.
        // Round the complete child extent (1501) up to the default grid.
        return 1520.0f;
    default:
        // Reserve one full cell around the narrower glyph; icon settings
        // may vary in Dev. This is not its exact ink/picking rectangle.
        return SchematicGrid::SPACING;
    }
}

} // namespace

Point GetPosition(const Parameter& parameter, bool scriptUi) {
    const int column = scriptUi ? std::max(parameter.hPos, SCRIPT_MINIMUM_COLUMN) : parameter.hPos;
    const int row = scriptUi ? std::max(parameter.vPos, 0) : parameter.vPos;
    return Point(static_cast<float>(column) * SchematicGrid::SPACING,
                 static_cast<float>(row) * SchematicGrid::SPACING);
}

Rect EstimateBounds(const Parameter& parameter, CKContext* context, bool scriptUi) {
    const Point position = GetPosition(parameter, scriptUi);
    return Rect(position.h, position.v, EstimateWidth(parameter, context), SchematicGrid::SPACING);
}

bool TryGetOperationBounds(const Operation& operation, Rect& bounds) {
    if (!std::isfinite(operation.hPos) || !std::isfinite(operation.vPos)) return false;
    const Rect result(operation.hPos, operation.vPos,
                      3.0f * SchematicGrid::SPACING, 3.0f * SchematicGrid::SPACING);
    if (!std::isfinite(result.Right()) || !std::isfinite(result.Bottom())) return false;
    bounds = result;
    return true;
}

} // namespace ParameterDisplayGeometry
