#include "ui/controls/ParameterBox.h"
#include "ui/controls/ValueEditor.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "features/values/ParameterCommands.h"
#include "workspace/EditorClient.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vdev {
namespace { constexpr float DLU_X = 1.5f, DLU_Y = 1.625f; }
ParameterBox::~ParameterBox() { Clear(); }






ObjectRef ParameterBox::WorkReference(size_t row) const {
    return row < m_Rows.size() ? m_Rows[row].workReference : ObjectRef{};
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
        if (!row.property.id.empty()) {
            auto &app = m_Copies.Editor();
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
        if (const auto picked = ObjectAt(mouse, picking->info.pickClass)) changed = (m_PickShift = ImGui::GetIO().KeyShift, PickResult(*picking, picked)) || changed;
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
    if (!m_Copies.Current(row.workReference)) return false;
    const auto work = row.workReference;
    if (row.typed && !row.References()) { row.invalid = true; return false; }
    auto P = [&](float x, float y) { return ImVec2(at.x + x * DLU_X * s, at.y + y * DLU_Y * s); };
    auto label = [&](float x, float y, float w, float h, const char *text, bool bold = true, vi::Align align = vi::CENTER) {
        vi::Text(P(x, y), P(x + w, y + h), text, bold, align);
    };
    bool changed = false;
    auto textEdit = [&](float x, float w, vi::Align align) {
        auto text = m_Copies.ReadText(work);
        if (vi::Edit("##text", P(x, 0), P(x + w, 12), text, align)) { m_Copies.WriteText(work, text); changed = true; }
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
        const auto action = ParameterDraftEditor(m_Copies, work, id, max.x - min.x,
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
        const auto action = ParameterDraftEditor(m_Copies, work, "##components", size.x, size.y,
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
        const auto action = ParameterDraftEditor(m_Copies, work, "##string", max.x - min.x, row.extended ? 200 * s : max.y - min.y,
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
        ParameterValue currentValue;
        m_Copies.Read(work, currentValue);
        const auto current = currentValue.object;
        const int32_t cid = row.extended ? row.filterClass : row.info.pickClass;
        ImGui::SetCursorScreenPos(P(19, 0));
        const auto action = ParameterDraftEditor(m_Copies, work, "##object", 195 * DLU_X * s, 12 * DLU_Y * s,
            cid, row.extended ? row.derived : true, row.extended ? row.filter : "", row.References(), &row.property, &m_Editors, 0, &row.beforeInteraction);
        m_InputAction |= action;
        if (action & EDITOR_INPUT_CANCEL) row.invalid = false;
        if (action & EDITOR_INPUT_INVALID) row.invalid = true;
        else if (action & EDITOR_INPUT_CHANGED) { row.invalid = false; changed = true; }
        if (row.extended) {
            label(15, 15, 28, 10, "Class", true, vi::LEFT);
            const auto classes = m_Copies.Classes(row.info.pickClass);
            int index = -1;
            for (size_t i = 0; i < classes.size(); ++i)
                if (classes[i].first == row.filterClass) index = static_cast<int>(i);
            const std::string className = index >= 0 ? classes[size_t(index)].second : std::string();
            const int chosen = vi::Combo("##class", P(53, 15), P(165, 27), className.c_str(),
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
            const EditorClient client(m_Copies.Editor().Host(), 0);
            const auto details = client.Query("object.details", {ObjectArgument("target", current)});
            const bool setup = details.status == EDITOR_OK && EditorClient::Integer(details, "can-setup");
            if (vi::Button("##showsetup", P(170, 30), P(215, 40), "Show Setup", setup))
                client.Command("object.open-setup", {ObjectArgument("target", current)});
        }
        break;
    }
    case ParameterUI::Flags: {
        ImGui::SetCursorScreenPos(at);
        const auto size = UiSize(row, s);
        const auto action = ParameterDraftEditor(m_Copies, work, "##flags", size.x, size.y, 0, true, {},
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
                ParameterValue value;
                if (!box.m_Copies.Read(row.workReference, value)) action |= EDITOR_INPUT_INVALID;
                else {
                    if (const auto *remembered = row.References()) value = ParameterDraftValue(std::move(value), *remembered);
                    const std::vector<EditorField> fields{ParameterValueField(value)};
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
        const auto action = ParameterDraftEditor(m_Copies, work, "##members", size.x, size.y, 0, true, {},
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
        const auto action = ParameterDraftEditor(m_Copies, work, "##specialized", size.x, size.y, 0, true, {},
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
    const auto action = row.arrayInput.Draw(m_Copies.Editor().Host(), input);
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


ParameterBox::Row *ParameterBox::RowOf(std::vector<Row> &rows, uint32_t work) {
    for (Row &row : rows) {
        if (row.work == work) return &row;
        if (Row *member = RowOf(row.members, work)) return member;
    }
    return nullptr;
}

ParameterBox::Row *ParameterBox::RowAt(std::vector<Row> &rows, const ImVec2 &mouse, uint32_t except) {
    for (Row &row : rows) {
        if (Row *member = RowAt(row.members, mouse, except)) return member;
        if (row.work != except && mouse.x >= row.drawnMin.x && mouse.y >= row.drawnMin.y && mouse.x < row.drawnMax.x &&
            mouse.y < row.drawnMax.y)
            return &row;
    }
    return nullptr;
}

ObjectRef ParameterBox::ObjectAt(const ImVec2 &mouse, int32_t cid) const { return m_Copies.Pick(mouse.x, mouse.y, cid); }

void ParameterBox::EndPicking() { if (m_PickWork) vi::BlockDialogButtons(false); }

// The pick's end (CK2UI's WindowProc, WM_LBUTTONDOWN): pick mode 1 asks the
// window under the mouse for a parameter (0x606: another parameter UI gives
// its value), pick mode 2 the main window's child for an object of the
// class (0x607).
bool ParameterBox::PickFrom(Row &row, const ImVec2 &mouse) {
    if (row.info.pick & 1) {
        if (Row *other = RowAt(m_Rows, mouse, row.work))
            if ((m_PickShift = ImGui::GetIO().KeyShift, PickResult(row, other->workReference))) return true;
    }
    const auto picked = ObjectAt(mouse, row.info.pickClass);
    m_PickShift = ImGui::GetIO().KeyShift;
    return picked && PickResult(row, picked);
}

// CK2UI's On_PickResult (sub_24E70420) and the UIs' own (sub_24E62BE0 ...):
// a parameter of a compatible type gives its value; else what the type
// takes from the object, Shift taking it relative to the parent (the
// position, orientation and matrix), the local box, the 2D entity's size.








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


} // namespace vdev
