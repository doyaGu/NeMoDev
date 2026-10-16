// The 3D Layout's overlay (PostRender / PostSpriteRender, Dev.exe sub_4A1E00 and
// sub_4A6DD0): icons, selection, the reference and screen grids, the world axis.
#include "core/layout/LayoutView.h"
#include "core/layout/ViewProjection.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/runtime/CkRuntimeHost.h"


namespace vdev {

// ------------------------------------------------------------------ drawing

void LayoutView::PostRender(CKRenderContext *rc, void *argument) {
    auto *view = static_cast<LayoutView *>(argument);
    if (!view || !rc->GetAttachedCamera()) return;
    // Dev.exe sub_4A1E00: the interface is drawn with the projection pushed
    // slightly back (z * 1.005) so lines do not fight the geometry, z test
    // less-or-equal, no blending, fog, specular or texture.
    VxMatrix projection = rc->GetProjectionTransformationMatrix();
    const VxMatrix saved = projection;
    projection[3][2] *= 1.005f;
    rc->SetProjectionTransformationMatrix(projection);
    const CKDWORD zfunc = rc->GetState(VXRENDERSTATE_ZFUNC), shade = rc->GetState(VXRENDERSTATE_SHADEMODE),
                  blend = rc->GetState(VXRENDERSTATE_ALPHABLENDENABLE),
                  specular = rc->GetState(VXRENDERSTATE_SPECULARENABLE), fog = rc->GetState(VXRENDERSTATE_FOGENABLE);
    rc->SetState(VXRENDERSTATE_ZFUNC, VXCMP_LESSEQUAL);
    rc->SetState(VXRENDERSTATE_SHADEMODE, 2);
    rc->SetState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
    rc->SetState(VXRENDERSTATE_SPECULARENABLE, FALSE);
    rc->SetState(VXRENDERSTATE_FOGENABLE, FALSE);
    rc->SetTexture(nullptr);
    if (view->m_Display.showGrid) view->DrawReferenceGrid(rc);
    view->DrawIcons(rc);
    view->DrawSelection(rc);
    if (view->m_Display.showScreenGrid) view->DrawScreenGrid(rc);
    view->ComputeWorldAxis(rc);
    rc->SetProjectionTransformationMatrix(saved);
    rc->SetState(VXRENDERSTATE_ZFUNC, zfunc);
    rc->SetState(VXRENDERSTATE_SHADEMODE, shade);
    rc->SetState(VXRENDERSTATE_ALPHABLENDENABLE, blend);
    rc->SetState(VXRENDERSTATE_SPECULARENABLE, specular);
    rc->SetState(VXRENDERSTATE_FOGENABLE, fog);
}

void LayoutView::PostSpriteRender(CKRenderContext *rc, void *argument) {
    auto *view = static_cast<LayoutView *>(argument);
    if (!view || !rc->GetAttachedCamera()) return;
    view->DrawSelection2D(rc);
    if (view->m_Display.showWorldAxis) view->DrawWorldAxis(rc);
}

namespace {

// Lines in the current world matrix, vertex colors given.
void DrawLines(CKRenderContext *rc, const VxVector *points, const CKDWORD *colors, int count) {
    VxDrawPrimitiveData *data = rc->GetDrawPrimitiveStructure(CKRST_DP_TR_CL_VC, count);
    if (!data) return;
    auto *positions = static_cast<unsigned char *>(data->PositionPtr);
    auto *vertexColors = static_cast<unsigned char *>(data->ColorPtr);
    std::vector<CKWORD> indices(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        *reinterpret_cast<VxVector *>(positions + i * data->PositionStride) = points[i];
        *reinterpret_cast<CKDWORD *>(vertexColors + i * data->ColorStride) = colors[i];
        indices[i] = static_cast<CKWORD>(i);
    }
    rc->DrawPrimitive(VX_LINELIST, indices.data(), count, data);
}

// A box's twelve edges (Dev's index list: 0-1-2-3, 4-5-6-7 and the uprights).
void DrawBox(CKRenderContext *rc, const VxMatrix &world, const VxBbox &box, CKDWORD color) {
    rc->SetWorldTransformationMatrix(world);
    const VxVector &a = box.Min, &b = box.Max;
    const VxVector corners[8] = {VxVector(b.x, b.y, b.z), VxVector(a.x, b.y, b.z), VxVector(a.x, a.y, b.z),
                                 VxVector(b.x, a.y, b.z), VxVector(b.x, b.y, a.z), VxVector(a.x, b.y, a.z),
                                 VxVector(a.x, a.y, a.z), VxVector(b.x, a.y, a.z)};
    static const int edges[24] = {0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7};
    VxVector points[24];
    CKDWORD colors[24];
    for (int i = 0; i < 24; ++i) {
        points[i] = corners[edges[i]];
        colors[i] = color;
    }
    DrawLines(rc, points, colors, 24);
}

} // namespace

// An icon mesh drawn with its matrix by the view's temporary entity (Dev
// lends the mesh to the object itself).
void LayoutView::RenderIcon(CKRenderContext *rc, const IconHit &icon) {
    CKContext *context = m_Host.Context();
    CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(m_IconEntity));
    CKMesh *mesh = CKMesh::Cast(context->GetObject(icon.mesh));
    if (!entity || !mesh) return;
    bool listed = false;
    for (int i = 0; i < entity->GetMeshCount(); ++i) listed = listed || entity->GetMesh(i) == mesh;
    if (!listed) entity->AddMesh(mesh);
    entity->SetCurrentMesh(mesh, FALSE);
    entity->SetWorldMatrix(icon.world);
    GuardedCall([&] { entity->Render(rc); }, &m_Host, "CK3dEntity::Render (icon)");
    rc->SetTexture(nullptr);
}

bool LayoutView::IconShown(CKObject *object, CKScene *scene) const {
    CKSceneObject *sceneObject = CKSceneObject::Cast(object);
    return sceneObject && !object->IsPrivate() && !IsInterfaceObject(object->GetID()) && object->IsVisible() &&
           sceneObject->IsInScene(scene);
}

bool LayoutView::IconListed(bool typeShown, CK_ID id) const {
    return typeShown || std::find(m_PlayerShown.begin(), m_PlayerShown.end(), id) != m_PlayerShown.end();
}

// Constant Size On Screen scales an icon with its distance to the camera
// (ViewProjection::ConstantScale). Its screen box: its mesh's box corners in
// front of the camera (not CK3dEntity::GetRenderExtents: one entity renders
// every icon of a frame, and its extents come back empty once it drew more
// than one).
std::vector<LayoutView::IconHit> LayoutView::PlaceIcons(const ViewProjection &projection, CK_ID through) const {
    std::vector<IconHit> icons;
    CKContext *context = m_Host.Context();
    CKScene *scene = Scene();
    if (!context || !scene) return icons;
    auto icon = [&](const char *name) -> CK_ID {
        auto found = m_Icons.find(name);
        return found == m_Icons.end() ? 0 : found->second;
    };
    auto place = [&](CK_ID owner, CK_ID meshId, VxMatrix world, bool constant, float zoom) {
        CKMesh *mesh = CKMesh::Cast(context->GetObject(meshId));
        if (!mesh) return;
        if (constant) {
            const float scale = projection.ConstantScale(VxVector(world[3][0], world[3][1], world[3][2]), zoom);
            for (int i = 0; i < 3; ++i) {
                VxVector axis(world[i][0], world[i][1], world[i][2]);
                if (SquareMagnitude(axis) > 0.0f) axis.Normalize();
                axis *= scale;
                world[i] = VxVector4(axis.x, axis.y, axis.z, 0.0f);
            }
        }
        IconHit hit;
        hit.owner = owner;
        hit.mesh = meshId;
        hit.world = world;
        hit.rect = VxRect(1e10f, 1e10f, -1e10f, -1e10f);
        const VxBbox &box = mesh->GetLocalBox();
        for (int corner = 0; corner < 8; ++corner) {
            const VxVector local((corner & 1) ? box.Max.x : box.Min.x, (corner & 2) ? box.Max.y : box.Min.y,
                                 (corner & 4) ? box.Max.z : box.Min.z);
            VxVector point;
            Vx3DMultiplyMatrixVector(&point, world, &local);
            const VxVector screen = projection.Project(point);
            if (screen.z <= 0.0f || screen.z >= 1.0f) continue;
            hit.rect.left = std::min(hit.rect.left, screen.x);
            hit.rect.top = std::min(hit.rect.top, screen.y);
            hit.rect.right = std::max(hit.rect.right, screen.x);
            hit.rect.bottom = std::max(hit.rect.bottom, screen.y);
        }
        icons.push_back(hit);
    };
    const bool listed = !m_PlayerShown.empty();
    std::vector<std::pair<CK3dEntity *, CK_ID>> targets;
    if (m_Display.showCameras || listed) {
        const XObjectPointerArray &cameras = context->GetObjectListByType(CKCID_CAMERA, TRUE);
        for (int i = 0; i < cameras.Size(); ++i) {
            CKCamera *camera = CKCamera::Cast(cameras[i]);
            if (!camera || camera->GetID() == through || !IconShown(camera, scene) ||
                !IconListed(m_Display.showCameras, camera->GetID()))
                continue;
            if (CK3dEntity *target = CKTargetCamera::Cast(camera) ? CKTargetCamera::Cast(camera)->GetTarget() : nullptr)
                targets.emplace_back(target, icon("CameraTarget"));
            if (!camera->GetCurrentMesh())
                place(camera->GetID(), icon("Camera"), camera->GetWorldMatrix(), m_Display.constantCamera,
                      m_Display.cameraZoom);
        }
    }
    if (m_Display.showLights || listed) {
        const XObjectPointerArray &lights = context->GetObjectListByType(CKCID_LIGHT, TRUE);
        for (int i = 0; i < lights.Size(); ++i) {
            CKLight *light = CKLight::Cast(lights[i]);
            if (!light || !IconShown(light, scene) || !IconListed(m_Display.showLights, light->GetID())) continue;
            if (CK3dEntity *target = CKTargetLight::Cast(light) ? CKTargetLight::Cast(light)->GetTarget() : nullptr)
                targets.emplace_back(target, icon("LightTarget"));
            const char *meshes[5] = {"Point", "Point", "Spot", "Parallel", "Directional"};
            if (!light->GetCurrentMesh())
                place(light->GetID(), icon(meshes[std::clamp(static_cast<int>(light->GetType()), 0, 4)]), light->GetWorldMatrix(),
                      m_Display.constantLight, m_Display.lightZoom);
        }
    }
    // Targets of the cameras and lights shown.
    for (const auto &target : targets)
        if (IconShown(target.first, scene) && !target.first->GetCurrentMesh())
            place(target.first->GetID(), target.second, target.first->GetWorldMatrix(), m_Display.constantFrame,
                  m_Display.frameZoom);
    // The 3D Frames (Dev's table of the entities flagged CK_3DENTITY_FRAME),
    // each with its representation (1, the first shape, unless changed).
    if ((m_Display.showFrames || listed) && !m_FrameShapes.empty()) {
        const XObjectPointerArray &entities = context->GetObjectListByType(CKCID_3DENTITY, TRUE);
        for (int i = 0; i < entities.Size(); ++i) {
            CK3dEntity *entity = CK3dEntity::Cast(entities[i]);
            if (!entity || !(entity->GetFlags() & CK_3DENTITY_FRAME) || !IconShown(entity, scene) ||
                !IconListed(m_Display.showFrames, entity->GetID()))
                continue;
            auto found = m_Representations.find(entity->GetID());
            const int representation = found != m_Representations.end() ? found->second : 1;
            if (representation < 1 || representation > static_cast<int>(m_FrameShapes.size())) continue;
            place(entity->GetID(), m_FrameShapes[representation - 1], entity->GetWorldMatrix(), m_Display.constantFrame,
                  m_Display.frameZoom);
        }
    }
    if (m_Display.showCurves || listed) {
        auto selected = [&](CK_ID id) { return std::find(m_Selection.begin(), m_Selection.end(), id) != m_Selection.end(); };
        const XObjectPointerArray &curves = context->GetObjectListByType(CKCID_CURVE, TRUE);
        for (int i = 0; i < curves.Size(); ++i) {
            CKCurve *curve = CKCurve::Cast(curves[i]);
            if (!curve || !IconShown(curve, scene) || !IconListed(m_Display.showCurves, curve->GetID()) ||
                m_HiddenCurvePoints.count(curve->GetID()))
                continue;
            for (int p = 0; p < curve->GetControlPointCount(); ++p)
                if (CKCurvePoint *point = curve->GetControlPoint(p))
                    if (!point->GetCurrentMesh())
                        place(point->GetID(), icon(selected(point->GetID()) ? "CurrentCurvePoint" : "CurvePoint"),
                              point->GetWorldMatrix(), m_Display.constantCurvePoint, m_Display.curvePointZoom);
        }
    }
    if (m_Display.showPortals || listed) {
        const XObjectPointerArray &entities = context->GetObjectListByType(CKCID_3DENTITY, TRUE);
        for (int i = 0; i < entities.Size(); ++i) {
            CK3dEntity *portal = CK3dEntity::Cast(entities[i]);
            if (!portal || !(portal->GetFlags() & CK_3DENTITY_PORTAL) || !IconShown(portal, scene) ||
                !IconListed(m_Display.showPortals, portal->GetID()))
                continue;
            place(portal->GetID(), icon(portal->IsActiveInCurrentScene() ? "Portal" : "PortalInactive"),
                  portal->GetWorldMatrix(), false, 0.0f);
        }
    }
    return icons;
}

std::vector<LayoutView::IconHit> LayoutView::PlaceIconsNow() const {
    ViewProjection projection;
    if (!Projection(projection)) return {};
    return PlaceIcons(projection, Camera()->GetID());
}

bool LayoutView::IconBox(CK_ID owner, VxMatrix &world, VxBbox &box) const {
    CKContext *context = m_Host.Context();
    for (const IconHit &hit : m_IconHits)
        if (hit.owner == owner)
            if (CKMesh *mesh = CKMesh::Cast(context->GetObject(hit.mesh))) {
                world = hit.world;
                box = mesh->GetLocalBox();
                return true;
            }
    return false;
}

// The objects without geometry (Dev.exe sub_4A1E00): cameras with their
// direction (to their target, else 10 ahead) in yellow and, shown, their
// view frustum in white, the near and far planes crossed; lights by type,
// their direction, spot cones and point ranges when shown; 3D frames (a
// cross), cameras' and lights' targets, curve points, portals (their mesh,
// another when inactive in the scene).
void LayoutView::DrawIcons(CKRenderContext *rc) {
    m_IconHits.clear();
    CKContext *context = m_Host.Context();
    CKCamera *view = rc->GetAttachedCamera();
    CKScene *scene = Scene();
    if (!scene || !view) return;
    auto shown = [&](CKObject *object) { return IconShown(object, scene); };
    auto cone = [&](CK_ID id) { return std::find(m_Cones.begin(), m_Cones.end(), id) != m_Cones.end(); };
    auto drawn = [&](bool type, CK_ID id) { return IconListed(type, id); };
    const bool listed = !m_PlayerShown.empty();
    const CKDWORD yellow = 0xFFFFFF00, white = 0xFFFFFFFF;
    const CKDWORD hotspotColor = static_cast<CKDWORD>(-6237953), falloffColor = static_cast<CKDWORD>(-12549889);
    auto line = [&](const VxVector &from, const VxVector &to, CKDWORD color) {
        const VxVector points[2] = {from, to};
        const CKDWORD colors[2] = {color, color};
        DrawLines(rc, points, colors, 2);
    };
    if (m_Display.showCameras || listed) {
        const XObjectPointerArray &cameras = context->GetObjectListByType(CKCID_CAMERA, TRUE);
        for (int i = 0; i < cameras.Size(); ++i) {
            CKCamera *camera = CKCamera::Cast(cameras[i]);
            if (!camera || camera == view || !shown(camera) || !drawn(m_Display.showCameras, camera->GetID())) continue;
            CK3dEntity *target = CKTargetCamera::Cast(camera) ? CKTargetCamera::Cast(camera)->GetTarget() : nullptr;
            VxVector aim(0.0f, 0.0f, 10.0f);
            if (target) target->GetPosition(&aim, camera);
            rc->SetWorldTransformationMatrix(camera->GetWorldMatrix());
            if (cone(camera->GetID())) {
                int w = 4, h = 3;
                camera->GetAspectRatio(w, h);
                const float fov = camera->GetFov(), front = camera->GetFrontPlane(), back = camera->GetBackPlane();
                const float nx = std::tan(fov * 0.5f) * front, ny = h * nx / std::max(1, w);
                const float fx = std::tan(fov * 0.5f) * back, fy = h * fx / std::max(1, w);
                const VxVector p[11] = {VxVector(0, 0, 0),       VxVector(nx, ny, front),   VxVector(-nx, ny, front),
                                        VxVector(-nx, -ny, front), VxVector(nx, -ny, front), VxVector(fx, fy, back),
                                        VxVector(-fx, fy, back),   VxVector(-fx, -fy, back), VxVector(fx, -fy, back),
                                        VxVector(0, 0, 0),         aim};
                static const int edges[34] = {0, 5, 0, 6, 0, 7, 0, 8, 1, 2, 2, 3, 3, 4, 4, 1, 1, 3, 2, 4, 5, 7,
                                              6, 8, 5, 6, 6, 7, 7, 8, 8, 5, 9, 10};
                VxVector points[34];
                CKDWORD colors[34];
                for (int e = 0; e < 34; ++e) {
                    points[e] = p[edges[e]];
                    colors[e] = edges[e] >= 9 ? yellow : white;
                }
                DrawLines(rc, points, colors, 34);
            } else {
                line(VxVector(0, 0, 0), aim, yellow);
            }
        }
    }
    if (m_Display.showLights || listed) {
        const XObjectPointerArray &lights = context->GetObjectListByType(CKCID_LIGHT, TRUE);
        for (int i = 0; i < lights.Size(); ++i) {
            CKLight *light = CKLight::Cast(lights[i]);
            if (!light || !shown(light) || !drawn(m_Display.showLights, light->GetID())) continue;
            const int type = light->GetType();
            CK3dEntity *target = CKTargetLight::Cast(light) ? CKTargetLight::Cast(light)->GetTarget() : nullptr;
            VxVector aim(0.0f, 0.0f, 10.0f);
            if (target) target->GetPosition(&aim, light);
            rc->SetWorldTransformationMatrix(light->GetWorldMatrix());
            const float range = light->GetRange();
            if (cone(light->GetID()) && (type == VX_LIGHTPOINT || type == VX_LIGHTSPOT)) {
                std::vector<VxVector> points;
                std::vector<CKDWORD> colors;
                auto circle = [&](int plane, float radius, float z, CKDWORD color) {
                    for (int k = 0; k < 32; ++k) {
                        const float a0 = k * 0.19634955f, a1 = (k + 1) * 0.19634955f;
                        auto at = [&](float a) {
                            const float c = std::cos(a) * radius, s = std::sin(a) * radius;
                            return plane == 0 ? VxVector(c, 0.0f, s) : plane == 1 ? VxVector(c, s, z) : VxVector(0.0f, c, s);
                        };
                        points.push_back(at(a0));
                        points.push_back(at(a1));
                        colors.push_back(color);
                        colors.push_back(color);
                    }
                };
                if (type == VX_LIGHTPOINT) {
                    // The range in the three planes.
                    circle(0, range, 0.0f, hotspotColor);
                    circle(1, range, 0.0f, hotspotColor);
                    circle(2, range, 0.0f, hotspotColor);
                } else {
                    // The hot spot and fall off cones at the range, four of their lines, the direction.
                    const float hot = std::tan(light->GetHotSpot() * 0.5f) * range,
                                fall = std::tan(light->GetFallOff() * 0.5f) * range;
                    circle(1, hot, range, hotspotColor);
                    circle(1, fall, range, falloffColor);
                    for (int k = 0; k < 4; ++k) {
                        const float a = k * 8 * 0.19634955f;
                        points.push_back(VxVector(0, 0, 0));
                        points.push_back(VxVector(std::cos(a) * hot, std::sin(a) * hot, range));
                        colors.push_back(hotspotColor);
                        colors.push_back(hotspotColor);
                        points.push_back(VxVector(0, 0, 0));
                        points.push_back(VxVector(std::cos(a) * fall, std::sin(a) * fall, range));
                        colors.push_back(falloffColor);
                        colors.push_back(falloffColor);
                    }
                    points.push_back(VxVector(0, 0, 0));
                    points.push_back(aim);
                    colors.push_back(yellow);
                    colors.push_back(yellow);
                }
                DrawLines(rc, points.data(), colors.data(), static_cast<int>(points.size()));
            } else if (type == VX_LIGHTSPOT || type == VX_LIGHTDIREC) {
                line(VxVector(0, 0, 0), aim, yellow);
            }
        }
    }
    // The icons, as the camera shows them (PlaceIcons), kept for the selection's boxes.
    VxRect rect;
    rc->GetViewRect(rect);
    m_IconHits = PlaceIcons(ViewProjection::Of(view, rect), view->GetID());
    for (const IconHit &icon : m_IconHits) RenderIcon(rc, icon);
}

// The selection (Dev.exe sub_4A1E00): the local box of each selected 3D
// entity with geometry in the selection color (white), the hierarchical
// box of characters, places' in green (their objects are inside it); with
// Show ObjectAxis, each entity's axes in red, green and blue, over
// everything.
void LayoutView::DrawSelection(CKRenderContext *rc) {
    CKContext *context = m_Host.Context();
    const CKDWORD white = 0xFFFFFFFF, green = 0xFF00FF00, yellow = 0xFFFFFF00;
    if (CK3dEntity *target = CK3dEntity::Cast(context->GetObject(m_DropTarget))) {
        VxMatrix world;
        VxBbox box;
        if (CKCharacter::Cast(target))
            DrawBox(rc, VxMatrix::Identity(), target->GetHierarchicalBox(FALSE), yellow);
        else if (target->GetCurrentMesh() || CKSprite3D::Cast(target))
            DrawBox(rc, target->GetWorldMatrix(), target->GetBoundingBox(TRUE), yellow);
        else if (IconBox(target->GetID(), world, box))
            DrawBox(rc, world, box, yellow);
    }
    std::vector<CK3dEntity *> places;
    for (CK_ID id : m_Selection)
        if (CKPlace *place = CKPlace::Cast(context->GetObject(id)))
            if (place->IsVisible()) places.push_back(place);
    std::vector<CK3dEntity *> axes;
    for (CK_ID id : m_Selection) {
        CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(id));
        if (!entity || !entity->IsVisible() || IsInterfaceObject(id)) continue;
        if (CKPlace::Cast(entity)) {
            DrawBox(rc, VxMatrix::Identity(), entity->GetHierarchicalBox(FALSE), green);
            continue;
        }
        if (CKPlace *place = entity->GetReferencePlace())
            if (std::find(places.begin(), places.end(), place) != places.end()) continue;
        axes.push_back(entity);
        if (CKCharacter::Cast(entity)) {
            DrawBox(rc, VxMatrix::Identity(), entity->GetHierarchicalBox(FALSE), white);
            continue;
        }
        CKMesh *mesh = entity->GetCurrentMesh();
        if (!CKSprite3D::Cast(entity) && !mesh) {
            // Objects drawn as icons: the icon's box.
            VxMatrix world;
            VxBbox box;
            if (IconBox(id, world, box)) DrawBox(rc, world, box, white);
            continue;
        }
        DrawBox(rc, entity->GetWorldMatrix(), entity->GetBoundingBox(TRUE), white);
    }
    if (!m_Display.showObjectAxis || axes.empty()) return;
    const CKDWORD zfunc = rc->GetState(VXRENDERSTATE_ZFUNC);
    rc->SetState(VXRENDERSTATE_ZFUNC, VXCMP_ALWAYS);
    CKCamera *camera = rc->GetAttachedCamera();
    for (CK3dEntity *entity : axes) {
        VxMatrix world = entity->GetWorldMatrix();
        if (m_Display.constantObjectAxis > 0) {
            // Constant world size, or constant on screen (Dev: distance *
            // tan(fov / 2) * percentage / 10).
            float size = m_Display.objectAxisZoom;
            if (m_Display.constantObjectAxis > 1 && camera) {
                VxVector at, from;
                entity->GetPosition(&at);
                camera->GetPosition(&from);
                size = Magnitude(at - from) * std::tan(camera->GetFov() * 0.5f) * size * 0.1f;
            }
            size *= 0.1f;
            for (int i = 0; i < 3; ++i) {
                VxVector axis(world[i][0], world[i][1], world[i][2]);
                axis.Normalize();
                axis *= size;
                world[i] = VxVector4(axis.x, axis.y, axis.z, 0.0f);
            }
        }
        rc->SetWorldTransformationMatrix(world);
        const VxVector points[6] = {VxVector(0, 0, 0), VxVector(1, 0, 0), VxVector(0, 0, 0),
                                    VxVector(0, 1, 0), VxVector(0, 0, 0), VxVector(0, 0, 1)};
        const CKDWORD colors[6] = {0xFFFF0000, 0xFFFF0000, 0xFF00FF00, 0xFF00FF00, 0xFF0000FF, 0xFF0000FF};
        DrawLines(rc, points, colors, 6);
    }
    rc->SetState(VXRENDERSTATE_ZFUNC, zfunc);
}

void LayoutView::DrawReferenceGrid(CKRenderContext *rc) {
    const int columns = std::max(1, m_Display.gridSections[0]) + 1, rows = std::max(1, m_Display.gridSections[1]) + 1;
    const float stepX = m_Display.gridStep[0], stepZ = m_Display.gridStep[1];
    const float halfX = (columns - 1) * 0.5f * stepX, halfZ = (rows - 1) * 0.5f * stepZ;
    rc->SetWorldTransformationMatrix(GuideMatrix());
    const int vertices = 2 * (columns + rows);
    VxDrawPrimitiveData *data = rc->GetDrawPrimitiveStructure(CKRST_DP_TR_CL_VC, vertices);
    if (!data) return;
    auto *positions = static_cast<unsigned char *>(data->PositionPtr);
    auto *colors = static_cast<unsigned char *>(data->ColorPtr);
    int n = 0;
    auto vertex = [&](float x, float z) {
        *reinterpret_cast<VxVector *>(positions + n * data->PositionStride) = VxVector(x, 0.0f, z);
        *reinterpret_cast<CKDWORD *>(colors + n * data->ColorStride) = m_Display.gridColor;
        ++n;
    };
    for (int i = 0; i < rows; ++i) {
        const float z = (i - (rows - 1) * 0.5f) * stepZ;
        vertex(-halfX, z);
        vertex(halfX, z);
    }
    for (int j = 0; j < columns; ++j) {
        const float x = (j - (columns - 1) * 0.5f) * stepX;
        vertex(x, -halfZ);
        vertex(x, halfZ);
    }
    std::vector<CKWORD> indices(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) indices[i] = static_cast<CKWORD>(i);
    rc->DrawPrimitive(VX_LINELIST, indices.data(), n, data);
}

// The world axis (Dev.exe sub_4A1E00): the world X, Y and Z of length 2 seen
// by the camera 50 in front of it, moved to 8% of the view from its left
// side and 15% from its bottom, with arrows of 25 degrees and 8 pixels; the
// labels 8 pixels past the tips.
void LayoutView::ComputeWorldAxis(CKRenderContext *rc) {
    m_Axis.valid = false;
    CKContext *context = m_Host.Context();
    CKCamera *camera = rc->GetAttachedCamera();
    if (!camera || !m_Display.showWorldAxis) {
        for (CK_ID id : m_AxisLabels)
            if (CK2dEntity *label = CK2dEntity::Cast(context->GetObject(id))) label->SetPosition(Vx2DVector(-100.0f, -100.0f));
        return;
    }
    VxRect view;
    rc->GetViewRect(view);
    const int width = static_cast<int>(view.GetWidth()), height = static_cast<int>(view.GetHeight());
    const float offsetX = static_cast<float>(42 * (width / 2) / 50), offsetY = static_cast<float>(35 * (height / 2) / 50);
    const VxVector axes[4] = {VxVector(0, 0, 2), VxVector(0, 2, 0), VxVector(2, 0, 0), VxVector(0, 0, 0)};
    Vx2DVector screen[4];
    // Dev keeps two decimals (rounded half away from zero).
    auto round2 = [](float v) { return static_cast<float>(static_cast<int>(v * 100.0f + (v >= 0.0f ? 0.5f : -0.5f))) * 0.01f; };
    for (int i = 0; i < 4; ++i) {
        VxVector local(0, 0, 0);
        if (i < 3) {
            camera->InverseTransformVector(&local, &axes[i]);
            local.Normalize();
            local *= 2.0f;
            local = VxVector(round2(local.x), round2(local.y), round2(local.z));
        }
        local += VxVector(0.0f, 0.0f, 50.0f);
        VxVector projected;
        rc->Transform(&projected, &local, camera);
        screen[i] = Vx2DVector(std::floor(projected.x - offsetX + 0.5f), std::floor(projected.y + offsetY + 0.5f));
    }
    m_Axis.origin = screen[3];
    // Dev's order: Z, Y, X; kept here as X, Y, Z.
    for (int axis = 0; axis < 3; ++axis) {
        const Vx2DVector tip = screen[2 - axis];
        m_Axis.tip[axis] = tip;
        Vx2DVector back(m_Axis.origin.x - tip.x, m_Axis.origin.y - tip.y);
        if (back.x == 0.0f && back.y == 0.0f) {
            m_Axis.left[axis] = m_Axis.right[axis] = tip;
            continue;
        }
        const float length = std::sqrt(back.x * back.x + back.y * back.y);
        back.x /= length;
        back.y /= length;
        const float angle = 0.43633232f;
        auto rotated = [&](float a) {
            return Vx2DVector(tip.x + (back.x * std::cos(a) - back.y * std::sin(a)) * 8.0f,
                              tip.y + (back.x * std::sin(a) + back.y * std::cos(a)) * 8.0f);
        };
        m_Axis.left[axis] = rotated(angle);
        m_Axis.right[axis] = rotated(-angle);
    }
    // Labels: tip - (8 - direction * 8), the direction being the axis seen
    // by the camera (y up).
    for (int axis = 0; axis < 3; ++axis) {
        CK2dEntity *label = CK2dEntity::Cast(context->GetObject(m_AxisLabels[axis]));
        if (!label) continue;
        VxVector local(0, 0, 0);
        camera->InverseTransformVector(&local, &axes[2 - axis]);
        local.Normalize();
        const Vx2DVector tip = m_Axis.tip[axis];
        label->SetPosition(Vx2DVector(std::floor(tip.x - (8.0f - local.x * 8.0f) + 0.5f),
                                      std::floor(tip.y - (8.0f + local.y * 8.0f) + 0.5f)));
    }
    m_Axis.valid = true;
}

// Dev.exe sub_4A6DD0: each selected, shown 2D entity's on-screen rectangle
// outlined, with its four handles (Handle2DAt's places, 10 pixels) when it
// is large enough to hold them.
void LayoutView::DrawSelection2D(CKRenderContext *rc) {
    CKContext *context = m_Host.Context();
    std::vector<VxVector4> points;
    for (CK_ID id : m_Selection) {
        CK2dEntity *sprite = CK2dEntity::Cast(context->GetObject(id));
        if (!sprite || !sprite->IsVisible() || IsInterfaceObject(id)) continue;
        VxRect source, rect;
        sprite->GetExtents(source, rect);
        const float l = rect.left - 1.0f, t = rect.top - 1.0f, r = rect.right, b = rect.bottom;
        auto line = [&](float x0, float y0, float x1, float y1) {
            points.push_back(VxVector4(x0, y0, 0.0f, 1.0f));
            points.push_back(VxVector4(x1, y1, 0.0f, 1.0f));
        };
        auto box = [&](float x0, float y0, float x1, float y1) {
            line(x0, y0, x1, y0);
            line(x1, y0, x1, y1);
            line(x1, y1, x0, y1);
            line(x0, y1, x0, y0);
        };
        box(l, t, r, b);
        const float h = 10.0f;
        if (rect.GetWidth() >= 2.0f * h && rect.GetHeight() >= 2.0f * h) {
            const float cx = std::floor((rect.left + rect.right) * 0.5f), cy = std::floor((rect.top + rect.bottom) * 0.5f);
            box(rect.left, rect.top, rect.left + h, rect.top + h);
            box(rect.right - h, rect.bottom - h, rect.right, rect.bottom);
            box(cx - h * 0.5f, rect.bottom - h, cx + h * 0.5f, rect.bottom);
            box(rect.right - h, cy - h * 0.5f, rect.right, cy + h * 0.5f);
        }
    }
    const int count = static_cast<int>(points.size());
    if (!count || count > 60000) return;
    VxDrawPrimitiveData *data = rc->GetDrawPrimitiveStructure(CKRST_DP_VC, count);
    if (!data) return;
    auto *positions = static_cast<unsigned char *>(data->PositionPtr);
    auto *colors = static_cast<unsigned char *>(data->ColorPtr);
    std::vector<CKWORD> indices(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        *reinterpret_cast<VxVector4 *>(positions + i * data->PositionStride) = points[i];
        *reinterpret_cast<CKDWORD *>(colors + i * data->ColorStride) = 0xFFFFFFFF;
        indices[i] = static_cast<CKWORD>(i);
    }
    const CKDWORD zenable = rc->GetState(VXRENDERSTATE_ZENABLE);
    rc->SetState(VXRENDERSTATE_ZENABLE, FALSE);
    rc->SetTexture(nullptr);
    rc->DrawPrimitive(VX_LINELIST, indices.data(), count, data);
    rc->SetState(VXRENDERSTATE_ZENABLE, zenable);
}

// Dev.exe sub_4AF910 (the lines, rebuilt with the view's size) and
// sub_4A1E00 (drawn after the selection): vertical and horizontal lines over
// the render context from the Screen Offset (taken modulo the size), Screen
// Step apart, each a step beyond the edges; colored 0x80FFFFFF, drawn as the
// rest of the interface without blending.
void LayoutView::DrawScreenGrid(CKRenderContext *rc) {
    const int width = rc->GetWidth(), height = rc->GetHeight();
    if (width <= 0 || height <= 0) return;
    float offsetX = m_Display.screenOffset[0], offsetY = m_Display.screenOffset[1];
    float stepX = m_Display.screenStep[0], stepY = m_Display.screenStep[1];
    if (m_Display.homogenousScreenGrid) {
        offsetX = width * offsetX + 0.5f;
        offsetY = height * offsetY + 0.5f;
        stepX = width * stepX + 0.5f;
        stepY = height * stepY + 0.5f;
    }
    if (stepX < 1.0f || stepY < 1.0f) return;
    const float startX = offsetX - static_cast<float>(static_cast<long long>(offsetX / width)) * width;
    // Dev divides the shifted X here (sub_4AF910), not the Y offset.
    const float startY = offsetY - static_cast<float>(static_cast<long long>(startX / height)) * height;
    const int columns = std::max(1, (startX != 0.0f ? 1 : 0) + static_cast<int>(std::floor((width - startX) / stepX)));
    const int rows = std::max(1, (startY != 0.0f ? 1 : 0) + static_cast<int>(std::floor((height - startY) / stepY)));
    const int count = 2 * (columns + rows);
    if (count > 60000) return;
    VxDrawPrimitiveData *data = rc->GetDrawPrimitiveStructure(CKRST_DP_VC, count);
    if (!data) return;
    auto *positions = static_cast<unsigned char *>(data->PositionPtr);
    auto *vertexColors = static_cast<unsigned char *>(data->ColorPtr);
    int n = 0;
    auto vertex = [&](float x, float y) {
        *reinterpret_cast<VxVector4 *>(positions + n * data->PositionStride) = VxVector4(x, y, 0.0f, 1.0f);
        *reinterpret_cast<CKDWORD *>(vertexColors + n * data->ColorStride) = 0x80FFFFFF;
        ++n;
    };
    for (int i = 0; i < columns; ++i) {
        const float x = (i + (startX == 0.0f ? 1 : 0)) * stepX + startX + 0.5f;
        vertex(x, -stepY);
        vertex(x, height + stepY);
    }
    for (int i = 0; i < rows; ++i) {
        const float y = (i + (startY == 0.0f ? 1 : 0)) * stepY + startY + 0.5f;
        vertex(-stepX, y);
        vertex(width + stepX, y);
    }
    std::vector<CKWORD> indices(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) indices[i] = static_cast<CKWORD>(i);
    rc->SetTexture(nullptr);
    rc->DrawPrimitive(VX_LINELIST, indices.data(), count, data);
}

void LayoutView::DrawWorldAxis(CKRenderContext *rc) {
    if (!m_Axis.valid) return;
    const CKDWORD colors[3] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF};
    VxDrawPrimitiveData *data = rc->GetDrawPrimitiveStructure(CKRST_DP_VC, 18);
    if (!data) return;
    auto *positions = static_cast<unsigned char *>(data->PositionPtr);
    auto *vertexColors = static_cast<unsigned char *>(data->ColorPtr);
    int n = 0;
    auto vertex = [&](const Vx2DVector &at, CKDWORD color) {
        *reinterpret_cast<VxVector4 *>(positions + n * data->PositionStride) = VxVector4(at.x, at.y, 0.0f, 1.0f);
        *reinterpret_cast<CKDWORD *>(vertexColors + n * data->ColorStride) = color;
        ++n;
    };
    for (int axis = 0; axis < 3; ++axis) {
        vertex(m_Axis.origin, colors[axis]);
        vertex(m_Axis.tip[axis], colors[axis]);
        vertex(m_Axis.tip[axis], colors[axis]);
        vertex(m_Axis.left[axis], colors[axis]);
        vertex(m_Axis.tip[axis], colors[axis]);
        vertex(m_Axis.right[axis], colors[axis]);
    }
    const CKDWORD zenable = rc->GetState(VXRENDERSTATE_ZENABLE);
    rc->SetState(VXRENDERSTATE_ZENABLE, FALSE);
    rc->SetTexture(nullptr);
    CKWORD indices[18];
    for (int i = 0; i < 18; ++i) indices[i] = static_cast<CKWORD>(i);
    rc->DrawPrimitive(VX_LINELIST, indices, 18, data);
    rc->SetState(VXRENDERSTATE_ZENABLE, zenable);
    CKContext *context = m_Host.Context();
    for (CK_ID id : m_AxisLabels)
        if (CK2dEntity *label = CK2dEntity::Cast(context->GetObject(id))) label->Render(rc);
}

} // namespace vdev
