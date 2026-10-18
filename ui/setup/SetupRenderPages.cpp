#include <filesystem>
// The Setups of the render objects: 3D entities, cameras, lights, sprites, materials, meshes, textures, grids.

#include "ui/setup/SetupWindows.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>


#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "imgui_internal.h"

#include "CKAll.h"

#include "core/runtime/CkRuntimeHost.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/ParameterBox.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/FileDialogs.h"
#include "ui/controls/TypeAhead.h"
#include "core/runtime/GridCompat.h"
#include "core/runtime/SdkCompat.h"
#include "core/level/LevelCommands.h"
#include "core/level/ObjectEditCommands.h"
#include "core/level/SetupCommands.h"
#include "core/level/SceneCommands.h"
#include "ui/schematic/Snapshots.h"
#include "core/runtime/TextEncoding.h"
#include "core/graph/ValueCommands.h"
#include "ui/setup/SetupWindowsDetail.h"

namespace vdev {

using namespace setup;

// ---------------------------------------------------------------- pages

// Position / Orientation / Scale (World and Local), World Size (Dev.exe
// sub_449270: the rects are its own); 3D objects: meshes, Pickable
// (sub_44F9C0).
void SetupWindows::Entity3DPage(CK3dEntity *entity, bool sized, bool details,
                                const std::function<void(const DevPage &page)> &fourth, Setup *setup) {
    // Dev: cameras and lights show Position and Orientation only.
    if (!entity) return;
    const CK_ID id = entity->GetID();
    const DevPage page;
    const ShownTransform &shown = Shown(entity);
    float ex = 0, ey = 0, ez = 0;
    Vx3DMatrixToEulerAngles(shown.world, &ex, &ey, &ez);
    const float worldEuler[3] = {ex * RAD_TO_DEG, ey * RAD_TO_DEG, ez * RAD_TO_DEG};
    Vx3DMatrixToEulerAngles(shown.local, &ex, &ey, &ez);
    const float localEuler[3] = {ex * RAD_TO_DEG, ey * RAD_TO_DEG, ez * RAD_TO_DEG};
    // A group at x (0, 200, 400): its title (and button), X, Y and Z in the
    // World and the Local (the parent's) referential.
    using Apply = std::function<void(CK3dEntity *, const float *, CK3dEntity *parent, bool local)>;
    auto group = [&](float x, const char *title, float titleRight, const char *button, float buttonRight,
                     const float *world, const float *local, const char *label, const Apply &apply,
                     const std::function<void()> &press) {
        page.Label(x + 20, 2, titleRight, 17, title);
        if (button) {
            const ImVec2 a = page.At(x + 105, 1), b = page.At(buttonRight, 17);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 0.0f));
            if (ImGui::Button(button, ImVec2(b.x - a.x, b.y - a.y))) press();
            ImGui::PopStyleVar();
        }
        page.Line(x + 25, 17, x + 26, 97);
        page.Label(x + 70, 17, x + 140, 32, "World");
        page.Label(x + 150, 17, x + 220, 32, "Local");
        static const char *const AXES[] = {"X :", "Y :", "Z :"};
        ImGui::PushID(title);
        for (int i = 0; i < 3; ++i) {
            const float y = 37.0f + 20.0f * i;
            page.Label(x + 40, y, x + 65, y + 20, AXES[i]);
            ImGui::PushID(i);
            float w[3] = {world[0], world[1], world[2]}, l[3] = {local[0], local[1], local[2]};
            page.Place(x + 70, y, x + 140, y + 20);
            if (ImGui::DragFloat("##v", &w[i], 0.01f, 0.0f, 0.0f, "%.4f"))
                Live(id, label, [&](CKObject *o) { apply(CK3dEntity::Cast(o), w, nullptr, false); });
            page.Place(x + 150, y, x + 220, y + 20);
            if (ImGui::DragFloat("##local", &l[i], 0.01f, 0.0f, 0.0f, "%.4f"))
                Live(id, label, [&](CKObject *o) {
                    CK3dEntity *moved = CK3dEntity::Cast(o);
                    apply(moved, l, moved->GetParent(), true);
                });
            ImGui::PopID();
        }
        ImGui::PopID();
    };
    const VxVector position = Translation(shown.world), localPosition = Translation(shown.local);
    const float p[3] = {position.x, position.y, position.z}, lp[3] = {localPosition.x, localPosition.y, localPosition.z};
    group(0, "Position", 95, "Reset World Matrix", 195, p, lp, "Set Position",
          [](CK3dEntity *e, const float *v, CK3dEntity *parent, bool) {
              VxVector at(v[0], v[1], v[2]);
              e->SetPosition(&at, parent);
          },
          [&] { Act("Reset World Matrix", [&] { return ResetWorldMatrix(*m_State.document, id); }); });
    group(200, "Orientation", 295, nullptr, 0, worldEuler, localEuler, "Set Orientation",
          [](CK3dEntity *e, const float *v, CK3dEntity *parent, bool) {
              VxMatrix rotation;
              Vx3DMatrixFromEulerAngles(rotation, v[0] / RAD_TO_DEG, v[1] / RAD_TO_DEG, v[2] / RAD_TO_DEG);
              VxQuaternion quaternion = Vx3DQuaternionFromMatrix(rotation);
              e->SetQuaternion(&quaternion, parent, FALSE, TRUE);
          },
          nullptr);
    if (sized) {
        const VxVector scale = ScaleOf(shown.world), localScale = ScaleOf(shown.local);
        const float sc[3] = {scale.x, scale.y, scale.z}, ls[3] = {localScale.x, localScale.y, localScale.z};
        group(400, "Scale", 465, "Set as Unit", 574, sc, ls, "Set Scale",
              [](CK3dEntity *e, const float *v, CK3dEntity *, bool local) {
                  VxVector size(v[0], v[1], v[2]);
                  e->SetScale(&size, FALSE, local ? TRUE : FALSE);
              },
              [&] { Act("Set as Unit", [&] { return SetScaleToUnit(*m_State.document, id); }); });
    }
    if (fourth) {
        fourth(page);
    } else if (sized) {
        // World Size (read only): the world bounding box.
        const VxBbox &box = shown.box;
        const float size[3] = {box.Max.x - box.Min.x, box.Max.y - box.Min.y, box.Max.z - box.Min.z};
        page.Label(620, 2, 690, 17, "World Size");
        page.Line(625, 17, 626, 97);
        static const char *const AXES[] = {"X :", "Y :", "Z :"};
        for (int i = 0; i < 3; ++i) {
            const float y = 37.0f + 20.0f * i;
            char text[32];
            std::snprintf(text, sizeof(text), "%.4f", size[i]);
            page.Label(640, y, 665, y + 20, AXES[i]);
            page.Label(673, y, 740, y + 20, text);
        }
    }
    const float right = (ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - page.origin.x) / page.s;
    page.Line(0, 100, std::max(1.0f, right), 101);
    ImGui::SetCursorScreenPos(page.At(0, 104));
    if (!details) return;

    // Object Meshes (Dev.exe sub_44F330): a click selects (Ctrl: adds or
    // takes off), a double click opens the Mesh Setup; the menu: Show Mesh
    // Setup, Set As Current, Add Mesh, Remove Mesh(es) (also Delete).
    page.Label(20, 110, 200, 130, "Object Meshes");
    static std::set<CK_ID> none;
    std::set<CK_ID> &rows = setup ? setup->meshRows : none;
    for (auto it = rows.begin(); it != rows.end();) {
        CKMesh *mesh = Get<CKMesh>(m_State, *it);
        bool held = false;
        for (int i = 0; mesh && i < entity->GetMeshCount(); ++i) held = held || entity->GetMesh(i) == mesh;
        it = held ? std::next(it) : rows.erase(it);
    }
    auto removeMeshes = [&] {
        const std::vector<CK_ID> chosen(rows.begin(), rows.end());
        if (chosen.empty()) return;
        Live(id, "Remove Mesh(es)", [this, chosen](CKObject *o) {
            for (CK_ID mesh : chosen) CK3dEntity::Cast(o)->RemoveMesh(Get<CKMesh>(m_State, mesh));
        });
        FlushPending();
        rows.clear();
    };
    ImGui::SetCursorScreenPos(page.At(20, 130));
    if (ImGui::BeginChild("meshes", ImVec2(190.0f * page.s, 130.0f * page.s), ImGuiChildFlags_Borders)) {
        TypeAhead typeAhead;
        for (int i = 0; i < entity->GetMeshCount(); ++i) {
            CKMesh *mesh = entity->GetMesh(i);
            if (!mesh) continue;
            ImGui::PushID(i);
            const CK_ID meshId = mesh->GetID();
            const bool selected = rows.count(meshId) != 0;
            const std::string text = NameOf(mesh) + " (Mesh)";
            if (typeAhead.Selectable(text.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) m_State.events.Post(ShowObjectSetup{meshId});
                else if (!ImGui::GetIO().KeyCtrl) rows = {meshId};
                else if (selected) rows.erase(meshId);
                else rows.insert(meshId);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) rows = {meshId};
            if (ImGui::BeginPopupContextItem("meshmenu")) {
                const bool single = rows.size() == 1;
                if (ImGui::MenuItem("Show Mesh Setup", nullptr, false, single))
                    m_State.events.Post(ShowObjectSetup{*rows.begin()});
                ImGui::Separator();
                if (ImGui::MenuItem("Set As Current", nullptr, false, single)) {
                    const CK_ID chosen = *rows.begin();
                    Live(id, "Set Current Mesh", [this, chosen](CKObject *o) {
                        CK3dEntity::Cast(o)->SetCurrentMesh(Get<CKMesh>(m_State, chosen), FALSE);
                    });
                    FlushPending();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Add Mesh")) {
                    if (setup) setup->openAddMesh = true;
                }
                if (ImGui::MenuItem("Remove Mesh(es)", "Delete", false, !rows.empty())) removeMeshes();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        // Elsewhere in the list: Add Mesh.
        if (ImGui::BeginPopupContextWindow("meshesmenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Add Mesh") && setup) setup->openAddMesh = true;
            ImGui::EndPopup();
        }
        if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            removeMeshes();
    }
    ImGui::EndChild();
    // A mesh dragged from the Level Manager (documentation "3D Object Setup").
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(LEVEL_OBJECT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            CK_ID dragged = 0;
            std::memcpy(&dragged, payload->Data, sizeof(dragged));
            CKMesh *mesh = Get<CKMesh>(m_State, dragged);
            bool held = false;
            for (int i = 0; mesh && i < entity->GetMeshCount(); ++i) held = held || entity->GetMesh(i) == mesh;
            if (mesh && !held && !payload->IsDelivery()) ImGui::SetTooltip("Add Mesh %s", NameOf(mesh).c_str());
            if (mesh && !held && payload->IsDelivery()) {
                Live(id, "Add Mesh", [this, dragged](CKObject *o) { CK3dEntity::Cast(o)->AddMesh(Get<CKMesh>(m_State, dragged)); });
                FlushPending();
            }
        }
        ImGui::EndDragDropTarget();
    }
    // Add Mesh (Dev: an Edit Parameter of a Mesh): the composition's meshes.
    if (setup && setup->openAddMesh) {
        setup->openAddMesh = false;
        ImGui::OpenPopup("Add Mesh");
    }
    if (ImGui::BeginPopup("Add Mesh")) {
        TypeAhead typeAhead;
        for (CKObject *object : ObjectsOf(*m_State.document, CKCID_MESH, true)) {
            ImGui::PushID(static_cast<int>(object->GetID()));
            if (typeAhead.Selectable(NameOf(object).c_str())) {
                const CK_ID chosen = object->GetID();
                Live(id, "Add Mesh", [this, chosen](CKObject *o) { CK3dEntity::Cast(o)->AddMesh(Get<CKMesh>(m_State, chosen)); });
                FlushPending();
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    // Current Mesh.
    page.Label(220, 110, 320, 130, "Current Mesh");
    page.Line(225, 130, 226, 260);
    CKMesh *current = entity->GetCurrentMesh();
    page.Label(240, 130, 400, 150, NameOf(current).c_str());
    if (current) {
        char text[4][32];
        std::snprintf(text[0], sizeof(text[0]), "%d", current->GetVertexCount());
        std::snprintf(text[1], sizeof(text[1]), "%d", current->GetFaceCount());
        std::snprintf(text[2], sizeof(text[2]), "%d", current->GetChannelCount());
        std::snprintf(text[3], sizeof(text[3]), "%s", CKPatchMesh::Cast(current) ? "Patch" : "Triangular");
        static const char *const NAMES[] = {"Vertices:", "Faces:", "Channels:", "Type:"};
        for (int i = 0; i < 4; ++i) {
            const float y = 160.0f + 15.0f * i;
            page.Label(240, y, 300, y + 15, NAMES[i]);
            page.Label(320, y, 400, y + 15, text[i]);
        }
    }
    // A body part's character (1220, Show Character Setup).
    if (CKBodyPart *part = CKBodyPart::Cast(entity)) {
        static const int SETUP_BUTTON[5] = {1145, 1141, 1142, 1142, 1145};
        CKCharacter *character = part->GetCharacter();
        page.Label(420, 108, 500, 130, "Character:");
        if (DevBitmapButton("charactersetup", page.At(500, 110), SETUP_BUTTON, false, character != nullptr, page.s,
                            "Show Character Setup") &&
            character)
            m_State.events.Post(ShowObjectSetup{character->GetID()});
        page.Label(520, 111, 620, 132, NameOf(character).c_str());
    } else if (CK3dEntity *parent = entity->GetParent()) {
        page.Label(420, 108, 500, 130, "Parent:");
        page.Label(500, 108, 620, 130, NameOf(parent).c_str());
    }
    bool pickable = properties::Entity3DPickable.get(entity);
    if (page.Check("Pickable", 620, 111, pickable)) LiveSet(id, properties::Entity3DPickable, pickable);
    page.Label(635, 109, 775, 125, "Pickable");
    page.Label(635, 124, 800, 140, "(Hold shift to pick it anyway)");
    page.End(800, 262);
}

// Camera Setup as Dev's (Dev.exe sub_454330).
void SetupWindows::CameraPage(CKCamera *camera) {
    if (!camera) return;
    const CK_ID id = camera->GetID();
    const DevPage page;
    Entity3DPage(camera, false, false);
    CK3dEntity *target = CKTargetCamera::Cast(camera) ? camera->GetTarget() : nullptr;
    // The distance to the target (1194, 1195: in the Scale's place).
    if (target) {
        VxVector from, to;
        camera->GetPosition(&from);
        target->GetPosition(&to);
        char text[32];
        std::snprintf(text, sizeof(text), "%.3f", Magnitude(to - from));
        page.Label(430, 55, 580, 70, "Distance from camera");
        page.Label(430, 70, 530, 85, "to target:");
        page.Label(540, 70, 600, 88, text);
        // Target position (documentation "Camera Setup": "Coordinates of the
        // target in the world coordinate system"): the target moves.
        page.Label(430, 90, 560, 105, "Target position");
        float at[3] = {to.x, to.y, to.z};
        page.Place(430, 105, 640, 125);
        if (ImGui::DragFloat3("##targetposition", at, 0.05f, 0.0f, 0.0f, "%.3f"))
            Live(target->GetID(), "Target Position", [at](CKObject *o) {
                VxVector p(at[0], at[1], at[2]);
                CK3dEntity::Cast(o)->SetPosition(&p);
            });
    }
    // Dev.exe sub_4584A0: the 3D Layout's list of the objects shown in Player
    // Mode (not the object's visibility); the 3D Layout draws the camera's
    // frustum (its own state, not the camera's).
    page.Line(25, 110, 26, 150);
    bool shown = m_State.playerShown.count(id) != 0;
    if (page.Check("Show In Player Mode", 45, 114, shown))
        m_State.SetInLayoutList(m_State.playerShown, id, shown, "Show In Player Mode");
    page.Label(60, 112, 210, 128, "Show In Player Mode");
    bool cone = m_State.layoutCones.count(id) != 0;
    if (page.Check("Show cone", 45, 134, cone)) m_State.SetInLayoutList(m_State.layoutCones, id, cone, "Show cone");
    page.Label(60, 132, 200, 148, "Show cone");
    if (CKTargetCamera::Cast(camera)) {
        page.Label(20, 190, 80, 210, "Target");
        CK_ID chosen = 0;
        if (ObjectCombo("##target", CKCID_3DENTITY, target, &chosen, page.Place(80, 190, 210, 210)))
            Live(id, "Camera Target", [this, chosen](CKObject *o) { CKCamera::Cast(o)->SetTarget(Get<CK3dEntity>(m_State, chosen)); });
    }

    // Projection Type.
    page.Label(220, 110, 400, 130, "Projection Type");
    page.Line(225, 130, 226, 170);
    const int projection = properties::CameraProjection.get(camera);
    bool perspective = projection == CK_PERSPECTIVEPROJECTION, orthographic = !perspective;
    if (page.Check("Perspective", 245, 134, perspective)) LiveSet(id, properties::CameraProjection, CK_PERSPECTIVEPROJECTION);
    page.Label(260, 132, 400, 148, "Perspective");
    if (page.Check("Orthographic", 245, 154, orthographic))
        LiveSet(id, properties::CameraProjection, CK_ORTHOGRAPHICPROJECTION);
    page.Label(260, 152, 400, 168, "Orthographic");

    // Aspect Ratio: Dev's presets (Custom, Normal 4:3, Extended 16:9,
    // Widescreen 7:3, Panoramic 20:7).
    page.Label(220, 190, 310, 210, "Aspect Ratio");
    bool aspect = properties::CameraAspectRatioActive.get(camera);
    if (page.Check("Active", 325, 194, aspect)) LiveSet(id, properties::CameraAspectRatioActive, aspect);
    page.Label(340, 190, 400, 210, "Active");
    page.Line(225, 210, 226, 270);
    int width = 4, height = 3;
    camera->GetAspectRatio(width, height);
    static const char *const types[] = {"Custom", "Normal", "Extended", "Widescreen", "Panoramic"};
    static const int RATIOS[5][2] = {{0, 0}, {4, 3}, {16, 9}, {7, 3}, {20, 7}};
    int type = 0;
    for (int r = 1; r < 5; ++r)
        if (width * RATIOS[r][1] == height * RATIOS[r][0]) type = r;
    page.Label(240, 210, 300, 230, "Type");
    page.Place(300, 210, 400, 230);
    int picked = type;
    if (TypeAheadCombo("##aspecttype", &picked, types, 5) && picked != type && picked > 0)
        Live(id, "Aspect Ratio", [picked](CKObject *o) { CKCamera::Cast(o)->SetAspectRatio(RATIOS[picked][0], RATIOS[picked][1]); });
    int x = width, y = height;
    page.Label(240, 230, 270, 250, "X :");
    page.Place(270, 230, 310, 250);
    if (ImGui::DragInt("##aspectx", &x, 0.1f, 1, 10000) && x > 0)
        Live(id, "Aspect Ratio", [x, height](CKObject *o) { CKCamera::Cast(o)->SetAspectRatio(x, height); });
    page.Label(330, 230, 360, 250, "Y :");
    page.Place(360, 230, 400, 250);
    if (ImGui::DragInt("##aspecty", &y, 0.1f, 1, 10000) && y > 0)
        Live(id, "Aspect Ratio", [width, y](CKObject *o) { CKCamera::Cast(o)->SetAspectRatio(width, y); });
    char ratio[32];
    std::snprintf(ratio, sizeof(ratio), "%.4f", height ? static_cast<float>(width) / height : 0.0f);
    page.Label(240, 250, 295, 270, "Ratio:");
    page.Label(303, 250, 370, 270, ratio);

    // Perspective: the field of view and the focal length (Dev.exe
    // sub_4531F0 140 / 142: the 35 mm film's, 18 / tan(FOV / 2)), the
    // presets (Custom first).
    page.Label(420, 110, 500, 130, "Perspective");
    const float half = properties::CameraFov.get(camera) * 0.5f;
    float focal = half > 0.0f ? 18.0f / std::tan(half) : 0.0f;
    static const int LENSES[] = {15, 20, 24, 28, 35, 50, 85, 135, 200};
    int lens = -1;
    for (int i = 0; i < 9; ++i)
        if (std::fabs(focal - LENSES[i]) < 0.01f) lens = i;
    const std::string preset = lens >= 0 ? std::to_string(LENSES[lens]) + " mm" : std::string("Custom");
    page.Place(540, 110, 600, 130);
    if (ImGui::BeginCombo("##lens", preset.c_str())) {
        TypeAhead typeAhead;
        typeAhead.Selectable("Custom", lens < 0);
        for (int i = 0; i < 9; ++i)
            if (typeAhead.Selectable((std::to_string(LENSES[i]) + " mm").c_str(), lens == i))
                LiveSet(id, properties::CameraFov, 2.0f * std::atan(18.0f / LENSES[i]));
        ImGui::EndCombo();
    }
    page.Line(425, 130, 426, 210);
    float fov = properties::CameraFov.get(camera) * RAD_TO_DEG;
    page.Label(430, 130, 560, 150, "Field of view");
    if (page.EditSlider("##fov", ImVec4(535, 130, 595, 150), ImVec4(455, 150, 590, 170), &fov, 1.0f, 179.0f, "%.2f", 0.1f) &&
        fov > 0.0f && fov < 180.0f)
        LiveSet(id, properties::CameraFov, fov / RAD_TO_DEG);
    page.Label(430, 170, 560, 190, "Focal length");
    if (page.EditSlider("##focal", ImVec4(535, 170, 595, 190), ImVec4(455, 190, 590, 210), &focal, 1.0f, 250.0f, "%.2f", 0.5f) &&
        focal > 0.0f)
        LiveSet(id, properties::CameraFov, 2.0f * std::atan(18.0f / focal));

    // Orthographic Parameter.
    page.Label(420, 230, 610, 250, "Orthographic Parameter");
    page.Line(425, 250, 426, 290);
    float zoom = properties::CameraOrthoZoom.get(camera);
    page.Label(430, 250, 560, 270, "Ortho zoom");
    if (page.EditSlider("##orthozoom", ImVec4(560, 250, 600, 270), ImVec4(455, 270, 590, 290), &zoom, 0.0f, 0.5f, "%.3f", 0.001f))
        LiveSet(id, properties::CameraOrthoZoom, zoom);

    // Clipping.
    page.Label(620, 110, 750, 130, "Clipping");
    page.Line(625, 130, 626, 210);
    float nearClip = properties::CameraNearClip.get(camera), farClip = properties::CameraFarClip.get(camera);
    page.Label(640, 130, 730, 150, "Near clip");
    if (page.EditSlider("##near", ImVec4(735, 130, 795, 150), ImVec4(645, 150, 770, 170), &nearClip, 0.01f,
                        std::max(100.0f, farClip), "%.2f", 0.05f)) {
        // Documentation "Camera Setup": "Near clip must be smaller than far clip".
        if (nearClip < farClip) LiveSet(id, properties::CameraNearClip, nearClip);
        else m_State.Log(LogLevel::Warning, "Near clip must be smaller than far clip");
    }
    page.Label(640, 170, 730, 190, "Far clip");
    if (page.EditSlider("##far", ImVec4(735, 170, 795, 190), ImVec4(645, 190, 770, 210), &farClip, 1.0f,
                        std::max(5000.0f, farClip), "%.2f", 1.0f)) {
        if (farClip > nearClip) LiveSet(id, properties::CameraFarClip, farClip);
        else m_State.Log(LogLevel::Warning, "Near clip must be smaller than far clip");
    }
    page.End(800, 290);
}

// Light Setup as Dev's (Dev.exe sub_45F020).
void SetupWindows::LightPage(CKLight *light) {
    if (!light) return;
    const CK_ID id = light->GetID();
    const DevPage page;
    Entity3DPage(light, false, false);
    // Dev.exe sub_4584A0: the 3D Layout's list of the objects shown in Player
    // Mode (not the object's visibility).
    page.Line(25, 110, 26, 170);
    bool shown = m_State.playerShown.count(id) != 0, specular = properties::LightSpecular.get(light);
    if (page.Check("Show In Player Mode", 45, 114, shown))
        m_State.SetInLayoutList(m_State.playerShown, id, shown, "Show In Player Mode");
    page.Label(60, 112, 210, 128, "Show In Player Mode");
    if (page.Check("Specular", 45, 134, specular)) LiveSet(id, properties::LightSpecular, specular);
    page.Label(60, 132, 200, 148, "Specular");
    const char *types[] = {"Point", "Spot", "Directional"};
    const int lightType = properties::LightType.get(light);
    int type = lightType == VX_LIGHTSPOT ? 1 : lightType == VX_LIGHTDIREC ? 2 : 0;
    page.Label(20, 170, 120, 190, "Type");
    page.Place(120, 170, 210, 190);
    if (TypeAheadCombo("##type", &type, types, 3))
        LiveSet(id, properties::LightType, type == 1 ? VX_LIGHTSPOT : type == 2 ? VX_LIGHTDIREC : VX_LIGHTPOINT);
    if (CKTargetLight::Cast(light)) {
        CK_ID chosen = 0;
        page.Label(20, 190, 80, 210, "Target");
        if (ObjectCombo("##target", CKCID_3DENTITY, light->GetTarget(), &chosen, page.Place(80, 190, 210, 210)))
            Live(id, "Light Target", [this, chosen](CKObject *o) { CKLight::Cast(o)->SetTarget(Get<CK3dEntity>(m_State, chosen)); });
    }
    // The power (not in Dev's page; CKLight::SetPower).
    float power = properties::LightPower.get(light);
    page.Label(20, 215, 120, 235, "Power");
    page.Place(120, 215, 210, 235);
    if (ImGui::DragFloat("##power", &power, 0.01f, 0.0f, 10.0f, "%.2f")) LiveSet(id, properties::LightPower, power);

    // Influence.
    page.Label(220, 110, 300, 130, "Influence");
    bool active = properties::LightActive.get(light);
    if (page.Check("Active", 325, 114, active)) LiveSet(id, properties::LightActive, active);
    page.Label(340, 112, 400, 128, "Active");
    page.Line(225, 130, 226, 220);
    const VxColor color = properties::LightColor.get(light);
    float rgb[3] = {color.r, color.g, color.b};
    page.Label(235, 130, 305, 150, "Color");
    if (page.ColorBar("Color", 345, 130, 375, 150, rgb)) LiveSet(id, properties::LightColor, VxColor(rgb[0], rgb[1], rgb[2]));
    float range = properties::LightRange.get(light);
    // Documentation "Light Setup": "Range and Show Influence are not
    // available for Directional Type Lights", nor is the attenuation.
    const bool directional = type == 2;
    ImGui::BeginDisabled(directional);
    page.Label(235, 155, 315, 175, "Range");
    if (page.EditSlider("##range", ImVec4(345, 155, 395, 175), ImVec4(245, 175, 370, 195), &range, 0.0f,
                        std::max(200.0f, range), "%.1f", 0.5f))
        LiveSet(id, properties::LightRange, range);
    // The 3D Layout draws the light's cone or range (its own state).
    bool influence = m_State.layoutCones.count(id) != 0;
    if (page.Check("Show Influence", 245, 204, influence))
        m_State.SetInLayoutList(m_State.layoutCones, id, influence, "Show influence");
    page.Label(260, 200, 410, 220, "Show Influence");
    ImGui::EndDisabled();

    // Attenuation, and its curve (VIFunctionDrawer, sub_4622F0: the
    // intensity at a distance, 1 / (constant + linear d + quadratic d2),
    // none beyond the range).
    page.Label(420, 110, 610, 130, "Attenuation");
    page.Line(425, 130, 426, 260);
    ImGui::BeginDisabled(directional);
    float constant = properties::LightConstantAttenuation.get(light), linear = properties::LightLinearAttenuation.get(light),
          quadratic = properties::LightQuadraticAttenuation.get(light);
    page.Label(440, 130, 540, 150, "Constant");
    if (page.EditSlider("##constant", ImVec4(545, 130, 595, 150), ImVec4(445, 150, 570, 170), &constant, 0.0f, 10.0f, "%.3f"))
        LiveSet(id, properties::LightConstantAttenuation, constant);
    page.Label(440, 170, 540, 190, "Linear");
    if (page.EditSlider("##linear", ImVec4(545, 170, 595, 190), ImVec4(445, 190, 570, 210), &linear, 0.0f, 10.0f, "%.3f"))
        LiveSet(id, properties::LightLinearAttenuation, linear);
    page.Label(440, 210, 540, 230, "Quadratic");
    if (page.EditSlider("##quadratic", ImVec4(545, 210, 595, 230), ImVec4(445, 230, 570, 250), &quadratic, 0.0f, 10.0f, "%.3f"))
        LiveSet(id, properties::LightQuadraticAttenuation, quadratic);
    ImGui::EndDisabled();
    if (!directional) {
        const ImVec2 a = page.At(240, 230), b = page.At(400, 320);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, b, vi::Color(9));
        dl->AddRect(a, b, vi::Color(4));
        const float top = a.y + 1.0f, height = b.y - a.y - 2.0f;
        dl->AddLine(ImVec2(a.x, top), ImVec2(b.x, top), vi::Color(6));
        ImVec2 previous;
        const int steps = static_cast<int>(b.x - a.x);
        for (int i = 0; i <= steps; ++i) {
            const float d = steps ? range * i / steps : 0.0f;
            const float divisor = properties::LightConstantAttenuation.get(light) + properties::LightLinearAttenuation.get(light) * d +
                                  properties::LightQuadraticAttenuation.get(light) * d * d;
            const float intensity = divisor > 0.0f ? std::min(1.0f, 1.0f / divisor) : 1.0f;
            const ImVec2 point(a.x + i, top + (1.0f - intensity) * height);
            if (i) dl->AddLine(previous, point, IM_COL32(255, 0, 0, 255));
            previous = point;
        }
    }

    // Spot Cone (a spot's).
    page.Label(620, 110, 800, 130, "Spot Cone");
    page.Line(625, 130, 626, 260);
    ImGui::BeginDisabled(type != 1);
    float hot = properties::LightHotSpot.get(light) * RAD_TO_DEG, fall = properties::LightFallOff.get(light) * RAD_TO_DEG,
          shape = properties::LightFallOffShape.get(light);
    page.Label(640, 130, 760, 150, "Hot Spot");
    if (page.EditSlider("##hotspot", ImVec4(760, 130, 800, 150), ImVec4(655, 150, 790, 170), &hot, 0.0f, 180.0f, "%.1f", 0.1f))
        LiveSet(id, properties::LightHotSpot, hot / RAD_TO_DEG);
    page.Label(640, 170, 760, 190, "Fall Off");
    if (page.EditSlider("##falloff", ImVec4(760, 170, 800, 190), ImVec4(655, 190, 790, 210), &fall, 0.0f, 180.0f, "%.1f", 0.1f))
        LiveSet(id, properties::LightFallOff, fall / RAD_TO_DEG);
    page.Label(640, 210, 760, 230, "Fall Off Shape");
    if (page.EditSlider("##falloffshape", ImVec4(760, 210, 800, 230), ImVec4(655, 230, 790, 250), &shape, 0.0f, 10.0f, "%.2f"))
        LiveSet(id, properties::LightFallOffShape, shape);
    ImGui::EndDisabled();
    page.End(800, 320);
}

// General flags, Position, Size, Material, UV mapping (source rect), Parent.
void SetupWindows::Entity2DPage(CK2dEntity *entity, bool sprite) {
    if (!entity) return;
    const CK_ID id = entity->GetID();
    ImGui::BeginGroup();
    Group("General");
    auto flag = [&](const ObjectProperty<bool> &property) {
        bool v = property.get(entity);
        if (ImGui::Checkbox(property.label, &v)) LiveSet(id, property, v);
    };
    ImGui::Indent(20);
    flag(properties::Entity2DPickable);
    ImGui::SameLine();
    ImGui::TextDisabled("(Hold shift to pick it anyway)");
    const bool homogeneous = entity->IsHomogeneousCoordinates() != 0;
    flag(properties::Entity2DHomogeneous);
    flag(properties::Entity2DClipToCamera);
    flag(properties::Entity2DRatioOffset);
    ImGui::Unindent(20);
    Group("Position");
    ImGui::Indent(20);
    Vx2DVector position, size;
    entity->GetPosition(position, homogeneous);
    entity->GetSize(size, homogeneous);
    float xy[2] = {position.x, position.y};
    ImGui::SetNextItemWidth(200);
    if (ImGui::DragFloat2("X / Y", xy, homogeneous ? 0.001f : 1.0f, 0.0f, 0.0f, "%.4f"))
        Live(id, "Set Position", [xy, homogeneous](CKObject *o) {
            CK2dEntity::Cast(o)->SetPosition(Vx2DVector(xy[0], xy[1]), homogeneous);
        });
    int z = entity->GetZOrder();
    ImGui::SetNextItemWidth(80);
    if (ImGui::DragInt("Z Order", &z, 0.2f)) LiveSet(id, properties::Entity2DZOrder, z);
    flag(properties::Entity2DBackground);
    ImGui::Unindent(20);
    ImGui::Dummy(ImVec2(1, 10));
    Group("Size");
    ImGui::Indent(20);
    float wh[2] = {size.x, size.y};
    ImGui::SetNextItemWidth(200);
    if (ImGui::DragFloat2("Width / Height", wh, homogeneous ? 0.001f : 1.0f, 0.0f, 0.0f, "%.4f"))
        Live(id, "Set Size", [wh, homogeneous](CKObject *o) { CK2dEntity::Cast(o)->SetSize(Vx2DVector(wh[0], wh[1]), homogeneous); });
    ImGui::Unindent(20);
    ImGui::EndGroup();
    ImGui::SameLine(380);
    ImGui::BeginGroup();
    CK_ID chosen = 0;
    if (!sprite) {
        Group("Material:");
        ImGui::SameLine();
        if (ObjectCombo("##material", CKCID_MATERIAL, entity->GetMaterial(), &chosen))
            Live(id, "Material", [this, chosen](CKObject *o) { CK2dEntity::Cast(o)->SetMaterial(Get<CKMaterial>(m_State, chosen)); });
        // Documentation "2D Frame Setup": "Clicking on the icon opens the Material setup".
        ImGui::SameLine();
        ImGui::BeginDisabled(!entity->GetMaterial());
        if (ImGui::SmallButton(">##materialsetup") && entity->GetMaterial())
            m_State.events.Post(ShowObjectSetup{entity->GetMaterial()->GetID()});
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Show Material Setup");
        ImGui::Dummy(ImVec2(1, 50));
    }
    // 2D Frame "UVMapping" (homogeneous source rectangle); 2D Sprite
    // "Cropping" (the same CK2dEntity source rectangle, in pixels).
    Group(sprite ? "Cropping" : "UVMapping");
    ImGui::SameLine();
    flag(properties::Entity2DSourceRect);
    VxRect source;
    entity->GetSourceRect(source);
    float uv[4] = {source.left, source.top, source.right, source.bottom};
    ImGui::SetNextItemWidth(260);
    if (ImGui::DragFloat4("L / T / R / B", uv, sprite ? 1.0f : 0.005f, 0.0f, 0.0f, sprite ? "%.0f" : "%.3f"))
        Live(id, "UV Mapping", [uv](CKObject *o) {
            VxRect rect(uv[0], uv[1], uv[2], uv[3]);
            CK2dEntity::Cast(o)->SetSourceRect(rect);
        });
    ImGui::Dummy(ImVec2(1, 10));
    Group("Parent");
    ImGui::SameLine();
    if (ObjectCombo("##parent", CKCID_2DENTITY, entity->GetParent(), &chosen) && chosen != id)
        Live(id, "Parent", [this, chosen](CKObject *o) { CK2dEntity::Cast(o)->SetParent(Get<CK2dEntity>(m_State, chosen)); });
    // Show Parent Setup (Dev.exe sub_479A10, command 816).
    ImGui::SameLine();
    ImGui::BeginDisabled(!entity->GetParent());
    if (ImGui::SmallButton(">##parentsetup") && entity->GetParent())
        m_State.events.Post(ShowObjectSetup{entity->GetParent()->GetID()});
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Show Parent Setup");
    flag(properties::Entity2DClipToParent);
    // Stick Left / Right / Top / Bottom: keep the distance to the parent's borders.
    const ObjectProperty<bool> *sticks[] = {&properties::Entity2DStickLeft, &properties::Entity2DStickRight,
                                            &properties::Entity2DStickTop, &properties::Entity2DStickBottom};
    for (int k = 0; k < 4; ++k) {
        if (k % 2) ImGui::SameLine(150);
        flag(*sticks[k]);
    }
    ImGui::EndGroup();
}

// Dev 2D Sprite Setup (documentation "2D Sprite Setup"): the image (formats,
// system caching, slots, preview, save options, color key) above the 2D
// layout of the 2D Frame Setup (Cropping instead of Material / UVMapping).
void SetupWindows::SpritePage(Setup &setup, CKSprite *sprite) {
    if (!sprite) return;
    const CK_ID id = sprite->GetID();
    ImGui::BeginGroup();
    VxImageDescEx system;
    const bool described = sprite->GetSystemTextureDesc(system) != FALSE;
    CKBYTE *pixels = sprite->LockSurfacePtr(-1);
    if (pixels) sprite->ReleaseSurfacePtr(-1);
    if (described && pixels)
        ImGui::Text("System Memory Format   %dx%d %d bits  %s", system.Width, system.Height, system.BitsPerPixel,
                    PixelFormatName(VxImageDesc2PixelFormat(system)));
    else
        ImGui::TextUnformatted("System Memory Format   not in system memory");
    int format = static_cast<int>(sprite->GetDesiredVideoFormat());
    ImGui::TextUnformatted("Desired Video Format");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170);
    // The formats the render driver takes are marked "*".
    CKContext *videoContext = m_State.document->Context();
    auto formatLabel = [&](int f) {
        return std::string(PixelFormatName(static_cast<VX_PIXELFORMAT>(f))) +
               (DriverTakesFormat(videoContext, static_cast<VX_PIXELFORMAT>(f)) ? " *" : "");
    };
    if (ImGui::BeginCombo("##videoformat", formatLabel(format).c_str())) {
        TypeAhead typeAhead;
        for (int f = 1; f <= 23; ++f)
            if (typeAhead.Selectable(formatLabel(f).c_str(), f == format))
                LiveSet(id, properties::SpriteVideoFormat, f);
        ImGui::EndCombo();
    }
    ImGui::EndGroup();
    ImGui::SameLine(460);
    ImGui::BeginGroup();
    Group("Video Memory Information");
    {
        VxImageDescEx video;
        const bool inVideo = sprite->IsInVideoMemory() != FALSE;
        const bool videoDescribed = inVideo && sprite->GetVideoTextureDesc(video) != FALSE;
        VideoMemoryLines(inVideo, videoDescribed, video);
    }
    ImGui::EndGroup();
    ImGui::SameLine(760);
    ImGui::BeginGroup();
    const int slots = sprite->GetSlotCount(), slot = sprite->GetCurrentSlot();
    auto go = [&](const char *label, int to) {
        ImGui::BeginDisabled(to < 0 || to >= slots || to == slot);
        if (ImGui::Button(label, ImVec2(60, 0))) LiveSet(id, properties::SpriteCurrentSlot, to);
        ImGui::EndDisabled();
    };
    go("Prev", slot - 1);
    ImGui::SameLine();
    ImGui::Text("%d/ %d", slot, slots);
    ImGui::SameLine();
    go("Next", slot + 1);
    go("First", 0);
    ImGui::SameLine(124);
    go("Last", slots - 1);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    // "Add Sprites" (Dev.exe sub_479A10, command 500 as the slot menu's Add
    // new slot): an image file of the same size in a new slot.
    if (ImGui::Button("Add Sprites", ImVec2(170, 0))) {
        FileDialogOptions options;
        options.filter = L"Images (*.bmp;*.tga;*.jpg;*.png;*.tif)\0*.bmp;*.tga;*.jpg;*.jpeg;*.png;*.tif;*.tiff\0All files (*.*)\0*.*\0";
        FlushPending();
        m_State.RequestFile(options, [this, id](const FileDialogResult &selection) {
            if (selection.status != FileDialogResult::Status::Selected) return;
            Report(AddSlotImages(*m_State.document, id, selection.paths), "Add Texture");
        });
    }
    ImGui::BeginDisabled(slots <= 1);
    if (ImGui::Button("Remove Current Slot", ImVec2(170, 0))) setup.confirmRemoveSlot = true;
    ImGui::EndDisabled();
    ImGui::EndGroup();
    ImGui::Separator();
    // Save Options and Color Key Transparency, left of the preview.
    ImGui::BeginGroup();
    Group("Save Options");
    const char *options[] = {"Raw Data", "External", "Specific Format", "Global Settings", "Original File"};
    int option = std::max(0, std::min(4, static_cast<int>(sprite->GetSaveOptions())));
    ImGui::SetNextItemWidth(160);
    if (TypeAheadCombo("##saveoptions", &option, options, 5))
        LiveSet(id, properties::SpriteSaveOptions, option);
    if (option == CKTEXTURE_IMAGEFORMAT) SaveFormatControls(sprite);
    ImGui::Dummy(ImVec2(1, 6));
    Group("Color Key Transparency");
    bool key = sprite->IsTransparent() != 0;
    if (ImGui::Checkbox("Active", &key)) LiveSet(id, properties::SpriteTransparent, key);
    const CKDWORD keyColor = sprite->GetTransparentColor();
    float rgb[3] = {((keyColor >> 16) & 0xFF) / 255.0f, ((keyColor >> 8) & 0xFF) / 255.0f, (keyColor & 0xFF) / 255.0f};
    if (ImGui::ColorEdit3("Picking", rgb, ImGuiColorEditFlags_NoInputs))
        LiveSet(id, properties::SpriteTransparentColor, VxColor(rgb[0], rgb[1], rgb[2], 1.0f));
    ImGui::SameLine();
    if (ImGui::Button(setup.pickingColor ? "Picking..." : "Pick")) setup.pickingColor = !setup.pickingColor;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick a color on the sprite");
    ImGui::TextDisabled("Activating the color key destroys the alpha channel.");
    ImGui::EndGroup();
    // Preview of the current slot.
    ImGui::SameLine(400);
    const int width = sprite->GetWidth(), height = sprite->GetHeight();
    const unsigned long long colorKey = static_cast<unsigned long long>(id) << 1;
    if (setup.shownTexture != id || setup.shownSlot != slot || ++setup.textureFrames > 120) {
        setup.shownTexture = id;
        setup.shownSlot = slot;
        setup.textureFrames = 0;
        if (CKBYTE *pixels = sprite->LockSurfacePtr(slot)) {
            PixelTexture(colorKey, width, height, pixels, width * 4, false);
            sprite->ReleaseSurfacePtr(slot);
        }
    }
    SlotPreview(setup, sprite, colorKey, width, height, 220.0f, false);
    ImGui::Separator();
    Entity2DPage(sprite, true);
}

void SetupWindows::SlotPreview(Setup &setup, CKObject *bitmap, unsigned long long key, int width, int height, float side,
                               bool alpha) {
    const float fit = width > 0 && height > 0 ? std::min(side / width, side / height) : 1.0f;
    const float scale = setup.zoom > 0.0f ? setup.zoom : fit;
    // Scrolled within the image zoomed (none when it fits).
    const ImVec2 room(std::max(0.0f, width * scale - side), std::max(0.0f, height * scale - side));
    setup.scroll = ImVec2(std::max(0.0f, std::min(room.x, setup.scroll.x)), std::max(0.0f, std::min(room.y, setup.scroll.y)));
    const ImVec2 scroll = setup.scroll;
    ImGui::PushID(alpha ? "alpha" : "image");
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, ImVec2(at.x + side + 2, at.y + side + 2), IM_COL32(255, 255, 255, 255));
    dl->AddRect(at, ImVec2(at.x + side + 2, at.y + side + 2), IM_COL32(0, 0, 0, 255));
    if (ImTextureID image = CachedPixelTexture(key)) {
        dl->PushClipRect(ImVec2(at.x + 1, at.y + 1), ImVec2(at.x + 1 + side, at.y + 1 + side), true);
        dl->AddImage(image, ImVec2(at.x + 1 - scroll.x, at.y + 1 - scroll.y),
                     ImVec2(at.x + 1 - scroll.x + width * scale, at.y + 1 - scroll.y + height * scale));
        dl->PopClipRect();
    }
    ImGui::InvisibleButton("##preview", ImVec2(side + 2, side + 2),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    // Documentation "Texture Setup": the right button held and moved zooms
    // (7% to 5000%), the left one moves a zoomed image.
    if (ImGui::IsItemActivated()) setup.previewDragged = false;
    if (ImGui::IsItemActive() && !setup.pickingColor) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) && (delta.x != 0.0f || delta.y != 0.0f)) {
            setup.previewDragged = true;
            setup.zoom = std::max(0.07f, std::min(50.0f, scale * (1.0f - delta.y * 0.01f + delta.x * 0.01f)));
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (delta.x != 0.0f || delta.y != 0.0f)) {
            setup.scroll = ImVec2(setup.scroll.x - delta.x, setup.scroll.y - delta.y);
        }
    }
    // Picking a color (the image's preview only): the pixel under the mouse
    // while the button is held is the color key (one undo step a drag).
    if (setup.pickingColor && !alpha && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        width > 0 && height > 0) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const int px = static_cast<int>((mouse.x - at.x - 1.0f + scroll.x) / scale),
                  py = static_cast<int>((mouse.y - at.y - 1.0f + scroll.y) / scale);
        if (px >= 0 && py >= 0 && px < width && py < height) {
            CKTexture *texture = CKTexture::Cast(bitmap);
            CKSprite *sprite = CKSprite::Cast(bitmap);
            const int slot = texture ? texture->GetCurrentSlot() : sprite->GetCurrentSlot();
            const CKDWORD pixel = texture ? texture->GetPixel(px, py, slot) : sprite->GetPixel(px, py, slot);
            const VxColor color(static_cast<int>((pixel >> 16) & 0xFF), static_cast<int>((pixel >> 8) & 0xFF),
                                static_cast<int>(pixel & 0xFF));
            if (texture) LiveSet(bitmap->GetID(), properties::TextureTransparentColor, color);
            else LiveSet(bitmap->GetID(), properties::SpriteTransparentColor, color);
        }
    }
    if (setup.pickingColor && !alpha && ImGui::IsItemDeactivated()) setup.pickingColor = false;
    // A Data Resources image dropped (Dev.exe sub_48AC30): on the image, the
    // current slot's (Ctrl or Shift: a new slot); on the alpha, the alpha
    // channel's (Ctrl or Shift: which channels first).
    if (ImGui::BeginDragDropTarget()) {
        const bool texture = CKTexture::Cast(bitmap) != nullptr;
        const ImGuiIO &io = ImGui::GetIO();
        const bool more = io.KeyCtrl || io.KeyShift;
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(RESOURCE_FILE_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            ResourceFilePayload file;
            std::memcpy(&file, payload->Data, sizeof(file));
            const char *what = texture ? "texture" : "sprite";
            if (!payload->IsDelivery()) {
                if (alpha) ImGui::SetTooltip(more ? "Extended replace texture alpha channel with image" : "Replace texture alpha channel with image");
                else ImGui::SetTooltip("%s", more ? (std::string("Add image to ") + what).c_str()
                                                  : (std::string("Replace ") + what + " slot with image").c_str());
            } else if (alpha && texture) {
                if (more) {
                    setup.droppedAlpha = file.path;
                    ImGui::OpenPopup("alphachannels");
                } else {
                    FlushPending();
                    Report(ReplaceSlotAlpha(*m_State.document, bitmap->GetID(), file.path), "Replace alpha");
                }
            } else if (!alpha) {
                FlushPending();
                const EditResult result = more ? AddSlotImages(*m_State.document, bitmap->GetID(), {file.path})
                                               : ReplaceSlotImage(*m_State.document, bitmap->GetID(), file.path);
                Report(result, more ? "Add new slot" : "Replace slot");
                if (result.ok)
                    m_State.Log(LogLevel::Info, "Image file '" + AnsiToUtf8(file.path) + "' loaded on " + what + " '" +
                                                    AnsiToUtf8(bitmap->GetName() ? bitmap->GetName() : "") + "'.");
            }
            setup.textureFrames = 1000;
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopup("alphachannels")) {
        static const char *const CHANNELS[] = {"Use Blue channel only", "Use Green channel only", "Use Red channel only",
                                               "Mix the channels"};
        static const int MASKS[] = {2, 4, 8, 14};
        for (int c = 0; c < 4; ++c)
            if (ImGui::MenuItem(CHANNELS[c]) && !setup.droppedAlpha.empty()) {
                FlushPending();
                Report(ReplaceSlotAlpha(*m_State.document, bitmap->GetID(), setup.droppedAlpha, MASKS[c]), "Replace alpha");
                setup.textureFrames = 1000;
            }
        ImGui::EndPopup();
    }
    if (setup.pickingColor && !alpha && ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    // The slot menu on a right click (not after a right drag's zoom).
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !setup.previewDragged)
        ImGui::OpenPopup("slotmenu");
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) setup.previewDragged = false;
    if (ImGui::BeginPopup("slotmenu")) {
        SlotMenu(setup, bitmap, alpha);
        ImGui::EndPopup();
    }
    if (!alpha) {
        SlotKeys(setup, bitmap);
        RemoveSlotDialog(setup, bitmap);
    }
    ImGui::PopID();
}

void SetupWindows::SaveFormatControls(CKObject *bitmap) {
    CKTexture *texture = CKTexture::Cast(bitmap);
    CKSprite *sprite = CKSprite::Cast(bitmap);
    if (!texture && !sprite) return;
    CKPluginManager *plugins = CKGetPluginManager();
    CKBitmapProperties *current = texture ? texture->GetSaveFormat() : sprite->GetSaveFormat();
    const std::string extension = current ? current->m_Ext.m_Data : "";
    std::vector<std::string> extensions, labels;
    for (int i = 0; plugins && i < plugins->GetPluginCount(CKPLUGIN_BITMAP_READER); ++i) {
        CKPluginEntry *reader = plugins->GetPluginInfo(CKPLUGIN_BITMAP_READER, i);
        if (!reader || !reader->m_PluginInfo.m_Extension.m_Data[0]) continue;
        if (reader->m_ReadersInfo && !(reader->m_ReadersInfo->m_ReaderFlags & CK_DATAREADER_FILESAVE)) continue;
        extensions.push_back(reader->m_PluginInfo.m_Extension.m_Data);
        labels.push_back(AnsiToUtf8(std::string(reader->m_PluginInfo.m_Summary.CStr() ? reader->m_PluginInfo.m_Summary.CStr() : "") +
                                    " (" + reader->m_PluginInfo.m_Extension.m_Data + ")"));
    }
    if (extensions.empty()) return;
    int chosen = -1;
    for (size_t i = 0; i < extensions.size(); ++i)
        if (_stricmp(extensions[i].c_str(), extension.c_str()) == 0) chosen = static_cast<int>(i);
    std::vector<const char *> items;
    for (const std::string &label : labels) items.push_back(label.c_str());
    ImGui::SetNextItemWidth(160);
    int picked = chosen;
    if (TypeAheadCombo("Format", &picked, items.data(), static_cast<int>(items.size())) && picked != chosen) {
        const std::string ext = extensions[picked];
        Live(bitmap->GetID(), "Image Format", [ext](CKObject *o) {
            std::vector<char> bytes = MakeSaveFormat(o->GetCKContext(), ext);
            if (bytes.empty()) return;
            CKBitmapProperties *properties = reinterpret_cast<CKBitmapProperties *>(bytes.data());
            if (CKTexture *t = CKTexture::Cast(o)) t->SetSaveFormat(properties);
            else if (CKSprite *s = CKSprite::Cast(o)) s->SetSaveFormat(properties);
        });
        FlushPending();
        // SetSaveFormat replaces the storage borrowed by current. Draw its
        // options from the new format on the next frame.
        return;
    }
    // Dev's Image Saver: "Alpha Channel is also saved." / "This mode does not
    // support Alpha Channel." (CKBitmapReader::IsAlphaSaved).
    if (current && plugins) {
        CKFileExtension ext(const_cast<char *>(extension.c_str()));
        if (CKBitmapReader *reader = plugins->GetBitmapReader(ext)) {
            ImGui::TextDisabled("%s", reader->IsAlphaSaved(current) ? "Alpha Channel is also saved."
                                                                    : "This mode does not support Alpha Channel.");
            reader->Release();
        }
    }
    if (!current || !plugins) return;
    CKContext *context = m_State.document->Context();
    CKFileExtension ext(const_cast<char *>(extension.c_str()));
    // Format Options: the reader's option parameter (Dev.exe sub_5889B0,
    // SetReaderOptionData on a copy of the format).
    CKGUID optionsType(0, 0);
    for (int i = 0; i < plugins->GetPluginCount(CKPLUGIN_BITMAP_READER); ++i) {
        CKPluginEntry *reader = plugins->GetPluginInfo(CKPLUGIN_BITMAP_READER, i);
        if (reader && reader->m_ReadersInfo && _stricmp(reader->m_PluginInfo.m_Extension.m_Data, extension.c_str()) == 0)
            optionsType = reader->m_ReadersInfo->m_SettingsParameterGuid;
    }
    void *optionData = reinterpret_cast<char *>(current) + sizeof(CKBitmapProperties);
    if (optionsType != CKGUID(0, 0) && ImGui::Button("Format Options...")) {
        if (CKParameterOut *options = plugins->GetReaderOptionData(context, optionData, ext, &current->m_ReaderGuid)) {
            m_FormatBox.Clear();
            if (CKParameter *old = CKParameter::Cast(context->GetObject(m_FormatParameter))) context->DestroyObject(old);
            m_FormatParameter = options->GetID();
            if (m_FormatBox.Add(options, "Format Options")) m_OpenFormatOptions = true;
        }
    }
    if (m_OpenFormatOptions) {
        m_OpenFormatOptions = false;
        ImGui::OpenPopup("Format Options###formatoptions");
    }
    const DialogAction action = EditParameterValueDialog("Format Options###formatoptions", "Format Options", m_FormatBox);
    if (action != DialogAction::None) {
        CKParameter *edited = m_FormatBox.Count() ? m_FormatBox.Work(0) : nullptr;
        if (action == DialogAction::Ok && edited) {
            const std::string wanted = extension;
            CKParameterOut *value = CKParameterOut::Cast(edited);
            Live(bitmap->GetID(), "Format Options", [this, wanted, value](CKObject *o) {
                CKTexture *t = CKTexture::Cast(o);
                CKSprite *s = CKSprite::Cast(o);
                CKBitmapProperties *format = t ? t->GetSaveFormat() : s ? s->GetSaveFormat() : nullptr;
                if (!format || !value) return;
                CKBitmapProperties *copy = CKCopyBitmapProperties(format);
                if (!copy) return;
                CKFileExtension e(const_cast<char *>(wanted.c_str()));
                CKGetPluginManager()->SetReaderOptionData(o->GetCKContext(), reinterpret_cast<char *>(copy) + sizeof(CKBitmapProperties),
                                                          value, e, &copy->m_ReaderGuid);
                if (t) t->SetSaveFormat(copy);
                else s->SetSaveFormat(copy);
                CKDeleteBitmapProperties(copy);
            });
            FlushPending();
            m_FormatInfoKey.erase(bitmap->GetID());
        }
        m_FormatBox.Clear();
        if (CKParameter *old = CKParameter::Cast(context->GetObject(m_FormatParameter))) context->DestroyObject(old);
        m_FormatParameter = 0;
    }
    // "%d x %d x %d (%d %s)" and "%d %s  (Save:%.1f ms  Read: %.1f ms)": the
    // current slot, its raw size; written by the reader in a state chunk
    // and read back (into a buffer of its own), the chunk's size and times.
    CKTexture *t = CKTexture::Cast(bitmap);
    CKSprite *s = CKSprite::Cast(bitmap);
    const int slot = t ? t->GetCurrentSlot() : s->GetCurrentSlot();
    const std::string key = extension + "/" + std::to_string(slot) + "/" + std::to_string(t ? t->GetWidth() : s->GetWidth());
    if (m_FormatInfoKey[bitmap->GetID()] != key) {
        m_FormatInfoKey[bitmap->GetID()] = key;
        std::string info;
        VxImageDescEx desc;
        const bool described = t ? t->GetImageDesc(desc) != FALSE : s->GetSystemTextureDesc(desc) != FALSE;
        CKBYTE *pixels = t ? t->LockSurfacePtr(slot) : s->LockSurfacePtr(slot);
        CKBitmapReader *reader = plugins->GetBitmapReader(ext);
        if (described && pixels && reader) {
            desc.Image = pixels;
            auto sized = [](int bytes) {
                char text[64];
                if (bytes > 1024) std::snprintf(text, sizeof(text), "%d KBytes", bytes >> 10);
                else std::snprintf(text, sizeof(text), "%d Bytes", bytes);
                return std::string(text);
            };
            char line[160];
            std::snprintf(line, sizeof(line), "%d x %d x %d (%s)", desc.Width, desc.Height, desc.BitsPerPixel,
                          sized(desc.BytesPerLine * desc.Height).c_str());
            info = line;
            CKStateChunk *chunk = CreateCKStateChunk(1234, nullptr);
            VxTimeProfiler timer;
            chunk->StartWrite();
            chunk->WriteIdentifier(1234);
            chunk->WriteReaderBitmap(desc, reader, current);
            chunk->CloseChunk();
            const float save = timer.Current();
            const int size = chunk->GetDataSize();
            std::vector<CKBYTE> back(static_cast<size_t>(desc.BytesPerLine) * desc.Height);
            VxImageDescEx read = desc;
            read.Image = back.data();
            timer.Reset();
            chunk->StartRead();
            chunk->SeekIdentifier(1234);
            chunk->ReadReaderBitmap(read);
            const float load = timer.Current();
            DeleteCKStateChunk(chunk);
            std::snprintf(line, sizeof(line), "%s  (Save:%.1f ms  Read: %.1f ms)", sized(size).c_str(), save, load);
            info += std::string("\n") + line;
        }
        if (pixels && s) s->ReleaseSurfacePtr(slot);
        if (reader) reader->Release();
        m_FormatInfo[bitmap->GetID()] = info;
    }
    if (!m_FormatInfo[bitmap->GetID()].empty()) ImGui::TextDisabled("%s", m_FormatInfo[bitmap->GetID()].c_str());
}

void SetupWindows::RunSlotCommand(Setup &setup, CKObject *bitmap, SlotCommand command) {
    const CK_ID id = bitmap->GetID();
    CKTexture *texture = CKTexture::Cast(bitmap);
    CKSprite *sprite = CKSprite::Cast(bitmap);
    const char *what = texture ? "texture" : "sprite";
    const std::string name = AnsiToUtf8(bitmap->GetName() ? bitmap->GetName() : "");
    static const wchar_t *const IMAGES =
        L"Images (*.bmp;*.tga;*.jpg;*.png;*.tif)\0*.bmp;*.tga;*.jpg;*.jpeg;*.png;*.tif;*.tiff\0All files (*.*)\0*.*\0";
    auto act = [&](const char *label, const EditResult &result) {
        Report(result, label);
        setup.textureFrames = 1000;
    };
    auto save = [&](bool alphaChannel) {
        FileDialogOptions options;
        options.save = true;
        options.filter = L"Bitmap (*.bmp)\0*.bmp\0Targa (*.tga)\0*.tga\0All files (*.*)\0*.*\0";
        options.extension = L"bmp";
        FlushPending();
        const int requestedSlot = texture ? texture->GetCurrentSlot() : sprite->GetCurrentSlot();
        m_State.RequestFile(options, [this, id, alphaChannel, requestedSlot](const FileDialogResult &selection) {
            if (selection.status != FileDialogResult::Status::Selected) return;
            CKObject *target = m_State.document->IsDiscarded(id) ? nullptr : m_State.document->Context()->GetObject(id);
            const std::string &path = selection.paths.front();
            if (target && SaveSlotImage(target, path, alphaChannel, requestedSlot))
                m_State.Log(LogLevel::Info, "Image saved to '" + path + "'.");
            else m_State.Log(LogLevel::Warning, "Cannot save the image");
        });
    };
    // A movie's frames are not slots to change (Dev: "Cannot modify avi").
    const char *movie = texture ? texture->GetMovieFileName() : nullptr;
    const bool changing = command == SlotCommand::Add || command == SlotCommand::Replace || command == SlotCommand::ReplaceAlpha;
    if (changing && movie && *movie) {
        m_State.Log(LogLevel::Warning, "Cannot modify avi");
        return;
    }
    const int slots = texture ? texture->GetSlotCount() : sprite->GetSlotCount();
    const int slot = texture ? texture->GetCurrentSlot() : sprite->GetCurrentSlot();
    const float shown = setup.zoom > 0.0f ? setup.zoom : 0.0f;
    switch (command) {
    case SlotCommand::Add:
    case SlotCommand::Replace:
    case SlotCommand::ReplaceAlpha: {
        if (command == SlotCommand::ReplaceAlpha && !texture) break;
        FileDialogOptions options;
        options.filter = IMAGES;
        FlushPending();
        m_State.RequestFile(options, [this, id, slot, command](const FileDialogResult &selection) {
            if (selection.status != FileDialogResult::Status::Selected) return;
            const EditResult result = command == SlotCommand::Add
                ? AddSlotImages(*m_State.document, id, selection.paths)
                : command == SlotCommand::Replace
                    ? ReplaceSlotImage(*m_State.document, id, selection.paths.front(), slot)
                    : ReplaceSlotAlpha(*m_State.document, id, selection.paths.front(), 14, slot);
            Report(result, "Image file");
            for (Setup &open : m_Doc.setups) if (open.object == id) open.textureFrames = 1000;
        }, command == SlotCommand::Add);
        break;
    }
    case SlotCommand::Save: save(false); break;
    case SlotCommand::SaveAlpha: save(true); break;
    // n / p wrap around (sub_487640 11, 12).
    case SlotCommand::Next:
    case SlotCommand::Previous: {
        if (slots <= 1) break;
        const int to = command == SlotCommand::Next ? (slot + 1) % slots : (slot + slots - 1) % slots;
        if (texture) LiveSet(id, properties::TextureCurrentSlot, to);
        else LiveSet(id, properties::SpriteCurrentSlot, to);
        FlushPending();
        setup.textureFrames = 1000;
        break;
    }
    // The zoom (Dev's VIStaticImage::ManageZoom): twice or half the scale
    // shown, 1:1, Best Fit.
    case SlotCommand::ZoomIn: setup.zoom = std::min(50.0f, (shown > 0.0f ? shown : 1.0f) * 2.0f); break;
    case SlotCommand::ZoomOut: setup.zoom = std::max(0.07f, (shown > 0.0f ? shown : 1.0f) * 0.5f); break;
    case SlotCommand::Zoom1: setup.zoom = 1.0f; break;
    case SlotCommand::ZoomFit: setup.zoom = 0.0f; break;
    }
}

// The keys with the Setup focused (Dev handles them on the key's release).
void SetupWindows::SlotKeys(Setup &setup, CKObject *bitmap) {
    const ImGuiIO &io = ImGui::GetIO();
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || io.WantTextInput || io.KeyCtrl || io.KeyAlt ||
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return;
    const bool texture = CKTexture::Cast(bitmap) != nullptr;
    auto released = [](ImGuiKey key) { return ImGui::IsKeyReleased(key); };
    if (released(ImGuiKey_O)) RunSlotCommand(setup, bitmap, SlotCommand::Add);
    else if (released(ImGuiKey_R)) RunSlotCommand(setup, bitmap, io.KeyShift && texture ? SlotCommand::ReplaceAlpha : SlotCommand::Replace);
    else if (released(ImGuiKey_S)) RunSlotCommand(setup, bitmap, SlotCommand::Save);
    else if (released(ImGuiKey_N)) RunSlotCommand(setup, bitmap, io.KeyShift ? SlotCommand::Previous : SlotCommand::Next);
    else if (released(ImGuiKey_P)) RunSlotCommand(setup, bitmap, SlotCommand::Previous);
    else if (released(ImGuiKey_KeypadAdd) || (released(ImGuiKey_Equal) && io.KeyShift))
        RunSlotCommand(setup, bitmap, SlotCommand::ZoomIn);
    else if (released(ImGuiKey_KeypadSubtract) || released(ImGuiKey_Minus)) RunSlotCommand(setup, bitmap, SlotCommand::ZoomOut);
    else if (released(ImGuiKey_KeypadDivide) || released(ImGuiKey_Slash)) RunSlotCommand(setup, bitmap, SlotCommand::Zoom1);
    else if (released(ImGuiKey_KeypadMultiply) || (released(ImGuiKey_8) && io.KeyShift))
        RunSlotCommand(setup, bitmap, SlotCommand::ZoomFit);
}

// Remove slot (the menu, the Remove Current Slot button) asks first
// (Dev.exe sub_487640 1: 1374 / 1308, "Warning!").
void SetupWindows::RemoveSlotDialog(Setup &setup, CKObject *bitmap) {
    if (setup.confirmRemoveSlot) {
        setup.confirmRemoveSlot = false;
        ImGui::OpenPopup("Warning!###removeslot");
    }
    if (!ImGui::BeginPopupModal("Warning!###removeslot", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(CKTexture::Cast(bitmap) ? "Remove current texture slot ?" : "Remove current sprite slot?");
    if (ImGui::Button("Yes", ImVec2(80, 0))) {
        FlushPending();
        Report(RemoveCurrentSlot(*m_State.document, bitmap->GetID()), "Remove slot");
        setup.textureFrames = 1000;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("No", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void SetupWindows::SlotMenu(Setup &setup, CKObject *bitmap, bool alpha) {
    const CK_ID id = bitmap->GetID();
    CKTexture *texture = CKTexture::Cast(bitmap);
    CKSprite *sprite = CKSprite::Cast(bitmap);
    const int slots = texture ? texture->GetSlotCount() : sprite->GetSlotCount();
    const int width = texture ? texture->GetWidth() : sprite->GetWidth();
    const int height = texture ? texture->GetHeight() : sprite->GetHeight();
    const char *what = texture ? "texture" : "sprite";
    const std::string name = AnsiToUtf8(bitmap->GetName() ? bitmap->GetName() : "");
    static const wchar_t *const IMAGES =
        L"Images (*.bmp;*.tga;*.jpg;*.png;*.tif)\0*.bmp;*.tga;*.jpg;*.jpeg;*.png;*.tif;*.tiff\0All files (*.*)\0*.*\0";
    auto reshow = [&] { setup.textureFrames = 1000; };
    auto act = [&](const char *label, const EditResult &result) {
        Report(result, label);
        reshow();
    };
    auto save = [&](bool alphaChannel) {
        FileDialogOptions options;
        options.save = true;
        options.filter = L"Bitmap (*.bmp)\0*.bmp\0Targa (*.tga)\0*.tga\0All files (*.*)\0*.*\0";
        options.extension = L"bmp";
        FlushPending();
        const int requestedSlot = texture ? texture->GetCurrentSlot() : sprite->GetCurrentSlot();
        m_State.RequestFile(options, [this, id, alphaChannel, requestedSlot](const FileDialogResult &selection) {
            if (selection.status != FileDialogResult::Status::Selected) return;
            CKObject *target = m_State.document->IsDiscarded(id) ? nullptr : m_State.document->Context()->GetObject(id);
            const std::string &path = selection.paths.front();
            if (target && SaveSlotImage(target, path, alphaChannel, requestedSlot))
                m_State.Log(LogLevel::Info, "Image saved to '" + path + "'.");
            else m_State.Log(LogLevel::Warning, "Cannot save the image");
        });
    };
    if (alpha) {
        if (ImGui::MenuItem("Replace alpha...", "Shift-R")) RunSlotCommand(setup, bitmap, SlotCommand::ReplaceAlpha);
        ImGui::Separator();
        auto op = [&](const char *label, SlotImageOp kind) {
            if (ImGui::MenuItem(label)) {
                FlushPending();
                act(label, EditSlotImage(*m_State.document, id, kind));
            }
        };
        op("Set alpha to opaque", SlotImageOp::AlphaOpaque);
        op("Set alpha to translucent", SlotImageOp::AlphaTranslucent);
        op("Set alpha to transparent", SlotImageOp::AlphaTransparent);
        op("Invert alpha", SlotImageOp::InvertAlpha);
        if (ImGui::MenuItem("Save Alpha Channel...")) RunSlotCommand(setup, bitmap, SlotCommand::SaveAlpha);
        return;
    }
    if (ImGui::MenuItem("Add new slot...", "O")) RunSlotCommand(setup, bitmap, SlotCommand::Add);
    if (ImGui::MenuItem("Replace slot...", "R")) RunSlotCommand(setup, bitmap, SlotCommand::Replace);
    if (ImGui::MenuItem("Remove slot", nullptr, false, slots > 1)) setup.confirmRemoveSlot = true;
    if (ImGui::MenuItem("Reload slot")) {
        FlushPending();
        act("Reload slot", ReloadSlotImage(*m_State.document, id));
    }
    if (ImGui::MenuItem("Save slot...", "S")) RunSlotCommand(setup, bitmap, SlotCommand::Save);
    if (texture) {
        if (ImGui::MenuItem("Resize Slot...")) {
            setup.openResize = true;
            setup.resizeWidth = width;
            setup.resizeHeight = height;
        }
        auto op = [&](const char *label, SlotImageOp kind) {
            if (ImGui::MenuItem(label)) {
                FlushPending();
                act(label, EditSlotImage(*m_State.document, id, kind));
            }
        };
        op("Invert Slot", SlotImageOp::InvertColors);
        op("HeightMap => NormalMap", SlotImageOp::NormalMap);
        op("HeightMap => BumpMap", SlotImageOp::BumpMap);
    }
    ImGui::Separator();
    const float shown = setup.zoom > 0.0f ? setup.zoom : 0.0f;
    if (ImGui::MenuItem("Zoom In", "+", false, shown == 0.0f || shown < 50.0f)) RunSlotCommand(setup, bitmap, SlotCommand::ZoomIn);
    if (ImGui::MenuItem("Zoom Out", "-", false, shown == 0.0f || shown > 0.07f))
        RunSlotCommand(setup, bitmap, SlotCommand::ZoomOut);
    if (ImGui::MenuItem("Zoom 1:1", "/")) RunSlotCommand(setup, bitmap, SlotCommand::Zoom1);
    if (ImGui::MenuItem("Zoom Best Fit", "*")) RunSlotCommand(setup, bitmap, SlotCommand::ZoomFit);
}

// Material Setup as Dev's (Dev.exe sub_464E20: the rects are its SetRect's;
// sub_46A670 what each Mode shows).
void SetupWindows::MaterialPage(Setup &setup, CKMaterial *material) {
    if (!material) return;
    const CK_ID id = material->GetID();
    CKContext *context = m_State.document->Context();
    const DevPage page;
    // Mode (sub_4634E0 712): Opaque, Transparent, Mask set the blend, Z and
    // alpha test settings; Custom shows them to edit. The combo keeps Custom
    // once chosen (sub_46A670).
    int mode = properties::MaterialMode.get(material);
    if (setup.materialCustom) mode = 3;
    // The colors' hue bars (each keeps its alpha; the diffuse's is the Alpha
    // slider).
    page.Line(22, 9, 23, 121);
    const ObjectProperty<VxColor> *const COLORS[] = {&properties::MaterialAmbient, &properties::MaterialDiffuse,
                                                     &properties::MaterialSpecular, &properties::MaterialEmissive};
    for (int i = 0; i < 4; ++i) {
        const float y = 10.0f + 30.0f * i;
        const VxColor c = COLORS[i]->get(material);
        float rgb[3] = {c.r, c.g, c.b};
        page.Label(30, y, 90, y + 20, COLORS[i]->label);
        if (page.ColorBar(COLORS[i]->label, 90, y, 120, y + 20, rgb)) LiveSet(id, *COLORS[i], VxColor(rgb[0], rgb[1], rgb[2]));
    }
    // Alpha with Transparent and Custom (sub_469C90).
    if (mode == 1 || mode == 3) {
        int alpha = properties::MaterialAlpha.get(material);
        page.Label(123, 40, 160, 60, "Alpha");
        page.Place(160, 40, 225, 60);
        if (ImGui::SliderInt("##alpha", &alpha, 0, 255)) LiveSet(id, properties::MaterialAlpha, alpha);
    }
    float power = properties::MaterialPower.get(material);
    page.Label(123, 70, 160, 90, "Power");
    page.Place(160, 70, 225, 90);
    if (ImGui::SliderFloat("##power", &power, 0.0f, 100.0f, "%.0f")) LiveSet(id, properties::MaterialPower, power);
    page.Line(3, 130, 435, 131);
    page.Line(230, 9, 231, 120);
    const char *modes[] = {"Opaque", "Transparent", "Mask", "Custom"};
    page.Label(235, 10, 350, 30, "Mode");
    page.Place(350, 10, 440, 30);
    const int before = mode;
    if (TypeAheadCombo("##mode", &mode, modes, 4) && mode != before) {
        setup.materialCustom = mode == 3;
        if (mode != 3) LiveSet(id, properties::MaterialMode, mode);
    }
    page.Line(235, 40, 440, 41);
    bool twoSided = properties::MaterialBothSided.get(material);
    page.Label(235, 52, 350, 68, "Both Sided");
    if (page.Check("##bothsided", 350, 55, twoSided)) LiveSet(id, properties::MaterialBothSided, twoSided);
    const char *fills[] = {"Point", "Wireframe", "Solid"};
    int fill = std::max(0, std::min(2, properties::MaterialFillMode.get(material) - 1));
    page.Label(235, 70, 350, 90, "Fill Mode");
    page.Place(350, 70, 440, 90);
    if (TypeAheadCombo("##fillmode", &fill, fills, 3)) LiveSet(id, properties::MaterialFillMode, fill + 1);
    const char *shades[] = {"Flat", "Gouraud", "Phong"};
    int shade = std::max(0, std::min(2, properties::MaterialShadeMode.get(material) - 1));
    page.Label(235, 90, 350, 110, "Shade Mode");
    page.Place(350, 90, 440, 110);
    if (TypeAheadCombo("##shademode", &shade, shades, 3)) LiveSet(id, properties::MaterialShadeMode, shade + 1);

    // Mask: the alpha reference; Custom: the alpha test, blend and Z
    // settings too (sub_469C90, sub_469F00, sub_46A440).
    static const char *const BLENDS[] = {"Zero", "One", "Src Color", "Inv Src Color", "Src Alpha", "Inv Src Alpha",
                                         "Dest Alpha", "Inv Dest Alpha", "Dest Color", "Inv Dest Color", "Src Alpha Sat",
                                         "Both Src Alpha", "Both Inv Src Alpha"};
    static const char *const COMPARES[] = {"Never", "Less", "Equal", "Less Equal", "Greater", "Not Equal", "Greater Equal",
                                           "Always"};
    auto compare = [&](const char *label, const ObjectProperty<int> &property) {
        int func = std::max(0, std::min(7, property.get(material) - 1));
        if (TypeAheadCombo(label, &func, COMPARES, 8)) LiveSet(id, property, func + 1);
    };
    if (mode == 2 || mode == 3) page.Line(455, 9, 456, mode == 3 ? 250 : 80);
    if (mode == 3) {
        bool test = properties::MaterialAlphaTest.get(material);
        page.Label(465, 12, 600, 28, "Alpha Test");
        if (page.Check("##alphatest", 600, 15, test)) LiveSet(id, properties::MaterialAlphaTest, test);
        page.Label(465, 30, 600, 50, "Alpha Test Function");
        page.Place(600, 30, 700, 50);
        compare("##alphafunc", properties::MaterialAlphaFunc);
    }
    if (mode == 2 || mode == 3) {
        int ref = properties::MaterialAlphaRef.get(material);
        page.Label(465, 60, 600, 80, "Alpha Ref Value");
        page.Place(600, 60, 700, 80);
        if (ImGui::SliderInt("##alpharef", &ref, 0, 255)) LiveSet(id, properties::MaterialAlphaRef, ref);
    }
    if (mode == 3) {
        page.Line(465, 90, 710, 91);
        bool blend = properties::MaterialAlphaBlend.get(material);
        page.Label(465, 102, 600, 118, "Blend");
        if (page.Check("##blend", 600, 105, blend)) LiveSet(id, properties::MaterialAlphaBlend, blend);
        int src = std::max(0, properties::MaterialSourceBlend.get(material) - 1),
            dst = std::max(0, properties::MaterialDestBlend.get(material) - 1);
        page.Label(465, 120, 600, 140, "Source Blend");
        page.Place(600, 120, 700, 140);
        if (TypeAheadCombo("##sourceblend", &src, BLENDS, 13)) LiveSet(id, properties::MaterialSourceBlend, src + 1);
        page.Label(465, 140, 600, 160, "Destination Blend");
        page.Place(600, 140, 700, 160);
        if (TypeAheadCombo("##destblend", &dst, BLENDS, 13)) LiveSet(id, properties::MaterialDestBlend, dst + 1);
        page.Line(465, 170, 710, 171);
        bool zwrite = properties::MaterialZWrite.get(material);
        page.Label(465, 182, 600, 198, "Z-Buffer Write");
        if (page.Check("##zwrite", 600, 184, zwrite)) LiveSet(id, properties::MaterialZWrite, zwrite);
        page.Label(465, 200, 600, 220, "Z Compare Function");
        page.Place(600, 200, 700, 220);
        compare("##zfunc", properties::MaterialZFunc);
    }

    // Used By (meshes, 2D entities and 3D sprites: documentation "all the
    // elements curently using the Material").
    std::vector<CKObject *> users;
    for (CKObject *object : ObjectsOf(*m_State.document, CKCID_MESH, true)) {
        CKMesh *mesh = CKMesh::Cast(object);
        for (int i = 0; i < mesh->GetMaterialCount(); ++i)
            if (mesh->GetMaterial(i) == material) {
                users.push_back(mesh);
                break;
            }
    }
    for (CKObject *object : ObjectsOf(*m_State.document, CKCID_2DENTITY, true))
        if (CK2dEntity::Cast(object)->GetMaterial() == material) users.push_back(object);
    for (CKObject *object : ObjectsOf(*m_State.document, CKCID_SPRITE3D, true))
        if (CKSprite3D::Cast(object)->GetMaterial() == material) users.push_back(object);
    page.Label(25, 131, 200, 150, "Used By");
    ImGui::SetCursorScreenPos(page.At(20, 150));
    UsedByList("Used By", users, ImVec2(190.0f * page.s, 150.0f * page.s));

    // The texture and its settings.
    static const int SETUP[5] = {1145, 1141, 1142, 1142, 1145};
    page.Label(220, 140, 280, 160, "Texture");
    CKTexture *texture = material->GetTexture(0);
    if (texture && DevBitmapButton("texturesetup", page.At(280, 142), SETUP, false, true, page.s, "Show Texture Setup"))
        m_State.events.Post(ShowObjectSetup{texture->GetID()});
    CK_ID chosen = 0;
    if (ObjectCombo("##texture", CKCID_TEXTURE, texture, &chosen, page.Place(300, 140, 445, 160)))
        Live(id, "Texture", [this, chosen](CKObject *o) { CKMaterial::Cast(o)->SetTexture0(Get<CKTexture>(m_State, chosen)); });
    page.Line(230, 159, 231, 280);
    const char *blends[] = {"Decal", "Modulate", "Decal Alpha", "Modulate Alpha", "Decal Mask", "Modulate Mask", "Copy", "Add", "Dot Product 3", "Max"};
    int textureBlend = std::max(0, std::min(9, static_cast<int>(material->GetTextureBlendMode()) - 1));
    page.Label(235, 160, 350, 180, "Texture Blend");
    page.Place(350, 160, 445, 180);
    if (TypeAheadCombo("##textureblend", &textureBlend, blends, 10))
        LiveSet(id, properties::MaterialTextureBlend, textureBlend + 1);
    const char *filters[] = {"Nearest", "Linear", "Mip Nearest", "Mip Linear", "Linear Mip Nearest", "Linear Mip Linear", "Anisotropic"};
    int minFilter = std::max(0, std::min(6, static_cast<int>(material->GetTextureMinMode()) - 1));
    int magFilter = std::max(0, std::min(6, static_cast<int>(material->GetTextureMagMode()) - 1));
    page.Label(235, 180, 350, 200, "Filter Min");
    page.Place(350, 180, 445, 200);
    if (TypeAheadCombo("##filtermin", &minFilter, filters, 7))
        LiveSet(id, properties::MaterialTextureMinMode, minFilter + 1);
    page.Label(235, 200, 350, 220, "Filter Mag");
    page.Place(350, 200, 445, 220);
    if (TypeAheadCombo("##filtermag", &magFilter, filters, 7))
        LiveSet(id, properties::MaterialTextureMagMode, magFilter + 1);
    const char *addresses[] = {"Wrap", "Mirror", "Clamp", "Border", "Mirror Once"};
    int address = std::max(0, std::min(4, static_cast<int>(material->GetTextureAddressMode()) - 1));
    page.Label(235, 220, 350, 240, "Address Mode");
    page.Place(350, 220, 445, 240);
    if (TypeAheadCombo("##addressmode", &address, addresses, 5))
        LiveSet(id, properties::MaterialTextureAddress, address + 1);
    bool perspective = material->PerspectiveCorrectionEnabled() != 0;
    page.Label(235, 242, 360, 258, "Perspective Correct");
    if (page.Check("##perspective", 360, 245, perspective)) LiveSet(id, properties::MaterialPerspectiveCorrect, perspective);
    // Border Color with the Border address mode (Dev.exe sub_468440).
    if (material->GetTextureAddressMode() == VXTEXTURE_ADDRESSBORDER) {
        const VxColor border = properties::MaterialTextureBorderColor.get(material);
        float rgba[4] = {border.r, border.g, border.b, border.a};
        page.Label(235, 260, 335, 280, "Border Color");
        if (page.ColorBar("Border Color", 350, 260, 380, 280, rgba, true))
            LiveSet(id, properties::MaterialTextureBorderColor, VxColor(rgba[0], rgba[1], rgba[2], rgba[3]));
    }

    // The effect (sub_469070): the render engine's effects, the textures the
    // effect takes (Texture 1 to 3, else its own words for them), its
    // parameter (745: Edit Parameter) and description.
    CKRenderManager *render = context->GetRenderManager();
    const int effectCount = render ? render->GetEffectCount() : 0;
    static const char *const EFFECTS[] = {"None", "TexGen", "TexGen Ref", "Bump Env", "DP3", "2 Textures", "3 Textures"};
    std::vector<std::string> names;
    for (int i = 0; i < effectCount; ++i) {
        const XString &summary = render->GetEffectDescription(i).Summary;
        names.push_back(summary.Length() ? AnsiToUtf8(summary.CStr()) : (i < 7 ? EFFECTS[i] : "Effect " + std::to_string(i)));
    }
    if (names.empty()) names.assign(EFFECTS, EFFECTS + 7);
    std::vector<const char *> items;
    for (const std::string &name : names) items.push_back(name.c_str());
    int effect = std::max(0, std::min(static_cast<int>(items.size()) - 1, properties::MaterialEffect.get(material)));
    page.Line(720, 9, 721, 250);
    page.Label(730, 10, 780, 30, "Effect");
    page.Place(780, 10, 930, 30);
    if (TypeAheadCombo("##effect", &effect, items.data(), static_cast<int>(items.size())))
        LiveSet(id, properties::MaterialEffect, effect);
    if (render && effect > 0 && effect < effectCount) {
        const VxEffectDescription &description = render->GetEffectDescription(effect);
        const XString *const WORDS[] = {&description.Tex1Description, &description.Tex2Description, &description.Tex3Description};
        for (int i = 0; i < std::min(3, description.MaxTextureCount); ++i) {
            const float y = 32.0f + 20.0f * i;
            const std::string label = WORDS[i]->Length() ? AnsiToUtf8(WORDS[i]->CStr()) : "Texture " + std::to_string(i + 1);
            page.Label(730, y, 810, y + 20, label.c_str());
            ImGui::PushID(i);
            CKTexture *extra = material->GetTexture(i + 1);
            if (extra && DevBitmapButton("effecttexturesetup", page.At(810, y + 2), SETUP, false, true, page.s, "Show Texture Setup"))
                m_State.events.Post(ShowObjectSetup{extra->GetID()});
            CK_ID picked = 0;
            if (ObjectCombo("##effecttexture", CKCID_TEXTURE, extra, &picked, page.Place(830, y, 930, y + 20))) {
                Live(id, "Effect Texture", [this, i, picked](CKObject *o) {
                    CKMaterial::Cast(o)->SetTexture(i + 1, Get<CKTexture>(m_State, picked));
                });
                FlushPending();
            }
            ImGui::PopID();
        }
        CKParameter *parameter = material->GetEffectParameter();
        if (parameter && description.ParameterType != CKGUID(0, 0)) {
            const ImVec2 a = page.At(730, 100), b = page.At(930, 118);
            ImGui::SetCursorScreenPos(a);
            const std::string value = ParameterValueText(parameter);
            if (ImGui::Button((value + "##effectparameter").c_str(), ImVec2(b.x - a.x, b.y - a.y))) {
                const char *type = context->GetParameterManager()->ParameterTypeToName(parameter->GetType());
                m_AttributeType = type ? type : "";
                m_AttributeBox.Clear();
                m_Doc.valueScript = 0;
                if (m_AttributeBox.Add(parameter, description.ParameterDescription.Length() ? description.ParameterDescription.CStr() : "Effect"))
                    m_OpenAttributeValue = true;
            }
            if (ImGui::IsItemHovered() && description.ParameterDescription.Length())
                ImGui::SetTooltip("%s", AnsiToUtf8(description.ParameterDescription.CStr()).c_str());
        }
        // The effect's image (Dev.exe sub_469070: InterfacePlugins\\<DescImage>
        // in the program's folder; here the runtime's), 128 x 128 at 766, 183.
        if (description.DescImage.Length()) {
            const std::string path = (std::filesystem::u8path(m_State.host.RuntimeDirectory()) / "InterfacePlugins" /
                                      std::filesystem::path(description.DescImage.CStr())).u8string();
            const unsigned long long imageKey = (std::hash<std::string>()(path) | 1ull) << 2 | 3ull;
            static std::set<std::string> tried;
            if (!tried.count(path)) {
                tried.insert(path);
                const char *dot = std::strrchr(path.c_str(), '.');
                CKFileExtension imageExt(const_cast<char *>(dot ? dot + 1 : ""));
                CKBitmapReader *reader = CKGetPluginManager() ? CKGetPluginManager()->GetBitmapReader(imageExt) : nullptr;
                CKBitmapProperties *read = nullptr;
                if (reader && reader->ReadFile(const_cast<char *>(std::filesystem::u8path(path).string().c_str()), &read) == 0 && read && read->m_Data) {
                    VxImageDescEx source = read->m_Format;
                    source.Image = static_cast<XBYTE *>(read->m_Data);
                    VxImageDescEx bgra;
                    bgra.Width = source.Width;
                    bgra.Height = source.Height;
                    bgra.BitsPerPixel = 32;
                    bgra.BytesPerLine = source.Width * 4;
                    bgra.RedMask = 0x00FF0000;
                    bgra.GreenMask = 0x0000FF00;
                    bgra.BlueMask = 0x000000FF;
                    bgra.AlphaMask = 0xFF000000;
                    std::vector<XBYTE> pixels(static_cast<size_t>(bgra.BytesPerLine) * bgra.Height);
                    bgra.Image = pixels.data();
                    VxDoBlit(source, bgra);
                    PixelTexture(imageKey, bgra.Width, bgra.Height, pixels.data(), bgra.BytesPerLine, false);
                }
                if (reader && read && read->m_Data) reader->ReleaseMemory(read->m_Data);
                if (reader) reader->Release();
            }
            if (ImTextureID image = CachedPixelTexture(imageKey))
                ImGui::GetWindowDrawList()->AddImage(image, page.At(766, 183), page.At(894, 311));
        }
        if (description.Description.Length()) {
            ImGui::SetCursorScreenPos(page.At(730, 120));
            ImGui::PushTextWrapPos(page.At(930, 0).x - ImGui::GetWindowPos().x + ImGui::GetScrollX());
            ImGui::TextUnformatted(AnsiToUtf8(description.Description.CStr()).c_str());
            ImGui::PopTextWrapPos();
        }
    }
    page.End(935, 300);
    AttributeValueDialog();
}

void SetupWindows::MeshPage(Setup &setup, CKMesh *mesh) {
    if (!mesh) return;
    const CK_ID id = mesh->GetID();
    CKContext *context = m_State.document->Context();
    ImGui::BeginGroup();
    bool progressive = mesh->IsPM() != 0;
    if (ImGui::Checkbox("Progressive Mesh", &progressive))
        LiveSet(id, properties::MeshProgressive, progressive);
    // Progressive meshes (documentation "Mesh Setup"): the vertices
    // rendered, the GeoMorphic Transition and its decimation (0: by halves).
    if (mesh->IsPM()) {
        int vertices = mesh->GetVerticesRendered();
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderInt("Vertices", &vertices, 0, std::max(1, mesh->GetVertexCount())))
            Live(id, "Vertices Rendered", [vertices](CKObject *o) { CKMesh::Cast(o)->SetVerticesRendered(vertices); });
        bool geomorph = mesh->IsPMGeoMorphEnabled() != 0;
        if (ImGui::Checkbox("GeoMorphic Transition", &geomorph))
            Live(id, "GeoMorphic Transition", [geomorph](CKObject *o) { CKMesh::Cast(o)->EnablePMGeoMorph(geomorph); });
        ImGui::SameLine();
        int step = mesh->GetPMGeoMorphStep();
        ImGui::SetNextItemWidth(80);
        if (ImGui::InputInt("Decimation", &step, 1, 10))
            Live(id, "GeoMorph Step", [step](CKObject *o) { CKMesh::Cast(o)->SetPMGeoMorphStep(std::max(0, step)); });
    } else {
        ImGui::Dummy(ImVec2(1, 30));
    }
    // Stripify (VXMESH_STRIPIFY: the mesh is stripified each time it loads).
    bool stripify = (mesh->GetFlags() & VXMESH_STRIPIFY) != 0;
    if (ImGui::Checkbox("Stripify", &stripify))
        Live(id, "Stripify", [stripify](CKObject *o) {
            CKMesh *m = CKMesh::Cast(o);
            m->SetFlags(stripify ? (m->GetFlags() | VXMESH_STRIPIFY) : (m->GetFlags() & ~VXMESH_STRIPIFY));
        });
    const char *wraps[] = {"None", "U", "V", "UV"};
    int wrap = std::max(0, std::min(3, static_cast<int>(mesh->GetWrapMode())));
    ImGui::SetNextItemWidth(120);
    if (TypeAheadCombo("Texture Wrap", &wrap, wraps, 4))
        LiveSet(id, properties::MeshWrapMode, wrap);
    const char *lits[] = {"Prelit", "Lit"};
    int lit = mesh->GetLitMode() == VX_LITMESH ? 1 : 0;
    ImGui::SetNextItemWidth(120);
    if (TypeAheadCombo("Lit Mode", &lit, lits, 2))
        LiveSet(id, properties::MeshLit, lit != 0);
    // Patch meshes (language pack [Mesh Setup]: "Iterations"): the subdivision
    // steps of the render mesh built from the patches.
    CKPatchMesh *patch = CKPatchMesh::Cast(mesh);
    if (patch) {
        int iterations = patch->GetIterationCount();
        ImGui::SetNextItemWidth(120);
        if (ImGui::SliderInt("Iterations", &iterations, 0, 10))
            LiveSet(id, properties::PatchMeshIterations, iterations);
    }
    ImGui::EndGroup();
    ImGui::SameLine(330);
    ImGui::BeginGroup();
    Group("Mesh Info");
    ImGui::Text("Vertices:     %d", mesh->GetVertexCount());
    ImGui::Text("Faces:        %d", mesh->GetFaceCount());
    ImGui::Text("Channels:     %d", mesh->GetChannelCount());
    ImGui::Text("Line Count    %d", mesh->GetLineCount());
    ImGui::Text("Type:         %s", patch ? "Patch" : "Triangular");
    ImGui::BeginDisabled(patch != nullptr); // a patch mesh rebuilds its faces from the patches
    if (ImGui::Button("Consolidate")) Act("Consolidate", [&] { return ConsolidateMesh(*m_State.document, id); });
    ImGui::EndDisabled();
    if (patch) {
        ImGui::Dummy(ImVec2(1, 6));
        Group("PatchMesh Info");
        ImGui::Text("Control Points:     %d", patch->GetVertCount());
        ImGui::Text("Vectors:            %d", patch->GetVecCount());
        ImGui::Text("Edges:              %d", patch->GetEdgeCount());
        ImGui::Text("Patches:            %d", patch->GetPatchCount());
        ImGui::Text("Texture Vertices:   %d", patch->GetTVCount());
    }
    ImGui::EndGroup();
    ImGui::SameLine(620);
    ImGui::BeginGroup();
    Group("Geometry Info (Local Referential)");
    const VxBbox &box = mesh->GetLocalBox();
    VxVector bary;
    mesh->GetBaryCenter(&bary);
    ImGui::Text("Radius:      %.2f", mesh->GetRadius());
    ImGui::Text("BBox:        Min: %.2f %.2f %.2f", box.Min.x, box.Min.y, box.Min.z);
    ImGui::Text("             Max: %.2f %.2f %.2f", box.Max.x, box.Max.y, box.Max.z);
    ImGui::Text("Barycenter:  %.2f %.2f %.2f", bary.x, bary.y, bary.z);
    ImGui::EndGroup();
    ImGui::Separator();
    std::vector<CKObject *> users;
    for (CKObject *object : ObjectsOf(*m_State.document, CKCID_3DENTITY, true)) {
        CK3dEntity *entity = CK3dEntity::Cast(object);
        for (int i = 0; i < entity->GetMeshCount(); ++i)
            if (entity->GetMesh(i) == mesh) {
                users.push_back(entity);
                break;
            }
    }
    ImGui::BeginGroup();
    UsedBy("Used By", users, 220);
    ImGui::EndGroup();
    ImGui::SameLine(310);
    ImGui::BeginGroup();
    Group("Channels");
    static const char *const BLENDS[] = {"Zero", "One", "Src Color", "Inv Src Color", "Src Alpha", "Inv Src Alpha",
                                         "Dest Alpha", "Inv Dest Alpha", "Dest Color", "Inv Dest Color", "Src Alpha Sat",
                                         "Both Src Alpha", "Both Inv Src Alpha"};
    // The channels' menu (Dev.exe sub_46B1F0): Show Material Setup, Add
    // Channel, Remove Channel(s) (also Delete); a click on a material name
    // selects (Ctrl: adds or takes off).
    for (auto it = setup.channelRows.begin(); it != setup.channelRows.end();)
        it = *it < mesh->GetChannelCount() ? std::next(it) : setup.channelRows.erase(it);
    auto removeChannels = [&] {
        std::vector<CK_ID> materials;
        for (int channel : setup.channelRows)
            if (CKMaterial *material = mesh->GetChannelMaterial(channel)) materials.push_back(material->GetID());
        if (materials.empty()) return;
        Live(id, "Remove Channel(s)", [this, materials](CKObject *o) {
            for (CK_ID material : materials) CKMesh::Cast(o)->RemoveChannelByMaterial(Get<CKMaterial>(m_State, material));
        });
        FlushPending();
        setup.channelRows.clear();
    };
    // At most 10 channels (Dev.exe sub_46FD90: "Cannot add more than 10 channels!").
    constexpr int MAX_CHANNELS = 10;
    auto addChannel = [&] {
        if (mesh->GetChannelCount() >= MAX_CHANNELS) m_State.Log(LogLevel::Warning, "Cannot add more than 10 channels!");
        else setup.openAddChannel = true;
    };
    auto addChannelOf = [&](CK_ID material) {
        if (mesh->GetChannelCount() >= MAX_CHANNELS) {
            m_State.Log(LogLevel::Warning, "Cannot add more than 10 channels!");
            return;
        }
        bool added = false;
        Live(id, "Add Channel", [this, material, &added](CKObject *o) {
            added = CKMesh::Cast(o)->AddChannel(Get<CKMaterial>(m_State, material), TRUE) >= 0;
        });
        FlushPending();
        if (!added) m_State.Log(LogLevel::Warning, "Add Channel: the channel cannot be added");
    };
    if (ImGui::BeginTable("channels", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(670, 220))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Material Name");
        ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Lit Mode", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Src Blend", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Dest Blend", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableHeadersRow();
        // The pass each active channel is rendered in (the faces' materials
        // the first; here a pass per active channel).
        int pass = 1;
        for (int i = 0; i < mesh->GetChannelCount(); ++i) {
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool selected = setup.channelRows.count(i) != 0;
            if (ImGui::Selectable(NameOf(mesh->GetChannelMaterial(i)).c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && mesh->GetChannelMaterial(i))
                    m_State.events.Post(ShowObjectSetup{mesh->GetChannelMaterial(i)->GetID()});
                else if (!ImGui::GetIO().KeyCtrl) setup.channelRows = {i};
                else if (selected) setup.channelRows.erase(i);
                else setup.channelRows.insert(i);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) setup.channelRows = {i};
            if (ImGui::BeginPopupContextItem("channelmenu")) {
                CKMaterial *material = setup.channelRows.size() == 1 ? mesh->GetChannelMaterial(*setup.channelRows.begin()) : nullptr;
                if (ImGui::MenuItem("Show Material Setup", nullptr, false, material != nullptr))
                    m_State.events.Post(ShowObjectSetup{material->GetID()});
                ImGui::Separator();
                if (ImGui::MenuItem("Add Channel")) addChannel();
                if (ImGui::MenuItem("Remove Channel(s)", "Delete", false, !setup.channelRows.empty())) removeChannels();
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            bool active = mesh->IsChannelActive(i) != 0;
            if (ImGui::Checkbox("##active", &active)) Live(id, "Channel Active", [i, active](CKObject *o) { CKMesh::Cast(o)->ActivateChannel(i, active); });
            ImGui::TableNextColumn();
            bool lit = mesh->IsChannelLit(i) != 0;
            if (ImGui::Checkbox("##lit", &lit)) Live(id, "Channel Lit", [i, lit](CKObject *o) { CKMesh::Cast(o)->LitChannel(i, lit); });
            ImGui::TableNextColumn();
            int src = std::max(0, static_cast<int>(mesh->GetChannelSourceBlend(i)) - 1);
            ImGui::SetNextItemWidth(-1);
            if (TypeAheadCombo("##src", &src, BLENDS, 13))
                Live(id, "Channel Source Blend", [i, src](CKObject *o) { CKMesh::Cast(o)->SetChannelSourceBlend(i, static_cast<VXBLEND_MODE>(src + 1)); });
            ImGui::TableNextColumn();
            int dst = std::max(0, static_cast<int>(mesh->GetChannelDestBlend(i)) - 1);
            ImGui::SetNextItemWidth(-1);
            if (TypeAheadCombo("##dst", &dst, BLENDS, 13))
                Live(id, "Channel Dest Blend", [i, dst](CKObject *o) { CKMesh::Cast(o)->SetChannelDestBlend(i, static_cast<VXBLEND_MODE>(dst + 1)); });
            ImGui::TableNextColumn();
            if (mesh->IsChannelActive(i)) ImGui::Text("%d", ++pass);
            else ImGui::TextDisabled("-");
            ImGui::PopID();
        }
        if (ImGui::BeginPopupContextWindow("channelsmenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Add Channel")) addChannel();
            ImGui::EndPopup();
        }
        if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            removeChannels();
        ImGui::EndTable();
    }
    // A material dragged onto the channels: a new channel of it ("Add a new
    // channel").
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(LEVEL_OBJECT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            CK_ID dropped = 0;
            std::memcpy(&dropped, payload->Data, sizeof(dropped));
            if (Get<CKMaterial>(m_State, dropped)) {
                if (!payload->IsDelivery())
                    ImGui::SetTooltip("%s", mesh->GetChannelCount() >= MAX_CHANNELS ? "Cannot add more than 10 channels!" : "Add a new channel");
                else
                    addChannelOf(dropped);
            }
        }
        ImGui::EndDragDropTarget();
    }
    // Add Channel (Dev: an Edit Parameter of a Channel Material): the
    // composition's materials.
    if (setup.openAddChannel) {
        setup.openAddChannel = false;
        ImGui::OpenPopup("Add Channel");
    }
    if (ImGui::BeginPopup("Add Channel")) {
        TypeAhead typeAhead;
        for (CKObject *object : ObjectsOf(*m_State.document, CKCID_MATERIAL, true)) {
            ImGui::PushID(static_cast<int>(object->GetID()));
            if (typeAhead.Selectable(NameOf(object).c_str())) {
                addChannelOf(object->GetID());
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::EndGroup();
    ImGui::SameLine(950);
    std::vector<CKObject *> materials;
    for (int i = 0; i < mesh->GetMaterialCount(); ++i)
        if (CKMaterial *material = mesh->GetMaterial(i)) materials.push_back(material);
    // Materials Used; a material dragged onto one replaces it on the faces
    // (Dev.exe sub_46FD90: "Replace '%s' with '%s'"), elsewhere in the list
    // it is every face's ("Add material or replace existing material").
    ImGui::BeginGroup();
    Group("Materials Used");
    auto droppedMaterial = [this](const ImGuiPayload *payload) -> CKMaterial * {
        CK_ID dropped = 0;
        std::memcpy(&dropped, payload->Data, sizeof(dropped));
        return Get<CKMaterial>(m_State, dropped);
    };
    if (ImGui::BeginChild("Materials Used", ImVec2(280, 220), ImGuiChildFlags_Borders)) {
        TypeAhead typeAhead;
        for (CKObject *material : materials) {
            ImGui::PushID(static_cast<int>(material->GetID()));
            const std::string text = NameOf(material) + " (Material)";
            if (typeAhead.Selectable(text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                m_State.events.Post(ShowObjectSetup{material->GetID()});
            // Its menu (documentation: "right-click and select Show Setup").
            if (ImGui::BeginPopupContextItem("usedmenu")) {
                if (ImGui::MenuItem("Show Setup")) m_State.events.Post(ShowObjectSetup{material->GetID()});
                ImGui::EndPopup();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(LEVEL_OBJECT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery))
                    if (CKMaterial *with = droppedMaterial(payload)) {
                        if (!payload->IsDelivery())
                            ImGui::SetTooltip("Replace '%s' with '%s'", NameOf(material).c_str(), NameOf(with).c_str());
                        else if (with != material)
                            Act("Replace Material", [&] { return ReplaceMeshMaterial(*m_State.document, id, material->GetID(), with->GetID()); });
                    }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(LEVEL_OBJECT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery))
            if (CKMaterial *with = droppedMaterial(payload)) {
                if (!payload->IsDelivery()) {
                    ImGui::SetTooltip("Add material or replace existing material");
                } else {
                    const CK_ID chosen = with->GetID();
                    Live(id, "Apply Material", [this, chosen](CKObject *o) {
                        CKMesh::Cast(o)->ApplyGlobalMaterial(Get<CKMaterial>(m_State, chosen));
                    });
                    FlushPending();
                }
            }
        ImGui::EndDragDropTarget();
    }
    ImGui::EndGroup();
}

void SetupWindows::TexturePage(Setup &setup, CKTexture *texture) {
    if (!texture) return;
    const CK_ID id = texture->GetID();
    CKContext *context = m_State.document->Context();
    CKBitmapData *bitmap = texture;
    ImGui::BeginGroup();
    // Dev.exe sub_48B520: the system copy's format, else "not in system
    // memory".
    {
        VxImageDescEx system;
        const bool described = texture->GetSystemTextureDesc(system) != FALSE;
        if (described && texture->LockSurfacePtr(-1))
            ImGui::Text("System Memory Format   %dx%d %d bits %s", system.Width, system.Height, system.BitsPerPixel,
                        PixelFormatName(VxImageDesc2PixelFormat(system)));
        else
            ImGui::TextUnformatted("System Memory Format   not in system memory");
    }
    // Desired Video Format
    int format = static_cast<int>(texture->GetDesiredVideoFormat());
    ImGui::TextUnformatted("Desired Video Format");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170);
    // The formats the render driver takes are marked "*".
    CKContext *videoContext = m_State.document->Context();
    auto formatLabel = [&](int f) {
        return std::string(PixelFormatName(static_cast<VX_PIXELFORMAT>(f))) +
               (DriverTakesFormat(videoContext, static_cast<VX_PIXELFORMAT>(f)) ? " *" : "");
    };
    if (ImGui::BeginCombo("##videoformat", formatLabel(format).c_str())) {
        TypeAhead typeAhead;
        for (int f = 1; f <= 23; ++f)
            if (typeAhead.Selectable(formatLabel(f).c_str(), f == format))
                LiveSet(id, properties::TextureVideoFormat, f);
        ImGui::EndCombo();
    }
    ImGui::EndGroup();
    ImGui::SameLine(460);
    ImGui::BeginGroup();
    Group("Video Memory Information");
    {
        VxImageDescEx video;
        const bool inVideo = texture->IsInVideoMemory() != FALSE;
        const bool videoDescribed = inVideo && texture->GetVideoTextureDesc(video) != FALSE;
        VideoMemoryLines(inVideo, videoDescribed, video);
    }
    ImGui::EndGroup();
    ImGui::SameLine(760);
    ImGui::BeginGroup();
    const int slots = texture->GetSlotCount(), slot = texture->GetCurrentSlot();
    auto go = [&](const char *label, int to) {
        ImGui::BeginDisabled(to < 0 || to >= slots || to == slot);
        if (ImGui::Button(label, ImVec2(60, 0))) LiveSet(id, properties::TextureCurrentSlot, to);
        ImGui::EndDisabled();
    };
    go("Prev", slot - 1);
    ImGui::SameLine();
    ImGui::Text("%d/ %d", slot, slots);
    ImGui::SameLine();
    go("Next", slot + 1);
    go("First", 0);
    ImGui::SameLine(124);
    go("Last", slots - 1);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    // Documentation "Texture Setup": "Add Texture - O: Opens a dialog box ...
    // choose an image file" (as the preview's menu and the O key).
    if (ImGui::Button("Add Slot...", ImVec2(150, 0))) RunSlotCommand(setup, texture, SlotCommand::Add);
    ImGui::BeginDisabled(slots <= 1);
    if (ImGui::Button("Remove Current Slot", ImVec2(150, 0))) setup.confirmRemoveSlot = true;
    ImGui::EndDisabled();
    ImGui::EndGroup();
    ImGui::Separator();
    ImGui::BeginGroup();
    const char *options[] = {"Raw Data", "External", "Image Format", "Global settings", "Include Original File"};
    int option = std::max(0, std::min(4, static_cast<int>(bitmap->GetSaveOptions())));
    ImGui::SetNextItemWidth(160);
    if (TypeAheadCombo("Save Options", &option, options, 5))
        LiveSet(id, properties::TextureSaveOptions, option);
    if (option == CKTEXTURE_IMAGEFORMAT) SaveFormatControls(texture);
    const char *mips[] = {"None", "Automatic"};
    int mip = texture->GetMipmapCount() > 0 ? 1 : 0;
    ImGui::SetNextItemWidth(160);
    if (TypeAheadCombo("Mip Levels", &mip, mips, 2)) LiveSet(id, properties::TextureMipmap, mip);
    bool cube = bitmap->IsCubeMap() != 0;
    if (ImGui::Checkbox("Cube Map", &cube)) LiveSet(id, properties::TextureCubeMap, cube);
    bool key = bitmap->IsTransparent() != 0;
    if (ImGui::Checkbox("Color Key Transparency", &key))
        LiveSet(id, properties::TextureTransparent, key);
    const CKDWORD keyColor = bitmap->GetTransparentColor();
    float rgb[3] = {((keyColor >> 16) & 0xFF) / 255.0f, ((keyColor >> 8) & 0xFF) / 255.0f, (keyColor & 0xFF) / 255.0f};
    if (ImGui::ColorEdit3("Pick Color", rgb, ImGuiColorEditFlags_NoInputs))
        LiveSet(id, properties::TextureTransparentColor, VxColor(rgb[0], rgb[1], rgb[2], 1.0f));
    ImGui::SameLine();
    if (ImGui::Button(setup.pickingColor ? "Picking..." : "Pick")) setup.pickingColor = !setup.pickingColor;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick a color on the texture");
    int threshold = bitmap->GetPickThreshold();
    ImGui::TextUnformatted("Alpha Picking Threshold");
    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderInt("##threshold", &threshold, 0, 255))
        LiveSet(id, properties::TexturePickThreshold, threshold);
    std::vector<CKObject *> users;
    for (CKObject *object : ObjectsOf(*m_State.document, CKCID_MATERIAL, true))
        for (int i = 0; i < 4; ++i)
            if (CKMaterial::Cast(object)->GetTexture(i) == texture) {
                users.push_back(object);
                break;
            }
    UsedBy("Used By", users, 110);
    ImGui::EndGroup();
    // Previews of the current slot: colors and alpha (uploaded again when
    // the slot changes, and every two seconds for changing images).
    ImGui::SameLine(320);
    const int width = texture->GetWidth(), height = texture->GetHeight();
    const unsigned long long colorKey = static_cast<unsigned long long>(id) << 1, alphaKey = colorKey | 1;
    if (setup.shownTexture != id || setup.shownSlot != slot || ++setup.textureFrames > 120) {
        setup.shownTexture = id;
        setup.shownSlot = slot;
        setup.textureFrames = 0;
        if (CKBYTE *pixels = bitmap->LockSurfacePtr(slot)) {
            PixelTexture(colorKey, width, height, pixels, width * 4, false);
            PixelTexture(alphaKey, width, height, pixels, width * 4, true);
            bitmap->ReleaseSurfacePtr(slot);
        }
    }
    for (unsigned long long key : {colorKey, alphaKey}) {
        ImGui::BeginGroup();
        SlotPreview(setup, texture, key, width, height, 384.0f, key == alphaKey);
        ImGui::EndGroup();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    // Resize Slot (Dev: an Edit Parameter of a 2D Vector, the size).
    if (setup.openResize) {
        setup.openResize = false;
        ImGui::OpenPopup("Resize Slot");
    }
    if (ImGui::BeginPopupModal("Resize Slot", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(200);
        ImGui::InputInt2("Size", &setup.resizeWidth);
        if (ImGui::Button("OK", ImVec2(80, 0))) {
            FlushPending();
            Report(ResizeSlotImages(*m_State.document, id, setup.resizeWidth, setup.resizeHeight), "Resize Slot");
            setup.textureFrames = 1000;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void SetupWindows::GridPage(Setup &setup, CKGrid *grid) {
    if (!grid) return;
    const CK_ID id = grid->GetID();
    CKContext *context = m_State.document->Context();
    // Its 3D Properties (documentation: the Level Manager's "Setup") or the
    // grid's own (its "Grid Setup").
    if (ImGui::RadioButton("Grid Properties", !setup.grid3d)) setup.grid3d = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("3D Properties", setup.grid3d)) setup.grid3d = true;
    if (setup.grid3d) {
        Entity3DPage(grid, true, true, nullptr, &setup);
        return;
    }
    CKGridManager *manager = static_cast<CKGridManager *>(context->GetManagerByGuid(GRID_MANAGER_GUID));
    CKAttributeManager *attributes = context->GetAttributeManager();
    CKParameterManager *parameters = context->GetParameterManager();
    const int width = grid->GetWidth(), length = grid->GetLength();
    const int layers = grid->GetLayerCount();
    if (setup.layer >= layers) setup.layer = std::max(0, layers - 1);
    for (auto it = setup.layers.begin(); it != setup.layers.end();) it = *it >= layers ? setup.layers.erase(it) : std::next(it);
    if (layers && setup.layers.empty()) setup.layers.insert(setup.layer);
    CKLayer *selected = layers ? grid->GetLayerByIndex(setup.layer) : nullptr;
    auto typeColor = [&](int type) {
        VxColor color(1.0f, 1.0f, 0.0f, 1.0f);
        if (manager) manager->GetAssociatedColor(type, &color);
        return color;
    };
    auto valueAt = [](CKLayer *layer, int x, int y) {
        int value = 0;
        if (layer) layer->GetValue2(x, y, &value);
        return value;
    };
    auto linker = [&](int type) { return manager && manager->GetAssociatedParam(type) == CKPGUID_LINKERGRAPH_ENUM; };
    // Status line: the square under the mouse and the grid size.
    if (setup.hoverX >= 0)
        ImGui::Text("X : %d    Y : %d    Value : %d        Width : %d    Length : %d", setup.hoverX, setup.hoverY,
                    valueAt(selected, setup.hoverX, setup.hoverY), width, length);
    else
        ImGui::Text("X : #    Y : #    Value : #        Width : %d    Length : %d", width, length);
    setup.hoverX = setup.hoverY = -1;
    ImGui::BeginGroup();
    // The value painted: Dev's 0..255 gradient.
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float barWidth = 384.0f;
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilledMultiColor(at, ImVec2(at.x + barWidth, at.y + 12), IM_COL32(0, 0, 0, 255),
                                    IM_COL32(255, 255, 255, 255), IM_COL32(255, 255, 255, 255), IM_COL32(0, 0, 0, 255));
        const float marker = at.x + barWidth * setup.paint / 255.0f;
        dl->AddTriangleFilled(ImVec2(marker, at.y + 12), ImVec2(marker - 4, at.y + 18), ImVec2(marker + 4, at.y + 18),
                              IM_COL32(220, 0, 0, 255));
        ImGui::InvisibleButton("##paintvalue", ImVec2(barWidth, 18));
        if (ImGui::IsItemActive())
            setup.paint = std::max(0, std::min(255, static_cast<int>((ImGui::GetIO().MousePos.x - at.x) / barWidth * 255.0f + 0.5f)));
        ImGui::SameLine();
        ImGui::Text("%d", setup.paint);
    }
    // The squares (documentation: "Layers are cumulated and their square
    // value (intensity*layer color) are added").
    {
        const float side = 384.0f;
        const float cell = width > 0 && length > 0 ? std::max(2.0f, std::min(side / width, side / length)) : 10.0f;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(at, ImVec2(at.x + side + 40, at.y + side + 40), IM_COL32(128, 128, 128, 255));
        const ImVec2 origin(at.x + 20, at.y + 20);
        for (int y = 0; y < length; ++y)
            for (int x = 0; x < width; ++x) {
                float r = 0.0f, g = 0.0f, b = 0.0f;
                for (int l = 0; l < layers; ++l) {
                    CKLayer *layer = grid->GetLayerByIndex(l);
                    if (!layer || !layer->IsVisible()) continue;
                    const int value = valueAt(layer, x, y);
                    if (value <= 0) continue;
                    const VxColor c = typeColor(layer->GetType());
                    const float k = std::min(255, value) / 255.0f;
                    r += c.r * k;
                    g += c.g * k;
                    b += c.b * k;
                }
                const ImU32 fill = IM_COL32(static_cast<int>(std::min(1.0f, r) * 255), static_cast<int>(std::min(1.0f, g) * 255),
                                            static_cast<int>(std::min(1.0f, b) * 255), 255);
                // Row 0 at the bottom (the grid seen from above).
                const ImVec2 a(origin.x + x * cell, origin.y + (length - 1 - y) * cell);
                dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), fill);
                dl->AddRect(a, ImVec2(a.x + cell + 1, a.y + cell + 1), IM_COL32(110, 110, 110, 255));
            }
        ImGui::SetCursorScreenPos(origin);
        ImGui::InvisibleButton("##squares", ImVec2(std::max(1.0f, width * cell), std::max(1.0f, length * cell)),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const int x = static_cast<int>((mouse.x - origin.x) / cell);
            const int y = length - 1 - static_cast<int>((mouse.y - origin.y) / cell);
            if (x >= 0 && x < width && y >= 0 && y < length) {
                setup.hoverX = x;
                setup.hoverY = y;
                // A double click: the Square Value dialog.
                if (selected && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    setup.squareX = x;
                    setup.squareY = y;
                    setup.squareValue = valueAt(selected, x, y);
                    setup.openSquare = true;
                } else {
                    // Left paints the value, right clears, on every layer
                    // chosen (one undo step per stroke and layer).
                    const bool paint = ImGui::IsMouseDown(ImGuiMouseButton_Left), clear = ImGui::IsMouseDown(ImGuiMouseButton_Right);
                    if (ImGui::IsItemActive() && (paint || clear))
                        for (int l : setup.layers) {
                            CKLayer *layer = grid->GetLayerByIndex(l);
                            const int value = paint ? setup.paint : 0;
                            if (layer && valueAt(layer, x, y) != value)
                                Live(layer->GetID(), "Paint Grid", [x, y, value](CKObject *o) {
                                    int v = value;
                                    static_cast<CKLayer *>(o)->SetValue2(x, y, &v);
                                });
                        }
                }
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + side + 44));
        ImGui::Dummy(ImVec2(side + 40, 1));
    }
    // The Square Value dialog (a Linker layer: None, Start, End, Start/End, Door).
    if (setup.openSquare) {
        setup.openSquare = false;
        ImGui::OpenPopup("Square Value");
    }
    if (ImGui::BeginPopupModal("Square Value", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        CKEnumStruct *choices = nullptr;
        if (selected && linker(selected->GetType()) && parameters)
            choices = parameters->GetEnumDescByType(parameters->ParameterGuidToType(CKPGUID_LINKERGRAPH_ENUM));
        ImGui::Text("Square (%d, %d)", setup.squareX, setup.squareY);
        if (choices) {
            for (int i = 0; i < choices->GetNumEnums(); ++i)
                if (ImGui::RadioButton(AnsiToUtf8(choices->GetEnumDescription(i)).c_str(), setup.squareValue == choices->GetEnumValue(i)))
                    setup.squareValue = choices->GetEnumValue(i);
        } else {
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Value", &setup.squareValue);
            setup.squareValue = std::max(0, std::min(255, setup.squareValue));
        }
        if (ImGui::Button("OK", ImVec2(80, 0))) {
            const int x = setup.squareX, y = setup.squareY, value = setup.squareValue;
            for (int l : setup.layers)
                if (CKLayer *layer = grid->GetLayerByIndex(l))
                    if (x >= 0 && x < width && y >= 0 && y < length && valueAt(layer, x, y) != value) {
                        Live(layer->GetID(), "Square Value", [x, y, value](CKObject *o) {
                            int v = value;
                            static_cast<CKLayer *>(o)->SetValue2(x, y, &v);
                        });
                        FlushPending();
                    }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // Orientation Mode / Priority
    const char *orientations[] = {"Free", "XZ", "XY", "YZ"};
    int orientation = std::max(0, std::min(3, static_cast<int>(grid->GetOrientationMode())));
    ImGui::SetNextItemWidth(90);
    if (TypeAheadCombo("Orientation Mode", &orientation, orientations, 4))
        LiveSet(id, properties::GridOrientation, orientation);
    ImGui::SameLine();
    int priority = grid->GetGridPriority();
    ImGui::SetNextItemWidth(60);
    if (ImGui::DragInt("Priority", &priority, 0.2f))
        LiveSet(id, properties::GridPriority, priority);
    // The Grid Cross: columns and rows added (large arrows) or deleted.
    {
        auto resize = [&](const char *label, int sideOf, bool add) {
            if (ImGui::Button(label)) Act(add ? "Add" : "Delete", [&] { return ResizeGrid(*m_State.document, id, sideOf, add); });
        };
        ImGui::TextUnformatted("Grid Cross");
        ImGui::SameLine();
        resize("<< Add Column##left", 0, true);
        ImGui::SameLine();
        resize("Add Column >>##right", 1, true);
        ImGui::SameLine();
        resize("Add Row (Top)", 3, true);
        ImGui::SameLine();
        resize("Add Row (Bottom)", 2, true);
        ImGui::Dummy(ImVec2(70, 1));
        ImGui::SameLine();
        resize("x Left Column", 0, false);
        ImGui::SameLine();
        resize("x Right Column", 1, false);
        ImGui::SameLine();
        resize("x Top Row", 3, false);
        ImGui::SameLine();
        resize("x Bottom Row", 2, false);
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    // A layer type's color and parameter (its row's menu, in either list).
    auto typeMenu = [&](int type) {
        const char *name = manager ? manager->GetTypeName(type) : nullptr;
        if (!name) return;
        VxColor color = typeColor(type);
        float rgb[3] = {color.r, color.g, color.b};
        if (ImGui::ColorPicker3("##typecolor", rgb, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs))
            Report(SetGridLayerTypeColor(*m_State.document, type, VxColor(rgb[0], rgb[1], rgb[2], color.a)), "Layer Type Color");
        ImGui::Separator();
        if (ImGui::MenuItem("Integer", nullptr, !linker(type))) Report(SetGridLayerTypeLinker(*m_State.document, type, false), "Param Type");
        if (ImGui::MenuItem("Linker", nullptr, linker(type))) Report(SetGridLayerTypeLinker(*m_State.document, type, true), "Param Type");
    };
    // Layers of the grid: color, name, visibility (V). Ctrl+click chooses
    // several; right-click: the type's color and parameter, Remove Layer. A
    // layer type dragged from the Global Layer List is added.
    if (ImGui::BeginTable("layers", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(330, 200))) {
        TypeAhead typeAhead(TypeAhead::Shared);
        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Layer Name");
        ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthFixed, 26);
        ImGui::TableHeadersRow();
        int removeType = -1;
        for (int l = 0; l < layers; ++l) {
            CKLayer *layer = grid->GetLayerByIndex(l);
            if (!layer) continue;
            ImGui::PushID(l);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const ImVec2 c = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(c.x + 2, c.y + 1), ImVec2(c.x + 22, c.y + 15),
                                                      ToImColor(typeColor(layer->GetType())));
            ImGui::Dummy(ImVec2(24, 15));
            ImGui::TableNextColumn();
            const char *name = manager ? manager->GetTypeName(layer->GetType()) : nullptr;
            if (typeAhead.Selectable(name ? AnsiToUtf8(name).c_str() : "?", setup.layers.count(l) != 0)) {
                if (ImGui::GetIO().KeyCtrl) {
                    if (!setup.layers.erase(l)) setup.layers.insert(l);
                    if (setup.layers.count(l)) setup.layer = l;
                    else if (!setup.layers.empty()) setup.layer = *setup.layers.begin();
                } else {
                    setup.layer = l;
                    setup.layers = {l};
                }
            }
            if (ImGui::BeginPopupContextItem()) {
                typeMenu(layer->GetType());
                ImGui::Separator();
                if (ImGui::MenuItem("Remove Layer")) removeType = layer->GetType();
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            bool visible = layer->IsVisible() != 0;
            if (ImGui::Checkbox("##visible", &visible)) {
                // The layers chosen together.
                const std::set<int> targets = setup.layers.count(l) ? setup.layers : std::set<int>{l};
                for (int t : targets)
                    if (CKLayer *each = grid->GetLayerByIndex(t)) LiveSet(each->GetID(), properties::LayerVisible, visible);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("VDEV_GRID_LAYER_TYPE")) {
                int type = 0;
                std::memcpy(&type, payload->Data, sizeof(type));
                if (!GridGetLayer(grid, type)) Act("Add Layer", [&] { return AddGridLayer(*m_State.document, id, type); });
            }
            ImGui::EndDragDropTarget();
        }
        if (removeType >= 0) Act("Remove Layer", [&] { return RemoveGridLayer(*m_State.document, id, removeType); });
    }
    // Grid is Class of ... (its classification attributes); double-click removes.
    const int category = manager ? GridClassificationCategory(manager) : -1;
    std::vector<CKAttributeType> classifications;
    for (CKAttributeType type = 0; attributes && type < attributes->GetAttributeCount(); ++type)
        if (attributes->IsAttributeIndexValid(type) && attributes->GetAttributeCategoryIndex(type) == category)
            classifications.push_back(type);
    if (ImGui::BeginTable("gridclass", 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(330, 120))) {
        TypeAhead typeAhead(TypeAhead::Shared);
        ImGui::TableSetupColumn("Grid is Class of ...");
        ImGui::TableHeadersRow();
        for (CKAttributeType type : classifications)
            if (grid->HasAttribute(type)) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(type);
                const char *name = attributes->GetAttributeNameByType(type);
                if (typeAhead.Selectable(name ? AnsiToUtf8(name).c_str() : "?", false, ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    Act("Remove Classification", [&] { return RemoveGridClassification(*m_State.document, id, type); });
                ImGui::PopID();
            }
        ImGui::EndTable();
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    // The Global Layer List (CKGridManager): a type dragged to the layers or
    // double-clicked adds a layer of it; right-click: New Layer Type, the
    // type's color and parameter, Rename (F2).
    if (ImGui::BeginTable("layertypes", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(450, 200))) {
        TypeAhead typeAhead(TypeAhead::Shared);
        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Layer Type Name");
        ImGui::TableSetupColumn("Param Type", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableHeadersRow();
        // Type 0 is the manager's "invalid layer type".
        for (int type = 1; manager && type < manager->GetLayerTypeCount(); ++type) {
            const char *name = manager->GetTypeName(type);
            if (!name) continue;
            ImGui::PushID(type);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const ImVec2 c = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(c.x + 2, c.y + 1), ImVec2(c.x + 22, c.y + 15), ToImColor(typeColor(type)));
            ImGui::Dummy(ImVec2(24, 15));
            ImGui::TableNextColumn();
            if (typeAhead.Selectable(AnsiToUtf8(name).c_str(), setup.layerType == type, ImGuiSelectableFlags_AllowDoubleClick)) {
                setup.layerType = type;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !GridGetLayer(grid, type))
                    Act("Add Layer", [&] { return AddGridLayer(*m_State.document, id, type); });
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("VDEV_GRID_LAYER_TYPE", &type, sizeof(type));
                ImGui::TextUnformatted(AnsiToUtf8(name).c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to the Layer List, or double-click: add a layer of this type");
            if (ImGui::BeginPopupContextItem("typemenu")) {
                setup.layerType = type;
                if (ImGui::MenuItem("New Layer Type")) Report(RegisterGridLayerType(*m_State.document, ""), "New Layer Type");
                if (ImGui::MenuItem("Rename", "F2")) {
                    setup.gridRenameText = AnsiToUtf8(name);
                    setup.renamingClassification = false;
                    setup.openGridRename = true;
                }
                ImGui::Separator();
                typeMenu(type);
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            const CKGUID guid = manager->GetAssociatedParam(type);
            const CKParameterType parameterType = parameters ? parameters->ParameterGuidToType(guid) : -1;
            CKParameterTypeDesc *description =
                parameters && parameterType >= 0 ? parameters->GetParameterTypeDescription(parameterType) : nullptr;
            ImGui::TextUnformatted(linker(type) ? "Linker" : description ? AnsiToUtf8(description->TypeName.CStr()).c_str() : "Integer");
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (ImGui::BeginPopupContextItem("layertypesmenu")) {
            if (ImGui::MenuItem("New Layer Type")) Report(RegisterGridLayerType(*m_State.document, ""), "New Layer Type");
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_F2, false) && manager && manager->GetTypeName(setup.layerType)) {
            setup.gridRenameText = AnsiToUtf8(manager->GetTypeName(setup.layerType));
            setup.renamingClassification = false;
            setup.openGridRename = true;
        }
    }
    // The Global Classification List (attributes of the grid category):
    // double-click adds it to the grid; right-click: New Classification,
    // Rename (F2).
    if (ImGui::BeginTable("classifications", 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(450, 120))) {
        TypeAhead typeAhead(TypeAhead::Shared);
        ImGui::TableSetupColumn("Classifications");
        ImGui::TableHeadersRow();
        for (CKAttributeType type : classifications) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(type);
            const char *name = attributes->GetAttributeNameByType(type);
            if (typeAhead.Selectable(name ? AnsiToUtf8(name).c_str() : "?", grid->HasAttribute(type) != 0 || setup.classification == type,
                                     ImGuiSelectableFlags_AllowDoubleClick)) {
                setup.classification = type;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !grid->HasAttribute(type))
                    Act("Add Classification", [&] { return AddGridClassification(*m_State.document, id, type); });
            }
            if (ImGui::BeginPopupContextItem("classmenu")) {
                setup.classification = type;
                if (ImGui::MenuItem("New Classification"))
                    Report(RegisterGridClassification(*m_State.document, ""), "New Classification");
                if (ImGui::MenuItem("Rename", "F2")) {
                    setup.gridRenameText = AnsiToUtf8(name ? name : "");
                    setup.renamingClassification = true;
                    setup.openGridRename = true;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (ImGui::BeginPopupContextItem("classesmenu")) {
            if (ImGui::MenuItem("New Classification")) Report(RegisterGridClassification(*m_State.document, ""), "New Classification");
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_F2, false) && attributes &&
            attributes->IsAttributeIndexValid(setup.classification)) {
            const char *name = attributes->GetAttributeNameByType(setup.classification);
            setup.gridRenameText = AnsiToUtf8(name ? name : "");
            setup.renamingClassification = true;
            setup.openGridRename = true;
        }
    }
    ImGui::EndGroup();
    // Rename a layer type or a classification (a name in use is not taken).
    if (setup.openGridRename) {
        setup.openGridRename = false;
        ImGui::OpenPopup("Rename###gridrename");
    }
    if (ImGui::BeginPopupModal("Rename###gridrename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(240);
        const bool entered = ImGui::InputText("##name", &setup.gridRenameText, ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("OK", ImVec2(80, 0)) || entered) {
            const std::string name = Utf8ToAnsi(setup.gridRenameText);
            Report(setup.renamingClassification ? RenameGridClassification(*m_State.document, setup.classification, name)
                                                : RenameGridLayerType(*m_State.document, setup.layerType, name),
                   "Rename");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace vdev
