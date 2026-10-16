#pragma once

#include <vector>

#include "imgui.h"

namespace vdev {

// Dear ImGui colors modeled on Virtools Dev 2.5: light gray panels with
// black text, dark menu bar, toolbars and status bar with light text.
void ApplyDevTheme(ImGuiStyle &style);

// Dark chrome (menu bar, toolbars, status bar): push before, pop after.
void PushDarkChrome();
void PopDarkChrome();

// Dev's context menus (screenshots): light, rounded, roomy rows. Push before
// BeginPopup, pop after (also when the popup is not open).
void PushDevMenuStyle();
void PopDevMenuStyle();

// ImGui::Begin for docked panels: their tab labels are light on Dev's dark
// tabs, the contents keep the panel colors.
bool BeginDevWindow(const char *name, bool *open = nullptr, ImGuiWindowFlags flags = 0);
// Right after BeginDevWindow of a window that is not one of Dev's default
// ones (documentation "Customizing the Interface"): "select its tab and
// then press DELETE, or right-click the tab and choose Close" (a Setup's
// menu says "Close Window"). Delete counts once the tab (or title bar) was
// clicked last, so the lists' own Delete keys stay theirs. Clears *open.
void DevTabClose(bool *open, const char *label = "Close");
// Options > Reset Interface ("close all separated windows"): the windows
// floating then (their ImGui IDs) close at their next DevTabClose.
void DevCloseSeparated(const std::vector<unsigned int> &windows);

// A text button of Dev's toolbars (a VIButton in ZC_TOOLBARBACK, outlined
// in ZC_TOOLBARBORDER, white text) in min..max, on a dark strip (inside
// PushDarkChrome); an active toggle is drawn pressed. The icon buttons are
// DevBitmapButton's.
bool DevToolbarPill(const char *id, const char *label, const ImVec2 &min, const ImVec2 &max, bool on, const char *tooltip,
                    bool enabled = true);

// A bold face of the UI font (expanded behavior graph names), set when the
// fonts are loaded; null when none was found.
void SetBoldFont(ImFont *font);
ImFont *BoldFont();

// InterfaceControls.dll's VI fonts 0 and 1 (sub_24211F50, GetVIFont): Arial
// of height 12 and width 5, Arial Bold of height 14 and width 5 (the status
// bar's texts); null when Arial was not found. VIFontWidth: the horizontal
// scale GDI gives them for that width (5 of an average of 6).
void SetVIFonts(ImFont *normal, ImFont *bold);
ImFont *VIFont(bool bold);
float VIFontHeight(bool bold);
float VIFontWidth(bool bold);
// A text in a VI font scale times its height, from at (its top left);
// clipMax: the clip rectangle's corner from at.
float VITextWidth(const char *text, bool bold, float scale);
void DrawVIText(ImDrawList *dl, const ImVec2 &at, ImU32 color, const char *text, bool bold, float scale,
                const ImVec2 *clipMax = nullptr);

constexpr ImU32 DEV_CHROME = IM_COL32(65, 65, 65, 255); // toolbars, status bar (screenshot)

} // namespace vdev
