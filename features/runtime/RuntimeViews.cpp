#include "features/runtime/RuntimeViews.h"
#include "features/runtime/RuntimeViewData.h"

namespace vdev {
namespace {
struct Window { const char *role; bool toggle; };
EditorStatus Open(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    const auto &window = *static_cast<Window *>(data);
    const EditorClient client(host, request->scope.view);
    const std::string role(window.role);
    EditorView id = 0;
    if (window.toggle) {
        const auto result = client.Query("workspace.role", {TextArgument("role", role)});
        if (result.status != EDITOR_OK) return result.status;
        id = EditorClient::Integer(result, "view");
    }
    const auto arg = id ? IntegerArgument("view", id) : TextArgument("role", role);
    const EditorRequest next{request->scope, &arg, 1};
    return host->execute(host->data, EditorLiteral(id ? "workspace.close-view" : "workspace.open-role"), &next, sink);
}
EditorStatus CloseTools(void *, const EditorHost *host, const EditorRequest *, EditorSink) {
    const EditorClient client(host, 0);
    const auto instances = client.Query("workspace.instances");
    const auto *types = EditorClient::Find(instances, "types"), *ids = EditorClient::Find(instances, "views");
    if (instances.status != EDITOR_OK || !types || !ids || types->texts.size() != ids->integers.size()) return EDITOR_ERROR;
    for (size_t i = 0; i < types->texts.size(); ++i)
        if (types->texts[i] == "runtime.debugger" || types->texts[i] == "runtime.profiler" || types->texts[i] == "runtime.parameters") {
            const auto result = client.Command("workspace.close-view", {IntegerArgument("view", ids->integers[i])});
            if (result.status != EDITOR_OK) return result.status;
        }
    return EDITOR_OK;
}
enum class WatchAction { Add, Remove, Clear };
EditorStatus Watches(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    const auto action = *static_cast<WatchAction *>(data);
    const EditorClient client(host, request->scope.view);
    EditorView id = request->scope.view;
    bool explicitView = false;
    std::vector<EditorValue> arguments;
    for (uint32_t i = 0; i < request->count; ++i) {
        const auto &value = request->values[i];
        if (std::string(value.key.data, value.key.size) == "view") { id = value.integer; explicitView = true; }
        else arguments.push_back(value);
    }
    if (id) {
        const auto view = client.Query("workspace.view", {IntegerArgument("view", id)});
        if (view.status != EDITOR_OK || EditorClient::Text(view, "type") != "runtime.parameters") {
            if (explicitView) return EDITOR_INVALID;
            id = 0;
        }
    }
    if (!id) {
        if (action != WatchAction::Add) {
            const auto role = client.Query("workspace.role", {TextArgument("role", "main.parameters")});
            if (role.status != EDITOR_OK) return role.status;
            id = EditorClient::Integer(role, "view");
            if (!id) return EDITOR_NO_CHANGE;
        }
    }
    if (!id) {
        const auto opened = client.Command("workspace.open-role", {TextArgument("role", "main.parameters")});
        if (opened.status != EDITOR_OK) return opened.status;
        id = EditorClient::Integer(opened, "view");
    }
    if (action == WatchAction::Clear) arguments.push_back(IntegerArgument("clear", 1));
    const EditorRequest next{request->scope, arguments.data(), uint32_t(arguments.size())};
    return host->navigateView(host->data, EditorString EDITOR_TEXT("runtime.parameters"), id, &next, sink);
}
} // namespace
bool RegisterRuntimeViews(EditorApplication &editor, std::string &error) {
    static Window windows[] = {{"main.debugger", false}, {"main.debugger", true}, {"main.profiler", false}, {"main.profiler", true},
        {"main.parameters", false}, {"main.parameters", true}};
    const EditorViewDesc views[] = {runtime::DebuggerView(), runtime::ProfilerView(), runtime::ParameterDebuggerView()};
    const EditorParameter objects[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter remove[] = {{EDITOR_TEXT("remove"), EDITOR_OBJECT}, {EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter view{EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, 1};
    static WatchAction watchActions[] = {WatchAction::Add, WatchAction::Remove, WatchAction::Clear};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.open-debugger"), EDITOR_TEXT("builtin.runtime-views"), EDITOR_TEXT("Script Debugger"), nullptr, 0, Open, &windows[0], EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.toggle-debugger"), EDITOR_TEXT("builtin.runtime-views"), EDITOR_TEXT("Script Debugger"), nullptr, 0, Open, &windows[1], EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.open-profiler"), EDITOR_TEXT("builtin.runtime-views"), EDITOR_TEXT("Profiler"), nullptr, 0, Open, &windows[2], EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.toggle-profiler"), EDITOR_TEXT("builtin.runtime-views"), EDITOR_TEXT("Profiler"), nullptr, 0, Open, &windows[3], EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.open-parameters"), EDITOR_TEXT("builtin.runtime-views"), EDITOR_TEXT("Parameter Debugger"), nullptr, 0, Open, &windows[4], EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.toggle-parameters"), EDITOR_TEXT("builtin.runtime-views"), EDITOR_TEXT("Parameter Debugger"), nullptr, 0, Open, &windows[5], EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.watch-parameters"), EDITOR_TEXT("builtin.runtime-views"), EDITOR_TEXT("Watch Parameters"), objects, 2, Watches, &watchActions[0], EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.unwatch-parameter"), EDITOR_TEXT("builtin.runtime-views"), EDITOR_TEXT("Remove Parameter Watch"), remove, 2, Watches, &watchActions[1], EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.clear-watches"), EDITOR_TEXT("builtin.runtime-views"), EDITOR_TEXT("Clear Parameter Watches"), &view, 1, Watches, &watchActions[2], EDITOR_APPLICATION, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.close-tools"), EDITOR_TEXT("builtin.runtime-views"), EDITOR_TEXT("Close Runtime Tools"), nullptr, 0, CloseTools, nullptr, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE}};
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("runtime.views");
    static const EditorString dependencies[] = {EDITOR_TEXT("runtime.execution"), EDITOR_TEXT("workspace.views"), EDITOR_TEXT("schematic.views"), EDITOR_TEXT("parameters.editing")};
    module.dependencies = dependencies; module.dependencyCount = uint32_t(std::size(dependencies));
    module.views = views; module.viewCount = uint32_t(std::size(views));
    module.commands = commands; module.commandCount = uint32_t(std::size(commands));
    const EditorMenuLocation location{EDITOR_TEXT("editors.diagnostics"), EDITOR_TEXT("Editors diagnostics")};
    const EditorMenuDesc menu{EDITOR_TEXT("runtime.parameters"), EDITOR_TEXT("runtime.toggle-parameters"), EDITOR_TEXT("editors.diagnostics"),
        EDITOR_TEXT("Parameter Debugger"), 0, EDITOR_TEXT("runtime.parameters-open")};
    const EditorParameter checked{EDITOR_TEXT("checked"), EDITOR_INTEGER_VALUE};
    const EditorQueryDesc query{sizeof(EditorQueryDesc), EDITOR_TEXT("runtime.parameters-open"), EDITOR_TEXT("builtin.runtime-views"),
        nullptr, 0, [](void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
            const EditorClient client(host, request->scope.view);
            const auto role = client.Query("workspace.role", {TextArgument("role", "main.parameters")});
            if (role.status != EDITOR_OK) return role.status;
            const auto value = IntegerArgument("checked", EditorClient::Integer(role, "view") != 0);
            return sink.write ? sink.write(sink.data, &value, 1) : EDITOR_INVALID;
        }, nullptr, EDITOR_QUERY_EACH_READ, EDITOR_WORKSPACE, &checked, 1};
    module.locations = &location; module.locationCount = 1;
    module.menus = &menu; module.menuCount = 1;
    module.queries = &query; module.queryCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev
