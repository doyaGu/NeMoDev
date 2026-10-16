#pragma once

// SchematicCanvas.cpp and SchematicInput.cpp's own constants and helpers.

#include "core/schematic/SchematicModel.h"

namespace vdev {
namespace canvas {

constexpr float MIN_ZOOM = 0.1f;
constexpr float MAX_ZOOM = 4.0f;
constexpr float DRAG_THRESHOLD = 4.0f;

inline bool Movable(ElementKind kind) {
    switch (kind) {
    case ElementKind::Behavior:
    case ElementKind::Operation:
    case ElementKind::LocalParam:
    case ElementKind::Shortcut:
    case ElementKind::Comment:
    case ElementKind::ControlPoint:
    case ElementKind::Start:
    case ElementKind::Link:
        return true;
    default:
        return false;
    }
}

} // namespace canvas
} // namespace vdev
