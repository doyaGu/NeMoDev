#pragma once

#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

namespace vdev {

// Dev's InterfaceControls.dll look (VIEdit, VIComboBox, VICheckButton,
// VIStaticText, VIDialog and its buttons) for the dialogs drawn after Dev's
// templates. Rectangles are in screen pixels.
namespace vi {

// InterfaceControls.dll's colors (sub_24211910): 0 black, 1 51, 2 65,
// 3 100, 4 128, 5 152, 6 176, 7 200, 8 230, 9 white, 10 red.
ImU32 Color(int index);

enum Align { LEFT, CENTER, RIGHT };

// A static text (VIStaticText) in its rectangle, vertically from the top.
void Text(const ImVec2 &min, const ImVec2 &max, const char *text, bool bold = false, Align align = LEFT,
          ImU32 color = IM_COL32(0, 0, 0, 255));
float TextWidth(const char *text, bool bold);

// A VIEdit: the text on the dialog's background over a thin line. True
// when the text was changed (text holds what is typed); deactivated: set
// on the frame it loses the focus after a change.
bool Edit(const char *id, const ImVec2 &min, const ImVec2 &max, std::string &text, Align align = CENTER,
          bool *deactivated = nullptr);

// A VIComboBox: the text, the arrow at the right, a dark line; a click
// shows the list (items is called only then). The index chosen, else -1.
int Combo(const char *id, const ImVec2 &min, const ImVec2 &max, const std::string &preview,
          const std::function<std::vector<std::string>()> &items, int current);

// A VIComboBox with an edit (CBS_DROPDOWN): the text typed, or an item of
// the list shown by the arrow. True when text is given (an item chosen, or
// the edit left after a change).
bool EditCombo(const char *id, const ImVec2 &min, const ImVec2 &max, std::string &text,
               const std::function<std::vector<std::string>()> &items);

// A VICheckButton of size pixels; true when toggled.
bool Check(const char *id, const ImVec2 &min, float size, bool &on);
// A VIRadioButton of size pixels with its label after it; true when chosen.
bool Radio(const char *id, const ImVec2 &min, float size, bool on, const char *label);

// A VIButton (InterfaceControls.dll DrawItem: RoundRect 9 x 9, outlined)
// with its label centered; disabled: the darker fill.
bool Button(const char *id, const ImVec2 &min, const ImVec2 &max, const char *label, bool enabled = true);

// A VIToolbarButton of the managers' toolbars (Level Manager's Set IC For
// Selected, Attribute Manager's): a pill outlined in ZC_TOOLBARBORDER on the
// toolbar, white under the mouse, its text white.
bool ToolbarButton(const char *id, const ImVec2 &min, const ImVec2 &max, const char *label, float s);

// A VICheckButton (InterfaceControls.dll OnPaint): its 11 x 11 box at (1, 1)
// black, a white cross in it when on, framed in ZC 7; the label 5 pixels
// right of the box, centered in the control's height. True when toggled.
bool CheckButton(const char *id, const ImVec2 &min, const ImVec2 &max, bool &on, const char *label,
                 bool enabled = true);

// A static frame of Dev's templates (SS_BLACKFRAME): a gray line.
void Frame(const ImVec2 &min, const ImVec2 &max);

// A list view of Dev's dialogs (SysListView32 in report view, or the
// VIList of the Script Debugger's log, header: true): the header, the rows
// on ZC 7, a selected one on ZC 4 with white text. columns: the header's
// labels and widths in pixels (0: the rest), their texts' alignment (the
// header's too; a label wider than its column ends with "..."). The row
// clicked at this frame (-1: none); doubleClicked, column: set for it.
// headerClicked: the headers are buttons (LVN_COLUMNCLICK), the one
// clicked at this frame (-1: none). Only the rows in view are drawn.
struct ListColumn {
    const char *label;
    float width;
    Align align = LEFT;
};
int ListView(const char *id, const ImVec2 &min, const ImVec2 &max, float scale, const std::vector<ListColumn> &columns,
             int rows, const std::function<std::string(int row, int column)> &cell,
             const std::function<bool(int row)> &selected, bool *doubleClicked = nullptr, int *column = nullptr,
             bool viList = false, int *headerClicked = nullptr,
             const std::function<ImTextureID(int row)> &icon = nullptr);

// A VIDialog: the dark title bar with its title in bold white, the body of
// ZC_DIALOGBACKDARK; client: the size below the title bar. Modal (an
// ImGui popup opened with ImGui::OpenPopup(id)). origin: the client's
// top-left corner. EndDialog only after true.
bool BeginDialog(const char *id, const std::string &title, const ImVec2 &client, float scale, ImVec2 &origin);
void EndDialog();
// A modeless VIDialog (the Script Debugger, the explorers): a window drawn
// as BeginDialog's, placed at the center when it first shows. EndWindow
// after it whatever it returns (ImGui::End).
bool BeginWindow(const char *id, const std::string &title, const ImVec2 &client, float scale, ImVec2 &origin);
void EndWindow();
float TitleHeight(float scale);

// The rounded panel holding a dialog's controls (VIDialog::SetRoundRect(7, 7)).
void Panel(const ImVec2 &min, const ImVec2 &max, float scale);

// A dialog laid out after its template in Dev.exe's resources (dialog units
// of 1.5 pixels, as Dev shows them): the panel 12 pixels from the client's
// sides and pad pixels around the template, the buttons' bar under it.
struct TemplateLayout {
    float s = 1.0f;
    ImVec2 origin;             // the template's (0, 0)
    ImVec2 panelMin, panelMax; // the rounded panel
    float buttonsY = 0.0f;     // the bar's buttons (DialogButton) top
    float right = 0.0f;        // the bar's right end
    ImVec2 P(float x, float y) const { return ImVec2(origin.x + x * 1.5f * s, origin.y + y * 1.5f * s); }
    // The rectangle (x, y, w, h) of the template's control.
    ImVec2 Min(float x, float y) const { return P(x, y); }
    ImVec2 Max(float x, float y, float w, float h) const { return P(x + w, y + h); }
};
// The client size of a template of units dialog units (buttons: with the bar).
ImVec2 TemplateClient(const ImVec2 &units, float scale, float pad, bool buttons = true);
TemplateLayout LayoutTemplate(const ImVec2 &clientOrigin, const ImVec2 &units, float scale, float pad);
// Draws the layout's panel.
void TemplatePanel(const TemplateLayout &layout);

// A dialog's custom button (VIDialogButton, 60 x 18): white outline and
// bold text; Enter / Escape press OK / Cancel when key is set.
bool DialogButton(const char *label, const ImVec2 &min, float scale, ImGuiKey key = ImGuiKey_None);
// While a parameter UI's target button holds the mouse (CK2UI's capture),
// the dialog buttons and their keys do nothing.
void BlockDialogButtons(bool blocked);

} // namespace vi
} // namespace vdev
