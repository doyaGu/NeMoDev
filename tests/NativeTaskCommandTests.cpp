#include "application/DocumentCommands.h"
#include "application/OutputCommands.h"
#include "core/document/Document.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "editor/ImGuiDrawing.h"
#include "features/resources/DataResourceCommands.h"
#include "workspace/EditorClient.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
struct Fixture : DocumentFiles::Observer, DocumentCommands::Presentation, OutputCommands::Presentation {
    CkRuntimeHost host;
    std::unique_ptr<Document> document;
    bool native = true;
    RuntimeOperation runtime{[this](const std::string &) {
                                 return (document && document->EditOpen()) || editor.HasEdits() ? "Finish the edit"
                                                                                                : std::string();
                             },
                             {},
                             [this] { return native && host.Context(); }};
    EditorApplication editor{[this] { return document.get(); }, [this] { return runtime.Available(); }};
    DocumentFiles files{[this] { return document.get(); }, runtime, *this};
    DocumentCommands fileCommands{editor, files, runtime, [this] { return document.get(); }, *this};
    OutputCommands outputs{editor, files, runtime, [this] { return document.get(); }, *this};
    DataResourceCommands resources{editor, runtime, host};
    EditorClient client{editor.Host(), 0};
    ObjectRef selected;
    DocumentToken token;
    size_t undo = 0;
    bool redo = false;
    bool dirty = false, releasedBeforePresentation = true;
    unsigned calls = 0, finished = 0;
    std::string error;
    Fixture() {
        const auto *path = std::getenv("VIRTOOLS_PATH");
        Check(host.Start(path ? path : ".", {}, &error), error.c_str());
        document = std::make_unique<Document>(host);
        Check(document->New(&error), error.c_str());
        const auto object = CreateLayoutObject(*document, LayoutObject::Frame3D);
        Check(object.ok, "create preserved object");
        selected = document->Reference(object.created);
        Check(RenameLevelObject(*document, selected, "Native admission").ok && document->History().Undo(),
              "prepare undo/redo");
        document->MarkSaved();
        document->History().MarkSavePoint();
        EditorCommandDesc command{};
        command.size = sizeof(command);
        command.id = EDITOR_TEXT("test.native");
        command.provider = EDITOR_TEXT("test.native");
        command.title = EDITOR_TEXT("Native probe");
        command.area = EDITOR_WORKSPACE;
        command.effect = EDITOR_RUNTIME_CONTROL;
        command.data = this;
        command.run = [](void *p, const EditorHost *, const EditorRequest *, EditorSink) {
            ++static_cast<Fixture *>(p)->calls;
            return EDITOR_OK;
        };
        EditorModuleDesc module{};
        module.size = sizeof(module);
        module.version = EDITOR_VERSION;
        module.id = EDITOR_TEXT("test.native");
        module.commands = &command;
        module.commandCount = 1;
        Check(fileCommands.Register(error) && outputs.Register(error) && resources.Register(error) &&
                  editor.Register(module, error) && editor.Activate(EDITOR_IMGUI_VERSION, error),
              error.c_str());
        token = document->Token();
        undo = document->History().UndoCount();
        redo = document->History().CanRedo();
        dirty = document->Dirty();
        editor.Attach(token, document->TopologyRevision(), true, true);
        editor.SetSelection(selected);
    }
    ~Fixture() {
        runtime.Finish();
        resources.Stop();
        outputs.Stop();
        fileCommands.Stop();
        editor.Stop();
    }
    void ReplacingDocument(bool) override {}
    void ReplacementCancelled() override {}
    void DocumentReplaced() override {}
    void Log(LogLevel, const std::string &) override {}
    LayoutInterfaceData CaptureLayout() const override { return {}; }
    std::unique_ptr<SchematicPrintContent> CapturePrint(std::string &message) override {
        message = "Injected print preparation failure";
        return {};
    }
    PrintChoice ChoosePrinter(const std::string &) override { throw std::runtime_error("printer must not be called"); }
    bool OpenOutput(const std::string &, std::string &) override { return true; }
    void HistoryChanged() override {}
    void Quit() override {}
    void FileFinished(const DocumentCommands::Result &) override { Presented(); }
    void OutputFinished(const OutputCommands::Result &) override { Presented(); }
    void Presented() {
        ++finished;
        releasedBeforePresentation &= runtime.Available() && editor.NativeAvailable();
    }
    void Preserved() {
        Check(document->Token() == token && document->Dirty() == dirty && editor.Selection() == selected &&
                  document->NativeId(selected) && document->History().UndoCount() == undo &&
                  document->History().CanRedo() == redo,
              "admission/cancellation changed document, selection or history");
        const std::string name = document->Resolve(selected)->GetName();
        Check(document->History().Redo() && std::string(document->Resolve(selected)->GetName()) == "Native admission" &&
                  document->History().Undo() && std::string(document->Resolve(selected)->GetName()) == name,
              "admission/cancellation changed the retained history records");
    }
    EditorResponse Start(const std::string &source) {
        const char *command = source == "file"     ? "document.open"
                              : source == "output" ? "document.export-web"
                                                   : "resource.open-library";
        return client.Command(command);
    }
    void Cancel(const std::string &source) {
        const char *query = source == "file"     ? "document.input"
                            : source == "output" ? "output.input"
                                                 : "resource.input";
        const char *command = source == "file"     ? "document.respond"
                              : source == "output" ? "output.respond"
                                                   : "resource.respond";
        const auto input = client.Query(query);
        Check(input.status == EDITOR_OK, "pending input missing");
        const auto expected = source == "file" ? EDITOR_CANCELLED : EDITOR_OK;
        Check(client.Command(command, {IntegerArgument("input", EditorClient::Integer(input, "input")),
                                       TextArgument("choice", "cancel")})
                      .status == expected,
              "cancel input");
    }
};
void InputExclusion(const std::string &source) {
    Fixture f;
    const auto input = f.Start(source);
    Check(input.status == EDITOR_QUEUED && !f.runtime.Busy() && !f.runtime.Available(),
          "input did not reserve native access");
    for (const std::string other : {"file", "output", "resource"})
        Check(f.Start(other).status == EDITOR_REFUSED, "another input workflow entered native access");
    Check(f.client.Command("test.native").status == EDITOR_REFUSED && !f.calls,
          "native command bypassed pending input");
    bool ran = false;
    Check(!f.runtime.Queue("Foreign native task", [&] { ran = true; }, {}), "unowned task bypassed pending input");
    f.Cancel(source);
    Check(f.client.Completion(input.operation).status == EDITOR_CANCELLED && f.runtime.Available() && !ran &&
              f.client.Command("test.native").status == EDITOR_OK && f.calls == 1 && f.releasedBeforePresentation,
          "canceled input retained native ownership or blocked presentation");
    f.Preserved();
}
void PreparationFailure() {
    Fixture f;
    const auto result = f.client.Command("document.print");
    Check(result.status == EDITOR_QUEUED && f.client.Completion(result.operation).status == EDITOR_ERROR &&
              f.runtime.Available() && f.finished == 1 && f.releasedBeforePresentation,
          "failed output preparation retained native access");
    const auto next = f.Start("file");
    Check(next.status == EDITOR_QUEUED, "failed output blocked a file workflow");
    f.Cancel("file");
    f.Preserved();
}
void ExpiredInput(const std::string &source) {
    Fixture f;
    const auto input = f.Start(source);
    Check(input.status == EDITOR_QUEUED, "start input");
    f.runtime.DocumentChanged();
    f.fileCommands.Poll();
    f.outputs.Poll();
    Check(f.client.Completion(input.operation).status == EDITOR_STALE && f.runtime.Available() &&
              f.releasedBeforePresentation,
          "expired input retained native access");
    f.Preserved();
}
} // namespace
int main(int argc, char **argv) {
    try {
        Check(argc == 2, "one native command case is required");
        const std::string name = argv[1];
        if (name == "FileInput")
            InputExclusion("file");
        else if (name == "OutputInput")
            InputExclusion("output");
        else if (name == "ResourceInput")
            InputExclusion("resource");
        else if (name == "PreparationFailure")
            PreparationFailure();
        else if (name == "ExpiredFileInput")
            ExpiredInput("file");
        else if (name == "ExpiredOutputInput")
            ExpiredInput("output");
        else
            throw std::runtime_error("Unknown native command case: " + name);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
