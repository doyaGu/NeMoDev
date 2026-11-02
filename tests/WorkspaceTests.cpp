#include "workspace/EditorWorkspace.h"
#include "editor/ImGuiDrawing.h"
#include "core/document/Document.h"
#include <map>
#include <stdexcept>
#include <thread>

using namespace vdev;
namespace {
void Check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
std::string Text(EditorString value) { return {value.data ? value.data : "", value.size}; }
struct Browser;
struct Provider {
    std::map<EditorView, Browser *> views;
    unsigned created = 0, destroyed = 0, closing = 0;
    bool closingScopeAlive = false;
    bool failCreate = false, failRestore = false, failSave = false, saveObject = false;
    EditorStatus mutation = EDITOR_OK;
    EditorStatus inspectionMutation = EDITOR_OK;
    unsigned inspections = 0;
    ObjectRef reference{};
};
struct Browser {
    Provider &provider;
    EditorView id;
    std::string topic;
    ObjectRef target{};
};
void *Create(void *data, const EditorHost *host, EditorView id) {
    auto &provider = *static_cast<Provider *>(data);
    const EditorValue type{EDITOR_TEXT("type"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("test.browser")};
    const EditorRequest request{host->scope(host->data, id), &type, 1};
    provider.mutation = host->execute(host->data, EditorString EDITOR_TEXT("workspace.open-view"), &request, {});
    if (provider.failCreate) return nullptr;
    auto *view = new Browser{provider, id, {}, provider.reference};
    provider.views.emplace(id, view);
    ++provider.created;
    return view;
}
void Destroy(void *data) {
    auto *view = static_cast<Browser *>(data);
    ++view->provider.destroyed;
    view->provider.views.erase(view->id);
    delete view;
}
EditorStatus Navigate(void *data, const EditorHost *, const EditorRequest *request) {
    auto &view = *static_cast<Browser *>(data);
    const auto topic = Text(request->values[0].text);
    if (topic == "fail") return EDITOR_ERROR;
    if (topic == "throw") throw std::runtime_error("navigation failure");
    view.topic = topic;
    return EDITOR_OK;
}
EditorStatus Save(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
    auto &view = *static_cast<Browser *>(data);
    if (view.provider.failSave) throw std::runtime_error("save state failure");
    const EditorValue field{EDITOR_TEXT("topic"), view.provider.saveObject ? EDITOR_OBJECT : EDITOR_TEXT_VALUE,
        {}, EditorText(view.topic)};
    return sink.write(sink.data, &field, 1);
}
EditorStatus Restore(void *data, const EditorHost *, const EditorRequest *request) {
    auto &view = *static_cast<Browser *>(data);
    if (view.provider.failRestore) return EDITOR_ERROR;
    return Navigate(data, nullptr, request);
}
EditorStatus Open(void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    EditorValue topic{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE};
    EditorView view = 0;
    for (uint32_t i = 0; i < request->count; ++i) {
        if (Text(request->values[i].key) == "topic") topic.text = request->values[i].text;
        if (Text(request->values[i].key) == "view") view = request->values[i].integer;
    }
    const EditorRequest navigation{request->scope, &topic, 1};
    return host->navigateView(host->data, EditorString EDITOR_TEXT("test.browser"), view, &navigation, sink);
}
EditorView Id(const EditorResponse &result) {
    Check(result.status == EDITOR_OK, "view command failed");
    for (const auto &field : result.fields) if (field.key == "view") return field.integer;
    throw std::runtime_error("view command lost its instance");
}
EditorViewDesc Description(Provider &provider, const char *name, const char *type = "test.browser") {
    static const EditorParameter topic{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE, 1};
    auto desc = EditorViewDesc{sizeof(EditorViewDesc), EDITOR_IMGUI_VERSION, EditorLiteral(type), EditorLiteral(name),
        EDITOR_TEXT("Browser"), Create, Destroy,
        [](void *, const EditorHost *, EditorView, EditorViewFrame *frame) { EditorImGuiWindow window(*frame); },
        &provider, EDITOR_WINDOW_VIEW, 1, EDITOR_WORKSPACE, &topic, 1, Navigate, 1, Save, Restore};
    static const EditorParameter fields{EDITOR_TEXT("topic"),EDITOR_TEXT_VALUE};
    desc.fields=&fields;desc.fieldCount=1;
    desc.inspect=[](void*data,const EditorHost*host,const EditorRequest*request,EditorSink sink){
        auto&view=*static_cast<Browser*>(data);++view.provider.inspections;
        const EditorValue type{EDITOR_TEXT("type"),EDITOR_TEXT_VALUE,{},EDITOR_TEXT("test.browser")};
        const EditorRequest mutate{request->scope,&type,1};
        view.provider.inspectionMutation=host->execute(host->data,EditorString EDITOR_TEXT("workspace.open-view"),&mutate,{});
        const EditorValue value{EDITOR_TEXT("topic"),EDITOR_TEXT_VALUE,{},EditorText(view.topic)};
        return sink.write(sink.data,&value,1);
    };
    desc.inspectRevision=[](void*data,const EditorHost*){uint64_t version=0;for(auto c:static_cast<Browser*>(data)->topic)version=version*131+uint8_t(c);return version;};
    desc.closing = [](void *data, const EditorHost *host, EditorView id) {
        auto &view = *static_cast<Browser *>(data);
        ++view.provider.closing;
        view.provider.closingScopeAlive = host->scope(host->data, id).view == id;
    };
    return desc;
}
}
void TestWorkspace(CkRuntimeHost &runtime) {
    Document document(runtime);
    std::string documentError;
    Check(document.New(&documentError), documentError.c_str());
    Provider first, second, third, fourth, incompatible, records;
    bool native = true;
    EditorApplication app([&] { return &document; }, [&] { return native; });
    EditorWorkspace workspace(app);
    std::string error;
    Check(workspace.Register(error), error.c_str());
    auto a = Description(first, "test.first"), b = Description(second, "test.second");
    auto c = Description(third, "test.third", "test.other");
    c.singleInstance = 0;
    auto d = c;
    d.id = EditorLiteral("test.native");
    d.area = EDITOR_DOCUMENT;
    auto e = c;
    e.provider = EditorLiteral("test.fourth");
    e.data = &fourth;
    auto f = b;
    f.provider = EditorLiteral("test.incompatible");
    f.data = &incompatible;
    f.stateVersion = 2;
    auto recordView = Description(records, "test.records", "test.records");
    static const EditorParameter recordFields[] = {{EDITOR_TEXT("details"), EDITOR_RECORD}, {EDITOR_TEXT("numbers"), EDITOR_REAL_LIST}};
    recordView.fields = recordFields; recordView.fieldCount = 2;
    recordView.inspect = [](void *, const EditorHost *, const EditorRequest *, EditorSink sink) {
        EditorField details{"details", EDITOR_RECORD};
        details.fields = {{"weights", EDITOR_REAL_LIST}, {"names", EDITOR_TEXT_LIST}};
        details.fields[0].reals = {.25, .75}; details.fields[1].texts = {"left", "right"};
        std::vector<EditorField> fields{details, {"numbers", EDITOR_REAL_LIST}}; fields[1].reals = {1, 2, 3};
        const auto values = BorrowEditorFields(fields);
        return sink.write(sink.data, values.data(), uint32_t(values.size()));
    };
    Provider boundFirst, boundSecond;
    auto *object = document.Context()->CreateObject(CKCID_OBJECT, "Bound view target");
    Check(object != nullptr, "bound view object");
    document.Include(object->GetID());
    boundFirst.reference = document.Reference(object->GetID());
    auto boundA = Description(boundFirst, "test.bound-first", "test.bound");
    boundA.area = EDITOR_DOCUMENT;
    boundA.documentBound = 1;
    boundA.saveState = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &view = *static_cast<Browser *>(data);
        const EditorValue values[] = {{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE, {}, EditorText(view.topic)},
                                      {EDITOR_TEXT("target"), EDITOR_OBJECT, view.target}};
        return sink.write(sink.data, values, 2);
    };
    boundA.restoreState = [](void *data, const EditorHost *, const EditorRequest *request) {
        auto &view = *static_cast<Browser *>(data);
        if (view.provider.failRestore)
            return EDITOR_ERROR;
        for (uint32_t i = 0; i < request->count; ++i) {
            const auto &value = request->values[i];
            if (Text(value.key) == "target")
                view.target = value.object;
            if (Text(value.key) == "topic")
                view.topic = Text(value.text);
        }
        return EDITOR_OK;
    };
    auto boundB = boundA;
    boundB.provider = EDITOR_TEXT("test.bound-second");
    boundB.data = &boundSecond;
    auto unbound = boundB;
    unbound.provider = EDITOR_TEXT("test.bound-unbound");
    unbound.documentBound = 0;
    const EditorViewDesc views[] = {a, b, c, d, e, f, recordView, boundA, boundB, unbound};
    const EditorParameter arguments[] = {{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE, 1},
        {EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, 1}};
    const EditorCommandDesc command{sizeof(EditorCommandDesc), EDITOR_TEXT("test.navigate"),
        EDITOR_TEXT("test.commands"), EDITOR_TEXT("Navigate"), arguments, 2, Open, nullptr,
        EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("test.browser-module");
    module.views = views; module.viewCount = uint32_t(std::size(views));
    auto mixed = command;
    mixed.id = EditorLiteral("test.mixed-navigation");
    mixed.count = 0;
    mixed.parameters = nullptr;
    mixed.area = EDITOR_DOCUMENT;
    mixed.effect = EDITOR_DOCUMENT_EDIT;
    mixed.run = [](void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
        const EditorRequest navigation{request->scope, nullptr, 0};
        return host->navigateView(host->data, EditorString EDITOR_TEXT("test.browser"), 0, &navigation, sink);
    };
    const EditorCommandDesc commands[] = {command, mixed};
    module.commands = commands; module.commandCount = 2;
    Check(app.Register(module, error), error.c_str());
    EditorConfiguration config;
    config.providers.push_back({"view", "test.browser", "test.first"});
    config.providers.push_back({"view", "test.other", "test.third"});
    config.providers.push_back({"view", "test.bound", "test.bound-first"});
    Check(app.Activate(EDITOR_IMGUI_VERSION, error, config), error.c_str());
    app.Attach(document.Token(), document.TopologyRevision(), true, true);
    {
        const auto view = workspace.Open("test.records");
        Check(view != 0, "open nested projection view");
        const auto expected = workspace.Inspect(view);
        std::vector<EditorField> copied;
        const EditorSink sink{&copied, [](void *data, const EditorValue *values, uint32_t count) {
            return CopyEditorFields(*static_cast<std::vector<EditorField> *>(data), values, count);
        }};
        Check(app.Host()->queryView(app.Host()->data, view, sink) == EDITOR_OK && copied == expected.fields,
              "workspace query forwarding dropped nested records or real lists");
        Check(workspace.Close(view), "close nested projection view");
    }
    const auto open = [&](const char *topic, EditorView target = 0) {
        const EditorValue values[] = {{EDITOR_TEXT("topic"), EDITOR_TEXT_VALUE, {}, EditorLiteral(topic)},
            {EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, {}, {}, target}};
        return app.Execute("test.navigate", {app.Scope(), values, 2});
    };
    auto id = Id(open("first topic"));
    Check(first.views.at(id)->topic == "first topic" && workspace.ViewCount() == 1, "navigation did not reach its view");
    Check(first.mutation == EDITOR_REFUSED, "creation callback mutated the workspace");
    const auto inspected=workspace.Inspect(id);
    Check(inspected.status==EDITOR_OK&&inspected.fields.front().text=="first topic","instance query missed its data");
    Check(first.inspectionMutation==EDITOR_REFUSED,"instance query modified workspace");
    const auto reads=first.inspections;
    Check(workspace.Inspect(id).status==EDITOR_OK&&first.inspections==reads,"unchanged instance query was repeated");
    EditorStatus inspectionThreadStatus=EDITOR_OK;
    std::thread inspectionThread([&]{inspectionThreadStatus=app.Host()->queryView(app.Host()->data,id,{});});inspectionThread.join();
    Check(inspectionThreadStatus!=EDITOR_OK,"instance query crossed threads");
    Check(app.Host()->pickRegions(app.Host()->data,id,nullptr,0)==EDITOR_REFUSED,"pick publication escaped drawing");

    Check(Id(open("second topic")) == id && first.created == 1, "single view was recreated");
    Check(open("fail").status == EDITOR_ERROR && first.views.at(id)->topic == "second topic", "failed navigation changed view");
    Check(open("throw").status == EDITOR_ERROR && first.views.at(id)->topic == "second topic", "throwing navigation changed view");
    const auto name = workspace.WindowName(id);
    Check(name.find("###editor-view-") != std::string::npos, "window has no instance identity");
    const EditorRequest direct{app.Scope(), nullptr, 0};
    Check(app.Host()->navigateView(app.Host()->data, EDITOR_TEXT("test.browser"), id, &direct, {}) == EDITOR_REFUSED,
          "navigation escaped registered commands");
    const auto other = workspace.Open("test.other"), another = workspace.Open("test.other");
    Check(other && another && other != another && workspace.WindowName(other) != workspace.WindowName(another),
          "independent view instances share identity");
    Check(open("wrong target", other).status == EDITOR_INVALID, "navigation accepted another view type");
    const EditorValue invalid{EDITOR_TEXT("topic"), EDITOR_INTEGER_VALUE, {}, {}, 1};
    Check(app.Execute("test.navigate", {app.Scope(), &invalid, 1}).status == EDITOR_INVALID, "invalid navigation type accepted");
    const auto list = app.Query("workspace.views", direct);
    Check(list.status == EDITOR_OK && list.fields[0].integers.size() == 3, "workspace query lost views");
    auto replacement = config;
    replacement.providers[0].provider = "test.second";
    unsigned writes = 0;
    bool refuseWrite = true, throwWrite = false;
    workspace.ConfigurationWriter([&](const EditorConfiguration &, std::string &message) {
        ++writes;
        Check(first.views.count(id) && !second.views.empty() && first.closing == 0,
              "configuration saved before preparation or after the old view was destroyed");
        if (throwWrite) throw std::runtime_error("Injected profile write exception");
        if (refuseWrite) { message = "Injected profile publication refusal"; return false; }
        return true;
    });
    second.failCreate = true;
    Check(!workspace.Configure(replacement, error) && first.views.count(id) && app.Configuration().providers[0].provider == "test.first",
          "failed creation replaced old view or configuration");
    second.failCreate = false;
    second.failRestore = true;
    Check(!workspace.Configure(replacement, error) && first.views.at(id)->topic == "second topic" && second.views.empty(),
          "failed state restoration lost old view or staged view");
    second.failRestore = false;
    auto multiple = replacement;
    multiple.providers[1].provider = "test.fourth";
    fourth.failCreate = true;
    Check(!workspace.Configure(multiple, error) && first.views.count(id) && third.views.count(other) &&
          third.views.count(another) && second.views.empty() && app.Configuration().providers[0].provider == "test.first",
          "later replacement failure did not roll back the entire view set");
    auto badVersion = config;
    badVersion.providers[0].provider = "test.incompatible";
    Check(!workspace.Configure(badVersion, error) && first.views.count(id) && incompatible.created == 0,
          "incompatible state version destroyed an open view");
    first.saveObject = true;
    Check(!workspace.Configure(replacement, error) && first.views.count(id), "native references escaped in saved view state");
    first.saveObject = false;
    Check(first.closing == 0, "failed replacement closed the source view");
    Check(writes == 0, "failed view preparation wrote configuration");
    workspace.Focus(id);
    const auto documentToken = document.Token();
    const auto history = document.History().UndoCount();
    const auto redo = document.History().CanRedo();
    const auto selected = app.Selection();
    Check(!workspace.Configure(replacement, error) && error == "Injected profile publication refusal" && writes == 1 &&
          first.views.count(id) && second.views.empty() && !first.closing && app.Scope(id).view &&
          workspace.LastFocused("test.browser") == id && app.Configuration().providers[0].provider == "test.first",
          "failed configuration publication lost the original view, focus or provider");
    throwWrite = true;
    Check(!workspace.Configure(replacement, error) && writes == 2 && first.views.count(id) && second.views.empty() &&
          !first.closing && document.Token() == documentToken && document.History().UndoCount() == history &&
          document.History().CanRedo() == redo && app.Selection() == selected,
          "throwing configuration writer changed the view or document");
    throwWrite = refuseWrite = false;
    Check(workspace.Configure(replacement, error), error.c_str());
    Check(writes == 3, "successful provider replacement was not saved exactly once");
    workspace.ConfigurationWriter({});
    Check(first.closing == 1 && first.closingScopeAlive, "replacement closing callback lost its scope");
    Check(first.views.empty() && second.views.size() == 1 && second.views.begin()->second->topic == "second topic",
          "provider replacement lost navigation");
    const auto old = id;
    id = second.views.begin()->first;
    Check(id != old && !app.Scope(old).view && open("stale", old).status == EDITOR_STALE, "replaced instance scope survived");
    const auto closingBefore = second.closing;
    second.failSave = true;
    Check(!workspace.Close(id) && second.views.count(id), "failed state capture closed a view");
    Check(second.closing == closingBefore, "failed close state capture ran closing callback");
    second.failSave = false;
    Check(workspace.Close(id) && !app.Scope(id).view, "view close retained its scope");
    Check(second.closing == closingBefore + 1 && second.closingScopeAlive,
          "view closed without its live closing callback");
    Check(!workspace.Configure(badVersion, error), "incompatible closed-view state was accepted");
    Check(open("throw").status == EDITOR_ERROR && second.views.empty(), "failed reopening retained a new view");
    const auto reopened = workspace.Open("test.browser");
    Check(reopened && second.views.at(reopened)->topic == "second topic", "close/reopen lost state");
    auto boundLayout = workspace.Configuration();
    boundLayout.views.push_back({"test.bound", "", "", false, false, false, WorkspaceWindowId::Role});
    Check(workspace.Configure(boundLayout, error), error.c_str());
    const auto bound = workspace.Open("test.bound", "bound.object");
    Check(bound && workspace.WindowName(bound) == "Browser###bound.object", "bound window role identity");
    boundFirst.views.at(bound)->topic = "Preserved document window";
    workspace.Focus(bound);
    Check(workspace.LastFocused("test.bound") == bound, "last focused instance");
    auto boundConfig = app.Configuration();
    for (auto &choice : boundConfig.providers)
        if (choice.id == "test.bound")
            choice.provider = "test.bound-second";
    boundSecond.failRestore = true;
    Check(!workspace.Configure(boundConfig, error) && boundFirst.views.count(bound) && boundSecond.views.empty() &&
              workspace.LastFocused("test.bound") == bound,
          "failed document view restore lost original");
    boundSecond.failRestore = false;
    auto unboundConfig = boundConfig;
    for (auto &choice : unboundConfig.providers)
        if (choice.id == "test.bound")
            choice.provider = "test.bound-unbound";
    Check(!workspace.Configure(unboundConfig, error) && boundFirst.views.count(bound),
          "view replacement changed document lifetime");
    ++boundFirst.views.at(bound)->target.lifetime;
    Check(!workspace.Configure(boundConfig, error) && boundFirst.views.count(bound),
          "stale object reference was restored");
    --boundFirst.views.at(bound)->target.lifetime;
    Check(workspace.Configure(boundConfig, error) && boundFirst.views.empty() && boundSecond.views.size() == 1,
          "document view replacement failed");
    const auto changedBound = boundSecond.views.begin()->first;
    Check(changedBound != bound && boundSecond.views.begin()->second->target == boundFirst.reference &&
              boundSecond.views.begin()->second->topic == "Preserved document window" &&
              workspace.LastFocused("test.bound") == changedBound &&
              workspace.WindowName(changedBound) == "Browser###bound.object",
          "document view replacement lost target state focus or window identity");
    workspace.DocumentChanging();
    Check(boundSecond.views.empty() && !app.Scope(changedBound).view && !workspace.LastFocused("test.bound") &&
              !second.views.empty(),
          "document change closed permanent windows or retained bound ones");
    const auto freshBound = workspace.Open("test.bound");
    Check(freshBound && boundSecond.views.at(freshBound)->topic.empty(),
          "bound state survived close across document scopes");
    Check(workspace.Close(freshBound), "close fresh document view");
    const auto nativeView = workspace.Open("test.native");
    Check(nativeView != 0, "document view did not open");
    native = false;
    app.NativeAvailable(false);
    Check(!workspace.Open("test.native") && !workspace.Close(nativeView), "native view lifecycle ran during native work");
    Check(app.Query("workspace.views", {app.Scope(), nullptr, 0}).status == EDITOR_OK &&
          open("while busy").status == EDITOR_OK, "workspace navigation was blocked by native work");
    EditorStatus wrongThread = EDITOR_OK;
    std::thread thread([&] { wrongThread = app.Execute("test.navigate", direct).status; });
    thread.join();
    Check(wrongThread == EDITOR_REFUSED, "worker accessed view navigation");
    native = true;
    app.NativeAvailable(true);
    workspace.CloseAll();
    Check(first.created == first.destroyed && second.created == second.destroyed && third.created == third.destroyed && fourth.created == fourth.destroyed,
          "view cleanup leaked instances");
    Check(open("throw").status == EDITOR_ERROR && workspace.ViewCount() == 0, "new failed navigation left a view");
    const auto count = document.History().UndoCount();
    Check(app.Execute("test.mixed-navigation", {app.Scope(), nullptr, 0}).status == EDITOR_REFUSED &&
          workspace.ViewCount() == 0 && document.History().UndoCount() == count,
          "document edit callback left an untracked workspace mutation");
    app.Attach({}, document.TopologyRevision(), true, false);
    const EditorValue type{EDITOR_TEXT("type"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("test.native")};
    Check(app.Execute("workspace.open-view", {app.Scope(), &type, 1}).status == EDITOR_OK && workspace.ViewCount() == 1,
          "an empty document prevented opening its view window");
    app.Attach(document.Token(), document.TopologyRevision(), true, true);
    const auto primary=workspace.Open("test.other","test.primary",true);
    Check(primary&&workspace.Role("test.primary")==primary,"workspace role was not assigned");
    Check(workspace.Open("test.other","test.primary",true)==primary,"workspace role created a duplicate instance");
    Check(!workspace.Open("test.native","test.primary"),"workspace role changed view type");
    Check(!workspace.Close(primary),"pinned primary view was closed");
    workspace.CloseAll();

    WorkspaceConfiguration layout;
    layout.contributions = app.Configuration();
    layout.splits = {{"root", "left", "right", WorkspaceDirection::Left, .4f}};
    layout.regions = {{"left", "", ""}, {"right", "", ""}};
    layout.views = {{"test.other", "test.main", "left", true, true},
                    {"test.other", "test.secondary", "right", true}};
    Check(workspace.Configure(layout, error), error.c_str());
    const auto main = workspace.Role("test.main"), secondary = workspace.Role("test.secondary");
    const auto roleQuery = [&](const char *role) {
        const EditorValue argument{EDITOR_TEXT("role"), EDITOR_TEXT_VALUE, {}, EditorLiteral(role)};
        const auto result = app.Query("workspace.role", {app.Scope(), &argument, 1});
        Check(result.status == EDITOR_OK && result.fields.size() == 1, "workspace role projection failed");
        return result.fields.front().integer;
    };
    uint64_t roleRevision = 0;
    Check(app.QueryRevision("workspace.role", roleRevision) == EDITOR_OK && roleQuery("test.main") == main &&
          roleQuery("test.secondary") == secondary && !roleQuery("test.missing"), "workspace role projection lost an instance");
    Check(main && secondary && main != secondary && workspace.ViewCount() == 2 &&
        workspace.OpenRole("test.main") == main && !workspace.Close(main), "workspace configuration did not create independent primary roles");
    const auto generation = [&] { uint64_t version = 0; Check(app.QueryRevision("workspace.instances", version) == EDITOR_OK, "workspace instance version unavailable"); return version; };
    const auto configured = generation();
    auto bad = layout;
    bad.splits.push_back({"left", "root", "new", WorkspaceDirection::Up, .5f});
    Check(!workspace.Configure(bad, error) && generation() == configured && workspace.Role("test.main") == main,
        "cyclic layout changed existing workspace");
    bad = layout; bad.views[1].role = "test.main";
    Check(!workspace.Configure(bad, error) && workspace.Role("test.secondary") == secondary, "duplicate role changed existing views");
    bad = layout; bad.views[1].region = "missing";
    Check(!workspace.Configure(bad, error), "unknown dock region accepted");
    bad = layout; bad.views.push_back({"test.missing", "test.extra", "left", true});
    Check(!workspace.Configure(bad, error), "unknown view type accepted");
    bad = layout; bad.legacyWindows = {{"Legacy", "left"}, {"Legacy", "right"}};
    Check(!workspace.Configure(bad, error), "duplicate legacy window placement accepted");
    bad = layout; bad.views.push_back({"test.native", "test.extra", "left", true});
    third.failCreate = true;
    Check(!workspace.Configure(bad, error) && workspace.ViewCount() == 2 && workspace.Role("test.main") == main &&
        workspace.Role("test.secondary") == secondary && !workspace.Role("test.extra"), "failed startup view replaced previous roles");
    third.failCreate = false;
    Check(workspace.Configuration().splits[0].fraction == .4f && workspace.Configuration().views.size() == 2,
        "failed workspace configuration replaced valid layout data");
    Check(workspace.Close(secondary) && generation() != configured, "closing configured view kept instance query cache");
    uint64_t changedRoleRevision = 0;
    Check(app.QueryRevision("workspace.role", changedRoleRevision) == EDITOR_OK && changedRoleRevision != roleRevision &&
          !roleQuery("test.secondary"), "closed role kept a cached view ID");
    const auto recreated = workspace.OpenRole("test.secondary");
    Check(recreated && recreated != secondary && generation() != configured, "configured role did not reopen");
    native = false;
    Check(roleQuery("test.secondary") == recreated, "workspace role query required native access or kept the closed instance");
    native = true;
    workspace.CloseAll();
}
