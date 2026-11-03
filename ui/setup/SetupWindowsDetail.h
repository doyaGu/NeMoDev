#pragma once

// The Setup files' own helpers (SetupWindows.cpp, SetupRenderPages.cpp,
// SetupScenePages.cpp, SetupObjectPages.cpp).

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"

#include "VxMatrix.h"
#include "VxColor.h"
#include "VxRect.h"

#include "features/setup/SetupViewData.h"
#include "ui/controls/DevTheme.h"
#include "ui/setup/SetupLayout.h"

namespace vdev {
namespace setup {

constexpr float RAD_TO_DEG = 57.29577951f;

inline ImU32 ToImColor(const VxColor &c) {
    return IM_COL32(static_cast<int>(c.r * 255), static_cast<int>(c.g * 255), static_cast<int>(c.b * 255), 255);
}

// A Dev group title with a vertical rule below it.
inline void Group(const char *title) {
    ImGui::PushStyleColor(ImGuiCol_Text, CurrentDevTheme().controls[0]);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
}


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
