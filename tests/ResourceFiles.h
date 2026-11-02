#pragma once
#include <chrono>
#include <filesystem>
#include <string>

namespace vdevtest {
inline std::filesystem::path ResourceTestRoot(const char *prefix) {
    namespace fs = std::filesystem;
    auto root = fs::temp_directory_path() /
                (std::string(prefix) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
#if defined(_WIN32)
    const auto native = fs::absolute(root).native();
    root = native.rfind(L"\\\\?\\", 0) == 0 ? fs::path(native)
           : native.rfind(L"\\\\", 0) == 0 ? fs::path(L"\\\\?\\UNC\\" + native.substr(2))
                                            : fs::path(L"\\\\?\\" + native);
#endif
    return root;
}
inline std::filesystem::path LongResourceDirectory(std::filesystem::path root) {
    while (root.u8string().size() < 360)
        root /= "resource directory with spaces " + std::string(32, 'x');
    std::filesystem::create_directories(root);
    return root;
}
} // namespace vdevtest
