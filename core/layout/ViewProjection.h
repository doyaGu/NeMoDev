#pragma once

#include "CKAll.h"

namespace vdev {

// A camera seen through a view rectangle (the render context's, letterboxed
// when it keeps the camera's aspect ratio): world points to the view's
// pixels and back, as CKRenderContext::Transform and the 3D Layout's picking
// do, without a render context. CK's field of view is horizontal: a pixel
// row is as wide as the view, y included.
struct ViewProjection {
    VxMatrix world;          // the camera's
    bool perspective = true;
    float fov = 0.5f;        // radians
    float orthoZoom = 1.0f;  // orthographic: view units per world unit
    float front = 1.0f, back = 4000.0f;
    VxRect view;             // pixels

    static ViewProjection Of(CKCamera *camera, const VxRect &view);

    // The ray through pixel (x, y): from the camera (orthographic: from the
    // point on its plane), one front plane long. False for an empty view.
    bool Ray(float x, float y, VxVector &origin, VxVector &direction) const;
    // A world point's pixel; z is its depth, in (0, 1) between the front and
    // back planes (0 or less: in front of the front plane or behind the camera).
    VxVector Project(const VxVector &point) const;
    // The scale that keeps an icon at position its own size (Dev: (101 -
    // zoom) * 2 * 18 / tan(fov / 2) / 70 units away it has its own size;
    // orthographic: 250 * the camera's zoom). zoom: Dev's icon zoom (0-100).
    float ConstantScale(const VxVector &position, float zoom) const;
};

} // namespace vdev
