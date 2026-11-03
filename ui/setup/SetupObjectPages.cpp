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
    auto typed = [&](const char *label, const std::string &value, bool multiline, bool fontFace) {
        std::string shown = value;
        const bool changed = multiline ? ImGui::InputTextMultiline(label, &shown, ImVec2(300, 70))
                                       : (ImGui::SetNextItemWidth(160), ImGui::InputText(label, &shown));
        if (changed) {
            if (fontFace) {
                auto changedFont = font;
                changedFont.face = shown;
                PreviewFont(id, changedFont);
            } else
                PreviewValue(id, "spritetext.text", shown);
        }
    };
    ImGui::BeginGroup();
    Group("Text");
    typed("##text", values.Text("spritetext.text"), true, false);
    ImGui::EndGroup();
    ImGui::SameLine(340);
    ImGui::BeginGroup();
    Group("Font");
    auto setFont = [&](const char *, const SetupFont &changed) { PreviewFont(id, changed); };
    if (!hasFont)
        ImGui::TextDisabled("(no font yet)");
    typed("Face", font.face, false, true);
    SetupFont edited = font;
    ImGui::SetNextItemWidth(100);
    if (ImGui::DragInt("Size", &edited.size, 0.2f, 1, 512)) setFont("Font Size", edited);
    edited = font;
    bool bold = font.weight >= 600;
    if (ImGui::Checkbox("Bold", &bold)) {
        edited.weight = bold ? 700 : 400;
        setFont("Font Weight", edited);
    }
    ImGui::SameLine();
    edited = font;
    if (ImGui::Checkbox("Italic", &edited.italic)) setFont("Font Italic", edited);
    ImGui::SameLine();
    edited = font;
    if (ImGui::Checkbox("Underline", &edited.underline)) setFont("Font Underline", edited);
    ImGui::EndGroup();
    ImGui::SameLine(640);
    ImGui::BeginGroup();
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
    ImGui::Dummy(ImVec2(1, 6));
    // CKSPRITETEXT_ALIGNMENT: a horizontal and a vertical choice
    // (CKSPRITETEXT_CENTER centers both ways).
    Group("Alignment");
    CKDWORD align = static_cast<CKDWORD>(values.Integer("spritetext.alignment"));
    if (align & CKSPRITETEXT_CENTER) align = CKSPRITETEXT_HCENTER | CKSPRITETEXT_VCENTER;
    const CKDWORD horizontal = CKSPRITETEXT_LEFT | CKSPRITETEXT_HCENTER | CKSPRITETEXT_RIGHT;
    const CKDWORD vertical = CKSPRITETEXT_TOP | CKSPRITETEXT_VCENTER | CKSPRITETEXT_BOTTOM;
    auto choice = [&](const char *label, CKDWORD bit, CKDWORD group) {
        if (ImGui::RadioButton(label, (align & bit) != 0) && !(align & bit)) {
            const CKDWORD changed = (align & ~group) | bit;
            PreviewValue(id, "spritetext.alignment", static_cast<int>(changed));
        }
    };
    choice("Left", CKSPRITETEXT_LEFT, horizontal);
    ImGui::SameLine();
    choice("Center##h", CKSPRITETEXT_HCENTER, horizontal);
    ImGui::SameLine();
    choice("Right", CKSPRITETEXT_RIGHT, horizontal);
    choice("Top", CKSPRITETEXT_TOP, vertical);
    ImGui::SameLine();
    choice("Center##v", CKSPRITETEXT_VCENTER, vertical);
    ImGui::SameLine();
    choice("Bottom", CKSPRITETEXT_BOTTOM, vertical);
    ImGui::EndGroup();
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
    const auto values = propertyData.Values();
    ImGui::BeginGroup();
    Group("Kinematic Chain");
    ObjectRef chosen{};
    ImGui::TextUnformatted("Start Effector");
    ImGui::SameLine(110);
    if (ObjectChoice("##start", CKCID_BODYPART, data.Object("start"), data.Text("start-name"), &chosen)) {
        PreviewValue(id, "kinematic.start", chosen);
        FlushPending();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!data.Object("start"));
    if (ImGui::SmallButton(">##startsetup") && data.Object("start"))
        Open(data.Object("start"));
    ImGui::EndDisabled();
    ImGui::TextUnformatted("End Effector");
    ImGui::SameLine(110);
    if (ObjectChoice("##end", CKCID_BODYPART, data.Object("end"), data.Text("end-name"), &chosen)) {
        PreviewValue(id, "kinematic.end", chosen);
        FlushPending();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!data.Object("end"));
    if (ImGui::SmallButton(">##endsetup") && data.Object("end"))
        Open(data.Object("end"));
    ImGui::EndDisabled();
    const bool complete = data.Object("start") && data.Object("end");
    const int bodies = complete ? int(data.Integer("body-count")) : 0;
    ImGui::Dummy(ImVec2(1, 6));
    ImGui::Text("Chain Length:   %s", complete ? std::to_string(data.Real("length")).c_str() : "-");
    ImGui::Text("Body Parts:     %d", bodies);
    if (complete && bodies <= 0) ImGui::TextDisabled("The end effector is not below the start effector.");
    ImGui::EndGroup();
    // Right: the body parts of the chain, from the start effector.
    ImGui::SameLine(420);
    ImGui::BeginGroup();
    Group("Chain");
    if (ImGui::BeginTable("chain", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(300, 150))) {
        TypeAhead typeAhead;
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 26);
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
            if (typeAhead.Selectable(part.Text("name").c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && body &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                Open(body);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndGroup();
    ImGui::Separator();
    ParameterUsers(target);
}

void SetupWindows::LayerPage(ObjectRef target) {
    const auto id = target;
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
    VxColor color{float(channels[0]), float(channels[1]), float(channels[2]), float(channels[3])};
    const auto square = [&](int x, int y) { return int(int64_t(squares[size_t(y) * size_t(width) + size_t(x)])); };
    ImGui::BeginGroup();
    Group("Layer");
    ImGui::TextUnformatted("Grid");
    ImGui::SameLine(110);
    ImGui::BeginDisabled(!grid);
    if (ImGui::SmallButton(">") && grid) {
        Open(grid);
        m_GridLayer = target;
    }
    ImGui::EndDisabled();
    if (grid && ImGui::IsItemHovered()) ImGui::SetTooltip("Grid Setup");
    ImGui::SameLine();
    ImGui::TextUnformatted(data.Text("grid-name").c_str());
    ImGui::TextUnformatted("Type");
    ImGui::SameLine(110);
    const ImVec2 swatch = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(swatch.x, swatch.y + 1), ImVec2(swatch.x + 20, swatch.y + 14),
                                              ToImColor(color));
    ImGui::Dummy(ImVec2(24, 15));
    ImGui::SameLine();
    ImGui::Text("%s  (%d)", data.Text("type-name").c_str(), int(data.Integer("type")));
    ImGui::TextUnformatted("Format");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(int(data.Integer("format")) == CKGRID_LAYER_FORMAT_NORMAL
                               ? "Normal"
                               : std::to_string(int(data.Integer("format"))).c_str());
    bool visible = values.Boolean("layer.visible");
    if (ImGui::Checkbox("Visible", &visible))
        PreviewValue(target, "layer.visible", visible);
    // The values: how many squares have one, their range.
    int filled = 0, low = 255, high = 0;
    for (int y = 0; y < length; ++y)
        for (int x = 0; x < width; ++x) {
            int value = 0;
            value = square(x, y);
            if (value > 0) ++filled;
            low = std::min(low, value);
            high = std::max(high, value);
        }
    ImGui::Dummy(ImVec2(1, 6));
    Group("Squares");
    ImGui::Text("Width : %d    Length : %d", width, length);
    ImGui::Text("With a value : %d of %d", filled, width * length);
    if (width * length > 0) ImGui::Text("Values : %d to %d", low, high);
    ImGui::EndGroup();
    // Right: the squares (row 0 at the bottom, as in the Grid Setup).
    ImGui::SameLine(330);
    ImGui::BeginGroup();
    const float side = 256.0f;
    const float cell = width > 0 && length > 0 ? std::max(1.0f, std::min(side / width, side / length)) : 0.0f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, ImVec2(at.x + side + 16, at.y + side + 16), CurrentDevTheme().previewBackground);
    const ImVec2 origin(at.x + 8, at.y + 8);
    for (int y = 0; y < length; ++y)
        for (int x = 0; x < width; ++x) {
            int value = 0;
            value = square(x, y);
            const float k = std::max(0, std::min(255, value)) / 255.0f;
            const ImVec2 a(origin.x + x * cell, origin.y + (length - 1 - y) * cell);
            dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell),
                              IM_COL32(static_cast<int>(color.r * 255 * k), static_cast<int>(color.g * 255 * k),
                                       static_cast<int>(color.b * 255 * k), 255));
        }
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##squares", ImVec2(std::max(1.0f, width * cell), std::max(1.0f, length * cell)));
    if (ImGui::IsItemHovered() && cell > 0.0f) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int x = static_cast<int>((mouse.x - origin.x) / cell);
        const int y = length - 1 - static_cast<int>((mouse.y - origin.y) / cell);
        int value = 0;
        if (x >= 0 && x < width && y >= 0 && y < length) {
            value = square(x, y);
            ImGui::SetTooltip("X : %d    Y : %d    Value : %d", x, y, value);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + side + 20));
    ImGui::Dummy(ImVec2(side + 16, 1));
    ImGui::EndGroup();
}

void SetupWindows::ParameterPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "parameter", description))
        return;
    const auto data = description.Values();
    ImGui::BeginGroup();
    Group(data.Text("heading").c_str());
    ImGui::TextUnformatted("Type");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(data.Text("type-name").c_str());
    ImGui::TextUnformatted("Owner");
    ImGui::SameLine(110);
    SetupLink("Owner", data.Object("owner"), data.Text("owner-name"));
    if (data.Object("behavior") && data.Object("behavior") != data.Object("owner")) {
        ImGui::TextUnformatted("Behavior");
        ImGui::SameLine(110);
        SetupLink("Behavior", data.Object("behavior"), data.Text("behavior-name"));
    }
    ImGui::Dummy(ImVec2(1, 6));
    // The value of a This parameter is its owner (CKParameterLocal::SetOwner).
    ParameterValue(data, data.Boolean("editable"));
    ImGui::Dummy(ImVec2(1, 6));
    ShowInSchematic(data);
    ImGui::EndGroup();
    if (data.Boolean("output")) {
        ImGui::SameLine(560);
        ImGui::BeginGroup();
        Group("Destinations");
        if (ImGui::BeginTable("destinations", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(380, 180))) {
            TypeAhead typeAhead;
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, 170);
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
        ImGui::EndGroup();
    }
}

void SetupWindows::ParameterInPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "parameter-input", description))
        return;
    const auto data = description.Values();
    ImGui::BeginGroup();
    Group(data.Boolean("enabled") ? "Input Parameter" : "Input Parameter (disabled)");
    ImGui::TextUnformatted("Type");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(data.Text("type-name").c_str());
    ImGui::TextUnformatted("Owner");
    ImGui::SameLine(110);
    SetupLink("Owner", data.Object("owner"), data.Text("owner-name"));
    if (data.Object("behavior") && data.Object("behavior") != data.Object("owner")) {
        ImGui::TextUnformatted("Behavior");
        ImGui::SameLine(110);
        SetupLink("Behavior", data.Object("behavior"), data.Text("behavior-name"));
    }
    ImGui::Dummy(ImVec2(1, 6));
    Group("Source");
    ImGui::TextUnformatted("Linked to");
    ImGui::SameLine(110);
    SetupLink("Linked to", data.Object("direct"), data.Text("direct-name"));
    ImGui::TextUnformatted("Shares with");
    ImGui::SameLine(110);
    SetupLink("Shares with", data.Object("shared"), data.Text("shared-name"));
    if (data.Object("value") && data.Object("value") != data.Object("direct")) {
        ImGui::TextUnformatted("Real source");
        ImGui::SameLine(110);
        SetupLink("Real source", data.Object("value"), data.Text("value-name"));
    }
    ImGui::Dummy(ImVec2(1, 6));
    // The value is the source's; only a local of its own (Dev's value
    // holder) is edited from here.
    ParameterValue(data, data.Boolean("editable"));
    ImGui::Dummy(ImVec2(1, 6));
    ShowInSchematic(data);
    ImGui::EndGroup();
}

void SetupWindows::OperationPage(ObjectRef target) {
    const auto id = target;
    SetupDetails description;
    if (!ReadDetails(target, "operation", description))
        return;
    const auto data = description.Values();
    ImGui::BeginGroup();
    Group("Parameter Operation");
    ImGui::TextUnformatted("Operation");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(data.Text("operation").c_str());
    ImGui::TextUnformatted("GUID");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(data.Text("guid").c_str());
    ImGui::TextUnformatted("Owner");
    ImGui::SameLine(110);
    SetupLink("Owner", data.Object("owner"), data.Text("owner-name"));
    ImGui::Dummy(ImVec2(1, 6));
    ShowInSchematic(data);
    ImGui::EndGroup();
    ImGui::SameLine(420);
    ImGui::BeginGroup();
    Group("Parameters");
    if (ImGui::BeginTable("operands", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(560, 0))) {
        TypeAhead typeAhead(TypeAhead::Shared);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120);
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
    ImGui::TextDisabled("Double-click a parameter to show its Setup.");
    ImGui::EndGroup();
}

void SetupWindows::BehaviorPage(ObjectRef target) {
    const auto id = target;
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
    ImGui::BeginGroup();
    Group(data.Text("heading").c_str());
    if (data.Boolean("prototype")) {
        ImGui::TextUnformatted("Prototype");
        ImGui::SameLine(110);
        ImGui::TextUnformatted(data.Text("prototype-name").c_str());
        ImGui::TextUnformatted("GUID");
        ImGui::SameLine(110);
        ImGui::TextUnformatted(data.Text("guid").c_str());
        ImGui::TextUnformatted("Version");
        ImGui::SameLine(110);
        ImGui::Text("0x%08x", static_cast<unsigned>(data.Integer("version")));
    }
    ImGui::TextUnformatted("Owner");
    ImGui::SameLine(110);
    SetupLink("Owner", data.Object("owner"), data.Text("owner-name"));
    if (target != script) {
        ImGui::TextUnformatted("Script");
        ImGui::SameLine(110);
        SetupLink("Script", script, data.Text("script-name"));
        if (data.Object("parent") != script) {
            ImGui::TextUnformatted("Parent");
            ImGui::SameLine(110);
            SetupLink("Parent", data.Object("parent"), data.Text("parent-name"));
        }
    }
    // Priority (Dev's Edit Priority, Y): one undo step, in the Script.
    ImGui::TextUnformatted("Priority");
    ImGui::SameLine(110);
    int priority = int(values.Integer("behavior.priority"));
    const bool editable = data.Boolean("schematic-enabled");
    ImGui::BeginDisabled(!editable);
    ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("##priority", &priority, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue) && editable)
        PreviewValue(target, "behavior.priority", priority);
    ImGui::EndDisabled();
    ImGui::TextUnformatted("Active");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(data.Boolean("active") ? "Yes" : "No");
    if (data.Boolean("using-target")) {
        ImGui::TextUnformatted("Target");
        ImGui::SameLine(110);
        SetupLink("Target", data.Object("target-parameter"), data.Text("target-parameter-name"));
    }
    ImGui::TextUnformatted("Applies to");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(data.Text("compatible").c_str());
    ImGui::Dummy(ImVec2(1, 6));
    ShowInSchematic(data);
    ImGui::EndGroup();
    // Middle: CK_BEHAVIOR_FLAGS (shown, set by CK and the prototype).
    ImGui::SameLine(330);
    ImGui::BeginGroup();
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
    ImGui::EndGroup();
    // Right: the contents.
    ImGui::SameLine(610);
    ImGui::BeginGroup();
    Group("Contents");
    ImGui::Text("Inputs:                %d", int(data.Integer("inputs")));
    ImGui::Text("Outputs:               %d", int(data.Integer("outputs")));
    ImGui::Text("Input Parameters:      %d", int(data.Integer("input-parameters")));
    ImGui::Text("Output Parameters:     %d", int(data.Integer("output-parameters")));
    ImGui::Text("Local Parameters:      %d", int(data.Integer("local-parameters")));
    if (graph) {
        ImGui::Text("Behaviors:             %d", int(data.Rows("behaviors").size()));
        ImGui::Text("Behavior Links:        %d", int(data.Integer("links")));
        ImGui::Text("Parameter Operations:  %d", int(data.Integer("operations")));
    }
    // A graph's behaviors; a double click shows one's Setup.
    if (graph) {
        ImGui::Dummy(ImVec2(1, 6));
        Group("Behaviors");
        if (ImGui::BeginTable("behaviors", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(520, 260))) {
            TypeAhead typeAhead;
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 260);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 130);
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
    ImGui::EndGroup();
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
    ImGui::TextUnformatted("Value");
    ImGui::SameLine(110);
    const auto text = data.Text("value-text");
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
    ImGui::TextUnformatted(text.size() > 2000 ? (text.substr(0, 2000) + "...").c_str() : text.c_str());
    ImGui::PopTextWrapPos();
    if (data.Object("value-object")) {
        ImGui::TextUnformatted("Object");
        ImGui::SameLine(110);
        SetupLink("Object", data.Object("value-object"), data.Text("value-object-name"));
    }
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 110.0f);
    ImGui::BeginDisabled(!data.Object("value") || !editable);
    if (ImGui::Button("Edit Value...", ImVec2(110, 0)))
        RunSetupCommand("parameter.edit", {ObjectArgument("target", data.Object("value"))});
    ImGui::EndDisabled();
}
void SetupWindows::ParameterUsers(ObjectRef target) {
    SetupDetails description;
    if (!ReadDetails(target, "parameter-users", description))
        return;
    const auto data = description.Values();
    Group("Used By");
    if (!ImGui::BeginTable("users", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                           ImVec2(760, 150)))
        return;
    TypeAhead ahead;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Behavior", ImGuiTableColumnFlags_WidthFixed, 220);
    ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, 180);
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
