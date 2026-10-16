#include "ui/controls/DevIcons.h"

#include <algorithm>

namespace vdev {

DevIcon IconForClass(CK_CLASSID cid) {
    if (cid == CKCID_LEVEL) return DevIcon::Level;
    if (cid == CKCID_PLACE) return DevIcon::Place;
    if (cid == CKCID_SCENE) return DevIcon::Scene;
    if (cid == CKCID_BEHAVIOR) return DevIcon::Script;
    if (cid && CKIsChildClassOf(cid, CKCID_2DENTITY)) return DevIcon::Entity2D;
    if (cid && CKIsChildClassOf(cid, CKCID_3DENTITY)) return DevIcon::Entity3D;
    if (cid == CKCID_MATERIAL) return DevIcon::Material;
    return DevIcon::Object;
}

void DrawDevIcon(ImDrawList *dl, const ImVec2 &at, float scale, DevIcon icon, ImU32 ink, ImU32 paper) {
    const auto Q = [&](float x, float y) { return ImVec2(at.x + x * scale, at.y + y * scale); };
    const float w = std::max(1.0f, scale);
    switch (icon) {
    case DevIcon::Level: // a stand
        dl->AddLine(Q(5, 0), Q(5, 3), ink, w);
        dl->AddLine(Q(2, 2), Q(8, 2), ink, w);
        dl->AddLine(Q(0, 4.5f), Q(10, 4.5f), ink, w);
        dl->AddLine(Q(1, 4.5f), Q(1, 10), ink, w);
        dl->AddLine(Q(1, 7), Q(9, 7), ink, w);
        dl->AddLine(Q(1, 9.5f), Q(10, 9.5f), ink, w);
        break;
    case DevIcon::Folder: // a folder with a tab
        dl->AddRectFilled(Q(0, 2), Q(10, 10), paper);
        dl->AddRect(Q(0, 2), Q(10, 10), ink, 0.0f, 0, w);
        dl->AddLine(Q(0, 2), Q(2, 0), ink, w);
        dl->AddLine(Q(2, 0), Q(10, 0), ink, w);
        dl->AddLine(Q(10, 0), Q(10, 2), ink, w);
        break;
    case DevIcon::LevelScripts: // a framed page
        dl->AddRectFilled(Q(0, 0), Q(10, 10), paper);
        dl->AddRect(Q(0, 0), Q(10, 10), ink, 0.0f, 0, w);
        dl->AddRectFilled(Q(3, 2.5f), Q(7, 5.5f), ink);
        dl->AddLine(Q(3, 7.5f), Q(7, 7.5f), ink, w);
        break;
    case DevIcon::Script: // two boxes joined by a link
        dl->AddRect(Q(0, 1), Q(4, 4.5f), ink, 0.0f, 0, w);
        dl->AddRect(Q(0, 6), Q(4, 9.5f), ink, 0.0f, 0, w);
        dl->AddRect(Q(6, 6), Q(10, 9.5f), ink, 0.0f, 0, w);
        dl->AddLine(Q(4, 7.75f), Q(6, 7.75f), ink, w);
        dl->AddLine(Q(2, 4.5f), Q(2, 6), ink, w);
        break;
    case DevIcon::Entity2D: // two frames
        dl->AddRectFilled(Q(0, 1), Q(4.5f, 9), paper);
        dl->AddRect(Q(0, 1), Q(4.5f, 9), ink, 0.0f, 0, w);
        dl->AddRectFilled(Q(5.5f, 1), Q(10, 9), paper);
        dl->AddRect(Q(5.5f, 1), Q(10, 9), ink, 0.0f, 0, w);
        break;
    case DevIcon::Entity3D: // a cube
        dl->AddRect(Q(0, 3), Q(7, 10), ink, 0.0f, 0, w);
        dl->AddLine(Q(0, 3), Q(3, 0), ink, w);
        dl->AddLine(Q(7, 3), Q(10, 0), ink, w);
        dl->AddLine(Q(3, 0), Q(10, 0), ink, w);
        dl->AddLine(Q(10, 0), Q(10, 7), ink, w);
        dl->AddLine(Q(7, 10), Q(10, 7), ink, w);
        break;
    case DevIcon::Material: // a shaded ball
        dl->AddCircleFilled(Q(5, 5), 4.5f * scale, paper);
        dl->AddCircle(Q(5, 5), 4.5f * scale, ink, 0, w * 1.4f);
        dl->AddCircleFilled(Q(3.6f, 3.6f), 1.3f * scale, ink);
        break;
    case DevIcon::Place: // a grid
        dl->AddRect(Q(0, 0), Q(10, 10), ink, 0.0f, 0, w);
        dl->AddLine(Q(5, 0), Q(5, 10), ink, w);
        dl->AddLine(Q(0, 5), Q(10, 5), ink, w);
        break;
    case DevIcon::Scene: // a clapperboard (library drawing)
        dl->AddRectFilled(Q(0, 3), Q(10, 10), paper);
        dl->AddRect(Q(0, 3), Q(10, 10), ink, 0.0f, 0, w);
        dl->AddRectFilled(Q(0, 0.5f), Q(10, 3), ink);
        dl->AddLine(Q(3, 0.5f), Q(2, 3), paper, w);
        dl->AddLine(Q(6.5f, 0.5f), Q(5.5f, 3), paper, w);
        break;
    case DevIcon::Object:
        dl->AddRectFilled(Q(1, 1), Q(9, 9), paper);
        dl->AddRect(Q(1, 1), Q(9, 9), ink, 0.0f, 0, w);
        break;
    }
}

} // namespace vdev
