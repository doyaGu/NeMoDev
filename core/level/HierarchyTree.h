#pragma once

#include <functional>
#include <string>
#include <vector>

#include "CKAll.h"

namespace vdev {

// An item of Dev's Hierarchy Manager (Dev.exe sub_565180 fills the tree,
// sub_565520 adds an object and what it holds).
struct HierarchyItem {
    enum Root { NONE = -1, BACKGROUND_2D = 0, FOREGROUND_2D = 1, ROOT_3D = 2 };
    CK_ID object = 0;          // 0 for a root
    CK_CLASSID cid = 0;        // a root's: the 2D or 3D entity class (its icon)
    std::string name;          // ANSI
    int root = NONE;           // the root this item is
    bool resource = false;     // a mesh, material or texture (Dev's Show/Hide buttons)
    bool renderObject = false; // has a Render Priority (CKRenderObject::GetZOrder)
    std::vector<HierarchyItem> children;
};

// The resources Dev's toolbar shows (Show/Hide Meshes, Materials, Textures;
// materials only under meshes shown, textures under materials shown).
struct HierarchyOptions {
    bool meshes = false;
    bool materials = false;
    bool textures = false;
};

// Dev's three roots in its order ("2D Background Root", "2D Foreground
// Root", "3D Root": sorted by name). The 3D Root holds the 3D entities
// without a parent in the current scene, the 2D roots the 2D entities
// without a parent, background or not; an entity holds its children and,
// with the options, its meshes (a 3D entity), its material (a 2D entity, a
// 3D sprite), a mesh its materials, a material its texture. Private and
// dynamic objects and curve points are left out, as are objects skip
// returns true for (the graveyard). Each level is sorted by name,
// case-insensitive, the unnamed last (Dev.exe sub_5778A0).
std::vector<HierarchyItem> BuildHierarchy(CKContext *context, const HierarchyOptions &options,
                                          const std::function<bool(CK_ID)> &skip = std::function<bool(CK_ID)>());

// The object and every entity below it (Dev's "... Initial Conditions on
// Hierarchy"): 3D or 2D children, depth first.
std::vector<CK_ID> HierarchyOf(CKContext *context, CK_ID object);

} // namespace vdev
