#pragma once
#include "imgui.h"

namespace vdev::setup {
// Setup coordinates share the current font and the selected page spacing.
float PageUnit();

// A numeric edit and its slider keep the same IDs in either page layout.
bool EditSlider(const char *id, float *value, float low, float high, const char *format, float speed = .01f);
bool ColorBar(const char *id, float *color, bool alpha = false, ImVec2 size = {});

// Native table columns wrap complete sections and keep their input IDs stable.
class SetupColumns {
public:
    SetupColumns(const char *id, int maximum, float width = 0);
    ~SetupColumns();
    SetupColumns(const SetupColumns &) = delete;
    SetupColumns &operator=(const SetupColumns &) = delete;
    explicit operator bool() const { return m_Open; }
    void Next();

private:
    bool m_Open = false;
};

// A label/control form uses the available section width, including nested cells.
class SetupForm {
public:
    explicit SetupForm(const char *id, float width = 0);
    ~SetupForm();
    SetupForm(const SetupForm &) = delete;
    SetupForm &operator=(const SetupForm &) = delete;
    explicit operator bool() const { return m_Open; }
    void Row(const char *label);

private:
    bool m_Open = false;
};

// A Setup page laid out as Dev's builders lay theirs (Dev.exe sub_464E20
// and the others: SetRect, pixels from the page's top left), scaled with
// the font. Made where the page begins.
struct DevPage {
    DevPage();
    ImVec2 origin;
    float s;
    ImVec2 At(float x, float y) const;
    // The next item in the rect: at its left, its frame centered in the
    // rect's height (at its top in a lower rect), as wide; the width.
    float Place(float x1, float y1, float x2, float y2) const;
    // A static text (VIStaticText) in its rect, level with a frame's text.
    void Label(float x1, float y1, float x2, float y2, const char *text) const;
    // One of Dev's "Lines" (a rect 1 pixel wide or high, color 4).
    void Line(float x1, float y1, float x2, float y2) const;
    // A check button (VIBitmapButton, bitmaps 609 to 612) at the point:
    // true when clicked (on toggled).
    bool Check(const char *id, float x, float y, bool &on) const;
    // A value's edit (VIEdit, a drag here) and slider (VISlider) in their
    // rects (x1, y1, x2, y2): true when changed. The slider is the id's
    // "##slider" (the edit the id).
    bool EditSlider(const char *id, const ImVec4 &edit, const ImVec4 &slider, float *value, float low, float high,
                    const char *format, float speed = 0.01f) const;
    // A color's hue bar (Dev's "Huebar"; a click shows the picker): true
    // when the color (rgb, or rgba with alpha) is changed.
    bool ColorBar(const char *id, float x1, float y1, float x2, float y2, float *color, bool alpha = false) const;
    // Reserve the template size and any larger measured text/control extent.
    void End(float x, float y) const;

private:
    void Include(ImVec2 point) const;
    mutable ImVec2 m_Extent;
};

} // namespace vdev::setup
