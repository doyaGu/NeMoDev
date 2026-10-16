#pragma once

#include <functional>
#include <map>
#include <set>
#include <vector>

#include "CKAll.h"

namespace vdev {

class Document;

// The 3D Layout's interface data in a composition (Dev.exe sub_4B4E50 writes
// it, sub_4B46C0 reads it): a chunk of the CKInterfaceObjectManager Dev
// saves for the 3D Layout view (GUID LAYOUT_VIEW_GUID; DoSaveObjects,
// sub_5CE470: the window's placement chunk first, then the view's own),
// version 2, identifier 0xF0F0AAAA: the selection group shown, the
// selection when there is none, Lock Selection; then the 3D Layout's lists
// (LayoutLists below), the camera and the Setups open.
const CKGUID LAYOUT_VIEW_GUID(0x332D44F2, 0x183A4AFC);
// LayoutInterfaceData::lists, in the order Dev writes them: the view's
// members +208, +204, +192, +196, +200 and +180.
//  +208 / +204: the lights / cameras whose cone shows;
//  +192 / +196 / +200: the cameras / lights / curves Player Mode shows;
//  +180: the curves whose control points the 3D Layout draws (sub_4A1E00
//   goes through it, nothing else draws them). Dev puts a curve there when
//   it enters a scene (sub_49DC50 message 60, from SequenceAddedToScene:
//   created, merged, put in a scene; not when a file is loaded), when the
//   curve point tools give it a point (sub_49ACC0) and when one of its
//   points is shown in Player Mode (sub_4A1670). Only the curve's deletion
//   takes it out (Check), not its removal from a scene; a new composition
//   empties it (sub_4ACD50). So it depends on what was done in Dev: a curve
//   that came with a file and was not listed there has no points drawn.
constexpr int LAYOUT_CONE_LIGHTS = 0, LAYOUT_CONE_CAMERAS = 1, LAYOUT_PLAYER_CAMERAS = 2, LAYOUT_PLAYER_LIGHTS = 3,
              LAYOUT_PLAYER_CURVES = 4, LAYOUT_DRAWN_CURVES = 5;
// The frames' flags (Dev.exe sub_4C53C0, sub_49DBB0): the representation
// (0 none, n the n-th Virtools_Frame_ shape), shown in Player Mode.
constexpr CKDWORD FRAME_REPRESENTATION = 0x2F, FRAME_PLAYER_SHOWN = 0x40;

struct LayoutInterfaceData {
    bool present = false; // read from the composition
    CK_ID group = 0;
    std::vector<CK_ID> selection;
    bool locked = false;
    // Kept: the object arrays in their order of writing, the objects with
    // flags (version 2) and the flags, the camera IDs, the Setups (GUID, two
    // objects, three ints), the arrays after 0xF0F0AAAC.
    std::vector<CK_ID> lists[6];
    std::vector<CK_ID> flagged;
    std::vector<CKDWORD> flags;
    std::vector<CK_ID> cameras;
    struct Setup {
        CKGUID guid;
        CK_ID object = 0, other = 0;
        int values[3] = {-1, -1, 0};
    };
    std::vector<Setup> setups;
    std::vector<CK_ID> last;
};

// The Setup windows of the data (Dev.exe sub_5CB9E0 opens them by GUID): the
// GUID of the Setup Dev shows for object (found in Dev's files: Camera,
// Light, Curve, 3D Frame, 3D Object, Texture, Mesh, Material, 3D Sprite, 2D
// Entity, Array, Scene, Level, Sound, Grid); false for the others.
bool LayoutSetupGuid(CKObject *object, CKGUID &guid);

// The data of the composition's 3D Layout interface object, if any.
LayoutInterfaceData ReadLayoutInterfaceData(CKContext *context);

// The 3D Layout's own lists, kept by the editor and saved in the data
// (Dev.exe sub_4B46C0 reads them, sub_4B4E50 writes them): the cameras and
// lights whose cone shows, what Player Mode shows although its type is not,
// each frame's representation (frames not listed: 1).
struct LayoutLists {
    std::set<CK_ID> cones;
    std::set<CK_ID> playerShown;
    std::map<CK_ID, int> representations;
    // The curves not in +180: their control points are not drawn.
    std::set<CK_ID> hiddenCurvePoints;
};

// The lists of data (+208 lights' and +204 cameras' cones; Player Mode's +192
// cameras, +196 lights, +200 curves, +188 2D entities and the frames flagged
// FRAME_PLAYER_SHOWN; the frames' table), the objects gone left out.
LayoutLists ReadLayoutLists(CKContext *context, const LayoutInterfaceData &data);

// Puts lists into data, each object in its class's list, and every frame
// of the composition (a 3D entity flagged CK_3DENTITY_FRAME) in the frames'
// table, its other bits kept; every curve in +180 (LAYOUT_DRAWN_CURVES),
// those already there first, so that Dev draws the curves made in the
// editor. live: the objects saved (not the 3D Layout's own, not deleted).
void WriteLayoutLists(CKContext *context, const LayoutLists &lists, const std::function<bool(CK_ID)> &live,
                      LayoutInterfaceData &data);

// Puts data in the 3D Layout interface object (made when there is none, with
// Dev's placement chunk, and saved with the composition), before a save.
void WriteLayoutInterfaceData(Document &document, const LayoutInterfaceData &data);

} // namespace vdev
