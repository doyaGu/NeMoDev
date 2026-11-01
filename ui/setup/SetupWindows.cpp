#include <filesystem>
#include "features/layout/SnapshotData.h"
// Dev's Setup panels; layout after the Dev 2.5 screenshots the user gave
// (Grid, 2D Frame, 3D Object, Target Camera, Point Light, Material, Mesh,
// Texture, Curve, Character, Sound, Array and Group Setup). Pages use copied query data.

#include "ui/setup/SetupWindows.h"
#include "ui/app/EditorState.h"
#include "workspace/EditorWorkspace.h"

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
#include "ui/setup/SetupWindowsDetail.h"
#include "ui/schematic/Snapshots.h"

namespace vdev {

using namespace setup;

// ---------------------------------------------------------------- frame

void SetupWindows::Warn(const std::string &message) { m_State.Log(LogLevel::Warning, message); }

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
        RetireTexture(std::move(setup.pictureTexture));
        m_Doc.shown.erase(setup.reference);
        Setup next;
        next.kind = setup.kind;
        next.slot = setup.slot;
        next.nameOwner = setup.nameOwner;
        next.attributes = setup.attributes;
        next.sceneFilter = setup.sceneFilter;
        next.page = std::move(page);
        next.reference = target;
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
    RetireTexture(std::move(setup.pictureTexture));
    m_Doc.shown.erase(setup.reference);
    m_Workspace.CloseOwner(setup.nameOwner);
}

void SetupWindows::CloseAll() {
    m_FormatInfo.clear();
    m_FormatInfoKey.clear();
    CancelPending();
    for (auto &setup : m_Doc.setups)
        Close(setup);
    m_Doc = DocumentState();
    m_PickSetup = {};
    m_PortalPlace = {};
    m_CurrentOwner = m_PropertyOwner = 0;
    m_CurrentTarget = m_PropertySetup = {};
}

const SetupWindows::ShownTransform &SetupWindows::Shown(ObjectRef target) {
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

// Class pages consume copied descriptions and submit edits through the workspace.
void SetupWindows::DrawObjectPage(Setup &setup) {
    if (setup.attributes) {
        AttributePage(setup);
    } else {
        switch (setup.kind) {
        case Kind::Entity3D:
            // 3D Frame Setup for frames, portals and places, 3D Object Setup for the others.
            if (setup.page.frame) {
                FramePage(setup.reference);
            } else {
                Entity3DPage(setup.reference, true, true, nullptr, &setup);
            }
            break;
        case Kind::Sprite3D:
            Sprite3DPage(setup.reference);
            break;
        case Kind::Sprite2D:
            SpritePage(setup, setup.reference);
            break;
        case Kind::Curve:
            CurvePage(setup, setup.reference);
            break;
        case Kind::Character:
            CharacterPage(setup, setup.reference);
            break;
        case Kind::Sound:
            SoundPage(setup.reference);
            break;
        case Kind::Array: ArrayPage(setup); break;
        case Kind::Scene:
            ScenePage(setup.reference);
            break;
        case Kind::Camera:
            CameraPage(setup.reference);
            break;
        case Kind::Light:
            LightPage(setup.reference);
            break;
        case Kind::Entity2D:
            Entity2DPage(setup.reference);
            break;
        case Kind::Material:
            MaterialPage(setup, setup.reference);
            break;
        case Kind::Mesh:
            MeshPage(setup, setup.reference);
            break;
        case Kind::Texture:
            TexturePage(setup, setup.reference);
            break;
        case Kind::Grid:
            GridPage(setup, setup.reference);
            break;
        case Kind::Animation:
            AnimationPage(setup.reference);
            break;
        case Kind::ObjectAnimation:
            ObjectAnimationPage(setup.reference);
            break;
        case Kind::MidiSound:
            MidiSoundPage(setup.reference);
            break;
        case Kind::SpriteText:
            SpriteTextPage(setup.reference);
            break;
        case Kind::KinematicChain:
            KinematicChainPage(setup.reference);
            break;
        case Kind::Layer:
            LayerPage(setup.reference);
            break;
        case Kind::Parameter:
            ParameterPage(setup.reference);
            break;
        case Kind::ParameterIn:
            ParameterInPage(setup.reference);
            break;
        case Kind::Operation:
            OperationPage(setup.reference);
            break;
        case Kind::Behavior:
            BehaviorPage(setup.reference);
            break;
        case Kind::BehaviorIO:
            BehaviorIOPage(setup.reference);
            break;
        case Kind::BehaviorLink:
            BehaviorLinkPage(setup.reference);
            break;
        case Kind::Synchro:
            SynchroPage(setup.reference);
            break;
        case Kind::State:
            StatePage(setup.reference);
            break;
        case Kind::CriticalSection:
            CriticalSectionPage(setup.reference);
            break;
        case Kind::InterfaceObjects:
            InterfaceObjectsPage(setup.reference);
            break;
        default:
            OtherPage(setup.reference);
            break;
        }
    }
}

bool SetupWindows::ReadDetails(ObjectRef target, const char *section, SetupDetails &data,
                               const std::vector<uint64_t> &indices) const {
    return ReadSetupDetails(Client(m_CurrentOwner), target, section, data, indices);
}
bool SetupWindows::ReadProperties(ObjectRef target, const std::vector<const char *> &properties, SetupDetails &data,
                                  const std::vector<uint64_t> &indices) const {
    return ReadSetupProperties(Client(m_CurrentOwner), target, properties, data, indices);
}
void SetupWindows::ChooseFile(const FileDialogOptions &options, std::function<void(const FileDialogResult &)> accept,
                              bool multiple) {
    if (FlushPending())
        m_State.RequestFile(options, std::move(accept), multiple);
}
EditorResponse SetupWindows::RunSetupCommand(const char *id, const std::vector<EditorValue> &arguments) {
    auto next = arguments;
    auto revision = std::find_if(next.begin(), next.end(), [](const auto &arg) {
        return std::string(arg.key.data, arg.key.size) == "document-revision";
    });
    auto target = std::find_if(next.begin(), next.end(),
                               [](const auto &arg) { return std::string(arg.key.data, arg.key.size) == "target"; });
    if (revision != next.end() && target != next.end()) {
        SetupDetails source;
        if (!ReadDetails(target->object, "structure", source) || source.documentRevision != revision->integer)
            return {EDITOR_STALE};
    }
    const bool pending = m_PropertyEdit != 0;
    if (!FlushPending())
        return {EDITOR_REFUSED};
    if (pending && revision != next.end() && target != next.end()) {
        SetupDetails source;
        if (!ReadDetails(target->object, "structure", source))
            return {EDITOR_STALE};
        revision->integer = source.documentRevision;
    }
    const auto result = Client(m_CurrentOwner).Command(id, next);
    if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE && result.status != EDITOR_QUEUED &&
        !result.message.empty())
        m_State.Log(LogLevel::Warning, result.message);
    return result;
}
void SetupWindows::PreviewIndexedProperty(ObjectRef target, const char *property, EditorField value,
                                          std::vector<uint64_t> indices, uint64_t revision,
                                          std::vector<ObjectRef> targets) {
    SetupDetails current;
    if (!ReadDetails(target, "structure", current) || current.documentRevision != revision) {
        CancelPending();
        return;
    }
    PreviewProperty(target, property, std::move(value), std::move(indices), {}, std::move(targets));
}
void SetupWindows::SetupLink(const char *label, ObjectRef target, const std::string &name) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(110);
    ImGui::BeginDisabled(!target);
    if (ImGui::SmallButton(">") && target)
        Open(target);
    ImGui::EndDisabled();
    if (target && ImGui::IsItemHovered())
        ImGui::SetTooltip("Setup");
    ImGui::SameLine();
    ImGui::TextUnformatted(name.c_str());
    ImGui::PopID();
}
void SetupWindows::UsedByRows(const char *title, const std::vector<EditorField> &rows, float height) {
    Group(title);
    UsedByList(title, rows, ImVec2(280, height));
}
bool SetupWindows::ObjectChoice(const char *label, uint32_t classId, ObjectRef current, const std::string &name,
                                ObjectRef *chosen, float width) {
    ImGui::SetNextItemWidth(width);
    bool changed = false;
    if (ImGui::BeginCombo(label, name.c_str())) {
        TypeAhead ahead;
        if (ahead.Selectable("-- None --", !current)) {
            *chosen = {};
            changed = true;
        }
        std::vector<SetupObjectRow> rows;
        if (ReadSetupChoices(Client(m_CurrentOwner), classId, rows))
            for (const auto &row : rows) {
                ImGui::PushID(int(row.object.object));
                if (ahead.Selectable(row.name.c_str(), row.object == current)) {
                    *chosen = row.object;
                    changed = true;
                }
                ImGui::PopID();
            }
        ImGui::EndCombo();
    }
    return changed;
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
    if (kind == SetupFileKind::Texture || kind == SetupFileKind::Sprite) {
        RunSlotCommand(setup, setup.reference, SlotCommand::Replace);
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
    const auto client = Client(setup.nameOwner);
    const bool read = ReadObjectPicture(client, setup.reference, setup.picture);
    if (read && (!setup.picture.present || setup.pictureRevision != setup.picture.Revision())) {
        RetireTexture(std::move(setup.pictureTexture));
        setup.pictureRevision = setup.picture.Revision();
    }
    if (read && setup.picture.present && !setup.pictureTexture) {
        auto rgba = setup.picture.pixels;
        for (size_t i = 0; i < rgba.size(); i += 4)
            std::swap(rgba[i], rgba[i + 2]);
        setup.pictureTexture = MainRenderer().CreateTexture(SnapshotData::WIDTH, SnapshotData::HEIGHT, rgba.data());
    }
    const auto at = ImGui::GetCursorScreenPos();
    auto *draw = ImGui::GetWindowDrawList();
    const ImVec2 picture(at.x + 23, at.y + 12), pictureMax(picture.x + 52, picture.y + 40);
    const auto texture = read && setup.pictureTexture ? setup.pictureTexture->Id() : DevBitmap(835);
    if (texture)
        draw->AddImage(texture, picture, pictureMax);
    ImGui::Dummy(ImVec2(98, 64));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("picturemenu");
    if (ImGui::BeginPopup("picturemenu")) {
        if (ImGui::MenuItem("Load Snapshot"))
            RunSetupCommand("object.load-picture", {ObjectArgument("target", setup.reference)});
        if (read && setup.picture.present && ImGui::MenuItem("Remove Snapshot"))
            RunSetupCommand("object.remove-picture", {ObjectArgument("target", setup.reference)});
        ImGui::EndPopup();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const auto *payload = ImGui::AcceptDragDropPayload(SNAPSHOT_PAYLOAD);
            payload && payload->DataSize == sizeof(SnapshotDrag)) {
            SnapshotDrag drag;
            std::memcpy(&drag, payload->Data, sizeof(drag));
            if (FlushPending())
                ApplySnapshot(client, drag, setup.reference, false);
        }
        ImGui::EndDragDropTarget();
    }
}

// ---------------------------------------------------------------- edits

bool SetupWindows::FinishProperty(bool commit) {
    if (!m_PropertyEdit)
        return true;
    auto &app = m_Workspace.Application();
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
void SetupWindows::PreviewProperty(ObjectRef target, const char *property, EditorField value,
                                   std::vector<uint64_t> indices, ObjectRef relative, std::vector<ObjectRef> targets) {
    if (!property || !target || target.document != Client().Scope().document)
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
    auto &app = m_Workspace.Application();
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
        Warn(std::string(property) + ": The property edit failed");
        return;
    }
    if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right))
        FinishProperty(true);
}

void SetupWindows::SetProperty(ObjectRef target, EditorView owner, const char *property, EditorField value) {
    if (!target || target.document != m_Workspace.Application().Scope(owner).document || !FlushPending())
        return;
    auto &app = m_Workspace.Application();
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

bool SetupWindows::EditObjectList(ObjectRef reference, const char *property, const std::vector<ObjectRef> &add,
                                  const std::vector<ObjectRef> &remove) {
    if (!FlushPending())
        return false;
    auto &app = m_Workspace.Application();
    if (!m_CurrentOwner)
        m_CurrentOwner = app.OpenScope();
    const EditorValue target{EditorLiteral("target"), EDITOR_OBJECT, reference};
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
                       [&](auto ref) { return std::find(remove.begin(), remove.end(), ref) != remove.end(); }),
        desired.objects.end());
    for (auto object : add) {
        const auto ref = object;
        if (std::find(desired.objects.begin(), desired.objects.end(), ref) == desired.objects.end())
            desired.objects.push_back(ref);
    }
    PreviewProperty(reference, property, std::move(desired));
    return FinishProperty(true);
}

void SetupWindows::CancelPending() { FinishProperty(false); }
bool SetupWindows::FlushPending() { return FinishProperty(true); }

// Dev "Used By" lists; double-click and the menu both navigate by full identity.
void SetupWindows::UsedByList(const char *id, const std::vector<EditorField> &rows, const ImVec2 &size) {
    if (ImGui::BeginChild(id, size, ImGuiChildFlags_Borders)) {
        TypeAhead ahead;
        for (const auto &value : rows) {
            const SetupFields row(value.fields);
            const auto target = row.Object("target");
            const auto text = row.Text("name") + " (" + row.Text("class-name") + ")";
            ImGui::PushID(int(target.object));
            if (ahead.Selectable(text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                Open(target);
            if (ImGui::BeginPopupContextItem("usermenu")) {
                if (ImGui::MenuItem("Show Setup"))
                    Open(target);
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

} // namespace vdev
