#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"

#include "ui/app/EditorState.h"
#include "core/level/HierarchyTree.h"
#include "ui/controls/ParameterBox.h"

namespace vdev {

// Dev's managers that are not Setups (screenshots the user gave): the
// Attribute Manager, the Add Attribute dialog (Setup Attribute page "Add
// Attribute", Level Manager "Add Attributes") and the Hierarchy Manager.
class ManagerWindows {
public:
    explicit ManagerWindows(EditorState &state) : m_State(state) {}

    // Draws the windows that are shown and the dialog when it was requested;
    // dock: where a window opened for the first time docks; the dialog
    // waits while openDialog is false (focus changes close popups).
    void Draw(ImGuiID dock, bool openDialog = true);
    // Where the Hierarchy Manager docks first (Dev: with the Building Blocks).
    ImGuiID hierarchyDock = 0;
    // The composition is closed: the ids kept go (EditorPanels::DocumentClosed).
    void DocumentClosed();
    // Dev's Add Attribute dialog for the objects (none: it only creates
    // types; more: with More>>), opened by Draw once it may (OpenAddAttribute).
    void RequestAddAttribute(const std::vector<CK_ID> &targets, bool more = false) {
        m_Doc.addAttributeTargets = targets;
        m_Doc.addAttributeMore = more;
        m_Doc.openAddAttribute = true;
    }
#ifdef VIRTOOLSDEV_UI_TESTS
    // A Hierarchy Manager row drawn at the last frame, wholly in the list
    // (the UI tests click it): its object (0: a root), name (ANSI), class,
    // depth, its children, whether open and selected; on screen its
    // rectangle, its arrow's and its Render Priority cell's (both empty
    // without them). None when the Hierarchy Manager was not drawn.
    struct ShownRow {
        CK_ID object = 0;
        std::string name;
        CK_CLASSID cid = 0;
        int depth = 0;
        bool children = false, open = false, selected = false;
        ImVec2 min, max, arrowMin, arrowMax, priorityMin, priorityMax;
    };
    std::vector<ShownRow> ShownHierarchyRows() const {
        if (ImGui::GetFrameCount() - m_HierarchyPickFrame > 1) return {};
        return m_HierarchyShownRows;
    }
#endif

private:
    void AddAttributeDialog();
    void AttributeTree(const char *id, const std::vector<CK_ID> &targets, bool multiple, float height);
    void CreatePanel();
    void AttributeManager(bool *open);
    // HierarchyManager.cpp
    void HierarchyManager(bool *open);
    void HierarchyDrop(CK_ID parent, int background);

    // What names the open composition's objects (CK gives their ids to the
    // next document's objects): made anew when it is closed; a field naming
    // an object goes here.
    struct DocumentState {
        CK_ID valueObject = 0; // Attribute Manager: the object whose value is edited
        CKAttributeType valueType = -1; // and its type (several types listed)
        // Attribute Manager: the objects selected in the right list.
        std::set<CK_ID> attributeRows;
        // Hierarchy Manager: the items selected (kept with the 3D Layout's
        // selection), the Shift anchor, the items open (roots by key), the
        // edits in place (the name, the Render Priority).
        std::set<CK_ID> hierarchySelection;
        SelectionFollower hierarchyFollower;
        CK_ID hierarchyAnchor = 0;
        std::set<CK_ID> hierarchyOpen;
        bool hierarchyStarted = false;
        CK_ID hierarchyRename = 0;
        CK_ID zEditObject = 0;
        // Add Attribute: the objects receiving the attributes, the dialog to
        // open (with More>>).
        std::vector<CK_ID> addAttributeTargets;
        bool openAddAttribute = false;
        bool addAttributeMore = false;
    };

    EditorState &m_State;
    DocumentState m_Doc;
    // Add Attribute dialog
    std::set<CKAttributeType> m_Chosen;
    bool m_More = false;
    char m_CategoryName[128] = "";
    char m_TypeName[128] = "";
    int m_TypeParameter = -1; // CKParameterType, -1: none
    std::string m_TypeCategory;
    // Attribute Manager
    CKAttributeType m_Selected = -1;
    // Documentation "Attributes Manager": the objects of "the attribute(s)
    // currently selected"; the types added with Ctrl + click (m_Selected
    // the one clicked last, which Copy and Paste take).
    std::set<CKAttributeType> m_MoreTypes;
    // Double-clicking a value opens Dev's Edit Parameter on it.
    ParameterBox m_ValueBox{m_State};
    std::string m_ValueType;
    bool m_OpenValue = false;
    bool m_ConfirmRemoveUser = false;
    // The type or the category renamed in place (Rename), its text; the
    // category whose Delete waits for its confirmation.
    CKAttributeType m_RenameType = -1;
    std::string m_RenameCategory;
    char m_RenameBuffer[256] = "";
    bool m_RenameFocus = false;
    std::string m_DeleteCategory;
    // The split bar's place (Dev's VISplitBar between the two lists, 2
    // pixels): the left list's width; 0 until the first frame (half).
    float m_AttributeSplit = 0.0f;
    std::string m_ValueText;
    // Hierarchy Manager: the items selected (the 3D Layout's selection, its
    // revision seen), the Shift anchor, the items open (roots by key), the
    // resources shown, the edits in place (the name, the Render Priority).
    HierarchyOptions m_HierarchyOptions;
    char m_HierarchyRenameBuffer[256] = "";
    bool m_HierarchyRenameFocus = false;
    ImGuiID m_Dock = 0;
    int m_ZEditValue = 0;
    bool m_ZEditFocus = false;
    // The rows shown last (screen rectangles), the parameter UIs' target
    // button's (Dev.exe sub_40C0F0, message 0x607: the row's object).
    struct PickRow {
        float x0, y0, x1, y1;
        CK_ID object;
    };
    std::vector<PickRow> m_HierarchyPickRows;
#ifdef VIRTOOLSDEV_UI_TESTS
    std::vector<ShownRow> m_HierarchyShownRows;
#endif
    int m_HierarchyPickFrame = -1;
    // A drag refused (body parts, different depths): told once a drag.
    bool m_HierarchyDragRefused = false;
};

} // namespace vdev
