#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"

#include "ui/app/EditorState.h"
#include "core/catalog/ParameterUIs.h"

namespace vdev {

// Dev's parameter box: a row per parameter, CK2UI.dll's wrapper (dialog
// 2000: the target button, the name in bold, then the type's UI on the
// lighter part) after the type's own UI (ParameterUIOf). As Dev's UIs do
// (CK2UI sub_24E64940), every row edits a working copy (a dynamic
// CKParameterOut of the type); Apply writes the copies that changed to
// their parameters as undo steps.
// The object of the class under the mouse in the topmost window that
// answers (EditorState::objectPick), as CK2UI asks the window under the
// mouse (message 0x607); popups and tooltips are passed. 0: none.
CK_ID ObjectUnderMouse(const EditorState &state, const ImVec2 &mouse, CK_CLASSID cid);

class ParameterBox {
public:
    explicit ParameterBox(EditorState &state) : m_State(state) {}
    ~ParameterBox();
    ParameterBox(const ParameterBox &) = delete;
    ParameterBox &operator=(const ParameterBox &) = delete;

    // Destroys the working copies.
    void Clear();
    // A row for target (label: its name, ANSI); type: the working copy's
    // type when target takes another one before Apply (Edit Parameter's
    // type), the target's otherwise. False without a CK context.
    bool Add(CKParameter *target, const std::string &label, const CKGUID &type = CKGUID());
    // Dev.exe sub_582930's rows for a Building Block (its Target first) or
    // a parameter operation: each input whose value holder is a local
    // parameter (not <This>, the input not disabled), named after the local.
    void AddInputs(CKObject *owner);
    bool Empty() const { return m_Rows.empty(); }
    size_t Count() const { return m_Rows.size(); }
    // A row's working copy as text (ANSI, CKParameter::GetStringValue).
    std::string Text(size_t row) const;
    // A row's working copy (what a dialog without a target reads back).
    CKParameter *Work(size_t row) const;
    // A row of a dependencies value of no parameter (Dev's Copy Options and
    // Select Delete Options, Dev.exe sub_5BDD60 / sub_5BF7B0: a parameter
    // of the type holding them); Dependencies reads the row back.
    bool AddDependencies(const std::string &label, const CKGUID &type, const CKDependencies &value);
    bool Dependencies(size_t row, CKDependencies &value) const;
    // The rows' size in pixels at scale. minimum: the name column at least
    // 100 pixels wide (Dev's Edit Parameters, Dev.exe sub_582930); else the
    // widest name (Dev's Edit Parameter).
    ImVec2 Size(float scale, bool minimum) const;
    // Draws the rows at at. True when a value changed.
    bool Draw(const ImVec2 &at, float scale, bool minimum);
    // True when a working copy differs from its parameter.
    bool Changed() const;
    // Writes the working copies that differ (script: the Script edited;
    // its session follows the values). The undo steps made.
    size_t Apply(CK_ID script);
    // A target button clicked waits for the next click (CK2UI's StartPick
    // captures the mouse): the dialog's buttons wait too.
    bool Picking() const { return m_PickWork != 0; }

private:
    struct Row {
        CK_ID target = 0; // the parameter Apply writes (0: a member of a structure's copy)
        CK_ID work = 0;   // the working copy (or a member of the structure's)
        bool owned = false;
        std::string label; // UTF-8
        ParameterUIInfo info;
        bool extended = false;           // Object: the class filter shown; String: the multi-line edit
        bool collapsed = false;          // Matrix, Flags, 2D curve: shown small (the wrapper's second button)
        CK_CLASSID filterClass = 0;      // Object: the class listed
        bool derived = true;             // Object: with the derived classes
        std::string filter;              // Object: the Text Filter
        CK_CLASSID ownerClass = 0;       // Script: the owner's class
        CK_ID owner = 0;                 // Script: the owner chosen
        bool ownerSet = false;
        bool waitingKey = false;         // Key: the next key pressed is the value
        int curvePoint = 0;              // Curve2D: the control point chosen
        bool hideTangents = false;       // Curve2D: Hide Tangents
        bool hideGrid = false;           // Curve2D: its grid button
        // Dependencies, Object Array: the value edited in the editor's memory,
        // copied to the working copy by its type (SetHeldValue).
        CKDependencies dependencies;
        XObjectArray objects;
        int dependencyClass = -1;        // Dependencies: the class chosen in the tree
        std::set<CK_CLASSID> folded;     // Dependencies: the tree's items folded
        std::set<int> chosen;            // Object Array: the items selected
        int anchor = -1;                 // Object Array: the last item clicked
        std::string text;                // Message, Synchro: the combo's text
        bool textSet = false;
        std::string created;             // Synchro: a name to make an object of (at Apply)
        std::string users;               // Synchro: Number Of Users
        bool usersSet = false;
        ImVec2 drawnMin, drawnMax;       // where the row was drawn last (picking another row)
        std::vector<Row> members;        // Struct: a row per member
    };
    struct Layout {
        float labelWidth = 0.0f; // the names' column (Dev's wrapper: 14 DLU + this + 2)
        float uiX = 0.0f, uiWidth = 0.0f, uiHeight = 0.0f, height = 0.0f;
    };
    void InitRow(Row &row, CKParameter *work);
    void AddRow(Row row);
    // The rows were made before a ClearAll (CkRuntimeHost::Generation): they
    // are dropped, the ids they hold no longer theirs.
    bool Stale();
    float LabelWidth(const std::vector<Row> &rows, float scale, bool minimum) const;
    Layout LayoutOf(const Row &row, float labelWidth, float scale) const;
    ImVec2 UiSize(const Row &row, float scale) const;
    float RowsHeight(const std::vector<Row> &rows, float scale, bool minimum, float *width) const;
    bool DrawRows(std::vector<Row> &rows, const ImVec2 &at, float scale, bool minimum);
    bool DrawUi(Row &row, const ImVec2 &at, float scale);
    struct UiPen;
    void Curve2DUi(Row &row, UiPen &pen);
    bool DependenciesUi(Row &row, CKParameter *work, const ImVec2 &at, float scale);
    bool ObjectArrayUi(Row &row, CKParameter *work, const ImVec2 &at, float scale);
    bool LodOptionsUi(Row &row, CKParameter *work, const ImVec2 &at, float scale);
    // The target button (CK2UI's StartPick and WindowProc): the row being
    // picked for, the object of its class under the mouse (message 0x607 to
    // the window there), another row's value (0x606, pick mode 1).
    Row *RowOf(std::vector<Row> &rows, CK_ID work);
    Row *RowAt(std::vector<Row> &rows, const ImVec2 &mouse, CK_ID except);
    CK_ID ObjectAt(const ImVec2 &mouse, CK_CLASSID cid) const;
    bool PickFrom(Row &row, const ImVec2 &mouse);
    // CK2UI's On_PickResult: a parameter of a compatible type gives its
    // value, an object what the type takes from it.
    bool PickResult(Row &row, CK_ID picked);
    bool RowChanged(const Row &row) const;
    size_t ApplyRow(const Row &row, CK_ID target, CK_ID script);

    EditorState &m_State;
    std::vector<Row> m_Rows;
    std::uint64_t m_Generation = 0; // the host's when the rows were made
    CK_ID m_PickWork = 0;    // the row whose target button is picking
    bool m_PickClick = false; // clicked: until the next click (else until the button is released)
    bool m_PickToggle = false;
    int m_PickFrame = 0;
    std::unique_ptr<ParameterBox> m_Chooser; // an Object Array's Add Object (Choose Object)
};

// Dev's Edit Parameters dialog around a box (Dev.exe sub_582930 and
// sub_584150: the rows in a rounded panel below the title, the buttons OK,
// Apply when apply, Cancel at the right under it), opened with
// ImGui::OpenPopup(id). What was pressed.
enum class DialogAction { None, Ok, Apply, Cancel };
DialogAction EditParametersDialog(const char *id, const std::string &title, ParameterBox &box, bool apply);

// Dev's Edit Parameter dialog (Dev.exe sub_589B10) without its name and
// type rows (options 16 and 32: Copy Options, Select Delete Options, Choose
// Object): the box's row in a black frame on the panel, OK and Cancel. With
// typeName, its type row, not editable (option 32 off: the Setup's
// attribute values, Dev.exe sub_4436E0).
DialogAction EditParameterValueDialog(const char *id, const std::string &title, ParameterBox &box,
                                      const char *typeName = nullptr);

} // namespace vdev
