#pragma once

#include <string>
struct SDL_Window;
namespace vdev {
class EditorApplication;
bool RegisterFileInputs(EditorApplication &editor, SDL_Window *window, std::string &error);
bool RegisterResourceInputs(EditorApplication &editor, SDL_Window *window, std::string &error);
} // namespace vdev
