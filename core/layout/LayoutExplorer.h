#pragma once

#include <utility>
#include <vector>

#include "CKAll.h"

namespace vdev {

// The 3D Layout's Selection Groups (Dev.exe sub_4C3D10, the top bar's combo,
// id 18): the groups of the scene, in the order the scene lists them.
std::vector<CK_ID> SceneGroups(CKContext *context, CKScene *scene);
// What choosing a group selects (Dev.exe sub_4A9740): its 3D and 2D entities.
std::vector<CK_ID> GroupSelection(CKContext *context, CK_ID group);
// The first of groups holding exactly the objects selected (sub_4C3D10, a
// selection change), else 0.
CK_ID MatchingGroup(CKContext *context, const std::vector<CK_ID> &groups, const std::vector<CK_ID> &selection);

// The 3D Layout Explorer's types (Dev.exe [Filter Box], the "Show in list"
// boxes; the bits of its filter).
enum ExplorerType : unsigned {
    EXPLORER_LIGHTS = 0x1,
    EXPLORER_CAMERAS = 0x2,
    EXPLORER_2D_SPRITES = 0x4,
    EXPLORER_3D_OBJECTS = 0x8,
    EXPLORER_CHARACTERS = 0x10,
    EXPLORER_FRAMES = 0x20,
    EXPLORER_CURVES = 0x40,
    EXPLORER_SCRIPTED = 0x80,
    EXPLORER_3D_SPRITES = 0x100,
    EXPLORER_ALL = 0x1FF,
};

// The type an object is listed under with the types shown (Dev.exe
// sub_59EFF0: the first that applies, in Dev's order: lights and their
// targets, cameras and theirs, 3D objects, grids and plain 3D entities, 3D
// sprites, 2D entities, characters, frames, curves, then any scripted
// object); 0: not listed. Private objects are never listed.
unsigned ExplorerTypeOf(CKObject *object, unsigned shown);
// The scene's behavioral objects listed with the types shown, and the type
// of each (scene order; Dev sorts them by name).
std::vector<std::pair<CK_ID, unsigned>> ExplorerObjects(CKContext *context, CKScene *scene, unsigned shown);
// The types of objects (Dev.exe sub_59EC90, the listed objects selected): a
// type's box does not change while one of them is selected.
unsigned SelectedTypes(CKContext *context, const std::vector<CK_ID> &objects);

} // namespace vdev
