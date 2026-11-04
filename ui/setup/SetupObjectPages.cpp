// The classes without a Dev Setup (animations, Script objects, parameters, ...) and the Attribute page.

#include "ui/setup/SetupWindows.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include "CKDefines.h"
#include "CKEnums.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/FileDialogs.h"
#include "ui/controls/TypeAhead.h"
#include "core/runtime/TextEncoding.h"
#include "ui/setup/SetupWindowsDetail.h"
#include "features/scene/SceneViewData.h"

namespace vdev {
using namespace setup;

void SetupWindows::AnimationPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "animation", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(
            target,
            {"animation.length", "animation.linked", "animation.frame-rate", "animation.can-be-interrupted",
             "animation.align-orientation", "animation.merge-factor", "animation.transition-preset",
             "animation.transition-from-now", "animation.transition-warp-from-current",
             "animation.transition-to-start", "animation.transition-warp-to-start",
             "animation.transition-warp-to-best", "animation.transition-warp-to-same-position",
             "animation.transition-use-velocity", "animation.transition-loop-if-equal",
             "animation.secondary-preset", "animation.secondary-play", "animation.secondary-warp"},
            propertyData))
        return;
    const auto values = propertyData.Values();
    const bool keyed = data.Boolean("keyed");
    auto flag = [&](const char *property, const char *label) {
        bool on = values.Boolean(property);
        if (ImGui::Checkbox(label, &on))
            PreviewValue(id, property, on);
    };
    {
        SetupColumns columns("##animation-settings", 4);
        if (columns) {
            const auto widthFor = [](const char *label) {
                ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x -
                                                          ImGui::CalcTextSize(label).x -
                                                          ImGui::GetStyle().ItemInnerSpacing.x));
            };
            ImGui::PushTextWrapPos(0);
            columns.Next();
            Group("Animation");
            float length = float(values.Real("animation.length"));
            widthFor("Length (frames)");
            if (ImGui::DragFloat("Length (frames)", &length, 0.5f, 1.0f, 100000.0f, "%.1f"))
                PreviewValue(id, "animation.length", length);
            ImGui::TextWrapped("Current frame:  %.2f  (step %.3f)", data.Real("frame"), data.Real("step"));
            ImGui::Spacing();
            bool linked = values.Boolean("animation.linked");
            float fps = float(values.Real("animation.frame-rate"));
            if (ImGui::Checkbox("Linked to frame rate", &linked))
                PreviewValue(id, "animation.linked", linked);
            ImGui::BeginDisabled(!linked);
            widthFor("Frames per second");
            if (ImGui::DragFloat("Frames per second", &fps, 0.1f, 1.0f, 1000.0f, "%.1f"))
                PreviewValue(id, "animation.frame-rate", fps);
            ImGui::EndDisabled();
            ImGui::Spacing();
            {
                SetupForm form("##animation-character");
                if (form) {
                    form.Row("Character");
                    SetupLink("Character", data.Object("character"), data.Text("character-name"));
                }
            }
            {
                SetupForm form("##animation-root");
                if (form) {
                    form.Row("Root Entity");
                    SetupLink("Root Entity", data.Object("root"), data.Text("root-name"));
                }
            }
            columns.Next();
            Group("Play Options");
            flag("animation.can-be-interrupted", "Can be interrupted");
            flag("animation.align-orientation", "Character takes its orientation");
            ImGui::Spacing();
            Group("Merge");
            if (data.Boolean("merged")) {
                float factor = float(values.Real("animation.merge-factor"));
                widthFor("Merge Factor");
                if (ImGui::SliderFloat("Merge Factor", &factor, 0.0f, 1.0f, "%.3f")) {
                    PreviewValue(id, "animation.merge-factor", factor);
                }
            } else {
                ImGui::TextDisabled("Not a merged animation");
            }
            columns.Next();
            // The transition and secondary modes (CK_ANIMATION_TRANSITION_MODE,
            // CK_SECONDARYANIMATION_FLAGS), used when a behavior asks for the
            // animation's own settings or when the animation is preset.
            Group("Transition Mode");
            flag("animation.transition-preset", "Preset (ignore the modes asked for)");
            const std::pair<const char *, const char *> transitions[] = {
                {"Break (from now)", "animation.transition-from-now"},
                {"Warp from current", "animation.transition-warp-from-current"},
                {"To start", "animation.transition-to-start"},
                {"Warp to start", "animation.transition-warp-to-start"},
                {"Warp to best", "animation.transition-warp-to-best"},
                {"Warp to same position", "animation.transition-warp-to-same-position"},
                {"Use velocity", "animation.transition-use-velocity"},
                {"Loop if equal", "animation.transition-loop-if-equal"}};
            for (int i = 0; i < 8; ++i) {
                const char *property = transitions[i].second;
                bool on = values.Boolean(property);
                if (ImGui::Checkbox(transitions[i].first, &on))
                    PreviewValue(id, property, on);
            }
            ImGui::Spacing();
            columns.Next();
            Group("Secondary Animation");
            flag("animation.secondary-preset", "Preset##secondary");
            const CKDWORD secondary = uint32_t(data.Integer("secondary"));
            const CKDWORD playMask = CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_LOOP |
                                     CKSECONDARYANIMATION_LASTFRAME | CKSECONDARYANIMATION_LOOPNTIMES;
            const std::pair<const char *, CKDWORD> plays[] = {
                {"One shot", CKSECONDARYANIMATION_ONESHOT},
                {"Loop", CKSECONDARYANIMATION_LOOP},
                {"Stay on last frame", CKSECONDARYANIMATION_LASTFRAME},
                {"Loop N times", CKSECONDARYANIMATION_LOOPNTIMES}};
            for (int i = 0; i < 4; ++i) {
                const CKDWORD bit = plays[i].second;
                if (ImGui::RadioButton(plays[i].first, (secondary & playMask) == bit) &&
                    (secondary & playMask) != bit)
                    PreviewValue(id, "animation.secondary-play", static_cast<int>(bit));
            }
            bool warp = (secondary & CKSECONDARYANIMATION_DOWARP) != 0;
            if (ImGui::Checkbox("Warp when starting or stopping", &warp))
                PreviewValue(id, "animation.secondary-warp", warp);
            ImGui::PopTextWrapPos();
        }
    }
    if (!keyed)
        return;
    // The object animations of a keyed animation: double-click opens the
    // Object Animation Setup, the entity column the entity's Setup.
    ImGui::Separator();
    Group("Object Animations");
    const float unit = PageUnit();
    if (ImGui::BeginTable("objectanimations", 8,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0, std::max(120 * unit, ImGui::GetContentRegionAvail().y - 4 * unit)),
                          std::max(1000 * unit, ImGui::GetContentRegionAvail().x))) {
        TypeAhead typeAhead;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Entity");
        ImGui::TableSetupColumn("Animation");
        ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 60 * unit);
        ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthFixed, 90 * unit);
        ImGui::TableSetupColumn("Rotation", ImGuiTableColumnFlags_WidthFixed, 90 * unit);
        ImGui::TableSetupColumn("Scale", ImGuiTableColumnFlags_WidthFixed, 90 * unit);
        ImGui::TableSetupColumn("Off-Axis Scale", ImGuiTableColumnFlags_WidthFixed, 100 * unit);
        ImGui::TableSetupColumn("Morph", ImGuiTableColumnFlags_WidthFixed, 90 * unit);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < data.Rows("animations").size(); ++i) {
            const SetupFields part(data.Rows("animations")[i].fields);
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const auto entity = part.Object("entity");
            if (ImGui::SmallButton(">") && entity)
                Open(entity);
            ImGui::SameLine();
            ImGui::TextUnformatted(part.Text("entity-name").c_str());
            ImGui::TableNextColumn();
            if (typeAhead.Selectable(part.Text("name").c_str(), false,
                                     ImGuiSelectableFlags_SpanAllColumns |
                                         ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                Open(part.Object("target"));
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", part.Real("length"));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(part.Text("position-keys").c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(part.Text("rotation-keys").c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(part.Text("scale-keys").c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(part.Text("axis-keys").c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(part.Text("morph-keys").c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void SetupWindows::ObjectAnimationPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "object-animation", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target,
                        {"objectanimation.length", "objectanimation.merge-factor",
                         "objectanimation.ignore-position", "objectanimation.ignore-rotation",
                         "objectanimation.ignore-scale", "objectanimation.ignore-off-axis-scale",
                         "objectanimation.ignore-morph"},
                        propertyData))
        return;
    const auto values = propertyData.Values();
    {
        SetupColumns columns("##objectanimation-settings", 3);
        if (columns) {
            const auto widthFor = [](const char *label) {
                ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x -
                                                          ImGui::CalcTextSize(label).x -
                                                          ImGui::GetStyle().ItemInnerSpacing.x));
            };
            ImGui::PushTextWrapPos(0);
            columns.Next();
            Group("Object Animation");
            {
                SetupForm form("##objectanimation-entity");
                if (form) {
                    form.Row("Entity");
                    SetupLink("Entity", data.Object("entity"), data.Text("entity-name"));
                }
            }
            float length = float(values.Real("objectanimation.length"));
            widthFor("Length (frames)");
            if (ImGui::DragFloat("Length (frames)", &length, 0.5f, 1.0f, 100000.0f, "%.1f"))
                PreviewValue(id, "objectanimation.length", length);
            ImGui::TextWrapped("Current step:  %.3f", data.Real("step"));
            ImGui::Spacing();
            {
                SetupForm form("##objectanimation-shared");
                if (form) {
                    form.Row("Shares data of");
                    SetupLink("Shares data of", data.Object("shared"), data.Text("shared-name"));
                }
            }
            if (data.Boolean("merged")) {
                float factor = float(values.Real("objectanimation.merge-factor"));
                widthFor("Merge Factor");
                if (ImGui::SliderFloat("Merge Factor", &factor, 0.0f, 1.0f, "%.3f"))
                    PreviewValue(id, "objectanimation.merge-factor", factor);
            }
            columns.Next();
            // CK_OBJECTANIMATION_FLAGS.
            Group("Ignore");
            const std::pair<const char *, const char *> ignored[] = {
                {"Position", "objectanimation.ignore-position"},
                {"Rotation", "objectanimation.ignore-rotation"},
                {"Scale", "objectanimation.ignore-scale"},
                {"Off-Axis Scale", "objectanimation.ignore-off-axis-scale"},
                {"Morph", "objectanimation.ignore-morph"}};
            for (const auto &entry : ignored) {
                bool on = values.Boolean(entry.second);
                if (ImGui::Checkbox(entry.first, &on))
                    PreviewValue(id, entry.second, on);
            }
            columns.Next();
            Group("Keys");
            {
                SetupForm form("##objectanimation-keys");
                if (form) {
                    const char *labels[] = {"Position", "Rotation", "Scale", "Off-Axis Scale", "Morph"};
                    const char *fields[] = {"position-keys", "rotation-keys", "scale-keys", "axis-keys",
                                            "morph-keys"};
                    for (int i = 0; i < 5; ++i) {
                        form.Row(labels[i]);
                        ImGui::TextWrapped("%s", data.Text(fields[i]).c_str());
                    }
                }
            }
            if (data.Boolean("morph"))
                ImGui::TextWrapped("Morph vertices:  %d%s", int(data.Integer("morph-count")),
                                   data.Boolean("morph-normals") ? "  (with normals)" : "");
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::Separator();
    Group("Used By");
    UsedByList("Used By", data.Rows("users"), {0, 150 * PageUnit()});
}

void SetupWindows::MidiSoundPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "midi", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target, {"sound.save-options"}, propertyData))
        return;
    const auto values = propertyData.Values();
    SetupColumns columns("##midi-settings", 2);
    if (!columns)
        return;
    columns.Next();
    Group("Midi File");
    const auto file = data.Text("file");
    ImGui::TextWrapped("%s", !file.empty() ? file.c_str() : "-- None --");
    if (ImGui::Button("Change File...")) {
        FileDialogOptions options;
        options.filter = L"Midi files (*.mid;*.midi;*.rmi)\0*.mid;*.midi;*.rmi\0All files (*.*)\0*.*\0";
        FlushPending();
        ChooseFile(options, [this, target, owner = m_CurrentOwner](const FileDialogResult &result) {
            if (result.status != FileDialogResult::Status::Selected)
                return;
            const std::string chosen = result.paths.front();
            SetProperty(target, owner, "sound.file", EditorField{"value", EDITOR_TEXT_VALUE, {}, chosen});
        });
    }
    ImGui::Spacing();
    const char *saves[] = {"External", "Include Original File", "Use Global"};
    int save = std::max(0, std::min(2, static_cast<int>(values.Integer("sound.save-options"))));
    ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x -
                                              ImGui::CalcTextSize("Save Options").x -
                                              ImGui::GetStyle().ItemInnerSpacing.x));
    if (TypeAheadCombo("Save Options", &save, saves, 3))
        PreviewValue(id, "sound.save-options", save);
    columns.Next();
    Group("Playback");
    // The Midi Manager plays the file; without it (or a file) there is nothing to play.
    const bool playable = data.Boolean("playable");
    ImGui::BeginDisabled(!playable);
    if (ImGui::Button(data.Boolean("playing") && !data.Boolean("paused") ? "Pause" : "Play"))
        RunSetupCommand("setup.sound-play", {ObjectArgument("target", target)});
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x <
        ImGui::CalcTextSize("Stop").x + 2 * ImGui::GetStyle().FramePadding.x)
        ImGui::NewLine();
    if (ImGui::Button("Stop"))
        RunSetupCommand("setup.sound-stop", {ObjectArgument("target", target)});
    ImGui::EndDisabled();
    if (!playable) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled(!file.empty() ? "(no Midi Manager)" : "(no file)");
        ImGui::PopTextWrapPos();
    }
    SetupForm form("##midi-state");
    if (form) {
        form.Row("State");
        ImGui::TextUnformatted(data.Boolean("paused")    ? "Paused"
                               : data.Boolean("playing") ? "Playing"
                                                         : "Stopped");
        form.Row("Position");
        ImGui::Text("%u", static_cast<unsigned>(data.Integer("position")));
    }
}

void SetupWindows::SpriteTextPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "sprite-text", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target,
                        {"spritetext.text", "spritetext.color", "spritetext.background-color", "spritetext.alignment",
                         "spritetext.font"},
                        propertyData))
        return;
    const auto values = propertyData.Values();
    const auto fontValues = values.Record("spritetext.font");
    SetupFont font{fontValues.Text("face"), int(fontValues.Integer("size")), int(fontValues.Integer("weight")),
                   fontValues.Boolean("italic"), fontValues.Boolean("underline")};
    const bool hasFont = data.Boolean("has-font");
    {
        SetupColumns columns("##spritetext-settings", 3);
        if (columns) {
            const auto widthFor = [](const char *label) {
                ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(label).x -
                                                          ImGui::GetStyle().ItemInnerSpacing.x));
            };
            const auto nextChoice = [](const char *label) {
                ImGui::SameLine();
                if (ImGui::GetContentRegionAvail().x < ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                                                           ImGui::CalcTextSize(label, nullptr, true).x)
                    ImGui::NewLine();
            };
            auto typed = [&](const char *label, const std::string &value, bool multiline, bool fontFace) {
                std::string shown = value;
                widthFor(label);
                const bool changed =
                    multiline ? ImGui::InputTextMultiline(
                                    label, &shown, {std::max(1.f, ImGui::GetContentRegionAvail().x), 70 * PageUnit()})
                              : ImGui::InputText(label, &shown);
                if (changed) {
                    if (fontFace) {
                        auto changedFont = font;
                        changedFont.face = shown;
                        PreviewFont(id, changedFont);
                    } else
                        PreviewValue(id, "spritetext.text", shown);
                }
            };
            columns.Next();
            Group("Text");
            typed("##text", values.Text("spritetext.text"), true, false);
            columns.Next();
            Group("Font");
            auto setFont = [&](const SetupFont &changed) { PreviewFont(id, changed); };
            if (!hasFont)
                ImGui::TextDisabled("(no font yet)");
            typed("Face", font.face, false, true);
            SetupFont edited = font;
            widthFor("Size");
            if (ImGui::DragInt("Size", &edited.size, 0.2f, 1, 512))
                setFont(edited);
            edited = font;
            bool bold = font.weight >= 600;
            if (ImGui::Checkbox("Bold", &bold)) {
                edited.weight = bold ? 700 : 400;
                setFont(edited);
            }
            nextChoice("Italic");
            edited = font;
            if (ImGui::Checkbox("Italic", &edited.italic))
                setFont(edited);
            nextChoice("Underline");
            edited = font;
            if (ImGui::Checkbox("Underline", &edited.underline))
                setFont(edited);
            columns.Next();
            Group("Colors");
            auto color = [&](const char *property, const char *label) {
                const auto &value = values.Reals(property);
                float c[4] = {0, 0, 0, 1};
                if (value.size() == 4)
                    for (int i = 0; i < 4; ++i)
                        c[i] = float(value[i]);
                if (ImGui::ColorEdit4(label, c, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf))
                    PreviewValue(id, property, VxColor(c[0], c[1], c[2], c[3]));
            };
            color("spritetext.color", "Text Color");
            color("spritetext.background-color", "Background Color");
            ImGui::Spacing();
            // CKSPRITETEXT_CENTER centers both the horizontal and vertical choices.
            Group("Alignment");
            CKDWORD align = static_cast<CKDWORD>(values.Integer("spritetext.alignment"));
            if (align & CKSPRITETEXT_CENTER)
                align = CKSPRITETEXT_HCENTER | CKSPRITETEXT_VCENTER;
            const CKDWORD horizontal = CKSPRITETEXT_LEFT | CKSPRITETEXT_HCENTER | CKSPRITETEXT_RIGHT;
            const CKDWORD vertical = CKSPRITETEXT_TOP | CKSPRITETEXT_VCENTER | CKSPRITETEXT_BOTTOM;
            auto choice = [&](const char *label, CKDWORD bit, CKDWORD group) {
                if (ImGui::RadioButton(label, (align & bit) != 0) && !(align & bit))
                    PreviewValue(id, "spritetext.alignment", static_cast<int>((align & ~group) | bit));
            };
            choice("Left", CKSPRITETEXT_LEFT, horizontal);
            nextChoice("Center##h");
            choice("Center##h", CKSPRITETEXT_HCENTER, horizontal);
            nextChoice("Right");
            choice("Right", CKSPRITETEXT_RIGHT, horizontal);
            choice("Top", CKSPRITETEXT_TOP, vertical);
            nextChoice("Center##v");
            choice("Center##v", CKSPRITETEXT_VCENTER, vertical);
            nextChoice("Bottom");
            choice("Bottom", CKSPRITETEXT_BOTTOM, vertical);
        }
    }
    ImGui::Separator();
    Entity2DPage(target, true);
}

void SetupWindows::KinematicChainPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "kinematic", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target, {"kinematic.start", "kinematic.end"}, propertyData))
        return;
    const bool complete = data.Object("start") && data.Object("end");
    const int bodies = complete ? int(data.Integer("body-count")) : 0;
    const float unit = PageUnit();
    {
        SetupColumns columns("##kinematic-settings", 2);
        if (columns) {
            columns.Next();
            Group("Kinematic Chain");
            {
                SetupForm form("##kinematic-effectors");
                if (form) {
                    const auto effector = [&](const char *label, const char *combo, const char *button,
                                              const char *property, const char *field, const char *name) {
                        form.Row(label);
                        ObjectRef chosen{};
                        const float buttonWidth = ImGui::CalcTextSize(">").x + 2 * ImGui::GetStyle().FramePadding.x;
                        const float width = std::max(1.f, ImGui::GetContentRegionAvail().x - buttonWidth -
                                                              ImGui::GetStyle().ItemSpacing.x);
                        if (ObjectChoice(combo, CKCID_BODYPART, data.Object(field), data.Text(name), &chosen, width)) {
                            PreviewValue(id, property, chosen);
                            FlushPending();
                        }
                        ImGui::SameLine();
                        ImGui::BeginDisabled(!data.Object(field));
                        if (ImGui::SmallButton(button) && data.Object(field))
                            Open(data.Object(field));
                        ImGui::EndDisabled();
                    };
                    effector("Start Effector", "##start", ">##startsetup", "kinematic.start", "start", "start-name");
                    effector("End Effector", "##end", ">##endsetup", "kinematic.end", "end", "end-name");
                }
            }
            ImGui::Spacing();
            {
                SetupForm form("##kinematic-values");
                if (form) {
                    form.Row("Chain Length");
                    ImGui::TextUnformatted(complete ? std::to_string(data.Real("length")).c_str() : "-");
                    form.Row("Body Parts");
                    ImGui::Text("%d", bodies);
                }
            }
            if (complete && bodies <= 0) {
                ImGui::PushTextWrapPos(0);
                ImGui::TextDisabled("The end effector is not below the start effector.");
                ImGui::PopTextWrapPos();
            }
            columns.Next();
            Group("Chain");
            if (ImGui::BeginTable("chain", 2,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_ScrollX,
                                  {0, 150 * unit}, std::max(300 * unit, ImGui::GetContentRegionAvail().x))) {
                TypeAhead typeAhead;
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 26 * unit);
                ImGui::TableSetupColumn("Body Part");
                ImGui::TableHeadersRow();
                for (int i = 0; i < bodies; ++i) {
                    const SetupFields part(data.Rows("bodies")[size_t(i)].fields);
                    const auto body = part.Object("target");
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", i);
                    ImGui::TableNextColumn();
                    if (typeAhead.Selectable(part.Text("name").c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                        body && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        Open(body);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::Separator();
    ParameterUsers(target);
}

void SetupWindows::LayerPage(ObjectRef target) {
    SetupDetails description;
    if (!ReadDetails(target, "layer", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target, {"layer.visible"}, propertyData))
        return;
    const auto values = propertyData.Values();
    const auto grid = data.Object("grid");
    const int width = int(data.Integer("width")), length = int(data.Integer("length"));
    const auto &squares = data.Integers("squares");
    if (width < 0 || length < 0 || uint64_t(width) * uint64_t(length) != squares.size())
        return;
    const auto &channels = data.Reals("color");
    if (channels.size() != 4)
        return;
    const VxColor color{float(channels[0]), float(channels[1]), float(channels[2]), float(channels[3])};
    const auto square = [&](int x, int y) { return int(int64_t(squares[size_t(y) * size_t(width) + size_t(x)])); };
    const float unit = PageUnit();
    SetupColumns columns("##layer-settings", 2);
    if (!columns)
        return;
    columns.Next();
    Group("Layer");
    {
        SetupForm form("##layer-properties");
        if (form) {
            form.Row("Grid");
            ImGui::BeginDisabled(!grid);
            if (ImGui::SmallButton(">") && grid) {
                Open(grid);
                m_GridLayer = target;
            }
            ImGui::EndDisabled();
            if (grid && ImGui::IsItemHovered())
                ImGui::SetTooltip("Grid Setup");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", data.Text("grid-name").c_str());
            form.Row("Type");
            const ImVec2 swatch = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled({swatch.x, swatch.y + unit},
                                                      {swatch.x + 20 * unit, swatch.y + 14 * unit}, ToImColor(color));
            ImGui::Dummy({24 * unit, 15 * unit});
            ImGui::SameLine();
            ImGui::TextWrapped("%s  (%d)", data.Text("type-name").c_str(), int(data.Integer("type")));
            form.Row("Format");
            ImGui::TextUnformatted(int(data.Integer("format")) == CKGRID_LAYER_FORMAT_NORMAL
                                       ? "Normal"
                                       : std::to_string(int(data.Integer("format"))).c_str());
        }
    }
    bool visible = values.Boolean("layer.visible");
    if (ImGui::Checkbox("Visible", &visible))
        PreviewValue(target, "layer.visible", visible);
    int filled = 0, low = 255, high = 0;
    for (int y = 0; y < length; ++y)
        for (int x = 0; x < width; ++x) {
            const int value = square(x, y);
            if (value > 0)
                ++filled;
            low = std::min(low, value);
            high = std::max(high, value);
        }
    ImGui::Spacing();
    Group("Squares");
    {
        SetupForm form("##layer-values");
        if (form) {
            form.Row("Width / Length");
            ImGui::Text("%d / %d", width, length);
            form.Row("With a value");
            ImGui::Text("%d of %d", filled, width * length);
            if (!squares.empty()) {
                form.Row("Values");
                ImGui::Text("%d to %d", low, high);
            }
        }
    }
    columns.Next();
    // Read-only, with row zero at the bottom as in the Grid Setup.
    const float margin = 8 * unit;
    const float side = std::max(1.f, std::min(256 * unit, ImGui::GetContentRegionAvail().x - 2 * margin));
    const float cell = width > 0 && length > 0 ? std::max(unit, std::min(side / width, side / length)) : unit;
    const auto contentId = ImGui::GetID("");
    if (ImGui::BeginChild("##layer-canvas", {0, side + 2 * margin}, ImGuiChildFlags_None,
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
                  firstRow = int(std::clamp(std::floor((double(clip.Min.y) - origin.y) / cell), 0., double(length))),
                  lastRow = int(std::clamp(std::ceil((double(clip.Max.y) - origin.y) / cell), 0., double(length)));
        for (int row = firstRow; row < lastRow; ++row)
            for (int x = firstX; x < lastX; ++x) {
                const int y = length - 1 - row;
                const float k = std::clamp(square(x, y), 0, 255) / 255.f;
                const ImVec2 a{origin.x + x * cell, origin.y + row * cell};
                dl->AddRectFilled(
                    a, {a.x + cell, a.y + cell},
                    IM_COL32(int(color.r * 255 * k), int(color.g * 255 * k), int(color.b * 255 * k), 255));
            }
        ImGui::SetCursorScreenPos(origin);
        ImGui::InvisibleButton("##squares", {std::max(1.f, width * cell), std::max(1.f, length * cell)});
        if (ImGui::IsItemHovered()) {
            const auto mouse = ImGui::GetIO().MousePos;
            const int x = int((mouse.x - origin.x) / cell), y = length - 1 - int((mouse.y - origin.y) / cell);
            if (x >= 0 && x < width && y >= 0 && y < length)
                ImGui::SetTooltip("X : %d    Y : %d    Value : %d", x, y, square(x, y));
        }
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy(extent);
        ImGui::PopID();
    }
    ImGui::EndChild();
}
void SetupWindows::ParameterPage(ObjectRef target) {
    SetupDetails description;
    if (!ReadDetails(target, "parameter", description))
        return;
    const auto data = description.Values();
    const bool output = data.Boolean("output");
    SetupColumns columns("##parameter-settings", output ? 2 : 1);
    if (!columns)
        return;
    columns.Next();
    Group(data.Text("heading").c_str());
    {
        SetupForm form("##parameter-info");
        if (form) {
            ImGui::PushTextWrapPos(0);
            form.Row("Type");
            ImGui::TextUnformatted(data.Text("type-name").c_str());
            form.Row("Owner");
            SetupLink("Owner", data.Object("owner"), data.Text("owner-name"));
            if (data.Object("behavior") && data.Object("behavior") != data.Object("owner")) {
                form.Row("Behavior");
                SetupLink("Behavior", data.Object("behavior"), data.Text("behavior-name"));
            }
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::Spacing();
    // A This parameter still shows its owner and cannot be edited.
    ParameterValue(data, data.Boolean("editable"));
    ImGui::Spacing();
    ShowInSchematic(data);
    if (!output)
        return;
    columns.Next();
    Group("Destinations");
    const float unit = PageUnit();
    if (ImGui::BeginTable("destinations", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_ScrollX,
                          {0, 180 * unit}, std::max(380 * unit, ImGui::GetContentRegionAvail().x))) {
        TypeAhead typeAhead;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, 170 * unit);
        ImGui::TableSetupColumn("Owner");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < data.Rows("destinations").size(); ++i) {
            const SetupFields destination(data.Rows("destinations")[i].fields);
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (typeAhead.Selectable(destination.Text("name").c_str(), false,
                                     ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                Open(destination.Object("target"));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(destination.Text("owner-name").c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void SetupWindows::ParameterInPage(ObjectRef target) {
    SetupDetails description;
    if (!ReadDetails(target, "parameter-input", description))
        return;
    const auto data = description.Values();
    SetupColumns columns("##parameterinput-settings", 3);
    if (!columns)
        return;
    columns.Next();
    Group(data.Boolean("enabled") ? "Input Parameter" : "Input Parameter (disabled)");
    {
        SetupForm form("##parameterinput-info");
        if (form) {
            ImGui::PushTextWrapPos(0);
            form.Row("Type");
            ImGui::TextUnformatted(data.Text("type-name").c_str());
            form.Row("Owner");
            SetupLink("Owner", data.Object("owner"), data.Text("owner-name"));
            if (data.Object("behavior") && data.Object("behavior") != data.Object("owner")) {
                form.Row("Behavior");
                SetupLink("Behavior", data.Object("behavior"), data.Text("behavior-name"));
            }
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::Spacing();
    ShowInSchematic(data);
    columns.Next();
    Group("Source");
    {
        SetupForm form("##parameterinput-source");
        if (form) {
            ImGui::PushTextWrapPos(0);
            form.Row("Linked to");
            SetupLink("Linked to", data.Object("direct"), data.Text("direct-name"));
            form.Row("Shares with");
            SetupLink("Shares with", data.Object("shared"), data.Text("shared-name"));
            if (data.Object("value") && data.Object("value") != data.Object("direct")) {
                form.Row("Real source");
                SetupLink("Real source", data.Object("value"), data.Text("value-name"));
            }
            ImGui::PopTextWrapPos();
        }
    }
    columns.Next();
    // Edit only an input's own local value, as before.
    ParameterValue(data, data.Boolean("editable"));
}

void SetupWindows::OperationPage(ObjectRef target) {
    SetupDetails description;
    if (!ReadDetails(target, "operation", description))
        return;
    const auto data = description.Values();
    SetupColumns columns("##operation-settings", 2);
    if (!columns)
        return;
    columns.Next();
    Group("Parameter Operation");
    {
        SetupForm form("##operation-info");
        if (form) {
            ImGui::PushTextWrapPos(0);
            form.Row("Operation");
            ImGui::TextUnformatted(data.Text("operation").c_str());
            form.Row("GUID");
            ImGui::TextUnformatted(data.Text("guid").c_str());
            form.Row("Owner");
            SetupLink("Owner", data.Object("owner"), data.Text("owner-name"));
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::Spacing();
    ShowInSchematic(data);
    columns.Next();
    Group("Parameters");
    const float unit = PageUnit();
    if (ImGui::BeginTable("operands", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX |
                              ImGuiTableFlags_ScrollY,
                          {0, 150 * unit}, std::max(560 * unit, ImGui::GetContentRegionAvail().x))) {
        TypeAhead typeAhead(TypeAhead::Shared);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60 * unit);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 140 * unit);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120 * unit);
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();
        for (const auto &value : data.Rows("operands")) {
            const SetupFields row(value.fields);
            ImGui::PushID(value.key.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.Text("label").c_str());
            ImGui::TableNextColumn();
            if (typeAhead.Selectable(row.Text("name").c_str(), false,
                                     ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
                row.Object("target") && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                Open(row.Object("target"));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.Text("type-name").c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.Text("value-text").c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Double-click a parameter to show its Setup.");
    ImGui::PopTextWrapPos();
}
void SetupWindows::BehaviorPage(ObjectRef target) {
    SetupDetails description;
    if (!ReadDetails(target, "behavior", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target, {"behavior.priority"}, propertyData))
        return;
    const auto values = propertyData.Values();
    const auto script = data.Object("script");
    const bool graph = data.Boolean("graph");
    SetupColumns columns("##behavior-settings", 3);
    if (!columns)
        return;
    columns.Next();
    Group(data.Text("heading").c_str());
    {
        SetupForm form("##behavior-info");
        if (form) {
            ImGui::PushTextWrapPos(0);
            if (data.Boolean("prototype")) {
                form.Row("Prototype");
                ImGui::TextUnformatted(data.Text("prototype-name").c_str());
                form.Row("GUID");
                ImGui::TextUnformatted(data.Text("guid").c_str());
                form.Row("Version");
                ImGui::Text("0x%08x", static_cast<unsigned>(data.Integer("version")));
            }
            form.Row("Owner");
            SetupLink("Owner", data.Object("owner"), data.Text("owner-name"));
            if (target != script) {
                form.Row("Script");
                SetupLink("Script", script, data.Text("script-name"));
                if (data.Object("parent") != script) {
                    form.Row("Parent");
                    SetupLink("Parent", data.Object("parent"), data.Text("parent-name"));
                }
            }
            // Priority keeps the existing Enter-to-submit edit in the Script.
            form.Row("Priority");
            int priority = int(values.Integer("behavior.priority"));
            const bool editable = data.Boolean("schematic-enabled");
            ImGui::BeginDisabled(!editable);
            if (ImGui::InputInt("##priority", &priority, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue) && editable)
                PreviewValue(target, "behavior.priority", priority);
            ImGui::EndDisabled();
            form.Row("Active");
            ImGui::TextUnformatted(data.Boolean("active") ? "Yes" : "No");
            if (data.Boolean("using-target")) {
                form.Row("Target");
                SetupLink("Target", data.Object("target-parameter"), data.Text("target-parameter-name"));
            }
            form.Row("Applies to");
            ImGui::TextUnformatted(data.Text("compatible").c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::Spacing();
    ShowInSchematic(data);
    // CK_BEHAVIOR_FLAGS remain read-only, set by CK and the prototype.
    columns.Next();
    Group("Flags");
    static const std::pair<CKDWORD, const char *> FLAGS[] = {
        {CKBEHAVIOR_ACTIVE, "Active"},
        {CKBEHAVIOR_SCRIPT, "Script"},
        {CKBEHAVIOR_BUILDINGBLOCK, "Building Block"},
        {CKBEHAVIOR_USEFUNCTION, "Uses a Function"},
        {CKBEHAVIOR_TOPMOST, "Topmost"},
        {CKBEHAVIOR_TARGETABLE, "Targetable"},
        {CKBEHAVIOR_MESSAGESENDER, "Message Sender"},
        {CKBEHAVIOR_MESSAGERECEIVER, "Message Receiver"},
        {CKBEHAVIOR_WAITSFORMESSAGE, "Waits for a Message"},
        {CKBEHAVIOR_VARIABLEINPUTS, "Variable Inputs"},
        {CKBEHAVIOR_VARIABLEOUTPUTS, "Variable Outputs"},
        {CKBEHAVIOR_VARIABLEPARAMETERINPUTS, "Variable Input Parameters"},
        {CKBEHAVIOR_VARIABLEPARAMETEROUTPUTS, "Variable Output Parameters"},
        {CKBEHAVIOR_CUSTOMEDITDIALOG, "Custom Edit Dialog"},
        {CKBEHAVIOR_CUSTOMSETTINGSEDITDIALOG, "Custom Settings Dialog"},
        {CKBEHAVIOR_LOCKED, "Locked"},
        {CKBEHAVIOR_LAUNCHEDONCE, "Launched Once"},
    };
    const CKDWORD flags = uint32_t(data.Integer("flags"));
    ImGui::BeginDisabled();
    for (const auto &flag : FLAGS) {
        bool on = (flags & flag.first) != 0;
        ImGui::Checkbox(flag.second, &on);
    }
    ImGui::EndDisabled();
    columns.Next();
    Group("Contents");
    {
        SetupForm form("##behavior-contents");
        if (form) {
            form.Row("Inputs");
            ImGui::Text("%d", int(data.Integer("inputs")));
            form.Row("Outputs");
            ImGui::Text("%d", int(data.Integer("outputs")));
            form.Row("Input Parameters");
            ImGui::Text("%d", int(data.Integer("input-parameters")));
            form.Row("Output Parameters");
            ImGui::Text("%d", int(data.Integer("output-parameters")));
            form.Row("Local Parameters");
            ImGui::Text("%d", int(data.Integer("local-parameters")));
            if (graph) {
                form.Row("Behaviors");
                ImGui::Text("%d", int(data.Rows("behaviors").size()));
                form.Row("Behavior Links");
                ImGui::Text("%d", int(data.Integer("links")));
                form.Row("Parameter Operations");
                ImGui::Text("%d", int(data.Integer("operations")));
            }
        }
    }
    // A graph's behaviors; a double click shows one's Setup.
    if (graph) {
        ImGui::Spacing();
        Group("Behaviors");
        const float unit = PageUnit();
        if (ImGui::BeginTable("behaviors", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_ScrollX,
                              {0, 260 * unit}, std::max(520 * unit, ImGui::GetContentRegionAvail().x))) {
            TypeAhead typeAhead;
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 260 * unit);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 130 * unit);
            ImGui::TableSetupColumn("Priority");
            ImGui::TableHeadersRow();
            for (int i = 0; i < int(data.Rows("behaviors").size()); ++i) {
                const SetupFields child(data.Rows("behaviors")[size_t(i)].fields);
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (typeAhead.Selectable(child.Text("name").c_str(), false,
                                         ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    Open(child.Object("target"));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(child.Boolean("base") ? "Building Block" : "Behavior Graph");
                ImGui::TableNextColumn();
                ImGui::Text("%d", int(child.Integer("priority")));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
}

void SetupWindows::BehaviorIOPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "behavior-io", description))
        return;
    const auto data = description.Values();
    const bool input = data.Boolean("input");
    ImGui::BeginGroup();
    Group(input ? "Behavior Input" : "Behavior Output");
    ImGui::TextUnformatted("Owner");
    ImGui::SameLine(110);
    SetupLink("Owner", data.Object("owner"), data.Text("owner-name"));
    ImGui::TextUnformatted("Active");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(data.Boolean("active") ? "Yes" : "No");
    ImGui::Dummy(ImVec2(1, 6));
    ShowInSchematic(data);
    ImGui::EndGroup();
    // The links are those of the owner's graph and, inside a graph, its own.
    ImGui::SameLine(330);
    ImGui::BeginGroup();
    Group("Links");
    if (ImGui::BeginTable("links", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(560, 180))) {
        TypeAhead typeAhead;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed, 230);
        ImGui::TableSetupColumn("To", ImGuiTableColumnFlags_WidthFixed, 230);
        ImGui::TableSetupColumn("Delay");
        ImGui::TableHeadersRow();
        for (const auto &value : data.Rows("links")) {
            const SetupFields link(value.fields);
            ImGui::PushID(int(link.Object("target").object));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (typeAhead.Selectable(link.Text("from").c_str(), false,
                                     ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                Open(link.Object("target"));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(link.Text("to").c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", int(link.Integer("delay")));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndGroup();
}

void SetupWindows::BehaviorLinkPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "behavior-link", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target, {"behavior-link.delay"}, propertyData))
        return;
    const auto values = propertyData.Values();
    ImGui::BeginGroup();
    Group("Behavior Link");
    ImGui::TextUnformatted("From");
    ImGui::SameLine(110);
    SetupLink("From", data.Object("from"), data.Text("from-name"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", data.Object("start") ? ("of " + data.Text("start-name")).c_str() : "");
    ImGui::TextUnformatted("To");
    ImGui::SameLine(110);
    SetupLink("To", data.Object("to"), data.Text("to-name"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", data.Object("end") ? ("of " + data.Text("end-name")).c_str() : "");
    ImGui::Dummy(ImVec2(1, 6));
    ImGui::TextUnformatted("Delay");
    ImGui::SameLine(110);
    int delay = int(values.Integer("behavior-link.delay"));
    const bool editable = data.Boolean("schematic-enabled");
    ImGui::BeginDisabled(!editable);
    ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("##delay", &delay, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue) && editable)
        PreviewValue(target, "behavior-link.delay", delay);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted("frames");
    ImGui::TextUnformatted("Current Delay");
    ImGui::SameLine(110);
    ImGui::Text("%d", int(data.Integer("current-delay")));
    ImGui::Dummy(ImVec2(1, 6));
    if (data.Object("start"))
        ShowInSchematic(data);
    ImGui::EndGroup();
}

void SetupWindows::SynchroPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "synchro", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target, {"synchro.users"}, propertyData))
        return;
    const auto values = propertyData.Values();
    ImGui::BeginGroup();
    Group("Rendez-Vous");
    ImGui::TextUnformatted("Number Of Users");
    ImGui::SameLine(160);
    int users = int(values.Integer("synchro.users"));
    ImGui::SetNextItemWidth(90);
    if (ImGui::DragInt("##users", &users, 0.1f, 0, 10000))
        PreviewValue(id, "synchro.users", users);
    const int arrived = int(data.Rows("arrived").size());
    ImGui::Text("Arrived:  %d", arrived);
    if (ImGui::Button("Reset", ImVec2(90, 0)))
        RunSetupCommand("setup.synchro-reset", {ObjectArgument("target", target)});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forgets the objects arrived");
    ImGui::EndGroup();
    ImGui::SameLine(330);
    ImGui::BeginGroup();
    Group("Arrived Objects");
    if (ImGui::BeginChild("arrived", ImVec2(280, 120), ImGuiChildFlags_Borders)) {
        TypeAhead typeAhead;
        for (int i = 0; i < arrived; ++i) {
            const SetupFields object(data.Rows("arrived")[size_t(i)].fields);
            const auto arrivedObject = object.Object("target");
            ImGui::PushID(i);
            if (typeAhead.Selectable(object.Text("name").c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                arrivedObject && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                Open(arrivedObject);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::Separator();
    ParameterUsers(target);
}

void SetupWindows::StatePage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "state", description))
        return;
    const auto data = description.Values();
    SetupDetails propertyData;
    if (!ReadProperties(target, {"state.active"}, propertyData))
        return;
    const auto values = propertyData.Values();
    Group("State");
    bool active = values.Boolean("state.active");
    if (ImGui::Checkbox("Active (entered)", &active))
        PreviewValue(target, "state.active", active);
    ImGui::Separator();
    ParameterUsers(target);
}

void SetupWindows::CriticalSectionPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "critical-section", description))
        return;
    const auto data = description.Values();
    Group("Critical Section");
    if (ImGui::Button("Reset", ImVec2(90, 0)))
        RunSetupCommand("setup.critical-section-reset", {ObjectArgument("target", target)});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lets the object inside the critical section go");
    ImGui::Separator();
    ParameterUsers(target);
}

void SetupWindows::InterfaceObjectsPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "interface-objects", description))
        return;
    const auto data = description.Values();
    Group("Interface Objects");
    ImGui::TextUnformatted("GUID");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(data.Text("guid").c_str());
    ImGui::TextUnformatted("Chunks");
    ImGui::SameLine(110);
    ImGui::Text("%d", int(data.Integer("count")));
    const auto &sizes = data.Integers("sizes");
    for (size_t i = 0; i < sizes.size(); ++i)
        if (int64_t(sizes[i]) >= 0)
            ImGui::BulletText("Chunk %d: %d bytes", int(i), int(sizes[i]));
}

void SetupWindows::OtherPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "other", description))
        return;
    const auto data = description.Values();
    ImGui::Text("%s  (%s, ID %u)", data.Text("name").c_str(), data.Text("class-name").c_str(), unsigned(target.object));
    ImGui::TextDisabled("This class has no specific Setup page; see its attributes.");
}

void SetupWindows::ShowInSchematic(const SetupFields &data) {
    ImGui::BeginDisabled(!data.Boolean("schematic-enabled"));
    if (ImGui::Button("Show in Schematic")) {
        const auto values = BorrowEditorFields(data.Rows("position"));
        RunSetupCommand("script.navigate", {values.data(), values.data() + values.values.size()});
    }
    ImGui::EndDisabled();
}
void SetupWindows::ParameterValue(const SetupFields &data, bool editable) {
    {
        SetupForm form("##parameter-value");
        if (form) {
            form.Row("Value");
            const auto text = data.Text("value-text");
            ImGui::TextWrapped("%s", text.size() > 2000 ? (text.substr(0, 2000) + "...").c_str() : text.c_str());
            if (data.Object("value-object")) {
                form.Row("Object");
                ImGui::PushTextWrapPos(0);
                SetupLink("Object", data.Object("value-object"), data.Text("value-object-name"));
                ImGui::PopTextWrapPos();
            }
        }
    }
    ImGui::BeginDisabled(!data.Object("value") || !editable);
    if (ImGui::Button("Edit Value..."))
        RunSetupCommand("parameter.edit", {ObjectArgument("target", data.Object("value"))});
    ImGui::EndDisabled();
}
void SetupWindows::ParameterUsers(ObjectRef target) {
    SetupDetails description;
    if (!ReadDetails(target, "parameter-users", description))
        return;
    const auto data = description.Values();
    Group("Used By");
    const float unit = PageUnit();
    if (!ImGui::BeginTable("users", 3,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_ScrollX,
                           {0, 150 * unit}, std::max(760 * unit, ImGui::GetContentRegionAvail().x)))
        return;
    TypeAhead ahead;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Behavior", ImGuiTableColumnFlags_WidthFixed, 220 * unit);
    ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, 180 * unit);
    ImGui::TableSetupColumn("Script");
    ImGui::TableHeadersRow();
    for (const auto &value : data.Rows("users")) {
        const SetupFields row(value.fields);
        ImGui::PushID(int(row.Object("target").object));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ahead.Selectable(row.Text("behavior-name").c_str(), false,
                             ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && row.Object("behavior"))
            Open(row.Object("behavior"));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.Text("name").c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.Text("script-name").c_str());
        ImGui::PopID();
    }
    ImGui::EndTable();
}
void SetupWindows::AttributePage(Setup &setup) {
    const auto client = Client(setup.nameOwner);
    ObjectAttributes data;
    if (!QueryObjectAttributes(client, setup.reference, data))
        return;
    if (!data.behavioral) {
        ImGui::TextDisabled("Only behavioral objects have attributes.");
        return;
    }
    if (!data.available)
        return;
    if (setup.attributeCatalog != data.catalog) {
        setup.attributeRows.clear();
        setup.attributeCatalog = data.catalog;
    }
    for (auto it = setup.attributeRows.begin(); it != setup.attributeRows.end();) {
        const bool held =
            std::any_of(data.rows.begin(), data.rows.end(), [&](const auto &row) { return row.type == *it; });
        it = held ? std::next(it) : setup.attributeRows.erase(it);
    }
    AttributeClipboard clipboard;
    QueryAttributeClipboard(client, clipboard);
    const float listHeight = std::max(80.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - 4);
    if (!ImGui::BeginTable("attributes", 4,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate,
                           ImVec2(0, listHeight)))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 24);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 260);
    ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 180);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_NoSort);
    ImGui::TableHeadersRow();
    std::vector<size_t> order;
    for (size_t i = 0; i < data.rows.size(); ++i)
        order.push_back(i);
    if (auto *specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
        const bool byName = specs->Specs[0].ColumnIndex == 1,
                   ascending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const auto &first = byName ? data.rows[a].name : data.rows[a].category;
            const auto &second = byName ? data.rows[b].name : data.rows[b].category;
            const auto compared = ImStricmp(first.c_str(), second.c_str());
            return ascending ? compared < 0 : compared > 0;
        });
    }
    auto typesArgument = [&](const std::vector<uint64_t> &types) {
        return EditorValue{
            EDITOR_TEXT("types"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {types.data(), uint32_t(types.size())}};
    };
    auto remove = [&](const std::vector<uint64_t> &types) {
        return RunSetupCommand("attribute.remove", {ObjectsArgument("objects", {setup.reference}), typesArgument(types),
                                                    IntegerArgument("catalog", data.catalog)});
    };
    auto removeSelected = [&] {
        const std::vector<uint64_t> types(setup.attributeRows.begin(), setup.attributeRows.end());
        const auto result = remove(types);
        if (result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE || result.status == EDITOR_QUEUED)
            setup.attributeRows.clear();
    };
    auto copySelected = [&] {
        const std::vector<uint64_t> types(setup.attributeRows.begin(), setup.attributeRows.end());
        RunSetupCommand("attribute.copy-values", {ObjectArgument("target", setup.reference), typesArgument(types),
                                                  IntegerArgument("catalog", data.catalog)});
    };
    auto paste = [&] {
        RunSetupCommand("attribute.paste-attributes", {ObjectsArgument("objects", {setup.reference}),
                                                       IntegerArgument("clipboard-revision", clipboard.revision)});
    };
    for (auto i : order) {
        const auto &row = data.rows[i];
        ImGui::PushID(int(row.type));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("x"))
            remove({row.type});
        ImGui::TableNextColumn();
        const bool selected = setup.attributeRows.count(row.type) != 0;
        if (ImGui::Selectable(row.name.empty() ? "?" : row.name.c_str(), selected)) {
            if (!ImGui::GetIO().KeyCtrl)
                setup.attributeRows = {row.type};
            else if (selected)
                setup.attributeRows.erase(row.type);
            else
                setup.attributeRows.insert(row.type);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected)
            setup.attributeRows = {row.type};
        if (ImGui::BeginPopupContextItem("attributemenu")) {
            if (ImGui::MenuItem("Delete")) removeSelected();
            if (ImGui::MenuItem("Copy Attribute Parameter Value"))
                copySelected();
            ImGui::EndPopup();
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.category.c_str());
        ImGui::TableNextColumn();
        if (row.parameter) {
            ImGui::Selectable(row.value.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                RunSetupCommand("attribute.edit-value",
                                {ObjectArgument("target", setup.reference), IntegerArgument("type", row.type),
                                 IntegerArgument("catalog", data.catalog), ObjectArgument("parameter", row.parameter)});
        }
        ImGui::PopID();
    }
    if (!clipboard.names.empty() &&
        ImGui::BeginPopupContextWindow("attributepaste",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Paste Attributes", "Ctrl+V"))
            paste();
        ImGui::EndPopup();
    }
    ImGui::EndTable();
    const bool keyboard = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                          !ImGui::GetIO().WantTextInput &&
                          !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (keyboard && !setup.attributeRows.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        removeSelected();
    if (keyboard && !clipboard.names.empty() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
        paste();
    if (ImGui::Button("Add Attribute"))
        RunSetupCommand("attribute.choose",
                        {ObjectsArgument("objects", {setup.reference}), IntegerArgument("more", 0)});
}

} // namespace vdev
