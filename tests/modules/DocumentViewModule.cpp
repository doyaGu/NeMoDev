#include "editor/ImGuiDrawing.h"

namespace {
struct State { uint64_t detached = 0, restored = 0, destroyed = 0; bool guarded = true; } state;
struct View { State &state; uint64_t value; };
}
extern "C" EDITOR_MODULE_EXPORT EditorStatus EDITOR_CALL NeMoDevModule(const EditorHost *host, EditorModuleDesc *out) {
    if (!host || host->version != EDITOR_VERSION || !out || out->size != sizeof(*out)) return EDITOR_INVALID;
    state = {};
    static const EditorViewDesc view = [] {
        EditorViewDesc result{};
        result.size = sizeof(result); result.imguiVersion = EDITOR_IMGUI_VERSION;
        result.id = EDITOR_TEXT("test.document"); result.provider = EDITOR_TEXT("test.external-document");
        result.title = EDITOR_TEXT("External document view"); result.area = EDITOR_DOCUMENT; result.documentBound = 1;
        result.data = &state;
        result.create = [](void *p, const EditorHost *, EditorView id) -> void * { return new View{*static_cast<State *>(p), id + 37}; };
        result.destroy = [](void *p) { auto *v = static_cast<View *>(p); ++v->state.destroyed; delete v; };
        result.draw = [](void *, const EditorHost *, EditorView, EditorViewFrame *) {};
        static const EditorParameter value{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE};
        result.fields = &value; result.fieldCount = 1;
        result.inspect = [](void *p, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const EditorValue value{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE, {}, {}, static_cast<View *>(p)->value};
            return sink.write(sink.data, &value, 1);
        };
        return result;
    }();
    static const EditorParameter fields[] = {{EDITOR_TEXT("detached"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("restored"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("destroyed"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("guarded"), EDITOR_INTEGER_VALUE}};
    static const EditorQueryDesc query{sizeof(query), EDITOR_TEXT("test.external-document-counts"), EDITOR_TEXT("test.external-document"),
        nullptr, 0, [](void *, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const EditorValue values[] = {{EDITOR_TEXT("detached"), EDITOR_INTEGER_VALUE, {}, {}, state.detached},
                {EDITOR_TEXT("restored"), EDITOR_INTEGER_VALUE, {}, {}, state.restored},
                {EDITOR_TEXT("destroyed"), EDITOR_INTEGER_VALUE, {}, {}, state.destroyed},
                {EDITOR_TEXT("guarded"), EDITOR_INTEGER_VALUE, {}, {}, state.guarded}};
            return sink.write(sink.data, values, 4);
        }, nullptr, EDITOR_QUERY_EACH_READ, EDITOR_WORKSPACE, fields, 4};
    out->version = EDITOR_VERSION; out->id = EDITOR_TEXT("test.external-document");
    out->data = &state; out->views = &view; out->viewCount = 1; out->queries = &query; out->queryCount = 1;
    out->document = [](void *p, const EditorHost *host, const EditorDocumentChange *change) {
        auto &state = *static_cast<State *>(p);
        if (change->phase == EDITOR_DOCUMENT_DETACH) ++state.detached;
        if (change->phase == EDITOR_DOCUMENT_RESTORE) ++state.restored;
        const EditorValue type{EDITOR_TEXT("type"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("test.document")};
        const EditorRequest request{host->scope(host->data, 0), &type, 1};
        state.guarded &= host->execute(host->data, EditorString EDITOR_TEXT("workspace.open-view"), &request, {}) == EDITOR_REFUSED;
        return EDITOR_OK;
    };
    return EDITOR_OK;
}
