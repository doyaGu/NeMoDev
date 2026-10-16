#pragma once

#include <vector>

#include "core/schematic/SchematicModel.h"

namespace vdev {

// Picking priority (library choice, not recovered Dev behavior): pins,
// link control points, operations, parameters, behaviors (deepest first;
// an expanded graph only by its frame band), comments, links, Start, Script
// header. tolerance is in Script-local pixels.
ElementRef PickElement(const SchematicModel &model, const Point &point, float tolerance);

// Elements whose bounds lie entirely inside rect (Ctrl+drag selection).
// strict: elements entirely inside (Dev "Strict Multi-Selection"); otherwise
// every element touching the rectangle.
std::vector<ElementRef> ElementsInRect(const SchematicModel &model, const Rect &rect, bool strict = true);

// The expanded graph drawn last (on top) whose frame contains the point, or
// null (the drawing order of SchematicRenderer: a graph, then its expanded
// children over it).
const BehaviorItem *TopGraphFrameAt(const SchematicModel &model, const Point &point);

// Distance from point to a polyline.
float DistanceToPath(const std::vector<Point> &path, const Point &point);


} // namespace vdev
