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

void LayoutWindow::DropTarget(const ImVec2 &at) {
    m_DropText.clear();
    m_View.SetDropTarget(0);
    if (!ImGui::BeginDragDropTarget()) return;
    const ImGuiIO &io = ImGui::GetIO();
    const int x = static_cast<int>(io.MousePos.x - at.x), y = static_cast<int>(io.MousePos.y - at.y);
    CKContext *context = m_State.host.Context();
    if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload(EDITOR_BUILDING_BLOCK_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        EditorBuildingBlock source{};
        std::vector<EditorBuildingBlock> blocks;
        if (ReadBuildingBlockDrag(payload->Data, size_t(payload->DataSize), source, blocks)) {
            const CKGUID guid(source.first, source.second);
            if (payload->IsDelivery()) {
                // Several Building Blocks dragged: each dropped (the last asks for its inputs).
                for (const auto &each : blocks)
                    DropBuildingBlock(x, y, CKGUID(each.first, each.second));
            } else {
                // Dev's <Action Info>: "Drop Behavior on %s", "Drop Behavior on Multi-Selection".
                const LayoutPickInfo info = m_View.PickInfo(x, y);
                CKObjectDeclaration *declaration = CKGetObjectDeclarationFromGuid(guid);
                const CK_CLASSID cid = declaration ? declaration->GetCompatibleClassId() : CKCID_BEOBJECT;
                const bool multiple =
                    m_State.objectSelection.Objects().size() > 1 && (m_State.objectSelection.Contains(info.entity3d) ||
                                                                     m_State.objectSelection.Contains(info.character));
                const CK_ID target = BlockTarget(info, cid, io.KeyCtrl);
                if (multiple)
                    m_DropText = "Drop Behavior on Multi-Selection";
                else if (CKObject *object = context->GetObject(target))
                    m_DropText = "Drop Behavior on " + DevObjectName(object);
                m_View.SetDropTarget(multiple ? info.entity3d : target);
            }
        }
    }
    if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload(EDITOR_RESOURCE_FILE_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        uint32_t source = 0;
        std::vector<ResourceFile> files;
        if (!ReadResourceDrag(payload->Data, size_t(payload->DataSize), source, files)) {
            ImGui::EndDragDropTarget();
            return;
        }
        const auto &file = files[source];
        if (payload->IsDelivery()) {
            DropResources(x, y, files);
        } else {
            const LayoutPickInfo info = m_View.PickInfo(x, y);
            m_View.SetDropTarget(info.character ? info.character : info.entity3d);
            // Images (Dev.exe sub_4C0F60): a texture, Shift a 3D sprite, Ctrl a 2D
            // sprite, both a 2D Entity (with the language pack's hint 369; Dev
            // shows the 3D sprite's, 368).
            if (file.cid == CKCID_TEXTURE || file.cid == CKCID_SPRITE || file.cid == CKCID_SPRITE3D)
                m_DropText = io.KeyShift && io.KeyCtrl ? "Drop as 2D Entity (Release CTRL or SHIFT for more)"
                             : io.KeyShift ? "Drop as 3D Sprite (Release SHIFT or hold CTRL for more)"
                             : io.KeyCtrl  ? "Drop as 2D Sprite (Release CTRL or hold SHIFT for more)"
                                           : "Drop as Texture (Hold CTRL or SHIFT for more)";
        }
    }
    // A Snapshot Buffer image (Dev.exe sub_4C0F60 "Bitmap", sub_4C0930): the
    // picture of the character or 3D entity under the mouse.
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(SNAPSHOT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        const auto input = InputState(x, y, true);
        const auto *character = EditorClient::Find(input, "character"), *entity = EditorClient::Find(input, "entity");
        const auto target = character && character->object ? character->object : entity ? entity->object : ObjectRef{};
        if (payload->IsDelivery()) {
            if (payload->DataSize == sizeof(SnapshotDrag) && target) {
                SnapshotDrag drag;
                std::memcpy(&drag, payload->Data, sizeof(drag));
                const auto result = ApplySnapshot(Client(), drag, target, false);
                Complete(result, AfterCommand::None);
            }
        } else
            m_View.SetDropTarget(target.object);
    }
    // An attribute from the Attribute Manager: on the object under the mouse,
    // or the selection it is in.
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(EDITOR_ATTRIBUTE_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        const LayoutPickInfo info = m_View.PickInfo(x, y);
        const CK_ID target = info.entity2d ? info.entity2d : info.character ? info.character : info.entity3d;
        const bool multiple = target && m_State.objectSelection.Objects().size() > 1 && m_State.objectSelection.Contains(target);
        if (payload->IsDelivery()) {
            EditorAttributePayload attribute;
            std::memcpy(&attribute, payload->Data, sizeof(attribute));
            if (target) m_State.DropAttributeOn(multiple ? AliveSelection() : std::vector<CK_ID>{target}, attribute);
        } else {
            m_View.SetDropTarget(target);
            if (multiple) m_DropText = "Drop Attribute on Multi-Selection";
            else if (CKObject *object = context->GetObject(target)) m_DropText = "Drop Attribute on " + DevObjectName(object);
        }
    }
    // A material from the Level Manager (Dev.exe sub_4C0A30): on the 3D
    // entity's mesh it replaces the material of the face under the mouse.
    const ImGuiPayload *dragged = ImGui::GetDragDropPayload();
    CK_ID material = 0;
    if (dragged && dragged->IsDataType(EDITOR_OBJECT_PAYLOAD) && dragged->DataSize == sizeof(ObjectRef)) {
        ObjectRef reference{}; std::memcpy(&reference, dragged->Data, sizeof(reference));
        material = m_State.document->NativeId(reference);
        if (!CKMaterial::Cast(context->GetObject(material))) material = 0;
    }
    if (material) {
        const LayoutPickInfo info = m_View.PickInfo(x, y);
        CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(info.entity3d));
        CKMesh *mesh = entity ? entity->GetCurrentMesh() : nullptr;
        if (mesh && info.material)
            if (const ImGuiPayload *payload =
                    ImGui::AcceptDragDropPayload(EDITOR_OBJECT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                if (!payload->IsDelivery()) {
                    m_View.SetDropTarget(entity->GetID());
                } else if (m_State.HasDocument()) {
                    const EditResult result =
                        ReplaceMeshMaterial(*m_State.document, mesh->GetID(), info.material, material);
                    if (!result.ok) m_State.Log(LogLevel::Info, "Replace Material: " + AnsiToUtf8(result.message));
                }
            }
    }
    ImGui::EndDragDropTarget();
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

// A Data Resources file (Dev.exe sub_4C0A30): loaded as on the level, its
// Scripts and animations given to the object under the mouse; images are a
// texture, Shift a 3D sprite, Ctrl a 2D sprite, both a 2D Entity (sub_4BF320).
void LayoutWindow::DropResources(int x, int y, const std::vector<ResourceFile> &files) {
    if (!m_State.HasDocument()) return;
    const LayoutPickInfo info = m_View.PickInfo(x, y);
    const ImGuiIO &io = ImGui::GetIO();
    std::vector<EditorString> paths;
    std::vector<uint64_t> classes;
    for (const auto &file : files) {
        CK_CLASSID cid = file.cid;
        if (cid == CKCID_TEXTURE || cid == CKCID_SPRITE || cid == CKCID_SPRITE3D)
            cid = io.KeyShift && io.KeyCtrl ? CKCID_2DENTITY : io.KeyShift ? CKCID_SPRITE3D
                  : io.KeyCtrl ? CKCID_SPRITE : CKCID_TEXTURE;
        paths.push_back(EditorText(file.path));
        classes.push_back(uint64_t(cid));
    }
    const auto target = m_State.document->Reference(info.character ? info.character : info.entity3d ? info.entity3d : info.entity2d);
    const EditorValue values[] = {
        {EDITOR_TEXT("paths"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {paths.data(), uint32_t(paths.size())}},
        {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {classes.data(), uint32_t(classes.size())}},
        {EDITOR_TEXT("targets"), EDITOR_OBJECT_LIST, {}, {}, 0, {&target, target ? 1u : 0u}}};
    auto &editor = m_State.workspace->Application();
    editor.Execute("resource.load", {editor.Scope(), values, 3});
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
