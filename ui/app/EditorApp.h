#pragma once

#include <functional>
#include "features/objects/ObjectCommands.h"
#include "features/values/ParameterCommands.h"
#include "features/arrays/ArrayCommands.h"
#include "features/resources/DataResourceCommands.h"
#include "features/objects/SelectionCommands.h"
#include "application/DocumentCommands.h"
#include "application/ExecutionCommands.h"
#include "application/OutputCommands.h"
#include "workspace/EditorWorkspace.h"
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include "ui/print/Printer.h"

#include "core/document/DocumentSaver.h"
#include "ui/panels/EditorPanels.h"
#include "ui/app/EditorState.h"
#include "ui/layout/LayoutWindow.h"
#include "ui/schematic/SchematicCanvas.h"
#include "features/scene/SceneCommands.h"
#include "features/scene/SceneViews.h"
#include "ui/setup/SetupWindows.h"
#include "ui/tools/ToolWindows.h"
#include "ui/help/HelpWindow.h"

namespace vdev {

#ifdef VIRTOOLSDEV_UI_TESTS
// Command-line display options for the regression executable.
struct StartupOptions {
    bool openScripts = false;  // open every shown Script (an undoable display edit)
    bool expandGraphs = false; // expand every behavior graph of the shown Scripts
    bool showAllScripts = true; // Dev: every Script in one Schematic (--owner-scripts: only the owner's)
    bool fit = false;
    std::string insertBlock; // --insert-block <name>: insert into the first shown Script, link from Start
    std::string showScript;  // --show-script <name>: scroll the Schematic to this Script (UTF-8)
    std::string focusWindow; // --focus-window <name>[|<name>...]: bring these panels to front (capture checks)
    bool messages = false;   // --messages: open the Message Explorer
    int playFrames = 0;      // --play N: show the Script Debugger and run N traced frames
    std::vector<std::string> setups; // --setup NAME: open the Setup of the object NAME (repeatable)
    bool expandLevel = false; // --expand-level: open every Level Manager node
    bool newComposition = false; // --new: File > New Composition first
    std::vector<std::pair<std::string, bool>> merges; // --merge FILE / --merge-scene FILE (as a new scene)
    // --import FILE, --import-character / --import-place / --import-scene FILE
    std::vector<std::pair<std::string, int>> imports; // Document::ImportMode
    std::vector<std::string> resources; // --resource FILE.rsc: open a Data Resource
    // --resource-folder PATH: the folder of the last one shown (its path ends with PATH)
    std::string resourceFolder;
    // --tool NAME: open a window (prefs[:page], plugins, paths, unused, versiontest,
    // profiler, watch, about, linkprops) for capture checks
    std::vector<std::string> tools;
    // --layout-tool NAME: the 3D Layout's tool (select, translate, rotate, scale,
    // orbit, orbitcamera, dolly, zoom, pan, roll, fov); --layout-view NAME: its camera
    std::string layoutTool;
    std::string layoutView;
    // --layout-select NAME (repeatable): objects selected in the 3D Layout;
    // --layout-group NAME: that name entered in its Selection Groups combo;
    // --layout-command ID (repeatable): a 3D Layout command by Dev's id (41
    // the 3D Layout Explorer, 56 the Snapshot Buffer, ...).
    std::vector<std::string> layoutSelect;
    std::string layoutGroup;
    std::vector<int> layoutCommands;
    bool layoutDebug = false; // --layout-debug: the 3D Layout's On-screen Debug Data
    // --drop-block NAME X Y: a Building Block dropped on the 3D Layout at a client point.
    std::string dropBlock;
    float dropX = 0.0f, dropY = 0.0f;
    std::vector<std::string> creates; // --create camera|light|frame|curve|grid|2d|material|texture|portal
    bool menuAt = false;     // --menu-at <x> <y>: context menu at a canvas point
    float menuX = 0.0f, menuY = 0.0f;
    std::vector<std::string> closeScripts;                 // --close-script <name> (repeatable)
    // --edit-parameters NAME: Dev's Edit Parameters of the first Building
    // Block or parameter operation named NAME (ANSI), once the window is up.
    std::string editParameters;
    // --edit-value TYPE: Dev's Edit Parameter dialog (value only) on a new
    // parameter of the type named TYPE (ANSI): a type's UI alone.
    std::string editValue;
    std::vector<std::pair<std::string, float>> zoomScripts; // --zoom-script <name> <scale>
    // --isolated (and the UI tests): Dev's default preferences, none read
    // from or saved to the preferences file or Dev's registry, no window
    // layout file (Main.cpp): the same start on every machine.
    bool isolated = false;
};
#endif

class EditorApp : private DocumentFiles::Views, private DocumentCommands::Presentation, private OutputCommands::Presentation {
public:
    explicit EditorApp(HtmlFonts fonts);
    ~EditorApp();

    // Starts the CK runtime; runtimeDirectory must already be on the DLL
    // search path (CK2.dll and VxMath.dll are delay-loaded).
    bool Initialize(SDL_Window * window, const std::string &runtimeDirectory, const std::string &fileToOpen, const std::vector<std::string> &modules = {}
#ifdef VIRTOOLSDEV_UI_TESTS
                    , const StartupOptions &startup = StartupOptions()
#endif
                    );
    void Frame();
    // Called only after rendering and releasing this frame's UI textures.
    void EndFrame() {
        m_Editor.NativeAvailable(!m_State.runtimeOperation.Busy());
        m_Editor.EndFrame();
        if (!m_State.runtimeOperation.Busy() && m_State.document) m_State.document->CollectBitmaps();
        m_State.runtimeOperation.Start();
    }
    void HandleEvent(const SDL_Event &event);
    void Shutdown();
    // Window close request: asks about unsaved changes first.
    void RequestQuit();
    // Dev creation tools (Create menu); the new object's Setup opens.
    void CreateObject(LayoutObject kind);
#ifdef VIRTOOLSDEV_UI_TESTS
    // Logs the label of the last undo step (capture-mode UI checks).
    void LogUndoTop();
#endif
    std::set<CK_ID> LevelSelection();
    EditorView OpenSceneView(const std::string &type);
    bool SceneViewOpen(const std::string &type) const;
    void CloseSceneView(const std::string &type);
    std::string SceneWindow(const std::string &type) const;
    std::string BuildingBlocksWindow() const { return m_Workspace.WindowName(m_Workspace.Role("main.building-blocks")); }
    bool ShouldQuit() const { return m_Quit; }
    // The 3D Layout is fullscreen: the editor's window, hidden, keeps its frames going.
    bool LayoutFullScreen() const { return m_Layout.FullScreen(); }
    // Files dropped on the window at a client point: a composition opens;
    // an image becomes a Script's snapshot, an .nms file replaces the graph
    // it is dropped on or is imported where it is dropped (Dev drag and drop
    // of resources).
    void DropFiles(const std::vector<std::wstring> &paths, float x, float y);
    EditorState &State() { return m_State; }
#ifdef VIRTOOLSDEV_UI_TESTS
    // What the UI tests (tests/ui) check the UI's work on.
    const SchematicCanvas &Canvas() const { return m_Canvas; }
    const LayoutWindow &Layout() const { return m_Layout; }
    LayoutWindow &Layout() { return m_Layout; }
    const EditorPanels &Panels() const { return m_Panels; }
    const SetupWindows &Setups() const { return m_Setups; }
    std::vector<SceneShownRow> LevelRows() { return SceneRowsForTests(m_Workspace.Role("main.level")); }
    std::vector<SceneShownRow> HierarchyRows() { return SceneRowsForTests(m_Workspace.Role("main.hierarchy")); }
    const HelpWindow &Help() const { return HelpForTests(); }
#endif

private:
    void MainMenu();
    // What the windows asked of each other the frame before (EditorState::events).
    void DispatchEvents();
    void ShowSchematicExplorer();
    bool m_CloseSeparated = false;
    void GlobalShortcuts();
    // A dialog, popup or menu open, a control held or a drag going on: what
    // they keep (ids, the state a drag began with) would no longer match the
    // composition after an undo or another file. Shortcuts and dropped files
    // wait for them.
    bool Busy() const;
    void SaveReportDialog();
    void UpdateTitle();

    EditorResponse Command(const char *id, const std::string &path = {});
    bool OpenFile(const std::string &path);
    LayoutInterfaceData CaptureLayout() const override { return LayoutDataToSave(); }
    void FileFinished(const DocumentCommands::Result &result) override;
    void HistoryChanged() override { m_State.selection.Clear(); UpdateTitle(); }
    void Quit() override { m_Quit = true; }
    void CompleteOpen(const std::string &path, const DocumentFiles::OpenResult &result);
    void RuntimeOperationFrame();
    // File > New Composition, Merge Composition (as New Scene), Revert to Saved.
    void NewComposition(bool background = true);
    void MergeComposition(bool asScene, const std::string &path = std::string()); // empty: ask for the file
    void RememberRecentFile(const std::string &path);
    // Resources menu: Import File (As), Open / Create New Data Resource.
    void ImportFile(Document::ImportMode mode, const std::string &path = std::string());
    // Editors > Close All Setups and Managers.
    void CloseSetupsAndManagers();
    bool RegisterSceneNavigation(std::string &);
    // Values of the General Preferences the CK context takes (file save
    // options, quantization, behavior iterations, path manager).
    void ApplyRuntimeSettings();
#ifdef VIRTOOLSDEV_UI_TESTS
    // The capture mode's startup actions (EditorStartup.cpp): after opening,
    // a --tool window, and each frame (settled: the start's focus is done).
    void RunStartupActions(bool opened);
    void OpenStartupTool(const std::string &name);
    void OpenStartupDialog(const std::string &name);
    void StartupFrame(bool settled);
#endif
    // Detach windows and render resources on the UI thread before native replacement/close.
    void CloseDocumentViews();
    void DetachDocument(bool replaceContext) override;
    void RestoreDocument() override;
    bool DocumentInteractionActive() const override;
    LayoutInterfaceData m_DetachedLayout;
    DocumentToken m_DetachedDocument{};
    void ReopenSetups(const LayoutInterfaceData &data);
    LayoutInterfaceData LayoutDataToSave() const;
    // The File menu's other commands (FileMenu.cpp): Save Version, Revert to
    // Saved, Export to Player, Create Web Page; the composition's file is the
    // last saved, else the one opened.
    std::string CurrentFilePath() const;
    // Dev's message box (Dev.exe sub_5D5E60, dialog 727): the text (ANSI,
    // lines centered), the title (empty: "Warning!"), icon: Dev's icon type
    // (1 to 6, 0 none), OK and Cancel or OK alone; onOk runs after OK.
    struct DevMessage {
        std::string text, title;
        int icon = 0;
        bool cancel = false;
        std::function<void()> onOk, onCancel;
    };
    void ShowMessage(DevMessage message);
    void MessageDialog();
    std::unique_ptr<SchematicPrintContent> CapturePrint(std::string &error) override;
    PrintChoice ChoosePrinter(const std::string &directory) override;
    bool OpenOutput(const std::string &path, std::string &error) override;
    void OutputFinished(const OutputCommands::Result &result) override;
#ifdef VIRTOOLSDEV_UI_TESTS
    std::string m_PendingPrintPages, m_PendingPrintDialog; // --tool printpages:DIR, print:DIR
    std::string m_PendingStartupDialog;                     // --tool breakpoints, breakpointconfig, invalidlinks
    int m_PrintPagesFrames = 0;
    EditorOperation m_PrintPagesOperation = 0;
#endif
#ifdef VIRTOOLSDEV_UI_TESTS
    void InsertStartupBlock();
#endif
    // Dev's status bar and its Time Settings (StatusBar.cpp).
    void StatusBar();
    void TimeSettingsDialog();
    // Dev's Duplicate Names Found (DuplicateNames.cpp): the first load of
    // DocumentFiles::PendingDuplicates waiting for its choices.
    void DuplicateNamesDialog();
    void ApplyPreferences();
    bool FileOperationAvailable() const {
        return !m_State.runtimeOperation.Busy() && !m_FileCommands.Busy() && !m_Outputs.Busy();
    }
    std::string PreferencesPath() const;
    void SavePreferences(); // not when isolated

    EditorState m_State;
    EditorApplication m_Editor;
    DocumentCommands m_FileCommands{m_Editor, m_State.files, m_State.runtimeOperation,
                                    [this] { return m_State.document.get(); }, *this};
    OutputCommands m_Outputs{m_Editor, m_State.files, m_State.runtimeOperation,
                              [this] { return m_State.document.get(); }, *this};
    ExecutionCommands m_Execution{m_Editor, [this] { return m_State.document.get(); },
                                  [this] { return FileOperationAvailable() && !m_ResourceCommands.Busy(); }};
    EditorWorkspace m_Workspace{m_Editor};
    DataResourceCommands m_ResourceCommands{m_Editor, m_State.host, [this] { return FileOperationAvailable(); }};
    ObjectCommands m_ObjectCommands;
    ParameterCommands m_ParameterCommands{[this] { return m_State.HasDocument() ? m_State.document.get() : nullptr; }};
    ArrayCommands m_ArrayCommands{[this] { return m_State.HasDocument() ? m_State.document.get() : nullptr; }, m_State.clipboard};
    SelectionCommands m_SelectionCommands{[this] { return m_State.HasDocument() ? m_State.document.get() : nullptr; },
                                          m_State.objectSelection};
    SchematicCanvas m_Canvas;
    EditorPanels m_Panels;
    SetupWindows m_Setups{m_State};
    SceneCommands m_SceneCommands{m_Editor, [this] { return m_State.HasDocument() ? m_State.document.get() : nullptr; },
        m_State.objectClipboard, m_State.clipboard, m_State.hiddenScripts, [this]() -> CKDependencies & { return m_State.CopyDependencies(); }};
    ToolWindows m_Tools;
    HtmlFonts m_HtmlFonts;
    LayoutWindow m_Layout{m_State};
#ifdef VIRTOOLSDEV_UI_TESTS
    ParameterBox m_StartupValue{m_State}; // --edit-value
    int m_StartupValueState = 0;          // 1: to open, 2: open
#endif
    SDL_Window * m_Window = nullptr;
    bool m_Quit = false;
    bool m_Shutdown = false;
    bool m_OpenSaveReport = false;
    std::string m_SaveReportText;
    std::vector<DevMessage> m_Messages; // the first one shown
    std::wstring m_Title;
    // Dev's main windows (3D Layout, Schematic, Level Manager, Building
    // Blocks) are always there; the Schematic Explorer opens from the toolbar.
    bool m_ShowExplorer = false;
    bool m_ResetLayout = false;
    int m_FocusSchematic = 0; // frames left to bring the default layout's front tabs forward
    const char *m_ToFront = nullptr; // the window an event brings forward, once no popup is open (DispatchEvents)
#ifdef VIRTOOLSDEV_UI_TESTS
    StartupOptions m_Startup;
    bool m_RunStartupAfterOpen = false;
    int m_StartupAfterLoads = 0;
    int m_FitFrames = 0;
#endif
    std::uint64_t m_ScriptsSeen = 0; // Document::ScriptsRevision when the stack was made
    // The status bar: the error revision seen (an error makes the Event Log's
    // bomb flash until then), the Time Settings dialog and its values.
#ifdef VIRTOOLSDEV_UI_TESTS
    CK_ID m_StartupParametersScript = 0, m_StartupParameters = 0; // --edit-parameters
#endif
    uint64_t m_LogErrorsSeen = 0;
    double m_BombFlashUntil = 0.0;
    bool m_OpenTimeSettings = false;
    bool m_HideFps = false;
    int m_TimeFrame = 0, m_TimeBehavior = 0;
    float m_TimeFrameLimit = 60.0f, m_TimeBehaviorLimit = 60.0f, m_TimeScale = 1.0f, m_TimeMin = 1.0f, m_TimeMax = 200.0f;
};

} // namespace vdev
