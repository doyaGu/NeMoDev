#include "features/layout/LayoutAuthoring.h"
#include "application/ObjectSelection.h"
#include "application/Preferences.h"
#include "core/document/Document.h"
#include "core/graph/ParameterValues.h"
#include "core/level/LevelCommands.h"
#include "core/layout/LayoutView.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/schematic/SchematicNavigation.h"
#include "workspace/EditorClient.h"
#include "editor/ImGuiDrawing.h"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
const EditorField &Field(const EditorResponse &r, const char *key) {
    const auto *field = EditorClient::Find(r, key);
    Check(field != nullptr, "missing authoring field");
    return *field;
}
EditorValue Real(const char *key, double value) {
    EditorValue result{EditorLiteral(key), EDITOR_REAL_VALUE};
    result.real = value;
    return result;
}
uint64_t Rotate() {
    for (int i = 0; i < CKGetPrototypeDeclarationCount(); ++i) {
        auto *declaration = CKGetPrototypeDeclaration(i);
        if (declaration && declaration->GetName() && !std::strcmp(declaration->GetName(), "Rotate")) {
            const auto guid = declaration->GetGuid();
            return (uint64_t(guid.d1) << 32) | guid.d2;
        }
    }
    throw std::runtime_error("Rotate is missing from the test runtime");
}
std::vector<ObjectRef> Inputs(const EditorResponse &r) {
    std::vector<ObjectRef> result;
    for (const auto &row : Field(r, "inputs").fields)
        for (const auto &field : row.fields)
            if (field.key == "object")
                result.push_back(field.object);
    return result;
}
struct SavedState {
    Document &doc;
    ObjectSelection &selection;
    DocumentToken token;
    size_t undo;
    bool redo, dirty, locked;
    std::vector<ObjectRef> objects;
    SavedState(Document &doc, ObjectSelection &selection)
        : doc(doc), selection(selection), token(doc.Token()), undo(doc.History().UndoCount()),
          redo(doc.History().CanRedo()), dirty(doc.Dirty()), locked(selection.Locked()),
          objects(selection.References()) {}
    void CheckRestored() const {
        Check(doc.Token() == token && doc.History().UndoCount() == undo && doc.History().CanRedo() == redo &&
                  doc.Dirty() == dirty && selection.References() == objects && selection.Locked() == locked,
              "Layout authoring changed the original document, selection or history");
    }
};
} // namespace

void TestLayoutAuthoring(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    Document doc(runtime);
    std::string error;
    Check(doc.New(&error), error.c_str());
    const auto first = CreateLayoutObject(doc, LayoutObject::Frame3D),
               second = CreateLayoutObject(doc, LayoutObject::Frame3D);
    Check(first.ok && second.ok, "create Layout authoring objects");
    ObjectSelection selection;
    selection.Restore(doc, {first.created}, true);
    Preferences preferences;
    LayoutView view(runtime);
    view.CompositionChanged();
    SchematicNavigation navigation;
    bool available = true;
    EditorApplication editor([&] { return &doc; }, [&] { return available; });
    LayoutAuthoring authoring(
        editor, [&] { return &doc; }, [&]() -> LayoutView & { return view; }, selection, preferences,
        [&]() -> SchematicNavigation & { return navigation; });
    int remembers = 0, shows = 0;
    authoring.rememberBehavior = [&](uint64_t) { ++remembers; };
    authoring.showBehavior = [&](ObjectRef script, ObjectRef block) {
        Check(doc.Resolve(script) && doc.Resolve(block), "drop navigation lost object lifetimes");
        ++shows;
    };
    Check(authoring.Register(error) && editor.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    auto sync = [&] { editor.Attach(doc.Token(), doc.TopologyRevision(), true, true); };
    sync();
    const EditorClient client(editor.Host(), 0);
    const auto rotate = Rotate();
    const auto firstRef = doc.Reference(first.created), secondRef = doc.Reference(second.created);
    const std::vector<ObjectRef> one{firstRef}, both{firstRef, secondRef};
    auto drop = [&](const std::vector<ObjectRef> &targets, uint64_t ask = 0) {
        return client.Command(
            "scene.drop-behavior",
            {ObjectsArgument("objects", targets), IntegerArgument("prototype", rotate), IntegerArgument("ask", ask)});
    };
    auto finish = [&](uint64_t id, bool accept, const std::vector<ObjectRef> &values = {}) {
        const EditorValue args[] = {IntegerArgument("drop", id), IntegerArgument("accept", accept),
                                    ObjectsArgument("values", values)};
        return editor.ApplyEdit("layout.finish-drop", {client.Scope(), args, 3});
    };
    auto clean = [&] {
        doc.History().Clear();
        doc.MarkSaved();
        doc.History().MarkSavePoint();
        sync();
    };
    clean();
    const SavedState reads(doc, selection);
    const auto revision = doc.ChangeRevision(), topology = doc.TopologyRevision();
    const auto creation = client.Query("layout.creation");
    Check(creation.status == EDITOR_OK && Field(creation, "target").object == firstRef &&
              Field(creation, "camera").object,
          "creation query lost camera or selection");
    Check(EditorClient::Integer(creation, "camera-cone") ==
                  uint64_t(preferences.Bool("3D Layout\\Auto Show Camera Cone")) &&
              EditorClient::Integer(creation, "show-player") ==
                  uint64_t(preferences.Bool("3D Layout\\Auto Show Entities In Player Mode")),
          "creation defaults differ from preferences");
    Check(client.Query("layout.drop-parameters").status == EDITOR_NO_CHANGE, "unexpected pending drop");
    Check(client.Query("layout.drop-target", {Real("x", 0), Real("y", 0), TextArgument("kind", "unknown")}).status ==
              EDITOR_INVALID,
          "unknown drop kind accepted");
    Check(client.Query("layout.drop-target", {Real("x", std::numeric_limits<double>::quiet_NaN()), Real("y", 0),
                                              TextArgument("kind", "behavior")})
                  .status == EDITOR_INVALID,
          "NaN drop position accepted");
    Check(client.Command("scene.drop-behavior", {ObjectsArgument("objects", one), IntegerArgument("prototype", 0)})
                  .status == EDITOR_INVALID,
          "unknown behavior accepted");
    const double shortPoint[] = {1};
    EditorValue position{EDITOR_TEXT("position"), EDITOR_REAL_LIST};
    position.reals = {shortPoint, 1};
    Check(client.Command("scene.drop-behavior", {position, IntegerArgument("prototype", rotate)}).status ==
              EDITOR_INVALID,
          "incomplete drop position was accepted");
    auto stale = firstRef;
    ++stale.lifetime;
    Check(client.Command("scene.drop-behavior",
                         {ObjectsArgument("objects", {firstRef, stale}), IntegerArgument("prototype", rotate)})
                  .status == EDITOR_STALE,
          "mixed stale targets partially changed the document");
    reads.CheckRestored();
    Check(doc.ChangeRevision() == revision && doc.TopologyRevision() == topology,
          "read-only authoring changed revisions");
    available = false;
    Check(client.Query("layout.creation").status == EDITOR_REFUSED && drop(one).status == EDITOR_REFUSED,
          "authoring accessed the runtime while busy");
    available = true;
    EditorStatus worker = EDITOR_OK;
    std::thread thread([&] { worker = client.Query("layout.creation").status; });
    thread.join();
    Check(worker == EDITOR_REFUSED, "authoring query ran on wrong thread");

    // Cancel must preserve an existing Redo branch, the selection and savepoint.
    Check(CreateLayoutObject(doc, LayoutObject::Frame3D).ok && doc.History().Undo(), "create Redo fixture");
    sync();
    const SavedState cancelled(doc, selection);
    const auto pending = drop(one);
    Check(pending.status == EDITOR_OK && doc.EditOpen() && doc.History().CanRedo(), "drop lost pending edit or Redo");
    auto input = client.Query("layout.drop-parameters");
    Check(input.status == EDITOR_OK && !Inputs(input).empty(), "drop input projection missing");
    Check(client.Command("layout.highlight-drop", {ObjectArgument("target", firstRef)}).status == EDITOR_REFUSED,
          "another edit entered the drop dialog");
    Check(finish(EditorClient::Integer(input, "drop") + 1, false).status == EDITOR_STALE && doc.EditOpen(),
          "stale drop response changed the active edit");
    Check(finish(EditorClient::Integer(input, "drop"), false).status == EDITOR_OK && !doc.EditOpen(),
          "drop cancellation failed");
    cancelled.CheckRestored();
    Check(!doc.Resolve(Field(pending, "created").object), "cancelled behavior remained live");
    Check(client.Query("layout.drop-parameters").status == EDITOR_NO_CHANGE, "cancel left pending input");

    clean();
    const auto accepted = drop(one);
    Check(accepted.status == EDITOR_OK, "drop behavior for parameter edits");
    input = client.Query("layout.drop-parameters");
    auto parameters = Inputs(input);
    std::vector<ObjectRef> drafts;
    std::vector<CKParameterOut *> working;
    for (auto ref : parameters) {
        auto *target = CKParameter::Cast(doc.Resolve(ref));
        auto *work = doc.Context()->CreateCKParameterOut("Layout drop draft", target->GetGUID(), TRUE);
        Check(work && work->CopyValue(target, FALSE) == CK_OK, "copy drop parameter draft");
        working.push_back(work);
        drafts.push_back(doc.Reference(work->GetID()));
    }
    ParameterValueDescription before;
    Check(DescribeParameterValue(doc, CKParameter::Cast(doc.Resolve(parameters.front())), before),
          "read dropped parameter");
    auto changed = before.value;
    if (!changed.components.empty())
        changed.components.front() += 2.5;
    else if (ParameterRealValue(changed.kind))
        changed.real += 2.5;
    else
        changed.integer = changed.integer ? 0 : 1;
    Check(WriteParameterDraft(doc, working.front(), changed), "edit drop draft");
    auto wrong = drafts;
    ++wrong.back().lifetime;
    Check(finish(EditorClient::Integer(input, "drop"), true, wrong).status == EDITOR_STALE && doc.EditOpen(),
          "invalid draft partially committed");
    Check(finish(EditorClient::Integer(input, "drop"), true, drafts).status == EDITOR_OK && !doc.EditOpen() &&
              doc.History().UndoCount() == 1,
          "drop and parameter edits were not one undo step");
    for (auto *work : working)
        doc.Context()->DestroyObject(work->GetID());
    ParameterValueDescription after;
    Check(DescribeParameterValue(doc, CKParameter::Cast(doc.Resolve(parameters.front())), after) &&
              after.value == changed,
          "drop ignored the value editor's result");
    const auto block = Field(accepted, "created").object;
    Check(doc.History().Undo() && !doc.Resolve(block) && !doc.Dirty(), "drop Undo did not restore savepoint");
    Check(doc.History().Redo() && doc.Resolve(block) &&
              DescribeParameterValue(doc, CKParameter::Cast(doc.Resolve(parameters.front())), after) &&
              after.value == changed,
          "drop Redo lost parameter values or object identities");
    Check(selection.References() == one && selection.Locked(), "drop changed selection or lock");

    clean();
    const auto multi = drop(both, 1);
    Check(multi.status == EDITOR_OK && doc.EditOpen(), "selection drop did not ask for last inputs");
    input = client.Query("layout.drop-parameters");
    Check(finish(EditorClient::Integer(input, "drop"), false).status == EDITOR_OK && !doc.Dirty() &&
              doc.History().UndoCount() == 0,
          "selection drop cancellation was partial");
    const auto quiet = drop(both, 2);
    Check(quiet.status == EDITOR_OK && !doc.EditOpen() && doc.History().UndoCount() == 1,
          "noninteractive selection drop was not one undo step");
    Check(doc.History().Undo() && !doc.Dirty(), "selection drop Undo failed");

    clean();
    const uint64_t repeated[] = {rotate, rotate};
    const EditorValue repeatedBlocks{EDITOR_TEXT("prototypes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {repeated, 2}};
    const auto repeatedDrop = client.Command(
        "scene.drop-behavior", {ObjectsArgument("objects", one), repeatedBlocks, IntegerArgument("ask", 1)});
    Check(repeatedDrop.status == EDITOR_OK && doc.EditOpen() && doc.History().UndoCount() == 1,
          "a batch asked before its final behavior or merged earlier drops");
    input = client.Query("layout.drop-parameters");
    Check(finish(EditorClient::Integer(input, "drop"), false).status == EDITOR_OK &&
              !doc.Resolve(Field(repeatedDrop, "created").object) && doc.History().UndoCount() == 1,
          "cancelling the last behavior removed an earlier accepted drop");
    Check(doc.History().Undo() && !doc.Dirty(), "earlier behavior in a batch lost its Undo");

    clean();
    const auto hiddenScript = Field(accepted, "script").object;
    navigation.hiddenScripts.insert(hiddenScript);
    const SavedState hidden(doc, selection);
    Check(drop({hiddenScript}, 2).status == EDITOR_REFUSED && !doc.EditOpen(), "hidden graph accepted a behavior");
    hidden.CheckRestored();
    Check(drop({hiddenScript, secondRef}, 2).status == EDITOR_OK && doc.History().UndoCount() == 1,
          "one hidden Script blocked other valid drop receivers");
    Check(doc.History().Undo() && !doc.Dirty(), "partial refusal did not preserve the successful drop's Undo");
    navigation.hiddenScripts.clear();

    // Material replacement uses the same undoable command for every caller.
    auto *mesh = CKMesh::Cast(doc.Context()->CreateObject(CKCID_MESH, "Drop material mesh"));
    auto *from = CKMaterial::Cast(doc.Context()->CreateObject(CKCID_MATERIAL, "Drop material before"));
    auto *to = CKMaterial::Cast(doc.Context()->CreateObject(CKCID_MATERIAL, "Drop material after"));
    Check(mesh && from && to, "create material drop objects");
    mesh->SetVertexCount(3);
    mesh->SetFaceCount(1);
    mesh->SetFaceMaterial(0, from);
    doc.AdoptObjects({mesh->GetID(), from->GetID(), to->GetID()});
    clean();
    const auto material =
        client.Command("layout.replace-material", {ObjectArgument("mesh", doc.Reference(mesh->GetID())),
                                                   ObjectArgument("from", doc.Reference(from->GetID())),
                                                   ObjectArgument("to", doc.Reference(to->GetID()))});
    Check(material.status == EDITOR_OK && mesh->GetFaceMaterial(0) == to && doc.History().UndoCount() == 1,
          "material drop was not a unified undoable command");
    Check(doc.History().Undo() && mesh->GetFaceMaterial(0) == from && !doc.Dirty(), "material drop Undo failed");
    const SavedState invalidMaterial(doc, selection);
    Check(client.Command("layout.replace-material",
                         {ObjectArgument("mesh", firstRef), ObjectArgument("from", doc.Reference(from->GetID())),
                          ObjectArgument("to", doc.Reference(to->GetID()))})
                  .status == EDITOR_STALE,
          "wrong material target class accepted");
    invalidMaterial.CheckRestored();

    clean();
    Check(drop(one).status == EDITOR_OK && doc.EditOpen(), "pending drop before document close");
    authoring.DocumentClosing();
    Check(!doc.EditOpen() && doc.History().UndoCount() == 1, "document close lost accepted default drop");
    Check(shows == remembers && remembers >= 5, "drop navigation and recents differ between entry points");
    const auto old = firstRef;
    Check(doc.New(&error), error.c_str());
    authoring.Poll();
    sync();
    Check(client.Query("layout.drop-parameters").status == EDITOR_NO_CHANGE &&
              client.Command("scene.drop-behavior",
                             {ObjectsArgument("objects", {old}), IntegerArgument("prototype", rotate)})
                      .status == EDITOR_STALE,
          "document replacement reused old drop identities");
    editor.Stop();
}
