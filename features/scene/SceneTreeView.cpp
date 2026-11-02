#include "features/scene/SceneViews.h"
#include "features/scene/SceneViewData.h"
#include "editor/ImGuiDrawing.h"
#include "workspace/BuildingBlockDrag.h"
#include "workspace/ResourceDrag.h"
#include "workspace/ObjectDrag.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "misc/cpp/imgui_stdlib.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <limits>

namespace vdev {
bool RegisterAttributeViews(EditorApplication &, std::string &);
namespace {
std::string Lower(std::string text) {
    for (auto &c : text) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}
void Icon(int id, ImVec2 at, float scale, bool executable = true) {
    if (!id) {
        return;
    }
    if (auto image = executable ? DevExeIcon(id) : DevIconImage(id)) {
        ImGui::GetWindowDrawList()->AddImage(image, at, ImVec2(at.x + 11 * scale, at.y + 11 * scale));
    }
}
class SceneTreeView {
  public:
    explicit SceneTreeView(bool hierarchy) : hierarchy(hierarchy) {}
    void Draw(const EditorHost *, EditorView, EditorViewFrame &);
    EditorStatus Navigate(const EditorRequest &);
    EditorStatus Inspect(EditorSink) const;
    EditorStatus Save(EditorSink) const;
    EditorStatus Restore(const EditorRequest &);
    uint64_t inspectionRevision = 1;
#ifdef VIRTOOLSDEV_UI_TESTS
    std::vector<SceneShownRow> shown;
    int shownFrame = -1;
#endif
  private:
    bool hierarchy;
    SceneTree tree;
    DocumentToken expandedDocument{};
    SceneTreeOptions options, loadedOptions;
    SceneSelection selection;
    uint64_t modelVersion = 0;
    std::map<uint64_t, bool> expanded;
    std::set<ObjectRef> hidden;
    std::vector<size_t> visible;
    ObjectRef anchor{}, section{}, group{}, focus{};
    size_t current = std::numeric_limits<size_t>::max();
    bool hideLayer = false, levelMode = true;
    int expandRequest = -1;
    float columns[9] = {20, 250, 30, 50, 135, 135, 90, 50, 100};
    EditorEdit rename = 0;
    bool renameFocus = false, renamePopup = false, findPopup = false, priorityPopup = false;
    std::string findText, worksetName;
    bool findObjects = true, findScripts = true;
    int priority = 0;
    ObjectRef priorityTarget{};
    uint64_t renameWorkset = 0;
    struct Pending {
        std::string title, text, command;
        std::vector<ObjectRef> objects;
        ObjectRef target{}, scene{};
        bool open = false;
        int deletion = 0;
        ObjectRef container{};
    } pending;
    bool IsOpen(size_t i) const {
        const auto f = expanded.find(tree.rows[i].key);
        return f == expanded.end() ? tree.rows[i].expanded : f->second;
    }
    bool Children(size_t i) const { return i + 1 < tree.rows.size() && tree.rows[i + 1].parent == i + 1; }
    int Depth(size_t i) const { return tree.rows[i].depth; }
    void OpenAncestors(size_t i) {
        while (tree.rows[i].parent) {
            i = tree.rows[i].parent - 1;
            expanded[tree.rows[i].key] = true;
        }
    }
    std::vector<ObjectRef> Targets() const { return {selection.objects.begin(), selection.objects.end()}; }
    std::vector<ObjectRef> Targets(const SceneRow &row) const {
        return selection.objects.count(row.object) ? Targets() : std::vector<ObjectRef>{row.object};
    }
    void Choose(size_t i, bool ctrl, bool shift);
    void Refresh(const EditorClient &);
    void Expand(int);
    void Find(bool forward);
    void BeginRename(const EditorClient &, const SceneRow &);
    struct DeleteRows {
        std::vector<ObjectRef> scripts, leave, destroy, members;
        ObjectRef scene{}, group{};
        bool groups = false;
    } deleting;
    void Delete(const EditorClient &);
    void ContinueDelete(const EditorClient &, int step);
    void Menu(const EditorClient &, const SceneRow &);
    void Drop(const EditorClient &, size_t);
    void Toolbar(const EditorClient &, ImVec2, float);
    void Side(const EditorClient &, ImVec2, float);
    void Rows(const EditorClient &, ImVec2, ImVec2, float);
    void Keyboard(const EditorClient &);
    void Dialogs(const EditorClient &, EditorViewFrame &);
    void Send(const EditorClient &client, const char *command, std::vector<EditorValue> args = {}) {
        const auto objects = Targets();
        args.push_back(ObjectsArgument("objects", objects));
        client.Command(command, args);
    }
};
#ifdef VIRTOOLSDEV_UI_TESTS
std::map<EditorView, SceneTreeView *> testViews;
#endif
void SceneTreeView::Refresh(const EditorClient &client) {
    const auto document = client.Scope().document;
    if (tree.document != document) {
        tree = {};
        tree.document = document;
        selection.Reset();
        if (expandedDocument != document) {
            expanded.clear();
        }
        expandedDocument = document;
        hidden.clear();
        anchor = section = group = focus = {};
        current = std::numeric_limits<size_t>::max();
        rename = 0;
        renameWorkset = 0;
        renamePopup = findPopup = priorityPopup = false;
        pending = {};
        deleting = {};
        options.context = {};
        modelVersion = 0;
        ++inspectionRevision;
    }
    const auto before = selection.objects;
    selection.Pull(client);
    if (before != selection.objects && !selection.objects.empty()) {
        for (size_t i = 0; i < tree.rows.size(); ++i) {
            if (selection.objects.count(tree.rows[i].object)) {
                current = i;
                anchor = tree.rows[i].object;
                break;
            }
        }
    }
    const auto version = client.Revision("scene.tree");
    if (version == modelVersion && options.priority == loadedOptions.priority &&
        options.reversed == loadedOptions.reversed && options.context == loadedOptions.context &&
        options.meshes == loadedOptions.meshes && options.materials == loadedOptions.materials &&
        options.textures == loadedOptions.textures) {
        return;
    }
    if (!document.value) {
        tree.rows.clear();
        return;
    }
    SceneTree next;
    auto status = QuerySceneTree(client, options, hierarchy, next);
    if (status == EDITOR_STALE && options.context) {
        options.context = {};
        levelMode = true;
        status = QuerySceneTree(client, options, hierarchy, next);
    }
    if (status != EDITOR_OK || next.document != document) {
        return;
    }
    tree = std::move(next);
    modelVersion = version;
    loadedOptions = options;
    std::set<ObjectRef> liveObjects;
    std::set<uint64_t> liveKeys;
    for (const auto &row : tree.rows) {
        if (row.object) {
            liveObjects.insert(row.object);
        }
        liveKeys.insert(row.key);
    }
    const auto live = [&](ObjectRef ref) { return liveObjects.count(ref) != 0; };
    for (auto it = selection.objects.begin(); it != selection.objects.end();) {
        if (!live(*it)) {
            it = selection.objects.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = hidden.begin(); it != hidden.end();) {
        if (!live(*it)) {
            it = hidden.erase(it);
        } else {
            ++it;
        }
    }
    // Removed branches must not keep accumulating window state.
    for (auto it = expanded.begin(); it != expanded.end();) {
        if (!liveKeys.count(it->first)) {
            it = expanded.erase(it);
        } else {
            ++it;
        }
    }
    if (anchor && !live(anchor)) {
        anchor = {};
    }
    if (section && !live(section)) {
        section = {};
    }
    if (group && !live(group)) {
        group = {};
    }
    ++inspectionRevision;
}
void SceneTreeView::Choose(size_t i, bool ctrl, bool shift) {
    const auto &row = tree.rows[i];
    if (shift && anchor) {
        size_t start = 0, end = 0;
        for (size_t k = 0; k < visible.size(); ++k) {
            if (tree.rows[visible[k]].object == anchor) {
                start = k;
            }
            if (visible[k] == i) {
                end = k;
            }
        }
        if (!ctrl) {
            selection.objects.clear();
        }
        for (size_t k = std::min(start, end); k <= std::max(start, end); ++k) {
            if (tree.rows[visible[k]].object) {
                selection.objects.insert(tree.rows[visible[k]].object);
            }
        }
    } else if (ctrl && row.object) {
        if (!selection.objects.erase(row.object)) {
            selection.objects.insert(row.object);
        }
        anchor = row.object;
    } else {
        selection.objects.clear();
        if (row.object) {
            selection.objects.insert(row.object);
        }
        anchor = row.object;
    }
    current = i;
    section = row.section;
    group = row.group;
    ++inspectionRevision;
}
void SceneTreeView::Expand(int value) {
    bool any = !selection.objects.empty();
    for (size_t i = 0; i < tree.rows.size(); ++i) {
        size_t parent = i;
        bool chosen = !any;
        for (;;) {
            if (selection.objects.count(tree.rows[parent].object)) {
                chosen = true;
            }
            if (!tree.rows[parent].parent) {
                break;
            }
            parent = tree.rows[parent].parent - 1;
        }
        if (chosen && Children(i)) {
            expanded[tree.rows[i].key] = value != 0;
        }
    }
}
void SceneTreeView::Find(bool forward) {
    if (tree.rows.empty() || findText.empty()) {
        return;
    }
    const auto needle = Lower(findText);
    const auto size = tree.rows.size();
    const auto start = current < size ? current : forward ? size - 1 : 0;
    for (size_t k = 1; k <= size; ++k) {
        const size_t i = forward ? (start + k) % size : (start + size - k % size) % size;
        const auto &row = tree.rows[i];
        if (!row.object || (row.kind == EDITOR_SCENE_SCRIPT ? !findScripts : !findObjects) ||
            Lower(row.name).find(needle) == std::string::npos) {
            continue;
        }
        Choose(i, false, false);
        OpenAncestors(i);
        focus = row.object;
        break;
    }
}
void SceneTreeView::BeginRename(const EditorClient &client, const SceneRow &row) {
    if (!row.object || rename) {
        return;
    }
    if (client.BeginEdit("object.name", {ObjectArgument("target", row.object)}, rename) != EDITOR_OK) {
        return;
    }
    renameFocus = true;
    renamePopup = !hierarchy;
}
void SceneTreeView::Delete(const EditorClient &client) {
    const auto objects = Targets();
    const auto plan =
        client.Query("scene.delete-plan", {ObjectsArgument("objects", objects), ObjectArgument("scene", section),
                                           ObjectArgument("group", group)});
    if (plan.status != EDITOR_OK) {
        return;
    }
    deleting = {SceneObjects(plan, "scripts"),
                SceneObjects(plan, "leave"),
                SceneObjects(plan, "destroy"),
                SceneObjects(plan, "members"),
                section,
                group,
                EditorClient::Integer(plan, "groups") != 0};
    const auto message = EditorClient::Text(plan, "message");
    if (!message.empty()) {
        pending = {"Delete Objects", message, "", {}, {}, {}, true};
        pending.deletion = 1;
    } else {
        ContinueDelete(client, 1);
    }
}
void SceneTreeView::ContinueDelete(const EditorClient &client, int step) {
    if (step == 1 && !deleting.scripts.empty()) {
        pending = {"Confirm Script(s) Deletion",
                   "Delete the Script(s) selected?",
                   "script.delete",
                   deleting.scripts,
                   {},
                   {},
                   true};
        pending.deletion = 2;
        return;
    }
    if (step <= 2 && !deleting.leave.empty()) {
        if (deleting.groups) {
            pending = {"Remove Group from Scene",
                       "Remove every Object in Group from Scene ?",
                       "object.remove-from-scene",
                       deleting.leave,
                       {},
                       deleting.scene,
                       true};
            pending.deletion = 3;
            return;
        }
        client.Command("object.remove-from-scene",
                       {ObjectsArgument("objects", deleting.leave), ObjectArgument("scene", deleting.scene),
                        IntegerArgument("dependencies", 1)});
    }
    if (!deleting.members.empty()) {
        client.Command("object.remove-from-group",
                       {ObjectsArgument("objects", deleting.members), ObjectArgument("group", deleting.group)});
    }
    if (!deleting.destroy.empty()) {
        client.Command("object.confirm-delete", {ObjectsArgument("objects", deleting.destroy)});
    }
    deleting = {};
}
void SceneTreeView::Menu(const EditorClient &client, const SceneRow &row) {
    const bool script = (row.types & EDITOR_OBJECT_SCRIPT) != 0;
    if (script && ImGui::MenuItem("Show in Schematic")) {
        client.Command("script.show", {ObjectArgument("target", row.object), IntegerArgument("focus", 1)});
    }
    if (ImGui::MenuItem("Setup", nullptr, false, bool(row.object))) {
        client.Command("object.open-setup", {ObjectArgument("target", row.object)});
    }
    if ((row.types & EDITOR_OBJECT_SCENE) && ImGui::MenuItem("Launch Scene")) {
        client.Command("scene.launch", {ObjectArgument("target", row.object)});
    }
    ImGui::Separator();
    for (int i = 0; i < 3; ++i) {
        const char *labels[] = {"Set Initial Conditions", "Restore Initial Conditions", "Remove Initial Conditions"};
        const char *hierarchyLabels[] = {"Set Initial Conditions on Hierarchy",
                                         "Restore Initial Conditions on Hierarchy",
                                         "Remove Initial Conditions on Hierarchy"};
        if (ImGui::MenuItem(hierarchy ? hierarchyLabels[i] : labels[i])) {
            Send(client, "scene.initial-conditions",
                 {IntegerArgument("mode", i), IntegerArgument("hierarchy", hierarchy)});
        }
    }
    if (client.Host()->menu(client.Host()->data,
                            hierarchy ? EditorString EDITOR_TEXT("hierarchy.context")
                                      : EditorString EDITOR_TEXT("level.context"),
                            client.Scope(), row.object, &rename) == EDITOR_OK) {
        anchor = row.object;
        renameFocus = true;
        renamePopup = !hierarchy;
    }
    if (ImGui::MenuItem(hierarchy ? "Edit Render Priority" : "Edit Priority", nullptr, false,
                        row.priority >= 0 || (hierarchy && (row.types & EDITOR_OBJECT_RENDER)))) {
        priorityTarget = row.object;
        priority = hierarchy ? row.renderPriority : row.priority;
        priorityPopup = true;
    }
    if (hierarchy) {
        return;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Activate", "A")) {
        Send(client, "scene.flags",
             {ObjectArgument("target", row.object),
              ObjectArgument("scene", levelMode ? tree.levelScene : options.context), IntegerArgument("mode", 3)});
    }
    if (ImGui::MenuItem("Deactivate", "D")) {
        Send(client, "scene.flags",
             {ObjectArgument("target", row.object),
              ObjectArgument("scene", levelMode ? tree.levelScene : options.context), IntegerArgument("mode", 4)});
    }
    for (auto kind : {EDITOR_OBJECT_SCENE, EDITOR_OBJECT_GROUP, EDITOR_OBJECT_PLACE}) {
        const char *label = kind == EDITOR_OBJECT_SCENE   ? "Send To Scene"
                            : kind == EDITOR_OBJECT_GROUP ? "Send To Group"
                                                          : "Send to Place";
        if (!ImGui::BeginMenu(label)) {
            continue;
        }
        std::set<ObjectRef> seen;
        for (const auto &target : tree.rows) {
            if ((target.types & kind) && seen.insert(target.object).second) {
                if (ImGui::MenuItem(target.name.c_str())) {
                    if (kind == EDITOR_OBJECT_SCENE) {
                        Send(client, "scene.send-to-scene",
                             {ObjectArgument("target", target.object), IntegerArgument("dependencies", 1)});
                    } else {
                        Send(client, kind == EDITOR_OBJECT_GROUP ? "scene.send-to-group" : "scene.send-to-place",
                             {ObjectArgument("target", target.object)});
                    }
                }
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Remove From Place")) {
        Send(client, "scene.remove-from-place");
    }
    if (ImGui::MenuItem("Place Selected in New Group", "G")) {
        Send(client, "object.create-group", {ObjectArgument("scene", tree.currentScene)});
    }
    if (ImGui::MenuItem("Place Selected in New Place", "P")) {
        Send(client, "object.create-place");
    }
    if (ImGui::MenuItem("Show Selected in 3D Layout", "V")) {
        Send(client, "scene.visibility", {IntegerArgument("mode", 0)});
    }
    if (ImGui::MenuItem("Hide Selected in 3D Layout", "H")) {
        Send(client, "scene.visibility", {IntegerArgument("mode", 1)});
    }
    if (row.group) {
        if (ImGui::MenuItem("Move Up", "+")) {
            Send(client, "scene.group-order",
                 {ObjectArgument("target", row.group), IntegerArgument("value", uint64_t(-1))});
        }
        if (ImGui::MenuItem("Move Down", "-")) {
            Send(client, "scene.group-order", {ObjectArgument("target", row.group), IntegerArgument("value", 1)});
        }
    }
    if (ImGui::BeginMenu("Send to Workset")) {
        std::set<int> seen;
        for (const auto &w : tree.rows) {
            if (w.kind == EDITOR_SCENE_WORKSET && seen.insert(w.workset).second) {
                if (ImGui::MenuItem(w.name.c_str())) {
                    Send(client, "scene.workset-add", {IntegerArgument("workset", w.workset)});
                }
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Add Attributes", (row.types & EDITOR_OBJECT_BEHAVIORAL) && !script)) {
        if (ImGui::MenuItem("Add Attribute...")) {
            Send(client, "attribute.choose");
        }
        const auto catalog = QuerySceneAttributes(client, Targets());
        for (const auto &category : catalog.categories) {
            if (ImGui::BeginMenu(category.name.empty() ? "(No category)" : category.name.c_str())) {
                for (auto id : category.types) {
                    if (const auto *type = catalog.Find(id)) {
                        if (ImGui::MenuItem(type->name.c_str())) {
                            Send(client, "attribute.add",
                                 {IntegerArgument("type", id), IntegerArgument("catalog", catalog.version)});
                        }
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndMenu();
    }
    if (row.saveOption >= 0 && ImGui::BeginMenu("Set Save Options")) {
        const char *bitmaps[] = {"Raw Data", "External", "Image Format", "Use Global", "Include Original File"};
        const char *sounds[] = {"External", "Include Original File", "Use Global"};
        const bool sound = (row.types & EDITOR_OBJECT_SOUND) != 0;
        for (int i = 0; i < (sound ? 3 : 5); ++i) {
            if (ImGui::MenuItem(sound ? sounds[i] : bitmaps[i], nullptr, row.saveOption == i)) {
                Send(client, "scene.save-options", {IntegerArgument("value", i)});
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem((row.types & (EDITOR_OBJECT_LEVEL | EDITOR_OBJECT_SCENE)) ? "Save Objects as..."
                                                                                  : "Save as...")) {
        Send(client, "scene.export");
    }
    if (ImGui::MenuItem("Copy", "Ctrl+C")) {
        Send(client, "scene.copy");
    }
    if (ImGui::MenuItem("Paste", "Ctrl+V")) {
        Send(client, "scene.paste", {ObjectArgument("scene", section)});
    }
    if (ImGui::MenuItem("Delete", "Delete", false, !(row.types & EDITOR_OBJECT_LEVEL))) {
        Delete(client);
    }
    if (ImGui::MenuItem("Create Script", "S", false, bool(row.types & EDITOR_OBJECT_BEHAVIORAL) && !script)) {
        Send(client, "script.create");
    }
    if (ImGui::MenuItem("Load Script...", nullptr, false, bool(row.types & EDITOR_OBJECT_BEHAVIORAL) && !script)) {
        client.Command("script.load", {ObjectArgument("target", row.object)});
    }
    if (ImGui::MenuItem("Select Children")) {
        std::set<ObjectRef> children;
        for (const auto &r : tree.rows) {
            if (r.parent && selection.objects.count(tree.rows[r.parent - 1].object) && r.object) {
                children.insert(r.object);
            }
        }
        selection.objects = std::move(children);
        ++inspectionRevision;
    }
}
void SceneTreeView::Drop(const EditorClient &client, size_t index) {
    if (!ImGui::BeginDragDropTarget()) {
        return;
    }
    const auto &row = tree.rows[index];
    if (const auto *payload =
            ImGui::AcceptDragDropPayload(hierarchy ? EDITOR_HIERARCHY_PAYLOAD : EDITOR_OBJECT_PAYLOAD)) {
        const auto objects = ReadObjectDrag(payload->Data, size_t(payload->DataSize));
        if (!objects.empty()) {
            if (hierarchy) {
                client.Command("scene.hierarchy",
                               {ObjectsArgument("objects", objects), ObjectArgument("target", row.object),
                                IntegerArgument("background", uint64_t(int64_t(row.root == 1   ? 1
                                                                               : row.root == 2 ? 0
                                                                                               : -1)))});
            } else {
                const SceneRow *owner = &row;
                bool inWorkset = row.kind == EDITOR_SCENE_WORKSET;
                std::vector<const SceneRow *> ancestors;
                for (auto parent = row.parent; parent; parent = tree.rows[parent - 1].parent) {
                    const auto &above = tree.rows[parent - 1];
                    ancestors.push_back(&above);
                    inWorkset = inWorkset || above.kind == EDITOR_SCENE_WORKSET;
                }
                if (!row.object || (row.types & EDITOR_OBJECT_SCRIPT)) {
                    owner = nullptr;
                    for (const auto *above : ancestors) {
                        if (above->object && !(above->types & EDITOR_OBJECT_SCRIPT)) {
                            owner = above;
                            break;
                        }
                    }
                }
                ObjectRef target = owner ? owner->object : ObjectRef{}, container{};
                if (owner && (owner->types & (EDITOR_OBJECT_SCENE | EDITOR_OBJECT_LEVEL | EDITOR_OBJECT_PLACE |
                                              EDITOR_OBJECT_GROUP))) {
                    container = target;
                } else if (!inWorkset) {
                    for (const auto *above : ancestors) {
                        if (above->object && above != owner &&
                            (above->types &
                             (EDITOR_OBJECT_SCENE | EDITOR_OBJECT_LEVEL | EDITOR_OBJECT_PLACE | EDITOR_OBJECT_GROUP))) {
                            container = above->object;
                            break;
                        }
                    }
                }
                if (row.kind == EDITOR_SCENE_WORKSET) {
                    client.Command("scene.workset-add",
                                   {ObjectsArgument("objects", objects), IntegerArgument("workset", row.workset)});
                } else if (target) {
                    if (ImGui::GetIO().KeyShift) {
                        client.Command("scene.copy-drag",
                                       {ObjectsArgument("objects", objects), ObjectArgument("target", target),
                                        ObjectArgument("container", container)});
                    } else {
                        const auto plan = client.Query("scene.move-plan", {ObjectsArgument("objects", objects),
                                                                           ObjectArgument("target", target),
                                                                           ObjectArgument("container", container)});
                        if (plan.status == EDITOR_OK && EditorClient::Integer(plan, "groups")) {
                            pending = {"Add Group to Scene",
                                       "Add every Object in Group to Scene ?",
                                       "scene.move",
                                       objects,
                                       target,
                                       {},
                                       true};
                            pending.container = container;
                        } else if (plan.status == EDITOR_OK) {
                            client.Command("scene.move",
                                           {ObjectsArgument("objects", objects), ObjectArgument("target", target),
                                            ObjectArgument("container", container),
                                            IntegerArgument("dependencies", 1)});
                        }
                    }
                }
            }
        }
    }
    if (!hierarchy) {
        if (const auto *payload = ImGui::AcceptDragDropPayload("SCHEMATIC_ATTRIBUTE")) {
            if (payload->DataSize == sizeof(EditorAttributePayload)) {
                EditorAttributePayload attribute{};
                std::memcpy(&attribute, payload->Data, sizeof(attribute));
                const auto targets = Targets(row);
                client.Command("attribute.add",
                               {ObjectsArgument("objects", targets), IntegerArgument("type", attribute.type),
                                IntegerArgument("catalog", attribute.catalog),
                                ObjectArgument("source", attribute.value)});
            }
        }
        if (const auto *payload = ImGui::AcceptDragDropPayload("SCHEMATIC_RESOURCE_FILE")) {
            uint32_t source = 0;
            std::vector<ResourceFile> files;
            if (ReadResourceDrag(payload->Data, size_t(payload->DataSize), source, files)) {
                std::vector<EditorString> paths;
                std::vector<uint64_t> classes;
                for (const auto &file : files) {
                    paths.push_back(EditorText(file.path));
                    classes.push_back(file.cid);
                }
                const auto objects = Targets(row);
                std::vector<ObjectRef> above;
                for (auto parent = row.parent; parent; parent = tree.rows[parent - 1].parent) {
                    if (tree.rows[parent - 1].object) {
                        above.push_back(tree.rows[parent - 1].object);
                    }
                }
                client.Command("resource.drop", {{EDITOR_TEXT("paths"),
                                                  EDITOR_TEXT_LIST,
                                                  {},
                                                  {},
                                                  0,
                                                  {},
                                                  {},
                                                  {paths.data(), uint32_t(paths.size())}},
                                                 {EDITOR_TEXT("classes"),
                                                  EDITOR_INTEGER_LIST,
                                                  {},
                                                  {},
                                                  0,
                                                  {},
                                                  {classes.data(), uint32_t(classes.size())}},
                                                 ObjectsArgument("targets", objects),
                                                 ObjectsArgument("above", above),
                                                 ObjectArgument("scene", row.section)});
            }
        }
        if (const auto *payload = ImGui::AcceptDragDropPayload(EDITOR_BUILDING_BLOCK_PAYLOAD)) {
            EditorBuildingBlock source{};
            std::vector<EditorBuildingBlock> blocks;
            if (ReadBuildingBlockDrag(payload->Data, size_t(payload->DataSize), source, blocks)) {
                const auto objects = Targets(row);
                std::vector<uint64_t> prototypes;
                for (const auto block : blocks)
                    prototypes.push_back((uint64_t(block.first) << 32) | block.second);
                const EditorValue list{EDITOR_TEXT("prototypes"),
                                       EDITOR_INTEGER_LIST,
                                       {},
                                       {},
                                       0,
                                       {},
                                       {prototypes.data(), uint32_t(prototypes.size())}};
                client.Command("scene.drop-behavior", {ObjectsArgument("objects", objects), list});
            }
        }
    }
    ImGui::EndDragDropTarget();
}
void SceneTreeView::Toolbar(const EditorClient &client, ImVec2 origin, float s) {
    auto at = [&](float x, float y) { return ImVec2(origin.x + x * s, origin.y + y * s); };
    auto *dl = ImGui::GetWindowDrawList();
    const float width = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(origin, at(width / s, 21), vi::Color(2));
    if (auto image = DevBitmap(834)) {
        dl->AddImage(image, origin, at(6, 21));
    }
    static const int EXPAND[5] = {976, 977, 973, 973, 976}, COLLAPSE[5] = {978, 974, 975, 975, 978},
                     SHOW[5] = {807, 807, 808, 808, 808};
    const float first = hierarchy ? 10 : 38;
    if (!hierarchy && DevBitmapButton("showhide", at(9, 1), SHOW, hideLayer, true, s, "Toggle Item Show/Hide")) {
        hideLayer = !hideLayer;
    }
    if (DevBitmapButton("expand", at(first, 1), EXPAND, false, true, s, "Expand Selected")) {
        Expand(1);
    }
    if (DevBitmapButton("collapse", at(first + 20, 1), COLLAPSE, false, true, s, "Collapse Selected")) {
        Expand(0);
    }
    if (hierarchy) {
        static const int MESH[5] = {1045, 1046, 1047, 1048, 1045}, MATERIAL[5] = {1052, 1042, 1043, 1044, 1052},
                         TEXTURE[5] = {1049, 1050, 1051, 1041, 1049};
        if (DevBitmapButton("meshes", at(78, 1), MESH, options.meshes, true, s, "Show/Hide Meshes")) {
            options.meshes = !options.meshes;
        }
        if (DevBitmapButton("materials", at(99, 1), MATERIAL, options.materials, true, s, "Show/Hide Materials")) {
            options.materials = !options.materials;
        }
        if (DevBitmapButton("textures", at(120, 1), TEXTURE, options.textures, true, s, "Show/Hide Textures")) {
            options.textures = !options.textures;
        }
    } else if (vi::ToolbarButton("setic", at(86, 3), at(221, 18), "Set IC For Selected", s)) {
        Send(client, "scene.initial-conditions", {IntegerArgument("mode", 0)});
    }
}
void SceneTreeView::Side(const EditorClient &client, ImVec2 origin, float s) {
    auto at = [&](float x, float y) { return ImVec2(origin.x + x * s, origin.y + y * s); };
    auto *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at(6, 21), ImVec2(at(76, 0).x, ImGui::GetWindowPos().y + ImGui::GetWindowHeight()), vi::Color(4));
    static const int PLACE[5] = {1028, 1029, 1030, 1030, 1028}, GROUP[5] = {1025, 1026, 1027, 1027, 1025},
                     ARRAY[5] = {1036, 1023, 1024, 1024, 1036}, SCENE[5] = {1031, 1032, 1033, 1033, 1031},
                     WORKSET[5] = {1035, 1034, 1022, 1022, 1035}, SCRIPT[5] = {1039, 1038, 1037, 1037, 1039};
    if (DevBitmapButton("createplace", at(10, 27), PLACE, false, true, s, "Create Place (P)")) {
        Send(client, "object.create-place");
    }
    if (DevBitmapButton("creategroup", at(30, 27), GROUP, false, true, s, "Create Group (G)")) {
        Send(client, "object.create-group", {ObjectArgument("scene", tree.currentScene)});
    }
    if (DevBitmapButton("createarray", at(50, 27), ARRAY, false, true, s, "Create Array (R)")) {
        client.Command("object.create-array");
    }
    if (DevBitmapButton("createscene", at(10, 45), SCENE, false, true, s, "Create Scene (C)")) {
        Send(client, "object.create-scene");
    }
    if (DevBitmapButton("createworkset", at(30, 45), WORKSET, false, true, s, "Create Workset (W)")) {
        Send(client, "scene.workset-create");
    }
    if (DevBitmapButton("createscript", at(50, 45), SCRIPT, false, !selection.objects.empty(), s,
                        "Create Script (S)")) {
        Send(client, "script.create");
    }
    ImGui::SetCursorScreenPos(at(9, 69));
    if (ImGui::Button("Level", ImVec2(62 * s, 18 * s))) {
        levelMode = true;
        options.context = {};
    }
    ImGui::SetCursorScreenPos(at(9, 91));
    if (ImGui::Button("Scene", ImVec2(62 * s, 18 * s))) {
        levelMode = false;
    }
    std::vector<const SceneRow *> scenes;
    std::set<ObjectRef> seen;
    for (const auto &row : tree.rows) {
        if (row.kind == EDITOR_SCENE_SCENE && seen.insert(row.object).second) {
            scenes.push_back(&row);
        }
    }
    std::sort(scenes.begin(), scenes.end(),
              [](const auto *a, const auto *b) { return Lower(a->name) < Lower(b->name); });
    if (!levelMode && !options.context && !scenes.empty()) {
        options.context = tree.currentScene != tree.levelScene ? tree.currentScene : scenes.front()->object;
    }
    static const int UP[5] = {671, 672, 673, 673, 671}, DOWN[5] = {668, 669, 670, 670, 668};
    auto step = [&](int delta) {
        auto i =
            std::find_if(scenes.begin(), scenes.end(), [&](const SceneRow *r) { return r->object == options.context; });
        if (scenes.empty()) {
            return;
        }
        int next = i == scenes.end() ? 0 : int(i - scenes.begin()) + delta;
        next = std::clamp(next, 0, int(scenes.size()) - 1);
        options.context = scenes[next]->object;
        levelMode = false;
    };
    if (DevBitmapButton("sceneup", at(9, 109), UP, false, true, s, nullptr)) {
        step(-1);
    }
    ImGui::SetCursorScreenPos(at(9, 126));
    if (ImGui::BeginChild("scenes", ImVec2(62 * s, 162 * s))) {
        for (const auto *row : scenes) {
            if (ImGui::Selectable(row->name.c_str(), !levelMode && options.context == row->object)) {
                options.context = row->object;
                levelMode = false;
            }
        }
    }
    ImGui::EndChild();
    if (DevBitmapButton("scenedown", at(9, 288), DOWN, false, true, s, nullptr)) {
        step(1);
    }
}
void SceneTreeView::Rows(const EditorClient &client, ImVec2 origin, ImVec2 room, float s) {
    visible.clear();
    std::vector<bool> included(tree.rows.size(), false);
    for (size_t i = 0; i < tree.rows.size(); ++i) {
        const auto &r = tree.rows[i];
        const bool parent = !r.parent || (included[r.parent - 1] && IsOpen(r.parent - 1));
        if (!parent || (hideLayer && r.object && hidden.count(r.object))) {
            continue;
        }
        included[i] = true;
        visible.push_back(i);
    }
    const char *child = hierarchy ? "##hierarchytree" : "##leveltree";
    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, vi::Color(hierarchy ? 7 : 6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginChild(child, room, 0, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs);
    auto *dl = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float rowHeight = (hierarchy ? 18 : 17) * s, header = 15 * s, scroll = ImGui::GetScrollY();
    float xs[10];
    xs[0] = start.x;
    if (hierarchy) {
        xs[1] = start.x + std::max(120 * s, ImGui::GetContentRegionAvail().x - 70 * s);
        xs[2] = xs[1] + 70 * s;
    } else {
        for (int i = 0; i < 9; ++i) {
            xs[i + 1] = xs[i] + columns[i] * s;
        }
    }
    const float right = std::max(xs[hierarchy ? 2 : 9], start.x + ImGui::GetContentRegionAvail().x);
    std::vector<EditorPickRegion> picks;
#ifdef VIRTOOLSDEV_UI_TESTS
    shown.clear();
    shownFrame = ImGui::GetFrameCount();
#endif
    ImGui::PushClipRect(ImVec2(start.x, start.y + scroll + header),
                        ImVec2(right, start.y + scroll + ImGui::GetWindowHeight()), true);
    for (size_t n = 0; n < visible.size(); ++n) {
        const size_t i = visible[n];
        const auto &row = tree.rows[i];
        const int depth = Depth(i);
        const float y = start.y + header + rowHeight * n;
        const ImVec2 min(start.x, y), max(right, y + rowHeight);
        if (max.y < start.y + scroll + header || min.y > start.y + scroll + ImGui::GetWindowHeight()) {
            continue;
        }
        const bool selected = row.object ? selection.objects.count(row.object) != 0 : current == i;
        dl->AddRectFilled(min, max, vi::Color(selected ? 4 : hierarchy ? 7 : row.section ? 6 : 7));
        dl->AddLine(ImVec2(min.x, max.y - 1), ImVec2(max.x, max.y - 1), vi::Color(9));
        const float nameLeft = hierarchy ? xs[0] : xs[1], nameRight = hierarchy ? xs[1] : xs[2];
        const ImVec2 arrow(nameLeft + 4 * s + depth * 13 * s, y + 3 * s);
        bool toggled = false;
        ImGui::PushID(reinterpret_cast<const void *>(uintptr_t(row.key)));
        if (Children(i)) {
            Icon(IsOpen(i) ? 0xDA : 0xD9, arrow, s);
            ImGui::SetCursorScreenPos(arrow);
            if (ImGui::InvisibleButton("##arrow", ImVec2(11 * s, 11 * s))) {
                expanded[row.key] = !IsOpen(i);
                toggled = true;
            }
        }
        Icon(row.icon, ImVec2(nameLeft + 18 * s + depth * 13 * s, y + 3 * s),
             s, false);
        const ImVec2 text(nameLeft + 32 * s + depth * 13 * s, y + (rowHeight - 12 * s) * .5f), clip(nameRight, max.y);
        const ImU32 ink = vi::Color(selected ? 9 : 0);
        const bool renaming = hierarchy && rename && anchor == row.object;
        if (!renaming) {
            DrawVIText(dl, text, ink, row.name.c_str(), false, s, &clip);
        }
        ImGui::SetCursorScreenPos(ImVec2(nameLeft, y));
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##row", ImVec2(nameRight - nameLeft, rowHeight - 1),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);
        const bool clicked = !toggled && ImGui::IsItemClicked(0), rightClicked = ImGui::IsItemClicked(1);
        // Keep the source selection intact until a press is known to be a
        // click. Starting a drag on an already-selected row moves that group.
        const bool released = !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && ImGui::IsItemDeactivated() &&
                              ImGui::IsMouseReleased(0) && !ImGui::IsMouseDragPastThreshold(0) &&
                              ImGui::IsItemHovered();
        if ((clicked && (!selected || ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift)) || released ||
            (rightClicked && !selected && row.kind != EDITOR_SCENE_WORKSET)) {
            Choose(i, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
        }
        if (row.object && ((ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) || ImGui::IsItemClicked(2))) {
            if (row.types & EDITOR_OBJECT_SCRIPT) {
                client.Command("script.show", {ObjectArgument("target", row.object), IntegerArgument("focus", 1)});
            } else {
                client.Command("object.open-setup", {ObjectArgument("target", row.object)});
            }
        } else if (clicked && row.script) {
            client.Command("script.show", {ObjectArgument("target", row.script), IntegerArgument("focus", 0)});
        }
        if (row.object && ImGui::IsItemVisible()) {
            const auto lo = dl->GetClipRectMin(), hi = dl->GetClipRectMax();
            picks.push_back({row.object, std::max(nameLeft, lo.x), std::max(y, lo.y), std::min(nameRight, hi.x),
                             std::min(max.y, hi.y)});
        }
        if (row.object && ImGui::BeginDragDropSource()) {
            const auto objects = WriteObjectDrag(row.object, Targets());
            if (!objects.empty())
                ImGui::SetDragDropPayload(hierarchy ? EDITOR_HIERARCHY_PAYLOAD : EDITOR_OBJECT_PAYLOAD, objects.data(),
                                          objects.size(), ImGuiCond_Once);
            ImGui::TextUnformatted(row.name.c_str());
            ImGui::EndDragDropSource();
        }
        Drop(client, i);
        if (rightClicked) {
            ImGui::OpenPopup("LevelRowMenu");
        }
        PushDevMenuStyle();
        if (ImGui::BeginPopup("LevelRowMenu")) {
            if (row.workset) {
                if (ImGui::MenuItem("Show Workset Scripts in Schematic")) {
                    client.Command("scene.workset-show",
                                   {IntegerArgument("workset", row.workset), IntegerArgument("mode", 0)});
                }
                if (ImGui::MenuItem("Hide Workset Scripts in Schematic")) {
                    client.Command("scene.workset-show",
                                   {IntegerArgument("workset", row.workset), IntegerArgument("mode", 1)});
                }
                if (ImGui::MenuItem("Create Workset")) {
                    Send(client, "scene.workset-create", {IntegerArgument("parent", row.workset)});
                }
                if (ImGui::MenuItem("Add Selection", nullptr, false, !selection.objects.empty())) {
                    Send(client, "scene.workset-add", {IntegerArgument("workset", row.workset)});
                }
                if (ImGui::MenuItem("Remove Selection From Workset", nullptr, false, !selection.objects.empty())) {
                    Send(client, "scene.workset-remove", {IntegerArgument("workset", row.workset)});
                }
                if (ImGui::MenuItem("Rename")) {
                    renameWorkset = row.workset;
                    worksetName = row.name;
                    renamePopup = true;
                }
                if (ImGui::MenuItem("Delete")) {
                    client.Command("scene.workset-delete", {IntegerArgument("workset", row.workset)});
                }
            } else {
                Menu(client, row);
            }
            ImGui::EndPopup();
        }
        PopDevMenuStyle();
        ImVec2 eyeMin{}, eyeMax{}, activateMin{}, activateMax{}, activeMin{}, activeMax{}, priorityMin{}, priorityMax{};
        auto button = [&](int column, const char *id) {
            ImGui::SetCursorScreenPos(ImVec2(xs[column], y));
            return ImGui::InvisibleButton(id, ImVec2(xs[column + 1] - xs[column], rowHeight - 1));
        };
        auto cellIcon = [&](int column, int icon) {
            Icon(icon, ImVec2((xs[column] + xs[column + 1] - 11 * s) * .5f, y + 3 * s), s);
        };
        if (hierarchy) {
            if (row.types & EDITOR_OBJECT_RENDER) {
                priorityMin = ImVec2(xs[1], y);
                priorityMax = ImVec2(xs[2], max.y - 1);
                if (priorityTarget == row.object) {
                    ImGui::SetCursorScreenPos(ImVec2(xs[1] + 4 * s, y + 1 * s));
                    ImGui::SetNextItemWidth(xs[2] - xs[1] - 8 * s);
                    if (priorityPopup) {
                        ImGui::SetKeyboardFocusHere();
                        priorityPopup = false;
                    }
                    const bool entered =
                        ImGui::InputInt("##priority", &priority, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
                    if (entered) {
                        const auto targets = Targets(row);
                        client.Command("scene.render-priority",
                                       {ObjectsArgument("objects", targets),
                                        IntegerArgument("value", uint64_t(int64_t(priority)))});
                        priorityTarget = {};
                    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsItemDeactivated()) {
                        priorityTarget = {};
                    }
                } else {
                    const auto value = std::to_string(row.renderPriority);
                    DrawVIText(dl, ImVec2(xs[1] + 8 * s, text.y), ink, value.c_str(), false, s);
                    button(1, "##priority-cell");
                    if (ImGui::IsItemClicked(0)) {
                        Choose(i, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
                        if (ImGui::IsMouseDoubleClicked(0)) {
                            priorityTarget = row.object;
                            priority = row.renderPriority;
                            priorityPopup = true;
                        }
                    }
                }
            }
        } else {
            if (row.object) {
                cellIcon(0, hidden.count(row.object) ? 0x182 : 0x181);
                if (button(0, "##layer")) {
                    const bool remove = hidden.count(row.object) != 0;
                    for (auto ref : Targets(row)) {
                        if (remove) {
                            hidden.erase(ref);
                        } else {
                            hidden.insert(ref);
                        }
                    }
                }
            }
            if (row.cells & EDITOR_CELL_INITIAL) {
                cellIcon(2, 0x186);
            }
            if (row.cells & EDITOR_CELL_HIDEABLE) {
                static const int eyes[2][3] = {{0x216, 0x2D2, 0x217}, {0x2D0, 0x2D3, 0x2D1}};
                const int state = row.cells & EDITOR_CELL_VISIBLE            ? 0
                                  : row.cells & EDITOR_CELL_HIERARCHY_HIDDEN ? 1
                                                                             : 2;
                cellIcon(3, eyes[(row.cells & EDITOR_CELL_PARENT_HIDDEN) ? 1 : 0][state]);
                eyeMin = ImVec2(xs[3], y);
                eyeMax = ImVec2(xs[4], max.y - 1);
                if (button(3, "##visible")) {
                    const auto targets = Targets(row);
                    const int mode =
                        (row.cells & EDITOR_CELL_VISIBLE)                                                     ? 1
                        : (!(row.types & EDITOR_OBJECT_RENDER) || (row.cells & EDITOR_CELL_HIERARCHY_HIDDEN)) ? 0
                                                                                                              : 2;
                    client.Command("scene.visibility",
                                   {ObjectsArgument("objects", targets), IntegerArgument("mode", mode)});
                }
            }
            if (row.cells & EDITOR_CELL_SCENE_FLAGS) {
                cellIcon(4, row.cells & EDITOR_CELL_START_ACTIVATE     ? 0x218
                            : row.cells & EDITOR_CELL_START_DEACTIVATE ? 0x219
                            : row.cells & EDITOR_CELL_START_LEAVE      ? 0x2EF
                                                                       : 0x218);
                cellIcon(5, row.cells & EDITOR_CELL_START_RESET ? 0x2D4 : 0x2D5);
                cellIcon(6, row.cells & EDITOR_CELL_ACTIVE ? 0x218 : 0x219);
                for (int col = 4; col <= 6; ++col) {
                    if (button(col, col == 4 ? "##activate" : col == 5 ? "##reset" : "##activenow")) {
                        const auto targets = Targets(row);
                        client.Command("scene.flags",
                                       {ObjectsArgument("objects", targets), ObjectArgument("target", row.object),
                                        ObjectArgument("scene", row.scene), IntegerArgument("mode", col - 4)});
                    }
                }
                activateMin = ImVec2(xs[4], y);
                activateMax = ImVec2(xs[5], max.y - 1);
                activeMin = ImVec2(xs[6], y);
                activeMax = ImVec2(xs[7], max.y - 1);
                if (row.priority >= 0) {
                    const auto value = std::to_string(row.priority);
                    DrawVIText(dl, ImVec2(xs[7] + 5 * s, text.y), ink, value.c_str(), false, s);
                    button(7, "##priority");
                    if (ImGui::IsItemClicked(0) && ImGui::IsMouseDoubleClicked(0)) {
                        Choose(i, false, false);
                        priorityTarget = row.object;
                        priority = row.priority;
                        priorityPopup = true;
                    }
                }
            }
            DrawVIText(dl, ImVec2(xs[8] + 3 * s, text.y), ink, row.owner.c_str(), false, s);
        }
        if (renaming) {
            ImGui::SetCursorScreenPos(text);
            ImGui::SetNextItemWidth(std::max(20.0f, nameRight - text.x));
            const auto action =
                client.Host()->drawEdit(client.Host()->data, rename, EditorString EDITOR_TEXT("##rename"), renameFocus);
            renameFocus = false;
            if (action & EDITOR_INPUT_ACCEPT) {
                if (client.Host()->finishEdit(client.Host()->data, rename, 1, nullptr) != EDITOR_REFUSED) {
                    rename = 0;
                }
            } else if (action & EDITOR_INPUT_CANCEL) {
                client.Host()->finishEdit(client.Host()->data, rename, 0, nullptr);
                rename = 0;
            }
        }
#ifdef VIRTOOLSDEV_UI_TESTS
        if (y >= start.y + scroll + header && max.y <= start.y + scroll + ImGui::GetWindowHeight()) {
            SceneShownRow report;
            report.object = row.object.object;
            report.cid = row.cid;
            report.name = row.name;
            report.depth = depth;
            report.children = Children(i);
            report.open = IsOpen(i);
            report.selected = selected;
            report.min = min;
            report.max = ImVec2(max.x, max.y - 1);
            if (report.children) {
                report.arrowMin = arrow;
                report.arrowMax = ImVec2(arrow.x + 11 * s, arrow.y + 11 * s);
            }
            report.eyeMin = eyeMin;
            report.eyeMax = eyeMax;
            report.activateMin = activateMin;
            report.activateMax = activateMax;
            report.activeMin = activeMin;
            report.activeMax = activeMax;
            report.priorityMin = priorityMin;
            report.priorityMax = priorityMax;
            shown.push_back(std::move(report));
        }
#endif
        if (focus && row.object == focus) {
            ImGui::SetScrollHereY();
            focus = {};
        }
        ImGui::PopID();
    }
    ImGui::PopClipRect();
    const char *headers[] = {
        "",           "Level",    "IC",   "Visible", "Activate At Scene Start", "Reset At Scene Start",
        "Active Now", "Priority", "Owner"};
    dl->AddRectFilled(ImVec2(start.x, start.y + scroll), ImVec2(right, start.y + scroll + header), vi::Color(6));
    for (int col = 0; col < (hierarchy ? 2 : 9); ++col) {
        const char *label = hierarchy ? (col ? "Render Priority" : "Hierarchy") : headers[col];
        DrawVIText(dl, ImVec2(xs[col] + 3 * s, start.y + scroll + 1 * s), vi::Color(0), label, false, s);
        ImGui::SetCursorScreenPos(ImVec2(xs[col], start.y + scroll));
        ImGui::PushID(col);
        if (ImGui::InvisibleButton("##header", ImVec2(xs[col + 1] - xs[col], header)) && !hierarchy &&
            (col == 1 || col == 7)) {
            const bool p = col == 7;
            if (options.priority == p) {
                options.reversed = !options.reversed;
            } else {
                options.priority = p;
                options.reversed = false;
            }
        }
        if (!hierarchy) {
            ImGui::SetCursorScreenPos(ImVec2(xs[col + 1] - 2 * s, start.y + scroll));
            ImGui::InvisibleButton("##width", ImVec2(4 * s, header));
            if (ImGui::IsItemActive()) {
                columns[col] = std::max(12.0f, columns[col] + ImGui::GetIO().MouseDelta.x / s);
            }
        }
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + header + rowHeight * visible.size()));
    ImGui::Dummy(ImVec2(right - start.x, 1));
    Keyboard(client);
    client.Host()->pickRegions(client.Host()->data, client.View(), picks.data(), uint32_t(picks.size()));
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}
void SceneTreeView::Keyboard(const EditorClient &client) {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput || rename ||
        priorityTarget || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        return;
    }
    const auto &io = ImGui::GetIO();
    if (!hierarchy && io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            Send(client, "scene.copy");
        }
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            Send(client, "scene.paste", {ObjectArgument("scene", section)});
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            findPopup = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            auto chosen = selection.objects;
            for (const auto &row : tree.rows) {
                if (row.parent && chosen.count(tree.rows[row.parent - 1].object) && row.object) {
                    selection.objects.insert(row.object);
                }
            }
            ++inspectionRevision;
        }
    }
    if (!hierarchy && ImGui::IsKeyPressed(ImGuiKey_F3, false)) {
        if (io.KeyCtrl && current < tree.rows.size()) {
            findText = tree.rows[current].name;
        }
        Find(!io.KeyShift);
    }
    if (!hierarchy && io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        Send(client, "scene.export");
    }
    if (client.Host()->shortcut(client.Host()->data, EditorString EDITOR_TEXT("object.name"))) {
        if (current < tree.rows.size() && tree.rows[current].kind == EDITOR_SCENE_WORKSET) {
            renameWorkset = tree.rows[current].workset;
            worksetName = tree.rows[current].name;
            renamePopup = true;
        } else if (selection.objects.size() == 1) {
            for (const auto &row : tree.rows) {
                if (row.object == *selection.objects.begin()) {
                    BeginRename(client, row);
                    break;
                }
            }
        }
    }
    if (io.KeyCtrl || io.KeyAlt) {
        return;
    }
    auto at = std::find(visible.begin(), visible.end(), current);
    int index = at == visible.end() ? -1 : int(at - visible.begin()), next = -2;
    const int page =
        std::max(1, int(ImGui::GetWindowHeight() / ((hierarchy ? 18 : 17) * (ImGui::GetFontSize() / 13))) - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        next = index + 1;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        next = index < 0 ? 0 : index - 1;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
        next = 0;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End)) {
        next = int(visible.size()) - 1;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
        next = index + page;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
        next = index - page;
    }
    if (index >= 0 && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        if (Children(current) && IsOpen(current)) {
            expanded[tree.rows[current].key] = false;
        } else if (tree.rows[current].parent) {
            const auto parent = tree.rows[current].parent - 1;
            auto found = std::find(visible.begin(), visible.end(), parent);
            if (found != visible.end()) {
                next = int(found - visible.begin());
            }
        }
    }
    if (index >= 0 && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        if (Children(current) && !IsOpen(current)) {
            expanded[tree.rows[current].key] = true;
        } else if (Children(current)) {
            next = index + 1;
        }
    }
    if (next != -2 && !visible.empty()) {
        next = std::clamp(next, 0, int(visible.size()) - 1);
        Choose(visible[next], false, false);
        focus = tree.rows[current].object;
        const float y = (15 + (hierarchy ? 18 : 17) * (next + 1)) * (ImGui::GetFontSize() / 13);
        if (y < ImGui::GetScrollY() + 15) {
            ImGui::SetScrollY(std::max(0.0f, y - 15));
        } else if (y > ImGui::GetScrollY() + ImGui::GetWindowHeight()) {
            ImGui::SetScrollY(y - ImGui::GetWindowHeight());
        }
    }
    if (selection.objects.size() == 1 &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
        const auto target = *selection.objects.begin();
        auto found =
            std::find_if(tree.rows.begin(), tree.rows.end(), [&](const SceneRow &r) { return r.object == target; });
        if (found != tree.rows.end() && (found->types & EDITOR_OBJECT_SCRIPT)) {
            client.Command("script.show", {ObjectArgument("target", target), IntegerArgument("focus", 1)});
        } else {
            client.Command("object.open-setup", {ObjectArgument("target", target)});
        }
    }
    if (hierarchy) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        Delete(client);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        client.Command("selection.lock", {IntegerArgument("locked", !selection.locked)});
    }
    if (ImGui::IsKeyPressed(ImGuiKey_1) || ImGui::IsKeyPressed(ImGuiKey_2)) {
        for (auto ref : selection.objects) {
            if (ImGui::IsKeyPressed(ImGuiKey_1)) {
                hidden.insert(ref);
            } else {
                hidden.erase(ref);
            }
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_V) || ImGui::IsKeyPressed(ImGuiKey_H)) {
        Send(client, "scene.visibility", {IntegerArgument("mode", ImGui::IsKeyPressed(ImGuiKey_V) ? 0 : 1)});
    }
    if (ImGui::IsKeyPressed(ImGuiKey_A) || ImGui::IsKeyPressed(ImGuiKey_D)) {
        Send(client, "scene.flags",
             {ObjectArgument("target", anchor), ObjectArgument("scene", levelMode ? tree.levelScene : options.context),
              IntegerArgument("mode", ImGui::IsKeyPressed(ImGuiKey_A) ? 3 : 4)});
    }
    if (ImGui::IsKeyPressed(ImGuiKey_P)) {
        Send(client, "object.create-place");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_G)) {
        Send(client, "object.create-group", {ObjectArgument("scene", tree.currentScene)});
    }
    if (ImGui::IsKeyPressed(ImGuiKey_C)) {
        Send(client, "object.create-scene");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_S)) {
        Send(client, "script.create");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R)) {
        client.Command("object.create-array");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_W)) {
        Send(client, "scene.workset-create");
    }
    if (group && selection.objects.size() == 1 &&
        (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd) ||
         ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))) {
        Send(client, "scene.group-order",
             {ObjectArgument("target", group),
              IntegerArgument(
                  "value",
                  uint64_t(ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd) ? -1 : 1))});
    }
}
void SceneTreeView::Dialogs(const EditorClient &client, EditorViewFrame &frame) {
    if (renamePopup) {
        ImGui::OpenPopup("Rename##level");
        renamePopup = false;
    }
    if (ImGui::BeginPopupModal("Rename##level", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        bool accept = false;
        if (renameWorkset) {
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            accept = ImGui::InputText("##name", &worksetName, ImGuiInputTextFlags_EnterReturnsTrue);
        } else if (rename) {
            const auto action = client.Host()->drawEdit(client.Host()->data, rename, EditorString EDITOR_TEXT("##name"),
                                                        ImGui::IsWindowAppearing());
            accept = (action & EDITOR_INPUT_ACCEPT) != 0;
        }
        if (accept || ImGui::Button("OK", ImVec2(100, 0))) {
            if (renameWorkset) {
                client.Command("scene.workset-rename",
                               {IntegerArgument("workset", renameWorkset), TextArgument("name", worksetName)});
                renameWorkset = 0;
                ImGui::CloseCurrentPopup();
            } else if (rename && client.Host()->finishEdit(client.Host()->data, rename, 1, nullptr) != EDITOR_REFUSED) {
                rename = 0;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (rename) {
                client.Host()->finishEdit(client.Host()->data, rename, 0, nullptr);
            }
            rename = 0;
            renameWorkset = 0;
            ImGui::CloseCurrentPopup();
        }
        frame.active = 1;
        ImGui::EndPopup();
    }
    if (findPopup) {
        ImGui::OpenPopup("Find Item");
        findPopup = false;
    }
    if (ImGui::BeginPopupModal("Find Item", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        const bool enter = ImGui::InputText("Find", &findText, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Checkbox("Objects", &findObjects);
        ImGui::SameLine();
        ImGui::Checkbox("Scripts", &findScripts);
        if (enter || ImGui::Button("Find Next")) {
            Find(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        frame.active = 1;
        ImGui::EndPopup();
    }
    if (priorityPopup && !hierarchy) {
        ImGui::OpenPopup(hierarchy ? "Render Priority" : "Edit Priority");
        priorityPopup = false;
    }
    if (ImGui::BeginPopupModal(hierarchy ? "Render Priority" : "Edit Priority", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        const bool enter = ImGui::InputInt("##priority", &priority, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter || ImGui::Button("OK")) {
            const auto objects = hierarchy ? std::vector<ObjectRef>{priorityTarget} : Targets();
            client.Command(
                hierarchy ? "scene.render-priority" : "scene.priority",
                {ObjectsArgument("objects", objects), IntegerArgument("value", uint64_t(int64_t(priority)))});
            priorityTarget = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            priorityTarget = {};
            ImGui::CloseCurrentPopup();
        }
        frame.active = 1;
        ImGui::EndPopup();
    }
    if (pending.open) {
        ImGui::OpenPopup(pending.title.c_str());
        pending.open = false;
    }
    if (!pending.title.empty() &&
        ImGui::BeginPopupModal(pending.title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(pending.text.c_str());
        const bool yes = ImGui::Button(pending.command.empty() ? "OK" : "Yes");
        ImGui::SameLine();
        const bool no = (!pending.command.empty() && ImGui::Button("No")) || ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (yes || no) {
            const int step = pending.deletion;
            if (!pending.command.empty() && (yes || step == 3 || pending.command == "scene.move")) {
                std::vector<EditorValue> args = {ObjectsArgument("objects", pending.objects)};
                if (pending.command == "scene.move") {
                    args.push_back(ObjectArgument("target", pending.target));
                    args.push_back(ObjectArgument("container", pending.container));
                    args.push_back(IntegerArgument("dependencies", yes));
                }
                if (step == 3) {
                    args.push_back(ObjectArgument("scene", pending.scene));
                    args.push_back(IntegerArgument("dependencies", yes));
                }
                client.Command(pending.command.c_str(), args);
            }
            ImGui::CloseCurrentPopup();
            pending = {};
            if (step) {
                ContinueDelete(client, step);
            }
        }
        frame.active = 1;
        ImGui::EndPopup();
    }
    if (rename || priorityTarget) {
        frame.active = 1;
    }
}
void SceneTreeView::Draw(const EditorHost *host, EditorView id, EditorViewFrame &frame) {
    EditorClient client(host, id);
    const auto before = selection.objects;
    const auto beforeSection = section, beforeGroup = group;
    Refresh(client);
    if (expandRequest >= 0) {
        Expand(expandRequest);
        expandRequest = -1;
    }
    if (focus) {
        for (size_t i = 0; i < tree.rows.size(); ++i) {
            if (tree.rows[i].object == focus) {
                Choose(i, false, false);
                OpenAncestors(i);
                break;
            }
        }
    }
    EditorPrepareViewWindow(frame);
    bool open = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool visibleWindow = BeginDevWindow(frame.window.data, frame.closable ? &open : nullptr,
                                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (visibleWindow) {
        if (frame.closable) {
            DevTabClose(&open);
        }
        const auto origin = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
        const float s = ImGui::GetFontSize() / 13;
        Toolbar(client, origin, s);
        if (!hierarchy) {
            Side(client, origin, s);
        }
        if (tree.document.value) {
            Rows(client, ImVec2(origin.x + (hierarchy ? 0 : 76 * s), origin.y + 21 * s),
                 ImVec2(std::max(1.0f, room.x - (hierarchy ? 0 : 76 * s)), std::max(1.0f, room.y - 21 * s)), s);
        } else {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + 82 * s, origin.y + 26 * s));
            ImGui::TextUnformatted("No composition loaded");
        }
        Dialogs(client, frame);
    }
    ImGui::End();
    if (!open) {
        frame.close = 1;
    }
    selection.Push(client);
    if (before != selection.objects || beforeSection != section || beforeGroup != group) {
        ++inspectionRevision;
    }
}
EditorStatus SceneTreeView::Navigate(const EditorRequest &request) {
    if (tree.document != request.scope.document) {
        tree = {};
        tree.document = request.scope.document;
        modelVersion = 0;
        selection.Reset();
        expanded.clear();
        hidden.clear();
        anchor = section = group = {};
        current = std::numeric_limits<size_t>::max();
    }
    for (uint32_t i = 0; i < request.count; ++i) {
        const auto &field = request.values[i];
        const std::string key(field.key.data, field.key.size);
        if (key == "target" && field.object) {
            focus = field.object;
        } else if (key == "expand" && field.integer) {
            expandRequest = int(field.integer) - 1;
        }
    }
    return EDITOR_OK;
}
EditorStatus SceneTreeView::Inspect(EditorSink sink) const {
    const auto objects = Targets();
    const EditorValue fields[] = {
        {EDITOR_TEXT("selection"), EDITOR_OBJECT_LIST, {}, {}, 0, {objects.data(), uint32_t(objects.size())}},
        {EDITOR_TEXT("scene"), EDITOR_OBJECT, section},
        {EDITOR_TEXT("group"), EDITOR_OBJECT, group},
        {EDITOR_TEXT("context"), EDITOR_OBJECT, levelMode ? tree.levelScene : options.context},
        {EDITOR_TEXT("rows"), EDITOR_INTEGER_VALUE, {}, {}, tree.rows.size()},
        {EDITOR_TEXT("resources"),
         EDITOR_INTEGER_VALUE,
         {},
         {},
         uint64_t(options.meshes | (options.materials << 1) | (options.textures << 2))}};
    return sink.write ? sink.write(sink.data, fields, 6) : EDITOR_INVALID;
}
EditorStatus SceneTreeView::Save(EditorSink sink) const {
    std::vector<uint64_t> widths, keys, opens;
    for (float width : columns) {
        widths.push_back(uint64_t(width * 1024));
    }
    for (const auto &item : expanded) {
        keys.push_back(item.first);
        opens.push_back(item.second);
    }
    const EditorValue fields[] = {
        {EDITOR_TEXT("document"), EDITOR_INTEGER_VALUE, {}, {}, tree.document.value},
        {EDITOR_TEXT("priority"), EDITOR_INTEGER_VALUE, {}, {}, options.priority},
        {EDITOR_TEXT("reversed"), EDITOR_INTEGER_VALUE, {}, {}, options.reversed},
        {EDITOR_TEXT("resources"),
         EDITOR_INTEGER_VALUE,
         {},
         {},
         uint64_t(options.meshes | (options.materials << 1) | (options.textures << 2))},
        {EDITOR_TEXT("hide-layer"), EDITOR_INTEGER_VALUE, {}, {}, hideLayer},
        {EDITOR_TEXT("level-mode"), EDITOR_INTEGER_VALUE, {}, {}, levelMode},
        {EDITOR_TEXT("columns"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {widths.data(), uint32_t(widths.size())}},
        {EDITOR_TEXT("keys"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {keys.data(), uint32_t(keys.size())}},
        {EDITOR_TEXT("open"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {opens.data(), uint32_t(opens.size())}},
        {EDITOR_TEXT("find"), EDITOR_TEXT_VALUE, {}, EditorText(findText)}};
    return sink.write ? sink.write(sink.data, fields, 10) : EDITOR_INVALID;
}
EditorStatus SceneTreeView::Restore(const EditorRequest &request) {
    std::map<std::string, const EditorValue *> fields;
    for (uint32_t i = 0; i < request.count; ++i) {
        fields.emplace(std::string(request.values[i].key.data, request.values[i].key.size), &request.values[i]);
    }
    if (fields.size() != 10 || !fields.count("columns") || !fields.count("keys") || !fields.count("open") ||
        fields["columns"]->type != EDITOR_INTEGER_LIST || fields["columns"]->integers.count != 9 ||
        fields["keys"]->type != EDITOR_INTEGER_LIST || fields["open"]->type != EDITOR_INTEGER_LIST ||
        fields["keys"]->integers.count != fields["open"]->integers.count) {
        return EDITOR_INVALID;
    }
    for (auto key : {"priority", "reversed", "resources", "hide-layer", "level-mode", "document"}) {
        if (!fields.count(key) || fields[key]->type != EDITOR_INTEGER_VALUE) {
            return EDITOR_INVALID;
        }
    }
    if (!fields.count("find") || fields["find"]->type != EDITOR_TEXT_VALUE || fields["resources"]->integer > 7) {
        return EDITOR_INVALID;
    }
    for (uint32_t i = 0; i < 9; ++i) {
        if (fields["columns"]->integers.data[i] < 12 * 1024 || fields["columns"]->integers.data[i] > 10000 * 1024) {
            return EDITOR_INVALID;
        }
    }
    expandedDocument = {fields["document"]->integer};
    options.priority = fields["priority"]->integer != 0;
    options.reversed = fields["reversed"]->integer != 0;
    const auto resources = fields["resources"]->integer;
    options.meshes = resources & 1;
    options.materials = resources & 2;
    options.textures = resources & 4;
    hideLayer = fields["hide-layer"]->integer != 0;
    levelMode = fields["level-mode"]->integer != 0;
    findText.assign(fields["find"]->text.data, fields["find"]->text.size);
    for (uint32_t i = 0; i < 9; ++i) {
        columns[i] = float(fields["columns"]->integers.data[i]) / 1024;
    }
    for (uint32_t i = 0; i < fields["keys"]->integers.count; ++i) {
        expanded[fields["keys"]->integers.data[i]] = fields["open"]->integers.data[i] != 0;
    }
    return EDITOR_OK;
}
void *Create(void *data, const EditorHost *, EditorView id) {
    auto *view = new SceneTreeView(data != nullptr);
#ifdef VIRTOOLSDEV_UI_TESTS
    testViews[id] = view;
#endif
    return view;
}
void Destroy(void *data) {
#ifdef VIRTOOLSDEV_UI_TESTS
    for (auto it = testViews.begin(); it != testViews.end();) {
        if (it->second == data) {
            it = testViews.erase(it);
        } else {
            ++it;
        }
    }
#endif
    delete static_cast<SceneTreeView *>(data);
}
} // namespace
#ifdef VIRTOOLSDEV_UI_TESTS
std::vector<SceneShownRow> SceneRowsForTests(EditorView id) {
    auto found = testViews.find(id);
    return found != testViews.end() && ImGui::GetFrameCount() - found->second->shownFrame <= 1
               ? found->second->shown
               : std::vector<SceneShownRow>{};
}
#endif
bool RegisterSceneViews(EditorApplication &editor, std::string &error) {
    static int hierarchy;
    EditorViewDesc views[2]{};
    static const EditorParameter navigate[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, 1},
                                               {EDITOR_TEXT("expand"), EDITOR_INTEGER_VALUE, 1}};
    static const EditorParameter fields[] = {
        {EDITOR_TEXT("selection"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("scene"), EDITOR_OBJECT},
        {EDITOR_TEXT("group"), EDITOR_OBJECT},          {EDITOR_TEXT("context"), EDITOR_OBJECT},
        {EDITOR_TEXT("rows"), EDITOR_INTEGER_VALUE},    {EDITOR_TEXT("resources"), EDITOR_INTEGER_VALUE}};
    for (int i = 0; i < 2; ++i) {
        auto &view = views[i];
        view.size = sizeof(view);
        view.imguiVersion = EDITOR_IMGUI_VERSION;
        view.id = i ? EditorString EDITOR_TEXT("scene.hierarchy") : EditorString EDITOR_TEXT("scene.level");
        view.provider = EDITOR_TEXT("builtin.scene");
        view.title = i ? EditorString EDITOR_TEXT("Hierarchy Manager") : EditorString EDITOR_TEXT("Level Manager");
        view.create = Create;
        view.destroy = Destroy;
        view.draw = [](void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) {
            static_cast<SceneTreeView *>(data)->Draw(host, id, *frame);
        };
        view.data = i ? &hierarchy : nullptr;
        view.area = EDITOR_WORKSPACE;
        view.parameters = navigate;
        view.count = 2;
        view.navigate = [](void *data, const EditorHost *, const EditorRequest *request) {
            return static_cast<SceneTreeView *>(data)->Navigate(*request);
        };
        view.stateVersion = 1;
        static const EditorParameter settings[] = {
            {EDITOR_TEXT("priority"), EDITOR_INTEGER_VALUE},   {EDITOR_TEXT("reversed"), EDITOR_INTEGER_VALUE},
            {EDITOR_TEXT("resources"), EDITOR_INTEGER_VALUE},  {EDITOR_TEXT("hide-layer"), EDITOR_INTEGER_VALUE},
            {EDITOR_TEXT("level-mode"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("columns"), EDITOR_INTEGER_LIST},
            {EDITOR_TEXT("find"), EDITOR_TEXT_VALUE}};
        view.settings = settings;
        view.settingCount = uint32_t(std::size(settings));
        view.saveState = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            return static_cast<SceneTreeView *>(data)->Save(sink);
        };
        view.restoreState = [](void *data, const EditorHost *, const EditorRequest *request) {
            return static_cast<SceneTreeView *>(data)->Restore(*request);
        };
        view.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            return static_cast<SceneTreeView *>(data)->Inspect(sink);
        };
        view.inspectRevision = [](void *data, const EditorHost *) {
            return static_cast<SceneTreeView *>(data)->inspectionRevision;
        };
        view.fields = fields;
        view.fieldCount = 6;
    }
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("scene.views");
    module.views = views;
    module.viewCount = 2;
    return editor.Register(module, error) && RegisterAttributeViews(editor, error);
}
} // namespace vdev
