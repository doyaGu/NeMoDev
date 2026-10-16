#include "core/schematic/SchematicEdits.h"

#include <algorithm>
#include <cmath>

#include "core/schematic/BehaviorDisplayGeometry.h"
#include "core/schematic/ParameterDisplayGeometry.h"
#include "core/schematic/SchematicGrid.h"

namespace vdev {

namespace {

constexpr float GRID = SchematicGrid::SPACING;

float Snap(float value, float origin = 0.0f) {
    return SchematicGrid::SnapToGrid(value, GRID, origin);
}

float SnapDelta(float value) {
    return std::round(value / GRID) * GRID;
}

Operation *FindOperation(BehaviorData *record, CK_ID id) {
    if (!record) return nullptr;
    for (auto &operation : record->operations)
        if (operation.id == id) return &operation;
    return nullptr;
}

const Operation *FindOperation(const BehaviorData *record, CK_ID id) {
    return FindOperation(const_cast<BehaviorData *>(record), id);
}

std::vector<Parameter> *ParameterTable(BehaviorData *record, ElementKind kind) {
    if (!record) return nullptr;
    return kind == ElementKind::LocalParam ? &record->localParams : &record->sharedParams;
}

// Delta that lands the primary element on the grid.
Point SnappedDelta(const InterfaceData &original, const ElementRef &primary, const Point &raw) {
    const BehaviorData *owner = FindRecord(original, primary.owner);
    switch (primary.kind) {
    case ElementKind::Behavior:
        if (const BehaviorData *record = FindRecord(original, primary.object))
            return Point(Snap(record->rect.hPos + raw.h) - record->rect.hPos,
                         Snap(record->rect.vPos + raw.v) - record->rect.vPos);
        break;
    case ElementKind::Operation:
        if (const Operation *operation = FindOperation(owner, primary.object))
            return Point(Snap(operation->hPos + raw.h) - operation->hPos,
                         Snap(operation->vPos + raw.v) - operation->vPos);
        break;
    case ElementKind::Comment:
        if (owner && primary.index >= 0 && primary.index < static_cast<int>(owner->comments.size())) {
            const Comment &comment = owner->comments[primary.index];
            return Point(Snap(comment.hPos + raw.h) - comment.hPos, Snap(comment.vPos + raw.v) - comment.vPos);
        }
        break;
    case ElementKind::ControlPoint:
        if (owner && primary.index >= 0 && primary.index < static_cast<int>(owner->links.size())) {
            const Link &link = owner->links[primary.index];
            if (primary.sub >= 0 && primary.sub < static_cast<int>(link.points.size())) {
                const Point &point = link.points[primary.sub];
                return Point(Snap(point.h + raw.h) - point.h, Snap(point.v + raw.v) - point.v);
            }
        }
        break;
    case ElementKind::Start: {
        // Start stays against the Script header: it moves vertically only.
        const float top = original.rootBehavior.rect.vPos;
        const float v = top + original.header.vStartPos;
        return Point(0.0f, Snap(v + raw.v, top) - v);
    }
    default:
        break;
    }
    return Point(SnapDelta(raw.h), SnapDelta(raw.v));
}

void Translate(Point &point, const Point &from, const Point &delta) {
    point = Point(from.h + delta.h, from.v + delta.v);
}

} // namespace

BehaviorData *FindRecord(InterfaceData &data, CK_ID id) {
    if (data.rootBehavior.id == id) return &data.rootBehavior;
    for (auto &record : data.behaviors)
        if (record.id == id) return &record;
    return nullptr;
}

const BehaviorData *FindRecord(const InterfaceData &data, CK_ID id) {
    return FindRecord(const_cast<InterfaceData &>(data), id);
}

std::set<CK_ID> CollectSubtree(const InterfaceData &data, CK_ID graph) {
    std::set<CK_ID> result{graph};
    bool grown = true;
    while (grown) {
        grown = false;
        for (const auto &record : data.behaviors)
            if (record.parentId && result.count(record.parentId) && result.insert(record.id).second) grown = true;
    }
    return result;
}

void MoveElements(InterfaceData &data, const InterfaceData &original, bool rootIsScript,
                  const std::vector<ElementRef> &items, const ElementRef &primary, const Point &rawDelta) {
    const Point delta = SnappedDelta(original, primary, rawDelta);

    std::set<CK_ID> moved;
    for (const auto &item : items) {
        if (item.kind != ElementKind::Behavior) continue;
        const std::set<CK_ID> subtree = CollectSubtree(original, item.object);
        moved.insert(subtree.begin(), subtree.end());
    }
    moved.erase(data.rootBehavior.id);

    for (CK_ID id : moved) {
        BehaviorData *record = FindRecord(data, id);
        const BehaviorData *from = FindRecord(original, id);
        if (!record || !from) continue;
        record->rect.hPos = from->rect.hPos + delta.h;
        record->rect.vPos = from->rect.vPos + delta.v;
        // Elements stored by a moved graph are flattened Script pixels.
        for (size_t i = 0; i < record->operations.size() && i < from->operations.size(); ++i) {
            record->operations[i].hPos = from->operations[i].hPos + delta.h;
            record->operations[i].vPos = from->operations[i].vPos + delta.v;
        }
        for (size_t i = 0; i < record->comments.size() && i < from->comments.size(); ++i) {
            record->comments[i].hPos = from->comments[i].hPos + delta.h;
            record->comments[i].vPos = from->comments[i].vPos + delta.v;
        }
        for (size_t i = 0; i < record->links.size() && i < from->links.size(); ++i)
            for (size_t p = 0; p < record->links[i].points.size() && p < from->links[i].points.size(); ++p)
                Translate(record->links[i].points[p], from->links[i].points[p], delta);
    }

    for (const auto &item : items) {
        if (moved.count(item.owner) && item.kind != ElementKind::Start) continue;
        BehaviorData *owner = FindRecord(data, item.owner);
        const BehaviorData *fromOwner = FindRecord(original, item.owner);
        switch (item.kind) {
        case ElementKind::Operation: {
            Operation *operation = FindOperation(owner, item.object);
            const Operation *from = FindOperation(fromOwner, item.object);
            if (operation && from) {
                operation->hPos = from->hPos + delta.h;
                operation->vPos = from->vPos + delta.v;
            }
            break;
        }
        case ElementKind::Comment:
            if (owner && fromOwner && item.index >= 0 && item.index < static_cast<int>(owner->comments.size()) &&
                item.index < static_cast<int>(fromOwner->comments.size())) {
                owner->comments[item.index].hPos = fromOwner->comments[item.index].hPos + delta.h;
                owner->comments[item.index].vPos = fromOwner->comments[item.index].vPos + delta.v;
            }
            break;
        case ElementKind::Link:
        case ElementKind::ControlPoint:
            if (owner && fromOwner && item.index >= 0 && item.index < static_cast<int>(owner->links.size()) &&
                item.index < static_cast<int>(fromOwner->links.size())) {
                Link &link = owner->links[item.index];
                const Link &from = fromOwner->links[item.index];
                for (size_t p = 0; p < link.points.size() && p < from.points.size(); ++p)
                    if (item.kind == ElementKind::Link || static_cast<int>(p) == item.sub)
                        Translate(link.points[p], from.points[p], delta);
            }
            break;
        case ElementKind::LocalParam:
        case ElementKind::Shortcut: {
            std::vector<Parameter> *table = ParameterTable(owner, item.kind);
            const std::vector<Parameter> *fromTable =
                ParameterTable(const_cast<BehaviorData *>(fromOwner), item.kind);
            if (!table || !fromTable || item.index < 0 || item.index >= static_cast<int>(table->size()) ||
                item.index >= static_cast<int>(fromTable->size()))
                break;
            const Parameter &from = (*fromTable)[item.index];
            int column = static_cast<int>(std::lround(from.hPos + delta.h / GRID));
            int row = static_cast<int>(std::lround(from.vPos + delta.v / GRID));
            if (rootIsScript && owner == &data.rootBehavior) {
                column = std::max(column, ParameterDisplayGeometry::SCRIPT_MINIMUM_COLUMN);
                row = std::max(row, 0);
            }
            (*table)[item.index].hPos = column;
            (*table)[item.index].vPos = row;
            break;
        }
        case ElementKind::Start:
            // Attached to the header: only the vertical position changes.
            data.header.vStartPos = original.header.vStartPos + delta.v;
            break;
        default:
            break;
        }
    }
}

void SetBehaviorFolded(InterfaceData &data, CK_ID behavior, bool folded) {
    BehaviorData *record = FindRecord(data, behavior);
    if (!record) return;
    record->folded = folded;
    if (folded || BehaviorDisplayGeometry::HasExpandedSize(*record)) return;
    // Window-relative extent of the graph's direct contents.
    float right = record->rect.hSize, bottom = record->rect.vSize;
    auto extend = [&](float h, float v) {
        right = std::max(right, h - record->rect.hPos);
        bottom = std::max(bottom, v - record->rect.vPos);
    };
    for (const auto &child : data.behaviors)
        if (child.parentId == behavior) extend(child.rect.Right(), child.rect.Bottom());
    for (const auto &operation : record->operations) extend(operation.hPos + 60.0f, operation.vPos + 60.0f);
    for (const auto &comment : record->comments) extend(comment.hPos + comment.width, comment.vPos + comment.height);
    record->hExpandSize = std::ceil((right + GRID * 2.0f) / GRID) * GRID;
    record->vExpandSize = std::ceil((bottom + GRID * 2.0f) / GRID) * GRID;
}

void SetScriptOpen(InterfaceData &data, bool open) {
    data.rootBehavior.folded = !open;
}

} // namespace vdev
