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
#include "ui/controls/BehaviorInfoDialog.h"
#include "ui/controls/ValueEditor.h"

namespace vdev {

class SchematicCanvas;

// Dev's windows of the Resources, Editors, Options and Help menus and the
// status bar (layouts after the Dev 2.5 screenshots the user gave):
// General Preferences, Installed Plugins, Path Manager, Unused Object
// Explorer, Building Block Version Test, Profiler, Parameter Debugger, Data
// Resources, Behavior Link Properties and About.
class ToolWindows {
public:
    ToolWindows(EditorState &state, SchematicCanvas &canvas) : m_State(state), m_Canvas(canvas) {}

    // topDock: the dock node of the Building Blocks (Parameter Debugger and
    // Data Resources tabs open there); applyPreferences: called after the
    // General Preferences are accepted; preferencesPath: where they are saved.
    // openPopups: false while the application moves the focus (it would close them).
    void Draw(ImGuiID topDock, bool openPopups = true);
    std::function<void()> applyPreferences;
    // The one object selected in the Level Manager, else 0.
    std::function<CK_ID()> levelTarget;

    // Opens a window.
    void OpenGeneralPreferences(int page = -1);
    void OpenInstalledPlugins() { m_ShowPlugins = true; }
    void OpenPathManager();
    void OpenUnusedObjects();
    void RunVersionTest();
    void OpenAbout() { m_OpenAbout = true; }
    // Ctrl+Alt+F12: Dev's All CK Object In Memory (dialog 135, Dev.exe
    // sub_5BB370, sub_5BB8D0): every CK object, as when it opens.
    void OpenAllObjects();
    void OpenLinkProperties();
    bool showProfiler = false;
    bool showParameterDebugger = false;
    // Data Resources: open (Resources > Open Data Resource) and create.
    bool OpenDataResource(const std::string &file, bool remember = true);
    // Shows the files of the last Data Resource's folder whose path ends
    // with relative (capture mode).
    void SelectResourceFolder(const std::string &relative);
    bool CreateNewDataResource(const std::string &file);
    void CloseAllResources();
    bool HasResources() const { return !m_Resources.empty(); }
    // Parameter Debugger: adds parameters (locals and outputs only).
    int WatchParameters(const std::vector<CK_ID> &parameters);
    void ClearWatches() { m_Doc.watches.clear(); }
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
    void InstalledPlugins();
    void PathManager();
    void UnusedObjectExplorer();
    void VersionTestDialog();
    void Profiler();
    void ProfilerDetails();
    void ParameterDebugger(ImGuiID dock);
    void AboutDialog();
    void LinkPropertiesDialog();
    // DataResources.cpp
    void DataResources(ImGuiID dock);
    struct Resource;
    void ResourceFolder(Resource &resource, const DataResourceFolder &folder);
    void LoadResource(const std::string &path, CK_CLASSID cid);
    // Import as Place / Scene, after Duplicate Names Found.
    void ImportResource(const std::string &path, Document::ImportMode mode);

    // What names the open composition's objects (CK gives their ids to the
    // next document's objects): made anew when it is closed; a field naming
    // an object goes here.
    struct DocumentState {
        std::vector<CK_ID> watches; // Parameter Debugger
        int watchSelected = -1;
        std::vector<UnusedObject> unused; // Unused Object Explorer
        std::set<CK_ID> excluded;
        VersionTestResult versionTest; // Building Block Version Test
        bool facesCounted = false;     // Profiler
    };

    EditorState &m_State;
    DocumentState m_Doc;
    SchematicCanvas &m_Canvas;
    bool m_OpenPopups = true;

    // General Preferences: edited on a copy, kept with OK.
    bool m_ShowPreferences = false;
    bool m_FocusPreferences = false;
    int m_CenterPreferences = 0;
    int m_Page = 0;
    Preferences m_Edit;
    // Installed Plugins
    bool m_ShowPlugins = false;
    std::set<std::string> m_OpenCategories{"Bitmap Readers", "Movie Readers"};
    // The plugin dlls unloaded (Temporary Unload); reloaded with the window closed.
    std::set<int> m_UnloadedDlls;
    void ReloadPluginDlls();
    // Path Manager
    bool m_OpenPaths = false;
    unsigned m_PathRevision = 0;
    std::vector<std::pair<std::string, std::vector<std::string>>> m_Paths;
    int m_PathCategory = 0;
    std::set<int> m_PathSelection;
    int m_EditPath = -1;
    std::string m_EditPathText;
    // Unused Object Explorer
    bool m_OpenUnused = false;
    // Building Block Version Test
    bool m_OpenVersionTest = false;
    // Profiler
    CKStats m_Stats = {};
    int m_Faces = 0, m_Vertices = 0;
    bool m_ProfilerDetails = false;
    bool m_ProfilerSorted = false; // Sort By Execution time: the slowest first
    bool m_ProfilingWasEnabled = false;
    bool m_ProfilerOpen = false;
    double m_LastStats = 0.0;
    // Parameter Debugger
    ValueEditState m_WatchEdit;
    ImGuiID m_TopDock = 0;
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
    // Data Resources
    struct Resource {
        DataResource data;
        bool open = true;
        std::string selectedFolder; // path
        CK_CLASSID selectedClass = 0;
        std::vector<std::string> files;
        std::string selectedFile;            // the one clicked last
        std::set<std::string> selectedFiles; // with Ctrl or Shift + click, several
        // The behavior files' descriptions (their .dsc; Dev.exe sub_561DD0).
        std::map<std::string, BehaviorDescription> descriptions;
    };
    void ShowFiles(Resource &resource, const DataResourceFolder &folder);
    // Behavior Information of a behavior file (Dev.exe sub_560990).
    BehaviorInfoDialog m_BehaviorInfo;
    std::vector<Resource> m_Resources;
};

} // namespace vdev
