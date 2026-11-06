#include "ui/native/EditorSession.h"
#include "workspace/EditorWorkspace.h"
#include "workspace/EditorClient.h"
#include "editor/ImGuiDrawing.h"
#include "core/layout/LayoutView.h"
#include "core/level/LevelCommands.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
struct Fixture {
    struct View { Fixture &owner; EditorView id; int value = 37; };
    EditorSession session;
    EditorApplication app{[this] { return session.document.get(); }, [this] { return session.runtimeOperation.Available(); }};
    EditorWorkspace workspace{app};
    std::map<EditorView, View *> views;
    std::vector<EditorDocumentPhase> events;
    unsigned destroyed = 0;
    bool refuseClose = false, failDetach = false, failRestore = false, failCommitted = false;
    bool rejectCommit = false, denied = false, guarded = true;
    ObjectRef selected;
    DocumentToken original;
    size_t undo = 0;
    bool dirty = false, redo = false;
    std::string error;
    explicit Fixture(const std::string &external = {}) {
        const auto *path = std::getenv("VIRTOOLS_PATH");
        Check(session.host.Start(path ? path : ".", {}, &error), error.c_str());
        session.document = std::make_unique<Document>(session.host);
        Check(session.document->New(&error), error.c_str());
        auto &doc = *session.document;
        const auto made = CreateLayoutObject(doc, LayoutObject::Frame3D);
        Check(made.ok, "create original object");
        selected = doc.Reference(made.created);
        Check(RenameLevelObject(doc, selected, "Retained history").ok && doc.History().Undo(), "prepare history");
        session.objectSelection.Restore(doc, {made.created});
        original = doc.Token(); undo = doc.History().UndoCount(); dirty = doc.Dirty(); redo = doc.History().CanRedo();
        session.interactionActive = [this] { return denied || app.HasEdits(); };
        Check(workspace.Register(error), error.c_str());
        EditorViewDesc bound{};
        bound.size = sizeof(bound); bound.imguiVersion = EDITOR_IMGUI_VERSION;
        bound.id = EDITOR_TEXT("test.document"); bound.provider = EDITOR_TEXT("test.builtin");
        bound.title = EDITOR_TEXT("Document view"); bound.area = EDITOR_DOCUMENT; bound.documentBound = 1;
        bound.data = this;
        bound.create = [](void *p, const EditorHost *, EditorView id) -> void * {
            auto &f = *static_cast<Fixture *>(p); auto *view = new View{f, id}; f.views.emplace(id, view); return view;
        };
        bound.destroy = [](void *p) {
            auto *v = static_cast<View *>(p); ++v->owner.destroyed; v->owner.views.erase(v->id); delete v;
        };
        bound.canClose = [](void *p, const EditorHost *, EditorView) {
            return static_cast<View *>(p)->owner.refuseClose ? EDITOR_REFUSED : EDITOR_OK;
        };
        bound.draw = [](void *, const EditorHost *, EditorView, EditorViewFrame *) {};
        static const EditorParameter value{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE};
        bound.fields = &value; bound.fieldCount = 1;
        bound.inspect = [](void *p, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const auto value = IntegerArgument("value", static_cast<View *>(p)->value);
            return sink.write(sink.data, &value, 1);
        };
        // Intentionally no saveState: rollback must retain the actual instance.
        auto canvas = bound;
        canvas.id = EDITOR_TEXT("test.canvas"); canvas.documentBound = 0;
        const EditorViewDesc descriptors[] = {bound, canvas};
        EditorQueryDesc query{};
        query.size = sizeof(query); query.id = EDITOR_TEXT("test.native-document"); query.provider = EDITOR_TEXT("test.builtin");
        query.area = EDITOR_DOCUMENT; query.fields = &value; query.fieldCount = 1;
        query.data = this;
        query.read = [](void *p, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const auto &f = *static_cast<Fixture *>(p);
            Check(f.session.host.Context()->GetCurrentLevel() != nullptr, "document callback has native access");
            const auto value = IntegerArgument("value", f.session.document->Token().value);
            return sink.write(sink.data, &value, 1);
        };
        EditorModuleDesc module{};
        module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.document-lifecycle");
        module.views = descriptors; module.viewCount = 2; module.data = this; module.queries = &query; module.queryCount = 1;
        module.document = [](void *p, const EditorHost *host, const EditorDocumentChange *change) {
            auto &f = *static_cast<Fixture *>(p); f.events.push_back(change->phase);
            const EditorClient client(host, 0);
            std::string error;
            f.guarded &= client.Query("test.native-document").status == EDITOR_OK &&
                client.Command("workspace.open-view", {TextArgument("type", "test.document")}).status == EDITOR_REFUSED &&
                !f.app.ChangeDocument(*change, error);
            if (change->phase == EDITOR_DOCUMENT_DETACH) {
                if (f.rejectCommit) f.denied = true;
                if (f.failDetach) throw std::runtime_error("Injected detach failure");
            } else if (change->phase == EDITOR_DOCUMENT_RESTORE) {
                if (f.failRestore) throw std::runtime_error("Injected restore failure");
                f.denied = false;
            } else if (f.failCommitted) throw std::runtime_error("Injected committed callback failure");
            return EDITOR_OK;
        };
        Check(app.Register(module, error), error.c_str());
        EditorConfiguration configuration;
        if (!external.empty()) {
            Check(app.Load(external, error), error.c_str());
            configuration.providers.push_back({"view", "test.document", "test.external-document"});
        }
        Check(app.Activate(EDITOR_IMGUI_VERSION, error, configuration), error.c_str());
        app.Attach(original, doc.TopologyRevision(), true, true);
        app.SetSelection(selected);
        Check(app.StartModules(error), error.c_str());
        session.documentViews = &workspace;
    }
    ~Fixture() {
        session.FinishRuntimeOperations(); session.documentViews = nullptr;
        workspace.CloseAll(); app.Stop();
    }
    std::vector<EditorView> Open() {
        std::vector<EditorView> ids{workspace.Open("test.document"), workspace.Open("test.document"), workspace.Open("test.canvas")};
        Check(ids[0] && ids[1] && ids[2] && ids[0] != ids[1], "open multiple views");
        return ids;
    }
    void Preserved(const std::vector<EditorView> &ids) {
        auto &doc = *session.document;
        Check(doc.Token() == original && app.Selection() == selected && doc.Dirty() == dirty &&
              doc.History().UndoCount() == undo && doc.History().CanRedo() == redo && doc.NativeId(selected) &&
              session.objectSelection.Contains(selected.object), "document, selection or history changed");
        for (auto id : ids) {
            Check(app.Scope(id).view == id && !workspace.WindowName(id).empty(), "original view scope was lost");
            if (const auto v = views.find(id); v != views.end()) Check(v->second->value == 37, "view state changed");
        }
        Check(workspace.ViewCount() == ids.size() && !destroyed && guarded, "rollback destroyed a view or bypassed callback guards");
    }
    bool New() {
        auto request = session.runtimeOperation.Begin("Replacing document");
        Check(bool(request), "acquire document workflow");
        bool completed = false, result = false;
        {
            auto submitting = request.Enter();
            Check(session.files.New(DefaultNewSceneEnvironment(), [&](bool ok) {
                completed = true; result = ok; request.Reset();
            }), "queue document replacement");
        }
        session.FinishRuntimeOperations();
        Check(completed && session.runtimeOperation.Available(), "replacement did not release its workflow");
        return result;
    }
};

void RetainedInstances(const std::string &external = {}) {
    Fixture f(external); const auto ids = f.Open();
    auto request = f.session.runtimeOperation.Begin("Retain document views");
    auto submitting = request.Enter();
    f.workspace.DetachDocument(true);
    Check(f.workspace.ViewCount() == ids.size() && !f.destroyed, "detachment destroyed instances");
    f.workspace.RestoreDocument();
    f.Preserved(ids);
    Check(f.events == std::vector<EditorDocumentPhase>{EDITOR_DOCUMENT_DETACH, EDITOR_DOCUMENT_RESTORE}, "document lifecycle order");
    if (!external.empty()) {
        const auto result = EditorClient(f.app.Host(), 0).Query("test.external-document-counts");
        Check(result.status == EDITOR_OK && EditorClient::Integer(result, "destroyed") == 0 &&
              EditorClient::Integer(result, "detached") == 1 && EditorClient::Integer(result, "restored") == 1 &&
              EditorClient::Integer(result, "guarded") == 1, "external module used a different replacement path");
    }
}
void FailedReplacement(const std::string &mode) {
    Fixture f; const auto ids = f.Open();
    f.rejectCommit = mode == "DispatchFailure"; f.failDetach = mode == "DetachFailure"; f.refuseClose = mode == "CloseRefusal";
    Check(!f.New(), "injected failure switched the document");
    f.Preserved(ids);
    Check(f.events == (f.refuseClose ? std::vector<EditorDocumentPhase>{}
                                   : std::vector<EditorDocumentPhase>{EDITOR_DOCUMENT_DETACH, EDITOR_DOCUMENT_RESTORE}),
          "failed replacement did not recover exactly once");
}
void RecoveryRetry() {
    Fixture f; const auto ids = f.Open();
    auto request = f.session.runtimeOperation.Begin("Recovery"); auto submitting = request.Enter();
    f.workspace.DetachDocument(true); f.failRestore = true;
    bool failed = false;
    try { f.workspace.RestoreDocument(); } catch (const std::runtime_error &) { failed = true; }
    Check(failed, "restore failure was lost");
    f.Preserved(ids); f.failRestore = false; f.workspace.RestoreDocument(); f.Preserved(ids);
    Check(f.events.size() == 3 && f.events.back() == EDITOR_DOCUMENT_RESTORE, "failed module could not retry recovery");
}
void SuccessfulReplacement(bool failCommitted) {
    Fixture f; const auto ids = f.Open(); f.failCommitted = failCommitted;
    Check(f.New() && f.session.document->Token() != f.original, "successful native replacement was reverted");
    Check(f.destroyed == 2 && f.workspace.ViewCount() == 1 && f.app.Scope(ids[2]).view == ids[2] &&
          f.app.Scope().document == f.session.document->Token() && f.app.Selection() == ObjectRef{} && f.guarded,
          "committed replacement kept old bound views or lost the persistent view");
    Check(f.events == std::vector<EditorDocumentPhase>{EDITOR_DOCUMENT_DETACH, EDITOR_DOCUMENT_REPLACED}, "committed event order");
    if (failCommitted) Check(!f.session.eventLog.Entries().empty(), "committed callback failure was not reported");
}
void QueuedReplacement() {
    Fixture f; f.Open(); bool completed = false, oldWork = false;
    Check(f.session.files.New(DefaultNewSceneEnvironment(), [&](bool ok) { completed = ok; }), "queue replacement");
    Check(f.session.RunRuntime("Old document work", [&] { oldWork = true; }), "queue old-document batch");
    f.session.FinishRuntimeOperations();
    Check(completed && !oldWork && f.session.document->Token() != f.original && f.guarded,
          "following queued work prevented replacement or entered the new document");
}
void ThreadChecks() {
    Fixture f; const auto ids = f.Open(); bool refused = false;
    std::thread wrong([&] {
        try { f.workspace.DetachDocument(true); } catch (...) { refused = true; }
    }); wrong.join();
    Check(refused && f.events.empty(), "wrong thread entered document callbacks");
    f.Preserved(ids);
    f.rejectCommit = true; Check(!f.New(), "valid replacement could not recover after wrong-thread refusal"); f.Preserved(ids);
}
void ModuleOrder() {
    EditorApplication app([] { return static_cast<Document *>(nullptr); });
    std::vector<std::string> events;
    struct Data { std::vector<std::string> &events; const char *name; } base{events, "test.base"}, child{events, "test.child"};
    for (auto *data : {&base, &child}) {
        EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION;
        module.id = EditorLiteral(data->name); module.data = data;
        const EditorString dependency = EDITOR_TEXT("test.base");
        if (data == &child) { module.dependencies = &dependency; module.dependencyCount = 1; }
        module.document = [](void *p, const EditorHost *, const EditorDocumentChange *change) {
            auto &data = *static_cast<Data *>(p); data.events.push_back(std::string(data.name) + ":" + std::to_string(change->phase));
            return EDITOR_OK;
        };
        std::string error; Check(app.Register(module, error), error.c_str());
    }
    std::string error; Check(app.Activate(EDITOR_IMGUI_VERSION, error) && app.StartModules(error), error.c_str());
    app.Attach({42}, 0, true, true);
    EditorDocumentChange change{sizeof(change), EDITOR_DOCUMENT_DETACH, {42}, 0, 1};
    Check(app.ChangeDocument(change, error), error.c_str());
    change.phase = EDITOR_DOCUMENT_RESTORE; Check(app.ChangeDocument(change, error), error.c_str());
    Check(events == std::vector<std::string>{"test.child:0", "test.base:0", "test.base:1", "test.child:1"}, "dependency recovery order changed");
}
void CameraRetention(bool detach) {
    Fixture f;
    LayoutView first(f.session.host), second(f.session.host);
    std::string error;
    Check(first.Start("", &error) && second.Start("", &error, nullptr, false), error.c_str());
    const auto one = first.ViewCamera(LayoutViewCamera::Perspective), two = second.ViewCamera(LayoutViewCamera::Top);
    first.SetCamera(one); second.SetCamera(two);
    first.Camera()->SetPosition3f(11, 23, 41); second.Camera()->SetPosition3f(59, 71, 89);
    const auto a = first.Camera()->GetWorldMatrix(), b = second.Camera()->GetWorldMatrix();
    if (detach) { first.Detach(); second.Detach(); Check(!first.Started() && !second.Started(), "detach retained render contexts"); }
    Check(first.Start("", &error) && second.Start("", &error, nullptr, false), error.c_str());
    Check(first.Camera()->GetID() == one && second.Camera()->GetID() == two &&
          first.Camera()->GetWorldMatrix() == a && second.Camera()->GetWorldMatrix() == b,
          "recreating a render context reset an independent viewport camera");
}
} // namespace
int main(int argc, char **argv) {
    try {
        Check(argc >= 2, "one document-view case is required"); const std::string name = argv[1];
        if (name == "RetainedInstances") RetainedInstances();
        else if (name == "DispatchFailure" || name == "DetachFailure" || name == "CloseRefusal") FailedReplacement(name);
        else if (name == "RecoveryRetry") RecoveryRetry();
        else if (name == "SuccessfulReplacement" || name == "CommittedCallbackFailure") SuccessfulReplacement(name == "CommittedCallbackFailure");
        else if (name == "ThreadChecks") ThreadChecks();
        else if (name == "QueuedReplacement") QueuedReplacement();
        else if (name == "ModuleOrder") ModuleOrder();
        else if (name == "ExternalProvider") { Check(argc == 3, "external module required"); RetainedInstances(argv[2]); }
        else if (name == "CameraRestart" || name == "CameraDetach") CameraRetention(name == "CameraDetach");
        else throw std::runtime_error("Unknown document-view case");
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
