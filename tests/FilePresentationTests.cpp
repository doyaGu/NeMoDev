#include "application/DocumentCommands.h"
#include "application/EditorModules.h"
#include "application/FileLoadState.h"
#include "features/layout/LayoutCommands.h"
#include "features/layout/LayoutSceneData.h"
#include "ui/native/EditorSession.h"
#include "workspace/EditorWorkspace.h"
#include "workspace/EditorClient.h"
#include "core/level/LevelCommands.h"
#include "editor/ImGuiDrawing.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cstdlib>

namespace {
using namespace vdev;
void Check(bool ok, const std::string &message) { if (!ok) throw std::runtime_error(message); }
struct Fixture : DocumentCommands::Presentation {
    EditorSession session;
    EditorApplication editor{[this] { return session.document.get(); }, [this] { return session.runtimeOperation.Available(); }};
    EditorWorkspace workspace{editor};
    DocumentCommands files{editor, session.files, session.runtimeOperation, [this] { return session.document.get(); }, *this};
    LayoutCommands layout{editor, [this] { return session.document.get(); }, [](EditorView) { return LayoutTarget{}; }, session.objectSelection};
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("nemodev-file-presentation-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string error;
    unsigned notices = 0, navigation = 0, externalNavigation = 0, reports = 0;
    bool pending = false, failNotice = false;
    DocumentCommands::Result last;
    explicit Fixture(const std::string &module = {}, const std::string &disabled = {}) {
        ImGui::CreateContext(); ImGui::GetIO().IniFilename = nullptr;
        std::filesystem::create_directories(root);
        const auto *runtime = std::getenv("VIRTOOLS_PATH");
        Check(session.host.Start(runtime ? runtime : ".", {}, &error), error);
        session.document = std::make_unique<Document>(session.host);
        Check(session.document->New(&error), error);
        Check(CreateScript(*session.document, session.host.Context()->GetCurrentLevel()->GetID(), "File Script").ok, "Cannot seed script");
        EditorModuleDesc views{};
        views.size = sizeof(views); views.version = EDITOR_VERSION; views.id = EDITOR_TEXT("test.file-views"); views.data = this;
        EditorViewDesc desc{};
        desc.size = sizeof(desc); desc.imguiVersion = EDITOR_IMGUI_VERSION; desc.id = EDITOR_TEXT("script.schematic");
        desc.provider = EDITOR_TEXT("test.builtin"); desc.title = EDITOR_TEXT("Schematic"); desc.area = EDITOR_DOCUMENT;
        desc.data = this; desc.singleInstance = 1;
        desc.create = [](void *data, const EditorHost *, EditorView) { return data; };
        desc.destroy = [](void *) {};
        desc.draw = [](void *, const EditorHost *, EditorView, EditorViewFrame *) {};
        static const EditorParameter fields[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, 1}, {EDITOR_TEXT("background"), EDITOR_INTEGER_VALUE, 1}};
        desc.parameters = fields; desc.count = 2;
        desc.navigate = [](void *data, const EditorHost *, const EditorRequest *request) {
            auto &self = *static_cast<Fixture *>(data); ++self.navigation;
            Check(request->scope.document == self.session.document->Token(), "Navigation used the previous document");
            Check(request->count && self.session.document->Resolve(request->values[0].object), "Navigation used a stale object");
            return EDITOR_OK;
        };
        auto replacement = desc; replacement.provider = EDITOR_TEXT("test.external");
        replacement.navigate = [](void *data, const EditorHost *, const EditorRequest *request) {
            auto &self = *static_cast<Fixture *>(data); ++self.externalNavigation;
            Check(request->scope.document == self.session.document->Token(), "Replacement received an old scope");
            return EDITOR_OK;
        };
        auto viewport = desc; viewport.id = EDITOR_TEXT("layout.viewport"); viewport.provider = EDITOR_TEXT("test.viewport");
        const EditorViewDesc providers[] = {desc, replacement}; views.views = providers; views.viewCount = 2;
        EditorModuleDesc viewportModule{}; viewportModule.size = sizeof(viewportModule); viewportModule.version = EDITOR_VERSION;
        viewportModule.id = EDITOR_TEXT("test.file-viewport"); viewportModule.views = &viewport; viewportModule.viewCount = 1;
        views.document = [](void *data, const EditorHost *, const EditorDocumentChange *change) {
            auto &self = *static_cast<Fixture *>(data);
            if (change->phase == EDITOR_DOCUMENT_REPLACED) self.pending = false;
            if (change->phase == EDITOR_DOCUMENT_LOADED) {
                ++self.notices; self.pending = true;
                if (self.failNotice) throw std::runtime_error("Injected file notice failure");
            }
            return EDITOR_OK;
        };
        views.update = [](void *data, const EditorHost *host, EditorModulePhase phase) {
            auto &self = *static_cast<Fixture *>(data);
            if (phase != EDITOR_BEFORE_VIEWS || !self.pending) return;
            FileLoadState loaded; Check(ReadFileLoadState(host, loaded), "Cannot read file state"); self.pending = false;
            if (!loaded.script) return;
            const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, loaded.script};
            const EditorRequest request{host->scope(host->data, 0), &target, 1};
            Check(host->navigateView(host->data, EditorString EDITOR_TEXT("script.schematic"), 0, &request, {}) == EDITOR_OK,
                  "Cannot route loaded script to selected provider");
        };
        std::vector<EditorModuleSource> sources{
            {"workspace", [&](auto &, auto &message) { return workspace.Register(message); }},
            {"viewport", [&](auto &app, auto &message) { return app.Register(viewportModule, message); }},
            {"files", [&](auto &, auto &message) { return files.Register(message); }},
            {"layout", [&](auto &, auto &message) { return layout.Register(message); }},
            {"views", [&](auto &app, auto &message) { return app.Register(views, message); }}};
        EditorConfiguration config; config.providers.push_back({"view", "script.schematic", "test.builtin"});
        if (!module.empty()) {
            std::vector<EditorModuleSource> external;
            Check(DiscoverEditorModules({}, {module}, external, error), error);
            sources.insert(sources.end(), external.begin(), external.end());
        }
        std::vector<EditorModuleSetting> settings;
        if (!disabled.empty()) settings.push_back({disabled, false});
        if (disabled == "test.file-views") config.providers.clear();
        Check(editor.Assemble(sources, EDITOR_IMGUI_VERSION, error, config, settings), error);
        editor.Attach(session.document->Token(), session.document->TopologyRevision(), true, true);
        session.documentViews = &workspace;
        Check(editor.StartModules(error), error);
    }
    ~Fixture() {
        files.Stop(); session.FinishRuntimeOperations(); session.documentViews = nullptr;
        workspace.CloseAll(); editor.Stop(); session.document.reset(); session.host.Stop(); ImGui::DestroyContext();
        std::error_code error; std::filesystem::remove_all(root, error);
    }
    LayoutInterfaceData CaptureLayout() const override { return {}; }
    void FileFinished(const DocumentCommands::Result &result) override { ++reports; last = result; }
    void HistoryChanged() override {}
    void Quit() override {}
    std::string Save() {
        const auto path = (root / "original.cmo").u8string(); SaveReport report;
        LayoutInterfaceData data; data.present = true;
        if (!session.document->Scripts().empty()) data.selection = {session.document->Scripts().front().id};
        Check(SaveComposition(*session.document, path, {&data}, {}, report), "Cannot seed composition"); return path;
    }
    EditorStatus Open(const std::string &path) {
        const EditorValue argument{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, EditorText(path)};
        auto result = editor.Execute("document.open", {editor.Scope(), &argument, 1});
        Check(result.status == EDITOR_QUEUED, "Open was not queued");
        editor.EndFrame();
        if (const auto *input = files.PendingInput(); input && input->kind == DocumentCommands::Input::Kind::Unsaved)
            Check(files.Respond(input->id, "discard") == EDITOR_QUEUED, "Cannot discard unsaved state for test load");
        session.FinishRuntimeOperations(); files.Poll();
        return last.status;
    }
    void Present() { Check(editor.UpdateModules(EDITOR_BEFORE_VIEWS), "Module presentation failed"); }
};
void Loaded(const std::string &mode, const std::string &module = {}) {
    Fixture f(module); const auto path = f.Save();
    if (mode == "Replacement") {
        auto config = f.editor.Configuration(); config.providers.front().provider = "test.external";
        Check(f.workspace.Configure(config, f.error), f.error);
    }
    if (mode == "NoticeFailure") f.failNotice = true;
    Check(f.Open(path) == EDITOR_OK && f.notices == 1 && f.reports == 1, "Successful open lost completion");
    FileLoadState state;
    Check(ReadFileLoadState(f.editor.Host(), state) && state.opened && state.composition && state.script && !state.imported,
          "Loaded composition metadata is incorrect");
    Check(f.navigation == 0 && f.externalNavigation == 0, "Read-only notification navigated immediately");
    f.Present();
    Check(mode == "Replacement" ? f.externalNavigation == 1 && !f.navigation : f.navigation == 1, "Wrong view provider used");
    const auto before = f.navigation + f.externalNavigation;
    for (int i = 0; i < 20; ++i) f.Present();
    Check(f.navigation + f.externalNavigation == before, "Stable frames repeated file navigation");
    if (mode == "NoticeFailure") Check(f.last.message.find("Injected file notice failure") != std::string::npos,
                                      "Notice error was lost after the document committed");
    if (!module.empty()) {
        const auto result = EditorClient(f.editor.Host(), 0).Query("test.file-notices");
        Check(EditorClient::Integer(result, "notices") == 1 && EditorClient::Integer(result, "readable") &&
                  EditorClient::Integer(result, "refused"), "External module used a different file notification path");
    }
}
void FailedOpen() {
    Fixture f; const auto token = f.session.document->Token();
    const auto undo = f.session.document->History().UndoCount(); const auto dirty = f.session.document->Dirty();
    auto selected = f.session.document->Reference(f.session.document->Scripts().front().id); f.editor.SetSelection(selected);
    const auto path = f.root / "broken.nmo"; std::ofstream(path) << "broken";
    Check(f.Open(path.u8string()) == EDITOR_ERROR && !f.notices, "Failed load published a success notice");
    f.Present();
    Check(!f.navigation && f.session.document->Token() == token && f.session.document->Dirty() == dirty &&
        f.session.document->History().UndoCount() == undo && f.editor.Selection() == selected, "Failed load changed original editing state");
}
void Nmo(const std::string &path) {
    Fixture f; Check(f.Open(path) == EDITOR_OK, "NMO open failed"); FileLoadState state;
    const bool readable = ReadFileLoadState(f.editor.Host(), state);
    Check(readable && state.opened && !state.composition,
          "NMO load metadata: readable=" + std::to_string(readable) + " opened=" + std::to_string(state.opened) +
          " composition=" + std::to_string(state.composition) + " native origin=" +
          std::to_string(int(f.session.document->CreatedFrom())) + " path=" + f.last.path); f.Present();
}
void Disabled(const std::string &module, bool views) {
    Fixture f(module, views ? "test.file-views" : "test.file-observer"); const auto path = f.Save();
    Check(f.Open(path) == EDITOR_OK, "Disabled module prevented opening"); f.Present();
    const auto notice = EditorClient(f.editor.Host(), 0).Query("test.file-notices");
    if (views) Check(f.notices == 0 && f.navigation == 0 && EditorClient::Integer(notice, "notices") == 1,
                     "Disabled views still handled completion");
    else Check(f.navigation == 1 && notice.status == EDITOR_INVALID, "Disabled external module remained active");
}
void SwitchBeforePresentation() {
    Fixture f; const auto path = f.Save(); Check(f.Open(path) == EDITOR_OK, "Open failed");
    auto config = f.editor.Configuration(); config.providers.front().provider = "test.external";
    Check(f.workspace.Configure(config, f.error), f.error); f.Present();
    Check(f.externalNavigation == 1 && !f.navigation, "Completion retained an old builtin instance");
}
void ReplacedBeforePresentation() {
    Fixture f; const auto path = f.Save(); Check(f.Open(path) == EDITOR_OK, "Open failed");
    Check(f.session.files.New(DefaultNewSceneEnvironment(), [](bool ok) { Check(ok, "New failed"); }), "New not accepted");
    f.session.FinishRuntimeOperations(); f.Present(); FileLoadState loaded;
    Check(ReadFileLoadState(f.editor.Host(), loaded) && !loaded.opened && !loaded.script && !f.navigation,
          "Old file completion navigated a replacement document");
}
void ImportOrMerge(bool merge, const std::string &library) {
    Fixture f;
    const auto path = merge ? f.Save() : library;
    const auto token = f.session.document->Token();
    const auto history = f.session.document->History().UndoCount();
    const EditorValue argument{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, EditorText(path)};
    Check(f.editor.Execute(merge ? "document.merge" : "document.import", {f.editor.Scope(), &argument, 1}).status == EDITOR_QUEUED,
          "Import/merge was not queued");
    f.editor.EndFrame();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (f.files.Busy() || f.session.runtimeOperation.Busy()) {
        f.session.PollRuntimeOperations(); f.files.Poll();
        if (const auto *input = f.files.PendingInput(); input && input->kind == DocumentCommands::Input::Kind::DuplicateNames)
            Check(f.files.Respond(input->id, "accept") == EDITOR_QUEUED, "Cannot answer duplicate names");
        f.session.runtimeOperation.Start();
        Check(std::chrono::steady_clock::now() < deadline, "Import/merge exceeded its deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    FileLoadState state;
    Check(f.last.status == EDITOR_OK && f.notices == 1 && ReadFileLoadState(f.editor.Host(), state) &&
          state.imported && !state.opened && !state.composition && f.session.document->Token() == token,
          "Import/merge did not publish current-document loading state");
    if (merge) Check(f.session.document->History().UndoCount() == history, "Merge changed existing undo history");
    if (state.script) Check(f.session.document->Resolve(state.script), "Import returned an invalid Script");
    f.Present();
}
void LayoutProjection() {
    Fixture f; const auto path = f.Save(); Check(f.Open(path) == EDITOR_OK, "Open failed");
    const auto state = EditorClient(f.editor.Host(), 0).Query("layout.file-interface");
    const auto *objects = EditorClient::Find(state, "objects"); FileLoadState loaded;
    Check(ReadFileLoadState(f.editor.Host(), loaded) && state.status == EDITOR_OK && objects && objects->objects.size() == 1 &&
          objects->objects.front() == loaded.script && f.session.document->Resolve(loaded.script),
          "Saved interface projection lost remapped object identities");
    const EditorClient client(f.editor.Host(), 0);
    auto links = client.Query("document.file-invalid-links");
    Check(links.status == EDITOR_OK && EditorClient::Integer(links, "total") == 0, "Valid file returned invalid links");
    const EditorValue count{EDITOR_TEXT("count"), EDITOR_INTEGER_VALUE, {}, {}, 129};
    Check(client.Query("document.file-invalid-links", {count}).status == EDITOR_INVALID, "Invalid link page limit was not enforced");
}
void EmptyScript() {
    Fixture f;
    Check(f.session.document->New(&f.error), f.error);
    f.editor.Attach(f.session.document->Token(), f.session.document->TopologyRevision(), true, true);
    const auto path = f.Save(); Check(f.Open(path) == EDITOR_OK, "Empty composition failed to open");
    FileLoadState state;
    Check(ReadFileLoadState(f.editor.Host(), state) && state.opened && state.composition && !state.script,
          "Empty composition returned an invalid Script identity");
    f.Present(); Check(!f.navigation, "Empty composition attempted Script navigation");
}
void Reports(const std::string &mode) {
    using Action = DocumentCommands::Action;
    DocumentCommands::Result value; value.path = "requested.cmo"; value.status = EDITOR_OK;
    value.action = mode == "PlayerReport" ? Action::Player : Action::Save;
    value.saved.saved = mode != "PlayerReport"; value.saved.report.saved = value.saved.saved;
    value.saved.report.documentSaved = value.saved.saved; value.written = {"actual.cmo"};
    value.saved.report.chunksWritten = 3; value.saved.report.scriptsVerified = 2;
    if (mode == "CancelledReport") value.status = EDITOR_CANCELLED;
    if (mode == "PlayerReport") value.saved.report.errors = {"failure"};
    const auto report = DocumentCommands::Describe(value);
    if (mode == "CancelledReport") Check(report.text.empty() && report.logs.empty() && report.recentFile.empty(), "Cancelled save showed a failure report");
    else if (mode == "PlayerReport") Check(report.text == "Save Level Error: failure" && report.title.empty(), "Player failure report changed");
    else Check(report.recentFile == "actual.cmo" && report.title == "Save report" && report.format == "report" &&
                   report.text.find("Saved actual.cmo") == 0 && report.text.find("Scripts verified in a new context: 2") != std::string::npos,
               "Successful save report lost actual output or verification details");
}
}
int main(int argc, char **argv) {
    try {
        Check(argc >= 2, "One file presentation case is required"); const std::string name = argv[1];
        if (name == "Import" && argc == 3) ImportOrMerge(false, argv[2]);
        else if (name == "Merge") ImportOrMerge(true, {});
        else if (name == "LayoutProjection") LayoutProjection();
        else if (name == "EmptyScript") EmptyScript();
        else if (name == "DisabledModule" && argc == 3) Disabled(argv[2], false);
        else if (name == "DisabledViews" && argc == 3) Disabled(argv[2], true);
        else if (name == "ProviderChanged") SwitchBeforePresentation();
        else if (name == "DocumentChanged") ReplacedBeforePresentation();
        else if (name == "FailedOpen") FailedOpen();
        else if (name == "Nmo" && argc == 3) Nmo(argv[2]);
        else if (name.find("Report") != std::string::npos) Reports(name);
        else Loaded(name, argc == 3 ? argv[2] : "");
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
