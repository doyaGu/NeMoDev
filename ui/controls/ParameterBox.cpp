// Dev's parameter box and CK2UI.dll's parameter UIs, after their dialog
// templates (Managers\CK2UI.dll dialogs 134 to 2012, in dialog units of MS
// Sans Serif 8: 1.5 x 1.625 pixels).
#include "ui/controls/ParameterBox.h"


#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "core/level/LevelCommands.h"
#include "core/level/ObjectEditCommands.h"
#include "core/graph/RuntimeGraph.h"
#include "core/runtime/TextEncoding.h"
#include "core/graph/ValueCommands.h"
#include "core/graph/ParameterValues.h"
#include "core/catalog/ValueText.h"
#include "core/document/Edit.h"
#include "ui/controls/ValueEditor.h"
#include "features/values/ParameterCommands.h"
#include "workspace/EditorWorkspace.h"

namespace vdev {

namespace {

template <typename T> T ValueOf(CKParameter *parameter) {
    T value{};
    if (parameter && parameter->GetDataSize() >= static_cast<int>(sizeof(T))) parameter->GetValue(&value, FALSE);
    return value;
}

template <typename T> void SetValueOf(CKParameter *parameter, const T &value) {
    if (parameter) parameter->SetValue(const_cast<T *>(&value), sizeof(T));
}

bool ParseInt(const std::string &text, int &value) { int32_t integer = 0; if (!ParseIntegerValue(text, integer)) return false; value = integer; return true; }

std::string TextOf(CKParameter *parameter) { return AnsiToUtf8(ParameterText(parameter)); }

void SetText(CKParameter *parameter, const std::string &utf8) {
    if (parameter) parameter->SetStringValue(const_cast<CKSTRING>(Utf8ToAnsi(utf8).c_str()));
}

std::vector<CK_ID> MembersOf(const Document &document, CKParameter *parameter) {
    std::vector<ParameterMember> members; std::vector<CK_ID> ids;
    if (ParameterMembers(document, parameter, members)) for (const auto &member : members)
        ids.push_back(document.NativeId(member.parameter, Document::ObjectScope::Existing));
    return ids;
}

// Dev's dialog units (MS Sans Serif 8).
constexpr float DLU_X = 1.5f, DLU_Y = 1.625f;

// The values held by pointer (the types' own memory, copied by their copy
// functions): a dependencies value, an object array.
template <typename T> T *HeldOf(CKParameter *parameter) {
    T *held = nullptr;
    if (parameter && parameter->GetDataSize() == static_cast<int>(sizeof(held))) parameter->GetValue(&held, FALSE);
    return held;
}

} // namespace

ParameterBox::~ParameterBox() { Clear(); }

void ParameterBox::Clear() {
    CKContext *context = m_State.host.Context();
    // The working copies of an earlier generation went with ClearAll; their ids may be another object's now.
    const bool alive = m_Generation == m_State.host.Generation();
    for (const Row &row : m_Rows)
        if (alive && row.owned && context && m_State.document && m_State.document->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing)) context->DestroyObject(row.work);
    m_Rows.clear();
    m_Editors.clear();
    m_CopyProvider.clear();
    if (m_PickWork) vi::BlockDialogButtons(false);
    m_PickWork = 0;
}

bool ParameterBox::AddDependencies(const std::string &label, const CKGUID &type, const CKDependencies &value) {
    CKContext *context = m_State.host.Context();
    if (!context) return false;
    CKParameterOut *work = context->CreateCKParameterOut(const_cast<CKSTRING>(label.c_str()), type, TRUE);
    CKDependencies copy = value;
    if (!SetHeldValue(work, &copy)) {
        if (work) context->DestroyObject(work);
        return false;
    }
    Row row;
    row.owned = true;
    row.label = AnsiToUtf8(label);
    InitRow(row, work);
    AddRow(std::move(row));
    return true;
}

bool ParameterBox::Dependencies(size_t row, CKDependencies &value) const {
    CKDependencies *held = HeldOf<CKDependencies>(Work(row));
    if (!held) return false;
    value = *held;
    return true;
}

CKParameter *ParameterBox::Work(size_t row) const {
    CKContext *context = m_State.host.Context();
    return row < m_Rows.size() && context && m_State.document ? CKParameter::Cast(m_State.document->Resolve(m_Rows[row].workReference, CKCID_PARAMETER, Document::ObjectScope::Existing)) : nullptr;
}

void ParameterBox::InitRow(Row &row, CKParameter *work) {
    unsigned remaining = 65536;
    InitRow(row, work, 0, remaining);
    ShareValues(row);
}
void ParameterBox::InitRow(Row &row, CKParameter *work, unsigned depth, unsigned &remaining) {
    if (depth >= 32 || !remaining) { row.invalid = true; return; }
    --remaining;
    CKContext *context = m_State.host.Context();
    CKParameterManager *manager = context->GetParameterManager();
    row.work = work->GetID();
    row.workReference = m_State.document ? m_State.document->Reference(row.work) : ObjectRef{};
    ParameterValueDescription description;
    row.typed = m_State.document && DescribeParameterValue(*m_State.document, work, description);
    if (row.typed) { row.valueRoot = std::make_unique<ParameterValue>(std::move(description.value)); row.valueOwner = row.valueRoot.get(); }
    if (row.typed && m_State.workspace) {
        auto &app = m_State.workspace->Application();
        if (const auto *property = app.Property(ParameterProperty(description.value.kind))) {
            row.property = *property;
        }
    }
    row.info = ParameterUIOf(manager, work->GetType());
    switch (row.info.ui) {
    case ParameterUI::Object:
        // Dev's generic classes show the class filter (sub_24E6C7E0).
        row.filterClass = row.info.pickClass;
        row.extended = row.filterClass == CKCID_OBJECT || row.filterClass == CKCID_BEOBJECT;
        break;
    case ParameterUI::Script: {
        CKBehavior *script = CKBehavior::Cast(context->GetObject(ValueOf<CK_ID>(work)));
        CKBeObject *owner = script ? script->GetOwner() : nullptr;
        row.ownerClass = owner ? owner->GetClassID() : CKCID_BEOBJECT;
        row.owner = owner ? owner->GetID() : 0;
        row.ownerSet = owner != nullptr;
        break;
    }
    case ParameterUI::Synchro: {
        // The object's name, a synchro object's number of users (sub_24E73240).
        CKObject *object = context->GetObject(ValueOf<CK_ID>(work));
        row.text = AnsiToUtf8(object && object->GetName() ? object->GetName() : "");
        if (CKSynchroObject *synchro = CKSynchroObject::Cast(object))
            row.users = std::to_string(synchro->GetRendezVousNumberOfWaiters());
        break;
    }
    case ParameterUI::Struct: {
        std::vector<ParameterMember> members;
        if (!ParameterMembers(*m_State.document, work, members)) { row.invalid = true; break; }
        row.members.clear();
        for (const auto &entry : members) {
            auto *member = CKParameter::Cast(m_State.document->Resolve(entry.parameter, CKCID_PARAMETER, Document::ObjectScope::Existing));
            if (!member) { row.invalid = true; break; }
            Row child; child.label = entry.name;
            InitRow(child, member, depth + 1, remaining);
            row.members.push_back(std::move(child));
        }
        break;
    }
    default: break;
    }
}

void ParameterBox::ShareValues(Row &row, ParameterValue *owner, std::vector<size_t> path) {
    if (!row.typed) {
        for (auto &member : row.members) ShareValues(member);
        return;
    }
    if (owner) { row.valueRoot.reset(); row.valueOwner = owner; row.valuePath = std::move(path); }
    else { row.valueOwner = row.valueRoot.get(); row.valuePath.clear(); }
    for (size_t i = 0; i < row.members.size(); ++i) {
        auto memberPath = row.valuePath; memberPath.push_back(i);
        ShareValues(row.members[i], row.valueOwner, std::move(memberPath));
    }
}
bool ParameterBox::ValuesCurrent(const Row &row) const {
    if (row.typed) {
        auto *work = CKParameter::Cast(m_State.document->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing));
        ParameterValueDescription description;
        const auto *remembered = row.References();
        if (!remembered || !DescribeParameterValue(*m_State.document, work, description)) return false;
        auto value = RememberParameterReferences(std::move(description.value), *remembered);
        auto *target = CKParameter::Cast(m_State.document->Resolve(row.targetReference, CKCID_PARAMETER));
        return DescribeParameterValue(*m_State.document, target, description) && ValidateParameterValue(*m_State.document, description, value);
    }
    for (const auto &member : row.members) if (!ValuesCurrent(member)) return false;
    return true;
}
void ParameterBox::TargetRow(Row &row, CKParameter *target) {
    row.target = target ? target->GetID() : 0;
    row.targetReference = m_State.document->Reference(row.target);
    if (!target) { row.invalid = true; return; }
    auto *work = CKParameter::Cast(m_State.document->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing));
    if (!work || work->GetGUID() != target->GetGUID()) return; // a pending type change
    if (row.info.ui != ParameterUI::Struct) return;
    std::vector<ParameterMember> members;
    if (!ParameterMembers(*m_State.document, target, members) || members.size() != row.members.size()) { row.invalid = true; return; }
    for (size_t i = 0; i < members.size(); ++i)
        TargetRow(row.members[i], CKParameter::Cast(m_State.document->Resolve(members[i].parameter, CKCID_PARAMETER)));
    ParameterValueDescription description;
    if (DescribeParameterValue(*m_State.document, target, description)) row.schema = ParameterFields(description).back();
}
bool ParameterBox::TargetsCurrent(const Row &row) const {
    auto *target = CKParameter::Cast(m_State.document->Resolve(row.targetReference, CKCID_PARAMETER));
    auto *work = CKParameter::Cast(m_State.document->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing));
    if (!target || !work || target->GetGUID() != work->GetGUID()) return false;
    if (row.info.ui == ParameterUI::Struct) {
        std::vector<ParameterMember> members;
        if (!ParameterMembers(*m_State.document, target, members) || members.size() != row.members.size()) return false;
        for (size_t i = 0; i < members.size(); ++i)
            if (members[i].parameter != row.members[i].targetReference || !TargetsCurrent(row.members[i])) return false;
    }
    return true;
}
bool ParameterBox::Retarget(size_t index, CKParameter *target) {
    if (!m_State.HasDocument() || index >= m_Rows.size() || !target) return false;
    auto *work = Work(index);
    if (!work || work->GetGUID() != target->GetGUID()) return false;
    TargetRow(m_Rows[index], target);
    return TargetsCurrent(m_Rows[index]);
}

bool ParameterBox::Add(CK_ID target, const std::string &label, const CKGUID &type) {
    if (!m_State.HasDocument() || !m_State.document->IsLive(target)) return false;
    return Add(CKParameter::Cast(m_State.document->Context()->GetObject(target)), label, type);
}

bool ParameterBox::Add(CKParameter *target, const std::string &label, const CKGUID &type) {
    CKContext *context = m_State.host.Context();
    if (!context || !target) return false;
    // The working copy (CK2UI sub_24E64940: a CKParameterOut of the type).
    const bool other = (type.d1 || type.d2) && !(type == target->GetGUID());
    CKParameterOut *work =
        context->CreateCKParameterOut(const_cast<CKSTRING>(label.c_str()), other ? type : target->GetGUID(), TRUE);
    if (!work) return false;
    if (!other) work->CopyValue(target, FALSE);
    Row row;
    row.target = target->GetID();
    row.targetReference = m_State.document->Reference(row.target);
    row.owned = true;
    row.label = AnsiToUtf8(label);
    InitRow(row, work);
    TargetRow(row, target);
    AddRow(std::move(row));
    return true;
}

void ParameterBox::AddRow(Row row) {
    // Rows left from an earlier generation name objects that are gone.
    if (m_Generation != m_State.host.Generation()) {
        Clear();
        m_Generation = m_State.host.Generation();
    }
    if (m_Editors.empty() && m_State.workspace) {
        auto &app = m_State.workspace->Application();
        m_Editors = app.CaptureValueEditors(); m_CopyProvider = app.CommandProvider("parameter.copy-value");
    }
    m_Rows.push_back(std::move(row));
}

bool ParameterBox::Stale() {
    if (m_Rows.empty() || m_Generation == m_State.host.Generation()) return false;
    Clear();
    return true;
}

// The names' column: the widest name in bold and 2 pixels (CK2UI message
// 0x60E), at least 100 in Dev's Edit Parameters (Dev.exe sub_582930).
float ParameterBox::LabelWidth(const std::vector<Row> &rows, float s, bool minimum) const {
    float width = 0.0f;
    for (const Row &row : rows) width = std::max(width, vi::TextWidth(row.label.c_str(), true) + 2.0f * s);
    return minimum ? std::max(width, 100.0f * s) : width;
}

ImVec2 ParameterBox::UiSize(const Row &row, float s) const {
    auto dlu = [&](float w, float h) { return ImVec2(w * DLU_X * s, h * DLU_Y * s); };
    switch (row.info.ui) {
    case ParameterUI::Angle: return dlu(216, 18);
    case ParameterUI::EulerAngles: return ImVec2(216 * DLU_X * s, 69.0f * s);
    case ParameterUI::Object: return row.extended ? dlu(219, 43) : dlu(219, 14);
    // The second button: the multi-line edit, 200 pixels high (sub_24E726D0).
    case ParameterUI::String: return row.extended ? ImVec2(220 * DLU_X * s + 5.0f * s, 205.0f * s) : dlu(231, 14);
    // Small: the value's text (static 2034, sub_24E6EE50).
    case ParameterUI::Matrix: return row.collapsed ? ImVec2(319 * DLU_X * s + 5.0f * s, 18 * DLU_Y * s) : dlu(336, 57);
    case ParameterUI::Flags: {
        // A check box per flag of one bit, at its bit's place (sub_24E6C460),
        // down to the last one; small: the value's text (static 2006).
        if (row.collapsed) return ImVec2(56 * DLU_X * s + 5.0f * s, 11 * DLU_Y * s + 5.0f * s);
        unsigned rows = 1;
        if (m_State.workspace && !row.property.id.empty()) {
            auto &app = m_State.workspace->Application();
            const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, row.workReference};
            const auto result = app.Query(row.property.query, {app.Scope(), &target, 1, EditorText(row.property.queryProvider)});
            if (result.status == EDITOR_OK)
                for (const auto &field : result.fields)
                    if (field.key == "rows" && field.type == EDITOR_INTEGER_VALUE && field.integer >= 1 && field.integer <= 16)
                        rows = unsigned(field.integer);
        }
        return ImVec2(169 * DLU_X * s + 5.0f * s, (2.0f + 10.0f * rows) * DLU_Y * s + 5.0f * s);
    }
    // Dialog 156 less 260 pixels unless Custom Dependencies (sub_24E6B180).
    case ParameterUI::Dependencies: {
        const auto *value = row.References();
        const bool custom = value && value->integer == 0;
        return ImVec2(296 * DLU_X * s, custom ? 196 * DLU_Y * s : 196 * DLU_Y * s - 260 * s);
    }
    case ParameterUI::ObjectArray: return dlu(216, 105);
    case ParameterUI::LodOptions: return dlu(231, 55);
    // Small: the curve alone, 200 x 40 pixels (sub_24E67210).
    case ParameterUI::Curve2D: return row.collapsed ? ImVec2(206.0f * s, 46.0f * s) : dlu(213, 169);
    case ParameterUI::Struct: {
        float width = 0.0f;
        const float height = RowsHeight(row.members, s, false, &width);
        return ImVec2(width, height);
    }
    case ParameterUI::None: return dlu(231, 14);
    default: return dlu(static_cast<float>(row.info.width), static_cast<float>(row.info.height));
    }
}

void ParameterBox::AddInputs(CKObject *owner) {
    auto add = [&](CKParameterIn *input) {
        if (!input || (input->GetObjectFlags() & CK_PARAMETERIN_DISABLED)) return;
        CKParameterLocal *holder = EditableValueOf(input);
        if (!holder || holder->IsMyselfParameter()) return;
        Add(holder, holder->GetName() ? holder->GetName() : "");
    };
    if (CKBehavior *behavior = CKBehavior::Cast(owner)) {
        if (behavior->IsUsingTarget()) add(behavior->GetTargetParameter());
        for (int i = 0; i < behavior->GetInputParameterCount(); ++i) add(behavior->GetInputParameter(i));
    } else if (CKParameterOperation *operation = CKParameterOperation::Cast(owner)) {
        add(operation->GetInParameter1());
        add(operation->GetInParameter2());
    }
}

// CK2UI's wrapper (sub_24E64C50): the name 14 DLU from the left, the UI
// after the names' column and 2 pixels, centered in the row; the row 2
// pixels taller than the UI or than the buttons with the name.
ParameterBox::Layout ParameterBox::LayoutOf(const Row &row, float labelWidth, float s) const {
    Layout layout;
    layout.labelWidth = labelWidth;
    layout.uiX = 14.0f * DLU_X * s + labelWidth + 2.0f * s;
    const ImVec2 ui = UiSize(row, s);
    layout.uiWidth = ui.x;
    layout.uiHeight = ui.y;
    const float side = row.info.expand && row.info.pick ? 15.0f * DLU_Y * s : 11.0f * DLU_Y * s;
    layout.height = std::max(ui.y, side) + 2.0f * s;
    return layout;
}

float ParameterBox::RowsHeight(const std::vector<Row> &rows, float s, bool minimum, float *width) const {
    const float labels = LabelWidth(rows, s, minimum);
    float height = 0.0f;
    for (size_t i = 0; i < rows.size(); ++i) {
        const Layout layout = LayoutOf(rows[i], labels, s);
        height += layout.height + (i ? 1.0f : 0.0f);
        if (width) *width = std::max(*width, layout.uiX + layout.uiWidth);
    }
    return height;
}

ImVec2 ParameterBox::Size(float s, bool minimum) const {
    float width = 0.0f;
    const float height = RowsHeight(m_Rows, s, minimum, &width);
    return ImVec2(width, height);
}

bool ParameterBox::Draw(const ImVec2 &at, float s, bool minimum) {
    m_InputAction = 0;
    if (Stale()) return false;
    ImGui::PushID(this);
    bool changed = false;
    // A target button picking (CK2UI's WindowProc while it holds the
    // mouse): what the window under the mouse gives for the class shows at
    // once (WM_MOUSEMOVE, message 0x607); clicked, the next click ends it
    // (WM_LBUTTONDOWN), the rows and the dialog's buttons waiting.
    Row *picking = m_PickWork ? RowOf(m_Rows, m_PickWork) : nullptr;
    if (m_PickWork && !picking) m_PickWork = 0;
    const bool wasCaptured = m_PickWork && m_PickClick; // the click ending it reaches nothing else
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (picking) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::GetForegroundDrawList()->AddCircle(mouse, 6.0f * s, vi::Color(10), 0, 2.0f);
        if (const CK_ID picked = ObjectAt(mouse, picking->info.pickClass)) changed = PickResult(*picking, picked) || changed;
        if (m_PickClick && ImGui::GetFrameCount() > m_PickFrame) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                changed = PickFrom(*picking, mouse) || changed;
                m_PickWork = 0;
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                m_PickWork = 0;
            }
        }
    }
    const bool captured = wasCaptured || (m_PickWork && m_PickClick);
    vi::BlockDialogButtons(captured);
    if (captured) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    changed = DrawRows(m_Rows, at, s, minimum) || changed;
    if (captured) ImGui::PopItemFlag();
    ImGui::PopID();
    return changed;
}

// The rows one under the other, 1 pixel apart (Dev.exe sub_584150); the
// gap shows the box's light background and, right of the names, a line of
// ZC 6 (sub_58BDD0).
bool ParameterBox::DrawRows(std::vector<Row> &rows, const ImVec2 &at, float s, bool minimum) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float labels = LabelWidth(rows, s, minimum);
    float width = 0.0f;
    RowsHeight(rows, s, minimum, &width);
    float y = at.y;
    bool changed = false;
    for (size_t i = 0; i < rows.size(); ++i) {
        Row &row = rows[i];
        const Layout layout = LayoutOf(row, labels, s);
        if (i) {
            dl->AddRectFilled(ImVec2(at.x, y), ImVec2(at.x + width, y + 1.0f), vi::Color(7));
            dl->AddLine(ImVec2(at.x + layout.uiX, y + 0.5f), ImVec2(at.x + width, y + 0.5f), vi::Color(6));
            y += 1.0f;
        }
        const ImVec2 min(at.x, y), max(at.x + layout.uiX + layout.uiWidth, y + layout.height);
        row.drawnMin = min;
        row.drawnMax = max;
        // CK2UI's wrapper (sub_24E64E80, WM_PAINT): the names' part ZC 6, a
        // black line, the UI's part ZC 7.
        dl->AddRectFilled(min, ImVec2(min.x + layout.uiX - 1.0f, max.y), vi::Color(6));
        dl->AddLine(ImVec2(min.x + layout.uiX - 0.5f, min.y), ImVec2(min.x + layout.uiX - 0.5f, max.y), vi::Color(0));
        dl->AddRectFilled(ImVec2(min.x + layout.uiX, min.y), max, vi::Color(7));
        ImGui::PushID(static_cast<int>(i));
        // The buttons (dialog 2000: (3, 1) and (3, 8)): the expand button
        // (CK2UI bitmaps 2006 / 2008 / 2003) above the target button
        // (2007 / 2005 / 2004), or the target button alone at the top.
        auto button = [&](const char *id, float dluY, const int bitmaps[3], bool pushed) {
            const ImVec2 a(min.x + 3.0f * DLU_X * s, min.y + dluY * DLU_Y * s);
            const ImVec2 size(11.0f * s, 11.0f * s);
            ImGui::SetCursorScreenPos(a);
            const bool clicked = ImGui::InvisibleButton(id, size);
            const int bitmap = pushed || ImGui::IsItemActive() ? bitmaps[2] : ImGui::IsItemHovered() ? bitmaps[1] : bitmaps[0];
            if (ImTextureID texture = CK2UIBitmap(bitmap)) dl->AddImage(texture, a, ImVec2(a.x + size.x, a.y + size.y));
            return clicked;
        };
        static const int EXPAND[3] = {2006, 2008, 2003};
        static const int PICK[3] = {2007, 2005, 2004};
        // The target button (button 2003, sub_24E649F0): pushed, it picks
        // what is under the mouse until the next click (CK2UI's StartPick);
        // dragged, until it is released.
        auto pick = [&](float dluY) {
            button("##pick", dluY, PICK, m_PickWork == row.work);
            if (ImGui::IsItemActivated()) {
                m_PickToggle = m_PickWork == row.work && m_PickClick;
                m_PickWork = row.work;
                m_PickClick = false;
            }
            if (ImGui::IsItemDeactivated() && m_PickWork == row.work) {
                if (ImGui::IsItemHovered()) {
                    // A click: picking until the next one (a second click on it stops).
                    m_PickClick = !m_PickToggle;
                    m_PickFrame = ImGui::GetFrameCount();
                    if (m_PickToggle) m_PickWork = 0;
                } else {
                    changed = PickFrom(row, ImGui::GetIO().MousePos) || changed;
                    m_PickWork = 0;
                }
            }
        };
        if (row.info.expand) {
            // The expand button (button 2004, sub_24E64A30: the UI's
            // On_Expand): the Object's class filter, the String's multi-line
            // edit, the Rect's representations, the Matrix, Flags and 2D
            // curve shown small.
            if (button("##expand", 1.0f, EXPAND, false)) {
                switch (row.info.ui) {
                case ParameterUI::Rect: row.representation = (row.representation + 1) % 3; break;
                case ParameterUI::Matrix:
                case ParameterUI::Flags:
                case ParameterUI::Curve2D: row.collapsed = !row.collapsed; break;
                default: row.extended = !row.extended; break;
                }
                changed = true;
            }
            if (row.info.pick) pick(8.0f);
        } else if (row.info.pick) {
            pick(1.0f);
        }
        const float uiY = y + (layout.height - layout.uiHeight) * 0.5f;
        vi::Text(ImVec2(min.x + 14.0f * DLU_X * s, uiY), ImVec2(min.x + layout.uiX - 2.0f * s, uiY + 13.0f * s),
                 row.label.c_str(), true);
        if (DrawUi(row, ImVec2(min.x + layout.uiX, uiY), s)) changed = true;
        ImGui::PopID();
        y += layout.height;
    }
    return changed;
}

// The type's UI at its dialog's top-left corner.
bool ParameterBox::DrawUi(Row &row, const ImVec2 &at, float s) {
    CKContext *context = m_State.host.Context();
    CKParameterManager *manager = context->GetParameterManager();
    CKParameter *work = CKParameter::Cast(context->GetObject(row.work));
    if (!work) return false;
    if (row.typed && !row.References()) { row.invalid = true; return false; }
    auto P = [&](float x, float y) { return ImVec2(at.x + x * DLU_X * s, at.y + y * DLU_Y * s); };
    auto label = [&](float x, float y, float w, float h, const char *text, bool bold = true, vi::Align align = vi::CENTER) {
        vi::Text(P(x, y), P(x + w, y + h), text, bold, align);
    };
    bool changed = false;
    auto integer = [&](const char *id, float x, float y, float w, int value, const std::function<void(int)> &set) {
        std::string text = std::to_string(value);
        bool done = false;
        if (vi::Edit(id, P(x, y), P(x + w, y + 12), text, vi::CENTER, &done)) {
            int parsed = 0;
            if (ParseInt(text, parsed)) {
                set(parsed);
                changed = true;
            }
        }
    };
    // A combo of choices with the CK_ID value (NULL: 0).
    auto objectCombo = [&](const char *id, float x, float y, float w, CK_ID current,
                           const std::function<ObjectChoices()> &choices, const std::function<void(CK_ID)> &set) {
        CKObject *object = context->GetObject(current);
        const std::string preview = AnsiToUtf8(object && object->GetName() ? object->GetName() : current ? "" : "NULL");
        ObjectChoices list;
        const int chosen = vi::Combo(id, P(x, y), P(x + w, y + 12), preview,
                                     [&] {
                                         list = choices();
                                         std::vector<std::string> names;
                                         for (const auto &choice : list) names.push_back(AnsiToUtf8(choice.second));
                                         return names;
                                     },
                                     [&] {
                                         for (size_t i = 0; i < list.size(); ++i)
                                             if (list[i].first == current) return static_cast<int>(i);
                                         return -1;
                                     }());
        if (chosen >= 0 && chosen < static_cast<int>(list.size())) {
            set(list[chosen].first);
            changed = true;
        }
    };
    auto textEdit = [&](float x, float w, vi::Align align) {
        auto text = TextOf(work);
        if (vi::Edit("##text", P(x, 0), P(x + w, 12), text, align)) { SetText(work, text); changed = true; }
    };

    switch (row.info.ui) {
    case ParameterUI::Number:
    case ParameterUI::Bool:
    case ParameterUI::Enum: {
        const bool boolean = row.info.ui == ParameterUI::Bool;
        const char *id = boolean ? "##bool" : row.info.ui == ParameterUI::Enum ? "##enum" : "##number";
        const auto min = boolean ? P(5, 2) : P(15, 0);
        const auto max = row.info.ui == ParameterUI::Enum ? P(210, 12) : P(70, 12);
        ImGui::SetCursorScreenPos(min);
        const auto action = ParameterDraftEditor(m_State, work, id, max.x - min.x,
            boolean ? 8 * DLU_X * s : max.y - min.y, 0, true, {}, row.References(), &row.property, &m_Editors, 0, &row.beforeInteraction);
        m_InputAction |= action;
        if (action & EDITOR_INPUT_CANCEL) row.invalid = false;
        if (action & EDITOR_INPUT_INVALID) row.invalid = true;
        else if (action & EDITOR_INPUT_CHANGED) { row.invalid = false; changed = true; }
        break;
    }
    case ParameterUI::Angle:
    case ParameterUI::EulerAngles:
    case ParameterUI::Vector:
    case ParameterUI::Quaternion:
    case ParameterUI::Vector2D:
    case ParameterUI::Matrix:
    case ParameterUI::Color:
    case ParameterUI::Box:
    case ParameterUI::Rect: {
        ImGui::SetCursorScreenPos(at);
        const auto size = UiSize(row, s);
        const unsigned representation = row.info.ui == ParameterUI::Matrix ? unsigned(row.collapsed) :
            row.info.ui == ParameterUI::Rect ? row.representation : 0;
        const auto action = ParameterDraftEditor(m_State, work, "##components", size.x, size.y,
            0, true, {}, row.References(), &row.property, &m_Editors, representation, &row.beforeInteraction);
        m_InputAction |= action;
        if (action & EDITOR_INPUT_CANCEL) row.invalid = false;
        if (action & EDITOR_INPUT_INVALID) row.invalid = true;
        else if (action & EDITOR_INPUT_CHANGED) { row.invalid = false; changed = true; }
        break;
    }
    case ParameterUI::Time: {
        // "%dm %ds %dms" (CK2UI sub_24E739A0).
        int m = 0, sec = 0, ms = 0;
        std::sscanf(ParameterText(work).c_str(), "%dm %ds %dms", &m, &sec, &ms);
        auto setTime = [&] {
            char text[64];
            std::snprintf(text, sizeof(text), "%dm %ds %dms", m, sec, ms);
            work->SetStringValue(text);
        };
        label(5, 0, 15, 10, "Min:");
        integer("##min", 20, 0, 25, m, [&](int v) { m = v; setTime(); });
        label(50, 0, 10, 10, "S:");
        integer("##s", 60, 0, 25, sec, [&](int v) { sec = v; setTime(); });
        label(90, 0, 15, 10, "Ms:");
        integer("##ms", 105, 0, 25, ms, [&](int v) { ms = v; setTime(); });
        break;
    }
    case ParameterUI::Key: {
        // A key pressed after the click is the value (a DirectInput code).
        const int key = ValueOf<int>(work);
        std::string name = key ? TextOf(work) : std::string();
        if (row.waitingKey) name = "Push a Key";
        if (name.empty()) name = "Click Here and Push a Key";
        if (vi::Button("##key", P(0, 1), P(145, 14), name.c_str())) row.waitingKey = !row.waitingKey;
        if (row.waitingKey) {
            for (int scan = 1; scan < 256; ++scan) {
                if (!VxIsKeyDown(static_cast<XDWORD>(scan))) continue;
                if (scan) {
                    SetValueOf(work, scan);
                    row.waitingKey = false;
                    changed = true;
                }
                break;
            }
        }
        break;
    }
    case ParameterUI::String: {
        const auto min = P(15, 0), max = P(220, 12);
        ImGui::SetCursorScreenPos(min);
        const auto action = ParameterDraftEditor(m_State, work, "##string", max.x - min.x, row.extended ? 200 * s : max.y - min.y,
            0, true, {}, row.References(), &row.property, &m_Editors, unsigned(row.extended), &row.beforeInteraction);
        m_InputAction |= action;
        if (action & EDITOR_INPUT_CANCEL) row.invalid = false;
        if (action & EDITOR_INPUT_INVALID) row.invalid = true;
        else if (action & EDITOR_INPUT_CHANGED) { row.invalid = false; changed = true; }
        break;
    }
    case ParameterUI::Object: {
        // CK2UI's Object UI (dialog 2011, sub_24E6CE00): the objects of the
        // class; with its filter: Class, Derived, Text Filter, Show Setup.
        const CK_ID current = ValueOf<CK_ID>(work);
        const CK_CLASSID cid = row.extended ? row.filterClass : row.info.pickClass;
        ImGui::SetCursorScreenPos(P(19, 0));
        const auto action = ParameterDraftEditor(m_State, work, "##object", 195 * DLU_X * s, 12 * DLU_Y * s,
            cid, row.extended ? row.derived : true, row.extended ? row.filter : "", row.References(), &row.property, &m_Editors, 0, &row.beforeInteraction);
        m_InputAction |= action;
        if (action & EDITOR_INPUT_CANCEL) row.invalid = false;
        if (action & EDITOR_INPUT_INVALID) row.invalid = true;
        else if (action & EDITOR_INPUT_CHANGED) { row.invalid = false; changed = true; }
        if (row.extended) {
            label(15, 15, 28, 10, "Class", true, vi::LEFT);
            const auto classes = DerivedClasses(row.info.pickClass);
            int index = -1;
            for (size_t i = 0; i < classes.size(); ++i)
                if (classes[i].first == row.filterClass) index = static_cast<int>(i);
            const char *className = CKClassIDToString(row.filterClass);
            const int chosen = vi::Combo("##class", P(53, 15), P(165, 27), className ? className : "",
                                         [&] {
                                             std::vector<std::string> names;
                                             for (const auto &entry : classes) names.push_back(entry.second);
                                             return names;
                                         },
                                         index);
            if (chosen >= 0) row.filterClass = classes[chosen].first;
            label(171, 15, 28, 10, "Derived", true, vi::CENTER);
            vi::Check("##derived", P(206, 15), 8.0f * DLU_X * s, row.derived);
            label(15, 30, 40, 10, "Text Filter", true, vi::LEFT);
            vi::Edit("##filter", P(59, 30), P(165, 42), row.filter, vi::CENTER);
            // Show Setup (sub_24E6D190): render objects, materials, levels,
            // scenes, places, sounds, arrays, meshes, textures.
            CKObject *object = context->GetObject(current);
            static const CK_CLASSID SETUPS[] = {CKCID_RENDEROBJECT, CKCID_MATERIAL, CKCID_LEVEL,   CKCID_SCENE, CKCID_PLACE,
                                                CKCID_WAVESOUND,    CKCID_DATAARRAY, CKCID_MESH, CKCID_TEXTURE};
            bool setup = false;
            for (CK_CLASSID setupClass : SETUPS) setup = setup || (object && CKIsChildClassOf(object, setupClass));
            if (vi::Button("##showsetup", P(170, 30), P(215, 40), "Show Setup", setup))
                m_State.events.Post(ShowObjectSetup{m_State.document->Reference(current)});
        }
        break;
    }
    case ParameterUI::Script: {
        // CK2UI's Script UI (dialog 158): the owner (the objects of the
        // class with Scripts), its class, its Scripts; a new owner takes its
        // first Script.
        const CK_ID current = ValueOf<CK_ID>(work);
        objectCombo("##owner", 15, 0, 120, row.ownerSet ? row.owner : (CK_ID)-1,
                    [&] { return ScriptOwners(*m_State.document, row.ownerClass); },
                    [&](CK_ID id) {
                        row.owner = id;
                        row.ownerSet = true;
                        const ObjectChoices scripts = ScriptsOf(*m_State.document, id);
                        SetValueOf<CK_ID>(work, scripts.empty() ? 0 : scripts.front().first);
                    });
        const auto classes = DerivedClasses(CKCID_BEOBJECT);
        int index = -1;
        for (size_t i = 0; i < classes.size(); ++i)
            if (classes[i].first == row.ownerClass) index = static_cast<int>(i);
        const char *className = CKClassIDToString(row.ownerClass);
        const int chosen = vi::Combo("##ownerclass", P(135, 0), P(210, 12), className ? className : "",
                                     [&] {
                                         std::vector<std::string> names;
                                         for (const auto &entry : classes) names.push_back(entry.second);
                                         return names;
                                     },
                                     index);
        if (chosen >= 0) {
            row.ownerClass = classes[chosen].first;
            row.ownerSet = false;
            row.owner = 0;
        }
        objectCombo("##script", 15, 15, 195, current,
                    [&] { return ScriptsOf(*m_State.document, row.ownerSet ? row.owner : 0); },
                    [&](CK_ID id) { SetValueOf(work, id); });
        break;
    }
    case ParameterUI::Message: {
        // An edit combo of the message types (CBS_DROPDOWN | CBS_SORT): a
        // name typed is a new type (sub_24E6F290: AddMessageType; nothing
        // for an empty name, -1).
        CKMessageManager *messages = context->GetMessageManager();
        const int current = ValueOf<int>(work);
        const char *name = messages ? messages->GetMessageTypeName(current) : nullptr;
        std::string text = AnsiToUtf8(name ? name : "");
        if (vi::EditCombo("##message", P(15, 0), P(210, 12), text, [&] {
                std::vector<std::string> names;
                for (int i = 0; messages && i < messages->GetMessageTypeCount(); ++i) {
                    const char *type = messages->GetMessageTypeName(i);
                    if (type && *type) names.push_back(AnsiToUtf8(type));
                }
                std::sort(names.begin(), names.end(),
                          [](const std::string &a, const std::string &b) { return _stricmp(a.c_str(), b.c_str()) < 0; });
                return names;
            })) {
            const std::string ansi = Utf8ToAnsi(text);
            SetValueOf<int>(work, ansi.empty() || !messages ? -1 : messages->AddMessageType(const_cast<CKSTRING>(ansi.c_str())));
            changed = true;
        }
        break;
    }
    case ParameterUI::Attribute: {
        // The attribute types (of the category chosen below) and their category.
        CKAttributeManager *attributes = context->GetAttributeManager();
        const int current = ValueOf<int>(work);
        const char *name = attributes ? attributes->GetAttributeNameByType(current) : nullptr;
        if (row.filter.empty() && attributes && current >= 0) {
            const char *category = attributes->GetAttributeCategory(current);
            row.filter = AnsiToUtf8(category ? category : "");
        }
        std::vector<int> types;
        const int chosen = vi::Combo("##attribute", P(15, 0), P(210, 12), AnsiToUtf8(name ? name : ""),
                                     [&] {
                                         std::vector<std::string> names;
                                         for (int t = 0; attributes && t < attributes->GetAttributeCount(); ++t) {
                                             if (!attributes->IsAttributeIndexValid(t)) continue;
                                             const char *category = attributes->GetAttributeCategory(t);
                                             if (!row.filter.empty() && AnsiToUtf8(category ? category : "") != row.filter) continue;
                                             const char *n = attributes->GetAttributeNameByType(t);
                                             types.push_back(t);
                                             names.push_back(AnsiToUtf8(n ? n : ""));
                                         }
                                         return names;
                                     },
                                     -1);
        if (chosen >= 0 && chosen < static_cast<int>(types.size())) {
            SetValueOf(work, types[chosen]);
            changed = true;
        }
        const int category = vi::Combo("##category", P(15, 15), P(170, 27), row.filter,
                                       [&] {
                                           std::vector<std::string> names;
                                           for (int c = 0; attributes && c < attributes->GetCategoriesCount(); ++c) {
                                               const char *n = attributes->GetCategoryName(c);
                                               names.push_back(AnsiToUtf8(n ? n : ""));
                                           }
                                           return names;
                                       },
                                       -1);
        if (category >= 0) row.filter = AnsiToUtf8(attributes->GetCategoryName(category) ? attributes->GetCategoryName(category) : "");
        label(170, 15, 38, 10, "Category", true, vi::CENTER);
        break;
    }
    case ParameterUI::Animation: {
        const CK_CLASSID cid = manager->TypeToClassID(work->GetType());
        objectCombo("##objects", 15, 0, 195, ValueOf<CK_ID>(work),
                    [&] { return ObjectsOfClass(*m_State.document, cid ? cid : CKCID_OBJECT, true, ""); },
                    [&](CK_ID id) { SetValueOf(work, id); });
        break;
    }
    case ParameterUI::Synchro: {
        // An edit combo of the objects of the class (sub_24E732D0): a name
        // typed is an object of the class, made at Apply when there is none
        // (synchro objects, critical sections and states); a synchro
        // object's Number Of Users.
        const CK_CLASSID cid = manager->TypeToClassID(work->GetType());
        if (vi::EditCombo("##synchro", P(15, 0), P(210, 12), row.text, [&] {
                std::vector<std::string> names;
                for (const auto &choice : ObjectsOfClass(*m_State.document, cid ? cid : CKCID_OBJECT, true, ""))
                    if (choice.first) names.push_back(AnsiToUtf8(choice.second));
                return names;
            })) {
            const std::string ansi = Utf8ToAnsi(row.text);
            CKObject *named = ansi.empty() ? nullptr
                                           : context->GetObjectByNameAndClass(const_cast<CKSTRING>(ansi.c_str()), cid ? cid : CKCID_OBJECT);
            row.created.clear();
            if (!named && !ansi.empty() && (cid == CKCID_SYNCHRO || cid == CKCID_CRITICALSECTION || cid == CKCID_STATE))
                row.created = ansi;
            SetValueOf<CK_ID>(work, named ? named->GetID() : 0);
            if (CKSynchroObject *synchro = CKSynchroObject::Cast(named))
                row.users = std::to_string(synchro->GetRendezVousNumberOfWaiters());
            changed = true;
        }
        label(15, 15, 140, 8, "Number Of Users:", true, vi::RIGHT);
        if (vi::Edit("##users", P(155, 15), P(210, 27), row.users, vi::CENTER)) {
            row.usersSet = true;
            changed = true;
        }
        break;
    }
    case ParameterUI::Flags: {
        ImGui::SetCursorScreenPos(at);
        const auto size = UiSize(row, s);
        const auto action = ParameterDraftEditor(m_State, work, "##flags", size.x, size.y, 0, true, {},
            row.References(), &row.property, &m_Editors, unsigned(row.collapsed), &row.beforeInteraction);
        m_InputAction |= action;
        if (action & EDITOR_INPUT_CANCEL) row.invalid = false;
        if (action & EDITOR_INPUT_INVALID) row.invalid = true;
        else if (action & EDITOR_INPUT_CHANGED) { row.invalid = false; changed = true; }
        break;
    }
    case ParameterUI::Struct:
    case ParameterUI::ObjectArray: {
        if (!row.typed) {
            if (row.info.ui == ParameterUI::Struct) changed = DrawRows(row.members, at, s, false);
            else row.invalid = true;
            break;
        }
        struct Members { ParameterBox *box; Row *row; ImVec2 at; float scale; } members{this, &row, at, s};
        const auto draw = [](void *data, const EditorHost *, const EditorValueInput *input) -> uint32_t {
            auto &view = *static_cast<Members *>(data);
            auto &box = *view.box; auto &row = *view.row;
            const auto previous = box.m_InputAction; box.m_InputAction = 0;
            ImGui::BeginGroup();
            const bool changed = row.info.ui == ParameterUI::Struct ? box.DrawRows(row.members, view.at, view.scale, false) : box.ObjectArrayUi(row, *input, view.at, view.scale);
            ImGui::EndGroup();
            auto action = box.m_InputAction;
            if (ImGui::IsItemActive() || ImGui::IsItemActivated()) action |= EDITOR_INPUT_ACTIVE;
            if (ImGui::IsItemDeactivatedAfterEdit() && !(action & EDITOR_INPUT_ACTIVE)) action |= EDITOR_INPUT_ACCEPT;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) action |= EDITOR_INPUT_CANCEL;
            if (changed && row.info.ui == ParameterUI::Struct) {
                ParameterValueDescription description;
                auto *work = CKParameter::Cast(box.m_State.document->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing));
                if (!DescribeParameterValue(*box.m_State.document, work, description)) action |= EDITOR_INPUT_INVALID;
                else {
                    if (auto *value = row.References()) description.value = RememberParameterReferences(std::move(description.value), *value);
                    const std::vector<EditorField> fields{ParameterValueField(description.value)};
                    const auto borrowed = BorrowEditorFields(fields);
                    if (input->update.write(input->update.data, borrowed.data(), 1) != EDITOR_OK) action |= EDITOR_INPUT_INVALID;
                }
            }
            if (changed) action |= EDITOR_INPUT_CHANGED;
            box.m_InputAction = previous | action;
            return action;
        };
        ImGui::SetCursorScreenPos(at);
        const auto size = UiSize(row, s);
        const auto action = ParameterDraftEditor(m_State, work, "##members", size.x, size.y, 0, true, {},
            row.References(), &row.property, &m_Editors, 0, &row.beforeInteraction, draw, &members);
        m_InputAction |= action;
        if (action & EDITOR_INPUT_CANCEL) row.invalid = false;
        if (action & EDITOR_INPUT_INVALID) row.invalid = true;
        else if (action & EDITOR_INPUT_CHANGED) changed = true;
        break;
    }
    case ParameterUI::Curve2D:
    case ParameterUI::Dependencies:
    case ParameterUI::LodOptions: {
        if (!row.typed) { textEdit(15, 205, vi::LEFT); break; }
        ImGui::SetCursorScreenPos(at);
        const auto size = UiSize(row, s);
        const auto action = ParameterDraftEditor(m_State, work, "##specialized", size.x, size.y, 0, true, {},
            row.References(), &row.property, &m_Editors, unsigned(row.collapsed), &row.beforeInteraction);
        m_InputAction |= action;
        if (action & EDITOR_INPUT_CANCEL) row.invalid = false;
        if (action & EDITOR_INPUT_INVALID) row.invalid = true;
        else if (action & EDITOR_INPUT_CHANGED) { row.invalid = false; changed = true; }
        break;
    }
    default: textEdit(15, 205, vi::LEFT); break;
    }
    return changed;
}

// CK2UI's Object Array UI (dialog 2010, sub_24E6FF30 ... sub_24E702E0): a
// list (15, 5, 195 x 95) of Name, ID, Class and Dyn; a double click shows
// the Setup, Del removes, Ctrl+C / Ctrl+V copy and paste the objects, + and
// - (the keypad's) move the item down and up; the right click's menu: Add
// Object, Copy Object(s), Paste Object(s) (at the item clicked, else at the
// end).
bool ParameterBox::ObjectArrayUi(Row &row, const EditorValueInput &input, const ImVec2 &at, float s) {
    CKContext *context = m_State.host.Context();
    std::vector<ObjectRef> values;
    if (input.value.objects.count) values.assign(input.value.objects.data, input.value.objects.data + input.value.objects.count);
    auto *array = &values;
    bool changed = false;
    const ImVec2 listMin(at.x + 15.0f * DLU_X * s, at.y + 5.0f * DLU_Y * s);
    const ImVec2 listMax(listMin.x + 195.0f * DLU_X * s, listMin.y + 95.0f * DLU_Y * s);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(listMin, listMax, vi::Color(9));
    dl->AddRect(listMin, listMax, vi::Color(0));
    // The columns (InsertColumn: 120 left, 40 right, 80 and 35 centered).
    static const char *const HEADERS[4] = {"Name", "ID", "Class", "Dyn"};
    const float widths[4] = {120.0f * s, 40.0f * s, 80.0f * s, 35.0f * s};
    const float header = 16.0f * s, line = 15.0f * s;
    dl->PushClipRect(ImVec2(listMin.x + 1.0f, listMin.y + 1.0f), ImVec2(listMax.x - 1.0f, listMax.y - 1.0f), true);
    float x = listMin.x + 1.0f;
    for (int c = 0; c < 4; ++c) {
        const ImVec2 h0(x, listMin.y + 1.0f), h1(x + widths[c], listMin.y + 1.0f + header);
        dl->AddRectFilled(h0, h1, vi::Color(7));
        dl->AddLine(ImVec2(h1.x - 1.0f, h0.y), ImVec2(h1.x - 1.0f, h1.y), vi::Color(4));
        dl->AddLine(ImVec2(h0.x, h1.y - 1.0f), ImVec2(h1.x, h1.y - 1.0f), vi::Color(4));
        const float w = ImGui::CalcTextSize(HEADERS[c]).x;
        const float tx = c == 0 ? h0.x + 4.0f * s : c == 1 ? h1.x - w - 4.0f * s : (h0.x + h1.x - w) * 0.5f;
        dl->AddText(ImVec2(tx, h0.y + (header - ImGui::GetFontSize()) * 0.5f), vi::Color(0), HEADERS[c]);
        x += widths[c];
    }
    dl->PopClipRect();
    // The items, scrolled in a child under the header.
    const ImVec2 itemsMin(listMin.x + 1.0f, listMin.y + 1.0f + header);
    ImGui::SetCursorScreenPos(itemsMin);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##items", ImVec2(listMax.x - itemsMin.x - 1.0f, listMax.y - itemsMin.y - 1.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_None);
    ImDrawList *il = ImGui::GetWindowDrawList();
    const ImGuiIO &io = ImGui::GetIO();
    int menuAt = -1;
    const int count = int(array->size());
    for (int i = 0; i < count; ++i) {
        CKObject *object = m_State.document->Resolve((*array)[i]);
        const ImVec2 r0 = ImGui::GetCursorScreenPos(), r1(r0.x + widths[0] + widths[1] + widths[2] + widths[3], r0.y + line);
        ImGui::PushID(i);
        ImGui::InvisibleButton("##item", ImVec2(r1.x - r0.x, line), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !row.chosen.count(i))) {
            if (io.KeyShift && row.anchor >= 0) {
                if (!io.KeyCtrl) row.chosen.clear();
                for (int k = std::min(row.anchor, i); k <= std::max(row.anchor, i); ++k) row.chosen.insert(k);
            } else if (io.KeyCtrl) {
                if (!row.chosen.erase(i)) row.chosen.insert(i);
                row.anchor = i;
            } else {
                row.chosen = {i};
                row.anchor = i;
            }
            ImGui::SetKeyboardFocusHere(-1);
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && object)
            m_State.events.Post(ShowObjectSetup{m_State.document->Reference(object->GetID())});
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) menuAt = i;
        ImGui::PopID();
        const bool selected = row.chosen.count(i) != 0;
        if (selected) il->AddRectFilled(r0, r1, IM_COL32(10, 36, 106, 255));
        const ImU32 ink = selected ? vi::Color(9) : vi::Color(0);
        char id[32];
        std::snprintf(id, sizeof(id), "%d", static_cast<int>(m_State.document->NativeId((*array)[i], Document::ObjectScope::Existing)));
        const char *cells[4] = {object && object->GetName() ? object->GetName() : "", id,
                                object ? CKClassIDToString(object->GetClassID()) : "",
                                object && (object->GetObjectFlags() & CK_OBJECT_DYNAMIC) == CK_OBJECT_DYNAMIC ? "X" : "-"};
        float cx = r0.x;
        for (int c = 0; c < 4; ++c) {
            const std::string text = c == 0 ? AnsiToUtf8(cells[0]) : std::string(cells[c] ? cells[c] : "");
            const float w = ImGui::CalcTextSize(text.c_str()).x;
            const float tx = c == 0 ? cx + 4.0f * s : c == 1 ? cx + widths[c] - w - 4.0f * s : cx + (widths[c] - w) * 0.5f;
            il->PushClipRect(ImVec2(cx, r0.y), ImVec2(cx + widths[c] - 1.0f, r1.y), true);
            il->AddText(ImVec2(tx, r0.y + (line - ImGui::GetFontSize()) * 0.5f), ink, text.c_str());
            il->PopClipRect();
            cx += widths[c];
            il->AddLine(ImVec2(cx - 1.0f, r0.y), ImVec2(cx - 1.0f, r1.y), vi::Color(7)); // LVS_EX_GRIDLINES
        }
        il->AddLine(ImVec2(r0.x, r1.y - 1.0f), ImVec2(r1.x, r1.y - 1.0f), vi::Color(7));
    }
    // Below the items: the menu inserts at the end.
    const ImVec2 rest = ImGui::GetCursorScreenPos();
    const float restHeight = std::max(1.0f, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - rest.y);
    ImGui::InvisibleButton("##rest", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), restHeight),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) row.chosen.clear();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) menuAt = count;
    const bool focused = ImGui::IsWindowFocused();
    // The selected items' IDs, in the list's order.
    auto selection = [&] {
        std::vector<CK_ID> ids;
        for (int i : row.chosen)
            if (i >= 0 && i < int(array->size())) if (auto id = m_State.document->NativeId((*array)[i])) ids.push_back(id);
        return ids;
    };
    auto insert = [&](int position, const std::vector<CK_ID> &ids) {
        for (CK_ID id : ids)
            if (auto ref = m_State.document->Reference(id); m_State.document->Resolve(ref)) array->insert(array->begin() + std::clamp(position++, 0, int(array->size())), ref);
        row.chosen.clear();
        changed = true;
    };
    if (focused && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !row.chosen.empty()) {
            for (auto it = row.chosen.rbegin(); it != row.chosen.rend(); ++it)
                if (*it >= 0 && *it < int(array->size())) array->erase(array->begin() + *it);
            row.chosen.clear();
            changed = true;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && !row.chosen.empty())
            m_State.objectClipboard.SetObjects(*m_State.document, selection());
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) insert(int(array->size()), m_State.objectClipboard.Objects(*m_State.document));
        // + and -: the first selected item and the one below / above it change places.
        const bool down = ImGui::IsKeyPressed(ImGuiKey_KeypadAdd), up = ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract);
        if ((down || up) && !row.chosen.empty()) {
            const int from = *row.chosen.begin(), to = from + (down ? 1 : -1);
            if (to >= 0 && to < int(array->size())) {
                std::swap((*array)[from], (*array)[to]);
                row.chosen = {to};
                row.anchor = to;
                changed = true;
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (menuAt >= 0) {
        ImGui::OpenPopup("##arraymenu");
        row.anchor = menuAt; // where Add Object and Paste Object(s) insert
    }
    PushDevMenuStyle();
    bool choose = false;
    if (ImGui::BeginPopup("##arraymenu")) {
        if (ImGui::MenuItem("Add Object")) choose = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Copy Object(s)", nullptr, false, !row.chosen.empty()))
            m_State.objectClipboard.SetObjects(*m_State.document, selection());
        if (ImGui::MenuItem("Paste Object(s)", nullptr, false, !m_State.objectClipboard.Objects(*m_State.document).empty()))
            insert(std::clamp(row.anchor, 0, int(array->size())), m_State.objectClipboard.Objects(*m_State.document));
        ImGui::EndPopup();
    }
    PopDevMenuStyle();
    // Add Object: CKContext::ChooseObject, Dev's CKUIM_CHOOSEOBJECT
    // (Dev.exe CKUICallBackFct): the Edit Parameter dialog without its name
    // and type on an "Object" parameter (CKPGUID_OBJECT: the Object UI with
    // its class filter); OK inserts the object chosen.
    const char *chooser = "Edit Parameter###ChooseObject";
    if (choose) {
        if (!m_Chooser) m_Chooser = std::make_unique<ParameterBox>(m_State);
        m_Chooser->Clear();
        if (CKParameterOut *object = context->CreateCKParameterOut((CKSTRING) "Object", CKPGUID_OBJECT, TRUE)) {
            m_Chooser->Add(object, "Object");
            context->DestroyObject(object);
            ImGui::OpenPopup(chooser);
        }
    }
    if (m_Chooser && !m_Chooser->Empty()) {
        const DialogAction action = EditParameterValueDialog(chooser, "Edit Parameter", *m_Chooser);
        if (action == DialogAction::Ok) {
            const CK_ID chosen = ValueOf<CK_ID>(m_Chooser->Work(0));
            if (chosen) insert(std::clamp(row.anchor, 0, int(array->size())), {chosen});
        }
        if (action != DialogAction::None || !m_Chooser->Empty()) m_InputAction |= EDITOR_INPUT_ACTIVE;
        if (action != DialogAction::None) m_Chooser->Clear();
    }
    if (changed) {
        auto value = input.value; value.objects = {array->data(), uint32_t(array->size())};
        if (input.update.write(input.update.data, &value, 1) != EDITOR_OK) row.invalid = true;
    }
    return changed;
}

// The LOD Manager's LOD Options UI (dialog 2012, sub_24E6DE90 /
// sub_24E6E0F0 / sub_24E6E3F0): the Multiple Meshes, Character Animation,
// Patch Mesh and Alpha flags; the screen's and faces' (an animation's)
// proportions and the alpha's, in percent; Alpha shows its edits, Character
// Animation hides Multiple Meshes and Patch Mesh and names the second row
// Anims.
std::string ParameterBox::Text(size_t row) const {
    CKContext *context = m_State.host.Context();
    CKParameter *work = row < m_Rows.size() && context ? CKParameter::Cast(context->GetObject(m_Rows[row].work)) : nullptr;
    return work ? ParameterText(work) : std::string();
}

ParameterBox::Row *ParameterBox::RowOf(std::vector<Row> &rows, CK_ID work) {
    for (Row &row : rows) {
        if (row.work == work) return &row;
        if (Row *member = RowOf(row.members, work)) return member;
    }
    return nullptr;
}

ParameterBox::Row *ParameterBox::RowAt(std::vector<Row> &rows, const ImVec2 &mouse, CK_ID except) {
    for (Row &row : rows) {
        if (Row *member = RowAt(row.members, mouse, except)) return member;
        if (row.work != except && mouse.x >= row.drawnMin.x && mouse.y >= row.drawnMin.y && mouse.x < row.drawnMax.x &&
            mouse.y < row.drawnMax.y)
            return &row;
    }
    return nullptr;
}

CK_ID ParameterBox::ObjectAt(const ImVec2 &mouse, CK_CLASSID cid) const { return ObjectUnderMouse(m_State, mouse, cid); }

CK_ID ObjectUnderMouse(const EditorState &state, const ImVec2 &mouse, CK_CLASSID cid) {
    if (!cid) return 0;
    if (state.workspace && state.HasDocument()) {
        bool handled=false;
        const auto ref = state.workspace->Pick(mouse.x, mouse.y, &handled);
        if (auto *object = state.document->Resolve(ref))
            return CKIsChildClassOf(object->GetClassID(), cid) ? object->GetID() : 0;
        if(handled)return 0;
    }
    ImGuiContext &g = *GImGui;
    for (int i = g.Windows.Size - 1; i >= 0; --i) {
        ImGuiWindow *window = g.Windows[i];
        // Docked windows are child windows of their dock's host.
        if (!window->WasActive || window->Hidden || (window->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip)) ||
            ((window->Flags & ImGuiWindowFlags_ChildWindow) && !window->DockIsActive))
            continue;
        // A docked window behind another tab.
        if (window->DockNode && (!window->DockTabIsVisible || window->DockNode->VisibleWindow != window)) continue;
        if (!window->Rect().Contains(mouse)) continue;
        auto found = state.objectPick.find(window->Name);
        if (found != state.objectPick.end() && found->second) return found->second(mouse.x, mouse.y, cid);
    }
    return 0;
}

// The pick's end (CK2UI's WindowProc, WM_LBUTTONDOWN): pick mode 1 asks the
// window under the mouse for a parameter (0x606: another parameter UI gives
// its value), pick mode 2 the main window's child for an object of the
// class (0x607).
bool ParameterBox::PickFrom(Row &row, const ImVec2 &mouse) {
    if (row.info.pick & 1) {
        CKContext *context = m_State.host.Context();
        if (Row *other = RowAt(m_Rows, mouse, row.work))
            if (CKParameter::Cast(context->GetObject(other->work)) && PickResult(row, other->work)) return true;
    }
    const CK_ID picked = ObjectAt(mouse, row.info.pickClass);
    return picked && PickResult(row, picked);
}

// CK2UI's On_PickResult (sub_24E70420) and the UIs' own (sub_24E62BE0 ...):
// a parameter of a compatible type gives its value; else what the type
// takes from the object, Shift taking it relative to the parent (the
// position, orientation and matrix), the local box, the 2D entity's size.
bool ParameterBox::PickResult(Row &row, CK_ID pickedId) {
    CKContext *context = m_State.host.Context();
    CKParameter *work = CKParameter::Cast(context->GetObject(row.work));
    CKObject *picked = context->GetObject(pickedId);
    if (!work || !picked) return false;
    if (CKParameter *parameter = CKParameter::Cast(picked)) {
        if (parameter == work || !parameter->IsCompatibleWith(work)) return false;
        work->CopyValue(parameter, FALSE);
        // The combos' own state follows the value.
        if (row.info.ui == ParameterUI::Script || row.info.ui == ParameterUI::Synchro ||
            row.info.ui == ParameterUI::Dependencies || row.info.ui == ParameterUI::ObjectArray)
            InitRow(row, work);
        return true;
    }
    const bool shift = ImGui::GetIO().KeyShift;
    CK3dEntity *entity = CK3dEntity::Cast(picked);
    CK3dEntity *parent = entity && shift ? entity->GetParent() : nullptr;
    CK2dEntity *entity2d = CK2dEntity::Cast(picked);
    switch (row.info.ui) {
    case ParameterUI::Object: {
        // The object when its combo lists it, else NULL (sub_24E6CC10).
        const CK_CLASSID cid = row.extended ? row.filterClass : row.info.pickClass;
        CK_ID chosen = 0;
        for (const auto &choice : ObjectsOfClass(*m_State.document, cid, row.extended ? row.derived : true,
                                                 row.extended ? Utf8ToAnsi(row.filter) : ""))
            if (choice.first == pickedId) chosen = pickedId;
        if (ValueOf<CK_ID>(work) == chosen) return false;
        ParameterValueDescription description;
        if (!DescribeParameterValue(*m_State.document, work, description)) return false;
        description.value.object = m_State.document->Reference(chosen);
        if (!WriteParameterDraft(*m_State.document, work, description.value)) return false;
        if (auto *value = row.References()) *value = description.value;
        row.invalid = false;
        return true;
    }
    case ParameterUI::Script: {
        // The behavior's owner and its Scripts, the behavior chosen when it
        // is one of them, else NULL (sub_24E72000).
        CKBehavior *behavior = CKBehavior::Cast(picked);
        if (!behavior) return false;
        CKBeObject *owner = behavior->GetOwner();
        row.owner = owner ? owner->GetID() : 0;
        row.ownerSet = owner != nullptr;
        if (owner) row.ownerClass = owner->GetClassID();
        CK_ID chosen = 0;
        for (const auto &choice : ScriptsOf(*m_State.document, row.owner))
            if (choice.first == pickedId) chosen = pickedId;
        SetValueOf(work, chosen);
        return true;
    }
    case ParameterUI::Vector:
        if (!entity) return false;
        {
            VxVector position;
            entity->GetPosition(&position, parent);
            if (ValueOf<VxVector>(work) == position) return false;
            SetValueOf(work, position);
        }
        return true;
    case ParameterUI::Quaternion:
        if (!entity) return false;
        {
            VxQuaternion quaternion;
            entity->GetQuaternion(&quaternion, parent);
            SetValueOf(work, quaternion);
        }
        return true;
    case ParameterUI::EulerAngles:
        if (!entity) return false;
        {
            VxVector angles;
            Vx3DMatrixToEulerAngles(entity->GetWorldMatrix(), &angles.x, &angles.y, &angles.z);
            if (ValueOf<VxVector>(work) == angles) return false;
            SetValueOf(work, angles);
        }
        return true;
    case ParameterUI::Matrix:
        if (!entity) return false;
        SetValueOf(work, parent ? entity->GetLocalMatrix() : entity->GetWorldMatrix());
        return true;
    case ParameterUI::Box:
        if (!entity) return false;
        SetValueOf(work, entity->GetBoundingBox(shift ? TRUE : FALSE));
        return true;
    case ParameterUI::Vector2D:
        if (!entity2d) return false;
        {
            Vx2DVector value;
            if (shift) entity2d->GetSize(value);
            else entity2d->GetPosition(value);
            SetValueOf(work, value);
        }
        return true;
    case ParameterUI::Rect:
        if (!entity2d) return false;
        {
            VxRect rect;
            entity2d->GetRect(rect);
            SetValueOf(work, rect);
        }
        return true;
    case ParameterUI::Color:
        if (CKMaterial *material = CKMaterial::Cast(picked)) {
            SetValueOf(work, material->GetDiffuse());
            return true;
        }
        return false;
    default: return false;
    }
}

bool ParameterBox::RowChanged(const Row &row) const {
    CKContext *context = m_State.host.Context();
    CKParameter *work = CKParameter::Cast(context->GetObject(row.work));
    CKParameter *target = CKParameter::Cast(context->GetObject(row.target));
    if (!work || !target) return false;
    if (row.info.ui == ParameterUI::Struct) {
        const std::vector<CK_ID> members = MembersOf(*m_State.document, target);
        for (size_t i = 0; i < row.members.size() && i < members.size(); ++i) {
            if (RowChanged(row.members[i])) return true;
        }
        return false;
    }
    if (row.typed) {
        ParameterValueDescription current, previous;
        return DescribeParameterValue(*m_State.document, work, current) &&
            DescribeParameterValue(*m_State.document, target, previous) && current.value != previous.value;
    }
    if (row.info.ui == ParameterUI::Synchro && (!row.created.empty() || row.usersSet)) return true;
    if (ManagesMemory(context, work->GetType())) return ParameterText(work) != ParameterText(target);
    return ParameterBytes(work) != ParameterBytes(target);
}

bool ParameterBox::Changed() const {
    for (const Row &row : m_Rows)
        if ((row.targetReference && !TargetsCurrent(row)) || RowChanged(row)) return true;
    return false;
}

size_t ParameterBox::PrepareRow(const Row &row) {
    auto *context = m_State.host.Context();
    auto *work = CKParameter::Cast(m_State.document->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing));
    if (!work) { m_ApplyFailed = true; return 0; }
    size_t steps = 0;
    for (const auto &member : row.members) { steps += PrepareRow(member); if (m_ApplyFailed) return steps; }
    if (row.info.ui == ParameterUI::Synchro) {
        // The object typed in made first (sub_24E732D0: CreateObject, added
        // to the level), then a synchro object's Number Of Users.
        const CK_CLASSID cid = context->GetParameterManager()->TypeToClassID(work->GetType());
        if (!row.created.empty()) {
            const EditResult made = CreateNamedObject(*m_State.document, cid, row.created);
            if (made.ok) {
                ++steps;
                SetValueOf<CK_ID>(work, made.created);
            } else {
                m_ApplyFailed = true;
                m_State.Log(LogLevel::Warning, "Edit Parameters: " + AnsiToUtf8(made.message));
                return steps;
            }
        }
        int users = 0;
        CKSynchroObject *synchro = CKSynchroObject::Cast(context->GetObject(ValueOf<CK_ID>(work)));
        if (row.usersSet && synchro && ParseInt(row.users, users) && users != synchro->GetRendezVousNumberOfWaiters()) {
            const EditResult set = EditObjectState(*m_State.document, synchro->GetID(), "Set Number Of Users",
                                                   [users](CKObject *object) {
                                                       static_cast<CKSynchroObject *>(object)->SetRendezVousNumberOfWaiters(users);
                                                   });
            if (set.ok) ++steps;
            else if (!set.Unchanged()) m_ApplyFailed = true;
        }
    }
    return steps;
}

size_t ParameterBox::ApplyRow(const Row &row) {
    auto *work = CKParameter::Cast(m_State.document->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing));
    auto *target = CKParameter::Cast(m_State.document->Resolve(row.targetReference, CKCID_PARAMETER));
    if (!work || !target || work->GetType() != target->GetType() || row.invalid ||
        (row.targetReference && !m_State.document->Resolve(row.targetReference)) ||
        !m_State.document->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing)) {
        m_ApplyFailed = true;
        return 0;
    }
    size_t steps = PrepareRow(row);
    if (m_ApplyFailed) return 0;
    ParameterValueDescription description;
    const bool described = DescribeParameterValue(*m_State.document, work, description);
    if (row.typed && !described) { m_ApplyFailed = true; return 0; }
    if (described) {
        if (auto *value = row.References()) description.value = RememberParameterReferences(std::move(description.value), *value);
        if (!m_State.workspace || row.property.id.empty()) { m_ApplyFailed = true; return 0; }
        const auto fields = ParameterFields(description);
        const auto borrowed = BorrowEditorFields(fields);
        const auto value = borrowed.values.front();
        std::vector<EditorField> contextValues{fields[2]};
        if (description.value.kind == ParameterValueKind::Structure) contextValues.push_back(row.schema);
        auto &app = m_State.workspace->Application();
        const auto result = app.ApplyEdit(row.property, app.Scope(), row.targetReference, value,
            contextValues);
        if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE) {
            m_ApplyFailed = true;
            m_State.Log(LogLevel::Warning, "Edit Parameters: " + result.message);
        }
        return steps + (result.status == EDITOR_OK ? 1 : 0);
    }
    if (!m_State.workspace || m_CopyProvider.empty()) { m_ApplyFailed = true; return 0; }
    const EditorValue values[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, row.targetReference},
        {EDITOR_TEXT("source"), EDITOR_OBJECT, row.workReference}};
    auto &app = m_State.workspace->Application();
    const auto result = app.ApplyEdit("parameter.copy-value", {app.Scope(), values, 2, EditorText(m_CopyProvider)});
    if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE) m_ApplyFailed = true;
    return steps + (result.status == EDITOR_OK ? 1 : 0);
}

DialogAction EditParametersDialog(const char *id, const std::string &title, ParameterBox &box, bool apply) {
    if (!ImGui::IsPopupOpen(id)) return DialogAction::None;
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 rows = box.Size(s, true);
    // The panel 10 from the sides and 6 below the title, the rows 2 inside
    // it; the buttons (60 x 18) 10 apart, 10 from the right, 4 below.
    const float inset = 2.0f * s;
    float width = rows.x + 2.0f * (10.0f * s + inset);
    width = std::max(width, vi::TextWidth(title.c_str(), true) + 40.0f * s);
    width = std::max(width, (apply ? 3 : 2) * 70.0f * s + 10.0f * s);
    const float panelBottom = 6.0f * s + rows.y + 2.0f * inset;
    const ImVec2 client(width, panelBottom + 4.0f * s + 18.0f * s + 5.0f * s);
    ImVec2 o;
    if (!vi::BeginDialog(id, title, client, s, o)) return DialogAction::None;
    const ImVec2 panelMin(o.x + 10.0f * s, o.y + 6.0f * s);
    const ImVec2 panelMax(o.x + width - 10.0f * s, o.y + panelBottom);
    vi::Panel(panelMin, panelMax, s);
    box.Draw(ImVec2(panelMin.x + inset, panelMin.y + inset), s, true);
    // The panel's outline over the rows' corners.
    ImGui::GetWindowDrawList()->AddRect(panelMin, panelMax, IM_COL32(34, 34, 34, 255), 7.0f * s, 0, 1.5f * s);
    float x = o.x + width - 10.0f * s - 60.0f * s;
    const float y = o.y + panelBottom + 4.0f * s;
    DialogAction action = DialogAction::None;
    if (vi::DialogButton("Cancel", ImVec2(x, y), s, ImGuiKey_Escape) && !box.InputActive()) action = DialogAction::Cancel;
    ImGui::BeginDisabled(!box.Valid());
    if (apply) {
        x -= 70.0f * s;
        if (vi::DialogButton("Apply", ImVec2(x, y), s) && box.Valid() && !box.InputActive()) action = DialogAction::Apply;
    }
    x -= 70.0f * s;
    if (vi::DialogButton("OK", ImVec2(x, y), s, ImGuiKey_Enter) && box.Valid() && !box.InputActive()) action = DialogAction::Ok;
    ImGui::EndDisabled();
    if (action == DialogAction::Ok || action == DialogAction::Cancel) ImGui::CloseCurrentPopup();
    vi::EndDialog();
    return action;
}

DialogAction EditParameterValueDialog(const char *id, const std::string &title, ParameterBox &box, const char *typeName) {
    if (!ImGui::IsPopupOpen(id)) return DialogAction::None;
    const float s = ImGui::GetFontSize() / 13.0f;
    // Dev.exe sub_589D50's layout: 265 pixels at least, the rows from (16,
    // 11), 21 apart.
    const ImVec2 rows = box.Size(s, false);
    const float width = std::max(265.0f * s, rows.x + 2.0f * s) + 2.0f * 6.0f * s + 20.0f * s;
    const float X = 16.0f * s, typeY = 11.0f * s, rowY = typeName ? typeY + 21.0f * s : typeY;
    const float panelBottom = rowY + rows.y + 2.0f * s + 5.0f * s + 1.0f * s;
    const ImVec2 client(width, panelBottom + 4.0f * s + 18.0f * s + 5.0f * s);
    ImVec2 o;
    if (!vi::BeginDialog(id, title, client, s, o)) return DialogAction::None;
    vi::Panel(ImVec2(o.x + 10.0f * s, o.y + 5.0f * s), ImVec2(o.x + width - 10.0f * s, o.y + panelBottom), s);
    if (typeName) {
        const float right = width - 16.0f * s, field = 165.0f * s;
        vi::Text(ImVec2(o.x + X, o.y + typeY + 2.0f * s), ImVec2(o.x + X + 120.0f * s, o.y + typeY + 16.0f * s),
                 "Parameter Type :", true);
        vi::Text(ImVec2(o.x + right - field, o.y + typeY + 3.0f * s), ImVec2(o.x + right, o.y + typeY + 16.0f * s), typeName);
    }
    box.Draw(ImVec2(o.x + X + 1.0f * s, o.y + rowY + 1.0f * s), s, false);
    ImGui::GetWindowDrawList()->AddRect(ImVec2(o.x + X, o.y + rowY), ImVec2(o.x + X + rows.x + 2.0f * s, o.y + rowY + rows.y + 2.0f * s),
                                        vi::Color(0));
    float x = o.x + width - 10.0f * s - 60.0f * s;
    const float y = o.y + panelBottom + 4.0f * s;
    DialogAction action = DialogAction::None;
    if (vi::DialogButton("Cancel", ImVec2(x, y), s, ImGuiKey_Escape) && !box.InputActive()) action = DialogAction::Cancel;
    x -= 70.0f * s;
    ImGui::BeginDisabled(!box.Valid());
    if (vi::DialogButton("OK", ImVec2(x, y), s, ImGuiKey_Enter) && box.Valid() && !box.InputActive()) action = DialogAction::Ok;
    ImGui::EndDisabled();
    if (action != DialogAction::None) ImGui::CloseCurrentPopup();
    vi::EndDialog();
    return action;
}

bool ParameterBox::Valid() const {
    const auto valid = [&](const auto &self, const Row &row) -> bool {
        return !row.invalid && std::all_of(row.members.begin(), row.members.end(), [&](const Row &member) { return self(self, member); });
    };
    return std::all_of(m_Rows.begin(), m_Rows.end(), [&](const Row &row) { return valid(valid, row); });
}
size_t ParameterBox::Apply(CK_ID script) {
    if (!m_State.HasDocument() || Stale() || !Valid()) return 0;
    for (const auto &row : m_Rows) if (!TargetsCurrent(row) || !ValuesCurrent(row)) return 0;
    Edit edit(*m_State.document, "Edit Parameters");
    m_ApplyFailed = false;
    size_t steps = 0;
    for (const auto &row : m_Rows) {
        steps += ApplyRow(row);
        if (m_ApplyFailed) break;
    }
    if (m_ApplyFailed) { edit.Cancel(); return 0; }
    if (steps) edit.Touches(script);
    const auto result = edit.Commit();
    if (!result.ok) return 0;
    const auto accepted = [&](const auto &self, Row &row) -> void {
        row.created.clear(); row.usersSet = false; row.beforeInteraction = {};
        for (auto &member : row.members) self(self, member);
    };
    for (auto &row : m_Rows) accepted(accepted, row);
    return steps;
}

} // namespace vdev

// Parameter values and dependency options share the native value editor. Its
// working parameters live only as long as this workspace overlay is open.
#include "workspace/EditorClient.h"
#include "editor/ImGuiDrawing.h"
namespace vdev {
namespace {
struct ParameterDialog {
    EditorState &state;
    std::unique_ptr<ParameterBox> box;
    EditorView view=0;
    EditorScope scope{};
    ObjectRef target{};
    std::vector<ObjectRef> objects;
    int kind=0;
    bool open=false,shown=false;
    ParameterDialog(EditorState &s):state(s),box(std::make_unique<ParameterBox>(s)){}
    void Clear(){box->Clear();objects.clear();target={};scope={};open=shown=false;view=0;}
    static EditorStatus Navigate(void *data,const EditorHost *host,const EditorRequest *request){
        auto &d=*static_cast<ParameterDialog*>(data);if(!d.state.document||d.state.document->Token()!=request->scope.document)return EDITOR_STALE;
        ObjectRef target{};std::vector<ObjectRef> objects;int kind=0;
        for(uint32_t i=0;i<request->count;++i){const auto&v=request->values[i];const std::string key(v.key.data,v.key.size);
            if(key=="target")target=v.object;else if(key=="objects"&&v.objects.count)objects.assign(v.objects.data,v.objects.data+v.objects.count);else if(key=="kind")kind=int(v.integer);}
        if(kind<0||kind>2)return EDITOR_INVALID;
        for(auto object:objects)if(!d.state.document->Resolve(object))return EDITOR_STALE;
        auto *parameter=CKParameter::Cast(d.state.document->Resolve(target));
        if(kind==0&&!parameter)return EDITOR_STALE;
        if(kind==1&&!d.state.document->Resolve(target))return EDITOR_STALE;
        auto next=std::make_unique<ParameterBox>(d.state);
        bool added=false;
        if(kind==0)added=next->Add(parameter,parameter->GetName()?parameter->GetName():"");
        else {
            CKDependencies options;
            if(kind==1)options=d.state.CopyDependencies();
            else{
                EditorClient client(host,d.view);const auto result=client.Query("object.delete-options");if(result.status!=EDITOR_OK)return result.status;
                for(const auto&f:result.fields){if(f.key=="flags")options.m_Flags=CK_DEPENDENCIES_FLAGS(f.integer);else if(f.key=="options"){options.Resize(int(f.integers.size()));for(size_t i=0;i<f.integers.size();++i)options[int(i)]=CKDWORD(f.integers[i]);}}
            }
            added=next->AddDependencies(kind==1?"Copy options":"Delete Options",kind==1?CKPGUID_COPYDEPENDENCIES:CKPGUID_DELETEDEPENDENCIES,options);
        }
        if(!added)return EDITOR_ERROR;
        d.box=std::move(next);d.objects=std::move(objects);d.target=target;d.kind=kind;d.scope=request->scope;
        d.open=true;d.shown=false;return EDITOR_OK;
    }
    static EditorStatus Open(void *data,const EditorHost *host,const EditorRequest *request,EditorSink sink){
        const int kind=*static_cast<const int*>(data);std::vector<EditorValue> values(request->values,request->values+request->count);
        values.push_back({EDITOR_TEXT("kind"),EDITOR_INTEGER_VALUE,{},{},uint64_t(kind)});
        const EditorRequest forwarded{request->scope,values.data(),uint32_t(values.size()),{},request->operation};
        return host->navigateView(host->data,EditorString EDITOR_TEXT("parameter.dialog"),0,&forwarded,sink);
    }
    static EditorStatus Accept(void *data,const EditorHost *host,const EditorRequest *request,EditorSink sink){
        auto&d=*static_cast<ParameterDialog*>(data);if(!d.open||!d.state.document||d.state.document->Token()!=d.scope.document||request->scope.view!=d.view)return EDITOR_STALE;
        if(d.kind==0){if(!d.state.document->Resolve(d.target)){d.box->Clear();d.open=false;return EDITOR_STALE;}if(!d.box->Valid())return EDITOR_INVALID;const bool changed=d.box->Changed();if(!d.box->Apply(0)&&changed)return EDITOR_ERROR;d.box->Clear();d.open=false;return EDITOR_OK;}
        CKDependencies options;if(!d.box->Dependencies(0,options))return EDITOR_INVALID;
        std::vector<uint64_t> flags;for(int i=0;i<options.Size();++i)flags.push_back(options[i]);
        const EditorValue values[]={ObjectsArgument("objects",d.objects),ObjectArgument("target",d.target),IntegerArgument("custom",1),IntegerArgument("flags",uint64_t(options.m_Flags)),
            {EDITOR_TEXT("options"),EDITOR_INTEGER_LIST,{},{},0,{}, {flags.data(),uint32_t(flags.size())}}};
        std::vector<EditorValue> arguments(values,values+5);if(d.kind==2)arguments.erase(arguments.begin()+1);
        const EditorRequest forwarded{request->scope,arguments.data(),uint32_t(arguments.size()),{},request->operation};
        const auto status=host->execute(host->data,d.kind==1?EditorString EDITOR_TEXT("scene.duplicate"):EditorString EDITOR_TEXT("object.destroy"),&forwarded,sink);
        if((status==EDITOR_OK||status==EDITOR_NO_CHANGE)&&d.kind==2)d.state.selection.Clear();
        d.box->Clear();d.open=false;
        return status;
    }
    void Draw(const EditorHost *host,EditorViewFrame &frame){
        if(!open){frame.close=1;return;}
        const char*id=kind==0?"Edit Parameter###AttributeManagerValue":kind==1?"Edit Parameter###LevelCopyOptions":"Select Delete Options###DeleteOptions";
        if(!shown){ImGui::OpenPopup(id);shown=true;}
        const auto action=EditParameterValueDialog(id,kind==2?"Select Delete Options":"Edit Parameter",*box);
        if(action==DialogAction::Cancel){box->Clear();open=false;frame.close=1;}
        else if(action==DialogAction::Ok){EditorClient client(host,view);client.Command("parameter.accept");}
        frame.active=open;
    }
};
}
bool RegisterParameterDialogs(EditorApplication &editor,EditorState &state,std::string &error){
    auto *data=new ParameterDialog(state);
    const EditorParameter parameter{EDITOR_TEXT("target"),EDITOR_OBJECT};
    const EditorParameter objects{EDITOR_TEXT("objects"),EDITOR_OBJECT_LIST};
    const EditorParameter copy[]={objects,parameter};
    static int parameterKind=0,copyKind=1,deleteKind=2;
    const EditorCommandDesc commands[]={
        {sizeof(EditorCommandDesc),EDITOR_TEXT("parameter.edit"),EDITOR_TEXT("builtin.parameters"),EDITOR_TEXT("Edit Parameter"),&parameter,1,ParameterDialog::Open,&parameterKind,EDITOR_DOCUMENT,EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc),EDITOR_TEXT("scene.copy-options"),EDITOR_TEXT("builtin.parameters"),EDITOR_TEXT("Copy Options"),copy,2,ParameterDialog::Open,&copyKind,EDITOR_DOCUMENT,EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc),EDITOR_TEXT("object.confirm-delete"),EDITOR_TEXT("builtin.parameters"),EDITOR_TEXT("Select Delete Options"),&objects,1,ParameterDialog::Open,&deleteKind,EDITOR_DOCUMENT,EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc),EDITOR_TEXT("parameter.accept"),EDITOR_TEXT("builtin.parameters"),EDITOR_TEXT("Edit Parameter"),nullptr,0,ParameterDialog::Accept,data,EDITOR_DOCUMENT,EDITOR_DOCUMENT_CHANGE}};
    const EditorParameter args[]={{EDITOR_TEXT("target"),EDITOR_OBJECT,1},{EDITOR_TEXT("objects"),EDITOR_OBJECT_LIST,1},{EDITOR_TEXT("kind"),EDITOR_INTEGER_VALUE}};
    EditorViewDesc view{};view.size=sizeof(view);view.imguiVersion=EDITOR_IMGUI_VERSION;view.id=EDITOR_TEXT("parameter.dialog");view.provider=EDITOR_TEXT("builtin.parameters");view.title=EDITOR_TEXT("Edit Parameter");view.kind=EDITOR_OVERLAY_VIEW;view.singleInstance=1;view.area=EDITOR_DOCUMENT;
    view.data=data;view.create=[](void*data,const EditorHost*,EditorView id)->void*{auto*d=static_cast<ParameterDialog*>(data);d->view=id;return d;};view.destroy=[](void*data){static_cast<ParameterDialog*>(data)->Clear();};
    view.draw=[](void*data,const EditorHost*host,EditorView,EditorViewFrame*frame){static_cast<ParameterDialog*>(data)->Draw(host,*frame);};view.parameters=args;view.count=3;view.navigate=ParameterDialog::Navigate;
    EditorModuleDesc module{};module.size=sizeof(module);module.version=EDITOR_VERSION;module.id=EDITOR_TEXT("parameters.dialogs");module.data=data;module.stop=[](void*data){delete static_cast<ParameterDialog*>(data);};module.commands=commands;module.commandCount=4;module.views=&view;module.viewCount=1;
    if(editor.Register(module,error))return true;delete data;return false;
}
} // namespace vdev
