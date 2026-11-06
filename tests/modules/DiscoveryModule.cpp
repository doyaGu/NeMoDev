#include "editor/Editor.h"

#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {
void Record(const std::string &text) {
    if (const auto *path = std::getenv("NEMODEV_MODULE_LOG"))
        std::ofstream(std::filesystem::u8path(path), std::ios::app) << text << '\n';
}
struct Unload { ~Unload() { Record("unload " MODULE_ID); } } unload;
struct Data { bool throwingStop = false; };
void Stop(void *data) {
    auto *owned = static_cast<Data *>(data);
    const auto raised = owned->throwingStop;
    Record("stop " MODULE_ID);
    delete owned;
    if (raised) throw std::runtime_error("injected module stop exception");
}
}

extern "C" EDITOR_MODULE_EXPORT EditorStatus EDITOR_CALL NeMoDevModule(const EditorHost *host, EditorModuleDesc *out) {
    if (!host || host->size != sizeof(EditorHost) || host->version != EDITOR_VERSION ||
        !out || out->size != sizeof(EditorModuleDesc) || out->version != EDITOR_VERSION) return EDITOR_INVALID;
    Record("entry " MODULE_ID);
    std::string mode;
    if (std::string(MODULE_ID) == "fixture.addon")
        if (const auto *value = std::getenv("NEMODEV_MODULE_FAILURE")) mode = value;
    out->data = new Data{mode == "stop-throw"};
    out->destroy = Stop;
    out->id = EDITOR_TEXT(MODULE_ID);
#ifdef MODULE_DEPENDENCY
    const EditorString dependency = mode == "missing" ? EditorString EDITOR_TEXT("fixture.missing") :
        mode == "cycle" ? EditorString EDITOR_TEXT(MODULE_ID) : EditorString EDITOR_TEXT(MODULE_DEPENDENCY);
    // Arrays must survive the entry's return until the host copies them.
    static EditorString dependencies[1]; dependencies[0] = dependency;
    out->dependencies = dependencies; out->dependencyCount = 1;
#endif
    static const EditorParameter field{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE};
    static const EditorQueryDesc query{sizeof(EditorQueryDesc), EDITOR_TEXT(MODULE_ID ".value"), EDITOR_TEXT(MODULE_ID),
        nullptr, 0, [](void *, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const EditorValue value{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE, {}, {}, 42};
            return sink.write(sink.data, &value, 1);
        }, nullptr, EDITOR_QUERY_EACH_READ, EDITOR_WORKSPACE, &field, 1};
    out->queries = &query; out->queryCount = 1;
    if (mode == "entry-error") return EDITOR_ERROR;
    if (mode == "entry-throw") throw std::runtime_error("injected module entry exception");
    if (mode == "bad-version") ++out->version;
    if (mode == "duplicate-id") out->id = EDITOR_TEXT("fixture.embedded");
    return EDITOR_OK;
}
