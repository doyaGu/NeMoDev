#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"

#include "ui/app/EditorState.h"
#include "core/document/ToolCommands.h"
#include "ui/controls/ParameterBox.h"

namespace vdev {

// Dev's windows of the Resources, Editors, Options and Help menus and the
// status bar (layouts after the Dev 2.5 screenshots the user gave):
// General Preferences, Unused Object
// Explorer, Building Block Version Test,
// Behavior Link Properties and About.
class ToolWindows {
public:
    explicit ToolWindows(EditorState &state) : m_State(state) {}

    // applyPreferences: called after the General Preferences are accepted.
    // openPopups: false while the application moves the focus (it would close them).
    void Draw(bool openPopups = true);
    std::function<void()> applyPreferences;

    // Opens a window.
    void OpenGeneralPreferences(int page = -1);
    void OpenUnusedObjects();
    void RunVersionTest();
    void OpenAbout() { m_OpenAbout = true; }
    // Ctrl+Alt+F12: Dev's All CK Object In Memory (dialog 135, Dev.exe
    // sub_5BB370, sub_5BB8D0): every CK object, as when it opens.
    void OpenAllObjects();
    void OpenLinkProperties();
    // Every window of the document goes (Close All Setups and Managers keeps
    // the Data Resources, as Dev keeps its default windows).
    void CloseManagers();
    void DocumentClosed();

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
    // ToolWindows.cpp
    void UnusedObjectExplorer();
    void VersionTestDialog();
    void AboutDialog();
    void LinkPropertiesDialog();
    // What names the open composition's objects (CK gives their ids to the
    // next document's objects): made anew when it is closed; a field naming
    // an object goes here.
    struct DocumentState {
        std::vector<UnusedObject> unused; // Unused Object Explorer
        std::set<CK_ID> excluded;
        VersionTestResult versionTest; // Building Block Version Test
    };

    EditorState &m_State;
    DocumentState m_Doc;
    bool m_OpenPopups = true;

    // General Preferences: edited on a copy, kept with OK.
    bool m_ShowPreferences = false;
    bool m_FocusPreferences = false;
    int m_CenterPreferences = 0;
    int m_Page = 0;
    Preferences m_Edit;
    // Unused Object Explorer
    bool m_OpenUnused = false;
    // Building Block Version Test
    bool m_OpenVersionTest = false;
    // About
    bool m_OpenAbout = false;
    void AllObjectsDialog();
    struct MemoryObject {
        std::string name, className;
        CK_ID id = 0;
        int size = 0;
        bool render = false, visible = false, dynamic = false, sceneObject = false, inScene = false;
        bool selected = false;
    };
    std::vector<MemoryObject> m_MemoryObjects; // CK's order until a header is clicked
    bool m_OpenAllObjects = false;
    int m_MemoryTotal = 0;
    int m_MemorySort = -1, m_MemoryAnchor = -1; // the column sorted by, the Shift click's row
    bool m_MemoryAscending = true;
    // Behavior Link Properties: its three rows (Edit Parameters).
    bool m_OpenLinkProperties = false;
    ParameterBox m_LinkBox{m_State};

};

} // namespace vdev
