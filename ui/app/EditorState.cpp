#include "ui/app/EditorState.h"

#include <stdexcept>
#include "core/level/ObjectQueries.h"
#include "core/level/AttributeQueries.h"

namespace vdev {

void SelectionFollower::Push(ObjectSelection &objects, const std::set<CK_ID> &selection, const Document *document,
                             EditorApplication &editor) {
    if (selection == m_Pushed) return;
    if (objects.Locked()) { m_Pushed = selection; return; }
    if (!document) return;
    std::vector<ObjectRef> targets;
    for (auto id : selection) targets.push_back(document->Reference(id));
    const EditorValue values[] = {
        {EditorLiteral("objects"), EDITOR_OBJECT_LIST, {}, {}, 0,
         {targets.data(), static_cast<uint32_t>(targets.size())}},
        {EditorLiteral("render-only"), EDITOR_INTEGER_VALUE, {}, {}, 1}};
    const auto result = editor.Execute("selection.replace", {editor.Scope(), values, 2});
    if (result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE || result.status == EDITOR_QUEUED) {
        m_Pushed = selection;
        m_Seen = objects.Revision();
    }
}

void EditorState::DropAttributeOn(const std::vector<CK_ID> &objects, const EditorAttributePayload &attribute) {
    if (!HasDocument()) return;
    const auto refs = document->References(objects);
    const EditorValue values[] = {{EDITOR_TEXT("objects"),EDITOR_OBJECT_LIST,{},{},0,{refs.data(),uint32_t(refs.size())}},
        {EDITOR_TEXT("type"),EDITOR_INTEGER_VALUE,{},{},uint64_t(attribute.type)},
        {EDITOR_TEXT("catalog"),EDITOR_INTEGER_VALUE,{},{},attribute.catalog},
        {EDITOR_TEXT("source"),EDITOR_OBJECT,attribute.value}};
    auto &editor=workspace->Application();editor.Execute("attribute.add",{editor.Scope(),values,4});
}

EditorState::EditorState() : runtimeOperation([this](const std::string &label) {
    return (document && document->EditOpen()) || (workspace && workspace->Application().HasEdits()) ||
                   (documentViews && documentViews->DocumentInteractionActive())
            ? "Finish the current operation before " + label : std::string();
}, [this](const std::string &label, const std::function<void()> &work) {
    if (!GuardedCall(work, &host, label.c_str())) throw std::runtime_error(label + " failed");
}) {}

EditorState::~EditorState() {
    FinishRuntimeOperations();
    fileDialogs.Close();
    ClearDocumentState();
    debugger = nullptr;
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
    return runtimeOperation.Queue(label, std::move(work), [this, completed = std::move(completed)](const std::string &error) {
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
    objectSelection.Reset();
    static_cast<DocumentSession &>(*this) = DocumentSession();
}

void EditorState::ReplacingDocument(bool replaceContext) {
    if (documentViews) documentViews->DetachDocument(replaceContext);
}

void EditorState::ReplacementCancelled() {
    if (documentViews) documentViews->RestoreDocument();
}

void EditorState::DocumentReplaced() {
    objectSelection.Reset();
    static_cast<DocumentSession &>(*this) = DocumentSession();
}

} // namespace vdev
