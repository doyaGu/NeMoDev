#include "ui/native/EditorSession.h"
#include "core/document/Edit.h"
#include "core/schematic/SchematicNavigation.h"
#include "core/document/FileCommands.h"
#include "core/graph/ScriptCommands.h"
#include "core/layout/LayoutCommands.h"
#include "core/runtime/VirtoolsRuntimeLayout.h"
#include "VirtoolsRuntimeCheck.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <SDL3/SDL.h>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)

namespace {
struct Folder {
    std::filesystem::path path = std::filesystem::current_path() /
        ("document-files-" + std::to_string(SDL_GetTicksNS()));
    Folder() { CHECK(std::filesystem::create_directory(path)); }
    ~Folder() { std::error_code error; std::filesystem::remove_all(path, error); }
    std::string File(const char *name) const { return (path / name).u8string(); }
};

// Exercise the same duplicate-check/answer/load sequence as the UI, with
// production runtime scheduling and native files; only the dialog is absent.
void Drain(vdev::EditorSession &state) {
    for (int step = 0; step < 10; ++step) {
        state.FinishRuntimeOperations();
        if (!state.files.PendingDuplicates()) return;
        state.files.ResolveDuplicates(true);
    }
    throw std::runtime_error("file workflow did not finish");
}

class FileViews : public vdev::DocumentFiles::Views {
public:
    vdev::Document &document;
    CKContext *expected = nullptr;
    bool detached = false;
    const std::thread::id thread = std::this_thread::get_id();
    explicit FileViews(vdev::Document &document) : document(document) {}
    void DetachDocument(bool) override {
        CHECK(std::this_thread::get_id() == thread);
        CHECK(!expected || document.Context() == expected);
        detached = true;
    }
    void RestoreDocument() override { detached = false; }
    bool DocumentInteractionActive() const override { return false; }
};

class LoadFailure : public CKBaseManager {
public:
    bool armed = false, fired = false;
    explicit LoadFailure(CKContext *context)
        : CKBaseManager(context, CKGUID(0x652d173b, 0x4f1c890e), "File workflow regression") {}
    CKDWORD GetValidFunctionsMask() override { return CKMANAGER_FUNC_PostLoad; }
    CKERROR PostLoad() override {
        if (armed) {
            armed = false;
            fired = true;
            throw std::runtime_error("Injected file workflow failure");
        }
        return CK_OK;
    }
};

void Workflows() {
    vdev::EditorSession state;
    std::string error;
    CHECK(state.host.Start(VirtoolsRuntimeDirectory(SDL_GetBasePath()), [&state](vdev::LogLevel level, const std::string &message) {
        state.RuntimeLog(level, message);
    }, &error));
    state.document = std::make_unique<vdev::Document>(state.host);
    CHECK(state.document->New(&error));
    Folder folder;
    const auto mainThread = std::this_thread::get_id();
    auto &document = *state.document;
    const auto frame = vdev::CreateLayoutObject(document, vdev::LayoutObject::Frame3D);
    CHECK(frame.ok);
    const auto script = vdev::CreateScript(document, frame.created, "Workflow Script");
    CHECK(script.ok);
    const auto cmo = folder.File("source.cmo"), nmo = folder.File("objects.nmo");
    CHECK(vdev::SaveObjectsAs(document, {frame.created}, nmo, &error));
    vdev::LayoutInterfaceData layout;
    bool saved = false;
    CHECK(state.files.Save(cmo, layout, {}, [&](const vdev::DocumentFiles::SaveResult &result) {
        CHECK(std::this_thread::get_id() == mainThread);
        saved = result.saved && result.clean && result.report.documentSaved;
    }));
    Drain(state);
    CHECK(saved && !document.Dirty());
    CHECK(vdev::RenameLevelObject(document, document.Reference(frame.created), "Undo then redo after failed file operation").ok);
    CHECK(document.History().Undo());
    vdev::Selection schematicSelection;
    schematicSelection.Bind([&document](uint32_t script, const vdev::ElementRef &ref) {
        return vdev::ElementAddress::Capture(document, script, ref);
    });
    schematicSelection.Set(script.created, vdev::Selection::HeaderOf(script.created));
    state.objectSelection.Restore(*state.document, {frame.created});
    const auto context = document.Context();
    FileViews views(document);
    views.expected = context;
    state.documentViews = &views;
    bool &detached = views.detached;
    const auto selection = schematicSelection;
    const auto objects = state.objectSelection;
    const auto undo = document.History().UndoCount();
    const auto redo = document.History().RedoLabel();
    const auto dirty = document.Dirty();
    const auto preserved = [&] {
        CHECK(document.Context() == context && document.Dirty() == dirty);
        CHECK(schematicSelection.script == selection.script && schematicSelection.items == selection.items);
        CHECK(state.objectSelection.Objects() == objects.Objects() && state.objectSelection.Revision() == objects.Revision());
        CHECK(document.History().UndoCount() == undo && document.History().CanRedo() && document.History().RedoLabel() == redo);
    };
    // Replacing/closing requires already-detached views. Reject attempts
    // during native work or a gesture before releasing any session owners.
    bool rejectedCallback = false;
    const auto originalScope = state.runtimeOperation.CurrentDocument();
    std::promise<void> entered, release;
    auto gate = release.get_future();
    CHECK(state.RunRuntime("Exclusive replacement regression", [&] { entered.set_value(); gate.wait(); }));
    state.runtimeOperation.Start();
    entered.get_future().wait();
    const bool newRejected = !state.files.New(vdev::DefaultNewSceneEnvironment(), [&](bool) { rejectedCallback = true; });
    const bool closeRejected = !state.files.Close([&](bool) { rejectedCallback = true; });
    release.set_value();
    Drain(state);
    CHECK(newRejected && closeRejected && !rejectedCallback && originalScope.IsCurrent());
    preserved();
    {
        vdev::Edit gesture(document, "Replacement waits for gesture");
        CHECK(!state.files.Close([&](bool) { rejectedCallback = true; }));
        CHECK(!state.files.New(vdev::DefaultNewSceneEnvironment(), [&](bool) { rejectedCallback = true; }));
        CHECK(!rejectedCallback && originalScope.IsCurrent());
        CHECK(gesture.Cancel());
    }
    preserved();
    bool failed = false;
    CHECK(state.files.Open(folder.File("missing.cmo"), {},
        [&](const vdev::DocumentFiles::OpenResult &result) { failed = !result.opened; }));
    Drain(state);
    CHECK(failed && !detached);
    preserved();

    auto *failure = new LoadFailure(context);
    CHECK(context->RegisterNewManager(failure) == CK_OK);
    for (bool merge : {false, true}) {
        failure->armed = true;
        failure->fired = false;
        failed = false;
        const auto completed = [&](const vdev::DocumentFiles::LoadResult &result) {
            CHECK(std::this_thread::get_id() == mainThread);
            failed = !result.loaded && result.objects.empty();
        };
        if (merge) state.files.Merge(cmo, false, completed);
        else state.files.Import(nmo, vdev::Document::ImportMode::File, vdev::DefaultNewSceneEnvironment(), completed);
        Drain(state);
        CHECK(failed && failure->fired);
        preserved();
    }

    // A failed destination write has a typed failure and retains the source
    // and the editing state. A subsequent export must not mark the doc saved.
    const std::string sourceBytes = [&] { std::ifstream file(cmo, std::ios::binary); return std::string(std::istreambuf_iterator<char>(file), {}); }();
    failed = false;
    CHECK(state.files.Save(folder.File("missing/output.cmo"), layout, {}, [&](const vdev::DocumentFiles::SaveResult &result) {
        failed = !result.saved && !result.clean && !result.report.documentSaved && !result.report.errors.empty();
    }));
    Drain(state);
    CHECK(failed);
    preserved();
    { std::ifstream file(cmo, std::ios::binary); CHECK(std::string(std::istreambuf_iterator<char>(file), {}) == sourceBytes); }
    CHECK(document.History().Redo());
    CHECK(document.Dirty());
    vdev::SaveOptions exportOptions;
    exportOptions.markSaved = false;
    saved = false;
    CHECK(state.files.Save(folder.File("export.cmo"), layout, exportOptions, [&](const vdev::DocumentFiles::SaveResult &result) {
        saved = result.saved && !result.clean && !result.report.documentSaved;
    }));
    Drain(state);
    CHECK(saved && document.Dirty());
    CHECK(document.History().Undo());

    bool loaded = false;
    int cancelled = 0;
    state.files.Import(nmo, vdev::Document::ImportMode::File, vdev::DefaultNewSceneEnvironment(),
        [&](const vdev::DocumentFiles::LoadResult &result) { CHECK(!result.loaded); ++cancelled; });
    state.FinishRuntimeOperations();
    CHECK(state.files.PendingDuplicates());
    state.files.ResolveDuplicates(false);
    CHECK(cancelled == 1 && !state.files.PendingDuplicates());
    state.files.WithDuplicates(nmo, [&](const std::vector<vdev::DuplicateName> *) { loaded = true; });
    state.FinishRuntimeOperations();
    CHECK(state.files.PendingDuplicates());
    state.files.ResolveDuplicates(false);
    CHECK(!loaded && !state.files.PendingDuplicates());
    state.files.WithDuplicates(nmo, [&](const std::vector<vdev::DuplicateName> *) { loaded = true; });
    state.FinishRuntimeOperations();
    CHECK(state.files.PendingDuplicates());
    const auto oldScope = state.runtimeOperation.CurrentDocument();
    state.ClearDocumentState();
    state.files.ResolveDuplicates(true);
    CHECK(!loaded && !state.files.PendingDuplicates() && !oldScope.IsCurrent());

    // A successful open performs its UI transition on the main thread and
    // commits before any unrelated work; old-document work/results expire.
    bool opened = false, staleWork = false, staleCompletion = false;
    detached = false;
    CHECK(state.files.Open(cmo, {}, [&](const vdev::DocumentFiles::OpenResult &result) {
        CHECK(std::this_thread::get_id() == mainThread);
        opened = result.opened;
    }));
    CHECK(state.RunRuntime("Old document request", [&] { staleWork = true; }, [&](bool) { staleCompletion = true; }));
    Drain(state);
    CHECK(detached && opened && !staleWork && !staleCompletion);
    schematicSelection.Validate([&document](const vdev::ElementAddress &address) {
        uint32_t script = 0;
        vdev::ElementRef ref;
        return address.Resolve(document, script, ref);
    });
    CHECK(document.Context() != context && schematicSelection.Empty() && state.objectSelection.Objects().empty());
    CHECK(document.CreatedFrom() == vdev::Document::Origin::LoadedComposition && !document.Dirty());
    CHECK(!document.History().CanUndo() && !document.History().CanRedo());

    opened = false;
    views.expected = nullptr;
    CHECK(state.files.Open(nmo, {}, [&](const vdev::DocumentFiles::OpenResult &result) { opened = result.opened; }));
    Drain(state);
    CHECK(opened && document.CreatedFrom() == vdev::Document::Origin::ImportedObjects && document.Dirty());
    CHECK(document.Context()->GetCurrentLevel() && !document.Scripts().empty());
    state.ClearDocumentState();
    CHECK(state.files.Close({}));
    Drain(state);
    CHECK(!state.HasDocument());
    CHECK(state.files.New(vdev::DefaultNewSceneEnvironment(), {}));
    Drain(state);
    CHECK(state.HasDocument() && !document.Dirty());
}
} // namespace

int main() {
    try {
        std::string why;
        if (!vdevtest::RuntimeAvailable(why)) { std::cout << why << '\n'; return vdevtest::SKIPPED; }
        Workflows();
        std::cout << "File workflow failures, scopes, duplicate choices, saves and CMO/NMO transitions passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
