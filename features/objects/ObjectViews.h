#pragma once
#include "application/EditorApplication.h"
namespace vdev {
bool RegisterObjectViews(EditorApplication &editor, std::string &error);
EditorConfiguration DefaultObjectConfiguration();
} // namespace vdev
