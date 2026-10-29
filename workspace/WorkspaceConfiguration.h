#pragma once
#include "application/EditorApplication.h"

namespace vdev {
struct WorkspaceViewPlacement {
    std::string type, role, region;
    bool startup = false, pinned = false, keepOnReset = false;
};
enum class WorkspaceDirection { Left, Right, Up, Down };
struct WorkspaceSplit {
    std::string source, first, remainder;
    WorkspaceDirection direction = WorkspaceDirection::Left;
    float fraction = .5f;
};
struct WorkspaceRegion {
    std::string id, anchorRole, anchorWindow;
};
// Only the remaining legacy windows use names. Registered views use type/role.
struct WorkspaceLegacyWindow {
    std::string name, region;
    bool contains = false, keepOnReset = false;
};
struct WorkspaceConfiguration {
    EditorConfiguration contributions;
    std::vector<WorkspaceViewPlacement> views;
    std::vector<WorkspaceSplit> splits;
    std::vector<WorkspaceRegion> regions;
    std::vector<WorkspaceLegacyWindow> legacyWindows;
};
} // namespace vdev
