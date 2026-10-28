#include "ui/app/EditorState.h"

#include <stdexcept>

namespace vdev {

EditorState::EditorState() : runtimeOperation([this](const std::string &label) {
    return document && document->EditOpen() ? "Finish the current operation before " + label : std::string();
}) {}

EditorState::~EditorState() {
    FinishRuntimeOperations();
    fileDialogs.Close();
    ClearDocumentState();
    debugger.reset();
    document.reset();
    // Native teardown can still invoke the runtime log sink. Keep the
    // log and its synchronization alive until the context has gone.
    host.Stop();
}

void EditorState::DispatchFileRequests() {
    if (!runtimeOperation.Busy() && (!document || !document->EditOpen())) fileDialogs.Dispatch();
}

void EditorState::RequestFile(const FileDialogOptions &options, FileDialogs::Completion completion, bool multiple) {
    const auto document = runtimeOperation.CurrentDocument();
    fileDialogs.Request(options, [this, document, completion = std::move(completion)](const FileDialogResult &result) {
        if (!document.IsCurrent()) return;
        if (result.status == FileDialogResult::Status::Error)
            Log(LogLevel::Error, result.error.empty() ? "File selection failed" : result.error);
        if (completion) completion(result);
    }, multiple);
}

bool EditorState::RunRuntime(const std::string &label, std::function<void()> work, std::function<void(bool)> completed) {
    return runtimeOperation.Queue(label, [this, label, work = std::move(work)] {
        if (!GuardedCall(work, &host, label.c_str())) throw std::runtime_error(label + " failed");
    }, [this, completed = std::move(completed)](const std::string &error) {
        DrainRuntimeLog();
        if (!error.empty()) Log(LogLevel::Error, error);
        if (completed) completed(error.empty());
    });
}

void EditorState::PollRuntimeOperations() {
    runtimeOperation.Poll();
    DrainRuntimeLog();
}

void EditorState::FinishRuntimeOperations() {
    runtimeOperation.Finish();
    DrainRuntimeLog();
}

void EditorState::ClearDocumentState() {
    runtimeOperation.DocumentChanged();
    files.DocumentChanged();
    const unsigned revision = objectSelection.revision;
    static_cast<DocumentSession &>(*this) = DocumentSession();
    objectSelection.revision = revision + 1;
}

} // namespace vdev
