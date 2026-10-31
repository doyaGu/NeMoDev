#include "ui/app/EnvironmentInputs.h"
#include "workspace/EditorClient.h"
#include "editor/ImGuiDrawing.h"
#include "ui/controls/FileDialogs.h"

namespace vdev {
namespace {
struct FolderDialogs {
    EditorClient client;
    SDL_Window *window;
    FileDialogs dialogs;
    uint64_t shown = 0;
    FolderDialogs(const EditorHost *host, EditorView view, SDL_Window *window) : client(host, view), window(window) {}
    void Draw() {
        dialogs.Dispatch();
        const auto input = client.Query("paths.folder-input");
        const auto id = EditorClient::Integer(input, "input");
        if (input.status != EDITOR_OK || !id || id == shown) return;
        shown = id;
        dialogs.RequestFolder(L"Edit Path", window, [this, id](const FileDialogResult &result) {
            const bool accepted = result.status == FileDialogResult::Status::Selected && !result.paths.empty();
            const std::string choice = accepted ? "accept" : result.status == FileDialogResult::Status::Error ? "error" : "cancel";
            client.Command("paths.folder-response", {IntegerArgument("input", id), TextArgument("choice", choice),
                TextArgument("path", accepted ? result.paths.front() : ""), TextArgument("message", result.error)});
        });
    }
};
} // namespace
bool RegisterEnvironmentInputs(EditorApplication &editor, SDL_Window *window, std::string &error) {
    EditorViewDesc view{};
    view.size = sizeof(view); view.imguiVersion = EDITOR_IMGUI_VERSION; view.id = EDITOR_TEXT("environment.folder-dialogs");
    view.provider = EDITOR_TEXT("builtin.environment-dialogs"); view.title = EDITOR_TEXT("Search path folders");
    view.area = EDITOR_APPLICATION; view.kind = EDITOR_OVERLAY_VIEW; view.singleInstance = 1; view.data = window;
    view.create = [](void *data, const EditorHost *host, EditorView id) -> void * { return new FolderDialogs(host, id, static_cast<SDL_Window *>(data)); };
    view.destroy = [](void *data) { delete static_cast<FolderDialogs *>(data); };
    view.draw = [](void *data, const EditorHost *, EditorView, EditorViewFrame *) { static_cast<FolderDialogs *>(data)->Draw(); };
    const EditorString dependency = EDITOR_TEXT("environment.settings");
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("environment.dialogs");
    module.dependencies = &dependency; module.dependencyCount = 1; module.views = &view; module.viewCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev
