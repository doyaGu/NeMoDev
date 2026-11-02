#include "workspace/ResourceDrag.h"
#include "workspace/ObjectDrag.h"
#include <filesystem>
// The Setups of the render objects: 3D entities, cameras, lights, sprites, materials, meshes, textures, grids.

#include "ui/setup/SetupWindows.h"
#include "workspace/EditorWorkspace.h"
#include "CKDefines.h"
#include "CKEnums.h"
#include "VxDefines.h"
#include "editor/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>


#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "imgui_internal.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/FileDialogs.h"
#include "ui/controls/TypeAhead.h"
#include "core/runtime/TextEncoding.h"
#include "ui/setup/SetupWindowsDetail.h"
#include "ui/schematic/Snapshots.h"

namespace vdev {
using namespace setup;

void SetupWindows::Entity3DPage(ObjectRef target, bool sized, bool details,
                                const std::function<void(const DevPage &page)> &fourth, Setup *setup) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "entity3d", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target, {"entity3d.current-mesh", "entity3d.pickable", "entity3d.meshes"}, propertyData))
        return;
    const auto values = propertyData.Values();
    // Dev: cameras and lights show Position and Orientation only.
    const DevPage page;
    const ShownTransform &shown = Shown(target);
    float ex = 0, ey = 0, ez = 0;
    Vx3DMatrixToEulerAngles(shown.world, &ex, &ey, &ez);
    const float worldEuler[3] = {ex * RAD_TO_DEG, ey * RAD_TO_DEG, ez * RAD_TO_DEG};
    Vx3DMatrixToEulerAngles(shown.local, &ex, &ey, &ez);
    const float localEuler[3] = {ex * RAD_TO_DEG, ey * RAD_TO_DEG, ez * RAD_TO_DEG};
    // A group at x (0, 200, 400): its title (and button), X, Y and Z in the
    // World and the Local (the parent's) referential.
    auto group = [&](float x, const char *title, float titleRight, const char *button, float buttonRight,
                     const float *world, const float *local, const char *label, const char *field, float divisor,
                     const std::function<void()> &press) {
        page.Label(x + 20, 2, titleRight, 17, title);
        if (button) {
            const ImVec2 a = page.At(x + 105, 1), b = page.At(buttonRight, 17);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 0.0f));
            if (ImGui::Button(button, ImVec2(b.x - a.x, b.y - a.y)))
                press();
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
                PreviewNumbers(id, ("entity3d.world-" + std::string(field)).c_str(), w, 3, divisor);
            page.Place(x + 150, y, x + 220, y + 20);
            if (ImGui::DragFloat("##local", &l[i], 0.01f, 0.0f, 0.0f, "%.4f"))
                PreviewNumbers(id, ("entity3d.local-" + std::string(field)).c_str(), l, 3, divisor);
            ImGui::PopID();
        }
        ImGui::PopID();
    };
    const VxVector position = Translation(shown.world), localPosition = Translation(shown.local);
    const float p[3] = {position.x, position.y, position.z}, lp[3] = {localPosition.x, localPosition.y, localPosition.z};
    group(0, "Position", 95, "Reset World Matrix", 195, p, lp, "Set Position", "position", 1.0f,
          [&] { RunSetupCommand("setup.reset-world-matrix", {ObjectArgument("target", target)}); });
    group(200, "Orientation", 295, nullptr, 0, worldEuler, localEuler, "Set Orientation", "orientation", RAD_TO_DEG,
          nullptr);
    if (sized) {
        const VxVector scale = ScaleOf(shown.world), localScale = ScaleOf(shown.local);
        const float sc[3] = {scale.x, scale.y, scale.z}, ls[3] = {localScale.x, localScale.y, localScale.z};
        group(400, "Scale", 465, "Set as Unit", 574, sc, ls, "Set Scale", "scale", 1.0f,
              [&] { RunSetupCommand("setup.scale-unit", {ObjectArgument("target", target)}); });
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
    static std::set<ObjectRef> none;
    auto &rows = setup ? setup->meshRows : none;
    for (auto it = rows.begin(); it != rows.end();) {
        const bool held = std::any_of(data.Rows("meshes").begin(), data.Rows("meshes").end(),
                                      [&](const auto &row) { return SetupFields(row.fields).Object("target") == *it; });
        it = held ? std::next(it) : rows.erase(it);
    }
    auto removeMeshes = [&] {
        const std::vector<ObjectRef> chosen(rows.begin(), rows.end());
        if (chosen.empty())
            return;
        EditObjectList(id, "entity3d.meshes", {}, chosen);
        FlushPending();
        rows.clear();
    };
    ImGui::SetCursorScreenPos(page.At(20, 130));
    if (ImGui::BeginChild("meshes", ImVec2(190.0f * page.s, 130.0f * page.s), ImGuiChildFlags_Borders)) {
        TypeAhead typeAhead;
        for (size_t i = 0; i < data.Rows("meshes").size(); ++i) {
            const SetupFields mesh(data.Rows("meshes")[i].fields);
            ImGui::PushID(i);
            const auto meshId = mesh.Object("target");
            const bool selected = rows.count(meshId) != 0;
            const std::string text = mesh.Text("name") + " (Mesh)";
            if (typeAhead.Selectable(text.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    Open(meshId);
                else if (!ImGui::GetIO().KeyCtrl) rows = {meshId};
                else if (selected) rows.erase(meshId);
                else rows.insert(meshId);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) rows = {meshId};
            if (ImGui::BeginPopupContextItem("meshmenu")) {
                const bool single = rows.size() == 1;
                if (ImGui::MenuItem("Show Mesh Setup", nullptr, false, single))
                    Open(*rows.begin());
                ImGui::Separator();
                if (ImGui::MenuItem("Set As Current", nullptr, false, single)) {
                    const auto chosen = *rows.begin();
                    PreviewValue(id, "entity3d.current-mesh", chosen);
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
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(EDITOR_OBJECT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            const auto reference = ObjectDragSource(payload->Data, size_t(payload->DataSize));
            SetupPageData dragged;
            const bool mesh =
                ReadSetupPage(Client(m_CurrentOwner), reference, -1, dragged) && dragged.kind == Kind::Mesh;
            const bool held = std::any_of(data.Rows("meshes").begin(), data.Rows("meshes").end(), [&](const auto &row) {
                return SetupFields(row.fields).Object("target") == reference;
            });
            if (mesh && !held && !payload->IsDelivery())
                ImGui::SetTooltip("Add Mesh %s", dragged.name.c_str());
            if (mesh && !held && payload->IsDelivery()) {
                EditObjectList(id, "entity3d.meshes", {reference});
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
        std::vector<SetupObjectRow> choices;
        ReadSetupChoices(Client(m_CurrentOwner), CKCID_MESH, choices);
        for (const auto &object : choices) {
            ImGui::PushID(int(object.object.object));
            if (typeAhead.Selectable(object.name.c_str())) {
                const auto chosen = object.object;
                EditObjectList(id, "entity3d.meshes", {chosen});
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
    const auto current = data.Object("mesh");
    page.Label(240, 130, 400, 150, data.Text("mesh-name").c_str());
    if (current) {
        char text[4][32];
        std::snprintf(text[0], sizeof(text[0]), "%d", int(data.Integer("vertices")));
        std::snprintf(text[1], sizeof(text[1]), "%d", int(data.Integer("faces")));
        std::snprintf(text[2], sizeof(text[2]), "%d", int(data.Integer("channel-count")));
        std::snprintf(text[3], sizeof(text[3]), "%s", data.Boolean("patch") ? "Patch" : "Triangular");
        static const char *const names[] = {"Vertices:", "Faces:", "Channels:", "Type:"};
        for (int i = 0; i < 4; ++i) {
            const float y = 160 + 15.0f * i;
            page.Label(240, y, 300, y + 15, names[i]);
            page.Label(320, y, 400, y + 15, text[i]);
        }
    }
    if (data.Boolean("body-part")) {
        static const int button[5] = {1145, 1141, 1142, 1142, 1145};
        const auto character = data.Object("character");
        page.Label(420, 108, 500, 130, "Character:");
        if (DevBitmapButton("charactersetup", page.At(500, 110), button, false, bool(character), page.s,
                            "Show Character Setup") &&
            character)
            Open(character);
        page.Label(520, 111, 620, 132, data.Text("character-name").c_str());
    } else if (data.Object("parent")) {
        page.Label(420, 108, 500, 130, "Parent:");
        page.Label(500, 108, 620, 130, data.Text("parent-name").c_str());
    }
    bool pickable = values.Boolean("entity3d.pickable");
    if (page.Check("Pickable", 620, 111, pickable))
        PreviewValue(id, "entity3d.pickable", pickable);
    page.Label(635, 109, 775, 125, "Pickable");
    page.Label(635, 124, 800, 140, "(Hold shift to pick it anyway)");
    page.End(800, 262);
}

void SetupWindows::CameraPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "camera", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target,
                        {"entity3d.world-position", "camera.target", "camera.projection", "camera.aspect-ratio-active",
                         "camera.ratio", "camera.fov", "camera.ortho-zoom", "camera.near-clip", "camera.far-clip"},
                        propertyData))
        return;
    const auto values = propertyData.Values();
    const DevPage page;
    Entity3DPage(target, false, false);
    const auto cameraTarget = data.Object("target");
    if (cameraTarget) {
        const auto to = VectorOf(data, "target-position");
        char text[32];
        std::snprintf(text, sizeof(text), "%.3f", float(data.Real("distance")));
        page.Label(430, 55, 580, 70, "Distance from camera");
        page.Label(430, 70, 530, 85, "to target:");
        page.Label(540, 70, 600, 88, text);
        // Target position (documentation "Camera Setup": "Coordinates of the
        // target in the world coordinate system"): the target moves.
        page.Label(430, 90, 560, 105, "Target position");
        float at[3] = {to.x, to.y, to.z};
        page.Place(430, 105, 640, 125);
        if (ImGui::DragFloat3("##targetposition", at, 0.05f, 0.0f, 0.0f, "%.3f"))
            PreviewValue(cameraTarget, "entity3d.world-position", VxVector(at[0], at[1], at[2]));
    }
    // Dev.exe sub_4584A0: the 3D Layout's list of the objects shown in Player
    // Mode (not the object's visibility); the 3D Layout draws the camera's
    // frustum (its own state, not the camera's).
    page.Line(25, 110, 26, 150);
    bool shown = data.Boolean("player-shown");
    if (page.Check("Show In Player Mode", 45, 114, shown))
        RunSetupCommand("setup.player-shown", {ObjectArgument("target", target), IntegerArgument("value", shown)});
    page.Label(60, 112, 210, 128, "Show In Player Mode");
    bool cone = data.Boolean("cone");
    if (page.Check("Show cone", 45, 134, cone))
        RunSetupCommand("setup.cone-shown", {ObjectArgument("target", target), IntegerArgument("value", cone)});
    page.Label(60, 132, 200, 148, "Show cone");
    if (data.Boolean("targeted")) {
        page.Label(20, 190, 80, 210, "Target");
        ObjectRef chosen{};
        if (ObjectChoice("##target", CKCID_3DENTITY, cameraTarget, data.Text("target-name"), &chosen,
                         page.Place(80, 190, 210, 210)))
            PreviewValue(id, "camera.target", chosen);
    }

    // Projection Type.
    page.Label(220, 110, 400, 130, "Projection Type");
    page.Line(225, 130, 226, 170);
    const int projection = int(values.Integer("camera.projection"));
    const auto perspectiveType = uint32_t(data.Integer("perspective-type"));
    const auto orthographicType = uint32_t(data.Integer("orthographic-type"));
    bool perspective = projection == perspectiveType, orthographic = !perspective;
    if (page.Check("Perspective", 245, 134, perspective))
        PreviewValue(id, "camera.projection", perspectiveType);
    page.Label(260, 132, 400, 148, "Perspective");
    if (page.Check("Orthographic", 245, 154, orthographic))
        PreviewValue(id, "camera.projection", orthographicType);
    page.Label(260, 152, 400, 168, "Orthographic");

    // Aspect Ratio: Dev's presets (Custom, Normal 4:3, Extended 16:9,
    // Widescreen 7:3, Panoramic 20:7).
    page.Label(220, 190, 310, 210, "Aspect Ratio");
    bool aspect = values.Boolean("camera.aspect-ratio-active");
    if (page.Check("Active", 325, 194, aspect))
        PreviewValue(id, "camera.aspect-ratio-active", aspect);
    page.Label(340, 190, 400, 210, "Active");
    page.Line(225, 210, 226, 270);
    const auto aspectRatio = Vector2Of(values, "camera.ratio");
    const int width = int(aspectRatio.x), height = int(aspectRatio.y);
    static const char *const types[] = {"Custom", "Normal", "Extended", "Widescreen", "Panoramic"};
    static const int RATIOS[5][2] = {{0, 0}, {4, 3}, {16, 9}, {7, 3}, {20, 7}};
    int type = 0;
    for (int r = 1; r < 5; ++r)
        if (width * RATIOS[r][1] == height * RATIOS[r][0]) type = r;
    page.Label(240, 210, 300, 230, "Type");
    page.Place(300, 210, 400, 230);
    int picked = type;
    if (TypeAheadCombo("##aspecttype", &picked, types, 5) && picked != type && picked > 0)
        PreviewValue(id, "camera.ratio", Vx2DVector(float(RATIOS[picked][0]), float(RATIOS[picked][1])));
    int x = width, y = height;
    page.Label(240, 230, 270, 250, "X :");
    page.Place(270, 230, 310, 250);
    if (ImGui::DragInt("##aspectx", &x, 0.1f, 1, 10000) && x > 0)
        PreviewValue(id, "camera.ratio", Vx2DVector(float(x), float(height)));
    page.Label(330, 230, 360, 250, "Y :");
    page.Place(360, 230, 400, 250);
    if (ImGui::DragInt("##aspecty", &y, 0.1f, 1, 10000) && y > 0)
        PreviewValue(id, "camera.ratio", Vx2DVector(float(width), float(y)));
    char ratio[32];
    std::snprintf(ratio, sizeof(ratio), "%.4f", height ? static_cast<float>(width) / height : 0.0f);
    page.Label(240, 250, 295, 270, "Ratio:");
    page.Label(303, 250, 370, 270, ratio);

    // Perspective: the field of view and the focal length (Dev.exe
    // sub_4531F0 140 / 142: the 35 mm film's, 18 / tan(FOV / 2)), the
    // presets (Custom first).
    page.Label(420, 110, 500, 130, "Perspective");
    const float half = float(values.Real("camera.fov")) * 0.5f;
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
                PreviewValue(id, "camera.fov", 2.0f * std::atan(18.0f / LENSES[i]));
        ImGui::EndCombo();
    }
    page.Line(425, 130, 426, 210);
    float fov = float(values.Real("camera.fov")) * RAD_TO_DEG;
    page.Label(430, 130, 560, 150, "Field of view");
    if (page.EditSlider("##fov", ImVec4(535, 130, 595, 150), ImVec4(455, 150, 590, 170), &fov, 1.0f, 179.0f, "%.2f", 0.1f) &&
        fov > 0.0f && fov < 180.0f)
        PreviewValue(id, "camera.fov", fov / RAD_TO_DEG);
    page.Label(430, 170, 560, 190, "Focal length");
    if (page.EditSlider("##focal", ImVec4(535, 170, 595, 190), ImVec4(455, 190, 590, 210), &focal, 1.0f, 250.0f, "%.2f", 0.5f) &&
        focal > 0.0f)
        PreviewValue(id, "camera.fov", 2.0f * std::atan(18.0f / focal));

    // Orthographic Parameter.
    page.Label(420, 230, 610, 250, "Orthographic Parameter");
    page.Line(425, 250, 426, 290);
    float zoom = float(values.Real("camera.ortho-zoom"));
    page.Label(430, 250, 560, 270, "Ortho zoom");
    if (page.EditSlider("##orthozoom", ImVec4(560, 250, 600, 270), ImVec4(455, 270, 590, 290), &zoom, 0.0f, 0.5f, "%.3f", 0.001f))
        PreviewValue(id, "camera.ortho-zoom", zoom);

    // Clipping.
    page.Label(620, 110, 750, 130, "Clipping");
    page.Line(625, 130, 626, 210);
    float nearClip = float(values.Real("camera.near-clip")), farClip = float(values.Real("camera.far-clip"));
    page.Label(640, 130, 730, 150, "Near clip");
    if (page.EditSlider("##near", ImVec4(735, 130, 795, 150), ImVec4(645, 150, 770, 170), &nearClip, 0.01f,
                        std::max(100.0f, farClip), "%.2f", 0.05f)) {
        // Documentation "Camera Setup": "Near clip must be smaller than far clip".
        if (nearClip < farClip)
            PreviewValue(id, "camera.near-clip", nearClip);
        else
            Warn("Near clip must be smaller than far clip");
    }
    page.Label(640, 170, 730, 190, "Far clip");
    if (page.EditSlider("##far", ImVec4(735, 170, 795, 190), ImVec4(645, 190, 770, 210), &farClip, 1.0f,
                        std::max(5000.0f, farClip), "%.2f", 1.0f)) {
        if (farClip > nearClip)
            PreviewValue(id, "camera.far-clip", farClip);
        else
            Warn("Near clip must be smaller than far clip");
    }
    page.End(800, 290);
}

void SetupWindows::LightPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "light", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target,
                        {"light.specular", "light.type", "light.target", "light.power", "light.active", "light.color",
                         "light.range", "light.constant-attenuation", "light.linear-attenuation",
                         "light.quadratic-attenuation", "light.hot-spot", "light.fall-off", "light.fall-off-shape"},
                        propertyData))
        return;
    const auto values = propertyData.Values();
    const DevPage page;
    Entity3DPage(target, false, false);
    // Dev.exe sub_4584A0: the 3D Layout's list of the objects shown in Player
    // Mode (not the object's visibility).
    page.Line(25, 110, 26, 170);
    bool shown = data.Boolean("player-shown"), specular = values.Boolean("light.specular");
    if (page.Check("Show In Player Mode", 45, 114, shown))
        RunSetupCommand("setup.player-shown", {ObjectArgument("target", target), IntegerArgument("value", shown)});
    page.Label(60, 112, 210, 128, "Show In Player Mode");
    if (page.Check("Specular", 45, 134, specular))
        PreviewValue(id, "light.specular", specular);
    page.Label(60, 132, 200, 148, "Specular");
    const char *types[] = {"Point", "Spot", "Directional"};
    const int lightType = int(values.Integer("light.type"));
    int type = lightType == VX_LIGHTSPOT ? 1 : lightType == VX_LIGHTDIREC ? 2 : 0;
    page.Label(20, 170, 120, 190, "Type");
    page.Place(120, 170, 210, 190);
    if (TypeAheadCombo("##type", &type, types, 3))
        PreviewValue(id, "light.type", type == 1 ? VX_LIGHTSPOT : type == 2 ? VX_LIGHTDIREC : VX_LIGHTPOINT);
    if (data.Boolean("targeted")) {
        ObjectRef chosen{};
        page.Label(20, 190, 80, 210, "Target");
        if (ObjectChoice("##target", CKCID_3DENTITY, data.Object("target"), data.Text("target-name"), &chosen,
                         page.Place(80, 190, 210, 210)))
            PreviewValue(id, "light.target", chosen);
    }
    // The power (not in Dev's page; CKLight::SetPower).
    float power = float(values.Real("light.power"));
    page.Label(20, 215, 120, 235, "Power");
    page.Place(120, 215, 210, 235);
    if (ImGui::DragFloat("##power", &power, 0.01f, 0.0f, 10.0f, "%.2f"))
        PreviewValue(id, "light.power", power);

    // Influence.
    page.Label(220, 110, 300, 130, "Influence");
    bool active = values.Boolean("light.active");
    if (page.Check("Active", 325, 114, active))
        PreviewValue(id, "light.active", active);
    page.Label(340, 112, 400, 128, "Active");
    page.Line(225, 130, 226, 220);
    const VxColor color = ColorOf(values, "light.color");
    float rgb[3] = {color.r, color.g, color.b};
    page.Label(235, 130, 305, 150, "Color");
    if (page.ColorBar("Color", 345, 130, 375, 150, rgb))
        PreviewValue(id, "light.color", VxColor(rgb[0], rgb[1], rgb[2]));
    float range = float(values.Real("light.range"));
    // Documentation "Light Setup": "Range and Show Influence are not
    // available for Directional Type Lights", nor is the attenuation.
    const bool directional = type == 2;
    ImGui::BeginDisabled(directional);
    page.Label(235, 155, 315, 175, "Range");
    if (page.EditSlider("##range", ImVec4(345, 155, 395, 175), ImVec4(245, 175, 370, 195), &range, 0.0f,
                        std::max(200.0f, range), "%.1f", 0.5f))
        PreviewValue(id, "light.range", range);
    // The 3D Layout draws the light's cone or range (its own state).
    bool influence = data.Boolean("cone");
    if (page.Check("Show Influence", 245, 204, influence))
        RunSetupCommand("setup.cone-shown", {ObjectArgument("target", target), IntegerArgument("value", influence)});
    page.Label(260, 200, 410, 220, "Show Influence");
    ImGui::EndDisabled();

    // Attenuation, and its curve (VIFunctionDrawer, sub_4622F0: the
    // intensity at a distance, 1 / (constant + linear d + quadratic d2),
    // none beyond the range).
    page.Label(420, 110, 610, 130, "Attenuation");
    page.Line(425, 130, 426, 260);
    ImGui::BeginDisabled(directional);
    float constant = float(values.Real("light.constant-attenuation")),
          linear = float(values.Real("light.linear-attenuation")),
          quadratic = float(values.Real("light.quadratic-attenuation"));
    page.Label(440, 130, 540, 150, "Constant");
    if (page.EditSlider("##constant", ImVec4(545, 130, 595, 150), ImVec4(445, 150, 570, 170), &constant, 0.0f, 10.0f, "%.3f"))
        PreviewValue(id, "light.constant-attenuation", constant);
    page.Label(440, 170, 540, 190, "Linear");
    if (page.EditSlider("##linear", ImVec4(545, 170, 595, 190), ImVec4(445, 190, 570, 210), &linear, 0.0f, 10.0f, "%.3f"))
        PreviewValue(id, "light.linear-attenuation", linear);
    page.Label(440, 210, 540, 230, "Quadratic");
    if (page.EditSlider("##quadratic", ImVec4(545, 210, 595, 230), ImVec4(445, 230, 570, 250), &quadratic, 0.0f, 10.0f, "%.3f"))
        PreviewValue(id, "light.quadratic-attenuation", quadratic);
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
            const float divisor = float(values.Real("light.constant-attenuation")) +
                                  float(values.Real("light.linear-attenuation")) * d +
                                  float(values.Real("light.quadratic-attenuation")) * d * d;
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
    float hot = float(values.Real("light.hot-spot")) * RAD_TO_DEG,
          fall = float(values.Real("light.fall-off")) * RAD_TO_DEG, shape = float(values.Real("light.fall-off-shape"));
    page.Label(640, 130, 760, 150, "Hot Spot");
    if (page.EditSlider("##hotspot", ImVec4(760, 130, 800, 150), ImVec4(655, 150, 790, 170), &hot, 0.0f, 180.0f, "%.1f", 0.1f))
        PreviewValue(id, "light.hot-spot", hot / RAD_TO_DEG);
    page.Label(640, 170, 760, 190, "Fall Off");
    if (page.EditSlider("##falloff", ImVec4(760, 170, 800, 190), ImVec4(655, 190, 790, 210), &fall, 0.0f, 180.0f, "%.1f", 0.1f))
        PreviewValue(id, "light.fall-off", fall / RAD_TO_DEG);
    page.Label(640, 210, 760, 230, "Fall Off Shape");
    if (page.EditSlider("##falloffshape", ImVec4(760, 210, 800, 230), ImVec4(655, 230, 790, 250), &shape, 0.0f, 10.0f, "%.2f"))
        PreviewValue(id, "light.fall-off-shape", shape);
    ImGui::EndDisabled();
    page.End(800, 320);
}

void SetupWindows::Entity2DPage(ObjectRef target, bool sprite) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "entity2d", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target,
                        {"entity2d.pickable", "entity2d.homogeneous", "entity2d.clip-to-camera",
                         "entity2d.ratio-offset", "entity2d.position", "entity2d.zorder", "entity2d.background",
                         "entity2d.size", "entity2d.material", "entity2d.source-rect", "entity2d.uv", "entity2d.parent",
                         "entity2d.clip-to-parent", "entity2d.stick-left", "entity2d.stick-right", "entity2d.stick-top",
                         "entity2d.stick-bottom"},
                        propertyData))
        return;
    const auto values = propertyData.Values();
    ImGui::BeginGroup();
    Group("General");
    auto flag = [&](const char *property, const char *label) {
        bool value = values.Boolean(property);
        if (ImGui::Checkbox(label, &value))
            PreviewValue(id, property, value);
    };
    ImGui::Indent(20);
    flag("entity2d.pickable", "Pickable");
    ImGui::SameLine();
    ImGui::TextDisabled("(Hold shift to pick it anyway)");
    const bool homogeneous = values.Boolean("entity2d.homogeneous");
    flag("entity2d.homogeneous", "Homogeneous Coordinates");
    flag("entity2d.clip-to-camera", "Clip To Camera");
    flag("entity2d.ratio-offset", "Relative To Viewport");
    ImGui::Unindent(20);
    Group("Position");
    ImGui::Indent(20);
    const auto position = Vector2Of(values, "entity2d.position"), size = Vector2Of(values, "entity2d.size");
    float xy[2] = {position.x, position.y};
    ImGui::SetNextItemWidth(200);
    if (ImGui::DragFloat2("X / Y", xy, homogeneous ? 0.001f : 1.0f, 0.0f, 0.0f, "%.4f"))
        PreviewValue(id, "entity2d.position", Vx2DVector(xy[0], xy[1]));
    int z = int(values.Integer("entity2d.zorder"));
    ImGui::SetNextItemWidth(80);
    if (ImGui::DragInt("Z Order", &z, 0.2f))
        PreviewValue(id, "entity2d.zorder", z);
    flag("entity2d.background", "Background Sprite");
    ImGui::Unindent(20);
    ImGui::Dummy(ImVec2(1, 10));
    Group("Size");
    ImGui::Indent(20);
    float wh[2] = {size.x, size.y};
    ImGui::SetNextItemWidth(200);
    if (ImGui::DragFloat2("Width / Height", wh, homogeneous ? 0.001f : 1.0f, 0.0f, 0.0f, "%.4f"))
        PreviewValue(id, "entity2d.size", Vx2DVector(wh[0], wh[1]));
    ImGui::Unindent(20);
    ImGui::EndGroup();
    ImGui::SameLine(380);
    ImGui::BeginGroup();
    ObjectRef chosen{};
    if (!sprite) {
        Group("Material:");
        ImGui::SameLine();
        if (ObjectChoice("##material", CKCID_MATERIAL, data.Object("material"), data.Text("material-name"), &chosen))
            PreviewValue(id, "entity2d.material", chosen);
        // Documentation "2D Frame Setup": "Clicking on the icon opens the Material setup".
        ImGui::SameLine();
        ImGui::BeginDisabled(!data.Object("material"));
        if (ImGui::SmallButton(">##materialsetup") && data.Object("material"))
            Open(data.Object("material"));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Show Material Setup");
        ImGui::Dummy(ImVec2(1, 50));
    }
    // 2D Frame "UVMapping" (homogeneous source rectangle); 2D Sprite
    // "Cropping" (the same CK2dEntity source rectangle, in pixels).
    Group(sprite ? "Cropping" : "UVMapping");
    ImGui::SameLine();
    flag("entity2d.source-rect", "Active");
    const auto source = RectOf(values, "entity2d.uv");
    float uv[4] = {source.left, source.top, source.right, source.bottom};
    ImGui::SetNextItemWidth(260);
    if (ImGui::DragFloat4("L / T / R / B", uv, sprite ? 1.0f : 0.005f, 0.0f, 0.0f, sprite ? "%.0f" : "%.3f"))
        PreviewValue(id, "entity2d.uv", VxRect(uv[0], uv[1], uv[2], uv[3]));
    ImGui::Dummy(ImVec2(1, 10));
    Group("Parent");
    ImGui::SameLine();
    if (ObjectChoice("##parent", CKCID_2DENTITY, data.Object("parent"), data.Text("parent-name"), &chosen) &&
        chosen != id)
        PreviewValue(id, "entity2d.parent", chosen);
    // Show Parent Setup (Dev.exe sub_479A10, command 816).
    ImGui::SameLine();
    ImGui::BeginDisabled(!data.Object("parent"));
    if (ImGui::SmallButton(">##parentsetup") && data.Object("parent"))
        Open(data.Object("parent"));
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Show Parent Setup");
    flag("entity2d.clip-to-parent", "Clip to Parent");
    // Stick Left / Right / Top / Bottom: keep the distance to the parent's borders.
    const std::pair<const char *, const char *> sticks[] = {
        {"entity2d.stick-left", "Stick Left"},
        {"entity2d.stick-right", "Stick Right"},
        {"entity2d.stick-top", "Stick Top"},
        {"entity2d.stick-bottom", "Stick Bottom"},
    };
    for (int k = 0; k < 4; ++k) {
        if (k % 2)
            ImGui::SameLine(150);
        flag(sticks[k].first, sticks[k].second);
    }
    ImGui::EndGroup();
}

namespace {
unsigned long long ImageKey(ObjectRef ref) {
    uint64_t hash = ref.document.value;
    hash ^= uint64_t(ref.object) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    hash ^= ref.lifetime + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return (hash | 1ull) << 2;
}
} // namespace
bool SetupWindows::ReadPixels(ObjectRef target, const char *section, uint64_t index, int &width, int &height,
                              std::vector<uint32_t> &pixels) const {
    std::vector<uint32_t> result;
    uint64_t first = 0, revision = 0;
    int w = 0, h = 0;
    do {
        SetupDetails description;
        if (!ReadDetails(target, section, description, {index, first}))
            return false;
        const auto data = description.Values();
        const auto total = data.Integer("total"), next = data.Integer("next");
        const auto &page = data.Integers("pixels");
        if (data.Integer("width") > 32768 || data.Integer("height") > 32768 || total > 67108864 || next <= first ||
            next > total || page.size() != next - first)
            return false;
        if (!first) {
            w = int(data.Integer("width"));
            h = int(data.Integer("height"));
            revision = description.documentRevision;
            result.reserve(size_t(total));
        }
        if (w <= 0 || h <= 0 || uint64_t(w) * uint64_t(h) != total || w != int(data.Integer("width")) ||
            h != int(data.Integer("height")) || revision != description.documentRevision)
            return false;
        for (auto pixel : page) {
            if (pixel > UINT32_MAX)
                return false;
            result.push_back(uint32_t(pixel));
        }
        first = next;
        if (first == total)
            break;
    } while (true);
    width = w;
    height = h;
    pixels = std::move(result);
    return true;
}

void SetupWindows::SpritePage(Setup &setup, ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "bitmap", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target,
                        {"sprite.video-format", "sprite.current-slot", "sprite.save-options", "sprite.transparent",
                         "sprite.transparent-color"},
                        propertyData))
        return;
    const auto values = propertyData.Values();
    ImGui::BeginGroup();
    if (data.Boolean("system-present") && data.Boolean("system-described"))
        ImGui::Text("System Memory Format   %dx%d %d bits  %s", int(data.Integer("system-width")),
                    int(data.Integer("system-height")), int(data.Integer("system-bits")),
                    data.Text("system-format").c_str());
    else
        ImGui::TextUnformatted("System Memory Format   not in system memory");
    int format = int(values.Integer("sprite.video-format"));
    ImGui::TextUnformatted("Desired Video Format");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170);
    const auto &formats = data.Texts("formats");
    auto formatLabel = [&](int f) {
        return f >= 0 && size_t(f) < formats.size() ? formats[size_t(f)] : std::string("Unknown");
    };
    if (ImGui::BeginCombo("##videoformat", formatLabel(format).c_str())) {
        TypeAhead typeAhead;
        for (int f = 1; f <= 23; ++f)
            if (typeAhead.Selectable(formatLabel(f).c_str(), f == format))
                PreviewValue(id, "sprite.video-format", f);
        ImGui::EndCombo();
    }
    ImGui::EndGroup();
    ImGui::SameLine(460);
    ImGui::BeginGroup();
    Group("Video Memory Information");
    {
        if (data.Boolean("video-present") && data.Boolean("video-described")) {
            ImGui::Text("Width:  %d", int(data.Integer("video-width")));
            ImGui::Text("Height: %d", int(data.Integer("video-height")));
            ImGui::Text("Bits:   %d", int(data.Integer("video-bits")));
            ImGui::Text("Format: %s", data.Text("video-format").c_str());
        } else
            ImGui::TextUnformatted(data.Boolean("video-present") ? "Not available" : "not in video memory");
    }
    ImGui::EndGroup();
    ImGui::SameLine(760);
    ImGui::BeginGroup();
    const int slots = int(data.Integer("slots")), slot = int(data.Integer("slot"));
    auto go = [&](const char *label, int to) {
        ImGui::BeginDisabled(to < 0 || to >= slots || to == slot);
        if (ImGui::Button(label, ImVec2(60, 0)))
            PreviewValue(id, "sprite.current-slot", to);
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
    if (ImGui::Button("Add Sprites", ImVec2(170, 0)))
        RunSlotCommand(setup, target, SlotCommand::Add);
    ImGui::BeginDisabled(slots <= 1);
    if (ImGui::Button("Remove Current Slot", ImVec2(170, 0))) setup.confirmRemoveSlot = true;
    ImGui::EndDisabled();
    ImGui::EndGroup();
    ImGui::Separator();
    // Save Options and Color Key Transparency, left of the preview.
    ImGui::BeginGroup();
    Group("Save Options");
    const char *options[] = {"Raw Data", "External", "Specific Format", "Global Settings", "Original File"};
    int option = std::max(0, std::min(4, int(values.Integer("sprite.save-options"))));
    ImGui::SetNextItemWidth(160);
    if (TypeAheadCombo("##saveoptions", &option, options, 5))
        PreviewValue(id, "sprite.save-options", option);
    if (option == CKTEXTURE_IMAGEFORMAT)
        SaveFormatControls(target);
    ImGui::Dummy(ImVec2(1, 6));
    Group("Color Key Transparency");
    bool key = values.Boolean("sprite.transparent");
    if (ImGui::Checkbox("Active", &key))
        PreviewValue(id, "sprite.transparent", key);
    const auto keyColor = ColorOf(values, "sprite.transparent-color");
    float rgb[3] = {keyColor.r, keyColor.g, keyColor.b};
    if (ImGui::ColorEdit3("Picking", rgb, ImGuiColorEditFlags_NoInputs))
        PreviewValue(id, "sprite.transparent-color", VxColor(rgb[0], rgb[1], rgb[2], 1.0f));
    ImGui::SameLine();
    if (ImGui::Button(setup.pickingColor ? "Picking..." : "Pick")) setup.pickingColor = !setup.pickingColor;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick a color on the sprite");
    ImGui::TextDisabled("Activating the color key destroys the alpha channel.");
    ImGui::EndGroup();
    // Preview of the current slot.
    ImGui::SameLine(400);
    const int width = int(data.Integer("width")), height = int(data.Integer("height"));
    const auto colorKey = ImageKey(target), alphaKey = colorKey | 1ull;
    if (setup.shownTexture != target || setup.shownSlot != slot || ++setup.textureFrames > 120) {
        setup.shownTexture = target;
        setup.shownSlot = slot;
        setup.textureFrames = 0;
        std::vector<uint32_t> pixels;
        int w = 0, h = 0;
        if (ReadPixels(target, "bitmap-pixels", uint64_t(slot), w, h, pixels)) {
            PixelTexture(colorKey, w, h, pixels.data(), w * 4, false);
        }
    }
    SlotPreview(setup, target, colorKey, width, height, 220.0f, false);
    ImGui::Separator();
    Entity2DPage(target, true);
}

void SetupWindows::SlotPreview(Setup &setup, ObjectRef target, unsigned long long key, int width, int height,
                               float side, bool alpha) {
    SetupDetails description;
    if (!ReadDetails(target, "bitmap", description))
        return;
    const auto data = description.Values();
    const int slot = int(data.Integer("slot"));
    const bool texture = data.Boolean("texture");
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
            SetupDetails pixel;
            if (ReadDetails(target, "bitmap-pixel", pixel, {uint64_t(slot), uint64_t(px), uint64_t(py)})) {
                const auto value = uint32_t(pixel.Values().Integer("pixel"));
                const VxColor color(int((value >> 16) & 255), int((value >> 8) & 255), int(value & 255));
                PreviewValue(target, texture ? "texture.transparent-color" : "sprite.transparent-color", color);
            }
        }
    }
    if (setup.pickingColor && !alpha && ImGui::IsItemDeactivated()) setup.pickingColor = false;
    // A Data Resources image dropped (Dev.exe sub_48AC30): on the image, the
    // current slot's (Ctrl or Shift: a new slot); on the alpha, the alpha
    // channel's (Ctrl or Shift: which channels first).
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiIO &io = ImGui::GetIO();
        const bool more = io.KeyCtrl || io.KeyShift;
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(EDITOR_RESOURCE_FILE_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            uint32_t source = 0;
            std::vector<ResourceFile> files;
            if (!ReadResourceDrag(payload->Data, size_t(payload->DataSize), source, files)) {
                ImGui::EndDragDropTarget();
                return;
            }
            const auto &file = files[source];
            const char *what = texture ? "texture" : "sprite";
            if (!payload->IsDelivery()) {
                if (alpha) ImGui::SetTooltip(more ? "Extended replace texture alpha channel with image" : "Replace texture alpha channel with image");
                else ImGui::SetTooltip("%s", more ? (std::string("Add image to ") + what).c_str()
                                                  : (std::string("Replace ") + what + " slot with image").c_str());
            } else if (alpha && texture) {
                if (more) {
                    setup.droppedAlpha = file.path;
                    setup.droppedAlphaSlot = slot;
                    setup.droppedAlphaRevision = description.documentRevision;
                    ImGui::OpenPopup("alphachannels");
                } else {
                    RunSetupCommand("setup.slot-replace-alpha",
                                    {ObjectArgument("target", target), TextArgument("path", file.path),
                                     IntegerArgument("slot", slot), IntegerArgument("channels", 14),
                                     IntegerArgument("document-revision", description.documentRevision)});
                }
            } else if (!alpha) {
                if (more) {
                    const auto path = EditorText(file.path);
                    const EditorValue paths{EDITOR_TEXT("paths"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {&path, 1}};
                    RunSetupCommand("setup.slot-add",
                                    {ObjectArgument("target", target), paths,
                                     IntegerArgument("document-revision", description.documentRevision)});
                } else
                    RunSetupCommand("setup.slot-replace",
                                    {ObjectArgument("target", target), TextArgument("path", file.path),
                                     IntegerArgument("slot", slot),
                                     IntegerArgument("document-revision", description.documentRevision)});
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
                RunSetupCommand("setup.slot-replace-alpha",
                                {ObjectArgument("target", target), TextArgument("path", setup.droppedAlpha),
                                 IntegerArgument("slot", setup.droppedAlphaSlot), IntegerArgument("channels", MASKS[c]),
                                 IntegerArgument("document-revision", setup.droppedAlphaRevision)});
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
        SlotMenu(setup, target, alpha);
        ImGui::EndPopup();
    }
    if (!alpha) {
        SlotKeys(setup, target);
        RemoveSlotDialog(setup, target);
    }
    ImGui::PopID();
}

void SetupWindows::SaveFormatControls(ObjectRef target) {
    SetupDetails description;
    if (!ReadDetails(target, "bitmap-format", description))
        return;
    const auto data = description.Values();
    const auto &extensions = data.Texts("extensions"), &labels = data.Texts("labels");
    if (extensions.empty() || extensions.size() != labels.size())
        return;
    int chosen = -1;
    for (size_t i = 0; i < extensions.size(); ++i)
        if (ImStricmp(extensions[i].c_str(), data.Text("extension").c_str()) == 0)
            chosen = int(i);
    std::vector<const char *> items;
    for (const auto &label : labels)
        items.push_back(label.c_str());
    ImGui::SetNextItemWidth(160);
    int picked = chosen;
    if (TypeAheadCombo("Format", &picked, items.data(), int(items.size())) && picked != chosen && picked >= 0) {
        PreviewValue(target, "bitmap.save-format", extensions[size_t(picked)]);
        FlushPending();
        return;
    }
    if (!data.Boolean("present"))
        return;
    ImGui::TextDisabled("%s", data.Boolean("alpha") ? "Alpha Channel is also saved."
                                                    : "This mode does not support Alpha Channel.");
    if (data.Boolean("options") && ImGui::Button("Format Options..."))
        RunSetupCommand("bitmap.edit-options", {ObjectArgument("target", target)});
    const auto key = data.Text("encoding-key");
    if (m_FormatInfoKey[target] != key) {
        SetupDetails encoding;
        m_FormatInfo[target] =
            ReadDetails(target, "bitmap-encoding", encoding) ? encoding.Values().Text("info") : std::string();
        m_FormatInfoKey[target] = key;
    }
    if (!m_FormatInfo[target].empty())
        ImGui::TextDisabled("%s", m_FormatInfo[target].c_str());
}

void SetupWindows::RunSlotCommand(Setup &setup, ObjectRef target, SlotCommand command) {
    const bool pending = m_PropertyEdit != 0;
    if (!FlushPending())
        return;
    SetupDetails description;
    if (!ReadDetails(target, "bitmap", description))
        return;
    const auto data = description.Values();
    const bool texture = data.Boolean("texture");
    const bool changing = command == SlotCommand::Add || command == SlotCommand::Replace || command == SlotCommand::ReplaceAlpha;
    if (changing && data.Boolean("movie")) {
        Warn("Cannot modify avi");
        return;
    }
    const int slots = int(data.Integer("slots")), slot = int(data.Integer("slot"));
    if (command == SlotCommand::Add || command == SlotCommand::Replace || command == SlotCommand::ReplaceAlpha ||
        command == SlotCommand::Save || command == SlotCommand::SaveAlpha) {
        if ((command == SlotCommand::ReplaceAlpha || command == SlotCommand::SaveAlpha) && !texture)
            return;
        FileDialogOptions options;
        options.save = command == SlotCommand::Save || command == SlotCommand::SaveAlpha;
        options.filter =
            options.save ? L"Bitmap (*.bmp)\0*.bmp\0Targa (*.tga)\0*.tga\0All files (*.*)\0*.*\0"
                         : L"Images (*.bmp;*.tga;*.jpg;*.png;*.tif)\0*.bmp;*.tga;*.jpg;*.jpeg;*.png;*.tif;*.tiff\0All "
                           L"files (*.*)\0*.*\0";
        if (options.save)
            options.extension = L"bmp";
        const auto owner = m_CurrentOwner ? m_CurrentOwner : setup.nameOwner;
        ChooseFile(
            options,
            [this, target, owner, slot, command, after = pending ? m_SubmittedEdit : 0,
             revision = description.documentRevision](const FileDialogResult &selection) {
                if (selection.status != FileDialogResult::Status::Selected || selection.paths.empty())
                    return;
                std::vector<EditorField> fields{{"target", EDITOR_OBJECT, target},
                                                {"document-revision", EDITOR_INTEGER_VALUE, {}, {}, revision}};
                const char *id = "setup.slot-add";
                if (command == SlotCommand::Add) {
                    fields.push_back({"paths", EDITOR_TEXT_LIST});
                    fields.back().texts = selection.paths;
                } else {
                    fields.push_back({"slot", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(slot)});
                    fields.push_back({"path", EDITOR_TEXT_VALUE, {}, selection.paths.front()});
                    id = command == SlotCommand::Replace        ? "setup.slot-replace"
                         : command == SlotCommand::ReplaceAlpha ? "setup.slot-replace-alpha"
                         : command == SlotCommand::Save         ? "setup.slot-save"
                                                                : "setup.slot-save-alpha";
                    if (command == SlotCommand::ReplaceAlpha)
                        fields.push_back({"channels", EDITOR_INTEGER_VALUE, {}, {}, 14});
                }
                const auto args = BorrowEditorFields(fields);
                const auto result = after ? Client(owner).CommandAfterEdit(after, id, args.values, "document-revision")
                                          : Client(owner).Command(id, args.values);
                if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE)
                    Warn("Image file operation failed");
                for (auto &open : m_Instances)
                    if (open.second->reference == target)
                        open.second->textureFrames = 1000;
            },
            command == SlotCommand::Add);
        return;
    }
    const float shown = setup.zoom;
    switch (command) {
    case SlotCommand::Next:
    case SlotCommand::Previous:
        if (slots > 1) {
            PreviewValue(target, texture ? "texture.current-slot" : "sprite.current-slot",
                         command == SlotCommand::Next ? (slot + 1) % slots : (slot + slots - 1) % slots);
            FlushPending();
            setup.textureFrames = 1000;
        }
        break;
    case SlotCommand::ZoomIn: setup.zoom = std::min(50.0f, (shown > 0.0f ? shown : 1.0f) * 2.0f); break;
    case SlotCommand::ZoomOut: setup.zoom = std::max(0.07f, (shown > 0.0f ? shown : 1.0f) * 0.5f); break;
    case SlotCommand::Zoom1: setup.zoom = 1.0f; break;
    case SlotCommand::ZoomFit: setup.zoom = 0.0f; break;
    default:
        break;
    }
}

void SetupWindows::SlotKeys(Setup &setup, ObjectRef target) {
    const ImGuiIO &io = ImGui::GetIO();
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || io.WantTextInput || io.KeyCtrl || io.KeyAlt ||
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return;
    SetupDetails description;
    if (!ReadDetails(target, "bitmap", description))
        return;
    const bool texture = description.Values().Boolean("texture");
    auto released = [](ImGuiKey key) { return ImGui::IsKeyReleased(key); };
    if (released(ImGuiKey_O))
        RunSlotCommand(setup, target, SlotCommand::Add);
    else if (released(ImGuiKey_R))
        RunSlotCommand(setup, target, io.KeyShift && texture ? SlotCommand::ReplaceAlpha : SlotCommand::Replace);
    else if (released(ImGuiKey_S))
        RunSlotCommand(setup, target, SlotCommand::Save);
    else if (released(ImGuiKey_N))
        RunSlotCommand(setup, target, io.KeyShift ? SlotCommand::Previous : SlotCommand::Next);
    else if (released(ImGuiKey_P))
        RunSlotCommand(setup, target, SlotCommand::Previous);
    else if (released(ImGuiKey_KeypadAdd) || (released(ImGuiKey_Equal) && io.KeyShift))
        RunSlotCommand(setup, target, SlotCommand::ZoomIn);
    else if (released(ImGuiKey_KeypadSubtract) || released(ImGuiKey_Minus))
        RunSlotCommand(setup, target, SlotCommand::ZoomOut);
    else if (released(ImGuiKey_KeypadDivide) || released(ImGuiKey_Slash))
        RunSlotCommand(setup, target, SlotCommand::Zoom1);
    else if (released(ImGuiKey_KeypadMultiply) || (released(ImGuiKey_8) && io.KeyShift))
        RunSlotCommand(setup, target, SlotCommand::ZoomFit);
}

void SetupWindows::RemoveSlotDialog(Setup &setup, ObjectRef target) {
    SetupDetails description;
    if (!ReadDetails(target, "bitmap", description))
        return;
    const auto data = description.Values();
    if (setup.confirmRemoveSlot) {
        setup.confirmRemoveSlot = false;
        ImGui::OpenPopup("Warning!###removeslot");
    }
    if (!ImGui::BeginPopupModal("Warning!###removeslot", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(data.Boolean("texture") ? "Remove current texture slot ?" : "Remove current sprite slot?");
    if (ImGui::Button("Yes", ImVec2(80, 0))) {
        RunSetupCommand("setup.slot-remove",
                        {ObjectArgument("target", target), IntegerArgument("slot", data.Integer("slot")),
                         IntegerArgument("document-revision", description.documentRevision)});
        setup.textureFrames = 1000;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("No", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void SetupWindows::SlotMenu(Setup &setup, ObjectRef target, bool alpha) {
    SetupDetails description;
    if (!ReadDetails(target, "bitmap", description))
        return;
    const auto data = description.Values();
    const bool texture = data.Boolean("texture");
    const int slots = int(data.Integer("slots")), width = int(data.Integer("width")),
              height = int(data.Integer("height"));
    auto act = [&](const char *id) {
        RunSetupCommand(id, {ObjectArgument("target", target), IntegerArgument("slot", data.Integer("slot")),
                             IntegerArgument("document-revision", description.documentRevision)});
        setup.textureFrames = 1000;
    };
    if (alpha) {
        if (ImGui::MenuItem("Replace alpha...", "Shift-R"))
            RunSlotCommand(setup, target, SlotCommand::ReplaceAlpha);
        ImGui::Separator();
        auto op = [&](const char *label, const char *id) {
            if (ImGui::MenuItem(label))
                act(id);
        };
        op("Set alpha to opaque", "setup.slot-alpha-opaque");
        op("Set alpha to translucent", "setup.slot-alpha-translucent");
        op("Set alpha to transparent", "setup.slot-alpha-transparent");
        op("Invert alpha", "setup.slot-invert-alpha");
        if (ImGui::MenuItem("Save Alpha Channel..."))
            RunSlotCommand(setup, target, SlotCommand::SaveAlpha);
        return;
    }
    if (ImGui::MenuItem("Add new slot...", "O"))
        RunSlotCommand(setup, target, SlotCommand::Add);
    if (ImGui::MenuItem("Replace slot...", "R"))
        RunSlotCommand(setup, target, SlotCommand::Replace);
    if (ImGui::MenuItem("Remove slot", nullptr, false, slots > 1)) setup.confirmRemoveSlot = true;
    if (ImGui::MenuItem("Reload slot")) {
        act("setup.slot-reload");
    }
    if (ImGui::MenuItem("Save slot...", "S"))
        RunSlotCommand(setup, target, SlotCommand::Save);
    if (texture) {
        if (ImGui::MenuItem("Resize Slot...")) {
            setup.openResize = true;
            setup.resizeWidth = width;
            setup.resizeHeight = height;
        }
        auto op = [&](const char *label, const char *id) {
            if (ImGui::MenuItem(label))
                act(id);
        };
        op("Invert Slot", "setup.slot-invert");
        op("HeightMap => NormalMap", "setup.slot-normal-map");
        op("HeightMap => BumpMap", "setup.slot-bump-map");
    }
    ImGui::Separator();
    const float shown = setup.zoom > 0.0f ? setup.zoom : 0.0f;
    if (ImGui::MenuItem("Zoom In", "+", false, shown == 0.0f || shown < 50.0f))
        RunSlotCommand(setup, target, SlotCommand::ZoomIn);
    if (ImGui::MenuItem("Zoom Out", "-", false, shown == 0.0f || shown > 0.07f))
        RunSlotCommand(setup, target, SlotCommand::ZoomOut);
    if (ImGui::MenuItem("Zoom 1:1", "/"))
        RunSlotCommand(setup, target, SlotCommand::Zoom1);
    if (ImGui::MenuItem("Zoom Best Fit", "*"))
        RunSlotCommand(setup, target, SlotCommand::ZoomFit);
}

void SetupWindows::MaterialPage(Setup &setup, ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "material", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target,
                        {"material.mode",
                         "material.ambient",
                         "material.diffuse",
                         "material.specular",
                         "material.emissive",
                         "material.alpha",
                         "material.power",
                         "material.both-sided",
                         "material.fill-mode",
                         "material.shade-mode",
                         "material.alpha-test",
                         "material.alpha-func",
                         "material.alpha-ref",
                         "material.alpha-blend",
                         "material.source-blend",
                         "material.dest-blend",
                         "material.zwrite",
                         "material.zfunc",
                         "material.texture",
                         "material.texture-blend",
                         "material.texture-min-mode",
                         "material.texture-mag-mode",
                         "material.texture-address",
                         "material.perspective-correct",
                         "material.texture-border-color",
                         "material.effect"},
                        propertyData))
        return;
    const auto values = propertyData.Values();
    const DevPage page;
    // Mode (sub_4634E0 712): Opaque, Transparent, Mask set the blend, Z and
    // alpha test settings; Custom shows them to edit. The combo keeps Custom
    // once chosen (sub_46A670).
    int mode = int(values.Integer("material.mode"));
    if (setup.materialCustom) mode = 3;
    // The colors' hue bars (each keeps its alpha; the diffuse's is the Alpha
    // slider).
    page.Line(22, 9, 23, 121);
    const char *const COLORS[] = {"material.ambient", "material.diffuse", "material.specular", "material.emissive"};
    const char *const COLOR_LABELS[] = {"Ambient", "Diffuse", "Specular", "Emissive"};
    for (int i = 0; i < 4; ++i) {
        const float y = 10.0f + 30.0f * i;
        const VxColor c = ColorOf(values, COLORS[i]);
        float rgb[3] = {c.r, c.g, c.b};
        page.Label(30, y, 90, y + 20, COLOR_LABELS[i]);
        if (page.ColorBar(COLOR_LABELS[i], 90, y, 120, y + 20, rgb))
            PreviewValue(id, COLORS[i], VxColor(rgb[0], rgb[1], rgb[2]));
    }
    // Alpha with Transparent and Custom (sub_469C90).
    if (mode == 1 || mode == 3) {
        int alpha = int(values.Integer("material.alpha"));
        page.Label(123, 40, 160, 60, "Alpha");
        page.Place(160, 40, 225, 60);
        if (ImGui::SliderInt("##alpha", &alpha, 0, 255))
            PreviewValue(id, "material.alpha", alpha);
    }
    float power = float(values.Real("material.power"));
    page.Label(123, 70, 160, 90, "Power");
    page.Place(160, 70, 225, 90);
    if (ImGui::SliderFloat("##power", &power, 0.0f, 100.0f, "%.0f"))
        PreviewValue(id, "material.power", power);
    page.Line(3, 130, 435, 131);
    page.Line(230, 9, 231, 120);
    const char *modes[] = {"Opaque", "Transparent", "Mask", "Custom"};
    page.Label(235, 10, 350, 30, "Mode");
    page.Place(350, 10, 440, 30);
    const int before = mode;
    if (TypeAheadCombo("##mode", &mode, modes, 4) && mode != before) {
        setup.materialCustom = mode == 3;
        if (mode != 3)
            PreviewValue(id, "material.mode", mode);
    }
    page.Line(235, 40, 440, 41);
    bool twoSided = values.Boolean("material.both-sided");
    page.Label(235, 52, 350, 68, "Both Sided");
    if (page.Check("##bothsided", 350, 55, twoSided))
        PreviewValue(id, "material.both-sided", twoSided);
    const char *fills[] = {"Point", "Wireframe", "Solid"};
    int fill = std::max(0, std::min(2, int(values.Integer("material.fill-mode")) - 1));
    page.Label(235, 70, 350, 90, "Fill Mode");
    page.Place(350, 70, 440, 90);
    if (TypeAheadCombo("##fillmode", &fill, fills, 3))
        PreviewValue(id, "material.fill-mode", fill + 1);
    const char *shades[] = {"Flat", "Gouraud", "Phong"};
    int shade = std::max(0, std::min(2, int(values.Integer("material.shade-mode")) - 1));
    page.Label(235, 90, 350, 110, "Shade Mode");
    page.Place(350, 90, 440, 110);
    if (TypeAheadCombo("##shademode", &shade, shades, 3))
        PreviewValue(id, "material.shade-mode", shade + 1);

    // Mask: the alpha reference; Custom: the alpha test, blend and Z
    // settings too (sub_469C90, sub_469F00, sub_46A440).
    static const char *const BLENDS[] = {"Zero", "One", "Src Color", "Inv Src Color", "Src Alpha", "Inv Src Alpha",
                                         "Dest Alpha", "Inv Dest Alpha", "Dest Color", "Inv Dest Color", "Src Alpha Sat",
                                         "Both Src Alpha", "Both Inv Src Alpha"};
    static const char *const COMPARES[] = {"Never", "Less", "Equal", "Less Equal", "Greater", "Not Equal", "Greater Equal",
                                           "Always"};
    auto compare = [&](const char *label, const char *property) {
        int func = std::max(0, std::min(7, int(values.Integer(property)) - 1));
        if (TypeAheadCombo(label, &func, COMPARES, 8))
            PreviewValue(id, property, func + 1);
    };
    if (mode == 2 || mode == 3) page.Line(455, 9, 456, mode == 3 ? 250 : 80);
    if (mode == 3) {
        bool test = values.Boolean("material.alpha-test");
        page.Label(465, 12, 600, 28, "Alpha Test");
        if (page.Check("##alphatest", 600, 15, test))
            PreviewValue(id, "material.alpha-test", test);
        page.Label(465, 30, 600, 50, "Alpha Test Function");
        page.Place(600, 30, 700, 50);
        compare("##alphafunc", "material.alpha-func");
    }
    if (mode == 2 || mode == 3) {
        int ref = int(values.Integer("material.alpha-ref"));
        page.Label(465, 60, 600, 80, "Alpha Ref Value");
        page.Place(600, 60, 700, 80);
        if (ImGui::SliderInt("##alpharef", &ref, 0, 255))
            PreviewValue(id, "material.alpha-ref", ref);
    }
    if (mode == 3) {
        page.Line(465, 90, 710, 91);
        bool blend = values.Boolean("material.alpha-blend");
        page.Label(465, 102, 600, 118, "Blend");
        if (page.Check("##blend", 600, 105, blend))
            PreviewValue(id, "material.alpha-blend", blend);
        int src = std::max(0, int(values.Integer("material.source-blend")) - 1),
            dst = std::max(0, int(values.Integer("material.dest-blend")) - 1);
        page.Label(465, 120, 600, 140, "Source Blend");
        page.Place(600, 120, 700, 140);
        if (TypeAheadCombo("##sourceblend", &src, BLENDS, 13))
            PreviewValue(id, "material.source-blend", src + 1);
        page.Label(465, 140, 600, 160, "Destination Blend");
        page.Place(600, 140, 700, 160);
        if (TypeAheadCombo("##destblend", &dst, BLENDS, 13))
            PreviewValue(id, "material.dest-blend", dst + 1);
        page.Line(465, 170, 710, 171);
        bool zwrite = values.Boolean("material.zwrite");
        page.Label(465, 182, 600, 198, "Z-Buffer Write");
        if (page.Check("##zwrite", 600, 184, zwrite))
            PreviewValue(id, "material.zwrite", zwrite);
        page.Label(465, 200, 600, 220, "Z Compare Function");
        page.Place(600, 200, 700, 220);
        compare("##zfunc", "material.zfunc");
    }

    // Used By (meshes, 2D entities and 3D sprites: documentation "all the
    // elements curently using the Material").
    page.Label(25, 131, 200, 150, "Used By");
    ImGui::SetCursorScreenPos(page.At(20, 150));
    UsedByList("Used By", data.Rows("users"), ImVec2(190.0f * page.s, 150.0f * page.s));

    // The texture and its settings.
    static const int SETUP[5] = {1145, 1141, 1142, 1142, 1145};
    page.Label(220, 140, 280, 160, "Texture");
    const auto texture = data.Object("texture");
    if (texture && DevBitmapButton("texturesetup", page.At(280, 142), SETUP, false, true, page.s, "Show Texture Setup"))
        Open(texture);
    ObjectRef chosen{};
    if (ObjectChoice("##texture", CKCID_TEXTURE, texture, data.Text("texture-name"), &chosen,
                     page.Place(300, 140, 445, 160)))
        PreviewValue(id, "material.texture", chosen);
    page.Line(230, 159, 231, 280);
    const char *blends[] = {"Decal", "Modulate", "Decal Alpha", "Modulate Alpha", "Decal Mask", "Modulate Mask", "Copy", "Add", "Dot Product 3", "Max"};
    int textureBlend = std::max(0, std::min(9, int(values.Integer("material.texture-blend")) - 1));
    page.Label(235, 160, 350, 180, "Texture Blend");
    page.Place(350, 160, 445, 180);
    if (TypeAheadCombo("##textureblend", &textureBlend, blends, 10))
        PreviewValue(id, "material.texture-blend", textureBlend + 1);
    const char *filters[] = {"Nearest", "Linear", "Mip Nearest", "Mip Linear", "Linear Mip Nearest", "Linear Mip Linear", "Anisotropic"};
    int minFilter = std::max(0, std::min(6, int(values.Integer("material.texture-min-mode")) - 1));
    int magFilter = std::max(0, std::min(6, int(values.Integer("material.texture-mag-mode")) - 1));
    page.Label(235, 180, 350, 200, "Filter Min");
    page.Place(350, 180, 445, 200);
    if (TypeAheadCombo("##filtermin", &minFilter, filters, 7))
        PreviewValue(id, "material.texture-min-mode", minFilter + 1);
    page.Label(235, 200, 350, 220, "Filter Mag");
    page.Place(350, 200, 445, 220);
    if (TypeAheadCombo("##filtermag", &magFilter, filters, 7))
        PreviewValue(id, "material.texture-mag-mode", magFilter + 1);
    const char *addresses[] = {"Wrap", "Mirror", "Clamp", "Border", "Mirror Once"};
    int address = std::max(0, std::min(4, int(values.Integer("material.texture-address")) - 1));
    page.Label(235, 220, 350, 240, "Address Mode");
    page.Place(350, 220, 445, 240);
    if (TypeAheadCombo("##addressmode", &address, addresses, 5))
        PreviewValue(id, "material.texture-address", address + 1);
    bool perspective = values.Boolean("material.perspective-correct");
    page.Label(235, 242, 360, 258, "Perspective Correct");
    if (page.Check("##perspective", 360, 245, perspective))
        PreviewValue(id, "material.perspective-correct", perspective);
    // Border Color with the Border address mode (Dev.exe sub_468440).
    if (values.Integer("material.texture-address") == VXTEXTURE_ADDRESSBORDER) {
        const VxColor border = ColorOf(values, "material.texture-border-color");
        float rgba[4] = {border.r, border.g, border.b, border.a};
        page.Label(235, 260, 335, 280, "Border Color");
        if (page.ColorBar("Border Color", 350, 260, 380, 280, rgba, true))
            PreviewValue(id, "material.texture-border-color", VxColor(rgba[0], rgba[1], rgba[2], rgba[3]));
    }

    // The effect (sub_469070): the render engine's effects, the textures the
    // effect takes (Texture 1 to 3, else its own words for them), its
    // parameter (745: Edit Parameter) and description.
    const int effectCount = int(data.Integer("effect-count"));
    const auto &names = data.Texts("effects");
    std::vector<const char *> items;
    for (const std::string &name : names) items.push_back(name.c_str());
    int effect = std::max(0, std::min(static_cast<int>(items.size()) - 1, int(values.Integer("material.effect"))));
    page.Line(720, 9, 721, 250);
    page.Label(730, 10, 780, 30, "Effect");
    page.Place(780, 10, 930, 30);
    if (TypeAheadCombo("##effect", &effect, items.data(), int(items.size()))) {
        PreviewValue(id, "material.effect", effect);
        // Effect descriptors may change the parameter and extra texture slots.
        FlushPending();
        return;
    }
    if (effect > 0 && effect < effectCount) {
        const auto &textures = data.Rows("effect-textures");
        for (size_t i = 0; i < textures.size(); ++i) {
            const SetupFields row(textures[i].fields);
            const auto extra = row.Object("target");
            const float y = 32.0f + 20.0f * i;
            page.Label(730, y, 810, y + 20, row.Text("label").c_str());
            ImGui::PushID(int(i));
            if (extra && DevBitmapButton("effecttexturesetup", page.At(810, y + 2), SETUP, false, true, page.s,
                                         "Show Texture Setup"))
                Open(extra);
            ObjectRef picked{};
            if (ObjectChoice("##effecttexture", CKCID_TEXTURE, extra, row.Text("name"), &picked,
                             page.Place(830, y, 930, y + 20))) {
                PreviewIndexedProperty(id, "material.effect-texture", {"value", EDITOR_OBJECT, picked},
                                       {uint64_t(i + 1)}, description.documentRevision);
                FlushPending();
            }
            ImGui::PopID();
        }
        if (data.Boolean("has-parameter")) {
            const ImVec2 a = page.At(730, 100), b = page.At(930, 118);
            ImGui::SetCursorScreenPos(a);
            if (ImGui::Button((data.Text("parameter-text") + "##effectparameter").c_str(),
                              ImVec2(b.x - a.x, b.y - a.y)))
                RunSetupCommand("parameter.edit", {ObjectArgument("target", data.Object("parameter"))});
            if (ImGui::IsItemHovered() && !data.Text("parameter-description").empty())
                ImGui::SetTooltip("%s", data.Text("parameter-description").c_str());
        }
        const auto path = data.Text("image-path");
        if (!path.empty()) {
            const auto imageKey = (std::hash<std::string>()(path) | 1ull) << 2 | 3ull;
            static std::set<std::string> tried;
            if (tried.insert(path).second) {
                int width = 0, height = 0;
                std::vector<uint32_t> pixels;
                if (ReadPixels(target, "material-effect-image", uint64_t(effect), width, height, pixels))
                    PixelTexture(imageKey, width, height, pixels.data(), width * 4, false);
            }
            if (ImTextureID image = CachedPixelTexture(imageKey))
                ImGui::GetWindowDrawList()->AddImage(image, page.At(766, 183), page.At(894, 311));
        }
        if (!data.Text("description").empty()) {
            ImGui::SetCursorScreenPos(page.At(730, 120));
            ImGui::PushTextWrapPos(page.At(930, 0).x - ImGui::GetWindowPos().x + ImGui::GetScrollX());
            ImGui::TextUnformatted(data.Text("description").c_str());
            ImGui::PopTextWrapPos();
        }
    }
    page.End(935, 300);
}

void SetupWindows::MeshPage(Setup &setup, ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "mesh", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target,
                        {"mesh.progressive", "mesh.vertices-rendered", "mesh.geo-morph", "mesh.geo-morph-step",
                         "mesh.stripify", "mesh.wrap-mode", "mesh.lit", "mesh.channels"},
                        propertyData))
        return;
    const auto values = propertyData.Values();
    if (data.Integer("channel-count") < 0 || uint64_t(data.Integer("channel-count")) != data.Rows("channels").size())
        return;
    SetupDetails patchData;
    const bool patch = data.Boolean("patch");
    if (patch && !ReadProperties(target, {"patchmesh.iterations"}, patchData))
        return;
    ImGui::BeginGroup();
    bool progressive = values.Boolean("mesh.progressive");
    if (ImGui::Checkbox("Progressive Mesh", &progressive))
        PreviewValue(id, "mesh.progressive", progressive);
    // Progressive meshes (documentation "Mesh Setup"): the vertices
    // rendered, the GeoMorphic Transition and its decimation (0: by halves).
    if (values.Boolean("mesh.progressive")) {
        int vertices = int(values.Integer("mesh.vertices-rendered"));
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderInt("Vertices", &vertices, 0, std::max(1, int(data.Integer("vertices")))))
            PreviewValue(id, "mesh.vertices-rendered", vertices);
        bool geomorph = values.Boolean("mesh.geo-morph");
        if (ImGui::Checkbox("GeoMorphic Transition", &geomorph))
            PreviewValue(id, "mesh.geo-morph", geomorph);
        ImGui::SameLine();
        int step = int(values.Integer("mesh.geo-morph-step"));
        ImGui::SetNextItemWidth(80);
        if (ImGui::InputInt("Decimation", &step, 1, 10))
            PreviewValue(id, "mesh.geo-morph-step", step);
    } else {
        ImGui::Dummy(ImVec2(1, 30));
    }
    // Stripify (VXMESH_STRIPIFY: the mesh is stripified each time it loads).
    bool stripify = values.Boolean("mesh.stripify");
    if (ImGui::Checkbox("Stripify", &stripify))
        PreviewValue(id, "mesh.stripify", stripify);
    const char *wraps[] = {"None", "U", "V", "UV"};
    int wrap = std::max(0, std::min(3, int(values.Integer("mesh.wrap-mode"))));
    ImGui::SetNextItemWidth(120);
    if (TypeAheadCombo("Texture Wrap", &wrap, wraps, 4))
        PreviewValue(id, "mesh.wrap-mode", wrap);
    const char *lits[] = {"Prelit", "Lit"};
    int lit = int(values.Boolean("mesh.lit"));
    ImGui::SetNextItemWidth(120);
    if (TypeAheadCombo("Lit Mode", &lit, lits, 2))
        PreviewValue(id, "mesh.lit", lit != 0);
    // Patch meshes (language pack [Mesh Setup]: "Iterations"): the subdivision
    // steps of the render mesh built from the patches.
    if (patch) {
        int iterations = int(patchData.Values().Integer("patchmesh.iterations"));
        ImGui::SetNextItemWidth(120);
        if (ImGui::SliderInt("Iterations", &iterations, 0, 10))
            PreviewValue(id, "patchmesh.iterations", iterations);
    }
    ImGui::EndGroup();
    ImGui::SameLine(330);
    ImGui::BeginGroup();
    Group("Mesh Info");
    ImGui::Text("Vertices:     %d", int(data.Integer("vertices")));
    ImGui::Text("Faces:        %d", int(data.Integer("faces")));
    ImGui::Text("Channels:     %d", int(data.Integer("channel-count")));
    ImGui::Text("Line Count    %d", int(data.Integer("lines")));
    ImGui::Text("Type:         %s", patch ? "Patch" : "Triangular");
    ImGui::BeginDisabled(patch); // a patch mesh rebuilds its faces from the patches
    if (ImGui::Button("Consolidate"))
        RunSetupCommand("setup.mesh-consolidate", {ObjectArgument("target", target)});
    ImGui::EndDisabled();
    if (patch) {
        ImGui::Dummy(ImVec2(1, 6));
        Group("PatchMesh Info");
        ImGui::Text("Control Points:     %d", int(data.Integer("control-points")));
        ImGui::Text("Vectors:            %d", int(data.Integer("vectors")));
        ImGui::Text("Edges:              %d", int(data.Integer("edges")));
        ImGui::Text("Patches:            %d", int(data.Integer("patches")));
        ImGui::Text("Texture Vertices:   %d", int(data.Integer("texture-vertices")));
    }
    ImGui::EndGroup();
    ImGui::SameLine(620);
    ImGui::BeginGroup();
    Group("Geometry Info (Local Referential)");
    VxBbox box;
    box.Min = VectorOf(data, "box-min");
    box.Max = VectorOf(data, "box-max");
    const auto bary = VectorOf(data, "barycenter");
    ImGui::Text("Radius:      %.2f", float(data.Real("radius")));
    ImGui::Text("BBox:        Min: %.2f %.2f %.2f", box.Min.x, box.Min.y, box.Min.z);
    ImGui::Text("             Max: %.2f %.2f %.2f", box.Max.x, box.Max.y, box.Max.z);
    ImGui::Text("Barycenter:  %.2f %.2f %.2f", bary.x, bary.y, bary.z);
    ImGui::EndGroup();
    ImGui::Separator();
    ImGui::BeginGroup();
    UsedByRows("Used By", data.Rows("users"), 220);
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
        it = *it < int(data.Integer("channel-count")) ? std::next(it) : setup.channelRows.erase(it);
    auto channelAt = [&](int i) {
        return i >= 0 && size_t(i) < data.Rows("channels").size()
                   ? SetupFields(data.Rows("channels")[size_t(i)].fields).Object("target")
                   : ObjectRef{};
    };
    auto removeChannels = [&] {
        std::vector<ObjectRef> materials;
        for (int channel : setup.channelRows)
            if (auto material = channelAt(channel))
                materials.push_back(material);
        if (materials.empty())
            return;
        EditObjectList(id, "mesh.channels", {}, materials);
        FlushPending();
        setup.channelRows.clear();
    };
    // At most 10 channels (Dev.exe sub_46FD90: "Cannot add more than 10 channels!").
    constexpr int MAX_CHANNELS = 10;
    auto addChannel = [&] {
        if (int(data.Integer("channel-count")) >= MAX_CHANNELS)
            Warn("Cannot add more than 10 channels!");
        else
            setup.openAddChannel = true;
    };
    auto addChannelOf = [&](ObjectRef material) {
        if (int(data.Integer("channel-count")) >= MAX_CHANNELS) {
            Warn("Cannot add more than 10 channels!");
            return;
        }
        bool added = false;
        added = EditObjectList(id, "mesh.channels", {material});
        FlushPending();
        if (!added)
            Warn("Add Channel: the channel cannot be added");
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
        for (int i = 0; i < int(data.Integer("channel-count")); ++i) {
            SetupDetails channelData;
            if (!ReadProperties(
                    target,
                    {"mesh.channel-active", "mesh.channel-lit", "mesh.channel-source-blend", "mesh.channel-dest-blend"},
                    channelData, {uint64_t(i)}))
                break;
            const auto channel = channelData.Values();
            const SetupFields row(data.Rows("channels")[size_t(i)].fields);
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool selected = setup.channelRows.count(i) != 0;
            if (ImGui::Selectable(row.Text("name").c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && channelAt(i))
                    Open(channelAt(i));
                else if (!ImGui::GetIO().KeyCtrl) setup.channelRows = {i};
                else if (selected) setup.channelRows.erase(i);
                else setup.channelRows.insert(i);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) setup.channelRows = {i};
            if (ImGui::BeginPopupContextItem("channelmenu")) {
                const auto material =
                    setup.channelRows.size() == 1 ? channelAt(*setup.channelRows.begin()) : ObjectRef{};
                if (ImGui::MenuItem("Show Material Setup", nullptr, false, bool(material)))
                    Open(material);
                ImGui::Separator();
                if (ImGui::MenuItem("Add Channel")) addChannel();
                if (ImGui::MenuItem("Remove Channel(s)", "Delete", false, !setup.channelRows.empty())) removeChannels();
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            bool active = channel.Boolean("mesh.channel-active");
            if (ImGui::Checkbox("##active", &active))
                PreviewIndexedProperty(id, "mesh.channel-active",
                                       {"value", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(active)}, {uint64_t(i)},
                                       description.documentRevision);
            ImGui::TableNextColumn();
            bool lit = channel.Boolean("mesh.channel-lit");
            if (ImGui::Checkbox("##lit", &lit))
                PreviewIndexedProperty(id, "mesh.channel-lit", {"value", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(lit)},
                                       {uint64_t(i)}, description.documentRevision);
            ImGui::TableNextColumn();
            int src = std::max(0, int(channel.Integer("mesh.channel-source-blend")) - 1);
            ImGui::SetNextItemWidth(-1);
            if (TypeAheadCombo("##src", &src, BLENDS, 13))
                PreviewIndexedProperty(id, "mesh.channel-source-blend",
                                       {"value", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(src + 1)}, {uint64_t(i)},
                                       description.documentRevision);
            ImGui::TableNextColumn();
            int dst = std::max(0, int(channel.Integer("mesh.channel-dest-blend")) - 1);
            ImGui::SetNextItemWidth(-1);
            if (TypeAheadCombo("##dst", &dst, BLENDS, 13))
                PreviewIndexedProperty(id, "mesh.channel-dest-blend",
                                       {"value", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(dst + 1)}, {uint64_t(i)},
                                       description.documentRevision);
            ImGui::TableNextColumn();
            if (channel.Boolean("mesh.channel-active"))
                ImGui::Text("%d", ++pass);
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
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(EDITOR_OBJECT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            const auto reference = ObjectDragSource(payload->Data, size_t(payload->DataSize));
            SetupPageData dropped;
            if (ReadSetupPage(Client(m_CurrentOwner), reference, -1, dropped) && dropped.kind == Kind::Material) {
                if (!payload->IsDelivery())
                    ImGui::SetTooltip("%s", int(data.Integer("channel-count")) >= MAX_CHANNELS
                                                ? "Cannot add more than 10 channels!"
                                                : "Add a new channel");
                else
                    addChannelOf(reference);
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
        std::vector<SetupObjectRow> choices;
        ReadSetupChoices(Client(m_CurrentOwner), CKCID_MATERIAL, choices);
        for (const auto &object : choices) {
            ImGui::PushID(int(object.object.object));
            if (typeAhead.Selectable(object.name.c_str())) {
                addChannelOf(object.object);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::EndGroup();
    ImGui::SameLine(950);
    // Materials Used; a material dragged onto one replaces it on the faces
    // (Dev.exe sub_46FD90: "Replace '%s' with '%s'"), elsewhere in the list
    // it is every face's ("Add material or replace existing material").
    ImGui::BeginGroup();
    Group("Materials Used");
    auto droppedMaterial = [this](const ImGuiPayload *payload, SetupPageData &page) -> ObjectRef {
        const auto reference = ObjectDragSource(payload->Data, size_t(payload->DataSize));
        return ReadSetupPage(Client(m_CurrentOwner), reference, -1, page) && page.kind == Kind::Material ? reference
                                                                                                         : ObjectRef{};
    };
    if (ImGui::BeginChild("Materials Used", ImVec2(280, 220), ImGuiChildFlags_Borders)) {
        TypeAhead typeAhead;
        for (const auto &item : data.Rows("materials")) {
            const SetupFields row(item.fields);
            const auto material = row.Object("target");
            ImGui::PushID(int(material.object));
            const std::string text = row.Text("name") + " (Material)";
            if (typeAhead.Selectable(text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                Open(material);
            // Its menu (documentation: "right-click and select Show Setup").
            if (ImGui::BeginPopupContextItem("usedmenu")) {
                if (ImGui::MenuItem("Show Setup"))
                    Open(material);
                ImGui::EndPopup();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(EDITOR_OBJECT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery))
                    if (SetupPageData page; auto with = droppedMaterial(payload, page)) {
                        if (!payload->IsDelivery())
                            ImGui::SetTooltip("Replace '%s' with '%s'", row.Text("name").c_str(), page.name.c_str());
                        else if (with != material)
                            RunSetupCommand("setup.mesh-replace-material",
                                            {ObjectArgument("target", target), ObjectArgument("from", material),
                                             ObjectArgument("to", with),
                                             IntegerArgument("document-revision", description.documentRevision)});
                    }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(EDITOR_OBJECT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery))
            if (SetupPageData page; auto with = droppedMaterial(payload, page)) {
                if (!payload->IsDelivery()) {
                    ImGui::SetTooltip("Add material or replace existing material");
                } else {
                    PreviewValue(id, "mesh.global-material", with);
                    FlushPending();
                }
            }
        ImGui::EndDragDropTarget();
    }
    ImGui::EndGroup();
}

void SetupWindows::TexturePage(Setup &setup, ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "bitmap", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target,
                        {"texture.video-format", "texture.current-slot", "texture.save-options", "texture.mipmap",
                         "texture.cube-map", "texture.transparent", "texture.transparent-color",
                         "texture.pick-threshold"},
                        propertyData))
        return;
    const auto values = propertyData.Values();
    ImGui::BeginGroup();
    if (data.Boolean("system-present") && data.Boolean("system-described"))
        ImGui::Text("System Memory Format   %dx%d %d bits  %s", int(data.Integer("system-width")),
                    int(data.Integer("system-height")), int(data.Integer("system-bits")),
                    data.Text("system-format").c_str());
    else
        ImGui::TextUnformatted("System Memory Format   not in system memory");
    int format = int(values.Integer("texture.video-format"));
    ImGui::TextUnformatted("Desired Video Format");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170);
    const auto &formats = data.Texts("formats");
    auto formatLabel = [&](int f) {
        return f >= 0 && size_t(f) < formats.size() ? formats[size_t(f)] : std::string("Unknown");
    };
    if (ImGui::BeginCombo("##videoformat", formatLabel(format).c_str())) {
        TypeAhead typeAhead;
        for (int f = 1; f <= 23; ++f)
            if (typeAhead.Selectable(formatLabel(f).c_str(), f == format))
                PreviewValue(id, "texture.video-format", f);
        ImGui::EndCombo();
    }
    ImGui::EndGroup();
    ImGui::SameLine(460);
    ImGui::BeginGroup();
    Group("Video Memory Information");
    {
        if (data.Boolean("video-present") && data.Boolean("video-described")) {
            ImGui::Text("Width:  %d", int(data.Integer("video-width")));
            ImGui::Text("Height: %d", int(data.Integer("video-height")));
            ImGui::Text("Bits:   %d", int(data.Integer("video-bits")));
            ImGui::Text("Format: %s", data.Text("video-format").c_str());
        } else
            ImGui::TextUnformatted(data.Boolean("video-present") ? "Not available" : "not in video memory");
    }
    ImGui::EndGroup();
    ImGui::SameLine(760);
    ImGui::BeginGroup();
    const int slots = int(data.Integer("slots")), slot = int(data.Integer("slot"));
    auto go = [&](const char *label, int to) {
        ImGui::BeginDisabled(to < 0 || to >= slots || to == slot);
        if (ImGui::Button(label, ImVec2(60, 0)))
            PreviewValue(id, "texture.current-slot", to);
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
    if (ImGui::Button("Add Slot...", ImVec2(150, 0)))
        RunSlotCommand(setup, target, SlotCommand::Add);
    ImGui::BeginDisabled(slots <= 1);
    if (ImGui::Button("Remove Current Slot", ImVec2(150, 0))) setup.confirmRemoveSlot = true;
    ImGui::EndDisabled();
    ImGui::EndGroup();
    ImGui::Separator();
    ImGui::BeginGroup();
    const char *options[] = {"Raw Data", "External", "Image Format", "Global settings", "Include Original File"};
    int option = std::max(0, std::min(4, int(values.Integer("texture.save-options"))));
    ImGui::SetNextItemWidth(160);
    if (TypeAheadCombo("Save Options", &option, options, 5))
        PreviewValue(id, "texture.save-options", option);
    if (option == CKTEXTURE_IMAGEFORMAT)
        SaveFormatControls(target);
    const char *mips[] = {"None", "Automatic"};
    int mip = int(values.Integer("texture.mipmap"));
    ImGui::SetNextItemWidth(160);
    if (TypeAheadCombo("Mip Levels", &mip, mips, 2))
        PreviewValue(id, "texture.mipmap", mip);
    bool cube = values.Boolean("texture.cube-map");
    if (ImGui::Checkbox("Cube Map", &cube))
        PreviewValue(id, "texture.cube-map", cube);
    bool key = values.Boolean("texture.transparent");
    if (ImGui::Checkbox("Color Key Transparency", &key))
        PreviewValue(id, "texture.transparent", key);
    const auto keyColor = ColorOf(values, "texture.transparent-color");
    float rgb[3] = {keyColor.r, keyColor.g, keyColor.b};
    if (ImGui::ColorEdit3("Pick Color", rgb, ImGuiColorEditFlags_NoInputs))
        PreviewValue(id, "texture.transparent-color", VxColor(rgb[0], rgb[1], rgb[2], 1.0f));
    ImGui::SameLine();
    if (ImGui::Button(setup.pickingColor ? "Picking..." : "Pick")) setup.pickingColor = !setup.pickingColor;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick a color on the texture");
    int threshold = int(values.Integer("texture.pick-threshold"));
    ImGui::TextUnformatted("Alpha Picking Threshold");
    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderInt("##threshold", &threshold, 0, 255))
        PreviewValue(id, "texture.pick-threshold", threshold);
    UsedByRows("Used By", data.Rows("users"), 110);
    ImGui::EndGroup();
    // Previews of the current slot: colors and alpha (uploaded again when
    // the slot changes, and every two seconds for changing images).
    ImGui::SameLine(320);
    const int width = int(data.Integer("width")), height = int(data.Integer("height"));
    const auto colorKey = ImageKey(target), alphaKey = colorKey | 1ull;
    if (setup.shownTexture != target || setup.shownSlot != slot || ++setup.textureFrames > 120) {
        setup.shownTexture = target;
        setup.shownSlot = slot;
        setup.textureFrames = 0;
        std::vector<uint32_t> pixels;
        int w = 0, h = 0;
        if (ReadPixels(target, "bitmap-pixels", uint64_t(slot), w, h, pixels)) {
            PixelTexture(colorKey, w, h, pixels.data(), w * 4, false);
            PixelTexture(alphaKey, w, h, pixels.data(), w * 4, true);
        }
    }
    for (unsigned long long key : {colorKey, alphaKey}) {
        ImGui::BeginGroup();
        SlotPreview(setup, target, key, width, height, 384.0f, key == alphaKey);
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
            RunSetupCommand("setup.slot-resize",
                            {ObjectArgument("target", target), IntegerArgument("width", setup.resizeWidth),
                             IntegerArgument("height", setup.resizeHeight),
                             IntegerArgument("document-revision", description.documentRevision)});
            setup.textureFrames = 1000;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void SetupWindows::GridPage(Setup &setup, ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "grid", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target, {"grid.orientation", "grid.priority"}, propertyData))
        return;
    const auto values = propertyData.Values();
    // Its 3D Properties (documentation: the Level Manager's "Setup") or the
    // grid's own (its "Grid Setup").
    if (ImGui::RadioButton("Grid Properties", !setup.grid3d)) setup.grid3d = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("3D Properties", setup.grid3d)) setup.grid3d = true;
    if (setup.grid3d) {
        Entity3DPage(target, true, true, nullptr, &setup);
        return;
    }
    const bool manager = data.Boolean("manager"), attributes = data.Boolean("attributes");
    const int width = int(data.Integer("width")), length = int(data.Integer("length"));
    const auto &layerRows = data.Rows("layers"), &types = data.Rows("types"),
               &classifications = data.Rows("classifications");
    if (width < 0 || length < 0)
        return;
    for (const auto &row : layerRows)
        if (SetupFields(row.fields).Integers("squares").size() != uint64_t(width) * uint64_t(length))
            return;
    const int layers = int(layerRows.size());
    if (setup.layer >= layers) setup.layer = std::max(0, layers - 1);
    for (auto it = setup.layers.begin(); it != setup.layers.end();)
        it = *it < 0 || *it >= layers ? setup.layers.erase(it) : std::next(it);
    if (layers && setup.layers.empty()) setup.layers.insert(setup.layer);
    auto layerAt = [&](int i) {
        return i >= 0 && i < layers ? SetupFields(layerRows[size_t(i)].fields) : SetupFields{};
    };
    const auto selected = layerAt(setup.layer);
    auto typeAt = [&](int type) {
        for (const auto &item : types) {
            const SetupFields row(item.fields);
            if (int(row.Integer("type")) == type)
                return row;
        }
        return SetupFields{};
    };
    auto classAt = [&](int type) {
        for (const auto &item : classifications) {
            const SetupFields row(item.fields);
            if (int(row.Integer("type")) == type)
                return row;
        }
        return SetupFields{};
    };
    auto typeColor = [&](int type) {
        const auto row = typeAt(type);
        return row.Has("color") ? ColorOf(row, "color") : VxColor(1, 1, 0, 1);
    };
    auto valueAt = [&](const SetupFields &layer, int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < length && layer.Has("squares")
                   ? int(layer.Integers("squares")[size_t(y) * width + x])
                   : 0;
    };
    auto linker = [&](int type) { return typeAt(type).Boolean("linker"); };
    auto hasLayer = [&](int type) {
        for (const auto &item : layerRows)
            if (int(SetupFields(item.fields).Integer("type")) == type)
                return true;
        return false;
    };
    auto gridAction = [&](const char *id, int type) {
        RunSetupCommand(id, {ObjectArgument("target", target), IntegerArgument("type", type),
                             IntegerArgument("document-revision", description.documentRevision)});
    };
    auto registerType = [&](bool classification) {
        RunSetupCommand(classification ? "setup.grid-register-classification" : "setup.grid-register-layer",
                        {ObjectArgument("target", target), TextArgument("name", ""),
                         IntegerArgument("document-revision", description.documentRevision)});
    };
    auto beginRename = [&] {
        setup.gridRenameRevision = description.documentRevision;
        setup.gridRenameIndex = setup.renamingClassification ? setup.classification : setup.layerType;
        setup.openGridRename = true;
    };
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
                    const auto layer = layerAt(l);
                    if (!layer.Object("target") || !layer.Boolean("visible"))
                        continue;
                    const int value = valueAt(layer, x, y);
                    if (value <= 0) continue;
                    const VxColor c = typeColor(int(layer.Integer("type")));
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
                if (selected.Object("target") && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    setup.squareX = x;
                    setup.squareY = y;
                    setup.squareValue = valueAt(selected, x, y);
                    setup.squareTargets.clear();
                    for (int l : setup.layers)
                        if (auto object = layerAt(l).Object("target"))
                            setup.squareTargets.push_back(object);
                    setup.squareRevision = description.documentRevision;
                    setup.openSquare = true;
                } else {
                    // One stroke edits all selected layers in one property draft.
                    const bool paint = ImGui::IsMouseDown(ImGuiMouseButton_Left),
                               clear = ImGui::IsMouseDown(ImGuiMouseButton_Right);
                    if (ImGui::IsItemActive() && (paint || clear)) {
                        std::vector<ObjectRef> targets;
                        for (int l : setup.layers)
                            if (auto object = layerAt(l).Object("target"))
                                targets.push_back(object);
                        if (!targets.empty()) {
                            EditorField field{"value", EDITOR_RECORD};
                            field.fields = {{"cells", EDITOR_INTEGER_LIST}};
                            EditorValueEditorDesc editor{};
                            if (m_PropertyKey == "setup.layer.cells")
                                m_Workspace.Application().EditState(m_PropertyEdit, field, editor);
                            const auto value = uint64_t(paint ? setup.paint : 0);
                            bool found = false;
                            auto &cells = field.fields.front().integers;
                            for (size_t i = 0; i < cells.size(); i += 3)
                                if (cells[i] == uint64_t(x) && cells[i + 1] == uint64_t(y)) {
                                    cells[i + 2] = value;
                                    found = true;
                                    break;
                                }
                            if (!found)
                                cells.insert(cells.end(), {uint64_t(x), uint64_t(y), value});
                            const auto first = targets.front();
                            PreviewProperty(first, "layer.cells", std::move(field), {}, {}, std::move(targets));
                        }
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
        const bool choices =
            selected.Object("target") && linker(int(selected.Integer("type"))) && !data.Rows("linker-values").empty();
        ImGui::Text("Square (%d, %d)", setup.squareX, setup.squareY);
        if (choices) {
            for (const auto &item : data.Rows("linker-values")) {
                const SetupFields choice(item.fields);
                const auto value = int(choice.Integer("value"));
                if (ImGui::RadioButton(choice.Text("name").c_str(), setup.squareValue == value))
                    setup.squareValue = value;
            }
        } else {
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Value", &setup.squareValue);
            setup.squareValue = std::max(0, std::min(255, setup.squareValue));
        }
        if (ImGui::Button("OK", ImVec2(80, 0))) {
            const int x = setup.squareX, y = setup.squareY, value = setup.squareValue;
            auto targets = setup.squareTargets;
            if (!targets.empty() && x >= 0 && x < width && y >= 0 && y < length) {
                EditorField field{"value", EDITOR_RECORD};
                field.fields = {{"cells", EDITOR_INTEGER_LIST}};
                field.fields.front().integers = {uint64_t(x), uint64_t(y), uint64_t(value)};
                const auto first = targets.front();
                PreviewIndexedProperty(first, "layer.cells", std::move(field), {}, setup.squareRevision,
                                       std::move(targets));
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
    int orientation = std::max(0, std::min(3, int(values.Integer("grid.orientation"))));
    ImGui::SetNextItemWidth(90);
    if (TypeAheadCombo("Orientation Mode", &orientation, orientations, 4))
        PreviewValue(id, "grid.orientation", orientation);
    ImGui::SameLine();
    int priority = int(values.Integer("grid.priority"));
    ImGui::SetNextItemWidth(60);
    if (ImGui::DragInt("Priority", &priority, 0.2f))
        PreviewValue(id, "grid.priority", priority);
    // The Grid Cross: columns and rows added (large arrows) or deleted.
    {
        auto resize = [&](const char *label, int sideOf, bool add) {
            if (ImGui::Button(label))
                RunSetupCommand("setup.grid-resize",
                                {ObjectArgument("target", target), IntegerArgument("side", sideOf),
                                 IntegerArgument("add", add),
                                 IntegerArgument("document-revision", description.documentRevision)});
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
        if (!typeAt(type).Has("name"))
            return;
        SetupDetails properties;
        if (!ReadProperties(target, {"grid.layer-color", "grid.layer-linker"}, properties, {uint64_t(type)}))
            return;
        const auto typeValues = properties.Values();
        VxColor color = ColorOf(typeValues, "grid.layer-color");
        float rgb[3] = {color.r, color.g, color.b};
        if (ImGui::ColorPicker3("##typecolor", rgb, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs)) {
            EditorField value{"value", EDITOR_REAL_LIST};
            value.reals = {rgb[0], rgb[1], rgb[2], color.a};
            PreviewIndexedProperty(id, "grid.layer-color", std::move(value), {uint64_t(type)},
                                   description.documentRevision);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Integer", nullptr, !typeValues.Boolean("grid.layer-linker")))
            PreviewIndexedProperty(id, "grid.layer-linker", {"value", EDITOR_INTEGER_VALUE, {}, {}, 0},
                                   {uint64_t(type)}, description.documentRevision);
        if (ImGui::MenuItem("Linker", nullptr, typeValues.Boolean("grid.layer-linker")))
            PreviewIndexedProperty(id, "grid.layer-linker", {"value", EDITOR_INTEGER_VALUE, {}, {}, 1},
                                   {uint64_t(type)}, description.documentRevision);
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
            const auto layer = layerAt(l);
            if (!layer.Object("target"))
                continue;
            ImGui::PushID(l);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const ImVec2 c = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(c.x + 2, c.y + 1), ImVec2(c.x + 22, c.y + 15),
                                                      ToImColor(typeColor(int(layer.Integer("type")))));
            ImGui::Dummy(ImVec2(24, 15));
            ImGui::TableNextColumn();
            const auto name = typeAt(int(layer.Integer("type"))).Text("name");
            if (typeAhead.Selectable(name.empty() ? "?" : name.c_str(), setup.layers.count(l) != 0)) {
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
                typeMenu(int(layer.Integer("type")));
                ImGui::Separator();
                if (ImGui::MenuItem("Remove Layer"))
                    removeType = int(layer.Integer("type"));
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            bool visible = layer.Boolean("visible");
            if (ImGui::Checkbox("##visible", &visible)) {
                // The layers chosen together.
                const std::set<int> targets = setup.layers.count(l) ? setup.layers : std::set<int>{l};
                std::vector<ObjectRef> objects;
                for (int t : targets)
                    if (auto each = layerAt(t).Object("target"))
                        objects.push_back(each);
                if (!objects.empty()) {
                    const auto first = objects.front();
                    PreviewProperty(first, "layer.visible",
                                    EditorField{"value", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(visible)}, {}, {},
                                    std::move(objects));
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("VDEV_GRID_LAYER_TYPE")) {
                int type = 0;
                if (payload->DataSize == sizeof(type)) {
                    std::memcpy(&type, payload->Data, sizeof(type));
                    if (!hasLayer(type))
                        gridAction("setup.grid-add-layer", type);
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (removeType >= 0)
            gridAction("setup.grid-remove-layer", removeType);
    }
    // Grid is Class of ... (its classification attributes); double-click removes.
    if (ImGui::BeginTable("gridclass", 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(330, 120))) {
        TypeAhead typeAhead(TypeAhead::Shared);
        ImGui::TableSetupColumn("Grid is Class of ...");
        ImGui::TableHeadersRow();
        for (const auto &item : classifications)
            if (const SetupFields row(item.fields); row.Boolean("held")) {
                const int type = int(row.Integer("type"));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(type);
                const auto name = row.Text("name");
                if (typeAhead.Selectable(name.empty() ? "?" : name.c_str(), false,
                                         ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    gridAction("setup.grid-remove-classification", type);
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
        for (const auto &item : types) {
            const SetupFields row(item.fields);
            const int type = int(row.Integer("type"));
            const auto name = row.Text("name");
            ImGui::PushID(type);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const ImVec2 c = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(c.x + 2, c.y + 1), ImVec2(c.x + 22, c.y + 15), ToImColor(typeColor(type)));
            ImGui::Dummy(ImVec2(24, 15));
            ImGui::TableNextColumn();
            if (typeAhead.Selectable(name.c_str(), setup.layerType == type, ImGuiSelectableFlags_AllowDoubleClick)) {
                setup.layerType = type;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !hasLayer(type))
                    gridAction("setup.grid-add-layer", type);
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("VDEV_GRID_LAYER_TYPE", &type, sizeof(type));
                ImGui::TextUnformatted(name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to the Layer List, or double-click: add a layer of this type");
            if (ImGui::BeginPopupContextItem("typemenu")) {
                setup.layerType = type;
                if (ImGui::MenuItem("New Layer Type"))
                    registerType(false);
                if (ImGui::MenuItem("Rename", "F2")) {
                    setup.gridRenameText = name;
                    setup.renamingClassification = false;
                    beginRename();
                }
                ImGui::Separator();
                typeMenu(type);
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.Text("parameter").c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (ImGui::BeginPopupContextItem("layertypesmenu")) {
            if (ImGui::MenuItem("New Layer Type"))
                registerType(false);
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_F2, false) && manager &&
            typeAt(setup.layerType).Has("name")) {
            setup.gridRenameText = typeAt(setup.layerType).Text("name");
            setup.renamingClassification = false;
            beginRename();
        }
    }
    // The Global Classification List (attributes of the grid category):
    // double-click adds it to the grid; right-click: New Classification,
    // Rename (F2).
    if (ImGui::BeginTable("classifications", 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(450, 120))) {
        TypeAhead typeAhead(TypeAhead::Shared);
        ImGui::TableSetupColumn("Classifications");
        ImGui::TableHeadersRow();
        for (const auto &item : classifications) {
            const SetupFields row(item.fields);
            const int type = int(row.Integer("type"));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(type);
            const auto name = row.Text("name");
            if (typeAhead.Selectable(name.empty() ? "?" : name.c_str(),
                                     row.Boolean("held") || setup.classification == type,
                                     ImGuiSelectableFlags_AllowDoubleClick)) {
                setup.classification = type;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !row.Boolean("held"))
                    gridAction("setup.grid-add-classification", type);
            }
            if (ImGui::BeginPopupContextItem("classmenu")) {
                setup.classification = type;
                if (ImGui::MenuItem("New Classification"))
                    registerType(true);
                if (ImGui::MenuItem("Rename", "F2")) {
                    setup.gridRenameText = name;
                    setup.renamingClassification = true;
                    beginRename();
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (ImGui::BeginPopupContextItem("classesmenu")) {
            if (ImGui::MenuItem("New Classification"))
                registerType(true);
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_F2, false) && attributes &&
            classAt(setup.classification).Has("name")) {
            const auto name = classAt(setup.classification).Text("name");
            setup.gridRenameText = name;
            setup.renamingClassification = true;
            beginRename();
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
            PreviewIndexedProperty(id, setup.renamingClassification ? "grid.classification-name" : "grid.layer-name",
                                   EditorField{"value", EDITOR_TEXT_VALUE, {}, setup.gridRenameText},
                                   {uint64_t(setup.gridRenameIndex)}, setup.gridRenameRevision);
            FlushPending();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace vdev
