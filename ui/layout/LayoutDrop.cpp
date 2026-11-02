// Resource and behavior drops use copied receivers and registered commands.
#include "ui/layout/LayoutWindow.h"
#include "ui/schematic/Snapshots.h"
#include "workspace/BuildingBlockDrag.h"
#include "workspace/ObjectDrag.h"
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
    if (target == Window().dropHighlight)
        return;
    const auto result = Execute("layout.highlight-drop", {ObjectArgument("target", target)});
    if (result.status == EDITOR_OK || result.status == EDITOR_QUEUED)
        Window().dropHighlight = target;
}

void LayoutWindow::DropTarget(const ImVec2 &at) {
    Window().dropText.clear();
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
                Window().dropText = EditorClient::Text(info, "text");
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
                const uint64_t cid = files[source].cid;
                const auto classification = Client().Query("resource.import-classes", {
                    {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {&cid, 1}}, IntegerArgument("image-kind", 0)});
                const auto *images = EditorClient::Find(classification, "images");
                if (classification.status == EDITOR_OK && images && images->integers.size() == 1 && images->integers[0])
                    Window().dropText = io.KeyShift && io.KeyCtrl ? "Drop as 2D Entity (Release CTRL or SHIFT for more)"
                                        : io.KeyShift ? "Drop as 3D Sprite (Release SHIFT or hold CTRL for more)"
                                        : io.KeyCtrl  ? "Drop as 2D Sprite (Release CTRL or hold SHIFT for more)"
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
                Window().dropText = EditorClient::Text(info, "text");
            }
        }
    }
    if (const auto *dragged = ImGui::GetDragDropPayload(); dragged && dragged->IsDataType(EDITOR_OBJECT_PAYLOAD)) {
        const auto source = ObjectDragSource(dragged->Data, size_t(dragged->DataSize));
        if (source) {
            const auto info = DropInfo(x, y, "material", 0, source);
            const auto mesh = ObjectField(info, "mesh"), material = ObjectField(info, "material");
            if (mesh && material)
                if (const auto *payload =
                        ImGui::AcceptDragDropPayload(EDITOR_OBJECT_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                    if (!payload->IsDelivery())
                        highlight = ObjectField(info, "highlight");
                    else
                        Execute("layout.replace-material",
                                {ObjectArgument("mesh", mesh), ObjectArgument("from", material),
                                 ObjectArgument("to", source)});
                }
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
        paths.push_back(EditorText(file.path));
        classes.push_back(file.cid);
    }
    const auto classification = Client().Query("resource.import-classes", {
        {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {classes.data(), uint32_t(classes.size())}},
        IntegerArgument("image-kind", io.KeyShift && io.KeyCtrl ? 4 : io.KeyShift ? 3 : io.KeyCtrl ? 2 : 1)});
    const auto *resolved = EditorClient::Find(classification, "classes");
    if (classification.status != EDITOR_OK || !resolved || resolved->integers.size() != classes.size()) return;
    classes = resolved->integers;
    const auto *targets = EditorClient::Find(info, "objects");
    if (!targets)
        return;
    Execute("resource.load",
            {{EDITOR_TEXT("paths"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {paths.data(), uint32_t(paths.size())}},
             {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {classes.data(), uint32_t(classes.size())}},
             ObjectsArgument("targets", targets->objects)});
}

void LayoutWindow::DocumentClosed() {
    if (!m_Window)
        return;
    Window().capturing = 0;
    CancelEdits();
    if (Window().tool == LayoutTool::Fly)
        Window().tool = LayoutTool::Select;
    Window().document = DocumentState();
    for (auto &settings : Window().transform)
        settings.referent = {};
    Window().curveEdit.referent = {};
    Window().dropHighlight = {};
    Window().dropBox.Clear();
}

void LayoutWindow::DropParametersDialog() {
    if (Window().document.dropFinish) {
        const auto result = Client().Completion(Window().document.dropFinish);
        if (result.status == EDITOR_QUEUED)
            return;
        Complete(result, AfterCommand::None);
        Window().document.dropFinish = 0;
        Window().dropBox.Clear();
        Window().document.drop = 0;
    }

    const auto revision = Client().Revision("layout.drop-parameters");
    if (revision != Window().document.dropRevision) {
        Window().document.dropInput = Client().Query("layout.drop-parameters");
        Window().document.dropRevision = revision;
    }
    const auto &input = Window().document.dropInput;
    const auto drop = EditorClient::Integer(input, "drop");
    if (input.status != EDITOR_OK || !drop) {
        Window().document.drop = 0;
        Window().dropBox.Clear();
        return;
    }
    const char *id = "###DropParameters";
    if (drop != Window().document.drop && Window().openPopups) {
        Window().dropBox.Clear();
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
                if (!object || !name || !Window().dropBox.Add(object->object, name->text)) {
                    added = false;
                    break;
                }
            }
        if (!added) {
            const EditorValue args[] = {IntegerArgument("drop", drop), IntegerArgument("accept", 0)};
            Complete(m_CommandEditor.ApplyEdit("layout.finish-drop", {Client().Scope(), args, 2}), AfterCommand::None);
            Window().dropBox.Clear();
            return;
        }
        ImGui::OpenPopup(id);
        Window().document.drop = drop;
    }
    const auto action = EditParametersDialog(id, EditorClient::Text(input, "title"), Window().dropBox, false);
    if (action != DialogAction::Ok && action != DialogAction::Cancel)
        return;
    std::vector<ObjectRef> values;
    if (action == DialogAction::Ok)
        for (size_t i = 0; i < Window().dropBox.Count(); ++i)
            values.push_back(Window().dropBox.WorkReference(i));
    const EditorValue args[] = {IntegerArgument("drop", drop), IntegerArgument("accept", action == DialogAction::Ok),
                                ObjectsArgument("values", values)};
    const auto result = m_CommandEditor.ApplyEdit("layout.finish-drop", {Client().Scope(), args, 3});
    if (result.status == EDITOR_QUEUED)
        Window().document.dropFinish = result.operation;
    else {
        Complete(result, AfterCommand::None);
        Window().dropBox.Clear();
        Window().document.drop = 0;
    }
}
} // namespace vdev
