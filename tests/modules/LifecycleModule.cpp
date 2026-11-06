#include "editor/Editor.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {
struct State {
    unsigned starts = 0, prepare = 0, before = 0, after = 0, stops = 0;
    bool documentAtStop = false;
    std::string report;
};
}
extern "C" EDITOR_MODULE_EXPORT EditorStatus EDITOR_CALL NeMoDevModule(const EditorHost *host, EditorModuleDesc *out) {
    if (!host || host->version != EDITOR_VERSION || host->size != sizeof(*host) || !out || out->size != sizeof(*out))
        return EDITOR_INVALID;
    auto *state = new State;
    if (const auto *report = std::getenv("NEMODEV_LIFECYCLE_REPORT")) state->report = report;
    out->id = EDITOR_TEXT("example.lifecycle");
    out->data = state;
    out->start = [](void *data, const EditorHost *) {
        ++static_cast<State *>(data)->starts;
        const auto *mode = std::getenv("NEMODEV_LIFECYCLE_START_MODE");
        if (mode && std::string(mode) == "throw") throw std::runtime_error("Injected external module start failure");
        return mode && std::string(mode) == "refuse" ? EDITOR_ERROR : EDITOR_OK;
    };
    out->update = [](void *data, const EditorHost *, EditorModulePhase phase) {
        auto &state = *static_cast<State *>(data);
        if (phase == EDITOR_PREPARE_FRAME) ++state.prepare;
        else if (phase == EDITOR_BEFORE_VIEWS) ++state.before;
        else if (phase == EDITOR_AFTER_VIEWS) ++state.after;
    };
    out->stop = [](void *data, const EditorHost *host) {
        auto &state = *static_cast<State *>(data);
        ++state.stops;
        state.documentAtStop = host->scope(host->data, 0).document.value != 0;
    };
    out->destroy = [](void *data) {
        auto *state = static_cast<State *>(data);
        if (!state->report.empty()) {
            if (auto *file = std::fopen(state->report.c_str(), "wb")) {
                std::fprintf(file, "%u %u %u %u %u %u\n", state->starts, state->prepare, state->before,
                             state->after, state->stops, unsigned(state->documentAtStop));
                std::fclose(file);
            }
        }
        delete state;
    };
    static const EditorParameter fields[] = {
        {EDITOR_TEXT("starts"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("prepare"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("before"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("after"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("stops"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc query{sizeof(query), EDITOR_TEXT("test.lifecycle"), EDITOR_TEXT("example.lifecycle"),
        nullptr, 0, [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const auto &state = *static_cast<State *>(data);
            const EditorValue values[] = {
                {EDITOR_TEXT("starts"), EDITOR_INTEGER_VALUE, {}, {}, state.starts},
                {EDITOR_TEXT("prepare"), EDITOR_INTEGER_VALUE, {}, {}, state.prepare},
                {EDITOR_TEXT("before"), EDITOR_INTEGER_VALUE, {}, {}, state.before},
                {EDITOR_TEXT("after"), EDITOR_INTEGER_VALUE, {}, {}, state.after},
                {EDITOR_TEXT("stops"), EDITOR_INTEGER_VALUE, {}, {}, state.stops}};
            return sink.write(sink.data, values, 5);
        }, state, EDITOR_QUERY_EACH_READ, EDITOR_WORKSPACE, fields, 5};
    // Entry arrays stay alive until the host has copied them.
    static EditorQueryDesc descriptor;
    descriptor = query;
    out->queries = &descriptor; out->queryCount = 1;
    return EDITOR_OK;
}
