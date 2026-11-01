#include "features/schematic/SchematicGraphCommands.h"
#include "features/schematic/SchematicCommands.h"
#include "features/schematic/SchematicPosition.h"
#include "features/values/ParameterCommands.h"
#include "features/objects/ObjectCommands.h"
#include "workspace/EditorClient.h"
#include "core/catalog/BuildingBlockCatalog.h"
#include "core/document/Clipboard.h"
#include "core/document/ScriptSession.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/RuntimeGraph.h"
#include "core/graph/BehaviorCommands.h"
#include "core/catalog/ParameterCatalog.h"
#include "core/graph/TopologyCommands.h"
#include "core/graph/OperationCommands.h"
#include "core/graph/ParameterValues.h"
#include "core/graph/ValueCommands.h"
#include "core/schematic/SchematicEdits.h"
#include "core/runtime/CkRuntimeHost.h"
#include "editor/ImGuiDrawing.h"
#include <charconv>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
EditorField &Field(std::vector<EditorField> &fields, const char *key) {
    for (auto &field : fields)
        if (field.key == key)
            return field;
    throw std::runtime_error(std::string("Missing Schematic field: ") + key);
}
const EditorField &Field(const std::vector<EditorField> &fields, const char *key) {
    for (const auto &field : fields)
        if (field.key == key)
            return field;
    throw std::runtime_error(std::string("Missing Schematic field: ") + key);
}
const EditorField &Field(const EditorResponse &r, const char *key) {
    const auto *field = EditorClient::Find(r, key);
    Check(field, key);
    return *field;
}
EditorField Number(const char *key, uint64_t n) { return {key, EDITOR_INTEGER_VALUE, {}, {}, n}; }
uint64_t Type(CKGUID type) { return uint64_t(type.d1) << 32 | type.d2; }
int NativeText(CKParameter *parameter, char *text, CKBOOL fromText) {
    int number = 0;
    if (fromText) {
        if (text && std::string(text) == "throw") {
            number = -1;
            parameter->SetValue(&number, sizeof(number));
            throw std::runtime_error("Injected native text conversion failure");
        }
        if (!text || text[0] != '#')
            return CKERR_INVALIDPARAMETER;
        const auto *end = text + std::strlen(text);
        const auto parsed = std::from_chars(text + 1, end, number);
        if (parsed.ec != std::errc() || parsed.ptr != end)
            return CKERR_INVALIDPARAMETER;
        return parameter->SetValue(&number, sizeof(number));
    }
    if (parameter->GetValue(&number, FALSE) != CK_OK)
        return 0;
    const auto value = "#" + std::to_string(number);
    if (text)
        std::memcpy(text, value.c_str(), value.size() + 1);
    return int(value.size() + 1);
}
struct Snapshot {
    Document &doc;
    size_t undo;
    bool dirty, redo;
    uint64_t revision, topology;
    explicit Snapshot(Document &doc)
        : doc(doc), undo(doc.History().UndoCount()), dirty(doc.Dirty()), redo(doc.History().CanRedo()),
          revision(doc.ChangeRevision()), topology(doc.TopologyRevision()) {}
    void Unchanged(const char *message) const {
        Check(undo == doc.History().UndoCount() && dirty == doc.Dirty() && redo == doc.History().CanRedo() &&
                  revision == doc.ChangeRevision() && topology == doc.TopologyRevision(),
              message);
    }
};
} // namespace
void TestSchematicAuthoring(vdev::CkRuntimeHost &runtime, const std::string &modulePath) {
    using namespace vdev;
    Document doc(runtime);
    std::string error;
    Check(doc.New(&error), error.c_str());
    auto *context = doc.Context();
    const auto root = CreateScript(doc, context->GetCurrentLevel()->GetID(), "Authoring");
    Check(root.ok, "create authoring Script");
    const auto other = CreateScript(doc, context->GetCurrentLevel()->GetID(), "Other authoring");
    Check(other.ok, "create other Script");
    Clipboard clipboard;
    EditorApplication app([&] { return &doc; });
    SchematicGraphCommands graphs([&] { return &doc; }, [] { return true; }, clipboard);
    SchematicCommands display([&] { return &doc; });
    ParameterCommands parameters([&] { return &doc; }, &clipboard);
    ObjectCommands objects([&] { return &doc; });
    const char *ids[] = {
        "value.boolean",     "value.integer",      "value.real",    "value.enumeration", "value.object",
        "value.angle",       "value.vector2",      "value.vector3", "value.quaternion",  "value.euler",
        "value.matrix",      "value.color",        "value.box",     "value.rect",        "value.string",
        "value.structure",   "value.object-array", "value.flags",   "value.curve2d",     "value.dependencies",
        "value.lod-options", "value.time",         "value.key",     "value.script",      "value.animation",
        "value.message",     "value.attribute",    "value.synchro", "text.single-line"};
    const EditorValueType types[] = {
        EDITOR_INTEGER_VALUE, EDITOR_INTEGER_VALUE, EDITOR_REAL_VALUE,    EDITOR_INTEGER_VALUE, EDITOR_OBJECT,
        EDITOR_REAL_VALUE,    EDITOR_REAL_LIST,     EDITOR_REAL_LIST,     EDITOR_REAL_LIST,     EDITOR_REAL_LIST,
        EDITOR_REAL_LIST,     EDITOR_REAL_LIST,     EDITOR_REAL_LIST,     EDITOR_REAL_LIST,     EDITOR_TEXT_VALUE,
        EDITOR_RECORD,        EDITOR_OBJECT_LIST,   EDITOR_INTEGER_VALUE, EDITOR_RECORD,        EDITOR_RECORD,
        EDITOR_RECORD,        EDITOR_REAL_VALUE,    EDITOR_INTEGER_VALUE, EDITOR_OBJECT,        EDITOR_OBJECT,
        EDITOR_RECORD,        EDITOR_INTEGER_VALUE, EDITOR_RECORD,        EDITOR_TEXT_VALUE};
    EditorValueEditorDesc editors[std::size(ids)]{};
    for (unsigned i = 0; i < std::size(ids); ++i)
        editors[i] = {sizeof(EditorValueEditorDesc),
                      EDITOR_IMGUI_VERSION,
                      EditorLiteral(ids[i]),
                      EDITOR_TEXT("test.values"),
                      EDITOR_TEXT("Test value"),
                      [](void *, const EditorHost *, const EditorValueInput *) -> uint32_t { return 0; },
                      nullptr,
                      types[i]};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("test.schematic-authoring-values");
    module.editors = editors;
    module.editorCount = uint32_t(std::size(editors));
    Check(graphs.Register(app, error) && display.Register(app, error) && parameters.Register(app, error) &&
              objects.Register(app, error) && app.Register(module, error) && app.Load(modulePath, error),
          error.c_str());
    EditorConfiguration native;
    native.providers = {{"command", "schematic.edit-parameters", "builtin.schematic-parameters"},
                        {"command", "schematic.edit-geometry", "builtin.schematic-geometry"}};
    Check(app.Activate(EDITOR_IMGUI_VERSION, error, native), error.c_str());
    const auto sync = [&] { app.Attach(doc.Token(), doc.TopologyRevision(), true, true); };
    sync();
    const auto view = app.OpenScope();
    EditorView activeView = view;
    const EditorClient client(app.Host(), view), global(app.Host(), 0);
    const auto saved = [&] {
        doc.History().Clear();
        doc.MarkSaved();
        doc.History().MarkSavePoint();
        sync();
    };
    const auto request = [&](const char *id, const std::vector<EditorField> &fields, bool query = false) {
        const auto values = BorrowEditorFields(fields);
        return query ? global.Query(id, {values.data(), values.data() + values.size()})
                     : global.Command(id, {values.data(), values.data() + values.size()});
    };
    const auto create = [&](CK_ID owner = 0) {
        EditorField at{"at", EDITOR_REAL_LIST};
        at.reals = {233, 117};
        return std::vector<EditorField>{{"target", EDITOR_OBJECT, doc.Reference(root.created)},
                                        {"owner", EDITOR_OBJECT, doc.Reference(owner ? owner : root.created)},
                                        Number("revision", doc.Session(root.created)->Revision()),
                                        Number("topology", doc.TopologyRevision()),
                                        at};
    };
    saved();
    Snapshot read(doc);
    const auto typeProjection = client.Query("schematic.parameter-types");
    Check(typeProjection.status == EDITOR_OK && !Field(typeProjection, "types").fields.empty(),
          "parameter type projection is empty");
    Check(client.Query("schematic.object-info", {ObjectArgument("target", doc.Reference(root.created))}).status ==
              EDITOR_OK,
          "object information failed");
    read.Unchanged("authoring queries changed the document");
    auto comment = create();
    const auto madeComment = request("schematic.add-comment", comment);
    Check(madeComment.status == EDITOR_OK && doc.History().UndoCount() == 1 &&
              doc.Session(root.created)->Data().rootBehavior.comments.size() == 1,
          "comment command did not create one undo step");
    Check(doc.Session(root.created)->Data().rootBehavior.comments.front().hPos == 240 && doc.History().Undo() &&
              !doc.Dirty(),
          "comment undo or snapping changed");
    sync();
    Snapshot stale(doc);
    Check(request("schematic.add-comment", comment).status == EDITOR_STALE, "stale creation revision was accepted");
    stale.Unchanged("stale creation changed history");
    auto local = create();
    local.push_back(Number("type", Type(CKPGUID_INT)));
    local.push_back({"name", EDITOR_TEXT_VALUE, {}, "Editable local"});
    saved();
    const auto added = request("schematic.add-local", local);
    Check(added.status == EDITOR_OK, "local parameter command failed");
    const auto parameter = Field(added, "created").object;
    auto *p = CKParameter::Cast(doc.Resolve(parameter));
    Check(p, "created local has no stable identity");
    int old = 7;
    p->SetValue(&old, sizeof(old));
    saved();
    const auto editor = [&](ObjectRef object = {}, uint64_t mode = 0) {
        const auto r = EditorClient(app.Host(), activeView)
                           .Query("schematic.parameter-editor", {ObjectArgument("target", doc.Reference(root.created)),
                                                                 ObjectArgument("object", object ? object : parameter),
                                                                 IntegerArgument("mode", mode)});
        Check(r.status == EDITOR_OK, "parameter editor query failed");
        return r;
    };
    const auto initial = editor();
    Snapshot inactive(doc);
    Check(Field(Field(initial, "rows").fields.front().fields, "target").object == parameter,
          "inactive editor created a working parameter");
    inactive.Unchanged("inactive parameter query changed the document");
    EditorEdit edit = 0;
    const auto begin = [&](EditorResponse state) {
        const auto &fields = Field(state, "arguments").fields;
        const auto values = BorrowEditorFields(fields);
        Check(EditorClient(app.Host(), activeView)
                      .BeginCommand("schematic.edit-parameters", {values.data(), values.data() + values.size()},
                                    edit) == EDITOR_OK,
              "begin parameter command");
        return Field(state, "arguments").fields.back();
    };
    const auto update = [&](const EditorField &value) {
        const std::vector<EditorField> fields{value};
        const auto values = BorrowEditorFields(fields);
        return app.UpdateEdit(edit, *values.data());
    };
    const auto commit = [&] {
        EditorOperation operation = 0;
        Check(app.FinishEdit(edit, true, &operation) == EDITOR_QUEUED, "queue parameter command");
        edit = 0;
        app.EndFrame();
        sync();
        return global.Completion(operation);
    };
    auto value = begin(initial);
    Field(value.fields, "name").text = "Changed local";
    Field(Field(value.fields, "rows").fields, "0").integer = 42;
    Snapshot before(doc);
    Check(update(value) == EDITOR_OK && update(value) == EDITOR_OK, "update parameter working copy");
    const auto active = editor();
    const auto work = Field(Field(active, "rows").fields.front().fields, "target").object;
    Check(work != parameter && doc.Resolve(work, CKCID_PARAMETER, Document::ObjectScope::Existing) &&
              ParameterText(p) == "7",
          "preview wrote the original or did not own a work copy");
    before.Unchanged("parameter preview changed document or history");
    Check(commit().status == EDITOR_OK && doc.History().UndoCount() == 1 && ParameterText(p) == "42" &&
              std::string(p->GetName()) == "Changed local",
          "parameter name and value did not share one undo step");
    Check(!doc.Resolve(work, CKCID_PARAMETER, Document::ObjectScope::Existing), "committed parameter work copy leaked");
    Check(doc.History().Undo() && !doc.Dirty() && ParameterText(p) == "7" &&
              std::string(p->GetName()) == "Editable local",
          "parameter undo lost value, name or save point");
    sync();
    Snapshot cancelled(doc);
    value = begin(editor());
    Field(Field(value.fields, "rows").fields, "0").integer = 99;
    Check(update(value) == EDITOR_OK, "cancel preview");
    const auto cancelWork = Field(Field(editor(), "rows").fields.front().fields, "target").object;
    Check(app.FinishEdit(edit, false) == EDITOR_CANCELLED, "cancel parameter dialog");
    edit = 0;
    cancelled.Unchanged("cancelled parameter dialog changed history or Redo");
    Check(!doc.Resolve(cancelWork, CKCID_PARAMETER, Document::ObjectScope::Existing), "cancelled work copy leaked");
    value = begin(editor());
    Field(value.fields, "type").integer = Type(CKPGUID_STRING);
    Field(value.fields, "rows").fields.clear();
    Check(update(value) == EDITOR_OK && p->GetGUID() == CKPGUID_INT, "type preview changed the original");
    value = Field(editor(), "arguments").fields.back();
    Field(Field(value.fields, "rows").fields, "0").text = "Typed text";
    Check(update(value) == EDITOR_OK && commit().status == EDITOR_OK && p->GetGUID() == CKPGUID_STRING &&
              ParameterText(p) == "Typed text" && doc.History().UndoCount() == 1,
          "type and typed value were not applied together");
    Check(doc.History().Undo() && p->GetGUID() == CKPGUID_INT && ParameterText(p) == "7" && !doc.Dirty(),
          "type undo did not restore original bytes");
    sync();

    // A caller cannot make a saved value refer to a temporary work parameter.
    const auto objectLocal =
        AddLocalParameter(doc, root.created, root.created, "Reference value", CKPGUID_OBJECT, Point(40, 360));
    Check(objectLocal.ok, "create object-valued local");
    saved();
    const auto objectRef = doc.Reference(objectLocal.created);
    value = begin(editor(objectRef));
    Check(update(value) == EDITOR_OK, "prepare object working copy");
    const auto selfWork = Field(Field(editor(objectRef), "rows").fields.front().fields, "target").object;
    Field(Field(value.fields, "rows").fields, "0").object = selfWork;
    Snapshot selfBefore(doc);
    const auto selfStatus = update(value);
    Check(selfStatus != EDITOR_OK && selfStatus != EDITOR_NO_CHANGE, "parameter retained its own work object");
    if (app.HasEdits())
        Check(app.FinishEdit(edit, false) == EDITOR_CANCELLED, "cancel rejected work reference");
    edit = 0;
    selfBefore.Unchanged("self reference changed history");
    Check(!doc.Resolve(selfWork, CKCID_PARAMETER, Document::ObjectScope::Existing),
          "refused self-reference leaked work");

    const CKGUID nestedType(0x6e736368, 0x00000501);
    XArray<CKGUID> memberTypes;
    for (auto type : {CKPGUID_INT, CKPGUID_COLOR, CKPGUID_OBJECTARRAY})
        memberTypes.PushBack(type);
    Check(context->GetParameterManager()->RegisterNewStructure(nestedType, "Schematic nested values",
                                                               "Count,Color,Objects", memberTypes) == CK_OK,
          "register Schematic parameter structure");
    const auto structured =
        AddLocalParameter(doc, root.created, root.created, "Nested parameter", nestedType, Point(40, 420));
    Check(structured.ok, "create nested local");
    saved();
    const auto structuredRef = doc.Reference(structured.created);
    auto *structuredParameter = CKParameter::Cast(doc.Resolve(structuredRef));
    ParameterValueDescription originalStructure;
    Check(DescribeParameterValue(doc, structuredParameter, originalStructure), "describe original structure");
    auto changedStructure = originalStructure.value;
    changedStructure.members[0].integer = 17;
    changedStructure.members[1].components = {.25, .5, .75, 1};
    changedStructure.members[2].objects = {doc.Reference(root.created)};
    value = begin(editor(structuredRef));
    auto candidate = ParameterValueField(changedStructure, "0");
    Field(Field(value.fields, "rows").fields, "0") = candidate;
    Snapshot nestedBefore(doc);
    Check(update(value) == EDITOR_OK, "nested parameter preview");
    nestedBefore.Unchanged("nested preview changed the document");
    ParameterValueDescription unchangedStructure;
    Check(DescribeParameterValue(doc, structuredParameter, unchangedStructure) &&
              unchangedStructure.value == originalStructure.value,
          "nested preview wrote original members");
    Check(commit().status == EDITOR_OK && doc.History().UndoCount() == 1,
          "nested parameter did not share one undo step");
    ParameterValueDescription appliedStructure;
    Check(DescribeParameterValue(doc, structuredParameter, appliedStructure) &&
              appliedStructure.value == changedStructure,
          "nested parameter lost a component or object list");
    Check(doc.History().Undo() && !doc.Dirty() && DescribeParameterValue(doc, structuredParameter, appliedStructure) &&
              appliedStructure.value == originalStructure.value,
          "nested undo failed");
    sync();
    // Input values of a graph are gathered read-only and committed together.
    const auto inputGraph = CreateBehaviorGraph(doc, root.created, root.created, "Parameter inputs", Point(500, 500));
    Check(inputGraph.ok, "create input graph");
    for (const auto *name : {"First", "Second"}) {
        auto fields = create(inputGraph.created);
        fields.pop_back();
        fields.push_back(Number("kind", uint64_t(PinKind::ParamIn)));
        fields.push_back({"name", EDITOR_TEXT_VALUE, {}, name});
        fields.push_back(Number("type", Type(CKPGUID_INT)));
        Check(request("schematic.add-pin", fields).status == EDITOR_OK, "parameter pin command failed");
    }
    auto *graph = CKBehavior::Cast(doc.Resolve(doc.Reference(inputGraph.created)));
    Check(graph && graph->GetInputParameterCount() == 2, "input pin creation lost a pin");
    for (int i = 0; i < 2; ++i) {
        auto *input = graph->GetInputParameter(i);
        input->Enable();
        if (!EditableValueOf(input)) {
            auto *local = graph->CreateLocalParameter((CKSTRING) "Input value", CKPGUID_INT);
            Check(local && input->SetDirectSource(local) == CK_OK, "create editable input value");
        }
    }
    saved();
    const auto inputs = editor(doc.Reference(inputGraph.created), 1);
    Check(Field(inputs, "rows").fields.size() == 2 && EditorClient::Integer(inputs, "apply"),
          "graph inputs projection lost an editable input");
    value = begin(inputs);
    Field(Field(value.fields, "rows").fields, "0").integer = 21;
    Field(Field(value.fields, "rows").fields, "1").integer = 34;
    Check(update(value) == EDITOR_OK && commit().status == EDITOR_OK && doc.History().UndoCount() == 1,
          "input batch was not one undo step");
    Check(ParameterText(EditableValueOf(graph->GetInputParameter(0))) == "21" &&
              ParameterText(EditableValueOf(graph->GetInputParameter(1))) == "34",
          "input batch missed a value");
    Check(doc.History().Undo() && !doc.Dirty(), "input batch undo lost save point");
    sync();
    // Display requests which already match must preserve the existing Redo.
    const auto &locals = doc.Session(root.created)->Data().rootBehavior.localParams;
    int localIndex = -1;
    for (size_t i = 0; i < locals.size(); ++i)
        if (locals[i].id == parameter.object)
            localIndex = int(i);
    Check(localIndex >= 0, "editable local display disappeared");
    EditorField elements{"elements", EDITOR_RECORD};
    elements.fields = {
        SchematicPosition(ElementAddress::Capture(
                              doc, root.created, {ElementKind::LocalParam, root.created, parameter.object, localIndex}),
                          "0")};
    auto displayRequest = create();
    displayRequest.erase(displayRequest.begin() + 4);
    displayRequest.erase(displayRequest.begin() + 1);
    displayRequest.push_back(elements);
    displayRequest.push_back(Number("mode", 0));
    displayRequest.push_back(Number("style", uint64_t(locals[size_t(localIndex)].style)));
    Snapshot sameStyle(doc);
    Check(request("schematic.set-display", displayRequest).status == EDITOR_NO_CHANGE,
          "unchanged display was recorded");
    sameStyle.Unchanged("unchanged display cleared Redo");
    value = begin(editor());
    Check(update(value) == EDITOR_OK, "prepare closed-view work copy");
    const auto closedWork = Field(Field(editor(), "rows").fields.front().fields, "target").object;
    app.CloseScope(view);
    edit = 0;
    Check(!app.HasEdits() && !doc.Resolve(closedWork, CKCID_PARAMETER, Document::ObjectScope::Existing),
          "closing a Schematic view leaked parameter work");
    // The external module uses the same preview, commit and release entry points.
    auto external = native;
    for (auto &choice : external.providers)
        if (choice.kind == "command" && choice.id == "schematic.edit-parameters")
            choice.provider = "example.gestures";
    Check(app.Configure(external, error), error.c_str());
    const auto newView = app.OpenScope();
    activeView = newView;
    const EditorClient outside(app.Host(), newView);
    for (uint64_t failure = 0; failure <= 4; ++failure) {
        saved();
        const auto projection = outside.Query("schematic.parameter-editor",
                                              {ObjectArgument("target", doc.Reference(root.created)),
                                               ObjectArgument("object", parameter), IntegerArgument("mode", 0)});
        Check(projection.status == EDITOR_OK, "external parameter projection");
        auto fields = Field(projection, "arguments").fields;
        auto &candidate = Field(fields, "value");
        Field(Field(candidate.fields, "rows").fields, "0").integer = 20 + failure;
        Check(outside.Command("example.gesture-failure", {IntegerArgument("mode", failure)}).status == EDITOR_OK,
              "set external failure mode");
        const auto borrowed = BorrowEditorFields(fields);
        Snapshot failureBefore(doc);
        Check(outside.BeginCommand("schematic.edit-parameters", {borrowed.data(), borrowed.data() + borrowed.size()},
                                   edit) == EDITOR_OK,
              "external parameter begin");
        const auto updated = update(candidate);
        if (failure == 1) {
            Check(updated == EDITOR_ERROR && !app.HasEdits(), "preview exception left a parameter edit open");
            edit = 0;
        } else {
            Check(updated == EDITOR_OK, "external parameter preview");
            const auto result = commit();
            Check(result.status == (failure == 0   ? EDITOR_OK
                                    : failure == 2 ? EDITOR_REFUSED
                                                   : EDITOR_ERROR),
                  "external parameter failure result");
        }
        if (!failure) {
            Check(ParameterText(p) == "20" && doc.History().UndoCount() == 1 && doc.History().Undo(),
                  "external parameter did not use one undo step");
            sync();
        } else {
            failureBefore.Unchanged("external failure changed the parameter history");
            Check(ParameterText(p) == "7", "external failure kept a changed parameter");
        }
        Check(!app.HasEdits(), "external parameter edit or work remained open");
    }
    Check(app.Configure(native, error), error.c_str());
    // Standard creation accepts both one prototype and a multi-selection drop.
    BuildingBlockCatalog catalog;
    catalog.Refresh();
    Check(!catalog.Entries().empty(), "Building Block catalog unavailable");
    CKGUID prototype;
    for (const auto &entry : catalog.Entries())
        if (entry.category.rfind("Logics", 0) == 0) {
            prototype = entry.guid;
            break;
        }
    Check(prototype != CKGUID(), "no Logics prototype");
    auto insertion = create();
    insertion.push_back(Number("prototype", Type(prototype)));
    saved();
    const auto inserted = request("schematic.insert-block", insertion);
    Check(inserted.status == EDITOR_OK && doc.History().UndoCount() == 1, "single Building Block command failed");
    Check(doc.History().Undo(), "undo Building Block");
    sync();
    saved();
    const auto blocksBefore = CKBehavior::Cast(context->GetObject(root.created))->GetSubBehaviorCount();
    insertion = create();
    EditorField prototypes{"prototypes", EDITOR_INTEGER_LIST};
    prototypes.integers = {Type(prototype), Type(prototype)};
    insertion.push_back(prototypes);
    Check(request("schematic.insert-block", insertion).status == EDITOR_OK && doc.History().UndoCount() == 1 &&
              CKBehavior::Cast(context->GetObject(root.created))->GetSubBehaviorCount() == blocksBefore + 2,
          "multi-Building Block drop was not one undo step");
    Check(doc.History().Undo() && !doc.Dirty(), "multi-Building Block undo lost save point");
    sync();
    auto invalid = create();
    invalid.back().reals[0] = std::numeric_limits<double>::infinity();
    Snapshot invalidBefore(doc);
    Check(request("schematic.add-comment", invalid).status == EDITOR_INVALID, "non-finite creation accepted");
    invalidBefore.Unchanged("invalid creation changed history");
    // Graph replacement captures whole-document revision and stable target identities.
    const auto target = CreateBehaviorGraph(doc, root.created, root.created, "Target", Point(200, 200));
    const auto source = CreateBehaviorGraph(doc, other.created, other.created, "Source", Point(400, 200));
    Check(target.ok && source.ok &&
              CreateBehaviorGraph(doc, other.created, source.created, "Child", Point(400, 260)).ok,
          "create replacement graphs");
    saved();
    const EditorClient current(app.Host(), newView);
    const auto replacements =
        current.Query("schematic.replacements", {ObjectArgument("target", doc.Reference(target.created))});
    Check(replacements.status == EDITOR_OK && !Field(replacements, "graphs").fields.empty(),
          "graph replacement projection");
    EditorField targets{"objects", EDITOR_OBJECT_LIST};
    targets.objects = {doc.Reference(target.created)};
    const std::vector<EditorField> replace{Number("revision", EditorClient::Integer(replacements, "revision")),
                                           targets,
                                           {"source", EDITOR_OBJECT, doc.Reference(source.created)}};
    const auto values = BorrowEditorFields(replace);
    Check(current.Command("schematic.replace-graphs", {values.data(), values.data() + values.size()}).status ==
                  EDITOR_OK &&
              doc.History().UndoCount() == 1 && doc.IsDiscarded(target.created),
          "graph replacement failed");
    Check(doc.History().Undo() && !doc.Dirty() && !doc.IsDiscarded(target.created), "graph replacement undo failed");
    sync();
    Snapshot replaceStale(doc);
    Check(current.Command("schematic.replace-graphs", {values.data(), values.data() + values.size()}).status ==
              EDITOR_STALE,
          "old replacement snapshot survived undo");
    replaceStale.Unchanged("stale replacement changed history");

    // Real Building Block settings still go through the original callback and
    // topology restoration, including no-change Apply and Undo.
    bool checkedSettings = false;
    for (const auto &entry : catalog.Entries()) {
        if (entry.category.rfind("Logics", 0) != 0)
            continue;
        auto *declaration = CKGetPrototypeFromGuid(entry.guid);
        if (!declaration || !declaration->GetLocalParameterCount())
            continue;
        const auto made = InsertBuildingBlock(doc, root.created, root.created, entry.guid, Point(800, 400));
        if (!made.ok)
            continue;
        const auto state = editor(doc.Reference(made.created), 2);
        auto settingsValue = Field(state, "arguments").fields.back();
        auto &rows = Field(settingsValue.fields, "rows").fields;
        size_t chosen = rows.size();
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].type == EDITOR_INTEGER_VALUE) {
                chosen = i;
                break;
            }
        if (chosen == rows.size())
            continue;
        saved();
        value = begin(editor(doc.Reference(made.created), 2));
        auto &setting = Field(value.fields, "rows").fields[chosen];
        setting.integer = setting.integer ? 0 : 1;
        Check(update(value) == EDITOR_OK && commit().status == EDITOR_OK && doc.History().UndoCount() == 1,
              "settings batch bypassed one-step undo");
        Check(doc.History().Undo() && !doc.Dirty(), "settings callback undo lost save point");
        sync();
        Snapshot unchangedSettings(doc);
        value = begin(editor(doc.Reference(made.created), 2));
        Check(update(value) == EDITOR_OK &&
                  (commit().status == EDITOR_NO_CHANGE || doc.History().UndoCount() == unchangedSettings.undo),
              "unchanged settings created an edit");
        unchangedSettings.Unchanged("unchanged settings cleared Redo");
        checkedSettings = true;
        break;
    }
    Check(checkedSettings, "no editable Building Block settings tested");
    // Operation catalogs and creation use the same signature matching as links.
    const CKGUID integerType = CKPGUID_INT;
    std::vector<OperationSignature> signatures =
        AvailableOperationSignatures(context, nullptr, nullptr, &integerType, &integerType);
    Check(!signatures.empty(), "parameter operation catalog unavailable");
    const auto signature = signatures.front();
    EditorField parts{"parts", EDITOR_INTEGER_LIST};
    parts.integers = {Type(signature.input1), Type(signature.input2), Type(signature.result),
                      Type(signature.operation)};
    EditorField locks{"locks", EDITOR_INTEGER_LIST};
    locks.integers = {1, 0, 1, 0};
    saved();
    Snapshot operationRead(doc);
    const auto choices = request("schematic.operation-choices", {parts, locks, Number("easy", 1)}, true);
    Check(choices.status == EDITOR_OK && EditorClient::Integer(choices, "valid") &&
              Field(choices, "names").texts.size() == 4,
          "operation choices lost names or valid signature");
    operationRead.Unchanged("operation choice query changed history");
    auto operationArgs = create();
    operationArgs.push_back(parts);
    Check(request("schematic.edit-operation", operationArgs).status == EDITOR_OK && doc.History().UndoCount() == 1,
          "operation creation did not use one undo step");
    Check(doc.History().Undo() && !doc.Dirty(), "operation creation undo failed");
    sync();
    // Plugin values without a portable description retain their native text
    // conversion and copier. Only the working copy may change before commit.
    auto *manager = context->GetParameterManager();
    CKParameterTypeDesc nativeType;
    nativeType.Guid = CKGUID(0x6e6d7363, 0x00000501);
    nativeType.TypeName = "Native text regression";
    nativeType.DefaultSize = sizeof(int);
    nativeType.StringFunction = NativeText;
    Check(manager->RegisterParameterType(&nativeType) == CK_OK, "register native text type");
    auto *nativeValue = context->CreateCKParameterLocal("Native text", nativeType.Guid);
    const int originalNative = 17;
    Check(nativeValue && nativeValue->SetValue(&originalNative, sizeof(originalNative)) == CK_OK,
          "create native text value");
    const auto nativeRef = doc.Reference(nativeValue->GetID());
    saved();
    Snapshot nativeRead(doc);
    const auto nativeProjection = editor(nativeRef);
    const auto &nativeRow = Field(nativeProjection, "rows").fields.front();
    Check(Field(nativeRow.fields, "editor").text == "text.single-line", "native text editor is unavailable");
    nativeRead.Unchanged("native text query changed the document");
    value = begin(nativeProjection);
    Field(Field(value.fields, "rows").fields, "0").text = "#93";
    Check(update(value) == EDITOR_OK && ParameterText(nativeValue) == "#17", "native text preview wrote the original");
    nativeRead.Unchanged("native text preview changed history");
    Check(commit().status == EDITOR_OK && ParameterText(nativeValue) == "#93" && doc.History().UndoCount() == 1,
          "native text did not commit in one undo step");
    Check(doc.History().Undo() && ParameterText(nativeValue) == "#17" && !doc.Dirty(), "native text undo failed");
    sync();
    Snapshot nativeCancel(doc);
    value = begin(editor(nativeRef));
    Field(Field(value.fields, "rows").fields, "0").text = "#28";
    Check(update(value) == EDITOR_OK && app.FinishEdit(edit, false) == EDITOR_CANCELLED, "cancel native text");
    edit = 0;
    nativeCancel.Unchanged("native text cancellation cleared Redo");
    value = begin(editor(nativeRef));
    Check(update(value) == EDITOR_OK, "prepare failing native text conversion");
    const auto nativeWork = Field(Field(editor(nativeRef), "rows").fields.front().fields, "target").object;
    Field(Field(value.fields, "rows").fields, "0").text = "throw";
    Check(update(value) == EDITOR_ERROR, "native text conversion exception was not reported");
    if (app.HasEdits())
        app.FinishEdit(edit, false);
    edit = 0;
    nativeCancel.Unchanged("native text conversion failure changed history");
    Check(ParameterText(nativeValue) == "#17" &&
              !doc.Resolve(nativeWork, CKCID_PARAMETER, Document::ObjectScope::Existing),
          "native text conversion failure modified the original or leaked its copy");
    doc.History().Clear();
    context->DestroyObject(nativeValue);
    manager->UnRegisterParameterType(nativeType.Guid);
    app.CloseScope(newView);
}
