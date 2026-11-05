#include "ui/setup/SetupLayout.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/DevBitmaps.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cfloat>

namespace vdev::setup {
float PageUnit() { return ImGui::GetFontSize() / 13.0f * CurrentDevTheme().setupCoordinateScale; }
bool EditSlider(const char *id, float *value, float low, float high, const char *format, float speed) {
    bool changed = ImGui::DragFloat(id, value, speed, 0, 0, format);
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(-FLT_MIN);
    float shown = std::max(low, std::min(high, *value));
    if (ImGui::SliderFloat("##slider", &shown, low, high, "")) {
        *value = shown;
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

bool ColorBar(const char *id, float *color, bool alpha, ImVec2 size) {
    if (size.x == 0 && size.y == 0) size = {48 * PageUnit(), ImGui::GetFrameHeight()};
    ImGui::PushID(id);
    const ImGuiColorEditFlags flags = alpha ? ImGuiColorEditFlags_AlphaPreviewHalf : ImGuiColorEditFlags_NoAlpha;
    if (ImGui::ColorButton(id, {color[0], color[1], color[2], alpha ? color[3] : 1}, flags, size))
        ImGui::OpenPopup("picker");
    bool changed = false;
    if (ImGui::BeginPopup("picker")) {
        changed = alpha ? ImGui::ColorPicker4("##picker", color, flags) : ImGui::ColorPicker3("##picker", color, flags);
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

SetupColumns::SetupColumns(const char *id, int maximum, float width) {
    // Table bookkeeping has its own ID; arranging controls does not rename them.
    const auto contentId = ImGui::GetID("");
    const auto &theme = CurrentDevTheme();
    const float scale = PageUnit();
    const ImVec2 padding{theme.setupSpacing.x * scale, theme.setupSpacing.y * scale};
    const float available = std::max(1.0f, width > 0 ? width : ImGui::GetContentRegionAvail().x);
    const float minimum = theme.setupColumnWidth * scale + 2 * padding.x;
    const int columns = int(std::clamp(std::floor(available / minimum), 1.0f, float(std::max(1, maximum))));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, padding);
    m_Open = ImGui::BeginTable(
        id, columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX,
        {std::max(available, minimum), 0});
    if (m_Open)
        ImGui::PushOverrideID(contentId);
    else
        ImGui::PopStyleVar();
}
SetupColumns::~SetupColumns() {
    if (m_Open) {
        ImGui::PopID();
        ImGui::EndTable();
        ImGui::PopStyleVar();
    }
}
void SetupColumns::Next() {
    if (m_Open) ImGui::TableNextColumn();
}

SetupForm::SetupForm(const char *id, float width) {
    // Preserve the same control and popup IDs in either table or plain layout.
    const auto contentId = ImGui::GetID("");
    const float available = std::max(1.0f, width > 0 ? width : ImGui::GetContentRegionAvail().x);
    m_Open = ImGui::BeginTable(
        id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX,
        {width > 0 ? width : 0, 0});
    if (m_Open) {
        const float label = std::min(CurrentDevTheme().setupLabelWidth * PageUnit(), available * .45f);
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, std::max(1.0f, label));
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::PushOverrideID(contentId);
    }
}
SetupForm::~SetupForm() {
    if (m_Open) {
        ImGui::PopID();
        ImGui::EndTable();
    }
}
void SetupForm::Row(const char *label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextWrapped("%s", label);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

DevPage::DevPage() : origin(ImGui::GetCursorScreenPos()), s(PageUnit()) {}
void DevPage::Include(ImVec2 point) const {
    m_Extent.x = std::max(m_Extent.x, point.x - origin.x);
    m_Extent.y = std::max(m_Extent.y, point.y - origin.y);
}
ImVec2 DevPage::At(float x, float y) const {
    const ImVec2 point{IM_ROUND(origin.x + x * s), IM_ROUND(origin.y + y * s)};
    Include(point);
    return point;
}

float DevPage::Place(float x1, float y1, float x2, float y2) const {
    const ImVec2 a = At(x1, y1), b = At(x2, y2);
    ImGui::SetCursorScreenPos(
        ImVec2(a.x, a.y + std::max(0.0f, IM_ROUND((b.y - a.y - ImGui::GetFrameHeight()) * 0.5f))));
    const float width = std::max(1.0f, b.x - a.x);
    Include({b.x, ImGui::GetCursorScreenPos().y + ImGui::GetFrameHeight()});
    ImGui::SetNextItemWidth(width);
    return width;
}

void DevPage::Label(float x1, float y1, float x2, float y2, const char *text) const {
    const ImVec2 a = At(x1, y1), b = At(x2, y2);
    const float frame = ImGui::GetFrameHeight();
    const float top = a.y + std::max(0.0f, IM_ROUND((b.y - a.y - frame) * 0.5f));
    const float y = top + std::max(0.0f, IM_ROUND((std::min(frame, b.y - a.y) - ImGui::GetFontSize()) * 0.5f));
    const auto size = ImGui::CalcTextSize(text);
    Include({a.x + size.x, y + size.y});
    ImGui::GetWindowDrawList()->AddText(ImVec2(a.x, y), CurrentDevTheme().controls[0], text);
}

void DevPage::Line(float x1, float y1, float x2, float y2) const {
    const ImVec2 a = At(x1, y1);
    ImVec2 b = At(x2, y2);
    b.x = std::max(b.x, a.x + 1.0f);
    b.y = std::max(b.y, a.y + 1.0f);
    ImGui::GetWindowDrawList()->AddRectFilled(a, b, CurrentDevTheme().controls[4]);
}

bool DevPage::Check(const char *id, float x, float y, bool &on) const {
    // On: the box with the cross (609, hovered 611), as VICheckButton draws
    // it; off: the plain box (610, 612).
    static const int CHECK[4] = {610, 612, 609, 611};
    ImVec2 size;
    DevBitmap(CHECK[0], &size);
    if (size.x <= 0.0f) size = ImVec2(11.0f, 11.0f);
    const ImVec2 at = At(x, y), max(at.x + size.x * s, at.y + size.y * s);
    Include(max);
    ImGui::SetCursorScreenPos(at);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(max.x - at.x, max.y - at.y));
    if (clicked) on = !on;
    const bool hovered = ImGui::IsItemHovered();
    if (ImTextureID texture = DevBitmap(CHECK[(on ? 2 : 0) + (hovered ? 1 : 0)]))
        ImGui::GetWindowDrawList()->AddImage(texture, at, max);
    return clicked;
}

bool DevPage::EditSlider(const char *id, const ImVec4 &edit, const ImVec4 &slider, float *value, float low, float high,
                         const char *format, float speed) const {
    Place(edit.x, edit.y, edit.z, edit.w);
    bool changed = ImGui::DragFloat(id, value, speed, 0.0f, 0.0f, format);
    ImGui::PushID(id);
    Place(slider.x, slider.y, slider.z, slider.w);
    float shown = std::max(low, std::min(high, *value));
    if (ImGui::SliderFloat("##slider", &shown, low, high, "")) {
        *value = shown;
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

bool DevPage::ColorBar(const char *id, float x1, float y1, float x2, float y2, float *color, bool alpha) const {
    const ImVec2 a = At(x1, y1), b = At(x2, y2);
    ImGui::SetCursorScreenPos(a);
    return setup::ColorBar(id, color, alpha, {b.x - a.x, b.y - a.y});
}

void DevPage::End(float x, float y) const {
    At(x, y);
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(m_Extent);
}

} // namespace vdev::setup
