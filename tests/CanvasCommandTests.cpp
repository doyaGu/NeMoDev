#include "features/layout/LayoutCommands.h"
#include "application/ObjectSelection.h"
#include "features/layout/LayoutSceneData.h"
#include "features/objects/SelectionCommands.h"
#include "features/schematic/SchematicGraphCommands.h"
#include "features/schematic/SchematicPosition.h"
#include "workspace/EditorClient.h"
#include "core/document/Document.h"
#include "core/document/Clipboard.h"
#include "core/document/Edit.h"
#include "core/document/LinkValidation.h"
#include "core/document/ScriptSession.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/TopologyCommands.h"
#include "core/layout/LayoutView.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/schematic/SchematicEdits.h"
#include "editor/ImGuiDrawing.h"
#include <stdexcept>
#include <thread>
#include <cmath>
#include <algorithm>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
const EditorField &Field(const EditorResponse &response, const char *key) {
    const auto *field = EditorClient::Find(response, key); Check(field != nullptr, "missing canvas query field"); return *field;
}
EditorField Number(const char *key, uint64_t value) { return {key, EDITOR_INTEGER_VALUE, {}, {}, value}; }
struct Snapshot {
    Document &doc; DocumentToken document; uint64_t revision, topology; size_t undo; bool redo, dirty;
    explicit Snapshot(Document &doc) : doc(doc), document(doc.Token()), revision(doc.ChangeRevision()), topology(doc.TopologyRevision()),
        undo(doc.History().UndoCount()), redo(doc.History().CanRedo()), dirty(doc.Dirty()) {}
    void Unchanged(const char *message) const {
        Check(doc.Token() == document && doc.ChangeRevision() == revision && doc.TopologyRevision() == topology &&
            doc.History().UndoCount() == undo && doc.History().CanRedo() == redo && doc.Dirty() == dirty, message);
    }
};
}
void TestCanvasCommands(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    Document doc(runtime); std::string error; Check(doc.New(&error), error.c_str());
    LayoutView view(runtime); view.CompositionChanged();
    ObjectSelection selection;
    const auto first = CreateLayoutObject(doc, LayoutObject::Frame3D), second = CreateLayoutObject(doc, LayoutObject::Frame3D);
    const auto script = CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(), "Canvas commands");
    Check(first.ok && second.ok && script.ok, "create canvas command fixture");
    const auto graph = CreateBehaviorGraph(doc, script.created, script.created, "Child", Point(80, 80));
    Check(graph.ok, "create child graph");
    auto data = doc.Session(script.created)->Data(); auto *runtimeScript = CKBehavior::Cast(doc.Resolve(doc.Reference(script.created)));
    auto *chunk = GenerateInterfaceChunk(data, runtimeScript); Check(chunk != nullptr, "seed a valid saved Schematic chunk");
    runtimeScript->SetInterfaceChunk(chunk); doc.RefreshScripts();
    selection.Restore(doc, {first.created, second.created});
    bool available = true; EditorApplication editor([&] { return &doc; }, [&] { return available; });
    SelectionCommands select([&] { return &doc; }, selection);
    LayoutCommands layout(editor, [&] { return &doc; }, [&](EditorView) { return LayoutTarget{0, view}; }, selection);
    Clipboard clipboard;
    SchematicGraphCommands schematic([&] { return &doc; }, [] { return true; }, clipboard);
    Check(select.Register(editor, error) && layout.Register(error) && schematic.Register(editor, error) && editor.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    auto sync = [&] { editor.Attach(doc.Token(), doc.TopologyRevision(), true, true); };
    sync(); const EditorClient client(editor.Host(), 0);
    auto run = [&](const char *id, const std::vector<EditorField> &fields) {
        const auto values = BorrowEditorFields(fields); return client.Command(id, std::vector<EditorValue>(values.data(), values.data() + values.size()));
    };
    auto graphArgs = [&](uint32_t object = 0) {
        const auto state = client.Query("schematic.script-state", {ObjectArgument("target", doc.Reference(script.created)), IntegerArgument("id", object)});
        Check(state.status == EDITOR_OK, "read script edit state");
        return std::vector<EditorField>{Field(state, "target"), Field(state, "revision"), Field(state, "topology")};
    };
    doc.History().Clear(); doc.MarkSaved(); doc.History().MarkSavePoint();
    const Snapshot reads(doc);
    LayoutSceneData cameras, objects;
    Check(ReadLayoutScene(client, "cameras", cameras) && cameras.camera && cameras.rows.size() >= 5, "camera projection lost default views");
    Check(ReadLayoutScene(client, "explorer", objects, {IntegerArgument("shown", 0x1ff)}) && objects.Find(first.created), "explorer projection lost scene objects");
    const auto copied = objects.Find(first.created)->name;
    Check(client.Query("layout.scene", {TextArgument("section", "bad-section")}).status == EDITOR_INVALID, "unknown Layout query section was accepted");
    Check(client.Query("layout.scene", {TextArgument("section", "explorer"), IntegerArgument("shown", 0x200)}).status == EDITOR_INVALID, "invalid Explorer filter was accepted");
    reads.Unchanged("scene queries changed the document or undo history");
    auto fold = graphArgs(graph.created); fold.push_back({"object", EDITOR_OBJECT, doc.Reference(graph.created)});
    const bool folded = FindRecord(doc.Session(script.created)->Data(), graph.created)->folded;
    Check(run("schematic.toggle-fold", fold).status == EDITOR_OK && doc.History().UndoCount() == 1 &&
        FindRecord(doc.Session(script.created)->Data(), graph.created)->folded != folded, "fold command was not one undo step");
    const Snapshot foldedState(doc);
    Check(run("schematic.toggle-fold", fold).status == EDITOR_STALE, "stale graph fold was accepted");
    foldedState.Unchanged("stale graph fold changed state");
    Check(doc.History().Undo() && !doc.Dirty() && FindRecord(doc.Session(script.created)->Data(), graph.created)->folded == folded, "fold undo did not restore the save point");
    Check(doc.History().Redo(), "redo graph fold"); sync();
    auto create = graphArgs(); create.push_back({"owner", EDITOR_OBJECT, doc.Reference(script.created)});
    EditorField at{"at", EDITOR_REAL_LIST}; at.reals = {160, 120}; create.push_back(at);
    const auto created = run("schematic.create-graph", create);
    Check(created.status == EDITOR_OK && Field(created, "created").object && doc.Resolve(Field(created, "created").object), "graph command lost created object identity");
    auto *sender = CKBehavior::Cast(doc.Resolve(Field(created, "created").object));
    const auto messageType = doc.Context()->GetMessageManager()->AddMessageType("Canvas message");
    sender->ModifyFlags(CKBEHAVIOR_MESSAGESENDER, 0);
    auto *messageInput = sender->CreateInputParameter("Message", CKPGUID_MESSAGE);
    auto *messageValue = sender->CreateLocalParameter("Message value", CKPGUID_MESSAGE);
    Check(messageInput && messageValue && messageValue->SetValue(&messageType, sizeof(messageType)) == CK_OK &&
        messageInput->SetDirectSource(messageValue) == CK_OK, "create message sender fixture");
    doc.MarkTopologyChanged(); sync();
    const Snapshot messageState(doc);
    const auto uses = client.Query("schematic.message-uses", {IntegerArgument("message", uint64_t(messageType))});
    const auto &useRows = Field(uses, "rows").fields;
    Check(uses.status == EDITOR_OK && useRows.size() == 1 &&
        std::any_of(useRows.front().fields.begin(), useRows.front().fields.end(), [&](const EditorField &f) {
            return f.key == "object" && f.object == doc.Reference(sender->GetID()); }), "message use projection lost source identity");
    Check(client.Query("schematic.message-uses", {IntegerArgument("message", UINT64_MAX)}).status == EDITOR_INVALID &&
        client.Query("schematic.activity", {ObjectArgument("target", doc.Reference(script.created))}).status == EDITOR_OK,
        "Schematic runtime queries did not validate the source");
    messageState.Unchanged("Schematic message or activity queries changed document state");
    Check(EditScriptData(doc, script.created, "Seed comments", [](InterfaceData &data) {
        data.rootBehavior.comments.emplace_back(); data.rootBehavior.comments.emplace_back();
    }), "create indexed Schematic fixture");
    const auto comment = ElementAddress::Capture(doc, script.created, {ElementKind::Comment, script.created, 0, 0});
    auto removeComments = graphArgs(); EditorField elements{"elements", EDITOR_RECORD};
    elements.fields.push_back(SchematicPosition(comment, "0"));
    auto invalidComment = comment; invalidComment.revision--;
    elements.fields.push_back(SchematicPosition(invalidComment, "1")); removeComments.push_back(elements);
    const Snapshot indexedState(doc);
    Check(run("schematic.delete-elements", removeComments).status == EDITOR_STALE,
        "partially stale indexed selection was accepted"); indexedState.Unchanged("indexed batch partially deleted comments");
    removeComments.back().fields.resize(1);
    Check(run("schematic.delete-elements", removeComments).status == EDITOR_OK &&
        doc.Session(script.created)->Data().rootBehavior.comments.size() == 1 && doc.History().UndoCount() == indexedState.undo + 1,
        "indexed delete did not create one undo step");
    Check(doc.History().Undo() && doc.Session(script.created)->Data().rootBehavior.comments.size() == 2,
        "indexed delete undo lost a comment");
    const Snapshot undoneComments(doc);
    Check(run("schematic.delete-elements", removeComments).status == EDITOR_STALE,
        "indexed position was accepted after Undo"); undoneComments.Unchanged("old indexed delete changed restored state");
    const auto selected = selection.References(); const bool locked = selection.Locked();
    const auto groupsBefore = doc.History().UndoCount();
    const auto grouped = client.Command("layout.create-selection-group", {TextArgument("name", std::string("Canvas selection")), ObjectsArgument("objects", selected)});
    Check(grouped.status == EDITOR_OK && Field(grouped, "created").object && doc.History().UndoCount() == groupsBefore + 1, "selection group was not one undo step");
    const auto group = Field(grouped, "created").object;
    const Snapshot duplicateGroup(doc);
    Check(client.Command("layout.create-selection-group", {TextArgument("name", std::string("CANVAS selection")),
        ObjectsArgument("objects", selected)}).status == EDITOR_REFUSED, "duplicate selection group was accepted");
    duplicateGroup.Unchanged("duplicate group changed document history");
    LayoutSceneData groups;
    Check(ReadLayoutScene(client, "groups", groups) && groups.matchingGroup == group && groups.Find(group.object), "group projection did not match the selection");
    Check(client.Command("selection.lock", {IntegerArgument("locked", 1)}).status == EDITOR_OK, "lock canvas selection");
    const Snapshot lockedState(doc);
    Check(client.Command("layout.choose-group", {ObjectArgument("target", group)}).status == EDITOR_REFUSED, "locked group changed the selection");
    lockedState.Unchanged("refused group selection changed the document");
    Check(client.Command("selection.lock", {IntegerArgument("locked", 0)}).status == EDITOR_OK, "unlock canvas selection");
    Check(client.Command("selection.clear").status == EDITOR_OK && client.Command("layout.choose-group", {ObjectArgument("target", group)}).status == EDITOR_OK &&
        selection.References() == selected && selection.Locked() == locked, "group selection did not use shared selection commands");
    Check(client.Command("selection.clear").status == EDITOR_OK && ReadLayoutScene(client, "groups", groups) && !groups.matchingGroup,
        "cached groups retained the previous selection in the same frame");
    Check(client.Command("layout.choose-group", {ObjectArgument("target", group)}).status == EDITOR_OK &&
        ReadLayoutScene(client, "groups", groups) && groups.matchingGroup == group,
        "group selection was not reflected in the cached projection");
    const auto camera = cameras.rows[1].object;
    const Snapshot cameraState(doc);
    Check(client.Command("layout.set-camera", {ObjectArgument("target", camera)}).status == EDITOR_OK && view.Camera()->GetID() == camera.object, "camera command did not select the projected camera");
    cameraState.Unchanged("camera browsing changed document history");
    const auto interfaceCamera = view.Camera(); interfaceCamera->SetFov(.25f);
    const auto reset = client.Command("layout.reset-camera", {ObjectArgument("target", camera)});
    Check(reset.status == EDITOR_OK || reset.status == EDITOR_NO_CHANGE, "reset interface camera failed");
    Check(doc.History().UndoCount() == cameraState.undo + (reset.status == EDITOR_OK ? 1 : 0) && doc.Dirty() == cameraState.dirty,
        "interface camera reset changed composition dirty state or split its undo step");
    if (reset.status == EDITOR_OK) Check(doc.History().Undo() && std::abs(view.Camera()->GetFov() - .25f) < .0001f && doc.Dirty() == cameraState.dirty,
        "interface camera reset could not be undone");
    const auto oldCamera = cameras.camera;
    Check(client.Command("layout.reset-camera", {ObjectArgument("target", oldCamera)}).status == EDITOR_STALE, "stale camera reset changed the new camera");
    const auto representationHistory = doc.History().UndoCount();
    const auto frames = client.Command("layout.frame-representation", {ObjectArgument("target", doc.Reference(first.created)), ObjectsArgument("objects", selected), IntegerArgument("value", 0)});
    Check((frames.status == EDITOR_OK || frames.status == EDITOR_NO_CHANGE) && doc.History().UndoCount() <= representationHistory + 1, "frame representation split its undo step");
    const Snapshot badFrames(doc); const std::vector<ObjectRef> invalidFrames{doc.Reference(first.created), doc.Reference(script.created)};
    Check(client.Command("layout.frame-representation", {ObjectArgument("target", doc.Reference(first.created)), ObjectsArgument("objects", invalidFrames), IntegerArgument("value", UINT64_MAX)}).status == EDITOR_INVALID,
        "invalid representation value reached the scene"); badFrames.Unchanged("invalid frame batch changed history");
    // A valid saved chunk followed by malformed unsaved display links exercises the same post-load explorer data.
    Check(EditScriptData(doc, script.created, "Seed invalid links", [&](InterfaceData &data) {
        Link link; link.start = LinkEndpoint(script.created, 0, ENDPOINT_BIN); link.end = LinkEndpoint(UINT32_MAX - 1, 0, ENDPOINT_BIN);
        data.rootBehavior.links.push_back(link); data.rootBehavior.links.push_back(link);
    }), "seed malformed Schematic links");
    doc.RefreshScripts(); const auto invalid = FindInvalidLinks(doc, true); Check(invalid.size() == 2, "invalid-link fixture did not produce two rows");
    schematic.LoadedLinks(invalid); doc.History().Clear(); doc.MarkSaved(); doc.History().MarkSavePoint(); sync();
    const auto links = client.Query("schematic.invalid-links"); Check(links.status == EDITOR_OK && EditorClient::Integer(links, "total") == 2, "invalid links projection lost rows");
    EditorField slots{"slots", EDITOR_INTEGER_LIST}; slots.integers = {0};
    const std::vector<EditorField> remove{Number("revision", EditorClient::Integer(links, "revision")), slots};
    Check(run("schematic.delete-invalid-links", remove).status == EDITOR_OK && doc.History().UndoCount() == 1 &&
        doc.Session(script.created)->Data().rootBehavior.links.size() == 1, "invalid links delete was not atomic");
    Check(doc.History().Undo() && !doc.Dirty() && doc.Session(script.created)->Data().rootBehavior.links.size() == 2, "invalid link undo did not restore the clean document");
    Check(client.Query("schematic.invalid-summary").status == EDITOR_OK, "invalid link query did not survive undo");
    const Snapshot oldLinks(doc); Check(run("schematic.delete-invalid-links", remove).status == EDITOR_STALE, "old invalid-link row was reused after undo"); oldLinks.Unchanged("old invalid-link deletion changed state");
    available = false; editor.NativeAvailable(false);
    ReadLayoutScene(client, "explorer", objects, {IntegerArgument("shown", 0x1ff)});
    Check(objects.Find(first.created) && objects.Find(first.created)->name == copied, "busy query overwrote its retained scene projection");
    Check(client.Query("layout.scene", {TextArgument("section", std::string("explorer"))}).status != EDITOR_OK &&
        client.Command("layout.copy", {ObjectsArgument("objects", {})}).status == EDITOR_REFUSED,
        "busy native commands or queries entered Layout state");
    editor.NativeAvailable(true); available = true;
    EditorStatus worker = EDITOR_OK;
    std::thread wrongThread([&] { worker = client.Query("schematic.messages").status; }); wrongThread.join();
    Check(worker != EDITOR_OK, "worker entered a native Schematic query");
    const auto token = doc.Token(); auto pending = fold;
    Check(doc.New(&error), error.c_str()); view.CompositionChanged(); sync();
    Check(doc.Token() != token && run("schematic.toggle-fold", pending).status == EDITOR_STALE &&
        client.Query("schematic.invalid-summary").status == EDITOR_OK && EditorClient::Integer(client.Query("schematic.invalid-summary"), "total") == 0,
        "document replacement reused graph targets or loaded invalid links");
}
