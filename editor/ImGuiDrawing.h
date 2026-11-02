#pragma once

#include "editor/Editor.h"
#include "imgui.h"

// Modules must import the host's ImGui implementation, never link another copy.
// The host sets the current context before every draw/create/destroy callback.
#define EDITOR_IMGUI_VERSION                                                                                           \
    {IMGUI_VERSION_NUM, sizeof(ImGuiIO), sizeof(ImGuiStyle), sizeof(ImDrawVert), sizeof(ImDrawIdx)}

inline EditorToolInput EditorImGuiToolInput(ImVec2 origin, bool hovered, bool focused) {
    const auto &io = ImGui::GetIO();
    EditorToolInput input{};
    input.size = sizeof(input);
    input.phase = EDITOR_TOOL_FRAME;
    input.x = io.MousePos.x - origin.x;
    input.y = io.MousePos.y - origin.y;
    input.dx = io.MouseDelta.x;
    input.dy = io.MouseDelta.y;
    input.wheel = hovered ? io.MouseWheel : 0;
    input.wheelX = hovered ? io.MouseWheelH : 0;
    input.time = ImGui::GetTime();
    input.hovered = hovered;
    input.focused = focused;
    input.modifiers =
        (io.KeyCtrl ? 1u : 0u) | (io.KeyShift ? 4u : 0u) | (io.KeyAlt ? 2u : 0u) | (io.KeySuper ? 8u : 0u);
    for (unsigned button = 0; button < 3; ++button) {
        if (ImGui::IsMouseDown(button)) input.buttons |= 1u << button;
        if (hovered && ImGui::IsMouseClicked(button)) input.pressed |= 1u << button;
        if (ImGui::IsMouseReleased(button)) input.released |= 1u << button;
    }
    return input;
}

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
