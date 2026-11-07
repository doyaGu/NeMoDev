#include "editor/Editor.h"
#include <cstdint>

namespace {
struct State { uint64_t notices = 0; bool readable = false, refused = false; };
EditorStatus Read(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
    const auto &s = *static_cast<State *>(data);
    const EditorValue values[] = {{EDITOR_TEXT("notices"), EDITOR_INTEGER_VALUE, {}, {}, s.notices},
        {EDITOR_TEXT("readable"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(s.readable)},
        {EDITOR_TEXT("refused"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(s.refused)}};
    return sink.write(sink.data, values, 3);
}
}
extern "C" EDITOR_MODULE_EXPORT EditorStatus EDITOR_CALL NeMoDevModule(const EditorHost *host, EditorModuleDesc *module) {
    if (!host || host->version != EDITOR_VERSION || !module || module->size != sizeof(*module)) return EDITOR_INVALID;
    auto *state = new State;
    *module = {}; module->size = sizeof(*module); module->version = EDITOR_VERSION;
    module->id = EDITOR_TEXT("test.file-observer"); module->data = state;
    module->destroy = [](void *data) { delete static_cast<State *>(data); };
    module->document = [](void *data, const EditorHost *host, const EditorDocumentChange *change) {
        if (change->phase != EDITOR_DOCUMENT_LOADED) return EDITOR_OK;
        auto &s = *static_cast<State *>(data); ++s.notices;
        const EditorRequest request{host->scope(host->data, 0)};
        s.readable = host->query(host->data, EditorString EDITOR_TEXT("document.file-state"), &request,
            {nullptr, [](void *, const EditorValue *, uint32_t count) { return count == 4 ? EDITOR_OK : EDITOR_INVALID; }}) == EDITOR_OK;
        s.refused = host->execute(host->data, EditorString EDITOR_TEXT("document.close"), &request, {}) == EDITOR_REFUSED;
        return EDITOR_OK;
    };
    static EditorQueryDesc query{sizeof(EditorQueryDesc), EDITOR_TEXT("test.file-notices"), EDITOR_TEXT("test.file-observer"),
        nullptr, 0, Read, nullptr, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION};
    static const EditorParameter fields[] = {{EDITOR_TEXT("notices"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("readable"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("refused"), EDITOR_INTEGER_VALUE}};
    query.fields = fields; query.fieldCount = 3;
    query.data = state; module->queries = &query; module->queryCount = 1;
    return EDITOR_OK;
}
