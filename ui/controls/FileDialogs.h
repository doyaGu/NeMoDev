#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

struct SDL_Window;

namespace vdev {

// Options are borrowed only during Request; the request owns its copies until
// the callback ends. Paths returned in FileDialogResult are UTF-8.
struct FileDialogOptions {
    bool save = false;
    const wchar_t *filter = L"All files (*.*)\0*.*\0"; // pairs of strings, each ended by \0
    const wchar_t *extension = nullptr;                // added to a name without one
    const wchar_t *title = nullptr;
    std::wstring initial; // file hint or existing starting directory
    SDL_Window *owner = nullptr; // null: the active window
};

struct FileDialogResult {
    enum class Status { Selected, Cancelled, Error };
    Status status = Status::Cancelled;
    std::vector<std::string> paths; // UTF-8
    int filterIndex = 0; // 1-based, 0 when the platform does not report it
    std::string error;
};

// Requests copy their options immediately. SDL callbacks only queue results;
// Dispatch invokes completions on the main thread, between UI frames.
class FileDialogs {
public:
    using Completion = std::function<void(const FileDialogResult &)>;
    FileDialogs();
    ~FileDialogs();
    FileDialogs(const FileDialogs &) = delete;
    FileDialogs &operator=(const FileDialogs &) = delete;
    void Request(const FileDialogOptions &options, Completion completion, bool multiple = false);
    void RequestFolder(const std::wstring &title, SDL_Window *owner, Completion completion);
    void Dispatch();
    void Close();
private:
    struct State;
    std::shared_ptr<State> m_State;
};

} // namespace vdev
