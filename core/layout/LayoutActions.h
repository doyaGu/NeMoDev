#pragma once
namespace vdev {
enum class CameraAction { Dolly, Zoom, Pan, Orbit, OrbitCamera, Roll, Fov };
enum class TransformAction { Translate, Rotate, Scale };
const char *CameraActionText(CameraAction, bool active, bool targeted, bool aroundSelection);
} // namespace vdev
