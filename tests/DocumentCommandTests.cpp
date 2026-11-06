#include "application/DocumentCommands.h"
#include "core/document/FileCommands.h"
#include "core/layout/LayoutCommands.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "editor/ImGuiDrawing.h"
#include "workspace/EditorClient.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
using namespace vdev;
void Check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
struct Views : DocumentFiles::Observer, DocumentCommands::Presentation {
    Document &document;
    EditorApplication &editor;
    int detached = 0, finished = 0, history = 0, quit = 0;
    bool captureFailure = false;
    DocumentCommands::Result last;
    Views(Document &document, EditorApplication &editor) : document(document), editor(editor) {}
    void ReplacingDocument(bool) override {
        Check(!document.EditOpen(), "replacement was wrapped in a document edit");
        ++detached;
        editor.NativeAvailable(false);
    }
    void ReplacementCancelled() override { editor.NativeAvailable(true); }
    void DocumentReplaced() override {}
    void Log(LogLevel, const std::string &) override {}
    LayoutInterfaceData CaptureLayout() const override {
        if (captureFailure) throw std::runtime_error("Cannot capture current view data");
        Check(!document.EditOpen(), "save was wrapped in a document edit");
        return {};
    }
    void FileFinished(const DocumentCommands::Result &result) override {
        ++finished;
        last = result;
    }
    void HistoryChanged() override { ++history; }
    void Quit() override { ++quit; }
};
std::string Bytes(const std::string &path) {
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
} // namespace

void TestDocumentCommands(vdev::CkRuntimeHost &host) {
    using namespace vdev;
    Document document(host);
    std::string error;
    Check(document.New(&error), error.c_str());
    bool holdPublication = false, failAfterPublication = false, failBeforePublication = false;
    std::promise<void> published, release;
    auto gate = release.get_future();
    RuntimeOperation runtime(
        [&](const std::string &) { return document.EditOpen() ? "Finish the edit" : std::string(); },
        [&](const std::string &label, const std::function<void()> &work) {
            if (failBeforePublication && label == "Saving composition") throw std::runtime_error("Before save publication");
            work();
            if (label != "Saving composition") return;
            if (holdPublication) {
                published.set_value();
                gate.wait();
            }
            if (failAfterPublication) throw std::runtime_error("Injected failure after publishing");
        });
    unsigned documentReads = 0;
    EditorApplication editor([&] { ++documentReads; return document.IsOpen() ? &document : nullptr; },
                             [&] { return runtime.Available(); });
    Views views(document, editor);
    DocumentFiles files([&] { ++documentReads; return &document; }, runtime, views);
    DocumentCommands commands(editor, files, runtime, [&] { ++documentReads; return &document; }, views);
    Check(commands.Register(error), error.c_str());
    Check(editor.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    const auto attach = [&] {
        editor.Attach(document.IsOpen() ? document.Token() : DocumentToken{}, document.TopologyRevision(), true,
                      document.IsOpen());
    };
    const auto drain = [&] {
        editor.EndFrame();
        runtime.Finish();
        attach();
        commands.Poll();
    };
    const auto execute = [&](const char *id, const std::string &path = std::string(), EditorView view = 0) {
        EditorValue argument{EditorLiteral("path"), EDITOR_TEXT_VALUE, {}, EditorText(path)};
        const EditorRequest request{editor.Scope(view), path.empty() ? nullptr : &argument, path.empty() ? 0u : 1u};
        return editor.Execute(id, request);
    };
    const auto status = [&](EditorOperation operation, uint64_t *written = nullptr) {
        const EditorValue value{EditorLiteral("operation"), EDITOR_INTEGER_VALUE, {}, {}, operation};
        const EditorRequest request{editor.Scope(), &value, 1};
        const auto result = editor.Query("application.operation", request);
        Check(result.status == EDITOR_OK, "operation was lost");
        EditorStatus valueStatus = EDITOR_INVALID;
        for (const auto &field : result.fields) {
            if (field.key == "status") valueStatus = static_cast<EditorStatus>(field.integer);
            if (written && field.key == "written") *written = field.integer;
        }
        return valueStatus;
    };
    attach();
    const EditorClient infoClient(editor.Host(), 0);
    commands.Poll();
    const auto initialInfo = infoClient.Query("document.info");
    Check(initialInfo.status == EDITOR_OK && EditorClient::Integer(initialInfo, "open") == 1 &&
              EditorClient::Integer(initialInfo, "document") == document.Token().value,
          "new document metadata was not published");
    const auto root =
        std::filesystem::temp_directory_path() /
        ("document-command-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Check(std::filesystem::create_directory(root), "temporary folder already exists");
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};
    const auto path = (root / "saved.cmo").u8string();
    const auto target = (root / "target.cmo").u8string();
    // Duplicate-name UI reads copies and submits a complete choice list once.
    // Native loading keeps the existing Ignore/Rename/Use Current behavior.
    {
        const EditorClient client(editor.Host(), 0);
        std::vector<CK_ID> exported;
        for (int i = 0; i < 140; ++i) {
            const auto object = CreateLayoutObject(document, LayoutObject::Frame3D);
            Check(object.ok && RenameLevelObject(document, document.Reference(object.created),
                                                 "Duplicate input " + std::to_string(i)).ok,
                  "duplicate input fixture failed");
            exported.push_back(object.created);
        }
        const auto duplicatePath = (root / "duplicates.nmo").u8string();
        Check(SaveObjectsAs(document, exported, duplicatePath, &error), error.c_str());
        attach();
        const auto selected = document.Reference(exported.front());
        editor.SetSelection(selected);
        const auto token = document.Token();
        const auto undo = document.History().UndoCount();
        const auto redo = document.History().CanRedo();
        const bool dirty = document.Dirty();
        const auto unchanged = [&] {
            Check(document.Token() == token && editor.Selection() == selected && document.Dirty() == dirty &&
                      document.History().UndoCount() == undo && document.History().CanRedo() == redo,
                  "duplicate input changed the document, selection or history");
        };
        std::vector<DuplicateAction> accepted;
        unsigned calls = 0, cancelled = 0;
        const auto begin = [&] {
            files.WithDuplicates(duplicatePath, [&](const std::vector<DuplicateName> *choices) {
                ++calls;
                accepted.clear();
                for (const auto &choice : *choices) accepted.push_back(choice.action);
            }, [&](DocumentFiles::LoadResult::Stop) { ++cancelled; });
            drain();
            const auto input = client.Query("document.input");
            Check(input.status == EDITOR_OK && EditorClient::Integer(input, "kind") == EDITOR_DUPLICATE_NAMES_INPUT,
                  "duplicate choices did not enter the normal file input view");
            return EditorClient::Integer(input, "input");
        };
        auto input = begin();
        auto first = client.Query("document.duplicate-rows", {IntegerArgument("input", input)});
        Check(first.status == EDITOR_OK && EditorClient::Integer(first, "total") >= 140 &&
                  EditorClient::Integer(first, "next") == 128 && EditorClient::Find(first, "rows")->fields.size() == 128,
              "duplicate input is not paged");
        const auto total = EditorClient::Integer(first, "total");
        const auto savedPage = first;
        const auto rows = client.Query("document.duplicate-rows", {IntegerArgument("input", input), IntegerArgument("first", 128)});
        Check(rows.status == EDITOR_OK && EditorClient::Integer(rows, "next") == total,
              "duplicate input lost its last page");
        first.fields.clear();
        Check(client.Query("document.duplicate-rows", {IntegerArgument("input", input)}).fields == savedPage.fields,
              "duplicate input borrowed the caller's projection storage");
        Check(client.Query("document.duplicate-rows", {IntegerArgument("input", input + 1)}).status == EDITOR_STALE &&
                  client.Query("document.duplicate-rows", {IntegerArgument("input", input), IntegerArgument("first", total + 1)}).status == EDITOR_INVALID,
              "duplicate pages accepted a different input or an invalid index");
        std::vector<uint64_t> actions(total, 0);
        const auto reply = [&](uint64_t request, const char *choice) {
            const EditorValue values{EDITOR_TEXT("actions"), EDITOR_INTEGER_LIST, {}, {}, 0, {},
                                      {actions.data(), uint32_t(actions.size())}};
            return client.Command("document.respond", {IntegerArgument("input", request), TextArgument("choice", choice), values});
        };
        actions.pop_back();
        Check(reply(input, "accept").status == EDITOR_INVALID && !calls && files.PendingDuplicates(),
              "partial duplicate choices were applied");
        actions.push_back(3);
        Check(reply(input, "accept").status == EDITOR_INVALID && !calls && files.PendingDuplicates(),
              "invalid duplicate action was applied");
        actions.back() = 0;
        actions[0] = 1;
        actions[1] = 2;
        Check(reply(input, "unsupported").status == EDITOR_INVALID && !calls, "invalid duplicate answer consumed the input");
        Check(runtime.Queue("Read copied duplicate input", [] {}, [](const std::string &) {}), "cannot prepare busy duplicate query");
        editor.NativeAvailable(false);
        const auto reads = documentReads;
        Check(client.Query("document.input").status == EDITOR_OK && client.Query("document.info").status == EDITOR_OK &&
                  client.Query("document.duplicate-rows", {IntegerArgument("input", input)}).status == EDITOR_OK &&
                  documentReads == reads, "busy duplicate queries entered the native document");
        runtime.Finish();
        editor.NativeAvailable(true);
        editor.EnterDraw();
        const auto submitted = reply(input, "accept");
        editor.LeaveDraw();
        Check(submitted.status == EDITOR_QUEUED && !calls, "duplicate answer skipped the normal command queue");
        std::fill(actions.begin(), actions.end(), UINT64_MAX);
        drain();
        Check(calls == 1 && accepted.size() == total && accepted[0] == DuplicateAction::Rename &&
                  accepted[1] == DuplicateAction::UseCurrent && accepted.back() == DuplicateAction::Ignore &&
                  !files.PendingDuplicates() && client.Query("document.input").status == EDITOR_NO_CHANGE,
              "duplicate answer borrowed a choice array or did not preserve all three actions");
        unchanged();
        Check(reply(input, "accept").status == EDITOR_STALE, "duplicate answer could be replayed");
        input = begin();
        Check(reply(input, "cancel").status == EDITOR_CANCELLED && calls == 1 && cancelled == 1,
              "cancelling a duplicate input invoked its loader");
        unchanged();
        input = begin();
        runtime.DocumentChanged();
        files.DocumentChanged();
        commands.Poll();
        Check(reply(input, "cancel").status == EDITOR_STALE &&
                  client.Query("document.input").status == EDITOR_NO_CHANGE && cancelled == 2,
              "old duplicate input survived document replacement");
        unchanged();
        files.WithDuplicates(duplicatePath, [](const std::vector<DuplicateName> *) { throw std::runtime_error("Load failed"); });
        drain();
        input = EditorClient::Integer(client.Query("document.input"), "input");
        actions.assign(total, 0);
        Check(reply(input, "accept").status == EDITOR_ERROR && !files.PendingDuplicates() &&
                  client.Query("document.input").status == EDITOR_NO_CHANGE,
              "a throwing duplicate loader retained its input");
        unchanged();
        for (const auto *choice : {"cancel", "error"}) {
            const auto loading = execute("document.merge", duplicatePath);
            drain();
            input = EditorClient::Integer(client.Query("document.input"), "input");
            Check(loading.status == EDITOR_QUEUED && commands.Busy() && input && !editor.NativeAvailable(),
                  "file operation ownership hid its duplicate-name input");
            const auto expected = std::string(choice) == "cancel" ? EDITOR_CANCELLED : EDITOR_ERROR;
            Check(reply(input, choice).status == expected && status(loading.operation) == expected &&
                      !commands.Busy() && !files.PendingDuplicates(),
                  "duplicate cancellation or presentation error retained the file operation");
            unchanged();
        }
        const auto stopped = execute("document.merge", duplicatePath);
        drain();
        Check(files.PendingDuplicates() && commands.Busy(), "cannot prepare stopped duplicate input");
        commands.Stop();
        Check(status(stopped.operation) == EDITOR_CANCELLED && !files.PendingDuplicates() && !commands.Busy(),
              "stopping a file command kept a duplicate loader alive");
        commands.Poll();
        Check(client.Query("document.input").status == EDITOR_NO_CHANGE, "a stopped duplicate input reappeared");
        unchanged();
        Check(document.New(&error), error.c_str());
        runtime.DocumentChanged();
        files.DocumentChanged();
        attach();
    }
    const auto created = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(created.ok, "create failed");
    const auto object = document.Reference(created.created);
    attach();
    auto saved = execute("document.save", path);
    Check(saved.status == EDITOR_QUEUED && saved.operation, "save has no operation identity");
    Check(status(saved.operation) == EDITOR_QUEUED, "queued save reported complete");
    drain();
    uint64_t written = 0;
    Check(status(saved.operation, &written) == EDITOR_OK && written == 1 &&
              std::filesystem::exists(std::filesystem::u8path(path)),
          "save result did not record its output");
    Check(!document.Dirty() && commands.CurrentFilePath() == path, "save point was not established");
    Check(RenameLevelObject(document, object, "Changed").ok, "rename failed");
    attach();
    const auto undoCount = document.History().UndoCount();
    auto undo = execute("history.undo");
    Check(undo.status == EDITOR_OK && !document.Dirty(), "history command was wrapped in an edit");
    Check(document.History().UndoCount() + 1 == undoCount && document.History().CanRedo(), "history semantics changed");
    Check(execute("history.redo").status == EDITOR_OK, "redo failed");
    const auto token = document.Token();
    const int detached = views.detached;
    const auto failed = execute("document.open", (root / "missing.cmo").u8string());
    Check(commands.PendingInput() && commands.PendingInput()->kind == DocumentCommands::Input::Kind::Unsaved,
          "replacement did not ask about unsaved edits");
    Check(commands.Respond(commands.PendingInput()->id, "discard") == EDITOR_QUEUED, "discard failed");
    drain();
    Check(status(failed.operation) == EDITOR_ERROR && document.Token() == token && views.detached == detached,
          "failed open detached the old document");
    Check(document.History().UndoCount() == undoCount, "failed open lost history");
    const auto documentInfo = infoClient.Query("document.info");
    Check(documentInfo.status == EDITOR_OK && EditorClient::Integer(documentInfo, "document") == document.Token().value &&
              EditorClient::Integer(documentInfo, "dirty") == uint64_t(document.Dirty()) &&
              EditorClient::Text(documentInfo, "path") == path && initialInfo.fields != documentInfo.fields,
          "document metadata did not follow saves and retained document replacement");
    const auto version = VersionedPath(commands.CurrentFilePath(), 1);
    { std::ofstream output(std::filesystem::u8path(version), std::ios::binary); output << "Previous version file"; }
    editor.SetSelection(object);
    const auto versionDirty = document.Dirty();
    failBeforePublication = true;
    const auto failedVersion = execute("document.save-version");
    Check(commands.PendingInput() && commands.PendingInput()->kind == DocumentCommands::Input::Kind::Confirm,
          "version overwrite confirmation missing");
    Check(commands.Respond(commands.PendingInput()->id, "accept") == EDITOR_QUEUED, "version overwrite answer failed");
    drain();
    failBeforePublication = false;
    Check(status(failedVersion.operation) == EDITOR_ERROR && Bytes(version) == "Previous version file" &&
          document.Token() == token && document.Dirty() == versionDirty && editor.Selection() == object &&
          document.History().UndoCount() == undoCount && commands.CurrentFilePath() == path,
          "failed version save changed document, save path, selection, history or previous file");
    const auto beforeCaptureFailure = Bytes(path);
    views.captureFailure = true;
    const auto saveBeforeFailure = execute("document.open", target);
    Check(commands.PendingInput() && commands.PendingInput()->kind == DocumentCommands::Input::Kind::Unsaved,
          "save-before-open did not request an answer");
    Check(commands.Respond(commands.PendingInput()->id, "save") == EDITOR_ERROR,
          "capture failure did not finish save-before-open");
    Check(status(saveBeforeFailure.operation) == EDITOR_ERROR && !commands.Busy() && !runtime.Busy() &&
          document.Token() == token && editor.Selection() == object && document.History().UndoCount() == undoCount &&
          Bytes(path) == beforeCaptureFailure, "capture failure lost the document or left a pending operation");
    const auto saveAsFailure = execute("document.save-as");
    Check(commands.PendingInput() && commands.Respond(commands.PendingInput()->id, "accept", target) == EDITOR_ERROR,
          "capture failure after file selection did not finish");
    Check(status(saveAsFailure.operation) == EDITOR_ERROR && !commands.Busy() && !std::filesystem::exists(target),
          "failed capture wrote a file or left a pending operation");
    views.captureFailure = false;
    auto saveCopy = execute("document.save", target);
    drain();
    Check(status(saveCopy.operation) == EDITOR_OK, "target preparation failed");
    const auto original = Bytes(target);
    Check(RenameLevelObject(document, object, "Unsaved again").ok, "second rename failed");
    attach();
    const auto cancel = execute("document.close");
    const auto oldInput = commands.PendingInput()->id;
    Check(commands.Respond(oldInput, "cancel") == EDITOR_CANCELLED, "close cancellation failed");
    Check(status(cancel.operation) == EDITOR_CANCELLED && document.Token() == token && document.Dirty(),
          "cancel modified the document");
    Check(commands.Respond(oldInput, "discard") == EDITOR_STALE, "old answer was accepted");
    // A queued invocation gets an identity before its provider executes.
    // Closing its view records rejection without starting native work.
    const auto owner = editor.OpenScope();
    editor.EnterDraw();
    const auto queued = execute("document.open", target, owner);
    editor.LeaveDraw();
    Check(queued.operation && status(queued.operation) == EDITOR_QUEUED, "deferred operation is not queryable");
    editor.CloseScope(owner);
    drain();
    Check(status(queued.operation) == EDITOR_STALE && !commands.Busy(), "closed view left a queued operation alive");
    // Saving for a replacement must use the old save path, never overwrite
    // the file that the pending Open intends to load.
    auto baselineSave = execute("document.save", path);
    drain();
    Check(status(baselineSave.operation) == EDITOR_OK, "old save destination failed");
    Check(RenameLevelObject(document, object, "Save before open").ok, "third rename failed");
    attach();
    const auto opened = execute("document.open", target);
    Check(commands.Respond(commands.PendingInput()->id, "save") == EDITOR_QUEUED, "save-before-open failed");
    drain();
    Check(status(opened.operation) == EDITOR_OK && document.Token() != token && Bytes(target) == original,
          "save-before-open overwrote its target or did not replace the document");
    Check(views.last.opened.opened && views.last.written.size() == 1 && views.last.written.front() == path,
          "replacement lost its earlier save output");
    // Accepted work belongs to the application. A closed view cannot erase a
    // write that finished before its old document scope expired.
    const auto late = (root / "late-output.cmo").u8string();
    const auto lateOwner = editor.OpenScope();
    const int received = views.finished;
    holdPublication = true;
    const auto lateSave = execute("document.save", late, lateOwner);
    editor.CloseScope(lateOwner);
    runtime.Start();
    const bool didPublish = published.get_future().wait_for(std::chrono::seconds(10)) == std::future_status::ready;
    runtime.DocumentChanged();
    release.set_value();
    drain();
    holdPublication = false;
    Check(didPublish && status(lateSave.operation, &written) == EDITOR_OK && written == 1 && !Bytes(late).empty(),
          "old receiver lost an actual file output");
    Check(views.finished == received, "old receiver changed the replacement session");
    const auto partial = (root / "published-before-error.cmo").u8string();
    failAfterPublication = true;
    const auto failedAfterWrite = execute("document.save", partial);
    drain();
    failAfterPublication = false;
    Check(status(failedAfterWrite.operation, &written) == EDITOR_ERROR && written == 1 && !Bytes(partial).empty(),
          "failure after publication hid a written file");
    Check(execute("application.quit").status == EDITOR_QUEUED && views.quit == 1, "application command failed");
}
