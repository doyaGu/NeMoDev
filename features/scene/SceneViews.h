#pragma once
#include "application/EditorApplication.h"
#include "features/scene/SceneModel.h"
#ifdef VIRTOOLSDEV_UI_TESTS
#include "imgui.h"
#endif
namespace vdev {
bool RegisterSceneViews(EditorApplication &, std::string &error);
#ifdef VIRTOOLSDEV_UI_TESTS
struct SceneShownRow {
    uint32_t object = 0, cid = 0;
    std::string name;
    int depth = 0;
    bool children = false, open = false, selected = false;
    ImVec2 min{}, max{}, arrowMin{}, arrowMax{}, eyeMin{}, eyeMax{}, activateMin{}, activateMax{}, activeMin{},
        activeMax{}, priorityMin{}, priorityMax{};
};
std::vector<SceneShownRow> SceneRowsForTests(EditorView);
#endif
} // namespace vdev
