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

std::string Format(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%g", value);
    return text;
}

bool ParseFloat(const std::string &text, float &value) {
    const char *begin = text.c_str();
    char *end = nullptr;
    value = std::strtof(begin, &end);
    if (end == begin) return false;
    while (*end == ' ') ++end;
    return *end == 0;
}

bool ParseInt(const std::string &text, int &value) {
    const char *begin = text.c_str();
    char *end = nullptr;
    value = static_cast<int>(std::strtol(begin, &end, 10));
    if (end == begin) return false;
    while (*end == ' ') ++end;
    return *end == 0;
}

std::string TextOf(CKParameter *parameter) { return AnsiToUtf8(ParameterText(parameter)); }

void SetText(CKParameter *parameter, const std::string &utf8) {
    if (parameter) parameter->SetStringValue(const_cast<CKSTRING>(Utf8ToAnsi(utf8).c_str()));
}

// A structure's members (its value: the CK_IDs of the members).
std::vector<CK_ID> MembersOf(CKContext *context, CKParameter *parameter) {
    std::vector<CK_ID> members;
    CKParameterManager *manager = context ? context->GetParameterManager() : nullptr;
    CKStructStruct *structure = manager && parameter ? manager->GetStructDescByType(parameter->GetType()) : nullptr;
    CK_ID *ids = structure ? static_cast<CK_ID *>(parameter->GetReadDataPtr(FALSE)) : nullptr;
    if (!ids) return members;
    for (int i = 0; i < structure->NbData; ++i) members.push_back(ids[i]);
    return members;
}

// Dev's dialog units (MS Sans Serif 8).
constexpr float DLU_X = 1.5f, DLU_Y = 1.625f;

// A 2D curve value (the parameter holds a CK2dCurve pointer).
CK2dCurve *CurveOf(CKParameter *parameter) {
    CK2dCurve *curve = nullptr;
    if (parameter && parameter->GetDataSize() == static_cast<int>(sizeof(curve))) parameter->GetValue(&curve, FALSE);
    return curve;
}

// The values held by pointer (the types' own memory, copied by their copy
// functions): a dependencies value, an object array.
template <typename T> T *HeldOf(CKParameter *parameter) {
    T *held = nullptr;
    if (parameter && parameter->GetDataSize() == static_cast<int>(sizeof(held))) parameter->GetValue(&held, FALSE);
    return held;
}

bool SameDependencies(CKDependencies *a, CKDependencies *b) {
    if (!a || !b) return a == b;
    if (a->m_Flags != b->m_Flags) return false;
    const int size = std::max(a->Size(), b->Size());
    for (int i = 0; i < size; ++i)
        if ((i < a->Size() ? (*a)[i] : 0) != (i < b->Size() ? (*b)[i] : 0)) return false;
    return true;
}

bool SameObjects(XObjectArray *a, XObjectArray *b) {
    if (!a || !b) return a == b;
    if (a->Size() != b->Size()) return false;
    for (int i = 0; i < a->Size(); ++i)
        if ((*a)[i] != (*b)[i]) return false;
    return true;
}

// The LOD Manager's LOD Options (MeshModifiers LODManager.h: LODAlpha 1,
// LODMultiMesh 2, LODAnimation 4, LODPatchMesh 8; the screen and faces
// proportions, the alpha's screen proportions).
struct LodOptions {
    int flags = 0;
    float screenMag = 0.4f, screenMin = 0.01f, facesMag = 1.0f, facesMin = 0.02f, alphaMag = 0.1f, alphaMin = 0.05f;
};

// The Rect UI's representation, shared by every Rect UI (CK2UI
// dword_24E88C68): 0 the corners, 1 the corner and the size, 2 the center
// and the half size.
int g_RectMode = 0;

bool SameCurve(CK2dCurve *a, CK2dCurve *b) {
    if (!a || !b) return a == b;
    if (a->GetControlPointCount() != b->GetControlPointCount()) return false;
    for (int i = 0; i < a->GetControlPointCount(); ++i) {
        CK2dCurvePoint *p = a->GetControlPoint(i), *q = b->GetControlPoint(i);
        if (!p || !q) return p == q;
        if (p->GetPosition().x != q->GetPosition().x || p->GetPosition().y != q->GetPosition().y ||
            (p->IsLinear() != FALSE) != (q->IsLinear() != FALSE) || p->GetTension() != q->GetTension() ||
            p->GetContinuity() != q->GetContinuity() || p->GetBias() != q->GetBias())
            return false;
    }
    return true;
}

} // namespace

// What a row's UI draws with: its origin and scale (CK2UI's dialog units),
// the window's draw list, the working copy, and whether it changed.
struct ParameterBox::UiPen {
    ImVec2 at;
    float s = 1.0f;
    ImDrawList *dl = nullptr;
    CKParameter *work = nullptr;
    bool changed = false;

    ImVec2 P(float x, float y) const { return ImVec2(at.x + x * DLU_X * s, at.y + y * DLU_Y * s); }
    void Label(float x, float y, float w, float h, const char *text, bool bold = true, vi::Align align = vi::CENTER) const {
        vi::Text(P(x, y), P(x + w, y + h), text, bold, align);
    }
    // An edit of a number of a value (its text from value); set: the
    // number typed, while it parses.
    void Number(const char *id, float x, float y, float w, const std::string &value, const std::function<void(float)> &set) {
        std::string text = value;
        bool done = false;
        if (vi::Edit(id, P(x, y), P(x + w, y + 12), text, vi::CENTER, &done)) {
            float parsed = 0.0f;
            if (ParseFloat(text, parsed)) {
                set(parsed);
                changed = true;
            }
        }
    }
    // The value's text in a wide edit (the types CK2UI has no small UI for).
    void TextEdit(float x, float w, vi::Align align) {
        std::string text = TextOf(work);
        bool done = false;
        if (vi::Edit("##text", P(x, 0), P(x + w, 12), text, align, &done)) {
            SetText(work, text);
            changed = true;
        }
    }
};

ParameterBox::~ParameterBox() { Clear(); }

void ParameterBox::Clear() {
    CKContext *context = m_State.host.Context();
    // The working copies of an earlier generation went with ClearAll; their ids may be another object's now.
    const bool alive = m_Generation == m_State.host.Generation();
    for (const Row &row : m_Rows)
        if (alive && row.owned && context && context->GetObject(row.work)) context->DestroyObject(row.work);
    m_Rows.clear();
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
    return row < m_Rows.size() && context ? CKParameter::Cast(context->GetObject(m_Rows[row].work)) : nullptr;
}

void ParameterBox::InitRow(Row &row, CKParameter *work) {
    CKContext *context = m_State.host.Context();
    CKParameterManager *manager = context->GetParameterManager();
    row.work = work->GetID();
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
    case ParameterUI::Dependencies:
        if (CKDependencies *held = HeldOf<CKDependencies>(work)) row.dependencies = *held;
        break;
    case ParameterUI::ObjectArray:
        if (XObjectArray *held = HeldOf<XObjectArray>(work)) row.objects = *held;
        break;
    case ParameterUI::Synchro: {
        // The object's name, a synchro object's number of users (sub_24E73240).
        CKObject *object = context->GetObject(ValueOf<CK_ID>(work));
        row.text = AnsiToUtf8(object && object->GetName() ? object->GetName() : "");
        if (CKSynchroObject *synchro = CKSynchroObject::Cast(object))
            row.users = std::to_string(synchro->GetRendezVousNumberOfWaiters());
        break;
    }
    case ParameterUI::Struct: {
        CKStructStruct *structure = manager->GetStructDescByType(work->GetType());
        const std::vector<CK_ID> members = MembersOf(context, work);
        row.members.clear();
        for (size_t i = 0; i < members.size(); ++i) {
            CKParameter *member = CKParameter::Cast(context->GetObject(members[i]));
            if (!member) continue;
            Row child;
            child.label = AnsiToUtf8(structure && structure->Desc && structure->Desc[i] ? structure->Desc[i] : "");
            InitRow(child, member);
            row.members.push_back(std::move(child));
        }
        break;
    }
    default: break;
    }
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
    row.owned = true;
    row.label = AnsiToUtf8(label);
    InitRow(row, work);
    AddRow(std::move(row));
    return true;
}

void ParameterBox::AddRow(Row row) {
    // Rows left from an earlier generation name objects that are gone.
    if (m_Generation != m_State.host.Generation()) {
        Clear();
        m_Generation = m_State.host.Generation();
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
        CKParameterManager *manager = m_State.host.Context()->GetParameterManager();
        CKParameter *work = CKParameter::Cast(m_State.host.Context()->GetObject(row.work));
        CKFlagsStruct *flags = work ? manager->GetFlagsDescByType(work->GetType()) : nullptr;
        int last = 0;
        for (int i = 0; flags && i < flags->GetNumFlags(); ++i)
            for (int bit = 0; bit < 32; ++bit)
                if (flags->GetFlagValue(i) == (1 << bit)) last = std::max(last, bit);
        return ImVec2(169 * DLU_X * s + 5.0f * s, (12.0f + 10.0f * (last / 2)) * DLU_Y * s + 5.0f * s);
    }
    // Dialog 156 less 260 pixels unless Custom Dependencies (sub_24E6B180).
    case ParameterUI::Dependencies: {
        CKParameter *work = CKParameter::Cast(m_State.host.Context()->GetObject(row.work));
        CKDependencies *dependencies = HeldOf<CKDependencies>(work);
        const bool custom = dependencies && dependencies->m_Flags == CK_DEPENDENCIES_CUSTOM;
        return ImVec2(296 * DLU_X * s, custom ? 196 * DLU_Y * s : 196 * DLU_Y * s - 260.0f * s);
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
                case ParameterUI::Rect: g_RectMode = (g_RectMode + 1) % 3; break;
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
    UiPen pen{at, s, ImGui::GetWindowDrawList(), work};
    ImDrawList *dl = pen.dl;
    auto P = [&](float x, float y) { return pen.P(x, y); };
    auto label = [&](float x, float y, float w, float h, const char *text, bool bold = true, vi::Align align = vi::CENTER) {
        pen.Label(x, y, w, h, text, bold, align);
    };
    bool &changed = pen.changed;
    auto number = [&](const char *id, float x, float y, float w, const std::string &value,
                      const std::function<void(float)> &set) { pen.Number(id, x, y, w, value, set); };
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
    // A VIAngleCtrl (InterfaceControls.dll: 300 x 22 pixels; "Turn" and its
    // edit, "Degree" and its edit ("%.3g"), the dial from 220 to 240).
    auto angle = [&](const char *id, ImVec2 o, float value, const std::function<void(float)> &set) {
        ImGui::PushID(id);
        auto A = [&](float x, float y) { return ImVec2(o.x + x * s, o.y + y * s); };
        // InterfaceControls' SetAngle: the whole turns, the rest in [0, 2 PI).
        const float wholeTurns = static_cast<float>(static_cast<int>(value / (2.0f * PI)));
        const float rest = value - wholeTurns * 2.0f * PI;
        vi::Text(A(0, 4), A(43, 18), "Turn", true, vi::CENTER);
        std::string turnText = std::to_string(static_cast<int>(wholeTurns));
        if (vi::Edit("##turn", A(43, 4), A(104, 16), turnText, vi::CENTER)) {
            int parsed = 0;
            if (ParseInt(turnText, parsed)) {
                set(parsed * 2.0f * PI + rest);
                changed = true;
            }
        }
        vi::Text(A(112, 4), A(160, 18), "Degree", true, vi::CENTER);
        char degrees[32];
        std::snprintf(degrees, sizeof(degrees), "%.3g", rest * 180.0f / PI);
        std::string degreeText = degrees;
        if (vi::Edit("##degree", A(160, 4), A(210, 16), degreeText, vi::CENTER)) {
            float parsed = 0.0f;
            if (ParseFloat(degreeText, parsed)) {
                set(wholeTurns * 2.0f * PI + parsed * PI / 180.0f);
                changed = true;
            }
        }
        // The dial: dragging sets the angle (the turns kept).
        const ImVec2 c = A(230, 10);
        ImGui::SetCursorScreenPos(A(220, 0));
        ImGui::InvisibleButton("##dial", ImVec2(20.0f * s, 20.0f * s));
        if (ImGui::IsItemActive()) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            float a = std::atan2(c.y - m.y, m.x - c.x);
            if (a < 0.0f) a += 2.0f * PI;
            set(wholeTurns * 2.0f * PI + a);
            changed = true;
        }
        dl->AddCircleFilled(c, 10.0f * s, vi::Color(8));
        dl->AddCircle(c, 10.0f * s, vi::Color(0));
        dl->AddLine(c, ImVec2(c.x + 10.0f * s * std::cos(rest), c.y - 10.0f * s * std::sin(rest)), vi::Color(0));
        ImGui::PopID();
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
    auto textEdit = [&](float x, float w, vi::Align align) { pen.TextEdit(x, w, align); };

    switch (row.info.ui) {
    case ParameterUI::Number: {
        // CK2UI sub_24E61A20: a Float is the number typed, an Integer too;
        // anything else goes through the type's own text.
        const CKGUID guid = work->GetGUID();
        std::string text = TextOf(work);
        bool done = false;
        if (vi::Edit("##number", P(15, 0), P(70, 12), text, vi::CENTER, &done)) {
            float real = 0.0f;
            int whole = 0;
            if (guid == CKPGUID_FLOAT && ParseFloat(text, real)) SetValueOf(work, real);
            else if (guid == CKPGUID_INT && ParseInt(text, whole)) SetValueOf(work, whole);
            else if (guid != CKPGUID_FLOAT && guid != CKPGUID_INT && ParseFloat(text, real)) SetText(work, text);
            changed = true;
        }
        if (done) SetText(work, text);
        break;
    }
    case ParameterUI::Angle: {
        angle("##angle", ImVec2(at.x + 2.0f * s, at.y + 2.0f * s), ValueOf<float>(work), [&](float v) { SetValueOf(work, v); });
        break;
    }
    case ParameterUI::EulerAngles: {
        VxVector v = ValueOf<VxVector>(work);
        for (int i = 0; i < 3; ++i) {
            ImGui::PushID(i);
            angle("##euler", ImVec2(at.x + 2.0f * s, at.y + 24.0f * s * i), v[i], [&](float a) {
                v[i] = a;
                SetValueOf(work, v);
            });
            ImGui::PopID();
        }
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
    case ParameterUI::Bool: {
        bool on = ValueOf<CKBOOL>(work) != FALSE;
        if (vi::Check("##bool", P(5, 2), 8.0f * DLU_X * s, on)) {
            SetValueOf<CKBOOL>(work, on ? TRUE : FALSE);
            changed = true;
        }
        break;
    }
    case ParameterUI::String: {
        std::string text = TextOf(work);
        if (row.extended) {
            const ImVec2 a = P(15, 0), b(P(220, 0).x, at.y + 200.0f * s);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, vi::Color(8));
            ImGui::PushStyleColor(ImGuiCol_Text, vi::Color(0));
            if (ImGui::InputTextMultiline("##string", &text, ImVec2(b.x - a.x, b.y - a.y))) {
                SetText(work, text);
                changed = true;
            }
            ImGui::PopStyleColor(2);
        } else {
            textEdit(15, 205, vi::LEFT);
        }
        break;
    }
    case ParameterUI::Vector:
    case ParameterUI::Quaternion:
    case ParameterUI::Vector2D: {
        const int count = row.info.ui == ParameterUI::Vector ? 3 : row.info.ui == ParameterUI::Quaternion ? 4 : 2;
        float v[4] = {};
        work->GetValue(v, FALSE);
        static const char *const AXES[4] = {"X:", "Y:", "Z:", "W:"};
        static const char *const IDS[4] = {"##x", "##y", "##z", "##w"};
        for (int i = 0; i < count; ++i) {
            label(5.0f + 70.0f * i, 0, 10, 10, AXES[i]);
            number(IDS[i], 15.0f + 70.0f * i, 0, 55, Format(v[i]), [&](float value) {
                v[i] = value;
                work->SetValue(v, static_cast<int>(sizeof(float)) * count);
            });
        }
        break;
    }
    case ParameterUI::Matrix: {
        if (row.collapsed) {
            vi::Text(P(15, 0), P(319, 18), TextOf(work).c_str());
            break;
        }
        VxMatrix m = ValueOf<VxMatrix>(work);
        static const char *const AXES[4] = {"X:", "Y:", "Z:", "W:"};
        static const char *const ROWS[4] = {"Right", "Up", "Dir", "Position"};
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) {
                ImGui::PushID(r * 4 + c);
                label(5.0f + 70.0f * c, 15.0f * r, 10, 10, AXES[c]);
                number("##m", 15.0f + 70.0f * c, 15.0f * r, 55, Format(m[r][c]), [&](float value) {
                    m[r][c] = value;
                    SetValueOf(work, m);
                });
                ImGui::PopID();
            }
        for (int r = 0; r < 4; ++r) label(285, 15.0f * r, 45, 12, ROWS[r], false, vi::LEFT);
        break;
    }
    case ParameterUI::Color: {
        // The color's button (2, 0, 38 x 12) and R, G, B, A in 0..255
        // (CK2UI sub_24E66410).
        VxColor color = ValueOf<VxColor>(work);
        const ImVec2 a = P(2, 0), b = P(40, 12);
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton("##swatch", ImVec2(b.x - a.x, b.y - a.y))) ImGui::OpenPopup("##colorpicker");
        dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, 1.0f)), 3.0f * s);
        dl->AddRect(a, b, vi::Color(3), 3.0f * s);
        if (ImGui::BeginPopup("##colorpicker")) {
            float rgba[4] = {color.r, color.g, color.b, color.a};
            if (ImGui::ColorPicker4("##picker", rgba, ImGuiColorEditFlags_AlphaBar)) {
                SetValueOf(work, VxColor(rgba[0], rgba[1], rgba[2], rgba[3]));
                changed = true;
            }
            ImGui::EndPopup();
        }
        float channels[4] = {color.r, color.g, color.b, color.a};
        static const char *const NAMES[4] = {"R:", "G:", "B:", "A:"};
        static const char *const IDS[4] = {"##r", "##g", "##b", "##a"};
        for (int i = 0; i < 4; ++i) {
            label(41.0f + 37.0f * i, 0, 10, 10, NAMES[i]);
            integer(IDS[i], 51.0f + 37.0f * i, 0, 20, static_cast<int>(channels[i] * 255.0f), [&](int value) {
                channels[i] = std::clamp(value, 0, 255) / 255.0f;
                SetValueOf(work, VxColor(channels[0], channels[1], channels[2], channels[3]));
            });
        }
        break;
    }
    case ParameterUI::Box: {
        VxBbox box = ValueOf<VxBbox>(work);
        static const char *const AXES[3] = {"X:", "Y:", "Z:"};
        for (int r = 0; r < 2; ++r) {
            VxVector &v = r ? box.Max : box.Min;
            for (int c = 0; c < 3; ++c) {
                ImGui::PushID(r * 3 + c);
                label(5.0f + 70.0f * c, 15.0f * r, 10, 10, AXES[c]);
                number("##b", 15.0f + 70.0f * c, 15.0f * r, 55, Format(v[c]), [&](float value) {
                    v[c] = value;
                    SetValueOf(work, box);
                });
                ImGui::PopID();
            }
            label(215, 15.0f * r, 53, 12, r ? "Max" : "Min", false, vi::LEFT);
        }
        break;
    }
    case ParameterUI::Rect: {
        // In the representation chosen with the expand button (sub_24E70FF0
        // and its reverse): Left,Top and Right,Bottom; Left,Top and Size;
        // Center and Half Size.
        const VxRect rect = ValueOf<VxRect>(work);
        float shown[4] = {rect.left, rect.top, rect.right, rect.bottom};
        if (g_RectMode == 1) {
            shown[2] = rect.right - rect.left;
            shown[3] = rect.bottom - rect.top;
        } else if (g_RectMode == 2) {
            shown[2] = (rect.right - rect.left) * 0.5f;
            shown[3] = (rect.bottom - rect.top) * 0.5f;
            shown[0] = rect.left + shown[2];
            shown[1] = rect.top + shown[3];
        }
        static const char *const FIRST[3] = {"Left,Top", "Left,Top", "Center"};
        static const char *const SECOND[3] = {"Right,Bottom", "Size", "Half Size"};
        for (int r = 0; r < 2; ++r) {
            for (int c = 0; c < 2; ++c) {
                ImGui::PushID(r * 2 + c);
                label(5.0f + 70.0f * c, 15.0f * r, 10, 10, c ? "Y:" : "X:");
                const int i = r * 2 + c;
                number("##r", 15.0f + 70.0f * c, 15.0f * r, 55, Format(shown[i]), [&](float v) {
                    shown[i] = v;
                    VxRect value(shown[0], shown[1], shown[2], shown[3]);
                    if (g_RectMode == 1) value = VxRect(shown[0], shown[1], shown[0] + shown[2], shown[1] + shown[3]);
                    else if (g_RectMode == 2)
                        value = VxRect(shown[0] - shown[2], shown[1] - shown[3], shown[0] + shown[2], shown[1] + shown[3]);
                    SetValueOf(work, value);
                });
                ImGui::PopID();
            }
            label(145, 15.0f * r, 68, 12, r ? SECOND[g_RectMode] : FIRST[g_RectMode], true, vi::LEFT);
        }
        break;
    }
    case ParameterUI::Object: {
        // CK2UI's Object UI (dialog 2011, sub_24E6CE00): the objects of the
        // class; with its filter: Class, Derived, Text Filter, Show Setup.
        const CK_ID current = ValueOf<CK_ID>(work);
        const CK_CLASSID cid = row.extended ? row.filterClass : row.info.pickClass;
        objectCombo("##object", 19, 0, 195, current,
                    [&] { return ObjectsOfClass(*m_State.document, cid, row.extended ? row.derived : true, row.extended ? Utf8ToAnsi(row.filter) : ""); },
                    [&](CK_ID id) { SetValueOf(work, id); });
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
                m_State.events.Post(ShowObjectSetup{current});
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
    case ParameterUI::Enum: {
        CKEnumStruct *enumeration = manager->GetEnumDescByType(work->GetType());
        const int current = ValueOf<int>(work);
        int index = -1;
        for (int i = 0; enumeration && i < enumeration->GetNumEnums(); ++i)
            if (enumeration->GetEnumValue(i) == current) index = i;
        const char *name = index >= 0 ? enumeration->GetEnumDescription(index) : nullptr;
        const int chosen = vi::Combo("##enum", P(15, 0), P(210, 12), AnsiToUtf8(name ? name : ""),
                                     [&] {
                                         std::vector<std::string> names;
                                         for (int i = 0; enumeration && i < enumeration->GetNumEnums(); ++i) {
                                             const char *n = enumeration->GetEnumDescription(i);
                                             names.push_back(AnsiToUtf8(n ? n : ""));
                                         }
                                         return names;
                                     },
                                     index);
        if (chosen >= 0) {
            SetValueOf(work, enumeration->GetEnumValue(chosen));
            changed = true;
        }
        break;
    }
    case ParameterUI::Flags: {
        // Dialog 2007 (sub_24E6C200): the check box of bit b at its place,
        // two columns (5 and 91) 10 apart, for the flags of one bit; small,
        // the value's text.
        if (row.collapsed) {
            vi::Text(P(5, 3), P(56, 11), TextOf(work).c_str());
            break;
        }
        CKFlagsStruct *flags = manager->GetFlagsDescByType(work->GetType());
        int value = ValueOf<int>(work);
        for (int i = 0; flags && i < flags->GetNumFlags(); ++i) {
            const int bit = flags->GetFlagValue(i);
            int place = -1;
            for (int b = 0; b < 32; ++b)
                if (bit == (1 << b)) place = b;
            if (place < 0) continue;
            bool on = (value & bit) != 0;
            const float x = (place % 2) ? 91.0f : 5.0f, y = place == 0 ? 3.0f : 2.0f + 10.0f * (place / 2);
            ImGui::PushID(i);
            if (vi::Check("##flag", P(x, y), 8.0f * DLU_X * s, on)) {
                value = on ? (value | bit) : (value & ~bit);
                SetValueOf(work, value);
                changed = true;
            }
            const char *name = flags->GetFlagDescription(i);
            vi::Text(P(x + 11.0f, y), P(x + 82.0f, y + 10.0f), AnsiToUtf8(name ? name : "").c_str(), false, vi::LEFT);
            ImGui::PopID();
        }
        break;
    }
    case ParameterUI::Curve2D: Curve2DUi(row, pen); break;
    case ParameterUI::Struct: changed = DrawRows(row.members, at, s, false) || changed; break;
    case ParameterUI::Dependencies:
        if (HeldOf<CKDependencies>(work)) changed = DependenciesUi(row, work, at, s) || changed;
        else textEdit(15, 205, vi::LEFT);
        break;
    case ParameterUI::ObjectArray:
        if (HeldOf<XObjectArray>(work)) changed = ObjectArrayUi(row, work, at, s) || changed;
        else textEdit(15, 205, vi::LEFT);
        break;
    case ParameterUI::LodOptions:
        if (work->GetDataSize() == static_cast<int>(sizeof(LodOptions))) changed = LodOptionsUi(row, work, at, s) || changed;
        else textEdit(15, 205, vi::LEFT);
        break;
    default: textEdit(15, 205, vi::LEFT); break;
    }
    return changed;
}

// CK2UI's 2D curve (dialog 102): the point chosen's X and Y, Hide
// Tangents and Linear, the curve from (0, 0) to (1, 1) below (3, 16,
// 205 x 148): a point drags, a double click adds one, Del or a right
// click removes the one chosen (two are kept).
void ParameterBox::Curve2DUi(Row &row, UiPen &pen) {
    const ImVec2 &at = pen.at;
    const float s = pen.s;
    ImDrawList *dl = pen.dl;
    auto P = [&](float x, float y) { return pen.P(x, y); };
    auto label = [&](float x, float y, float w, float h, const char *text) { pen.Label(x, y, w, h, text); };
    auto number = [&](const char *id, float x, float y, float w, const std::string &value,
                      const std::function<void(float)> &set) { pen.Number(id, x, y, w, value, set); };
    auto textEdit = [&](float x, float w, vi::Align align) { pen.TextEdit(x, w, align); };
    bool &changed = pen.changed;
    CKParameter *work = pen.work;
    CK2dCurve *curve = CurveOf(work);
    if (!curve) {
        textEdit(15, 205, vi::LEFT);
        return;
    }
    if (row.collapsed) {
        // Small (sub_24E67810): the curve alone, 200 x 40 pixels at (3, 3).
        const ImVec2 a(at.x + 3.0f * s, at.y + 3.0f * s), b(a.x + 200.0f * s, a.y + 40.0f * s);
        dl->AddRectFilled(a, b, vi::Color(8));
        dl->AddRect(a, b, vi::Color(3));
        ImVec2 previous;
        for (int i = 0; i <= 100; ++i) {
            const float x = i / 100.0f;
            const ImVec2 point(a.x + 1.0f * s + x * (b.x - a.x - 2.0f * s),
                               b.y - 1.0f * s - std::clamp(curve->GetY(x), 0.0f, 1.0f) * (b.y - a.y - 2.0f * s));
            if (i) dl->AddLine(previous, point, vi::Color(0), 1.0f * s);
            previous = point;
        }
        return;
    }
    const int count = curve->GetControlPointCount();
    row.curvePoint = std::clamp(row.curvePoint, 0, std::max(0, count - 1));
    CK2dCurvePoint *chosen = count ? curve->GetControlPoint(row.curvePoint) : nullptr;
    if (chosen) {
        Vx2DVector position = chosen->GetPosition();
        label(5, 1, 8, 8, "X:");
        number("##cx", 15, 1, 55, Format(position.x), [&](float v) {
            position.x = std::clamp(v, 0.0f, 1.0f);
            chosen->SetPosition(position);
            curve->Update();
        });
        label(75, 1, 8, 8, "Y:");
        number("##cy", 85, 1, 55, Format(position.y), [&](float v) {
            position.y = v;
            chosen->SetPosition(position);
            curve->Update();
        });
    }
    // Its owner-drawn buttons (CK2UI bitmaps 2010 tangents, 2011 / 2012
    // linear, 2013 the grid).
    auto bitmapButton = [&](const char *id, float x, int bitmap, bool on, const char *tip) {
        const ImVec2 a = P(x, 2), b = P(x + 11, 12);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(b.x - a.x, b.y - a.y));
        if (on || ImGui::IsItemActive()) dl->AddRectFilled(a, b, vi::Color(4));
        if (ImTextureID texture = CK2UIBitmap(bitmap)) dl->AddImage(texture, a, ImVec2(a.x + 16.0f * s, a.y + 16.0f * s));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        return clicked;
    };
    if (bitmapButton("##tangents", 144, 2010, row.hideTangents, "Hide Tangents")) row.hideTangents = !row.hideTangents;
    const bool linear = chosen && chosen->IsLinear();
    if (bitmapButton("##linear", 159, linear ? 2012 : 2011, linear, "Linear") && chosen) {
        chosen->SetLinear(!linear);
        curve->Update();
        changed = true;
    }
    if (bitmapButton("##grid", 174, 2013, !row.hideGrid, "Grid")) row.hideGrid = !row.hideGrid;
    const ImVec2 a = P(3, 16), b = P(208, 164);
    dl->AddRectFilled(a, b, vi::Color(8));
    dl->AddRect(a, b, vi::Color(3));
    const float margin = 6.0f * s;
    const ImVec2 g0(a.x + margin, a.y + margin), g1(b.x - margin, b.y - margin);
    auto toScreen = [&](const Vx2DVector &v) {
        return ImVec2(g0.x + v.x * (g1.x - g0.x), g1.y - v.y * (g1.y - g0.y));
    };
    auto fromScreen = [&](const ImVec2 &m) {
        return Vx2DVector((m.x - g0.x) / (g1.x - g0.x), (g1.y - m.y) / (g1.y - g0.y));
    };
    for (int i = 1; i < 4 && !row.hideGrid; ++i) {
        const float x = g0.x + (g1.x - g0.x) * i / 4.0f, y = g0.y + (g1.y - g0.y) * i / 4.0f;
        dl->AddLine(ImVec2(x, g0.y), ImVec2(x, g1.y), vi::Color(7));
        dl->AddLine(ImVec2(g0.x, y), ImVec2(g1.x, y), vi::Color(7));
    }
    dl->AddRect(g0, g1, vi::Color(5));
    ImVec2 previous;
    for (int i = 0; i <= 100; ++i) {
        const float x = i / 100.0f;
        const ImVec2 point = toScreen(Vx2DVector(x, curve->GetY(x)));
        if (i) dl->AddLine(previous, point, vi::Color(0), 1.5f * s);
        previous = point;
    }
    ImGui::SetCursorScreenPos(a);
    ImGui::InvisibleButton("##curve", ImVec2(b.x - a.x, b.y - a.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (ImGui::IsItemActivated()) {
        // The point under the mouse is chosen.
        for (int i = 0; i < count; ++i) {
            const ImVec2 point = toScreen(curve->GetControlPoint(i)->GetPosition());
            if (std::fabs(point.x - mouse.x) <= 5.0f * s && std::fabs(point.y - mouse.y) <= 5.0f * s) row.curvePoint = i;
        }
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f) && chosen) {
        Vx2DVector v = fromScreen(mouse);
        // Between its neighbors, the ends at 0 and 1.
        const float left = row.curvePoint > 0 ? curve->GetControlPoint(row.curvePoint - 1)->GetPosition().x : 0.0f;
        const float right = row.curvePoint + 1 < count ? curve->GetControlPoint(row.curvePoint + 1)->GetPosition().x : 1.0f;
        v.x = row.curvePoint == 0 ? 0.0f : row.curvePoint == count - 1 ? 1.0f : std::clamp(v.x, left, right);
        v.y = std::clamp(v.y, 0.0f, 1.0f);
        chosen->SetPosition(v);
        curve->Update();
        changed = true;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        Vx2DVector v = fromScreen(mouse);
        v.x = std::clamp(v.x, 0.0f, 1.0f);
        v.y = std::clamp(v.y, 0.0f, 1.0f);
        curve->AddControlPoint(v);
        curve->Update();
        for (int i = 0; i < curve->GetControlPointCount(); ++i)
            if (curve->GetControlPoint(i)->GetPosition().x == v.x) row.curvePoint = i;
        changed = true;
    }
    const bool remove = (ImGui::IsItemClicked(ImGuiMouseButton_Right) ||
                         (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete)));
    if (remove && chosen && count > 2) {
        curve->DeleteControlPoint(chosen);
        row.curvePoint = std::max(0, row.curvePoint - 1);
        changed = true;
    }
    for (int i = 0; i < curve->GetControlPointCount(); ++i) {
        CK2dCurvePoint *point = curve->GetControlPoint(i);
        const ImVec2 c = toScreen(point->GetPosition());
        const float h = 3.0f * s;
        if (!row.hideTangents && !point->IsLinear()) {
            const Vx2DVector in = point->GetInTangent(), out = point->GetOutTangent();
            const float k = 0.15f;
            dl->AddLine(c, toScreen(point->GetPosition() - in * k), vi::Color(5));
            dl->AddLine(c, toScreen(point->GetPosition() + out * k), vi::Color(5));
        }
        dl->AddRectFilled(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h),
                          i == row.curvePoint ? vi::Color(10) : vi::Color(1));
    }
}

// CK2UI's Dependencies UI (dialog 156, sub_24E6ADA0 / sub_24E6B180 /
// sub_24E6B070): No / Full / Custom Dependencies; Custom shows the classes
// with options (a tree) and the chosen class's options, two columns of
// check boxes from (132, 33), 10 apart.
bool ParameterBox::DependenciesUi(Row &row, CKParameter *work, const ImVec2 &at, float s) {
    CKDependencies *dependencies = &row.dependencies;
    const CK_DEPENDENCIES_OPMODE mode = DependenciesModeOf(m_State.host.Context()->GetParameterManager(), work->GetType());
    auto P = [&](float x, float y) { return ImVec2(at.x + x * DLU_X * s, at.y + y * DLU_Y * s); };
    bool changed = false;
    const float radio = 8.0f * DLU_X * s;
    static const char *const LABELS[3] = {"No Dependencies", "Full Dependencies", "Custom Dependencies"};
    static const CK_DEPENDENCIES_FLAGS FLAGS[3] = {CK_DEPENDENCIES_NONE, CK_DEPENDENCIES_FULL, CK_DEPENDENCIES_CUSTOM};
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        if (vi::Radio("##radio", P(7, 3.0f + 10.0f * i), radio, dependencies->m_Flags == FLAGS[i], LABELS[i]) &&
            dependencies->m_Flags != FLAGS[i]) {
            dependencies->m_Flags = FLAGS[i];
            changed = true;
        }
        ImGui::PopID();
    }
    if (changed) SetHeldValue(work, dependencies);
    if (dependencies->m_Flags != CK_DEPENDENCIES_CUSTOM) return changed;
    const int classes = CKGetClassCount();
    if (dependencies->Size() < classes) {
        const int old = dependencies->Size();
        dependencies->Resize(classes);
        for (int i = old; i < classes; ++i) (*dependencies)[i] = 0;
    }
    // The tree (SysTreeView32 at (5, 35), 120 x 155): the root chosen first.
    const std::vector<DependencyClass> tree = DependencyClasses(mode);
    if (row.dependencyClass < 0 && !tree.empty()) row.dependencyClass = static_cast<int>(tree.front().cid);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 treeMin = P(5, 35), treeMax = P(125, 190);
    dl->AddRectFilled(treeMin, treeMax, vi::Color(9));
    dl->AddRect(treeMin, treeMax, vi::Color(0));
    ImGui::SetCursorScreenPos(ImVec2(treeMin.x + 1.0f, treeMin.y + 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f * s, 2.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##tree", ImVec2(treeMax.x - treeMin.x - 2.0f, treeMax.y - treeMin.y - 2.0f), ImGuiChildFlags_None);
    ImDrawList *tl = ImGui::GetWindowDrawList();
    const float line = 16.0f * s, indent = 19.0f * s;
    int hiddenBelow = -1; // the depth of a folded item: its descendants are not shown
    for (size_t i = 0; i < tree.size(); ++i) {
        const DependencyClass &entry = tree[i];
        if (hiddenBelow >= 0 && entry.depth > hiddenBelow) continue;
        hiddenBelow = -1;
        const bool parent = i + 1 < tree.size() && tree[i + 1].depth > entry.depth;
        const bool folded = row.folded.count(entry.cid) != 0;
        if (parent && folded) hiddenBelow = entry.depth;
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float x = cursor.x + indent * entry.depth;
        ImGui::PushID(static_cast<int>(entry.cid));
        // The button (TVS_HASBUTTONS): a 9 x 9 box with - or +.
        if (parent) {
            const ImVec2 b0(x + 2.0f * s, cursor.y + (line - 9.0f * s) * 0.5f), b1(b0.x + 9.0f * s, b0.y + 9.0f * s);
            ImGui::SetCursorScreenPos(b0);
            if (ImGui::InvisibleButton("##fold", ImVec2(9.0f * s, 9.0f * s))) {
                if (folded) row.folded.erase(entry.cid);
                else row.folded.insert(entry.cid);
            }
            tl->AddRectFilled(b0, b1, vi::Color(9));
            tl->AddRect(b0, b1, vi::Color(5));
            const float cy = (b0.y + b1.y) * 0.5f, cx = (b0.x + b1.x) * 0.5f;
            tl->AddLine(ImVec2(b0.x + 2.0f * s, cy), ImVec2(b1.x - 2.0f * s, cy), vi::Color(0));
            if (folded) tl->AddLine(ImVec2(cx, b0.y + 2.0f * s), ImVec2(cx, b1.y - 2.0f * s), vi::Color(0));
        }
        const ImVec2 text(x + 15.0f * s, cursor.y + (line - ImGui::GetFontSize()) * 0.5f);
        const bool selected = static_cast<int>(entry.cid) == row.dependencyClass;
        const float textWidth = ImGui::CalcTextSize(entry.name.c_str()).x;
        ImGui::SetCursorScreenPos(ImVec2(text.x - 2.0f * s, cursor.y));
        if (ImGui::InvisibleButton("##class", ImVec2(textWidth + 4.0f * s, line))) row.dependencyClass = static_cast<int>(entry.cid);
        if (selected) tl->AddRectFilled(ImVec2(text.x - 2.0f * s, cursor.y + 1.0f), ImVec2(text.x + textWidth + 2.0f * s, cursor.y + line - 1.0f),
                                        IM_COL32(10, 36, 106, 255));
        tl->AddText(text, selected ? vi::Color(9) : vi::Color(0), entry.name.c_str());
        ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + line));
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    // The chosen class's options (sub_24E6B350).
    if (row.dependencyClass >= 0 && row.dependencyClass < classes) {
        const std::vector<std::string> options = DependencyOptions(row.dependencyClass, mode);
        CKDWORD &bits = (*dependencies)[row.dependencyClass];
        for (size_t i = 0; i < options.size(); ++i) {
            const float x = (i % 2) ? 218.0f : 132.0f;
            const float y = i == 0 ? 33.0f : 32.0f + 10.0f * (i / 2);
            bool on = (bits & (1u << i)) != 0;
            ImGui::PushID(static_cast<int>(i));
            if (vi::Check("##option", P(x, y + 1.0f), radio, on)) {
                bits = on ? (bits | (1u << i)) : (bits & ~(1u << i));
                SetHeldValue(work, dependencies);
                changed = true;
            }
            vi::Text(P(x + 11.0f, y), P(x + 78.0f, y + 10.0f), AnsiToUtf8(options[i]).c_str(), false, vi::LEFT);
            ImGui::PopID();
        }
    }
    return changed;
}

// CK2UI's Object Array UI (dialog 2010, sub_24E6FF30 ... sub_24E702E0): a
// list (15, 5, 195 x 95) of Name, ID, Class and Dyn; a double click shows
// the Setup, Del removes, Ctrl+C / Ctrl+V copy and paste the objects, + and
// - (the keypad's) move the item down and up; the right click's menu: Add
// Object, Copy Object(s), Paste Object(s) (at the item clicked, else at the
// end).
bool ParameterBox::ObjectArrayUi(Row &row, CKParameter *work, const ImVec2 &at, float s) {
    CKContext *context = m_State.host.Context();
    XObjectArray *array = &row.objects;
    // The objects deleted since leave (XObjectArray::Check).
    for (int i = array->Size() - 1; i >= 0; --i)
        if (!context->GetObject((*array)[i])) {
            array->RemoveAt(i);
            row.chosen.clear();
        }
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
    const int count = array->Size();
    for (int i = 0; i < count; ++i) {
        CKObject *object = context->GetObject((*array)[i]);
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
            m_State.events.Post(ShowObjectSetup{object->GetID()});
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) menuAt = i;
        ImGui::PopID();
        const bool selected = row.chosen.count(i) != 0;
        if (selected) il->AddRectFilled(r0, r1, IM_COL32(10, 36, 106, 255));
        const ImU32 ink = selected ? vi::Color(9) : vi::Color(0);
        char id[32];
        std::snprintf(id, sizeof(id), "%d", static_cast<int>((*array)[i]));
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
            if (i >= 0 && i < array->Size()) ids.push_back((*array)[i]);
        return ids;
    };
    auto insert = [&](int position, const std::vector<CK_ID> &ids) {
        for (CK_ID id : ids)
            if (context->GetObject(id)) array->Insert(std::clamp(position++, 0, array->Size()), id);
        row.chosen.clear();
        changed = true;
    };
    if (focused && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !row.chosen.empty()) {
            for (auto it = row.chosen.rbegin(); it != row.chosen.rend(); ++it)
                if (*it >= 0 && *it < array->Size()) array->RemoveAt(*it);
            row.chosen.clear();
            changed = true;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && !row.chosen.empty()) m_State.objectClipboard = selection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) insert(array->Size(), m_State.objectClipboard);
        // + and -: the first selected item and the one below / above it change places.
        const bool down = ImGui::IsKeyPressed(ImGuiKey_KeypadAdd), up = ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract);
        if ((down || up) && !row.chosen.empty()) {
            const int from = *row.chosen.begin(), to = from + (down ? 1 : -1);
            if (to >= 0 && to < array->Size()) {
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
        if (ImGui::MenuItem("Copy Object(s)", nullptr, false, !row.chosen.empty())) m_State.objectClipboard = selection();
        if (ImGui::MenuItem("Paste Object(s)", nullptr, false, !m_State.objectClipboard.empty()))
            insert(std::clamp(row.anchor, 0, array->Size()), m_State.objectClipboard);
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
            if (chosen) insert(std::clamp(row.anchor, 0, array->Size()), {chosen});
        }
        if (action != DialogAction::None) m_Chooser->Clear();
    }
    if (changed) SetHeldValue(work, array);
    return changed;
}

// The LOD Manager's LOD Options UI (dialog 2012, sub_24E6DE90 /
// sub_24E6E0F0 / sub_24E6E3F0): the Multiple Meshes, Character Animation,
// Patch Mesh and Alpha flags; the screen's and faces' (an animation's)
// proportions and the alpha's, in percent; Alpha shows its edits, Character
// Animation hides Multiple Meshes and Patch Mesh and names the second row
// Anims.
bool ParameterBox::LodOptionsUi(Row &row, CKParameter *work, const ImVec2 &at, float s) {
    (void)row;
    LodOptions options = ValueOf<LodOptions>(work);
    auto P = [&](float x, float y) { return ImVec2(at.x + x * DLU_X * s, at.y + y * DLU_Y * s); };
    bool changed = false;
    const float box = 8.0f * DLU_X * s;
    auto flag = [&](const char *id, float x, float y, int bit, const char *text, float textX) {
        bool on = (options.flags & bit) != 0;
        if (vi::Check(id, P(x, y), box, on)) {
            options.flags = on ? (options.flags | bit) : (options.flags & ~bit);
            changed = true;
        }
        vi::Text(P(textX, y), P(99.0f, y + 10.0f), text, true); // to the group box's edge
    };
    const bool animation = (options.flags & 4) != 0, alpha = (options.flags & 1) != 0;
    ImGui::GetWindowDrawList()->AddRect(P(5, 3), P(100, 39), vi::Color(5), 3.0f * s); // the group box 2084
    if (!animation) flag("##meshes", 13, 8, 2, "Multiple Meshes", 28);
    flag("##animation", 13, 18, 4, "Character Animation", 28);
    if (!animation) flag("##patch", 13, 28, 8, "Patch Mesh", 27);
    flag("##alpha", 13, 42, 1, "Alpha", 28);
    vi::Text(P(140, 1), P(170, 11), "Mag", true, vi::CENTER);
    vi::Text(P(185, 1), P(215, 11), "Min", true, vi::CENTER);
    vi::Text(P(105, 10), P(135, 20), "Screen:", true, vi::RIGHT);
    vi::Text(P(105, 26), P(135, 36), animation ? "Anims:" : "Faces:", true, vi::RIGHT);
    if (alpha) vi::Text(P(80, 41), P(135, 51), "Screen Alpha:", true, vi::RIGHT);
    // "%g%%" of the proportion; what is typed is in percent.
    auto percent = [&](const char *id, float x, float y, float &value) {
        char text[32];
        std::snprintf(text, sizeof(text), "%g%%", value * 100.0f);
        std::string typed = text;
        if (vi::Edit(id, P(x, y), P(x + 30.0f, y + 12.0f), typed, vi::CENTER)) {
            float parsed = 0.0f;
            if (std::sscanf(typed.c_str(), "%g", &parsed) == 1) {
                value = parsed * 0.01f;
                changed = true;
            }
        }
    };
    percent("##screenmag", 140, 10, options.screenMag);
    percent("##screenmin", 185, 10, options.screenMin);
    percent("##facesmag", 140, 25, options.facesMag);
    percent("##facesmin", 185, 25, options.facesMin);
    if (alpha) {
        percent("##alphamag", 140, 41, options.alphaMag);
        percent("##alphamin", 185, 41, options.alphaMin);
    }
    if (changed) SetValueOf(work, options);
    return changed;
}

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
    if (!cid || state.objectPick.empty()) return 0;
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
        SetValueOf(work, chosen);
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
        const std::vector<CK_ID> members = MembersOf(context, target);
        for (size_t i = 0; i < row.members.size() && i < members.size(); ++i) {
            Row member = row.members[i];
            member.target = members[i];
            if (RowChanged(member)) return true;
        }
        return false;
    }
    if (row.info.ui == ParameterUI::Curve2D && CurveOf(work)) return !SameCurve(CurveOf(work), CurveOf(target));
    if (row.info.ui == ParameterUI::Dependencies && HeldOf<CKDependencies>(work))
        return !SameDependencies(HeldOf<CKDependencies>(work), HeldOf<CKDependencies>(target));
    if (row.info.ui == ParameterUI::ObjectArray && HeldOf<XObjectArray>(work))
        return !SameObjects(HeldOf<XObjectArray>(work), HeldOf<XObjectArray>(target));
    if (row.info.ui == ParameterUI::Synchro && (!row.created.empty() || row.usersSet)) return true;
    if (ManagesMemory(context, work->GetType())) return ParameterText(work) != ParameterText(target);
    return ParameterBytes(work) != ParameterBytes(target);
}

bool ParameterBox::Changed() const {
    for (const Row &row : m_Rows)
        if (RowChanged(row)) return true;
    return false;
}

size_t ParameterBox::ApplyRow(const Row &row, CK_ID targetId, CK_ID script) {
    CKContext *context = m_State.host.Context();
    CKParameter *work = CKParameter::Cast(context->GetObject(row.work));
    CKParameter *target = CKParameter::Cast(context->GetObject(targetId));
    if (!work || !target || work->GetType() != target->GetType()) return 0;
    if (row.info.ui == ParameterUI::Struct) {
        size_t steps = 0;
        const std::vector<CK_ID> members = MembersOf(context, target);
        for (size_t i = 0; i < row.members.size() && i < members.size(); ++i) steps += ApplyRow(row.members[i], members[i], script);
        return steps;
    }
    size_t steps = 0;
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
                m_State.Log(LogLevel::Warning, "Edit Parameters: " + AnsiToUtf8(made.message));
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
        }
    }
    EditResult result = EditResult::Refused(std::string());
    const bool held = (row.info.ui == ParameterUI::Curve2D && CurveOf(work)) ||
                      (row.info.ui == ParameterUI::Dependencies && HeldOf<CKDependencies>(work)) ||
                      (row.info.ui == ParameterUI::ObjectArray && HeldOf<XObjectArray>(work));
    if (held) {
        // A value held by pointer: the parameter's state before and after
        // the copy (the type's copy function).
        Row compared = row;
        compared.created.clear();
        compared.usersSet = false;
        compared.target = targetId;
        if (!RowChanged(compared)) return steps;
        result = EditObjectState(*m_State.document, targetId, "Set Value",
                                 [work](CKObject *object) { static_cast<CKParameter *>(object)->CopyValue(work, FALSE); });
    } else if (ManagesMemory(context, work->GetType())) {
        const std::string text = ParameterText(work);
        if (text == ParameterText(target)) return steps;
        result = SetParameterText(*m_State.document, script, targetId, text);
    } else {
        const std::vector<unsigned char> bytes = ParameterBytes(work);
        if (bytes == ParameterBytes(target)) return steps;
        result = SetParameterBytes(*m_State.document, script, targetId, bytes);
    }
    if (!result.ok && !result.Unchanged())
        m_State.Log(LogLevel::Warning, "Edit Parameters: " + AnsiToUtf8(result.message));
    return steps + (result.ok ? 1 : 0);
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
    if (vi::DialogButton("Cancel", ImVec2(x, y), s, ImGuiKey_Escape)) action = DialogAction::Cancel;
    if (apply) {
        x -= 70.0f * s;
        if (vi::DialogButton("Apply", ImVec2(x, y), s)) action = DialogAction::Apply;
    }
    x -= 70.0f * s;
    if (vi::DialogButton("OK", ImVec2(x, y), s, ImGuiKey_Enter)) action = DialogAction::Ok;
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
    if (vi::DialogButton("Cancel", ImVec2(x, y), s, ImGuiKey_Escape)) action = DialogAction::Cancel;
    x -= 70.0f * s;
    if (vi::DialogButton("OK", ImVec2(x, y), s, ImGuiKey_Enter)) action = DialogAction::Ok;
    if (action != DialogAction::None) ImGui::CloseCurrentPopup();
    vi::EndDialog();
    return action;
}

size_t ParameterBox::Apply(CK_ID script) {
    if (!m_State.HasDocument() || Stale()) return 0;
    size_t steps = 0;
    for (Row &row : m_Rows) {
        steps += ApplyRow(row, row.target, script);
        // Made once (Apply then OK).
        row.created.clear();
        row.usersSet = false;
    }
    return steps;
}

} // namespace vdev
