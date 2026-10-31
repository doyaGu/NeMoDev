#pragma once
#include <string>
struct SDL_Window;
namespace vdev {
class EditorApplication;
bool RegisterEnvironmentInputs(EditorApplication &, SDL_Window *, std::string &error);
} // namespace vdev
