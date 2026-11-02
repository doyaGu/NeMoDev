#pragma once
#include "ui/native/EditorSession.h"
#include "core/layout/LayoutView.h"
#include "core/layout/LayoutInterface.h"
#include "core/catalog/BuildingBlockCatalog.h"
#include "core/graph/Debugger.h"
#include "ui/schematic/SchematicStyle.h"

#include "application/DocumentCommands.h"
#include "application/ExecutionCommands.h"
#include "application/OutputCommands.h"
#include "application/MessageCommands.h"
#include "features/arrays/ArrayCommands.h"
#include "features/objects/ObjectCommands.h"
#include "features/objects/SelectionCommands.h"
#include "features/resources/DataResourceCommands.h"
#include "features/setup/SetupCommands.h"
#include "features/schematic/SchematicCommands.h"
#include "features/values/ParameterCommands.h"
#include "features/environment/EnvironmentCommands.h"
#include "features/diagnostics/DiagnosticCommands.h"
#include "features/layout/LayoutCommands.h"
#include "features/layout/LayoutPresentation.h"
#include "features/layout/LayoutAuthoring.h"
#include "features/layout/SnapshotCommands.h"
#include "features/schematic/SchematicGraphCommands.h"
#include "workspace/EditorWorkspace.h"
#include <functional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include "ui/print/Printer.h"

#include "core/document/DocumentSaver.h"
#include "ui/panels/EditorPanels.h"
#include "ui/app/EditorStartup.h"
#include "ui/app/StatusBar.h"
#include "ui/layout/LayoutWindow.h"
#include "ui/schematic/SchematicCanvas.h"
#include "ui/schematic/SchematicViews.h"
#include "features/scene/SceneCommands.h"
#include "features/scene/SceneViews.h"
#include "ui/setup/SetupWindows.h"
#include "features/preferences/PreferencesCommands.h"
#include "ui/help/HelpWindow.h"

namespace vdev {


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
#ifdef VIRTOOLSDEV_UI_TESTS
    bool StartupSettled() const {
        return !m_Session.runtimeOperation.Busy() && m_FocusSchematic <= 0 && !m_StartupAfterLoads;
    }
#endif
    void Frame();
    // Called only after rendering and releasing this frame's UI textures.
    void EndFrame() {
        m_Editor.NativeAvailable(!m_Session.runtimeOperation.Busy());
        m_Editor.EndFrame();
        if (!m_Session.runtimeOperation.Busy() && m_Session.document) m_Session.document->CollectBitmaps();
        m_Session.runtimeOperation.Start();
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
    void ReportFailure(const std::string &message) { m_Session.Log(LogLevel::Error, message); }
    // The 3D Layout is fullscreen: the editor's window, hidden, keeps its frames going.
    bool LayoutFullScreen() const { return m_Layout.FullScreen(); }
    // Files dropped on the window at a client point: a composition opens;
    // an image becomes a Script's snapshot, an .nms file replaces the graph
    // it is dropped on or is imported where it is dropped (Dev drag and drop
    // of resources).
    void DropFiles(const std::vector<std::wstring> &paths, float x, float y);
    void ShowSchematic(const ElementAddress &address, ObjectRef focus = {}, EditorView view = 0) {
        m_Schematics.RequestShow(address, focus, view);
    }
#ifdef VIRTOOLSDEV_UI_TESTS
    ParameterCopies &ParameterCopiesForTests() { return m_ParameterCopies; }
    const Debugger *DebuggerForTests() const { return m_Debugger; }
    Preferences &SettingsForTests() { return m_Settings; }
    BuildingBlockCatalog &CatalogForTests() { return m_Catalog; }
    std::vector<CKGUID> &RecentBlocksForTests() { return m_RecentBlocks; }
    EditorWorkspace &WorkspaceForTests() { return m_Workspace; }
    EditorSession &State() { return m_Session; }
    // What the UI tests (tests/ui) check the UI's work on.
    SchematicCanvas &Canvas(EditorView view = 0) const { return view ? *m_Schematics.Find(view) : m_Schematics.Current(); }
    std::string SchematicWindow(EditorView view = 0) const { return m_Schematics.Window(view); }
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
    // Refresh stacks and finish view navigation after document edits settle.
    void RefreshSchematicViews();

    void ShowSchematicExplorer();
    bool m_CloseSeparated = false;
    void GlobalShortcuts();
    // A dialog, popup or menu open, a control held or a drag going on: what
    // they keep (ids, the state a drag began with) would no longer match the
    // composition after an undo or another file. Shortcuts and dropped files
    // wait for them.
    bool Busy() const;
    void UpdateTitle();

    EditorResponse Command(const char *id, const std::string &path = {});
    bool OpenFile(const std::string &path);
    LayoutInterfaceData CaptureLayout() const override { return LayoutDataToSave(); }
    void FileFinished(const DocumentCommands::Result &result) override;
    void HistoryChanged() override { m_Schematics.ClearSelections(); UpdateTitle(); }
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
    std::unique_ptr<SchematicPrintContent> CapturePrint(std::string &error) override;
    PrintChoice ChoosePrinter(const std::string &directory) override;
    bool OpenOutput(const std::string &path, std::string &error) override;
    void OutputFinished(const OutputCommands::Result &result) override;
#ifdef VIRTOOLSDEV_UI_TESTS
    std::string m_PendingPrintPages, m_PendingPrintDialog; // --tool printpages:DIR, print:DIR
    EditorOperation m_StartupResourceOperation = 0;
    std::string m_PendingStartupDialog;                     // --tool breakpoints, breakpointconfig, invalidlinks
    int m_PrintPagesFrames = 0;
    EditorOperation m_PrintPagesOperation = 0;
#endif
#ifdef VIRTOOLSDEV_UI_TESTS
    void InsertStartupBlock();
#endif
    // Dev's status bar and its Time Settings (StatusBar.cpp).
    void StatusBar();
    void ApplyPreferences();
    bool FileOperationAvailable() const {
        return !m_Session.runtimeOperation.Busy() && !m_FileCommands.Busy() && !m_Outputs.Busy();
    }
    std::string PreferencesPath() const;
    void SavePreferences(); // not when isolated
    bool SaveSearchPaths(const PathCategories &, std::string &error);
    bool SaveSettings(Preferences next, std::string &error);
    bool WriteSettings(const Preferences &, std::string &error);

    Preferences m_Settings;
    SchematicStyle m_Style;
    BuildingBlockCatalog m_Catalog;
    std::vector<CKGUID> m_RecentBlocks;
    CKDependencies m_CopyDependencies;
    bool m_CopyDependenciesSet = false;
    const Debugger *m_Debugger = nullptr;
    CKDependencies &CopyDependencies() {
        if (!m_CopyDependenciesSet) {
            if (auto *defaults = CKGetDefaultClassDependencies(CK_DEPENDENCIES_COPY)) m_CopyDependencies = *defaults;
            else m_CopyDependencies.m_Flags = CK_DEPENDENCIES_CUSTOM;
            m_CopyDependenciesSet = true;
        }
        return m_CopyDependencies;
    }
    void RememberBlock(CKGUID guid) {
        m_RecentBlocks.erase(std::remove(m_RecentBlocks.begin(), m_RecentBlocks.end(), guid), m_RecentBlocks.end());
        m_RecentBlocks.insert(m_RecentBlocks.begin(), guid);
        const auto limit = size_t(std::max(1, m_Settings.maxRecentBlocks));
        if (m_RecentBlocks.size() > limit) m_RecentBlocks.resize(limit);
    }
    EditorSession m_Session;
    EditorApplication m_Editor;
    MessageCommands m_Messages;
    DocumentCommands m_FileCommands{m_Editor, m_Session.files, m_Session.runtimeOperation,
                                    [this] { return m_Session.document.get(); }, *this};
    OutputCommands m_Outputs{m_Editor, m_Session.files, m_Session.runtimeOperation,
                              [this] { return m_Session.document.get(); }, *this};
    ExecutionCommands m_Execution{m_Editor, [this] { return m_Session.document.get(); },
                                  [this] { return FileOperationAvailable() && !m_ResourceCommands.Busy(); },
                                  m_Settings};
    EditorWorkspace m_Workspace{m_Editor};
    DataResourceCommands m_ResourceCommands{m_Editor, m_Session.host, [this] { return FileOperationAvailable(); }};
    DiagnosticCommands m_Diagnostics{m_Editor, m_Session.runtimeOperation, m_Session.host,
        [this] { return m_Session.document.get(); }, [this] { return FileOperationAvailable() && !m_ResourceCommands.Busy(); }};
    PreferencesCommands m_Preferences{m_Editor, m_Session.host, m_Settings,
        [this] { return FileOperationAvailable() && !m_ResourceCommands.Busy(); },
        [this](const Preferences &preferences, std::string &error) { return WriteSettings(preferences, error); },
        [this] { ApplyPreferences(); m_Panels.RefreshStack(); }};
    EnvironmentCommands m_Environment{m_Editor, m_Session.host,
        [this] { return FileOperationAvailable() && !m_ResourceCommands.Busy(); },
        [this](const PathCategories &paths, std::string &error) { return SaveSearchPaths(paths, error); },
        [this] { m_Catalog.Refresh(); m_SceneCommands.RefreshCatalog(); m_Schematics.Invalidate(); m_Diagnostics.InvalidateVersions(); m_Preferences.InvalidateCatalog(); }};
    ObjectCommands m_ObjectCommands;
    ParameterCommands m_ParameterCommands{[this] { return m_Session.HasDocument() ? m_Session.document.get() : nullptr; }, &m_Session.clipboard};
    ArrayCommands m_ArrayCommands{[this] { return m_Session.HasDocument() ? m_Session.document.get() : nullptr; }, m_Session.clipboard};
    SetupCommands m_SetupCommands{[this] { return m_Session.HasDocument() ? m_Session.document.get() : nullptr; }};
    SchematicCommands m_SchematicCommands{[this] { return m_Session.HasDocument() ? m_Session.document.get() : nullptr; }};
    SchematicGraphCommands m_SchematicGraph{
        [this] { return m_Session.HasDocument() ? m_Session.document.get() : nullptr; },
        [this] { return m_Settings.Bool("Schematic\\Test Parameter Link after Loading"); }, m_Session.clipboard};
    SelectionCommands m_SelectionCommands{[this] { return m_Session.HasDocument() ? m_Session.document.get() : nullptr; },
                                          m_Session.objectSelection};
    SchematicViews m_Schematics;
    ParameterCopies m_ParameterCopies{m_Session.host, m_Workspace, [this] { return m_Session.document.get(); },
        [this] {
            const auto &native = CopyDependencies();
            ParameterDependencies value; value.flags = uint32_t(native.m_Flags);
            for (int i = 0; i < native.Size(); ++i) value.options.push_back(native[i]);
            return value;
        }, [this] { m_Schematics.ClearSelections(); }};
    EditorPanels m_Panels;
    SetupWindows m_Setups{m_Workspace, m_Session.fileDialogs};
    SceneCommands m_SceneCommands{m_Editor, [this] { return m_Session.HasDocument() ? m_Session.document.get() : nullptr; },
        m_Session.objectClipboard, m_Session.clipboard, [this]() -> SchematicNavigation & { return m_Schematics.Current().Navigation(); }, [this]() -> CKDependencies & { return CopyDependencies(); }};
    HtmlFonts m_HtmlFonts;
    LayoutWindow m_Layout{m_Session.host, m_ParameterCopies, m_Editor};
    LayoutCommands m_LayoutCommands{m_Editor,
        [this] { return m_Session.HasDocument() ? m_Session.document.get() : nullptr; },
        [this]() -> LayoutView & { return m_Layout.View(); }, m_Session.objectSelection};
    LayoutAuthoring m_LayoutAuthoring{m_Editor,
                                      [this] { return m_Session.HasDocument() ? m_Session.document.get() : nullptr; },
                                      [this]() -> LayoutView & { return m_Layout.View(); },
                                      m_Session.objectSelection,
                                      m_Settings,
                                      [this]() -> SchematicNavigation & { return m_Schematics.Current().Navigation(); }};
    LayoutPresentation m_LayoutPresentation{m_Editor,
                                            [this] { return m_Session.HasDocument() ? m_Session.document.get() : nullptr; },
                                            [this]() -> LayoutView & { return m_Layout.View(); },
                                            m_Layout,
                                            m_Session.objectSelection,
                                            m_Settings,
                                            [this] { return m_Execution.State().state == EDITOR_EXECUTION_RUNNING; }};
    SnapshotCommands m_Snapshots{m_Editor, m_Session.runtimeOperation, m_Layout,
                                 [this] { return m_Session.HasDocument() ? m_Session.document.get() : nullptr; }};

#ifdef VIRTOOLSDEV_UI_TESTS
    ParameterBox m_StartupValue{m_ParameterCopies}; // --edit-value
    int m_StartupValueState = 0;          // 1: to open, 2: open
#endif
    SDL_Window * m_Window = nullptr;
    bool m_Quit = false;
    bool m_Shutdown = false;
    std::wstring m_Title;
    // Dev's main windows (3D Layout, Schematic, Level Manager, Building
    // Blocks) are always there; the Schematic Explorer opens from the toolbar.
    bool m_ResetLayout = false;
    int m_FocusSchematic = 0; // frames left to bring the default layout's front tabs forward
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
    EditorStatusBar m_StatusBar;
};

} // namespace vdev
