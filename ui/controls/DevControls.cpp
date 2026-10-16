#include "ui/controls/DevControls.h"

#include <algorithm>

#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include "ui/controls/DevTheme.h"
#include "ui/controls/TypeAhead.h"

namespace vdev {
namespace vi {

namespace {

ImFont *FontOf(bool bold) { return bold && BoldFont() ? BoldFont() : ImGui::GetFont(); }

constexpr ImU32 DARK_LINE = IM_COL32(34, 34, 34, 255);

} // namespace

ImU32 Color(int index) {
    static const ImU32 TABLE[] = {IM_COL32(0, 0, 0, 255),       IM_COL32(51, 51, 51, 255),    IM_COL32(65, 65, 65, 255),
                                  IM_COL32(100, 100, 100, 255), IM_COL32(128, 128, 128, 255), IM_COL32(152, 152, 152, 255),
                                  IM_COL32(176, 176, 176, 255), IM_COL32(200, 200, 200, 255), IM_COL32(230, 230, 230, 255),
                                  IM_COL32(255, 255, 255, 255), IM_COL32(255, 0, 0, 255)};
    return index >= 0 && index < static_cast<int>(sizeof(TABLE) / sizeof(TABLE[0])) ? TABLE[index] : TABLE[0];
}

float TextWidth(const char *text, bool bold) {
    ImGui::PushFont(FontOf(bold));
    const float width = ImGui::CalcTextSize(text).x;
    ImGui::PopFont();
    return width;
}

void Text(const ImVec2 &min, const ImVec2 &max, const char *text, bool bold, Align align, ImU32 color) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImGui::PushFont(FontOf(bold));
    const float width = ImGui::CalcTextSize(text).x;
    float x = min.x;
    if (align == CENTER) x = std::max(min.x, (min.x + max.x - width) * 0.5f);
    else if (align == RIGHT) x = std::max(min.x, max.x - width);
    dl->PushClipRect(min, ImVec2(max.x, std::max(max.y, min.y + ImGui::GetFontSize())), true);
    dl->AddText(ImVec2(x, min.y), color, text);
    dl->PopClipRect();
    ImGui::PopFont();
}

bool Edit(const char *id, const ImVec2 &min, const ImVec2 &max, std::string &text, Align align, bool *deactivated) {
    const float width = std::max(1.0f, max.x - min.x), height = std::max(1.0f, max.y - min.y);
    const float textWidth = ImGui::CalcTextSize(text.c_str()).x;
    float padX = 3.0f;
    if (align == CENTER) padX = std::max(2.0f, (width - textWidth) * 0.5f);
    else if (align == RIGHT) padX = std::max(2.0f, width - textWidth - 3.0f);
    const float padY = std::max(0.0f, (height - ImGui::GetFontSize()) * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, Color(0));
    ImGui::SetCursorScreenPos(min);
    ImGui::SetNextItemWidth(width);
    const bool changed = ImGui::InputText(id, &text, ImGuiInputTextFlags_AutoSelectAll);
    if (deactivated) *deactivated = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y - 0.5f), ImVec2(max.x, max.y - 0.5f), Color(5));
    return changed;
}

namespace {
// The frame a Combo's list took a choice in: its Enter is not the dialog's
// OK as well (DialogButton).
int g_ComboChoiceFrame = -1;
} // namespace

int Combo(const char *id, const ImVec2 &min, const ImVec2 &max, const std::string &preview,
          const std::function<std::vector<std::string>()> &items, int current) {
    ImGui::PushID(id);
    const ImVec2 size(std::max(1.0f, max.x - min.x), std::max(1.0f, max.y - min.y));
    ImGui::SetCursorScreenPos(min);
    const bool clicked = ImGui::InvisibleButton("##combo", size);
    if (clicked) ImGui::OpenPopup("##list");
    const bool open = ImGui::IsPopupOpen("##list");
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float s = ImGui::GetFontSize() / 13.0f;
    const float textY = min.y + (size.y - ImGui::GetFontSize()) * 0.5f;
    const float arrowX = max.x - 9.0f * s;
    // The text (highlighted while the list shows, as Dev's focused combo).
    if (open) {
        const float w = std::min(ImGui::CalcTextSize(preview.c_str()).x + 4.0f * s, arrowX - min.x - 4.0f * s);
        dl->AddRectFilled(ImVec2(min.x + 1.0f * s, textY - 1.0f * s), ImVec2(min.x + 1.0f * s + w, textY + ImGui::GetFontSize()),
                          Color(0));
    }
    dl->PushClipRect(min, ImVec2(arrowX - 4.0f * s, max.y), true);
    dl->AddText(ImVec2(min.x + 3.0f * s, textY), open ? Color(9) : Color(0), preview.c_str());
    dl->PopClipRect();
    const float ay = min.y + size.y * 0.5f;
    dl->AddTriangleFilled(ImVec2(arrowX - 4.5f * s, ay - 2.0f * s), ImVec2(arrowX + 4.5f * s, ay - 2.0f * s),
                          ImVec2(arrowX, ay + 2.5f * s), Color(0));
    if (!open) {
        dl->AddLine(ImVec2(min.x, max.y - 1.5f), ImVec2(max.x, max.y - 1.5f), DARK_LINE);
        dl->AddLine(ImVec2(min.x, max.y - 0.5f), ImVec2(max.x, max.y - 0.5f), Color(3));
    }
    int chosen = -1;
    ImGui::SetNextWindowPos(ImVec2(min.x, max.y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(size.x, 0.0f), ImVec2(std::max(size.x, 400.0f * s), 320.0f * s));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Color(8));
    ImGui::PushStyleColor(ImGuiCol_Text, Color(0));
    ImGui::PushStyleColor(ImGuiCol_Header, Color(4));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f * s, 2.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginPopup("##list")) {
        TypeAhead typeAhead;
        const std::vector<std::string> list = items();
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            ImGui::PushID(i);
            if (typeAhead.Selectable(list[i].c_str(), i == current)) {
                chosen = i;
                g_ComboChoiceFrame = ImGui::GetFrameCount();
                ImGui::CloseCurrentPopup();
            }
            if (i == current && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopID();
    return chosen;
}

bool EditCombo(const char *id, const ImVec2 &min, const ImVec2 &max, std::string &text,
               const std::function<std::vector<std::string>()> &items) {
    ImGui::PushID(id);
    const float s = ImGui::GetFontSize() / 13.0f;
    const float arrowLeft = max.x - 13.0f * s;
    bool done = false;
    Edit("##text", min, ImVec2(arrowLeft, max.y), text, LEFT, &done);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(ImVec2(arrowLeft, min.y));
    if (ImGui::InvisibleButton("##arrow", ImVec2(std::max(1.0f, max.x - arrowLeft), std::max(1.0f, max.y - min.y))))
        ImGui::OpenPopup("##list");
    const float ax = max.x - 6.5f * s, ay = (min.y + max.y) * 0.5f;
    dl->AddTriangleFilled(ImVec2(ax - 4.5f * s, ay - 2.0f * s), ImVec2(ax + 4.5f * s, ay - 2.0f * s), ImVec2(ax, ay + 2.5f * s),
                          Color(0));
    dl->AddLine(ImVec2(arrowLeft, max.y - 1.5f), ImVec2(max.x, max.y - 1.5f), DARK_LINE);
    ImGui::SetNextWindowPos(ImVec2(min.x, max.y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(max.x - min.x, 0.0f), ImVec2(std::max(max.x - min.x, 400.0f * s), 320.0f * s));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Color(8));
    ImGui::PushStyleColor(ImGuiCol_Text, Color(0));
    ImGui::PushStyleColor(ImGuiCol_Header, Color(4));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f * s, 2.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginPopup("##list")) {
        TypeAhead typeAhead;
        const std::vector<std::string> list = items();
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            ImGui::PushID(i);
            if (typeAhead.Selectable(list[i].c_str(), list[i] == text)) {
                text = list[i];
                done = true;
                ImGui::CloseCurrentPopup();
            }
            if (list[i] == text && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopID();
    return done;
}

bool Check(const char *id, const ImVec2 &min, float size, bool &on) {
    ImGui::SetCursorScreenPos(min);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
    if (clicked) on = !on;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + size, min.y + size);
    dl->AddRectFilled(min, max, ImGui::IsItemHovered() ? Color(9) : Color(8));
    dl->AddRect(min, max, Color(1));
    if (on) {
        // A cross, as Dev's parameter boxes show a TRUE (Behavior Link Properties).
        const float t = std::max(1.0f, size / 8.0f), m = size * 0.24f;
        dl->AddLine(ImVec2(min.x + m, min.y + m), ImVec2(max.x - m, max.y - m), Color(0), t);
        dl->AddLine(ImVec2(min.x + m, max.y - m), ImVec2(max.x - m, min.y + m), Color(0), t);
    }
    return clicked;
}

bool Radio(const char *id, const ImVec2 &min, float size, bool on, const char *label) {
    ImGui::SetCursorScreenPos(min);
    const float width = size + 4.0f + ImGui::CalcTextSize(label).x;
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, size));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 c(min.x + size * 0.5f, min.y + size * 0.5f);
    dl->AddCircleFilled(c, size * 0.5f, ImGui::IsItemHovered() ? Color(9) : Color(8));
    dl->AddCircle(c, size * 0.5f, Color(1));
    if (on) dl->AddCircleFilled(c, size * 0.22f, Color(0));
    dl->AddText(ImVec2(min.x + size + 4.0f, min.y + (size - ImGui::GetFontSize()) * 0.5f), Color(0), label);
    return clicked;
}

bool ToolbarButton(const char *id, const ImVec2 &min, const ImVec2 &max, const char *label, float s) {
    ImGui::SetCursorScreenPos(min);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(max.x - min.x, max.y - min.y));
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float rounding = (max.y - min.y) * 0.5f;
    if (held) dl->AddRectFilled(min, max, Color(3), rounding);
    dl->AddRect(min, max, Color(hovered ? 9 : 6), rounding, 0, 1.0f);
    const float width = VITextWidth(label, false, s);
    DrawVIText(dl, ImVec2(IM_ROUND((min.x + max.x - width) * 0.5f), IM_ROUND((min.y + max.y - 12.0f * s) * 0.5f)),
               Color(9), label, false, s);
    return clicked;
}

bool Button(const char *id, const ImVec2 &min, const ImVec2 &max, const char *label, bool enabled) {
    ImGui::SetCursorScreenPos(min);
    if (!enabled) ImGui::BeginDisabled();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, max.x - min.x), std::max(1.0f, max.y - min.y)));
    if (!enabled) ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    const float s = ImGui::GetFontSize() / 13.0f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    // RoundRect(..., 9, 9): corners of radius 4.5.
    const ImU32 fill = !enabled ? Color(6) : held ? Color(5) : hovered ? Color(8) : Color(7);
    dl->AddRectFilled(min, max, fill, 4.5f * s);
    dl->AddRect(min, max, enabled ? Color(3) : Color(4), 4.5f * s);
    // The label, smaller when the button is too narrow for it (Dev's dialog
    // font is smaller than the editor's).
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float size = ImGui::GetFontSize();
    const float room = std::max(1.0f, max.x - min.x - 6.0f * s);
    if (textSize.x > room) {
        size *= room / textSize.x;
        textSize = ImVec2(room, textSize.y * room / textSize.x);
    }
    dl->PushClipRect(min, max, true);
    dl->AddText(ImGui::GetFont(), size, ImVec2((min.x + max.x - textSize.x) * 0.5f, (min.y + max.y - textSize.y) * 0.5f),
                enabled ? Color(0) : Color(1), label);
    dl->PopClipRect();
    return clicked && enabled;
}

bool CheckButton(const char *id, const ImVec2 &min, const ImVec2 &max, bool &on, const char *label, bool enabled) {
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 size(std::max(1.0f, max.x - min.x), std::max(1.0f, max.y - min.y));
    ImGui::SetCursorScreenPos(min);
    if (!enabled) ImGui::BeginDisabled();
    const bool clicked = ImGui::InvisibleButton(id, size);
    if (!enabled) ImGui::EndDisabled();
    if (clicked && enabled) on = !on;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 a(IM_ROUND(min.x + 1.0f * s), IM_ROUND(min.y + 1.0f * s));
    const ImVec2 b(IM_ROUND(a.x + 11.0f * s), IM_ROUND(a.y + 11.0f * s));
    dl->AddRectFilled(a, b, Color(0));
    if (on) {
        const float t = std::max(1.0f, s);
        dl->AddLine(ImVec2(a.x + 2.0f * s, a.y + 2.0f * s), ImVec2(b.x - 2.0f * s, b.y - 2.0f * s), Color(9), t);
        dl->AddLine(ImVec2(a.x + 2.0f * s, b.y - 3.0f * s), ImVec2(b.x - 2.0f * s, a.y + 1.0f * s), Color(9), t);
    }
    dl->AddRect(a, b, Color(7), 0.0f, 0, std::max(1.0f, s));
    if (label && *label) {
        const float y = min.y + (size.y - ImGui::GetFontSize()) * 0.5f;
        dl->PushClipRect(ImVec2(b.x + 5.0f * s, min.y), ImVec2(max.x + 200.0f * s, max.y + ImGui::GetFontSize()), true);
        dl->AddText(ImVec2(b.x + 5.0f * s, y), enabled ? Color(0) : Color(6), label);
        dl->PopClipRect();
    }
    return clicked && enabled;
}

void Frame(const ImVec2 &min, const ImVec2 &max) {
    ImGui::GetWindowDrawList()->AddRect(ImVec2(IM_ROUND(min.x), IM_ROUND(min.y)), ImVec2(IM_ROUND(max.x), IM_ROUND(max.y)),
                                        Color(4));
}

namespace {

// A header's label in width pixels: cut, with "...", when wider.
std::string Fitted(const char *text, float width) {
    if (ImGui::CalcTextSize(text).x <= width) return text;
    std::string cut = text;
    while (!cut.empty() && ImGui::CalcTextSize((cut + "...").c_str()).x > width) cut.pop_back();
    return cut + "...";
}

// Where a text of width w starts in the column [x, x + columnWidth).
float Aligned(Align align, float x, float columnWidth, float w, float s) {
    if (align == CENTER) return x + std::max(6.0f * s, (columnWidth - w) * 0.5f);
    if (align == RIGHT) return x + std::max(6.0f * s, columnWidth - 6.0f * s - w);
    return x + 6.0f * s;
}

} // namespace

int ListView(const char *id, const ImVec2 &min, const ImVec2 &max, float s, const std::vector<ListColumn> &columns,
             int rows, const std::function<std::string(int row, int column)> &cell,
             const std::function<bool(int row)> &selected, bool *doubleClicked, int *column, bool viList,
             int *headerClicked, const std::function<ImTextureID(int row)> &icon) {
    if (doubleClicked) *doubleClicked = false;
    if (column) *column = -1;
    if (headerClicked) *headerClicked = -1;
    ImDrawList *outer = ImGui::GetWindowDrawList();
    // The border: the list view's sunken edge, the VIList's dark line.
    outer->AddRectFilled(min, max, Color(7));
    if (viList) {
        outer->AddRect(min, max, Color(1));
        outer->AddLine(ImVec2(min.x, min.y), ImVec2(max.x, min.y), Color(1), 2.0f * s);
    } else {
        outer->AddRect(min, max, Color(1));
        outer->AddLine(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, min.y + 1.0f), Color(4));
        outer->AddLine(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(min.x + 1.0f, max.y - 1.0f), Color(4));
    }
    const float inset = viList ? 0.0f : 2.0f;
    const ImVec2 inMin(min.x + inset, min.y + (viList ? 2.0f * s : inset)), inMax(max.x - inset, max.y - inset);
    const float headerHeight = IM_ROUND(18.0f * s), rowHeight = IM_ROUND(16.0f * s);
    // The widths: the last 0 takes the rest.
    std::vector<float> widths;
    float fixed = 0.0f;
    for (const ListColumn &c : columns) fixed += c.width;
    for (const ListColumn &c : columns) widths.push_back(c.width > 0.0f ? c.width : std::max(0.0f, inMax.x - inMin.x - fixed));
    // The header.
    const float textY = (headerHeight - ImGui::GetFontSize()) * 0.5f;
    if (viList) {
        outer->AddLine(ImVec2(inMin.x, inMin.y + headerHeight), ImVec2(inMax.x, inMin.y + headerHeight), Color(1), 2.0f * s);
        if (!columns.empty()) {
            const float w = ImGui::CalcTextSize(columns[0].label).x;
            outer->AddText(ImVec2(IM_ROUND((inMin.x + inMax.x - w) * 0.5f), IM_ROUND(inMin.y + textY)), Color(0), columns[0].label);
        }
    } else {
        outer->AddRectFilled(inMin, ImVec2(inMax.x, inMin.y + headerHeight), IM_COL32(240, 240, 240, 255));
        outer->AddLine(ImVec2(inMin.x, inMin.y + headerHeight - 1.0f), ImVec2(inMax.x, inMin.y + headerHeight - 1.0f), Color(9));
        float x = inMin.x;
        outer->PushClipRect(inMin, ImVec2(inMax.x, inMin.y + headerHeight), true);
        for (size_t c = 0; c < columns.size(); ++c) {
            const std::string label = Fitted(columns[c].label, std::max(0.0f, widths[c] - 8.0f * s));
            const float w = ImGui::CalcTextSize(label.c_str()).x;
            outer->AddText(ImVec2(IM_ROUND(Aligned(columns[c].align, x, widths[c], w, s)), IM_ROUND(inMin.y + textY)), Color(0),
                           label.c_str());
            if (headerClicked && widths[c] > 0.0f) {
                ImGui::SetCursorScreenPos(ImVec2(x, inMin.y));
                ImGui::PushID(id);
                ImGui::PushID(static_cast<int>(c));
                if (ImGui::InvisibleButton("##header", ImVec2(widths[c], headerHeight))) *headerClicked = static_cast<int>(c);
                if (ImGui::IsItemActive())
                    outer->AddRectFilled(ImVec2(x, inMin.y), ImVec2(x + widths[c], inMin.y + headerHeight), IM_COL32(0, 0, 0, 24));
                ImGui::PopID();
                ImGui::PopID();
            }
            x += widths[c];
            outer->AddLine(ImVec2(x - 1.0f, inMin.y + 2.0f * s), ImVec2(x - 1.0f, inMin.y + headerHeight - 2.0f * s), Color(5));
            outer->AddLine(ImVec2(x, inMin.y + 2.0f * s), ImVec2(x, inMin.y + headerHeight - 2.0f * s), Color(9));
        }
        outer->PopClipRect();
    }
    // The rows, scrolled.
    int clicked = -1;
    ImGui::SetCursorScreenPos(ImVec2(inMin.x, inMin.y + headerHeight + (viList ? 2.0f * s : 0.0f)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    const ImVec2 rowsSize(std::max(1.0f, inMax.x - inMin.x), std::max(1.0f, inMax.y - inMin.y - headerHeight));
    if (ImGui::BeginChild(id, rowsSize, ImGuiChildFlags_None)) {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float width = std::max(rowsSize.x - ImGui::GetStyle().ScrollbarSize * (ImGui::GetScrollMaxY() > 0 ? 1.0f : 0.0f),
                                     1.0f);
        // The rows in view (and one around them).
        const float scrolled = ImGui::GetScrollY();
        const int first = std::max(0, static_cast<int>(scrolled / rowHeight) - 1);
        const int last = std::min(rows, static_cast<int>((scrolled + rowsSize.y) / rowHeight) + 2);
        for (int r = first; r < last; ++r) {
            const float y = at.y + rowHeight * r;
            ImGui::SetCursorScreenPos(ImVec2(at.x, y));
            ImGui::PushID(r);
            if (ImGui::InvisibleButton("##row", ImVec2(width, rowHeight))) clicked = r;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                clicked = r;
                if (doubleClicked) *doubleClicked = true;
            }
            const bool chosen = selected && selected(r);
            if (!ImGui::IsItemVisible()) {
                ImGui::PopID();
                continue;
            }
            if (chosen) dl->AddRectFilled(ImVec2(at.x, y), ImVec2(at.x + width, y + rowHeight), Color(4));
            float x = at.x;
            const float ty = y + (rowHeight - ImGui::GetFontSize()) * 0.5f;
            for (size_t c = 0; c < widths.size(); ++c) {
                const std::string text = cell(r, static_cast<int>(c));
                dl->PushClipRect(ImVec2(x, y), ImVec2(x + widths[c] - 2.0f * s, y + rowHeight), true);
                // The first column's icon (11 x 11), the text after it.
                float iconWidth = 0.0f;
                if (c == 0 && icon)
                    if (ImTextureID image = icon(r)) {
                        const ImVec2 at(IM_ROUND(x + 4.0f * s), IM_ROUND(y + (rowHeight - 11.0f * s) * 0.5f));
                        dl->AddImage(image, at, ImVec2(at.x + 11.0f * s, at.y + 11.0f * s));
                        iconWidth = 13.0f * s;
                    }
                const float tx = columns[c].align == LEFT ? x + 6.0f * s + iconWidth
                                                          : Aligned(columns[c].align, x, widths[c], ImGui::CalcTextSize(text.c_str()).x, s);
                dl->AddText(ImVec2(IM_ROUND(tx), IM_ROUND(ty)), chosen ? Color(9) : Color(0), text.c_str());
                dl->PopClipRect();
                if (clicked == r && column && ImGui::GetIO().MousePos.x >= x && ImGui::GetIO().MousePos.x < x + widths[c])
                    *column = static_cast<int>(c);
                x += widths[c];
            }
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + rowHeight * rows));
        ImGui::Dummy(ImVec2(1.0f, 0.0f));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    return clicked;
}

float TitleHeight(float scale) { return 24.0f * scale; }

namespace {

// The title bar of a VIDialog drawn in the current window; its caption moves
// the window. The client's top-left corner.
ImVec2 DialogChrome(const std::string &title, const ImVec2 &size, float s) {
    const float title_ = TitleHeight(s);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetWindowPos();
    dl->AddRectFilled(ImVec2(at.x + 1.0f, at.y + 1.0f), ImVec2(at.x + size.x - 1.0f, at.y + 1.0f + title_), Color(2),
                      6.0f * s, ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(at.x + 1.0f, at.y + 1.0f + title_), ImVec2(at.x + size.x - 1.0f, at.y + 1.0f + title_),
                IM_COL32(188, 188, 188, 255));
    ImGui::PushFont(FontOf(true));
    dl->PushClipRect(at, ImVec2(at.x + size.x - 6.0f * s, at.y + title_), true);
    dl->AddText(ImVec2(at.x + 9.0f * s, at.y + 1.0f + (title_ - ImGui::GetFontSize()) * 0.5f), Color(9), title.c_str());
    dl->PopClipRect();
    ImGui::PopFont();
    // Moved by its title bar only (a caption).
    ImGui::SetCursorScreenPos(at);
    ImGui::InvisibleButton("##caption", ImVec2(size.x, title_ + 1.0f));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(ImVec2(at.x + delta.x, at.y + delta.y));
    }
    return ImVec2(at.x + 1.0f, at.y + 2.0f + title_);
}

} // namespace

bool BeginWindow(const char *id, const std::string &title, const ImVec2 &client, float s, ImVec2 &origin) {
    const ImVec2 size(client.x + 2.0f, client.y + TitleHeight(s) + 2.0f);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(4));
    ImGui::PushStyleColor(ImGuiCol_Border, Color(0));
    const bool open = ImGui::Begin(id, nullptr,
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                       ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    if (!open) return false;
    origin = DialogChrome(title, size, s);
    return true;
}

void EndWindow() { ImGui::End(); }

bool BeginDialog(const char *id, const std::string &title, const ImVec2 &client, float s, ImVec2 &origin) {
    const float title_ = TitleHeight(s);
    const ImVec2 size(client.x + 2.0f, client.y + title_ + 2.0f);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Color(4));
    ImGui::PushStyleColor(ImGuiCol_Border, Color(0));
    // Dev's modal dialogs leave the windows behind as they are.
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 0));
    const bool open = ImGui::BeginPopupModal(id, nullptr,
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                 ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(4);
    if (!open) return false;
    origin = DialogChrome(title, size, s);
    return true;
}

void EndDialog() { ImGui::EndPopup(); }

void Panel(const ImVec2 &min, const ImVec2 &max, float s) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(min, max, Color(7), 7.0f * s);
    dl->AddRect(min, max, DARK_LINE, 7.0f * s, 0, 1.5f * s);
}

ImVec2 TemplateClient(const ImVec2 &units, float s, float pad, bool buttons) {
    return ImVec2(2.0f * 12.0f * s + 2.0f * pad * s + units.x * 1.5f * s,
                  3.0f * s + 4.0f * s + units.y * 1.5f * s + 5.0f * s + (buttons ? 4.0f * s + 18.0f * s + 5.0f * s : 10.0f * s));
}

TemplateLayout LayoutTemplate(const ImVec2 &clientOrigin, const ImVec2 &units, float s, float pad) {
    TemplateLayout layout;
    layout.s = s;
    layout.panelMin = ImVec2(clientOrigin.x + 12.0f * s, clientOrigin.y + 3.0f * s);
    layout.origin = ImVec2(layout.panelMin.x + pad * s, layout.panelMin.y + 4.0f * s);
    layout.panelMax = ImVec2(layout.origin.x + units.x * 1.5f * s + pad * s, layout.origin.y + units.y * 1.5f * s + 5.0f * s);
    layout.buttonsY = layout.panelMax.y + 4.0f * s;
    layout.right = layout.panelMax.x;
    return layout;
}

void TemplatePanel(const TemplateLayout &layout) { Panel(layout.panelMin, layout.panelMax, layout.s); }

namespace {
bool g_DialogButtonsBlocked = false;
}

void BlockDialogButtons(bool blocked) { g_DialogButtonsBlocked = blocked; }

bool DialogButton(const char *label, const ImVec2 &min, float s, ImGuiKey key) {
    const ImVec2 size(60.0f * s, 18.0f * s);
    const ImVec2 max(min.x + size.x, min.y + size.y);
    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##dialogbutton", size) && !g_DialogButtonsBlocked;
    ImGui::PopID();
    if (g_DialogButtonsBlocked || g_ComboChoiceFrame == ImGui::GetFrameCount()) key = ImGuiKey_None;
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (held) dl->AddRectFilled(min, max, Color(3), size.y * 0.5f);
    else if (hovered) dl->AddRectFilled(min, max, Color(5), size.y * 0.5f);
    dl->AddRect(min, max, IM_COL32(188, 188, 188, 255), size.y * 0.5f, 0, 1.0f * s);
    ImGui::PushFont(FontOf(true));
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2((min.x + max.x - textSize.x) * 0.5f, (min.y + max.y - textSize.y) * 0.5f), Color(9), label);
    ImGui::PopFont();
    if (key != ImGuiKey_None && ImGui::IsKeyPressed(key, false) && !ImGui::IsAnyItemActive()) clicked = true;
    return clicked;
}

} // namespace vi
} // namespace vdev
