#pragma once

// The Setup files' own helpers (SetupWindows.cpp, SetupRenderPages.cpp,
// SetupScenePages.cpp, SetupObjectPages.cpp).

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"

#include "CKAll.h"

#include "ui/app/EditorState.h"
#include "ui/controls/ObjectText.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {
namespace setup {

constexpr float RAD_TO_DEG = 57.29577951f;

inline std::string NameOf(CKObject *object, const char *none = "-- None --") { return DisplayName(object, none); }

inline ImU32 ToImColor(const VxColor &c) {
    return IM_COL32(static_cast<int>(c.r * 255), static_cast<int>(c.g * 255), static_cast<int>(c.b * 255), 255);
}

// A Dev group title with a vertical rule below it.
inline void Group(const char *title) {
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
}

// A Setup page laid out as Dev's builders lay theirs (Dev.exe sub_464E20
// and the others: SetRect, pixels from the page's top left), scaled with
// the font. Made where the page begins.
struct DevPage {
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float s = ImGui::GetFontSize() / 13.0f;
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
    // The page's extent (what it scrolls) to the point.
    void End(float x, float y) const;
};

inline VxVector VectorOf(SetupFields values, const char *key) {
    const auto &v = values.Reals(key);
    return v.size() == 3 ? VxVector(float(v[0]), float(v[1]), float(v[2])) : VxVector(0, 0, 0);
}
inline Vx2DVector Vector2Of(SetupFields values, const char *key) {
    const auto &v = values.Reals(key);
    return v.size() == 2 ? Vx2DVector(float(v[0]), float(v[1])) : Vx2DVector(0, 0);
}
inline VxRect RectOf(SetupFields values, const char *key) {
    const auto &v = values.Reals(key);
    return v.size() == 4 ? VxRect(float(v[0]), float(v[1]), float(v[2]), float(v[3])) : VxRect(0, 0, 0, 0);
}
inline VxColor ColorOf(SetupFields values, const char *key) {
    const auto &v = values.Reals(key);
    return v.size() == 4 ? VxColor(float(v[0]), float(v[1]), float(v[2]), float(v[3])) : VxColor(0, 0, 0, 1);
}

// The render driver of the first render context (else driver 0) takes
// textures of the format (VxDriverDesc::TextureFormats): Dev's "*" after a
// Desired Video Format.
inline VxVector Translation(const VxMatrix &m) {
    return VxVector(m[3][0], m[3][1], m[3][2]);
}

inline VxVector ScaleOf(const VxMatrix &m) {
    return VxVector(Magnitude(VxVector(m[0][0], m[0][1], m[0][2])), Magnitude(VxVector(m[1][0], m[1][1], m[1][2])),
                    Magnitude(VxVector(m[2][0], m[2][1], m[2][2])));
}

} // namespace setup
} // namespace vdev
