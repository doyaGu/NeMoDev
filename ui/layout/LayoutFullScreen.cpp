#include "ui/layout/LayoutWindow.h"
#include "ui/controls/DevTheme.h"
#include "core/runtime/TextEncoding.h"
#include <SDL3/SDL.h>

namespace vdev {
void LayoutWindow::ToggleFullScreen() { Execute("layout.toggle-fullscreen"); }
void LayoutWindow::LeaveFullScreen(FullScreenExit reason) {
    m_PendingToggle = m_PendingDeactivate = false;
    Execute("layout.leave-fullscreen", {IntegerArgument("deactivated", reason == FullScreenExit::Deactivated)});
}
void LayoutWindow::FullScreenEvent(const SDL_Event &event) {
    if (!FullScreen())
        return;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && m_PlayerMode)
        PlayerClick(int(event.button.x), int(event.button.y), event.button.clicks >= 2);
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        const auto modifiers = event.key.mod;
        const bool alt = (modifiers & SDL_KMOD_ALT) != 0, ctrl = (modifiers & SDL_KMOD_CTRL) != 0;
        const bool shift = (modifiers & SDL_KMOD_SHIFT) != 0;
        if (!shift && event.key.key == SDLK_RETURN && alt && !ctrl) m_PendingToggle = true;
        else {
            const std::string name = event.key.key == SDLK_RETURN ? "Enter" : SDL_GetKeyName(event.key.key);
            const std::string chord = std::string(ctrl ? "Ctrl+" : "") + (alt ? "Alt+" : "") +
                (shift ? "Shift+" : "") + (modifiers & SDL_KMOD_GUI ? "Super+" : "") + name;
            EditorKey key;
            if (ReadEditorKey(chord, key))
                for (const auto &row : Client().Query("layout.player-shortcuts").fields) {
                    if (row.type != EDITOR_TEXT_LIST)
                        continue;
                    for (const auto &chord : row.texts) {
                        EditorKey binding;
                        if (ReadEditorKey(chord, binding) && binding == key) {
                            m_PendingPlay.push_back(row.key);
                            break;
                        }
                    }
                }
        }
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        m_PendingDeactivate = true;
}

void LayoutWindow::FullScreenCommands() {
    const bool toggle = m_PendingToggle, deactivate = m_PendingDeactivate;
    std::vector<std::string> commands;
    commands.swap(m_PendingPlay);
    m_PendingToggle = m_PendingDeactivate = false;
    // Dev.exe sub_4B95C0 (WM_ACTIVATEAPP): another application taking over
    // pauses the composition, which leaves fullscreen.
    if (deactivate && FullScreen()) {
        Execute("runtime.pause");
        LeaveFullScreen(FullScreenExit::Deactivated);
    }
    if (toggle) ToggleFullScreen();
    for (const auto &command : commands) {
        // Play / Stop and Reset leave fullscreen first (Dev.exe sub_5C5F50, sub_5CD680).
        if (command != "runtime.step-frame") LeaveFullScreen();
        Execute(command.c_str());
    }
}

} // namespace vdev
