#include "features/scene/SceneCommands.h"
#include "features/scene/SceneQueries.h"
#include "features/scene/SceneViewData.h"
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
void TestAttributeProjectionCopies() {
    // A provider may return malformed or changing pages. Keep the previous
    // complete list in every failure case rather than exposing a partial list.
    struct Pages {
        int mode = 0;
        std::string name = "Copied name";
    } pages;
    EditorHost host{};
    host.data = &pages;
    host.scope = [](void *, EditorView view) { return EditorScope{{123}, view}; };
    host.query = [](void *data, EditorString, const EditorRequest *request, EditorSink sink) {
        const auto &pages = *static_cast<Pages *>(data);
        const auto first = request->values[1].integer;
        const auto target = request->values[0].object;
        EditorField row{std::to_string(first), EDITOR_RECORD};
        row.fields = {{"type", EDITOR_INTEGER_VALUE, {}, {}, pages.mode == 4 ? 7 : first + 7},
                      {"name", EDITOR_TEXT_VALUE, {}, pages.name},
                      {"category", EDITOR_TEXT_VALUE, {}, "Category"},
                      {"value", EDITOR_TEXT_VALUE, {}, "3"},
                      {"parameter", EDITOR_OBJECT, {{123}, uint32_t(first + 10), 5}},
                      {"parameter-name", EDITOR_TEXT_VALUE, {}, "Integer"}};
        if (pages.mode == 5)
            row.fields[4].object.document = {321};
        if (pages.mode == 7)
            row.fields.erase(row.fields.begin() + 3);
        if (pages.mode == 9)
            row.fields[4].object = {{123}, 0, 0};
        EditorField rows{"rows", EDITOR_RECORD};
        if (pages.mode != 6)
            rows.fields.push_back(std::move(row));
        const std::vector<EditorField> fields{
            {"target", EDITOR_OBJECT, target},
            {"catalog", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(pages.mode == 2 && first ? 12 : 11)},
            {"document-revision", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(pages.mode == 1 && first ? 4 : 3)},
            {"behavioral", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(pages.mode == 8 ? 2 : 1)},
            {"available", EDITOR_INTEGER_VALUE, {}, {}, 1},
            {"total", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(pages.mode == 3 && first ? 3 : 2)},
            {"next", EDITOR_INTEGER_VALUE, {}, {}, pages.mode == 6 ? first : first + 1},
            std::move(rows)};
        const auto output = BorrowEditorFields(fields);
        return sink.write(sink.data, output.data(), uint32_t(output.size()));
    };
    const EditorClient client(&host, 0);
    const ObjectRef target{{123}, 1, 2};
    ObjectAttributes copied;
    Check(QueryObjectAttributes(client, target, copied) && copied.rows.size() == 2 &&
              copied.rows[0].name == pages.name && copied.rows[1].parameter.object == 11,
          "copy provider attribute pages");
    pages.name = "Replacement name";
    for (int mode = 1; mode <= 9; ++mode) {
        pages.mode = mode;
        Check(!QueryObjectAttributes(client, target, copied) && copied.rows.size() == 2 &&
                  copied.rows.front().name == "Copied name" && copied.documentRevision == 3,
              "malformed provider replaced a complete attribute projection");
    }
}
} // namespace
void TestSceneCommands(CkRuntimeHost &runtime) {
    TestAttributeProjectionCopies();
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
    // Setup reads attachment order and parameter identities, including attributes
    // that the manager's global object list intentionally hides.
    const EditorClient client(app.Host(), 0);
    ObjectAttributes attached;
    const auto state = document.ChangeRevision();
    const auto steps = document.History().UndoCount();
    const bool canRedo = document.History().CanRedo(), dirty = document.Dirty();
    Check(
        QueryObjectAttributes(client, a, attached) && attached.behavioral && attached.available &&
            attached.rows.size() == 1 && attached.rows.front().type == index &&
            attached.rows.front().name == "Scene Query Value" && attached.rows.front().value == "0" &&
            attached.rows.front().parameter ==
                document.Reference(CKBeObject::Cast(document.Resolve(a))->GetAttributeParameter(int(index))->GetID()) &&
            document.ChangeRevision() == state && document.History().UndoCount() == steps &&
            document.History().CanRedo() == canRedo && document.Dirty() == dirty,
        "object attribute projection lost identity or changed document");
    Check(query("object.attributes", {ObjectArgument("target", a), IntegerArgument("first", UINT64_MAX)}).status ==
              EDITOR_INVALID,
          "attribute page accepted an invalid offset");
    const auto retained = attached;
    auto staleTarget = a;
    ++staleTarget.lifetime;
    Check(!QueryObjectAttributes(client, staleTarget, attached) && attached.target == retained.target &&
              attached.rows.front().parameter == retained.rows.front().parameter,
          "failed object attribute query replaced copied data");
    const std::vector<uint64_t> copiedTypes{index};
    const auto typesArgument = [&](const std::vector<uint64_t> &types) {
        return EditorValue{
            EDITOR_TEXT("types"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {types.data(), uint32_t(types.size())}};
    };
    Check(command("attribute.copy-values",
                  {ObjectArgument("target", a), typesArgument(copiedTypes), IntegerArgument("catalog", token)})
                  .status == EDITOR_OK,
          "copy Setup attribute values");
    AttributeClipboard copied;
    Check(QueryAttributeClipboard(client, copied) && copied.names == std::vector<std::string>{"Scene Query Value"},
          "attribute clipboard projection");
    const std::vector<uint64_t> invalidTypes{index, UINT64_MAX};
    Check(command("attribute.copy-values",
                  {ObjectArgument("target", a), typesArgument(invalidTypes), IntegerArgument("catalog", token)})
                      .status == EDITOR_INVALID &&
              Number(query("attribute.clipboard"), "revision") == copied.revision,
          "invalid copy replaced the clipboard");
    Check(command("attribute.set-text", {ObjectArgument("target", b), IntegerArgument("type", index),
                                         IntegerArgument("catalog", token), TextArgument("text", "7")})
                  .status == EDITOR_OK,
          "set paste destination");
    auto *material = document.Context()->CreateObject(CKCID_OBJECT, "Nonbehavioral paste destination");
    Check(material != nullptr, "create invalid paste destination");
    document.Include(material->GetID());
    publish();
    const auto pasteSteps = document.History().UndoCount();
    const auto pasteRevision = document.ChangeRevision();
    const auto pasteAttributes = [&](const std::vector<ObjectRef> &destinations, uint64_t version) {
        return command("attribute.paste-attributes",
                       {ObjectsArgument("objects", destinations), IntegerArgument("clipboard-revision", version)});
    };
    Check(pasteAttributes({b, document.Reference(material->GetID())}, copied.revision).status != EDITOR_OK &&
              ReadAttributeObjects(document, {int(index)}, {second.created}).front().value == "7" &&
              document.History().UndoCount() == pasteSteps && document.ChangeRevision() == pasteRevision,
          "failed attribute paste retained an earlier destination's change");
    Check(pasteAttributes({b}, copied.revision).status == EDITOR_OK &&
              document.History().UndoCount() == pasteSteps + 1 &&
              ReadAttributeObjects(document, {int(index)}, {second.created}).front().value == "0" &&
              document.History().Undo() &&
              ReadAttributeObjects(document, {int(index)}, {second.created}).front().value == "7" &&
              document.History().Redo() &&
              ReadAttributeObjects(document, {int(index)}, {second.created}).front().value == "0",
          "attribute paste did not remain one reversible edit");
    Check(command("attribute.copy-values",
                  {ObjectArgument("target", b), typesArgument(copiedTypes), IntegerArgument("catalog", token)})
                      .status == EDITOR_OK &&
              pasteAttributes({a}, copied.revision).status == EDITOR_STALE,
          "old attribute clipboard revision pasted replacement values");
    Check(command("attribute.edit-value",
                  {ObjectArgument("target", a), IntegerArgument("type", UINT64_MAX), IntegerArgument("catalog", token),
                   ObjectArgument("parameter", retained.rows.front().parameter)})
                  .status == EDITOR_INVALID,
          "overflowed attribute index reached native editor");
    auto wrongParameter = retained.rows.front().parameter;
    ++wrongParameter.lifetime;
    Check(command("attribute.edit-value",
                  {ObjectArgument("target", a), IntegerArgument("type", index), IntegerArgument("catalog", token),
                   ObjectArgument("parameter", wrongParameter)})
                  .status == EDITOR_STALE,
          "stale attribute parameter reached native editor");
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
    auto *attributes = document.Context()->GetAttributeManager();
    auto *dynamic = CKBeObject::Cast(
        document.Context()->CreateObject(CKCID_3DENTITY, "Dynamic Setup attributes", CK_OBJECTCREATION_DYNAMIC));
    Check(dynamic != nullptr, "dynamic attribute query fixture");
    document.Include(dynamic->GetID());
    std::vector<CKAttributeType> hiddenTypes;
    for (unsigned i = 0; i < 257; ++i) {
        const auto name = "Setup paging hidden " + std::to_string(i);
        const auto type = attributes->RegisterNewAttributeType(const_cast<CKSTRING>(name.c_str()), CKGUID(),
                                                               CKCID_BEOBJECT, CK_ATTRIBUT_HIDDEN);
        Check(type >= 0 && dynamic->SetAttribute(type), "hidden attribute query fixture");
        hiddenTypes.push_back(type);
    }
    publish();
    ObjectAttributes paged;
    Check(QueryObjectAttributes(client, document.Reference(dynamic->GetID()), paged) &&
              paged.rows.size() == hiddenTypes.size(),
          "Setup omitted hidden or dynamic attributes, or lost a query page");
    for (size_t i = 0; i < paged.rows.size(); ++i)
        Check(paged.rows[i].type == uint64_t(dynamic->GetAttributeType(int(i))) && !paged.rows[i].parameter,
              "Setup changed native attachment order or created a marker parameter");
    for (auto type : hiddenTypes)
        attributes->UnRegisterAttribute(type);
    document.Context()->DestroyObject(dynamic, CK_DESTROY_FREEID);
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
