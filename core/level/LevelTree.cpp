#include "core/level/LevelTree.h"

#include <algorithm>
#include <cstring>

#include "core/document/Document.h"
#include "core/document/ScriptSession.h"

namespace vdev {

std::string LevelFolderName(CK_CLASSID cid) {
    switch (cid) {
    case CKCID_2DENTITY: return "2D Frames";
    case CKCID_SPRITE: return "2D Sprites";
    case CKCID_SPRITETEXT: return "Sprite Texts";
    case CKCID_MATERIAL: return "Materials";
    case CKCID_TEXTURE: return "Textures";
    case CKCID_3DENTITY: return "3D Frames";
    case CKCID_3DOBJECT: return "3D Objects";
    case CKCID_CAMERA:
    case CKCID_TARGETCAMERA: return "Cameras";
    case CKCID_LIGHT:
    case CKCID_TARGETLIGHT: return "Lights";
    case CKCID_CHARACTER: return "Characters";
    case CKCID_BODYPART: return "Body Parts";
    case CKCID_MESH: return "Meshes";
    case CKCID_PATCHMESH: return "Patch Meshes";
    case CKCID_CURVE: return "Curves";
    case CKCID_CURVEPOINT: return "Curve Points";
    case CKCID_GRID: return "Grids";
    case CKCID_GROUP: return "Groups";
    case CKCID_DATAARRAY: return "Arrays";
    case CKCID_WAVESOUND: return "Sounds";
    case CKCID_MIDISOUND: return "Midi Sounds";
    case CKCID_SPRITE3D: return "3D Sprites";
    case CKCID_ANIMATION:
    case CKCID_KEYEDANIMATION: return "Global Animations";
    default: {
        const char *name = CKClassIDToString(cid);
        return name ? std::string(name) + "s" : "Objects";
    }
    }
}

bool LevelNameLess(CKObject *a, CKObject *b) {
    const char *x = a && a->GetName() ? a->GetName() : "";
    const char *y = b && b->GetName() ? b->GetName() : "";
    return _stricmp(x, y) < 0;
}

SceneContents ListSceneContents(CKContext *context, CKScene *scene, CKLevel *level, bool showDynamic) {
    SceneContents result;
    if (!context || !scene) return result;
    for (CKSceneObjectIterator it = scene->GetObjectIterator(); !it.End(); it++) {
        CKSceneObject *object = CKSceneObject::Cast(context->GetObject(it.GetObjectID()));
        if (!object || object == level || object->IsPrivate() || (object->IsDynamic() && !showDynamic)) continue;
        const CK_CLASSID cid = object->GetClassID();
        if (cid == CKCID_LEVEL || cid == CKCID_SCENE || CKBehavior::Cast(object) || cid == CKCID_OBJECTANIMATION)
            continue;
        if (CKBodyPart *part = CKBodyPart::Cast(object))
            if (part->GetCharacter()) continue;
        if (CKAnimation *animation = CKAnimation::Cast(object))
            if (animation->GetCharacter()) continue;
        if (CKPlace *place = CKPlace::Cast(object)) {
            result.places.push_back(place);
            continue;
        }
        CKPlace *place = nullptr;
        for (CK3dEntity *up = CK3dEntity::Cast(object) ? CK3dEntity::Cast(object)->GetParent() : nullptr; up && !place;
             up = up->GetParent())
            place = CKPlace::Cast(up);
        if (place) result.placed[place->GetID()][LevelFolderName(cid)].push_back(object);
        else result.global[LevelFolderName(cid)].push_back(object);
    }
    std::sort(result.places.begin(), result.places.end(), LevelNameLess);
    return result;
}

std::vector<CK_ID> SortScripts(Document &document, std::vector<CK_ID> order, ScriptSort sort) {
    CKContext *context = document.Context();
    if (!context) return order;
    auto name = [&](CK_ID id) {
        CKObject *object = context->GetObject(id);
        return object && object->GetName() ? object->GetName() : "";
    };
    auto owner = [&](CK_ID id) -> CKObject * {
        CKBehavior *script = CKBehavior::Cast(context->GetObject(id));
        return script ? script->GetOwner() : nullptr;
    };
    auto color = [&](CK_ID id) {
        ScriptSession *session = document.Session(id);
        return session ? session->Data().header.color : 0u;
    };
    switch (sort) {
    case ScriptSort::Name:
        std::stable_sort(order.begin(), order.end(), [&](CK_ID a, CK_ID b) { return _stricmp(name(a), name(b)) < 0; });
        break;
    case ScriptSort::Owner:
        std::stable_sort(order.begin(), order.end(), [&](CK_ID a, CK_ID b) { return LevelNameLess(owner(a), owner(b)); });
        break;
    case ScriptSort::Color:
        std::stable_sort(order.begin(), order.end(), [&](CK_ID a, CK_ID b) { return color(a) < color(b); });
        break;
    case ScriptSort::ClassId:
        std::stable_sort(order.begin(), order.end(), [&](CK_ID a, CK_ID b) {
            CKObject *x = owner(a), *y = owner(b);
            return (x ? x->GetClassID() : 0) < (y ? y->GetClassID() : 0);
        });
        break;
    case ScriptSort::Invert:
        std::reverse(order.begin(), order.end());
        break;
    }
    return order;
}

} // namespace vdev
