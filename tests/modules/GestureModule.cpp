#include "editor/Editor.h"
#include <stdexcept>

namespace {
struct Module {
    unsigned failure = 0;
    uint64_t updates = 0, cancels = 0, releases = 0, commits = 0;
    struct Command {
        Module *module;
        EditorString id, provider;
    } entries[2];
    EditorCommandDesc commands[3]{};
};
EditorStatus Preview(void *data, const EditorHost *host, const EditorRequest *request, EditorPreviewAction action,
                     void **state) {
    auto &entry = *static_cast<Module::Command *>(data);
    auto &m = *entry.module;
    if (action == EDITOR_PREVIEW_UPDATE)
        ++m.updates;
    if (action == EDITOR_PREVIEW_CANCEL)
        ++m.cancels;
    if (action == EDITOR_PREVIEW_RELEASE)
        ++m.releases;
    auto next = *request;
    next.handler = entry.provider;
    const auto result = host->previewCommand(host->data, entry.id, &next, action, state, {});
    if (action == EDITOR_PREVIEW_RELEASE && m.failure == 4)
        throw std::runtime_error("injected release failure");
    if (action == EDITOR_PREVIEW_UPDATE && m.failure == 1)
        throw std::runtime_error("injected preview failure");
    return result;
}
EditorStatus Commit(void *data, const EditorHost *host, const EditorRequest *request, void *state, EditorSink sink) {
    auto &entry = *static_cast<Module::Command *>(data);
    auto &m = *entry.module;
    ++m.commits;
    if (m.failure == 2)
        return EDITOR_REFUSED;
    auto next = *request;
    next.handler = entry.provider;
    const auto result = host->previewCommand(host->data, entry.id, &next, EDITOR_PREVIEW_COMMIT, &state, sink);
    if (m.failure == 3)
        throw std::runtime_error("injected commit failure");
    return result;
}
EditorStatus Run(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    auto &entry = *static_cast<Module::Command *>(data);
    auto next = *request;
    next.handler = entry.provider;
    return host->execute(host->data, entry.id, &next, sink);
}
EditorStatus Failure(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
    if (request->values[0].integer > 4)
        return EDITOR_INVALID;
    static_cast<Module *>(data)->failure = unsigned(request->values[0].integer);
    return EDITOR_OK;
}
EditorStatus Read(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
    const auto &m = *static_cast<Module *>(data);
    const EditorValue fields[] = {{EDITOR_TEXT("updates"), EDITOR_INTEGER_VALUE, {}, {}, m.updates},
                                  {EDITOR_TEXT("cancels"), EDITOR_INTEGER_VALUE, {}, {}, m.cancels},
                                  {EDITOR_TEXT("releases"), EDITOR_INTEGER_VALUE, {}, {}, m.releases},
                                  {EDITOR_TEXT("commits"), EDITOR_INTEGER_VALUE, {}, {}, m.commits}};
    return sink.write(sink.data, fields, 4);
}
} // namespace
extern "C" EDITOR_MODULE_EXPORT EditorStatus EDITOR_CALL NeMoDevModule(const EditorHost *host, EditorModuleDesc *out) {
    if (!host || host->size != sizeof(*host) || host->version != EDITOR_VERSION || !out || out->size != sizeof(*out))
        return EDITOR_INVALID;
    static const EditorParameter geometry[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT},
                                               {EDITOR_TEXT("mode"), EDITOR_INTEGER_VALUE},
                                               {EDITOR_TEXT("elements"), EDITOR_RECORD, 1},
                                               {EDITOR_TEXT("primary"), EDITOR_RECORD, 1},
                                               {EDITOR_TEXT("coherence"), EDITOR_INTEGER_VALUE, 1},
                                               {EDITOR_TEXT("auto-resize"), EDITOR_INTEGER_VALUE, 1},
                                               {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
                                               {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE},
                                               {EDITOR_TEXT("value"), EDITOR_RECORD}};
    static const EditorParameter layout[] = {
        {EDITOR_TEXT("target"), EDITOR_OBJECT},        {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("action"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("grabbed"), EDITOR_OBJECT, 1, 1}, {EDITOR_TEXT("referent"), EDITOR_OBJECT, 1, 1},
        {EDITOR_TEXT("curve"), EDITOR_OBJECT, 1, 1},   {EDITOR_TEXT("start"), EDITOR_REAL_LIST},
        {EDITOR_TEXT("point"), EDITOR_REAL_LIST, 1},   {EDITOR_TEXT("options"), EDITOR_RECORD},
        {EDITOR_TEXT("value"), EDITOR_RECORD},         {EDITOR_TEXT("started"), EDITOR_REAL_VALUE, 1}};
    static const EditorParameter failure{EDITOR_TEXT("mode"), EDITOR_INTEGER_VALUE};
    static const EditorParameter fields[] = {{EDITOR_TEXT("updates"), EDITOR_INTEGER_VALUE},
                                             {EDITOR_TEXT("cancels"), EDITOR_INTEGER_VALUE},
                                             {EDITOR_TEXT("releases"), EDITOR_INTEGER_VALUE},
                                             {EDITOR_TEXT("commits"), EDITOR_INTEGER_VALUE}};
    auto *m = new Module;
    m->entries[0] = {m, EDITOR_TEXT("schematic.edit-geometry"), EDITOR_TEXT("builtin.schematic-geometry")};
    m->entries[1] = {m, EDITOR_TEXT("layout.edit-gesture"), EDITOR_TEXT("builtin.layout-gestures")};
    m->commands[0] = {sizeof(EditorCommandDesc),
                      m->entries[0].id,
                      EDITOR_TEXT("example.gestures"),
                      EDITOR_TEXT("External Schematic Move"),
                      geometry,
                      9,
                      Run,
                      &m->entries[0],
                      EDITOR_DOCUMENT,
                      EDITOR_DOCUMENT_EDIT,
                      nullptr,
                      Preview,
                      Commit};
    m->commands[1] = {sizeof(EditorCommandDesc),
                      m->entries[1].id,
                      EDITOR_TEXT("example.gestures"),
                      EDITOR_TEXT("External Layout Move"),
                      layout,
                      12,
                      Run,
                      &m->entries[1],
                      EDITOR_DOCUMENT,
                      EDITOR_DOCUMENT_EDIT,
                      nullptr,
                      Preview,
                      Commit};
    m->commands[2] = {sizeof(EditorCommandDesc),
                      EDITOR_TEXT("example.gesture-failure"),
                      EDITOR_TEXT("example.gestures"),
                      EDITOR_TEXT("Inject Gesture Failure"),
                      &failure,
                      1,
                      Failure,
                      m,
                      EDITOR_DOCUMENT,
                      EDITOR_SESSION_CHANGE};
    static EditorQueryDesc query;
    query = {sizeof(query),
             EDITOR_TEXT("example.gesture-state"),
             EDITOR_TEXT("example.gestures"),
             nullptr,
             0,
             Read,
             m,
             EDITOR_QUERY_EACH_READ,
             EDITOR_DOCUMENT,
             fields,
             4};
    out->version = EDITOR_VERSION;
    out->id = EDITOR_TEXT("example.gestures");
    out->commands = m->commands;
    out->commandCount = 3;
    out->queries = &query;
    out->queryCount = 1;
    out->data = m;
    out->stop = [](void *data) { delete static_cast<Module *>(data); };
    return EDITOR_OK;
}
