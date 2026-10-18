#include <filesystem>
// Dev's Setup panels; layout after the Dev 2.5 screenshots the user gave
// (Grid, 2D Frame, 3D Object, Target Camera, Point Light, Material, Mesh,
// Texture, Curve, Character, Sound, Array and Group Setup). Values are read from CK every frame.

#include "ui/setup/SetupWindows.h"
#include "core/runtime/SdkCompat.h"

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
#include "ui/controls/DevTheme.h"
#include "ui/controls/FileDialogs.h"
#include "ui/controls/TypeAhead.h"
#include "core/runtime/GridCompat.h"
#include "core/level/LevelCommands.h"
#include "core/level/ObjectEditCommands.h"
#include "core/level/SceneCommands.h"
#include "ui/setup/SetupWindowsDetail.h"
#include "ui/schematic/Snapshots.h"
#include "core/runtime/TextEncoding.h"
#include "core/graph/ValueCommands.h"

namespace vdev {

using namespace setup;

// ---------------------------------------------------------------- frame

SetupWindows::Kind SetupWindows::KindOf(CKObject *object) {
    if (!object) return Kind::Other;
    const CK_CLASSID cid = object->GetClassID();
    if (cid == CKCID_LEVEL || CKIsChildClassOf(cid, CKCID_SCENE)) return Kind::Scene;
    if (CKIsChildClassOf(cid, CKCID_GRID)) return Kind::Grid;
    if (CKIsChildClassOf(cid, CKCID_CURVE)) return Kind::Curve;
    if (CKIsChildClassOf(cid, CKCID_CHARACTER)) return Kind::Character;
    if (CKIsChildClassOf(cid, CKCID_SPRITE3D)) return Kind::Sprite3D;
    if (CKIsChildClassOf(cid, CKCID_SPRITETEXT)) return Kind::SpriteText;
    if (CKIsChildClassOf(cid, CKCID_SPRITE)) return Kind::Sprite2D;
    if (CKIsChildClassOf(cid, CKCID_ANIMATION)) return Kind::Animation;
    if (CKIsChildClassOf(cid, CKCID_OBJECTANIMATION)) return Kind::ObjectAnimation;
    if (CKIsChildClassOf(cid, CKCID_MIDISOUND)) return Kind::MidiSound;
    if (CKIsChildClassOf(cid, CKCID_CAMERA)) return Kind::Camera;
    if (CKIsChildClassOf(cid, CKCID_LIGHT)) return Kind::Light;
    if (CKIsChildClassOf(cid, CKCID_3DENTITY)) return Kind::Entity3D;
    if (CKIsChildClassOf(cid, CKCID_2DENTITY)) return Kind::Entity2D;
    if (CKIsChildClassOf(cid, CKCID_MATERIAL)) return Kind::Material;
    if (CKIsChildClassOf(cid, CKCID_MESH)) return Kind::Mesh;
    if (CKIsChildClassOf(cid, CKCID_TEXTURE)) return Kind::Texture;
    if (CKIsChildClassOf(cid, CKCID_WAVESOUND)) return Kind::Sound;
    if (CKIsChildClassOf(cid, CKCID_DATAARRAY)) return Kind::Array;
    if (CKIsChildClassOf(cid, CKCID_GROUP)) return Kind::Group;
    if (CKIsChildClassOf(cid, CKCID_KINEMATICCHAIN)) return Kind::KinematicChain;
    if (CKIsChildClassOf(cid, CKCID_LAYER)) return Kind::Layer;
    if (CKIsChildClassOf(cid, CKCID_PARAMETER)) return Kind::Parameter;
    if (CKIsChildClassOf(cid, CKCID_PARAMETERIN)) return Kind::ParameterIn;
    if (CKIsChildClassOf(cid, CKCID_PARAMETEROPERATION)) return Kind::Operation;
    if (CKIsChildClassOf(cid, CKCID_BEHAVIOR)) return Kind::Behavior;
    if (CKIsChildClassOf(cid, CKCID_BEHAVIORIO)) return Kind::BehaviorIO;
    if (CKIsChildClassOf(cid, CKCID_BEHAVIORLINK)) return Kind::BehaviorLink;
    if (CKIsChildClassOf(cid, CKCID_SYNCHRO)) return Kind::Synchro;
    if (CKIsChildClassOf(cid, CKCID_STATE)) return Kind::State;
    if (CKIsChildClassOf(cid, CKCID_CRITICALSECTION)) return Kind::CriticalSection;
    if (CKIsChildClassOf(cid, CKCID_INTERFACEOBJECTMANAGER)) return Kind::InterfaceObjects;
    return Kind::Other;
}

std::string SetupWindows::TitleOf(const Setup &setup, CKObject *object) const {
    // Dev titles: "3D Object Setup", "Target Camera Setup", "Point Light Setup", ...
    if (CKLight *light = CKLight::Cast(object)) {
        const VXLIGHT_TYPE type = light->GetType();
        const char *kind = type == VX_LIGHTSPOT ? "Spot" : type == VX_LIGHTDIREC ? "Directional" : "Point";
        return std::string(CKTargetLight::Cast(object) ? "Target " : "") + kind + " Light Setup";
    }
    const CK_CLASSID cid = object ? object->GetClassID() : 0;
    if (cid == CKCID_3DENTITY && (CK3dEntity::Cast(object)->GetFlags() & CK_3DENTITY_PORTAL)) return "Portal Setup";
    if (IsFrame(object)) return "3D Frame Setup";
    if (CKBehavior *behavior = CKBehavior::Cast(object))
        return behavior->GetType() == CKBEHAVIORTYPE_SCRIPT     ? "Script Setup"
               : behavior->GetType() == CKBEHAVIORTYPE_BEHAVIOR ? "Behavior Graph Setup"
                                                                : "Building Block Setup";
    if (CKParameterLocal *local = CKParameterLocal::Cast(object))
        return local->IsMyselfParameter() ? "This Parameter Setup" : "Local Parameter Setup";
    switch (cid) {
    case CKCID_PLACE: return "Place Setup";
    case CKCID_BODYPART: return "Body Part Setup";
    case CKCID_3DOBJECT:
    case CKCID_3DENTITY: return "3D Object Setup";
    case CKCID_2DENTITY: return "2D Frame Setup";
    case CKCID_TARGETCAMERA: return "Target Camera Setup";
    case CKCID_CAMERA: return "Camera Setup";
    case CKCID_CURVE: return "Curve Setup";
    case CKCID_CHARACTER: return "Character Setup";
    case CKCID_WAVESOUND: return "Sound Setup";
    case CKCID_DATAARRAY: return "Array Setup";
    case CKCID_GROUP: return "Group Setup";
    case CKCID_LEVEL: return "Level Setup";
    case CKCID_SPRITE3D: return "3D Sprite Setup";
    case CKCID_SPRITE: return "2D Sprite Setup";
    case CKCID_SCENE: return "Scene Setup";
    case CKCID_SPRITETEXT: return "Sprite Text Setup";
    case CKCID_ANIMATION: return "Animation Setup";
    case CKCID_KEYEDANIMATION: return "Keyed Animation Setup";
    case CKCID_OBJECTANIMATION: return "Object Animation Setup";
    case CKCID_MIDISOUND: return "Midi Sound Setup";
    case CKCID_KINEMATICCHAIN: return "Kinematic Chain Setup";
    case CKCID_LAYER: return "Layer Setup";
    case CKCID_PARAMETEROUT: return "Output Parameter Setup";
    case CKCID_PARAMETER: return "Parameter Setup";
    case CKCID_PARAMETERIN: return "Input Parameter Setup";
    case CKCID_PARAMETEROPERATION: return "Parameter Operation Setup";
    case CKCID_BEHAVIORIO: return "Behavior IO Setup";
    case CKCID_BEHAVIORLINK: return "Behavior Link Setup";
    case CKCID_SYNCHRO: return "Synchro Setup";
    case CKCID_STATE: return "State Setup";
    case CKCID_CRITICALSECTION: return "Critical Section Setup";
    case CKCID_INTERFACEOBJECTMANAGER: return "Interface Objects Setup";
    default: break;
    }
    const char *name = cid ? CKClassIDToString(cid) : nullptr;
    (void)setup;
    return std::string(name ? name : "Object") + " Setup";
}

void SetupWindows::Open(CK_ID id) {
    CKObject *object = m_State.HasDocument() ? m_State.document->Context()->GetObject(id) : nullptr;
    if (!object) return;
    // Dev (sub_4B0010): a curve point opens the Setup of its curve, the
    // target of a target camera or light the Setup of that camera or light.
    int point = -1;
    if (CKCurvePoint *curvePoint = CKCurvePoint::Cast(object))
        if (CKCurve *curve = curvePoint->GetCurve()) {
            for (int i = 0; i < curve->GetControlPointCount(); ++i)
                if (curve->GetControlPoint(i) == curvePoint) point = i;
            object = curve;
        }
    if (CK3dEntity *entity = CK3dEntity::Cast(object)) {
        const CKDWORD flags = entity->GetFlags();
        const CK_CLASSID owner = (flags & CK_3DENTITY_TARGETCAMERA) ? CKCID_TARGETCAMERA
                                 : (flags & CK_3DENTITY_TARGETLIGHT) ? CKCID_TARGETLIGHT : 0;
        const XObjectPointerArray &list = owner ? m_State.document->Context()->GetObjectListByType(owner, TRUE)
                                                : XObjectPointerArray();
        for (int i = 0; i < list.Size(); ++i) {
            CKTargetCamera *camera = CKTargetCamera::Cast(list[i]);
            CKTargetLight *light = CKTargetLight::Cast(list[i]);
            if ((camera && camera->GetTarget() == entity) || (light && light->GetTarget() == entity)) {
                object = list[i];
                break;
            }
        }
    }
    id = object->GetID();
    const Kind kind = KindOf(object);
    // Dev.exe sub_4B0010: the Setup already showing the object, else (Do not
    // open setups of the same type: Show New Setups off) the first Setup of
    // its type shows it; otherwise a new Setup opens.
    // Shift held (Dev: Shift+drag of a Setup's tab) opens an additional one.
    const bool showNew = m_State.preferences.Bool("3D Layout\\Show New Setups") || ImGui::GetIO().KeyShift;
    Setup *shown = nullptr;
    int slot = 0;
    for (Setup &setup : m_Doc.setups)
        if (setup.kind == kind) {
            if (setup.object == id || (!showNew && !shown)) shown = &setup;
            if (setup.object == id) break;
            slot = std::max(slot, setup.slot + 1);
        }
    if (shown) {
        shown->object = id;
        shown->open = true;
        shown->focus = true;
        if (point >= 0) shown->point = point;
        return;
    }
    Setup setup;
    setup.kind = kind;
    setup.object = id;
    setup.slot = slot;
    setup.focus = true;
    setup.attributes = attributesFirst;
    if (point >= 0) setup.point = point;
    m_Doc.setups.push_back(setup);
}

std::vector<SetupWindows::OpenSetup> SetupWindows::OpenSetups() const {
    std::vector<OpenSetup> result;
    if (!m_State.HasDocument()) return result;
    CKContext *context = m_State.document->Context();
    for (const Setup &setup : m_Doc.setups) {
        if (!setup.open || !context->GetObject(setup.object)) continue;
        OpenSetup open;
        open.object = setup.object;
        if (CKCurve *curve = CKCurve::Cast(context->GetObject(setup.object)))
            if (setup.point >= 0 && setup.point < curve->GetControlPointCount())
                if (CKCurvePoint *point = curve->GetControlPoint(setup.point)) open.other = point->GetID();
        open.front = setup.object == m_Doc.front;
        result.push_back(open);
    }
    return result;
}

void SetupWindows::CloseAll() {
    FlushPending();
    m_Doc = DocumentState();
}

const SetupWindows::ShownTransform &SetupWindows::Shown(CK3dEntity *entity) {
    auto found = m_Doc.shown.find(entity->GetID());
    if (found == m_Doc.shown.end() || m_Notify) {
        ShownTransform &shown = m_Doc.shown[entity->GetID()];
        shown.world = entity->GetWorldMatrix();
        shown.local = entity->GetLocalMatrix();
        shown.box = entity->GetBoundingBox(FALSE);
        return shown;
    }
    return found->second;
}

void SetupWindows::Draw(ImGuiID dock) {
    // When the Setups take the transforms (RealTime Notifications).
    const Preferences &prefs = m_State.preferences;
    const bool playing = m_State.debugger && m_State.debugger->GetState() == Debugger::State::Running;
    m_PlayFrames = playing ? m_PlayFrames + 1 : 0;
    if (playing)
        m_Notify = prefs.Bool("3D Layout\\Player RealTime Notifications") &&
                   m_PlayFrames % std::max(1, prefs.Int("3D Layout\\Player RealTime Notifications FrameSkip")) == 0;
    else
        m_Notify = !m_State.layoutTransforming || prefs.Bool("3D Layout\\Author RealTime Notifications");
    // Layer Setup's Grid: the Grid Setup opened selects the layer.
    if (CKLayer *layer = m_Doc.gridLayer ? Get<CKLayer>(m_State, m_Doc.gridLayer) : nullptr) {
        CKGrid *grid = Get<CKGrid>(m_State, layer->GetOwner());
        for (int i = 0; grid && i < grid->GetLayerCount(); ++i)
            if (grid->GetLayerByIndex(i) == layer)
                for (Setup &setup : m_Doc.setups)
                    if (setup.object == grid->GetID()) setup.layer = i;
    }
    m_Doc.gridLayer = 0;
    if (!m_State.HasDocument()) {
        m_Doc.setups.clear();
        return;
    }
    for (Setup &setup : m_Doc.setups)
        if (setup.open) DrawSetup(setup, dock);
    m_Doc.setups.erase(std::remove_if(m_Doc.setups.begin(), m_Doc.setups.end(), [](const Setup &s) { return !s.open; }),
                   m_Doc.setups.end());
    // A gesture ends when no control is held any more.
    if ((m_Pending || m_PendingSceneActive) && !ImGui::IsAnyItemActive()) FlushPending();
    PortalPick();
    SetupPick();
}

void SetupWindows::SetupPick() {
    if (!m_PickSetup) return;
    Setup *picking = nullptr;
    for (Setup &setup : m_Doc.setups)
        if (setup.object == m_PickSetup && setup.slot == m_PickSetupSlot) picking = &setup;
    CKObject *object = picking ? m_State.document->Context()->GetObject(picking->object) : nullptr;
    if (!object || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_PickSetup = 0;
        return;
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetTooltip("Pick an entity");
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    const CK_ID picked = ObjectUnderMouse(m_State, ImGui::GetMousePos(), object->GetClassID());
    if (!picked) return; // a click elsewhere (the picker itself) is not a pick
    FlushPending();
    picking->object = picked;
    m_PickSetup = 0;
}

void SetupWindows::PortalPick() {
    if (!m_PortalPlace) return;
    if (!Get<CKPlace>(m_State, m_PortalPlace) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_PortalPlace = 0;
        return;
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetTooltip("Pick a place");
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    const CK_ID place = m_PortalPlace;
    m_PortalPlace = 0;
    const CK_ID other = ObjectUnderMouse(m_State, ImGui::GetMousePos(), CKCID_PLACE);
    if (!other) {
        m_State.Log(LogLevel::Info, "Automatic Portal: no place picked");
        return;
    }
    FlushPending();
    const EditResult result = CreateAutomaticPortal(*m_State.document, place, other);
    Report(result, "Automatic Portal");
    if (result.ok) {
        CKObject *portal = m_State.document->Context()->GetObject(result.created);
        m_State.Log(LogLevel::Info, std::string("Automatic Portal: ") + AnsiToUtf8(portal && portal->GetName() ? portal->GetName() : ""));
    }
}

void SetupWindows::DrawSetup(Setup &setup, ImGuiID dock) {
    CKObject *object = m_State.document->Context()->GetObject(setup.object);
    if (!object) {
        setup.open = false;
        return;
    }
    static const char *const IDS[] = {"setup3d", "setupcamera", "setuplight", "setup2d", "setupmaterial",
                                      "setupmesh", "setuptexture", "setupgrid", "setupother", "setupcurve",
                                      "setupcharacter", "setupsound", "setuparray", "setupgroup", "setupscene", "setupsprite3d", "setupsprite2d",
                                      "setupanimation", "setupobjectanimation", "setupmidi", "setupspritetext",
                                      "setupchain", "setuplayer", "setupparameter", "setupparameterin", "setupoperation",
                                      "setupbehavior", "setupbehaviorio", "setupbehaviorlink", "setupsynchro", "setupstate",
                                      "setupcriticalsection", "setupinterfaceobjects"};
    // The first Setup of a type keeps the type's window (its place in the layout).
    std::string title = TitleOf(setup, object) + "###" + IDS[static_cast<int>(setup.kind)];
    if (setup.slot) title += "#" + std::to_string(setup.slot);
    if (dock) ImGui::SetNextWindowDockID(dock, ImGuiCond_FirstUseEver);
    if (setup.focus) {
        ImGui::SetNextWindowFocus();
        setup.focus = false;
    }
    if (!BeginDevWindow(title.c_str(), &setup.open)) {
        ImGui::End();
        return;
    }
    // Documentation "Setups": closed with Delete or the tab's "Close Window".
    DevTabClose(&setup.open, "Close Window");
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) m_Doc.front = setup.object;
    Header(setup, object);
    object = m_State.document->Context()->GetObject(setup.object);
    if (!object) {
        ImGui::End();
        return;
    }
    ImGui::PushID(static_cast<int>(setup.kind));
    Sidebar(setup, object);
    ImGui::SameLine();
    ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 5));
    // Dev Group Setup has the Attribute page only.
    if (setup.kind == Kind::Group) setup.attributes = true;
    if (setup.attributes) {
        AttributePage(setup, object);
    } else {
        switch (setup.kind) {
        case Kind::Entity3D:
            // 3D Frame Setup for frames, portals and places, 3D Object Setup for the others.
            if (IsFrame(object)) {
                FramePage(CK3dEntity::Cast(object));
            } else {
                Entity3DPage(CK3dEntity::Cast(object), true, true, nullptr, &setup);
            }
            break;
        case Kind::Sprite3D: Sprite3DPage(CKSprite3D::Cast(object)); break;
        case Kind::Sprite2D: SpritePage(setup, CKSprite::Cast(object)); break;
        case Kind::Curve: CurvePage(setup, CKCurve::Cast(object)); break;
        case Kind::Character: CharacterPage(setup, CKCharacter::Cast(object)); break;
        case Kind::Sound: SoundPage(CKWaveSound::Cast(object)); break;
        case Kind::Array: ArrayPage(setup, CKDataArray::Cast(object)); break;
        case Kind::Scene: ScenePage(object); break;
        case Kind::Camera: CameraPage(CKCamera::Cast(object)); break;
        case Kind::Light: LightPage(CKLight::Cast(object)); break;
        case Kind::Entity2D: Entity2DPage(CK2dEntity::Cast(object)); break;
        case Kind::Material: MaterialPage(setup, CKMaterial::Cast(object)); break;
        case Kind::Mesh: MeshPage(setup, CKMesh::Cast(object)); break;
        case Kind::Texture: TexturePage(setup, CKTexture::Cast(object)); break;
        case Kind::Grid: GridPage(setup, CKGrid::Cast(object)); break;
        case Kind::Animation: AnimationPage(CKAnimation::Cast(object)); break;
        case Kind::ObjectAnimation: ObjectAnimationPage(CKObjectAnimation::Cast(object)); break;
        case Kind::MidiSound: MidiSoundPage(CKMidiSound::Cast(object)); break;
        case Kind::SpriteText: SpriteTextPage(CKSpriteText::Cast(object)); break;
        case Kind::KinematicChain: KinematicChainPage(CKKinematicChain::Cast(object)); break;
        case Kind::Layer: LayerPage(CKLayer::Cast(object)); break;
        case Kind::Parameter: ParameterPage(CKParameter::Cast(object)); break;
        case Kind::ParameterIn: ParameterInPage(CKParameterIn::Cast(object)); break;
        case Kind::Operation: OperationPage(CKParameterOperation::Cast(object)); break;
        case Kind::Behavior: BehaviorPage(CKBehavior::Cast(object)); break;
        case Kind::BehaviorIO: BehaviorIOPage(CKBehaviorIO::Cast(object)); break;
        case Kind::BehaviorLink: BehaviorLinkPage(CKBehaviorLink::Cast(object)); break;
        case Kind::Synchro: SynchroPage(CKSynchroObject::Cast(object)); break;
        case Kind::State: StatePage(CKStateObject::Cast(object)); break;
        case Kind::CriticalSection: CriticalSectionPage(CKCriticalSectionObject::Cast(object)); break;
        case Kind::InterfaceObjects: InterfaceObjectsPage(InterfaceObjectManagerOf(object)); break;
        default: OtherPage(object); break;
        }
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopID();
    // An attribute from the Attribute Manager anywhere in the Setup: on its object.
    if (CKBeObject::Cast(object) && !CKBehavior::Cast(object)) {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (ImGui::BeginDragDropTargetCustom(window->Rect(), window->ID)) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(ATTRIBUTE_PAYLOAD)) {
                AttributePayload attribute;
                std::memcpy(&attribute, payload->Data, sizeof(attribute));
                FlushPending();
                m_State.DropAttributeOn({setup.object}, attribute);
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::End();
}

// Dev's dark strip: the Name list of the objects of the same class.
void SetupWindows::Header(Setup &setup, CKObject *object) {
    const ImVec2 stripMin = ImGui::GetCursorScreenPos();
    const float stripHeight = ImGui::GetFrameHeight() + 6.0f;
    ImGui::GetWindowDrawList()->AddRectFilled(
        stripMin, ImVec2(stripMin.x + ImGui::GetContentRegionAvail().x, stripMin.y + stripHeight), DEV_CHROME);
    PushDarkChrome();
    // Dev.exe sub_442A30: the picker (Pick an entity of the same type) and
    // the Level / Current Scene filter (pressed: the current scene) before
    // the Name combo.
    const float s = ImGui::GetFontSize() / 13.0f;
    CKContext *context = m_State.document->Context();
    static const int PICKER[5] = {719, 721, 720, 720, 719};
    static const int FILTER[5] = {1355, 1356, 1357, 1354, 1357};
    const bool picking = m_PickSetup == setup.object && m_PickSetupSlot == setup.slot;
    if (DevBitmapButton("picker", ImVec2(stripMin.x + 40.0f * s, stripMin.y + 1.0f), PICKER, picking, true, s,
                        "Pick an entity of the same type")) {
        m_PickSetup = picking ? 0 : setup.object;
        m_PickSetupSlot = setup.slot;
    }
    CKLevel *level = context->GetCurrentLevel();
    CKScene *current = context->GetCurrentScene();
    CKSceneObject *sceneObject = CKSceneObject::Cast(object);
    const bool scoped = sceneObject && level && !CKScene::Cast(object) && !CKPlace::Cast(object) && !CKLevel::Cast(object);
    if (DevBitmapButton("scenefilter", ImVec2(stripMin.x + 62.0f * s, stripMin.y + 1.0f), FILTER, setup.sceneFilter, true,
                        s, "Toggle Level/Current Scene Filter")) {
        setup.sceneFilter = !setup.sceneFilter;
        if (setup.sceneFilter && scoped && current && !sceneObject->IsInScene(current)) {
            setup.sceneFilter = false;
            m_State.Log(LogLevel::Warning, "Object not in current scene !");
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(stripMin.x + 86.0f * s, stripMin.y + 3.0f));
    ImGui::TextUnformatted("Name");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##name", NameOf(object).c_str())) {
        // The objects of the scene the filter gives (the level scene when
        // the object is not in the current one), Dev.exe sub_44DD30.
        CKScene *scene = level ? level->GetLevelScene() : nullptr;
        if (scoped && setup.sceneFilter && current && sceneObject->IsInScene(current)) scene = current;
        TypeAhead typeAhead;
        for (CKObject *other : ObjectsOf(*m_State.document, object->GetClassID(), false)) {
            CKSceneObject *candidate = CKSceneObject::Cast(other);
            if (scoped && scene && other != object && candidate && !candidate->IsInScene(scene)) continue;
            if (typeAhead.Selectable(NameOf(other).c_str(), other == object)) setup.object = other->GetID();
        }
        ImGui::EndCombo();
    }
    // F2: the object renamed (the setup's window focused).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) && ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        m_Doc.renameObject = setup.object;
        m_Doc.renameText = NameOf(object);
        m_Doc.openRename = true;
    }
    if (m_Doc.openRename && m_Doc.renameObject == setup.object) {
        m_Doc.openRename = false;
        ImGui::OpenPopup("Rename###setuprename");
    }
    if (ImGui::BeginPopupModal("Rename###setuprename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(260);
        const bool entered = ImGui::InputText("##name", &m_Doc.renameText, ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("OK", ImVec2(80, 0)) || entered) {
            FlushPending();
            Report(RenameLevelObject(*m_State.document, m_Doc.renameObject, Utf8ToAnsi(m_Doc.renameText)), "Rename");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // The file (documentation: "The object can be replaced by clicking on
    // the folder"): the slot's image, the sound's file.
    if (CKTexture *texture = CKTexture::Cast(object)) {
        ImGui::SameLine();
        ImGui::TextUnformatted("   Filename");
        ImGui::SameLine();
        const char *file = texture->GetSlotFileName(texture->GetCurrentSlot());
        ImGui::TextUnformatted(file && *file ? AnsiToUtf8(file).c_str() : "-");
        ImGui::SameLine();
        if (ImGui::SmallButton("...##texturefile")) RunSlotCommand(setup, texture, SlotCommand::Replace);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Replace the slot's image");
    }
    // A 2D sprite's image file, as a texture's (documentation "Setups":
    // "Textures, Sprite or Sounds").
    CKSprite *sprite = CKSpriteText::Cast(object) ? nullptr : CKSprite::Cast(object);
    if (sprite) {
        ImGui::SameLine();
        ImGui::TextUnformatted("   Filename");
        ImGui::SameLine();
        const char *file = sprite->GetSlotFileName(sprite->GetCurrentSlot());
        ImGui::TextUnformatted(file && *file ? AnsiToUtf8(file).c_str() : "-");
        ImGui::SameLine();
        if (ImGui::SmallButton("...##spritefile")) RunSlotCommand(setup, sprite, SlotCommand::Replace);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Replace the slot's image");
    }
    if (CKWaveSound *sound = CKWaveSound::Cast(object)) {
        ImGui::SameLine();
        ImGui::TextUnformatted("   Filename");
        ImGui::SameLine();
        const char *file = sound->GetSoundFileName();
        ImGui::TextUnformatted(file && *file ? AnsiToUtf8(file).c_str() : "-");
        ImGui::SameLine();
        if (ImGui::SmallButton("...##soundfile")) {
            FileDialogOptions options;
            options.filter = L"Wave files (*.wav)\0*.wav\0All files (*.*)\0*.*\0";
            FlushPending();
            m_State.RequestFile(options, [this, id = setup.object](const FileDialogResult &result) {
                if (result.status != FileDialogResult::Status::Selected) return;
                const std::string chosen = result.paths.front();
                // The sound made again from the file (SetFileStreaming recreates it).
                Report(EditObjectState(*m_State.document, id, "Change File", [chosen](CKObject *o) {
                    CKWaveSound *wave = CKWaveSound::Cast(o);
                    wave->SetSoundFileName(const_cast<CKSTRING>(std::filesystem::u8path(chosen).string().c_str()));
                    wave->SetFileStreaming(wave->GetFileStreaming(), TRUE);
                }), "Change File");
            });
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Replace the sound's file");
    }
    if (CKMidiSound *midi = CKMidiSound::Cast(object)) {
        ImGui::SameLine();
        ImGui::TextUnformatted("   Filename");
        ImGui::SameLine();
        const char *file = midi->GetSoundFileName();
        ImGui::TextUnformatted(file && *file ? AnsiToUtf8(file).c_str() : "-");
        ImGui::SameLine();
        if (ImGui::SmallButton("...##midifile")) {
            FileDialogOptions options;
            options.filter = L"MIDI files (*.mid)\0*.mid;*.midi\0All files (*.*)\0*.*\0";
            FlushPending();
            m_State.RequestFile(options, [this, id = setup.object](const FileDialogResult &result) {
                if (result.status != FileDialogResult::Status::Selected) return;
                const std::string chosen = result.paths.front();
                Report(EditObjectState(*m_State.document, id, "Change File", [chosen](CKObject *o) {
                    CKMidiSound::Cast(o)->SetSoundFileName(const_cast<CKSTRING>(std::filesystem::u8path(chosen).string().c_str()));
                }), "Change File");
            });
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Replace the MIDI file");
    }
    PopDarkChrome();
    ImGui::SetCursorScreenPos(ImVec2(stripMin.x, stripMin.y + stripHeight + 2.0f));
}

// Dev's left column: the object's picture, Parameter / Attribute, initial conditions.
void SetupWindows::Sidebar(Setup &setup, CKObject *object) {
    ImGui::BeginChild("sidebar", ImVec2(104, 0), ImGuiChildFlags_None);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    // The picture (52 x 40, Dev.exe sub_441860 / sub_44CF90), else resvis.dll's
    // place for it (bitmap 835).
    const ImVec2 picture(at.x + 23, at.y + 12), pictureMax(picture.x + 52, picture.y + 40);
    auto found = m_State.objectPictures.find(object->GetID());
    ImTextureID texture = found != m_State.objectPictures.end() ? SnapshotTexture(found->second, nullptr) : DevBitmap(835);
    if (texture) dl->AddImage(texture, picture, pictureMax);
    ImGui::Dummy(ImVec2(98, 64));
    // Its menu (Dev.exe sub_448CA0): Load Snapshot (an image file,
    // CKLoadBitmap), Remove Snapshot (with a picture).
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("picturemenu");
    if (ImGui::BeginPopup("picturemenu")) {
        if (ImGui::MenuItem("Load Snapshot")) {
            FileDialogOptions options;
            options.filter = L"Images (*.bmp;*.tga;*.jpg;*.png)\0*.bmp;*.tga;*.jpg;*.jpeg;*.png\0All files (*.*)\0*.*\0";
            FlushPending();
            m_State.RequestFile(options, [this, id = object->GetID()](const FileDialogResult &result) {
                if (result.status != FileDialogResult::Status::Selected || m_State.document->IsDiscarded(id)) return;
                const std::string &path = result.paths.front();
                if (void *bitmap = CKLoadBitmap(const_cast<CKSTRING>(std::filesystem::u8path(path).string().c_str())))
                    m_State.SetObjectPictureBitmap(id, bitmap);
                else m_State.Log(LogLevel::Warning, "Load Snapshot: cannot read " + path);
            });
        }
        if (found != m_State.objectPictures.end() && ImGui::MenuItem("Remove Snapshot"))
            m_State.SetObjectPictureBitmap(object->GetID(), nullptr);
        ImGui::EndPopup();
    }
    // A Snapshot Buffer image dropped here is the object's picture (sub_448680, "Bitmap").
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(SNAPSHOT_PAYLOAD)) {
            int slot = -1;
            std::memcpy(&slot, payload->Data, sizeof(slot));
            m_State.SetObjectPicture(object->GetID(), slot);
        }
        ImGui::EndDragDropTarget();
    }
    // Animations are scene objects, not behavioral objects: no attributes.
    const bool behavioral = CKBeObject::Cast(object) != nullptr;
    if (!behavioral) setup.attributes = false;
    if (setup.kind != Kind::Group && behavioral && ImGui::Button("Parameter", ImVec2(92, 0))) setup.attributes = false;
    if (behavioral && ImGui::Button("Attribute", ImVec2(92, 0))) setup.attributes = true;
    ImGui::Dummy(ImVec2(1, 18));
    CKContext *context = m_State.document->Context();
    CKScene *scene = InitialConditionsScene(context);
    CKSceneObject *sceneObject = CKSceneObject::Cast(object);
    const bool here = scene && sceneObject && scene->IsObjectHere(sceneObject);
    const bool initial = here && scene->GetObjectInitialValue(sceneObject);
    auto ic = [&](const char *label, InitialConditions what, bool enabled) {
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Button(label, ImVec2(92, 0))) {
            const EditResult result = ApplyInitialConditions(*m_State.document, {object->GetID()}, what);
            m_State.Log(result.ok ? LogLevel::Info : LogLevel::Warning,
                        std::string(label) + (result.ok ? "" : ": " + AnsiToUtf8(result.message)));
        }
        ImGui::EndDisabled();
    };
    // Dev's Scene and Place Setups have no initial conditions buttons, nor
    // the pages of the objects that are not in scenes.
    if (setup.kind == Kind::Scene || CKPlace::Cast(object) || !CKSceneObject::Cast(object)) {
        ImGui::EndChild();
        return;
    }
    ic("Set IC", InitialConditions::Set, here);
    ic("Remove IC", InitialConditions::Remove, initial);
    ic("Restore IC", InitialConditions::Restore, initial);
    ImGui::EndChild();
}

// ---------------------------------------------------------------- edits

void SetupWindows::Live(CK_ID id, const char *label, const std::function<void(CKObject *)> &change) {
    CKContext *context = m_State.document->Context();
    if (m_PendingSceneActive || (m_Pending && (m_PendingObject != id || m_PendingLabel != label))) FlushPending();
    if (!m_Pending) {
        m_Pending = CaptureObjectState(context, id);
        m_PendingObject = id;
        m_PendingLabel = label;
    }
    if (CKObject *object = context->GetObject(id))
        if (!GuardedCall([&] { change(object); }, &m_State.document->Host(), label))
            m_State.Log(LogLevel::Warning, std::string(label) + " raised an exception");
}

void SetupWindows::LiveScene(CK_ID id, const char *label, const std::function<void(CKScene *)> &change) {
    CKScene *scene = Get<CKScene>(m_State, id);
    if (!scene) return;
    if (m_Pending || (m_PendingSceneActive && (m_PendingScene != id || m_PendingLabel != label))) FlushPending();
    if (!m_PendingSceneActive) {
        m_PendingSceneActive = true;
        m_PendingScene = id;
        m_PendingSceneBefore = ReadSceneEnvironment(scene);
        m_PendingLabel = label;
    }
    if (!GuardedCall([&] { change(scene); }, &m_State.document->Host(), label))
        m_State.Log(LogLevel::Warning, std::string(label) + " raised an exception");
}

void SetupWindows::FlushPending() {
    if (m_PendingSceneActive) {
        if (m_State.HasDocument())
            CommitSceneEnvironment(*m_State.document, m_PendingScene, m_PendingSceneBefore, m_PendingLabel);
        m_PendingSceneActive = false;
        m_PendingScene = 0;
        m_PendingLabel.clear();
    }
    if (!m_Pending) return;
    if (m_State.HasDocument())
        CommitObjectEdit(*m_State.document, m_PendingObject, m_Pending, m_PendingLabel);
    else
        ReleaseObjectState(m_Pending);
    m_Pending = nullptr;
    m_PendingObject = 0;
    m_PendingLabel.clear();
}

// Dev "Used By" lists; double-click opens the user's Setup.
void SetupWindows::UsedBy(const char *title, const std::vector<CKObject *> &users, float height) {
    Group(title);
    UsedByList(title, users, ImVec2(280, height));
}

void SetupWindows::UsedByList(const char *id, const std::vector<CKObject *> &users, const ImVec2 &size) {
    if (ImGui::BeginChild(id, size, ImGuiChildFlags_Borders)) {
        TypeAhead typeAhead;
        for (CKObject *user : users) {
            const char *cls = CKClassIDToString(user->GetClassID());
            const std::string text = NameOf(user) + " (" + (cls ? cls : "?") + ")";
            ImGui::PushID(static_cast<int>(user->GetID()));
            if (typeAhead.Selectable(text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                m_State.events.Post(ShowObjectSetup{user->GetID()});
            // Its menu (documentation: "right-click and select Show Setup").
            if (ImGui::BeginPopupContextItem("usermenu")) {
                if (ImGui::MenuItem("Show Setup")) m_State.events.Post(ShowObjectSetup{user->GetID()});
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Dev's layout

namespace setup {

ImVec2 DevPage::At(float x, float y) const { return ImVec2(IM_ROUND(origin.x + x * s), IM_ROUND(origin.y + y * s)); }

float DevPage::Place(float x1, float y1, float x2, float y2) const {
    const ImVec2 a = At(x1, y1), b = At(x2, y2);
    ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + std::max(0.0f, IM_ROUND((b.y - a.y - ImGui::GetFrameHeight()) * 0.5f))));
    const float width = std::max(1.0f, b.x - a.x);
    ImGui::SetNextItemWidth(width);
    return width;
}

void DevPage::Label(float x1, float y1, float x2, float y2, const char *text) const {
    const ImVec2 a = At(x1, y1), b = At(x2, y2);
    const float frame = ImGui::GetFrameHeight();
    const float top = a.y + std::max(0.0f, IM_ROUND((b.y - a.y - frame) * 0.5f));
    const float y = top + std::max(0.0f, IM_ROUND((std::min(frame, b.y - a.y) - ImGui::GetFontSize()) * 0.5f));
    ImGui::GetWindowDrawList()->AddText(ImVec2(a.x, y), IM_COL32(0, 0, 0, 255), text);
}

void DevPage::Line(float x1, float y1, float x2, float y2) const {
    const ImVec2 a = At(x1, y1);
    ImVec2 b = At(x2, y2);
    b.x = std::max(b.x, a.x + 1.0f);
    b.y = std::max(b.y, a.y + 1.0f);
    ImGui::GetWindowDrawList()->AddRectFilled(a, b, vi::Color(4));
}

bool DevPage::Check(const char *id, float x, float y, bool &on) const {
    // On: the box with the cross (609, hovered 611), as VICheckButton draws
    // it; off: the plain box (610, 612).
    static const int CHECK[4] = {610, 612, 609, 611};
    ImVec2 size;
    DevBitmap(CHECK[0], &size);
    if (size.x <= 0.0f) size = ImVec2(11.0f, 11.0f);
    const ImVec2 at = At(x, y), max(at.x + size.x * s, at.y + size.y * s);
    ImGui::SetCursorScreenPos(at);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(max.x - at.x, max.y - at.y));
    if (clicked) on = !on;
    const bool hovered = ImGui::IsItemHovered();
    if (ImTextureID texture = DevBitmap(CHECK[(on ? 2 : 0) + (hovered ? 1 : 0)]))
        ImGui::GetWindowDrawList()->AddImage(texture, at, max);
    return clicked;
}

bool DevPage::EditSlider(const char *id, const ImVec4 &edit, const ImVec4 &slider, float *value, float low, float high,
                         const char *format, float speed) const {
    Place(edit.x, edit.y, edit.z, edit.w);
    bool changed = ImGui::DragFloat(id, value, speed, 0.0f, 0.0f, format);
    ImGui::PushID(id);
    Place(slider.x, slider.y, slider.z, slider.w);
    float shown = std::max(low, std::min(high, *value));
    if (ImGui::SliderFloat("##slider", &shown, low, high, "")) {
        *value = shown;
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

bool DevPage::ColorBar(const char *id, float x1, float y1, float x2, float y2, float *color, bool alpha) const {
    const ImVec2 a = At(x1, y1), b = At(x2, y2);
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(a);
    const ImGuiColorEditFlags flags = alpha ? ImGuiColorEditFlags_AlphaPreviewHalf : ImGuiColorEditFlags_NoAlpha;
    if (ImGui::ColorButton(id, ImVec4(color[0], color[1], color[2], alpha ? color[3] : 1.0f), flags,
                           ImVec2(b.x - a.x, b.y - a.y)))
        ImGui::OpenPopup("picker");
    bool changed = false;
    if (ImGui::BeginPopup("picker")) {
        changed = alpha ? ImGui::ColorPicker4("##picker", color, flags) : ImGui::ColorPicker3("##picker", color, flags);
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

void DevPage::End(float x, float y) const {
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(x * s, y * s));
}

} // namespace setup

bool SetupWindows::ObjectCombo(const char *label, CK_CLASSID cid, CKObject *current, CK_ID *chosen, float width) {
    ImGui::SetNextItemWidth(width);
    bool picked = false;
    if (ImGui::BeginCombo(label, NameOf(current).c_str())) {
        TypeAhead typeAhead;
        if (typeAhead.Selectable("-- None --", !current)) {
            *chosen = 0;
            picked = true;
        }
        for (CKObject *object : ObjectsOf(*m_State.document, cid, true))
            if (typeAhead.Selectable(NameOf(object).c_str(), object == current)) {
                *chosen = object->GetID();
                picked = true;
            }
        ImGui::EndCombo();
    }
    return picked;
}

} // namespace vdev
