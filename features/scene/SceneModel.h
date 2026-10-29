#pragma once
#include "editor/Scene.h"
#include <string>
#include <vector>

namespace vdev {
struct SceneRow {
    uint64_t key = 0;
    uint32_t parent = 0; // index + 1; zero is a top-level row
    ObjectRef object{}, scene{}, section{}, group{}, script{};
    uint32_t kind = EDITOR_SCENE_OBJECT, types = 0, cells = 0, cid = 0;
    int depth = 0, priority = -1, renderPriority = 0, icon = 0, workset = 0, root = -1, saveOption = -1;
    bool expanded = false;
    std::string name, owner;
};
struct SceneTree {
    DocumentToken document{};
    ObjectRef level{}, levelScene{}, currentScene{};
    std::vector<SceneRow> rows;
};
struct SceneTreeOptions {
    bool priority = false, reversed = false, dynamic = false;
    bool meshes = false, materials = false, textures = false;
    ObjectRef context{};
};
struct SceneWorkset {
    uint64_t id = 0, parent = 0;
    std::string name;
    std::vector<ObjectRef> members;
};
} // namespace vdev
