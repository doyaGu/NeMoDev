#pragma once
#include "application/DocumentFiles.h"
#include "application/EventLog.h"
#include "application/ObjectSelection.h"
#include "application/RuntimeOperation.h"
#include "core/document/Clipboard.h"
#include "core/document/Document.h"
#include "core/document/UndoHistory.h"
#include "ui/controls/FileDialogs.h"

namespace vdev {
// Native editing lifetime. Views receive queries, commands and their own
// state; this owner is private to application assembly and native tests.
class EditorSession : public DocumentFiles::Observer {
  public:
    EditorSession();
    ~EditorSession();
    CkRuntimeHost host;
    std::unique_ptr<Document> document;
    ObjectSelection objectSelection;
    ObjectClipboard objectClipboard;
    Clipboard clipboard;
    EventLog eventLog;
    FileDialogs fileDialogs;
    DocumentViews *documentViews = nullptr;
    std::function<bool()> interactionActive;

    bool HasDocument() const { return document && document->IsOpen(); }
    UndoHistory &History() { return document->History(); }
    void Log(LogLevel level, const std::string &text) override { eventLog.Append(level, text); }
    void RuntimeLog(LogLevel level, const std::string &text) { eventLog.Post(level, text); }
    void DrainRuntimeLog() { eventLog.Drain(); }
    void DispatchFileRequests();
    void RequestFile(const FileDialogOptions &, FileDialogs::Completion, bool multiple = false, const void *requester = nullptr);
    bool RunRuntime(const std::string &, std::function<void()>, std::function<void(bool)> completed = {});
    void PollRuntimeOperations();
    void FinishRuntimeOperations();
    void ClearDocumentState();
    void ReplacingDocument(bool replaceContext) override;
    void ReplacementCancelled() override;
    void DocumentReplaced() override;
    DocumentFiles files{[this] { return document.get(); }, runtimeOperation, *this};
    // Drain the worker before destroying its document, file owner or runtime.
    RuntimeOperation runtimeOperation;
};
} // namespace vdev
