// Dev's parameter box and CK2UI.dll's parameter UIs, after their dialog
// templates (Managers\CK2UI.dll dialogs 134 to 2012, in dialog units of MS
// Sans Serif 8: 1.5 x 1.625 pixels).
#include "ui/controls/ParameterBox.h"
#include "core/level/SetupCommands.h"

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
#include "core/graph/ParameterPicking.h"
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

bool ParameterBox::AddDefault(uint64_t type, const std::string &label) {
    const CKGUID guid(CKDWORD(type >> 32), CKDWORD(type));
    if (auto *work = Work(0); work && work->GetGUID() == guid)
        return true;
    Clear();
    auto *context = m_State.host.Context();
    if (!context || context->GetParameterManager()->ParameterGuidToType(guid) < 0)
        return false;
    auto *work = context->CreateCKParameterOut(Utf8ToAnsi(label).c_str(), guid, TRUE);
    if (!work)
        return false;
    Row row;
    row.owned = true;
    row.label = label;
    InitRow(row, work);
    AddRow(std::move(row));
    return true;
}
bool ParameterBox::AddDraft(const ParameterBox &source, const std::string &label) {
    return Add(source.Work(0), Utf8ToAnsi(label));
}
bool ParameterBox::CopyDraft(const ParameterBox &source) {
    auto *to = Work(0), *from = source.Work(0);
    if (!to || !from || !to->IsDynamic() || !from->IsDynamic() || to->GetGUID() != from->GetGUID() ||
        to->CopyValue(from, FALSE) != CK_OK)
        return false;
    InitRow(m_Rows.front(), to);
    return true;
}
ObjectRef ParameterBox::WorkReference(size_t row) const {
    return row < m_Rows.size() ? m_Rows[row].workReference : ObjectRef{};
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
        auto value = ParameterDraftValue(std::move(description.value), *remembered);
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

bool ParameterBox::Add(ObjectRef target, const std::string &label) {
    if (!m_State.HasDocument())
        return false;
    return Add(CKParameter::Cast(m_State.document->Resolve(target, CKCID_PARAMETER)), Utf8ToAnsi(label));
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
                    if (auto *value = row.References()) description.value = ParameterDraftValue(std::move(description.value), *value);
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
    case ParameterUI::Time:
    case ParameterUI::Key:
    case ParameterUI::Script:
    case ParameterUI::Message:
    case ParameterUI::Attribute:
    case ParameterUI::Animation:
    case ParameterUI::Synchro:
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
bool ParameterBox::ObjectArrayUi(Row &row, const EditorValueInput &input, const ImVec2 &at, float) {
    ImGui::SetCursorScreenPos(at);
    const auto action = row.arrayInput.Draw(m_State.workspace->Application().Host(), input);
    m_InputAction |= action;
    if (action & EDITOR_INPUT_INVALID)
        row.invalid = true;
    return action & EDITOR_INPUT_CHANGED;
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
        for (const auto ref : state.workspace->Pick(mouse.x, mouse.y, &handled))
            if (auto *object = state.document->Resolve(ref); object && CKIsChildClassOf(object->GetClassID(), cid))
                return object->GetID();
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
    auto *context = m_State.host.Context();
    auto *work = CKParameter::Cast(context->GetObject(row.work));
    if (!m_State.document ||
        !PickParameterDraft(*m_State.document, work, context->GetObject(pickedId), ImGui::GetIO().KeyShift,
                            row.extended ? row.filterClass : row.info.pickClass, row.extended ? row.derived : true,
                            row.extended ? Utf8ToAnsi(row.filter) : ""))
        return false;
    ParameterValueDescription description;
    if (row.typed && DescribeParameterValue(*m_State.document, work, description)) {
        if (auto *value = row.References())
            *value = std::move(description.value);
        row.beforeInteraction = {};
    } else if (row.info.ui == ParameterUI::Dependencies || row.info.ui == ParameterUI::ObjectArray)
        InitRow(row, work);
    row.invalid = false;
    return true;
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
            DescribeParameterValue(*m_State.document, target, previous) &&
            ParameterDraftValue(std::move(current.value), *row.References()) != previous.value;
    }
    if (ManagesMemory(context, work->GetType())) return ParameterText(work) != ParameterText(target);
    return ParameterBytes(work) != ParameterBytes(target);
}

bool ParameterBox::Changed() const {
    for (const Row &row : m_Rows)
        if ((row.targetReference && !TargetsCurrent(row)) || RowChanged(row)) return true;
    return false;
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
    size_t steps = 0;
    ParameterValueDescription description;
    const bool described = DescribeParameterValue(*m_State.document, work, description);
    if (row.typed && !described) { m_ApplyFailed = true; return 0; }
    if (described) {
        if (auto *value = row.References()) description.value = ParameterDraftValue(std::move(description.value), *value);
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
    if (!result.ok && !result.Unchanged()) return 0;
    const auto accepted = [&](const auto &self, Row &row) -> void {
        row.beforeInteraction = {};
        for (auto &member : row.members) self(self, member);
    };
    for (auto &row : m_Rows) {
        accepted(accepted, row);
        if (!row.typed) continue;
        auto *target = CKParameter::Cast(m_State.document->Resolve(row.targetReference, CKCID_PARAMETER));
        auto *work = CKParameter::Cast(m_State.document->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing));
        ParameterValueDescription value;
        if (target && work && DescribeParameterValue(*m_State.document, target, value) && WriteParameterDraft(*m_State.document, work, value.value))
            if (auto *remembered = row.References()) *remembered = std::move(value.value);
    }
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
    ObjectRef target{}, bitmapOptions{}, attributeOwner{};
    int attributeType = -1;
    std::string attributeTypeName;
    std::vector<ObjectRef> objects;
    int kind=0;
    uint64_t bitmapRevision = 0;
    bool open=false,shown=false;
    ParameterDialog(EditorState &s):state(s),box(std::make_unique<ParameterBox>(s)){}
    ~ParameterDialog() { Clear(); }
    void ClearBitmapOptions() {
        if (bitmapOptions && state.document)
            if (auto *parameter =
                    state.document->Resolve(bitmapOptions, CKCID_PARAMETER, Document::ObjectScope::Existing))
                state.document->Context()->DestroyObject(parameter);
        bitmapOptions = {};
    }
    void Clear() {
        box->Clear();
        ClearBitmapOptions();
        objects.clear();
        target = {};
        attributeOwner = {};
        attributeType = -1;
        attributeTypeName.clear();
        scope = {};
        open = shown = false;
        view = 0;
    }
    static EditorStatus Navigate(void *data,const EditorHost *host,const EditorRequest *request){
        auto &d=*static_cast<ParameterDialog*>(data);if(!d.state.document||d.state.document->Token()!=request->scope.document)return EDITOR_STALE;
        ObjectRef target{};std::vector<ObjectRef> objects;int kind=0;
        for(uint32_t i=0;i<request->count;++i){const auto&v=request->values[i];const std::string key(v.key.data,v.key.size);
            if(key=="target")target=v.object;else if(key=="objects"&&v.objects.count)objects.assign(v.objects.data,v.objects.data+v.objects.count);else if(key=="kind")kind=int(v.integer);}
        if (kind < 0 || kind > 4)
            return EDITOR_INVALID;
        for(auto object:objects)if(!d.state.document->Resolve(object))return EDITOR_STALE;
        auto *parameter=CKParameter::Cast(d.state.document->Resolve(target));
        if ((kind == 0 || kind == 4) && !parameter)
            return EDITOR_STALE;
        if(kind==1&&!d.state.document->Resolve(target))return EDITOR_STALE;
        auto next=std::make_unique<ParameterBox>(d.state);
        bool added=false;
        ObjectRef bitmapOptions{};
        ObjectRef attributeOwner{};
        int attributeType = -1;
        std::string attributeTypeName;
        const auto destroy = [&](CKParameterOut *value) {
            if (value)
                d.state.document->Context()->DestroyObject(value);
        };
        std::unique_ptr<CKParameterOut, decltype(destroy)> ownedOptions(nullptr, destroy);
        if (kind == 4) {
            if (objects.size() != 1)
                return EDITOR_INVALID;
            auto *owner = CKBeObject::Cast(d.state.document->Resolve(objects.front()));
            if (!owner)
                return EDITOR_STALE;
            for (int i = 0; i < owner->GetAttributeCount(); ++i)
                if (owner->GetAttributeParameterByIndex(i) == parameter) {
                    attributeType = owner->GetAttributeType(i);
                    break;
                }
            if (attributeType < 0)
                return EDITOR_STALE;
            attributeOwner = objects.front();
            auto *context = d.state.document->Context();
            const auto *typeName = context->GetParameterManager()->ParameterTypeToName(parameter->GetType());
            attributeTypeName = typeName ? AnsiToUtf8(typeName) : std::string();
            const auto *name = parameter->GetName();
            if (!name || !*name)
                name = context->GetAttributeManager()->GetAttributeNameByType(attributeType);
            added = next->Add(parameter, name ? name : "");
        } else if (kind == 3) {
            auto *bitmap = d.state.document->Resolve(target);
            if (!bitmap || (!CKTexture::Cast(bitmap) && !CKSprite::Cast(bitmap)))
                return EDITOR_STALE;
            auto *options = ReadBitmapOptions(*d.state.document, bitmap);
            if (!options)
                return EDITOR_REFUSED;
            ownedOptions.reset(options);
            options->ModifyObjectFlags(CK_OBJECT_PRIVATE, 0);
            bitmapOptions = d.state.document->Reference(options->GetID());
            added = next->Add(options, "Format Options");
        } else if (kind == 0)
            added = next->Add(parameter, parameter->GetName() ? parameter->GetName() : "");
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
        d.box->Clear();
        d.ClearBitmapOptions();
        d.box = std::move(next);
        d.bitmapOptions = bitmapOptions;
        d.bitmapRevision = d.state.document->ChangeRevision();
        d.attributeOwner = attributeOwner;
        d.attributeType = attributeType;
        d.attributeTypeName = std::move(attributeTypeName);
        d.objects = std::move(objects);
        d.target = target;
        d.kind = kind;
        d.scope = request->scope;
        ownedOptions.release();
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
        if (d.kind == 3) {
            if (!d.state.document->Resolve(d.target) || d.state.document->ChangeRevision() != d.bitmapRevision ||
                !d.box->Valid() || !d.box->Count()) {
                d.Clear();
                return EDITOR_STALE;
            }
            auto *work = d.box->Work(0);
            if (!work)
                return EDITOR_INVALID;
            const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, d.target};
            const EditorRequest read{request->scope, &target, 1};
            EditorResponse projection;
            auto status =
                host->query(host->data, EditorString EDITOR_TEXT("setup.bitmap.format-options-value"), &read,
                            {&projection, [](void *p, const EditorValue *values, uint32_t count) {
                                 return CopyEditorFields(static_cast<EditorResponse *>(p)->fields, values, count);
                             }});
            if (status != EDITOR_OK) {
                d.shown = false;
                return status;
            }
            std::vector<EditorField> fields{{"target", EDITOR_OBJECT, d.target}};
            for (auto &field : projection.fields) {
                if (field.key == "schema")
                    continue;
                if (field.key == "value")
                    field = {"value", EDITOR_OBJECT, d.state.document->Reference(work->GetID())};
                fields.push_back(std::move(field));
            }
            const auto values = BorrowEditorFields(fields);
            const EditorRequest write{
                request->scope, values.data(), uint32_t(values.values.size()), {}, request->operation};
            status =
                host->execute(host->data, EditorString EDITOR_TEXT("setup.bitmap.format-options-set"), &write, sink);
            if (status == EDITOR_OK || status == EDITOR_NO_CHANGE) {
                d.box->Clear();
                d.ClearBitmapOptions();
                d.open = false;
            } else
                d.shown = false;
            return status;
        }
        if (d.kind == 0 || d.kind == 4) {
            auto *parameter = CKParameter::Cast(d.state.document->Resolve(d.target));
            auto *owner = CKBeObject::Cast(d.state.document->Resolve(d.attributeOwner));
            if (!parameter || (d.kind == 4 && (!owner || owner->GetAttributeParameter(d.attributeType) != parameter))) {
                d.Clear();
                return EDITOR_STALE;
            }
            if (!d.box->Valid()) {
                d.shown = false;
                return EDITOR_INVALID;
            }
            const bool changed = d.box->Changed();
            auto *script = OwningScript(parameter);
            size_t steps = 0;
            if (d.kind == 4) {
                EditGroup group(*d.state.document, "Edit Attribute");
                steps = d.box->Apply(script ? script->GetID() : 0);
            } else
                steps = d.box->Apply(script ? script->GetID() : 0);
            if (!steps && changed) {
                d.shown = false;
                return EDITOR_ERROR;
            }
            d.box->Clear();
            d.open = false;
            return EDITOR_OK;
        }
        CKDependencies options;if(!d.box->Dependencies(0,options))return EDITOR_INVALID;
        std::vector<uint64_t> flags;for(int i=0;i<options.Size();++i)flags.push_back(options[i]);
        const EditorValue values[]={ObjectsArgument("objects",d.objects),ObjectArgument("target",d.target),IntegerArgument("custom",1),IntegerArgument("flags",uint64_t(options.m_Flags)),
            {EDITOR_TEXT("options"),EDITOR_INTEGER_LIST,{},{},0,{}, {flags.data(),uint32_t(flags.size())}}};
        std::vector<EditorValue> arguments(values,values+5);if(d.kind==2)arguments.erase(arguments.begin()+1);
        const EditorRequest forwarded{request->scope,arguments.data(),uint32_t(arguments.size()),{},request->operation};
        const auto status=host->execute(host->data,d.kind==1?EditorString EDITOR_TEXT("scene.duplicate"):EditorString EDITOR_TEXT("object.destroy"),&forwarded,sink);
        if((status==EDITOR_OK||status==EDITOR_NO_CHANGE)&&d.kind==2)d.state.Schematic().selection.Clear();
        d.box->Clear();d.open=false;
        return status;
    }
    void Draw(const EditorHost *host,EditorViewFrame &frame){
        if(!open){frame.close=1;return;}
        const char *id = kind == 4   ? "Edit Parameter###AttributeValue"
                         : kind == 3 ? "Format Options###formatoptions"
                         : kind == 0 ? "Edit Parameter###AttributeManagerValue"
                         : kind == 1 ? "Edit Parameter###LevelCopyOptions"
                                     : "Select Delete Options###DeleteOptions";
        if(!shown){ImGui::OpenPopup(id);shown=true;}
        const auto action = EditParameterValueDialog(id,
                                                     kind == 3   ? "Format Options"
                                                     : kind == 2 ? "Select Delete Options"
                                                                 : "Edit Parameter",
                                                     *box, kind == 4 ? attributeTypeName.c_str() : nullptr);
        if (action == DialogAction::Cancel) {
            box->Clear();
            ClearBitmapOptions();
            open = false;
            frame.close = 1;
        } else if (action == DialogAction::Ok) {
            EditorClient client(host, view);
            client.Command("parameter.accept");
        }
        frame.active=open;
    }
};
}
bool RegisterParameterDialogs(EditorApplication &editor,EditorState &state,std::string &error){
    auto *data=new ParameterDialog(state);
    const EditorParameter parameter{EDITOR_TEXT("target"),EDITOR_OBJECT};
    const EditorParameter objects{EDITOR_TEXT("objects"),EDITOR_OBJECT_LIST};
    const EditorParameter pickArgs[] = {{EDITOR_TEXT("point"), EDITOR_REAL_LIST},
                                        {EDITOR_TEXT("class"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc pick{
        sizeof(EditorQueryDesc),
        EDITOR_TEXT("parameter.object-at"),
        EDITOR_TEXT("builtin.parameters"),
        pickArgs,
        2,
        [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
            auto &state = *static_cast<EditorState *>(data);
            const auto &point = request->values[0];
            const auto cid = request->values[1].integer;
            if (point.reals.count != 2 || !std::isfinite(point.reals.data[0]) || !std::isfinite(point.reals.data[1]) ||
                cid > INT32_MAX)
                return EDITOR_INVALID;
            const auto id = ObjectUnderMouse(state, ImVec2(float(point.reals.data[0]), float(point.reals.data[1])),
                                             CK_CLASSID(cid));
            const EditorValue result{EDITOR_TEXT("target"), EDITOR_OBJECT,
                                     state.document ? state.document->Reference(id) : ObjectRef{}};
            return sink.write ? sink.write(sink.data, &result, 1) : EDITOR_INVALID;
        },
        &state,
        EDITOR_QUERY_EACH_READ,
        EDITOR_DOCUMENT,
        &parameter,
        1};
    const EditorParameter copy[]={objects,parameter};
    static int parameterKind = 0, copyKind = 1, deleteKind = 2, bitmapKind = 3;
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("bitmap.edit-options"), EDITOR_TEXT("builtin.parameters"),
         EDITOR_TEXT("Format Options"), &parameter, 1, ParameterDialog::Open, &bitmapKind, EDITOR_DOCUMENT,
         EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("parameter.edit"), EDITOR_TEXT("builtin.parameters"),
         EDITOR_TEXT("Edit Parameter"), &parameter, 1, ParameterDialog::Open, &parameterKind, EDITOR_DOCUMENT,
         EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("scene.copy-options"), EDITOR_TEXT("builtin.parameters"),
         EDITOR_TEXT("Copy Options"), copy, 2, ParameterDialog::Open, &copyKind, EDITOR_DOCUMENT,
         EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("object.confirm-delete"), EDITOR_TEXT("builtin.parameters"),
         EDITOR_TEXT("Select Delete Options"), &objects, 1, ParameterDialog::Open, &deleteKind, EDITOR_DOCUMENT,
         EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("parameter.accept"), EDITOR_TEXT("builtin.parameters"),
         EDITOR_TEXT("Edit Parameter"), nullptr, 0, ParameterDialog::Accept, data, EDITOR_DOCUMENT,
         EDITOR_DOCUMENT_CHANGE}};
    const EditorParameter args[]={{EDITOR_TEXT("target"),EDITOR_OBJECT,1},{EDITOR_TEXT("objects"),EDITOR_OBJECT_LIST,1},{EDITOR_TEXT("kind"),EDITOR_INTEGER_VALUE}};
    EditorViewDesc view{};view.size=sizeof(view);view.imguiVersion=EDITOR_IMGUI_VERSION;view.id=EDITOR_TEXT("parameter.dialog");view.provider=EDITOR_TEXT("builtin.parameters");view.title=EDITOR_TEXT("Edit Parameter");view.kind=EDITOR_OVERLAY_VIEW;view.singleInstance=1;view.area=EDITOR_DOCUMENT;
    view.data=data;view.create=[](void*data,const EditorHost*,EditorView id)->void*{auto*d=static_cast<ParameterDialog*>(data);d->view=id;return d;};view.destroy=[](void*data){static_cast<ParameterDialog*>(data)->Clear();};
    view.draw=[](void*data,const EditorHost*host,EditorView,EditorViewFrame*frame){static_cast<ParameterDialog*>(data)->Draw(host,*frame);};view.parameters=args;view.count=3;view.navigate=ParameterDialog::Navigate;
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("parameters.dialogs");
    module.data = data;
    module.stop = [](void *data) { delete static_cast<ParameterDialog *>(data); };
    module.commands = commands;
    module.commandCount = 5;
    module.views = &view;
    module.viewCount = 1;
    module.queries = &pick;
    module.queryCount = 1;
    if(editor.Register(module,error))return true;delete data;return false;
}
} // namespace vdev
