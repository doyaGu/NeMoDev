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
        if (types->texts[i] == "runtime.debugger" || types->texts[i] == "runtime.profiler") {
            const auto result = client.Command("workspace.close-view", {IntegerArgument("view", ids->integers[i])});
            if (result.status != EDITOR_OK) return result.status;
        }
    return EDITOR_OK;
}
} // namespace
bool RegisterRuntimeViews(EditorApplication &editor, std::string &error) {
    static Window windows[] = {{"main.debugger", false}, {"main.debugger", true}, {"main.profiler", false}, {"main.profiler", true}};
    const EditorViewDesc views[] = {runtime::DebuggerView(), runtime::ProfilerView()};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.open-debugger"), EDITOR_TEXT("builtin.runtime-views"), EDITOR_TEXT("Script Debugger"), nullptr, 0, Open, &windows[0], EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.toggle-debugger"), EDITOR_TEXT("builtin.runtime-views"), EDITOR_TEXT("Script Debugger"), nullptr, 0, Open, &windows[1], EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.open-profiler"), EDITOR_TEXT("builtin.runtime-views"), EDITOR_TEXT("Profiler"), nullptr, 0, Open, &windows[2], EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.toggle-profiler"), EDITOR_TEXT("builtin.runtime-views"), EDITOR_TEXT("Profiler"), nullptr, 0, Open, &windows[3], EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.close-tools"), EDITOR_TEXT("builtin.runtime-views"), EDITOR_TEXT("Close Runtime Tools"), nullptr, 0, CloseTools, nullptr, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE}};
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("runtime.views");
    static const EditorString dependencies[] = {EDITOR_TEXT("runtime.execution"), EDITOR_TEXT("workspace.views"), EDITOR_TEXT("schematic.views")};
    module.dependencies = dependencies; module.dependencyCount = uint32_t(std::size(dependencies));
    module.views = views; module.viewCount = uint32_t(std::size(views));
    module.commands = commands; module.commandCount = uint32_t(std::size(commands));
    return editor.Register(module, error);
}
} // namespace vdev
