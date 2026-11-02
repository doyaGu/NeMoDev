#pragma once
#include "application/EditorApplication.h"

namespace vdev {
enum class WorkspaceWindowId { Instance, Role, Named };
struct WorkspaceViewPlacement {
    std::string type, role, region;
    bool startup = false, pinned = false, keepOnReset = false;
    WorkspaceWindowId windowId = WorkspaceWindowId::Instance;
    std::string windowName;
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
// Persistent window identity is independent of process-local EditorView scopes.
struct WorkspaceViewState {
    uint64_t instance = 0;
    std::string type, provider, module, title, role, tool;
    bool open = true, pinned = false, single = false;
    uint32_t version = 0;
    std::vector<EditorField> settings;
    bool placed = false;
    float x = 0, y = 0, width = 0, height = 0;
    unsigned dock = 0;
};
struct WorkspaceState {
    WorkspaceConfiguration configuration;
    std::vector<WorkspaceViewState> views;
    std::string layout;
    uint64_t focused = 0;
};
bool ValidateWorkspaceState(const WorkspaceState &, std::string &error);
} // namespace vdev
