#include "application/OutputCommands.h"
#include "core/document/FileCommands.h"
#include "core/document/OutputFiles.h"
#include "core/document/SaveFileOperations.h"
#include "core/layout/LayoutCommands.h"
#include "editor/ImGuiDrawing.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <utility>

namespace {
using namespace vdev;
void Check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
std::string Bytes(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void Write(const std::filesystem::path &path, const std::string &text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    Check(bool(out), "cannot prepare output file");
}
struct Observer : DocumentFiles::Observer {
    void ReplacingDocument(bool) override {}
    void ReplacementCancelled() override {}
    void DocumentReplaced() override {}
    void Log(LogLevel, const std::string &) override {}
};
struct WebPublication : SaveFileOperations {
    enum Mode { Normal, RestoreFailure, ThrowingRestore, CompetingWriter } mode = Normal;
    std::filesystem::path page, composition, backup;
    unsigned compositionWrites = 0;
    std::error_code Replace(const std::filesystem::path &from, const std::filesystem::path &to, bool overwrite) override {
        if (mode != Normal && to == page) {
            if (mode == CompetingWriter) {
                // Preserve size and time: metadata alone cannot identify the
                // file this operation published before the other writer ran.
                const auto time = std::filesystem::last_write_time(composition);
                Write(composition, std::string(std::filesystem::file_size(composition), 'C'));
                std::filesystem::last_write_time(composition, time);
            }
            return std::make_error_code(std::errc::io_error);
        }
        if (mode != Normal && to == composition && ++compositionWrites > 1) {
            backup = from;
            if (mode == ThrowingRestore) throw std::runtime_error("Injected web restore failure");
            return std::make_error_code(std::errc::permission_denied);
        }
        return SaveFileOperations::Replace(from, to, overwrite);
    }
};
struct Pages : SchematicPrintContent {
    int &renders, &alive;
    Pages(int &renders, int &alive) : renders(renders), alive(alive) {
        width = 830;
        height = 600;
        ++alive;
    }
    ~Pages() override { --alive; }
    bool Render(float, float, int w, int h, float, const SchematicPrint &, std::vector<unsigned char> &pixels,
                std::string &) const override {
        ++renders;
        pixels.assign(size_t(w) * h * 4, 255);
        return true;
    }
};
struct Destination : PrintDestination {
    PrinterSettings settings;
    std::string fail;
    int prepared = 0, begun = 0, pages = 0, tiles = 0, ended = 0, submitted = 0, cancelled = 0;
    const PrinterSettings &Settings() const override { return settings; }
    bool PreparePages(int count, int, int, std::string &error) override {
        prepared = count;
        if (fail == "prepare") {
            error = "prepare";
            return false;
        }
        return true;
    }
    PrinterStartStatus BeginDocument(const std::string &, std::string &error) override {
        ++begun;
        if (fail == "cancel") return PrinterStartStatus::Cancelled;
        if (fail == "begin") {
            error = "begin";
            return PrinterStartStatus::Failed;
        }
        return PrinterStartStatus::Started;
    }
    bool BeginPage(std::string &error) override {
        ++pages;
        if (fail == "page") {
            error = "page";
            return false;
        }
        return true;
    }
    bool DrawTile(const unsigned char *, int, int, int, int, int, int, std::string &error) override {
        ++tiles;
        if (fail == "tile") {
            error = "tile";
            return false;
        }
        return true;
    }
    bool EndPage(std::string &error) override {
        ++ended;
        if (fail == "end") {
            error = "end";
            return false;
        }
        return true;
    }
    bool Submit(std::string &job, std::string &error) override {
        ++submitted;
        if (fail == "throw") throw std::runtime_error("submit exception");
        if (fail == "submit") {
            error = "submit";
            return false;
        }
        job = "test-printer-job-17";
        return true;
    }
    void Cancel() override { ++cancelled; }
};
struct Presentation : OutputCommands::Presentation {
    int renders = 0, alive = 0, previews = 0, completed = 0;
    PrinterSelectionStatus choice = PrinterSelectionStatus::Selected;
    std::shared_ptr<Destination> destination = std::make_shared<Destination>();
    OutputCommands::Result last;
    LayoutInterfaceData CaptureLayout() const override { return {}; }
    std::unique_ptr<SchematicPrintContent> CapturePrint(std::string &) override {
        return std::make_unique<Pages>(renders, alive);
    }
    PrintChoice ChoosePrinter(const std::string &) override {
        return {choice, destination, "printer selection failed"};
    }
    bool OpenOutput(const std::string &path, std::string &) override {
        Check(std::filesystem::exists(std::filesystem::u8path(path)), "preview opened before publication");
        ++previews;
        return true;
    }
    void OutputFinished(const OutputCommands::Result &result) override {
        last = result;
        ++completed;
    }
};
class SaveHook final : public CKBaseManager {
  public:
    std::function<void()> callback;
    explicit SaveHook(CKContext *context)
        : CKBaseManager(context, CKGUID(0x1d30fab1, 0x7cd5e119), "Output regression") {}
    CKDWORD GetValidFunctionsMask() override { return CKMANAGER_FUNC_PostSave; }
    CKERROR PostSave() override {
        if (callback) {
            auto action = std::exchange(callback, {});
            action();
        }
        return CK_OK;
    }
};
} // namespace
void TestOutputCommands(vdev::CkRuntimeHost &host) {
    using namespace vdev;
    namespace fs = std::filesystem;
    Document document(host);
    std::string error;
    Check(document.New(&error), error.c_str());
    const auto made = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(made.ok, "output object");
    const auto object = document.Reference(made.created);
    bool failAfter = false;
    RuntimeOperation runtime({}, [&](const std::string &label, const std::function<void()> &work) {
        work();
        if (failAfter && label == "Creating web page") throw std::runtime_error("after publication");
    });
    EditorApplication editor([&] { return &document; }, [&] { return runtime.Available(); });
    Observer observer;
    WebPublication publication;
    DocumentFiles files([&] { return &document; }, runtime, observer, &publication);
    Presentation view;
    OutputCommands commands(editor, files, runtime, [&] { return &document; }, view);
    Check(commands.Register(error), error.c_str());
    const EditorParameter outputPath{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE};
    const EditorCommandDesc throwing{
        sizeof(EditorCommandDesc),
        EDITOR_TEXT("test.web-throw"),
        EDITOR_TEXT("test.outputs"),
        EDITOR_TEXT("Start web export then fail"),
        &outputPath,
        1,
        [](void *, const EditorHost *host, const EditorRequest *request, EditorSink) -> EditorStatus {
            auto nested = *request;
            Check(host->execute(host->data, EDITOR_TEXT("document.export-web"), &nested, {}) == EDITOR_QUEUED,
                  "nested web work was not accepted");
            throw std::runtime_error("Outer output callback failed");
        },
        nullptr,
        EDITOR_DOCUMENT,
        EDITOR_FILE_OPERATION};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("test.outputs");
    module.commands = &throwing;
    module.commandCount = 1;
    Check(editor.Register(module, error) && editor.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    editor.Attach(document.Token(), document.TopologyRevision(), true, true);
    editor.SetSelection(object);
    const auto token = document.Token();
    const auto undo = document.History().UndoCount();
    const auto dirty = document.Dirty();
    const auto preserved = [&] {
        Check(document.Token() == token && document.Resolve(object) && editor.Selection() == object,
              "output changed document or selection");
        Check(document.History().UndoCount() == undo && document.Dirty() == dirty, "output changed history/save point");
    };
    const auto root = fs::temp_directory_path() /
                      ("output-command-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Check(fs::create_directory(root), "output folder");
    struct Cleanup {
        fs::path root;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(root, e);
        }
    } cleanup{root};
    const auto page = (root / "page.htm").u8string();
    const auto runWeb = [&](bool cmo = false, bool preview = false, EditorView scope = 0) {
        const EditorValue values[] = {
            {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, EditorText(page)},
            {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("Exported composition")},
            {EDITOR_TEXT("width"), EDITOR_INTEGER_VALUE, {}, {}, 640},
            {EDITOR_TEXT("height"), EDITOR_INTEGER_VALUE, {}, {}, 480},
            {EDITOR_TEXT("cmo"), EDITOR_INTEGER_VALUE, {}, {}, cmo ? 1u : 0u},
            {EDITOR_TEXT("preview"), EDITOR_INTEGER_VALUE, {}, {}, preview ? 1u : 0u}};
        return editor.Execute("document.export-web", {editor.Scope(scope), values, 6});
    };
    for (bool cmo : {false, true}) {
        const auto result = runWeb(cmo, true);
        Check(result.status == EDITOR_QUEUED && commands.Busy(), "web has no owned operation");
        runtime.Finish();
        Check(view.last.status == EDITOR_OK && view.last.written.size() == 2 && !commands.Busy(),
              ("web output failed: " + view.last.message).c_str());
        Check(Bytes(fs::u8path(page)).find("<TITLE>Exported composition</TITLE>") != std::string::npos,
              "web title changed");
        Check(Bytes(fs::u8path(page)).find("WIDTH=\"640\" HEIGHT=\"480\"") != std::string::npos,
              "web dimensions changed");
        Check(IsPlayerFile(WebPageCompositionPath(page, cmo)) == !cmo, "web player flags changed");
        preserved();
    }
    Check(view.previews == 2, "successful web preview was not opened");
    const auto previousPage = Bytes(fs::u8path(page));
    const auto composition = WebPageCompositionPath(page, false);
    const auto previousComposition = Bytes(fs::u8path(composition));
#ifndef _WIN32
    // The native save callback makes only the final HTML destination read-only
    // after both candidates exist. Composition publication therefore succeeds
    // before HTML publication fails, exercising reverse restoration.
    auto *hook = new SaveHook(document.Context());
    Check(document.Context()->RegisterNewManager(hook) == CK_OK, "save hook");
    const auto permissions = fs::status(fs::u8path(page)).permissions();
    hook->callback = [&] {
        fs::permissions(fs::u8path(page), fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);
    };
    runWeb(false, true);
    runtime.Finish();
    fs::permissions(fs::u8path(page), permissions);
    Check(view.last.status == EDITOR_ERROR && view.last.written.empty() && view.previews == 2,
          "partial web output was reported successful");
    Check(Bytes(fs::u8path(page)) == previousPage && Bytes(fs::u8path(composition)) == previousComposition,
          "web publication failure lost old files");
    preserved();
#endif
    for (auto mode : {WebPublication::RestoreFailure, WebPublication::ThrowingRestore, WebPublication::CompetingWriter}) {
        Write(fs::u8path(page), previousPage);
        Write(fs::u8path(composition), previousComposition);
        publication.page = fs::canonical(fs::u8path(page));
        publication.composition = fs::canonical(fs::u8path(composition));
        publication.mode = mode;
        publication.compositionWrites = 0;
        publication.backup.clear();
        const auto previews = view.previews;
        runWeb(false, true);
        runtime.Finish();
        publication.mode = WebPublication::Normal;
        Check(view.last.status == EDITOR_ERROR && view.previews == previews &&
                  view.last.written == std::vector<std::string>{composition} && Bytes(fs::u8path(page)) == previousPage,
              "web recovery failure lost its output report or opened a preview");
        // The backup path remains in the completion diagnostics even when
        // restoration was deliberately skipped to preserve an outside write.
        fs::path backup = publication.backup;
        if (backup.empty()) {
            for (const auto &entry : fs::directory_iterator(root))
                if (entry.path().filename().u8string().find(".nemodev-save-") == 0 && Bytes(entry.path()) == previousComposition)
                    backup = entry.path();
        }
        Check(!backup.empty() && Bytes(backup) == previousComposition &&
                  view.last.message.find(backup.u8string()) != std::string::npos,
              "web recovery failure discarded its old composition or recovery path");
        if (mode == WebPublication::CompetingWriter)
            Check(Bytes(fs::u8path(composition)) == std::string(fs::file_size(fs::u8path(composition)), 'C'),
                  "web rollback overwrote an outside writer");
        Check(fs::copy_file(backup, fs::u8path(composition), fs::copy_options::overwrite_existing) && fs::remove(backup),
              "web recovery backup was unusable");
        preserved();
    }
    // A completion failure after publication must still record both outputs;
    // preview only follows a successful complete native operation.
    failAfter = true;
    const auto receiver = editor.OpenScope();
    const auto late = runWeb(false, true, receiver);
    Check(late.status == EDITOR_QUEUED, "late web invocation was refused");
    editor.CloseScope(receiver);
    runtime.Finish();
    failAfter = false;
    if (!(view.last.status == EDITOR_ERROR && view.last.written.size() == 2 && view.previews == 2))
        throw std::runtime_error("late web status=" + std::to_string(view.last.status) +
                                 ", outputs=" + std::to_string(view.last.written.size()) +
                                 ", previews=" + std::to_string(view.previews) + ", message=" + view.last.message);
    preserved();
    const EditorValue outputPathValue{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, EditorText(page)};
    const auto outerFailed = editor.Execute("test.web-throw", {editor.Scope(), &outputPathValue, 1});
    Check(outerFailed.status == EDITOR_QUEUED, "outer callback erased accepted asynchronous work");
    runtime.Finish();
    const EditorValue operation{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, outerFailed.operation};
    const auto completed = editor.Query("application.operation", {editor.Scope(), &operation, 1});
    bool callbackError = false, outputsRetained = false, failed = false;
    for (const auto &field : completed.fields) {
        if (field.key == "message" && field.text.find("Command callback raised an exception") != std::string::npos)
            callbackError = true;
        if (field.key == "written" && field.integer == 2) outputsRetained = true;
        if (field.key == "status" && field.integer == EDITOR_ERROR) failed = true;
    }
    Check(callbackError && outputsRetained && failed && !commands.Busy(),
          "outer callback failure lost native completion, output paths or diagnostics");
    preserved();
    const auto input = [&] { return editor.Query("output.input", {editor.Scope(), nullptr, 0}); };
    const auto number = [](const EditorResponse &data, const char *key) {
        for (const auto &f : data.fields)
            if (f.key == key) return f.integer;
        return uint64_t(0);
    };
    const auto respond = [&](uint64_t id, const char *choice) {
        const EditorValue values[] = {{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE, {}, {}, id},
                                      {EDITOR_TEXT("choice"), EDITOR_TEXT_VALUE, {}, EditorLiteral(choice)}};
        return editor.Execute("output.respond", {editor.Scope(), values, 2});
    };
    // Owned input survives a view closing; old answers and a replaced document
    // cannot submit a different print job.
    const auto printing = [&] { return editor.Execute("document.print", {editor.Scope(), nullptr, 0}); };
    for (const auto *failure : {"prepare", "cancel", "begin", "page", "tile", "end", "submit", "throw", ""}) {
        view.destination = std::make_shared<Destination>();
        view.destination->fail = failure;
        const auto op = printing();
        Check(op.status == EDITOR_QUEUED && view.alive == 1, "print did not capture owned data");
        const auto pending = input();
        const auto id = number(pending, "input");
        Check(id && number(pending, "kind") == EDITOR_PRINT_INPUT, "print input projection missing");
        const EditorValue invalid[] = {{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE, {}, {}, id},
                                       {EDITOR_TEXT("field"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("per-mm-h")},
                                       {EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE, {}, {}, ~uint64_t(0)}};
        Check(editor.Execute("output.print-setting", {editor.Scope(), invalid, 3}).status == EDITOR_INVALID,
              "print integer overflow accepted");
        Check(respond(id, "accept").status == EDITOR_OK, "print answer failed");
        const auto expected = std::string(failure).empty()       ? EDITOR_OK
                              : std::string(failure) == "cancel" ? EDITOR_CANCELLED
                                                                 : EDITOR_ERROR;
        Check(view.last.status == expected && !commands.Busy() && view.alive == 0,
              "print failure did not clean owned state");
        if (expected == EDITOR_OK)
            Check(view.last.printerJob == "test-printer-job-17" && view.destination->submitted == 1,
                  "printer job identity was lost");
        else
            Check(view.destination->cancelled >= 1, "failed print was not cancelled");
        Check(respond(id, "accept").status == EDITOR_STALE, "print accepted an old reply");
        preserved();
    }
    view.choice = PrinterSelectionStatus::Cancelled;
    printing();
    Check(view.last.status == EDITOR_CANCELLED && !commands.Busy() && view.alive == 0,
          "printer selection cancellation leaked state");
    view.choice = PrinterSelectionStatus::Failed;
    printing();
    Check(view.last.status == EDITOR_ERROR && !commands.Busy() && view.alive == 0,
          "printer selection failure leaked state");
    view.choice = PrinterSelectionStatus::Selected;
    view.destination = std::make_shared<Destination>();
    printing();
    const auto stale = number(input(), "input");
    runtime.DocumentChanged();
    commands.Poll();
    Check(!commands.Busy() && view.last.status == EDITOR_STALE && view.alive == 0 &&
              respond(stale, "accept").status == EDITOR_STALE,
          "document replacement retained print input");
    // Common multi-file publication: names must be distinct, and a failure
    // never exposes a half-declared set.
    SaveReport report;
    {
        OutputFiles outputs(report);
        const auto a = outputs.Add((root / "same.txt").u8string());
        Check(!a.empty() && outputs.Add((root / "same.txt").u8string()).empty(), "output aliases accepted");
    }
    // Failure to restore one output cannot prevent restoration of the others.
    // Backups must remain usable after the OutputFiles owner is destroyed.
    for (bool throws : {false, true}) {
        struct RecoveryFailure : SaveFileOperations {
            std::filesystem::path blocked, recovery, backup;
            std::map<std::filesystem::path, unsigned> calls;
            std::vector<std::filesystem::path> restored;
            bool throws = false;
            std::error_code Replace(const std::filesystem::path &from, const std::filesystem::path &to,
                                   bool overwrite) override {
                if (++calls[to] > 1) restored.push_back(to);
                if (to == blocked) return std::make_error_code(std::errc::io_error);
                if (to == recovery && calls[to] > 1) {
                    backup = from;
                    if (throws) throw std::filesystem::filesystem_error("Injected output recovery failure", from, to,
                                                                      std::make_error_code(std::errc::permission_denied));
                    return std::make_error_code(std::errc::permission_denied);
                }
                return SaveFileOperations::Replace(from, to, overwrite);
            }
        } failure;
        const auto first = root / "recover-first.txt", second = root / "recover-second.txt", third = root / "recover-third.txt";
        Write(first, "Previous first");
        Write(second, "Previous second");
        Write(third, "Previous third");
        failure.blocked = fs::canonical(third);
        failure.recovery = fs::canonical(second);
        failure.throws = throws;
        SaveReport recovered;
        std::vector<std::string> written{"Earlier output"};
        {
            OutputFiles outputs(recovered, &failure);
            Write(outputs.Add(first.u8string()), "New first");
            Write(outputs.Add(second.u8string()), "New second");
            Write(outputs.Add(third.u8string()), "New third");
            Check(!outputs.Publish(written), "blocked third output was published");
        }
        Check(Bytes(first) == "Previous first" && Bytes(second) == "New second" && Bytes(third) == "Previous third",
              "one recovery failure prevented the other old outputs from being restored");
        Check(failure.restored == std::vector<fs::path>({fs::canonical(second), fs::canonical(first)}),
              "outputs were not recovered exactly once in reverse order");
        Check(written == std::vector<std::string>({"Earlier output", second.u8string()}), "remaining outputs were misreported");
        Check(Bytes(failure.backup) == "Previous second" &&
                  std::any_of(recovered.notes.begin(), recovered.notes.end(), [&](const auto &note) {
                      return note.find(failure.backup.u8string()) != std::string::npos;
                  }), "recovery backup or its location was discarded");
        Check(fs::copy_file(failure.backup, second, fs::copy_options::overwrite_existing) && Bytes(second) == "Previous second",
              "retained recovery backup could not restore the old file");
        Check(fs::remove(failure.backup), "remove manually recovered backup");
    }
    for (const auto &entry : fs::directory_iterator(root))
        Check(entry.path().filename().u8string().find(".nemodev-save-") != 0, "output temporary file leaked");
}
