#include "core/runtime/VirtoolsRuntimeLayout.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/Resources.h"
#include "core/document/Document.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <SDL3/SDL.h>

static void RequirePath(const std::string &actual, const std::filesystem::path &expected) {
    if (std::filesystem::u8path(actual).lexically_normal() != expected.lexically_normal())
        throw std::runtime_error("Expected " + expected.u8string() + ", got " + actual);
}

static void CheckInstalledRuntime(const std::string &path) {
#if defined(__linux__)
    const char *original = SDL_getenv("XDG_DATA_DIRS");
    const auto previous = original ? std::optional<std::string>(original) : std::nullopt;
#endif
    vdev::CkRuntimeHost host;
    std::string error;
    const char *base = SDL_GetBasePath();
    if (!host.Start(VirtoolsRuntimeDirectory(path == "auto" && base ? base : path), {}, &error))
        throw std::runtime_error(error);
    const auto &counts = host.Counts();
    if (!counts.behaviorDlls || !counts.managerDlls || !counts.renderEngines || !counts.behaviorPrototypes)
        throw std::runtime_error("The installed runtime did not register its plugins");
    if (!std::filesystem::is_regular_file(std::filesystem::u8path(vdev::DataFilePath("interface.nmo"))))
        throw std::runtime_error("The application bundle's interface data is unavailable");
    {
        vdev::Document document(host);
        if (!document.New(&error) || !document.Context()->GetCurrentLevel())
            throw std::runtime_error("Cannot create a composition with the installed runtime: " + error);
#if !defined(_WIN32)
        FONT_HANDLE font = VxCreateFont("Noto Sans", -24, 400, FALSE, FALSE);
        if (!font) throw std::runtime_error("Cannot create the bundled font");
        struct ReleaseFont { FONT_HANDLE font; ~ReleaseFont() { VxDeleteFont(font); } } release{font};
        VXFONTINFO info;
        if (!VxGetFontInfo(font, info) || info.FaceName != "Noto Sans")
            throw std::runtime_error("The runtime did not find the bundled Noto font");
        std::vector<XDWORD> pixels(160 * 48, 0xffffffff);
        VxImageDescEx desc;
        desc.Width = 160; desc.Height = 48; desc.BytesPerLine = 160 * 4;
        desc.BitsPerPixel = 32; desc.Image = reinterpret_cast<XBYTE *>(pixels.data());
        desc.RedMask = 0xff0000; desc.GreenMask = 0xff00; desc.BlueMask = 0xff; desc.AlphaMask = 0xff000000;
        BITMAP_HANDLE bitmap = VxCreateBitmap(desc);
        if (!bitmap) throw std::runtime_error("Cannot create the font bitmap");
        struct ReleaseBitmap { BITMAP_HANDLE bitmap; ~ReleaseBitmap() { VxDeleteBitmap(bitmap); } } image{bitmap};
        CKRECT rectangle{0, 0, 160, 48};
        if (!VxDrawBitmapText(bitmap, font, "NeMoDev", &rectangle, VXTEXT_LEFT | VXTEXT_TOP, 0xffffffff, 0xff000000))
            throw std::runtime_error("Cannot draw with the bundled font");
        std::unique_ptr<XBYTE[]> copy(VxConvertBitmap(bitmap, desc));
        bool ink = false;
        if (copy) for (int y = 0; y < desc.Height; ++y)
            for (int x = 0; x < desc.Width; ++x)
                if (desc.Image[y * desc.BytesPerLine + x * 4] != 255) ink = true;
        if (!ink) throw std::runtime_error("The bundled font did not produce glyph pixels");
#endif
    }
    host.Stop();
#if defined(__linux__)
    const char *restored = SDL_getenv("XDG_DATA_DIRS");
    if (previous != (restored ? std::optional<std::string>(restored) : std::nullopt))
        throw std::runtime_error("Closing the runtime did not restore the SDL font environment");
#endif
    std::cout << "Installed runtime: plugins registered, interface data found, composition created";
#if !defined(_WIN32)
    std::cout << ", bundled font glyphs checked";
#endif
    std::cout << '\n';
}

#if defined(__linux__)
static void CheckRuntimeStartFailure(const std::string &path) {
    const char *original = SDL_getenv("XDG_DATA_DIRS");
    const auto previous = original ? std::optional<std::string>(original) : std::nullopt;
    vdev::CkRuntimeHost host;
    std::string error;
    if (host.Start(VirtoolsRuntimeDirectory(path), {}, &error) ||
        error.find("SDL video initialization failed:") == std::string::npos)
        throw std::runtime_error("Expected a video initialization failure after font setup: " + error);
    const char *restored = SDL_getenv("XDG_DATA_DIRS");
    if (previous != (restored ? std::optional<std::string>(restored) : std::nullopt))
        throw std::runtime_error("Runtime startup failure changed the SDL font environment");
    std::cout << "Runtime startup failure restored the font directories\n";
}
#endif

int main(int argc, char **argv) {
    const auto root = std::filesystem::temp_directory_path() /
        ("nemodev-runtime-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        if (argc == 3 && std::string(argv[1]) == "--runtime") {
            CheckInstalledRuntime(argv[2]);
            return 0;
        }
#if defined(__linux__)
        if (argc == 3 && std::string(argv[1]) == "--runtime-failure") {
            CheckRuntimeStartFailure(argv[2]);
            return 0;
        }
#endif
        if (argc != 1) throw std::runtime_error("Expected no arguments or --runtime PATH");
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
        RequirePath(VirtoolsDataDirectory((game / "Bin").u8string()), game / "data");
        std::filesystem::create_directories(game / "Bin/data");
        RequirePath(VirtoolsDataDirectory((game / "Bin").u8string()), game / "data");
        std::filesystem::create_directories(game / "data");
        RequirePath(VirtoolsDataDirectory((game / "Bin").u8string() + "/"), game / "data");
        RequirePath(VirtoolsDataDirectory(game.u8string()), game / "data");

        const auto app = root / "Relocated Package" / "NeMoDev.app";
        const auto contents = app / "Contents";
        for (const char *folder : {"Frameworks", "MacOS", "Resources", "PlugIns/RenderEngines"})
            std::filesystem::create_directories(contents / folder);
        std::ofstream(contents / "Frameworks" / VIRTOOLS_CK2_FILENAME).put('\0');
        for (const auto &input : {app, contents, contents / "Resources", contents / "MacOS", contents / "Frameworks"})
            RequirePath(VirtoolsRuntimeDirectory(input.u8string()), contents / "Frameworks");
        RequirePath(VirtoolsPluginRoot((contents / "Frameworks").u8string()), contents / "PlugIns");
        RequirePath(VirtoolsDataDirectory((contents / "Resources").u8string()), contents / "Resources/data");

        std::filesystem::remove(contents / "Frameworks" / VIRTOOLS_CK2_FILENAME);
        RequirePath(VirtoolsRuntimeDirectory((contents / "Resources").u8string()), contents / "Resources");
        const auto developmentBin = app.parent_path() / "Bin";
        std::filesystem::create_directories(developmentBin);
        std::ofstream(developmentBin / VIRTOOLS_CK2_FILENAME).put('\0');
        for (const auto &input : {app, contents, contents / "Resources", contents / "MacOS"})
            RequirePath(VirtoolsRuntimeDirectory(input.u8string()), developmentBin);
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
