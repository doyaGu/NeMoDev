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
#include <cfloat>
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

void SetupWindows::Entity3DPage(ObjectRef target, Setup *setup, const std::function<void()> &fourth) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "entity3d", description)) return;
    const auto data = description.Values();
    Entity3DTransform(target, true, fourth);
    ImGui::Separator();
    if (!setup) return;
    SetupDetails propertyData;
    if (!ReadProperties(target, {"entity3d.current-mesh", "entity3d.pickable", "entity3d.meshes"}, propertyData))
        return;
    const auto values = propertyData.Values();
    SetupColumns columns("##entity-details", 3);
    if (!columns) return;
    columns.Next();
    // Object Meshes (Dev.exe sub_44F330): a click selects (Ctrl: adds or
    // takes off), a double click opens the Mesh Setup; the menu: Show Mesh
    // Setup, Set As Current, Add Mesh, Remove Mesh(es) (also Delete).
    Group("Object Meshes");
    auto &rows = setup->meshRows;
    for (auto it = rows.begin(); it != rows.end();) {
        const bool held = std::any_of(data.Rows("meshes").begin(), data.Rows("meshes").end(),
                                      [&](const auto &row) { return SetupFields(row.fields).Object("target") == *it; });
        it = held ? std::next(it) : rows.erase(it);
    }
    auto removeMeshes = [&] {
        const std::vector<ObjectRef> chosen(rows.begin(), rows.end());
        if (chosen.empty()) return;
        EditObjectList(id, "entity3d.meshes", {}, chosen);
        FlushPending();
        rows.clear();
    };
    if (ImGui::BeginChild("meshes", ImVec2(0, 130 * PageUnit()), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
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
                else if (!ImGui::GetIO().KeyCtrl)
                    rows = {meshId};
                else if (selected)
                    rows.erase(meshId);
                else
                    rows.insert(meshId);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) rows = {meshId};
            if (ImGui::BeginPopupContextItem("meshmenu")) {
                const bool single = rows.size() == 1;
                if (ImGui::MenuItem("Show Mesh Setup", nullptr, false, single)) Open(*rows.begin());
                ImGui::Separator();
                if (ImGui::MenuItem("Set As Current", nullptr, false, single)) {
                    const auto chosen = *rows.begin();
                    PreviewValue(id, "entity3d.current-mesh", chosen);
                    FlushPending();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Add Mesh")) {
                    setup->openAddMesh = true;
                }
                if (ImGui::MenuItem("Remove Mesh(es)", "Delete", false, !rows.empty())) removeMeshes();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        // Elsewhere in the list: Add Mesh.
        if (ImGui::BeginPopupContextWindow("meshesmenu",
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Add Mesh")) setup->openAddMesh = true;
            ImGui::EndPopup();
        }
        if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            removeMeshes();
    }
    ImGui::EndChild();
    // A mesh dragged from the Level Manager (documentation "3D Object Setup").
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload =
                ImGui::AcceptDragDropPayload(EDITOR_OBJECT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            const auto reference = ObjectDragSource(payload->Data, size_t(payload->DataSize));
            SetupPageData dragged;
            const bool mesh =
                ReadSetupPage(Client(m_CurrentOwner), reference, -1, dragged) && dragged.kind == Kind::Mesh;
            const bool held = std::any_of(data.Rows("meshes").begin(), data.Rows("meshes").end(), [&](const auto &row) {
                return SetupFields(row.fields).Object("target") == reference;
            });
            if (mesh && !held && !payload->IsDelivery()) ImGui::SetTooltip("Add Mesh %s", dragged.name.c_str());
            if (mesh && !held && payload->IsDelivery()) {
                EditObjectList(id, "entity3d.meshes", {reference});
                FlushPending();
            }
        }
        ImGui::EndDragDropTarget();
    }
    // Add Mesh (Dev: an Edit Parameter of a Mesh): the composition's meshes.
    if (setup->openAddMesh) {
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
    columns.Next();
    Group("Current Mesh");
    ImGui::TextWrapped("%s", data.Text("mesh-name").c_str());
    if (data.Object("mesh")) {
        SetupForm form("##mesh-summary");
        if (form) {
            form.Row("Vertices");
            ImGui::Text("%d", int(data.Integer("vertices")));
            form.Row("Faces");
            ImGui::Text("%d", int(data.Integer("faces")));
            form.Row("Channels");
            ImGui::Text("%d", int(data.Integer("channel-count")));
            form.Row("Type");
            ImGui::TextUnformatted(data.Boolean("patch") ? "Patch" : "Triangular");
        }
    }
    columns.Next();
    if (data.Boolean("body-part")) {
        Group("Character");
        static const int button[5] = {1145, 1141, 1142, 1142, 1145};
        const auto character = data.Object("character");
        if (DevBitmapButton("charactersetup", ImGui::GetCursorScreenPos(), button, false, bool(character), PageUnit(),
                            "Show Character Setup") &&
            character)
            Open(character);
        ImGui::TextWrapped("%s", data.Text("character-name").c_str());
    } else if (data.Object("parent")) {
        Group("Parent");
        ImGui::TextWrapped("%s", data.Text("parent-name").c_str());
    }
    bool pickable = values.Boolean("entity3d.pickable");
    if (ImGui::Checkbox("Pickable", &pickable)) PreviewValue(id, "entity3d.pickable", pickable);
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("(Hold shift to pick it anyway)");
    ImGui::PopTextWrapPos();
}

void SetupWindows::Entity3DTransform(ObjectRef target, bool sized, const std::function<void()> &fourth) {
    SetupColumns columns("##entity-transform", sized ? 4 : 2);
    if (!columns) return;
    const auto &shown = Shown(target);
    const auto group = [&](const char *title, const char *field, float *world, float *local, float divisor,
                           const char *button, const char *command) {
        columns.Next();
        Group(title);
        if (button && ImGui::SmallButton(button)) RunSetupCommand(command, {ObjectArgument("target", target)});
        ImGui::PushID(title);
        TransformFields(target, field, world, local, divisor, "##v");
        ImGui::PopID();
    };
    const auto wp = Translation(shown.world), lp = Translation(shown.local);
    float world[] = {wp.x, wp.y, wp.z}, local[] = {lp.x, lp.y, lp.z};
    group("Position", "position", world, local, 1, "Reset World Matrix", "setup.reset-world-matrix");
    Vx3DMatrixToEulerAngles(shown.world, &world[0], &world[1], &world[2]);
    Vx3DMatrixToEulerAngles(shown.local, &local[0], &local[1], &local[2]);
    for (int i = 0; i < 3; ++i) {
        world[i] *= RAD_TO_DEG;
        local[i] *= RAD_TO_DEG;
    }
    group("Orientation", "orientation", world, local, RAD_TO_DEG, nullptr, nullptr);
    if (!sized) return;
    const auto ws = ScaleOf(shown.world), ls = ScaleOf(shown.local);
    float scale[] = {ws.x, ws.y, ws.z}, localScale[] = {ls.x, ls.y, ls.z};
    group("Scale", "scale", scale, localScale, 1, "Set as Unit", "setup.scale-unit");
    columns.Next();
    if (fourth) {
        fourth();
    } else {
        Group("World Size");
        SetupForm form("##world-size");
        if (form) {
            const auto &box = shown.box;
            const float size[] = {box.Max.x - box.Min.x, box.Max.y - box.Min.y, box.Max.z - box.Min.z};
            const char *axes[] = {"X :", "Y :", "Z :"};
            for (int i = 0; i < 3; ++i) {
                form.Row(axes[i]);
                ImGui::Text("%.4f", size[i]);
            }
        }
    }
}

void SetupWindows::CameraPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "camera", description)) return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target,
                        {"entity3d.world-position", "camera.target", "camera.projection", "camera.aspect-ratio-active",
                         "camera.ratio", "camera.fov", "camera.ortho-zoom", "camera.near-clip", "camera.far-clip"},
                        propertyData))
        return;
    const auto values = propertyData.Values();
    Entity3DTransform(target);
    ImGui::Separator();
    SetupColumns columns("##camera-settings", 3);
    if (!columns) return;
    columns.Next();
    Group("Display and Target");
    bool shown = data.Boolean("player-shown"), cone = data.Boolean("cone");
    if (ImGui::Checkbox("Show In Player Mode", &shown))
        RunSetupCommand("setup.player-shown", {ObjectArgument("target", target), IntegerArgument("value", shown)});
    if (ImGui::Checkbox("Show cone", &cone))
        RunSetupCommand("setup.cone-shown", {ObjectArgument("target", target), IntegerArgument("value", cone)});
    const auto cameraTarget = data.Object("target");
    {
        SetupForm form("##camera-target");
        if (form) {
            if (data.Boolean("targeted")) {
                form.Row("Target");
                ObjectRef chosen{};
                if (ObjectChoice("##target", CKCID_3DENTITY, cameraTarget, data.Text("target-name"), &chosen,
                                 ImGui::GetContentRegionAvail().x))
                    PreviewValue(id, "camera.target", chosen);
            }
            if (cameraTarget) {
                form.Row("Distance to target");
                ImGui::Text("%.3f", float(data.Real("distance")));
                form.Row("Target position");
                const auto to = VectorOf(data, "target-position");
                float at[] = {to.x, to.y, to.z};
                if (ImGui::DragFloat3("##targetposition", at, .05f, 0, 0, "%.3f"))
                    PreviewValue(cameraTarget, "entity3d.world-position", VxVector(at[0], at[1], at[2]));
            }
        }
    }
    columns.Next();
    Group("Projection Type");
    const auto perspectiveType = uint32_t(data.Integer("perspective-type"));
    const auto orthographicType = uint32_t(data.Integer("orthographic-type"));
    const bool perspective = values.Integer("camera.projection") == perspectiveType;
    if (ImGui::RadioButton("Perspective", perspective)) PreviewValue(id, "camera.projection", perspectiveType);
    if (ImGui::RadioButton("Orthographic", !perspective)) PreviewValue(id, "camera.projection", orthographicType);
    Group("Aspect Ratio");
    bool aspect = values.Boolean("camera.aspect-ratio-active");
    if (ImGui::Checkbox("Active", &aspect)) PreviewValue(id, "camera.aspect-ratio-active", aspect);
    {
        SetupForm form("##camera-aspect");
        if (form) {
            const auto aspectRatio = Vector2Of(values, "camera.ratio");
            const int width = int(aspectRatio.x), height = int(aspectRatio.y);
            static const char *const types[] = {"Custom", "Normal", "Extended", "Widescreen", "Panoramic"};
            static const int ratios[5][2] = {{0, 0}, {4, 3}, {16, 9}, {7, 3}, {20, 7}};
            int type = 0;
            for (int r = 1; r < 5; ++r)
                if (width * ratios[r][1] == height * ratios[r][0]) type = r;
            form.Row("Type");
            int picked = type;
            if (TypeAheadCombo("##aspecttype", &picked, types, 5) && picked != type && picked > 0)
                PreviewValue(id, "camera.ratio", Vx2DVector(float(ratios[picked][0]), float(ratios[picked][1])));
            form.Row("X");
            int x = width, y = height;
            if (ImGui::DragInt("##aspectx", &x, .1f, 1, 10000) && x > 0)
                PreviewValue(id, "camera.ratio", Vx2DVector(float(x), float(height)));
            form.Row("Y");
            if (ImGui::DragInt("##aspecty", &y, .1f, 1, 10000) && y > 0)
                PreviewValue(id, "camera.ratio", Vx2DVector(float(width), float(y)));
            form.Row("Ratio");
            ImGui::Text("%.4f", height ? float(width) / height : 0.f);
        }
    }
    columns.Next();
    Group("Perspective");
    {
        SetupForm form("##camera-lens");
        if (form) {
            form.Row("Lens");
            const float half = float(values.Real("camera.fov")) * .5f;
            float focal = half > 0 ? 18.f / std::tan(half) : 0;
            static const int lenses[] = {15, 20, 24, 28, 35, 50, 85, 135, 200};
            int lens = -1;
            for (int i = 0; i < 9; ++i)
                if (std::fabs(focal - lenses[i]) < .01f) lens = i;
            const std::string preset = lens >= 0 ? std::to_string(lenses[lens]) + " mm" : "Custom";
            if (ImGui::BeginCombo("##lens", preset.c_str())) {
                TypeAhead typeAhead;
                typeAhead.Selectable("Custom", lens < 0);
                for (int i = 0; i < 9; ++i)
                    if (typeAhead.Selectable((std::to_string(lenses[i]) + " mm").c_str(), lens == i))
                        PreviewValue(id, "camera.fov", 2.f * std::atan(18.f / lenses[i]));
                ImGui::EndCombo();
            }
            form.Row("Field of view");
            float fov = float(values.Real("camera.fov")) * RAD_TO_DEG;
            if (EditSlider("##fov", &fov, 1, 179, "%.2f", .1f) && fov > 0 && fov < 180)
                PreviewValue(id, "camera.fov", fov / RAD_TO_DEG);
            form.Row("Focal length");
            if (EditSlider("##focal", &focal, 1, 250, "%.2f", .5f) && focal > 0)
                PreviewValue(id, "camera.fov", 2.f * std::atan(18.f / focal));
        }
    }
    Group("Orthographic Parameter");
    {
        SetupForm form("##camera-ortho");
        if (form) {
            form.Row("Ortho zoom");
            float zoom = float(values.Real("camera.ortho-zoom"));
            if (EditSlider("##orthozoom", &zoom, 0, .5f, "%.3f", .001f)) PreviewValue(id, "camera.ortho-zoom", zoom);
        }
    }
    columns.Next();
    Group("Clipping");
    SetupForm form("##camera-clipping");
    if (!form) return;
    float nearClip = float(values.Real("camera.near-clip")), farClip = float(values.Real("camera.far-clip"));
    form.Row("Near clip");
    if (EditSlider("##near", &nearClip, .01f, std::max(100.f, farClip), "%.2f", .05f)) {
        if (nearClip < farClip)
            PreviewValue(id, "camera.near-clip", nearClip);
        else
            Warn("Near clip must be smaller than far clip");
    }
    form.Row("Far clip");
    if (EditSlider("##far", &farClip, 1, std::max(5000.f, farClip), "%.2f", 1)) {
        if (farClip > nearClip)
            PreviewValue(id, "camera.far-clip", farClip);
        else
            Warn("Near clip must be smaller than far clip");
    }
}

void SetupWindows::LightPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "light", description)) return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target,
                        {"light.specular", "light.type", "light.target", "light.power", "light.active", "light.color",
                         "light.range", "light.constant-attenuation", "light.linear-attenuation",
                         "light.quadratic-attenuation", "light.hot-spot", "light.fall-off", "light.fall-off-shape"},
                        propertyData))
        return;
    const auto values = propertyData.Values();
    Entity3DTransform(target);
    ImGui::Separator();
    SetupColumns columns("##light-settings", 3);
    if (!columns) return;
    columns.Next();
    Group("Light");
    bool shown = data.Boolean("player-shown"), specular = values.Boolean("light.specular");
    if (ImGui::Checkbox("Show In Player Mode", &shown))
        RunSetupCommand("setup.player-shown", {ObjectArgument("target", target), IntegerArgument("value", shown)});
    if (ImGui::Checkbox("Specular", &specular)) PreviewValue(id, "light.specular", specular);
    const char *types[] = {"Point", "Spot", "Directional"};
    const int lightType = int(values.Integer("light.type"));
    int type = lightType == VX_LIGHTSPOT ? 1 : lightType == VX_LIGHTDIREC ? 2 : 0;
    {
        SetupForm form("##light-main");
        if (form) {
            form.Row("Type");
            if (TypeAheadCombo("##type", &type, types, 3))
                PreviewValue(id, "light.type", type == 1 ? VX_LIGHTSPOT : type == 2 ? VX_LIGHTDIREC : VX_LIGHTPOINT);
            if (data.Boolean("targeted")) {
                form.Row("Target");
                ObjectRef chosen{};
                if (ObjectChoice("##target", CKCID_3DENTITY, data.Object("target"), data.Text("target-name"), &chosen,
                                 ImGui::GetContentRegionAvail().x))
                    PreviewValue(id, "light.target", chosen);
            }
            form.Row("Power");
            float power = float(values.Real("light.power"));
            if (ImGui::DragFloat("##power", &power, .01f, 0, 10, "%.2f")) PreviewValue(id, "light.power", power);
        }
    }
    columns.Next();
    Group("Influence");
    bool active = values.Boolean("light.active");
    if (ImGui::Checkbox("Active", &active)) PreviewValue(id, "light.active", active);
    const bool directional = type == 2;
    float range = float(values.Real("light.range"));
    {
        SetupForm form("##light-influence");
        if (form) {
            form.Row("Color");
            const auto color = ColorOf(values, "light.color");
            float rgb[] = {color.r, color.g, color.b};
            if (ColorBar("Color", rgb)) PreviewValue(id, "light.color", VxColor(rgb[0], rgb[1], rgb[2]));
            ImGui::BeginDisabled(directional);
            form.Row("Range");
            if (EditSlider("##range", &range, 0, std::max(200.f, range), "%.1f", .5f))
                PreviewValue(id, "light.range", range);
            ImGui::EndDisabled();
        }
    }
    ImGui::BeginDisabled(directional);
    bool influence = data.Boolean("cone");
    if (ImGui::Checkbox("Show Influence", &influence))
        RunSetupCommand("setup.cone-shown", {ObjectArgument("target", target), IntegerArgument("value", influence)});
    ImGui::EndDisabled();
    columns.Next();
    Group("Attenuation");
    ImGui::BeginDisabled(directional);
    {
        SetupForm form("##light-attenuation");
        if (form) {
            const char *labels[] = {"Constant", "Linear", "Quadratic"};
            const char *fields[] = {"light.constant-attenuation", "light.linear-attenuation",
                                    "light.quadratic-attenuation"};
            const char *ids[] = {"##constant", "##linear", "##quadratic"};
            for (int i = 0; i < 3; ++i) {
                form.Row(labels[i]);
                float value = float(values.Real(fields[i]));
                if (EditSlider(ids[i], &value, 0, 10, "%.3f")) PreviewValue(id, fields[i], value);
            }
        }
    }
    ImGui::EndDisabled();
    if (!directional) {
        const auto a = ImGui::GetCursorScreenPos();
        const ImVec2 size{ImGui::GetContentRegionAvail().x, 90 * PageUnit()}, b{a.x + size.x, a.y + size.y};
        auto *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(a, b, vi::Color(9));
        draw->AddRect(a, b, vi::Color(4));
        const float top = a.y + 1, height = size.y - 2;
        draw->AddLine({a.x, top}, {b.x, top}, vi::Color(6));
        const int steps = std::max(1, int(size.x));
        ImVec2 previous;
        for (int i = 0; i <= steps; ++i) {
            const float distance = range * i / steps;
            const float divisor = float(values.Real("light.constant-attenuation")) +
                                  float(values.Real("light.linear-attenuation")) * distance +
                                  float(values.Real("light.quadratic-attenuation")) * distance * distance;
            const float intensity = divisor > 0 ? std::min(1.f, 1.f / divisor) : 1.f;
            const ImVec2 point{a.x + size.x * i / steps, top + (1 - intensity) * height};
            if (i) draw->AddLine(previous, point, CurrentDevTheme().previewCurve);
            previous = point;
        }
        ImGui::Dummy(size);
    }
    columns.Next();
    Group("Spot Cone");
    ImGui::BeginDisabled(type != 1);
    {
        SetupForm form("##light-spot");
        if (form) {
            form.Row("Hot Spot");
            float hot = float(values.Real("light.hot-spot")) * RAD_TO_DEG;
            if (EditSlider("##hotspot", &hot, 0, 180, "%.1f", .1f))
                PreviewValue(id, "light.hot-spot", hot / RAD_TO_DEG);
            form.Row("Fall Off");
            float fall = float(values.Real("light.fall-off")) * RAD_TO_DEG;
            if (EditSlider("##falloff", &fall, 0, 180, "%.1f", .1f))
                PreviewValue(id, "light.fall-off", fall / RAD_TO_DEG);
            form.Row("Fall Off Shape");
            float shape = float(values.Real("light.fall-off-shape"));
            if (EditSlider("##falloffshape", &shape, 0, 10, "%.2f")) PreviewValue(id, "light.fall-off-shape", shape);
        }
    }
    ImGui::EndDisabled();
}

void SetupWindows::Entity2DPage(ObjectRef target, bool sprite) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "entity2d", description)) return;
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
    SetupColumns columns("##entity2d-settings", 3);
    if (!columns) return;
    auto flag = [&](const char *property, const char *label) {
        bool value = values.Boolean(property);
        if (ImGui::Checkbox(label, &value)) PreviewValue(id, property, value);
    };
    // Native inputs keep their visible label and component IDs from the existing page.
    const auto width = [](const char *label) {
        ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(label).x -
                                                  ImGui::GetStyle().ItemInnerSpacing.x));
    };
    columns.Next();
    Group("General");
    flag("entity2d.pickable", "Pickable");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("(Hold shift to pick it anyway)");
    ImGui::PopTextWrapPos();
    const bool homogeneous = values.Boolean("entity2d.homogeneous");
    flag("entity2d.homogeneous", "Homogeneous Coordinates");
    flag("entity2d.clip-to-camera", "Clip To Camera");
    flag("entity2d.ratio-offset", "Relative To Viewport");
    columns.Next();
    Group("Position");
    const auto position = Vector2Of(values, "entity2d.position"), size = Vector2Of(values, "entity2d.size");
    float xy[] = {position.x, position.y};
    width("X / Y");
    if (ImGui::DragFloat2("X / Y", xy, homogeneous ? .001f : 1.f, 0, 0, "%.4f"))
        PreviewValue(id, "entity2d.position", Vx2DVector(xy[0], xy[1]));
    int z = int(values.Integer("entity2d.zorder"));
    width("Z Order");
    if (ImGui::DragInt("Z Order", &z, .2f)) PreviewValue(id, "entity2d.zorder", z);
    flag("entity2d.background", "Background Sprite");
    Group("Size");
    float wh[] = {size.x, size.y};
    width("Width / Height");
    if (ImGui::DragFloat2("Width / Height", wh, homogeneous ? .001f : 1.f, 0, 0, "%.4f"))
        PreviewValue(id, "entity2d.size", Vx2DVector(wh[0], wh[1]));
    columns.Next();
    ObjectRef chosen{};
    if (!sprite) {
        Group("Material:");
        const auto material = data.Object("material");
        ImGui::BeginDisabled(!material);
        if (ImGui::SmallButton(">##materialsetup") && material) Open(material);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Show Material Setup");
        ImGui::SameLine();
        if (ObjectChoice("##material", CKCID_MATERIAL, material, data.Text("material-name"), &chosen,
                         ImGui::GetContentRegionAvail().x))
            PreviewValue(id, "entity2d.material", chosen);
    }
    Group(sprite ? "Cropping" : "UVMapping");
    flag("entity2d.source-rect", "Active");
    const auto source = RectOf(values, "entity2d.uv");
    float uv[] = {source.left, source.top, source.right, source.bottom};
    width("L / T / R / B");
    if (ImGui::DragFloat4("L / T / R / B", uv, sprite ? 1.f : .005f, 0, 0, sprite ? "%.0f" : "%.3f"))
        PreviewValue(id, "entity2d.uv", VxRect(uv[0], uv[1], uv[2], uv[3]));
    columns.Next();
    Group("Parent");
    const auto parent = data.Object("parent");
    ImGui::BeginDisabled(!parent);
    if (ImGui::SmallButton(">##parentsetup") && parent) Open(parent);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Show Parent Setup");
    ImGui::SameLine();
    if (ObjectChoice("##parent", CKCID_2DENTITY, parent, data.Text("parent-name"), &chosen,
                     ImGui::GetContentRegionAvail().x) &&
        chosen != id)
        PreviewValue(id, "entity2d.parent", chosen);
    flag("entity2d.clip-to-parent", "Clip to Parent");
    flag("entity2d.stick-left", "Stick Left");
    flag("entity2d.stick-right", "Stick Right");
    flag("entity2d.stick-top", "Stick Top");
    flag("entity2d.stick-bottom", "Stick Bottom");
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

void SetupWindows::BitmapInfo(Setup &setup, ObjectRef target, SetupFields data, SetupFields values) {
    SetupColumns columns("##bitmap-info", 3);
    if (!columns) return;
    ImGui::PushTextWrapPos(0);
    const bool texture = data.Boolean("texture");
    columns.Next();
    Group("System Memory Format");
    {
        SetupForm form("##bitmap-system");
        if (form) {
            if (data.Boolean("system-present") && data.Boolean("system-described")) {
                form.Row("Dimensions");
                ImGui::Text("%d x %d", int(data.Integer("system-width")), int(data.Integer("system-height")));
                form.Row("Bits");
                ImGui::Text("%d", int(data.Integer("system-bits")));
                form.Row("Format");
                ImGui::TextWrapped("%s", data.Text("system-format").c_str());
            } else {
                form.Row("Status");
                ImGui::TextWrapped("not in system memory");
            }
            form.Row("Desired Video Format");
            int format = int(values.Integer(texture ? "texture.video-format" : "sprite.video-format"));
            const auto &formats = data.Texts("formats");
            const auto label = [&](int f) {
                return f >= 0 && size_t(f) < formats.size() ? formats[size_t(f)] : std::string("Unknown");
            };
            if (ImGui::BeginCombo("##videoformat", label(format).c_str())) {
                TypeAhead typeAhead;
                for (int f = 1; f <= 23; ++f)
                    if (typeAhead.Selectable(label(f).c_str(), f == format))
                        PreviewValue(target, texture ? "texture.video-format" : "sprite.video-format", f);
                ImGui::EndCombo();
            }
        }
    }
    columns.Next();
    Group("Video Memory Information");
    {
        SetupForm form("##bitmap-video");
        if (form) {
            if (data.Boolean("video-present") && data.Boolean("video-described")) {
                form.Row("Width");
                ImGui::Text("%d", int(data.Integer("video-width")));
                form.Row("Height");
                ImGui::Text("%d", int(data.Integer("video-height")));
                form.Row("Bits");
                ImGui::Text("%d", int(data.Integer("video-bits")));
                form.Row("Format");
                ImGui::TextWrapped("%s", data.Text("video-format").c_str());
            } else {
                form.Row("Status");
                ImGui::TextWrapped("%s", data.Boolean("video-present") ? "Not available" : "not in video memory");
            }
        }
    }
    columns.Next();
    Group("Slots");
    const int slots = int(data.Integer("slots")), slot = int(data.Integer("slot"));
    const auto go = [&](const char *label, int to) {
        ImGui::BeginDisabled(to < 0 || to >= slots || to == slot);
        if (ImGui::Button(label)) PreviewValue(target, texture ? "texture.current-slot" : "sprite.current-slot", to);
        ImGui::EndDisabled();
    };
    {
        SetupForm form("##bitmap-slots");
        if (form) {
            form.Row("Current slot");
            go("Prev", slot - 1);
            ImGui::SameLine();
            ImGui::Text("%d/ %d", slot, slots);
            ImGui::SameLine();
            if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize("Next").x + 2 * ImGui::GetStyle().FramePadding.x)
                ImGui::NewLine();
            go("Next", slot + 1);
            form.Row("Jump");
            go("First", 0);
            ImGui::SameLine();
            go("Last", slots - 1);
        }
    }
    if (ImGui::Button(texture ? "Add Slot..." : "Add Sprites")) RunSlotCommand(setup, target, SlotCommand::Add);
    ImGui::BeginDisabled(slots <= 1);
    if (ImGui::Button("Remove Current Slot")) setup.confirmRemoveSlot = true;
    ImGui::EndDisabled();
    ImGui::PopTextWrapPos();
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
    BitmapInfo(setup, target, data, values);
    const int slot = int(data.Integer("slot"));
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
    dl->AddRectFilled(at, ImVec2(at.x + side + 2, at.y + side + 2), CurrentDevTheme().imageBackground);
    dl->AddRect(at, ImVec2(at.x + side + 2, at.y + side + 2), CurrentDevTheme().previewBorder);
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
    if (!ReadDetails(target, "bitmap-format", description)) return;
    const auto data = description.Values();
    const auto &extensions = data.Texts("extensions"), &labels = data.Texts("labels");
    if (extensions.empty() || extensions.size() != labels.size()) return;
    int chosen = -1;
    for (size_t i = 0; i < extensions.size(); ++i)
        if (ImStricmp(extensions[i].c_str(), data.Text("extension").c_str()) == 0) chosen = int(i);
    std::vector<const char *> items;
    for (const auto &label : labels) items.push_back(label.c_str());
    ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Format").x -
                                              ImGui::GetStyle().ItemInnerSpacing.x));
    int picked = chosen;
    if (TypeAheadCombo("Format", &picked, items.data(), int(items.size())) && picked != chosen && picked >= 0) {
        PreviewValue(target, "bitmap.save-format", extensions[size_t(picked)]);
        FlushPending();
        return;
    }
    if (!data.Boolean("present")) return;
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", data.Boolean("alpha") ? "Alpha Channel is also saved."
                                                    : "This mode does not support Alpha Channel.");
    ImGui::PopTextWrapPos();
    if (data.Boolean("options") && ImGui::Button("Format Options..."))
        RunSetupCommand("bitmap.edit-options", {ObjectArgument("target", target)});
    const auto key = data.Text("encoding-key");
    if (m_FormatInfoKey[target] != key) {
        SetupDetails encoding;
        m_FormatInfo[target] =
            ReadDetails(target, "bitmap-encoding", encoding) ? encoding.Values().Text("info") : std::string();
        m_FormatInfoKey[target] = key;
    }
    if (!m_FormatInfo[target].empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", m_FormatInfo[target].c_str());
        ImGui::PopTextWrapPos();
    }
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
    if (!ReadDetails(target, "material", description)) return;
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
    // Custom is a view choice; the other modes edit the existing blend, Z and alpha properties together.
    int mode = setup.materialCustom ? 3 : int(values.Integer("material.mode"));
    SetupColumns columns("##material-settings", 3);
    if (!columns) return;
    columns.Next();
    Group("Colors");
    {
        SetupForm form("##material-colors");
        if (form) {
            const char *fields[] = {"material.ambient", "material.diffuse", "material.specular", "material.emissive"};
            const char *labels[] = {"Ambient", "Diffuse", "Specular", "Emissive"};
            for (int i = 0; i < 4; ++i) {
                form.Row(labels[i]);
                const auto color = ColorOf(values, fields[i]);
                float rgb[] = {color.r, color.g, color.b};
                if (ColorBar(labels[i], rgb)) PreviewValue(id, fields[i], VxColor(rgb[0], rgb[1], rgb[2]));
            }
            if (mode == 1 || mode == 3) {
                form.Row("Alpha");
                int alpha = int(values.Integer("material.alpha"));
                if (ImGui::SliderInt("##alpha", &alpha, 0, 255)) PreviewValue(id, "material.alpha", alpha);
            }
            form.Row("Power");
            float power = float(values.Real("material.power"));
            if (ImGui::SliderFloat("##power", &power, 0, 100, "%.0f")) PreviewValue(id, "material.power", power);
        }
    }
    columns.Next();
    Group("Surface");
    {
        SetupForm form("##material-surface");
        if (form) {
            form.Row("Mode");
            const char *modes[] = {"Opaque", "Transparent", "Mask", "Custom"};
            const int before = mode;
            if (TypeAheadCombo("##mode", &mode, modes, 4) && mode != before) {
                setup.materialCustom = mode == 3;
                if (mode != 3) PreviewValue(id, "material.mode", mode);
            }
            form.Row("Both Sided");
            bool twoSided = values.Boolean("material.both-sided");
            if (ImGui::Checkbox("##bothsided", &twoSided)) PreviewValue(id, "material.both-sided", twoSided);
            form.Row("Fill Mode");
            const char *fills[] = {"Point", "Wireframe", "Solid"};
            int fill = std::clamp(int(values.Integer("material.fill-mode")) - 1, 0, 2);
            if (TypeAheadCombo("##fillmode", &fill, fills, 3)) PreviewValue(id, "material.fill-mode", fill + 1);
            form.Row("Shade Mode");
            const char *shades[] = {"Flat", "Gouraud", "Phong"};
            int shade = std::clamp(int(values.Integer("material.shade-mode")) - 1, 0, 2);
            if (TypeAheadCombo("##shademode", &shade, shades, 3)) PreviewValue(id, "material.shade-mode", shade + 1);
        }
    }
    static const char *const blends[] = {"Zero",
                                         "One",
                                         "Src Color",
                                         "Inv Src Color",
                                         "Src Alpha",
                                         "Inv Src Alpha",
                                         "Dest Alpha",
                                         "Inv Dest Alpha",
                                         "Dest Color",
                                         "Inv Dest Color",
                                         "Src Alpha Sat",
                                         "Both Src Alpha",
                                         "Both Inv Src Alpha"};
    static const char *const compares[] = {"Never",   "Less",      "Equal",         "Less Equal",
                                           "Greater", "Not Equal", "Greater Equal", "Always"};
    auto compare = [&](const char *label, const char *property) {
        int func = std::clamp(int(values.Integer(property)) - 1, 0, 7);
        if (TypeAheadCombo(label, &func, compares, 8)) PreviewValue(id, property, func + 1);
    };
    if (mode == 2 || mode == 3) {
        columns.Next();
        Group("Alpha and Depth");
        SetupForm form("##material-alpha");
        if (form) {
            if (mode == 3) {
                form.Row("Alpha Test");
                bool test = values.Boolean("material.alpha-test");
                if (ImGui::Checkbox("##alphatest", &test)) PreviewValue(id, "material.alpha-test", test);
                form.Row("Alpha Test Function");
                compare("##alphafunc", "material.alpha-func");
            }
            form.Row("Alpha Ref Value");
            int ref = int(values.Integer("material.alpha-ref"));
            if (ImGui::SliderInt("##alpharef", &ref, 0, 255)) PreviewValue(id, "material.alpha-ref", ref);
            if (mode == 3) {
                form.Row("Blend");
                bool blend = values.Boolean("material.alpha-blend");
                if (ImGui::Checkbox("##blend", &blend)) PreviewValue(id, "material.alpha-blend", blend);
                form.Row("Source Blend");
                int src = std::max(0, int(values.Integer("material.source-blend")) - 1);
                if (TypeAheadCombo("##sourceblend", &src, blends, 13))
                    PreviewValue(id, "material.source-blend", src + 1);
                form.Row("Destination Blend");
                int dst = std::max(0, int(values.Integer("material.dest-blend")) - 1);
                if (TypeAheadCombo("##destblend", &dst, blends, 13)) PreviewValue(id, "material.dest-blend", dst + 1);
                form.Row("Z-Buffer Write");
                bool zwrite = values.Boolean("material.zwrite");
                if (ImGui::Checkbox("##zwrite", &zwrite)) PreviewValue(id, "material.zwrite", zwrite);
                form.Row("Z Compare Function");
                compare("##zfunc", "material.zfunc");
            }
        }
    }
    columns.Next();
    Group("Texture");
    static const int setupButton[5] = {1145, 1141, 1142, 1142, 1145};
    {
        SetupForm form("##material-texture");
        if (form) {
            form.Row("Texture");
            const auto texture = data.Object("texture");
            if (texture) {
                if (DevBitmapButton("texturesetup", ImGui::GetCursorScreenPos(), setupButton, false, true, PageUnit(),
                                    "Show Texture Setup"))
                    Open(texture);
                ImGui::SameLine();
            }
            ObjectRef chosen{};
            if (ObjectChoice("##texture", CKCID_TEXTURE, texture, data.Text("texture-name"), &chosen,
                             ImGui::GetContentRegionAvail().x))
                PreviewValue(id, "material.texture", chosen);
            form.Row("Texture Blend");
            const char *textureBlends[] = {"Decal",         "Modulate", "Decal Alpha", "Modulate Alpha", "Decal Mask",
                                           "Modulate Mask", "Copy",     "Add",         "Dot Product 3",  "Max"};
            int textureBlend = std::clamp(int(values.Integer("material.texture-blend")) - 1, 0, 9);
            if (TypeAheadCombo("##textureblend", &textureBlend, textureBlends, 10))
                PreviewValue(id, "material.texture-blend", textureBlend + 1);
            const char *filters[] = {
                "Nearest",           "Linear",     "Mip Nearest", "Mip Linear", "Linear Mip Nearest",
                "Linear Mip Linear", "Anisotropic"};
            form.Row("Filter Min");
            int minFilter = std::clamp(int(values.Integer("material.texture-min-mode")) - 1, 0, 6);
            if (TypeAheadCombo("##filtermin", &minFilter, filters, 7))
                PreviewValue(id, "material.texture-min-mode", minFilter + 1);
            form.Row("Filter Mag");
            int magFilter = std::clamp(int(values.Integer("material.texture-mag-mode")) - 1, 0, 6);
            if (TypeAheadCombo("##filtermag", &magFilter, filters, 7))
                PreviewValue(id, "material.texture-mag-mode", magFilter + 1);
            form.Row("Address Mode");
            const char *addresses[] = {"Wrap", "Mirror", "Clamp", "Border", "Mirror Once"};
            int address = std::clamp(int(values.Integer("material.texture-address")) - 1, 0, 4);
            if (TypeAheadCombo("##addressmode", &address, addresses, 5))
                PreviewValue(id, "material.texture-address", address + 1);
            form.Row("Perspective Correct");
            bool perspective = values.Boolean("material.perspective-correct");
            if (ImGui::Checkbox("##perspective", &perspective))
                PreviewValue(id, "material.perspective-correct", perspective);
            if (values.Integer("material.texture-address") == VXTEXTURE_ADDRESSBORDER) {
                form.Row("Border Color");
                const auto border = ColorOf(values, "material.texture-border-color");
                float rgba[] = {border.r, border.g, border.b, border.a};
                if (ColorBar("Border Color", rgba, true))
                    PreviewValue(id, "material.texture-border-color", VxColor(rgba[0], rgba[1], rgba[2], rgba[3]));
            }
        }
    }
    columns.Next();
    Group("Effect");
    const int effectCount = int(data.Integer("effect-count"));
    const auto &names = data.Texts("effects");
    std::vector<const char *> items;
    for (const auto &name : names) items.push_back(name.c_str());
    int effect = std::max(0, std::min(int(items.size()) - 1, int(values.Integer("material.effect"))));
    {
        SetupForm form("##material-effect");
        if (form) {
            form.Row("Effect");
            if (TypeAheadCombo("##effect", &effect, items.data(), int(items.size()))) {
                PreviewValue(id, "material.effect", effect);
                FlushPending();
                return;
            }
            if (effect > 0 && effect < effectCount) {
                const auto &textures = data.Rows("effect-textures");
                for (size_t i = 0; i < textures.size(); ++i) {
                    const SetupFields row(textures[i].fields);
                    form.Row(row.Text("label").c_str());
                    const auto extra = row.Object("target");
                    ImGui::PushID(int(i));
                    if (extra) {
                        if (DevBitmapButton("effecttexturesetup", ImGui::GetCursorScreenPos(), setupButton, false, true,
                                            PageUnit(), "Show Texture Setup"))
                            Open(extra);
                        ImGui::SameLine();
                    }
                    ObjectRef picked{};
                    if (ObjectChoice("##effecttexture", CKCID_TEXTURE, extra, row.Text("name"), &picked,
                                     ImGui::GetContentRegionAvail().x)) {
                        PreviewIndexedProperty(id, "material.effect-texture", {"value", EDITOR_OBJECT, picked},
                                               {uint64_t(i + 1)}, description.documentRevision);
                        FlushPending();
                    }
                    ImGui::PopID();
                }
            }
        }
    }
    if (effect > 0 && effect < effectCount) {
        if (data.Boolean("has-parameter")) {
            if (ImGui::Button((data.Text("parameter-text") + "##effectparameter").c_str(),
                              {ImGui::GetContentRegionAvail().x, 0}))
                RunSetupCommand("parameter.edit", {ObjectArgument("target", data.Object("parameter"))});
            if (ImGui::IsItemHovered() && !data.Text("parameter-description").empty())
                ImGui::SetTooltip("%s", data.Text("parameter-description").c_str());
        }
        if (!data.Text("description").empty()) ImGui::TextWrapped("%s", data.Text("description").c_str());
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
            if (ImTextureID image = CachedPixelTexture(imageKey)) {
                const float side = std::min(128 * PageUnit(), ImGui::GetContentRegionAvail().x);
                ImGui::Image(image, {side, side});
            }
        }
    }
    columns.Next();
    Group("Used By");
    UsedByList("Used By", data.Rows("users"), {0, 150 * PageUnit()});
}

void SetupWindows::MeshPage(Setup &setup, ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "mesh", description)) return;
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
    if (patch && !ReadProperties(target, {"patchmesh.iterations"}, patchData)) return;
    {
        SetupColumns columns("##mesh-settings", 3);
        if (columns) {
            ImGui::PushTextWrapPos(0);
            const auto widthFor = [](const char *label) {
                ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(label).x -
                                                          ImGui::GetStyle().ItemInnerSpacing.x));
            };
            columns.Next();
            Group("Rendering");
            bool progressive = values.Boolean("mesh.progressive");
            if (ImGui::Checkbox("Progressive Mesh", &progressive)) PreviewValue(id, "mesh.progressive", progressive);
            if (values.Boolean("mesh.progressive")) {
                int vertices = int(values.Integer("mesh.vertices-rendered"));
                widthFor("Vertices");
                if (ImGui::SliderInt("Vertices", &vertices, 0, std::max(1, int(data.Integer("vertices")))))
                    PreviewValue(id, "mesh.vertices-rendered", vertices);
                bool geomorph = values.Boolean("mesh.geo-morph");
                if (ImGui::Checkbox("GeoMorphic Transition", &geomorph)) PreviewValue(id, "mesh.geo-morph", geomorph);
                int step = int(values.Integer("mesh.geo-morph-step"));
                widthFor("Decimation");
                if (ImGui::InputInt("Decimation", &step, 1, 10)) PreviewValue(id, "mesh.geo-morph-step", step);
            }
            bool stripify = values.Boolean("mesh.stripify");
            if (ImGui::Checkbox("Stripify", &stripify)) PreviewValue(id, "mesh.stripify", stripify);
            const char *wraps[] = {"None", "U", "V", "UV"};
            int wrap = std::clamp(int(values.Integer("mesh.wrap-mode")), 0, 3);
            widthFor("Texture Wrap");
            if (TypeAheadCombo("Texture Wrap", &wrap, wraps, 4)) PreviewValue(id, "mesh.wrap-mode", wrap);
            const char *lits[] = {"Prelit", "Lit"};
            int lit = int(values.Boolean("mesh.lit"));
            widthFor("Lit Mode");
            if (TypeAheadCombo("Lit Mode", &lit, lits, 2)) PreviewValue(id, "mesh.lit", lit != 0);
            if (patch) {
                int iterations = int(patchData.Values().Integer("patchmesh.iterations"));
                widthFor("Iterations");
                if (ImGui::SliderInt("Iterations", &iterations, 0, 10))
                    PreviewValue(id, "patchmesh.iterations", iterations);
            }
            columns.Next();
            Group("Mesh Info");
            {
                SetupForm form("##mesh-info");
                if (form) {
                    const char *labels[] = {"Vertices", "Faces", "Channels", "Line Count"};
                    const char *fields[] = {"vertices", "faces", "channel-count", "lines"};
                    for (int i = 0; i < 4; ++i) {
                        form.Row(labels[i]);
                        ImGui::Text("%d", int(data.Integer(fields[i])));
                    }
                    form.Row("Type");
                    ImGui::TextUnformatted(patch ? "Patch" : "Triangular");
                }
            }
            ImGui::BeginDisabled(patch);
            if (ImGui::Button("Consolidate"))
                RunSetupCommand("setup.mesh-consolidate", {ObjectArgument("target", target)});
            ImGui::EndDisabled();
            if (patch) {
                Group("PatchMesh Info");
                SetupForm form("##patch-info");
                if (form) {
                    const char *labels[] = {"Control Points", "Vectors", "Edges", "Patches", "Texture Vertices"};
                    const char *fields[] = {"control-points", "vectors", "edges", "patches", "texture-vertices"};
                    for (int i = 0; i < 5; ++i) {
                        form.Row(labels[i]);
                        ImGui::Text("%d", int(data.Integer(fields[i])));
                    }
                }
            }
            columns.Next();
            Group("Geometry Info (Local Referential)");
            {
                SetupForm form("##mesh-geometry");
                if (form) {
                    form.Row("Radius");
                    ImGui::Text("%.2f", float(data.Real("radius")));
                    const char *labels[] = {"BBox Min", "BBox Max", "Barycenter"};
                    const char *fields[] = {"box-min", "box-max", "barycenter"};
                    for (int i = 0; i < 3; ++i) {
                        const auto value = VectorOf(data, fields[i]);
                        form.Row(labels[i]);
                        ImGui::TextWrapped("%.2f %.2f %.2f", value.x, value.y, value.z);
                    }
                }
            }
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::Separator();
    SetupColumns columns("##mesh-lists", 3);
    if (!columns) return;
    columns.Next();
    Group("Used By");
    UsedByList("Used By", data.Rows("users"), {0, 220 * PageUnit()});
    columns.Next();
    Group("Channels");
    static const char *const BLENDS[] = {"Zero",
                                         "One",
                                         "Src Color",
                                         "Inv Src Color",
                                         "Src Alpha",
                                         "Inv Src Alpha",
                                         "Dest Alpha",
                                         "Inv Dest Alpha",
                                         "Dest Color",
                                         "Inv Dest Color",
                                         "Src Alpha Sat",
                                         "Both Src Alpha",
                                         "Both Inv Src Alpha"};
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
            if (auto material = channelAt(channel)) materials.push_back(material);
        if (materials.empty()) return;
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
        if (!added) Warn("Add Channel: the channel cannot be added");
    };
    if (ImGui::BeginTable("channels", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_ScrollX | ImGuiTableFlags_NoSavedSettings,
                          ImVec2(0, 220 * PageUnit()), std::max(670 * PageUnit(), ImGui::GetContentRegionAvail().x))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Material Name");
        ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed, 50 * PageUnit());
        ImGui::TableSetupColumn("Lit Mode", ImGuiTableColumnFlags_WidthFixed, 60 * PageUnit());
        ImGui::TableSetupColumn("Src Blend", ImGuiTableColumnFlags_WidthFixed, 120 * PageUnit());
        ImGui::TableSetupColumn("Dest Blend", ImGuiTableColumnFlags_WidthFixed, 120 * PageUnit());
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 40 * PageUnit());
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
                else if (!ImGui::GetIO().KeyCtrl)
                    setup.channelRows = {i};
                else if (selected)
                    setup.channelRows.erase(i);
                else
                    setup.channelRows.insert(i);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) setup.channelRows = {i};
            if (ImGui::BeginPopupContextItem("channelmenu")) {
                const auto material =
                    setup.channelRows.size() == 1 ? channelAt(*setup.channelRows.begin()) : ObjectRef{};
                if (ImGui::MenuItem("Show Material Setup", nullptr, false, bool(material))) Open(material);
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
            else
                ImGui::TextDisabled("-");
            ImGui::PopID();
        }
        if (ImGui::BeginPopupContextWindow("channelsmenu",
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
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
        if (const ImGuiPayload *payload =
                ImGui::AcceptDragDropPayload(EDITOR_OBJECT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
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
    columns.Next();
    // Materials Used; a material dragged onto one replaces it on the faces
    // (Dev.exe sub_46FD90: "Replace '%s' with '%s'"), elsewhere in the list
    // it is every face's ("Add material or replace existing material").
    Group("Materials Used");
    auto droppedMaterial = [this](const ImGuiPayload *payload, SetupPageData &page) -> ObjectRef {
        const auto reference = ObjectDragSource(payload->Data, size_t(payload->DataSize));
        return ReadSetupPage(Client(m_CurrentOwner), reference, -1, page) && page.kind == Kind::Material ? reference
                                                                                                         : ObjectRef{};
    };
    if (ImGui::BeginChild("Materials Used", ImVec2(0, 220 * PageUnit()), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
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
                if (ImGui::MenuItem("Show Setup")) Open(material);
                ImGui::EndPopup();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *payload =
                        ImGui::AcceptDragDropPayload(EDITOR_OBJECT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery))
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
        if (const ImGuiPayload *payload =
                ImGui::AcceptDragDropPayload(EDITOR_OBJECT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery))
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
}

void SetupWindows::TexturePage(Setup &setup, ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "bitmap", description)) return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target,
                        {"texture.video-format", "texture.current-slot", "texture.save-options", "texture.mipmap",
                         "texture.cube-map", "texture.transparent", "texture.transparent-color",
                         "texture.pick-threshold"},
                        propertyData))
        return;
    const auto values = propertyData.Values();
    BitmapInfo(setup, target, data, values);
    ImGui::Separator();
    const int slot = int(data.Integer("slot"));
    {
        SetupColumns columns("##texture-settings", 2);
        if (columns) {
            columns.Next();
            Group("Save Options");
            const auto widthFor = [](const char *label) {
                ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(label).x -
                                                          ImGui::GetStyle().ItemInnerSpacing.x));
            };
            const char *options[] = {"Raw Data", "External", "Image Format", "Global settings",
                                     "Include Original File"};
            int option = std::clamp(int(values.Integer("texture.save-options")), 0, 4);
            widthFor("Save Options");
            if (TypeAheadCombo("Save Options", &option, options, 5)) PreviewValue(id, "texture.save-options", option);
            if (option == CKTEXTURE_IMAGEFORMAT) SaveFormatControls(target);
            const char *mips[] = {"None", "Automatic"};
            int mip = int(values.Integer("texture.mipmap"));
            widthFor("Mip Levels");
            if (TypeAheadCombo("Mip Levels", &mip, mips, 2)) PreviewValue(id, "texture.mipmap", mip);
            bool cube = values.Boolean("texture.cube-map");
            if (ImGui::Checkbox("Cube Map", &cube)) PreviewValue(id, "texture.cube-map", cube);
            bool key = values.Boolean("texture.transparent");
            if (ImGui::Checkbox("Color Key Transparency", &key)) PreviewValue(id, "texture.transparent", key);
            const auto keyColor = ColorOf(values, "texture.transparent-color");
            float rgb[] = {keyColor.r, keyColor.g, keyColor.b};
            if (ImGui::ColorEdit3("Pick Color", rgb, ImGuiColorEditFlags_NoInputs))
                PreviewValue(id, "texture.transparent-color", VxColor(rgb[0], rgb[1], rgb[2], 1.0f));
            ImGui::SameLine();
            const char *pickLabel = setup.pickingColor ? "Picking..." : "Pick";
            if (ImGui::GetContentRegionAvail().x <
                ImGui::CalcTextSize(pickLabel).x + 2 * ImGui::GetStyle().FramePadding.x)
                ImGui::NewLine();
            if (ImGui::Button(pickLabel)) setup.pickingColor = !setup.pickingColor;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick a color on the texture");
            int threshold = int(values.Integer("texture.pick-threshold"));
            ImGui::TextWrapped("Alpha Picking Threshold");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::SliderInt("##threshold", &threshold, 0, 255))
                PreviewValue(id, "texture.pick-threshold", threshold);
            columns.Next();
            Group("Used By");
            UsedByList("Used By", data.Rows("users"), {0, 110 * PageUnit()});
        }
    }
    // Keep the existing per-slot texture cache; window reflow does not request new pixels.
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
    {
        SetupColumns columns("##texture-previews", 2);
        if (columns)
            for (const auto key : {colorKey, alphaKey}) {
                columns.Next();
                Group(key == alphaKey ? "Alpha" : "Image");
                const float side = std::max(1.f, std::min(384 * PageUnit(), ImGui::GetContentRegionAvail().x - 2));
                SlotPreview(setup, target, key, width, height, side, key == alphaKey);
            }
    }
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
    if (!ReadDetails(target, "grid", description)) return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target, {"grid.orientation", "grid.priority"}, propertyData)) return;
    const auto values = propertyData.Values();
    // Its 3D Properties (documentation: the Level Manager's "Setup") or the
    // grid's own (its "Grid Setup").
    if (ImGui::RadioButton("Grid Properties", !setup.grid3d)) setup.grid3d = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("3D Properties", setup.grid3d)) setup.grid3d = true;
    if (setup.grid3d) {
        Entity3DPage(target, &setup);
        return;
    }
    const bool manager = data.Boolean("manager"), attributes = data.Boolean("attributes");
    const int width = int(data.Integer("width")), length = int(data.Integer("length"));
    const auto &layerRows = data.Rows("layers"), &types = data.Rows("types"),
               &classifications = data.Rows("classifications");
    if (width < 0 || length < 0) return;
    for (const auto &row : layerRows)
        if (SetupFields(row.fields).Integers("squares").size() != uint64_t(width) * uint64_t(length)) return;
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
            if (int(row.Integer("type")) == type) return row;
        }
        return SetupFields{};
    };
    auto classAt = [&](int type) {
        for (const auto &item : classifications) {
            const SetupFields row(item.fields);
            if (int(row.Integer("type")) == type) return row;
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
            if (int(SetupFields(item.fields).Integer("type")) == type) return true;
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
    const float unit = PageUnit();
    // Status line: the square under the mouse and the grid size.
    if (setup.hoverX >= 0)
        ImGui::TextWrapped("X : %d    Y : %d    Value : %d        Width : %d    Length : %d", setup.hoverX,
                           setup.hoverY, valueAt(selected, setup.hoverX, setup.hoverY), width, length);
    else
        ImGui::TextWrapped("X : #    Y : #    Value : #        Width : %d    Length : %d", width, length);
    setup.hoverX = setup.hoverY = -1;
    SetupColumns columns("##grid-sections", 3);
    if (!columns) return;
    columns.Next();
    Group("Paint");
    // The value painted: Dev's 0..255 gradient.
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float barWidth = std::max(1.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("255").x -
                                                 ImGui::GetStyle().ItemSpacing.x);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilledMultiColor(at, ImVec2(at.x + barWidth, at.y + 12 * unit), IM_COL32(0, 0, 0, 255),
                                    IM_COL32(255, 255, 255, 255), IM_COL32(255, 255, 255, 255), IM_COL32(0, 0, 0, 255));
        const float marker = at.x + barWidth * setup.paint / 255.0f;
        dl->AddTriangleFilled(ImVec2(marker, at.y + 12 * unit), ImVec2(marker - 4 * unit, at.y + 18 * unit),
                              ImVec2(marker + 4 * unit, at.y + 18 * unit), CurrentDevTheme().previewMarker);
        ImGui::InvisibleButton("##paintvalue", ImVec2(barWidth, 18 * unit));
        if (ImGui::IsItemActive())
            setup.paint = std::max(
                0, std::min(255, static_cast<int>((ImGui::GetIO().MousePos.x - at.x) / barWidth * 255.0f + 0.5f)));
        ImGui::SameLine();
        ImGui::Text("%d", setup.paint);
    }
    // The squares (documentation: "Layers are cumulated and their square
    // value (intensity*layer color) are added").
    {
        const float margin = 20 * unit;
        const float side = std::max(1.f, std::min(384 * unit, ImGui::GetContentRegionAvail().x - 2 * margin));
        const float cell = width > 0 && length > 0 ? std::max(2.f, std::min(side / width, side / length)) : 10.f;
        const auto contentId = ImGui::GetID("");
        if (ImGui::BeginChild("##grid-canvas", {0, side + 2 * margin}, ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            ImGui::PushOverrideID(contentId);
            const auto at = ImGui::GetCursorScreenPos();
            const ImVec2 extent{std::max(side, width * cell) + 2 * margin, std::max(side, length * cell) + 2 * margin};
            auto *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(at, {at.x + extent.x, at.y + extent.y}, CurrentDevTheme().previewBackground);
            const ImVec2 origin{at.x + margin, at.y + margin};
            const auto clip = ImGui::GetCurrentWindow()->ClipRect;
            const int firstX = int(std::clamp(std::floor((double(clip.Min.x) - origin.x) / cell), 0., double(width))),
                      lastX = int(std::clamp(std::ceil((double(clip.Max.x) - origin.x) / cell), 0., double(width))),
                      firstRow =
                          int(std::clamp(std::floor((double(clip.Min.y) - origin.y) / cell), 0., double(length))),
                      lastRow = int(std::clamp(std::ceil((double(clip.Max.y) - origin.y) / cell), 0., double(length)));
            for (int row = firstRow; row < lastRow; ++row)
                for (int x = firstX; x < lastX; ++x) {
                    const int y = length - 1 - row;
                    float r = 0.0f, g = 0.0f, b = 0.0f;
                    for (int l = 0; l < layers; ++l) {
                        const auto layer = layerAt(l);
                        if (!layer.Object("target") || !layer.Boolean("visible")) continue;
                        const int value = valueAt(layer, x, y);
                        if (value <= 0) continue;
                        const VxColor c = typeColor(int(layer.Integer("type")));
                        const float k = std::min(255, value) / 255.0f;
                        r += c.r * k;
                        g += c.g * k;
                        b += c.b * k;
                    }
                    const ImU32 fill =
                        IM_COL32(static_cast<int>(std::min(1.0f, r) * 255), static_cast<int>(std::min(1.0f, g) * 255),
                                 static_cast<int>(std::min(1.0f, b) * 255), 255);
                    // Row 0 at the bottom (the grid seen from above).
                    const ImVec2 a(origin.x + x * cell, origin.y + (length - 1 - y) * cell);
                    dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), fill);
                    dl->AddRect(a, ImVec2(a.x + cell + 1, a.y + cell + 1), CurrentDevTheme().previewGrid);
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
                            if (auto object = layerAt(l).Object("target")) setup.squareTargets.push_back(object);
                        setup.squareRevision = description.documentRevision;
                        setup.openSquare = true;
                    } else {
                        // One stroke edits all selected layers in one property draft.
                        const bool paint = ImGui::IsMouseDown(ImGuiMouseButton_Left),
                                   clear = ImGui::IsMouseDown(ImGuiMouseButton_Right);
                        const auto delta = ImGui::GetIO().MouseDelta;
                        if (ImGui::IsItemActive() && (paint || clear) &&
                            (ImGui::IsItemActivated() || delta.x != 0 || delta.y != 0)) {
                            std::vector<ObjectRef> targets;
                            for (int l : setup.layers)
                                if (auto object = layerAt(l).Object("target")) targets.push_back(object);
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
                                if (!found) cells.insert(cells.end(), {uint64_t(x), uint64_t(y), value});
                                const auto first = targets.front();
                                PreviewProperty(first, "layer.cells", std::move(field), {}, {}, std::move(targets));
                            }
                        }
                    }
                }
            }
            ImGui::SetCursorScreenPos(at);
            ImGui::Dummy(extent);
            ImGui::PopID();
        }
        ImGui::EndChild();
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
    ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Orientation Mode").x -
                                              ImGui::GetStyle().ItemInnerSpacing.x));
    if (TypeAheadCombo("Orientation Mode", &orientation, orientations, 4))
        PreviewValue(id, "grid.orientation", orientation);
    int priority = int(values.Integer("grid.priority"));
    ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Priority").x -
                                              ImGui::GetStyle().ItemInnerSpacing.x));
    if (ImGui::DragInt("Priority", &priority, 0.2f)) PreviewValue(id, "grid.priority", priority);
    // The Grid Cross: columns and rows added (large arrows) or deleted.
    {
        auto resize = [&](const char *label, int sideOf, bool add) {
            if (ImGui::Button(label))
                RunSetupCommand("setup.grid-resize",
                                {ObjectArgument("target", target), IntegerArgument("side", sideOf),
                                 IntegerArgument("add", add),
                                 IntegerArgument("document-revision", description.documentRevision)});
        };
        Group("Grid Cross");
        const struct {
            const char *add, *remove;
            int side;
        } sides[] = {{"<< Add Column##left", "x Left Column", 0},
                     {"Add Column >>##right", "x Right Column", 1},
                     {"Add Row (Top)", "x Top Row", 3},
                     {"Add Row (Bottom)", "x Bottom Row", 2}};
        for (const auto &side : sides) {
            resize(side.add, side.side, true);
            ImGui::SameLine();
            if (ImGui::GetContentRegionAvail().x <
                ImGui::CalcTextSize(side.remove).x + 2 * ImGui::GetStyle().FramePadding.x)
                ImGui::NewLine();
            resize(side.remove, side.side, false);
        }
    }
    columns.Next();
    Group("Layers and Classifications");
    // A layer type's color and parameter (its row's menu, in either list).
    auto typeMenu = [&](int type) {
        if (!typeAt(type).Has("name")) return;
        SetupDetails properties;
        if (!ReadProperties(target, {"grid.layer-color", "grid.layer-linker"}, properties, {uint64_t(type)})) return;
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
    if (ImGui::BeginTable("layers", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_NoSavedSettings,
                          ImVec2(0, 200 * unit))) {
        TypeAhead typeAhead(TypeAhead::Shared);
        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 40 * unit);
        ImGui::TableSetupColumn("Layer Name");
        ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthFixed, 26 * unit);
        ImGui::TableHeadersRow();
        int removeType = -1;
        for (int l = 0; l < layers; ++l) {
            const auto layer = layerAt(l);
            if (!layer.Object("target")) continue;
            ImGui::PushID(l);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const ImVec2 c = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(c.x + 2 * unit, c.y + unit),
                                                      ImVec2(c.x + 22 * unit, c.y + 15 * unit),
                                                      ToImColor(typeColor(int(layer.Integer("type")))));
            ImGui::Dummy(ImVec2(24 * unit, 15 * unit));
            ImGui::TableNextColumn();
            const auto name = typeAt(int(layer.Integer("type"))).Text("name");
            if (typeAhead.Selectable(name.empty() ? "?" : name.c_str(), setup.layers.count(l) != 0)) {
                if (ImGui::GetIO().KeyCtrl) {
                    if (!setup.layers.erase(l)) setup.layers.insert(l);
                    if (setup.layers.count(l))
                        setup.layer = l;
                    else if (!setup.layers.empty())
                        setup.layer = *setup.layers.begin();
                } else {
                    setup.layer = l;
                    setup.layers = {l};
                }
            }
            if (ImGui::BeginPopupContextItem()) {
                typeMenu(int(layer.Integer("type")));
                ImGui::Separator();
                if (ImGui::MenuItem("Remove Layer")) removeType = int(layer.Integer("type"));
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            bool visible = layer.Boolean("visible");
            if (ImGui::Checkbox("##visible", &visible)) {
                // The layers chosen together.
                const std::set<int> targets = setup.layers.count(l) ? setup.layers : std::set<int>{l};
                std::vector<ObjectRef> objects;
                for (int t : targets)
                    if (auto each = layerAt(t).Object("target")) objects.push_back(each);
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
                    if (!hasLayer(type)) gridAction("setup.grid-add-layer", type);
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (removeType >= 0) gridAction("setup.grid-remove-layer", removeType);
    }
    // Grid is Class of ... (its classification attributes); double-click removes.
    if (ImGui::BeginTable("gridclass", 1,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_NoSavedSettings,
                          ImVec2(0, 120 * unit))) {
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
    columns.Next();
    Group("Global Lists");
    // The Global Layer List (CKGridManager): a type dragged to the layers or
    // double-clicked adds a layer of it; right-click: New Layer Type, the
    // type's color and parameter, Rename (F2).
    if (ImGui::BeginTable("layertypes", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_ScrollX | ImGuiTableFlags_NoSavedSettings,
                          ImVec2(0, 200 * unit), std::max(450 * unit, ImGui::GetContentRegionAvail().x))) {
        TypeAhead typeAhead(TypeAhead::Shared);
        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 40 * unit);
        ImGui::TableSetupColumn("Layer Type Name");
        ImGui::TableSetupColumn("Param Type", ImGuiTableColumnFlags_WidthFixed, 140 * unit);
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
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(c.x + 2 * unit, c.y + unit),
                                                      ImVec2(c.x + 22 * unit, c.y + 15 * unit), ToImColor(typeColor(type)));
            ImGui::Dummy(ImVec2(24 * unit, 15 * unit));
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
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Drag to the Layer List, or double-click: add a layer of this type");
            if (ImGui::BeginPopupContextItem("typemenu")) {
                setup.layerType = type;
                if (ImGui::MenuItem("New Layer Type")) registerType(false);
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
            if (ImGui::MenuItem("New Layer Type")) registerType(false);
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
    if (ImGui::BeginTable("classifications", 1,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_NoSavedSettings,
                          ImVec2(0, 120 * unit))) {
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
                if (ImGui::MenuItem("New Classification")) registerType(true);
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
            if (ImGui::MenuItem("New Classification")) registerType(true);
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
