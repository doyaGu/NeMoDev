#include "ui/app/EditorApp.h"
#include "ui/controls/DevControls.h"
#include "imgui.h"

namespace vdev {
std::string EditorApp::CurrentFilePath() const { return m_FileCommands.CurrentFilePath(); }

void EditorApp::ShowMessage(DevMessage message) { m_Messages.push_back(std::move(message)); }

// Dev's message box (dialog 727, sub_5A0DD0's size): the icon at the left,
// the text's lines centered, OK (and Cancel) below.
void EditorApp::MessageDialog() {
    const char *id = "###DevMessage";
    if (m_Messages.empty()) return;
    const DevMessage &message = m_Messages.front();
    if (m_FocusSchematic <= 0 && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    const int closed = vi::MessageDialog(id, message.title, message.text, message.icon, message.cancel);
    if (!closed) return;
    std::function<void()> then = std::move(closed == 1 ? m_Messages.front().onOk : m_Messages.front().onCancel);
    m_Messages.erase(m_Messages.begin());
    if (then) then();
}


} // namespace vdev
