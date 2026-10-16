#include "core/schematic/LinkEndpointGeometry.h"
#include "core/schematic/BehaviorDisplayGeometry.h"
#include "core/schematic/ParameterDisplayGeometry.h"
#include "core/schematic/SchematicGrid.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace {

constexpr float GRID_SIZE = SchematicGrid::SPACING;
constexpr float HALF_GRID = GRID_SIZE * 0.5f;

const BehaviorData *FindBehavior(const InterfaceData &data, CK_ID id) {
    if (data.rootBehavior.id == id) return &data.rootBehavior;
    for (const auto &behavior : data.behaviors)
        if (behavior.id == id) return &behavior;
    return nullptr;
}

const Operation *FindOperation(const InterfaceData &data, CK_ID id) {
    for (const auto &operation : data.rootBehavior.operations)
        if (operation.id == id) return &operation;
    for (const auto &behavior : data.behaviors)
        for (const auto &operation : behavior.operations)
            if (operation.id == id) return &operation;
    return nullptr;
}

int ParameterDisplayColumn(const std::vector<int> &columns, int index, int columnCount) {
    // 0x4CBE90 keeps separate PIN/POUT occupancy arrays. Unsaved entries do
    // not reserve slots. Only a conflict rewrites the display column in Dev;
    // reproduce that computation here without mutating the saved table.
    std::unordered_set<int> occupied;
    int displayColumn = 0;
    for (size_t i = 0; i <= static_cast<size_t>(index); ++i) {
        displayColumn = columns[i];
        int checkedColumn = std::max(0, std::min(displayColumn, columnCount - 1));
        if (occupied.count(checkedColumn)) {
            while (checkedColumn > 0 && occupied.count(checkedColumn)) --checkedColumn;
            while (checkedColumn < columnCount - 1 && occupied.count(checkedColumn)) ++checkedColumn;
            displayColumn = checkedColumn;
        }
        occupied.insert(checkedColumn);
    }
    return displayColumn;
}

bool ResolveBehaviorParameter(const BehaviorData &behavior,
                              const LinkEndpoint &endpoint, Point &position) {
    const bool hasTarget = BehaviorDisplayGeometry::HasTargetParameter(behavior);
    if (endpoint.type == ENDPOINT_TARGET_PIN) {
        if (!hasTarget) return false;
        // UI identifier 96: Setup(0), position(-H,-V), bounds(0,.65V,H,V).
        // 0x4FE460/0x4FE490 -> 0x4CE420 -> 0x4F8010.
        position = Point(behavior.rect.hPos - HALF_GRID, behavior.rect.vPos - HALF_GRID);
        return true;
    }
    const bool input = endpoint.IsParameterInput();
    Rect content;
    if (!BehaviorDisplayGeometry::TryGetContentBounds(behavior, content)) return false;
    // Keep the previous estimate when an unfolded record has no expanded size.
    if (!behavior.folded && !BehaviorDisplayGeometry::HasExpandedSize(behavior)) {
        const float offset = hasTarget ? GRID_SIZE : HALF_GRID;
        position = Point(behavior.rect.hPos + offset + GRID_SIZE * endpoint.index,
                         input ? behavior.rect.vPos : behavior.rect.Bottom());
        return true;
    }
    const float height = content.Bottom();
    const float contentLeft = content.hPos;
    const float contentWidth = content.hSize;
    // Check before float-to-int conversion in the native grid formula.
    const float maxCoordinate = static_cast<float>(std::numeric_limits<int>::max() - 1024);
    const int columnCount = SchematicGrid::LayoutColumnCount(contentWidth);
    if (columnCount <= 0) return false;
    const auto &columns = input
        ? (behavior.folded ? behavior.outwardInputs : behavior.inwardInputs)
        : (behavior.folded ? behavior.outwardOutputs : behavior.inwardOutputs);
    const auto &rows = input
        ? (behavior.folded ? behavior.foldedInputRows : behavior.expandedInputRows)
        : (behavior.folded ? behavior.foldedOutputRows : behavior.expandedOutputRows);
    float h = GRID_SIZE * static_cast<float>(endpoint.index);
    float v = input ? -GRID_SIZE : height;
    bool orientation = false;
    if (!input && hasTarget) h += GRID_SIZE + contentLeft;
    if (static_cast<size_t>(endpoint.index) < columns.size()) {
        h = GRID_SIZE * static_cast<float>(ParameterDisplayColumn(columns, endpoint.index, columnCount));
        const int row = static_cast<size_t>(endpoint.index) < rows.size() ? rows[endpoint.index] : 0;
        v = row < 0 ? -GRID_SIZE : height;
        orientation = input ? row > 0 : row < 0;
    }
    if (!std::isfinite(h) || std::fabs(h) > maxCoordinate) return false;
    // Native SetPosition first aligns the wrapper, then 0x4F8010 aligns
    // the translated glyph bounds midpoint to a cell center.
    h = SchematicGrid::SnapToGrid(h, GRID_SIZE);
    v = SchematicGrid::SnapToGrid(v, GRID_SIZE);
    const bool upperGlyph = input ? orientation : !orientation;
    const float glyphMidpoint = (upperGlyph ? 0.175f : 0.825f) * GRID_SIZE;
    position = Point(behavior.rect.hPos + SchematicGrid::SnapToGridCellCenter(h + HALF_GRID),
                     behavior.rect.vPos + SchematicGrid::SnapToGridCellCenter(v + glyphMidpoint));
    return true;
}

bool ResolveBehaviorIO(const BehaviorData &behavior, const LinkEndpoint &endpoint,
                       Point &position) {
    const bool input = endpoint.IsBehaviorInput();
    const bool runtimeKnown = BehaviorDisplayGeometry::HasRuntimeState(behavior);
    const int count = input ? behavior.runtimeInputCount : behavior.runtimeOutputCount;
    if (runtimeKnown && (count == 0 || endpoint.index >= count)) return false;
    Rect content;
    if (!BehaviorDisplayGeometry::TryGetContentBounds(behavior, content)) return false;
    const bool compact = behavior.folded ||
        (runtimeKnown && (behavior.runtimeBehaviorFlags & CKBEHAVIOR_BUILDINGBLOCK));
    // Hand-built records without runtime data or expanded sizes retain estimates.
    if (!behavior.folded && (!runtimeKnown || !BehaviorDisplayGeometry::HasExpandedSize(behavior))) {
        float h = input ? behavior.rect.hPos : behavior.rect.Right();
        if (behavior.folded && !BehaviorDisplayGeometry::HasTargetParameter(behavior)) h += input ? -2.0f : 2.0f;
        position = Point(h, behavior.rect.vPos + (behavior.folded ? HALF_GRID : GRID_SIZE) +
                            static_cast<float>(endpoint.index) * GRID_SIZE);
        return true;
    }
    const float width = content.Right();
    const float height = content.Bottom();
    const float maxCoordinate = static_cast<float>(std::numeric_limits<int>::max() - 1024);
    float requestedY;
    if (compact) {
        requestedY = HALF_GRID + static_cast<float>(endpoint.index) * GRID_SIZE - 5.0f;
    } else {
        // 0x4CBE90 stores step and cursor as floats, but retains the unrounded
        // quotient for cursor initialization and each sum before subtracting5.
        const double quotient = static_cast<double>(height) / count;
        const float step = static_cast<float>(quotient);
        float cursor = static_cast<float>(-0.5 * quotient - HALF_GRID);
        double next = 0.0;
        for (int i = 0; i <= endpoint.index; ++i) {
            next = static_cast<double>(cursor) + step;
            cursor = static_cast<float>(next);
        }
        requestedY = static_cast<float>(next - 5.0);
    }
    if (!std::isfinite(requestedY) || std::fabs(requestedY) > maxCoordinate) return false;
    // Type12/13 SetPosition at0x4F7B10 stores only snapped Y, preserving X.
    // BIN requests contentLeft-H; bounds width/2+4 and link +H/2 give left-2.
    // BOUT requests right; bounds width/2+8 and link -H/2 give right+2.
    const float contentLeft = content.hPos;
    const float h = input ? contentLeft - 2.0f : width + 2.0f;
    position = Point(behavior.rect.hPos + h,
                     behavior.rect.vPos + SchematicGrid::SnapToGrid(requestedY, GRID_SIZE) + HALF_GRID);
    return true;
}

bool ResolvePosition(const InterfaceData &data,
                     const LinkEndpoint &endpoint, Point &position, bool rootIsScript) {
    if (endpoint.hasLegacyTypeCode) return false;
    if (endpoint.type == ENDPOINT_TARGET_PIN) {
        if (endpoint.index != -2) return false;
    } else if (endpoint.index < 0) {
        return false;
    }
    if (endpoint.IsStartBehaviorInput()) {
        // The stored id is the Start CKBehaviorIO, not the Script CKBehavior.
        // Runtime identity validation belongs to ValidateInterfaceData.
        if (!rootIsScript || endpoint.index != 0) return false;
        // LoadScriptHeader 0x4EFB90 adds Script top only to stored Start Y;
        // GetSourcePoint 0x4DD500 then adds (1.5H, 0.5V).
        position = Point(data.header.hStartPos + 1.5f * GRID_SIZE,
                         data.rootBehavior.rect.vPos + data.header.vStartPos + HALF_GRID);
        return true;
    }

    if (const BehaviorData *behavior = FindBehavior(data, endpoint.id)) {
        const float baseH = behavior->rect.hPos, baseV = behavior->rect.vPos;
        if (endpoint.IsParameterLocal() || endpoint.IsParameterOutputShortcut()) {
            const auto &parameters = endpoint.IsParameterLocal()
                ? behavior->localParams : behavior->sharedParams;
            if (endpoint.index >= static_cast<int>(parameters.size())) return false;
            const auto &parameter = parameters[endpoint.index];
            const Point display = ParameterDisplayGeometry::GetPosition(
                parameter, rootIsScript && behavior == &data.rootBehavior);
            // Categories 2/3 select 0x4D4180/0x4D4200, not 0x4F8010:
            // UI position + half spacing, independent of NAME text width.
            // GetSourcePoint/GetDestPoint add the owning window position.
            position = Point(baseH + display.h + HALF_GRID, baseV + display.v + HALF_GRID);
            return true;
        }
        if (endpoint.IsBehaviorInput() || endpoint.IsBehaviorOutput()) {
            // 0x4DD500/0x4DD750 accept UI type22, not Script type23, in
            // their BehaviorIO branch. Script control flow uses Start type26.
            if (rootIsScript && behavior == &data.rootBehavior) return false;
            return ResolveBehaviorIO(*behavior, endpoint, position);
        }
        if (endpoint.type == ENDPOINT_TARGET_PIN || endpoint.IsParameterInput() || endpoint.IsParameterOutput()) {
            // Script type23 inherits type4 but has no type22 parameter glyph
            // layout. Vtable 0x60E388 +200/+204 -> 0x4FD140 writes (0,0);
            // the parameter-link helper adds the Script window position.
            if (rootIsScript && behavior == &data.rootBehavior) {
                position = Point(baseH, baseV);
                return true;
            }
            return ResolveBehaviorParameter(*behavior, endpoint, position);
        }
        return false;
    }

    const Operation *operation = FindOperation(data, endpoint.id);
    if (!operation) return false;
    if (endpoint.IsParameterInput() && endpoint.index < 2) {
        // Actual operation UI type 20 / vtable 0x60E028, NOT local type 9.
        // 0x4F5660 places PINs at (0,0)/(2H,0), POUT at (H,2V).
        // 0x4F5F90 / 0x4CE480 -> 0x4F8010 bounds center -> 20px cell center.
        position = Point(operation->hPos + HALF_GRID + endpoint.index * 2.0f * GRID_SIZE,
                         operation->vPos + HALF_GRID);
        return true;
    }
    if (endpoint.IsParameterOutput() && endpoint.index == 0) {
        position = Point(operation->hPos + GRID_SIZE + HALF_GRID,
                         operation->vPos + 2.0f * GRID_SIZE + HALF_GRID);
        return true;
    }
    return false;
}

} // namespace

bool TryGetLinkEndpointPosition(
    const InterfaceData &data, const LinkEndpoint &endpoint, Point &position,
    bool rootIsScript) {
    Point resolved;
    if (!ResolvePosition(data, endpoint, resolved, rootIsScript) ||
        !std::isfinite(resolved.h) || !std::isfinite(resolved.v)) return false;
    position = resolved; // Failed lookup leaves the caller's position unchanged.
    return true;
}
