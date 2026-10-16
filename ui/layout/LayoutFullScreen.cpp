#include "ui/layout/LayoutWindow.h"
#include "ui/controls/DevTheme.h"
#include "core/runtime/TextEncoding.h"
#include <SDL3/SDL.h>

namespace vdev {
void LayoutWindow::ToggleFullScreen() {
    if (m_View.FullScreen()) { LeaveFullScreen(); return; }
    if (!m_Started || !m_PlayerMode) return;
    const Preferences &p = m_State.preferences;
    CKContext *context = m_State.host.Context();
    const int driver = LayoutFullScreenDriver(context, p.Text("3D Layout\\Player Mode\\FullScren Driver"));
    const std::string wanted = p.Text("3D Layout\\Player Mode\\Render Mode");
    VxDisplayMode mode;
    if (!LayoutFullScreenMode(context, driver, wanted, mode)) {
        m_State.Log(LogLevel::Warning, "3D Layout: unavailable fullscreen mode " + wanted); return;
    }
    m_Switching = true;
    const bool full = m_View.GoFullScreen(driver, mode);
    if (full && m_MainWindow && !(SDL_GetWindowFlags(m_MainWindow) & SDL_WINDOW_HIDDEN))
        m_MainHiddenForFullScreen = SDL_HideWindow(m_MainWindow);
    m_Switching = false;
    if (!full) m_State.Log(LogLevel::Warning, "3D Layout: cannot enter fullscreen");
}
void LayoutWindow::LeaveFullScreen(FullScreenExit reason) {
    m_PendingToggle = m_PendingDeactivate = false;
    if (!m_View.FullScreen()) return;
    m_Switching = true;
    const bool stopped = m_View.StopFullScreen();
    m_Switching = false;
    if (!stopped) { m_State.Log(LogLevel::Error, "3D Layout: cannot leave fullscreen"); return; }
    if (m_MainWindow && m_MainHiddenForFullScreen) {
        // Showing the editor after another application takes focus must not
        // activate it and immediately take that application's focus back.
        const char *hint = SDL_GetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN);
        const bool hadHint = hint != nullptr;
        const std::string previous = hint ? hint : "";
        if (reason == FullScreenExit::Deactivated) SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
        SDL_ShowWindow(m_MainWindow);
        if (reason == FullScreenExit::Deactivated) {
            if (hadHint) SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, previous.c_str());
            else SDL_ResetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN);
        }
        m_MainHiddenForFullScreen = false;
    }
    if (m_MainWindow && reason == FullScreenExit::ReturnToEditor) SDL_RaiseWindow(m_MainWindow);
}
void LayoutWindow::FullScreenEvent(const SDL_Event &event) {
    if (!m_View.FullScreen()) return;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && m_PlayerMode)
        LayoutSendClick(m_State.host.Context(), m_View.PickInfo(int(event.button.x), int(event.button.y)), event.button.clicks >= 2);
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        const bool alt = (event.key.mod & SDL_KMOD_ALT) != 0, ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
        const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
        if (!shift) {
            if (event.key.key == SDLK_RETURN && alt && !ctrl) m_PendingToggle = true;
            else if (event.key.key == SDLK_SPACE && ctrl && !alt) m_PendingPlay.push_back(PlayCommand::PlayStop);
            else if (event.key.key == SDLK_SPACE && alt && !ctrl) m_PendingPlay.push_back(PlayCommand::Step);
            else if (event.key.key == SDLK_BACKSPACE && (ctrl || alt)) m_PendingPlay.push_back(PlayCommand::Reset);
        }
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        m_PendingDeactivate = true;
}

void LayoutWindow::FullScreenCommands() {
    const bool toggle = m_PendingToggle, deactivate = m_PendingDeactivate;
    std::vector<PlayCommand> commands;
    commands.swap(m_PendingPlay);
    m_PendingToggle = m_PendingDeactivate = false;
    // Dev.exe sub_4B95C0 (WM_ACTIVATEAPP): another application taking over
    // pauses the composition, which leaves fullscreen.
    if (deactivate && m_View.FullScreen()) {
        if (play) play(PlayCommand::Pause);
        LeaveFullScreen(FullScreenExit::Deactivated);
    }
    if (toggle) ToggleFullScreen();
    for (PlayCommand command : commands) {
        // Play / Stop and Reset leave fullscreen first (Dev.exe sub_5C5F50, sub_5CD680).
        if (command != PlayCommand::Step) LeaveFullScreen();
        if (play) play(command);
    }
}

} // namespace vdev
