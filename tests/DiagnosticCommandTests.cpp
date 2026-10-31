#include "features/diagnostics/DiagnosticCommands.h"
#include "features/schematic/SchematicCommands.h"
#include "core/catalog/BuildingBlockCatalog.h"
#include "core/document/Document.h"
#include "core/graph/TopologyCommands.h"
#include "core/level/LevelCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "workspace/EditorClient.h"
#include "editor/ImGuiDrawing.h"
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
const EditorField &Field(const EditorResponse &value, const char *key) {
    const auto *field = EditorClient::Find(value, key); Check(field, "missing diagnostics field"); return *field;
}
const EditorField &Field(const EditorField &value, const char *key) {
    for (const auto &field : value.fields) if (field.key == key) return field;
    throw std::runtime_error("missing diagnostics row field");
}
std::map<ObjectRef, CKDWORD> Flags(Document &doc) {
    std::map<ObjectRef, CKDWORD> values;
    const auto &objects = doc.Context()->GetObjectListByType(CKCID_OBJECT, TRUE);
    for (int i = 0; i < objects.Size(); ++i) if (objects[i]) values[doc.Reference(objects[i]->GetID())] = objects[i]->GetObjectFlags();
    return values;
}
}
void TestDiagnosticCommands(vdev::CkRuntimeHost &host) {
    using namespace vdev;
    Document doc(host); std::string error; Check(doc.New(&error), error.c_str());
    bool native = true, failWork = false; unsigned reads = 0;
    const auto current = [&] { ++reads; return &doc; };
    EditorApplication editor(current, [&] { return native; });
    RuntimeOperation work({}, [&](const std::string &label, const std::function<void()> &run) {
        if (failWork) throw std::runtime_error("Injected diagnostic worker refusal");
        if (!GuardedCall(run, &host, label.c_str())) throw std::runtime_error("Diagnostic worker failed");
    });
    DiagnosticCommands diagnostics(editor, work, host, current, [&] { return native; });
    SchematicCommands schematic(current);
    const auto draw = [](void *, const EditorHost *, const EditorValueInput *) -> uint32_t { return 0; };
    const EditorValueEditorDesc editors[] = {
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.string"), EDITOR_TEXT("test.diagnostics"), EDITOR_TEXT("Text"), draw, nullptr, EDITOR_TEXT_VALUE},
        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.color"), EDITOR_TEXT("test.diagnostics"), EDITOR_TEXT("Color"), draw, nullptr, EDITOR_REAL_LIST}};
    const EditorParameter batch[] = {{EDITOR_TEXT("scripts"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("prototypes"), EDITOR_INTEGER_LIST}};
    const EditorCommandDesc failReplace{sizeof(EditorCommandDesc), EDITOR_TEXT("test.refuse-replace"), EDITOR_TEXT("test.diagnostics"),
        EDITOR_TEXT("Refuse Replacements"), batch, 3, [](void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
            const auto status = host->execute(host->data, EditorString EDITOR_TEXT("schematic.replace-blocks"), request, sink);
            return status == EDITOR_OK ? EDITOR_ERROR : status;
        }, nullptr, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT};
    EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("test.diagnostics");
    module.editors = editors; module.editorCount = 2; module.commands = &failReplace; module.commandCount = 1;
    Check(diagnostics.Register(error) && schematic.Register(editor, error) && editor.Register(module, error) && editor.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    const auto attach = [&] { editor.Attach(doc.Token(), doc.TopologyRevision(), true, true); };
    attach();
    const auto one = editor.OpenScope(), two = editor.OpenScope();
    const EditorClient client(editor.Host(), one), other(editor.Host(), two), app(editor.Host(), 0);
    Check(Field(app.Query("diagnostics.info"), "runtime").text == host.RuntimeDirectory(),
          "runtime information changed the UTF-8 directory path");
    std::vector<ObjectRef> materials;
    for (int i = 0; i < 133; ++i) {
        const auto made = CreateLayoutObject(doc, LayoutObject::Material);
        Check(made.ok, "create unused diagnostic material"); materials.push_back(doc.Reference(made.created));
    }
    const auto owner = CreateLayoutObject(doc, LayoutObject::Frame3D);
    Check(owner.ok, "create diagnostic owner");
    const auto selected = doc.Reference(owner.created); editor.SetSelection(selected);
    doc.History().Clear(); doc.MarkSaved(); doc.History().MarkSavePoint();
    Check(RenameLevelObject(doc, selected, "Diagnostic owner").ok && doc.History().Undo(), "seed diagnostic Redo"); attach();
    const auto token = doc.Token(); const auto change = doc.ChangeRevision(); const auto topology = doc.TopologyRevision();
    const auto undo = doc.History().UndoCount(); const auto redo = doc.History().CanRedo(); const auto dirty = doc.Dirty();
    const auto flags = Flags(doc);
    const auto finish = [&](const EditorClient &origin, EditorResponse result) {
        Check(result.status == EDITOR_QUEUED && result.operation, "diagnostic check was not queued");
        editor.NativeAvailable(false); work.Finish(); editor.NativeAvailable(true);
        return origin.Completion(result.operation);
    };
    auto result = finish(client, client.Command("diagnostics.scan-unused"));
    Check(result.status == EDITOR_OK, "unused diagnostic scan failed");
    const auto unused = client.Query("diagnostics.unused");
    Check(unused.status == EDITOR_OK && Field(unused, "rows").fields.size() == 128 && Field(unused, "total").integer >= 133,
          "unused diagnostic query is missing its bounded page");
    const auto revision = Field(unused, "revision").integer;
    const auto tail = client.Query("diagnostics.unused", {IntegerArgument("first", 128), IntegerArgument("revision", revision)});
    Check(tail.status == EDITOR_OK && !Field(tail, "rows").fields.empty() && Field(tail, "revision").integer == revision,
          "unused diagnostic query lost its continuation");
    Check(Flags(doc) == flags && doc.Token() == token && doc.ChangeRevision() == change && doc.TopologyRevision() == topology &&
          doc.History().UndoCount() == undo && doc.History().CanRedo() == redo && doc.Dirty() == dirty && editor.Selection() == selected,
          "read-only diagnostic scan changed document flags, selection or undo history");
    Check(other.Query("diagnostics.unused").status == EDITOR_NO_CHANGE, "diagnostic results leaked into another view");
    Check(finish(other, other.Command("diagnostics.scan-unused")).status == EDITOR_OK, "second view diagnostic scan failed");
    const auto independent = other.Query("diagnostics.unused");
    failWork = true;
    Check(finish(client, client.Command("diagnostics.scan-unused")).status == EDITOR_ERROR &&
          client.Query("diagnostics.unused").fields == unused.fields && other.Query("diagnostics.unused").fields == independent.fields,
          "failed diagnostic scan discarded a previous result or another view's result");
    failWork = false;
    const auto frozen = client.Query("diagnostics.unused");
    native = false; editor.NativeAvailable(false); const auto beforeReads = reads;
    Check(client.Query("diagnostics.unused").fields == frozen.fields && client.Query("diagnostics.info").status == EDITOR_OK && reads == beforeReads,
          "busy diagnostic query entered native state");
    Check(client.Command("diagnostics.scan-unused").status == EDITOR_REFUSED &&
          client.Command("diagnostics.delete-unused", {IntegerArgument("revision", revision), ObjectsArgument("objects", materials)}).status == EDITOR_REFUSED,
          "busy diagnostic command modified native state");
    native = true; editor.NativeAvailable(true);
    EditorStatus thread = EDITOR_OK;
    std::thread worker([&] { thread = client.Query("diagnostics.unused").status; }); worker.join();
    Check(thread == EDITOR_REFUSED, "diagnostic query crossed editor threads");
    const auto oldMaterial = materials.back();
    Check(doc.Context()->DestroyObject(oldMaterial.object, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK,
          "delete a scanned material without a document edit");
    auto *replacementMaterial = doc.Context()->CreateObject(CKCID_MATERIAL, "Reused diagnostic material");
    Check(replacementMaterial && replacementMaterial->GetID() == oldMaterial.object && !doc.Resolve(oldMaterial),
          "diagnostic material slot was not reused");
    const std::vector<ObjectRef> expired{oldMaterial};
    Check(other.Command("diagnostics.delete-unused", {IntegerArgument("revision", Field(independent, "revision").integer),
          ObjectsArgument("objects", expired)}).status == EDITOR_STALE && doc.Context()->GetObject(oldMaterial.object) == replacementMaterial &&
          doc.ChangeRevision() == change && doc.History().CanRedo() == redo && editor.Selection() == selected,
          "an old diagnostic row deleted a replacement object or changed history");
    auto pending = client.Command("diagnostics.scan-memory");
    Check(pending.status == EDITOR_QUEUED, "queue diagnostic before closing its view");
    editor.CloseScope(one); diagnostics.Poll(); work.Finish();
    Check(app.Completion(pending.operation).status == EDITOR_CANCELLED, "closed diagnostic view still published its scan");
    Check(other.Command("diagnostics.delete-unused", {IntegerArgument("revision", revision), ObjectsArgument("objects", materials)}).status == EDITOR_STALE,
          "diagnostic row from another scan was accepted");
    const auto oldVersion = Field(independent, "revision").integer;
    Check(RenameLevelObject(doc, selected, "Changed after scan").ok, "change after unused scan"); attach();
    Check(other.Command("diagnostics.delete-unused", {IntegerArgument("revision", oldVersion), ObjectsArgument("objects", materials)}).status == EDITOR_STALE,
          "stale unused analysis was allowed to delete objects after document changes");
    Check(doc.History().Undo(), "undo change after scan"); attach();
    Check(finish(other, other.Command("diagnostics.scan-memory")).status == EDITOR_OK, "memory diagnostic scan failed");
    const auto memory = other.Query("diagnostics.memory");
    Check(memory.status == EDITOR_OK && Field(memory, "rows").fields.size() == 128 && Field(memory, "bytes").integer > 0,
          "memory diagnostic lost object sizes or pagination");
    const auto firstSize = Field(Field(memory, "rows").fields.front(), "size").integer;
    Check(firstSize > 0, "memory diagnostic returned an empty first row");
    Check(finish(other, other.Command("diagnostics.scan-unused")).status == EDITOR_OK, "refresh unused scan before deletion");
    auto fresh = other.Query("diagnostics.unused");
    const std::vector<ObjectRef> remove{materials.front()};
    Check(other.Command("diagnostics.delete-unused", {IntegerArgument("revision", Field(fresh, "revision").integer), ObjectsArgument("objects", remove)}).status == EDITOR_OK &&
          !doc.Resolve(materials.front()) && !doc.History().CanUndo() && !doc.History().CanRedo(), "unused deletion did not keep its irreversible history semantics");
    BuildingBlockCatalog catalog; catalog.Refresh(); CKGUID guid;
    for (const auto &entry : catalog.Entries()) {
        auto *prototype = CKGetPrototypeFromGuid(entry.guid);
        if (entry.category.rfind("Logics", 0) == 0 && prototype && prototype->GetInputCount() && prototype->GetOutputCount()) { guid = entry.guid; break; }
    }
    Check(guid.IsValid(), "diagnostic fixture has no logic Building Block");
    const auto script = CreateScript(doc, owner.created, "Version diagnostics"); Check(script.ok, "create diagnostic Script");
    const auto a = InsertBuildingBlock(doc, script.created, script.created, guid, Point(100, 100));
    const auto b = InsertBuildingBlock(doc, script.created, script.created, guid, Point(300, 100));
    Check(a.ok && b.ok, "create diagnostic Building Blocks");
    const auto aRef = doc.Reference(a.created), bRef = doc.Reference(b.created);
    for (auto ref : {aRef, bRef}) {
        auto *block = CKBehavior::Cast(doc.Resolve(ref)); block->SetVersion(block->GetVersion() + 1);
    }
    doc.History().Clear(); doc.MarkSaved(); doc.History().MarkSavePoint(); attach();
    Check(finish(other, other.Command("diagnostics.scan-versions")).status == EDITOR_OK, "version diagnostic scan failed");
    const auto versions = other.Query("diagnostics.versions");
    Check(Field(versions, "total").integer == 2 && Field(versions, "report").text.find("Version Differences") != std::string::npos,
          "version diagnostics did not retain changed Building Blocks");
    const auto versionArgs = std::vector<EditorValue>{IntegerArgument("revision", Field(versions, "revision").integer)};
    const auto replaced = other.Command("diagnostics.replace-versions", versionArgs);
    Check(replaced.status == EDITOR_OK && Field(replaced, "replaced").integer == 2 && doc.History().UndoCount() == 1 &&
          doc.IsDiscarded(aRef.object) && doc.IsDiscarded(bRef.object), "version replacements did not share one command and undo step");
    Check(doc.History().Undo() && doc.Resolve(aRef) && doc.Resolve(bRef) && !doc.Dirty(), "version replacement undo did not recover the original blocks");
    attach();
    const auto beforeInvalid = doc.ChangeRevision();
    const auto scriptRef = doc.Reference(script.created);
    const std::vector<ObjectRef> scripts{scriptRef, scriptRef}, objects{aRef, bRef};
    const std::vector<uint64_t> prototypes{(uint64_t(guid.d1) << 32) | guid.d2, UINT64_MAX};
    const EditorValue prototypeArg{EDITOR_TEXT("prototypes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {prototypes.data(), uint32_t(prototypes.size())}};
    Check(other.Command("schematic.replace-blocks", {ObjectsArgument("scripts", scripts), ObjectsArgument("objects", objects), prototypeArg}).status == EDITOR_INVALID &&
          doc.ChangeRevision() == beforeInvalid && !doc.History().CanUndo() && doc.History().CanRedo(), "invalid replacement batch partially changed the document or lost Redo");
    auto validPrototypes = prototypes; validPrototypes[1] = validPrototypes[0];
    const EditorValue validArg{EDITOR_TEXT("prototypes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {validPrototypes.data(), uint32_t(validPrototypes.size())}};
    Check(other.Command("test.refuse-replace", {ObjectsArgument("scripts", scripts), ObjectsArgument("objects", objects), validArg}).status == EDITOR_ERROR &&
          doc.Resolve(aRef) && doc.Resolve(bRef) && !doc.History().CanUndo() && doc.History().CanRedo() && !doc.Dirty(),
          "failed outer replacement command did not restore both blocks and Redo history");
    diagnostics.InvalidateVersions();
    Check(other.Query("diagnostics.versions", versionArgs).status == EDITOR_STALE &&
          other.Command("diagnostics.replace-versions", versionArgs).status == EDITOR_STALE, "plugin catalog changes retained an actionable version report");
    pending = other.Command("diagnostics.scan-memory");
    work.DocumentChanged(); Check(doc.New(&error), error.c_str()); attach(); diagnostics.Poll(); work.Finish();
    Check(app.Completion(pending.operation).status == EDITOR_CANCELLED && other.Query("diagnostics.memory").status == EDITOR_NO_CHANGE,
          "old diagnostic result reached a replacement document");
    pending = app.Command("diagnostics.scan-memory"); diagnostics.Stop(); work.Finish();
    Check(app.Completion(pending.operation).status == EDITOR_CANCELLED, "shutdown left a diagnostic operation pending");
}
