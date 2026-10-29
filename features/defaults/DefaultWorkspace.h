#pragma once
#include "workspace/WorkspaceConfiguration.h"
namespace vdev {
WorkspaceConfiguration DefaultWorkspaceConfiguration();
bool RegisterDefaultWorkspace(EditorApplication &, std::string &error);
} // namespace vdev
