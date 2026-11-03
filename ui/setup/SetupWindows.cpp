#include <filesystem>
#include "features/layout/SnapshotData.h"
// Dev's Setup panels; layout after the Dev 2.5 screenshots the user gave
// (Grid, 2D Frame, 3D Object, Target Camera, Point Light, Material, Mesh,
// Texture, Curve, Character, Sound, Array and Group Setup). Pages use copied query data.

#include "ui/setup/SetupWindows.h"
#include "workspace/EditorWorkspace.h"
#include "editor/ImGuiDrawing.h"
#include "editor/Scene.h"
#include "CKDefines.h"

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

void SetupWindows::Report(EditorStatus status, const std::string &message) {
    auto &app = m_Workspace.Application();
    if (app.report)
        app.report({status, message});
}
void SetupWindows::Warn(const std::string &message) { Report(EDITOR_REFUSED, message); }

EditorClient SetupWindows::Client(EditorView owner) const { return {m_Workspace.Application().Host(), owner}; }

bool SetupWindows::Register(std::string &error) {
    const EditorParameter target{EDITOR_TEXT("target"), EDITOR_OBJECT};
    const EditorParameter args[] = {target, {EDITOR_TEXT("point"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter fields[] = {target,
                                      {EDITOR_TEXT("other"), EDITOR_OBJECT},
                                      {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE}};
    EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("object.open-setup"), EDITOR_TEXT("builtin.setup-windows"),
         EDITOR_TEXT("Setup"), &target, 1,
         [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
             return static_cast<SetupWindows *>(data)->Show(request->values[0].object, sink);
         },
         this, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("setup.navigate"), EDITOR_TEXT("builtin.setup-windows"),
         EDITOR_TEXT("Choose Setup Object"), &target, 1,
         [](void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
             return host->navigateView(host->data, EditorString EDITOR_TEXT("object.setup"), request->scope.view,
                                       request, sink);
         },
         nullptr, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE}};
    EditorViewDesc view{};
    view.size = sizeof(view);
    view.imguiVersion = EDITOR_IMGUI_VERSION;
    view.id = EDITOR_TEXT("object.setup");
    view.provider = EDITOR_TEXT("builtin.setup-windows");
    view.title = EDITOR_TEXT("Setup");
    view.area = EDITOR_DOCUMENT;
    view.documentBound = 1;
    view.data = this;
    view.create = [](void *data, const EditorHost *, EditorView id) -> void * {
        auto &owner = *static_cast<SetupWindows *>(data);
        auto setup = std::make_unique<Setup>();
        setup->owner = &owner;
        setup->nameOwner = id;
        setup->attributes = owner.attributesFirst;
        setup->page.title = "Setup";
        owner.m_Instances.emplace(id, setup.get());
        return setup.release();
    };
    view.destroy = [](void *data) {
        auto *setup = static_cast<Setup *>(data);
        auto &owner = *setup->owner;
        owner.Close(*setup);
        owner.m_Instances.erase(setup->nameOwner);
        delete setup;
    };
    view.draw = [](void *data, const EditorHost *, EditorView, EditorViewFrame *frame) {
        auto &setup = *static_cast<Setup *>(data);
        auto &owner = *setup.owner;
        owner.PrepareFrame();
        owner.DrawSetup(setup, *frame);
        if (owner.m_PropertyEdit && owner.m_PropertyOwner == setup.nameOwner && !ImGui::IsAnyItemActive() &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
            owner.FinishProperty(true);
        owner.PortalPick();
        owner.SetupPick();
        frame->active = setup.renameEdit || (owner.m_PropertyEdit && owner.m_PropertyOwner == setup.nameOwner);
    };
    view.parameters = args;
    view.count = 2;
    view.navigate = [](void *data, const EditorHost *, const EditorRequest *request) {
        auto &setup = *static_cast<Setup *>(data);
        int point = -1;
        for (uint32_t i = 0; i < request->count; ++i)
            if (std::string(request->values[i].key.data, request->values[i].key.size) == "point" &&
                request->values[i].integer)
                point = int(request->values[i].integer - 1);
        return setup.owner->Navigate(setup, request->values[0].object, point);
    };
    view.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &setup = *static_cast<Setup *>(data);
        const EditorValue values[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, setup.reference},
                                      {EDITOR_TEXT("other"), EDITOR_OBJECT, setup.page.point},
                                      {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(setup.kind)},
                                      {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE, {}, EditorText(setup.page.title)}};
        return sink.write(sink.data, values, 4);
    };
    view.fields = fields;
    view.fieldCount = 4;
    view.inspectRevision = [](void *data, const EditorHost *) {
        return static_cast<Setup *>(data)->inspectionRevision;
    };
    view.stateVersion = 1;
    view.saveState = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &setup = *static_cast<Setup *>(data);
        return setup.owner->Save(setup, sink);
    };
    view.restoreState = [](void *data, const EditorHost *, const EditorRequest *request) {
        auto &setup = *static_cast<Setup *>(data);
        return setup.owner->Restore(setup, *request);
    };
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("setup.windows");
    module.commands = commands;
    module.commandCount = 2;
    module.views = &view;
    module.viewCount = 1;
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

std::vector<EditorView> SetupWindows::Instances() const {
    const auto state = Client().Query("workspace.instances");
    const auto *types = EditorClient::Find(state, "types"), *views = EditorClient::Find(state, "views");
    std::vector<EditorView> result;
    if (state.status != EDITOR_OK || !types || !views || types->texts.size() != views->integers.size())
        return result;
    for (size_t i = 0; i < types->texts.size(); ++i)
        if (types->texts[i] == "object.setup")
            result.push_back(views->integers[i]);
    return result;
}
EditorStatus SetupWindows::Show(ObjectRef requested, EditorSink sink) {
    ObjectRef target{};
    int point = -1;
    SetupPageData page;
    if (!ReadSetupTarget(Client(), requested, target, point) || !ReadSetupPage(Client(), target, point, page))
        return EDITOR_STALE;
    ApplyPreferences();
    const bool showNew =
        m_Preferences.Bool("3D Layout\\Show New Setups") || (ImGui::GetCurrentContext() && ImGui::GetIO().KeyShift);
    EditorView chosen = 0;
    for (auto view : Instances()) {
        const auto info = Client().Inspect(view);
        const auto *object = EditorClient::Find(info, "target");
        if (info.status != EDITOR_OK || !object || EditorClient::Integer(info, "kind") != uint64_t(page.kind))
            continue;
        if (object->object == target) {
            chosen = view;
            break;
        }
        if (!showNew && !chosen)
            chosen = view;
    }
    const bool created = !chosen;
    if (created) {
        static const char *const roles[] = {"setup3d",
                                            "setupcamera",
                                            "setuplight",
                                            "setup2d",
                                            "setupmaterial",
                                            "setupmesh",
                                            "setuptexture",
                                            "setupgrid",
                                            "setupother",
                                            "setupcurve",
                                            "setupcharacter",
                                            "setupsound",
                                            "setuparray",
                                            "setupgroup",
                                            "setupscene",
                                            "setupsprite3d",
                                            "setupsprite2d",
                                            "setupanimation",
                                            "setupobjectanimation",
                                            "setupmidi",
                                            "setupspritetext",
                                            "setupchain",
                                            "setuplayer",
                                            "setupparameter",
                                            "setupparameterin",
                                            "setupoperation",
                                            "setupbehavior",
                                            "setupbehaviorio",
                                            "setupbehaviorlink",
                                            "setupsynchro",
                                            "setupstate",
                                            "setupcriticalsection",
                                            "setupinterfaceobjects"};
        const std::string base = roles[unsigned(page.kind)];
        std::string role = base;
        for (unsigned i = 1; m_Workspace.Role(role); ++i)
            role = base + "#" + std::to_string(i);
        chosen = m_Workspace.Open("object.setup", role);
    }
    if (!chosen)
        return EDITOR_ERROR;
    const EditorValue values[] = {ObjectArgument("target", target),
                                  IntegerArgument("point", point >= 0 ? uint64_t(point) + 1 : 0)};
    const EditorRequest next{m_Workspace.Application().Scope(chosen), values, 2};
    const auto status = m_Workspace.Application().Host()->navigateView(
        m_Workspace.Application().Host()->data, EditorString EDITOR_TEXT("object.setup"), chosen, &next, sink);
    if (created && status != EDITOR_OK && status != EDITOR_NO_CHANGE)
        m_Workspace.Close(chosen);
    return status;
}
EditorStatus SetupWindows::Navigate(Setup &setup, ObjectRef requested, int point) {
    ObjectRef target{};
    int selected = -1;
    SetupPageData page;
    if (!ReadSetupTarget(Client(), requested, target, selected) ||
        !ReadSetupPage(Client(), target, point >= 0 ? point : selected, page))
        return EDITOR_STALE;
    if (setup.reference && setup.kind != page.kind)
        return EDITOR_INVALID;
    ChangeTarget(setup, target, point >= 0 ? point : selected);
    setup.kind = page.kind;
    setup.page = std::move(page);
    ++setup.inspectionRevision;
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
        if (m_PickSetupView == setup.nameOwner) {
            m_PickSetup = {};
            m_PickSetupView = 0;
        }
        RetireTexture(std::move(setup.pictureTexture));
        if (m_PortalOwner == setup.nameOwner) {
            m_PortalPlace = {};
            m_PortalOwner = 0;
        }
        m_Shown.erase(setup.reference);
        Setup next;
        next.kind = setup.kind;
        next.owner = this;
        next.inspectionRevision = setup.inspectionRevision + 1;
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
    const auto focused = m_Workspace.LastFocused("object.setup");
    for (auto view : Instances()) {
        const auto state = Client().Inspect(view);
        const auto *target = EditorClient::Find(state, "target"), *other = EditorClient::Find(state, "other");
        if (state.status == EDITOR_OK && target && target->object)
            result.push_back({target->object, other ? other->object : ObjectRef{}, view, view == focused});
    }
    return result;
}

EditorStatus SetupWindows::Save(const Setup &setup, EditorSink sink) const {
    if (setup.renameEdit || setup.array.cell || setup.array.dialog || setup.array.openPopup || setup.array.openDelete ||
        setup.openResize || setup.openSquare || setup.openGridRename || setup.confirmRemoveSlot || setup.filePending ||
        setup.pickingColor || !setup.droppedAlpha.empty() || (m_PickSetup && m_PickSetupView == setup.nameOwner) ||
        (m_PortalPlace && m_PortalOwner == setup.nameOwner))
        return EDITOR_REFUSED;
    std::vector<EditorField> fields{{"target", EDITOR_OBJECT, setup.reference},
                                    {"point", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(setup.point)},
                                    {"attributes", EDITOR_INTEGER_VALUE, {}, {}, setup.attributes},
                                    {"scene-filter", EDITOR_INTEGER_VALUE, {}, {}, setup.sceneFilter},
                                    {"material-custom", EDITOR_INTEGER_VALUE, {}, {}, setup.materialCustom},
                                    {"attribute-catalog", EDITOR_INTEGER_VALUE, {}, {}, setup.attributeCatalog},
                                    {"mesh-rows", EDITOR_OBJECT_LIST},
                                    {"body-part-rows", EDITOR_OBJECT_LIST},
                                    {"zoom", EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, setup.zoom},
                                    {"scroll", EDITOR_REAL_LIST},
                                    {"grid3d", EDITOR_INTEGER_VALUE, {}, {}, setup.grid3d}};
    fields[6].objects.assign(setup.meshRows.begin(), setup.meshRows.end());
    fields[7].objects.assign(setup.bodyPartRows.begin(), setup.bodyPartRows.end());
    fields[9].reals = {setup.scroll.x, setup.scroll.y};
    const auto integers = [&](const char *key, const auto &values) {
        EditorField field{key, EDITOR_INTEGER_LIST};
        for (auto value : values)
            field.integers.push_back(uint64_t(value));
        fields.push_back(std::move(field));
    };
    integers("channel-rows", setup.channelRows);
    integers("attribute-rows", setup.attributeRows);
    integers("layers", setup.layers);
    integers("array-rows", setup.array.selected);
    const auto number = [&](const char *key, int value) {
        fields.push_back({key, EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(value))});
    };
    number("layer", setup.layer);
    number("paint", setup.paint);
    number("layer-type", setup.layerType);
    number("classification", setup.classification);
    number("array-anchor", setup.array.anchor);
    number("array-column", setup.array.column);
    number("array-focus-row", setup.array.focusRow);
    number("array-focus-column", setup.array.focusColumn);
    fields.push_back({"array-revision", EDITOR_INTEGER_VALUE, {}, {}, setup.array.revision});
    const auto values = BorrowEditorFields(fields);
    return sink.write(sink.data, values.data(), uint32_t(values.size()));
}
EditorStatus SetupWindows::Restore(Setup &setup, const EditorRequest &request) {
    std::vector<EditorField> fields;
    if (CopyEditorFields(fields, request.values, request.count) != EDITOR_OK)
        return EDITOR_INVALID;
    Setup next;
    next.owner = this;
    next.nameOwner = setup.nameOwner;
    ObjectRef target{};
    int point = 0;
    bool hasTarget = false;
    for (const auto &field : fields) {
        if (field.key == "target") {
            if (field.type != EDITOR_OBJECT || (field.object && field.object.document != request.scope.document))
                return EDITOR_STALE;
            target = field.object;
            hasTarget = true;
        } else if (field.key == "point") {
            if (field.type != EDITOR_INTEGER_VALUE || field.integer > INT32_MAX)
                return EDITOR_INVALID;
            point = int(field.integer);
        }
    }
    if (!hasTarget)
        return EDITOR_INVALID;
    if (target) {
        const auto status = Navigate(next, target, point);
        if (status != EDITOR_OK)
            return status;
    } else
        next.page.title = "Setup";
    for (const auto &field : fields) {
        const auto &key = field.key;
        if (key == "target" || key == "point")
            continue;
        if (key == "attributes" || key == "scene-filter" || key == "material-custom" || key == "grid3d") {
            if (field.type != EDITOR_INTEGER_VALUE || field.integer > 1)
                return EDITOR_INVALID;
            if (key == "attributes")
                next.attributes = field.integer;
            if (key == "scene-filter")
                next.sceneFilter = field.integer;
            if (key == "material-custom")
                next.materialCustom = field.integer;
            if (key == "grid3d")
                next.grid3d = field.integer;
        } else if (key == "mesh-rows" || key == "body-part-rows") {
            if (field.type != EDITOR_OBJECT_LIST)
                return EDITOR_INVALID;
            auto &rows = key == "mesh-rows" ? next.meshRows : next.bodyPartRows;
            rows.insert(field.objects.begin(), field.objects.end());
        } else if (key == "zoom") {
            if (field.type != EDITOR_REAL_VALUE || !std::isfinite(float(field.real)) || field.real < 0 ||
                field.real > 50)
                return EDITOR_INVALID;
            next.zoom = float(field.real);
        } else if (key == "scroll") {
            if (field.type != EDITOR_REAL_LIST || field.reals.size() != 2 || !std::isfinite(float(field.reals[0])) ||
                !std::isfinite(float(field.reals[1])))
                return EDITOR_INVALID;
            next.scroll = {float(field.reals[0]), float(field.reals[1])};
        } else if (key == "attribute-catalog" || key == "array-revision") {
            if (field.type != EDITOR_INTEGER_VALUE)
                return EDITOR_INVALID;
            (key == "attribute-catalog" ? next.attributeCatalog : next.array.revision) = field.integer;
        } else if (key == "channel-rows" || key == "attribute-rows" || key == "layers" || key == "array-rows") {
            if (field.type != EDITOR_INTEGER_LIST)
                return EDITOR_INVALID;
            for (auto value : field.integers) {
                if (value > INT32_MAX)
                    return EDITOR_INVALID;
                if (key == "channel-rows")
                    next.channelRows.insert(int(value));
                if (key == "attribute-rows")
                    next.attributeRows.insert(value);
                if (key == "layers")
                    next.layers.insert(int(value));
                if (key == "array-rows")
                    next.array.selected.insert(int(value));
            }
        } else {
            if (field.type != EDITOR_INTEGER_VALUE || int64_t(field.integer) < -1 || int64_t(field.integer) > INT32_MAX)
                return EDITOR_INVALID;
            const int value = int(int64_t(field.integer));
            if (key == "layer")
                next.layer = value;
            else if (key == "paint")
                next.paint = value;
            else if (key == "layer-type")
                next.layerType = value;
            else if (key == "classification")
                next.classification = value;
            else if (key == "array-anchor")
                next.array.anchor = value;
            else if (key == "array-column")
                next.array.column = value;
            else if (key == "array-focus-row")
                next.array.focusRow = value;
            else if (key == "array-focus-column")
                next.array.focusColumn = value;
            else
                return EDITOR_INVALID;
        }
    }
    next.array.target = next.reference;
    next.inspectionRevision = setup.inspectionRevision + 1;
    setup = std::move(next);
    return EDITOR_OK;
}

void SetupWindows::Close(Setup &setup) {
    if (m_PropertyOwner == setup.nameOwner) {
        // The workspace has already closed this scope and restored its drafts.
        m_PropertyEdit = 0;
        m_SubmittedEdit = 0;
        m_PropertyKey.clear();
        m_PropertyTarget = {};
        m_PropertySetup = {};
        m_PropertyControl = m_PropertyActive = m_CancelledControl = 0;
        m_PropertyOwner = 0;
    }
    setup.renameEdit = 0;
    setup.array = ArrayTable{};
    if (m_PickSetupView == setup.nameOwner) {
        m_PickSetup = {};
        m_PickSetupView = 0;
    }
    RetireTexture(std::move(setup.pictureTexture));
    m_Shown.erase(setup.reference);
    m_Files.Cancel(&setup);
    if (m_PortalOwner == setup.nameOwner) {
        m_PortalPlace = {};
        m_PortalOwner = 0;
    }
}

void SetupWindows::CloseAll() {
    for (auto id : Instances())
        m_Workspace.Close(id);
    m_FormatInfo.clear();
    m_FormatInfoKey.clear();
    m_Shown.clear();
    m_GridLayer = {};
    CancelPending();
    m_PickSetup = {};
    m_PickSetupView = 0;
    m_PortalPlace = {};
    m_PortalOwner = 0;
    m_CurrentOwner = m_PropertyOwner = 0;
    m_CurrentTarget = m_PropertySetup = {};
}

const SetupWindows::ShownTransform &SetupWindows::Shown(ObjectRef target) {
    auto [found, inserted] = m_Shown.try_emplace(target);
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

void SetupWindows::PrepareFrame() {
    if (m_PreparedFrame == ImGui::GetFrameCount())
        return;
    m_PreparedFrame = ImGui::GetFrameCount();
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
    if (m_GridLayer && ReadSetupPage(Client(), m_GridLayer, -1, layer))
        for (auto &entry : m_Instances)
            if (entry.second->reference == layer.grid && layer.layer >= 0)
                entry.second->layer = layer.layer;
    m_GridLayer = {};
    if (m_CancelledControl && ImGui::GetFrameCount() > m_CancelledFrame + 1 && !ImGui::IsAnyItemActive() &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
        m_CancelledControl = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) CancelPending();
}

void SetupWindows::SetupPick() {
    if (!m_PickSetup) return;
    Setup *picking = nullptr;
    const auto found = m_Instances.find(m_PickSetupView);
    if (found != m_Instances.end() && found->second->reference == m_PickSetup)
        picking = found->second;
    SetupPageData page;
    if (!picking || !ReadSetupPage(Client(), picking->reference, picking->point, page) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_PickSetup = {};
        m_PickSetupView = 0;
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
    Client(picking->nameOwner).Command("setup.navigate", {ObjectArgument("target", picked->object)});
    m_PickSetup = {};
    m_PickSetupView = 0;
}

void SetupWindows::PortalPick() {
    if (!m_PortalPlace) return;
    SetupPageData page;
    if (!ReadSetupPage(Client(), m_PortalPlace, -1, page) || page.classId != CKCID_PLACE ||
        ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_PortalPlace = {};
        m_PortalOwner = 0;
        return;
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetTooltip("Pick a place");
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    const auto place = m_PortalPlace;
    m_PortalPlace = {};
    m_PortalOwner = 0;
    const auto mouse = ImGui::GetMousePos();
    const double xy[] = {mouse.x, mouse.y};
    EditorValue point{EDITOR_TEXT("point"), EDITOR_REAL_LIST};
    point.reals = {xy, 2};
    const auto picked = Client().Query("parameter.object-at", {point, IntegerArgument("class", CKCID_PLACE)});
    const auto *other = EditorClient::Find(picked, "target");
    if (picked.status != EDITOR_OK || !other || other->type != EDITOR_OBJECT || !other->object) {
        Report(EDITOR_OK, "Automatic Portal: no place picked");
        return;
    }
    FlushPending();
    const auto result = Client().Command("object.create-automatic-portal",
                                         {ObjectArgument("place", place), ObjectArgument("other", other->object)});
    if (result.status != EDITOR_QUEUED && !result.message.empty())
        Report(result.status, "Automatic Portal: " + result.message);
    for (const auto &field : result.fields) if (result.status == EDITOR_OK && field.key == "created") {
            const auto details = Client().Query("object.details", {ObjectArgument("target", field.object)});
            Report(EDITOR_OK, "Automatic Portal: " + EditorClient::Text(details, "name"));
    }
}

void SetupWindows::DrawSetup(Setup &setup, EditorViewFrame &frame) {
    m_CurrentOwner = setup.nameOwner;
    m_CurrentTarget = setup.reference;
    if (m_PropertyEdit && m_PropertyOwner == setup.nameOwner && m_PropertySetup != setup.reference)
        FinishProperty(false);
    SetupPageData page;
    if (!setup.reference) {
        EditorPrepareViewWindow(frame);
        bool open = true;
        if (BeginDevWindow(frame.window.data, &open)) {
            ImGui::TextDisabled("Choose an object to open its Setup.");
            const auto selection = Client(setup.nameOwner).Query("selection.objects");
            const auto *objects = EditorClient::Find(selection, "objects");
            ImGui::BeginDisabled(!objects || objects->objects.empty());
            if (ImGui::Button("Open Selected Object") && objects && !objects->objects.empty())
                Client(setup.nameOwner).Command("setup.navigate", {ObjectArgument("target", objects->objects.front())});
            ImGui::EndDisabled();
        }
        frame.close = !open;
        ImGui::End();
        return;
    }
    if (!ReadSetupPage(Client(), setup.reference, setup.point, page)) {
        frame.close = 1;
        return;
    }
    if (setup.page.point != page.point || setup.page.title != page.title)
        ++setup.inspectionRevision;
    setup.page = page;
    EditorPrepareViewWindow(frame);
    bool open = true;
    if (!BeginDevWindow(frame.window.data, &open)) {
        frame.close = !open;
        ImGui::End();
        return;
    }
    DevTabClose(&open, "Close Window");
    frame.close = !open;
    Header(setup, page);
    ImGui::PushID(static_cast<int>(setup.kind));
    const bool attributes = setup.attributes;
    Sidebar(setup, page);
    if (attributes != setup.attributes && m_PropertyOwner == setup.nameOwner)
        FinishProperty(false);
    ImGui::SameLine();
    ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, vi::Color(0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, CurrentDevTheme().setupSpacing);
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
    if (FlushPending()) {
        const auto scope = Client(m_CurrentOwner).Scope();
        const auto found = m_Instances.find(scope.view);
        if (found == m_Instances.end())
            return;
        found->second->filePending = true;
        m_Files.Request(
            options,
            [this, scope, accept = std::move(accept)](const FileDialogResult &result) {
                const auto current = Client(scope.view).Scope();
                if (current.document != scope.document || current.view != scope.view)
                    return;
                const auto source = m_Instances.find(scope.view);
                if (source == m_Instances.end())
                    return;
                source->second->filePending = false;
                if (result.status == FileDialogResult::Status::Error)
                    Warn(result.error);
                accept(result);
            },
            multiple, found->second);
    }
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
    const auto client = Client(m_CurrentOwner);
    const EditorRequest request{client.Scope(),
                                next.data(),
                                uint32_t(next.size()),
                                {},
                                0,
                                pending ? m_SubmittedEdit : 0,
                                pending && revision != next.end() ? EditorString EDITOR_TEXT("document-revision")
                                                                  : EditorString{}};
    const auto result = m_Workspace.Application().Execute(id, request);
    if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE && result.status != EDITOR_QUEUED &&
        !result.message.empty())
        Warn(result.message);
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
    PushDarkChrome();
    {
        SetupForm form("##setup-header");
        if (form) {
            form.Row("Name");
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, DevChrome());
            const float unit = PageUnit();
            static const int picker[5] = {719, 721, 720, 720, 719};
            static const int filter[5] = {1355, 1356, 1357, 1354, 1357};
            const bool picking = m_PickSetup == setup.reference && m_PickSetupView == setup.nameOwner;
            if (DevBitmapButton("picker", ImGui::GetCursorScreenPos(), picker, picking, true, unit,
                                "Pick an entity of the same type")) {
                m_PickSetup = picking ? ObjectRef{} : setup.reference;
                m_PickSetupView = picking ? 0 : setup.nameOwner;
            }
            ImGui::SameLine();
            if (ImGui::GetContentRegionAvail().x < 22 * unit) ImGui::NewLine();
            if (DevBitmapButton("scenefilter", ImGui::GetCursorScreenPos(), filter, setup.sceneFilter, true, unit,
                                "Toggle Level/Current Scene Filter")) {
                setup.sceneFilter = !setup.sceneFilter;
                if (setup.sceneFilter && page.sceneScoped && page.currentScene && !page.inCurrentScene) {
                    setup.sceneFilter = false;
                    Warn("Object not in current scene !");
                }
            }
            ImGui::SameLine();
            if (ImGui::GetContentRegionAvail().x < 3 * ImGui::GetFrameHeight()) ImGui::NewLine();
            ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x));
            if (ImGui::BeginCombo("##name", page.name.c_str())) {
                std::vector<SetupObjectRow> rows;
                if (ReadSetupObjects(Client(setup.nameOwner), setup.reference, setup.sceneFilter, rows)) {
                    TypeAhead typeAhead;
                    for (const auto &row : rows)
                        if (typeAhead.Selectable(row.name.c_str(), row.object == setup.reference))
                            Client(setup.nameOwner).Command("setup.navigate", {ObjectArgument("target", row.object)});
                }
                ImGui::EndCombo();
            }
            if (page.fileKind != SetupFileKind::None) {
                form.Row("Filename");
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, DevChrome());
                const char *id = page.fileKind == SetupFileKind::Texture  ? "...##texturefile"
                                 : page.fileKind == SetupFileKind::Sprite ? "...##spritefile"
                                 : page.fileKind == SetupFileKind::Wave   ? "...##soundfile"
                                                                          : "...##midifile";
                if (ImGui::SmallButton(id)) ReplaceFile(setup, page.fileKind);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(page.fileKind == SetupFileKind::Wave   ? "Replace the sound's file"
                                      : page.fileKind == SetupFileKind::Midi ? "Replace the MIDI file"
                                                                             : "Replace the slot's image");
                ImGui::SameLine();
                if (ImGui::GetContentRegionAvail().x < ImGui::GetFrameHeight()) ImGui::NewLine();
                const auto at = ImGui::GetCursorScreenPos();
                const ImVec2 size{std::max(1.f, ImGui::GetContentRegionAvail().x), ImGui::GetFrameHeight()};
                const ImVec2 min{at.x, at.y + ImGui::GetStyle().FramePadding.y}, max{at.x + size.x, at.y + size.y};
                const char *filename = page.filename.empty() ? "-" : page.filename.c_str();
                ImGui::Dummy(size);
                ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), min, max, max.x, filename, nullptr, nullptr);
                if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
                    ImGui::PushTextWrapPos(35 * ImGui::GetFontSize());
                    ImGui::TextUnformatted(filename);
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
        }
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
            if (m_Workspace.Finish(setup.renameEdit, true)) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || !setup.renameEdit) {
            m_Workspace.Finish(setup.renameEdit, false);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    PopDarkChrome();
    ImGui::Spacing();
}

// Dev's left column: the object's picture, Parameter / Attribute, initial conditions.
void SetupWindows::Sidebar(Setup &setup, const SetupPageData &page) {
    const float unit = ImGui::GetFontSize() / 14.0f;
    const float width = CurrentDevTheme().setupSidebarWidth * unit;
    const float button = std::max(1.0f, width - 12 * unit);
    ImGui::BeginChild("sidebar", ImVec2(width, 0), ImGuiChildFlags_None);
    Picture(setup);
    // Animations are scene objects, not behavioral objects: no attributes.
    const bool behavioral = page.behavioral;
    if (!behavioral)
        setup.attributes = false;
    if (setup.kind != Kind::Group && behavioral && ImGui::Button("Parameter", ImVec2(button, 0)))
        setup.attributes = false;
    if (behavioral && ImGui::Button("Attribute", ImVec2(button, 0))) setup.attributes = true;
    ImGui::Dummy(ImVec2(1, 18 * unit));
    auto ic = [&](const char *label, uint64_t mode, bool enabled) {
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Button(label, ImVec2(button, 0))) {
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
    ChooseFile(options, [this, target = setup.reference, owner = setup.nameOwner](const FileDialogResult &result) {
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
    if (setup.pictureTexture && !setup.pictureTexture->NativeTexture()) setup.pictureTexture.reset();
    if (read && setup.picture.present && !setup.pictureTexture) {
        auto rgba = setup.picture.pixels;
        for (size_t i = 0; i < rgba.size(); i += 4)
            std::swap(rgba[i], rgba[i + 2]);
        setup.pictureTexture = MainRenderer().CreateTexture(SnapshotData::WIDTH, SnapshotData::HEIGHT, rgba.data());
    }
    const float unit = ImGui::GetFontSize() / 14.0f;
    const auto at = ImGui::GetCursorScreenPos();
    auto *draw = ImGui::GetWindowDrawList();
    const ImVec2 picture(at.x + 23 * unit, at.y + 12 * unit), pictureMax(picture.x + 52 * unit, picture.y + 40 * unit);
    const auto texture = read && setup.pictureTexture ? setup.pictureTexture->Id() : DevBitmap(835);
    if (texture)
        draw->AddImage(texture, picture, pictureMax);
    ImGui::Dummy(ImVec2(98 * unit, 64 * unit));
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
    const auto status = app.FinishEdit(m_PropertyEdit, commit, &m_SubmittedEdit);
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
            return;
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
    app.FinishEdit(edit, true);
}

bool SetupWindows::EditObjectList(ObjectRef reference, const char *property, const std::vector<ObjectRef> &add,
                                  const std::vector<ObjectRef> &remove) {
    if (!FlushPending())
        return false;
    auto &app = m_Workspace.Application();
    if (!m_CurrentOwner)
        return false;
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



} // namespace vdev
