#include "features/environment/EnvironmentViews.h"
#include "features/environment/EnvironmentViewData.h"
namespace vdev {
namespace {
EditorStatus Open(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    const auto *role = static_cast<const char *>(data);
    const EditorValue argument{EDITOR_TEXT("role"), EDITOR_TEXT_VALUE, {}, EditorLiteral(role)};
    const EditorRequest next{request->scope, &argument, 1};
    return host->execute(host->data, EditorString EDITOR_TEXT("workspace.open-role"), &next, sink);
}
EditorStatus Close(void *, const EditorHost *host, const EditorRequest *, EditorSink) {
    const EditorClient client(host, 0);
    const auto instances = client.Query("workspace.instances");
    const auto *ids = EditorClient::Find(instances, "views"), *types = EditorClient::Find(instances, "types");
    if (instances.status != EDITOR_OK || !ids || !types || ids->integers.size() != types->texts.size()) return EDITOR_ERROR;
    for (size_t i = 0; i < ids->integers.size(); ++i)
        if (types->texts[i] == "environment.plugins" || types->texts[i] == "environment.paths") {
            const auto result = client.Command("workspace.close-view", {IntegerArgument("view", ids->integers[i])});
            if (result.status != EDITOR_OK) return result.status;
        }
    return EDITOR_OK;
}
} // namespace
bool RegisterEnvironmentViews(EditorApplication &editor, std::string &error) {
    const EditorViewDesc views[] = {environment::PluginView(), environment::PathView()};
    static char pluginRole[] = "main.plugins", pathRole[] = "main.paths";
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("environment.open-plugins"), EDITOR_TEXT("builtin.environment-views"),
            EDITOR_TEXT("Installed Plugins"), nullptr, 0, Open, pluginRole, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("environment.open-paths"), EDITOR_TEXT("builtin.environment-views"),
            EDITOR_TEXT("Path Manager"), nullptr, 0, Open, pathRole, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("environment.close-managers"), EDITOR_TEXT("builtin.environment-views"),
            EDITOR_TEXT("Close Plugin and Path Managers"), nullptr, 0, Close, nullptr, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE}};
    const EditorMenuLocation locations[] = {{EDITOR_TEXT("editors.paths"), EDITOR_TEXT("Editors paths")},
        {EDITOR_TEXT("options.plugins"), EDITOR_TEXT("Options plugins")}};
    const EditorMenuDesc menus[] = {
        {EDITOR_TEXT("environment.open-paths"), EDITOR_TEXT("environment.open-paths"), EDITOR_TEXT("editors.paths"), EDITOR_TEXT("Path Manager"), 0},
        {EDITOR_TEXT("environment.open-plugins"), EDITOR_TEXT("environment.open-plugins"), EDITOR_TEXT("options.plugins"), EDITOR_TEXT("Installed Plugins"), 0}};
    const EditorString dependencies[] = {EDITOR_TEXT("environment.settings"), EDITOR_TEXT("workspace.views")};
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("environment.views");
    module.dependencies = dependencies; module.dependencyCount = uint32_t(std::size(dependencies));
    module.commands = commands; module.commandCount = uint32_t(std::size(commands));
    module.views = views; module.viewCount = uint32_t(std::size(views));
    module.locations = locations; module.locationCount = uint32_t(std::size(locations));
    module.menus = menus; module.menuCount = uint32_t(std::size(menus));
    return editor.Register(module, error);
}
} // namespace vdev
