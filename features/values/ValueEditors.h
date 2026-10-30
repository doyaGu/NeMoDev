#pragma once
#include "application/EditorApplication.h"
namespace vdev {
bool ValueTargetChanged(const EditorValueInput &);
bool RegisterValueEditors(EditorApplication &, std::string &error);
}
