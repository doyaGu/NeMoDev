#include "application/DocumentCommands.h"
#include "workspace/EditorClient.h"
#include "core/document/ScriptSession.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/TopologyCommands.h"
#include "core/graph/NmsCommands.h"
#include "core/schematic/SchematicEdits.h"
#include "editor/ImGuiDrawing.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
using namespace vdev;
void Check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Views : DocumentFiles::Observer, DocumentCommands::Presentation {
    unsigned finished = 0;
    DocumentCommands::Result last;
    void ReplacingDocument(bool) override { throw std::runtime_error("A graph file must not replace the document"); }
    void ReplacementCancelled() override {}
    void DocumentReplaced() override {}
    void Log(LogLevel, const std::string &) override {}
    LayoutInterfaceData CaptureLayout() const override { return {}; }
    void FileFinished(const DocumentCommands::Result &result) override {
        last = result;
        ++finished;
    }
    void HistoryChanged() override {}
    void Quit() override {}
};
EditorField Number(const char *key, uint64_t n) { return {key, EDITOR_INTEGER_VALUE, {}, {}, n}; }
struct Saved {
    Document &doc;
    ObjectRef selection;
    EditorApplication &app;
    DocumentToken token;
    size_t undo;
    bool redo, dirty;
    uint64_t revision, topology;
    Saved(Document &doc, EditorApplication &app)
        : doc(doc), selection(app.Selection()), app(app), token(doc.Token()), undo(doc.History().UndoCount()),
          redo(doc.History().CanRedo()), dirty(doc.Dirty()), revision(doc.ChangeRevision()),
          topology(doc.TopologyRevision()) {}
    void Unchanged() const {
        Check(doc.Token() == token && doc.History().UndoCount() == undo && doc.History().CanRedo() == redo &&
                  doc.Dirty() == dirty && doc.ChangeRevision() == revision && doc.TopologyRevision() == topology &&
                  app.Selection() == selection,
              "graph file failure changed the document, selection or undo/redo");
    }
};
} // namespace
void TestGraphFileCommands(vdev::CkRuntimeHost &host) {
    using namespace vdev;
    Document doc(host);
    std::string error;
    Check(doc.New(&error), error.c_str());
    const auto script = CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(), "Graph file commands");
    const auto source = CreateBehaviorGraph(doc, script.created, script.created, "Saved graph", Point(200, 100));
    const auto target = CreateBehaviorGraph(doc, script.created, script.created, "Replaced graph", Point(600, 100));
    Check(script.ok && source.ok && target.ok &&
              CreateBehaviorGraph(doc, script.created, source.created, "Saved child", Point(200, 200)).ok,
          "graph file fixture");
    const auto folder = std::filesystem::temp_directory_path() /
                        ("graph-files-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(folder);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{folder};
    const auto path = (folder / "graph.nms").u8string(), missing = (folder / "missing.nms").u8string();
    Check(SaveBehaviorsAsNms(doc, {source.created}, path, &error), error.c_str());
    bool failBefore = false, failAfter = false;
    RuntimeOperation runtime({}, [&](const std::string &, const std::function<void()> &work) {
        if (failBefore)
            throw std::runtime_error("Before graph native work");
        work();
        if (failAfter)
            throw std::runtime_error("After graph native work");
    });
    DocumentCommands *owner = nullptr;
    EditorApplication app([&] { return &doc; }, [&] { return !runtime.Busy() && (!owner || !owner->Busy()); });
    Views views;
    DocumentFiles files([&] { return &doc; }, runtime, views);
    DocumentCommands commands(app, files, runtime, [&] { return &doc; }, views);
    owner = &commands;
    Check(commands.Register(error) && app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    const auto sync = [&] { app.Attach(doc.Token(), doc.TopologyRevision(), true, true); };
    sync();
    const auto view = app.OpenScope();
    const EditorClient client(app.Host(), view), global(app.Host(), 0);
    app.SetSelection(doc.Reference(target.created));
    doc.History().Clear();
    doc.MarkSaved();
    doc.History().MarkSavePoint();
    Check(EditScriptData(doc, script.created, "Seed redo", [](InterfaceData &d) { d.header.color ^= 1; }) &&
              doc.History().Undo(),
          "graph file Redo fixture");
    sync();
    const auto request = [&](CK_ID graph, const std::string &file) {
        EditorField at{"at", EDITOR_REAL_LIST};
        at.reals = {440, 240};
        return std::vector<EditorField>{{"target", EDITOR_OBJECT, doc.Reference(script.created)},
                                        {"graph", EDITOR_OBJECT, doc.Reference(graph)},
                                        Number("revision", doc.Session(script.created)->Revision()),
                                        Number("topology", doc.TopologyRevision()),
                                        at,
                                        {"path", EDITOR_TEXT_VALUE, {}, file}};
    };
    const auto execute = [&](const EditorClient &origin, const char *id, const std::vector<EditorField> &fields) {
        const auto values = BorrowEditorFields(fields);
        return origin.Command(id, {values.data(), values.data() + values.size()});
    };
    const auto drain = [&](EditorResponse operation) {
        Check(operation.status == EDITOR_QUEUED && operation.operation, "graph file command has no operation");
        app.EndFrame();
        runtime.Finish();
        sync();
        commands.Poll();
        return global.Completion(operation.operation);
    };
    for (const auto *command : {"schematic.import-graph", "schematic.replace-file-graph"}) {
        Saved before(doc, app);
        const auto args =
            request(command == std::string("schematic.import-graph") ? script.created : target.created, missing);
        const auto result = drain(execute(client, command, args));
        Check(result.status != EDITOR_OK && result.status != EDITOR_QUEUED && !commands.Busy() && !runtime.Busy(),
              "missing graph file command remained active");
        before.Unchanged();
        failBefore = true;
        const auto failed = drain(
            execute(client, command,
                    request(command == std::string("schematic.import-graph") ? script.created : target.created, path)));
        failBefore = false;
        Check(failed.status == EDITOR_ERROR, "pre-load exception was accepted");
        before.Unchanged();
    }
    auto stale = request(script.created, path);
    Check(EditScriptData(doc, script.created, "Change graph", [](InterfaceData &d) { d.header.color ^= 2; }),
          "make graph stale");
    sync();
    Saved beforeStale(doc, app);
    Check(drain(execute(client, "schematic.import-graph", stale)).status == EDITOR_STALE,
          "stale graph file target accepted");
    beforeStale.Unchanged();
    Check(doc.History().Undo(), "restore stale target fixture");
    sync();
    const auto imported = drain(execute(client, "schematic.import-graph", request(script.created, path)));
    Check(imported.status == EDITOR_OK && doc.History().UndoCount() == 1 && bool(views.last.graph.created),
          "valid graph import did not make one undo step");
    const auto created = views.last.graph.created;
    Check(doc.Resolve(created) && app.Selection() == doc.Reference(target.created),
          "graph import replaced editor selection");
    Check(doc.History().Undo() && !doc.Dirty() && !doc.Resolve(created), "graph import undo lost save point");
    sync();
    const auto replaced = drain(execute(client, "schematic.replace-file-graph", request(target.created, path)));
    Check(replaced.status == EDITOR_OK && doc.History().UndoCount() == 1 && doc.IsDiscarded(target.created),
          "valid graph replacement failed");
    Check(doc.History().Undo() && !doc.Dirty() && !doc.IsDiscarded(target.created), "file replacement undo failed");
    sync();
    failAfter = true;
    const auto after = drain(execute(client, "schematic.import-graph", request(script.created, path)));
    failAfter = false;
    Check(after.status == EDITOR_OK && !after.message.empty() && doc.History().UndoCount() == 1,
          "committed graph import was reported as rolled back after runner failure");
    Check(doc.History().Undo(), "undo committed graph warning");
    sync();
    // Closing the requesting view must cancel a waiting file chooser, even if
    // an asynchronous answer arrives before the regular Poll call.
    Saved beforeClose(doc, app);
    const auto waiting = execute(client, "schematic.replace-file-graph", request(target.created, {}));
    Check(waiting.status == EDITOR_QUEUED && commands.PendingInput(), "graph file chooser was not requested");
    const auto input = commands.PendingInput()->id;
    app.CloseScope(view);
    Check(commands.Respond(input, "accept", path) == EDITOR_CANCELLED && !commands.Busy() && !runtime.Busy() &&
              global.Completion(waiting.operation).status == EDITOR_CANCELLED,
          "closed Schematic accepted a late file answer");
    beforeClose.Unchanged();
}
