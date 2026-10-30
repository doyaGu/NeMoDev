#include "features/defaults/DefaultWorkspace.h"

namespace vdev {
WorkspaceConfiguration DefaultWorkspaceConfiguration() {
    WorkspaceConfiguration result;
    result.contributions.providers = {{"editor", "text.single-line", "builtin.text"},
                                      {"editor", "value.boolean", "builtin.values"},
                                      {"editor", "value.integer", "builtin.values"},
                                      {"editor", "value.real", "builtin.values"},
                                      {"editor", "value.enumeration", "builtin.values"},
                                      {"editor", "value.object", "builtin.values"},
                                      {"editor", "value.angle", "builtin.values"},
                                      {"editor", "value.vector2", "builtin.values"},
                                      {"editor", "value.vector3", "builtin.values"},
                                      {"editor", "value.quaternion", "builtin.values"},
                                      {"editor", "value.euler", "builtin.values"},
                                      {"editor", "value.matrix", "builtin.values"},
                                      {"editor", "value.color", "builtin.values"},
                                      {"editor", "value.box", "builtin.values"},
                                      {"editor", "value.rect", "builtin.values"},
                                      {"command", "parameter.integer-set", "builtin.parameters"},
                                      {"command", "parameter.vector3-set", "builtin.parameters"},
                                      {"query", "parameter.vector3-value", "builtin.parameters"},
                                      {"command", "object.rename", "builtin.objects"},
                                      {"command", "object.create-frame3d", "builtin.objects"},
                                      {"command", "resource.load", "builtin.documents"},
                                      {"query", "object.details", "builtin.objects"},
                                      {"view", "object.properties", "builtin.properties"},
                                      {"view", "object.summary", "builtin.summary"},
                                      {"view", "document.file-dialogs", "builtin.file-dialogs"},
                                      {"view", "document.output-dialogs", "builtin.output-dialogs"},
                                      {"view", "resource.file-dialogs", "builtin.resource-dialogs"},
                                      {"view", "application.event-log", "builtin.log"},
                                      {"view", "resource.data", "builtin.data-resources"},
                                      {"view", "resource.building-blocks", "builtin.building-blocks"},
                                      {"command", "runtime.play", "builtin.execution"}};
    result.views = {{"document.file-dialogs", "", "", true},
                    {"document.output-dialogs", "", "", true},
                    {"scene.level", "main.level", "editing", true, true},
                    {"resource.building-blocks", "main.building-blocks", "resources", true, true},
                    {"resource.file-dialogs", "", "", true},
                    {"scene.hierarchy", "main.hierarchy", "resources"},
                    {"scene.attributes", "main.attributes", "editing"},
                    {"resource.data", "", "resources", false, false, true}};
    result.splits = {{"root", "top", "editing", WorkspaceDirection::Up, .5f},
                     {"top", "resources", "layout", WorkspaceDirection::Right, .5f}};
    result.regions = {
        {"layout", "", "3D Layout"}, {"editing", "", "Schematic"}, {"resources", "main.building-blocks", ""}};
    result.legacyWindows = {{"3D Layout", "layout", false, true},
                            {"Schematic", "editing", false, true},
                            {"Parameter Debugger", "resources"},
                            {"###setup", "editing", true}};
    return result;
}
bool RegisterDefaultWorkspace(EditorApplication &editor, std::string &error) {
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("scene.open-hierarchy"), EDITOR_TEXT("builtin.workspace-navigation"),
         EDITOR_TEXT("Hierarchy Manager"), nullptr, 0,
         [](void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
             const EditorValue role{EDITOR_TEXT("role"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("main.hierarchy")};
             const EditorRequest next{request->scope, &role, 1};
             return host->execute(host->data, EditorString EDITOR_TEXT("workspace.open-role"), &next, sink);
         },
         nullptr, EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("scene.open-attributes"), EDITOR_TEXT("builtin.workspace-navigation"),
         EDITOR_TEXT("Attributes Manager"), nullptr, 0,
         [](void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
             const EditorValue role{EDITOR_TEXT("role"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("main.attributes")};
             const EditorRequest next{request->scope, &role, 1};
             return host->execute(host->data, EditorString EDITOR_TEXT("workspace.open-role"), &next, sink);
         },
         nullptr, EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE}};
    const EditorMenuLocation location{EDITOR_TEXT("editors.managers"), EDITOR_TEXT("Editors menu")};
    const EditorMenuDesc menus[] = {
        {EDITOR_TEXT("scene.open-attributes"), EDITOR_TEXT("scene.open-attributes"), EDITOR_TEXT("editors.managers"),
         EDITOR_TEXT("Attributes Manager"), 0, EDITOR_TEXT("scene.attributes-open")},
        {EDITOR_TEXT("scene.open-hierarchy"), EDITOR_TEXT("scene.open-hierarchy"), EDITOR_TEXT("editors.managers"),
         EDITOR_TEXT("Hierarchy Manager"), 1, EDITOR_TEXT("scene.hierarchy-open")}};
    const EditorParameter checked{EDITOR_TEXT("checked"), EDITOR_INTEGER_VALUE};
    const auto read = [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
        struct State {
            const char *role;
            bool open;
        } state{static_cast<const char *>(data), false};
        const auto collect = [](void *data, const EditorValue *values, uint32_t count) {
            auto &state = *static_cast<State *>(data);
            for (uint32_t i = 0; i < count; ++i) {
                const auto &value = values[i];
                if (std::string(value.key.data, value.key.size) != "roles")
                    continue;
                for (uint32_t j = 0; j < value.texts.count; ++j)
                    if (std::string(value.texts.data[j].data, value.texts.data[j].size) == state.role)
                        state.open = true;
            }
            return EDITOR_OK;
        };
        if (host->query(host->data, EditorString EDITOR_TEXT("workspace.instances"), request, {&state, collect}) !=
            EDITOR_OK)
            return EDITOR_REFUSED;
        const EditorValue value{EDITOR_TEXT("checked"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(state.open)};
        return sink.write ? sink.write(sink.data, &value, 1) : EDITOR_INVALID;
    };
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("scene.attributes-open"), EDITOR_TEXT("builtin.workspace-navigation"),
         nullptr, 0, read, const_cast<char *>("main.attributes"), EDITOR_QUERY_EACH_READ, EDITOR_WORKSPACE, &checked,
         1},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("scene.hierarchy-open"), EDITOR_TEXT("builtin.workspace-navigation"),
         nullptr, 0, read, const_cast<char *>("main.hierarchy"), EDITOR_QUERY_EACH_READ, EDITOR_WORKSPACE, &checked,
         1}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("workspace.defaults");
    module.commands = commands;
    module.commandCount = 2;
    module.locations = &location;
    module.locationCount = 1;
    module.menus = menus;
    module.menuCount = 2;
    module.queries = queries;
    module.queryCount = 2;
    return editor.Register(module, error);
}
} // namespace vdev
