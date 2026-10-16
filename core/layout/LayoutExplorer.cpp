#include "core/layout/LayoutExplorer.h"

#include <algorithm>

namespace vdev {

std::vector<CK_ID> SceneGroups(CKContext *context, CKScene *scene) {
    std::vector<CK_ID> groups;
    if (!context || !scene) return groups;
    CKObjectArray *array = CreateCKObjectArray();
    scene->ComputeObjectList(array, CKCID_GROUP, TRUE);
    for (array->Reset(); !array->EndOfList(); array->Next())
        if (CKObject *group = array->GetData(context)) groups.push_back(group->GetID());
    DeleteCKObjectArray(array);
    return groups;
}

std::vector<CK_ID> GroupSelection(CKContext *context, CK_ID id) {
    std::vector<CK_ID> objects;
    CKGroup *group = context ? CKGroup::Cast(context->GetObject(id)) : nullptr;
    if (!group) return objects;
    for (int i = 0; i < group->GetObjectCount(); ++i) {
        CKBeObject *object = group->GetObject(i);
        if (object && (CKIsChildClassOf(object, CKCID_3DENTITY) || CKIsChildClassOf(object, CKCID_2DENTITY)) &&
            std::find(objects.begin(), objects.end(), object->GetID()) == objects.end())
            objects.push_back(object->GetID());
    }
    return objects;
}

// Dev compares the counts, then looks each member up in the selection.
CK_ID MatchingGroup(CKContext *context, const std::vector<CK_ID> &groups, const std::vector<CK_ID> &selection) {
    if (!context || selection.empty()) return 0;
    for (CK_ID id : groups) {
        CKGroup *group = CKGroup::Cast(context->GetObject(id));
        if (!group || group->GetObjectCount() == 0 || group->GetObjectCount() != static_cast<int>(selection.size()))
            continue;
        bool same = true;
        for (int i = 0; i < group->GetObjectCount() && same; ++i) {
            CKBeObject *object = group->GetObject(i);
            same = object && std::find(selection.begin(), selection.end(), object->GetID()) != selection.end();
        }
        if (same) return id;
    }
    return 0;
}

namespace {

CKDWORD EntityFlags(CKObject *object) {
    CK3dEntity *entity = CK3dEntity::Cast(object);
    return entity ? entity->GetFlags() : 0;
}

} // namespace

unsigned ExplorerTypeOf(CKObject *object, unsigned shown) {
    if (!object || !CKIsChildClassOf(object, CKCID_BEOBJECT) || object->IsPrivate()) return 0;
    const CK_CLASSID cid = object->GetClassID();
    const CKDWORD flags = EntityFlags(object);
    const bool plain = cid == CKCID_3DENTITY;
    if ((shown & EXPLORER_LIGHTS) &&
        (CKIsChildClassOf(object, CKCID_LIGHT) || (plain && (flags & CK_3DENTITY_TARGETLIGHT))))
        return EXPLORER_LIGHTS;
    if ((shown & EXPLORER_CAMERAS) &&
        (CKIsChildClassOf(object, CKCID_CAMERA) || (plain && (flags & CK_3DENTITY_TARGETCAMERA))))
        return EXPLORER_CAMERAS;
    if ((shown & EXPLORER_3D_OBJECTS) &&
        (plain || CKIsChildClassOf(object, CKCID_3DOBJECT) || CKIsChildClassOf(object, CKCID_GRID)) &&
        !(flags & (CK_3DENTITY_FRAME | CK_3DENTITY_TARGETCAMERA | CK_3DENTITY_TARGETLIGHT)))
        return EXPLORER_3D_OBJECTS;
    if ((shown & EXPLORER_3D_SPRITES) && CKIsChildClassOf(object, CKCID_SPRITE3D)) return EXPLORER_3D_SPRITES;
    if ((shown & EXPLORER_2D_SPRITES) && CKIsChildClassOf(object, CKCID_2DENTITY)) return EXPLORER_2D_SPRITES;
    if ((shown & EXPLORER_CHARACTERS) && CKIsChildClassOf(object, CKCID_CHARACTER)) return EXPLORER_CHARACTERS;
    if ((shown & EXPLORER_FRAMES) && plain && (flags & CK_3DENTITY_FRAME)) return EXPLORER_FRAMES;
    if ((shown & EXPLORER_CURVES) && CKIsChildClassOf(object, CKCID_CURVE)) return EXPLORER_CURVES;
    if ((shown & EXPLORER_SCRIPTED) && static_cast<CKBeObject *>(object)->GetScriptCount() != 0)
        return EXPLORER_SCRIPTED;
    return 0;
}

std::vector<std::pair<CK_ID, unsigned>> ExplorerObjects(CKContext *context, CKScene *scene, unsigned shown) {
    std::vector<std::pair<CK_ID, unsigned>> objects;
    if (!context || !scene || !shown) return objects;
    CKObjectArray *array = CreateCKObjectArray();
    scene->ComputeObjectList(array, CKCID_BEOBJECT, TRUE);
    for (array->Reset(); !array->EndOfList(); array->Next()) {
        CKObject *object = array->GetData(context);
        if (const unsigned type = ExplorerTypeOf(object, shown)) objects.emplace_back(object->GetID(), type);
    }
    DeleteCKObjectArray(array);
    return objects;
}

// Dev.exe sub_59EC90: scripted entities, lights, cameras and 2D entities by
// class, then by exact class id (3D entities by their target and frame
// flags, 3D sprites, characters, 3D objects and grids, curves).
unsigned SelectedTypes(CKContext *context, const std::vector<CK_ID> &objects) {
    unsigned types = 0;
    for (CK_ID id : objects) {
        CKBeObject *object = context ? CKBeObject::Cast(context->GetObject(id)) : nullptr;
        if (!object) continue;
        const bool entity = CKIsChildClassOf(object, CKCID_2DENTITY) || CKIsChildClassOf(object, CKCID_3DENTITY);
        if (object->GetScriptCount() != 0 && entity) types |= EXPLORER_SCRIPTED;
        if (CKIsChildClassOf(object, CKCID_LIGHT)) types |= EXPLORER_LIGHTS;
        if (CKIsChildClassOf(object, CKCID_CAMERA)) types |= EXPLORER_CAMERAS;
        if (CKIsChildClassOf(object, CKCID_2DENTITY)) types |= EXPLORER_2D_SPRITES;
        const CKDWORD flags = EntityFlags(object);
        switch (object->GetClassID()) {
        case CKCID_3DENTITY:
            types |= (flags & CK_3DENTITY_TARGETLIGHT)    ? EXPLORER_LIGHTS
                     : (flags & CK_3DENTITY_TARGETCAMERA) ? EXPLORER_CAMERAS
                     : (flags & CK_3DENTITY_FRAME)        ? EXPLORER_FRAMES
                                                          : EXPLORER_3D_OBJECTS;
            break;
        case CKCID_SPRITE3D: types |= EXPLORER_3D_SPRITES; break;
        case CKCID_CHARACTER: types |= EXPLORER_CHARACTERS; break;
        case CKCID_3DOBJECT:
        case CKCID_GRID: types |= EXPLORER_3D_OBJECTS; break;
        case CKCID_CURVE: types |= EXPLORER_CURVES; break;
        default: break;
        }
    }
    return types;
}

} // namespace vdev
