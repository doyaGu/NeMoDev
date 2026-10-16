// The Create menu and the 3D Layout's creation tools, Replace Material, Duplicate.
#include "core/level/LevelCommands.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <set>

#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/GridCompat.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Document.h"
#include "core/graph/ScriptCommands.h"
#include "core/document/UndoHistory.h"
#include "core/level/detail/LevelCommandsDetail.h"

namespace vdev {

using namespace level;

// ---------------------------------------------------------------- Create (Dev 3D Layout creation tools)

std::string NewObjectName(CKContext *context, CK_CLASSID cid, const std::string &base, int options) {
    // Dev.exe sub_5D53B0: the largest number ending the names of the objects
    // of that class (of the current scene; option 1: frames only, 4: portals
    // only); none: base, else "base.%04d" with the next number. A name not
    // ending in digits counts as 0.
    CKLevel *level = context->GetCurrentLevel();
    if (!level) return base;
    // Dev always has a current scene; before any launch this editor uses the level scene.
    CKScene *scene = context->GetCurrentScene() ? context->GetCurrentScene() : level->GetLevelScene();
    if (!scene) return base;
    const XObjectPointerArray &list = (options & 2) ? level->ComputeObjectList(cid, TRUE) : scene->ComputeObjectList(cid, TRUE);
    int highest = -1;
    for (int i = 0; i < list.Size(); ++i) {
        CKObject *object = list[i];
        if (!object) continue;
        CK3dEntity *entity = CK3dEntity::Cast(object);
        if ((options & 1) && !(entity && (entity->GetFlags() & CK_3DENTITY_FRAME))) continue;
        if ((options & 4) && !(entity && (entity->GetFlags() & CK_3DENTITY_PORTAL))) continue;
        const char *name = object->GetName();
        if (!name || !*name) continue;
        size_t end = std::strlen(name);
        while (end > 0 && name[end - 1] >= '0' && name[end - 1] <= '9') --end;
        highest = std::max(highest, std::atoi(name + end));
    }
    if (highest + 1 == 0) return base;
    char text[512];
    std::snprintf(text, sizeof(text), "%s.%04d", base.c_str(), highest + 1);
    return text;
}

namespace {

// Dev places new objects relative to the 3D Layout's camera; without one the
// camera of reference is the current scene's starting camera, else the level
// scene's, else the first camera.
CKCamera *ReferenceCamera(CKContext *context, CK_ID view) {
    if (CKCamera *camera = CKCamera::Cast(context->GetObject(view))) return camera;
    if (CKScene *scene = context->GetCurrentScene())
        if (CKCamera *camera = scene->GetStartingCamera()) return camera;
    if (CKLevel *level = context->GetCurrentLevel())
        if (CKScene *scene = level->GetLevelScene())
            if (CKCamera *camera = scene->GetStartingCamera()) return camera;
    const XObjectPointerArray &cameras = context->GetObjectListByType(CKCID_CAMERA, TRUE);
    for (int i = 0; i < cameras.Size(); ++i)
        if (CKCamera *camera = CKCamera::Cast(cameras[i]))
            if (!camera->IsPrivate() && !camera->IsDynamic()) return camera;
    return nullptr;
}

// A point in front of the camera (Dev: front plane + distance in perspective,
// distance along the view direction in orthographic projection).
void InFront(CK3dEntity *entity, CKCamera *camera, float distance) {
    if (!camera) {
        VxVector origin(0.0f, 0.0f, 0.0f);
        entity->SetPosition(&origin);
        return;
    }
    if (camera->GetProjectionType() == CK_PERSPECTIVEPROJECTION) {
        VxVector ahead(0.0f, 0.0f, camera->GetFrontPlane() + distance);
        entity->SetPosition(&ahead, camera);
    } else {
        VxVector position, behind, view(0.0f, 0.0f, -1.0f), zero(0.0f, 0.0f, 0.0f);
        camera->Transform(&behind, &view);
        camera->Transform(&position, &zero);
        VxVector direction = behind - position;
        direction.Normalize();
        direction *= distance;
        entity->SetPosition(&direction);
    }
}

} // namespace

EditResult CreateLayoutObject(Document &document, LayoutObject kind, CK_ID selection,
                              CK_ID view) {
    CKContext *context = document.Context();
    CKLevel *level = context->GetCurrentLevel();
    CKScene *scene = context->GetCurrentScene();
    if (!level) return EditResult::Refused("No current level");
    if (!scene) scene = level->GetLevelScene();
    CKCamera *reference = ReferenceCamera(context, view);
    std::set<CK_ID> created;
    CKObject *object = nullptr;
    const char *label = "Create";
    auto make = [&](CK_CLASSID cid, const std::string &name) {
        CKObject *made = context->CreateObject(cid, const_cast<CKSTRING>(name.c_str()));
        if (made) created.insert(made->GetID());
        return made;
    };
    bool ok = GuardedCall([&] {
        switch (kind) {
        case LayoutObject::Camera: {
            // Dev.exe sub_4A9960: a target camera copying the view camera.
            label = "Create Camera";
            CKTargetCamera *camera = CKTargetCamera::Cast(make(CKCID_TARGETCAMERA, NewObjectName(context, CKCID_CAMERA, "New Camera", 0)));
            if (!camera) return;
            object = camera;
            if (reference) {
                camera->SetProjectionType(reference->GetProjectionType());
                camera->SetOrthographicZoom(reference->GetOrthographicZoom());
                int w = 4, h = 3;
                reference->GetAspectRatio(w, h);
                camera->SetAspectRatio(w, h);
                camera->SetFov(reference->GetFov());
                camera->SetFrontPlane(reference->GetFrontPlane());
                camera->SetBackPlane(reference->GetBackPlane());
                camera->SetWorldMatrix(reference->GetWorldMatrix());
                if (CK3dEntity *target = reference->GetTarget()) {
                    std::string targetName = std::string(camera->GetName()) + "_Target";
                    CK3dEntity *made = CK3dEntity::Cast(make(CKCID_3DENTITY, targetName));
                    if (made) {
                        made->ModifyObjectFlags(CK_OBJECT_INTERFACEOBJ, 0);
                        made->SetWorldMatrix(target->GetWorldMatrix());
                        camera->SetTarget(made);
                    }
                }
            }
            camera->ModifyObjectFlags(CK_OBJECT_INTERFACEOBJ, 0);
            break;
        }
        case LayoutObject::Light: {
            // sub_4A9E90: a target light 25 units in front of the camera, point, range 200.
            label = "Create Light";
            CKTargetLight *light = CKTargetLight::Cast(make(CKCID_TARGETLIGHT, NewObjectName(context, CKCID_LIGHT, "New Light", 0)));
            if (!light) return;
            object = light;
            if (reference) light->SetWorldMatrix(reference->GetWorldMatrix());
            InFront(light, reference, 25.0f);
            light->ModifyObjectFlags(CK_OBJECT_INTERFACEOBJ, 0);
            light->SetType(VX_LIGHTPOINT);
            light->SetRange(200.0f);
            break;
        }
        case LayoutObject::Frame3D: {
            // sub_4AAB50: a 3D entity with CK_3DENTITY_FRAME, 10 units in front.
            label = "Create 3D Frame";
            CK3dEntity *frame = CK3dEntity::Cast(make(CKCID_3DENTITY, NewObjectName(context, CKCID_3DENTITY, "New 3D Frame", 1)));
            if (!frame) return;
            object = frame;
            if (reference) frame->SetWorldMatrix(reference->GetWorldMatrix());
            InFront(frame, reference, 10.0f);
            frame->SetFlags(frame->GetFlags() | CK_3DENTITY_FRAME);
            frame->ModifyObjectFlags(CK_OBJECT_INTERFACEOBJ, 0);
            break;
        }
        case LayoutObject::Curve: {
            // sub_4AA200: an empty curve in front of the camera.
            label = "Create Curve";
            CKCurve *curve = CKCurve::Cast(make(CKCID_CURVE, NewObjectName(context, CKCID_CURVE, "New Curve", 0)));
            if (!curve) return;
            object = curve;
            InFront(curve, reference, 10.0f);
            break;
        }
        case LayoutObject::Grid: {
            // sub_4AB690: 10 x 10 squares of 30, height validity 10, the
            // "Grid" attribute, the default classification, one cleared layer.
            label = "Create Grid";
            CKGrid *grid = CKGrid::Cast(make(CKCID_GRID, NewObjectName(context, CKCID_GRID, "New Grid", 0)));
            if (!grid) return;
            object = grid;
            // Dev adds the grid to the level and the scene first.
            level->AddObject(grid);
            if (scene != level->GetLevelScene()) scene->AddObjectToScene(grid, TRUE);
            if (CKAttributeManager *attributes = context->GetAttributeManager()) {
                const CKAttributeType type = attributes->GetAttributeTypeByName((CKSTRING) "Grid");
                if (type >= 0) grid->SetAttribute(type);
            }
            grid->SetDimensions(10, 10, 30.0f, 30.0f);
            grid->SetHeightValidity(10.0f);
            InFront(grid, reference, 60.0f);
            VxVector position;
            grid->GetPosition(&position);
            position -= VxVector(15.0f, 5.0f, 15.0f);
            grid->SetPosition(&position);
            GridAddClassificationByName(grid, "- Default Grid Class -");
            if (CKLayer *layer = GridAddLayer(grid, 1))
                if (void *squares = layer->GetSquareArray())
                    std::memset(squares, 0, static_cast<size_t>(4 * grid->GetWidth() * grid->GetLength()));
            grid->ConstructMeshTexture(0.5f);
            break;
        }
        case LayoutObject::Frame2D: {
            // sub_4AA530: 80 x 60 at the middle of the render view (the
            // level's render context, else 640 x 480) less (20, 15).
            label = "Create 2D Frame";
            CK2dEntity *frame = CK2dEntity::Cast(make(CKCID_2DENTITY, NewObjectName(context, CKCID_2DENTITY, "New 2D Frame", 0)));
            if (!frame) return;
            object = frame;
            VxRect view(0.0f, 0.0f, 640.0f, 480.0f);
            if (CKRenderContext *rc = level->GetRenderContextCount() > 0 ? level->GetRenderContext(0) : nullptr) {
                VxRect rect;
                rc->GetViewRect(rect);
                if (rect.GetWidth() > 0.0f && rect.GetHeight() > 0.0f) view = rect;
            }
            frame->SetPosition(Vx2DVector((view.left + view.right) * 0.5f - 20.0f, (view.top + view.bottom) * 0.5f - 15.0f));
            frame->SetSize(Vx2DVector(80.0f, 60.0f));
            break;
        }
        case LayoutObject::Material:
            // sub_4AA810
            label = "Create Material";
            object = make(CKCID_MATERIAL, NewObjectName(context, CKCID_MATERIAL, "New Material", 0));
            break;
        case LayoutObject::Texture: {
            // sub_4AA9A0: 128 x 128, 32 bits.
            label = "Create Texture";
            CKTexture *texture = CKTexture::Cast(make(CKCID_TEXTURE, NewObjectName(context, CKCID_TEXTURE, "New Texture", 0)));
            if (!texture) return;
            object = texture;
            texture->Create(128, 128, 32, 0);
            break;
        }
        case LayoutObject::Portal: {
            // sub_4AAFD0: a 3D entity with CK_3DENTITY_PORTAL; around the
            // selected 3D entity ("<name> Portal"), else 10 units in front.
            label = "Create Portal";
            CK3dEntity *around = CK3dEntity::Cast(context->GetObject(selection));
            if (around && (CKPlace::Cast(around) || (around->GetFlags() & CK_3DENTITY_PORTAL))) around = nullptr;
            const std::string name = around ? std::string(around->GetName() ? around->GetName() : "") + " Portal"
                                            : NewObjectName(context, CKCID_3DENTITY, "New Portal", 4);
            CK3dEntity *portal = CK3dEntity::Cast(make(CKCID_3DENTITY, name));
            if (!portal) return;
            object = portal;
            if (reference) portal->SetWorldMatrix(reference->GetWorldMatrix());
            InFront(portal, reference, 10.0f);
            portal->SetFlags(portal->GetFlags() | CK_3DENTITY_PORTAL);
            portal->ModifyObjectFlags(CK_OBJECT_INTERFACEOBJ, 0);
            if (around) {
                const VxBbox &local = around->GetBoundingBox(TRUE);
                VxVector center = (local.Min + local.Max) * 0.5f, world;
                around->Transform(&world, &center);
                portal->SetPosition(&world);
                VxVector p, d, u;
                around->GetOrientation(&d, &u, &p);
                portal->SetOrientation(&d, &u, &p);
                VxVector scale;
                around->GetScale(&scale, TRUE);
                const VxBbox unit(VxVector(-1.0f, -1.0f, -1.0f), VxVector(1.0f, 1.0f, 1.0f));
                portal->SetBoundingBox(&unit, TRUE);
                VxVector half((local.Max.x - local.Min.x) * 0.5f * scale.x, (local.Max.y - local.Min.y) * 0.5f * scale.y,
                              (local.Max.z - local.Min.z) * 0.5f * scale.z);
                portal->SetScale(&half, FALSE, TRUE);
            }
            break;
        }
        }
    }, &document.Host(), "Create");
    if (!ok || !object) {
        for (CK_ID id : created) context->DestroyObject(id);
        return EditResult::Refused("The object could not be created");
    }
    const CK_ID id = object->GetID(), levelId = level->GetID(), sceneId = scene->GetID();
    const std::vector<CK_ID> added(created.begin(), created.end());
    const bool activate = kind == LayoutObject::Portal;
    return Apply(
        document, label,
        [added, levelId, sceneId, activate, id](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKScene *in = Get<CKScene>(doc, sceneId);
            if (!owner) return false;
            for (CK_ID each : added)
                if (CKSceneObject *made = Get<CKSceneObject>(doc, each)) {
                    if (!owner->GetLevelScene() || !owner->GetLevelScene()->IsObjectHere(made)) owner->AddObject(made);
                    if (in && in != owner->GetLevelScene() && !in->IsObjectHere(made)) in->AddObjectToScene(made, TRUE);
                }
            if (activate && in)
                if (CKSceneObject *made = Get<CKSceneObject>(doc, id)) in->Activate(made, TRUE);
            return true;
        },
        [added, levelId, sceneId](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKScene *in = Get<CKScene>(doc, sceneId);
            if (!owner) return false;
            for (CK_ID each : added)
                if (CKSceneObject *made = Get<CKSceneObject>(doc, each)) {
                    if (in && in != owner->GetLevelScene()) in->RemoveObjectFromScene(made, TRUE);
                    owner->RemoveObject(made);
                }
            return true;
        },
        created, id);
}

EditResult ReplaceMeshMaterial(Document &document, CK_ID meshId, CK_ID from, CK_ID to) {
    CKMesh *mesh = Get<CKMesh>(document, meshId);
    CKMaterial *before = Get<CKMaterial>(document, from), *after = Get<CKMaterial>(document, to);
    if (!mesh || !after) return EditResult::Refused("Drop a material on a face");
    if (before == after) return EditResult::Refused("The face has that material already");
    std::vector<int> faces, channels;
    for (int i = 0; i < mesh->GetFaceCount(); ++i)
        if (mesh->GetFaceMaterial(i) == before) faces.push_back(i);
    for (int i = 0; i < mesh->GetChannelCount(); ++i)
        if (mesh->GetChannelMaterial(i) == before) channels.push_back(i);
    if (faces.empty()) return EditResult::Refused("No face has the material");
    return Apply(
        document, "Replace Material",
        [meshId, from, to](Document &doc) {
            CKMesh *target = Get<CKMesh>(doc, meshId);
            CKMaterial *material = Get<CKMaterial>(doc, to);
            if (!target || !material) return false;
            target->ReplaceMaterial(Get<CKMaterial>(doc, from), material);
            return true;
        },
        [meshId, from, faces, channels](Document &doc) {
            CKMesh *target = Get<CKMesh>(doc, meshId);
            if (!target) return false;
            CKMaterial *material = Get<CKMaterial>(doc, from);
            for (int face : faces)
                if (face < target->GetFaceCount()) target->SetFaceMaterial(face, material);
            for (int channel : channels)
                if (channel < target->GetChannelCount()) target->SetChannelMaterial(channel, material);
            return true;
        });
}

EditResult DuplicateObjects(Document &document, const std::vector<CK_ID> &objects,
                            std::vector<std::pair<CK_ID, CK_ID>> *pairs, CKDependencies *dependencies) {
    CKContext *context = document.Context();
    CKLevel *level = context->GetCurrentLevel();
    if (!level || !level->GetLevelScene()) return EditResult::Refused("CKContext::CopyObjects needs a current level");
    XObjectArray originals;
    for (CK_ID id : objects) {
        CKObject *object = context->GetObject(id);
        if (!object || document.IsDiscarded(id) || CKCurvePoint::Cast(object) || CKLevel::Cast(object) ||
            CKScene::Cast(object))
            continue;
        originals.PushBack(id);
    }
    if (!originals.Size()) return EditResult::Refused("Duplicate: select objects first");
    // CopyObjects returns the top-level copies only: what it made is what is
    // new after it.
    auto everything = [context] {
        std::set<CK_ID> ids;
        const XObjectPointerArray &all = context->GetObjectListByType(CKCID_OBJECT, TRUE);
        for (int i = 0; i < all.Size(); ++i)
            if (all[i]) ids.insert(all[i]->GetID());
        return ids;
    };
    const std::set<CK_ID> before = everything();
    XObjectArray copies;
    const bool copied = GuardedCall(
        [&] {
            copies = context->CopyObjects(originals,
                                          dependencies ? dependencies : CKGetDefaultClassDependencies(CK_DEPENDENCIES_COPY),
                                          CK_OBJECTCREATION_NONAMECHECK, const_cast<CKSTRING>(".Copy"));
        },
        &document.Host(), "CKContext::CopyObjects");
    std::set<CK_ID> created;
    for (CK_ID id : everything())
        if (!before.count(id)) created.insert(id);
    if (!copied || copies.Size() != originals.Size()) {
        for (CK_ID id : created) context->DestroyObject(id);
        return EditResult::Refused("CKContext::CopyObjects failed");
    }
    CKScene *levelScene = level->GetLevelScene();
    GuardedCall(
        [&] {
            for (int i = 0; i < copies.Size(); ++i) {
                CKObject *original = context->GetObject(originals[i]);
                CKObject *copy = context->GetObject(copies[i]);
                // The copied Scripts' displays.
                CKBeObject *from = CKBeObject::Cast(original), *to = CKBeObject::Cast(copy);
                if (from && to && !CKBehavior::Cast(from))
                    for (int s = 0; s < from->GetScriptCount() && s < to->GetScriptCount(); ++s)
                        CopyScriptDisplay(document, from->GetScript(s), to->GetScript(s));
                // The level, and the scenes of the original.
                CKSceneObject *source = CKSceneObject::Cast(original), *made = CKSceneObject::Cast(copy);
                if (!source || !made) continue;
                if (!levelScene->IsObjectHere(made)) level->AddObject(made);
                for (int s = 0; s < source->GetSceneInCount(); ++s)
                    if (CKScene *scene = source->GetSceneIn(s))
                        if (scene != levelScene && !scene->IsObjectHere(made)) scene->AddObjectToScene(made, TRUE);
            }
        },
        &document.Host(), "Duplicate");
    // Where each new object is now, to take it out and put it back.
    struct Membership {
        CK_ID object;
        bool level;
        std::vector<CK_ID> scenes;
    };
    std::vector<Membership> memberships;
    for (CK_ID id : created) {
        CKSceneObject *made = CKSceneObject::Cast(context->GetObject(id));
        if (!made || CKLevel::Cast(made) || CKScene::Cast(made)) continue;
        Membership membership{id, levelScene->IsObjectHere(made) != FALSE, {}};
        for (int s = 0; s < made->GetSceneInCount(); ++s)
            if (CKScene *scene = made->GetSceneIn(s))
                if (scene != levelScene) membership.scenes.push_back(scene->GetID());
        memberships.push_back(std::move(membership));
    }
    if (pairs) {
        pairs->clear();
        for (int i = 0; i < copies.Size(); ++i) pairs->emplace_back(originals[i], copies[i]);
    }
    const CK_ID levelId = level->GetID();
    return Apply(
        document, "Duplicate",
        [memberships, levelId](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            if (!owner || !owner->GetLevelScene()) return false;
            for (const Membership &membership : memberships) {
                CKSceneObject *made = Get<CKSceneObject>(doc, membership.object);
                if (!made) continue;
                if (membership.level && !owner->GetLevelScene()->IsObjectHere(made)) owner->AddObject(made);
                for (CK_ID id : membership.scenes)
                    if (CKScene *scene = Get<CKScene>(doc, id))
                        if (!scene->IsObjectHere(made)) scene->AddObjectToScene(made, FALSE);
            }
            return true;
        },
        [memberships, levelId](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            if (!owner) return false;
            for (const Membership &membership : memberships) {
                CKSceneObject *made = Get<CKSceneObject>(doc, membership.object);
                if (!made) continue;
                for (CK_ID id : membership.scenes)
                    if (CKScene *scene = Get<CKScene>(doc, id)) scene->RemoveObjectFromScene(made, FALSE);
                if (membership.level) owner->RemoveObject(made);
            }
            return true;
        },
        created, copies[0]);
}

} // namespace vdev
