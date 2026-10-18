// SDL3 window/event loop and the editor's ImGui GPU renderer.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "ui/render/UiRenderer.h"
#include "ui/app/CaptureDriver.h"
#include "ui/app/EditorApp.h"
#include "ui/controls/DevTheme.h"
#include "ui/schematic/Snapshots.h"
#include "core/runtime/Resources.h"
#include "core/runtime/TextEncoding.h"
#include "core/runtime/VirtoolsRuntimeLayout.h"
#include <algorithm>
#include <cstdio>
#include <clocale>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#ifdef VIRTOOLSDEV_UI_TESTS
#include "ui/app/UiTestRun.h"
#endif

namespace {
namespace fs = std::filesystem;
void LoadFonts(float scale) {
    ImGuiIO &io = ImGui::GetIO();
    const fs::path directory = fs::u8path(SDL_GetBasePath()) / "data" / "fonts";
    const auto regular = (directory / "NotoSans-Regular.ttf").u8string();
    const auto bold = (directory / "NotoSans-Bold.ttf").u8string();
    const auto cjk = (directory / "NotoSansSC-Regular.otf").u8string();
    auto add = [&](float size, bool strong) {
        ImFont *font = io.Fonts->AddFontFromFileTTF((strong ? bold : regular).c_str(), size);
        if (!font) font = io.Fonts->AddFontDefault();
        ImFontConfig merge; merge.MergeMode = true; merge.DstFont = font;
        if (fs::exists(fs::u8path(cjk))) io.Fonts->AddFontFromFileTTF(cjk.c_str(), size, &merge);
        return font;
    };
    io.FontDefault = add(14.0f * scale, false);
    ImFont *normal = add(12.0f * scale, false);
    ImFont *strong = add(14.0f * scale, true);
    vdev::SetVIFonts(normal, strong);
}
std::string ResolveRuntime(const std::vector<std::wstring> &args) {
    std::string path;
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == L"--runtime") { path = vdev::WideToUtf8(args[i+1]); break; }
    if (path.empty()) if (const char *value = SDL_getenv("VIRTOOLS_PATH")) path = value;
    if (path.empty()) path = SDL_GetBasePath();
    return VirtoolsRuntimeDirectory(path);
}
bool CaptureFrame(const std::wstring &path) {
    std::vector<unsigned char> pixels;
    int width = 0, height = 0;
    if (!vdev::MainRenderer().ReadFrame(pixels, width, height)) return false;
    for (size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i+2]);
    return vdev::WriteBgraPng(vdev::WideToUtf8(path), width, height, pixels.data());
}
}

// Kept for the Schematic page capture path while print jobs own their output.
bool MainWritePng(const std::wstring &path, unsigned int width, unsigned int height, const unsigned char *pixels) {
    return vdev::WriteBgraPng(vdev::WideToUtf8(path), int(width), int(height), pixels);
}

int main(int argc, char **argv) {
#ifndef _WIN32
    std::setlocale(LC_CTYPE, "");
#endif
    std::vector<std::wstring> args;
    for (int i = 0; i < argc; ++i) args.push_back(vdev::Utf8ToWide(argv[i]));
    vdev::CommandLine line; line.Parse(args);
    const bool capture = line.Capture(), headless = line.Headless();
    if (!line.badOption.empty()) {
        std::fprintf(stderr, "Invalid option: %s\n", vdev::WideToUtf8(line.badOption).c_str()); return 2;
    }
    if (!line.fileToOpen.empty() && !fs::exists(fs::u8path(vdev::WideToUtf8(line.fileToOpen)))) {
        std::fprintf(stderr, "Sample not found: %s\n", vdev::WideToUtf8(line.fileToOpen).c_str()); return 77;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { std::fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    const std::string runtime = ResolveRuntime(args);
    const float scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    SDL_Window *window = SDL_CreateWindow("NemoDev", int(1600 * scale), int(960 * scale),
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (!window) { std::fprintf(stderr, "%s\n", SDL_GetError()); SDL_Quit(); return 1; }
    if (const vdev::EmbeddedImage *icon = vdev::FindEmbeddedImage("app", "appicon", 10)) {
        SDL_Surface *surface = SDL_CreateSurfaceFrom(icon->width, icon->height, SDL_PIXELFORMAT_RGBA32,
            const_cast<unsigned char *>(icon->rgba), icon->width * 4);
        if (surface) { SDL_SetWindowIcon(window, surface); SDL_DestroySurface(surface); }
    }
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    char *preferences = SDL_GetPrefPath("Virtools", "VirtoolsDev");
    const std::string iniPath = preferences ? (fs::u8path(preferences) / "VirtoolsDevInterface.ini").u8string() : std::string();
    SDL_free(preferences);
    if (!line.startup.isolated && !iniPath.empty()) {
        std::error_code failure;
        const auto saved = fs::u8path(iniPath);
        const auto previous = fs::u8path(SDL_GetBasePath()) / "VirtoolsDevInterface.ini";
        if (!fs::exists(saved, failure) && fs::exists(previous, failure)) {
            fs::copy_file(previous, saved, failure);
            if (failure) std::fprintf(stderr, "Cannot migrate the saved window layout: %s\n", failure.message().c_str());
        }
    }
    io.IniFilename = line.startup.isolated || iniPath.empty() ? nullptr : iniPath.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    vdev::ApplyDevTheme(ImGui::GetStyle()); ImGui::GetStyle().ScaleAllSizes(scale);
    LoadFonts(scale);
    ImGui_ImplSDL3_InitForSDLGPU(window);
    std::string error;
    if (!vdev::MainRenderer().Start(window, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext(); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }
    if (!headless) SDL_ShowWindow(window);
    vdev::EditorApp app;
    int exitCode = 0;
    if (!app.Initialize(window, runtime, vdev::WideToUtf8(line.fileToOpen), line.startup)) exitCode = 1;
#ifdef VIRTOOLSDEV_UI_TESTS
    std::unique_ptr<vdev::UiTestRun> tests;
    if (!exitCode && line.UiTests()) tests = std::make_unique<vdev::UiTestRun>(app, line.uiTests, line.uiResults, line.uiShow);
#endif
    int frame = 0;
    std::vector<std::wstring> dropped;
    while (!exitCode && !app.ShouldQuit()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            app.HandleEvent(event);
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))) app.RequestQuit();
            if (event.type == SDL_EVENT_DROP_BEGIN) dropped.clear();
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) dropped.push_back(vdev::Utf8ToWide(event.drop.data));
            if (event.type == SDL_EVENT_DROP_COMPLETE && !dropped.empty()) { app.DropFiles(dropped, event.drop.x, event.drop.y); dropped.clear(); }
        }
        vdev::MainRenderer().NewFrame(); ImGui_ImplSDL3_NewFrame();
        if (capture) line.FeedInput(frame, app);
        ImGui::NewFrame();
        ImGuiErrorRecoveryState recovery;
        ImGui::ErrorRecoveryStoreState(&recovery);
        try { app.Frame(); }
        catch (const vdev::TextEncodingError &failure) {
            const bool assertRecovery = io.ConfigErrorRecoveryEnableAssert;
            io.ConfigErrorRecoveryEnableAssert = false;
            ImGui::ErrorRecoveryTryToRecoverState(&recovery);
            io.ConfigErrorRecoveryEnableAssert = assertRecovery;
            app.State().Log(vdev::LogLevel::Error, failure.what());
        }
        ImGui::Render();
        if (!vdev::MainRenderer().Render(ImGui::GetDrawData(), !headless, &error)) {
            std::fprintf(stderr, "UI render failed: %s\n", error.c_str()); exitCode = 3; break;
        }
        vdev::ReleaseRetiredTextures();
        if (capture && ++frame >= line.captureFrames) { exitCode = CaptureFrame(line.capturePath) ? 0 : 3; break; }
#ifdef VIRTOOLSDEV_UI_TESTS
        if (tests) {
            tests->PreSwap(); tests->PostSwap();
            if (tests->Finished()) { exitCode = tests->ExitCode(); break; }
        }
#endif
        if (!capture) SDL_Delay(1);
    }
#ifdef VIRTOOLSDEV_UI_TESTS
    if (tests) { if (!tests->Finished()) exitCode = 1; tests->Stop(); }
#endif
    app.Shutdown();
    vdev::ReleaseRetiredTextures();
    vdev::MainRenderer().Stop(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
#ifdef VIRTOOLSDEV_UI_TESTS
    if (tests) tests->Destroy();
#endif
    SDL_DestroyWindow(window); SDL_Quit();
    return exitCode;
}
