#include "features/scene/SceneCommands.h"
#include "features/scene/SceneQueries.h"
#include "workspace/EditorClient.h"
#include "features/objects/ObjectCommands.h"
#include "core/level/LevelCommands.h"
#include "core/level/HierarchyTree.h"
#include "core/level/SceneCommands.h"
#include "core/level/AttributeQueries.h"
#include "editor/ImGuiDrawing.h"
#include <stdexcept>
#include <thread>
#include <algorithm>

using namespace vdev;
namespace {
void Check(bool ok, const char *message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}
uint64_t Number(const EditorResponse &r, const char *name) {
    return EditorClient::Integer(r, name);
}
std::vector<ObjectRef> Refs(const EditorResponse &r, const char *name) {
    const auto *f = EditorClient::Find(r, name);
    return f ? f->objects : std::vector<ObjectRef>{};
}
} // namespace
void TestSceneCommands(CkRuntimeHost &runtime) {
    Document document(runtime);
    std::string error;
    Check(document.New(&error), error.c_str());
    document.MarkTopologyChanged();
    auto prepared = document.PrepareNew(&error, SceneEnvironment{});
    Check(bool(prepared), error.c_str());
    prepared->Commit();
    const auto savePointProbe = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(savePointProbe.ok && document.History().Undo() && !document.Dirty(),
          "prepared document lost its saved history position");
    bool available = true;
    EditorApplication app([&] { return &document; }, [&] { return available; });
    ObjectClipboard objects;
    Clipboard clipboard;
    SchematicNavigation navigation;
    CKDependencies copy;
    SceneCommands scene(
        app, [&] { return &document; }, objects, clipboard, [&]() -> SchematicNavigation & { return navigation; }, [&]() -> CKDependencies & { return copy; });
    Check(scene.Register(error), error.c_str());
    Check(app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    auto publish = [&] {
        app.Attach(document.Token(), document.TopologyRevision(), true, true);
        scene.Publish(app.Revision(), 0, 0, false);
    };
    publish();
    auto command = [&](const char *id, const std::vector<EditorValue> &values = {}) {
        return app.Execute(id, {app.Scope(), values.data(), uint32_t(values.size())});
    };
    auto query = [&](const char *id, const std::vector<EditorValue> &values = {}) {
        return app.Query(id, {app.Scope(), values.data(), uint32_t(values.size())});
    };
    const auto first = CreateLayoutObject(document, LayoutObject::Frame3D),
               second = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(first.ok && second.ok, "scene fixtures failed");
    const auto a = document.Reference(first.created), b = document.Reference(second.created);
    const auto script = CreateScript(document, first.created, "Projected Script");
    Check(script.ok, "script fixture failed");
    const auto group = CreateGroup(document, "Projected Group", {first.created});
    Check(group.ok, "group fixture failed");
    publish();
    const auto referencesUndo = document.History().UndoCount();
    const std::vector<ObjectRef> references{b, a, b, document.Reference(script.created)};
    const auto copiedReferences = command("scene.copy-references", {ObjectsArgument("objects", references)});
    Check((copiedReferences.status == EDITOR_OK || copiedReferences.status == EDITOR_NO_CHANGE) &&
              Refs(query("scene.clipboard"), "objects") == references &&
              document.History().UndoCount() == referencesUndo,
          "array reference copy lost ordering, duplicates or scripts, or changed undo history");
    const auto undo = document.History().UndoCount();
    const auto revision = document.TopologyRevision();
    const auto tree = query("scene.tree");
    Check(tree.status == EDITOR_OK && Number(tree, "total") > 4, "level projection missing");
    const auto refs = Refs(tree, "objects");
    Check(std::find(refs.begin(), refs.end(), a) != refs.end() &&
              std::find(refs.begin(), refs.end(), document.Reference(script.created)) != refs.end(),
          "level lost objects/scripts");
    Check(document.History().UndoCount() == undo && document.TopologyRevision() == revision, "query modified document");
    const auto hierarchy = query("scene.tree", {IntegerArgument("hierarchy", 1), IntegerArgument("resources", 7)});
    Check(hierarchy.status == EDITOR_OK, "hierarchy projection failed");
    std::vector<CK_ID> expected;
    std::function<void(const HierarchyItem &)> walk = [&](const auto &item) {
        expected.push_back(item.object);
        for (const auto &child : item.children) {
            walk(child);
        }
    };
    for (const auto &root : BuildHierarchy(document, {true, true, true})) {
        walk(root);
    }
    const auto projected = Refs(hierarchy, "objects");
    Check(projected.size() == expected.size(), "hierarchy changed native rows");
    for (size_t i = 0; i < expected.size(); ++i) {
        Check(projected[i].object == expected[i], "hierarchy changed ordering");
    }
    const std::vector<ObjectRef> targets{a, b};
    Check(command("scene.visibility", {ObjectsArgument("objects", targets), IntegerArgument("mode", 1)}).status ==
              EDITOR_OK,
          "visibility command failed");
    Check(!document.Resolve(a)->IsVisible() && document.History().UndoCount() == undo + 1,
          "visibility not one undo step");
    Check(document.History().Undo() && document.Resolve(a)->IsVisible(), "visibility undo failed");
    publish();
    Check(command("scene.hierarchy", {ObjectsArgument("objects", std::vector<ObjectRef>{b}),
                                      ObjectArgument("target", a), IntegerArgument("background", uint64_t(-1))})
                  .status == EDITOR_OK,
          "hierarchy move failed");
    Check(CK3dEntity::Cast(document.Resolve(b))->GetParent() == document.Resolve(a), "hierarchy parent mismatch");
    Check(document.History().Undo(), "hierarchy undo failed");
    publish();
    const auto beforeWorkset = document.History().UndoCount();
    auto workset = command("scene.workset-create", {ObjectsArgument("objects", targets)});
    Check(workset.status == EDITOR_OK && Number(workset, "workset"), "workset creation failed");
    Check(document.History().UndoCount() == beforeWorkset, "workset created a native undo step");
    Check(command("scene.workset-add",
                  {ObjectsArgument("objects", targets), IntegerArgument("workset", Number(workset, "workset"))})
                  .status == EDITOR_OK,
          "workset membership failed");
    Check(command("attribute.create-category", {TextArgument("name", std::string("Scene Query Tests"))}).status ==
              EDITOR_OK,
          "category creation failed");
    const CKGUID parameterGuid = CKPGUID_INT;
    const uint64_t guid = (uint64_t(parameterGuid.d1) << 32) | parameterGuid.d2;
    const auto type = command("attribute.create-type", {TextArgument("name", std::string("Scene Query Value")),
                                                        IntegerArgument("parameter", guid),
                                                        TextArgument("category", std::string("Scene Query Tests"))});
    Check(type.status == EDITOR_OK, "attribute type creation failed");
    const auto index = Number(type, "type");
    const auto catalog = query("attribute.catalog");
    Check(catalog.status == EDITOR_OK, "attribute catalog failed");
    const auto token = Number(catalog, "catalog");
    const auto previous = document.History().UndoCount();
    Check(command("attribute.add", {ObjectsArgument("objects", targets), IntegerArgument("type", index),
                                    IntegerArgument("catalog", token)})
                  .status == EDITOR_OK,
          "attribute add failed");
    Check(document.History().UndoCount() == previous + 1, "attribute add not one undo step");
    Check(command("attribute.set-text", {ObjectArgument("target", a), IntegerArgument("type", index),
                                         IntegerArgument("catalog", token), TextArgument("text", std::string("42"))})
                  .status == EDITOR_OK,
          "attribute value failed");
    Check(ReadAttributeObjects(document, {int(index)}, {first.created}).front().value == "42",
          "attribute value differs");
    Check(document.History().Undo(), "attribute undo failed");
    Check(ReadAttributeObjects(document, {int(index)}, {first.created}).front().value == "0",
          "attribute undo lost value");
    Check(
        command("attribute.delete-type", {IntegerArgument("type", index), IntegerArgument("catalog", token)}).status ==
            EDITOR_OK,
        "attribute deletion failed");
    Check(command("attribute.create-type",
                  {TextArgument("name", std::string("Scene Query Value")), IntegerArgument("parameter", guid),
                   TextArgument("category", std::string("Scene Query Tests"))})
                  .status == EDITOR_OK,
          "attribute recreation failed");
    Check(command("attribute.add", {ObjectsArgument("objects", targets), IntegerArgument("type", index),
                                    IntegerArgument("catalog", token)})
                  .status == EDITOR_STALE,
          "reused type accepted stale catalog");
    publish();
    auto plan = query("scene.delete-plan", {ObjectsArgument("objects", targets),
                                            ObjectArgument("group", document.Reference(group.created))});
    Check(plan.status == EDITOR_OK && Refs(plan, "members") == targets && Refs(plan, "destroy").empty(),
          "group delete became destruction");
    // Layout paste follows the copied source, even after a different selection
    // or a reversible delete has moved that source into the graveyard.
    Check(command("scene.copy", {ObjectsArgument("objects", std::vector<ObjectRef>{b})}).status == EDITOR_OK,
          "copy Layout source");
    const auto retainedClipboard = query("scene.clipboard");
    Check(retainedClipboard.status == EDITOR_OK && Refs(retainedClipboard, "objects") == std::vector<ObjectRef>{b},
          "clipboard projection lost its source");
    const auto clipboardVersion = Number(retainedClipboard, "revision");
    const auto sceneTarget = document.Reference(document.Context()->GetCurrentScene()->GetID());
    const auto paste = [&] { return command("scene.duplicate-clipboard", {ObjectArgument("target", sceneTarget),
        IntegerArgument("clipboard-revision", clipboardVersion)}); };
    const auto cloneSteps = document.History().UndoCount();
    const auto cloned = paste();
    const auto copies = Refs(cloned, "copies");
    Check(cloned.status == EDITOR_OK && copies.size() == 1 && document.Resolve(copies.front()) &&
        document.History().UndoCount() == cloneSteps + 1, "clipboard duplication lost its result or split undo");
    Check(document.History().Undo(), "undo clipboard duplication");
    const auto retired = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(retired.ok, "create retained clipboard source");
    const auto retiredRef = document.Reference(retired.created);
    Check(command("scene.copy", {ObjectsArgument("objects", std::vector<ObjectRef>{retiredRef})}).status == EDITOR_OK,
        "copy retained source");
    const auto retainedVersion = Number(query("scene.clipboard"), "revision");
    Check(document.History().Undo() && !document.Resolve(retiredRef) && document.Context()->GetObject(retired.created),
        "undone clipboard source was not retained");
    Check(command("scene.duplicate-clipboard", {ObjectArgument("target", sceneTarget),
        IntegerArgument("clipboard-revision", retainedVersion)}).status == EDITOR_OK,
        "retained clipboard source could not be pasted after undo");
    Check(document.History().Undo(), "undo retained source paste");
    Check(command("scene.copy", {ObjectsArgument("objects", std::vector<ObjectRef>{a})}).status == EDITOR_OK, "replace clipboard");
    const auto staleSteps = document.History().UndoCount(); const auto staleRevision = document.ChangeRevision();
    Check(paste().status == EDITOR_STALE && document.History().UndoCount() == staleSteps &&
        document.ChangeRevision() == staleRevision && Refs(retainedClipboard, "objects") == std::vector<ObjectRef>{b},
        "old clipboard revision duplicated a different source or changed an owned query");
    Check(command("scene.clear-clipboard").status == EDITOR_OK && Refs(query("scene.clipboard"), "objects").empty(),
          "clearing clipboard did not clear the shared source");
    uint64_t version = 0;
    Check(app.Host()->queryRevision(app.Host()->data, EditorString EDITOR_TEXT("scene.tree"), &version) == EDITOR_OK &&
              version,
          "published tree version missing");
    EditorStatus thread = EDITOR_OK;
    uint64_t unchanged = 123;
    std::thread worker([&] {
        thread = app.Host()->queryRevision(app.Host()->data, EditorString EDITOR_TEXT("scene.tree"), &unchanged);
    });
    worker.join();
    Check(thread != EDITOR_OK && unchanged == 123, "query version crossed threads");
    available = false;
    Check(query("scene.tree").status != EDITOR_OK &&
              command("scene.priority", {ObjectsArgument("objects", targets), IntegerArgument("value", 5)}).status !=
                  EDITOR_OK,
          "native work admitted scene reads/writes");
    available = true;
    Check(DestroyLevelObjects(document, {first.created}).ok, "stale fixture destruction failed");
    publish();
    Check(command("scene.visibility", {ObjectsArgument("objects", targets), IntegerArgument("mode", 1)}).status ==
              EDITOR_STALE,
          "stale object modified scene");
    const auto snapshot = tree.fields;
    Check(Refs(tree, "objects") == refs, "later command changed owned tree projection");
    Check(document.New(&error), error.c_str());
    publish();
    Check(command("scene.workset-delete", {IntegerArgument("workset", Number(workset, "workset"))}).status ==
              EDITOR_STALE,
          "worksets survived document replacement");
    scene.Stop();
    app.Stop();
}
