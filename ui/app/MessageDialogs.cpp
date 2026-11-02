#include "ui/app/MessageDialogs.h"

#include "editor/ImGuiDrawing.h"
#include "ui/controls/DevControls.h"
#include "workspace/EditorClient.h"
#include "imgui_internal.h"

namespace vdev {
namespace {
struct MessageDialog {
    uint64_t shown = 0;
    EditorOperation response = 0;
    ImGuiID popup = 0;
    std::string error;
    void Reset() {
        for (int i = 0; i < ImGui::GetCurrentContext()->OpenPopupStack.Size; ++i)
            if (ImGui::GetCurrentContext()->OpenPopupStack[i].PopupId == popup) {
                ImGui::ClosePopupToLevel(i, true);
                break;
            }
        shown = response = popup = 0;
        error.clear();
    }
    void Draw(const EditorClient &client) {
        if (response) {
            const auto result = client.Completion(response);
            if (result.status == EDITOR_QUEUED) return;
            response = 0;
            if (result.status != EDITOR_OK)
                error = result.message.empty() ? "Unable to close this message. Try again." : result.message;
        }
        const auto current = client.Query("message.current");
        const auto id = EditorClient::Integer(current, "message");
        if (current.status != EDITOR_OK || !id) { Reset(); return; }
        if (shown != id) { Reset(); shown = id; }
        const char *name = "###ApplicationMessage";
        if (!ImGui::IsPopupOpen(name)) {
            // File confirmations and other modal dialogs finish first.
            if (ImGui::GetTopMostPopupModal()) return;
            popup = ImGui::GetID(name);
            ImGui::OpenPopup(name);
        }
        auto text = EditorClient::Text(current, "text");
        if (!error.empty()) text += "\n\n" + error;
        const auto answer = vi::MessageDialog(name, EditorClient::Text(current, "title"), text,
                                              int(EditorClient::Integer(current, "icon")),
                                              EditorClient::Integer(current, "cancel") != 0);
        if (!answer) return;
        const auto result = client.Command("message.respond", {IntegerArgument("message", id),
            TextArgument("choice", answer == 1 ? "accept" : "cancel")});
        if (result.status == EDITOR_QUEUED) response = result.operation;
        else if (result.status != EDITOR_OK)
            error = result.message.empty() ? "Unable to close this message. Try again." : result.message;
    }
};
}
bool RegisterMessageDialogs(EditorApplication &editor, std::string &error) {
    const EditorViewDesc view{sizeof(EditorViewDesc), EDITOR_IMGUI_VERSION,
        EDITOR_TEXT("application.messages"), EDITOR_TEXT("builtin.message-dialogs"), EDITOR_TEXT("Messages"),
        [](void *, const EditorHost *, EditorView) -> void * { return new MessageDialog; },
        [](void *data) { delete static_cast<MessageDialog *>(data); },
        [](void *data, const EditorHost *host, EditorView view, EditorViewFrame *) {
            static_cast<MessageDialog *>(data)->Draw(EditorClient(host, view));
        }, nullptr, EDITOR_OVERLAY_VIEW, 1, EDITOR_APPLICATION};
    const EditorString dependency = EDITOR_TEXT("application.messages");
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("application.message-dialogs");
    module.dependencies = &dependency;
    module.dependencyCount = 1;
    module.views = &view;
    module.viewCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev
