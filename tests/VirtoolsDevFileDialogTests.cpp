#include "ui/app/EditorState.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <utility>

struct PendingDialog { SDL_DialogFileCallback callback; void *userdata; SDL_PropertiesID properties; };
static std::vector<PendingDialog> pending;
// Substitute only the native launch boundary. Requests, owned materials,
// callback processing, document checks and dispatch use production code.
extern "C" void SDLCALL SDL_ShowFileDialogWithProperties(SDL_FileDialogType, SDL_DialogFileCallback callback,
                                                         void *userdata, SDL_PropertiesID properties) {
    pending.push_back({callback, userdata, properties});
}
static void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static PendingDialog Take() { Require(pending.size() == 1, "one native request"); auto result = pending.front(); pending.clear(); return result; }
int main() {
    try {
        Require(SDL_Init(0), "SDL initialization");
        vdev::EditorState editor;
        const auto mainThread = std::this_thread::get_id();
        bool called = false;
        {
            std::wstring filters = L"Images"; filters += L'\0'; filters += L"*.png;*.bmp"; filters += L'\0';
            std::wstring title = L"Choose Image";
            vdev::FileDialogOptions options;
            options.filter = filters.c_str(); options.title = title.c_str(); options.save = true; options.extension = L"png";
            editor.RequestFile(options, [&](const vdev::FileDialogResult &result) {
                Require(std::this_thread::get_id() == mainThread, "completion runs on main thread");
                Require(result.status == vdev::FileDialogResult::Status::Selected && result.paths == std::vector<std::string>{u8"/tmp/\u4e2d\u6587.png"}, "selected UTF-8 path and extension");
                Require(result.filterIndex == 1, "selected filter"); called = true;
            });
        }
        auto request = Take();
        const auto *filters = static_cast<SDL_DialogFileFilter *>(SDL_GetPointerProperty(request.properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, nullptr));
        Require(filters && std::string(filters[0].name) == "Images" && std::string(filters[0].pattern) == "png;bmp", "filters outlive caller strings");
        Require(std::string(SDL_GetStringProperty(request.properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, "")) == "Choose Image", "title outlives caller strings");
        std::thread worker([request] { const char *files[] = {u8"/tmp/\u4e2d\u6587", nullptr}; request.callback(request.userdata, files, 0); }); worker.join();
        Require(!called, "native callback only queues");
        editor.document = std::make_unique<vdev::Document>(editor.host);
        { vdev::Edit input(*editor.document, "Other input"); editor.DispatchFileRequests(); Require(!called, "completion waits for existing Edit"); Require(input.Cancel(), "input cancelled"); }
        editor.DispatchFileRequests(); Require(called, "queued result delivered");
        bool cancelled = false;
        editor.RequestFile({}, [&](const vdev::FileDialogResult &result) { cancelled = result.status == vdev::FileDialogResult::Status::Cancelled && result.paths.empty(); });
        request = Take(); const char *empty[] = {nullptr}; request.callback(request.userdata, empty, -1); editor.DispatchFileRequests(); Require(cancelled, "cancellation stays distinct");
        bool failed = false;
        editor.RequestFile({}, [&](const vdev::FileDialogResult &result) { failed = result.status == vdev::FileDialogResult::Status::Error && result.error == "native dialog failure"; });
        request = Take(); SDL_SetError("native dialog failure"); request.callback(request.userdata, nullptr, -1); editor.DispatchFileRequests(); Require(failed, "native error stays distinct");
        // SDL's Windows backend splits a location at the final separator;
        // an existing directory must not become the proposed filename.
        const auto folder = std::filesystem::u8path(SDL_GetBasePath()) /
            ("test-dialog-start-" + std::to_string(SDL_GetPerformanceCounter()));
        Require(std::filesystem::create_directory(folder), "create starting directory");
        for (bool directory : {true, false}) {
            vdev::FileDialogOptions options;
            const auto location = directory ? folder : folder / "composition.cmo";
            options.initial = vdev::Utf8ToWide(location.u8string());
            editor.RequestFile(options, [](const vdev::FileDialogResult &) {});
            request = Take();
            const std::string actual = SDL_GetStringProperty(request.properties, SDL_PROP_FILE_DIALOG_LOCATION_STRING, "");
            const std::string expected = (directory ? folder / "" : location).u8string();
            request.callback(request.userdata, empty, -1); editor.DispatchFileRequests();
            if (actual != expected) {
                std::filesystem::remove(folder);
                Require(false, directory ? "starting directory retains trailing separator" : "filename hint retained");
            }
        }
        Require(std::filesystem::remove(folder), "remove empty starting directory");
        bool stale = false;
        editor.RequestFile({}, [&](const vdev::FileDialogResult &) { stale = true; }); request = Take();
        editor.ClearDocumentState(); request.callback(request.userdata, empty, -1); editor.DispatchFileRequests(); Require(!stale, "closed document result discarded");
        editor.RequestFile({}, [&](const vdev::FileDialogResult &) { stale = true; }); request = Take();
        editor.fileDialogs.Close(); request.callback(request.userdata, empty, -1); editor.DispatchFileRequests(); Require(!stale, "late shutdown callback discarded");
        std::cout << "File dialog ownership, main-thread dispatch and document lifetime passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; SDL_Quit(); return 1; }
    SDL_Quit(); return 0;
}
