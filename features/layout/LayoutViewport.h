#pragma once
#include "editor/Editor.h"
#include <functional>
#include <string>
#include <vector>

namespace vdev {
class LayoutView;
// The window and its textures stay with the renderer. These operations are
// explicit: reading pixels is reserved for snapshots, never for presenting.
struct LayoutScreenMode {
    int driver = 0, width = 0, height = 0, bitsPerPixel = 0, refreshRate = 0;
};
class LayoutViewport {
  public:
    virtual ~LayoutViewport() = default;
    virtual bool CapturePixels(std::vector<unsigned char> &, int &width, int &height) = 0;
    virtual bool EnterFullScreen(const LayoutScreenMode &, std::string &error) = 0;
    virtual bool ExitFullScreen(bool activateEditor, std::string &error) = 0;
};
// One registered Layout and its native view. The window owns both; command
// callbacks resolve the requesting scope rather than changing a global view.
struct LayoutTarget {
    EditorView id;
    LayoutView &view;
    LayoutViewport *viewport = nullptr;
};
using LayoutTargetLookup = std::function<LayoutTarget(EditorView)>;
} // namespace vdev
