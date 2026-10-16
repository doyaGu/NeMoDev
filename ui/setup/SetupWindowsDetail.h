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

template <typename T> T *Get(EditorState &state, CK_ID id) {
    return state.HasDocument() ? T::Cast(state.document->Context()->GetObject(id)) : nullptr;
}

// The composition's objects of a class (and of its derived classes when
// derived), by name: not the deleted ones (Document::LiveObjects).
inline std::vector<CKObject *> ObjectsOf(const Document &document, CK_CLASSID cid, bool derived) {
    std::vector<CKObject *> objects;
    for (CKObject *object : document.LiveObjects(cid, derived))
        if (!object->IsPrivate() && !object->IsDynamic()) objects.push_back(object);
    std::sort(objects.begin(), objects.end(), [](CKObject *a, CKObject *b) {
        return _stricmp(a->GetName() ? a->GetName() : "", b->GetName() ? b->GetName() : "") < 0;
    });
    return objects;
}

// Dev (sub_4B0010) opens the 3D Frame Setup for 3D entities flagged
// CK_3DENTITY_FRAME (and its variants for portals and places), the 3D Object
// Setup for the other 3D entities and 3D objects.
inline bool IsFrame(CKObject *object) {
    CK3dEntity *entity = CK3dEntity::Cast(object);
    if (!entity) return false;
    if (CKPlace::Cast(object)) return true;
    const CK_CLASSID cid = object->GetClassID();
    if (cid != CKCID_3DENTITY && cid != CKCID_3DOBJECT) return false;
    return (entity->GetFlags() & (CK_3DENTITY_FRAME | CK_3DENTITY_PORTAL)) != 0;
}

inline const char *PixelFormatName(VX_PIXELFORMAT format) {
    const char *name = VxPixelFormat2String(format);
    return name && *name ? name : "Unknown";
}

// The render driver of the first render context (else driver 0) takes
// textures of the format (VxDriverDesc::TextureFormats): Dev's "*" after a
// Desired Video Format.
inline bool DriverTakesFormat(CKContext *context, VX_PIXELFORMAT format) {
    CKRenderManager *renderer = context ? context->GetRenderManager() : nullptr;
    if (!renderer) return false;
    int driver = 0;
    if (renderer->GetRenderContextCount() > 0 && renderer->GetRenderContext(0)) driver = renderer->GetRenderContext(0)->GetDriverIndex();
    VxDriverDesc *desc = driver >= 0 && driver < renderer->GetRenderDriverCount() ? renderer->GetRenderDriverDescription(driver) : nullptr;
    if (!desc) return false;
    for (int i = 0; i < desc->TextureFormats.Size(); ++i)
        if (VxImageDesc2PixelFormat(desc->TextureFormats[i]) == format) return true;
    return false;
}

// Dev's Video Memory Information (documentation "Texture Setup": "height x
// width in pixels, color depth in bits, video format in bits per argb
// component").
inline void VideoMemoryLines(bool inVideo, bool described, const VxImageDescEx &desc) {
    if (!inVideo) {
        ImGui::TextUnformatted("Not In Video Memory");
        return;
    }
    ImGui::TextUnformatted("In Video Memory");
    if (!described) return;
    ImGui::Text("%d x %d x %d bits", desc.Width, desc.Height, desc.BitsPerPixel);
    ImGui::Text("%s", PixelFormatName(VxImageDesc2PixelFormat(desc)));
}

inline VxVector Translation(const VxMatrix &m) {
    return VxVector(m[3][0], m[3][1], m[3][2]);
}

inline VxVector ScaleOf(const VxMatrix &m) {
    return VxVector(Magnitude(VxVector(m[0][0], m[0][1], m[0][2])), Magnitude(VxVector(m[1][0], m[1][1], m[1][2])),
                    Magnitude(VxVector(m[2][0], m[2][1], m[2][2])));
}

} // namespace setup
} // namespace vdev
