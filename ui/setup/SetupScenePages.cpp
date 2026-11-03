// The Setups of curves, characters, sounds, arrays, levels and scenes, 3D frames and sprites, places, 2D entities.

#include "ui/setup/SetupWindows.h"
#include "workspace/EditorWorkspace.h"
#include "CKDefines.h"
#include "CKEnums.h"
#include "VxDefines.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <map>


#include "imgui.h"
#include "imgui_internal.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/FileDialogs.h"
#include "ui/controls/TypeAhead.h"
#include "core/runtime/TextEncoding.h"
#include "ui/setup/SetupWindowsDetail.h"

namespace vdev {
using namespace setup;

void SetupWindows::CurvePage(Setup &setup, ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "curve", description, {uint64_t(std::max(0, setup.point))}))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target, {"curve.open", "curve.step-count", "curve.fitting"}, propertyData))
        return;
    const auto values = propertyData.Values();
    const int count = int(data.Integer("count"));
    setup.point = int(data.Integer("index"));
    const auto point = data.Object("point");
    SetupDetails pointData;
    if (point && !ReadProperties(point,
                                 {"curvepoint.position", "curvepoint.bias", "curvepoint.tension",
                                  "curvepoint.continuity", "curvepoint.linear"},
                                 pointData))
        return;
    const auto pointValues = pointData.Values();
    // Position / Orientation / Scale, and the current point's position.
    // Dev's second object position (sub_449270 40..43): the point's.
    Entity3DPage(target, nullptr, [&] {
        Group("Current Point Position");
        SetupForm form("##curve-point-position");
        if (!form) return;
        const auto p = VectorOf(pointValues, "curvepoint.position");
        float v[3] = {p.x, p.y, p.z};
        const char *axes[] = {"X :", "Y :", "Z :"};
        ImGui::BeginDisabled(!point);
        for (int i = 0; i < 3; ++i) {
            form.Row(axes[i]);
            ImGui::PushID(i);
            if (ImGui::DragFloat("##point", &v[i], .01f, 0, 0, "%.4f") && point)
                PreviewValue(point, "curvepoint.position", VxVector(v[0], v[1], v[2]));
            ImGui::PopID();
        }
        ImGui::EndDisabled();
    });
    const float lower = ImGui::GetCursorPosY();
    // Left: player visibility, type, steps, fitting, length.
    ImGui::SetCursorPos(ImVec2(8, lower));
    ImGui::BeginGroup();
    bool shown = data.Boolean("player-shown");
    // Dev.exe sub_4584A0: the 3D Layout's list of the objects shown in Player Mode
    // (not the object's visibility).
    if (ImGui::Checkbox("Show In Player Mode", &shown))
        RunSetupCommand("setup.player-shown", {ObjectArgument("target", target), IntegerArgument("value", shown)});
    ImGui::Dummy(ImVec2(1, 6));
    Group("Curve type");
    const bool linear = data.Boolean("linear");
    auto setLinear = [&](bool on) {
        RunSetupCommand("setup.curve-linear", {ObjectArgument("target", target), IntegerArgument("value", on)});
    };
    if (ImGui::RadioButton("Spline", !linear) && linear) setLinear(false);
    ImGui::SameLine();
    if (ImGui::RadioButton("Linear", linear) && !linear) setLinear(true);
    const bool open = values.Boolean("curve.open");
    if (ImGui::RadioButton("Open", open) && !open)
        PreviewValue(id, "curve.open", true);
    ImGui::SameLine();
    if (ImGui::RadioButton("Close", !open) && open)
        PreviewValue(id, "curve.open", false);
    ImGui::Dummy(ImVec2(1, 6));
    int steps = int(values.Integer("curve.step-count"));
    ImGui::SetNextItemWidth(120);
    if (ImGui::SliderInt("Step count", &steps, 1, 1000))
        PreviewValue(id, "curve.step-count", steps);
    float fitting = float(values.Real("curve.fitting"));
    ImGui::SetNextItemWidth(120);
    if (ImGui::SliderFloat("Fitting Coef", &fitting, -1.0f, 1.0f, "%.3f"))
        PreviewValue(id, "curve.fitting", fitting);
    ImGui::Text("Curve length :  %.4f", data.Real("length"));
    ImGui::EndGroup();
    // Middle: Modify Curve.
    ImGui::SameLine(330);
    ImGui::BeginGroup();
    Group("Modify Curve");
    // Dev (sub_4584A0, commands 128..131): each button turns on the 3D
    // Layout's tool on this curve (pressed again: off); Referential is the
    // plane Add Points puts points on.
    static const char *const tools[4] = {"Add Points", "Insert Points", "Edit Points", "Delete Points"};
    const auto client = Client(setup.nameOwner);
    const auto tool = client.Query("layout.curve-tool");
    const auto *toolTarget = EditorClient::Find(tool, "target");
    const auto toolId = EditorClient::Integer(tool, "tool");
    auto plane = int(EditorClient::Integer(tool, "plane"));
    for (int i = 0; i < 4; ++i) {
        const bool active = toolTarget && toolTarget->object == setup.reference && toolId == uint64_t(128 + i);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(tools[i], ImVec2(110, 0))) {
            client.Command("layout.set-curve-tool",
                           {ObjectArgument("target", setup.reference), IntegerArgument("tool", active ? 0 : 128 + i),
                            IntegerArgument("plane", plane)});
        }
        if (active) ImGui::PopStyleColor();
    }
    ImGui::TextUnformatted("Referential");
    static const char *const referentials[2] = {"Guide", "View Plane"};
    ImGui::SetNextItemWidth(110);
    if (TypeAheadCombo("##referential", &plane, referentials, 2))
        client.Command("layout.set-curve-tool",
                       {ObjectArgument("target", toolTarget ? toolTarget->object : ObjectRef{}),
                        IntegerArgument("tool", toolId), IntegerArgument("plane", plane)});
    // Show curve points (Dev.exe sub_4584A0 183): the 3D Layout draws the
    // curve's control points (its list +180).
    bool showPoints = data.Boolean("points-shown");
    if (ImGui::Checkbox("Show curve points", &showPoints))
        RunSetupCommand("setup.curve-points-shown",
                        {ObjectArgument("target", target), IntegerArgument("value", showPoints)});
    ImGui::EndGroup();
    // Right: the current control point.
    ImGui::SameLine(560);
    ImGui::BeginGroup();
    Group("Current Point");
    ImGui::Text("%d  out of  %d", count ? setup.point + 1 : 0, count);
    if (ImGui::Button("|<")) setup.point = 0;
    ImGui::SameLine();
    if (ImGui::Button("<")) setup.point = std::max(0, setup.point - 1);
    ImGui::SameLine();
    if (ImGui::Button(">")) setup.point = std::min(std::max(0, count - 1), setup.point + 1);
    ImGui::SameLine();
    if (ImGui::Button(">|")) setup.point = std::max(0, count - 1);
    ImGui::BeginDisabled(!point);
    auto tcb = [&](const char *property, const char *label) {
        float value = point ? float(pointValues.Real(property)) : 0;
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderFloat(label, &value, -10, 10, "%.3f") && point)
            PreviewValue(point, property, value);
    };
    tcb("curvepoint.bias", "Bias");
    tcb("curvepoint.tension", "Tension");
    tcb("curvepoint.continuity", "Continuity");
    bool segment = point && pointValues.Boolean("curvepoint.linear");
    if (ImGui::Checkbox("Next Segment Is Linear", &segment) && point)
        PreviewValue(point, "curvepoint.linear", segment);
    ImGui::Text("Point distance on curve :  %.4f", data.Real("point-length"));
    ImGui::EndDisabled();
    ImGui::EndGroup();
}

void SetupWindows::CharacterPage(Setup &setup, ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "character", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target, {"entity3d.pickable"}, propertyData))
        return;
    const auto values = propertyData.Values();
    Entity3DPage(target);
    const float lower = ImGui::GetCursorPosY();
    ImGui::SetCursorPos(ImVec2(8, lower));
    ImGui::BeginGroup();
    Group("Character Bodyparts");
    if (ImGui::BeginTable("bodyparts", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                              ImGuiTableFlags_Resizable, ImVec2(700, 230))) {
        TypeAhead typeAhead;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Parent");
        ImGui::TableSetupColumn("Current Mesh");
        ImGui::TableSetupColumn("Vertices", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableHeadersRow();
        const auto root = data.Object("root");
        // The rows (Dev.exe sub_44F330): a click selects (Ctrl: adds or
        // takes off), a double click opens the Body Part Setup; the menu:
        // Show Body Part Setup, Remove Body Part; elsewhere Add Body Part.
        auto &rows = setup.bodyPartRows;
        for (auto it = rows.begin(); it != rows.end();) {
            const bool held = std::any_of(data.Rows("bodies").begin(), data.Rows("bodies").end(), [&](const auto &row) {
                return SetupFields(row.fields).Object("target") == *it;
            });
            it = held ? std::next(it) : rows.erase(it);
        }
        auto removeParts = [&] {
            if (rows.empty()) return;
            FlushPending();
            RunSetupCommand("setup.character-remove-parts",
                            {ObjectArgument("target", target), ObjectsArgument("objects", {rows.begin(), rows.end()})});
            rows.clear();
        };
        for (size_t i = 0; i < data.Rows("bodies").size(); ++i) {
            const SetupFields part(data.Rows("bodies")[i].fields);
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const auto partId = part.Object("target");
            const bool selected = rows.count(partId) != 0;
            const std::string name = part.Text("name") + (partId == root ? "  (Root)" : "");
            if (typeAhead.Selectable(name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    Open(partId);
                else if (!ImGui::GetIO().KeyCtrl) rows = {partId};
                else if (selected) rows.erase(partId);
                else rows.insert(partId);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) rows = {partId};
            if (ImGui::BeginPopupContextItem("bodypartmenu")) {
                if (ImGui::MenuItem("Show Body Part Setup", nullptr, false, rows.size() == 1))
                    Open(*rows.begin());
                ImGui::Separator();
                if (ImGui::MenuItem("Remove Body Part", "Delete", false, !rows.empty())) removeParts();
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(part.Text("parent-name").c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(part.Text("mesh-name").c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", int(part.Integer("vertices")));
            ImGui::PopID();
        }
        if (ImGui::BeginPopupContextWindow("bodypartsmenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Add Body Part")) setup.openAddBodyPart = true;
            ImGui::EndPopup();
        }
        if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            removeParts();
        ImGui::EndTable();
    }
    // Add Body Part: the body parts of no character.
    if (setup.openAddBodyPart) {
        setup.openAddBodyPart = false;
        ImGui::OpenPopup("Add Body Part");
    }
    if (ImGui::BeginPopup("Add Body Part")) {
        TypeAhead typeAhead;
        bool any = false;
        for (const auto &value : data.Rows("available")) {
            const SetupFields object(value.fields);
            any = true;
            ImGui::PushID(int(object.Object("target").object));
            if (typeAhead.Selectable(object.Text("name").c_str())) {
                FlushPending();
                RunSetupCommand("setup.character-add-parts", {ObjectArgument("target", target),
                                                              ObjectsArgument("objects", {object.Object("target")})});
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        if (!any) ImGui::TextDisabled("No body part without a character");
        ImGui::EndPopup();
    }
    ImGui::EndGroup();
    ImGui::SameLine(740);
    ImGui::BeginGroup();
    bool pickable = values.Boolean("entity3d.pickable");
    if (ImGui::Checkbox("Pickable", &pickable))
        PreviewValue(id, "entity3d.pickable", pickable);
    ImGui::TextDisabled("(Hold shift to pick it anyway)");
    ImGui::Dummy(ImVec2(1, 8));
    ImGui::Text("Body parts:  %d", int(data.Rows("bodies").size()));
    ImGui::Text("Animations:  %d", int(data.Rows("animations").size()));
    ImGui::Text("Active:      %s", data.Text("active-name").c_str());
    ImGui::Dummy(ImVec2(1, 8));
    // The character's animations; double-click opens the Animation Setup.
    UsedByRows("Animations", data.Rows("animations"), 150);
    ImGui::EndGroup();
}

void SetupWindows::SoundPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "sound", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target,
                        {"wavesound.streaming", "sound.save-options", "wavesound.type", "wavesound.loop",
                         "wavesound.gain", "wavesound.pitch", "wavesound.pan", "wavesound.attached",
                         "wavesound.position", "wavesound.direction", "wavesound.cone", "wavesound.distance"},
                        propertyData))
        return;
    const auto values = propertyData.Values();
    const bool source = data.Boolean("source");
    const bool point = values.Integer("wavesound.type") == CK_WAVESOUND_POINT;
    // Left: the sound file, save options, type.
    ImGui::BeginGroup();
    Group("Sound Info");
    ImGui::Text("Sampling rate :  %d Hz", int(data.Integer("sample-rate")));
    ImGui::Text("Sample size :    %d bits", int(data.Integer("sample-bits")));
    ImGui::Text("Channels :       %d (%s)", int(data.Integer("channels")),
                data.Integer("channels") > 1 ? "Stereo" : "Mono");
    // Documentation "Sound Setup": "Sound duration (in minutes:seconds:milliseconds)".
    {
        const int length = std::max(0, int(data.Integer("length")));
        ImGui::Text("Duration :       %d:%02d:%03d", length / 60000, (length / 1000) % 60, length % 1000);
    }
    // Streamed (documentation "Sound Setup": "Enables streaming"): the
    // sound made again from its file (SetFileStreaming, recreated).
    bool streamed = values.Boolean("wavesound.streaming") != 0;
    if (ImGui::Checkbox("Streamed", &streamed))
        PreviewValue(id, "wavesound.streaming", streamed);
    ImGui::Dummy(ImVec2(1, 6));
    const char *saves[] = {"External", "Include Original File", "Use Global"};
    int save = std::max(0, std::min(2, static_cast<int>(values.Integer("sound.save-options"))));
    ImGui::SetNextItemWidth(170);
    if (TypeAheadCombo("Save Options", &save, saves, 3))
        PreviewValue(id, "sound.save-options", save);
    ImGui::Dummy(ImVec2(1, 6));
    Group("Type");
    if (ImGui::RadioButton("Background", !point) && point)
        PreviewValue(id, "wavesound.type", CK_WAVESOUND_BACKGROUND);
    ImGui::SameLine();
    if (ImGui::RadioButton("Point", point) && !point)
        PreviewValue(id, "wavesound.type", CK_WAVESOUND_POINT);
    if (point && source) {
        const auto position = VectorOf(data, "listener-position");
        const float distance = float(data.Real("distance"));
        ImGui::Text("Distance to listener :  %.3f", distance);
        ImGui::Text("Sound position :  %.2f  %.2f  %.2f", position.x, position.y, position.z);
        ImGui::Text("Relative angle :  %.1f", std::atan2(position.x, position.z) * RAD_TO_DEG);
    } else if (point) {
        ImGui::TextDisabled("Distance to listener :  (not available)");
    }
    ImGui::EndGroup();
    // Middle: Misc and 2D settings.
    ImGui::SameLine(330);
    ImGui::BeginGroup();
    Group("Misc");
    bool loop = values.Boolean("wavesound.loop") != 0;
    if (ImGui::Checkbox("Loop", &loop))
        PreviewValue(id, "wavesound.loop", loop);
    ImGui::BeginDisabled(!source);
    if (ImGui::Button("Play", ImVec2(60, 0)))
        RunSetupCommand("setup.sound-play", {ObjectArgument("target", target)});
    ImGui::SameLine();
    // Pause (documentation: "Plays, pauses or stops"); again: on from there.
    if (ImGui::Button(data.Boolean("paused") ? "Resume" : "Pause", ImVec2(60, 0)))
        RunSetupCommand("setup.sound-pause", {ObjectArgument("target", target)});
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(60, 0)))
        RunSetupCommand("setup.sound-stop", {ObjectArgument("target", target)});
    ImGui::EndDisabled();
    if (!source) ImGui::TextDisabled("(no sound device)");
    ImGui::Dummy(ImVec2(1, 6));
    Group("2D");
    float gain = values.Real("wavesound.gain");
    ImGui::SetNextItemWidth(160);
    if (ImGui::SliderFloat("Gain", &gain, 0.0f, 1.0f, "%.3f"))
        PreviewValue(id, "wavesound.gain", gain);
    float pitch = values.Real("wavesound.pitch");
    ImGui::SetNextItemWidth(160);
    if (ImGui::SliderFloat("Pitch", &pitch, 0.5f, 2.0f, "%.3f"))
        PreviewValue(id, "wavesound.pitch", pitch);
    // Pan (documentation: "Background Sounds only"): -1 left to 1 right.
    ImGui::BeginDisabled(point);
    float pan = values.Real("wavesound.pan");
    ImGui::SetNextItemWidth(160);
    if (ImGui::SliderFloat("Pan", &pan, -1.0f, 1.0f, "%.3f"))
        PreviewValue(id, "wavesound.pan", pan);
    ImGui::EndDisabled();
    ImGui::EndGroup();
    // Right: 3D settings (point sounds).
    ImGui::SameLine(640);
    ImGui::BeginGroup();
    Group("3D");
    ImGui::BeginDisabled(!point);
    const auto attached = data.Object("attached");
    const auto position = VectorOf(values, "wavesound.position"), direction = VectorOf(values, "wavesound.direction");
    ObjectRef chosen{};
    ImGui::TextUnformatted("Attached Object");
    if (ObjectChoice("##attached", CKCID_3DENTITY, attached, data.Text("attached-name"), &chosen, 200))
        PreviewValue(id, "wavesound.attached", chosen);
    auto vector3 = [&](const char *label, VxVector value, bool isPosition) {
        float v[3] = {value.x, value.y, value.z};
        ImGui::SetNextItemWidth(240);
        if (ImGui::DragFloat3(label, v, 0.01f, 0.0f, 0.0f, "%.3f")) {
            PreviewValue(id, isPosition ? "wavesound.position" : "wavesound.direction", VxVector(v[0], v[1], v[2]));
        }
    };
    vector3("Relative Position", position, true);
    vector3("Relative Orientation", direction, false);
    const auto coneValue = VectorOf(values, "wavesound.cone");
    float cone[3] = {coneValue.x, coneValue.y, coneValue.z};
    bool coneChanged = false;
    ImGui::SetNextItemWidth(120);
    coneChanged |= ImGui::SliderFloat("Cone inside angle", &cone[0], 0.0f, 360.0f, "%.1f");
    ImGui::SetNextItemWidth(120);
    coneChanged |= ImGui::SliderFloat("Cone outside angle", &cone[1], 0.0f, 360.0f, "%.1f");
    ImGui::SetNextItemWidth(120);
    coneChanged |= ImGui::SliderFloat("Outside gain", &cone[2], 0.0f, 1.0f, "%.3f");
    if (coneChanged)
        PreviewValue(id, "wavesound.cone", VxVector(cone[0], cone[1], cone[2]));
    const auto distance = Vector2Of(values, "wavesound.distance");
    float range[2] = {distance.x, distance.y};
    ImGui::SetNextItemWidth(160);
    if (ImGui::DragFloat2("Min / Max distance", range, 0.1f, 0.0f, 100000.0f, "%.2f"))
        PreviewValue(id, "wavesound.distance", Vx2DVector(range[0], range[1]));
    ImGui::EndDisabled();
    ImGui::EndGroup();
}

void SetupWindows::ArrayPage(Setup &setup) {
    if (!FlushPending())
        return;
    auto &application = m_Workspace.Application();
    setup.array.Draw(EditorClient(application.Host(), setup.nameOwner), setup.reference);
}

void SetupWindows::ScenePage(ObjectRef target) {
    SetupDetails description;
    if (!ReadDetails(target, "scene", description))
        return;
    const auto data = description.Values();
    const auto id = data.Object("scene");
    if (!id) {
        ImGui::TextDisabled("The level has no level scene.");
        return;
    }
    SetupDetails propertyData;
    if (!ReadProperties(id,
                        {"scene.ambient", "scene.fog", "scene.background", "scene.use-environment", "scene.fog-mode",
                         "scene.fog-start", "scene.fog-end", "scene.fog-density", "scene.background-image",
                         "scene.starting-camera"},
                        propertyData))
        return;
    const auto values = propertyData.Values();
    SetupColumns columns("##scene-sections", 2);
    if (!columns) return;
    columns.Next();
    Group("Colors");
    {
        SetupForm form("##scene-colors");
        if (form) {
            auto color = [&](const char *label, const char *field) {
                form.Row(label);
                const auto c = ColorOf(values, field);
                ImGui::PushID(label);
                const float side = ImGui::GetFrameHeight();
                if (ImGui::ColorButton("##swatch", {c.r, c.g, c.b, 1}, ImGuiColorEditFlags_NoTooltip,
                                       {std::max(side, 46 * PageUnit()), side}))
                    ImGui::OpenPopup("picker");
                if (ImGui::BeginPopup("picker")) {
                    float rgb[3] = {c.r, c.g, c.b};
                    if (ImGui::ColorPicker3("##picker", rgb))
                        PreviewValue(id, field, VxColor(rgb[0], rgb[1], rgb[2], c.a));
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            };
            color("Ambient", "scene.ambient");
            color("Fog", "scene.fog");
            color("Background", "scene.background");
        }
    }
    columns.Next();
    bool use = values.Boolean("scene.use-environment");
    if (ImGui::Checkbox("Use settings at Scene Start", &use))
        PreviewValue(id, "scene.use-environment", use);
    columns.Next();
    Group("Fog Settings");
    int type = std::clamp(int(values.Integer("scene.fog-mode")), 0, 3);
    {
        SetupForm form("##scene-fog");
        if (form) {
            form.Row("Type:");
            const char *types[] = {"None", "Exponential", "Exponential Squared", "Linear"};
            if (TypeAheadCombo("##fogtype", &type, types, 4)) PreviewValue(id, "scene.fog-mode", type);
            if (type != 0) {
                auto number = [&](const char *label, float value, float step, const char *field) {
                    form.Row(label);
                    ImGui::PushID(label);
                    if (ImGui::DragFloat("##value", &value, step, 0, 0, "%.3f")) PreviewValue(id, field, value);
                    ImGui::PopID();
                };
                number("Fog Start:", float(values.Real("scene.fog-start")), .5f, "scene.fog-start");
                number("Fog End:", float(values.Real("scene.fog-end")), .5f, "scene.fog-end");
                number("Fog Density:", float(values.Real("scene.fog-density")), .001f, "scene.fog-density");
            }
        }
    }
    if (type != 0 && values.Real("scene.fog-start") > values.Real("scene.fog-end")) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(CurrentDevTheme().warningText),
                           "Fog Start must be smaller than Fog End");
        ImGui::PopTextWrapPos();
    }
    columns.Next();
    Group("Other Settings");
    SetupForm form("##scene-other");
    if (form) {
        ObjectRef chosen{};
        form.Row("Background Image");
        if (ObjectChoice("##background", CKCID_TEXTURE, data.Object("background"), data.Text("background-name"),
                         &chosen, ImGui::GetContentRegionAvail().x))
            PreviewValue(id, "scene.background-image", chosen);
        form.Row("Starting Camera");
        if (ObjectChoice("##camera", CKCID_CAMERA, data.Object("camera"), data.Text("camera-name"), &chosen,
                         ImGui::GetContentRegionAvail().x))
            PreviewValue(id, "scene.starting-camera", chosen);
    }
}

void SetupWindows::TransformFields(ObjectRef target, const char *field, float *world, float *local, float divisor,
                                   const char *worldId) {
    const auto contentId = ImGui::GetID("");
    if (!ImGui::BeginTable("##components", local ? 3 : 2,
                           ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
        return;
    ImGui::TableSetupColumn("##axis", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("X :").x);
    ImGui::TableSetupColumn("World");
    if (local) ImGui::TableSetupColumn("Local");
    ImGui::TableHeadersRow();
    ImGui::PushOverrideID(contentId);
    const char *axes[] = {"X :", "Y :", "Z :"};
    for (int i = 0; i < 3; ++i) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(axes[i]);
        ImGui::PushID(i);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::DragFloat(worldId, &world[i], .01f, 0, 0, "%.4f"))
            PreviewNumbers(target, ("entity3d.world-" + std::string(field)).c_str(), world, 3, divisor);
        if (local) {
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##local", &local[i], .01f, 0, 0, "%.4f"))
                PreviewNumbers(target, ("entity3d.local-" + std::string(field)).c_str(), local, 3, divisor);
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    ImGui::EndTable();
}

void SetupWindows::FramePage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "frame", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target, {"entity3d.pickable"}, propertyData))
        return;
    const auto values = propertyData.Values();
    const auto parent = data.Object("parent");
    {
        SetupColumns columns("##frame-transform", 3);
        if (columns) {
            auto group = [&](const char *title, const char *button, const std::function<void()> &press, float *world,
                             float *local, const char *field, float divisor) {
                columns.Next();
                Group(title);
                if (button && ImGui::SmallButton(button)) press();
                ImGui::PushID(title);
                TransformFields(id, field, world, parent ? local : nullptr, divisor, "##world");
                ImGui::PopID();
            };
            const ShownTransform &transform = Shown(target);
            const VxVector wp = Translation(transform.world), lp = Translation(transform.local);
            float pw[] = {wp.x, wp.y, wp.z}, pl[] = {lp.x, lp.y, lp.z};
            group(
                "Position", "Reset World Matrix",
                [&] { RunSetupCommand("setup.reset-world-matrix", {ObjectArgument("target", target)}); }, pw, pl,
                "position", 1);
            float ow[3]{}, ol[3]{};
            Vx3DMatrixToEulerAngles(transform.world, &ow[0], &ow[1], &ow[2]);
            Vx3DMatrixToEulerAngles(transform.local, &ol[0], &ol[1], &ol[2]);
            for (int i = 0; i < 3; ++i) {
                ow[i] *= RAD_TO_DEG;
                ol[i] *= RAD_TO_DEG;
            }
            group("Orientation", nullptr, nullptr, ow, ol, "orientation", RAD_TO_DEG);
            const auto ws = ScaleOf(transform.world), ls = ScaleOf(transform.local);
            float sw[] = {ws.x, ws.y, ws.z}, sl[] = {ls.x, ls.y, ls.z};
            group(
                "Scale", data.Boolean("place") ? "Set as Unit" : nullptr,
                [&] { RunSetupCommand("setup.scale-unit", {ObjectArgument("target", target)}); }, sw, sl, "scale", 1);
        }
    }
    const bool place = data.Boolean("place"), portal = data.Boolean("portal");
    ImGui::Separator();
    if (place) {
        PlaceLinks(target);
        return;
    }
    ImGui::BeginGroup();
    if (portal) {
        // Portal Setup: Pickable.
        bool pickable = values.Boolean("entity3d.pickable");
        if (ImGui::Checkbox("Pickable", &pickable))
            PreviewValue(id, "entity3d.pickable", pickable);
        ImGui::SameLine();
        ImGui::TextDisabled("(Hold shift to pick it anyway)");
        ImGui::EndGroup();
        return;
    }
    bool shown = data.Boolean("player-shown");
    // Dev.exe sub_4584A0: the 3D Layout's list of the objects shown in Player Mode
    // (not the object's visibility).
    if (ImGui::Checkbox("Show In Player Mode", &shown))
        RunSetupCommand("setup.player-shown", {ObjectArgument("target", target), IntegerArgument("value", shown)});
    // Dev keeps the representation in the 3D Layout's own table (Dev.exe
    // sub_4C53C0): "No Representation" or one of its "Virtools_Frame_*"
    // meshes, drawn by the 3D Layout only. It is not object state: the
    // table's own undo step.
    ImGui::TextUnformatted("Frame Representation:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(250);
    const auto presentation = Client().Query("layout.presentation");
    const auto *shapeNames = EditorClient::Find(presentation, "frame-shapes");
    const std::vector<std::string> emptyShapes;
    const auto &shapes = shapeNames && shapeNames->type == EDITOR_TEXT_LIST ? shapeNames->texts : emptyShapes;
    const int representation = int(data.Integer("representation"));
    const bool known = representation >= 0 && representation <= static_cast<int>(shapes.size());
    const std::string current = representation == 0 ? "No Representation" : known ? shapes[representation - 1] : "";
    if (ImGui::BeginCombo("##representation", current.c_str())) {
        TypeAhead typeAhead;
        if (typeAhead.Selectable("No Representation", representation == 0))
            RunSetupCommand(
                "layout.frame-representation",
                {ObjectArgument("target", target), ObjectsArgument("objects", {target}), IntegerArgument("value", 0)});
        for (int i = 0; i < static_cast<int>(shapes.size()); ++i) {
            ImGui::PushID(i);
            if (typeAhead.Selectable(shapes[i].c_str(), representation == i + 1))
                RunSetupCommand("layout.frame-representation",
                                {ObjectArgument("target", target), ObjectsArgument("objects", {target}),
                                 IntegerArgument("value", i + 1)});
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::EndGroup();
}

void SetupWindows::PlaceLinks(ObjectRef target) {
    SetupDetails description;
    if (!ReadDetails(target, "place", description))
        return;
    const auto data = description.Values();
    const auto &places = data.Rows("places"), &portals = data.Rows("portals");
    const auto scene = data.Object("scene");
    auto relink = [&](const char *, ObjectRef fromPlace, ObjectRef fromPortal, ObjectRef toPlace, ObjectRef toPortal) {
        RunSetupCommand("setup.place-link",
                        {ObjectArgument("target", target), ObjectArgument("from-place", fromPlace),
                         ObjectArgument("from-portal", fromPortal), ObjectArgument("to-place", toPlace),
                         ObjectArgument("to-portal", toPortal)});
    };
    struct Link {
        ObjectRef place{}, portal{};
    };
    std::vector<Link> links;
    int hoveredRow = -1;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16);
    if (ImGui::BeginTable("placelinks", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(860, 380))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Visible Place");
        ImGui::TableSetupColumn("Through Portal");
        ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < data.Rows("links").size(); ++i) {
            const SetupFields current(data.Rows("links")[i].fields);
            const auto other = current.Object("place"), portal = current.Object("portal");
            const auto otherId = other, portalId = portal;
            links.push_back({otherId, portalId});
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##place", current.Text("place-name").c_str())) {
                TypeAhead typeAhead;
                for (const auto &value : places) {
                    const SetupFields candidate(value.fields);
                    const auto ref = candidate.Object("target");
                    if (typeAhead.Selectable(candidate.Text("name").c_str(), ref == other) && ref != other)
                        relink("Visible Place", otherId, portalId, ref, portalId);
                }
                ImGui::EndCombo();
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##portal", current.Text("portal-name").c_str())) {
                TypeAhead typeAhead;
                if (typeAhead.Selectable("-- None --", !portal) && portal)
                    relink("Through Portal", otherId, portalId, otherId, {});
                for (const auto &value : portals) {
                    const SetupFields candidate(value.fields);
                    const auto ref = candidate.Object("target");
                    if (typeAhead.Selectable(candidate.Text("name").c_str(), ref == portal) && ref != portal)
                        relink("Through Portal", otherId, portalId, otherId, ref);
                }
                ImGui::EndCombo();
            }
            ImGui::TableNextColumn();
            // Active: the portal's Activate At Scene Start flag (blue / red in Dev's 3D Layout).
            bool active = current.Boolean("active");
            ImGui::BeginDisabled(!current.Boolean("active-enabled"));
            if (ImGui::Checkbox("##active", &active))
                RunSetupCommand("scene.flags",
                                {ObjectsArgument("objects", {portalId}), ObjectArgument("target", portalId),
                                 ObjectArgument("scene", scene), IntegerArgument("mode", active ? 3 : 4)});
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        hoveredRow = ImGui::TableGetHoveredRow() - 1; // row 0: the headers
        ImGui::EndTable();
    }
    // Context menu (documentation): Attach New Place, Remove Place, Setup.
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_PlaceMenuRow = hoveredRow >= 0 && hoveredRow < static_cast<int>(links.size()) ? hoveredRow : -1;
        ImGui::OpenPopup("placemenu");
    }
    PushDevMenuStyle();
    if (ImGui::BeginPopup("placemenu")) {
        const Link row = m_PlaceMenuRow >= 0 && m_PlaceMenuRow < static_cast<int>(links.size()) ? links[m_PlaceMenuRow] : Link();
        // A new link to a place not linked yet; its portal is None.
        ObjectRef next{};
        for (const auto &value : places) {
            const auto candidate = SetupFields(value.fields).Object("target");
            bool linked = false;
            for (const auto &link : links)
                linked = linked || link.place == candidate;
            if (!linked) {
                next = candidate;
                break;
            }
        }
        // Automatic Portal (Dev.exe sub_471C10 203): the next click picks the
        // other place (SetupWindows::PortalPick).
        if (ImGui::MenuItem("Automatic Portal")) {
            m_PortalPlace = target;
            m_PortalOwner = m_CurrentOwner;
        }
        if (ImGui::MenuItem("Attach New Place", nullptr, false, bool(next)))
            relink("Attach New Place", {}, {}, next, {});
        if (ImGui::MenuItem("Remove Place", nullptr, false, bool(row.place)))
            relink("Remove Place", row.place, row.portal, {}, {});
        ImGui::Separator();
        if (ImGui::MenuItem("Place Setup", nullptr, false, bool(row.place)))
            Open(row.place);
        if (ImGui::MenuItem("Portal Setup", nullptr, false, bool(row.portal)))
            Open(row.portal);
        ImGui::EndPopup();
    }
    PopDevMenuStyle();
}

void SetupWindows::Sprite3DPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "sprite3d", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target,
                        {"sprite3d.mode", "entity3d.pickable", "sprite3d.offset", "sprite3d.uv", "sprite3d.material",
                         "sprite3d.size"},
                        propertyData))
        return;
    const auto values = propertyData.Values();
    const char *axes[] = {"X :", "Y :", "Z :"};
    {
        SetupColumns columns("##sprite-transform", 3);
        if (columns) {
            auto group = [&](const char *title, const char *button, const std::function<void()> &press, float *v,
                             int count, const char *heading, const char *field, float divisor) {
                columns.Next();
                Group(title);
                if (button && ImGui::SmallButton(button)) press();
                ImGui::TextUnformatted(heading);
                ImGui::PushID(title);
                {
                    SetupForm form("##components");
                    if (form)
                        for (int i = 0; i < count; ++i) {
                            form.Row(axes[i]);
                            ImGui::PushID(i);
                            if (ImGui::DragFloat("##v", &v[i], .01f, 0, 0, "%.4f"))
                                PreviewNumbers(id, field, v, count, divisor);
                            ImGui::PopID();
                        }
                }
                ImGui::PopID();
            };
            const ShownTransform &shown = Shown(target);
            const auto position = Translation(shown.world);
            float p[] = {position.x, position.y, position.z};
            group(
                "Position", "Reset World Matrix",
                [&] { RunSetupCommand("setup.reset-world-matrix", {ObjectArgument("target", target)}); }, p, 3, "World",
                "entity3d.world-position", 1);
            float e[3]{};
            Vx3DMatrixToEulerAngles(shown.world, &e[0], &e[1], &e[2]);
            for (auto &angle : e) angle *= RAD_TO_DEG;
            group("Orientation", nullptr, nullptr, e, 3, "World", "entity3d.world-orientation", RAD_TO_DEG);
            const auto size = Vector2Of(values, "sprite3d.size");
            float sz[] = {size.x, size.y};
            group("Scale", nullptr, nullptr, sz, 2, " ", "sprite3d.size", 1);
        }
    }
    ImGui::Separator();
    SetupColumns columns("##sprite-properties", 3);
    if (!columns) return;
    columns.Next();
    Group("Sprite Type");
    const char *types[] = {"Billboard", "X Rotate", "Y Rotate", "Orientable"};
    const int mode = int(values.Integer("sprite3d.mode"));
    for (int t = 0; t < 4; ++t) {
        bool on = mode == t;
        if (ImGui::Checkbox(types[t], &on) && on && mode != t) PreviewValue(id, "sprite3d.mode", t);
    }
    bool pickable = values.Boolean("entity3d.pickable");
    if (ImGui::Checkbox("Pickable", &pickable)) PreviewValue(id, "entity3d.pickable", pickable);
    ImGui::TextWrapped("(Hold shift to pick it anyway)");
    columns.Next();
    Group("Offset");
    const auto offset = Vector2Of(values, "sprite3d.offset");
    float off[] = {offset.x, offset.y};
    {
        SetupForm form("##offset-fields");
        if (form)
            for (int i = 0; i < 2; ++i) {
                form.Row(axes[i]);
                ImGui::PushID(i);
                if (ImGui::DragFloat("##offset", &off[i], .01f, 0, 0, "%.4f"))
                    PreviewValue(id, "sprite3d.offset", Vx2DVector(off[0], off[1]));
                ImGui::PopID();
            }
    }
    Group("UVMapping");
    const auto uv = RectOf(values, "sprite3d.uv");
    float r[] = {uv.left, uv.top, uv.right, uv.bottom};
    {
        SetupForm form("##uv-fields");
        const char *labels[] = {"U min", "V min", "U max", "V max"};
        if (form)
            for (int i = 0; i < 4; ++i) {
                form.Row(labels[i]);
                ImGui::PushID(i);
                if (ImGui::DragFloat("##uv", &r[i], .001f, 0, 0, "%.4f"))
                    PreviewValue(id, "sprite3d.uv", VxRect(r[0], r[1], r[2], r[3]));
                ImGui::PopID();
            }
    }
    columns.Next();
    Group("Material:");
    const auto material = data.Object("material");
    ImGui::BeginDisabled(!material);
    if (ImGui::SmallButton(">") && material) Open(material);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Material Setup");
    ObjectRef chosen{};
    if (ObjectChoice("##material", CKCID_MATERIAL, material, data.Text("material-name"), &chosen,
                     ImGui::GetContentRegionAvail().x))
        PreviewValue(id, "sprite3d.material", chosen);
}

} // namespace vdev
