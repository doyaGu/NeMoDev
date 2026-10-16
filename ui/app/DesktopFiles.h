#pragma once
#include <string>
namespace vdev {
bool OpenDesktopFile(const std::string &path, std::string *error = nullptr);
bool OpenHelpTopic(const std::string &topic, std::string *error = nullptr);
}
