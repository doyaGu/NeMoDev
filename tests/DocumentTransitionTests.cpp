#include "application/Preferences.h"
#include "application/DocumentCommands.h"
#include "application/ExecutionCommands.h"
#include "core/layout/LayoutCommands.h"
#include "core/level/LevelCommands.h"
#include "editor/ImGuiDrawing.h"

#include <chrono>
#include <filesystem>
#include <stdexcept>

namespace {
using namespace vdev;
void Check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
struct Views : DocumentFiles::Observer, DocumentCommands::Presentation {
    EditorApplication &editor;
    Document &document;
    int detachFailure = 0, detached = 0, restored = 0, attached = 0, replaced = 0;
    bool restoreFailure = false, attachFailure = false, denyCommit = false, quiesced = false;
    std::string logs;
    Views(EditorApplication &editor, Document &document) : editor(editor), document(document) {}
    void ReplacingDocument(bool) override {
        ++detached;
        if (detachFailure == 1) throw std::runtime_error("Before detachment");
        editor.NativeAvailable(false);
        quiesced = true;
        if (detachFailure == 2) throw std::runtime_error("After detachment");
    }
    void ReplacementCancelled() override {
        ++restored;
        editor.NativeAvailable(true);
        quiesced = false;
        if (restoreFailure) throw std::runtime_error("View restoration failed");
    }
    void DocumentReplaced() override {
        ++replaced;
        quiesced = false;
    }
    void Log(LogLevel, const std::string &message) override { logs += message + "\n"; }
    LayoutInterfaceData CaptureLayout() const override { return {}; }
    void FileFinished(const DocumentCommands::Result &result) override {
        if (result.status != EDITOR_OK) return;
        editor.Attach(document.IsOpen() ? document.Token() : DocumentToken{}, document.TopologyRevision(), true,
                      document.IsOpen());
        ++attached;
        if (attachFailure) throw std::runtime_error("New view attachment failed");
    }
    void HistoryChanged() override {}
    void Quit() override {}
};
} // namespace
void TestDocumentTransitions(vdev::CkRuntimeHost &host) {
    using namespace vdev;
    namespace fs = std::filesystem;
    Document document(host);
    std::string error;
    Check(document.New(&error), error.c_str());
    Views *viewsPtr = nullptr;
    std::string failBefore, failAfter;
    RuntimeOperation runtime(
        [&](const std::string &label) {
            if (viewsPtr && viewsPtr->denyCommit && viewsPtr->quiesced &&
                (label.find("Finishing ") == 0 || label == "Closing composition"))
                return std::string("Commit admission failed");
            return std::string();
        },
        [&](const std::string &label, const std::function<void()> &work) {
            if (label == failBefore) throw std::runtime_error("Before native work");
            work();
            if (label == failAfter) throw std::runtime_error("After native work");
        });
    DocumentCommands *owner = nullptr;
    EditorApplication editor([&] { return &document; }, [&] { return !runtime.Busy() && (!owner || !owner->Busy()); });
    Views views(editor, document);
    viewsPtr = &views;
    DocumentFiles files([&] { return &document; }, runtime, views);
    DocumentCommands commands(editor, files, runtime, [&] { return &document; }, views);
    owner = &commands;
    Preferences preferences;
    ExecutionCommands execution(editor, [&] { return &document; }, [&] { return !runtime.Busy() && !commands.Busy(); }, preferences);
    files.Execution(&execution);
    Check(commands.Register(error) && execution.Register(error) && editor.Activate(EDITOR_IMGUI_VERSION, error),
          error.c_str());
    execution.Bind(document);
    const auto attach = [&] {
        editor.Attach(document.Token(), document.TopologyRevision(), true, document.IsOpen());
        execution.Synchronize();
    };
    const auto root =
        fs::temp_directory_path() /
        ("document-transition-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Check(fs::create_directory(root), "cannot create transition folder");
    struct Cleanup {
        fs::path root;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(root, e);
        }
    } cleanup{root};
    const auto path = (root / "source.cmo").u8string();
    const auto made = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(made.ok, "transition object");
    const auto object = document.Reference(made.created);
    SaveReport report;
    SaveOptions save;
    save.allowOverwrite = true;
    Check(SaveDocumentAs(document, path, save, report), "transition source save");
    Check(RenameLevelObject(document, object, "Transition changed").ok && document.History().Undo(),
          "transition undo/redo setup");
    attach();
    editor.SetSelection(object);
    const auto token = document.Token();
    const auto context = document.Context();
    const auto scope = runtime.CurrentDocument();
    const auto undo = document.History().UndoCount();
    const auto redo = document.History().RedoLabel();
    const auto revision = document.TopologyRevision();
    const auto dirty = document.Dirty();
    Check(editor.Execute("runtime.play", {editor.Scope(), nullptr, 0}).status == EDITOR_OK, "transition play");
    const auto preserved = [&] {
        Check(document.Token() == token && document.Context() == context && scope.IsCurrent(),
              "failed switch changed document identity");
        Check(document.Resolve(object) && editor.Selection() == object,
              "failed switch changed selection or object lifetime");
        Check(document.History().UndoCount() == undo && document.History().CanRedo() &&
                  document.History().RedoLabel() == redo,
              "failed switch discarded history");
        Check(document.TopologyRevision() == revision && document.Dirty() == dirty,
              "failed switch reset or dirtied old document");
        Check(execution.State().state == EDITOR_EXECUTION_RUNNING, "failed switch ended old playback");
    };
    const auto outcome = [&](EditorOperation operation) {
        const EditorValue value{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, operation};
        return editor.Query("application.operation", {editor.Scope(), &value, 1});
    };
    const auto status = [&](EditorOperation operation) {
        const auto result = outcome(operation);
        for (const auto &f : result.fields)
            if (f.key == "status") return EditorStatus(f.integer);
        return EDITOR_INVALID;
    };
    const auto run = [&](const char *id) {
        const EditorValue file{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, EditorText(path)};
        const auto response = editor.Execute(id, {editor.Scope(), std::string(id) == "document.open" ? &file : nullptr,
                                                  std::string(id) == "document.open" ? 1u : 0u});
        Check(response.status == EDITOR_QUEUED, "transition command was not accepted");
        if (const auto *input = commands.PendingInput())
            Check(commands.Respond(input->id, "discard") == EDITOR_QUEUED, "transition discard");
        runtime.Finish();
        commands.Poll();
        return response.operation;
    };
    for (const auto *id : {"document.open", "document.new", "document.close"}) {
        for (int failure : {1, 2}) {
            views.detachFailure = failure;
            const auto op = run(id);
            Check(status(op) == EDITOR_ERROR, "detach failure did not finish command");
            preserved();
        }
        views.detachFailure = 2;
        views.restoreFailure = true;
        const auto failedRestore = run(id);
        Check(status(failedRestore) == EDITOR_ERROR &&
                  views.logs.find("cannot restore document views") != std::string::npos,
              "failed restoration was not reported");
        preserved();
        views.detachFailure = 0;
        views.restoreFailure = false;
        views.denyCommit = true;
        const auto denied = run(id);
        Check(status(denied) == EDITOR_ERROR, "second admission failure was not completed");
        preserved();
        views.denyCommit = false;
        failBefore = std::string(id) == "document.open"  ? "Finishing composition load"
                     : std::string(id) == "document.new" ? "Finishing new composition"
                                                         : "Closing composition";
        const auto failedStart = run(id);
        Check(status(failedStart) == EDITOR_ERROR, "commit dispatch failure was not completed");
        preserved();
        failBefore.clear();
    }
    for (const auto *id : {"document.open", "document.new"}) {
        const auto before = views.detached;
        failAfter = std::string(id) == "document.open" ? "Opening composition" : "Creating composition";
        const auto op = run(id);
        Check(status(op) == EDITOR_ERROR && views.detached == before, "failed candidate detached views");
        preserved();
        failAfter.clear();
    }
    // A failing new-view attachment is a view failure: the committed document
    // and its new scope remain available and the operation carries diagnostics.
    views.attachFailure = true;
    failAfter = "Finishing composition load";
    const auto opened = run("document.open");
    failAfter.clear();
    Check(status(opened) == EDITOR_OK && document.IsOpen() && document.Token() != token &&
              document.Context() != context,
          "attachment failure lost committed document");
    Check(!scope.IsCurrent() && !document.History().CanUndo() && !document.History().CanRedo(),
          "successful replacement retained old scope/history");
    const auto result = outcome(opened);
    bool diagnostic = false, completionDiagnostic = false;
    for (const auto &field : result.fields)
        if (field.key == "message" && field.text.find("New view attachment failed") != std::string::npos)
            diagnostic = true;
    for (const auto &field : result.fields)
        if (field.key == "message" && field.text.find("finishing the runtime operation failed") != std::string::npos)
            completionDiagnostic = true;
    Check(diagnostic && completionDiagnostic && !commands.Busy(),
          "attachment failure left pending work or lost diagnostics");
    views.attachFailure = false;
    attach();
    const auto created = run("document.new");
    Check(status(created) == EDITOR_OK && document.IsOpen() && !document.Dirty(),
          "New did not attach a clean candidate");
    attach();
    const auto closed = run("document.close");
    Check(status(closed) == EDITOR_OK && !document.IsOpen(), "Close did not complete after failure recovery");
    execution.Stop();
}
