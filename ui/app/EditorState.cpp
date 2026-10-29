#include "ui/app/EditorState.h"

#include <stdexcept>
#include "core/level/ObjectQueries.h"
#include "core/level/AttributeQueries.h"

namespace vdev {

void ObjectSelection::Set(const Document &document, std::vector<CK_ID> ids) {
    std::vector<ObjectRef> references;
    std::vector<CK_ID> live;
    references.reserve(ids.size()); live.reserve(ids.size());
    for (CK_ID id : ids) {
        const auto ref = document.Reference(id);
        if (!document.Resolve(ref)) continue;
        references.push_back(ref); live.push_back(id);
    }
    m_References = std::move(references);
    m_Ids = std::move(live);
    if (m_Ids.empty()) locked = false;
    ++revision;
}

void ObjectSelection::Validate(const Document &document) {
    std::size_t live = 0;
    for (ObjectRef ref : m_References) {
        const auto id = document.NativeId(ref);
        if (!id) continue;
        m_References[live] = ref; m_Ids[live++] = id;
    }
    if (live == m_References.size()) return;
    m_References.resize(live); m_Ids.resize(live);
    if (m_Ids.empty()) locked = false;
    ++revision;
}

void SelectionFollower::Push(ObjectSelection &objects, const std::set<CK_ID> &selection, const Document *document) {
    if (selection == m_Pushed) return;
    m_Pushed = selection;
    if (objects.locked || !document) return;
    auto rendered = FilterObjects(*document, {selection.begin(), selection.end()}, CKCID_RENDEROBJECT);
    if (rendered != objects.Objects()) objects.Set(*document, std::move(rendered));
    m_Seen = objects.revision;
}

void EditorState::DropAttributeOn(const std::vector<CK_ID> &objects, const AttributePayload &attribute) {
    if (!HasDocument()) return;
    const auto catalog = ReadAttributeCatalog(*document);
    const auto *type = catalog.Find(attribute.type);
    const auto name = type ? type->name : std::string();
    std::vector<std::pair<std::string, bool>> each;
    const auto result = DropAttribute(*document, objects, attribute.type, attribute.value, &each);
    LogAttributeAdds(name.c_str(), each, result.ok);
    if (!result.ok && !result.Unchanged() && each.empty()) Log(LogLevel::Warning, "Add Attribute: " + AnsiToUtf8(result.message));
    else if (result.ok && !result.message.empty()) Log(LogLevel::Warning, AnsiToUtf8(result.message));
}

EditorState::EditorState() : runtimeOperation([this](const std::string &label) {
    return (document && document->EditOpen()) || (workspace && workspace->Application().HasEdits())
            ? "Finish the current operation before " + label : std::string();
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
