#include "features/schematic/SchematicScriptData.h"
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ui/app/EditorState.h"
#include "core/graph/BehaviorCommands.h"
#include "core/graph/Debugger.h"
#include "core/document/LinkValidation.h"
#include "core/level/LevelTree.h"
#include "core/catalog/ParameterCatalog.h"
#include "core/graph/TopologyCommands.h"
#include "ui/schematic/SchematicParameterDialog.h"
#include "features/resources/BuildingBlockData.h"
#include "features/schematic/SchematicModelData.h"
#include "features/schematic/SchematicPosition.h"
#include "ui/controls/ValueEditor.h"
#include "core/schematic/SchematicModel.h"
#include "ui/schematic/SchematicRenderer.h"
#include "core/schematic/SchematicSearch.h"
#include "workspace/EditorClient.h"

namespace vdev {

// The Schematic View: a vertical stack of Scripts with pan/zoom, selection
// and direct-manipulation editing. Each Script keeps a model rebuilt when
// its session revision or the document topology changes.
class SchematicCanvas {
public:
    explicit SchematicCanvas(EditorState &state, EditorView instance);
    ~SchematicCanvas();
    SchematicNavigation &Navigation() { return m_Navigation; }
    const SchematicNavigation &Navigation() const { return m_Navigation; }
    void ValidateNavigation();
    void ShowScript(CK_ID, const ElementRef & = {}, CK_ID focusBehavior = 0, bool focus = true);
    void RefreshStack();
    bool ScriptOpen(CK_ID) const;
    EditorStatus Navigate(const EditorRequest &);
    EditorStatus Inspect(EditorSink);
    EditorStatus Save(EditorSink) const;
    EditorStatus Restore(const EditorRequest &);
    void Draw(EditorViewFrame &);

    void Draw(const char *title, bool *open);
    void CancelEdits() {
        CancelInteraction();
        CloseValueInputs();
    }
    void CancelInteraction();
    void ResetView();
    void FitAll();
    void Invalidate();
    // A drag going on (move, resize, link, selection rectangle...): it ends
    // with the data it began with.
    bool Busy() const { return m_Tool != Tool::Idle; }
    // The composition is closed: Set Shortcut Source's next click no longer picks.
    void DocumentClosed();
    // Rebuilds the models (values shown while the Script Debugger runs).
    void RefreshValues() {
        ++m_ModelRefresh;
        for (auto &view : m_Views) view.revision = ~0ull;
    }
    void ShowMessageExplorer();
    // Shows a parameter where the Schematic displays it (Parameter Debugger "Go to source").
    void GoToParameter(ObjectRef parameter) { GoToShortcutSource(parameter); }
    // A file dropped at a screen point (EditorApp::DropFiles); false when unused.
    bool DropFile(const std::wstring &path, const std::string &ansiPath, const ImVec2 &screen);
    // Zoom of one Script body, applied when its view exists (--zoom-script).
    void SetScriptZoom(CK_ID script, float scale) { m_PendingZoom.emplace_back(script, scale); }
    // Opens the context menu at a canvas-relative point (--menu-at, capture checks).
    void RequestMenuAt(const ImVec2 &offset) {
        m_MenuRequest = offset;
        m_MenuRequested = true;
        m_MenuDelay = 5; // after the startup focus changes (they close popups) and pending zooms
    }

    // Commands shared with menus and shortcuts.
    void ToggleFold(CK_ID script, CK_ID behavior);
    void ToggleScriptOpen(CK_ID script);
    void AddComment(CK_ID script, const Point &local);
    // graphsConfirmed: Dev's "Confirm Graph(es) Deletion" answered (asked
    // when behavior graphs are among the behaviors deleted).
    void DeleteSelection(bool graphsConfirmed = false);
    void SelectAll();
    void Copy();
    void Cut();
    // At the last mouse position over the Schematic (or the selection).
    void Paste();
    // Ctrl+Shift+V: shortcuts of the copied parameters at the mouse.
    void PasteShortcuts();
    void Duplicate();
    // G: group the selected behaviors (or a new empty graph at the mouse).
    void GroupSelection();
    // Shift+Del: delete the selected graphs, keeping their contents.
    void UngroupSelection();
    // E / Return: parameters of the selected behavior or operation.
    void OpenParametersDialog();
    // The same for a behavior or operation of a Script (it is selected).
    void OpenParametersOf(CK_ID script, CK_ID object);
    // The Schematic Explorer's Edit (Dev.exe sub_5B8690): the element's
    // dialog - a behavior's settings (else its parameters), an operation's
    // or a parameter pin's owner's parameters, a comment's text, a local's
    // or a shortcut's value.
    void EditElement(CK_ID script, const ElementRef &ref);
    // A Building Block's page of documentation.chm (Html Help; the Building
    // Blocks panel's too).
    void OpenHtmlHelpOf(const BuildingBlockEntry &entry);
    // Dev's Find (Dev.exe sub_544BF0; documentation "Schematic Find"):
    // Ctrl+F asks for the text (Search Closed Scripts/Graphs, Search Hidden
    // Scripts, Search Backward; Find Next); F3 / Shift+F3 the next /
    // previous element whose name has it (any case), in the Schematic's
    // Script order; past the last one "Passed the end of Schematic" and the
    // selection cleared. Ctrl+F3: the next with the selected element's name.
    void OpenFind() { m_OpenFind = true; }
    bool Finding() const { return !m_FindText.empty(); }
    const ElementRef &Found() const { return m_FoundRef; }
    void FindNext(bool backward);
    void FindSelected(bool backward);
    // Behaviors saved as an .nms file (the file asked for, then Behavior
    // Information): the Schematic's Save As, the Level Manager's Script rows.
    void SaveBehaviorsAs(const std::vector<CK_ID> &behaviors, const std::string &suggestion);
    // Y: priority of the selected behaviors.
    void OpenPriorityDialog();
    // Shift+R: rebuild every Script model from the runtime and display data.
    void Refresh();
    bool LinkMode() const { return m_LinkMode; }
    // Current interaction, for the status bar.
    const char *ActionText() const;
    // Topology commands (CK graph + display data, undoable).
    void ToggleTargetOfSelection();
    void BeginRename();
    void OpenLocalParameterPicker(CK_ID script, const Point &local);
    void OpenOperationPicker(CK_ID script, const Point &local);
    // Pins of the selected behavior graph (or the Script, for parameters).
    void AddGraphIo(bool input);
    void OpenGraphParameterPicker(bool input);
    void CreateGraphAt(CK_ID script, const Point &local);
#ifdef VIRTOOLSDEV_UI_TESTS
    // A behavior drawn at the last frame, wholly in the canvas and in its
    // open Script's body (the UI tests find what they click with it); none
    // when the canvas was not drawn.
    struct ShownBehavior {
        CK_ID script = 0, behavior = 0;
        std::string name; // UTF-8
        bool buildingBlock = false;
        ImVec2 min, max; // screen
    };
    std::vector<ShownBehavior> ShownBehaviors() const;
    // A pin of a behavior of an open graph drawn at the last frame, in the
    // canvas: its Script, owner, kind, index and center on the screen.
    struct ShownPin {
        CK_ID script = 0, owner = 0;
        PinKind kind = PinKind::None;
        int index = 0;
        ImVec2 center; // screen
    };
    std::vector<ShownPin> ShownPins() const;
    // An open Script's graph drawn at the last frame (its bounds, where a
    // drop inserts into it): the part of it in the canvas and in its body;
    // its header (the name, the owner's row under it; a right click there:
    // the Script's menu), whole; its Start's center when it has one.
    struct ShownScript {
        CK_ID script = 0;
        ImVec2 min, max;             // screen
        ImVec2 headerMin, headerMax; // screen
        bool hasStart = false;
        ImVec2 start; // screen
    };
    std::vector<ShownScript> ShownScripts() const;
#endif
    // The global zoom of the whole Schematic (+ / -), each Script's scale apart.
    float GlobalZoom() const { return m_View.zoom; }
    // The Script Debugger's Breakpoints buttons: Configure Breakpoints (the
    // selection's breakpoint, else the first), Breakpoint List (Dev's
    // Breakpoints), Add Breakpoint (the next click on a behavior, a behavior
    // link or a parameter toggles its breakpoint, Dev.exe sub_5866B0 1187),
    // Goto (the breakpoint's element shown).
    void ConfigureBreakpoints();
    // The Invalid Link Explorer shown (its toolbar icon's).
    void ShowInvalidLinks();
    void OpenBreakpointList();
    void PickBreakpoint();
    void GoToBreakpoint(const SchematicBreakpoint &);
    // Print Schematic (Dev.exe sub_5380F0): the Scripts shown, stacked from
    // the top at 100% (each with its own zoom). PrintSize: Dev's width (the
    // widest open Script less 100 pixels, at least 140) and the stack's
    // height; false without a Script shown.
    std::unique_ptr<SchematicPrintContent> CapturePrint() const;

private:
    friend class SchematicViews;
    struct ScriptView {
        CK_ID id = 0;
        ObjectRef identity{};
        SchematicModel model;
        std::map<ElementRef, ElementAddress> positions;
        std::uint64_t revision = ~0ull;
        std::uint64_t topology = ~0ull;
        std::uint64_t viewRevision = ~0ull;
        std::uint64_t sourceVersion = 0, refresh = 0;
        float content = 0.0f; // the contents' height at the last build
        float offsetY = 0.0f;
        bool rootIsScript = true;
        // Each Script has its own zoom (scale), about its Start: the header,
        // the band and Start stay; the body grows from Start. All bodies
        // share one horizontal scroll.
        float scale = 1.0f;
        Point anchor; // Script-local zoom center: the Start tip
        float stackTop = 0.0f;   // top of the band in stack units
        ViewTransform frame;     // band and header: Script-local (+ offsetY) to screen
        ViewTransform transform; // body: Script-local (+ offsetY) to screen
        ImVec2 bodyMin, bodyMax; // body area on screen (right of the header)
        float Zoom() const { return transform.zoom; }
    };
    struct Hit {
        ScriptView *view = nullptr;
        ElementRef ref;
        Point local;
        bool onHeader = false; // over the Script header column (frame coordinates)
        bool onStart = false;  // over Start (attached to the header, frame coordinates)
    };
    enum class Tool { Idle, Pending, Pan, Zoom, Resize, ResizeElement, Reorder, RectSelect, Move, Link, CopyDrag, ExportDrag };

    void RequestFile(const FileDialogOptions &, FileDialogs::Completion);
    CK_ID ToolbarScript() const;

    // The Scripts whose header is selected, else ToolbarScript.

    std::vector<CK_ID> ToolbarScripts() const;

    void SyncViews(ImFont *font);
    ElementAddress CapturePosition(uint32_t, const ElementRef &);
    bool ValidPosition(const ElementAddress &) const;
    bool ReadModel(ScriptView &, const EditorField &, ImFont *);
    void ApplyNavigation(const ElementAddress &, const std::vector<ObjectRef> &, bool, bool);
    std::vector<ObjectRef> m_AllScripts, m_VisibleScripts, m_FocusParents;
    std::vector<EditorField> m_ScriptRows;
    uint64_t m_Topology = 0, m_ContentRevision = 0, m_ScriptsRevision = 0, m_ModelRefresh = 1;
    bool m_SceneExcluded = false;

    ScriptView *ViewFor(CK_ID script);
    ScriptView *ViewAtScreen(const ImVec2 &screen);
    void UpdateTransforms();
    void PanBy(float dx, float dy); // screen pixels: stack vertically, every body horizontally
    void AutoScroll(const ImVec2 &canvasMin, const ImVec2 &canvasMax);
    void Scrollbars(const ImVec2 &canvasMin, const ImVec2 &canvasMax, bool horizontal, bool vertical);
    double m_AutoScrollTime = 0.0;
    ImFont *m_Font = nullptr; // the font of the last Draw (printing)
    void ZoomScript(ScriptView &view, float scale);
    ScriptView *BoundaryAt(const ImVec2 &screen); // band whose bottom edge is under the mouse
    Hit HitAt(const ImVec2 &mouse);
    // The workspace's instance hit test. The query supplies candidates in pick order.
    EditorStatus PickObjectsAt(const ImVec2 &mouse, EditorSink);
    ImVec2 m_PickMin, m_PickMax; // the canvas drawn last
    int m_PickFrame = -1;
    void ApplyFocus(const ImVec2 &size);
    void HandleInput(const ImVec2 &canvasMin, const ImVec2 &canvasMax, bool hovered, bool focused);
    bool BeginGeometry(CK_ID script, uint64_t mode, const std::vector<ElementRef> &, const ElementRef &primary = {});
    bool UpdateGeometry(EditorField);
    void BeginMove();
    void UpdateMove(const Point &world);
    void EndMove(bool commit);
    void ContextMenu();
    void CommentEditor();
    void Tooltip(const Hit &hit);
    bool IsSelected(CK_ID script, const ElementRef &ref) const { return m_Navigation.selection.Contains(script, ref); }
    void Click(const Hit &hit);
    void DoubleClick(const Hit &hit);
    // Link endpoint named by a hit: pins, Start, local parameters, shortcuts.
    CK_ID SelectedGraph();
    bool EndpointOf(const Hit &hit, LinkEndpoint &endpoint, bool &behaviorLink, Point &position) const;
    ElementAddress EndpointAddress(CK_ID script, const LinkEndpoint &);
    void EndLink(const Hit &hit);
    // Links from the selected pins of the pressed kind (SchematicLinking.cpp); false: not a multi link.
    void RetainLinkSources();
    void ContinueConnection(bool clearSelection = false);
    // Shift+drag of pins: export, re-import, border position (SchematicLinking.cpp).
    void BeginExportDrag();
    void UpdateExportDrag(const ImVec2 &mouse);
    void EndExportDrag(const ImVec2 &mouse);
    // Moved locals dropped across a graph border export (re-import) their input.

    void AcceptBuildingBlockDrop();
    CK_ID GraphAt(ScriptView &view, const Point &local) const;
    void Report(const EditResult &result, const char *what);
    void RenameDialog();
    void DelayDialog();
    void OpenLinkDelay(CK_ID script, const ElementRef &);
    void LocalParameterDialog();
    void OperationDialog();
    // Dev's Edit Parameters (Dev.exe sub_582930: a Building Block's or a
    // parameter operation's inputs) and Edit Parameter (sub_589D50: a pin,
    // a local or an output: its name, its type, its value).
    void ParametersDialog();
    void PriorityDialog();
    void ConfirmGraphDeletion();
    void BlockMenus();
    // parameter: the pin (CKParameterIn or Out), the local or the output edited.
    void OpenValueDialog(CK_ID script, ObjectRef parameter);
    void OpenParameterEditor(ObjectRef script, ObjectRef object, unsigned mode);
    void InsertBlockAt(const CKGUID &guid, CK_ID script, const Point &local);
    void BeginCopyDrag();
    void EndCopyDrag(const Point &world);
    void Toolbar();
    // Context menus and their commands (SchematicMenus.cpp).
    const ParamItem *FindParam(const ScriptView &view, const ElementRef &ref) const;
    void SetParamStyle(CK_ID script, const ParamItem &param, ParameterStyle style);
    void ToggleCommentIcon(CK_ID script, const ElementRef &ref);
    void SpaceAction();
    void BeginDrawGraph();
    void EndDrawGraph(ScriptView &view, const Rect &local);
    void AddThisAt(CK_ID script, const Point &local);
    void GoToShortcutSource(ObjectRef source);
    void OpenHtmlHelp(CK_ID behavior);
    void SortScripts(ScriptSort sort);
    void OpenQuickAdd(CK_ID script, const Point &local);
    void CkPropertiesDialog();
    void ShortcutAnalysisDialog();
    void QuickAddDialog();
    void ScriptColorDialog();
    void CommonEditItems(bool rename, bool saveAs);
    void ContextMenuItems();
    // Dev "Edit Settings" (S) of the selected Building Block.
    void OpenSettingsDialog();
    // Parameter operation links (Dev): choose the conversion of an
    // incompatible link, change it, or insert a visible operation in a link.
    enum class OperationChoice { NewLink, ReplaceConversion, InsertInLink };
    void OpenOperationChoice(OperationChoice mode, CK_ID script, const LinkEndpoint &from, const LinkEndpoint &to,
                             const ElementRef &link = ElementRef());
    void OperationChoiceDialog();
    void ApplyOperationChoice(const EditorField &choice);
    // Dev M: replace the selected operation, keeping its links where the types allow.
    void OpenModifyOperation();
    // Shortcut sources, in every Script of the composition, as one undo step.
    // In every Script, or (scripts given) in those only.
    void ConvertToShortcutSource(ObjectRef source, const std::vector<CK_ID> &scripts = {});
    // .nms behavior files (Dev Save As... Alt+S, Import Behavior Graph).
    void SaveSelectionAsNms();          // selected behaviors, else the current Script
    void FindDialog();
    bool m_OpenFind = false, m_FindClosed = false, m_FindHidden = false, m_FindBackward = false;
    std::string m_FindText;
    CK_ID m_FoundScript = 0;
    ElementRef m_FoundRef;
    void ImportBehaviorFile(CK_ID script, const Point &local);
    void ConfirmScriptDeletion();
    // Dev "Replacing BBs and BGs" (SchematicMenus.cpp).
    void OpenReplaceBlock(ObjectRef script, ObjectRef behavior, const CKGUID &prototype);
    void ReplaceBlockDialog();
    bool LoadReplacements(ObjectRef);
    // Script Debugger: breakpoints of the selection (F9), their conditions
    // (Shift+F9), Execute Script(s) (Alt+X).
    void ToggleBreakpointOfSelection();
    void BreakpointItems(); // inside a context menu
    bool SelectionBreakpoint(SchematicBreakpoint &) const;
    std::vector<SchematicBreakpoint> Breakpoints() const;
    void OpenBreakpointConfig();
    // Loads a breakpoint's conditions into the Breakpoints Configuration.
    void LoadBreakpointConfig(const SchematicBreakpoint &);
    void BreakpointConfigDialog();
    void BreakpointListDialog();
    // Invalid Link Explorer's Delete: the links' records go (one undo step), the list found again.
    void ExecuteSelectedScripts();
    using ConditionRow = BreakpointCondition;
    // Dev "Pick Schematic Parameter in Schematic" / "Pick Reference Parameter
    // in Schematic": the next click names the parameter of this condition row.
    void PickConditionParameter(ObjectRef parameter);
    // The parameter a breakpoint condition can pick at hit: a local, a
    // shortcut's, a behavior's or an operation's parameter pin (0: none).
    ObjectRef ConditionParameterAt(const Hit &hit);
    void LoadSnapshotFor(CK_ID script, const std::string &path = {});
    void SetSnapshotPixels(ObjectRef script, const std::vector<unsigned char> &, unsigned width, unsigned height);
    void ReplaceGraphDialog();
    void ReplaceWithFile(CK_ID script, CK_ID graph);
    bool IsBuildingBlockAt(const Hit &hit) const;
    CKGUID m_ReplaceGuid;
    bool m_ReplaceAll = false;
    // Dev "Message - Sender and Receiver Retrieval" (H).
    EditorClient Client() const { return {m_EditorHost, m_Instance}; }
    bool DiagnosticOpen(bool invalid) const;
    void ToggleDiagnostic(bool invalid);
    const EditorHost *m_EditorHost = nullptr;
    std::vector<EditorField> m_InvalidRows;
    uint64_t m_InvalidRevision = 0;
    enum class GraphFollow { None, Created, ClearSelection, Geometry, Comment, Block, FileCreated, FileReplaced };
    struct GraphCommand {
        EditorOperation operation; DocumentToken document; ObjectRef script, owner; GraphFollow follow;
        std::vector<ElementAddress> selection;
        uint64_t documentRevision = 0;
    };
    std::vector<GraphCommand> m_GraphCommands;
    EditorResponse GraphState(uint32_t script, uint32_t object = 0);
    EditorResponse ObjectInfo(CK_ID script, CK_ID object);
    EditorResponse ElementInfo(CK_ID script, const ElementRef &);
    std::vector<EditorField> AuthoringArguments(CK_ID script, CK_ID owner, const Point &at = {});
    void ExecuteAuthoring(const char *, std::vector<EditorField>, GraphFollow = GraphFollow::None);
    void LoadParameterTypes();
    void RememberBlocks(const std::vector<uint64_t> &);
    std::vector<uint64_t> RecentBlocks();
    BuildingBlockData m_BlockCatalog;
    std::vector<ObjectRef> ScriptReferences(const std::vector<uint32_t> &) const;
    void Execute(const char *, const std::vector<EditorValue> & = {}, GraphFollow = GraphFollow::None,
                 ObjectRef script = {}, ObjectRef owner = {});
    void ExecuteGraph(const char *command, uint32_t script, uint32_t object, const char *objectKey,
                      std::vector<EditorField> extra = {}, GraphFollow follow = GraphFollow::None);
    void CompleteGraph(const EditorResponse &, const GraphCommand &);
    void PollGraphCommands();
    void BeginPickShortcutSource(ObjectRef from);
    void RetargetAllShortcuts(ObjectRef from, ObjectRef to);
    // Dev Shift+Q / Ctrl+Q / Alt+Q: initial conditions of the selected (else current) Scripts.
    void InitialConditionsOfScripts(int action);
    // The selected variable Building Block (Construct), or 0.
    CK_ID SelectedVariableBlock(PinKind kind);
    // Dev P: links of the selection switch between direct (no control point) and best path.
    void UpdateLinkPaths();

    // Comment edges and the bottom-right corner of an expanded graph resize them.
    bool ResizeHandleAt(ScriptView &view, const Point &local, ElementRef &ref, int &edges) const;
    void SelectAdjacentScript(int step); // Shift+Up/Down
    void MoveHighlightedScript(int step); // Ctrl+Up/Down with a Script selected
    void ScrollToEnd(bool end);           // Home / End
    void BodyMenu(ScriptView &view, const Point &local);
    void ScriptHeaderMenu(ScriptView &view);
    void BehaviorMenu(ScriptView &view, const BehaviorItem &behavior);
    void ParameterMenu(ScriptView &view, const ParamItem &param);
    void LinkMenu(ScriptView &view, const LinkItem &link, const Point &local);
    bool BlockCategoryMenu(CKGUID &chosen); // inside an open menu: the Building Block categories

    // What names the open composition's objects (CK gives their ids to the
    // next document's objects): the Script last clicked and the one under
    // the mouse, the drags and gestures, the pick modes, the dialogs' targets
    // and their open requests. Made anew when the composition is closed
    // (DocumentClosed); a field naming an object goes here.
    struct DocumentState {
        ElementAddress editAddress;
        CK_ID currentScript = 0; // last Script clicked or selected (Dev's dark header)
        CK_ID mouseScript = 0;
        Point mouseLocal; // last Script-local mouse position over mouseScript
        ElementRef hovered;
        CK_ID hoveredScript = 0;
        ElementRef tooltipRef;
        // Script Debugger breakpoint conditions (Shift+F9); Dev "Pick
        // Schematic Parameter in Schematic" / "Pick Reference Parameter in
        // Schematic": the next click names the parameter of this condition row.
        bool openBreakpointConfig = false;
        SchematicBreakpoint breakpoint;
        EditorOperation breakpointCommit = 0, breakpointListCommit = 0;
        std::vector<ConditionRow> conditions;
        std::vector<BreakpointParameter> conditionParameters;
        int pickConditionRow = -1;
        bool pickReference = false;
        int conditionPick = -1;                // the condition row selected
        int editRow = -1, editColumn = -1;     // the cell double-clicked
        std::string editText;
        // Script Debugger's Add Breakpoint: the next click toggles a breakpoint.
        bool pickBreakpoint = false;
        // Dev's Breakpoints (dialog 671): the list as it will be at Ok.
        bool openBreakpointList = false;
        std::vector<SchematicBreakpoint> listBreakpoints, originalBreakpoints;
        int listPick = -1;
        ObjectRef pickShortcutFrom{}; // Set Shortcut Source: the next click picks the new source
        // Drags and gestures.
        Hit press;
        CK_ID zoomScript = 0;   // Zoom Mode drag
        CK_ID resizeScript = 0; // band boundary dragged (Tool::Resize)
        CK_ID reorderScript = 0; // Tool::Reorder
        ElementRef resizeRef;   // Tool::ResizeElement: comment or expanded graph
        CK_ID moveScript = 0;
        bool moveChanged = false;
        EditorEdit gesture = 0;
        EditorField geometryValue;
        std::vector<ElementAddress> geometrySelection;
        uint64_t geometryRevision = 0;
        CK_ID linkScript = 0;
        std::vector<ElementAddress> linkSources;
        ElementAddress linkAddress, linkFixedAddress;
        bool linkReverse = false;
        std::vector<EditorField> connection;
        LinkEndpoint linkFrom;
        CK_ID relinkLink = 0; // Alt+drag of a linked I/O moves this link's end
        ElementRef relinkRef;
        CK_ID exportScript = 0; // Shift+drag export
        std::vector<PinItem> exportPins;
        std::vector<ElementAddress> exportAddresses;
        CK_ID copyScript = 0; // Shift+drag copy
        std::vector<EditorField> copySelection;
        Rect copyBounds;
        // Menus and dialogs: their targets and open requests.
        Hit menuHit;
        // Ctrl + right-click on a local with Script headers selected: those
        // Scripts (Convert to Shortcut Source in them only).
        std::vector<CK_ID> menuScripts;
        bool openCommentEditor = false;
        ElementAddress commentAddress;
        CK_ID dialogScript = 0; // target Script, graph and Script-local position
        std::vector<EditorField> creation;
        bool openReplaceBlock = false;
        bool openReplaceGraph = false;
        ObjectRef replaceScript{};
        ObjectRef replaceBehavior{}; // the Building Block menus replace it (Shift+middle/right click)
        EditorOperation replaceOperation = 0;
        CKGUID replacePrototype = CKGUID();
        ObjectRef replaceSource{};
        EditorResponse replacement;
        std::vector<EditorField> replacementGraphs;
        bool openOperationChoice = false;
        std::vector<EditorField> choices;
        bool openDeleteScript = false;
        bool openDeleteGraphs = false;
        std::vector<ObjectRef> deleteScripts;
        ObjectRef modifyOperation{}; // the Add Parameter Operation dialog replaces this operation
        bool openCkProperties = false;
        ObjectRef ckPropertiesObject{};
        bool openShortcutAnalysis = false;
        ObjectRef shortcutSource{};
        EditorResponse shortcutAnalysis;
        int shortcutPick = -1;
        bool openQuickAdd = false;
        bool openScriptColor = false;
        std::vector<ObjectRef> colorScripts;
        bool openRename = false;
        CK_ID renameObject = 0;
        bool openDelay = false;
        ObjectRef delayTarget{};
        bool openLocalPicker = false;
        bool openOperationPicker = false;
        bool openBlockMenu = false;
        bool openRecentMenu = false;
        CK_ID blockMenuScript = 0;
        Point blockMenuLocal;
        std::vector<EditorField> blockCreation, quickCreation;
    };

    EditorState &m_State;
    EditorView m_Instance = 0;
    DocumentToken m_Document{};
    SchematicNavigation m_Navigation;
    ElementAddress m_PressAddress, m_MenuAddress, m_FoundAddress;
    // Published outside read-only view queries.
    std::vector<ObjectRef> m_QuerySelection, m_QueryScripts;
    ObjectRef m_QueryScript{};
    ElementRef m_QueryElement;
    std::uint64_t m_StackRevision = 0;
    std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t> m_QueryVersion{};
    void PublishNavigation();
    bool m_MenuStale = false;
    void RetainHit(const Hit &, ElementAddress &);
    std::vector<ScriptView> m_Views;
    ViewTransform m_View;
    bool m_ViewInitialized = false;
    DocumentState m_Doc;
    ImVec2 m_CanvasSize;
    Tool m_Tool = Tool::Idle;
    ImGuiMouseButton m_DragButton = ImGuiMouseButton_Left;
    ImVec2 m_PressScreen;
    Point m_PressWorld;
    std::vector<std::pair<CK_ID, float>> m_PendingZoom;
    ImVec2 m_MenuRequest;
    bool m_MenuRequested = false;
    int m_MenuDelay = 0;
    OperationChoice m_ChoiceMode = OperationChoice::NewLink;
    int m_ChoicePick = 0;
    bool m_DrawGraphMode = false; // Dev G: the next drag draws a behavior graph
    std::string m_QuickAddText;
    int m_QuickAddPick = 0;
    float m_ScrollX = 0.0f; // horizontal scroll of every body (frame units, >= 0)
    float m_ResizeTop = 0.0f;
    int m_ResizeEdges = 0;  // 1 right, 2 bottom
    Rect m_ResizeStart;     // its rectangle at the press (Script-local)
    float m_ZoomStart = 1.0f;
    bool m_SpacePanned = false;
    // Move tool
    // The move or resize going on: one undo step when the mouse is released,
    // taken back by Escape. Its Script's data is taken (ScriptData) when it
    // first changes, so a click that moves nothing records nothing.
    // Context menu / Comment editor targets
    double m_TooltipSince = 0.0;
    // Link tool
    bool m_LinkBehavior = false;
    Point m_LinkFromPoint;
    bool m_LinkMode = false;  // L: link with clicks while auto-link is off
    bool m_LinkClick = false; // the current link follows the mouse until the next click
    // The control points clicked while linking (documentation "Link Mode":
    // "each click will create a control point until you click to the final
    // item"), Script-local; the link takes them.
    std::vector<Point> m_LinkPoints;
    int m_LinkEndedFrame = -1; // L ended a link this frame: not the link mode's L too
    // Its display data takes the control points (from: where the link began).

    // Ctrl+V of parameters (and operations) without behaviors.
    void PasteClipboard(bool shortcuts);
    std::vector<EditorField> SelectedPositions();
    // New locals of the parameters' name, type and value, and new operations
    // of the operations' kind, each at its point (in the open graph there).
    // A pIn's source replaced by a new link (documentation "Invalid or
    // existing pLinks"): asked first; a local linked to it alone goes with it.
    bool m_OpenReplaceSource = false;
    std::string m_ReplaceInputName;
    void ReplaceSourceDialog();
    // Shift+drag export
    // Shift+drag copy
    // Dialogs: target Script, graph and Script-local position.
    void CloseValueInputs();
    void ClosePropertyInput();
    bool BeginPropertyInput(const char *, ObjectRef, const std::vector<EditorField> & = {});
    EditorEdit m_PropertyEdit = 0;
    EditorView m_PropertyOwner = 0;
    ObjectRef m_RenameTarget{};
    EditorEdit m_RenameEdit = 0;
    EditorView m_RenameOwner = 0;
    std::string m_PickerFilter;
    std::string m_LocalName;
    // The type dialog adds a local or a graph pin using captured creation arguments.
    enum class TypePurpose { Local, GraphInput, GraphOutput };
    TypePurpose m_TypePurpose = TypePurpose::Local;
    struct TypeChoice {
        uint64_t type;
        std::string name;
    };
    std::vector<TypeChoice> m_Types;
    int m_TypeIndex = -1;
    // Add / Modify Parameter Operation: the parts (input 1, input 2, output,
    // operation; Dev keeps them between openings), their locks, their lists.
    uint64_t m_OperationParts[4] = {(uint64_t(CKPGUID_NONE.d1) << 32) | CKPGUID_NONE.d2,
                                    (uint64_t(CKPGUID_NONE.d1) << 32) | CKPGUID_NONE.d2,
                                    (uint64_t(CKPGUID_NONE.d1) << 32) | CKPGUID_NONE.d2, 0};
    bool m_OperationLocks[4] = {false, false, false, false};
    std::vector<TypeChoice> m_OperationChoices[4];
    std::string m_OperationNames[4];
    bool m_OperationValid = false;
    bool m_OperationChoicesDirty = true;
    void RefreshOperationChoices();
    // Copied parameter fields and the one pending dialog command.
    std::unique_ptr<SchematicParameterDialog> m_ParameterDialog;
    bool m_OpenPriority = false;
    std::vector<ObjectRef> m_PriorityTargets;
    // Building Block menus (middle click: categories; Shift+right click: recent)
};

} // namespace vdev
