#include "core/layout/GestureMath.h"

#include <algorithm>
#include <cmath>

namespace vdev {

VxVector Rotated(const VxVector &axis, float angle, const VxVector &vector) {
    VxMatrix rotation;
    Vx3DMatrixFromRotation(rotation, axis, angle);
    VxVector result;
    Vx3DRotateVector(&result, rotation, &vector);
    return result;
}

float Snapped(float value, float step) {
    return step > 0.0f ? std::floor(value / step + 0.5f) * step : value;
}

// --------------------------------------------------------------- 3D transformations

VxVector TranslatePlaneNormal(int constrained, const VxVector axes[3], const VxMatrix *camera) {
    VxMatrix inverse;
    if (camera) Vx3DInverseMatrix(inverse, *camera);
    // An axis's length on the screen (CK3dEntity::InverseTransformVector).
    auto onScreen = [&](const VxVector &axis) {
        VxVector local(axis);
        if (camera) Vx3DRotateVector(&local, inverse, &axis);
        return std::fabs(local.x) + std::fabs(local.y);
    };
    VxVector a = axes[0], b = axes[1], normal(0.0f, 0.0f, 1.0f);
    switch (constrained) {
    case 1: a = axes[0], b = onScreen(axes[2]) > onScreen(axes[1]) ? axes[2] : axes[1]; break;
    case 2: a = axes[1], b = onScreen(axes[2]) > onScreen(axes[0]) ? axes[2] : axes[0]; break;
    case 4: a = axes[2], b = onScreen(axes[1]) > onScreen(axes[0]) ? axes[1] : axes[0]; break;
    case 3: a = axes[0], b = axes[1]; break;
    case 5: a = axes[0], b = axes[2]; break;
    case 6: a = axes[1], b = axes[2]; break;
    default:
        // No or every axis: the view's plane (the camera's direction).
        if (camera) normal = VxVector((*camera)[2][0], (*camera)[2][1], (*camera)[2][2]);
        break;
    }
    if (constrained >= 1 && constrained <= 6) normal = CrossProduct(a, b);
    if (SquareMagnitude(normal) > 0.0f) normal.Normalize();
    return normal;
}

bool StickyTranslate(const VxVector &normal, const VxVector &grab, const VxVector &origin, const VxVector &direction,
                     VxVector &delta) {
    const float along = DotProduct(normal, direction);
    if (along == 0.0f) return false;
    const float t = DotProduct(normal, grab - origin) / along;
    if (t <= 0.0f) return false; // Dev: the plane is behind the camera
    delta = origin + direction * t - grab;
    return true;
}

VxVector SlidingTranslate(int constrained, float dx, float dy) {
    VxVector local(0.0f, 0.0f, 0.0f);
    switch (constrained) {
    case 1: local.x = dx; break;
    case 2: local.y = dy; break;
    case 3: local.x = dx, local.y = dy; break;
    case 4: local.z = dy; break;
    case 5: local.x = dx, local.z = dy; break;
    case 6: local.y = dy, local.z = dx; break;
    default: break;
    }
    return local;
}

VxVector ConstrainedMove(int constrained, const VxVector &local, const VxVector axes[3]) {
    VxVector kept = local;
    if (!(constrained & 1)) kept.x = 0.0f;
    if (!(constrained & 2)) kept.y = 0.0f;
    if (!(constrained & 4)) kept.z = 0.0f;
    return axes[0] * kept.x + axes[1] * kept.y + axes[2] * kept.z;
}

VxVector InAxes(const VxVector &world, const VxVector axes[3]) {
    return VxVector(DotProduct(world, axes[0]), DotProduct(world, axes[1]), DotProduct(world, axes[2]));
}

void RotateAngles(int constrained, float dx, float dy, float step, float &first, float &second) {
    first = second = 0.0f;
    switch (constrained) {
    case 1: case 2: case 4: first = (dy + dx) * step; break;
    case 3: case 5: first = step * dx, second = step * dy; break;
    case 6: first = step * dy, second = step * dx; break;
    default: first = dx * step; break;
    }
    first *= 0.017453292f;
    second *= 0.017453292f;
}

float ScaleStep(float dx, float dy, float step) {
    return -dy * step - -dx * step + 1.0f;
}

VxVector ScaleFactors(int mode, int constrained, float f) {
    if (mode == 0) return VxVector(f, f, f);
    if (mode == 1) {
        if (f == 0.0f) return VxVector(1.0f, 1.0f, 1.0f);
        switch (constrained) {
        case 1: return VxVector(f, 1.0f / f, 1.0f / f);
        case 2: return VxVector(1.0f / f, f, 1.0f / f);
        case 4: return VxVector(1.0f / f, 1.0f / f, f);
        case 3: return VxVector(f, f, 1.0f / (f * f));
        case 5: return VxVector(f, 1.0f / (f * f), f);
        case 6: return VxVector(1.0f / (f * f), f, f);
        default: return VxVector(1.0f, 1.0f, 1.0f);
        }
    }
    return VxVector((constrained & 1) ? f : 1.0f, (constrained & 2) ? f : 1.0f, (constrained & 4) ? f : 1.0f);
}

// --------------------------------------------------------------- snap

VxVector ScreenPoint(const ViewProjection &projection, const VxVector &world) {
    VxVector out = projection.Project(world);
    out.z = 0.0f;
    return out;
}

VxVector NearestCellCorner(const VxVector &at, const VxVector &worldStep, const ViewProjection &projection) {
    const VxVector &w = worldStep;
    const VxVector step(w.x != 0.0f ? w.x : 1.0f, w.y != 0.0f ? w.y : 1.0f, w.z != 0.0f ? w.z : 1.0f);
    const VxVector cell(std::floor(at.x / step.x) * step.x, std::floor(at.y / step.y) * step.y,
                        std::floor(at.z / step.z) * step.z);
    const VxVector point = ScreenPoint(projection, at);
    VxVector target = at;
    float best = -1.0f;
    for (int corner = 0; corner < 8; ++corner) {
        const VxVector c(cell.x + ((corner & 1) ? step.x : 0.0f), cell.y + ((corner & 2) ? step.y : 0.0f),
                         cell.z + ((corner & 4) ? step.z : 0.0f));
        const float distance = Magnitude(ScreenPoint(projection, c) - point);
        if (best < 0.0f || distance < best) best = distance, target = c;
    }
    return target;
}

bool SnapToVertex(const VxVector world[3], const VxVector flat[3], const VxVector &point, float radius,
                  VxVector &target) {
    int nearest = 0;
    for (int k = 1; k < 3; ++k)
        if (Magnitude(flat[k] - point) < Magnitude(flat[nearest] - point)) nearest = k;
    if (Magnitude(flat[nearest] - point) > radius) return false;
    target = world[nearest];
    return true;
}

bool SnapToEdge(const VxVector world[3], const VxVector flat[3], const VxVector &point, float radius,
                VxVector &target) {
    int nearest = 0;
    float best = 0.0f, ratio = 0.0f;
    for (int k = 0; k < 3; ++k) {
        const VxVector edge = flat[(k + 1) % 3] - flat[k];
        const float length = Magnitude(edge);
        const VxVector along = length > 0.0f ? edge / length : VxVector(0.0f, 0.0f, 0.0f);
        const VxVector foot = along * DotProduct(along, point - flat[k]);
        const float distance = Magnitude(flat[k] + foot - point);
        if (k == 0 || distance < best) {
            best = distance;
            nearest = k;
            ratio = Magnitude(foot) / (length > 0.0f ? length : 1.0f);
        }
    }
    if (best > radius) return false;
    target = world[nearest] + (world[(nearest + 1) % 3] - world[nearest]) * ratio;
    return true;
}

VxVector SnapCorrection(const VxVector &delta, const VxVector &at, const VxVector &target) {
    const VxVector correction = target - at;
    VxVector result = delta;
    if (result.x != 0.0f) result.x += correction.x;
    if (result.y != 0.0f) result.y += correction.y;
    if (result.z != 0.0f) result.z += correction.z;
    return result;
}

// --------------------------------------------------------------- cameras

float NextOrthoZoom(float zoom, float speed, float dy) {
    return std::clamp(zoom * (speed * dy + 1.0f), 0.000001f, 1000000.0f);
}

VxVector PanMove(float dx, float dy, float speed, bool faster, float distance) {
    VxVector local(dx * speed, -dy * speed, 0.0f);
    if (faster) local *= distance * 0.014285714f;
    return local;
}

VxVector Orbited(const VxVector &pivot, const VxVector &xAxis, float pitch, float yaw, const VxVector &offset) {
    const VxVector axisY(0.0f, 1.0f, 0.0f);
    return pivot + Rotated(axisY, yaw, Rotated(xAxis, pitch, offset));
}

bool NextFov(float fov, float dx, float dy, float speed, float &next) {
    const float value = (dx - dy) * speed + fov;
    if (!(value < 3.0f && value >= 0.01f)) return false;
    next = value;
    return true;
}

// --------------------------------------------------------------- 2D entities

void SnapEdges(const VxRect &rect, const std::vector<VxRect> &targets, int influence, int offset[2], bool found[2]) {
    offset[0] = offset[1] = influence + 1;
    found[0] = found[1] = false;
    for (const VxRect &target : targets) {
        // X: the target beside the rectangle; Y: above or below it.
        for (int axis = 0; axis < 2; ++axis) {
            const bool overlap = axis == 0 ? !(target.bottom < rect.top || target.top > rect.bottom)
                                           : !(target.right < rect.left || target.left > rect.right);
            if (!overlap) continue;
            const float ours[2] = {axis == 0 ? rect.left : rect.top, axis == 0 ? rect.right : rect.bottom};
            const float theirs[2] = {axis == 0 ? target.left : target.top, axis == 0 ? target.right : target.bottom};
            // Dev's order: our start against their start and end, our end against their end and start.
            const float pairs[4][2] = {{ours[0], theirs[0]}, {ours[0], theirs[1]}, {ours[1], theirs[1]}, {ours[1], theirs[0]}};
            for (const auto &pair : pairs) {
                if (std::fabs(pair[0] - pair[1]) > static_cast<float>(influence)) continue;
                const int moved = static_cast<int>(pair[1] - pair[0]);
                if (moved < offset[axis]) offset[axis] = moved;
                found[axis] = true;
                break;
            }
        }
    }
}

Vx2DVector ScaleFactors2D(int constrained, float dx, float dy, const Vx2DVector &size, float snapPercent) {
    const float width = std::max(1.0f, size.x), height = std::max(1.0f, size.y);
    float fx = (constrained & 1) ? 1.0f + dx / width : 1.0f, fy = (constrained & 2) ? 1.0f + dy / height : 1.0f;
    fx = std::max(fx, 1.0f / width);
    fy = std::max(fy, 1.0f / height);
    if (snapPercent > 0.0f) {
        const float step = snapPercent * 0.01f;
        fx = std::floor(fx / step + 0.5f) * step;
        fy = std::floor(fy / step + 0.5f) * step;
    }
    return Vx2DVector(fx, fy);
}

} // namespace vdev
