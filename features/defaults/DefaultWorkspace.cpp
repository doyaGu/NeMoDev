#include "features/defaults/DefaultWorkspace.h"
#include "features/setup/SetupCommands.h"
#include "features/schematic/SchematicCommands.h"
#include "features/diagnostics/DiagnosticCommands.h"
#include "features/preferences/PreferencesCommands.h"
#include "features/layout/LayoutCommands.h"
#include "features/layout/LayoutPresentation.h"
#include "features/layout/LayoutAuthoring.h"
#include "features/layout/SnapshotCommands.h"
#include "features/schematic/SchematicGraphCommands.h"

namespace vdev {
WorkspaceConfiguration DefaultWorkspaceConfiguration() {
    WorkspaceConfiguration result;
    result.contributions.providers = {{"editor", "text.single-line", "builtin.text"},
                                      {"editor", "value.boolean", "builtin.values"},
                                      {"editor", "value.integer", "builtin.values"},
                                      {"editor", "value.real", "builtin.values"},
                                      {"editor", "value.enumeration", "builtin.values"},
                                      {"editor", "value.flags", "builtin.values"},
                                      {"editor", "value.time", "builtin.values"},
                                      {"command", "parameter.time-set", "builtin.parameters"},
                                      {"query", "parameter.time-value", "builtin.parameters"},
                                      {"editor", "value.key", "builtin.values"},
                                      {"command", "parameter.key-set", "builtin.parameters"},
                                      {"query", "parameter.key-value", "builtin.parameters"},
                                      {"editor", "value.script", "builtin.values"},
                                      {"command", "parameter.script-set", "builtin.parameters"},
                                      {"query", "parameter.script-value", "builtin.parameters"},
                                      {"editor", "value.animation", "builtin.values"},
                                      {"command", "parameter.animation-set", "builtin.parameters"},
                                      {"query", "parameter.animation-value", "builtin.parameters"},
                                      {"editor", "value.message", "builtin.values"},
                                      {"command", "parameter.message-set", "builtin.parameters"},
                                      {"query", "parameter.message-value", "builtin.parameters"},
                                      {"editor", "value.attribute", "builtin.values"},
                                      {"command", "parameter.attribute-set", "builtin.parameters"},
                                      {"query", "parameter.attribute-value", "builtin.parameters"},
                                      {"editor", "value.synchro", "builtin.values"},
                                      {"command", "parameter.synchro-set", "builtin.parameters"},
                                      {"query", "parameter.synchro-value", "builtin.parameters"},
                                      {"query", "parameter.selection-choices", "builtin.parameters"},
                                      {"editor", "value.curve2d", "builtin.values"},
                                      {"editor", "value.dependencies", "builtin.values"},
                                      {"editor", "value.lod-options", "builtin.values"},
                                      {"command", "parameter.curve2d-set", "builtin.parameters"},
                                      {"query", "parameter.curve2d-value", "builtin.parameters"},
                                      {"command", "parameter.dependencies-set", "builtin.parameters"},
                                      {"query", "parameter.dependencies-value", "builtin.parameters"},
                                      {"command", "parameter.lod-options-set", "builtin.parameters"},
                                      {"query", "parameter.lod-options-value", "builtin.parameters"},
                                      {"command", "parameter.flags-set", "builtin.parameters"},
                                      {"query", "parameter.flags-value", "builtin.parameters"},
                                      {"editor", "value.object", "builtin.values"},
                                      {"editor", "value.structure", "builtin.values"},
                                      {"editor", "value.object-array", "builtin.values"},
                                      {"editor", "value.string", "builtin.values"},
                                      {"editor", "value.angle", "builtin.values"},
                                      {"editor", "value.vector2", "builtin.values"},
                                      {"editor", "value.vector3", "builtin.values"},
                                      {"editor", "value.quaternion", "builtin.values"},
                                      {"editor", "value.euler", "builtin.values"},
                                      {"editor", "value.matrix", "builtin.values"},
                                      {"editor", "value.color", "builtin.values"},
                                      {"editor", "value.box", "builtin.values"},
                                      {"editor", "value.rect", "builtin.values"},
                                      {"command", "parameter.copy-value", "builtin.parameters"},
                                      {"command", "parameter.structure-set", "builtin.parameters"},
                                      {"query", "parameter.structure-value", "builtin.parameters"},
                                      {"command", "parameter.integer-set", "builtin.parameters"},
                                      {"command", "parameter.vector3-set", "builtin.parameters"},
                                      {"query", "parameter.vector3-value", "builtin.parameters"},
                                      {"command", "bitmap.edit-options", "builtin.parameters"},
                                      {"command", "object.open-setup", "builtin.setup-windows"},
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
                                      {"command", "script.navigate", "builtin.schematic"},
                                      {"view", "script.schematic", "builtin.schematic"},
                                      {"view", "resource.data", "builtin.data-resources"},
                                      {"view", "resource.building-blocks", "builtin.building-blocks"},
                                      {"command", "runtime.play", "builtin.execution"},
                                      {"view", "runtime.debugger", "builtin.runtime-views"},
                                      {"view", "runtime.profiler", "builtin.runtime-views"},
                                      {"view", "runtime.parameters", "builtin.runtime-views"},
                                      {"view", "environment.plugins", "builtin.environment-views"},
                                      {"view", "environment.paths", "builtin.environment-views"},
                                      {"command", "paths.choose-folder", "builtin.environment"},
                                      {"command", "paths.apply", "builtin.environment"},
                                      {"view", "environment.link-options", "builtin.environment-views"},
                                      {"query", "environment.link-options", "builtin.environment"},
                                      {"command", "environment.set-link-options", "builtin.environment"},
                                      {"view", "diagnostics.unused", "builtin.diagnostic-views"},
                                      {"view", "diagnostics.versions", "builtin.diagnostic-views"},
                                      {"view", "diagnostics.memory", "builtin.diagnostic-views"},
                                      {"view", "diagnostics.about", "builtin.diagnostic-views"}};
    for (const auto *id : {"array.row-insert", "array.rows-remove", "array.rows-duplicate", "array.row-move",
                           "array.column-set", "array.column-insert", "array.column-remove", "array.column-move",
                           "array.sort", "array.key-set", "array.cells-set", "array.copy", "array.paste",
                           "array.paste-shortcuts", "array.remove-shortcuts"})
        result.contributions.providers.push_back({"command", id, "builtin.arrays"});
    for (const auto *id : {"array.table", "array.cells-value", "array.column-value", "array.column-new-value", "array.paste-options"})
        result.contributions.providers.push_back({"query", id, "builtin.arrays"});
    result.contributions.providers.push_back({"editor", "value.array-column", "builtin.array-values"});
    const auto &setup = SetupCommands::DefaultProviders();
    result.contributions.providers.insert(result.contributions.providers.end(), setup.begin(), setup.end());
    const auto &schematic = SchematicCommands::DefaultProviders();
    result.contributions.providers.insert(result.contributions.providers.end(), schematic.begin(), schematic.end());
    const auto &diagnostics = DiagnosticCommands::DefaultProviders();
    result.contributions.providers.insert(result.contributions.providers.end(), diagnostics.begin(), diagnostics.end());
    const auto &preferences = PreferencesCommands::DefaultProviders();
    result.contributions.providers.insert(result.contributions.providers.end(), preferences.begin(), preferences.end());
    for (const auto *id : {"object.attributes", "attribute.clipboard"})
        result.contributions.providers.push_back({"query", id, "builtin.scene"});
    for (const auto *id : {"attribute.copy-values", "attribute.paste-attributes", "attribute.edit-value"})
        result.contributions.providers.push_back({"command", id, "builtin.scene"});
    result.contributions.providers.push_back({"query", "scene.clipboard", "builtin.scene"});
    result.contributions.providers.push_back({"command", "scene.clear-clipboard", "builtin.scene"});
    result.contributions.providers.push_back({"command", "scene.duplicate-clipboard", "builtin.scene"});
    const auto &layout = LayoutCommands::DefaultProviders();
    result.contributions.providers.insert(result.contributions.providers.end(), layout.begin(), layout.end());
    const auto &presentation = LayoutPresentation::DefaultProviders();
    result.contributions.providers.insert(result.contributions.providers.end(), presentation.begin(),
                                          presentation.end());
    const auto &authoring = LayoutAuthoring::DefaultProviders();
    result.contributions.providers.insert(result.contributions.providers.end(), authoring.begin(), authoring.end());
    const auto &snapshots = SnapshotCommands::DefaultProviders();
    result.contributions.providers.insert(result.contributions.providers.end(), snapshots.begin(), snapshots.end());
    const auto &graph = SchematicGraphCommands::DefaultProviders();
    result.contributions.providers.insert(result.contributions.providers.end(), graph.begin(), graph.end());
    result.contributions.providers.push_back({"view", "schematic.invalid-links", "builtin.schematic-diagnostics"});
    result.contributions.providers.push_back({"view", "schematic.messages", "builtin.schematic-diagnostics"});
    result.views = {{"document.file-dialogs", "", "", true},
                    {"document.output-dialogs", "", "", true},
                    {"environment.folder-dialogs", "", "", true},
                    {"scene.level", "main.level", "editing", true, true},
                    {"script.schematic", "main.schematic", "editing", true, true},
                    {"resource.building-blocks", "main.building-blocks", "resources", true, true},
                    {"resource.file-dialogs", "", "", true},
                    {"snapshot.file-dialogs", "", "", true},
                    {"scene.hierarchy", "main.hierarchy", "resources"},
                    {"scene.attributes", "main.attributes", "editing"},
                    {"resource.data", "", "resources", false, false, true},
                    {"runtime.debugger", "main.debugger", ""},
                    {"runtime.profiler", "main.profiler", ""},
                    {"runtime.parameters", "main.parameters", "resources"},
                    {"environment.plugins", "main.plugins", ""},
                    {"environment.paths", "main.paths", ""},
                    {"environment.link-options", "main.link-options", ""},
                    {"preferences.general", "main.preferences", ""},
                    {"schematic.invalid-links", "main.invalid-links", ""},
                    {"schematic.messages", "main.messages", ""},
                    {"diagnostics.unused", "main.unused", ""},
                    {"diagnostics.versions", "main.versions", ""},
                    {"diagnostics.memory", "main.memory", ""},
                    {"diagnostics.about", "main.about", ""}};
    result.splits = {{"root", "top", "editing", WorkspaceDirection::Up, .5f},
                     {"top", "resources", "layout", WorkspaceDirection::Right, .5f}};
    result.regions = {
        {"layout", "", "3D Layout"}, {"editing", "main.schematic", ""}, {"resources", "main.building-blocks", ""}};
    result.legacyWindows = {{"3D Layout", "layout", false, true},
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
