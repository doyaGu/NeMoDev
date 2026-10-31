#include "core/level/ArrayCommands.h"
#include "core/layout/LayoutCommands.h"
// The Setups of curves, characters, sounds, arrays, levels and scenes, 3D frames and sprites, places, 2D entities.

#include "ui/setup/SetupWindows.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>


#include "imgui.h"
#include "imgui_internal.h"

#include "CKAll.h"

#include "core/runtime/CkRuntimeHost.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/FileDialogs.h"
#include "ui/controls/TypeAhead.h"
#include "core/runtime/GridCompat.h"
#include "core/level/LevelCommands.h"
#include "core/level/ObjectEditCommands.h"
#include "core/level/ObjectProperties.h"
#include "core/level/SetupCommands.h"
#include "core/level/SceneCommands.h"
#include "ui/schematic/Snapshots.h"
#include "core/runtime/TextEncoding.h"
#include "core/graph/ValueCommands.h"
#include "ui/setup/SetupWindowsDetail.h"

namespace vdev {

using namespace setup;

// ---------------------------------------------------------------- Curve Setup

void SetupWindows::CurvePage(Setup &setup, CKCurve *curve) {
    if (!curve) return;
    const CK_ID id = curve->GetID();
    const int count = curve->GetControlPointCount();
    setup.point = std::max(0, std::min(setup.point, count - 1));
    CKCurvePoint *point = count ? curve->GetControlPoint(setup.point) : nullptr;
    // Position / Orientation / Scale, and the current point's position.
    // Dev's second object position (sub_449270 40..43): the point's.
    Entity3DPage(curve, true, false, [&](const DevPage &page) {
        page.Label(620, 2, 800, 20, "Current Point Position");
        page.Line(625, 20, 626, 97);
        VxVector p(0, 0, 0);
        if (point) point->GetPosition(&p);
        float v[3] = {p.x, p.y, p.z};
        const char *axes[] = {"X :", "Y :", "Z :"};
        ImGui::BeginDisabled(!point);
        for (int i = 0; i < 3; ++i) {
            ImGui::PushID(i);
            const float y = 37.0f + 20.0f * i;
            page.Label(640, y, 665, y + 20, axes[i]);
            page.Place(670, y, 740, y + 20);
            if (ImGui::DragFloat("##point", &v[i], 0.01f, 0.0f, 0.0f, "%.4f") && point)
                LiveSet(point->GetID(), properties::CurvePointPosition, VxVector(v[0], v[1], v[2]));
            ImGui::PopID();
        }
        ImGui::EndDisabled();
    });
    const float lower = ImGui::GetCursorPosY();
    // Left: player visibility, type, steps, fitting, length.
    ImGui::SetCursorPos(ImVec2(8, lower));
    ImGui::BeginGroup();
    bool shown = m_State.document->Layout().playerShown.count(id) != 0;
    // Dev.exe sub_4584A0: the 3D Layout's list of the objects shown in Player Mode
    // (not the object's visibility).
    if (ImGui::Checkbox("Show In Player Mode", &shown))
        SetLayoutMembership(*m_State.document, LayoutList::PlayerShown, id, shown, "Show In Player Mode");
    ImGui::Dummy(ImVec2(1, 6));
    Group("Curve type");
    bool linear = count > 0;
    for (int i = 0; i < count; ++i)
        if (CKCurvePoint *p = curve->GetControlPoint(i))
            if (!p->IsLinear()) linear = false;
    std::vector<CK_ID> points;
    for (int i = 0; i < count; ++i)
        if (CKCurvePoint *p = curve->GetControlPoint(i)) points.push_back(p->GetID());
    auto setLinear = [&](bool on) {
        EditObjectsState(*m_State.document, points, on ? "Linear Curve" : "Spline Curve",
                         [on](CKObject *o) { CKCurvePoint::Cast(o)->SetLinear(on); });
    };
    if (ImGui::RadioButton("Spline", !linear) && linear) setLinear(false);
    ImGui::SameLine();
    if (ImGui::RadioButton("Linear", linear) && !linear) setLinear(true);
    const bool open = curve->IsOpen() != 0;
    if (ImGui::RadioButton("Open", open) && !open) LiveSet(id, properties::CurveOpen, true);
    ImGui::SameLine();
    if (ImGui::RadioButton("Close", !open) && open)
        LiveSet(id, properties::CurveOpen, false);
    ImGui::Dummy(ImVec2(1, 6));
    int steps = curve->GetStepCount();
    ImGui::SetNextItemWidth(120);
    if (ImGui::SliderInt("Step count", &steps, 1, 1000))
        LiveSet(id, properties::CurveStepCount, steps);
    float fitting = curve->GetFittingCoeff();
    ImGui::SetNextItemWidth(120);
    if (ImGui::SliderFloat("Fitting Coef", &fitting, -1.0f, 1.0f, "%.3f"))
        LiveSet(id, properties::CurveFitting, fitting);
    ImGui::Text("Curve length :  %.4f", curve->GetLength());
    ImGui::EndGroup();
    // Middle: Modify Curve.
    ImGui::SameLine(330);
    ImGui::BeginGroup();
    Group("Modify Curve");
    // Dev (sub_4584A0, commands 128..131): each button turns on the 3D
    // Layout's tool on this curve (pressed again: off); Referential is the
    // plane Add Points puts points on.
    static const char *const tools[4] = {"Add Points", "Insert Points", "Edit Points", "Delete Points"};
    CurveTool &tool = m_State.curveTool;
    for (int i = 0; i < 4; ++i) {
        const bool active = tool.curve == id && tool.tool == 128 + i;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(tools[i], ImVec2(110, 0))) {
            if (active) {
                tool.tool = 0;
            } else {
                tool.curve = id;
                tool.tool = 128 + i;
            }
        }
        if (active) ImGui::PopStyleColor();
    }
    ImGui::TextUnformatted("Referential");
    static const char *const referentials[2] = {"Guide", "View Plane"};
    ImGui::SetNextItemWidth(110);
    TypeAheadCombo("##referential", &tool.referential, referentials, 2);
    // Show curve points (Dev.exe sub_4584A0 183): the 3D Layout draws the
    // curve's control points (its list +180).
    bool showPoints = !m_State.document->Layout().hiddenCurvePoints.count(id);
    if (ImGui::Checkbox("Show curve points", &showPoints))
        SetLayoutMembership(*m_State.document, LayoutList::HiddenCurvePoints, id, !showPoints, "Show curve points");
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
    const CK_ID pointId = point ? point->GetID() : 0;
    auto tcb = [&](const ObjectProperty<float> &property) {
        float value = point ? property.get(point) : 0.0f;
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderFloat(property.label, &value, -10.0f, 10.0f, "%.3f") && point) LiveSet(pointId, property, value);
    };
    tcb(properties::CurvePointBias);
    tcb(properties::CurvePointTension);
    tcb(properties::CurvePointContinuity);
    bool segment = point && point->IsLinear();
    if (ImGui::Checkbox("Next Segment Is Linear", &segment) && point)
        LiveSet(pointId, properties::CurvePointLinear, segment);
    ImGui::Text("Point distance on curve :  %.4f", point ? point->GetLength() : 0.0f);
    ImGui::EndDisabled();
    ImGui::EndGroup();
}

// ---------------------------------------------------------------- Character Setup

void SetupWindows::CharacterPage(Setup &setup, CKCharacter *character) {
    if (!character) return;
    const CK_ID id = character->GetID();
    Entity3DPage(character, true, false);
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
        CKBodyPart *root = character->GetRootBodyPart();
        // The rows (Dev.exe sub_44F330): a click selects (Ctrl: adds or
        // takes off), a double click opens the Body Part Setup; the menu:
        // Show Body Part Setup, Remove Body Part; elsewhere Add Body Part.
        std::set<CK_ID> &rows = setup.bodyPartRows;
        for (auto it = rows.begin(); it != rows.end();) {
            CKBodyPart *part = Get<CKBodyPart>(m_State, *it);
            it = part && part->GetCharacter() == character ? std::next(it) : rows.erase(it);
        }
        auto removeParts = [&] {
            if (rows.empty()) return;
            FlushPending();
            Report(RemoveBodyParts(*m_State.document, id, std::vector<CK_ID>(rows.begin(), rows.end())), "Remove Body Part");
            rows.clear();
        };
        for (int i = 0; i < character->GetBodyPartCount(); ++i) {
            CKBodyPart *part = character->GetBodyPart(i);
            if (!part) continue;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const CK_ID partId = part->GetID();
            const bool selected = rows.count(partId) != 0;
            const std::string name = NameOf(part) + (part == root ? "  (Root)" : "");
            if (typeAhead.Selectable(name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) m_State.events.Post(ShowObjectSetup{m_State.document->Reference(partId)});
                else if (!ImGui::GetIO().KeyCtrl) rows = {partId};
                else if (selected) rows.erase(partId);
                else rows.insert(partId);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) rows = {partId};
            if (ImGui::BeginPopupContextItem("bodypartmenu")) {
                if (ImGui::MenuItem("Show Body Part Setup", nullptr, false, rows.size() == 1))
                    m_State.events.Post(ShowObjectSetup{m_State.document->Reference(*rows.begin())});
                ImGui::Separator();
                if (ImGui::MenuItem("Remove Body Part", "Delete", false, !rows.empty())) removeParts();
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(NameOf(part->GetParent()).c_str());
            ImGui::TableNextColumn();
            CKMesh *mesh = part->GetCurrentMesh();
            ImGui::TextUnformatted(NameOf(mesh).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", mesh ? mesh->GetVertexCount() : 0);
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
        for (CKObject *object : ObjectsOf(*m_State.document, CKCID_BODYPART, true)) {
            if (CKBodyPart::Cast(object)->GetCharacter()) continue;
            any = true;
            ImGui::PushID(static_cast<int>(object->GetID()));
            if (typeAhead.Selectable(NameOf(object).c_str())) {
                FlushPending();
                Report(AddBodyParts(*m_State.document, id, {object->GetID()}), "Add Body Part");
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
    bool pickable = character->IsPickable() != 0;
    if (ImGui::Checkbox("Pickable", &pickable))
        LiveSet(id, properties::Entity3DPickable, pickable);
    ImGui::TextDisabled("(Hold shift to pick it anyway)");
    ImGui::Dummy(ImVec2(1, 8));
    ImGui::Text("Body parts:  %d", character->GetBodyPartCount());
    ImGui::Text("Animations:  %d", character->GetAnimationCount());
    ImGui::Text("Active:      %s", NameOf(character->GetActiveAnimation()).c_str());
    ImGui::Dummy(ImVec2(1, 8));
    // The character's animations; double-click opens the Animation Setup.
    std::vector<CKObject *> animations;
    for (int i = 0; i < character->GetAnimationCount(); ++i)
        if (CKAnimation *animation = character->GetAnimation(i)) animations.push_back(animation);
    UsedBy("Animations", animations, 150);
    ImGui::EndGroup();
}

// ---------------------------------------------------------------- Sound Setup

void SetupWindows::SoundPage(CKWaveSound *sound) {
    if (!sound) return;
    const CK_ID id = sound->GetID();
    CKWaveFormat format;
    std::memset(&format, 0, sizeof(format));
    sound->GetSoundFormat(format);
    const bool source = sound->GetSource() != nullptr;
    const bool point = sound->GetType() == CK_WAVESOUND_POINT;
    // Left: the sound file, save options, type.
    ImGui::BeginGroup();
    Group("Sound Info");
    ImGui::Text("Sampling rate :  %d Hz", static_cast<int>(format.nSamplesPerSec));
    ImGui::Text("Sample size :    %d bits", static_cast<int>(format.wBitsPerSample));
    ImGui::Text("Channels :       %d (%s)", static_cast<int>(format.nChannels), format.nChannels > 1 ? "Stereo" : "Mono");
    // Documentation "Sound Setup": "Sound duration (in minutes:seconds:milliseconds)".
    {
        const int length = std::max(0, static_cast<int>(sound->GetSoundLength()));
        ImGui::Text("Duration :       %d:%02d:%03d", length / 60000, (length / 1000) % 60, length % 1000);
    }
    // Streamed (documentation "Sound Setup": "Enables streaming"): the
    // sound made again from its file (SetFileStreaming, recreated).
    bool streamed = sound->GetFileStreaming() != 0;
    if (ImGui::Checkbox("Streamed", &streamed))
        LiveSet(id, properties::WaveSoundStreaming, streamed);
    ImGui::Dummy(ImVec2(1, 6));
    const char *saves[] = {"External", "Include Original File", "Use Global"};
    int save = std::max(0, std::min(2, static_cast<int>(sound->GetSaveOptions())));
    ImGui::SetNextItemWidth(170);
    if (TypeAheadCombo("Save Options", &save, saves, 3))
        LiveSet(id, properties::SoundSaveOptions, save);
    ImGui::Dummy(ImVec2(1, 6));
    Group("Type");
    if (ImGui::RadioButton("Background", !point) && point)
        LiveSet(id, properties::WaveSoundType, CK_WAVESOUND_BACKGROUND);
    ImGui::SameLine();
    if (ImGui::RadioButton("Point", point) && !point)
        LiveSet(id, properties::WaveSoundType, CK_WAVESOUND_POINT);
    if (point && source) {
        VxVector position, direction;
        float distance = 0.0f;
        sound->GetSound3DInformation(position, direction, distance);
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
    bool loop = sound->GetLoopMode() != 0;
    if (ImGui::Checkbox("Loop", &loop)) LiveSet(id, properties::WaveSoundLoop, loop);
    ImGui::BeginDisabled(!source);
    if (ImGui::Button("Play", ImVec2(60, 0))) GuardedCall([&] { sound->Play(); }, &m_State.document->Host(), "Play");
    ImGui::SameLine();
    // Pause (documentation: "Plays, pauses or stops"); again: on from there.
    if (ImGui::Button(sound->IsPaused() ? "Resume" : "Pause", ImVec2(60, 0)))
        GuardedCall([&] { sound->IsPaused() ? sound->Resume() : sound->Pause(); }, &m_State.document->Host(), "Pause");
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(60, 0))) GuardedCall([&] { sound->Stop(); }, &m_State.document->Host(), "Stop");
    ImGui::EndDisabled();
    if (!source) ImGui::TextDisabled("(no sound device)");
    ImGui::Dummy(ImVec2(1, 6));
    Group("2D");
    float gain = sound->GetGain();
    ImGui::SetNextItemWidth(160);
    if (ImGui::SliderFloat("Gain", &gain, 0.0f, 1.0f, "%.3f"))
        LiveSet(id, properties::WaveSoundGain, gain);
    float pitch = sound->GetPitch();
    ImGui::SetNextItemWidth(160);
    if (ImGui::SliderFloat("Pitch", &pitch, 0.5f, 2.0f, "%.3f"))
        LiveSet(id, properties::WaveSoundPitch, pitch);
    // Pan (documentation: "Background Sounds only"): -1 left to 1 right.
    ImGui::BeginDisabled(point);
    float pan = sound->GetPan();
    ImGui::SetNextItemWidth(160);
    if (ImGui::SliderFloat("Pan", &pan, -1.0f, 1.0f, "%.3f"))
        LiveSet(id, properties::WaveSoundPan, pan);
    ImGui::EndDisabled();
    ImGui::EndGroup();
    // Right: 3D settings (point sounds).
    ImGui::SameLine(640);
    ImGui::BeginGroup();
    Group("3D");
    ImGui::BeginDisabled(!point);
    CK3dEntity *attached = sound->GetAttachedEntity();
    VxVector position, direction;
    sound->GetPosition(position);
    sound->GetDirection(direction);
    CK_ID chosen = 0;
    ImGui::TextUnformatted("Attached Object");
    if (ObjectCombo("##attached", CKCID_3DENTITY, attached, &chosen, 200))
        LiveSet(id, properties::WaveSoundAttached, chosen);
    auto vector3 = [&](const char *label, VxVector value, bool isPosition) {
        float v[3] = {value.x, value.y, value.z};
        ImGui::SetNextItemWidth(240);
        if (ImGui::DragFloat3(label, v, 0.01f, 0.0f, 0.0f, "%.3f")) {
            LiveSet(id, isPosition ? properties::WaveSoundPosition : properties::WaveSoundDirection,
                    VxVector(v[0], v[1], v[2]));
        }
    };
    vector3("Relative Position", position, true);
    vector3("Relative Orientation", direction, false);
    float inside = 0, outside = 0, outsideGain = 0;
    sound->GetCone(&inside, &outside, &outsideGain);
    float cone[3] = {inside, outside, outsideGain};
    bool coneChanged = false;
    ImGui::SetNextItemWidth(120);
    coneChanged |= ImGui::SliderFloat("Cone inside angle", &cone[0], 0.0f, 360.0f, "%.1f");
    ImGui::SetNextItemWidth(120);
    coneChanged |= ImGui::SliderFloat("Cone outside angle", &cone[1], 0.0f, 360.0f, "%.1f");
    ImGui::SetNextItemWidth(120);
    coneChanged |= ImGui::SliderFloat("Outside gain", &cone[2], 0.0f, 1.0f, "%.3f");
    if (coneChanged)
        LiveSet(id, properties::WaveSoundCone, VxVector(cone[0], cone[1], cone[2]));
    float minimum = 0, maximum = 0;
    CKDWORD behavior = 1;
    sound->GetMinMaxDistance(&minimum, &maximum, &behavior);
    float range[2] = {minimum, maximum};
    ImGui::SetNextItemWidth(160);
    if (ImGui::DragFloat2("Min / Max distance", range, 0.1f, 0.0f, 100000.0f, "%.2f"))
        LiveSet(id, properties::WaveSoundDistance, Vx2DVector(range[0], range[1]));
    ImGui::EndDisabled();
    ImGui::EndGroup();
}

// ---------------------------------------------------------------- Array Setup

void SetupWindows::ArrayPage(Setup &setup) {
    if (!m_State.workspace || !FlushPending()) return;
    auto &application = m_State.workspace->Application();
    if (!setup.nameOwner) setup.nameOwner = application.OpenScope();
    application.EnterDraw();
    struct Drawing { EditorApplication &application; ~Drawing() { application.LeaveDraw(); } } drawing{application};
    setup.array.Draw(EditorClient(application.Host(), setup.nameOwner), setup.reference);
}

void SetupWindows::ScenePage(CKObject *object) {
    CKScene *scene = CKScene::Cast(object);
    if (CKLevel *level = CKLevel::Cast(object)) scene = level->GetLevelScene();
    if (!scene) {
        ImGui::TextDisabled("The level has no level scene.");
        return;
    }
    const CK_ID id = scene->GetID();
    const float top = ImGui::GetCursorPosY();
    // Colors: large swatches; a click opens the color picker.
    ImGui::SetCursorPos(ImVec2(8, top));
    ImGui::BeginGroup();
    Group("Colors");
    auto color = [&](const char *label, CKDWORD value, const char *field) {
        const VxColor c(static_cast<CKDWORD>(value));
        const float alpha = c.a;
        ImGui::PushID(label);
        ImGui::SetCursorPosX(38);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(180);
        if (ImGui::ColorButton("##swatch", ImVec4(c.r, c.g, c.b, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(46, 30)))
            ImGui::OpenPopup("picker");
        if (ImGui::BeginPopup("picker")) {
            float rgb[3] = {c.r, c.g, c.b};
            if (ImGui::ColorPicker3("##picker", rgb))
                PreviewColor(id, field, VxColor(rgb[0], rgb[1], rgb[2], alpha));
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::Dummy(ImVec2(1, 4));
    };
    color("Ambient", scene->GetAmbientLight(), "scene.ambient");
    color("Fog", scene->GetFogColor(), "scene.fog");
    color("Background", scene->GetBackgroundColor(), "scene.background");
    ImGui::EndGroup();
    const float lower = ImGui::GetCursorPosY() + 12.0f;
    // Use settings at Scene Start (top right).
    ImGui::SetCursorPos(ImVec2(450, top));
    bool use = scene->EnvironmentSettings() != FALSE;
    if (ImGui::Checkbox("Use settings at Scene Start", &use))
        PreviewInteger(id, "scene.use-environment", use);
    // Fog Settings (bottom left).
    ImGui::SetCursorPos(ImVec2(8, lower));
    ImGui::BeginGroup();
    Group("Fog Settings");
    const char *types[] = {"None", "Exponential", "Exponential Squared", "Linear"};
    int type = std::max(0, std::min(3, static_cast<int>(scene->GetFogMode())));
    ImGui::SetCursorPosX(38);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Type:");
    ImGui::SameLine(180);
    ImGui::SetNextItemWidth(190);
    if (TypeAheadCombo("##fogtype", &type, types, 4))
        PreviewInteger(id, "scene.fog-mode", type);
    if (type != 0) {
        auto number = [&](const char *label, float value, float step, const char *field) {
            ImGui::PushID(label);
            ImGui::SetCursorPosX(38);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(180);
            ImGui::SetNextItemWidth(190);
            if (ImGui::DragFloat("##value", &value, step, 0.0f, 0.0f, "%.3f"))
                PreviewReal(id, field, value);
            ImGui::PopID();
        };
        number("Fog Start:", scene->GetFogStart(), 0.5f, "scene.fog-start");
        number("Fog End:", scene->GetFogEnd(), 0.5f, "scene.fog-end");
        number("Fog Density:", scene->GetFogDensity(), 0.001f, "scene.fog-density");
        if (scene->GetFogStart() > scene->GetFogEnd())
            ImGui::TextColored(ImVec4(0.6f, 0.0f, 0.0f, 1.0f), "Fog Start must be smaller than Fog End");
    }
    ImGui::EndGroup();
    // Other Settings (bottom right).
    ImGui::SetCursorPos(ImVec2(450, lower));
    ImGui::BeginGroup();
    Group("Other Settings");
    CK_ID chosen = 0;
    ImGui::SetCursorPosX(480);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Background Image");
    ImGui::SameLine(210);
    if (ObjectCombo("##background", CKCID_TEXTURE, scene->GetBackgroundTexture(), &chosen, 210))
        PreviewObject(id, "scene.background-image", chosen);
    ImGui::SetCursorPosX(480);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Starting Camera");
    ImGui::SameLine(210);
    if (ObjectCombo("##camera", CKCID_CAMERA, scene->GetStartingCamera(), &chosen, 210))
        PreviewObject(id, "scene.starting-camera", chosen);
    ImGui::EndGroup();
}

// ---------------------------------------------------------------- 3D Frame / 3D Sprite Setup

// Dev 3D Frame Setup (screenshot): Position / Orientation / Scale in World
// and, for an entity with a parent, Local columns (documentation, Place
// Setup screenshot); Show In Player Mode; Frame Representation.
void SetupWindows::FramePage(CK3dEntity *frame) {
    if (!frame) return;
    const CK_ID id = frame->GetID();
    CK3dEntity *parent = frame->GetParent();
    const float top = ImGui::GetCursorPosY();
    float bottom = top;
    const char *axes[] = {"X :", "Y :", "Z :"};
    // One group: title (+ button), World and Local columns of X / Y / Z.
    auto group = [&](float x, const char *title, const char *button, const std::function<void()> &press, float *world,
                     float *local, const char *label, const char *field, float divisor) {
        ImGui::SetCursorPos(ImVec2(x + 8, top));
        Group(title);
        if (button) {
            ImGui::SameLine();
            if (ImGui::SmallButton(button))
                press();
        }
        ImGui::SetCursorPosX(x + 60);
        ImGui::TextUnformatted("World");
        if (parent) {
            ImGui::SameLine(x + 180);
            ImGui::TextUnformatted("Local");
        }
        for (int i = 0; i < 3; ++i) {
            ImGui::PushID(title);
            ImGui::PushID(i);
            ImGui::SetCursorPosX(x + 30);
            ImGui::TextUnformatted(axes[i]);
            ImGui::SameLine(x + 60);
            ImGui::SetNextItemWidth(100);
            if (ImGui::DragFloat("##world", &world[i], 0.01f, 0.0f, 0.0f, "%.4f"))
                PreviewNumbers(id, ("entity3d.world-" + std::string(field)).c_str(), world, 3, divisor);
            if (parent) {
                ImGui::SameLine(x + 175);
                ImGui::SetNextItemWidth(100);
                if (ImGui::DragFloat("##local", &local[i], 0.01f, 0.0f, 0.0f, "%.4f"))
                    PreviewNumbers(id, ("entity3d.local-" + std::string(field)).c_str(), local, 3, divisor);
            }
            ImGui::PopID();
            ImGui::PopID();
        }
        bottom = std::max(bottom, ImGui::GetCursorPosY());
    };
    const ShownTransform &transform = Shown(frame);
    const VxVector wp = Translation(transform.world), lp = Translation(transform.local);
    float pw[3] = {wp.x, wp.y, wp.z}, pl[3] = {lp.x, lp.y, lp.z};
    group(
        0, "Position", "Reset World Matrix",
        [&] { Act("Reset World Matrix", [&] { return ResetWorldMatrix(*m_State.document, id); }); }, pw, pl,
        "Set Position", "position", 1.0f);
    float ow[3] = {0, 0, 0}, ol[3] = {0, 0, 0};
    Vx3DMatrixToEulerAngles(transform.world, &ow[0], &ow[1], &ow[2]);
    Vx3DMatrixToEulerAngles(transform.local, &ol[0], &ol[1], &ol[2]);
    for (int i = 0; i < 3; ++i) {
        ow[i] *= RAD_TO_DEG;
        ol[i] *= RAD_TO_DEG;
    }
    group(300, "Orientation", nullptr, nullptr, ow, ol, "Set Orientation", "orientation", RAD_TO_DEG);
    const VxVector ws = ScaleOf(transform.world), ls = ScaleOf(transform.local);
    float sw[3] = {ws.x, ws.y, ws.z}, sl[3] = {ls.x, ls.y, ls.z};
    CKPlace *place = CKPlace::Cast(frame);
    const bool portal = !place && (frame->GetFlags() & CK_3DENTITY_PORTAL) != 0;
    group(
        600, "Scale", place ? "Set as Unit" : nullptr,
        [&] { Act("Set as Unit", [&] { return SetScaleToUnit(*m_State.document, id); }); }, sw, sl, "Set Scale",
        "scale", 1.0f);
    ImGui::SetCursorPos(ImVec2(0, bottom + 4));
    ImGui::Separator();
    if (place) {
        PlaceLinks(place);
        return;
    }
    ImGui::SetCursorPosX(40);
    ImGui::BeginGroup();
    if (portal) {
        // Portal Setup: Pickable.
        bool pickable = frame->IsPickable() != 0;
        if (ImGui::Checkbox("Pickable", &pickable))
            LiveSet(id, properties::Entity3DPickable, pickable);
        ImGui::SameLine();
        ImGui::TextDisabled("(Hold shift to pick it anyway)");
        ImGui::EndGroup();
        return;
    }
    bool shown = m_State.document->Layout().playerShown.count(id) != 0;
    // Dev.exe sub_4584A0: the 3D Layout's list of the objects shown in Player Mode
    // (not the object's visibility).
    if (ImGui::Checkbox("Show In Player Mode", &shown))
        SetLayoutMembership(*m_State.document, LayoutList::PlayerShown, id, shown, "Show In Player Mode");
    // Dev keeps the representation in the 3D Layout's own table (Dev.exe
    // sub_4C53C0): "No Representation" or one of its "Virtools_Frame_*"
    // meshes, drawn by the 3D Layout only. It is not object state: the
    // table's own undo step.
    ImGui::TextUnformatted("Frame Representation:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(250);
    const std::vector<std::string> &shapes = m_State.layoutFrameShapes;
    auto found = m_State.document->Layout().representations.find(id);
    const int representation = found != m_State.document->Layout().representations.end() ? found->second : 1;
    const bool known = representation >= 0 && representation <= static_cast<int>(shapes.size());
    const std::string current = representation == 0 ? "No Representation" : known ? shapes[representation - 1] : "";
    if (ImGui::BeginCombo("##representation", current.c_str())) {
        TypeAhead typeAhead;
        if (typeAhead.Selectable("No Representation", representation == 0)) SetFrameRepresentation(*m_State.document, {id}, 0);
        for (int i = 0; i < static_cast<int>(shapes.size()); ++i) {
            ImGui::PushID(i);
            if (typeAhead.Selectable(shapes[i].c_str(), representation == i + 1)) SetFrameRepresentation(*m_State.document, {id}, i + 1);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::EndGroup();
}

// Place Setup "Portals and other Places" (documentation; language pack
// [Setups]<Scene>): the places seen from this one and the portal each is
// seen through (None: always visible), with the portal's activation. Each
// change of the links is one undo step (SetPlaceLinks); Active is the
// portal's Activate At Scene Start flag (Level Manager Activate).
void SetupWindows::PlaceLinks(CKPlace *place) {
    const CK_ID id = place->GetID();
    CKContext *context = m_State.document->Context();
    std::vector<CKObject *> places;
    for (CKObject *object : ObjectsOf(*m_State.document, CKCID_PLACE, true))
        if (object != place) places.push_back(object);
    std::vector<CKObject *> portals;
    for (CKObject *object : ObjectsOf(*m_State.document, CKCID_3DENTITY, true))
        if (CK3dEntity::Cast(object)->GetFlags() & CK_3DENTITY_PORTAL) portals.push_back(object);
    CKScene *scene = InitialConditionsScene(context);
    // Links are changed as a whole list (SetPlaceLinks, one undo step).
    auto relink = [&](const char *label, CK_ID fromPlace, CK_ID fromPortal, CK_ID toPlace, CK_ID toPortal) {
        std::vector<PlaceLink> list = PlaceLinksOf(place);
        auto at = std::find_if(list.begin(), list.end(), [&](const PlaceLink &link) {
            return fromPlace && link.place == fromPlace && link.portal == fromPortal;
        });
        if (at != list.end()) {
            if (toPlace) *at = {toPlace, toPortal};
            else list.erase(at);
        } else if (toPlace) {
            list.push_back({toPlace, toPortal});
        }
        Report(SetPlaceLinks(*m_State.document, id, list, label), label);
    };
    struct Link {
        CK_ID place = 0, portal = 0;
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
        for (int i = 0; i < place->GetPortalCount(); ++i) {
            CK3dEntity *portal = nullptr;
            CKPlace *other = place->GetPortal(i, &portal);
            const CK_ID otherId = other ? other->GetID() : 0, portalId = portal ? portal->GetID() : 0;
            links.push_back({otherId, portalId});
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##place", NameOf(other).c_str())) {
                TypeAhead typeAhead;
                for (CKObject *candidate : places)
                    if (typeAhead.Selectable(NameOf(candidate).c_str(), candidate == other) && candidate != other)
                        relink("Visible Place", otherId, portalId, candidate->GetID(), portalId);
                ImGui::EndCombo();
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##portal", NameOf(portal).c_str())) {
                TypeAhead typeAhead;
                if (typeAhead.Selectable("-- None --", !portal) && portal) relink("Through Portal", otherId, portalId, otherId, 0);
                for (CKObject *candidate : portals)
                    if (typeAhead.Selectable(NameOf(candidate).c_str(), candidate == portal) && candidate != portal)
                        relink("Through Portal", otherId, portalId, otherId, candidate->GetID());
                ImGui::EndCombo();
            }
            ImGui::TableNextColumn();
            // Active: the portal's Activate At Scene Start flag (blue / red in Dev's 3D Layout).
            bool active = portal && scene && (scene->GetObjectFlags(portal) & CK_SCENEOBJECT_START_ACTIVATE);
            ImGui::BeginDisabled(!portal || !scene || !scene->IsObjectHere(portal));
            if (ImGui::Checkbox("##active", &active))
                Report(ModifySceneFlags(*m_State.document, {portalId},
                                        active ? CK_SCENEOBJECT_START_ACTIVATE : CK_SCENEOBJECT_START_DEACTIVATE,
                                        active ? CK_SCENEOBJECT_START_DEACTIVATE | CK_SCENEOBJECT_START_LEAVE
                                               : CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_LEAVE,
                                        active ? "Activate" : "Deactivate"),
                       "Portal Active");
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
        CK_ID next = 0;
        for (CKObject *candidate : places) {
            bool linked = false;
            for (const Link &link : links) linked = linked || link.place == candidate->GetID();
            if (!linked) {
                next = candidate->GetID();
                break;
            }
        }
        // Automatic Portal (Dev.exe sub_471C10 203): the next click picks the
        // other place (SetupWindows::PortalPick).
        if (ImGui::MenuItem("Automatic Portal")) m_PortalPlace = m_State.document->Reference(place->GetID());
        if (ImGui::MenuItem("Attach New Place", nullptr, false, next != 0)) relink("Attach New Place", 0, 0, next, 0);
        if (ImGui::MenuItem("Remove Place", nullptr, false, row.place != 0)) relink("Remove Place", row.place, row.portal, 0, 0);
        ImGui::Separator();
        if (ImGui::MenuItem("Place Setup", nullptr, false, row.place != 0))
            m_State.events.Post(ShowObjectSetup{m_State.document->Reference(row.place)});
        if (ImGui::MenuItem("Portal Setup", nullptr, false, row.portal != 0))
            m_State.events.Post(ShowObjectSetup{m_State.document->Reference(row.portal)});
        ImGui::EndPopup();
    }
    PopDevMenuStyle();
}

void SetupWindows::Report(const EditResult &result, const char *what) {
    if (!result.ok && !result.Unchanged() && !result.message.empty()) m_State.Log(LogLevel::Warning, std::string(what) + ": " + AnsiToUtf8(result.message));
}

// Dev 3D Sprite Setup (screenshot; language pack [3D Sprite Setup]):
// Position, Orientation (World), Scale X / Y (the sprite size), Sprite
// Type, Pickable, Offset, UVMapping, Material.
void SetupWindows::Sprite3DPage(CKSprite3D *sprite) {
    if (!sprite) return;
    const CK_ID id = sprite->GetID();
    const float top = ImGui::GetCursorPosY();
    float bottom = top;
    const char *axes[] = {"X :", "Y :", "Z :"};
    auto group = [&](float x, const char *title, const char *button, const std::function<void()> &press, float *v,
                     int count, const char *label, const char *heading, const char *field, float divisor) {
        ImGui::SetCursorPos(ImVec2(x + 8, top));
        Group(title);
        if (button) {
            ImGui::SameLine();
            if (ImGui::SmallButton(button))
                press();
        }
        ImGui::SetCursorPosX(x + 60);
        ImGui::TextUnformatted(heading);
        for (int i = 0; i < count; ++i) {
            ImGui::PushID(title);
            ImGui::PushID(i);
            ImGui::SetCursorPosX(x + 30);
            ImGui::TextUnformatted(axes[i]);
            ImGui::SameLine(x + 75);
            ImGui::SetNextItemWidth(105);
            if (ImGui::DragFloat("##v", &v[i], 0.01f, 0.0f, 0.0f, "%.4f"))
                PreviewNumbers(id, field, v, count, divisor);
            ImGui::PopID();
            ImGui::PopID();
        }
        bottom = std::max(bottom, ImGui::GetCursorPosY());
    };
    const ShownTransform &shown = Shown(sprite);
    const VxVector position = Translation(shown.world);
    float p[3] = {position.x, position.y, position.z};
    group(
        0, "Position", "Reset World Matrix",
        [&] { Act("Reset World Matrix", [&] { return ResetWorldMatrix(*m_State.document, id); }); }, p, 3,
        "Set Position", "World", "entity3d.world-position", 1.0f);
    float e[3] = {0, 0, 0};
    Vx3DMatrixToEulerAngles(shown.world, &e[0], &e[1], &e[2]);
    for (float &angle : e) angle *= RAD_TO_DEG;
    group(300, "Orientation", nullptr, nullptr, e, 3, "Set Orientation", "World", "entity3d.world-orientation",
          RAD_TO_DEG);
    Vx2DVector size;
    sprite->GetSize(size);
    float sz[2] = {size.x, size.y};
    group(600, "Scale", nullptr, nullptr, sz, 2, "Set Size", " ", "sprite3d.size", 1.0f);
    ImGui::SetCursorPos(ImVec2(0, bottom + 4));
    ImGui::Separator();
    const float lower = ImGui::GetCursorPosY();
    // Sprite Type (one of four) and Pickable.
    ImGui::SetCursorPos(ImVec2(8, lower));
    ImGui::BeginGroup();
    Group("Sprite Type");
    const char *types[] = {"Billboard", "X Rotate", "Y Rotate", "Orientable"};
    const int mode = static_cast<int>(sprite->GetMode());
    for (int t = 0; t < 4; ++t) {
        bool on = mode == t;
        ImGui::SetCursorPosX(40);
        if (ImGui::Checkbox(types[t], &on) && on && mode != t)
            LiveSet(id, properties::Sprite3DMode, t);
    }
    bool pickable = sprite->IsPickable() != 0;
    ImGui::SetCursorPosX(40);
    if (ImGui::Checkbox("Pickable", &pickable))
        LiveSet(id, properties::Entity3DPickable, pickable);
    ImGui::SetCursorPosX(68);
    ImGui::TextUnformatted("(Hold shift to pick it anyway)");
    ImGui::EndGroup();
    // Offset and UVMapping (inside the group, SameLine offsets count from its left).
    ImGui::SetCursorPos(ImVec2(308, lower));
    ImGui::BeginGroup();
    Group("Offset");
    Vx2DVector offset;
    sprite->GetOffset(offset);
    float off[2] = {offset.x, offset.y};
    for (int i = 0; i < 2; ++i) {
        ImGui::PushID(i);
        ImGui::SetCursorPosX(338);
        ImGui::TextUnformatted(axes[i]);
        ImGui::SameLine(75);
        ImGui::SetNextItemWidth(90);
        if (ImGui::DragFloat("##offset", &off[i], 0.01f, 0.0f, 0.0f, "%.4f"))
            LiveSet(id, properties::Sprite3DOffset, Vx2DVector(off[0], off[1]));
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(1, 6));
    Group("UVMapping");
    VxRect uv;
    sprite->GetUVMapping(uv);
    float r[4] = {uv.left, uv.top, uv.right, uv.bottom};
    auto uvField = [&](int index) {
        ImGui::PushID(index);
        ImGui::SetNextItemWidth(90);
        if (ImGui::DragFloat("##uv", &r[index], 0.001f, 0.0f, 0.0f, "%.4f"))
            LiveSet(id, properties::Sprite3DUv, VxRect(r[0], r[1], r[2], r[3]));
        ImGui::PopID();
    };
    for (int row = 0; row < 2; ++row) {
        ImGui::SetCursorPosX(338);
        ImGui::TextUnformatted("U :");
        ImGui::SameLine(75);
        uvField(row * 2);
        ImGui::SameLine(192);
        ImGui::TextUnformatted("V :");
        ImGui::SameLine(237);
        uvField(row * 2 + 1);
    }
    ImGui::EndGroup();
    // Material (the button opens its Setup).
    ImGui::SetCursorPos(ImVec2(720, lower));
    ImGui::BeginGroup();
    Group("Material:");
    ImGui::SameLine();
    CKMaterial *material = sprite->GetMaterial();
    ImGui::BeginDisabled(!material);
    if (ImGui::SmallButton(">") && material) m_State.events.Post(ShowObjectSetup{m_State.document->Reference(material->GetID())});
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Material Setup");
    ImGui::SameLine();
    CK_ID chosen = 0;
    if (ObjectCombo("##material", CKCID_MATERIAL, material, &chosen, 190))
        LiveSet(id, properties::Sprite3DMaterial, chosen);
    ImGui::EndGroup();
}

} // namespace vdev
