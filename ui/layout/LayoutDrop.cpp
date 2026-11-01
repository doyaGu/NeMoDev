// Resource and behavior drops use copied receivers and registered commands.
#include "ui/layout/LayoutWindow.h"
#include "ui/schematic/Snapshots.h"
#include "workspace/BuildingBlockDrag.h"
#include <cstring>

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
                std::vector<uint64_t> prototypes;
                for (const auto block : blocks)
                    prototypes.push_back((uint64_t(block.first) << 32) | block.second);
                DropBuildingBlocks(x, y, prototypes);
            } else {
                const auto info = DropInfo(x, y, "behavior", (uint64_t(source.first) << 32) | source.second);
                highlight = ObjectField(info, "highlight");
                m_DropText = EditorClient::Text(info, "text");
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

void LayoutWindow::DropBuildingBlocks(int x, int y, const std::vector<uint64_t> &prototypes) {
    HighlightDrop({});
    const double point[] = {double(x), double(y)};
    EditorValue position{EDITOR_TEXT("position"), EDITOR_REAL_LIST};
    position.reals = {point, 2};
    const EditorValue blocks{EDITOR_TEXT("prototypes"),
                             EDITOR_INTEGER_LIST,
                             {},
                             {},
                             0,
                             {},
                             {prototypes.data(), uint32_t(prototypes.size())}};
    Execute("scene.drop-behavior",
            {position, blocks, IntegerArgument("ctrl", ImGui::GetIO().KeyCtrl), IntegerArgument("ask", 1)});
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
    if (m_Tool == LayoutTool::Fly)
        m_Tool = LayoutTool::Select;
    m_Doc = DocumentState();
    for (auto &settings : m_Transform)
        settings.referent = {};
    m_CurveEdit.referent = {};
    m_DropHighlight = {};
    m_DropBox.Clear();
}

void LayoutWindow::DropParametersDialog() {
    const auto revision = Client().Revision("layout.drop-parameters");
    if (revision != m_Doc.dropRevision) {
        m_Doc.dropInput = Client().Query("layout.drop-parameters");
        m_Doc.dropRevision = revision;
    }
    const auto &input = m_Doc.dropInput;
    const auto drop = EditorClient::Integer(input, "drop");
    if (input.status != EDITOR_OK || !drop) {
        m_Doc.drop = 0;
        m_DropBox.Clear();
        return;
    }
    const char *id = "###DropParameters";
    if (drop != m_Doc.drop && m_OpenPopups) {
        m_DropBox.Clear();
        const auto *inputs = EditorClient::Find(input, "inputs");
        bool added = inputs != nullptr;
        if (inputs)
            for (const auto &row : inputs->fields) {
                const EditorField *object = nullptr, *name = nullptr;
                for (const auto &field : row.fields) {
                    if (field.key == "object")
                        object = &field;
                    if (field.key == "name")
                        name = &field;
                }
                if (!object || !name || !m_DropBox.Add(object->object, name->text)) {
                    added = false;
                    break;
                }
            }
        if (!added) {
            const EditorValue args[] = {IntegerArgument("drop", drop), IntegerArgument("accept", 0)};
            Complete(m_CommandEditor.ApplyEdit("layout.finish-drop", {Client().Scope(), args, 2}), AfterCommand::None);
            m_DropBox.Clear();
            return;
        }
        ImGui::OpenPopup(id);
        m_Doc.drop = drop;
    }
    const auto action = EditParametersDialog(id, EditorClient::Text(input, "title"), m_DropBox, false);
    if (action != DialogAction::Ok && action != DialogAction::Cancel)
        return;
    std::vector<ObjectRef> values;
    if (action == DialogAction::Ok)
        for (size_t i = 0; i < m_DropBox.Count(); ++i)
            values.push_back(m_DropBox.WorkReference(i));
    const EditorValue args[] = {IntegerArgument("drop", drop), IntegerArgument("accept", action == DialogAction::Ok),
                                ObjectsArgument("values", values)};
    Complete(m_CommandEditor.ApplyEdit("layout.finish-drop", {Client().Scope(), args, 3}), AfterCommand::None);
    m_DropBox.Clear();
    m_Doc.drop = 0;
}
} // namespace vdev
