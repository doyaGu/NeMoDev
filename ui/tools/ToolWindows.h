#pragma once

#include <functional>
#include <string>

#include "imgui.h"

#include "ui/app/EditorState.h"

namespace vdev {

// General Preferences, pending the application settings view migration.
class ToolWindows {
public:
    explicit ToolWindows(EditorState &state) : m_State(state) {}

    // applyPreferences: called after the General Preferences are accepted.
    // openPopups: false while the application moves the focus (it would close them).
    void Draw(bool openPopups = true) { m_OpenPopups = openPopups; GeneralPreferences(); }
    std::function<void()> applyPreferences;

    // Opens a window.
    void OpenGeneralPreferences(int page = -1);
    void CloseManagers() { m_ShowPreferences = false; }

    static const char *const PREFERENCE_PAGES[];
    static const int PREFERENCE_PAGE_COUNT;

private:
    // General Preferences pages (GeneralPreferences.cpp)
    void GeneralPreferences();
    void RenderingPage(Preferences &p);
    void ActionsPage(Preferences &p);
    void InterfacePage(Preferences &p);
    int m_AxisKindShown = 0; // Interface page: 0 the object axis's settings, 1 the snap marker's (view state)
    void ReferenceGuidePage(Preferences &p);
    void SnapPage(Preferences &p);
    void SchematicPage(Preferences &p);
    void MiscPage(Preferences &p);
    EditorState &m_State;
    bool m_OpenPopups = true;

    // General Preferences: edited on a copy, kept with OK.
    bool m_ShowPreferences = false;
    bool m_FocusPreferences = false;
    int m_CenterPreferences = 0;
    int m_Page = 0;
    Preferences m_Edit;

};

} // namespace vdev
