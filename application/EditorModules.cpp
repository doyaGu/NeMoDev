#include "application/EditorModules.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>

namespace vdev {
namespace fs = std::filesystem;
namespace {
bool Library(const fs::path &path) {
    auto extension = path.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
#ifdef _WIN32
    return extension == ".dll";
#elif defined(__APPLE__)
    return extension == ".dylib";
#else
    return extension == ".so";
#endif
}
} // namespace

std::string EditorModuleDirectoryAt(const std::string &basePath) {
    if (basePath.empty()) return {};
    auto base = fs::u8path(basePath).lexically_normal();
    if (base.filename().empty()) base = base.parent_path();
    for (auto parent = base; !parent.empty(); parent = parent.parent_path()) {
        if (parent.filename() == "Contents" && parent.parent_path().extension() == ".app")
            return (parent / "PlugIns/EditorModules").u8string();
        if (parent == parent.parent_path()) break;
    }
    if (base.extension() == ".app") return (base / "Contents/PlugIns/EditorModules").u8string();
    return (base / "modules").u8string();
}

bool DiscoverEditorModules(const std::vector<EditorModuleDirectory> &directories,
                           const std::vector<std::string> &files,
                           std::vector<EditorModuleSource> &out, std::string &error) {
    std::string location;
    try {
        std::vector<fs::path> selected;
        const auto add = [&](const fs::path &path) {
            location = path.u8string();
            const auto canonical = fs::canonical(path);
            if (!fs::is_regular_file(canonical)) throw std::runtime_error("Expected a module file");
            for (const auto &previous : selected)
                if (fs::equivalent(previous, canonical)) return;
            if (selected.size() == 4096) throw std::runtime_error("Too many editor modules");
            selected.push_back(canonical);
        };
        for (const auto &directory : directories) {
            location = directory.path;
            if (location.empty()) throw std::runtime_error("Empty module directory");
            const auto path = fs::u8path(location);
            if (!fs::exists(path) && !directory.required) continue;
            if (!fs::is_directory(path)) throw std::runtime_error("Expected a module directory");
            std::vector<fs::path> entries;
            for (const auto &entry : fs::directory_iterator(path))
                if (Library(entry.path()) && !entry.is_directory()) entries.push_back(entry.path());
            std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
                return a.filename().u8string() < b.filename().u8string();
            });
            for (const auto &entry : entries) add(entry);
        }
        for (const auto &file : files) add(fs::u8path(file));
        std::vector<EditorModuleSource> result;
        for (const auto &path : selected) {
            const auto file = path.u8string();
            result.push_back({file, [file](EditorApplication &editor, std::string &error) {
                                  return editor.Load(file, error);
                              }, file});
        }
        out = std::move(result);
        error.clear();
        return true;
    } catch (const std::exception &failure) {
        error = "Editor module discovery: " + location + ": " + failure.what();
        return false;
    }
}
} // namespace vdev
