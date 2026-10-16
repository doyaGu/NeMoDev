#include "ui/controls/FileDialogs.h"

#include <SDL3/SDL.h>
#include <filesystem>
#include <mutex>
#include <list>
#include <cwchar>
#include <utility>
#include "core/runtime/TextEncoding.h"

namespace vdev {
struct FileDialogs::State {
    struct Finished { Completion completion; FileDialogResult result; };
    std::mutex mutex;
    bool open = true;
    std::list<Finished> finished;

    struct Request {
        std::shared_ptr<State> state;
        std::list<Finished> finished;
        std::vector<std::string> names, patterns;
        std::vector<SDL_DialogFileFilter> filters;
        std::string initial, title, extension;
        SDL_PropertiesID properties = 0;
        ~Request() { if (properties) SDL_DestroyProperties(properties); }
    };

    static void SDLCALL Completed(void *userdata, const char *const *files, int filter) {
        std::unique_ptr<Request> request(static_cast<Request *>(userdata));
        FileDialogResult &result = request->finished.front().result;
        try {
            if (!files) {
                result.status = FileDialogResult::Status::Error;
                result.error = SDL_GetError();
            } else if (*files) {
                result.status = FileDialogResult::Status::Selected;
                for (const char *const *file = files; *file; ++file) result.paths.emplace_back(*file);
                result.filterIndex = filter < 0 ? 0 : filter + 1;
                if (!request->extension.empty())
                    for (std::string &path : result.paths)
                        if (std::filesystem::u8path(path).extension().empty()) path += "." + request->extension;
            }
        } catch (const std::exception &error) {
            result.status = FileDialogResult::Status::Error;
            result.paths.clear();
            try { result.error = error.what(); } catch (...) { result.error.clear(); }
        }
        std::lock_guard<std::mutex> lock(request->state->mutex);
        if (request->state->open)
            request->state->finished.splice(request->state->finished.end(), request->finished);
    }

    void Show(const FileDialogOptions &options, Completion completion, bool multiple, bool folder) {
        auto request = std::make_unique<Request>();
        request->state = owner.lock();
        if (!request->state) return;
        request->finished.push_back({std::move(completion), {}});
        request->initial = WideToUtf8(options.initial);
        if (!request->initial.empty()) {
            const auto location = std::filesystem::u8path(request->initial);
            std::error_code failure;
            // SDL's Windows backend splits folder/file at the last separator.
            // Retain an existing directory as a directory on every backend.
            if (std::filesystem::is_directory(location, failure)) request->initial = (location / "").u8string();
        }
        request->title = options.title ? WideToUtf8(options.title) : std::string();
        request->extension = options.save && options.extension ? WideToUtf8(options.extension) : std::string();
        if (!folder && options.filter) {
            const wchar_t *part = options.filter;
            while (*part) {
                request->names.push_back(WideToUtf8(part));
                part += wcslen(part) + 1;
                if (!*part) break;
                std::string pattern = WideToUtf8(part), extensions;
                for (size_t start = 0; start < pattern.size();) {
                    const size_t end = pattern.find(';', start);
                    std::string extension = pattern.substr(start, end == std::string::npos ? end : end - start);
                    if (extension == "*.*") extension = "*";
                    else if (extension.compare(0, 2, "*.") == 0) extension.erase(0, 2);
                    if (!extensions.empty()) extensions += ';';
                    extensions += extension;
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
                request->patterns.push_back(std::move(extensions));
                part += wcslen(part) + 1;
            }
            request->names.resize(request->patterns.size());
            for (size_t i = 0; i < request->names.size(); ++i)
                request->filters.push_back({request->names[i].c_str(), request->patterns[i].c_str()});
        }
        request->properties = SDL_CreateProperties();
        const SDL_PropertiesID properties = request->properties;
        const bool ready = properties &&
            SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, options.owner) &&
            SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, request->filters.data()) &&
            SDL_SetNumberProperty(properties, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, request->filters.size()) &&
            SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_LOCATION_STRING, request->initial.c_str()) &&
            SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, request->title.c_str()) &&
            SDL_SetBooleanProperty(properties, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, multiple);
        Request *pending = request.release();
        if (!ready) { Completed(pending, nullptr, -1); return; }
        SDL_ShowFileDialogWithProperties(folder ? SDL_FILEDIALOG_OPENFOLDER : options.save ? SDL_FILEDIALOG_SAVEFILE : SDL_FILEDIALOG_OPENFILE,
                                         &Completed, pending, properties);
    }
    std::weak_ptr<State> owner;
};

FileDialogs::FileDialogs() : m_State(std::make_shared<State>()) { m_State->owner = m_State; }
FileDialogs::~FileDialogs() { Close(); }
void FileDialogs::Request(const FileDialogOptions &options, Completion completion, bool multiple) {
    m_State->Show(options, std::move(completion), multiple, false);
}
void FileDialogs::RequestFolder(const std::wstring &title, SDL_Window *owner, Completion completion) {
    FileDialogOptions options;
    options.title = title.c_str(); options.owner = owner;
    m_State->Show(options, std::move(completion), false, true);
}
void FileDialogs::Dispatch() {
    std::list<State::Finished> finished;
    { std::lock_guard<std::mutex> lock(m_State->mutex); finished.swap(m_State->finished); }
    for (auto &item : finished) {
        if (!m_State->open) break;
        item.completion(item.result);
    }
}
void FileDialogs::Close() {
    if (!m_State) return;
    std::lock_guard<std::mutex> lock(m_State->mutex);
    m_State->open = false;
    m_State->finished.clear();
    m_State->owner.reset();
}
} // namespace vdev
