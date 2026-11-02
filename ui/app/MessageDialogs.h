#pragma once

#include <string>

namespace vdev {
class EditorApplication;
bool RegisterMessageDialogs(EditorApplication &editor, std::string &error);
} // namespace vdev
