#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "CKAll.h"
#include "core/runtime/FileDuplicates.h"

// SDK helpers for the source-built Ballanced runtime.
namespace vdev {

inline CKInterfaceObjectManager *InterfaceObjectManagerOf(CKObject *object) {
    return object && CKIsChildClassOf(object, CKCID_INTERFACEOBJECTMANAGER)
               ? static_cast<CKInterfaceObjectManager *>(object) : nullptr;
}

CKSPRITETEXT_ALIGNMENT SpriteTextAlign(CKSpriteText *text);

// ------------------------------------------------------------- picking


// Rectangular picking returns object pointers owned by the CK context.
inline CKERROR RenderRectPick(CKRenderContext *rc, CKContext *context, const VxRect &rect, CKBOOL intersect,
                              std::vector<CKObject *> &objects) {
    XObjectPointerArray found;
    found.Reserve(context->GetObjectListByType(CKCID_RENDEROBJECT, TRUE).Size() + 256);
    const CKERROR error = rc->RectPick(rect, found, intersect);
    for (int i = 0; i < found.Size(); ++i)
        if (found[i]) objects.push_back(found[i]);
    return error;
}

// CKRenderContext::Pick as 2.1 has it: the 3D entity met (the face data in
// result), the 2D entity at the point in result->Sprite (CK2_3D 2.1
// RCKRenderContext::Pick: Pick3D, then _Pick2D).
inline CKRenderObject *RenderPick(CKRenderContext *rc, int x, int y, CKPICKRESULT *result,
                                  CKBOOL ignoreUnpickable = FALSE) {
    return rc->Pick(x, y, result, ignoreUnpickable);
}

// ------------------------------------------------------------- render drivers

inline const char *DriverName(const VxDriverDesc *desc) {
    return desc->DriverName;
}
inline const char *DriverDescription(const VxDriverDesc *desc) {
    return desc->DriverDesc;
}
inline int DisplayModeCount(const VxDriverDesc *desc) {
    return desc->DisplayModes ? desc->DisplayModeCount : 0;
}
inline const VxDisplayMode &DisplayModeAt(const VxDriverDesc *desc, int i) {
    return desc->DisplayModes[i];
}

// ------------------------------------------------------------- prototypes

// A prototype's input parameter description: 2.1 keeps an array of
// pointers, 2.5 an XClassArray of the descriptions.
inline const CKPARAMETER_DESC *PrototypeInParameter(CKBehaviorPrototype *prototype, int i) {
    if (!prototype || i < 0 || i >= prototype->GetInParameterCount()) return nullptr;
    return prototype->GetInParameterList()[i];
}

// ------------------------------------------------------------- managers

// CKGridManager's classification category (the 2.5 header spells it
// GetGridClassificationCatego; the vtable slot is the same).
inline int GridClassificationCategory(CKGridManager *manager) {
    return manager->GetGridClassificationCategory();
}

inline CKDWORD DebugCurrentAction(const CKDebugContext *debug) {
    return debug->CurrentBehaviorAction;
}

// ------------------------------------------------------------- behavior graphs

// ------------------------------------------------------------- contexts

// CKCreateContext as the SDK compiled with declares it (the 2.1 SDK has a
// render engine argument, VIRTOOLS_CKCREATECONTEXT_HAS_RENDER_ENGINE, which
// the library finds on the SDK's CK2 target; 2.5 a configuration file
// instead).
// renderEngine: the index of a render engine plugin (2.1).
CKERROR CreateCKContext(CKContext **context, void *window, int renderEngine = 0);

// ------------------------------------------------------------- loading files

// CKFile::Load with the Duplicate Names Found choices (none: CKFile::Load).
// Under CK_LOAD_CHECKDUPLICATES, CKContext::LoadVerifyObjectUnicity asks the
// load callback about each duplicate (CKLOAD_OK, CKLOAD_RENAME,
// CKLOAD_USECURRENT).
CKERROR LoadFileChoosingDuplicates(CKContext *context, CKFile *file, const std::string &path, CKObjectArray *objects,
                                   CK_LOAD_FLAGS flags, const std::vector<DuplicateName> *duplicates);

} // namespace vdev
