#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "ui/app/EditorState.h"
#include "ui/controls/ParameterBox.h"
#include "core/graph/TopologyCommands.h"
#include "core/schematic/SchematicSearch.h"
#include "core/document/ToolCommands.h"
#include "ui/controls/ValueEditor.h"

namespace vdev {

class SchematicCanvas;

class EditorPanels {
public:
    EditorPanels(EditorState &state, SchematicCanvas &canvas) : m_State(state), m_Canvas(canvas) {}

    void ScriptsPanel(bool *open);
    void BuildingBlocksPanel(bool *open);
    void ExplorerPanel(bool *open);
    void LogPanel(bool *open);
    // Dev Script Debugger (DebuggerPanel.cpp).
    void DebuggerPanel(bool *open);

    // Shows the Scripts of the given Script's owner (Dev shows the Scripts of
    // the selected object) and centers the view on it.
    // Shows the Script's stack (all Scripts, or the owner's); focus scrolls
    // the Schematic to it (opening a file starts at the top of the stack).
    void ShowScript(CK_ID script, const ElementRef &ref = ElementRef(), CK_ID focusBehavior = 0, bool focus = true);
    // The stack made again (the Scripts or the filters changed), for the
    // Script it was made for while it exists.
    void RefreshStack();
    void FocusExplorer() { m_FocusSearch = true; }
    // Opens every node of the Level Manager (its Expand button; --expand-level).
    void ExpandLevelTree() { m_LevelTreeOpen = 1; }
    // Rows selected in the Level Manager (objects and Scripts).
    const std::set<CK_ID> &LevelSelection() const { return m_Doc.levelSelection; }
    // F3 / Shift+F3: show the next / previous Explorer result.
    void FindNext(int direction);
    // The Schematic Explorer's results of its last search.
    const std::vector<SearchResult> &SearchResults() const { return m_Doc.results; }
    // The Delete dialog for objects (the 3D Layout's Delete); drawn by
    // DeleteObjectsDialog, which the application calls each frame.
    void RequestDeleteObjects(const std::vector<CK_ID> &objects) {
        m_Doc.deleteObjects = objects;
        m_Doc.openDeleteObjects = true;
    }
    void DeleteObjectsDialog();
    // Show Object/Script In Level Manager: the tree opened to the object's
    // row, scrolled to it and highlighted, when it draws next (ShowInLevelManager).
    void FocusLevelRow(CK_ID object) { m_Doc.levelFocus = object; }
    // A Building Block dropped onto a row asks for its inputs as a 3D Layout
    // drop does (LayoutWindow::DropAsking).
    std::function<std::pair<CK_ID, CK_ID>(const std::function<std::pair<CK_ID, CK_ID>()> &drop)> dropAsking;
    // The composition is closed: the ids kept (rows, results, a dialog's
    // object) name objects of the next one after ClearAll.
    void DocumentClosed();
    // A Level Manager row drawn at the last frame, wholly in the list (the
    // UI tests click it): its object (0: a folder), name, class, depth, its
    // children, whether open and selected; on screen its rectangle, its
    // arrow's, its Visible cell's, its Activate At Scene Start and Active Now
    // cells' (empty without them).
    struct ShownRow {
        CK_ID object = 0;
        std::string name;
        CK_CLASSID cid = 0;
        int depth = 0;
        bool children = false, open = false, selected = false;
        ImVec2 min, max, arrowMin, arrowMax, eyeMin, eyeMax, activateMin, activateMax, activeMin, activeMax;
    };
    // None when the Level Manager was not drawn.
    std::vector<ShownRow> ShownLevelRows() const;

private:
    void SelectRow(CK_ID id);
    // The rows follow the 3D Layout's selection and set it (its render objects).
    void SyncObjectSelection();
    void FlagClick(CK_ID id, int column, CKScene *scene);
    void RowMenu(CK_ID id, CK_ID script, CK_ID group = 0);
    void CreateWorkset(int parent);
    void WorksetMenu(int workset);
    std::vector<CK_ID> WorksetScripts(int workset) const;
    // The rows drawn last (their visible part on screen), the parameter UIs'
    // target button's (Dev.exe sub_405700, message 0x607: the row's object).
    struct PickRow {
        float x0, y0, x1, y1;
        CK_ID object;
    };
    std::vector<PickRow> m_PickRows;
    std::vector<ShownRow> m_ShownRows;
    int m_PickFrame = -1;
    // Level Manager commands on the selected rows (toolbar, keys, menu).
    std::vector<CK_ID> LevelTargets() const { return std::vector<CK_ID>(m_Doc.levelSelection.begin(), m_Doc.levelSelection.end()); }
    void CreateScripts();
    void LoadScriptOn(CK_ID object);
    void RenameDialog();
    void SaveObjectsDialog();
    void AttributeMenu(const std::vector<CK_ID> &targets); // inside a menu
    CKDependencies m_DeleteOptions; // the dialog's copy of the Delete Options
    std::string m_RenameText;
    void Report(const EditResult &result, const char *what);
    int m_LevelTreeOpen = -1; // Level Manager expand (1) / collapse (0) request
    // Expand / Collapse Selected (Dev: "the selected branch"): the rows
    // selected and those under them; nothing selected, every row.
    int m_LevelBranchOpen = -1;
    // The order of the rows (a header clicked: Name, Priority; Dev's
    // documentation "Sorting Level Manager by Priority"); again: the other way.
    int m_LevelSort = 0; // 0 name, 1 priority
    bool m_LevelSortReversed = false;
    bool LevelLess(CKObject *a, CKObject *b) const;
    // A Priority cell double-clicked: its value edited (Dev.exe sub_569960).
    CK_ID m_PriorityObject = 0;
    int m_PriorityValue = 0;
    bool m_OpenPriority = false;
    void PriorityDialog();
    // Rows copied with Shift held while they are dropped (documentation
    // "Moving and Copying in Level Manager"): after the Copy options, the
    // copies go into the container (a place, a scene, a group; the level).
    std::vector<CK_ID> m_CopyObjects;
    CK_ID m_CopyContainer = 0;
    bool m_OpenCopyOptions = false;
    void CopyOptionsDialog();
    // An entity's "Save as..." (documentation "Level Manager Shortcut
    // Menu"): a .nmo, Hide script representation in Schematic view asked.
    void SaveAsDialog();
    bool m_OpenSaveAs = false;
    bool m_SaveAsHide = false;
    std::string m_SaveAsName;
    // Scripts deleted from the Level Manager (Delete on Script rows), after
    // Dev's "Confirm Script(s) Deletion".
    void DeleteScriptsAsked(const std::vector<CK_ID> &scripts);
    // The tree's columns as shown (Dev.exe sub_5687A0, in Dev pixels):
    // Show/Hide, Name, IC, Visible, Activate At Scene Start, Reset At Scene
    // Start, Active Now, Priority, the last one; their header's separators
    // drag them wider or narrower. The rows listed last frame.
    float m_LevelColumns[9] = {20, 250, 30, 50, 135, 135, 90, 50, 100};
    int m_LevelRows = 0;
    // A Data Resources file dropped onto a row this frame (Dev.exe
    // sub_56B120), loaded once the tree is drawn; selected: the row was
    // selected (a behavior file then goes to every selected row).
    struct LevelDrop {
        bool pending = false;
        std::string path;
        CK_CLASSID cid = 0;
        LevelDropTarget target;
        bool selected = false;
    };
    LevelDrop m_LevelDrop;
    void DropOnLevel(const LevelDrop &drop);
    // Rows dragged onto a row this frame (Dev.exe sub_56E790): the row
    // dragged (the selection when it is selected), the target row, its
    // workset (a workset's row) and whether its parent is one.
    struct LevelMove {
        bool pending = false;
        CK_ID dragged = 0;
        LevelDropTarget target;
        int workset = 0;
        bool inWorkset = false;
        bool copy = false; // Shift held: copies
    };
    LevelMove m_LevelMove;
    void MoveOnLevel(const LevelMove &move);
    // A Building Block dropped onto a row this frame (Dev.exe sub_56BAB0):
    // onto a Script into it, onto an object into its first Script shown
    // (else a new one); onto a selected row, each row selected; alone, it
    // asks for its inputs.
    struct LevelBlockDrop {
        bool pending = false;
        CKGUID guid;
        CK_ID row = 0;
        bool selected = false;
        bool ask = true; // the Edit Parameter dialog (one Building Block only)
    };
    LevelBlockDrop m_LevelBlock;
    // An attribute from the Attribute Manager dropped onto a row this frame.
    struct LevelAttributeDrop {
        bool pending = false;
        AttributePayload attribute{};
        CK_ID row = 0;
        bool selected = false;
    };
    LevelAttributeDrop m_LevelAttribute;
    void DropBlockOnLevel(const LevelBlockDrop &drop);
    // The rows drawn (last frame's until the tree is drawn again), in order:
    // Dev's tree keys move through them (the arrows, Page Up / Down, Home /
    // End; Right opens a row or goes to its first child, Left closes it or
    // goes to its parent). The keys' row: m_NavKey while the selection is
    // the one it was given (a folder's: none), else the first row selected.
    struct NavRow {
        CK_ID object = 0, scene = 0, group = 0;
        ImGuiID key = 0;
        int depth = 0;
        bool children = false, open = false;
        std::string label;
    };
    std::vector<NavRow> m_NavRows, m_NavDrawn;
    ImGuiID m_NavKey = 0;
    bool m_NavClicked = false;
    std::set<CK_ID> m_NavSelection;
    int NavCurrent() const;
    void MoveOnScene(CKScene *scene, const std::vector<CK_ID> &objects);
    void MoveOnPlace(CKPlace *place, const std::vector<CK_ID> &objects);
    void MoveOnGroup(CKGroup *group, const std::vector<CK_ID> &objects);
    void MoveScriptsOn(CKBeObject *target, const std::vector<CK_ID> &scripts, const char *refusal);
    // Workset members (Dev.exe sub_578290): objects of the level scene, once.
    void AddToWorkset(int workset, const std::vector<CK_ID> &objects);
    // Dev's Delete by the rows' section (sub_5734A0); Create Group / Place
    // as Dev's (sub_576910, sub_573F10).
    void DeleteLevelRows(const std::vector<CK_ID> &targets);
    // Dev's Ctrl+C / Ctrl+V (Dev.exe sub_5760C0, sub_5757C0): the objects
    // selected, else the Scripts; the objects into the groups, places,
    // scenes selected (the level's section: copies of them), the Scripts
    // onto the objects selected.
    void CopyOnLevel();
    void PasteOnLevel();
    // Find Item (Ctrl+F; F3, Shift+F3 the next, the previous; sub_5796C0):
    // the rows whose name has the text (any case), after the current one,
    // round the tree.
    void FindDialog();
    void FindOnLevel(bool forward);
    std::vector<std::pair<CK_ID, CK_ID>> LevelSearchOrder(); // the rows (object, its section's scene: 0 the level)
    bool m_OpenFind = false;
    std::string m_FindText;
    bool m_FindObjects = true, m_FindScripts = true;
    int m_FindIndex = -1;
    void CreateGroupOf(const std::vector<CK_ID> &targets, const char *what);
    void CreatePlaceOf(const std::vector<CK_ID> &targets, const char *what);
    // A question of the Level Manager (Dev's message box: Yes, No).
    struct LevelQuestion {
        bool open = false;
        std::string title, text;
        std::function<void(bool yes)> answer;
    };
    LevelQuestion m_LevelQuestion;
    void Ask(const std::string &title, const std::string &text, std::function<void(bool yes)> answer);
    void QuestionDialog();
    // Level Manager view state (Dev documentation): the Level / Scene context
    // of the side toolbar, the Show/Hide Layer flags and Toggle Show/Hide.
    bool m_LevelMode = true;
    bool m_HideLayer = false;
    // Dev's Building Blocks Resource: the category tree on the left (path:
    // the category's, "Logics/Calculator"), the Building Blocks of the one
    // selected (and its sub-categories) in columns on the right.
    void DrawCategory(const std::string &name, const std::string &path, const BuildingBlockCategory &node);
    void BuildingBlockItem(size_t index);
    void RunSearch();
    // The Schematic Explorer's parts: the rows chosen made the Schematic's
    // selection; the tree view; the parameters of the Parameter Filter.
    void ExplorerSelect();
    void ExplorerTree(const ImVec2 &min, const ImVec2 &max);
    CKParameter *ExplorerValue(bool replace);
    int m_ExplorerRename = -1; // the row F2 renames (-2: the tree's node)
    std::string m_ExplorerRenameText;

    // What names the open composition's objects (CK gives their ids to the
    // next document's objects): made anew when it is closed; a field naming
    // an object goes here.
    struct DocumentState {
        // Level Manager: the rows selected (kept with the 3D Layout's
        // selection), the layers hidden, the context scene (chosen, and this
        // frame's: the level scene in Level mode).
        std::set<CK_ID> levelSelection;
        SelectionFollower levelFollower;
        std::set<CK_ID> layerHidden;
        CK_ID levelFocus = 0; // FocusLevelRow's, until the tree draws
        CK_ID contextScene = 0;
        CK_ID contextSceneId = 0;
        // Its menu: the row's object and group (Move Up/Down), the group of
        // the last clicked member row, the workset.
        CK_ID levelMenuObject = 0;
        // The Explorer's tree: the node chosen (GoTo, Delete, F2), its Script.
        CK_ID treeScript = 0, treeBehavior = 0;
        CK_ID levelMenuGroup = 0;
        CK_ID selectionGroup = 0;
        CK_ID selectionScene = 0; // the section of the last clicked row (0: the level's)
        int levelMenuWorkset = 0;
        // Delete and Rename waiting for their dialogs.
        bool openDeleteObjects = false;
        std::vector<CK_ID> deleteObjects;
        bool openRename = false;
        CK_ID renameObject = 0;
        int renameWorkset = 0;
        // The Schematic Explorer's results (made again when the query or
        // the composition changes), the rows chosen (Shift's anchor), the
        // Schematic's selection when it opened (Cancel gives it back), the
        // parameters holding the Parameter Filter's value and replace value.
        std::vector<SearchResult> results;
        int currentResult = -1;
        std::set<int> chosen;
        int anchor = -1;
        Selection selectionBefore;
        CK_ID filterValue = 0, replaceValue = 0;
        std::string resultsKey;
        // The Script Debugger's Debug Context combos (the debugger's object
        // and Script, or the ones chosen to lock it to).
        CK_ID debugObject = 0, debugScript = 0;
    };

    EditorState &m_State;
    DocumentState m_Doc;
    ParameterBox m_DeleteBox{m_State}; // after m_State: it keeps the reference
    ParameterBox m_CopyBox{m_State};
    SchematicCanvas &m_Canvas;
    // Schematic Explorer (Dev's Schematic Filter Box, dialog 732): Show in
    // List (Dev's default: Scripts), the CK ID and Text Filters, Parse All
    // Items, the tree view, the Parameter Filter (mode, type, Derived).
    std::string m_SearchText;
    std::string m_SearchIdText;
    unsigned m_SearchKinds = SearchScripts;
    bool m_SearchAllScripts = false;
    bool m_SearchTree = false;
    int m_SearchFilter = 0;
    int m_SearchType = -1;
    bool m_SearchDerived = false;
    bool m_FocusSearch = false;
    bool m_ExplorerShown = false;
    ParameterBox m_ExplorerBox{m_State};
    bool m_ExplorerEditReplace = false;
    bool m_OpenExplorerValue = false;
    // Building Blocks
    std::string m_BlockFilter;
    std::set<size_t> m_BlockSelection; // the Building Blocks selected (catalog indices)
    std::string m_BlockCategory;       // the category listed ("": none)
    std::vector<size_t> m_BlockShown;  // the list's rows in their order (Shift + click: a range)
    size_t m_BlockAnchor = SIZE_MAX;
    // Script Debugger: the Debug Log shown, the edits' text, the reached
    // breakpoints (blinking since, Goto's last one).
    bool m_DebugLogShown = true;
    std::string m_MaxFramesText, m_FpsText;
    int m_DebugReached = 0;
    double m_DebugBlinkStart = -100.0;
    int m_DebugGoto = -1;
    // Log
    bool m_LogPlaced = false; // the Event Log was put at its place this session
    size_t m_LastLogSize = 0;
};

} // namespace vdev
