#pragma once
#include <string>
struct SDL_Window;
namespace vdev {
class EditorApplication;
bool RegisterOutputInputs(EditorApplication &editor, SDL_Window *window, std::string &error);
} // namespace vdev
