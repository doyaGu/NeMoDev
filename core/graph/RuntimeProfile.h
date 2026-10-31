#pragma once

#include "CKAll.h"
#include "editor/Identity.h"
#include <string>
#include <vector>

namespace vdev {
class Document;

struct RuntimeProfileRow {
    std::string name;
    double time = 0;
    int parent = -1;
    ObjectRef object{}, script{};
    bool dynamic = false;
};

// A copied sample. No row retains a manager, behavior or render context.
struct RuntimeProfile {
    CKStats stats{};
    VxStats render{};
    int objects3d = 0, textures = 0, sprites = 0, behaviors = 0;
    uint64_t faces = 0, vertices = 0, videoTextures = 0, textureBytes = 0, videoSprites = 0, spriteBytes = 0;
    std::vector<RuntimeProfileRow> rows;
};

RuntimeProfile ReadRuntimeProfile(Document &, bool details);
void ReadRuntimeGeometry(Document &, uint64_t &faces, uint64_t &vertices);
} // namespace vdev
