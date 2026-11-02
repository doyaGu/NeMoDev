#pragma once
#include "ui/controls/ObjectArrayInput.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"

#include "ui/controls/ParameterCopies.h"
#include "core/catalog/ParameterUIInfo.h"
#include "core/graph/ParameterValueData.h"
#include "application/EditorApplication.h"

namespace vdev {

// Dev's parameter box: a row per parameter, CK2UI.dll's wrapper (dialog
// 2000: the target button, the name in bold, then the type's UI on the
// lighter part) after the type's own UI (ParameterUIOf). As Dev's UIs do
// (CK2UI sub_24E64940), every row edits a working copy (a dynamic
// CKParameterOut of the type); Apply writes the copies that changed to
// their parameters as undo steps.
class ParameterBox {
public:
    explicit ParameterBox(ParameterCopies &copies) : m_Copies(copies) {}
    ~ParameterBox();
    ParameterBox(const ParameterBox &) = delete;
    ParameterBox &operator=(const ParameterBox &) = delete;

    // Destroys the working copies.
    void Clear();
    // A row for target (label: its name, ANSI); type: the working copy's
    // type when target takes another one before Apply (Edit Parameter's
    // type), the target's otherwise. False without a CK context.
    // Copied query rows use full identities and UTF-8 names.
    bool Add(ObjectRef target, const std::string &label, uint64_t type = 0);
    bool Empty() const { return m_Rows.empty(); }
    size_t Count() const { return m_Rows.size(); }
    // A row's working copy as text (ANSI, CKParameter::GetStringValue).
    std::string Text(size_t row) const;
    // A row of a dependencies value of no parameter (Dev's Copy Options and
    // Select Delete Options, Dev.exe sub_5BDD60 / sub_5BF7B0: a parameter
    // of the type holding them); Dependencies reads the row back.
    bool AddDefault(uint64_t type, const std::string &label);
    bool AddDraft(const ParameterBox &, const std::string &label);
    bool CopyDraft(const ParameterBox &);
    ObjectRef WorkReference(size_t row = 0) const;
    bool AddDependencies(const std::string &label, uint64_t type, const ParameterDependencies &value);
    bool Dependencies(size_t row, ParameterDependencies &value) const;
    // The rows' size in pixels at scale. minimum: the name column at least
    // 100 pixels wide (Dev's Edit Parameters, Dev.exe sub_582930); else the
    // widest name (Dev's Edit Parameter).
    ImVec2 Size(float scale, bool minimum) const;
    // Draws the rows at at. True when a value changed.
    bool Draw(const ImVec2 &at, float scale, bool minimum);
    // True when a working copy differs from its parameter.
    bool Changed() const;
    bool Valid() const;
    bool InputActive() const { return (m_InputAction & EDITOR_INPUT_ACTIVE) != 0; }
    // Writes the working copies that differ (script: the Script edited;
    // its session follows the values). The undo steps made.
    size_t Apply(uint32_t script);
    // A target button clicked waits for the next click (CK2UI's StartPick
    // captures the mouse): the dialog's buttons wait too.
    bool Picking() const { return m_PickWork != 0; }

private:
    struct Row {
        ObjectRef targetReference{}, workReference{};
        std::unique_ptr<ParameterValue> valueRoot;
        ParameterValue *valueOwner = nullptr;
        std::vector<size_t> valuePath;
        ParameterValue *References() const {
            auto *value = valueOwner;
            for (auto index : valuePath) {
                if (!value || value->kind != ParameterValueKind::Structure || index >= value->members.size()) return nullptr;
                value = &value->members[index];
            }
            return value;
        }
        bool invalid = false, typed = false;
        ParameterValue beforeInteraction;
        PropertyBinding property;
        EditorField schema;
        uint32_t target = 0; // the parameter Apply writes (0: a member of a structure's copy)
        uint32_t work = 0;   // the working copy (or a member of the structure's)
        bool owned = false;
        std::string label; // UTF-8
        ParameterUIInfo info;
        bool extended = false;           // Object: the class filter shown; String: the multi-line edit
        unsigned representation = 0; // Rect: corners, size or center/half-size
        bool collapsed = false;          // Matrix, Flags, 2D curve: shown small (the wrapper's second button)
        int32_t filterClass = 0;      // Object: the class listed
        bool derived = true;             // Object: with the derived classes
        std::string filter;              // Object: the Text Filter
        ObjectArrayInput arrayInput;
        ImVec2 drawnMin, drawnMax;       // where the row was drawn last (picking another row)
        std::vector<Row> members;        // Struct: a row per member
    };
    struct Layout {
        float labelWidth = 0.0f; // the names' column (Dev's wrapper: 14 DLU + this + 2)
        float uiX = 0.0f, uiWidth = 0.0f, uiHeight = 0.0f, height = 0.0f;
    };
    void InitRow(Row &row, ObjectRef work);
    void InitRow(Row &row, ObjectRef work, unsigned depth, unsigned &remaining);
    void AddRow(Row row);
    void ShareValues(Row &, ParameterValue *owner = nullptr, std::vector<size_t> path = {});
    void ForgetRow(const Row &);
    void TargetRow(Row &, ObjectRef );
    bool ValuesCurrent(const Row &) const;
    bool TargetsCurrent(const Row &) const;
    // The rows were made before a ClearAll (CkRuntimeHost::Generation): they
    // are dropped, the ids they hold no longer theirs.
    bool Stale();
    float LabelWidth(const std::vector<Row> &rows, float scale, bool minimum) const;
    Layout LayoutOf(const Row &row, float labelWidth, float scale) const;
    ImVec2 UiSize(const Row &row, float scale) const;
    float RowsHeight(const std::vector<Row> &rows, float scale, bool minimum, float *width) const;
    bool DrawRows(std::vector<Row> &rows, const ImVec2 &at, float scale, bool minimum);
    bool DrawUi(Row &row, const ImVec2 &at, float scale);
    bool ObjectArrayUi(Row &row, const EditorValueInput &, const ImVec2 &at, float scale);
    // The target button (CK2UI's StartPick and WindowProc): the row being
    // picked for, the object of its class under the mouse (message 0x607 to
    // the window there), another row's value (0x606, pick mode 1).
    Row *RowOf(std::vector<Row> &rows, uint32_t work);
    Row *RowAt(std::vector<Row> &rows, const ImVec2 &mouse, uint32_t except);
    ObjectRef ObjectAt(const ImVec2 &mouse, int32_t cid) const;
    bool PickFrom(Row &row, const ImVec2 &mouse);
    // CK2UI's On_PickResult: a parameter of a compatible type gives its
    // value, an object what the type takes from it.
    bool PickResult(Row &row, ObjectRef picked);
    bool RowChanged(const Row &row) const;
    size_t ApplyRow(const Row &row);

    ParameterCopies &m_Copies;
    std::vector<Row> m_Rows;
    std::uint64_t m_Generation = 0; // the host's when the rows were made
    bool m_ApplyFailed = false;
    uint32_t m_InputAction = 0;
    EditorValueEditors m_Editors;
    std::string m_CopyProvider;
    uint32_t m_PickWork = 0;    // the row whose target button is picking
    bool m_PickClick = false; // clicked: until the next click (else until the button is released)
    bool m_PickToggle = false;
    bool m_PickShift = false;
    int m_PickFrame = 0;
    void EndPicking();
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
