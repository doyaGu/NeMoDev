#include "core/runtime/Resources.h"

#include <SDL3/SDL.h>

#include <filesystem>

#include "core/runtime/VirtoolsRuntimeLayout.h"

namespace vdev {

std::string DataFilePath(const std::string &name) {
    const char *base = SDL_GetBasePath();
    if (!base) return {};
    return (std::filesystem::u8path(VirtoolsDataDirectory(base)) / std::filesystem::u8path(name)).u8string();
}

} // namespace vdev
