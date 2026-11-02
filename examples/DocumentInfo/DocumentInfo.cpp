#include "editor/ImGuiDrawing.h"

#include <string>

namespace {
struct Info {
    std::string path;
    bool dirty = false;
};
EditorStatus Copy(void *data, const EditorValue *fields, uint32_t count) {
    auto &info = *static_cast<Info *>(data);
    for (uint32_t i = 0; i < count; ++i) {
        const std::string key(fields[i].key.data, fields[i].key.size);
        if (key == "path") info.path.assign(fields[i].text.data ? fields[i].text.data : "", fields[i].text.size);
        if (key == "dirty") info.dirty = fields[i].integer != 0;
    }
    return EDITOR_OK;
}
EditorStatus Open(void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    const EditorRequest navigation{request->scope, nullptr, 0};
    return host->navigateView(host->data, EDITOR_TEXT("example.document-info"), 0, &navigation, sink);
}
}

extern "C" EDITOR_MODULE_EXPORT EditorStatus EDITOR_CALL NeMoDevModule(const EditorHost *host, EditorModuleDesc *out) {
    if (!host || host->size != sizeof(EditorHost) || host->version != EDITOR_VERSION ||
        !out || out->size != sizeof(EditorModuleDesc) || out->version != EDITOR_VERSION) return EDITOR_INVALID;
    static const EditorString dependencies[] = {EDITOR_TEXT("documents.operations"), EDITOR_TEXT("workspace.defaults")};
    static const EditorCommandDesc command{sizeof(EditorCommandDesc), EDITOR_TEXT("example.show-document-info"),
        EDITOR_TEXT("example.document-info"), EDITOR_TEXT("Document Info"), nullptr, 0, Open, nullptr,
        EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE};
    static const EditorViewDesc view = [] {
        EditorViewDesc value{}; value.size = sizeof(value); value.imguiVersion = EDITOR_IMGUI_VERSION;
        value.id = EDITOR_TEXT("example.document-info"); value.provider = EDITOR_TEXT("example.document-info");
        value.title = EDITOR_TEXT("Document Info"); value.kind = EDITOR_WINDOW_VIEW; value.singleInstance = 1;
        value.area = EDITOR_WORKSPACE;
        value.create = [](void *, const EditorHost *, EditorView) -> void * { return new Info; };
        value.destroy = [](void *data) { delete static_cast<Info *>(data); };
        value.draw = [](void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) {
            EditorImGuiWindow window(*frame);
            if (!window) return;
            auto &info = *static_cast<Info *>(data);
            const EditorRequest request{host->scope(host->data, id), nullptr, 0};
            if (host->query(host->data, EDITOR_TEXT("document.info"), &request, {&info, Copy}) != EDITOR_OK) {
                ImGui::TextUnformatted("Document unavailable"); return;
            }
            ImGui::TextUnformatted(info.path.empty() ? "Untitled" : info.path.c_str());
            ImGui::TextUnformatted(info.dirty ? "Modified" : "Saved");
        };
        return value;
    }();
    static const EditorMenuDesc menu{EDITOR_TEXT("example.document-info-menu"), EDITOR_TEXT("example.show-document-info"),
        EDITOR_TEXT("editors.managers"), EDITOR_TEXT("Document Info"), 200, {}};
    out->id = EDITOR_TEXT("example.document-info");
    out->dependencies = dependencies; out->dependencyCount = 2;
    out->commands = &command; out->commandCount = 1;
    out->views = &view; out->viewCount = 1;
    out->menus = &menu; out->menuCount = 1;
    return EDITOR_OK;
}
