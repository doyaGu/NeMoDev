#pragma once

#include <functional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include "ui/print/Printer.h"

#include "core/document/DocumentSaver.h"
#include "ui/panels/EditorPanels.h"
#include "ui/app/EditorState.h"
#include "ui/layout/LayoutWindow.h"
#include "ui/schematic/SchematicCanvas.h"
#include "ui/panels/ManagerWindows.h"
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

class EditorApp {
public:
    explicit EditorApp(HelpFonts fonts) : m_Canvas(m_State), m_Panels(m_State, m_Canvas), m_Tools(m_State, m_Canvas), m_Help(fonts) {}

    // Starts the CK runtime; runtimeDirectory must already be on the DLL
    // search path (CK2.dll and VxMath.dll are delay-loaded).
    bool Initialize(SDL_Window * window, const std::string &runtimeDirectory, const std::string &fileToOpen
#ifdef VIRTOOLSDEV_UI_TESTS
                    , const StartupOptions &startup = StartupOptions()
#endif
                    );
    void Frame();
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
    const EditorPanels &Panels() const { return m_Panels; }
    const SetupWindows &Setups() const { return m_Setups; }
    const ManagerWindows &Managers() const { return m_Managers; }
    const HelpWindow &Help() const { return m_Help; }
#endif

private:
    enum class PendingAction { None, New, Open, OpenPath, Close, Quit };
    std::string m_PendingPath;

    void MainMenu();
    // What the windows asked of each other the frame before (EditorState::events).
    void DispatchEvents();
    void ShowSchematicExplorer();
    // Dev's arrangement. The windows docked before go back to their tab
    // rows (Setups and the Attribute Manager with the Schematic, the
    // Hierarchy Manager, Parameter Debugger and Data Resources with the
    // Building Blocks); closeSeparated (Reset Interface): the floating ones
    // close, but Data Resources, which dock again.
    void BuildDefaultLayout(unsigned int dockspace, bool closeSeparated = false);
    bool m_CloseSeparated = false;
    void GlobalShortcuts();
    // A dialog, popup or menu open, a control held or a drag going on: what
    // they keep (ids, the state a drag began with) would no longer match the
    // composition after an undo or another file. Shortcuts and dropped files
    // wait for them.
    bool Busy() const;
    void UnsavedChangesDialog();
    void SaveReportDialog();
    void UpdateTitle();

    void BeginAction(PendingAction action);
    void RunAction(PendingAction action);
    bool OpenFile(const std::string &path);
    // File > New Composition, Merge Composition (as New Scene), Revert to Saved.
    void NewComposition();
    void ValidateLinks();
    void MergeComposition(bool asScene, const std::string &path = std::string()); // empty: ask for the file
    void RememberRecentFile(const std::string &path);
    // Resources menu: Import File (As), Open / Create New Data Resource.
    void ImportFile(Document::ImportMode mode, const std::string &path = std::string());
    void OpenDataResourceDialog();
    void CreateDataResourceDialog();
    // Help > Online Reference: the optional local HTML reference.
    void OpenOnlineReference();
    // Editors > Close All Setups and Managers.
    void CloseSetupsAndManagers();
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
    void CloseDocument();
    void SaveAs(std::function<void(bool)> completed = {});
    void Save(std::function<void(bool)> completed = {});
    bool SaveTo(const std::string &path, bool allowOverwrite);
    // What Save and the exports write with the composition (the pictures
    // are then written: not changed any longer).
    CompositionData CompositionToSave(const LayoutInterfaceData &layout);
    void ReopenSetups(const LayoutInterfaceData &data);
    LayoutInterfaceData LayoutDataToSave() const;
    // The File menu's other commands (FileMenu.cpp): Save Version, Revert to
    // Saved, Export to Player, Create Web Page; the composition's file is the
    // last saved, else the one opened.
    std::string CurrentFilePath() const;
    void SaveVersion();
    void RevertToSaved();
    void ExportToPlayer();
    // Writes the composition without marking it saved, with these
    // CK_FILE_WRITEMODE flags added; the report goes to the Event Log.
    bool ExportTo(const std::string &path, unsigned int writeMode, std::string *error);
    void WebPageDialog();
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
    // Print Schematic (SchematicPrint.cpp, Dev.exe sub_5380F0): the printer's
    // setup, Schematic Print Parameters (dialog 702), then the pages.
    void PrintSchematic(
#ifdef VIRTOOLSDEV_UI_TESTS
        const std::string &pngFolder = std::string()
#endif
    );
    void PrintParametersDialog();
    void PrintPages();
    void EndPrint();
    struct PrintJob {
        bool open = false;
        std::shared_ptr<Printer> printer;
        float width = 0.0f, height = 0.0f; // the Schematic's pixels
        int paper[2] = {};                 // 0.1 mm, orientation applied
        int device[2] = {};                // the paper less 10 mm, in printer pixels
        // Dev's fields: pixels per mm, pixels a page, pages; horizontal, vertical.
        int perMm[2] = {4, 4}, resolution[2] = {}, pages[2] = {1, 1};
        std::string text[6]; // the edits, in the order of the fields
#ifdef VIRTOOLSDEV_UI_TESTS
        std::string pngFolder; // --tool printpages:DIR: the pages as PNG files, no printer
#endif
    };
    PrintJob m_Print;
    SchematicPrint m_PrintOptions; // Dev's globals: kept from one print to the next
#ifdef VIRTOOLSDEV_UI_TESTS
    std::string m_PendingPrintPages, m_PendingPrintDialog; // --tool printpages:DIR, print:DIR
    std::string m_PendingStartupDialog;                     // --tool breakpoints, breakpointconfig, invalidlinks
    int m_PrintPagesFrames = 0;
#endif
    void Undo();
    void Redo();
#ifdef VIRTOOLSDEV_UI_TESTS
    void InsertStartupBlock();
#endif
    // Dev's status bar and its Time Settings (StatusBar.cpp).
    void StatusBar();
    void TimeSettingsDialog();
    // Dev's Duplicate Names Found (DuplicateNames.cpp): the first load of
    // EditorState::duplicateNames waiting for its choices.
    void DuplicateNamesDialog();
    void ApplyPreferences();
    std::string PreferencesPath() const;
    void SavePreferences(); // not when isolated

    EditorState m_State;
    SchematicCanvas m_Canvas;
    EditorPanels m_Panels;
    SetupWindows m_Setups{m_State};
    ManagerWindows m_Managers{m_State};
    ToolWindows m_Tools;
    HelpWindow m_Help;
    LayoutWindow m_Layout{m_State};
#ifdef VIRTOOLSDEV_UI_TESTS
    ParameterBox m_StartupValue{m_State}; // --edit-value
    int m_StartupValueState = 0;          // 1: to open, 2: open
#endif
    SDL_Window * m_Window = nullptr;
    bool m_Quit = false;
    bool m_LayoutChecked = false;
    PendingAction m_Pending = PendingAction::None;
    bool m_OpenUnsavedDialog = false;
    bool m_OpenSaveReport = false;
    std::string m_SaveReportText;
    std::string m_LastSavedPath;
    std::vector<DevMessage> m_Messages; // the first one shown
    // Create Web Page (Dev.exe sub_5C7240): the page's file and title kept
    // from one page to the next, the player's size and the two options
    // reset each time.
    bool m_OpenWebPage = false;
    unsigned m_WebPageRevision = 0;
    bool m_WebPageResume = false; // opened again as it was (the overwrite was cancelled)
    void WriteWebPage();
    std::string m_WebPagePath, m_WebPageTitle = "Virtools Generated Page";
    std::string m_WebPageEditPath, m_WebPageEditTitle; // while the dialog is open
    std::string m_WebPageWidth, m_WebPageHeight;
    bool m_WebPageCmo = false, m_WebPagePreview = false;
    std::wstring m_Title;
    // Dev's main windows (3D Layout, Schematic, Level Manager, Building
    // Blocks) are always there; the Schematic Explorer opens from the
    // Schematic's toolbar, the Event Log from the status bar.
    bool m_ShowExplorer = false;
    int m_DebugRefresh = 0;
    bool m_ShowLog = false;
    bool m_ResetLayout = false;
    int m_FocusSchematic = 0; // frames left to bring the default layout's front tabs forward
    const char *m_ToFront = nullptr; // the window an event brings forward, once no popup is open (DispatchEvents)
#ifdef VIRTOOLSDEV_UI_TESTS
    StartupOptions m_Startup;
    int m_FitFrames = 0;
#endif
    std::uint64_t m_ScriptsSeen = 0; // Document::ScriptsRevision when the stack was made
    // The status bar: the log entries seen (an error makes the Event Log's
    // bomb flash until then), the Time Settings dialog and its values.
#ifdef VIRTOOLSDEV_UI_TESTS
    CK_ID m_StartupParametersScript = 0, m_StartupParameters = 0; // --edit-parameters
#endif
    size_t m_LogSeen = 0;
    double m_BombFlashUntil = 0.0;
    bool m_OpenTimeSettings = false;
    bool m_HideFps = false;
    int m_TimeFrame = 0, m_TimeBehavior = 0;
    float m_TimeFrameLimit = 60.0f, m_TimeBehaviorLimit = 60.0f, m_TimeScale = 1.0f, m_TimeMin = 1.0f, m_TimeMax = 200.0f;
};

} // namespace vdev
