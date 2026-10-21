#include "ui/controls/DevTheme.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <set>

#include "imgui_internal.h"

namespace vdev {

namespace {

ImVec4 Gray(int level, int alpha = 255) {
    return ImVec4(level / 255.0f, level / 255.0f, level / 255.0f, alpha / 255.0f);
}

ImVec4 Rgb(int r, int g, int b, int alpha = 255) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, alpha / 255.0f);
}

} // namespace

void ApplyDevTheme(ImGuiStyle &style, float contentScale) {
    style = ImGuiStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 7.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;
    style.WindowPadding = ImVec2(6, 6);
    style.FramePadding = ImVec2(5, 3);
    style.ItemSpacing = ImVec2(6, 4);

    ImVec4 *c = style.Colors;
    c[ImGuiCol_Text] = Gray(0);
    c[ImGuiCol_TextDisabled] = Gray(70);
    c[ImGuiCol_WindowBg] = Gray(176);
    c[ImGuiCol_ChildBg] = Gray(176);
    c[ImGuiCol_PopupBg] = Gray(212);
    c[ImGuiCol_Border] = Gray(80);
    c[ImGuiCol_BorderShadow] = Gray(0, 0);
    c[ImGuiCol_FrameBg] = Gray(222);
    c[ImGuiCol_FrameBgHovered] = Gray(236);
    c[ImGuiCol_FrameBgActive] = Gray(246);
    c[ImGuiCol_TitleBg] = Gray(100); // dock tab strips (screenshot)
    c[ImGuiCol_TitleBgActive] = Gray(100);
    c[ImGuiCol_TitleBgCollapsed] = Gray(100);
    c[ImGuiCol_MenuBarBg] = Gray(51);
    c[ImGuiCol_ScrollbarBg] = Gray(160);
    c[ImGuiCol_ScrollbarGrab] = Gray(120);
    c[ImGuiCol_ScrollbarGrabHovered] = Gray(100);
    c[ImGuiCol_ScrollbarGrabActive] = Gray(80);
    c[ImGuiCol_CheckMark] = Gray(0);
    c[ImGuiCol_SliderGrab] = Gray(110);
    c[ImGuiCol_SliderGrabActive] = Gray(80);
    c[ImGuiCol_Button] = Gray(200);
    c[ImGuiCol_ButtonHovered] = Gray(220);
    c[ImGuiCol_ButtonActive] = Gray(240);
    c[ImGuiCol_Header] = Rgb(120, 140, 185);
    c[ImGuiCol_HeaderHovered] = Rgb(150, 168, 205);
    c[ImGuiCol_HeaderActive] = Rgb(110, 130, 180);
    c[ImGuiCol_Separator] = Gray(110);
    c[ImGuiCol_SeparatorHovered] = Gray(80);
    c[ImGuiCol_SeparatorActive] = Gray(60);
    c[ImGuiCol_ResizeGrip] = Gray(140);
    c[ImGuiCol_ResizeGripHovered] = Gray(110);
    c[ImGuiCol_ResizeGripActive] = Gray(90);
    c[ImGuiCol_Tab] = Gray(118);
    c[ImGuiCol_TabHovered] = Gray(140);
    c[ImGuiCol_TabSelected] = Gray(51);
    c[ImGuiCol_TabSelectedOverline] = Gray(51);
    c[ImGuiCol_TabDimmed] = Gray(118);
    c[ImGuiCol_TabDimmedSelected] = Gray(58);
    c[ImGuiCol_TabDimmedSelectedOverline] = Gray(58);
    c[ImGuiCol_DockingPreview] = Rgb(70, 90, 160, 160);
    c[ImGuiCol_DockingEmptyBg] = Gray(100);
    c[ImGuiCol_TableHeaderBg] = Gray(214);
    c[ImGuiCol_TableBorderStrong] = Gray(110);
    c[ImGuiCol_TableBorderLight] = Gray(150);
    c[ImGuiCol_TableRowBg] = Gray(0, 0);
    c[ImGuiCol_TableRowBgAlt] = Gray(190);
    c[ImGuiCol_TextSelectedBg] = Rgb(110, 130, 190, 140);
    c[ImGuiCol_DragDropTarget] = Rgb(40, 60, 200);
    c[ImGuiCol_NavCursor] = Rgb(40, 60, 200);
    c[ImGuiCol_ModalWindowDimBg] = Gray(0, 70);
    if (!std::isfinite(contentScale) || contentScale <= 0)
        contentScale = 1;
    style.ScaleAllSizes(contentScale);
    style.FontSizeBase = 14;
    style.FontScaleDpi = contentScale;
}

void PushDevMenuStyle() {
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Gray(249));
    ImGui::PushStyleColor(ImGuiCol_Border, Gray(200));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Gray(229));
    ImGui::PushStyleColor(ImGuiCol_Header, Gray(229));
    ImGui::PushStyleColor(ImGuiCol_Separator, Gray(215));
    ImGui::PushStyleColor(ImGuiCol_Text, Gray(0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 8.0f));
}

void PopDevMenuStyle() {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(6);
}

bool BeginDevWindow(const char *name, bool *open, ImGuiWindowFlags flags) {
    // The dock node draws its tab bar inside the first Begin of the frame.
    ImGui::PushStyleColor(ImGuiCol_Text, Gray(236));
    const bool visible = ImGui::Begin(name, open, flags);
    ImGui::PopStyleColor();
    return visible;
}

namespace {
std::set<ImGuiID> s_Separated; // the floating windows Reset Interface closes
} // namespace

void DevCloseSeparated(const std::vector<unsigned int> &windows) { s_Separated.insert(windows.begin(), windows.end()); }

void DevTabClose(bool *open, const char *label) {
    if (!open) return;
    if (!s_Separated.empty() && s_Separated.erase(ImGui::GetCurrentWindow()->ID)) {
        *open = false;
        return;
    }
    ImGuiStorage *storage = ImGui::GetStateStorage();
    const ImGuiID armed = ImGui::GetID("##tabarmed");
    // After Begin the last item is the window's tab (docked) or its title bar.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
        storage->SetBool(armed, true);
    else if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        storage->SetBool(armed, false);
    // Dev's tab menu (Dev.exe sub_414030): "Move to separated window" (a
    // docked window made floating), then Close.
    ImGuiWindow *host = ImGui::GetCurrentWindow();
    if (ImGui::BeginPopupContextItem("##tabmenu")) {
        if (ImGui::MenuItem("Move to separated window", nullptr, false, host->DockIsActive))
            ImGui::DockContextQueueUndockWindow(ImGui::GetCurrentContext(), host);
        if (ImGui::MenuItem(label)) *open = false;
        ImGui::EndPopup();
    }
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        storage->SetBool(armed, false);
        return;
    }
    if (storage->GetBool(armed) && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        *open = false;
}

void PushDarkChrome() {
    ImGui::PushStyleColor(ImGuiCol_Text, Gray(235));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, Gray(170));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Gray(66));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, Gray(51));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Gray(65));
    ImGui::PushStyleColor(ImGuiCol_Header, Gray(96));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Gray(100));
    ImGui::PushStyleColor(ImGuiCol_Button, Gray(58));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Gray(90));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Gray(110));
    ImGui::PushStyleColor(ImGuiCol_Border, Gray(200));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Gray(70));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Gray(90));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, Gray(240));
    ImGui::PushStyleColor(ImGuiCol_Separator, Gray(120));
}

void PopDarkChrome() {
    ImGui::PopStyleColor(15);
}

// One Dev Schematic toolbar button: text buttons are outlined pills, icon
// buttons are flat; an active toggle is drawn pressed.
bool DevToolbarPill(const char *id, const char *label, const ImVec2 &a, const ImVec2 &b, bool on, const char *tooltip,
                    bool enabled) {
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(a);
    if (!enabled) ImGui::BeginDisabled();
    const bool clicked = ImGui::InvisibleButton("button", ImVec2(std::max(1.0f, b.x - a.x), std::max(1.0f, b.y - a.y)));
    const bool held = ImGui::IsItemActive();
    if (!enabled) ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float rounding = (b.y - a.y) * 0.5f;
    const ImU32 fill = (on || held) && enabled ? IM_COL32(118, 118, 118, 255)
                       : hovered && enabled    ? IM_COL32(80, 80, 80, 255)
                                               : 0;
    if (fill) dl->AddRectFilled(a, b, fill, rounding);
    dl->AddRect(a, b, IM_COL32(196, 196, 196, 255), rounding);
    // The label, smaller when it does not fit (Dev's font is smaller).
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float size = ImGui::GetFontSize();
    const float room = std::max(1.0f, b.x - a.x - 2.0f * rounding);
    if (textSize.x > room) {
        size *= room / textSize.x;
        textSize = ImVec2(room, textSize.y * room / textSize.x);
    }
    const ImU32 text = enabled ? IM_COL32(236, 236, 236, 255) : IM_COL32(130, 130, 130, 255);
    dl->AddText(ImGui::GetFont(), size, ImVec2(a.x + (b.x - a.x - textSize.x) * 0.5f, a.y + (b.y - a.y - textSize.y) * 0.5f),
                text, label);
    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return clicked && enabled;
}



namespace {
ImFont *g_BoldFont = nullptr;
ImFont *g_VIFonts[2] = {};
} // namespace

void SetBoldFont(ImFont *font) { g_BoldFont = font; }
ImFont *BoldFont() { return g_BoldFont; }

void SetVIFonts(ImFont *normal, ImFont *bold) {
    g_VIFonts[0] = normal;
    g_VIFonts[1] = bold;
}

ImFont *VIFont(bool bold) {
    ImFont *font = g_VIFonts[bold ? 1 : 0];
    return font ? font : bold && g_BoldFont ? g_BoldFont : ImGui::GetFont();
}

float VIFontHeight(bool bold) { return bold ? 14.0f : 12.0f; }

// GDI's lfWidth 5 against the fonts' average character width of 6: Dev's
// status bar texts are 5/6 as wide as Arial's own (measured on a Dev
// screenshot: "Nothing selected" 0.82, "No action" in bold 0.84).
float VIFontWidth(bool bold) { return g_VIFonts[bold ? 1 : 0] ? 5.0f / 6.0f : 1.0f; }

float VITextWidth(const char *text, bool bold, float scale) {
    ImFont *font = VIFont(bold);
    const float size = VIFontHeight(bold) * scale;
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x * VIFontWidth(bold);
}

void DrawVIText(ImDrawList *dl, const ImVec2 &at, ImU32 color, const char *text, bool bold, float scale, const ImVec2 *clipMax) {
    ImFont *font = VIFont(bold);
    const float size = VIFontHeight(bold) * scale, narrow = VIFontWidth(bold);
    const int first = dl->VtxBuffer.Size;
    // Laid out at the font's own width (clipped there), then narrowed about at.x.
    if (clipMax) dl->PushClipRect(at, ImVec2(at.x + (clipMax->x - at.x) / narrow, clipMax->y), true);
    dl->AddText(font, size, at, color, text);
    if (clipMax) dl->PopClipRect();
    if (narrow != 1.0f)
        for (int i = first; i < dl->VtxBuffer.Size; ++i) dl->VtxBuffer[i].pos.x = at.x + (dl->VtxBuffer[i].pos.x - at.x) * narrow;
}

} // namespace vdev
