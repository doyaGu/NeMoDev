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
#include "ui/controls/ParameterBox.h"
#include "ui/controls/BehaviorInfoDialog.h"
#include "ui/controls/ValueEditor.h"
#include "core/schematic/SchematicModel.h"
#include "ui/schematic/SchematicRenderer.h"
#include "core/schematic/SchematicSearch.h"

namespace vdev {

// The Schematic View: a vertical stack of Scripts with pan/zoom, selection
// and direct-manipulation editing. Each Script keeps a model rebuilt when
// its session revision or the document topology changes.
class SchematicCanvas {
public:
    explicit SchematicCanvas(EditorState &state) : m_State(state) {}

    void Draw(const char *title, bool *open);
    void ResetView();
    void FitAll();
    void Invalidate() { m_Views.clear(); }
    // A drag going on (move, resize, link, selection rectangle...): it ends
    // with the data it began with.
    bool Busy() const { return m_Tool != Tool::Idle; }
    // The composition is closed: Set Shortcut Source's next click no longer picks.
    void DocumentClosed() {
        Invalidate();
        m_Doc = DocumentState(); // a gesture going on is taken back (none is: Busy)
    }
    // Rebuilds the models (values shown while the Script Debugger runs).
    void RefreshValues() {
        for (auto &view : m_Views) view.revision = ~0ull;
    }
    void ShowMessageExplorer() { m_ShowMessages = true; }
    // Shows a parameter where the Schematic displays it (Parameter Debugger "Go to source").
    void GoToParameter(CK_ID parameter) { GoToShortcutSource(parameter); }
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
    // The global zoom of the whole Schematic (+ / -), each Script's scale apart.
    float GlobalZoom() const { return m_View.zoom; }
    // The Script Debugger's Breakpoints buttons: Configure Breakpoints (the
    // selection's breakpoint, else the first), Breakpoint List (Dev's
    // Breakpoints), Add Breakpoint (the next click on a behavior, a behavior
    // link or a parameter toggles its breakpoint, Dev.exe sub_5866B0 1187),
    // Goto (the breakpoint's element shown).
    void ConfigureBreakpoints();
    // The Invalid Link Explorer shown (its toolbar icon's).
    void ShowInvalidLinks() { m_ShowLinkErrors = true; }
    void OpenBreakpointList();
    void PickBreakpoint();
    void GoToBreakpoint(CK_ID script, const ExtraData &data);
    // Print Schematic (Dev.exe sub_5380F0): the Scripts shown, stacked from
    // the top at 100% (each with its own zoom). PrintSize: Dev's width (the
    // widest open Script less 100 pixels, at least 140) and the stack's
    // height; false without a Script shown.
    bool PrintSize(float &width, float &height) const;
    // A page: the stack's pixels from (x, y), size of them, drawn scale times
    // into drawList from (0, 0) in Dev's print mode.
    void DrawPrintPage(ImDrawList *drawList, float x, float y, const ImVec2 &size, float scale,
                       const SchematicPrint &print) const;

private:
    struct ScriptView {
        CK_ID id = 0;
        SchematicModel model;
        std::uint64_t revision = ~0ull;
        std::uint64_t topology = ~0ull;
        std::uint64_t viewRevision = ~0ull;
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

    CK_ID ToolbarScript() const;

    // The Scripts whose header is selected, else ToolbarScript.

    std::vector<CK_ID> ToolbarScripts() const;

    void SyncViews(ImFont *font);
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
    // The parameter UIs' target button over the canvas (EditorState::objectPick).
    CK_ID PickObjectAt(const ImVec2 &mouse, CK_CLASSID cid);
    ImVec2 m_PickMin, m_PickMax; // the canvas drawn last
    int m_PickFrame = -1;
    void ApplyFocus(const ImVec2 &size);
    void HandleInput(const ImVec2 &canvasMin, const ImVec2 &canvasMax, bool hovered, bool focused);
    void BeginMove();
    void UpdateMove(const Point &world);
    void EndMove(bool commit);
    void ContextMenu();
    void CommentEditor();
    void Tooltip(const Hit &hit);
    bool IsSelected(CK_ID script, const ElementRef &ref) const { return m_State.selection.Contains(script, ref); }
    void Click(const Hit &hit);
    void DoubleClick(const Hit &hit);
    // Link endpoint named by a hit: pins, Start, local parameters, shortcuts.
    CK_ID SelectedGraph() const;
    bool EndpointOf(const Hit &hit, LinkEndpoint &endpoint, bool &behaviorLink, Point &position) const;
    void EndLink(const Hit &hit);
    // Links from the selected pins of the pressed kind (SchematicLinking.cpp); false: not a multi link.
    bool MultiLink(const Hit &hit);
    // Shift+drag of pins: export, re-import, border position (SchematicLinking.cpp).
    void BeginExportDrag();
    void UpdateExportDrag(const ImVec2 &mouse);
    void EndExportDrag(const ImVec2 &mouse);
    // Moved locals dropped across a graph border export (re-import) their input.
    bool ExportMovedLocals();
    void AcceptBuildingBlockDrop();
    CK_ID GraphAt(ScriptView &view, const Point &local) const;
    void Report(const EditResult &result, const char *what);
    void RenameDialog();
    void DelayDialog();
    void LocalParameterDialog();
    void OperationDialog();
    // Dev's Edit Parameters (Dev.exe sub_582930: a Building Block's or a
    // parameter operation's inputs) and Edit Parameter (sub_589D50: a pin,
    // a local or an output: its name, its type, its value).
    void ParametersDialog();
    void ValueDialog();
    void PriorityDialog();
    void InvalidLinksDialog();
    void ConfirmGraphDeletion();
    bool m_InvalidLinksAnswered = false, m_InvalidLinksAccepted = false;
    void BlockMenus();
    // parameter: the pin (CKParameterIn or Out), the local or the output edited.
    void OpenValueDialog(CK_ID script, CKObject *parameter);
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
    void GoToShortcutSource(CK_ID source);
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
    void ApplyOperationChoice(const OperationSignature &signature);
    // Dev M: replace the selected operation, keeping its links where the types allow.
    void OpenModifyOperation();
    // Shortcut sources, in every Script of the composition, as one undo step.
    // In every Script, or (scripts given) in those only.
    void ConvertToShortcutSource(CK_ID source, const std::vector<CK_ID> &scripts = {});
    // .nms behavior files (Dev Save As... Alt+S, Import Behavior Graph).
    void SaveSelectionAsNms();          // selected behaviors, else the current Script
    // Dev.exe sub_5C01E0: the file chosen, Behavior Information for the
    // first behavior; saved with OK (the .dsc written beside), not with
    // Cancel.
    void SaveBehaviorInfo();
    BehaviorInfoDialog m_SaveInfo;
    void FindDialog();
    int m_MessagePick = -1;  // Message Explorer: the message chosen
    CK_ID m_MessageUse = 0;  // its sender or receiver chosen
    bool m_OpenFind = false, m_FindClosed = false, m_FindHidden = false, m_FindBackward = false;
    std::string m_FindText;
    CK_ID m_FoundScript = 0;
    ElementRef m_FoundRef;
    std::vector<CK_ID> m_SaveBehaviors;
    std::string m_SavePath;
    void ImportBehaviorFile(CK_ID script, const Point &local);
    void ConfirmScriptDeletion();
    // Dev "Replacing BBs and BGs" (SchematicMenus.cpp).
    void OpenReplaceBlock(CK_ID script, CK_ID behavior, const CKGUID &prototype);
    void ReplaceBlockDialog();
    // Script Debugger: breakpoints of the selection (F9), their conditions
    // (Shift+F9), Execute Script(s) (Alt+X).
    void ToggleBreakpointOfSelection();
    void BreakpointItems(); // inside a context menu
    bool SelectionBreakpoint(ExtraDataType &type, CK_ID &object, CK_ID &owner) const;
    void OpenBreakpointConfig();
    // Loads a breakpoint's conditions into the Breakpoints Configuration.
    void LoadBreakpointConfig(CK_ID script, ExtraDataType type, CK_ID object);
    void BreakpointConfigDialog();
    void BreakpointListDialog();
    // Invalid Link Explorer's Delete: the links' records go (one undo step), the list found again.
    void DeleteInvalidLinks(const std::vector<InvalidLink> &links);
    void ExecuteSelectedScripts();
    struct ConditionRow {
        CK_ID parameter = 0;
        int comparison = 0;
        int logical = 0;
        std::string value;
    };
    // Dev "Pick Schematic Parameter in Schematic" / "Pick Reference Parameter
    // in Schematic": the next click names the parameter of this condition row.
    void PickConditionParameter(CK_ID parameter);
    // The parameter a breakpoint condition can pick at hit: a local, a
    // shortcut's, a behavior's or an operation's parameter pin (0: none).
    CK_ID ConditionParameterAt(const Hit &hit) const;
    void LoadSnapshotFor(CK_ID script);
    void ReplaceGraphDialog();
    void ReplaceWithFile(CK_ID script, CK_ID graph);
    bool IsBuildingBlockAt(const Hit &hit) const;
    CKGUID m_ReplaceGuid;
    bool m_ReplaceAll = false;
    // Dev "Message - Sender and Receiver Retrieval" (H).
    void MessageExplorer();
    bool m_ShowMessages = false;
    // Dev's Invalid Link Explorer (dialog 749, EditorState::invalidLinks).
    void LinkErrorsExplorer();
    bool m_ShowLinkErrors = false;
    int m_LinkErrorPick = -1;
    void BeginPickShortcutSource(CK_ID from);
    void RetargetAllShortcuts(CK_ID from, CK_ID to);
    void ReplaceOperation(CK_ID script, CK_ID operation, const OperationSignature &signature);
    // Dev Shift+Q / Ctrl+Q / Alt+Q: initial conditions of the selected (else current) Scripts.
    void InitialConditionsOfScripts(int action);
    void SettingsDialog();
    // The selected variable Building Block (Construct), or 0.
    CK_ID SelectedVariableBlock(PinKind kind) const;
    // Dev P: links of the selection switch between direct (no control point) and best path.
    void UpdateLinkPaths();
    void RouteNewLink(CK_ID script, const LinkEndpoint &a, const LinkEndpoint &b);
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
        CK_ID breakpointScript = 0;
        ExtraDataType breakpointType = EXTRA_DATA_BEHAVIOR;
        CK_ID breakpointObject = 0;
        std::vector<ConditionRow> conditions;
        std::vector<std::pair<CK_ID, std::string>> conditionParameters;
        int pickConditionRow = -1;
        bool pickReference = false;
        int conditionPick = -1;                // the condition row selected
        int editRow = -1, editColumn = -1;     // the cell double-clicked
        std::string editText;
        // Script Debugger's Add Breakpoint: the next click toggles a breakpoint.
        bool pickBreakpoint = false;
        // Dev's Breakpoints (dialog 671): the list as it will be at Ok.
        bool openBreakpointList = false;
        std::vector<Debugger::Breakpoint> listBreakpoints;
        int listPick = -1;
        CK_ID pickShortcutFrom = 0; // Set Shortcut Source: the next click picks the new source
        // Drags and gestures.
        Hit press;
        CK_ID zoomScript = 0;   // Zoom Mode drag
        CK_ID resizeScript = 0; // band boundary dragged (Tool::Resize)
        CK_ID reorderScript = 0; // Tool::Reorder
        ElementRef resizeRef;   // Tool::ResizeElement: comment or expanded graph
        CK_ID moveScript = 0;
        InterfaceData moveOriginal;
        std::vector<ElementRef> moveItems;
        std::vector<std::pair<Rect, Rect>> moveLimits; // Move Item Coherence Test: item, its graph (at the press)
        ElementRef movePrimary;
        bool moveChanged = false;
        // The move or resize going on: one undo step when the mouse is
        // released, taken back by Escape. Its Script's data is taken
        // (ScriptData) when it first changes, so a click that moves nothing
        // records nothing.
        std::unique_ptr<Edit> gesture;
        CK_ID linkScript = 0;
        LinkEndpoint linkFrom;
        CK_ID relinkLink = 0; // Alt+drag of a linked I/O moves this link's end
        ElementRef relinkRef;
        CK_ID exportScript = 0; // Shift+drag export
        std::vector<PinItem> exportPins;
        CK_ID copyScript = 0; // Shift+drag copy
        std::vector<CK_ID> copyBehaviors;
        std::vector<Comment> copyComments;
        // Locals (and shortcuts' sources) and operations copied too, at their place.
        std::vector<std::pair<CK_ID, Point>> copyLocals, copyOperations;
        InterfaceData copySource;
        Rect copyBounds;
        // Menus and dialogs: their targets and open requests.
        Hit menuHit;
        // Edit Parameter changed a type and links no longer fit: OK deletes
        // them, Cancel undoes the edit (Dev's "Invalid Parameter Link").
        std::vector<ElementRef> invalidLinks;
        CK_ID invalidLinksScript = 0;
        // Ctrl + right-click on a local with Script headers selected: those
        // Scripts (Convert to Shortcut Source in them only).
        std::vector<CK_ID> menuScripts;
        bool openCommentEditor = false;
        CK_ID commentScript = 0;
        ElementRef commentRef;
        CK_ID dialogScript = 0; // target Script, graph and Script-local position
        CK_ID dialogGraph = 0;
        Point dialogPosition;
        bool openReplaceBlock = false;
        bool openReplaceGraph = false;
        CK_ID replaceScript = 0;
        CK_ID replaceBehavior = 0; // the Building Block menus replace it (Shift+middle/right click)
        CK_ID replaceSource = 0;
        bool openOperationChoice = false;
        CK_ID choiceScript = 0;
        LinkEndpoint choiceFrom, choiceTo;
        ElementRef choiceLink;
        std::vector<OperationSignature> choices;
        bool openDeleteScript = false;
        bool openDeleteGraphs = false;
        std::vector<CK_ID> deleteScripts;
        CK_ID modifyOperation = 0; // the Add Parameter Operation dialog replaces this operation
        bool openSettings = false;
        CK_ID settingsBehavior = 0;
        std::vector<BehaviorSetting> settings;
        std::map<int, std::string> settingsTexts; // edited texts by slot
        std::vector<int> settingsSlots;            // the parameter box's rows' slots
        bool openCkProperties = false;
        CK_ID ckPropertiesObject = 0;
        bool openShortcutAnalysis = false;
        CK_ID shortcutSource = 0;
        std::vector<SearchResult> shortcutResults;
        int shortcutPick = -1;
        bool openQuickAdd = false;
        CK_ID quickAddScript = 0;
        Point quickAddLocal;
        bool openScriptColor = false;
        std::vector<CK_ID> colorScripts;
        bool openRename = false;
        CK_ID renameObject = 0;
        bool openDelay = false;
        CK_ID delayLink = 0;
        bool openLocalPicker = false;
        bool openOperationPicker = false;
        CK_ID pinOwner = 0;
        bool openParameters = false;
        CK_ID parametersObject = 0; // behavior or operation
        bool openValue = false;
        CK_ID valueParameter = 0;
        CK_ID valueHolder = 0;
        bool openBlockMenu = false;
        bool openRecentMenu = false;
        CK_ID blockMenuScript = 0;
        Point blockMenuLocal;
    };

    EditorState &m_State;
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
    float m_ScriptColor[3] = {0.8f, 0.8f, 0.8f};
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
    std::string m_CommentText;
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
    void SetNewLinkPoints(CK_ID script, const LinkEndpoint &from, const LinkEndpoint &to, const std::vector<Point> &points);
    // Ctrl+V of parameters (and operations) without behaviors.
    void PasteParameters();
    // New locals of the parameters' name, type and value, and new operations
    // of the operations' kind, each at its point (in the open graph there).
    void CopyParametersTo(CK_ID script, const std::vector<std::pair<CK_ID, Point>> &locals,
                          const std::vector<std::pair<CK_ID, Point>> &operations, const char *label);
    // A pIn's source replaced by a new link (documentation "Invalid or
    // existing pLinks"): asked first; a local linked to it alone goes with it.
    bool m_OpenReplaceSource = false;
    CK_ID m_ReplaceScript = 0;
    LinkEndpoint m_ReplaceFrom, m_ReplaceTo;
    std::vector<Point> m_ReplacePoints;
    bool m_ReplaceRoute = false;
    void ReplaceSourceDialog();
    CKParameterIn *InputOf(const LinkEndpoint &endpoint) const;
    void CreateParameterLinkReplacing(CK_ID script, const LinkEndpoint &from, const LinkEndpoint &to,
                                      const std::vector<Point> &points, bool route);
    // Shift+drag export
    // Shift+drag copy
    // Dialogs: target Script, graph and Script-local position.
    std::string m_RenameText;
    int m_DelayValue = 0;
    std::string m_PickerFilter;
    std::string m_LocalName;
    // The type dialog adds a local (graph = m_Doc.dialogGraph) or a graph pin.
    enum class TypePurpose { Local, GraphInput, GraphOutput };
    TypePurpose m_TypePurpose = TypePurpose::Local;
    std::vector<ParameterTypeEntry> m_Types;
    int m_TypeIndex = -1;
    std::vector<OperationEntry> m_Operations;
    // Add / Modify Parameter Operation: the parts (input 1, input 2, output,
    // operation; Dev keeps them between openings), their locks, their lists.
    CKGUID m_OperationParts[4] = {CKPGUID_NONE, CKPGUID_NONE, CKPGUID_NONE, CKGUID()};
    bool m_OperationLocks[4] = {false, false, false, false};
    std::vector<CKGUID> m_OperationChoices[4];
    bool m_OperationValid = false;
    bool m_OperationChoicesDirty = true;
    void RefreshOperationChoices();
    // Parameters / value / priority dialogs: Dev's parameter box, the
    // dialog's title, Apply (a Building Block's, Dev's mode 4); Edit
    // Parameter's parameter, its value holder (0: the value is only shown),
    // its name and type as typed / chosen and whether they may change.
    ParameterBox m_ParameterBox{m_State};
    std::string m_ParametersTitle;
    bool m_ParametersApply = false;
    std::string m_ValueName;
    CKGUID m_ValueType;
    bool m_ValueNameEditable = false;
    bool m_ValueTypeEditable = false;
    bool m_OpenPriority = false;
    int m_PriorityValue = 0;
    // Edit Priority with Script headers selected: those Scripts' priority.
    std::vector<CK_ID> m_PriorityScripts;
    // Building Block menus (middle click: categories; Shift+right click: recent)
};

} // namespace vdev
