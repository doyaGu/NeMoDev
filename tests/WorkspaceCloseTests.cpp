#include "workspace/EditorWorkspace.h"
#include "editor/ImGuiDrawing.h"
#include "core/document/Document.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace vdev;
void Check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
struct View;
struct Provider {
    std::map<EditorView, View *> views;
    std::vector<std::string> *events = nullptr;
    unsigned created = 0, prepared = 0, closing = 0, destroyed = 0;
    bool failPreparation = false, throwClosing = false, throwDestroy = false;
    bool preparationScopeAlive = false, closingScopeAlive = false;
    EditorStatus closeStatus = EDITOR_OK, mutation = EDITOR_OK, query = EDITOR_ERROR;
    EditorView refuseView = 0;
};
struct View {
    Provider &provider;
    EditorView id;
    std::string topic = "original topic";
};
EditorViewDesc Describe(Provider &provider, const char *name) {
    EditorViewDesc desc{};
    desc.size = sizeof(desc); desc.imguiVersion = EDITOR_IMGUI_VERSION;
    desc.id = EDITOR_TEXT("test.close"); desc.provider = EditorLiteral(name);
    desc.title = EDITOR_TEXT("Close test"); desc.area = EDITOR_WORKSPACE;
    desc.data = &provider; desc.singleInstance = 1;
    desc.create = [](void *data, const EditorHost *, EditorView id) -> void * {
        auto &provider = *static_cast<Provider *>(data);
        ++provider.created;
        auto *view = new View{provider, id};
        provider.views.emplace(id, view);
        return view;
    };
    desc.canClose = [](void *data, const EditorHost *host, EditorView id) {
        auto &provider = static_cast<View *>(data)->provider;
        ++provider.prepared;
        if (provider.events) provider.events->push_back("prepare");
        provider.preparationScopeAlive = host->scope(host->data, id).view == id;
        const EditorRequest query{host->scope(host->data, id)};
        provider.query = host->query(host->data, EditorString EDITOR_TEXT("workspace.instances"), &query,
                                     {nullptr, [](void *, const EditorValue *, uint32_t) { return EDITOR_OK; }});
        const EditorValue type{EDITOR_TEXT("type"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("test.close")};
        const EditorRequest request{host->scope(host->data, id), &type, 1};
        provider.mutation = host->execute(host->data, EditorString EDITOR_TEXT("workspace.open-view"), &request, {});
        if (provider.failPreparation)
            throw std::runtime_error("Injected close failure");
        return provider.refuseView && provider.refuseView != id ? EDITOR_OK : provider.closeStatus;
    };
    desc.closing = [](void *data, const EditorHost *host, EditorView id) {
        auto &provider = static_cast<View *>(data)->provider;
        ++provider.closing;
        if (provider.events) provider.events->push_back("closing");
        provider.closingScopeAlive = host->scope(host->data, id).view == id;
        if (provider.throwClosing) throw std::runtime_error("Injected final cleanup exception");
    };
    desc.destroy = [](void *data) {
        auto *view = static_cast<View *>(data);
        auto &provider = view->provider;
        ++provider.destroyed;
        if (provider.events) provider.events->push_back("destroy");
        provider.views.erase(view->id);
        delete view;
        if (provider.throwDestroy) throw std::runtime_error("Injected destruction exception");
    };
    desc.draw = [](void *, const EditorHost *, EditorView, EditorViewFrame *) {};
    static const EditorParameter topic{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE};
    desc.fields = &topic; desc.fieldCount = 1;
    desc.stateVersion = 1;
    desc.inspect = desc.saveState = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const EditorValue value{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE, {},
                                EditorText(static_cast<View *>(data)->topic)};
        return sink.write(sink.data, &value, 1);
    };
    desc.restoreState = [](void *data, const EditorHost *, const EditorRequest *request) {
        const auto text = request->values[0].text;
        static_cast<View *>(data)->topic.assign(text.data, text.size);
        return EDITOR_OK;
    };
    return desc;
}
struct Fixture {
    Provider old, next;
    EditorApplication app;
    EditorWorkspace workspace;
    EditorConfiguration config;
    std::string error;
    unsigned writes = 0;
    bool refuseWrite = false, throwWrite = false;
    std::vector<std::string> events;
    std::vector<EditorResponse> reports;
    explicit Fixture(bool multiple = false, Document *document = nullptr)
        : app([document] { return document; }), workspace(app) {
        Check(workspace.Register(error), error.c_str());
        EditorViewDesc views[] = {Describe(old, "test.old"), Describe(next, "test.next")};
        for (auto &view : views) {
            view.singleInstance = !multiple;
            if (document) { view.area = EDITOR_DOCUMENT; view.documentBound = 1; }
        }
        EditorModuleDesc module{};
        module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.close");
        module.views = views; module.viewCount = 2;
        config.providers.push_back({"view", "test.close", "test.old"});
        Check(app.Register(module, error) && app.Activate(EDITOR_IMGUI_VERSION, error, config), error.c_str());
        if (document) app.Attach(document->Token(), document->TopologyRevision(), true, true);
        config.providers[0].provider = "test.next";
        old.events = &events;
        workspace.ConfigurationWriter([this](const auto &, const auto &, std::string &error) {
            ++writes;
            events.push_back("write");
            if (throwWrite) throw std::runtime_error("Injected write exception");
            if (refuseWrite) { error = "Injected write refusal"; return false; }
            return true;
        });
        app.report = [this](const auto &response) { reports.push_back(response); };
    }
    ~Fixture() {
        // These observers are destroyed before the workspace member.
        old.events = nullptr;
        app.report = {};
    }
    EditorView Open() {
        const auto id = workspace.Open("test.close");
        Check(id != 0 && workspace.Focus(id), "original view did not open");
        return id;
    }
    std::string Topic(EditorView id) {
        const auto &result = workspace.Inspect(id);
        Check(result.status == EDITOR_OK && result.fields.size() == 1 && result.fields.front().key == "topic" &&
                  result.fields.front().type == EDITOR_TEXT_VALUE,
              "view query did not return its topic");
        return result.fields.front().text;
    }
    void Preserved(EditorView id, size_t count = 1) {
        Check(!old.closing && !old.destroyed && next.views.empty() && workspace.ViewCount() == count &&
                  !workspace.WindowName(id).empty() && app.Scope(id).view == id &&
                  workspace.ActionView("test.close", 0) == id && Topic(id) == "original topic",
              "failed replacement lost old view state, focus or scope");
        Check(app.Configuration().providers[0].provider == "test.old", "failed replacement selected the new provider");
    }
};
void ReplacementFailure(bool throwing) {
    Fixture f;
    const auto id = f.Open();
    f.old.failPreparation = throwing;
    f.old.closeStatus = EDITOR_REFUSED;
    Check(!f.workspace.Configure(f.config, f.error), "replacement succeeded after old-view close failed");
    Check(f.writes == 0 && f.old.prepared == 1 && f.next.destroyed == 1 &&
              f.error.find("test.old") != std::string::npos, "failed close preparation reached configuration saving");
    Check(f.old.preparationScopeAlive && f.old.mutation == EDITOR_REFUSED && f.old.query == EDITOR_OK,
          "close preparation lost its read-only query scope");
    f.Preserved(id);
    f.old.failPreparation = false;
    f.old.closeStatus = EDITOR_OK;
    Check(f.workspace.Configure(f.config, f.error) && f.writes == 1 && f.old.closing == 1 &&
              f.old.closingScopeAlive && f.old.destroyed == 1 && f.next.views.size() == 1,
          "replacement could not retry after close preparation failed");
}
void MultipleReplacementFailure() {
    Fixture f(true);
    const auto first = f.Open(), second = f.Open();
    f.old.refuseView = second;
    f.old.closeStatus = EDITOR_REFUSED;
    Check(!f.workspace.Configure(f.config, f.error) && f.old.prepared == 2 && f.writes == 0 &&
              f.next.destroyed == 2, "one refusing view did not cancel the entire replacement");
    f.Preserved(second, 2);
    Check(f.Topic(first) == "original topic" && f.app.Scope(first).view == first,
          "earlier successful preparation changed or closed its original view");
}
void WriterFailure(bool throwing) {
    Fixture f(true);
    const auto first = f.Open(), second = f.Open();
    f.throwWrite = throwing; f.refuseWrite = !throwing;
    Check(!f.workspace.Configure(f.config, f.error) && f.writes == 1 && f.old.prepared == 2 && f.next.destroyed == 2 &&
              f.error == (throwing ? "Injected write exception" : "Injected write refusal"),
          "configuration write failure did not roll back replacement preparation");
    f.Preserved(second, 2);
    Check(f.app.Scope(first).view == first && f.events == std::vector<std::string>{"prepare", "prepare", "write"},
          "failed configuration saving ran final cleanup");
}
void SuccessfulReplacement() {
    Fixture f(true);
    const auto first = f.Open(), second = f.Open();
    Check(f.workspace.Configure(f.config, f.error), f.error.c_str());
    Check(f.events == std::vector<std::string>{"prepare", "prepare", "write", "closing", "destroy", "closing", "destroy"},
          "replacement did not prepare all closes before saving and releasing old views");
    Check(f.old.closingScopeAlive && f.old.prepared == 2 && f.old.destroyed == 2 && f.writes == 1 &&
              !f.app.Scope(first).view && !f.app.Scope(second).view && f.workspace.ViewCount() == 2 &&
              f.next.views.size() == 2 && f.app.Configuration().providers[0].provider == "test.next",
          "successful replacement kept old scopes or lost new views");
}
void NormalClose() {
    Fixture f;
    const auto id = f.Open();
    f.old.failPreparation = true;
    Check(!f.workspace.Close(id), "normal close ignored a preparation exception");
    f.Preserved(id);
    f.old.failPreparation = false;
    f.old.closeStatus = EDITOR_REFUSED;
    Check(!f.workspace.Close(id), "normal close ignored preparation refusal");
    f.Preserved(id);
    f.old.closeStatus = EDITOR_NO_CHANGE;
    Check(f.workspace.Close(id) && f.old.closing == 1 && f.old.closingScopeAlive && f.old.destroyed == 1 &&
              !f.app.Scope(id).view && !f.workspace.ViewCount(), "normal close did not complete exactly once");
    const auto reopened = f.Open();
    Check(reopened != id && f.Topic(reopened) == "original topic",
          "refused close damaged saved state for a later reopen");
}
void CleanupExceptions(bool replacing) {
    Fixture f;
    const auto id = f.Open();
    f.old.throwClosing = f.old.throwDestroy = true;
    Check(replacing ? f.workspace.Configure(f.config, f.error) : f.workspace.Close(id),
          "final cleanup incorrectly vetoed a committed close");
    Check(f.old.prepared == 1 && f.old.closing == 1 && f.old.closingScopeAlive && f.old.destroyed == 1 &&
              !f.app.Scope(id).view && f.old.views.empty(), "final cleanup exception left a half-closed view");
    Check(f.reports.size() == 1 && f.reports[0].status == EDITOR_ERROR &&
              f.reports[0].message.find("cleanup raised an exception: test.close (test.old)") != std::string::npos &&
              f.reports[0].message.find("destruction raised an exception: test.close (test.old)") != std::string::npos,
          "final cleanup failures were not reported with their view and provider");
    Check(f.workspace.ViewCount() == (replacing ? 1 : 0) && f.writes == (replacing ? 1 : 0),
          "cleanup exceptions disagreed with the committed configuration");
}
void ForcedClose() {
    Fixture f(true);
    f.Open(); f.Open();
    f.old.closeStatus = EDITOR_REFUSED;
    f.workspace.CloseAll();
    Check(f.old.prepared == 0 && f.old.closing == 2 && f.old.destroyed == 2 && !f.workspace.ViewCount(),
          "shutdown allowed a view to veto final cleanup");
}
void ReporterException() {
    Fixture f;
    const auto id = f.Open();
    f.old.throwClosing = true;
    f.app.report = [](const auto &) { throw std::runtime_error("Injected report exception"); };
    Check(f.workspace.Configure(f.config, f.error), "diagnostic reporting vetoed a committed replacement");
    Check(f.old.destroyed == 1 && f.writes == 1 && f.next.views.size() == 1 &&
              f.workspace.ViewCount() == 1 && !f.app.Scope(id).view,
          "diagnostic exception interrupted installation of the replacement");
}
void WrongThread() {
    Fixture f;
    const auto id = f.Open();
    bool closed = true;
    std::thread thread([&] { closed = f.workspace.Close(id); });
    thread.join();
    Check(!closed && !f.old.prepared, "close preparation ran on the wrong thread");
    f.Preserved(id);
    Check(f.workspace.Close(id), "thread rejection left callback restrictions active");
}
void DocumentPreserved() {
    CkRuntimeHost runtime;
    std::string error;
    const auto *path = std::getenv("VIRTOOLS_PATH");
    Check(runtime.Start(path ? path : ".", {}, &error), error.c_str());
    Document document(runtime);
    Check(document.New(&error), error.c_str());
    const auto created = CreateGroup(document, "Original", {});
    Check(created.ok, "could not create the undo history fixture");
    const auto target = document.Reference(created.created);
    Check(RenameLevelObject(document, target, "Renamed").ok && document.History().Undo(),
          "could not prepare undo and redo history");
    Fixture f(false, &document);
    const auto id = f.Open();
    f.app.SetSelection(target);
    const auto token = document.Token();
    const auto revision = document.ChangeRevision(), topology = document.TopologyRevision();
    const auto undo = document.History().UndoCount();
    const bool redo = document.History().CanRedo();
    const auto undoLabel = document.History().UndoLabel(), redoLabel = document.History().RedoLabel();
    const bool dirty = document.Dirty();
    f.old.failPreparation = true;
    Check(!f.workspace.Configure(f.config, f.error), "document view ignored close preparation failure");
    f.Preserved(id);
    Check(document.Token() == token && document.ChangeRevision() == revision && document.TopologyRevision() == topology &&
              document.History().UndoCount() == undo && document.History().CanRedo() == redo &&
              document.History().UndoLabel() == undoLabel && document.History().RedoLabel() == redoLabel &&
              document.Dirty() == dirty && f.app.Selection() == target && document.Resolve(target) &&
              std::string(document.Resolve(target)->GetName()) == "Original", "failed view replacement changed the document or history");
    Check(document.History().Redo() && std::string(document.Resolve(target)->GetName()) == "Renamed" &&
              document.History().Undo() && std::string(document.Resolve(target)->GetName()) == "Original",
          "failed replacement damaged usable undo or redo history");
    f.old.closeStatus = EDITOR_REFUSED;
    f.workspace.DocumentChanging();
    Check(!f.workspace.ViewCount() && f.old.prepared == 1 && f.old.closing == 1 && f.old.destroyed == 1,
          "document switch let a document-bound view refuse its final cleanup");
}
void ExternalProvider(const char *path) {
    Provider next;
    EditorApplication app({});
    EditorWorkspace workspace(app);
    std::string error;
    Check(workspace.Register(error) && app.Load(path, error), error.c_str());
    auto view = Describe(next, "test.next");
    view.inspect = nullptr; view.fields = nullptr; view.fieldCount = 0;
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.close-target");
    module.views = &view; module.viewCount = 1;
    EditorConfiguration config;
    config.providers.push_back({"view", "test.close", "test.external"});
    Check(app.Register(module, error) && app.Activate(EDITOR_IMGUI_VERSION, error, config), error.c_str());
    const auto id = workspace.Open("test.close");
    Check(id && workspace.Focus(id), "external provider did not create its view");
    unsigned writes = 0;
    workspace.ConfigurationWriter([&](const auto &, const auto &, std::string &) { ++writes; return true; });
    config.providers[0].provider = "test.next";
    for (const uint64_t mode : {0, 2}) {
        const EditorValue value{EDITOR_TEXT("mode"), EDITOR_INTEGER_VALUE, {}, {}, mode};
        Check(app.Execute("test.close-mode", {app.Scope(id), &value, 1}).status == EDITOR_OK,
              "external failure could not be selected through a command");
        Check(!workspace.Configure(config, error) && !writes && app.Scope(id).view == id &&
                  workspace.ViewCount() == 1 && next.views.empty() &&
                  app.Configuration().providers[0].provider == "test.external", "external close failure lost the original provider");
    }
    auto counts = app.Query("test.close-counts", {app.Scope()});
    Check(counts.status == EDITOR_OK && counts.fields.size() == 5 && counts.fields[0].integer == 2 && !counts.fields[1].integer &&
              !counts.fields[2].integer && counts.fields[3].integer && counts.fields[4].integer,
          "external close preparation mutated or cleaned up its original view");
    const EditorValue allow{EDITOR_TEXT("mode"), EDITOR_INTEGER_VALUE, {}, {}, 1};
    Check(app.Execute("test.close-mode", {app.Scope(id), &allow, 1}).status == EDITOR_OK &&
              workspace.Configure(config, error) && writes == 1 && !app.Scope(id).view && next.views.size() == 1,
          "external provider replacement did not retry successfully");
    counts = app.Query("test.close-counts", {app.Scope()});
    Check(counts.status == EDITOR_OK && counts.fields.size() == 5 && counts.fields[0].integer == 3 && counts.fields[1].integer == 1 &&
              counts.fields[2].integer == 1 && counts.fields[3].integer, "external final cleanup lost its source scope or ran twice");
}
} // namespace
int main(int argc, char **argv) {
    try {
        Check(argc >= 2, "one workspace close test name is required");
        const std::string name = argv[1];
        if (name == "ReplacementFailure") ReplacementFailure(true);
        else if (name == "ReplacementRefusal") ReplacementFailure(false);
        else if (name == "MultipleReplacementFailure") MultipleReplacementFailure();
        else if (name == "WriterRefusal") WriterFailure(false);
        else if (name == "WriterException") WriterFailure(true);
        else if (name == "SuccessfulReplacement") SuccessfulReplacement();
        else if (name == "NormalClose") NormalClose();
        else if (name == "CloseCleanupExceptions") CleanupExceptions(false);
        else if (name == "ReplacementCleanupExceptions") CleanupExceptions(true);
        else if (name == "ForcedClose") ForcedClose();
        else if (name == "ReporterException") ReporterException();
        else if (name == "WrongThread") WrongThread();
        else if (name == "DocumentPreserved") DocumentPreserved();
        else if (name == "ExternalProvider" && argc == 3) ExternalProvider(argv[2]);
        else throw std::runtime_error("unknown workspace close test: " + name);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
