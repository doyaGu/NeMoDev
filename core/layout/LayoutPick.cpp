// The 3D Layout's picking: objects and icons under a point or in a rectangle,
// rays from the view, points on planes and curves.
#include "core/layout/LayoutView.h"
#include "core/layout/ViewProjection.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/runtime/CkRuntimeHost.h"


namespace vdev {

CK_ID LayoutView::Pick(int x, int y, bool ignoreUnpickable, VxVector *point) const {
    CKObject *object = PickObject(x, y, ignoreUnpickable, point, nullptr);
    if (CKBodyPart *part = CKBodyPart::Cast(object))
        if (CKCharacter *character = part->GetCharacter()) return character->GetID();
    return object ? object->GetID() : 0;
}

LayoutPickInfo LayoutView::PickInfo(int x, int y) const {
    LayoutPickInfo info;
    CKPICKRESULT desc = {};
    CKObject *object = PickObject(x, y, false, nullptr, &desc);
    auto materialOf = [&info](CKMaterial *material) {
        if (!material) return;
        info.material = material->GetID();
        if (CKTexture *texture = material->GetTexture()) info.texture = texture->GetID();
    };
    if (CK2dEntity *sprite = CK2dEntity::Cast(object)) {
        info.entity2d = sprite->GetID();
        materialOf(sprite->GetMaterial());
        return info;
    }
    CK3dEntity *entity = CK3dEntity::Cast(object);
    if (!entity) return info;
    info.entity3d = entity->GetID();
    if (CKBodyPart *part = CKBodyPart::Cast(entity))
        if (CKCharacter *character = part->GetCharacter()) info.character = character->GetID();
    if (CKSprite3D *sprite = CKSprite3D::Cast(entity)) {
        materialOf(sprite->GetMaterial());
    } else if (CKMesh *mesh = entity->GetCurrentMesh()) {
        info.mesh = mesh->GetID();
        // The face met (an icon's owner was not met by CK's pick).
        if (desc.FaceIndex >= 0 && desc.FaceIndex < mesh->GetFaceCount()) {
            materialOf(mesh->GetFaceMaterial(desc.FaceIndex));
            info.face = desc.FaceIndex;
            info.point = desc.IntersectionPoint;
            info.u = desc.TexU;
            info.v = desc.TexV;
        }
    }
    return info;
}

CK_ID LayoutPickObject(CKContext *context, const LayoutPickInfo &info, CK_CLASSID cid, CK_ID camera) {
    if (!context) return 0;
    CKObject *entity = context->GetObject(info.entity3d);
    switch (cid) {
    case CKCID_OBJECT:
    case CKCID_BEOBJECT:
    case CKCID_3DENTITY:
    case CKCID_SPRITE3D:
    case CKCID_LIGHT:
    case CKCID_TARGETLIGHT:
    case CKCID_3DOBJECT:
    case CKCID_BODYPART: return entity ? info.entity3d : 0;
    case CKCID_SCENE: {
        CKScene *scene = context->GetCurrentScene();
        return scene ? scene->GetID() : 0;
    }
    case CKCID_LEVEL: {
        CKLevel *level = context->GetCurrentLevel();
        return level ? level->GetID() : 0;
    }
    case CKCID_PLACE: {
        CK3dEntity *placed = CK3dEntity::Cast(entity);
        CKPlace *place = placed ? placed->GetReferencePlace() : nullptr;
        return place ? place->GetID() : 0;
    }
    case CKCID_2DENTITY:
    case CKCID_SPRITE:
    case CKCID_SPRITETEXT: return context->GetObject(info.entity2d) ? info.entity2d : 0;
    case CKCID_MATERIAL: return context->GetObject(info.material) ? info.material : 0;
    case CKCID_TEXTURE: return context->GetObject(info.texture) ? info.texture : 0;
    case CKCID_MESH:
    case CKCID_PATCHMESH: return context->GetObject(info.mesh) ? info.mesh : 0;
    case CKCID_CAMERA:
    case CKCID_TARGETCAMERA: {
        if (entity) return info.entity3d;
        CKObject *shown = context->GetObject(camera);
        return shown && CKIsChildClassOf(shown, cid) ? camera : 0;
    }
    case CKCID_CHARACTER: return context->GetObject(info.character) ? info.character : 0;
    case CKCID_CURVE: {
        if (CKCurvePoint *point = CKCurvePoint::Cast(entity))
            if (CKCurve *curve = point->GetCurve()) return curve->GetID();
        return entity ? info.entity3d : 0;
    }
    default: return 0;
    }
}

CK_ID LayoutSendClick(CKContext *context, const LayoutPickInfo &info, bool doubleClick) {
    CKMessageManager *messages = context ? context->GetMessageManager() : nullptr;
    if (!messages) return 0;
    CKBeObject *target = CKBeObject::Cast(context->GetObject(info.entity3d));
    if (!target) target = CKBeObject::Cast(context->GetObject(info.entity2d));
    if (!target) return 0;
    const CKMessageType type = messages->AddMessageType((CKSTRING) (doubleClick ? "OnDblClick" : "OnClick"));
    return messages->SendMessageSingle(type, target, nullptr) ? target->GetID() : 0;
}

CKObject *LayoutView::PickObject(int x, int y, bool ignoreUnpickable, VxVector *point, CKPICKRESULT *picked) const {
    CKPICKRESULT desc = {};
    desc.FaceIndex = -1;
    CKRenderObject *object = nullptr;
    // CK picks the geometry with a render context; the icons need none.
    if (CKRenderContext *rc = RenderContext())
        GuardedCall([&] { object = RenderPick(rc, x, y, &desc, ignoreUnpickable ? TRUE : FALSE); }, &m_Host,
                    "CKRenderContext::Pick");
    if (picked) *picked = desc;
    // A 2D entity in front wins over the 3D entity behind (a background one does not).
    CK2dEntity *sprite = CK2dEntity::Cast(m_Host.Context()->GetObject(desc.Sprite));
    CK3dEntity *entity = CK3dEntity::Cast(object);
    if (sprite && (!entity || !sprite->IsBackground()) && !IsInterfaceObject(sprite->GetID())) return sprite;
    if (entity && IsInterfaceObject(entity->GetID())) entity = nullptr;
    VxVector origin, direction, hit;
    const bool ray = ViewRay(x, y, origin, direction);
    float nearest = 1.0e30f;
    if (entity) {
        entity->Transform(&hit, &desc.IntersectionPoint);
        if (ray) nearest = Magnitude(hit - origin);
    }
    // The icons under the point (Dev's records), met by the ray, may be nearer.
    CK3dEntity *iconEntity = CK3dEntity::Cast(m_Host.Context()->GetObject(m_IconEntity));
    CK_ID iconOwner = 0;
    for (const IconHit &icon : PlaceIconsNow()) {
        if (!ray || !iconEntity || x < icon.rect.left || x > icon.rect.right || y < icon.rect.top || y > icon.rect.bottom)
            continue;
        CKMesh *mesh = CKMesh::Cast(m_Host.Context()->GetObject(icon.mesh));
        if (!mesh) continue;
        iconEntity->SetCurrentMesh(mesh, FALSE);
        iconEntity->SetWorldMatrix(icon.world);
        const VxVector end = origin + direction;
        VxIntersectionDesc met = {};
        if (!iconEntity->RayIntersection(&origin, &end, &met, nullptr)) continue;
        VxVector at;
        iconEntity->Transform(&at, &met.IntersectionPoint);
        const float distance = Magnitude(at - origin);
        if (distance < nearest) {
            nearest = distance;
            iconOwner = icon.owner;
            hit = at;
        }
    }
    if (iconOwner) {
        if (point) *point = hit;
        // The face met is not the owner's (CK's pick did not meet it).
        if (picked) picked->FaceIndex = -1;
        return m_Host.Context()->GetObject(iconOwner);
    }
    if (!entity) return nullptr;
    if (point) *point = hit;
    return entity;
}

// The ray under a view point (Dev.exe sub_48EED0): from the camera through
// its front plane (perspective), along the view (orthographic); y is over
// the view's width, the field of view being horizontal.
bool LayoutView::ViewRay(int x, int y, VxVector &origin, VxVector &direction) const {
    ViewProjection projection;
    return Projection(projection) &&
           projection.Ray(static_cast<float>(x), static_cast<float>(y), origin, direction);
}

std::vector<CK_ID> LayoutView::RectPick(const VxRect &rect, bool intersect) const {
    std::vector<CK_ID> found;
    VxRect r = rect;
    if (r.left > r.right) std::swap(r.left, r.right);
    if (r.top > r.bottom) std::swap(r.top, r.bottom);
    CKContext *context = m_Host.Context();
    std::vector<CKObject *> objects;
    // CK picks the geometry with a render context; the icons need none.
    if (CKRenderContext *rc = RenderContext())
        GuardedCall([&] { RenderRectPick(rc, context, r, intersect ? TRUE : FALSE, objects); }, &m_Host,
                    "CKRenderContext::RectPick");
    for (CKObject *object : objects) {
        if (IsInterfaceObject(object->GetID())) continue;
        if (CKBodyPart *part = CKBodyPart::Cast(object)) object = part->GetCharacter();
        if (!CKRenderObject::Cast(object)) continue;
        if (std::find(found.begin(), found.end(), object->GetID()) == found.end()) found.push_back(object->GetID());
    }
    // The icons' screen boxes (Dev.exe sub_49EE70), in it or touching it.
    for (const IconHit &icon : PlaceIconsNow()) {
        const VxRect &b = icon.rect;
        const bool inside = b.left >= r.left && b.right <= r.right && b.top >= r.top && b.bottom <= r.bottom;
        const bool touching = b.left <= r.right && b.right >= r.left && b.top <= r.bottom && b.bottom >= r.top;
        if ((intersect ? touching : inside) && std::find(found.begin(), found.end(), icon.owner) == found.end())
            found.push_back(icon.owner);
    }
    return found;
}

// The reference grid (Dev.exe sub_4AF580, sub_4AF3E0): Sections + 1 lines
// each way, Step apart, centered on the origin of the guide's plane (local
// XZ), in the guide color.
bool LayoutView::RayOnPlane(int x, int y, const VxVector &point, const VxVector &normal, VxVector &hit) const {
    VxVector origin, direction;
    if (!ViewRay(x, y, origin, direction)) return false;
    const float facing = DotProduct(direction, normal);
    if (std::fabs(facing) < 1.0e-6f) return false;
    const float t = DotProduct(point - origin, normal) / facing;
    if (t < 0.0f) return false;
    hit = origin + direction * t;
    return true;
}

bool LayoutView::CurveAt(int x, int y, CK_ID curveId, VxVector &hit, float *along) const {
    CKRenderContext *rc = RenderContext();
    CKCurve *curve = CKCurve::Cast(m_Host.Context() ? m_Host.Context()->GetObject(curveId) : nullptr);
    if (!rc || !curve || curve->GetControlPointCount() < 2) return false;
    const float step = 0.1f / static_cast<float>(std::max(1, curve->GetStepCount()));
    float nearest = 6.0f * 6.0f;
    bool found = false;
    for (float t = 0.0f; t < 1.0f; t += step) {
        VxVector at, screen;
        if (curve->GetPos(t, &at) != CK_OK) continue;
        ScreenOfWorldPoint(rc, &screen, at);
        if (screen.z <= 0.0f || screen.z >= 1.0f) continue;
        const float dx = screen.x - x, dy = screen.y - y, distance = dx * dx + dy * dy;
        if (distance < nearest) {
            nearest = distance;
            hit = at;
            if (along) *along = t;
            found = true;
        }
    }
    return found;
}

} // namespace vdev
