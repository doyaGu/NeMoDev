#include "core/layout/ViewProjection.h"

#include <algorithm>
#include <cmath>

namespace vdev {

namespace {

VxVector Times(const VxMatrix &matrix, const VxVector &point) {
    VxVector result;
    Vx3DMultiplyMatrixVector(&result, matrix, &point);
    return result;
}

VxVector Local(const VxMatrix &world, const VxVector &point) {
    VxMatrix inverse;
    Vx3DInverseMatrix(inverse, world);
    return Times(inverse, point);
}

} // namespace

ViewProjection ViewProjection::Of(CKCamera *camera, const VxRect &view) {
    ViewProjection projection;
    projection.view = view;
    if (!camera) return projection;
    projection.world = camera->GetWorldMatrix();
    projection.perspective = camera->GetProjectionType() == CK_PERSPECTIVEPROJECTION;
    projection.fov = camera->GetFov();
    projection.orthoZoom = camera->GetOrthographicZoom();
    projection.front = camera->GetFrontPlane();
    projection.back = camera->GetBackPlane();
    return projection;
}

bool ViewProjection::Ray(float x, float y, VxVector &origin, VxVector &direction) const {
    const float width = std::max(1.0f, view.GetWidth()), height = std::max(1.0f, view.GetHeight());
    const float sx = (2.0f * (x - view.left) - width) / width, sy = (height - 2.0f * (y - view.top)) / width;
    const float nearPlane = std::max(0.001f, front);
    VxVector nearLocal, farLocal;
    if (perspective) {
        const float t = std::tan(fov * 0.5f) * nearPlane;
        nearLocal = VxVector(0.0f, 0.0f, 0.0f);
        farLocal = VxVector(sx * t, sy * t, nearPlane);
    } else {
        const float zoom = orthoZoom > 0.0f ? 1.0f / orthoZoom : 1.0f;
        nearLocal = VxVector(sx * zoom, sy * zoom, 0.0f);
        farLocal = VxVector(sx * zoom, sy * zoom, nearPlane);
    }
    origin = Times(world, nearLocal);
    direction = Times(world, farLocal) - origin;
    return SquareMagnitude(direction) > 0.0f;
}

VxVector ViewProjection::Project(const VxVector &point) const {
    const VxVector local = Local(world, point);
    const float width = view.GetWidth(), height = view.GetHeight();
    float sx = 0.0f, sy = 0.0f, z = 0.0f;
    if (perspective) {
        if (local.z <= 0.0f) return VxVector(view.left, view.top, -1.0f); // behind the camera
        const float t = std::tan(fov * 0.5f);
        sx = local.x / (local.z * t);
        sy = local.y / (local.z * t);
        z = back > front ? back / (back - front) * (1.0f - front / local.z) : 0.0f;
    } else {
        sx = local.x * orthoZoom;
        sy = local.y * orthoZoom;
        z = back > front ? (local.z - front) / (back - front) : 0.0f;
    }
    return VxVector(view.left + (1.0f + sx) * width * 0.5f, view.top + (height - sy * width) * 0.5f, z);
}

float ViewProjection::ConstantScale(const VxVector &position, float zoom) const {
    const float k = perspective ? (101.0f - zoom) * 2.0f * 0.014285714f * 18.0f / std::tan(fov * 0.5f)
                                : (101.0f - zoom) * 2.0f * 250.0f * orthoZoom;
    return std::max(0.001f, k > 0.0f ? Magnitude(Local(world, position)) / k : 1.0f);
}

} // namespace vdev
