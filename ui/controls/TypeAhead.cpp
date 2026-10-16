#include "ui/controls/TypeAhead.h"

#include <cctype>
#include <cstring>

#include "imgui_internal.h"

namespace vdev {

namespace {

// The item typed to, highlighted on the next frame (one list has the focus
// at a time).
struct Pending {
    ImGuiID list = 0;
    int index = -1;
    int frame = -1;
};
Pending g_Pending;
// The list that had the typing last, and when: a list given it again (or
// another list) starts with nothing typed.
struct Typing {
    ImGuiID list = 0;
    int frame = -1;
};
Typing g_Typing;
// The list that had a highlight at a frame (it is shown selected then).
Typing g_Highlight;
// ImGui's pause after which the letters typed start again (GetTypingSelectRequest).
constexpr float TYPING_PAUSE = 1.80f;

// The label shown (before "##") starts with typed, ASCII letters in any case.
bool StartsWith(const std::string &label, const std::string &typed) {
    const size_t hidden = label.find("##");
    const size_t length = hidden == std::string::npos ? label.size() : hidden;
    if (length < typed.size()) return false;
    for (size_t i = 0; i < typed.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(label[i])) != std::toupper(static_cast<unsigned char>(typed[i])))
            return false;
    return true;
}

} // namespace

TypeAhead::TypeAhead(int flags) : m_Flags(flags), m_Key(ImGui::GetID("##typeahead")) {
    const int frame = ImGui::GetFrameCount();
    if (g_Pending.frame < frame - 1) g_Pending = Pending();
    m_ShowHighlight = g_Highlight.list == m_Key && g_Highlight.frame == frame - 1;
    if (!(m_Flags & Shared)) m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
}

TypeAhead::~TypeAhead() {
    const int frame = ImGui::GetFrameCount();
    if (m_Highlighted) g_Highlight = Typing{m_Key, frame};
    if (!m_Focused) return;
    if (g_Typing.list != m_Key || g_Typing.frame != frame - 1) ImGui::GetCurrentContext()->TypingSelectState.Clear();
    g_Typing = Typing{m_Key, frame};
    ImGuiTypingSelectRequest *request = ImGui::GetTypingSelectRequest(ImGuiTypingSelectFlags_AllowSingleCharMode);
    // While letters are being typed a space is one of them (as in a
    // browser): the Space key does not choose the highlighted item then.
    ImGuiContext &g = *ImGui::GetCurrentContext();
    const ImGuiTypingSelectState &typing = g.TypingSelectState;
    if (typing.SearchBuffer[0] != 0 && typing.LastRequestTime + TYPING_PAUSE > static_cast<float>(g.Time))
        ImGui::SetKeyOwner(ImGuiKey_Space, m_Key);
    if (!request || !request->SelectRequest) return;
    const bool single = request->SingleCharMode;
    const std::string typed(request->SearchBuffer, single ? request->SingleCharSize : request->SearchBufferLen);
    const int current = m_Current >= 0 ? m_Current : m_Selected;
    int first = -1, after = -1;
    for (int i = 0; i < static_cast<int>(m_Labels.size()) && after < 0; ++i) {
        if (!StartsWith(m_Labels[i], typed)) continue;
        if (first < 0) first = i;
        if (current >= 0 && (single ? i > current : i >= current)) after = i;
    }
    const int target = after >= 0 ? after : first;
    if (target >= 0 && (target != current || !m_Highlighted)) g_Pending = Pending{m_Key, target, frame};
}

bool TypeAhead::Selectable(const char *label, bool selected, ImGuiSelectableFlags flags, const ImVec2 &size) {
    const int index = static_cast<int>(m_Labels.size());
    const ImGuiID id = ImGui::GetID(label); // Selectable's own
    const bool typedTo = g_Pending.list == m_Key && g_Pending.index == index;
    const bool focused = id == ImGui::GetFocusID();
    const bool highlighted = typedTo || (focused && ImGui::GetCurrentContext()->NavCursorVisible);
    const bool shown = (m_ShowHighlight || typedTo) ? highlighted : selected;
    const bool clicked = ImGui::Selectable(label, shown, flags, size);
    if (focused) m_Current = index;
    if ((m_Flags & Shared) && focused && ImGui::IsWindowFocused()) m_Focused = true;
    if (selected && m_Selected < 0) m_Selected = index;
    if (highlighted) m_Highlighted = true;
    m_Labels.emplace_back(label);
    if (typedTo) {
        g_Pending = Pending();
        ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
        ImGui::SetNavCursorVisibleAfterMove();
        ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleEdgeY);
    }
    return clicked;
}

bool TypeAheadCombo(const char *label, int *current, const char *const items[], int count) {
    const char *preview = *current >= 0 && *current < count ? items[*current] : "";
    if (!ImGui::BeginCombo(label, preview)) return false;
    bool changed = false;
    {
        TypeAhead typeAhead;
        for (int i = 0; i < count; ++i) {
            ImGui::PushID(i);
            const bool selected = i == *current;
            if (typeAhead.Selectable(items[i] ? items[i] : "", selected) && !selected) {
                *current = i;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
    }
    ImGui::EndCombo();
    if (changed) ImGui::MarkItemEdited(ImGui::GetCurrentContext()->LastItemData.ID);
    return changed;
}

} // namespace vdev
