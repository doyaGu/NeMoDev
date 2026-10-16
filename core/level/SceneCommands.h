#pragma once

#include <string>
#include <vector>

#include "CKAll.h"
#include "core/document/EditResult.h"

namespace vdev {

class Document;

enum class InitialConditions {
    Set,     // Dev Shift+Q: CKScene::SetObjectInitialValue(object, CKSaveObjectState(object))
    Remove,  // Dev Ctrl+Q: no initial value in the current scene
    Restore, // Dev Alt+Q: CKReadObjectState(object, initial value)
};

// Initial conditions of Scripts or objects in the level's current scene, as
// one undoable command (scene data is saved with the composition). Objects
// that are not in the scene (or, for Remove/Restore, have no initial value)
// are skipped; the message names how many were changed.
EditResult ApplyInitialConditions(Document &document, const std::vector<CK_ID> &objects,
                                  InitialConditions action);

// Scene flags of objects or Scripts in a scene (Level Manager columns
// Activate At Scene Start, Reset At Scene Start): adds and removes
// CK_SCENEOBJECT_FLAGS bits, undoably. scene: 0 for the current scene (the
// level scene when none).
EditResult ModifySceneFlags(Document &document, const std::vector<CK_ID> &objects,
                            CKDWORD add, CKDWORD remove, const std::string &label, CK_ID scene = 0);

// The scene initial conditions apply to: the current scene, else the level scene.
CKScene *InitialConditionsScene(CKContext *context);

} // namespace vdev
