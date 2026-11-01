#include <filesystem>
#include "features/layout/SnapshotData.h"
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

EditorClient SetupWindows::Client(EditorView owner) const { return {m_Workspace.Application().Host(), owner}; }

bool SetupWindows::Register(std::string &error) {
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT};
    const EditorCommandDesc command{sizeof(EditorCommandDesc),
                                    EDITOR_TEXT("object.open-setup"),
                                    EDITOR_TEXT("builtin.setup-windows"),
                                    EDITOR_TEXT("Setup"),
                                    &target,
                                    1,
                                    [](void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
                                        return static_cast<SetupWindows *>(data)->Show(request->values[0].object);
                                    },
                                    this,
                                    EDITOR_WORKSPACE,
                                    EDITOR_SESSION_CHANGE};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("setup.windows");
    module.commands = &command;
    module.commandCount = 1;
    return m_Workspace.Application().Register(module, error);
}

void SetupWindows::ApplyPreferences() {
    const auto revision = Client().Revision("preferences.values");
    if (!revision || revision != m_PreferencesRevision) {
        const auto values = Client().Query("preferences.values");
        const auto *record = EditorClient::Find(values, "value");
        std::string error;
        if (values.status == EDITOR_OK && record && record->type == EDITOR_RECORD &&
            m_Preferences.ReadValues(record->fields, error))
            m_PreferencesRevision = Client().Revision("preferences.values");
    }
}

void SetupWindows::Open(ObjectRef target) { Client().Command("object.open-setup", {ObjectArgument("target", target)}); }

EditorStatus SetupWindows::Show(ObjectRef requested) {
    ObjectRef target{};
    int point = -1;
    SetupPageData page;
    if (!ReadSetupTarget(Client(), requested, target, point) || !ReadSetupPage(Client(), target, point, page))
        return EDITOR_STALE;
    ApplyPreferences();
    const bool showNew =
        m_Preferences.Bool("3D Layout\\Show New Setups") || (ImGui::GetCurrentContext() && ImGui::GetIO().KeyShift);
    Setup *shown = nullptr;
    int slot = 0;
    for (Setup &setup : m_Doc.setups)
        if (setup.open && setup.kind == page.kind) {
            if (setup.reference == target || (!showNew && !shown))
                shown = &setup;
            if (setup.reference == target)
                break;
            slot = std::max(slot, setup.slot + 1);
        }
    if (shown) {
        ChangeTarget(*shown, target, point);
        shown->focus = true;
        return EDITOR_OK;
    }
    Setup setup;
    setup.kind = page.kind;
    setup.page = page;
    setup.object = target.object;
    setup.reference = target;
    setup.slot = slot;
    setup.focus = true;
    setup.attributes = attributesFirst;
    if (point >= 0) setup.point = point;
    m_Doc.setups.push_back(std::move(setup));
    return EDITOR_OK;
}

void SetupWindows::ChangeTarget(Setup &setup, ObjectRef target, int point) {
    if (setup.reference != target) {
        SetupPageData page;
        if (!ReadSetupPage(Client(), target, point, page))
            return;
        if (m_PropertyEdit && m_PropertyOwner == setup.nameOwner)
            FinishProperty(false);
        m_Workspace.Finish(setup.renameEdit, false);
        setup.array.Cancel(Client(setup.nameOwner));
        if (m_PickSetup == setup.reference && m_PickSetupSlot == setup.slot)
            m_PickSetup = {};
        m_Doc.shown.erase(setup.reference);
        Setup next;
        next.kind = setup.kind;
        next.slot = setup.slot;
        next.nameOwner = setup.nameOwner;
        next.attributes = setup.attributes;
        next.sceneFilter = setup.sceneFilter;
        next.page = std::move(page);
        next.reference = target;
        next.object = target.object;
        setup = std::move(next);
    }
    if (point >= 0) {
        setup.point = point;
        ReadSetupPage(Client(), setup.reference, setup.point, setup.page);
    }
}

std::vector<SetupWindows::OpenSetup> SetupWindows::OpenSetups() const {
    std::vector<OpenSetup> result;
    for (const Setup &setup : m_Doc.setups) {
        if (!setup.open || setup.page.target != setup.reference)
            continue;
        result.push_back({setup.reference, setup.page.point, setup.reference == m_Doc.front});
    }
    return result;
}

void SetupWindows::Close(Setup &setup) {
    if (m_PropertyEdit && m_PropertyOwner == setup.nameOwner)
        FinishProperty(false);
    m_Workspace.Finish(setup.renameEdit, false);
    setup.array.Cancel(Client(setup.nameOwner));
    if (m_PickSetup == setup.reference && m_PickSetupSlot == setup.slot)
        m_PickSetup = {};
    m_Doc.shown.erase(setup.reference);
    m_Workspace.CloseOwner(setup.nameOwner);
}

void SetupWindows::CloseAll() {
    CancelPending();
    for (auto &setup : m_Doc.setups)
        Close(setup);
    m_Doc = DocumentState();
    m_PickSetup = {};
    m_PortalPlace = {};
    m_CurrentOwner = m_PropertyOwner = 0;
    m_CurrentTarget = m_PropertySetup = {};
}

const SetupWindows::ShownTransform &SetupWindows::Shown(CK3dEntity *entity) {
    const auto target = m_State.document->Reference(entity->GetID());
    auto [found, inserted] = m_Doc.shown.try_emplace(target);
    if (inserted) {
        found->second.world.SetIdentity();
        found->second.local.SetIdentity();
        found->second.box = VxBbox(VxVector(0, 0, 0), VxVector(0, 0, 0));
    }
    if (inserted || m_Notify) {
        SetupTransformData data;
        if (ReadSetupTransform(Client(), target, data)) {
            for (int i = 0; i < 16; ++i) {
                found->second.world[i / 4][i % 4] = data.world[i];
                found->second.local[i / 4][i % 4] = data.local[i];
            }
            for (int i = 0; i < 3; ++i) {
                found->second.box.Min[i] = data.box[i];
                found->second.box.Max[i] = data.box[i + 3];
            }
        }
    }
    return found->second;
}

void SetupWindows::Draw(ImGuiID dock) {
    if (m_Doc.setups.empty() && !m_PortalPlace && !m_PickSetup) return;
    // When the Setups take the transforms (RealTime Notifications).
    ApplyPreferences();
    const Preferences &prefs = m_Preferences;
    const auto runtime = Client().Query("runtime.state");
    const bool playing = runtime.status == EDITOR_OK && EditorClient::Integer(runtime, "state") == 1;
    m_PlayFrames = playing ? m_PlayFrames + 1 : 0;
    if (playing)
        m_Notify = prefs.Bool("3D Layout\\Player RealTime Notifications") &&
                   m_PlayFrames % std::max(1, prefs.Int("3D Layout\\Player RealTime Notifications FrameSkip")) == 0;
    else {
        const auto input = Client().Query("layout.input");
        const bool transforming =
            EditorClient::Integer(input, "active") &&
            (EditorClient::Integer(input, "kind") == 1 || EditorClient::Integer(input, "kind") == 2);
        m_Notify = !transforming || prefs.Bool("3D Layout\\Author RealTime Notifications");
    }
    // Layer Setup's Grid: the Grid Setup opened selects the layer.
    SetupPageData layer;
    if (m_Doc.gridLayer && ReadSetupPage(Client(), m_Doc.gridLayer, -1, layer))
        for (Setup &setup : m_Doc.setups)
            if (setup.reference == layer.grid && layer.layer >= 0)
                setup.layer = layer.layer;
    m_Doc.gridLayer = {};
    if (!Client().Scope().document.value) {
        CloseAll();
        return;
    }
    if (m_CancelledControl && ImGui::GetFrameCount() > m_CancelledFrame + 1 && !ImGui::IsAnyItemActive() &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
        m_CancelledControl = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) CancelPending();
    for (Setup &setup : m_Doc.setups)
        if (setup.open) DrawSetup(setup, dock);
    for (Setup &setup : m_Doc.setups)
        if (!setup.open)
            Close(setup);
    m_Doc.setups.erase(std::remove_if(m_Doc.setups.begin(), m_Doc.setups.end(), [](const Setup &s) { return !s.open; }),
                   m_Doc.setups.end());
    if (m_PropertyEdit && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right))
        FinishProperty(true);
    PortalPick();
    SetupPick();
}

void SetupWindows::SetupPick() {
    if (!m_PickSetup) return;
    Setup *picking = nullptr;
    for (Setup &setup : m_Doc.setups)
        if (setup.open && setup.reference == m_PickSetup && setup.slot == m_PickSetupSlot)
            picking = &setup;
    SetupPageData page;
    if (!picking || !ReadSetupPage(Client(), picking->reference, picking->point, page) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_PickSetup = {};
        return;
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetTooltip("Pick an entity");
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    const auto mouse = ImGui::GetMousePos();
    const double xy[] = {mouse.x, mouse.y};
    EditorValue point{EDITOR_TEXT("point"), EDITOR_REAL_LIST};
    point.reals = {xy, 2};
    const auto result =
        Client(picking->nameOwner).Query("parameter.object-at", {point, IntegerArgument("class", page.classId)});
    const auto *picked = EditorClient::Find(result, "target");
    if (result.status != EDITOR_OK || !picked || picked->type != EDITOR_OBJECT || !picked->object)
        return;
    ChangeTarget(*picking, picked->object);
    m_PickSetup = {};
}

void SetupWindows::PortalPick() {
    if (!m_PortalPlace) return;
    SetupPageData page;
    if (!ReadSetupPage(Client(), m_PortalPlace, -1, page) || page.classId != CKCID_PLACE ||
        ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_PortalPlace = {};
        return;
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetTooltip("Pick a place");
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    const auto place = m_PortalPlace;
    m_PortalPlace = {};
    const auto mouse = ImGui::GetMousePos();
    const double xy[] = {mouse.x, mouse.y};
    EditorValue point{EDITOR_TEXT("point"), EDITOR_REAL_LIST};
    point.reals = {xy, 2};
    const auto picked = Client().Query("parameter.object-at", {point, IntegerArgument("class", CKCID_PLACE)});
    const auto *other = EditorClient::Find(picked, "target");
    if (picked.status != EDITOR_OK || !other || other->type != EDITOR_OBJECT || !other->object) {
        m_State.Log(LogLevel::Info, "Automatic Portal: no place picked");
        return;
    }
    FlushPending();
    const auto result = Client().Command("object.create-automatic-portal",
                                         {ObjectArgument("place", place), ObjectArgument("other", other->object)});
    if (result.status != EDITOR_QUEUED && !result.message.empty())
        m_State.Log(result.status == EDITOR_OK ? LogLevel::Info : LogLevel::Warning, "Automatic Portal: " + result.message);
    for (const auto &field : result.fields) if (result.status == EDITOR_OK && field.key == "created") {
            const auto details = Client().Query("object.details", {ObjectArgument("target", field.object)});
            m_State.Log(LogLevel::Info, "Automatic Portal: " + EditorClient::Text(details, "name"));
    }
}

void SetupWindows::DrawSetup(Setup &setup, ImGuiID dock) {
    {
        auto &app = m_Workspace.Application();
        if (!setup.nameOwner)
            setup.nameOwner = app.OpenScope();
        m_CurrentOwner = setup.nameOwner;
        m_CurrentTarget = setup.reference;
        if (m_PropertyEdit && m_PropertyOwner == setup.nameOwner && m_PropertySetup != setup.reference)
            FinishProperty(false);
    }
    SetupPageData page;
    if (!ReadSetupPage(Client(), setup.reference, setup.point, page)) {
        setup.open = false;
        return;
    }
    setup.page = page;
    static const char *const IDS[] = {"setup3d", "setupcamera", "setuplight", "setup2d", "setupmaterial",
                                      "setupmesh", "setuptexture", "setupgrid", "setupother", "setupcurve",
                                      "setupcharacter", "setupsound", "setuparray", "setupgroup", "setupscene", "setupsprite3d", "setupsprite2d",
                                      "setupanimation", "setupobjectanimation", "setupmidi", "setupspritetext",
                                      "setupchain", "setuplayer", "setupparameter", "setupparameterin", "setupoperation",
                                      "setupbehavior", "setupbehaviorio", "setupbehaviorlink", "setupsynchro", "setupstate",
                                      "setupcriticalsection", "setupinterfaceobjects"};
    // The first Setup of a type keeps the type's window (its place in the layout).
    std::string title = page.title + "###" + IDS[static_cast<int>(setup.kind)];
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
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        m_Doc.front = setup.reference;
    Header(setup, page);
    if (m_PropertyEdit && m_PropertyOwner == setup.nameOwner && m_PropertySetup != setup.reference)
        FinishProperty(false);
    m_CurrentTarget = setup.reference;
    if (page.target != setup.reference && !ReadSetupPage(Client(), setup.reference, setup.point, page)) {
        setup.open = false;
        ImGui::End();
        return;
    }
    setup.page = page;
    ImGui::PushID(static_cast<int>(setup.kind));
    const bool attributes = setup.attributes;
    Sidebar(setup, page);
    if (attributes != setup.attributes && m_PropertyOwner == setup.nameOwner)
        FinishProperty(false);
    ImGui::SameLine();
    ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 5));
    // Dev Group Setup has the Attribute page only.
    if (setup.kind == Kind::Group) setup.attributes = true;
    if (setup.kind == Kind::Array && setup.attributes && setup.nameOwner)
        setup.array.Cancel(Client(setup.nameOwner));
    DrawObjectPage(setup);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopID();
    // An attribute from the Attribute Manager anywhere in the Setup: on its object.
    if (page.attributeDrop) {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (ImGui::BeginDragDropTargetCustom(window->Rect(), window->ID)) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(EDITOR_ATTRIBUTE_PAYLOAD)) {
                if (payload->DataSize == sizeof(EditorAttributePayload)) {
                    EditorAttributePayload attribute;
                    std::memcpy(&attribute, payload->Data, sizeof(attribute));
                    FlushPending();
                    Client(setup.nameOwner)
                        .Command("attribute.add", {ObjectsArgument("objects", {setup.reference}),
                                                   IntegerArgument("type", uint64_t(attribute.type)),
                                                   IntegerArgument("catalog", attribute.catalog),
                                                   ObjectArgument("source", attribute.value)});
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::End();
}

// Class-specific pages retain their existing native editors until their own migration.
void SetupWindows::DrawObjectPage(Setup &setup) {
    auto *object = m_State.document->Resolve(setup.reference);
    if (!object)
        return;
    if (setup.attributes) {
        AttributePage(setup, object);
    } else {
        switch (setup.kind) {
        case Kind::Entity3D:
            // 3D Frame Setup for frames, portals and places, 3D Object Setup for the others.
            if (setup.page.frame) {
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
        case Kind::Array: ArrayPage(setup); break;
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
}

// Dev's dark strip: the Name list of the objects of the same class.
void SetupWindows::Header(Setup &setup, const SetupPageData &page) {
    const ImVec2 stripMin = ImGui::GetCursorScreenPos();
    const float stripHeight = ImGui::GetFrameHeight() + 6.0f;
    ImGui::GetWindowDrawList()->AddRectFilled(
        stripMin, ImVec2(stripMin.x + ImGui::GetContentRegionAvail().x, stripMin.y + stripHeight), DEV_CHROME);
    PushDarkChrome();
    // Dev.exe sub_442A30: the picker (Pick an entity of the same type) and
    // the Level / Current Scene filter (pressed: the current scene) before
    // the Name combo.
    const float s = ImGui::GetFontSize() / 13.0f;
    static const int PICKER[5] = {719, 721, 720, 720, 719};
    static const int FILTER[5] = {1355, 1356, 1357, 1354, 1357};
    const bool picking = m_PickSetup == setup.reference && m_PickSetupSlot == setup.slot;
    if (DevBitmapButton("picker", ImVec2(stripMin.x + 40.0f * s, stripMin.y + 1.0f), PICKER, picking, true, s,
                        "Pick an entity of the same type")) {
        m_PickSetup = picking ? ObjectRef{} : setup.reference;
        m_PickSetupSlot = setup.slot;
    }
    if (DevBitmapButton("scenefilter", ImVec2(stripMin.x + 62.0f * s, stripMin.y + 1.0f), FILTER, setup.sceneFilter, true,
                        s, "Toggle Level/Current Scene Filter")) {
        setup.sceneFilter = !setup.sceneFilter;
        if (setup.sceneFilter && page.sceneScoped && page.currentScene && !page.inCurrentScene) {
            setup.sceneFilter = false;
            m_State.Log(LogLevel::Warning, "Object not in current scene !");
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(stripMin.x + 86.0f * s, stripMin.y + 3.0f));
    ImGui::TextUnformatted("Name");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##name", page.name.c_str())) {
        std::vector<SetupObjectRow> rows;
        if (ReadSetupObjects(Client(setup.nameOwner), setup.reference, setup.sceneFilter, rows)) {
            TypeAhead typeAhead;
            for (const auto &row : rows)
                if (typeAhead.Selectable(row.name.c_str(), row.object == setup.reference))
                    ChangeTarget(setup, row.object);
        }
        ImGui::EndCombo();
    }
    // F2: the object renamed (the setup's window focused).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        m_Workspace.Shortcut("object.name")) {
        setup.openRename = m_Workspace.Begin("object.name", setup.reference, setup.renameEdit, setup.nameOwner);
    }
    if (setup.openRename) {
        setup.openRename = false;
        ImGui::OpenPopup("Rename###setuprename");
    }
    if (ImGui::BeginPopupModal("Rename###setuprename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(260);
        const bool entered = m_Workspace.DrawInput(setup.renameEdit, "##name", ImGui::IsWindowAppearing());
        if (ImGui::Button("OK", ImVec2(80, 0)) || entered) {
            FlushPending();
            if (m_Workspace.Finish(setup.renameEdit, true))
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || !setup.renameEdit) {
            m_Workspace.Finish(setup.renameEdit, false);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (page.fileKind != SetupFileKind::None) {
        ImGui::SameLine();
        ImGui::TextUnformatted("   Filename");
        ImGui::SameLine();
        ImGui::TextUnformatted(page.filename.empty() ? "-" : page.filename.c_str());
        ImGui::SameLine();
        const char *id = page.fileKind == SetupFileKind::Texture  ? "...##texturefile"
                         : page.fileKind == SetupFileKind::Sprite ? "...##spritefile"
                         : page.fileKind == SetupFileKind::Wave   ? "...##soundfile"
                                                                  : "...##midifile";
        if (ImGui::SmallButton(id))
            ReplaceFile(setup, page.fileKind);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(page.fileKind == SetupFileKind::Wave   ? "Replace the sound's file"
                              : page.fileKind == SetupFileKind::Midi ? "Replace the MIDI file"
                                                                     : "Replace the slot's image");
    }
    PopDarkChrome();
    ImGui::SetCursorScreenPos(ImVec2(stripMin.x, stripMin.y + stripHeight + 2.0f));
}

// Dev's left column: the object's picture, Parameter / Attribute, initial conditions.
void SetupWindows::Sidebar(Setup &setup, const SetupPageData &page) {
    ImGui::BeginChild("sidebar", ImVec2(104, 0), ImGuiChildFlags_None);
    Picture(setup);
    // Animations are scene objects, not behavioral objects: no attributes.
    const bool behavioral = page.behavioral;
    if (!behavioral)
        setup.attributes = false;
    if (setup.kind != Kind::Group && behavioral && ImGui::Button("Parameter", ImVec2(92, 0)))
        setup.attributes = false;
    if (behavioral && ImGui::Button("Attribute", ImVec2(92, 0)))
        setup.attributes = true;
    ImGui::Dummy(ImVec2(1, 18));
    auto ic = [&](const char *label, uint64_t mode, bool enabled) {
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Button(label, ImVec2(92, 0))) {
            FlushPending();
            Client(setup.nameOwner)
                .Command("scene.initial-conditions",
                         {ObjectsArgument("objects", {setup.reference}), IntegerArgument("mode", mode)});
        }
        ImGui::EndDisabled();
    };
    // Dev's Scene and Place Setups have no initial conditions buttons, nor
    // the pages of the objects that are not in scenes.
    if (!page.initialVisible) {
        ImGui::EndChild();
        return;
    }
    ic("Set IC", 0, page.initialHere);
    ic("Remove IC", 2, page.initialValue);
    ic("Restore IC", 1, page.initialValue);
    ImGui::EndChild();
}

void SetupWindows::ReplaceFile(Setup &setup, SetupFileKind kind) {
    auto *object = m_State.document->Resolve(setup.reference);
    if (!object)
        return;
    if (kind == SetupFileKind::Texture || kind == SetupFileKind::Sprite) {
        RunSlotCommand(setup, object, SlotCommand::Replace);
        return;
    }
    FileDialogOptions options;
    options.filter = kind == SetupFileKind::Wave ? L"Wave files (*.wav)\0*.wav\0All files (*.*)\0*.*\0"
                                                 : L"MIDI files (*.mid)\0*.mid;*.midi\0All files (*.*)\0*.*\0";
    FlushPending();
    m_State.RequestFile(options, [this, target = setup.reference,
                                  owner = setup.nameOwner](const FileDialogResult &result) {
        if (result.status == FileDialogResult::Status::Selected && !result.paths.empty())
            SetProperty(target, owner, "sound.file", EditorField{"value", EDITOR_TEXT_VALUE, {}, result.paths.front()});
    });
}

void SetupWindows::Picture(Setup &setup) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    // The picture (52 x 40, Dev.exe sub_441860 / sub_44CF90), else resvis.dll's
    // place for it (bitmap 835).
    const ImVec2 picture(at.x + 23, at.y + 12), pictureMax(picture.x + 52, picture.y + 40);
    auto found = m_State.document->Pictures().find(setup.object);
    ImTextureID texture = found != m_State.document->Pictures().end()
        ? SnapshotTexture(found->second.get(), nullptr, found->second) : DevBitmap(835);
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
            m_State.RequestFile(options, [this, target = setup.reference](const FileDialogResult &result) {
                if (result.status != FileDialogResult::Status::Selected || result.paths.empty() || !m_State.document ||
                    !m_State.document->Resolve(target))
                    return;
                const std::string &path = result.paths.front();
                if (void *bitmap = CKLoadBitmap(const_cast<CKSTRING>(std::filesystem::u8path(path).string().c_str())))
                    SetObjectPicture(*m_State.document, target.object, bitmap);
                else
                    m_State.Log(LogLevel::Warning, "Load Snapshot: cannot read " + path);
            });
        }
        if (found != m_State.document->Pictures().end() && ImGui::MenuItem("Remove Snapshot"))
            SetObjectPicture(*m_State.document, setup.object, nullptr);
        ImGui::EndPopup();
    }
    // A Snapshot Buffer image dropped here is the object's picture (sub_448680, "Bitmap").
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(SNAPSHOT_PAYLOAD)) {
            if (payload->DataSize == sizeof(SnapshotDrag)) {
                SnapshotDrag drag;
                std::memcpy(&drag, payload->Data, sizeof(drag));
                ApplySnapshot(EditorClient(m_State.workspace->Application().Host(), 0), drag, setup.reference, false);
            }
        }
        ImGui::EndDragDropTarget();
    }
}

// ---------------------------------------------------------------- edits

bool SetupWindows::FinishProperty(bool commit) {
    if (!m_PropertyEdit || !m_State.workspace)
        return true;
    auto &app = m_State.workspace->Application();
    const auto status = app.FinishEdit(m_PropertyEdit, commit);
    if (status == EDITOR_QUEUED || status == EDITOR_NO_CHANGE || status == EDITOR_STALE || status == EDITOR_CANCELLED ||
        status == EDITOR_ERROR) {
        if (!commit) {
            auto *g = ImGui::GetCurrentContext();
            if (g && g->ActiveId == m_PropertyActive)
                ImGui::ClearActiveID();
            if (g && (g->InputTextDeactivatedState.ID == m_PropertyControl ||
                      g->InputTextDeactivatedState.ID == m_PropertyActive))
                g->InputTextDeactivatedState.ID = 0;
            m_CancelledControl = m_PropertyControl;
            m_CancelledFrame = ImGui::GetFrameCount();
        }
        m_PropertyEdit = 0;
        m_PropertyKey.clear();
        m_PropertyTarget = {};
        m_PropertyControl = 0;
    }
    // Legacy Setup actions run outside registered drawing callbacks. Finish
    // the property before such an action starts its own core command.
    if (status == EDITOR_QUEUED)
        app.EndFrame();
    return status == EDITOR_QUEUED || status == EDITOR_NO_CHANGE || status == EDITOR_STALE ||
           status == EDITOR_CANCELLED;
}
void SetupWindows::PreviewProperty(CK_ID id, const char *property, EditorField value, std::vector<uint64_t> indices,
                                   ObjectRef relative, std::vector<ObjectRef> targets) {
    if (!property || !m_State.workspace || !m_State.HasDocument())
        return;
    const auto control = ImGui::GetItemID();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        return;
    if (m_CancelledControl == control) {
        if (ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
            ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsItemDeactivatedAfterEdit())
            return;
        m_CancelledControl = 0;
    }
    auto &app = m_State.workspace->Application();
    const auto target = m_State.document->Reference(id);
    const auto key = "setup." + std::string(property);
    if (m_PropertyEdit && (m_PropertyTarget != target || m_PropertyKey != key || m_PropertyControl != control))
        if (!FinishProperty(true))
            return;
    if (!m_PropertyEdit) {
        if (!m_CurrentOwner)
            m_CurrentOwner = app.OpenScope();
        m_PropertyOwner = m_CurrentOwner;
        const EditorValue context[] = {
            {EditorLiteral("target"), EDITOR_OBJECT, target},
            {EditorLiteral("indices"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {indices.data(), uint32_t(indices.size())}},
            {EditorLiteral("relative"), EDITOR_OBJECT, relative},
            {EditorLiteral("targets"), EDITOR_OBJECT_LIST, {}, {}, 0, {targets.data(), uint32_t(targets.size())}}};
        if (app.BeginEdit(key, {app.Scope(m_PropertyOwner), context, 4}, m_PropertyEdit) != EDITOR_OK)
            return;
        m_PropertyTarget = target;
        m_PropertySetup = m_CurrentTarget;
        m_PropertyKey = key;
        m_PropertyControl = control;
    }
    const std::vector<EditorField> fields{std::move(value)};
    const auto values = BorrowEditorFields(fields);
    m_PropertyActive = ImGui::GetActiveID();
    if (app.UpdateEdit(m_PropertyEdit, values.values.front()) != EDITOR_OK) {
        FinishProperty(false);
        Report(EditResult::Error("The property edit failed"), property);
        return;
    }
    if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right))
        FinishProperty(true);
}

void SetupWindows::SetProperty(ObjectRef target, EditorView owner, const char *property, EditorField value) {
    if (!m_State.workspace || !m_State.HasDocument() || !m_State.document->Resolve(target) || !FlushPending())
        return;
    auto &app = m_State.workspace->Application();
    EditorEdit edit = 0;
    if (app.BeginEdit("setup." + std::string(property), app.Scope(owner), target, edit) != EDITOR_OK)
        return;
    const std::vector<EditorField> fields{std::move(value)};
    const auto values = BorrowEditorFields(fields);
    if (app.UpdateEdit(edit, values.values.front()) != EDITOR_OK) {
        app.FinishEdit(edit, false);
        return;
    }
    if (app.FinishEdit(edit, true) == EDITOR_QUEUED)
        app.EndFrame();
}

bool SetupWindows::EditObjectList(CK_ID id, const char *property, const std::vector<CK_ID> &add,
                                  const std::vector<CK_ID> &remove) {
    if (!m_State.workspace || !FlushPending())
        return false;
    auto &app = m_State.workspace->Application();
    if (!m_CurrentOwner)
        m_CurrentOwner = app.OpenScope();
    const EditorValue target{EditorLiteral("target"), EDITOR_OBJECT, m_State.document->Reference(id)};
    const auto query = "setup." + std::string(property) + "-value";
    const auto result = app.Query(query, {app.Scope(m_CurrentOwner), &target, 1});
    if (result.status != EDITOR_OK)
        return false;
    auto value =
        std::find_if(result.fields.begin(), result.fields.end(), [](const auto &f) { return f.key == "value"; });
    if (value == result.fields.end() || value->type != EDITOR_OBJECT_LIST)
        return false;
    EditorField desired = *value;
    desired.objects.erase(
        std::remove_if(desired.objects.begin(), desired.objects.end(),
                       [&](auto ref) { return std::find(remove.begin(), remove.end(), ref.object) != remove.end(); }),
        desired.objects.end());
    for (auto object : add) {
        const auto ref = m_State.document->Reference(object);
        if (std::find(desired.objects.begin(), desired.objects.end(), ref) == desired.objects.end())
            desired.objects.push_back(ref);
    }
    PreviewProperty(id, property, std::move(desired));
    return FinishProperty(true);
}

void SetupWindows::CancelPending() { FinishProperty(false); }
bool SetupWindows::FlushPending() { return FinishProperty(true); }

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
                m_State.events.Post(ShowObjectSetup{m_State.document->Reference(user->GetID())});
            // Its menu (documentation: "right-click and select Show Setup").
            if (ImGui::BeginPopupContextItem("usermenu")) {
                if (ImGui::MenuItem("Show Setup")) m_State.events.Post(ShowObjectSetup{m_State.document->Reference(user->GetID())});
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
