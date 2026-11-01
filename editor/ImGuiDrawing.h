#pragma once

#include "editor/Editor.h"
#include "imgui.h"

// Modules must import the host's ImGui implementation, never link another copy.
// The host sets the current context before every draw/create/destroy callback.
#define EDITOR_IMGUI_VERSION                                                                                           \
    {IMGUI_VERSION_NUM, sizeof(ImGuiIO), sizeof(ImGuiStyle), sizeof(ImDrawVert), sizeof(ImDrawIdx)}

// Keeps Begin/End balanced even when drawing returns early or throws. Styling
// and size preparation remain in the view, including custom dialog title bars.
inline void EditorPrepareViewWindow(const EditorViewFrame &frame) {
    if (frame.restorePlacement) {
        // Setting a position requests undocking, even when a dock ID is
        // also supplied. Docked windows take their geometry from the node.
        if (!frame.dock) {
            ImGui::SetNextWindowPos({frame.x, frame.y});
            ImGui::SetNextWindowSize({frame.width, frame.height});
        }
        ImGui::SetNextWindowDockID(frame.dock);
    }
    if (!frame.restorePlacement && frame.initialDock)
        ImGui::SetNextWindowDockID(frame.initialDock, ImGuiCond_FirstUseEver);
    if (frame.focus)
        ImGui::SetNextWindowFocus();
}
class EditorImGuiWindow {
  public:
    explicit EditorImGuiWindow(EditorViewFrame &frame, ImGuiWindowFlags flags = 0) {
        EditorPrepareViewWindow(frame);
        bool open = true;
        m_Visible = ImGui::Begin(frame.window.data, frame.closable ? &open : nullptr, flags);
        frame.close = !open;
    }
    ~EditorImGuiWindow() { ImGui::End(); }
    EditorImGuiWindow(const EditorImGuiWindow &) = delete;
    EditorImGuiWindow &operator=(const EditorImGuiWindow &) = delete;
    explicit operator bool() const { return m_Visible; }
  private:
    bool m_Visible;
};
