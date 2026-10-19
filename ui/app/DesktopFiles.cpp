#include "ui/app/DesktopFiles.h"
#include "core/runtime/Resources.h"
#include <SDL3/SDL.h>
#include <filesystem>

namespace vdev {
bool OpenDesktopFile(const std::string &path, std::string *error) {
    std::error_code failure;
    const auto file = std::filesystem::absolute(std::filesystem::u8path(path), failure);
    if (failure || !std::filesystem::exists(file, failure)) {
        if (error) *error = "File not found: " + path;
        return false;
    }
    const std::string name = file.generic_u8string();
    std::string url = "file://";
    if (name.empty() || name.front() != '/') url += '/';
    const char *hex = "0123456789ABCDEF";
    for (unsigned char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') url += char(c);
        else { url += '%'; url += hex[c >> 4]; url += hex[c & 15]; }
    }
    if (SDL_OpenURL(url.c_str())) return true;
    if (error) *error = SDL_GetError();
    return false;
}
bool HelpAvailable() {
    std::error_code failure;
    return std::filesystem::is_regular_file(std::filesystem::u8path(DataFilePath("help/help.ndh")), failure);
}
}
