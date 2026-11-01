#include "ui/schematic/Snapshots.h"
#include "workspace/BuildingBlockDrag.h"
// Building Blocks and Data Resources files dragged onto the 3D Layout
// (Dev.exe sub_4C0A30 and the functions it calls).

#include "ui/layout/LayoutWindow.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

#include "core/schematic/SchematicEdits.h"
#include "core/document/ScriptSession.h"
#include "core/runtime/TextEncoding.h"
#include "core/document/ToolCommands.h"
#include "core/graph/TopologyCommands.h"
#include "core/level/LevelCommands.h"
#include "core/graph/ValueCommands.h"

namespace vdev {
namespace {
ObjectRef ObjectField(const EditorResponse &r, const char *key) {
    const auto *value = EditorClient::Find(r, key);
    return value && value->type == EDITOR_OBJECT ? value->object : ObjectRef{};
}
} // namespace

EditorResponse LayoutWindow::DropInfo(int x, int y, const char *kind, uint64_t prototype, ObjectRef source) const {
    EditorValue px{EDITOR_TEXT("x"), EDITOR_REAL_VALUE}, py{EDITOR_TEXT("y"), EDITOR_REAL_VALUE};
    px.real = x;
    py.real = y;
    return Client().Query("layout.drop-target",
                          {px, py, TextArgument("kind", std::string(kind)), IntegerArgument("prototype", prototype),
                           IntegerArgument("ctrl", ImGui::GetIO().KeyCtrl), ObjectArgument("source", source)});
}
void LayoutWindow::HighlightDrop(ObjectRef target) {
    if (target == m_DropHighlight)
        return;
    const auto result = Execute("layout.highlight-drop", {ObjectArgument("target", target)});
    if (result.status == EDITOR_OK || result.status == EDITOR_QUEUED)
        m_DropHighlight = target;
}
void LayoutWindow::DropTarget(const ImVec2 &at) {
    m_DropText.clear();
    ObjectRef highlight{};
    if (!ImGui::BeginDragDropTarget()) {
        HighlightDrop({});
        return;
    }
    const auto &io = ImGui::GetIO();
    const int x = int(io.MousePos.x - at.x), y = int(io.MousePos.y - at.y);
    if (const auto *payload =
            ImGui::AcceptDragDropPayload(EDITOR_BUILDING_BLOCK_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        EditorBuildingBlock source{};
        std::vector<EditorBuildingBlock> blocks;
        if (ReadBuildingBlockDrag(payload->Data, size_t(payload->DataSize), source, blocks)) {
            if (payload->IsDelivery()) {
                HighlightDrop({});
                for (const auto &each : blocks)
                    DropBuildingBlock(x, y, CKGUID(each.first, each.second));
            } else {
                const CKGUID guid(source.first, source.second);
                const LayoutPickInfo info = m_View.PickInfo(x, y);
                auto *declaration = CKGetObjectDeclarationFromGuid(guid);
                const auto cid = declaration ? declaration->GetCompatibleClassId() : CKCID_BEOBJECT;
                const bool multiple =
                    m_State.objectSelection.Objects().size() > 1 && (m_State.objectSelection.Contains(info.entity3d) ||
                                                                     m_State.objectSelection.Contains(info.character));
                const auto target = BlockTarget(info, cid, io.KeyCtrl);
                if (multiple)
                    m_DropText = "Drop Behavior on Multi-Selection";
                else if (auto *object = m_State.host.Context()->GetObject(target))
                    m_DropText = "Drop Behavior on " + DevObjectName(object);
                highlight = m_State.document->Reference(multiple ? info.entity3d : target);
            }
        }
    }
    if (const auto *payload =
            ImGui::AcceptDragDropPayload(EDITOR_RESOURCE_FILE_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        uint32_t source = 0;
        std::vector<ResourceFile> files;
        if (ReadResourceDrag(payload->Data, size_t(payload->DataSize), source, files)) {
            if (payload->IsDelivery())
                DropResources(x, y, files);
            else {
                highlight = ObjectField(DropInfo(x, y, "resource"), "highlight");
                const auto cid = files[source].cid;
                if (cid == CKCID_TEXTURE || cid == CKCID_SPRITE || cid == CKCID_SPRITE3D)
                    m_DropText = io.KeyShift && io.KeyCtrl ? "Drop as 2D Entity (Release CTRL or SHIFT for more)"
                                 : io.KeyShift             ? "Drop as 3D Sprite (Release SHIFT or hold CTRL for more)"
                                 : io.KeyCtrl              ? "Drop as 2D Sprite (Release CTRL or hold SHIFT for more)"
                                                           : "Drop as Texture (Hold CTRL or SHIFT for more)";
            }
        }
    }
    if (const auto *payload = ImGui::AcceptDragDropPayload(SNAPSHOT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        const auto input = InputState(x, y, true);
        const auto *character = EditorClient::Find(input, "character"), *entity = EditorClient::Find(input, "entity");
        const auto target = character && character->object ? character->object : entity ? entity->object : ObjectRef{};
        if (payload->IsDelivery()) {
            if (payload->DataSize == sizeof(SnapshotDrag) && target) {
                SnapshotDrag drag;
                std::memcpy(&drag, payload->Data, sizeof(drag));
                Complete(ApplySnapshot(Client(), drag, target, false), AfterCommand::None);
            }
        } else
            highlight = target;
    }
    if (const auto *payload =
            ImGui::AcceptDragDropPayload(EDITOR_ATTRIBUTE_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        if (payload->DataSize == sizeof(EditorAttributePayload)) {
            const auto info = DropInfo(x, y, "attribute");
            if (payload->IsDelivery()) {
                EditorAttributePayload attribute;
                std::memcpy(&attribute, payload->Data, sizeof(attribute));
                const auto *objects = EditorClient::Find(info, "objects");
                if (objects && !objects->objects.empty())
                    Execute("attribute.add",
                            {ObjectsArgument("objects", objects->objects),
                             IntegerArgument("type", uint64_t(attribute.type)),
                             IntegerArgument("catalog", attribute.catalog), ObjectArgument("source", attribute.value)});
            } else {
                highlight = ObjectField(info, "highlight");
                m_DropText = EditorClient::Text(info, "text");
            }
        }
    }
    if (const auto *dragged = ImGui::GetDragDropPayload();
        dragged && dragged->IsDataType(EDITOR_OBJECT_PAYLOAD) && dragged->DataSize == sizeof(ObjectRef)) {
        ObjectRef source;
        std::memcpy(&source, dragged->Data, sizeof(source));
        const auto info = DropInfo(x, y, "material", 0, source);
        const auto mesh = ObjectField(info, "mesh"), material = ObjectField(info, "material");
        if (mesh && material)
            if (const auto *payload =
                    ImGui::AcceptDragDropPayload(EDITOR_OBJECT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                if (!payload->IsDelivery())
                    highlight = ObjectField(info, "highlight");
                else
                    Execute("layout.replace-material", {ObjectArgument("mesh", mesh), ObjectArgument("from", material),
                                                        ObjectArgument("to", source)});
            }
    }
    ImGui::EndDragDropTarget();
    HighlightDrop(highlight);
}
// Dev.exe sub_4BFD20: the object a Building Block goes to, by the class it
// applies to; Ctrl on a body part keeps the body part rather than its
// character; else the first object there.
CK_ID LayoutWindow::BlockTarget(const LayoutPickInfo &info, CK_CLASSID cid, bool ctrl) const {
    CKContext *context = m_State.host.Context();
    const CK_ID character = info.character && !ctrl ? info.character : 0;
    CK_ID target = 0;
    switch (cid) {
    case CKCID_SCENE: target = m_View.Scene() ? m_View.Scene()->GetID() : 0; break;
    case CKCID_LEVEL: target = context->GetCurrentLevel() ? context->GetCurrentLevel()->GetID() : 0; break;
    case CKCID_2DENTITY:
    case CKCID_SPRITE: target = info.entity2d; break;
    case CKCID_MATERIAL: target = info.material; break;
    case CKCID_TEXTURE: target = info.texture; break;
    case CKCID_MESH:
    case CKCID_PATCHMESH: target = info.mesh; break;
    case CKCID_BEOBJECT:
    case CKCID_RENDEROBJECT: target = character ? character : info.entity3d ? info.entity3d : info.entity2d; break;
    case CKCID_3DENTITY:
    case CKCID_3DOBJECT:
    case CKCID_BODYPART: target = character ? character : info.entity3d; break;
    case CKCID_CHARACTER: target = info.character; break;
    default: target = info.entity3d; break;
    }
    for (CK_ID id : {info.entity3d, info.character, info.entity2d, info.mesh, info.material, info.texture})
        if (!target) target = id;
    return target;
}

// A dropped Building Block: on the object under the mouse, or, when it is
// in a selection of several, on each selected object of its class (Dev.exe
// sub_4C2F30). Dev then asks for the Building Block's parameters; here the
// Schematic shows it with its defaults. One undo step.
void LayoutWindow::DropBuildingBlock(int x, int y, const CKGUID &guid) {
    if (!m_State.HasDocument()) return;
    CKContext *context = m_State.host.Context();
    CKObjectDeclaration *declaration = CKGetObjectDeclarationFromGuid(guid);
    if (!declaration) return;
    const CK_CLASSID cid = declaration->GetCompatibleClassId();
    const LayoutPickInfo info = m_View.PickInfo(x, y);
    std::vector<CK_ID> targets;
    const bool multiple = m_State.objectSelection.Objects().size() > 1 &&
                          (m_State.objectSelection.Contains(info.entity3d) || m_State.objectSelection.Contains(info.character));
    if (multiple) {
        for (CK_ID id : AliveSelection())
            if (CKIsChildClassOf(context->GetObject(id), cid)) targets.push_back(id);
        if (targets.empty()) {
            m_State.Log(LogLevel::Warning, "Incompatible Class, cannot add behavior to selection.");
            return;
        }
    } else if (const CK_ID target = BlockTarget(info, cid, ImGui::GetIO().KeyCtrl)) {
        targets.push_back(target);
    } else {
        return;
    }
    // A drop still asking: its values stay as they are.
    if (m_Doc.dropEdit) m_Doc.dropEdit->Commit();
    CK_ID script = 0, block = 0;
    auto drop = std::make_unique<Edit>(*m_State.document, "Drop Behavior");
    for (CK_ID id : targets) DropBlockOn(id, guid, script, block);
    // Dev asks for the inputs of the Building Block (of the last one dropped).
    CKBehavior *dropped = block ? CKBehavior::Cast(context->GetObject(block)) : nullptr;
    if (dropped && (dropped->GetInputParameterCount() > 0 || dropped->IsUsingTarget())) {
        m_Doc.dropBlock = block;
        m_Doc.dropScript = script;
        m_Doc.dropEdit = std::move(drop); // the values set join the drop, Cancel takes it back
        m_Doc.openDropParameters = true;
    } else {
        drop->Commit();
    }
    if (!block) return;
    m_State.RememberBlock(guid);
    m_State.events.Post(ShowElement{*m_State.document, script, ElementRef{ElementKind::Behavior, script, block}, block});
}

// Dev.exe sub_4C3510 (DropBuildingBlockOn): the object's first Script shown
// in the Schematic, else a new "<object> Script"; the refusals logged.
bool LayoutWindow::DropBlockOn(CK_ID object, const CKGUID &guid, CK_ID &script, CK_ID &block) {
    CKContext *context = m_State.host.Context();
    CKBeObject *owner = CKBeObject::Cast(context->GetObject(object));
    if (!owner || CKBehavior::Cast(owner)) return false;
    CK_ID into = 0;
    bool insertRefused = false;
    const EditResult dropped =
        DropBuildingBlockOn(*m_State.document, object, guid, m_State.Schematic().HiddenIds(*m_State.document), &into, &insertRefused);
    if (!dropped.ok) {
        if (!insertRefused) {
            m_State.Log(LogLevel::Warning, AnsiToUtf8(dropped.message));
            return false;
        }
        CKObjectDeclaration *declaration = CKGetObjectDeclarationFromGuid(guid);
        m_State.Log(LogLevel::Warning, "Incompatible Class, cannot add behavior <" +
                                           AnsiToUtf8(declaration && declaration->GetName() ? declaration->GetName() : "") +
                                           "> to object <" + DevObjectName(owner) + ">: " + AnsiToUtf8(dropped.message));
        return false;
    }
    script = into;
    block = dropped.created;
    return true;
}

void LayoutWindow::DropResources(int x, int y, const std::vector<ResourceFile> &files) {
    const auto info = DropInfo(x, y, "resource");
    if (info.status != EDITOR_OK)
        return;
    const auto &io = ImGui::GetIO();
    std::vector<EditorString> paths;
    std::vector<uint64_t> classes;
    for (const auto &file : files) {
        auto cid = file.cid;
        if (cid == CKCID_TEXTURE || cid == CKCID_SPRITE || cid == CKCID_SPRITE3D)
            cid = io.KeyShift && io.KeyCtrl ? CKCID_2DENTITY
                  : io.KeyShift             ? CKCID_SPRITE3D
                  : io.KeyCtrl              ? CKCID_SPRITE
                                            : CKCID_TEXTURE;
        paths.push_back(EditorText(file.path));
        classes.push_back(cid);
    }
    const auto *targets = EditorClient::Find(info, "objects");
    if (!targets)
        return;
    Execute("resource.load",
            {{EDITOR_TEXT("paths"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {paths.data(), uint32_t(paths.size())}},
             {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {classes.data(), uint32_t(classes.size())}},
             ObjectsArgument("targets", targets->objects)});
}
void LayoutWindow::DocumentClosed() {
    m_Capturing = 0;
    CancelEdits();
    if (m_Doc.dropEdit) m_Doc.dropEdit->Commit(); // the document goes with its history
    // A flight going on ends with the composition, not recorded.
    if (m_Tool == LayoutTool::Fly) m_Tool = LayoutTool::Select;
    // The 3D Layout Explorer closes without restoring the selection it saved.
    m_Doc = DocumentState();
    for (auto &settings : m_Transform)
        settings.referent = {};
    m_CurveEdit.referent = {};
    m_DropHighlight = {};
    m_DropBox.Clear();
}
std::pair<CK_ID, CK_ID> LayoutWindow::DropAsking(const std::function<std::pair<CK_ID, CK_ID>()> &drop) {
    if (!m_State.HasDocument()) return {0, 0};
    // A drop still asking: its values stay as they are.
    if (m_Doc.dropEdit) m_Doc.dropEdit->Commit();
    m_Doc.dropEdit.reset();
    m_Doc.dropBlock = 0;
    auto edit = std::make_unique<Edit>(*m_State.document, "Drop Behavior");
    const std::pair<CK_ID, CK_ID> made = drop();
    CKBehavior *dropped = made.second ? CKBehavior::Cast(m_State.host.Context()->GetObject(made.second)) : nullptr;
    if (dropped && (dropped->GetInputParameterCount() > 0 || dropped->IsUsingTarget())) {
        m_Doc.dropBlock = made.second;
        m_Doc.dropScript = made.first;
        m_Doc.dropEdit = std::move(edit);
        m_Doc.openDropParameters = true;
    } else {
        edit->Commit();
    }
    return made;
}
void LayoutWindow::DropParametersDialog() {
    CKContext *context = m_State.host.Context();
    CKBehavior *block = m_Doc.dropBlock && m_State.HasDocument() ? CKBehavior::Cast(context->GetObject(m_Doc.dropBlock)) : nullptr;
    if (!block) {
        if (m_Doc.dropEdit) m_Doc.dropEdit->Commit();
        m_Doc.dropEdit.reset();
        m_Doc.dropBlock = 0;
        m_Doc.openDropParameters = false;
        m_DropBox.Clear();
        return;
    }
    const char *id = "###DropParameters";
    if (m_Doc.openDropParameters && m_OpenPopups) {
        m_DropBox.Clear();
        m_DropBox.AddInputs(block);
        ImGui::OpenPopup(id);
        m_Doc.openDropParameters = false;
    }
    const std::string title = AnsiToUtf8(block->GetName() ? block->GetName() : "") + " - Edit Parameters";
    const DialogAction action = EditParametersDialog(id, title, m_DropBox, false);
    if (action == DialogAction::Ok) {
        // The values set join the drop's undo step.
        m_DropBox.Apply(m_Doc.dropScript);
        if (m_Doc.dropEdit) m_Doc.dropEdit->Commit();
        m_Doc.dropEdit.reset();
        m_Doc.dropBlock = 0;
        m_DropBox.Clear();
    } else if (action == DialogAction::Cancel) {
        m_DropBox.Clear();
        m_Doc.dropEdit.reset(); // taken back
        m_Doc.dropBlock = 0;
    }
}

} // namespace vdev
