#pragma once

#include <vector>

#include "CKAll.h"
#include "core/layout/ViewProjection.h"

namespace vdev {

// The 3D Layout's gestures' arithmetic (LayoutTools), without CK objects:
// what a mouse move, a time or a snap makes of positions, angles and
// factors. The gestures read the objects, call these and write the objects.
// Axes are Dev's constraint bits (X 1, Y 2, Z 4); a move dx, dy is the
// mouse's travel over the view's width and height.

// --------------------------------------------------------------- vectors

// vector turned by angle (radians) about axis.
VxVector Rotated(const VxVector &axis, float angle, const VxVector &vector);
// value to the nearest multiple of step (step 0 or less: value).
float Snapped(float value, float step);

// --------------------------------------------------------------- 3D transformations

// Translate's plane (Dev.exe sub_491B60), its normal: the plane holds the
// constrained axes of the referential (axes: its X, Y and Z in the world); a
// single axis takes as its second the one seen longest on the screen
// (camera: the view camera's world matrix, null for none); none or every
// axis, the view's plane.
VxVector TranslatePlaneNormal(int constrained, const VxVector axes[3], const VxMatrix *camera);
// Sticky Translate: the move from grab to where the mouse's ray (origin,
// direction) meets the plane through grab; false when the ray is parallel to
// it or the plane is behind the camera.
bool StickyTranslate(const VxVector &normal, const VxVector &grab, const VxVector &origin, const VxVector &direction,
                     VxVector &delta);
// Translate out of Sticky Mode: the travel times the step along the
// constrained axes (in the referential): dx goes along X, dy along the other.
VxVector SlidingTranslate(int constrained, float dx, float dy);
// A move in the referential (local) kept on the constrained axes, in the world.
VxVector ConstrainedMove(int constrained, const VxVector &local, const VxVector axes[3]);
// A world move in the referential's axes.
VxVector InAxes(const VxVector &world, const VxVector axes[3]);
// Rotate (sub_4955C0): the two angles (radians) a move adds; step: degrees
// per view.
void RotateAngles(int constrained, float dx, float dy, float step, float &first, float &second);
// Scale (sub_497770): the factor of a move, (up - right) * step per view;
// dx, dy as the mouse went (right, down).
float ScaleStep(float dx, float dy, float step);
// The factors of a move's step f: 0 uniform, 1 keeping the volume (the other
// axes give back what the constrained ones take), 2 the constrained axes.
VxVector ScaleFactors(int mode, int constrained, float f);

// --------------------------------------------------------------- snap

// A world point on the screen, its depth dropped (Dev compares the points on
// the screen).
VxVector ScreenPoint(const ViewProjection &projection, const VxVector &world);
// The corner of the World Step cell holding at nearest to it on the screen.
VxVector NearestCellCorner(const VxVector &at, const VxVector &step, const ViewProjection &projection);
// The face's vertex (world, flat: on the screen) nearest point on the screen,
// within radius pixels.
bool SnapToVertex(const VxVector world[3], const VxVector flat[3], const VxVector &point, float radius,
                  VxVector &target);
// The point of the face's edge nearest point on the screen (Dev: the foot's
// distance from the edge's start over the edge's length, its length not its
// sign, not kept on the edge), within radius pixels.
bool SnapToEdge(const VxVector world[3], const VxVector flat[3], const VxVector &point, float radius,
                VxVector &target);
// The move corrected so that at goes to target: only the axes moving.
VxVector SnapCorrection(const VxVector &delta, const VxVector &at, const VxVector &target);

// --------------------------------------------------------------- cameras

// A step per view, or per second at constant speed.
inline float Speed(float step, bool constant, float seconds) { return step * (constant ? seconds : 1.0f); }
// The orthographic zoom after a Zoom move (kept within 1e-6 and 1e6).
float NextOrthoZoom(float zoom, float speed, float dy);
// Pan (sub_4942A0): the move in the camera's referential; faster: a
// perspective camera with Shift, by its distance from the origin.
VxVector PanMove(float dx, float dy, float speed, bool faster, float distance);
// Orbit (sub_4936C0): offset turned by pitch about xAxis, then by yaw about
// the world's Y, from pivot.
VxVector Orbited(const VxVector &pivot, const VxVector &xAxis, float pitch, float yaw, const VxVector &offset);
// Field of View (sub_48D750): the new value; false out of [0.01, 3).
bool NextFov(float fov, float dx, float dy, float speed, float &next);

// --------------------------------------------------------------- 2D entities

// Translate's edges snap (Dev.exe sub_491060): the offsets of each axis
// (0 X, 1 Y) that put an edge of rect on an edge of a target within
// influence pixels (X: a target beside the rectangle; Y: above or below);
// found[axis] when one does.
void SnapEdges(const VxRect &rect, const std::vector<VxRect> &targets, int influence, int offset[2], bool found[2]);
// Scale: the factors of the travel dx, dy (pixels) for a size, at least one
// pixel of it; snapped to the Scaling Percentage when given.
Vx2DVector ScaleFactors2D(int constrained, float dx, float dy, const Vx2DVector &size, float snapPercent);

} // namespace vdev
