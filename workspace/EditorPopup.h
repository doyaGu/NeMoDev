#pragma once
#include "editor/ImGuiDrawing.h"
#include "imgui_internal.h"

namespace vdev {
// A workspace modal belongs to its view, including programmatic closes and
// provider replacement while the native runtime is busy.
class EditorPopup {
public:
    ~EditorPopup() {
        if (!m_Id) return;
        if (auto *context = ImGui::GetCurrentContext())
            for (int i = 0; i < context->OpenPopupStack.Size; ++i)
                if (context->OpenPopupStack[i].PopupId == m_Id) { ImGui::ClosePopupToLevel(i, false); break; }
    }
    void Open(const EditorViewFrame &frame) {
        if (m_Id) return;
        m_Id = ImGui::GetID(frame.window.data);
        ImGui::OpenPopup(frame.window.data);
    }
    EditorPopup() = default;
    EditorPopup(const EditorPopup &) = delete;
    EditorPopup &operator=(const EditorPopup &) = delete;
private:
    ImGuiID m_Id = 0;
};
} // namespace vdev
