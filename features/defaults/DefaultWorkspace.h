#pragma once
#include "workspace/WorkspaceConfiguration.h"
namespace vdev {
WorkspaceConfiguration DefaultWorkspaceConfiguration();
WorkspaceConfiguration AvailableWorkspaceConfiguration(WorkspaceConfiguration, const EditorApplication &);
bool RegisterDefaultWorkspace(EditorApplication &, std::string &error);
} // namespace vdev
