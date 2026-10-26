#include "core/runtime/VirtoolsRuntimeLayout.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void RequirePath(const std::string &actual, const std::filesystem::path &expected) {
    if (std::filesystem::u8path(actual).lexically_normal() != expected.lexically_normal())
        throw std::runtime_error("Expected " + expected.u8string() + ", got " + actual);
}

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("nemodev-runtime-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto flat = root / "flat";
        std::filesystem::create_directories(flat / "RenderEngines");
        std::ofstream(flat / VIRTOOLS_CK2_FILENAME).put('\0');
        RequirePath(VirtoolsRuntimeDirectory(flat.u8string() + "/"), flat);
        RequirePath(VirtoolsPluginRoot(flat.u8string()), flat);

        const auto game = root / "game";
        std::filesystem::create_directories(game / "Bin");
        std::filesystem::create_directories(game / "RenderEngines");
        std::ofstream(game / "Bin" / VIRTOOLS_CK2_FILENAME).put('\0');
        RequirePath(VirtoolsRuntimeDirectory(game.u8string()), game / "Bin");
        RequirePath(VirtoolsPluginRoot((game / "Bin").u8string()), game);

        const auto app = root / "Relocated Package" / "NeMoDev.app";
        const auto contents = app / "Contents";
        for (const char *folder : {"Frameworks", "MacOS", "Resources", "PlugIns/RenderEngines"})
            std::filesystem::create_directories(contents / folder);
        std::ofstream(contents / "Frameworks" / VIRTOOLS_CK2_FILENAME).put('\0');
        for (const auto &input : {app, contents, contents / "Resources", contents / "MacOS", contents / "Frameworks"})
            RequirePath(VirtoolsRuntimeDirectory(input.u8string()), contents / "Frameworks");
        RequirePath(VirtoolsPluginRoot((contents / "Frameworks").u8string()), contents / "PlugIns");

        std::filesystem::remove(contents / "Frameworks" / VIRTOOLS_CK2_FILENAME);
        RequirePath(VirtoolsRuntimeDirectory((contents / "Resources").u8string()), contents / "Resources");
        RequirePath(VirtoolsRuntimeDirectory((root / "missing").u8string()), root / "missing");
        RequirePath(VirtoolsPluginRoot((root / "missing").u8string()), root / "missing");
        std::filesystem::create_directories(root / "ordinary" / "Frameworks");
        std::ofstream(root / "ordinary" / "Frameworks" / VIRTOOLS_CK2_FILENAME).put('\0');
        RequirePath(VirtoolsRuntimeDirectory((root / "ordinary").u8string()), root / "ordinary");
        std::filesystem::remove_all(root);
        std::cout << "Flat, Bin and relocated application-bundle runtime layouts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return 1;
    }
}
