#include "application/EditorApplication.h"
#include "core/document/Document.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "editor/ImGuiDrawing.h"
#include "features/objects/ObjectCommands.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}

bool RegisterAppearance(EditorApplication &app, std::string &error) {
    const EditorValue colors[] = {{EDITOR_TEXT("color.test"), EDITOR_INTEGER_VALUE, {}, {}, 0x112233ff},
                                 {EDITOR_TEXT("color.test"), EDITOR_INTEGER_VALUE, {}, {}, 0x445566ff}};
    const EditorThemeDesc themes[] = {
        {sizeof(EditorThemeDesc), EDITOR_TEXT("test.theme"), EDITOR_TEXT("test.first"), EDITOR_TEXT("First"), {}, colors, 1},
        {sizeof(EditorThemeDesc), EDITOR_TEXT("test.theme"), EDITOR_TEXT("test.second"), EDITOR_TEXT("Second"), {}, colors + 1, 1}};
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.appearance");
    module.themes = themes; module.themeCount = 2;
    return app.Register(module, error);
}

struct Tools {
    unsigned frames = 0, cancels = 0, leaves = 0;
    bool capture = false;
    bool Register(EditorApplication &app, std::string &error) {
        EditorViewDesc view{};
        view.size = sizeof(view);
        view.imguiVersion = EDITOR_IMGUI_VERSION;
        view.id = EDITOR_TEXT("test.viewport");
        view.provider = EDITOR_TEXT("test.views");
        view.title = EDITOR_TEXT("Viewport");
        view.area = EDITOR_WORKSPACE;
        view.create = [](void *, const EditorHost *, EditorView) -> void * { return new int; };
        view.draw = [](void *, const EditorHost *, EditorView, EditorViewFrame *) {};
        view.destroy = [](void *p) { delete static_cast<int *>(p); };
        view.defaultTool = EDITOR_TEXT("test.select");
        EditorToolDesc tool{};
        tool.size = sizeof(tool);
        tool.id = view.defaultTool;
        tool.view = view.id;
        tool.title = EDITOR_TEXT("Select");
        tool.provider = EDITOR_TEXT("test.tools");
        tool.data = this;
        tool.input = [](void *p, const EditorHost *, const EditorRequest *, const EditorToolInput *input,
                        uint32_t *flags) {
            auto &tools = *static_cast<Tools *>(p);
            if (input->phase == EDITOR_TOOL_FRAME) {
                ++tools.frames;
                *flags = tools.capture ? EDITOR_TOOL_CAPTURED : 0;
            }
            if (input->phase == EDITOR_TOOL_CANCEL) {
                ++tools.cancels;
                tools.capture = false;
            }
            if (input->phase == EDITOR_TOOL_LEAVE) ++tools.leaves;
            return EDITOR_OK;
        };
        const EditorBindingDesc binding{tool.id, EDITOR_TEXT("R"), view.id};
        EditorModuleDesc module{};
        module.size = sizeof(module);
        module.version = EDITOR_VERSION;
        module.id = EDITOR_TEXT("test.tools");
        module.views = &view;
        module.viewCount = 1;
        module.tools = &tool;
        module.toolCount = 1;
        module.bindings = &binding;
        module.bindingCount = 1;
        return app.Register(module, error);
    }
};

void CapturedConfiguration(bool appearance = false) {
    Tools tools;
    EditorApplication app({});
    std::string error;
    EditorConfiguration initial;
    initial.providers = {{"theme", "test.theme", "test.first"}};
    Check(tools.Register(app, error) && RegisterAppearance(app, error) && app.Activate(EDITOR_IMGUI_VERSION, error, initial), error.c_str());
    const auto view = app.OpenScope();
    const auto desc = app.Views().front();
    void *data = app.CreateView(desc, view);
    Check(data != nullptr, "create view");
    tools.capture = true;
    EditorToolInput input{};
    input.size = sizeof(input);
    input.phase = EDITOR_TOOL_FRAME;
    input.focused = input.hovered = 1;
    Check(app.ToolInput(app.Scope(view), input) == EDITOR_OK && app.ToolCaptured(view), "capture tool");
    auto configuration = app.Configuration();
    const auto revision = app.AppearanceRevision();
    if (appearance) configuration.providers.front().provider = "test.second";
    else configuration.bindings = {{"test.select", "T"}};
    Check(app.Configure(configuration, error), "shortcut configuration was coupled to a captured tool");
    Check(app.ToolCaptured(view) && tools.cancels == 0 && tools.leaves == 0 && app.ToolChoice(view) == "test.select",
          "shortcut configuration restarted the tool");
    Check(app.AppearanceRevision() == revision + unsigned(appearance), "configuration changed the wrong appearance version");
    Check(app.ToolInput(app.Scope(view), input) == EDITOR_OK && tools.frames == 2 && app.ToolCaptured(view),
          "captured input did not survive configuration");
    app.CancelTool(view);
    Check(!app.ToolCaptured(view) && tools.cancels == 1, "cancel lost its original tool");
    app.CloseScope(view);
    Check(app.DestroyView(desc, data), "destroy view");
}

struct DocumentFixture {
    CkRuntimeHost runtime;
    std::unique_ptr<Document> document;
    ObjectCommands objects{[this] { return document.get(); }};
    EditorApplication app{[this] { return document.get(); }};
    ObjectRef object;
    std::string error;
    unsigned previews = 0, cancels = 0, commits = 0, releases = 0, destroyed = 0;
    bool cleanAtDestroy = false;

    struct Preview {
        Document *document;
        ObjectRef object;
        std::string name;
    };

    DocumentFixture() {
        const auto *path = std::getenv("VIRTOOLS_PATH");
        Check(runtime.Start(path ? path : ".", {}, &error), error.c_str());
        document = std::make_unique<Document>(runtime);
        Check(document->New(&error), error.c_str());
        EditorValueEditorDesc editors[2]{
            {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("text.single-line"),
             EDITOR_TEXT("test.text"), EDITOR_TEXT("Text"),
             [](void *, const EditorHost *, const EditorValueInput *) -> uint32_t { return EDITOR_INPUT_NONE; },
             nullptr, EDITOR_TEXT_VALUE}};
        editors[1] = editors[0];
        editors[1].provider = EDITOR_TEXT("test.other-text");
        const EditorParameter parameters[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT},
                                              {EDITOR_TEXT("value"), EDITOR_TEXT_VALUE}};
        EditorCommandDesc preview{};
        preview.size = sizeof(preview);
        preview.id = EDITOR_TEXT("test.rename-preview");
        preview.provider = EDITOR_TEXT("test.values");
        preview.title = EDITOR_TEXT("Rename Preview");
        preview.parameters = parameters;
        preview.count = 2;
        preview.data = this;
        preview.area = EDITOR_DOCUMENT;
        preview.effect = EDITOR_DOCUMENT_EDIT;
        preview.run = [](void *p, const EditorHost *, const EditorRequest *request, EditorSink) {
            auto &f = *static_cast<DocumentFixture *>(p);
            const auto *value = Argument(*request, "value");
            return RenameLevelObject(*f.document, Argument(*request, "target")->object,
                                     std::string(value->text.data, value->text.size))
                           .ok
                       ? EDITOR_OK
                       : EDITOR_ERROR;
        };
        preview.preview = [](void *p, const EditorHost *, const EditorRequest *request, EditorPreviewAction action,
                             void **state) -> EditorStatus {
            auto &f = *static_cast<DocumentFixture *>(p);
            auto *saved = static_cast<Preview *>(*state);
            if (action == EDITOR_PREVIEW_UPDATE) {
                const auto object = Argument(*request, "target")->object;
                auto *native = f.document->Resolve(object);
                if (!native) return EDITOR_STALE;
                if (!saved) {
                    *state = saved = new Preview{f.document.get(), object, native->GetName()};
                    ++f.previews;
                }
                const auto *value = Argument(*request, "value");
                native->SetName(std::string(value->text.data, value->text.size).c_str());
            } else if (saved) {
                if (action == EDITOR_PREVIEW_CANCEL) {
                    if (auto *native = saved->document->Resolve(saved->object)) native->SetName(saved->name.c_str());
                    ++f.cancels;
                } else
                    ++f.releases;
                delete saved;
                *state = nullptr;
                --f.previews;
            }
            return EDITOR_OK;
        };
        preview.commitPreview = [](void *p, const EditorHost *, const EditorRequest *request, void *state, EditorSink) {
            auto &f = *static_cast<DocumentFixture *>(p);
            auto &saved = *static_cast<Preview *>(state);
            auto *native = saved.document->Resolve(saved.object);
            if (!native) return EDITOR_STALE;
            native->SetName(saved.name.c_str());
            ++f.commits;
            const auto *value = Argument(*request, "value");
            return RenameLevelObject(*f.document, saved.object, std::string(value->text.data, value->text.size)).ok
                       ? EDITOR_OK
                       : EDITOR_ERROR;
        };
        EditorModuleDesc module{};
        module.size = sizeof(module);
        module.version = EDITOR_VERSION;
        module.id = EDITOR_TEXT("test.values");
        module.editors = editors;
        module.editorCount = 2;
        module.commands = &preview;
        module.commandCount = 1;
        module.data = this;
        module.destroy = [](void *p) {
            auto &f = *static_cast<DocumentFixture *>(p);
            ++f.destroyed;
            f.cleanAtDestroy = f.previews == 0 && !f.app.HasEdits() &&
                               f.app.Query("object.details", {f.app.Scope()}).status != EDITOR_OK;
        };
        EditorConfiguration configuration;
        configuration.providers = {{"editor", "text.single-line", "test.text"}, {"theme", "test.theme", "test.first"}};
        Check(objects.Register(app, error) && app.Register(module, error) && RegisterAppearance(app, error) &&
                  app.Activate(EDITOR_IMGUI_VERSION, error, configuration),
              error.c_str());
        Attach();
        const auto result = app.Execute("object.create-frame3d", {app.Scope()});
        Check(result.status == EDITOR_OK, "create frame");
        for (const auto &field : result.fields)
            if (field.type == EDITOR_OBJECT) object = field.object;
        Check(bool(object), "created frame reference missing");
        document->History().Clear();
        document->MarkSaved();
        document->History().MarkSavePoint();
    }
    ~DocumentFixture() { app.Stop(); }
    static const EditorValue *Argument(const EditorRequest &request, const std::string &key) {
        for (uint32_t i = 0; i < request.count; ++i)
            if (std::string(request.values[i].key.data, request.values[i].key.size) == key) return &request.values[i];
        throw std::runtime_error("missing command argument");
    }
    EditorEdit BeginPreview(EditorView view) {
        const EditorValue values[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, object},
                                      {EDITOR_TEXT("value"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("Original")}};
        EditorEdit edit = 0;
        Check(app.BeginCommand("test.rename-preview", {app.Scope(view), values, 2}, edit) == EDITOR_OK &&
                  app.UpdateEdit(edit, "Preview") == EDITOR_OK,
              "begin native preview");
        Check(Name() == "Preview" && previews == 1 && !document->Dirty(), "preview marked the document dirty");
        return edit;
    }
    void Attach() { app.Attach(document->Token(), document->TopologyRevision(), true, true); }
    std::string Name() {
        const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, object};
        const auto result = app.Query("object.details", {app.Scope(), &target, 1});
        Check(result.status == EDITOR_OK, "read object");
        for (const auto &field : result.fields)
            if (field.key == "name") return field.text;
        throw std::runtime_error("name missing");
    }
};

void DraftConfiguration(bool appearance = false) {
    DocumentFixture f;
    const auto original = f.Name();
    const auto view = f.app.OpenScope();
    EditorEdit edit = 0;
    Check(f.app.BeginEdit("object.name", f.app.Scope(view), f.object, edit) == EDITOR_OK &&
              f.app.UpdateEdit(edit, "Configured draft") == EDITOR_OK,
          "begin name draft");
    auto configuration = f.app.Configuration();
    const auto revision = f.app.AppearanceRevision();
    if (appearance) configuration.providers.back().provider = "test.second";
    else configuration.bindings = {{"object.name", "Ctrl+N"}};
    Check(f.app.Configure(configuration, f.error), "shortcut configuration was coupled to an edit draft");
    Check(f.Name() == original && !f.document->Dirty() && !f.document->History().CanUndo(),
          "configuration committed a draft");
    Check(f.app.AppearanceRevision() == revision + unsigned(appearance), "configuration changed the wrong appearance version");
    Check(f.app.FinishEdit(edit, true) == EDITOR_QUEUED, "finish configured draft");
    f.app.EndFrame();
    Check(f.Name() == "Configured draft" && f.document->History().CanUndo(), "configured draft was lost");
    Check(f.document->History().Undo() && f.Name() == original && !f.document->History().CanUndo(),
          "configured interaction did not produce one undo step");
}

void FailedConfigurationKeepsDraft() {
    DocumentFixture f;
    const auto original = f.Name();
    EditorEdit edit = 0;
    Check(f.app.BeginEdit("object.name", f.app.Scope(), f.object, edit) == EDITOR_OK &&
              f.app.UpdateEdit(edit, "Preserved draft") == EDITOR_OK,
          "begin name draft");
    auto configuration = f.app.Configuration();
    configuration.providers.front().provider = "test.other-text";
    configuration.bindings = {{"object.name", "Ctrl+N"}};
    Check(!f.app.Configure(configuration, f.error), "active edit providers were replaced");
    std::string value;
    EditorValueEditorDesc editor{};
    Check(f.app.EditState(edit, value, editor) && value == "Preserved draft" &&
              std::string(editor.provider.data, editor.provider.size) == "test.text" &&
              f.app.Configuration().bindings.empty() && f.Name() == original,
          "refused configuration changed an edit");
    Check(f.app.FinishEdit(edit, false) == EDITOR_CANCELLED && !f.document->History().CanUndo(),
          "cancel lost the original editor");
    Check(f.app.Configure(configuration, f.error), "finished edit retained its providers");
}

void PreviewConfigurationCancel() {
    DocumentFixture f;
    const auto original = f.Name();
    const auto edit = f.BeginPreview(f.app.OpenScope());
    auto configuration = f.app.Configuration();
    configuration.bindings = {{"object.name", "Ctrl+N"}};
    Check(f.app.Configure(configuration, f.error), "binding configuration blocked a preview");
    Check(f.app.FinishEdit(edit, false) == EDITOR_CANCELLED && f.Name() == original && f.cancels == 1 &&
              f.previews == 0 && !f.document->Dirty() && !f.document->History().CanUndo(),
          "configured preview did not cancel once");
}

void PendingPreviewConfiguration() {
    DocumentFixture f;
    const auto original = f.Name();
    const auto edit = f.BeginPreview(f.app.OpenScope());
    EditorOperation operation = 0;
    Check(f.app.FinishEdit(edit, true, &operation) == EDITOR_QUEUED && operation && f.previews == 1,
          "queue native preview");
    auto configuration = f.app.Configuration();
    configuration.bindings = {{"object.name", "Ctrl+N"}};
    Check(f.app.Configure(configuration, f.error), "binding configuration blocked a pending preview");
    f.app.EndFrame();
    Check(f.Name() == "Preview" && f.commits == 1 && f.releases == 1 && f.cancels == 0 && f.previews == 0,
          "pending preview used the wrong owner");
    Check(f.document->History().Undo() && f.Name() == original && !f.document->History().CanUndo(),
          "pending preview did not produce one undo step");
}

void PendingPreviewClose() {
    DocumentFixture f;
    const auto original = f.Name();
    const auto view = f.app.OpenScope();
    const auto edit = f.BeginPreview(view);
    EditorOperation operation = 0;
    Check(f.app.FinishEdit(edit, true, &operation) == EDITOR_QUEUED, "queue native preview");
    f.app.CloseScope(view);
    f.app.EndFrame();
    const EditorValue argument{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, operation};
    const auto completion = f.app.Query("application.operation", {f.app.Scope(), &argument, 1});
    Check(completion.status == EDITOR_OK && completion.fields.front().integer == EDITOR_STALE && f.Name() == original &&
              f.cancels == 1 && f.commits == 0 && !f.app.HasEdits() && !f.document->History().CanUndo() &&
              !f.document->Dirty(),
          "closed view retained or committed its pending preview");
}

void DraftScopeLifetime() {
    DocumentFixture f;
    const auto original = f.Name();
    const auto a = f.app.OpenScope(), b = f.app.OpenScope();
    const auto edit = f.BeginPreview(a);
    f.app.CloseScope(b);
    Check(f.app.EditTarget(edit) == f.object && f.Name() == "Preview" && f.cancels == 0,
          "closing another view canceled an edit");
    f.app.CloseScope(a);
    Check(!f.app.EditTarget(edit) && f.Name() == original && f.cancels == 1 && !f.app.HasEdits() &&
              !f.document->History().CanUndo(),
          "owning view did not cancel its draft");
}

void DocumentReplacement() {
    DocumentFixture f;
    const auto original = f.Name();
    const auto view = f.app.OpenScope();
    const auto edit = f.BeginPreview(view);
    const auto oldScope = f.app.Scope(view);
    f.app.SetSelection(f.object);
    auto oldDocument = std::move(f.document);
    f.document = std::make_unique<Document>(f.runtime);
    Check(f.document->New(&f.error), f.error.c_str());
    f.Attach();
    Check(f.previews == 0 && f.cancels == 1 && !f.app.EditTarget(edit) && !f.app.Selection() && !f.app.HasEdits() &&
              oldDocument->Resolve(f.object)->GetName() == original && !oldDocument->Dirty() &&
              !oldDocument->History().CanUndo(),
          "document replacement mixed edit lifetimes");
    Check(f.app.Query("object.details", {oldScope}).status == EDITOR_STALE &&
              f.app.Configuration().providers.front().provider == "test.text",
          "document replacement cleared application configuration");
}

void StopCleanupOrder() {
    DocumentFixture f;
    const auto original = f.Name();
    const auto edit = f.BeginPreview(f.app.OpenScope());
    Check(f.app.FinishEdit(edit, true) == EDITOR_QUEUED, "queue preview before stop");
    f.app.Stop();
    Check(f.destroyed == 1 && f.cleanAtDestroy && f.previews == 0 && f.cancels == 1 &&
              f.document->Resolve(f.object)->GetName() == original && !f.document->History().CanUndo(),
          "module data was released before dependent edit state");
    f.app.Stop();
    Check(f.destroyed == 1 && f.cancels == 1, "repeated stop restored or released state twice");
}

struct QueryFixture {
    struct Source {
        const char *text;
        unsigned reads = 0;
    } sources[2]{{"Original query text retained while configuration changes"},
                 {"Replacement query text from the selected provider"}};
    EditorApplication app{{}};
    std::string error;
    QueryFixture() {
        const EditorParameter field{EDITOR_TEXT("value"), EDITOR_TEXT_VALUE};
        EditorQueryDesc queries[2]{};
        for (unsigned i = 0; i < 2; ++i) {
            auto &query = queries[i];
            query.size = sizeof(query);
            query.id = EDITOR_TEXT("test.query");
            query.provider = i ? EditorString EDITOR_TEXT("test.second") : EditorString EDITOR_TEXT("test.first");
            query.area = EDITOR_WORKSPACE;
            query.refresh = EDITOR_QUERY_VERSIONED;
            query.fields = &field;
            query.fieldCount = 1;
            query.data = &sources[i];
            query.revision = [](void *, const EditorHost *) -> uint64_t { return 1; };
            query.read = [](void *p, const EditorHost *, const EditorRequest *, EditorSink sink) {
                auto &source = *static_cast<Source *>(p);
                ++source.reads;
                const EditorValue value{EDITOR_TEXT("value"), EDITOR_TEXT_VALUE, {}, EditorLiteral(source.text)};
                return sink.write(sink.data, &value, 1);
            };
        }
        const EditorCommandDesc command{
            sizeof(EditorCommandDesc),
            EDITOR_TEXT("test.noop"),
            EDITOR_TEXT("test.queries"),
            EDITOR_TEXT("Noop"),
            nullptr,
            0,
            [](void *, const EditorHost *, const EditorRequest *, EditorSink) { return EDITOR_OK; },
            nullptr,
            EDITOR_WORKSPACE,
            EDITOR_SESSION_CHANGE};
        EditorModuleDesc module{};
        module.size = sizeof(module);
        module.version = EDITOR_VERSION;
        module.id = EDITOR_TEXT("test.queries");
        module.queries = queries;
        module.queryCount = 2;
        module.commands = &command;
        module.commandCount = 1;
        EditorConfiguration configuration;
        configuration.providers = {{"query", "test.query", "test.first"}};
        Check(app.Register(module, error) && app.Activate(EDITOR_IMGUI_VERSION, error, configuration), error.c_str());
    }
    std::string Read(EditorView view) {
        const auto result = app.Query("test.query", {app.Scope(view)});
        Check(result.status == EDITOR_OK && result.fields.size() == 1, "read query projection");
        return result.fields.front().text;
    }
};

void QueryConfigurationFailure() {
    QueryFixture f;
    const auto view = f.app.OpenScope();
    const auto original = f.Read(view);
    uint64_t version = 0, next = 0;
    Check(f.app.QueryRevision("test.query", version) == EDITOR_OK, "query version");
    auto configuration = f.app.Configuration();
    configuration.bindings = {{"test.noop", "Invalid+Key"}};
    Check(!f.app.Configure(configuration, f.error) && f.Read(view) == original && f.sources[0].reads == 1 &&
              f.app.QueryRevision("test.query", next) == EDITOR_OK && next == version,
          "failed configuration changed cached results or their version");
    configuration = f.app.Configuration();
    configuration.providers.front().provider = "test.second";
    Check(f.app.Configure(configuration, f.error) && f.Read(view) == f.sources[1].text && f.sources[1].reads == 1 &&
              f.app.QueryRevision("test.query", next) == EDITOR_OK && next > version,
          "query replacement retained the old cache");
    version = next;
    configuration.providers.front().provider = "test.first";
    Check(f.app.Configure(configuration, f.error) && f.Read(view) == original && f.sources[0].reads == 2 &&
              f.app.QueryRevision("test.query", next) == EDITOR_OK && next > version,
          "restored query reused an old published version");
}

void DeliveryAcrossConfiguration() {
    QueryFixture f;
    const auto view = f.app.OpenScope();
    const auto original = f.Read(view);
    struct Delivery {
        QueryFixture &fixture;
        EditorView view;
        bool completed = false;
    } delivery{f, view};
    const EditorSink sink{&delivery, [](void *p, const EditorValue *values, uint32_t count) -> EditorStatus {
                              auto &delivery = *static_cast<Delivery *>(p);
                              auto &f = delivery.fixture;
                              Check(count == 1, "query delivery fields");
                              const auto span = values[0].text;
                              const std::string original(span.data, span.size);
                              auto configuration = f.app.Configuration();
                              configuration.providers.front().provider = "test.second";
                              Check(f.app.Configure(configuration, f.error) &&
                                        f.Read(delivery.view) == f.sources[1].text,
                                    "nested provider replacement failed");
                              Check(std::string(span.data, span.size) == original && original == f.sources[0].text,
                                    "configuration invalidated a delivered projection");
                              delivery.completed = true;
                              return EDITOR_OK;
                          }};
    const auto *host = f.app.Host();
    const EditorRequest request{f.app.Scope(view)};
    Check(host->query(host->data, EditorString EDITOR_TEXT("test.query"), &request, sink) == EDITOR_OK &&
              delivery.completed,
          "retained projection did not survive configuration");
}

void QueryScopeLifetime() {
    QueryFixture f;
    const auto a = f.app.OpenScope(), b = f.app.OpenScope();
    const auto oldScope = f.app.Scope(a);
    f.Read(a);
    f.Read(b);
    struct Watch {
        unsigned calls = 0;
        EditorScope scope{};
    } first, second;
    const auto changed = [](void *p, const EditorHost *, const EditorChange *change) {
        auto &watch = *static_cast<Watch *>(p);
        ++watch.calls;
        watch.scope = change->scope;
    };
    EditorSubscription subscriptions[2]{};
    const auto *host = f.app.Host();
    Check(host->watch(host->data, a, changed, &first, &subscriptions[0]) == EDITOR_OK &&
              host->watch(host->data, b, changed, &second, &subscriptions[1]) == EDITOR_OK,
          "subscribe views");
    f.app.EndFrame();
    first.calls = second.calls = 0;
    f.app.CloseScope(a);
    Check(f.Read(b) == f.sources[0].text && f.sources[0].reads == 2 &&
              f.app.Query("test.query", {oldScope}).status == EDITOR_STALE,
          "closing a view invalidated another view's projection");
    const DocumentToken replacement{42};
    f.app.Attach(replacement, 1, false, false);
    f.app.EndFrame();
    Check(first.calls == 0 && second.calls == 1 && second.scope.document == replacement && second.scope.view == b,
          "document replacement failed to retarget only live subscriptions");
    Check(f.Read(b) == f.sources[0].text && f.sources[0].reads == 3,
          "document replacement retained a previous document's cache");
}
} // namespace

int main(int argc, char **argv) {
    try {
        Check(argc == 2, "one ownership test name is required");
        const std::string name = argv[1];
        if (name == "CapturedConfiguration")
            CapturedConfiguration();
        else if (name == "CapturedAppearance")
            CapturedConfiguration(true);
        else if (name == "DraftConfiguration")
            DraftConfiguration();
        else if (name == "DraftAppearance")
            DraftConfiguration(true);
        else if (name == "FailedConfigurationKeepsDraft")
            FailedConfigurationKeepsDraft();
        else if (name == "PreviewConfigurationCancel")
            PreviewConfigurationCancel();
        else if (name == "PendingPreviewConfiguration")
            PendingPreviewConfiguration();
        else if (name == "PendingPreviewClose")
            PendingPreviewClose();
        else if (name == "DraftScopeLifetime")
            DraftScopeLifetime();
        else if (name == "DocumentReplacement")
            DocumentReplacement();
        else if (name == "StopCleanupOrder")
            StopCleanupOrder();
        else if (name == "QueryConfigurationFailure")
            QueryConfigurationFailure();
        else if (name == "DeliveryAcrossConfiguration")
            DeliveryAcrossConfiguration();
        else if (name == "QueryScopeLifetime")
            QueryScopeLifetime();
        else
            throw std::runtime_error("Unknown ownership test: " + name);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
