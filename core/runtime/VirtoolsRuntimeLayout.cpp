#include "core/runtime/VirtoolsRuntimeLayout.h"

#include <cctype>
#include <algorithm>
#include <filesystem>
#include <vector>

#include "CKAll.h"

namespace {

bool IsDirectory(const std::string &path) {
    std::error_code error;
    return std::filesystem::is_directory(std::filesystem::u8path(path), error);
}

std::string WithoutTrailingSeparator(std::string path) {
    while (path.size() > 1 && (path.back() == '\\' || path.back() == '/')) path.pop_back();
    return path;
}

std::string Lower(std::string text) {
    for (char &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool IsFile(const std::string &path) {
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::u8path(path), error);
}

const char *const IGNORED_PLUGINS[] = {"bmlplus.dll", "angelscript.dll"};

} // namespace

const char *const VIRTOOLS_PLUGIN_FOLDERS[4] = {"RenderEngines", "Managers", "BuildingBlocks", "Plugins"};

std::string VirtoolsRuntimeDirectory(const std::string &path) {
    const std::string directory = WithoutTrailingSeparator(path.empty() ? "." : path);
    if (!IsFile(directory + "/" VIRTOOLS_CK2_FILENAME) && IsFile(directory + "/Bin/" VIRTOOLS_CK2_FILENAME)) return directory + "/Bin";
    return directory;
}

std::string VirtoolsPluginRoot(const std::string &runtimeDirectory) {
    const std::string directory = WithoutTrailingSeparator(runtimeDirectory.empty() ? "." : runtimeDirectory);
    if (IsDirectory(directory + "/RenderEngines")) return directory;
    const std::string parent = std::filesystem::u8path(directory).parent_path().u8string();
    if (IsDirectory(parent + "/RenderEngines")) return parent;
    return directory;
}

bool VirtoolsIsIgnoredPlugin(const std::string &fileName) {
    const size_t slash = fileName.find_last_of("\\/");
    const std::string name = Lower(slash == std::string::npos ? fileName : fileName.substr(slash + 1));
    for (const char *ignored : IGNORED_PLUGINS)
        if (name == ignored) return true;
    return false;
}

int VirtoolsParsePlugins(CKPluginManager *plugins, const std::string &directory, std::vector<std::string> *skipped) {
    if (!plugins || !IsDirectory(directory)) return 0;
    std::string path = directory;
    VxAddLibrarySearchPath(&path[0]);
    int registered = 0;
    std::error_code error;
    std::vector<std::string> files;
    for (std::filesystem::directory_iterator it(std::filesystem::u8path(path), error), end;
         !error && it != end; it.increment(error)) {
        const auto extension = Lower(it->path().extension().u8string());
#ifdef _WIN32
        const bool plugin = extension == ".dll";
#elif defined(__APPLE__)
        const bool plugin = extension == ".dylib" || extension == ".so";
#else
        const bool plugin = extension == ".so";
#endif
        if (plugin && IsFile(it->path().u8string())) files.push_back(it->path().u8string());
    }
    std::sort(files.begin(), files.end());
    for (std::string &file : files) {
        if (VirtoolsIsIgnoredPlugin(file)) {
            if (skipped) skipped->push_back(file);
            continue;
        }
        if (plugins->RegisterPlugin(file.data()) == CK_OK) ++registered;
    }
    return registered;
}

int VirtoolsParseRuntimePlugins(CKPluginManager *plugins, const std::string &runtimeDirectory,
                                std::vector<std::string> *skipped) {
    const std::string root = VirtoolsPluginRoot(runtimeDirectory);
    int registered = 0;
    for (const char *folder : VIRTOOLS_PLUGIN_FOLDERS)
        registered += VirtoolsParsePlugins(plugins, root + "/" + folder, skipped);
    return registered;
}
