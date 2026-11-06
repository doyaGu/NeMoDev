#include "editor/Editor.h"
#include <cstring>
#include <map>
#include <stdexcept>

namespace {
bool Is(EditorString value, const char *text) {
    return value.size == std::strlen(text) && std::memcmp(value.data, text, value.size) == 0;
}
struct Live {
    EditorEdit edit = 0;
    double height = 0, start = 0;
};
struct Module {
    std::map<EditorView, Live> views;
    unsigned failure = 0;
};
struct Geometry {
    uint64_t revision = 0, topology = 0;
    double height = 0;
};
EditorStatus ReadGeometry(void *data, const EditorValue *values, uint32_t count) {
    auto &geometry = *static_cast<Geometry *>(data);
    for (uint32_t i = 0; i < count; ++i) {
        const auto &v = values[i];
        if (Is(v.key, "revision")) geometry.revision = v.integer;
        if (Is(v.key, "topology")) geometry.topology = v.integer;
        if (Is(v.key, "value"))
            for (uint32_t j = 0; j < v.fields.count; ++j)
                if (Is(v.fields.data[j].key, "size") && v.fields.data[j].reals.count == 2)
                    geometry.height = v.fields.data[j].reals.data[1];
    }
    return EDITOR_OK;
}
EditorStatus Input(void *data, const EditorHost *host, const EditorRequest *request, const EditorToolInput *input,
                   uint32_t *flags) {
    auto &module = *static_cast<Module *>(data);
    auto &live = module.views[request->scope.view];
    *flags = 0;
    if (input->phase == EDITOR_TOOL_ENTER && module.failure == 3)
        throw std::runtime_error("injected tool entry failure");
    if (input->phase == EDITOR_TOOL_CANCEL || input->phase == EDITOR_TOOL_LEAVE) {
        // Deliberately leave the preview for the host to cancel. A module must
        // not leak document state even when its own cancellation is incomplete.
        live = {};
        return EDITOR_OK;
    }
    if (input->phase != EDITOR_TOOL_FRAME) return EDITOR_OK;
    if (input->pressed & 1) {
        const EditorValue args[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, host->selection(host->data)},
                                    {EDITOR_TEXT("mode"), EDITOR_INTEGER_VALUE, {}, {}, 1}};
        Geometry geometry;
        EditorRequest query{request->scope, args, 2};
        auto status = host->query(host->data, EDITOR_TEXT("schematic.geometry"), &query, {&geometry, ReadGeometry});
        if (status != EDITOR_OK) return status;
        double size[] = {0, geometry.height};
        EditorValue dimensions{EDITOR_TEXT("size"), EDITOR_REAL_LIST};
        dimensions.reals = {size, 2};
        EditorValue fields[] = {args[0],
                                args[1],
                                {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, geometry.revision},
                                {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE, {}, {}, geometry.topology},
                                {EDITOR_TEXT("value"), EDITOR_RECORD}};
        fields[4].fields = {&dimensions, 1};
        EditorRequest begin{request->scope, fields, 5};
        if (module.failure == 4) begin.scope = host->scope(host->data, 0);
        status = host->beginCommand(host->data, EDITOR_TEXT("schematic.edit-geometry"), &begin, &live.edit);
        if (status != EDITOR_OK) return status;
        live.height = geometry.height;
        live.start = input->y;
    }
    if (live.edit) {
        double size[] = {0, live.height + input->y - live.start};
        EditorValue dimensions{EDITOR_TEXT("size"), EDITOR_REAL_LIST};
        dimensions.reals = {size, 2};
        EditorValue value{EDITOR_TEXT("value"), EDITOR_RECORD};
        value.fields = {&dimensions, 1};
        auto status = host->editValue(host->data, live.edit, &value);
        if (status != EDITOR_OK) return status;
        if (module.failure == 1) throw std::runtime_error("injected tool update failure");
        if (module.failure == 2) {
            *flags = 0x80000000u;
            return EDITOR_OK;
        }
        if (input->released & 1) {
            status = host->finishEdit(host->data, live.edit, 1, nullptr);
            live.edit = 0;
            return status == EDITOR_QUEUED ? EDITOR_OK : status;
        }
        *flags = EDITOR_TOOL_CAPTURED;
    }
    return EDITOR_OK;
}
} // namespace
extern "C" EDITOR_MODULE_EXPORT EditorStatus EDITOR_CALL NeMoDevModule(const EditorHost *host, EditorModuleDesc *out) {
    if (!host || host->size != sizeof(*host) || host->version != EDITOR_VERSION || !out || out->size != sizeof(*out))
        return EDITOR_INVALID;
    auto *module = new Module;
    EditorToolDesc tools[2]{};
    tools[0].size = sizeof(EditorToolDesc);
    tools[0].id = EDITOR_TEXT("test.resize");
    tools[0].provider = EDITOR_TEXT("external.tools");
    tools[0].title = EDITOR_TEXT("Resize Script");
    tools[0].view = EDITOR_TEXT("test.viewport");
    tools[0].input = Input;
    tools[0].data = module;
    tools[1] = tools[0];
    tools[1].id = EDITOR_TEXT("test.alternate");
    const EditorParameter mode{EDITOR_TEXT("mode"), EDITOR_INTEGER_VALUE};
    const EditorCommandDesc command{sizeof(EditorCommandDesc),
                                    EDITOR_TEXT("test.tool-failure"),
                                    EDITOR_TEXT("external.tools"),
                                    EDITOR_TEXT("Inject Tool Failure"),
                                    &mode,
                                    1,
                                    [](void *data, const EditorHost *, const EditorRequest *r, EditorSink) {
                                        static_cast<Module *>(data)->failure = unsigned(r->values[0].integer);
                                        return EDITOR_OK;
                                    },
                                    module,
                                    EDITOR_WORKSPACE,
                                    EDITOR_SESSION_CHANGE};
    const EditorToolbarItemDesc button{sizeof(EditorToolbarItemDesc),
                                       {},
                                       EDITOR_TEXT("test.button"),
                                       EDITOR_TEXT("external.toolbar"),
                                       EDITOR_TEXT("test.tools"),
                                       EDITOR_TEXT("Resize"),
                                       EDITOR_TEXT("test.resize"),
                                       {},
                                       nullptr,
                                       0,
                                       5,
                                       1,
                                       55,
                                       19};
    const EditorBindingDesc binding{EDITOR_TEXT("test.resize"), EDITOR_TEXT("R"), EDITOR_TEXT("test.viewport")};
    out->version = EDITOR_VERSION;
    out->id = EDITOR_TEXT("external.tools");
    out->tools = tools;
    out->toolCount = 2;
    out->commands = &command;
    out->commandCount = 1;
    out->toolbarItems = &button;
    out->toolbarItemCount = 1;
    out->bindings = &binding;
    out->bindingCount = 1;
    // Register copies all descriptors while the entry's stack is still live.
    // The public entry instead must keep its arrays alive until it returns to
    // the host, so store this metadata with the module below.
    struct Owned {
        Module *module;
        EditorToolDesc tools[2];
        EditorCommandDesc command;
        EditorParameter mode;
        EditorToolbarItemDesc button;
        EditorBindingDesc binding;
    };
    auto *owned = new Owned{module, {tools[0], tools[1]}, command, mode, button, binding};
    owned->command.parameters = &owned->mode;
    out->tools = owned->tools;
    out->commands = &owned->command;
    out->toolbarItems = &owned->button;
    out->bindings = &owned->binding;
    out->data = owned;
    out->destroy = [](void *data) {
        auto *owned = static_cast<Owned *>(data);
        delete owned->module;
        delete owned;
    };
    return EDITOR_OK;
}
