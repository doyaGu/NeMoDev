#pragma once
#include "features/scene/SceneModel.h"
#include "core/document/Document.h"
#include <set>
namespace vdev {
SceneTree ReadLevelTree(Document &, const SceneTreeOptions &, const std::vector<SceneWorkset> &,
                        const std::set<CK_ID> &hidden);
SceneTree ReadHierarchyTree(Document &, const SceneTreeOptions &);
} // namespace vdev
