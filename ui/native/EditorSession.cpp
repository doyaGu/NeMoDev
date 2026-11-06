#include "ui/native/EditorSession.h"
#include <stdexcept>

namespace vdev {
EditorSession::EditorSession() : runtimeOperation([this](const std::string &label) {
    return (document && document->EditOpen()) || (interactionActive && interactionActive()) ||
                   (documentViews && documentViews->DocumentInteractionActive())
            ? "Finish the current operation before " + label : std::string();
}, [this](const std::string &label, const std::function<void()> &work) {
    if (!GuardedCall(work, &host, label.c_str())) throw std::runtime_error(label + " failed");
}, [this] { return host.Context() != nullptr; }) {}

EditorSession::~EditorSession() {
    FinishRuntimeOperations();
    fileDialogs.Close();
    ClearDocumentState();
    document.reset();
    // Native teardown can still invoke the runtime log sink. Keep the
    // log and its synchronization alive until the context has gone.
    host.Stop();
}

void EditorSession::DispatchFileRequests() {
    if (!runtimeOperation.Busy() && (!document || !document->EditOpen())) fileDialogs.Dispatch();
}

void EditorSession::RequestFile(const FileDialogOptions &options, FileDialogs::Completion completion, bool multiple, const void *requester) {
    const auto document = runtimeOperation.CurrentDocument();
    fileDialogs.Request(options, [this, document, completion = std::move(completion)](const FileDialogResult &result) {
        if (!document.IsCurrent()) return;
        if (result.status == FileDialogResult::Status::Error)
            Log(LogLevel::Error, result.error.empty() ? "File selection failed" : result.error);
        if (completion) completion(result);
    }, multiple, requester);
}

bool EditorSession::RunRuntime(const std::string &label, std::function<void()> work, std::function<void(bool)> completed) {
    return runtimeOperation.Queue(label, std::move(work), [this, completed = std::move(completed)](const std::string &error) {
        DrainRuntimeLog();
        if (!error.empty()) Log(LogLevel::Error, error);
        if (completed) completed(error.empty());
    });
}

void EditorSession::PollRuntimeOperations() {
    runtimeOperation.Poll();
    DrainRuntimeLog();
}

void EditorSession::FinishRuntimeOperations() {
    runtimeOperation.Finish();
    DrainRuntimeLog();
}

void EditorSession::ClearDocumentState() {
    runtimeOperation.DocumentChanged();
    files.DocumentChanged();
    objectSelection.Reset();
    objectClipboard = {};
    clipboard = {};
}

void EditorSession::ReplacingDocument(bool replaceContext) {
    if (documentViews) documentViews->DetachDocument(replaceContext);
}

void EditorSession::ReplacementCancelled() {
    if (documentViews) documentViews->RestoreDocument();
}

void EditorSession::DocumentReplaced() {
    objectSelection.Reset();
    objectClipboard = {};
    clipboard = {};
    if (documentViews) {
        try {
            documentViews->DocumentReplaced(HasDocument() ? document->Token() : DocumentToken{},
                                             document ? document->TopologyRevision() : 0, HasDocument());
        } catch (const std::exception &error) {
            Log(LogLevel::Error, std::string("Document switched; cannot restore views: ") + error.what());
        } catch (...) {
            Log(LogLevel::Error, "Document switched; cannot restore views");
        }
    }
}

} // namespace vdev
