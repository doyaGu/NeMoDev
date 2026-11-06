#include "editor/ImGuiDrawing.h"

#include <stdexcept>

namespace {
struct State {
    uint64_t mode = 0, prepared = 0, closing = 0, destroyed = 0;
    bool scopeAlive = true, mutationRefused = false;
} state;
}

extern "C" EDITOR_MODULE_EXPORT EditorStatus EDITOR_CALL NeMoDevModule(const EditorHost *host, EditorModuleDesc *out) {
    if (!host || host->size != sizeof(EditorHost) || host->version != EDITOR_VERSION || !out || out->size != sizeof(*out))
        return EDITOR_INVALID;
    state = {};
    static const EditorViewDesc view = [] {
        EditorViewDesc desc{};
        desc.size = sizeof(desc); desc.imguiVersion = EDITOR_IMGUI_VERSION;
        desc.id = EDITOR_TEXT("test.close"); desc.provider = EDITOR_TEXT("test.external");
        desc.title = EDITOR_TEXT("External close test"); desc.area = EDITOR_WORKSPACE;
        desc.singleInstance = 1; desc.data = &state;
        desc.create = [](void *data, const EditorHost *, EditorView) { return data; };
        desc.canClose = [](void *data, const EditorHost *host, EditorView id) {
            auto &state = *static_cast<State *>(data);
            ++state.prepared;
            state.scopeAlive = state.scopeAlive && host->scope(host->data, id).view == id;
            const EditorValue type{EDITOR_TEXT("type"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("test.close")};
            const EditorRequest request{host->scope(host->data, id), &type, 1};
            state.mutationRefused = host->execute(host->data, EditorString EDITOR_TEXT("workspace.open-view"), &request, {}) == EDITOR_REFUSED;
            if (state.mode == 2) throw std::runtime_error("Injected external close exception");
            return state.mode == 1 ? EDITOR_OK : EDITOR_REFUSED;
        };
        desc.closing = [](void *data, const EditorHost *host, EditorView id) {
            auto &state = *static_cast<State *>(data);
            ++state.closing;
            state.scopeAlive = state.scopeAlive && host->scope(host->data, id).view == id;
        };
        desc.destroy = [](void *data) { ++static_cast<State *>(data)->destroyed; };
        desc.draw = [](void *, const EditorHost *, EditorView, EditorViewFrame *) {};
        return desc;
    }();
    static const EditorParameter mode{EDITOR_TEXT("mode"), EDITOR_INTEGER_VALUE};
    static const EditorCommandDesc command{sizeof(command), EDITOR_TEXT("test.close-mode"), EDITOR_TEXT("test.external"),
        EDITOR_TEXT("Set close result"), &mode, 1,
        [](void *, const EditorHost *, const EditorRequest *request, EditorSink) {
            state.mode = request->values[0].integer;
            return EDITOR_OK;
        }, nullptr, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE};
    static const EditorParameter fields[] = {
        {EDITOR_TEXT("prepared"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("closing"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("destroyed"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("scopeAlive"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("mutationRefused"), EDITOR_INTEGER_VALUE}};
    static const EditorQueryDesc query{sizeof(query), EDITOR_TEXT("test.close-counts"), EDITOR_TEXT("test.external"),
        nullptr, 0, [](void *, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const EditorValue values[] = {
                {EDITOR_TEXT("prepared"), EDITOR_INTEGER_VALUE, {}, {}, state.prepared},
                {EDITOR_TEXT("closing"), EDITOR_INTEGER_VALUE, {}, {}, state.closing},
                {EDITOR_TEXT("destroyed"), EDITOR_INTEGER_VALUE, {}, {}, state.destroyed},
                {EDITOR_TEXT("scopeAlive"), EDITOR_INTEGER_VALUE, {}, {}, state.scopeAlive},
                {EDITOR_TEXT("mutationRefused"), EDITOR_INTEGER_VALUE, {}, {}, state.mutationRefused}};
            return sink.write(sink.data, values, 5);
        }, nullptr, EDITOR_QUERY_EACH_READ, EDITOR_WORKSPACE, fields, 5};
    out->version = EDITOR_VERSION; out->id = EDITOR_TEXT("test.external-close");
    out->views = &view; out->viewCount = 1;
    out->commands = &command; out->commandCount = 1;
    out->queries = &query; out->queryCount = 1;
    return EDITOR_OK;
}
