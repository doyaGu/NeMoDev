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

static void DefaultDevStyle(ImGuiStyle &style) {
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
    style.FontSizeBase = 14;
}

DevTheme DefaultDevTheme() {
    DevTheme theme{};
    DefaultDevStyle(theme.style);
    const int chrome[] = {235, 170, 66, 51, 65, 96, 100, 58, 90, 110, 200, 70, 90, 240, 120};
    const int menu[] = {249, 200, 229, 229, 215, 0};
    const int controls[] = {0, 51, 65, 100, 128, 152, 176, 200, 230, 255};
    const int pill[] = {118, 80, 196, 236, 130};
    for (int i = 0; i < 15; ++i) theme.chrome[i] = Gray(chrome[i]);
    for (int i = 0; i < 6; ++i) theme.menu[i] = Gray(menu[i]);
    for (int i = 0; i < 10; ++i) theme.controls[i] = IM_COL32(controls[i], controls[i], controls[i], 255);
    theme.controls[10] = IM_COL32(255, 0, 0, 255);
    for (int i = 0; i < 5; ++i) theme.pill[i] = IM_COL32(pill[i], pill[i], pill[i], 255);
    theme.tabText = IM_COL32(236, 236, 236, 255);
    theme.controlLine = IM_COL32(34, 34, 34, 255);
    theme.controlOutline = IM_COL32(188, 188, 188, 255);
    theme.controlTableHeader = IM_COL32(240, 240, 240, 255);
    theme.controlTableSort = IM_COL32(0, 0, 0, 24);
    const int layout[] = {65, 157, 128, 21, 100, 40, 100, 150, 20, 255, 255, 255, 255};
    for (int i = 0; i < 13; ++i) theme.layout[i] = IM_COL32(layout[i], layout[i], layout[i], 255);
    theme.layout[11] = IM_COL32(0, 255, 0, 255);
    theme.viWidth[0] = theme.viWidth[1] = 5.0f / 6.0f;
    theme.menuRounding = 8;
    theme.menuPadding = {10, 8};
    theme.menuSpacing = {12, 8};
    theme.selectionFill = theme.selectionBorder = IM_COL32(10, 36, 106, 255);
    theme.selectionText = IM_COL32(255, 255, 255, 255);
    theme.hoverBorder = IM_COL32(120, 120, 120, 255);
    theme.previewBackground = IM_COL32(128, 128, 128, 255);
    theme.previewBorder = IM_COL32(0, 0, 0, 255);
    theme.previewGrid = IM_COL32(110, 110, 110, 255);
    theme.previewCurve = IM_COL32(255, 0, 0, 255);
    theme.previewMarker = IM_COL32(220, 0, 0, 255);
    theme.imageBackground = theme.snapshotBackground = IM_COL32(255, 255, 255, 255);
    theme.warningText = IM_COL32(153, 0, 0, 255);
    theme.snapshotEmptyBorder = IM_COL32(160, 160, 160, 255);
    return theme;
}
DevTheme DefaultUiTheme() {
    DevTheme theme{};
    const ImGuiCol chrome[] = {
        ImGuiCol_Text,   ImGuiCol_TextDisabled,  ImGuiCol_PopupBg,        ImGuiCol_MenuBarBg,     ImGuiCol_WindowBg,
        ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_Button,         ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive,
        ImGuiCol_Border, ImGuiCol_FrameBg,       ImGuiCol_FrameBgHovered, ImGuiCol_CheckMark,     ImGuiCol_Separator};
    const ImGuiCol menu[] = {ImGuiCol_PopupBg, ImGuiCol_Border,    ImGuiCol_HeaderHovered,
                             ImGuiCol_Header,  ImGuiCol_Separator, ImGuiCol_Text};
    const ImGuiCol control[] = {ImGuiCol_Text,          ImGuiCol_MenuBarBg, ImGuiCol_FrameBg,      ImGuiCol_TitleBg,
                                ImGuiCol_TextDisabled,  ImGuiCol_Separator, ImGuiCol_WindowBg,     ImGuiCol_Border,
                                ImGuiCol_HeaderHovered, ImGuiCol_FrameBg,   ImGuiCol_PlotHistogram};
    const ImGuiCol pill[] = {ImGuiCol_ButtonActive, ImGuiCol_ButtonHovered, ImGuiCol_Border, ImGuiCol_Text,
                             ImGuiCol_TextDisabled};
    for (int i = 0; i < 15; ++i) theme.chrome[i] = theme.style.Colors[chrome[i]];
    for (int i = 0; i < 6; ++i) theme.menu[i] = theme.style.Colors[menu[i]];
    for (int i = 0; i < 11; ++i) theme.controls[i] = ImGui::ColorConvertFloat4ToU32(theme.style.Colors[control[i]]);
    for (int i = 0; i < 5; ++i) theme.pill[i] = ImGui::ColorConvertFloat4ToU32(theme.style.Colors[pill[i]]);
    for (auto &color : theme.layout) color = theme.controls[2];
    for (const int i : {1, 3, 5, 7}) theme.layout[i] = theme.controls[7];
    theme.layout[4] = theme.controls[6];
    for (const int i : {8, 10, 12}) theme.layout[i] = theme.controls[0];
    theme.layout[9] = ImGui::ColorConvertFloat4ToU32(theme.style.Colors[ImGuiCol_NavCursor]);
    theme.layout[11] = ImGui::ColorConvertFloat4ToU32(theme.style.Colors[ImGuiCol_PlotHistogram]);
    theme.tabText = theme.controls[0];
    theme.controlLine = theme.controlOutline = theme.controls[7];
    theme.controlTableHeader = ImGui::ColorConvertFloat4ToU32(theme.style.Colors[ImGuiCol_TableHeaderBg]);
    theme.controlTableSort = ImGui::ColorConvertFloat4ToU32(theme.style.Colors[ImGuiCol_HeaderActive]);
    theme.menuRounding = theme.style.PopupRounding;
    theme.menuPadding = theme.style.WindowPadding;
    theme.menuSpacing = theme.style.ItemSpacing;
    theme.viWidth[0] = theme.viWidth[1] = 1;
    theme.selectionFill = ImGui::ColorConvertFloat4ToU32(theme.style.Colors[ImGuiCol_HeaderActive]);
    theme.selectionText = theme.controls[0];
    theme.selectionBorder = ImGui::ColorConvertFloat4ToU32(theme.style.Colors[ImGuiCol_NavCursor]);
    theme.hoverBorder = theme.controls[8];
    theme.previewBackground = theme.imageBackground = theme.snapshotBackground = theme.controls[6];
    theme.previewBorder = theme.previewGrid = theme.snapshotEmptyBorder = theme.controls[7];
    theme.previewCurve = ImGui::ColorConvertFloat4ToU32(theme.style.Colors[ImGuiCol_PlotLines]);
    theme.previewMarker = theme.warningText =
        ImGui::ColorConvertFloat4ToU32(theme.style.Colors[ImGuiCol_PlotHistogram]);
    VisitSchematicColors(theme.schematic, [&](const char *, ImU32 &color) { color = theme.controls[0]; });
    auto &graph = theme.schematic;
    graph.canvas = graph.thumbnailFill = theme.controls[6];
    graph.grid = graph.headerEdge = graph.thumbnailBorder = graph.bevelDark = graph.graphNameEdge =
        graph.behaviorBorder = graph.pinBorder = graph.parameterBorder = graph.operationBorder = graph.commentBorder =
            graph.startBorder = theme.controls[7];
    graph.behaviorFill = graph.buildingBlockFill = graph.graphFill = graph.openGraphFill = graph.openGraphTitle =
        graph.pinFill = graph.targetFill = graph.plainLocalFill = graph.collapsedFill = graph.operationFill =
            graph.commentFill = graph.startFill = theme.controls[2];
    graph.currentHeaderFill = graph.hover = theme.controls[8];
    graph.mutedText = theme.controls[4];
    graph.selection = graph.rubberBandBorder = theme.selectionBorder;
    graph.rubberBand = theme.selectionFill;
    graph.trace = graph.breakpointFill = graph.unresolvedText = theme.warningText;
    graph.running = theme.previewCurve;
    graph.breakpointEdge = theme.previewBorder;
    theme.dialogRounding = theme.style.WindowRounding;
    theme.groupRounding = theme.style.ChildRounding;
    theme.groupBorder = theme.style.ChildBorderSize;
    theme.setupSpacing = theme.style.ItemSpacing;
    theme.listPadding = theme.eventLogPadding = theme.style.WindowPadding;
    return theme;
}
namespace {
DevTheme g_Theme = DefaultUiTheme();
}
const DevTheme &CurrentDevTheme() { return g_Theme; }
void SetDevTheme(const DevTheme &theme) { g_Theme = theme; }
void ApplyDevTheme(ImGuiStyle &style, float contentScale) {
    if (!std::isfinite(contentScale) || contentScale <= 0) contentScale = 1;
    style = CurrentDevTheme().style;
    style.ScaleAllSizes(contentScale);
    style.FontScaleDpi = contentScale;
}

void PushDevMenuStyle() {
    ImGui::PushStyleColor(ImGuiCol_PopupBg, CurrentDevTheme().menu[0]);
    ImGui::PushStyleColor(ImGuiCol_Border, CurrentDevTheme().menu[1]);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, CurrentDevTheme().menu[2]);
    ImGui::PushStyleColor(ImGuiCol_Header, CurrentDevTheme().menu[3]);
    ImGui::PushStyleColor(ImGuiCol_Separator, CurrentDevTheme().menu[4]);
    ImGui::PushStyleColor(ImGuiCol_Text, CurrentDevTheme().menu[5]);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, CurrentDevTheme().menuRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, CurrentDevTheme().menuPadding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, CurrentDevTheme().menuSpacing);
}

void PopDevMenuStyle() {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(6);
}

bool BeginDevWindow(const char *name, bool *open, ImGuiWindowFlags flags) {
    // The dock node draws its tab bar inside the first Begin of the frame.
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(CurrentDevTheme().tabText));
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
    ImGui::PushStyleColor(ImGuiCol_Text, CurrentDevTheme().chrome[0]);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, CurrentDevTheme().chrome[1]);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, CurrentDevTheme().chrome[2]);
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, CurrentDevTheme().chrome[3]);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, CurrentDevTheme().chrome[4]);
    ImGui::PushStyleColor(ImGuiCol_Header, CurrentDevTheme().chrome[5]);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, CurrentDevTheme().chrome[6]);
    ImGui::PushStyleColor(ImGuiCol_Button, CurrentDevTheme().chrome[7]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, CurrentDevTheme().chrome[8]);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, CurrentDevTheme().chrome[9]);
    ImGui::PushStyleColor(ImGuiCol_Border, CurrentDevTheme().chrome[10]);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, CurrentDevTheme().chrome[11]);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, CurrentDevTheme().chrome[12]);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, CurrentDevTheme().chrome[13]);
    ImGui::PushStyleColor(ImGuiCol_Separator, CurrentDevTheme().chrome[14]);
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
    const ImU32 fill = (on || held) && enabled ? CurrentDevTheme().pill[0]
                       : hovered && enabled    ? CurrentDevTheme().pill[1]
                                               : 0;
    if (fill) dl->AddRectFilled(a, b, fill, rounding);
    dl->AddRect(a, b, CurrentDevTheme().pill[2], rounding);
    // The label, smaller when it does not fit (Dev's font is smaller).
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float size = ImGui::GetFontSize();
    const float room = std::max(1.0f, b.x - a.x - 2.0f * rounding);
    if (textSize.x > room) {
        size *= room / textSize.x;
        textSize = ImVec2(room, textSize.y * room / textSize.x);
    }
    const ImU32 text = enabled ? CurrentDevTheme().pill[3] : CurrentDevTheme().pill[4];
    dl->AddText(ImGui::GetFont(), size, ImVec2(a.x + (b.x - a.x - textSize.x) * 0.5f, a.y + (b.y - a.y - textSize.y) * 0.5f),
                text, label);
    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return clicked && enabled;
}



namespace {
ImFont *g_BoldFont = nullptr;
ImFont *g_VIFonts[2] = {};
float g_VIHeights[2] = {12, 14}, g_VIWidths[2] = {5.0f / 6.0f, 5.0f / 6.0f};
} // namespace

void SetBoldFont(ImFont *font) { g_BoldFont = font; }
ImFont *BoldFont() { return g_BoldFont; }

void SetVIFonts(ImFont *normal, ImFont *bold, float normalHeight, float boldHeight, float normalWidth,
                float boldWidth) {
    g_VIHeights[0] = normalHeight;
    g_VIHeights[1] = boldHeight;
    g_VIWidths[0] = normalWidth;
    g_VIWidths[1] = boldWidth;
    g_VIFonts[0] = normal;
    g_VIFonts[1] = bold;
}

ImFont *VIFont(bool bold) {
    ImFont *font = g_VIFonts[bold ? 1 : 0];
    return font ? font : bold && g_BoldFont ? g_BoldFont : ImGui::GetFont();
}

float VIFontHeight(bool bold) { return g_VIHeights[bold ? 1 : 0]; }

// GDI's lfWidth 5 against the fonts' average character width of 6: Dev's
// status bar texts are 5/6 as wide as Arial's own (measured on a Dev
// screenshot: "Nothing selected" 0.82, "No action" in bold 0.84).
float VIFontWidth(bool bold) { return g_VIFonts[bold ? 1 : 0] ? g_VIWidths[bold ? 1 : 0] : 1.0f; }

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
