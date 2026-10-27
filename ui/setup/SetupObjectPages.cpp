#include <filesystem>
// The classes without a Dev Setup (animations, Script objects, parameters, ...) and the Attribute page.

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
#include "core/runtime/GridCompat.h"
#include "core/level/LevelCommands.h"
#include "core/level/ObjectEditCommands.h"
#include "core/level/ObjectProperties.h"
#include "core/level/SceneCommands.h"
#include "core/graph/TopologyCommands.h"
#include "ui/schematic/Snapshots.h"
#include "ui/controls/ObjectText.h"
#include "ui/controls/TypeAhead.h"
#include "core/runtime/TextEncoding.h"
#include "core/graph/ValueCommands.h"
#include "ui/setup/SetupWindowsDetail.h"

namespace vdev {

using namespace setup;

// ---------------------------------------------------------------- classes without a Dev Setup
// Dev.exe opens no Setup for these classes (sub_4B0010 has no editor for
// them); the pages follow their class definitions (SDK headers).

namespace {

const char *ControllerName(CKAnimController *controller) {
    if (!controller) return "-";
    switch (controller->GetType()) {
    case CKANIMATION_LINPOS_CONTROL:
    case CKANIMATION_LINROT_CONTROL:
    case CKANIMATION_LINSCL_CONTROL:
    case CKANIMATION_LINSCLAXIS_CONTROL: return "Linear";
    case CKANIMATION_TCBPOS_CONTROL:
    case CKANIMATION_TCBROT_CONTROL:
    case CKANIMATION_TCBSCL_CONTROL:
    case CKANIMATION_TCBSCLAXIS_CONTROL: return "TCB";
    case CKANIMATION_BEZIERPOS_CONTROL:
    case CKANIMATION_BEZIERSCL_CONTROL: return "Bezier";
    case CKANIMATION_MORPH_CONTROL: return "Morph";
    default: return "?";
    }
}

// "TCB (12)": the controller type and its key count.
std::string KeysOf(CKAnimController *controller) {
    if (!controller) return "-";
    return std::string(ControllerName(controller)) + " (" + std::to_string(controller->GetKeyCount()) + ")";
}

} // namespace

void SetupWindows::SetupLink(const char *label, CKObject *object) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(110);
    ImGui::BeginDisabled(!object);
    if (ImGui::SmallButton(">") && object) m_State.events.Post(ShowObjectSetup{object->GetID()});
    ImGui::EndDisabled();
    if (object && ImGui::IsItemHovered()) ImGui::SetTooltip("Setup");
    ImGui::SameLine();
    ImGui::TextUnformatted(NameOf(object).c_str());
    ImGui::PopID();
}

// Animation and Keyed Animation Setup (CKAnimation, CKKeyedAnimation): length,
// frame rate, play options, the transition and secondary modes stored in the
// animation, the merge factor; keyed animations list their object animations.
void SetupWindows::AnimationPage(CKAnimation *animation) {
    if (!animation) return;
    const CK_ID id = animation->GetID();
    CKKeyedAnimation *keyed = CKKeyedAnimation::Cast(animation);
    auto flag = [&](const ObjectProperty<bool> &property) {
        bool on = property.get(animation);
        if (ImGui::Checkbox(property.label, &on)) LiveSet(id, property, on);
    };
    // Left: the animation.
    ImGui::BeginGroup();
    Group("Animation");
    float length = animation->GetLength();
    ImGui::SetNextItemWidth(120);
    if (ImGui::DragFloat("Length (frames)", &length, 0.5f, 1.0f, 100000.0f, "%.1f"))
        LiveSet(id, properties::AnimationLength, length);
    ImGui::Text("Current frame:  %.2f  (step %.3f)", animation->GetFrame(), animation->GetStep());
    ImGui::Dummy(ImVec2(1, 6));
    bool linked = animation->IsLinkedToFrameRate() != 0;
    float fps = animation->GetLinkedFrameRate();
    if (ImGui::Checkbox("Linked to frame rate", &linked))
        LiveSet(id, properties::AnimationLinked, linked);
    ImGui::BeginDisabled(!linked);
    ImGui::SetNextItemWidth(120);
    if (ImGui::DragFloat("Frames per second", &fps, 0.1f, 1.0f, 1000.0f, "%.1f"))
        LiveSet(id, properties::AnimationFrameRate, fps);
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(1, 6));
    SetupLink("Character", animation->GetCharacter());
    SetupLink("Root Entity", animation->GetRootEntity());
    ImGui::EndGroup();
    // Middle: play options and merging.
    ImGui::SameLine(330);
    ImGui::BeginGroup();
    Group("Play Options");
    flag(properties::AnimationCanBeInterrupted);
    flag(properties::AnimationAlignOrientation);
    ImGui::Dummy(ImVec2(1, 6));
    Group("Merge");
    if (animation->IsMerged()) {
        float factor = m_MergeFactor >= 0.0f ? m_MergeFactor : animation->GetMergeFactor();
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderFloat("Merge Factor", &factor, 0.0f, 1.0f, "%.3f")) {
            m_MergeFactor = factor;
            LiveSet(id, properties::AnimationMergeFactor, factor);
        }
        if (!ImGui::IsItemActive()) m_MergeFactor = -1.0f;
    } else {
        ImGui::TextDisabled("Not a merged animation");
    }
    ImGui::EndGroup();
    // Right: the transition and secondary modes (CK_ANIMATION_TRANSITION_MODE,
    // CK_SECONDARYANIMATION_FLAGS), used when a behavior asks for the
    // animation's own settings or when the animation is preset.
    ImGui::SameLine(640);
    ImGui::BeginGroup();
    Group("Transition Mode");
    flag(properties::AnimationTransitionPreset);
    const std::pair<const char *, const ObjectProperty<bool> *> transitions[] = {
        {"Break (from now)", &properties::AnimationTransitionFromNow},
        {"Warp from current", &properties::AnimationTransitionWarpFromCurrent},
        {"To start", &properties::AnimationTransitionToStart},
        {"Warp to start", &properties::AnimationTransitionWarpToStart},
        {"Warp to best", &properties::AnimationTransitionWarpToBest},
        {"Warp to same position", &properties::AnimationTransitionWarpToSamePosition},
        {"Use velocity", &properties::AnimationTransitionUseVelocity},
        {"Loop if equal", &properties::AnimationTransitionLoopIfEqual}};
    for (int i = 0; i < 8; ++i) {
        if (i % 2) ImGui::SameLine(190);
        const ObjectProperty<bool> &property = *transitions[i].second;
        bool on = property.get(animation);
        if (ImGui::Checkbox(transitions[i].first, &on)) LiveSet(id, property, on);
    }
    ImGui::Dummy(ImVec2(1, 6));
    Group("Secondary Animation");
    flag(properties::AnimationSecondaryPreset);
    const CKDWORD secondary = static_cast<CKDWORD>(animation->GetSecondaryAnimationMode());
    const CKDWORD playMask = CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_LOOP | CKSECONDARYANIMATION_LASTFRAME |
                             CKSECONDARYANIMATION_LOOPNTIMES;
    const std::pair<const char *, CKDWORD> plays[] = {{"One shot", CKSECONDARYANIMATION_ONESHOT},
                                                      {"Loop", CKSECONDARYANIMATION_LOOP},
                                                      {"Stay on last frame", CKSECONDARYANIMATION_LASTFRAME},
                                                      {"Loop N times", CKSECONDARYANIMATION_LOOPNTIMES}};
    for (int i = 0; i < 4; ++i) {
        if (i % 2) ImGui::SameLine(190);
        const CKDWORD bit = plays[i].second;
        if (ImGui::RadioButton(plays[i].first, (secondary & playMask) == bit) && (secondary & playMask) != bit)
            LiveSet(id, properties::AnimationSecondaryPlay, static_cast<int>(bit));
    }
    bool warp = (secondary & CKSECONDARYANIMATION_DOWARP) != 0;
    if (ImGui::Checkbox("Warp when starting or stopping", &warp))
        LiveSet(id, properties::AnimationSecondaryWarp, warp);
    ImGui::EndGroup();
    if (!keyed) return;
    // The object animations of a keyed animation: double-click opens the
    // Object Animation Setup, the entity column the entity's Setup.
    ImGui::Separator();
    Group("Object Animations");
    if (ImGui::BeginTable("objectanimations", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                                     ImGuiTableFlags_Resizable,
                          ImVec2(0, std::max(120.0f, ImGui::GetContentRegionAvail().y - 4)))) {
        TypeAhead typeAhead;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Entity");
        ImGui::TableSetupColumn("Animation");
        ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Rotation", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Scale", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Off-Axis Scale", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Morph", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableHeadersRow();
        for (int i = 0; i < keyed->GetAnimationCount(); ++i) {
            CKObjectAnimation *part = keyed->GetAnimation(i);
            if (!part) continue;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            CK3dEntity *entity = part->Get3dEntity();
            if (ImGui::SmallButton(">") && entity) m_State.events.Post(ShowObjectSetup{entity->GetID()});
            ImGui::SameLine();
            ImGui::TextUnformatted(NameOf(entity).c_str());
            ImGui::TableNextColumn();
            if (typeAhead.Selectable(NameOf(part).c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                m_State.events.Post(ShowObjectSetup{part->GetID()});
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", part->GetLength());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(KeysOf(part->GetPositionController()).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(KeysOf(part->GetRotationController()).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(KeysOf(part->GetScaleController()).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(KeysOf(part->GetScaleAxisController()).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(KeysOf(part->GetMorphController()).c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// Object Animation Setup (CKObjectAnimation): the animated entity, length,
// the information ignored when playing, the key controllers, sharing and
// merging, and the keyed animations using it.
void SetupWindows::ObjectAnimationPage(CKObjectAnimation *animation) {
    if (!animation) return;
    const CK_ID id = animation->GetID();
    const CKDWORD flags = animation->GetFlags();
    ImGui::BeginGroup();
    Group("Object Animation");
    SetupLink("Entity", animation->Get3dEntity());
    float length = animation->GetLength();
    ImGui::SetNextItemWidth(120);
    if (ImGui::DragFloat("Length (frames)", &length, 0.5f, 1.0f, 100000.0f, "%.1f"))
        LiveSet(id, properties::ObjectAnimationLength, length);
    ImGui::Text("Current step:  %.3f", animation->GetCurrentStep());
    ImGui::Dummy(ImVec2(1, 6));
    CKObjectAnimation *shared = animation->Shared();
    SetupLink("Shares data of", shared != animation ? shared : nullptr);
    if (animation->IsMerged()) {
        float factor = animation->GetMergeFactor();
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderFloat("Merge Factor", &factor, 0.0f, 1.0f, "%.3f"))
            LiveSet(id, properties::ObjectAnimationMergeFactor, factor);
    }
    ImGui::EndGroup();
    // Middle: CK_OBJECTANIMATION_FLAGS.
    ImGui::SameLine(330);
    ImGui::BeginGroup();
    Group("Ignore");
    const std::pair<const char *, const ObjectProperty<bool> *> ignored[] = {
        {"Position", &properties::ObjectAnimationIgnorePosition},
        {"Rotation", &properties::ObjectAnimationIgnoreRotation},
        {"Scale", &properties::ObjectAnimationIgnoreScale},
        {"Off-Axis Scale", &properties::ObjectAnimationIgnoreOffAxisScale},
        {"Morph", &properties::ObjectAnimationIgnoreMorph}};
    for (const auto &entry : ignored) {
        bool on = entry.second->get(animation);
        if (ImGui::Checkbox(entry.first, &on)) LiveSet(id, *entry.second, on);
    }
    ImGui::EndGroup();
    // Right: the controllers.
    ImGui::SameLine(560);
    ImGui::BeginGroup();
    Group("Keys");
    ImGui::Text("Position:        %s", KeysOf(animation->GetPositionController()).c_str());
    ImGui::Text("Rotation:        %s", KeysOf(animation->GetRotationController()).c_str());
    ImGui::Text("Scale:           %s", KeysOf(animation->GetScaleController()).c_str());
    ImGui::Text("Off-Axis Scale:  %s", KeysOf(animation->GetScaleAxisController()).c_str());
    ImGui::Text("Morph:           %s", KeysOf(animation->GetMorphController()).c_str());
    if (animation->HasMorphInfo())
        ImGui::Text("Morph vertices:  %d%s", animation->GetMorphVertexCount(),
                    animation->HasMorphNormalInfo() ? "  (with normals)" : "");
    ImGui::EndGroup();
    ImGui::Separator();
    std::vector<CKObject *> users;
    for (CKObject *object : ObjectsOf(*m_State.document, CKCID_KEYEDANIMATION, true)) {
        CKKeyedAnimation *owner = CKKeyedAnimation::Cast(object);
        for (int i = 0; i < owner->GetAnimationCount(); ++i)
            if (owner->GetAnimation(i) == animation) {
                users.push_back(owner);
                break;
            }
    }
    UsedBy("Used By", users, 150);
}

// Midi Sound Setup (CKMidiSound): the file, its save options, playback.
void SetupWindows::MidiSoundPage(CKMidiSound *sound) {
    if (!sound) return;
    const CK_ID id = sound->GetID();
    CKContext *context = m_State.document->Context();
    ImGui::BeginGroup();
    Group("Midi File");
    const char *file = sound->GetSoundFileName();
    ImGui::TextUnformatted(file && *file ? AnsiToUtf8(file).c_str() : "-- None --");
    if (ImGui::Button("Change File...", ImVec2(130, 0))) {
        FileDialogOptions options;
        options.filter = L"Midi files (*.mid;*.midi;*.rmi)\0*.mid;*.midi;*.rmi\0All files (*.*)\0*.*\0";
        FlushPending();
        m_State.RequestFile(options, [this, id](const FileDialogResult &result) {
            if (result.status != FileDialogResult::Status::Selected) return;
            const std::string chosen = result.paths.front();
            Report(EditObjectState(*m_State.document, id, "Change File", [chosen](CKObject *o) {
                CKMidiSound::Cast(o)->SetSoundFileName(const_cast<CKSTRING>(std::filesystem::u8path(chosen).string().c_str()));
            }), "Change File");
        });
    }
    ImGui::Dummy(ImVec2(1, 6));
    const char *saves[] = {"External", "Include Original File", "Use Global"};
    int save = std::max(0, std::min(2, static_cast<int>(sound->GetSaveOptions())));
    ImGui::SetNextItemWidth(170);
    if (TypeAheadCombo("Save Options", &save, saves, 3))
        LiveSet(id, properties::SoundSaveOptions, save);
    ImGui::EndGroup();
    ImGui::SameLine(330);
    ImGui::BeginGroup();
    Group("Playback");
    // The Midi Manager plays the file; without it (or a file) there is nothing to play.
    const bool playable = context->GetManagerByGuid(MIDI_MANAGER_GUID) && file && *file;
    ImGui::BeginDisabled(!playable);
    if (ImGui::Button(sound->IsPlaying() && !sound->IsPaused() ? "Pause" : "Play", ImVec2(60, 0)))
        GuardedCall([&] {
            if (sound->IsPlaying()) sound->Pause(!sound->IsPaused());
            else sound->Play();
        }, &m_State.document->Host(), "Play");
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(60, 0))) GuardedCall([&] { sound->Stop(); }, &m_State.document->Host(), "Stop");
    ImGui::EndDisabled();
    if (!playable) ImGui::TextDisabled(file && *file ? "(no Midi Manager)" : "(no file)");
    ImGui::Text("State:      %s", sound->IsPaused() ? "Paused" : sound->IsPlaying() ? "Playing" : "Stopped");
    ImGui::Text("Position:   %u", static_cast<unsigned>(playable ? sound->GetCurrentPos() : 0));
    ImGui::EndGroup();
}

// Sprite Text Setup (CKSpriteText): the text, its font, colors and alignment
// above the 2D layout of the 2D Sprite Setup (the sprite's image is the
// rendered text).
void SetupWindows::SpriteTextPage(CKSpriteText *text) {
    if (!text) return;
    const CK_ID id = text->GetID();
    SpriteTextFont font;
    const bool hasFont = ReadSpriteTextFont(text, &font);
    // A field being typed shows what was typed; it is applied when left.
    auto typed = [&](const char *label, const std::string &value, bool multiline,
                     const std::function<void(CKObject *, const std::string &)> &apply) {
        const CK_ID key = id * 2 + (multiline ? 1 : 0);
        std::string shown = m_SpriteTextEditing == key ? m_SpriteTextEdit : AnsiToUtf8(value);
        char buffer[2048];
        strncpy(buffer, shown.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = 0;
        const bool changed = multiline ? ImGui::InputTextMultiline(label, buffer, sizeof(buffer), ImVec2(300, 70))
                                       : (ImGui::SetNextItemWidth(160), ImGui::InputText(label, buffer, sizeof(buffer)));
        if (changed) {
            m_SpriteTextEditing = key;
            m_SpriteTextEdit = buffer;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && m_SpriteTextEditing == key) {
            const std::string entered = Utf8ToAnsi(m_SpriteTextEdit);
            if (entered != value) Live(id, label[0] == '#' ? "Sprite Text" : label, [apply, entered](CKObject *o) { apply(o, entered); });
            FlushPending();
            m_SpriteTextEditing = 0;
        }
    };
    ImGui::BeginGroup();
    Group("Text");
    typed("##text", text->GetText() ? text->GetText() : "", true,
          [](CKObject *o, const std::string &v) { properties::SpriteTextText.set(o, v); });
    ImGui::EndGroup();
    ImGui::SameLine(340);
    ImGui::BeginGroup();
    Group("Font");
    auto setFont = [&](const char *label, const SpriteTextFont &changed) {
        Live(id, label, [changed](CKObject *o) {
            CKSpriteText::Cast(o)->SetFont(const_cast<CKSTRING>(changed.face.c_str()), changed.size, changed.weight,
                                           changed.italic, changed.underline);
        });
    };
    if (!hasFont) ImGui::TextDisabled("(no font yet)");
    typed("Face", font.face, false, [font](CKObject *o, const std::string &v) {
        CKSpriteText::Cast(o)->SetFont(const_cast<CKSTRING>(v.c_str()), font.size, font.weight, font.italic, font.underline);
    });
    SpriteTextFont edited = font;
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
    auto color = [&](const ObjectProperty<VxColor> &property) {
        const VxColor value = property.get(text);
        float c[4] = {value.r, value.g, value.b, value.a};
        if (ImGui::ColorEdit4(property.label, c, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf))
            LiveSet(id, property, VxColor(c[0], c[1], c[2], c[3]));
    };
    color(properties::SpriteTextColor);
    color(properties::SpriteTextBackgroundColor);
    ImGui::Dummy(ImVec2(1, 6));
    // CKSPRITETEXT_ALIGNMENT: a horizontal and a vertical choice
    // (CKSPRITETEXT_CENTER centers both ways).
    Group("Alignment");
    CKDWORD align = static_cast<CKDWORD>(properties::SpriteTextAlignment.get(text));
    if (align & CKSPRITETEXT_CENTER) align = CKSPRITETEXT_HCENTER | CKSPRITETEXT_VCENTER;
    const CKDWORD horizontal = CKSPRITETEXT_LEFT | CKSPRITETEXT_HCENTER | CKSPRITETEXT_RIGHT;
    const CKDWORD vertical = CKSPRITETEXT_TOP | CKSPRITETEXT_VCENTER | CKSPRITETEXT_BOTTOM;
    auto choice = [&](const char *label, CKDWORD bit, CKDWORD group) {
        if (ImGui::RadioButton(label, (align & bit) != 0) && !(align & bit)) {
            const CKDWORD changed = (align & ~group) | bit;
            LiveSet(id, properties::SpriteTextAlignment, static_cast<int>(changed));
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
    Entity2DPage(text, true);
}

namespace {

// The Script a behavior belongs to (its topmost parent).
CKBehavior *ScriptOf(CKBehavior *behavior) {
    while (behavior && behavior->GetParent()) behavior = behavior->GetParent();
    return behavior;
}

// The behavior a parameter belongs to: its owner, or its operation's.
CKBehavior *BehaviorOf(CKObject *parameter) {
    CKObject *owner = nullptr;
    if (CKParameter *p = CKParameter::Cast(parameter)) owner = p->GetOwner();
    else if (CKParameterIn *in = CKParameterIn::Cast(parameter)) owner = in->GetOwner();
    if (CKParameterOperation *operation = CKParameterOperation::Cast(owner)) return operation->GetOwner();
    return CKBehavior::Cast(owner);
}

std::string ValueText(CKParameter *parameter) { return ParameterValueText(parameter); }

std::string TypeName(CKContext *context, CKParameterType type) {
    const char *name = context->GetParameterManager()->ParameterTypeToName(type);
    return name && *name ? AnsiToUtf8(name) : std::string("?");
}

std::string GuidText(const CKGUID &guid) {
    char text[32];
    std::snprintf(text, sizeof(text), "0x%08x, 0x%08x", static_cast<unsigned>(guid.d1), static_cast<unsigned>(guid.d2));
    return text;
}

// "Name (Owner)": a pin, an IO or a parameter with what it belongs to.
std::string OwnedName(CKObject *object, CKObject *owner) {
    return NameOf(object, "-- None --") + (owner ? " (" + NameOf(owner) + ")" : std::string());
}

// The object a parameter holds (object parameters), else null.
CKObject *ValueObjectOf(CKParameter *parameter) {
    return parameter && parameter->GetParameterClassID() ? parameter->GetValueObject(FALSE) : nullptr;
}

} // namespace

void SetupWindows::ShowInSchematic(CKBehavior *behavior, const ElementRef &ref) {
    CKBehavior *script = ScriptOf(behavior);
    const bool shown = script && m_State.document->FindScript(script->GetID());
    ImGui::BeginDisabled(!shown);
    if (ImGui::Button("Show in Schematic") && shown)
        m_State.events.Post(ShowElement{
            script->GetID(),
            ref.IsValid() || behavior == script || !behavior->GetParent()
                ? ref
                : ElementRef{ElementKind::Behavior, behavior->GetParent()->GetID(), behavior->GetID()},
            behavior != script ? behavior->GetID() : 0});
    ImGui::EndDisabled();
}

void SetupWindows::ParameterUsers(CKObject *object) {
    CKContext *context = m_State.document->Context();
    struct User {
        CKObject *parameter;
        CKBehavior *behavior;
    };
    // The input parameters getting it, then the locals and outputs holding
    // it that are not their sources (a Building Block's own local, as IK
    // Position's chain).
    std::vector<User> users;
    std::vector<CKParameter *> sources;
    const XObjectPointerArray &inputs = context->GetObjectListByType(CKCID_PARAMETERIN, TRUE);
    for (int i = 0; i < inputs.Size(); ++i) {
        CKParameterIn *input = CKParameterIn::Cast(inputs[i]);
        if (!input || m_State.document->IsDiscarded(input->GetID())) continue; // a deleted Building Block's
        CKParameter *source = input->GetRealSource();
        if (ValueObjectOf(source) != object) continue;
        users.push_back({input, BehaviorOf(input)});
        sources.push_back(source);
    }
    const XObjectPointerArray &parameters = context->GetObjectListByType(CKCID_PARAMETER, TRUE);
    for (int i = 0; i < parameters.Size(); ++i) {
        CKParameter *parameter = CKParameter::Cast(parameters[i]);
        if (parameter && !m_State.document->IsDiscarded(parameter->GetID()) && ValueObjectOf(parameter) == object &&
            std::find(sources.begin(), sources.end(), parameter) == sources.end())
            users.push_back({parameter, BehaviorOf(parameter)});
    }
    Group("Used By");
    if (!ImGui::BeginTable("users", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                           ImVec2(760, 150)))
        return;
    TypeAhead typeAhead;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Behavior", ImGuiTableColumnFlags_WidthFixed, 220);
    ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, 180);
    ImGui::TableSetupColumn("Script");
    ImGui::TableHeadersRow();
    for (const User &user : users) {
        ImGui::PushID(static_cast<int>(user.parameter->GetID()));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (typeAhead.Selectable(NameOf(user.behavior).c_str(), false,
                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && user.behavior)
            m_State.events.Post(ShowObjectSetup{user.behavior->GetID()});
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(NameOf(user.parameter, "<Unnamed>").c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(NameOf(ScriptOf(user.behavior)).c_str());
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void SetupWindows::ParameterValue(CKParameter *value, bool editable) {
    CKContext *context = m_State.document->Context();
    ImGui::TextUnformatted("Value");
    ImGui::SameLine(110);
    const std::string text = value ? ValueText(value) : std::string("-- None --");
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
    ImGui::TextUnformatted(text.size() > 2000 ? (text.substr(0, 2000) + "...").c_str() : text.c_str());
    ImGui::PopTextWrapPos();
    if (CKObject *object = ValueObjectOf(value)) SetupLink("Object", object);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 110.0f);
    ImGui::BeginDisabled(!value || !editable);
    if (ImGui::Button("Edit Value...", ImVec2(110, 0)) && value) {
        // Dev's Edit Parameter around the parameter (its type shown).
        m_AttributeType = TypeName(context, value->GetType());
        m_AttributeBox.Clear();
        CKBehavior *script = ScriptOf(BehaviorOf(value));
        m_Doc.valueScript = script ? script->GetID() : 0;
        if (m_AttributeBox.Add(value, value->GetName() ? value->GetName() : "")) m_OpenAttributeValue = true;
    }
    ImGui::EndDisabled();
}

// Kinematic Chain Setup (CKKinematicChain): its start and end effectors (body
// parts, the end one below the start one), the chain between them.
void SetupWindows::KinematicChainPage(CKKinematicChain *chain) {
    if (!chain) return;
    const CK_ID id = chain->GetID();
    ImGui::BeginGroup();
    Group("Kinematic Chain");
    CK_ID chosen = 0;
    ImGui::TextUnformatted("Start Effector");
    ImGui::SameLine(110);
    if (ObjectCombo("##start", CKCID_BODYPART, chain->GetStartEffector(), &chosen)) {
        Live(id, "Start Effector", [chosen](CKObject *o) {
            CKKinematicChain::Cast(o)->SetStartEffector(CKBodyPart::Cast(o->GetCKContext()->GetObject(chosen)));
        });
        FlushPending();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!chain->GetStartEffector());
    if (ImGui::SmallButton(">##startsetup") && chain->GetStartEffector())
        m_State.events.Post(ShowObjectSetup{chain->GetStartEffector()->GetID()});
    ImGui::EndDisabled();
    ImGui::TextUnformatted("End Effector");
    ImGui::SameLine(110);
    if (ObjectCombo("##end", CKCID_BODYPART, chain->GetEndEffector(), &chosen)) {
        Live(id, "End Effector", [chosen](CKObject *o) {
            CKKinematicChain::Cast(o)->SetEndEffector(CKBodyPart::Cast(o->GetCKContext()->GetObject(chosen)));
        });
        FlushPending();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!chain->GetEndEffector());
    if (ImGui::SmallButton(">##endsetup") && chain->GetEndEffector())
        m_State.events.Post(ShowObjectSetup{chain->GetEndEffector()->GetID()});
    ImGui::EndDisabled();
    const bool complete = chain->GetStartEffector() && chain->GetEndEffector();
    const int bodies = complete ? chain->GetChainBodyCount() : 0;
    ImGui::Dummy(ImVec2(1, 6));
    ImGui::Text("Chain Length:   %s", complete ? std::to_string(chain->GetChainLength()).c_str() : "-");
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
            CKBodyPart *part = chain->GetEffector(i);
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", i);
            ImGui::TableNextColumn();
            if (typeAhead.Selectable(NameOf(part).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && part &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                m_State.events.Post(ShowObjectSetup{part->GetID()});
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndGroup();
    ImGui::Separator();
    ParameterUsers(chain);
}

// Layer Setup (CKLayer): its grid, type and visibility, its squares' values
// (painted in the Grid Setup).
void SetupWindows::LayerPage(CKLayer *layer) {
    if (!layer) return;
    CKContext *context = m_State.document->Context();
    CKGridManager *manager = static_cast<CKGridManager *>(context->GetManagerByGuid(GRID_MANAGER_GUID));
    CKGrid *grid = CKGrid::Cast(context->GetObject(layer->GetOwner()));
    const int width = grid ? grid->GetWidth() : 0, length = grid ? grid->GetLength() : 0;
    VxColor color(1.0f, 1.0f, 0.0f, 1.0f);
    if (manager) manager->GetAssociatedColor(layer->GetType(), &color);
    ImGui::BeginGroup();
    Group("Layer");
    ImGui::TextUnformatted("Grid");
    ImGui::SameLine(110);
    ImGui::BeginDisabled(!grid);
    if (ImGui::SmallButton(">") && grid) {
        m_State.events.Post(ShowObjectSetup{grid->GetID()});
        m_Doc.gridLayer = layer->GetID();
    }
    ImGui::EndDisabled();
    if (grid && ImGui::IsItemHovered()) ImGui::SetTooltip("Grid Setup");
    ImGui::SameLine();
    ImGui::TextUnformatted(NameOf(grid).c_str());
    ImGui::TextUnformatted("Type");
    ImGui::SameLine(110);
    const ImVec2 swatch = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(swatch.x, swatch.y + 1), ImVec2(swatch.x + 20, swatch.y + 14),
                                              ToImColor(color));
    ImGui::Dummy(ImVec2(24, 15));
    ImGui::SameLine();
    const char *type = manager ? manager->GetTypeName(layer->GetType()) : nullptr;
    ImGui::Text("%s  (%d)", type ? AnsiToUtf8(type).c_str() : "?", layer->GetType());
    ImGui::TextUnformatted("Format");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(layer->GetFormat() == CKGRID_LAYER_FORMAT_NORMAL ? "Normal" : std::to_string(layer->GetFormat()).c_str());
    bool visible = layer->IsVisible() != 0;
    if (ImGui::Checkbox("Visible", &visible))
        LiveSet(layer->GetID(), properties::LayerVisible, visible);
    // The values: how many squares have one, their range.
    int filled = 0, low = 255, high = 0;
    for (int y = 0; y < length; ++y)
        for (int x = 0; x < width; ++x) {
            int value = 0;
            layer->GetValue2(x, y, &value);
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
    dl->AddRectFilled(at, ImVec2(at.x + side + 16, at.y + side + 16), IM_COL32(128, 128, 128, 255));
    const ImVec2 origin(at.x + 8, at.y + 8);
    for (int y = 0; y < length; ++y)
        for (int x = 0; x < width; ++x) {
            int value = 0;
            layer->GetValue2(x, y, &value);
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
            layer->GetValue2(x, y, &value);
            ImGui::SetTooltip("X : %d    Y : %d    Value : %d", x, y, value);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + side + 20));
    ImGui::Dummy(ImVec2(side + 16, 1));
    ImGui::EndGroup();
}

// Local and output parameters (CKParameterLocal, CKParameterOut): type,
// owner, value; an output lists its destinations.
void SetupWindows::ParameterPage(CKParameter *parameter) {
    if (!parameter) return;
    CKContext *context = m_State.document->Context();
    CKParameterLocal *local = CKParameterLocal::Cast(parameter);
    CKParameterOut *output = CKParameterOut::Cast(parameter);
    CKBehavior *behavior = BehaviorOf(parameter);
    bool setting = false;
    if (local && behavior)
        for (int i = 0; i < behavior->GetLocalParameterCount(); ++i)
            if (behavior->GetLocalParameter(i) == local) setting = behavior->IsLocalParameterSetting(i) != 0;
    ImGui::BeginGroup();
    Group(local ? (local->IsMyselfParameter() ? "This Parameter" : setting ? "Setting" : "Local Parameter")
                : output ? "Output Parameter" : "Parameter");
    ImGui::TextUnformatted("Type");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(TypeName(context, parameter->GetType()).c_str());
    SetupLink("Owner", parameter->GetOwner());
    if (behavior && behavior != parameter->GetOwner()) SetupLink("Behavior", behavior);
    ImGui::Dummy(ImVec2(1, 6));
    // The value of a This parameter is its owner (CKParameterLocal::SetOwner).
    ParameterValue(parameter, !(local && local->IsMyselfParameter()));
    ImGui::Dummy(ImVec2(1, 6));
    ShowInSchematic(behavior);
    ImGui::EndGroup();
    if (output) {
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
            for (int i = 0; i < output->GetDestinationCount(); ++i) {
                CKParameter *destination = output->GetDestination(i);
                if (!destination) continue;
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (typeAhead.Selectable(NameOf(destination).c_str(), false,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    m_State.events.Post(ShowObjectSetup{destination->GetID()});
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(NameOf(destination->GetOwner()).c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndGroup();
    }
    AttributeValueDialog();
}

// Input parameters (CKParameterIn): type, owner, where the value comes from.
void SetupWindows::ParameterInPage(CKParameterIn *input) {
    if (!input) return;
    CKContext *context = m_State.document->Context();
    CKBehavior *behavior = BehaviorOf(input);
    ImGui::BeginGroup();
    Group(input->IsEnabled() ? "Input Parameter" : "Input Parameter (disabled)");
    ImGui::TextUnformatted("Type");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(TypeName(context, input->GetType()).c_str());
    SetupLink("Owner", input->GetOwner());
    if (behavior && behavior != input->GetOwner()) SetupLink("Behavior", behavior);
    ImGui::Dummy(ImVec2(1, 6));
    Group("Source");
    CKParameter *direct = input->GetDirectSource();
    CKParameterIn *shared = input->GetSharedSource();
    CKParameter *real = input->GetRealSource();
    SetupLink("Linked to", direct);
    SetupLink("Shares with", shared);
    if (real && real != direct) SetupLink("Real source", real);
    ImGui::Dummy(ImVec2(1, 6));
    // The value is the source's; only a local of its own (Dev's value
    // holder) is edited from here.
    ParameterValue(real, EditableValueOf(input) != nullptr);
    ImGui::Dummy(ImVec2(1, 6));
    ShowInSchematic(behavior);
    ImGui::EndGroup();
    AttributeValueDialog();
}

// Parameter Operation Setup (CKParameterOperation): the operation, its two
// inputs and its output.
void SetupWindows::OperationPage(CKParameterOperation *operation) {
    if (!operation) return;
    CKContext *context = m_State.document->Context();
    CKParameterManager *manager = context->GetParameterManager();
    ImGui::BeginGroup();
    Group("Parameter Operation");
    const char *name = manager->OperationGuidToName(operation->GetOperationGuid());
    ImGui::TextUnformatted("Operation");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(name && *name ? AnsiToUtf8(name).c_str() : "?");
    ImGui::TextUnformatted("GUID");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(GuidText(operation->GetOperationGuid()).c_str());
    SetupLink("Owner", operation->GetOwner());
    ImGui::Dummy(ImVec2(1, 6));
    CKBehavior *owner = operation->GetOwner();
    ShowInSchematic(owner, owner ? ElementRef{ElementKind::Operation, owner->GetID(), operation->GetID()} : ElementRef());
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
        auto row = [&](const char *label, CKObject *pin, CKParameterType type, CKParameter *value) {
            ImGui::PushID(label);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            if (typeAhead.Selectable(NameOf(pin).c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
                pin && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                m_State.events.Post(ShowObjectSetup{pin->GetID()});
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(pin ? TypeName(context, type).c_str() : "");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value ? ValueText(value).c_str() : "");
            ImGui::PopID();
        };
        CKParameterIn *a = operation->GetInParameter1(), *b = operation->GetInParameter2();
        CKParameterOut *result = operation->GetOutParameter();
        row("Input 1", a, a ? a->GetType() : 0, a ? a->GetRealSource() : nullptr);
        row("Input 2", b, b ? b->GetType() : 0, b ? b->GetRealSource() : nullptr);
        row("Output", result, result ? result->GetType() : 0, result);
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Double-click a parameter to show its Setup.");
    ImGui::EndGroup();
}

// Script, Behavior Graph and Building Block Setup (CKBehavior): what it is,
// where it is, its priority, flags and contents.
void SetupWindows::BehaviorPage(CKBehavior *behavior) {
    if (!behavior) return;
    CKContext *context = m_State.document->Context();
    CKBehavior *script = ScriptOf(behavior);
    const bool graph = behavior->GetType() != CKBEHAVIORTYPE_BASE;
    ImGui::BeginGroup();
    Group(behavior->GetType() == CKBEHAVIORTYPE_SCRIPT ? "Script" : graph ? "Behavior Graph" : "Building Block");
    if (!graph || behavior->GetPrototypeGuid() != CKGUID()) {
        const char *prototype = behavior->GetPrototypeName();
        ImGui::TextUnformatted("Prototype");
        ImGui::SameLine(110);
        ImGui::TextUnformatted(prototype && *prototype ? AnsiToUtf8(prototype).c_str() : "-");
        ImGui::TextUnformatted("GUID");
        ImGui::SameLine(110);
        ImGui::TextUnformatted(GuidText(behavior->GetPrototypeGuid()).c_str());
        ImGui::TextUnformatted("Version");
        ImGui::SameLine(110);
        ImGui::Text("0x%08x", static_cast<unsigned>(behavior->GetVersion()));
    }
    SetupLink("Owner", behavior->GetOwner());
    if (behavior != script) {
        SetupLink("Script", script);
        if (behavior->GetParent() != script) SetupLink("Parent", behavior->GetParent());
    }
    // Priority (Dev's Edit Priority, Y): one undo step, in the Script.
    ImGui::TextUnformatted("Priority");
    ImGui::SameLine(110);
    int priority = behavior->GetPriority();
    const bool editable = script && m_State.document->FindScript(script->GetID());
    ImGui::BeginDisabled(!editable);
    ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("##priority", &priority, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue) && editable) {
        const EditResult result =
            SetBehaviorPriority(*m_State.document, script->GetID(), behavior->GetID(), priority);
        if (!result.ok && !result.Unchanged()) Report(result, "Set Priority");
    }
    ImGui::EndDisabled();
    ImGui::TextUnformatted("Active");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(behavior->IsActive() ? "Yes" : "No");
    if (behavior->IsUsingTarget()) {
        CKParameterIn *target = behavior->GetTargetParameter();
        SetupLink("Target", target);
    }
    const char *compatible = CKClassIDToString(behavior->GetCompatibleClassID());
    ImGui::TextUnformatted("Applies to");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(compatible ? compatible : "?");
    ImGui::Dummy(ImVec2(1, 6));
    ShowInSchematic(behavior);
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
    const CKDWORD flags = behavior->GetFlags();
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
    ImGui::Text("Inputs:                %d", behavior->GetInputCount());
    ImGui::Text("Outputs:               %d", behavior->GetOutputCount());
    ImGui::Text("Input Parameters:      %d", behavior->GetInputParameterCount());
    ImGui::Text("Output Parameters:     %d", behavior->GetOutputParameterCount());
    ImGui::Text("Local Parameters:      %d", behavior->GetLocalParameterCount());
    if (graph) {
        ImGui::Text("Behaviors:             %d", behavior->GetSubBehaviorCount());
        ImGui::Text("Behavior Links:        %d", behavior->GetSubBehaviorLinkCount());
        ImGui::Text("Parameter Operations:  %d", behavior->GetParameterOperationCount());
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
            for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i) {
                CKBehavior *child = behavior->GetSubBehavior(i);
                if (!child) continue;
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (typeAhead.Selectable(NameOf(child).c_str(), false,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    m_State.events.Post(ShowObjectSetup{child->GetID()});
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(child->GetType() == CKBEHAVIORTYPE_BASE ? "Building Block" : "Behavior Graph");
                ImGui::TableNextColumn();
                ImGui::Text("%d", child->GetPriority());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndGroup();
}

// Behavior IO Setup (CKBehaviorIO): an input or output of a behavior and the
// links starting or ending at it.
void SetupWindows::BehaviorIOPage(CKBehaviorIO *io) {
    if (!io) return;
    CKBehavior *owner = io->GetOwner();
    const bool input = (io->GetType() & CK_BEHAVIORIO_IN) != 0;
    ImGui::BeginGroup();
    Group(input ? "Behavior Input" : "Behavior Output");
    SetupLink("Owner", owner);
    ImGui::TextUnformatted("Active");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(io->IsActive() ? "Yes" : "No");
    ImGui::Dummy(ImVec2(1, 6));
    ShowInSchematic(owner);
    ImGui::EndGroup();
    // The links are those of the owner's graph and, inside a graph, its own.
    std::vector<CKBehaviorLink *> links;
    for (CKBehavior *graph : {owner ? owner->GetParent() : nullptr, owner}) {
        if (!graph || graph->GetType() == CKBEHAVIORTYPE_BASE) continue;
        for (int i = 0; i < graph->GetSubBehaviorLinkCount(); ++i)
            if (CKBehaviorLink *link = graph->GetSubBehaviorLink(i))
                if ((link->GetInBehaviorIO() == io || link->GetOutBehaviorIO() == io) &&
                    std::find(links.begin(), links.end(), link) == links.end())
                    links.push_back(link);
    }
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
        for (CKBehaviorLink *link : links) {
            CKBehaviorIO *from = link->GetInBehaviorIO(), *to = link->GetOutBehaviorIO();
            ImGui::PushID(static_cast<int>(link->GetID()));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (typeAhead.Selectable(OwnedName(from, from ? from->GetOwner() : nullptr).c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                m_State.events.Post(ShowObjectSetup{link->GetID()});
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(OwnedName(to, to ? to->GetOwner() : nullptr).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", link->GetInitialActivationDelay());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndGroup();
}

// Behavior Link Setup (CKBehaviorLink): its ends and its delay (Dev's Link
// Delay, in frames).
void SetupWindows::BehaviorLinkPage(CKBehaviorLink *link) {
    if (!link) return;
    CKBehaviorIO *from = link->GetInBehaviorIO(), *to = link->GetOutBehaviorIO();
    CKBehavior *start = from ? from->GetOwner() : nullptr;
    CKBehavior *script = ScriptOf(start ? start : to ? to->GetOwner() : nullptr);
    ImGui::BeginGroup();
    Group("Behavior Link");
    SetupLink("From", from);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", start ? ("of " + NameOf(start)).c_str() : "");
    SetupLink("To", to);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", to && to->GetOwner() ? ("of " + NameOf(to->GetOwner())).c_str() : "");
    ImGui::Dummy(ImVec2(1, 6));
    ImGui::TextUnformatted("Delay");
    ImGui::SameLine(110);
    int delay = link->GetInitialActivationDelay();
    const bool editable = script && m_State.document->FindScript(script->GetID());
    ImGui::BeginDisabled(!editable);
    ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("##delay", &delay, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue) && editable) {
        const EditResult result = SetLinkDelay(*m_State.document, script->GetID(), link->GetID(), delay);
        if (!result.ok && !result.Unchanged()) Report(result, "Set Link Delay");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted("frames");
    ImGui::TextUnformatted("Current Delay");
    ImGui::SameLine(110);
    ImGui::Text("%d", link->GetActivationDelay());
    ImGui::Dummy(ImVec2(1, 6));
    if (start) ShowInSchematic(start);
    ImGui::EndGroup();
}

// Synchro Setup (CKSynchroObject, the Rendez Vous building block's object):
// how many objects the rendez-vous waits for, those arrived.
void SetupWindows::SynchroPage(CKSynchroObject *synchro) {
    if (!synchro) return;
    const CK_ID id = synchro->GetID();
    ImGui::BeginGroup();
    Group("Rendez-Vous");
    ImGui::TextUnformatted("Number Of Users");
    ImGui::SameLine(160);
    int users = synchro->GetRendezVousNumberOfWaiters();
    ImGui::SetNextItemWidth(90);
    if (ImGui::DragInt("##users", &users, 0.1f, 0, 10000))
        LiveSet(id, properties::SynchroUsers, users);
    const int arrived = synchro->GetRendezVousNumberOfArrivedObjects();
    ImGui::Text("Arrived:  %d", arrived);
    if (ImGui::Button("Reset", ImVec2(90, 0))) synchro->Reset();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forgets the objects arrived");
    ImGui::EndGroup();
    ImGui::SameLine(330);
    ImGui::BeginGroup();
    Group("Arrived Objects");
    if (ImGui::BeginChild("arrived", ImVec2(280, 120), ImGuiChildFlags_Borders)) {
        TypeAhead typeAhead;
        for (int i = 0; i < arrived; ++i) {
            CKBeObject *object = synchro->GetRendezVousArrivedObject(i);
            ImGui::PushID(i);
            if (typeAhead.Selectable(NameOf(object).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && object &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                m_State.events.Post(ShowObjectSetup{object->GetID()});
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::Separator();
    ParameterUsers(synchro);
}

// State Setup (CKStateObject, Enter / Leave State and Is In State): whether
// the state is entered.
void SetupWindows::StatePage(CKStateObject *state) {
    if (!state) return;
    Group("State");
    bool active = state->IsStateActive() != 0;
    if (ImGui::Checkbox("Active (entered)", &active))
        LiveSet(state->GetID(), properties::StateActive, active);
    ImGui::Separator();
    ParameterUsers(state);
}

// Critical Section Setup (CKCriticalSectionObject, Enter / Leave Critical
// Section): CK does not tell which object is inside; Reset lets it go.
void SetupWindows::CriticalSectionPage(CKCriticalSectionObject *section) {
    if (!section) return;
    Group("Critical Section");
    if (ImGui::Button("Reset", ImVec2(90, 0))) section->Reset();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lets the object inside the critical section go");
    ImGui::Separator();
    ParameterUsers(section);
}

// Interface Objects Setup (CKInterfaceObjectManager): the editor data an
// interface stored in the composition, by its GUID.
void SetupWindows::InterfaceObjectsPage(CKInterfaceObjectManager *manager) {
    if (!manager) return;
    Group("Interface Objects");
    ImGui::TextUnformatted("GUID");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(GuidText(manager->GetGuid()).c_str());
    ImGui::TextUnformatted("Chunks");
    ImGui::SameLine(110);
    ImGui::Text("%d", manager->GetChunkCount());
    for (int i = 0; i < manager->GetChunkCount(); ++i)
        if (CKStateChunk *chunk = manager->GetChunk(i))
            ImGui::BulletText("Chunk %d: %d bytes", i, chunk->GetDataSize());
}

void SetupWindows::OtherPage(CKObject *object) {
    const char *cls = CKClassIDToString(object->GetClassID());
    ImGui::Text("%s  (%s, ID %u)", NameOf(object).c_str(), cls ? cls : "?", static_cast<unsigned>(object->GetID()));
    ImGui::TextDisabled("This class has no specific Setup page; see its attributes.");
}

// Dev Attribute page: the object's attributes (category, value), Add / Remove;
// rows selected with a click (Ctrl: added or taken off), their menu (Dev.exe
// sub_4436E0: Delete, Copy Attribute Parameter Value; elsewhere Paste
// Attributes, sub_448E40).
void SetupWindows::AttributePage(Setup &setup, CKObject *object) {
    CKBeObject *beObject = CKBeObject::Cast(object);
    if (!beObject) {
        ImGui::TextDisabled("Only behavioral objects have attributes.");
        return;
    }
    CKContext *context = m_State.document->Context();
    CKAttributeManager *attributes = context->GetAttributeManager();
    if (!attributes) return;
    const CK_ID id = object->GetID();
    const float listHeight = std::max(80.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - 4);
    // Documentation "Attribute Mode": sorted by Name or Category (a click on
    // the header), the selected ones removed with DELETE.
    if (!ImGui::BeginTable("attributes", 4,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable |
                               ImGuiTableFlags_SortTristate,
                           ImVec2(0, listHeight))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 24);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 260);
    ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 180);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_NoSort);
    ImGui::TableHeadersRow();
    std::vector<int> order(static_cast<size_t>(beObject->GetAttributeCount()));
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    if (ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs())
        if (specs->SpecsCount > 0) {
            const bool byName = specs->Specs[0].ColumnIndex == 1;
            const bool ascending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            auto key = [&](int i) {
                const CKAttributeType type = beObject->GetAttributeType(i);
                const char *text = byName ? attributes->GetAttributeNameByType(type) : attributes->GetAttributeCategory(type);
                return std::string(text ? text : "");
            };
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                const int compared = _stricmp(key(a).c_str(), key(b).c_str());
                return ascending ? compared < 0 : compared > 0;
            });
        }
    for (auto it = setup.attributeRows.begin(); it != setup.attributeRows.end();)
        it = beObject->HasAttribute(*it) ? std::next(it) : setup.attributeRows.erase(it);
    auto removeSelected = [&] {
        FlushPending();
        Report(RemoveAttributes(*m_State.document, id, std::vector<CKAttributeType>(setup.attributeRows.begin(), setup.attributeRows.end())),
               "Delete");
        setup.attributeRows.clear();
    };
    auto copySelected = [&] {
        std::vector<CK_ID> values;
        std::vector<std::string> names;
        for (CKAttributeType type : setup.attributeRows)
            if (CKParameterOut *value = beObject->GetAttributeParameter(type)) {
                const char *name = attributes->GetAttributeNameByType(type);
                values.push_back(value->GetID());
                names.push_back(name ? name : "");
            }
        m_State.clipboard.SetParameters(*m_State.document, std::move(values), std::move(names));
    };
    for (int i : order) {
        const CKAttributeType type = beObject->GetAttributeType(i);
        ImGui::PushID(type);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("x")) {
            const EditResult result = RemoveAttribute(*m_State.document, id, type);
            if (!result.ok) m_State.Log(LogLevel::Warning, "Remove Attribute: " + AnsiToUtf8(result.message));
        }
        ImGui::TableNextColumn();
        const char *name = attributes->GetAttributeNameByType(type);
        const bool selected = setup.attributeRows.count(type) != 0;
        if (ImGui::Selectable(AnsiToUtf8(name ? name : "?").c_str(), selected)) {
            if (!ImGui::GetIO().KeyCtrl) setup.attributeRows = {type};
            else if (selected) setup.attributeRows.erase(type);
            else setup.attributeRows.insert(type);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) setup.attributeRows = {type};
        if (ImGui::BeginPopupContextItem("attributemenu")) {
            if (ImGui::MenuItem("Delete")) removeSelected();
            if (ImGui::MenuItem("Copy Attribute Parameter Value")) copySelected();
            ImGui::EndPopup();
        }
        ImGui::TableNextColumn();
        const char *category = attributes->GetAttributeCategory(type);
        ImGui::TextUnformatted(AnsiToUtf8(category ? category : "").c_str());
        ImGui::TableNextColumn();
        if (CKParameterOut *value = beObject->GetAttributeParameterByIndex(i)) {
            // The value's text (Dev.exe sub_44D4C0); a double click edits it
            // in Dev's Edit Parameter (sub_4436E0: name hidden, type shown).
            ImGui::Selectable(ParameterValueText(value).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                CKParameterManager *manager = context->GetParameterManager();
                const char *typeName = manager->ParameterTypeToName(value->GetType());
                m_AttributeType = typeName ? typeName : "";
                m_Doc.valueScript = 0;
                m_AttributeBox.Clear();
                if (m_AttributeBox.Add(value, value->GetName() ? value->GetName() : (name ? name : "")))
                    m_OpenAttributeValue = true;
            }
        }
        ImGui::PopID();
    }
    // Elsewhere in the list: Paste Attributes, once attributes were copied.
    if (!m_State.clipboard.attributes.empty() &&
        ImGui::BeginPopupContextWindow("attributepaste", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Paste Attributes", "Ctrl+V")) {
            FlushPending();
            Report(PasteAttributes(*m_State.document, id, m_State.clipboard.attributes, m_State.clipboard.parameters),
                   "Paste Attributes");
        }
        ImGui::EndPopup();
    }
    ImGui::EndTable();
    if (!setup.attributeRows.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        removeSelected();
    if (!m_State.clipboard.attributes.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        FlushPending();
        Report(PasteAttributes(*m_State.document, id, m_State.clipboard.attributes, m_State.clipboard.parameters),
               "Paste Attributes");
    }
    // Dev: "Add Attribute" opens the attribute tree (Add Selected, More>>).
    if (ImGui::Button("Add Attribute")) m_State.events.Post(OpenAddAttribute{{id}});
    AttributeValueDialog();
}

// OK writes the value (the parameter box's undo steps, one "Edit Attribute").
void SetupWindows::AttributeValueDialog() {
    const char *id = "Edit Parameter###AttributeValue";
    if (m_OpenAttributeValue) {
        ImGui::OpenPopup(id);
        m_OpenAttributeValue = false;
    }
    const DialogAction action = EditParameterValueDialog(id, "Edit Parameter", m_AttributeBox, AnsiToUtf8(m_AttributeType).c_str());
    if (action == DialogAction::Ok && m_State.HasDocument()) {
        EditGroup group(*m_State.document, "Edit Attribute");
        m_AttributeBox.Apply(m_Doc.valueScript);
    }
    if (action != DialogAction::None) m_AttributeBox.Clear();
}

} // namespace vdev
