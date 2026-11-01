#include "core/schematic/SchematicModelBuilder.h"
#include "features/schematic/SchematicGraphCommands.h"
#include "features/schematic/SchematicModelData.h"
#include "features/schematic/SchematicPosition.h"
#include "workspace/EditorClient.h"
#include "core/document/Document.h"
#include "core/document/Clipboard.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/TopologyCommands.h"
#include "core/schematic/SchematicEdits.h"
#include "core/schematic/SchematicNavigation.h"
#include "core/schematic/SchematicSearch.h"
#include "core/graph/RuntimeGraph.h"
#include "core/runtime/CkRuntimeHost.h"
#include "editor/ImGuiDrawing.h"
#include <stdexcept>
#include <thread>
#include "core/graph/ValueCommands.h"
#include "core/runtime/TextEncoding.h"
namespace {
using namespace vdev;
void Check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
const EditorField &Field(const EditorResponse &r, const char *key) {
    auto *f = EditorClient::Find(r, key);
    Check(f, key);
    return *f;
}

void RetainedQueries(CkRuntimeHost &runtime) {
    Document document(runtime);
    std::string error;
    Check(document.New(&error), error);
    struct Probe {
        Document *document;
        unsigned calls = 0;
    } probe{&document}, alternate{&document};
    EditorApplication app([&] { return &document; });
    const EditorParameter object{EDITOR_TEXT("object"), EDITOR_OBJECT},
        valid{EDITOR_TEXT("valid"), EDITOR_INTEGER_VALUE};
    const auto read = [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &p = *static_cast<Probe *>(data);
        ++p.calls;
        const EditorValue result{EDITOR_TEXT("valid"),
                                 EDITOR_INTEGER_VALUE,
                                 {},
                                 {},
                                 uint64_t(p.document->Resolve(request->values[0].object) != nullptr)};
        return sink.write(sink.data, &result, 1);
    };
    EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("test.live-reference"), EDITOR_TEXT("test.references"), &object, 1, read,
         &probe, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, &valid, 1},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("test.retained-reference"), EDITOR_TEXT("test.references"), &object, 1,
         read, &probe, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, &valid, 1, nullptr, EDITOR_QUERY_RETAINED_REFERENCES}};
    EditorCommandDesc command{sizeof(EditorCommandDesc),
                              EDITOR_TEXT("test.reference-command"),
                              EDITOR_TEXT("test.references"),
                              EDITOR_TEXT("Reference Command"),
                              &object,
                              1,
                              read,
                              &probe,
                              EDITOR_DOCUMENT,
                              EDITOR_SESSION_CHANGE};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("test.references");
    module.queries = queries;
    module.queryCount = 2;
    module.commands = &command;
    module.commandCount = 1;
    Check(app.Register(module, error), error);
    auto incompatible = queries[0];
    incompatible.provider = EditorString EDITOR_TEXT("test.other");
    incompatible.flags = EDITOR_QUERY_RETAINED_REFERENCES;
    module.id = EditorString EDITOR_TEXT("test.incompatible-reference-policy");
    module.queries = &incompatible;
    module.queryCount = 1;
    module.commandCount = 0;
    Check(!app.Register(module, error), "query providers changed reference checking");
    incompatible.id = EditorString EDITOR_TEXT("test.unknown-flags");
    incompatible.flags = 2;
    Check(!app.Register(module, error), "unknown query flags accepted");
    auto other = queries[1];
    other.provider = EditorString EDITOR_TEXT("test.other");
    other.data = &alternate;
    module.id = EditorString EDITOR_TEXT("test.reference-override");
    module.queries = &other;
    Check(app.Register(module, error), error);
    EditorConfiguration configuration;
    configuration.providers = {{"query", "test.retained-reference", "test.references"}};
    Check(app.Activate(EDITOR_IMGUI_VERSION, error, configuration), error);
    app.Attach(document.Token(), document.TopologyRevision(), true, true);
    const auto view = app.OpenScope();
    const EditorClient client(app.Host(), view);
    auto *original = document.Context()->CreateObject(CKCID_OBJECT, "Original reference");
    Check(original, "reference fixture");
    const auto ref = document.Reference(original->GetID());
    Check(client.Query("test.live-reference", {ObjectArgument("object", ref)}).status == EDITOR_OK,
          "live query refused an object");
    Check(document.Context()->DestroyObject(ref.object, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID) == CK_OK,
          "delete reference fixture");
    auto *replacement = document.Context()->CreateObject(CKCID_OBJECT, "Reused reference");
    Check(replacement && replacement->GetID() == ref.object, "SDK did not reuse fixture ID");
    const auto calls = probe.calls;
    Check(client.Query("test.live-reference", {ObjectArgument("object", ref)}).status == EDITOR_STALE &&
              probe.calls == calls,
          "ordinary query read a reused object");
    Check(client.Command("test.reference-command", {ObjectArgument("object", ref)}).status == EDITOR_STALE &&
              probe.calls == calls,
          "command accepted a stale object");
    const auto result = client.Query("test.retained-reference", {ObjectArgument("object", ref)});
    Check(result.status == EDITOR_OK && !EditorClient::Integer(result, "valid") && probe.calls == calls + 1,
          "retained query could not validate an old identity");
    auto foreign = ref;
    foreign.document.value += 77;
    Check(client.Query("test.retained-reference", {ObjectArgument("object", foreign)}).status == EDITOR_OK &&
              client.Query("test.live-reference", {ObjectArgument("object", foreign)}).status == EDITOR_STALE,
          "reference checking lost document ownership");
    const EditorValue argument = ObjectArgument("object", ref);
    const EditorRequest request{app.Scope(view), &argument, 1, EDITOR_TEXT("test.other")};
    Check(app.Query("test.retained-reference", request).status == EDITOR_OK && alternate.calls == 1,
          "explicit retained query provider was not selected");
    const auto before = probe.calls;
    EditorStatus threadStatus = EDITOR_OK;
    std::thread thread(
        [&] { threadStatus = client.Query("test.retained-reference", {ObjectArgument("object", ref)}).status; });
    thread.join();
    Check(threadStatus == EDITOR_REFUSED && probe.calls == before, "retained query bypassed its thread");
    app.NativeAvailable(false);
    Check(client.Query("test.retained-reference", {ObjectArgument("object", ref)}).status == EDITOR_REFUSED &&
              probe.calls == before,
          "retained query bypassed native work");
    app.NativeAvailable(true);
    app.CloseScope(view);
    Check(client.Query("test.retained-reference", {ObjectArgument("object", ref)}).status == EDITOR_STALE,
          "retained query used a closed view");
}
} // namespace
void TestSchematicProjections(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    RetainedQueries(runtime);
    Document document(runtime);
    std::string error;
    Check(document.New(&error), error);
    auto *context = document.Context();
    const auto script = CreateScript(document, context->GetCurrentLevel()->GetID(), "Projection");
    Check(script.ok, "create projection Script");
    const auto graph = CreateBehaviorGraph(document, script.created, script.created, "Graph", Point(200, 120));
    Check(graph.ok, "create projection graph");
    Check(EditScriptData(document, script.created, "Comments",
                         [](InterfaceData &d) {
                             for (int i = 0; i < 140; ++i) {
                                 Comment c;
                                 c.text = "Search " + std::to_string(i);
                                 c.hPos = float(i * 10);
                                 c.vPos = 240;
                                 d.rootBehavior.comments.push_back(c);
                             }
                         }),
          "create paged model");
    auto *targetGraph = CKBehavior::Cast(context->GetObject(graph.created));
    targetGraph->SetAsTargetable(TRUE);
    Check(targetGraph->UseTarget(TRUE) == CK_OK, "create Target pin fixture");
    document.History().Clear();
    document.MarkSaved();
    document.History().MarkSavePoint();
    EditorApplication app([&] { return &document; });
    Clipboard clipboard;
    SchematicGraphCommands queries([&] { return &document; }, [] { return true; }, clipboard);
    Check(queries.Register(app, error) && app.Activate(EDITOR_IMGUI_VERSION, error), error);
    app.Attach(document.Token(), document.TopologyRevision(), true, true);
    auto view = app.OpenScope();
    const EditorClient client(app.Host(), view);
    const auto target = document.Reference(script.created);
    Check(client.Query("schematic.model", {ObjectArgument("target", target), IntegerArgument("open", 2)}).status ==
              EDITOR_INVALID &&
          client.Query("schematic.snapshot", {ObjectArgument("target", target), IntegerArgument("open", 2)}).status ==
              EDITOR_INVALID,
          "model and snapshot disagreed on invalid open state");
    Check(client.Query("schematic.model", {ObjectArgument("target", document.Reference(context->GetCurrentLevel()->GetID())),
                                            IntegerArgument("open", 1)}).status == EDITOR_STALE,
          "model query accepted a non-Script object");
    EditorField stackState{"state", EDITOR_RECORD};
    stackState.fields = {{"target", EDITOR_OBJECT, target}, {"options", EDITOR_INTEGER_VALUE, {}, {}, 5}};
    const std::vector<EditorField> stateFields{stackState};
    const auto stackArguments = BorrowEditorFields(stateFields);
    const auto stack =
        client.Query("schematic.stack", {stackArguments.data(), stackArguments.data() + stackArguments.size()});
    Check(stack.status == EDITOR_OK, "stack query status " + std::to_string(stack.status) + ": " + stack.message);
    Check(Field(stack, "scripts").objects.size() == 1 && Field(stack, "scripts").objects[0] == target,
          "stack lost Script");
    const auto *session = document.Session(script.created);
    EditorField model;
    uint64_t first = 0, total = 0;
    unsigned pages = 0;
    do {
        const auto result = client.Query(
            "schematic.model",
            {ObjectArgument("target", target), IntegerArgument("open", 1), IntegerArgument("first", first),
             IntegerArgument("revision", session->Revision()), IntegerArgument("topology", document.TopologyRevision()),
             IntegerArgument("view-revision", session->ViewRevision()), IntegerArgument("refresh", 1)});
        Check(result.status == EDITOR_OK,
              "model query status " + std::to_string(result.status) + ": " + result.message);
        const auto &page = Field(result, "model");
        if (!first)
            model = page;
        else {
            EditorField *items = nullptr;
            const EditorField *extra = nullptr;
            for (auto &f : model.fields)
                if (f.key == "items")
                    items = &f;
            for (const auto &f : page.fields)
                if (f.key == "items")
                    extra = &f;
            Check(items && extra, "model item page missing");
            items->fields.insert(items->fields.end(), extra->fields.begin(), extra->fields.end());
        }
        total = EditorClient::Integer(result, "total");
        const auto next = EditorClient::Integer(result, "next");
        Check(next > first || !total, "model paging did not advance");
        first = next;
        ++pages;
    } while (first < total);
    SchematicModel decoded;
    Check(ReadSchematicModel(model, decoded), "cannot decode Schematic model");
    Check(pages >= 2 && decoded.comments.size() == 140 && decoded.FindBehavior(graph.created) &&
              decoded.IsOpenGraph(script.created),
          "decoded model differs from native model");
    SchematicModel original;
    original = BuildSchematicModel(*session, context, {}, true);
    Check(SchematicModelFields(original, 0, 100000) == SchematicModelFields(decoded, 0, 100000),
          "native and copied model fields differ");
    Check(!document.Dirty() && !document.History().UndoCount(), "model queries changed the document");
    const auto canonical = SchematicModelFields(decoded, 0, 100000);
    auto malformed = model;
    malformed.fields.erase(malformed.fields.begin());
    Check(!ReadSchematicModel(malformed, decoded) && SchematicModelFields(decoded, 0, 100000) == canonical,
          "failed projection decode changed the displayed model");
    const ElementRef targetPin{ElementKind::Pin, graph.created, graph.created, -2, -1, PinKind::Target};
    const auto targetPosition = ElementAddress::Capture(document, script.created, targetPin);
    ElementAddress roundtrip;
    Check(targetPosition && ReadSchematicPosition(SchematicPosition(targetPosition), roundtrip) &&
              roundtrip.element == targetPin,
          "Target position lost its -2 slot");
    const std::vector<EditorField> targetNavigation{{"target", EDITOR_OBJECT, target},
                                                    SchematicPosition(targetPosition, "position")};
    const auto targetArgs = BorrowEditorFields(targetNavigation);
    Check(client.Query("schematic.navigation-target", {targetArgs.data(), targetArgs.data() + targetArgs.size()})
                  .status == EDITOR_OK,
          "Target pin navigation failed");
    Check(targetGraph->UseTarget(FALSE) == CK_OK && targetGraph->UseTarget(TRUE) == CK_OK,
          "replace Target pin fixture");
    EditorField oldPin{"elements", EDITOR_RECORD};
    oldPin.fields = {SchematicPosition(targetPosition, "0")};
    const std::vector<EditorField> oldPinFields{oldPin};
    const auto oldPinArgs = BorrowEditorFields(oldPinFields);
    const auto oldPinResult =
        client.Query("schematic.validate-positions", {oldPinArgs.data(), oldPinArgs.data() + oldPinArgs.size()});
    Check(oldPinResult.status == EDITOR_OK && Field(oldPinResult, "elements").fields.empty(),
          "Target selection followed a replacement endpoint");
    unsigned sourceReads = 0, destinationReads = 0;
    Selection selection;
    selection.Bind([&](uint32_t id, const ElementRef &ref) {
        ++sourceReads;
        return ElementAddress::Capture(document, id, ref);
    });
    selection.Set(script.created, Selection::HeaderOf(script.created));
    const auto readCount = sourceReads;
    Selection saved = selection;
    saved.Rebase();
    Check(sourceReads == readCount && selection.Addresses().size() == 1, "copied selection retained its source reader");
    Selection destination;
    destination.Bind([&](uint32_t id, const ElementRef &ref) {
        ++destinationReads;
        return ElementAddress::Capture(document, id, ref);
    });
    destination = selection;
    destination.Rebase();
    Check(destinationReads == 1 && sourceReads == readCount,
          "selection assignment replaced the receiving view's reader");
    auto refs = std::vector<EditorField>{
        SchematicPosition(ElementAddress::Capture(document, script.created, Selection::HeaderOf(script.created)), "1")};
    ElementAddress firstComment =
        ElementAddress::Capture(document, script.created, {ElementKind::Comment, script.created, 0, 0});
    const auto find = [&](uint64_t options, const std::vector<ObjectRef> &hidden, const std::vector<ObjectRef> &closed,
                          const ElementAddress &previous = ElementAddress{}) {
        EditorField cursor{"previous", EDITOR_RECORD};
        if (previous)
            cursor = SchematicPosition(previous, "previous");
        const std::vector<EditorField> owned{
            {"text", EDITOR_TEXT_VALUE, {}, "Search"},          {"order", EDITOR_OBJECT_LIST, {}, {}, 0, {target}},
            {"hidden", EDITOR_OBJECT_LIST, {}, {}, 0, hidden},  {"closed", EDITOR_OBJECT_LIST, {}, {}, 0, closed},
            {"options", EDITOR_INTEGER_VALUE, {}, {}, options}, cursor};
        const auto values = BorrowEditorFields(owned);
        return client.Query("schematic.find", {values.data(), values.data() + values.size()});
    };
    Check(EditorClient::Integer(find(0, {}, {}), "found"), "forward Find lost comments");
    const auto reverse = find(7, {}, {});
    ElementAddress last;
    Check(reverse.status == EDITOR_OK && ReadSchematicPosition(Field(reverse, "address"), last) &&
              last.element.index == 139,
          "backward Find did not start at the last result");
    Check(!EditorClient::Integer(find(0, {target}, {}), "found") &&
              EditorClient::Integer(find(2, {target}, {}), "found"),
          "Find ignored hidden Script settings");
    Check(!EditorClient::Integer(find(0, {}, {target}), "found") &&
              EditorClient::Integer(find(1, {}, {target}), "found"),
          "Find ignored closed Script settings");
    const auto search = [&](unsigned kinds, const std::string &text, unsigned filter = 0, ObjectRef value = ObjectRef{},
                            uint64_t first = 0) {
        return client.Query(
            "schematic.search",
            {TextArgument("text", text), IntegerArgument("id", 0), IntegerArgument("kinds", kinds),
             IntegerArgument("all", 1), ObjectsArgument("scripts", {}), IntegerArgument("filter", filter),
             IntegerArgument("type", (uint64_t(CKPGUID_INT.d1) << 32) | CKPGUID_INT.d2), IntegerArgument("derived", 0),
             ObjectArgument("value", value), IntegerArgument("first", first)});
    };
    const auto page1 = search(SearchComments, "Search"), page2 = search(SearchComments, "Search", 0, {}, 128);
    Check(page1.status == EDITOR_OK && EditorClient::Integer(page1, "total") == 140 &&
              Field(page1, "rows").fields.size() == 128 && Field(page2, "rows").fields.size() == 12,
          "Explorer search paging lost comments");
    Check(EditScriptData(document, script.created, "Add after search",
                         [](InterfaceData &d) { d.rootBehavior.comments.emplace_back(); }),
          "change indexed search results");
    EditorField positions{"elements", EDITOR_RECORD};
    positions.fields = {SchematicPosition(firstComment, "0"), refs[0]};
    const std::vector<EditorField> retainedPositions{positions};
    const auto borrowedPositions = BorrowEditorFields(retainedPositions);
    const auto validPositions =
        client.Query("schematic.validate-positions",
                     {borrowedPositions.data(), borrowedPositions.data() + borrowedPositions.size()});
    Check(validPositions.status == EDITOR_OK && Field(validPositions, "elements").fields.size() == 1,
          "retained query did not prune a stale table position");
    const auto history = document.History().UndoCount();
    Check(client.Command("schematic.delete-search-results",
                         {borrowedPositions.data(), borrowedPositions.data() + borrowedPositions.size()})
                      .status == EDITOR_STALE &&
              document.History().UndoCount() == history &&
              document.Session(script.created)->Data().rootBehavior.comments.size() == 141,
          "stale deletion changed part of the document");
    Check(document.History().Undo() && !document.Dirty(), "restore projection fixture");
    const auto restarted = find(3, {}, {}, firstComment);
    ElementAddress restartedAt;
    Check(restarted.status == EDITOR_OK && ReadSchematicPosition(Field(restarted, "address"), restartedAt) &&
              restartedAt.element.index == 0,
          "Find reused an old index after Undo");
    positions.fields = {SchematicPosition(restartedAt, "0")};
    const std::vector<EditorField> deletion{positions};
    const auto deletionArgs = BorrowEditorFields(deletion);
    Check(client.Command("schematic.delete-search-results",
                         {deletionArgs.data(), deletionArgs.data() + deletionArgs.size()})
                      .status == EDITOR_OK &&
              document.History().UndoCount() == 1 &&
              document.Session(script.created)->Data().rootBehavior.comments.size() == 139,
          "search deletion was not one undo step");
    Check(document.History().Undo() && !document.Dirty(), "search deletion Undo lost the save point");
    const auto local =
        AddLocalParameter(document, script.created, script.created, "Amount", CKPGUID_INT, Point(60, 80));
    Check(local.ok, "value search fixture");
    Check(EditScriptData(document, script.created, "Parameter display",
                         [&](InterfaceData &d) {
                             for (auto &p : d.rootBehavior.localParams)
                                 if (p.id == local.created)
                                     p.style = PARAM_STYLE_NAMEVALUE;
                         }),
          "parameter display fixture");
    const auto measure = [](const std::string &text) { return float(text.size() * 7); };
    const auto measured = BuildSchematicModel(*document.Session(script.created), context, measure, true);
    auto copiedModel = BuildSchematicModel(*document.Session(script.created), context, {}, true);
    copiedModel.MeasureText(measure);
    Check(SchematicModelFields(measured, 0, 100000) == SchematicModelFields(copiedModel, 0, 100000),
          "copied parameter text measurement differs from native builder");
    auto *amount = CKParameter::Cast(document.Resolve(document.Reference(local.created)));
    int seven = 7;
    amount->SetValue(&seven, sizeof(seven));
    document.History().Clear();
    document.MarkSaved();
    document.History().MarkSavePoint();
    auto *filter = context->CreateCKParameterOut("Filter", CKPGUID_INT, TRUE);
    Check(filter && filter->SetValue(&seven, sizeof(seven)) == CK_OK, "filter draft fixture");
    const auto filterRef = document.Reference(filter->GetID());
    const auto filtered = search(SearchLocalParameters, "Amount", 2, filterRef);
    Check(filtered.status == EDITOR_OK && Field(filtered, "rows").fields.size() == 1 && !document.Dirty() &&
              !document.History().UndoCount(),
          "value filter changed the document or missed a local");
    int different = 19;
    filter->SetValue(&different, sizeof(different));
    Check(Field(search(SearchLocalParameters, "Amount", 2, filterRef), "rows").fields.empty(),
          "filter query reused old draft contents");
    positions.fields.clear();
    for (const auto &row : Field(filtered, "rows").fields)
        for (const auto &field : row.fields)
            if (field.key == "address") {
                auto value = field;
                value.key = std::to_string(positions.fields.size());
                positions.fields.push_back(std::move(value));
            }
    const std::vector<EditorField> replaceValues{positions, {"value", EDITOR_OBJECT, filterRef}};
    const auto replaceArgs = BorrowEditorFields(replaceValues);
    Check(
        client.Command("schematic.replace-search-values", {replaceArgs.data(), replaceArgs.data() + replaceArgs.size()})
                    .status == EDITOR_OK &&
            ParameterText(amount) == "19" && document.History().UndoCount() == 1,
        "replace search values did not use one undo step");
    Check(document.History().Undo() && ParameterText(amount) == "7" && !document.Dirty(),
          "replace search values Undo failed");
    context->DestroyObject(filter);
    document.History().Clear();
    // A drawing projection owns pixels rather than an SDK bitmap handle.
    std::vector<unsigned char> imagePixels(257 * 257 * 4, 17);
    for (size_t i = 3; i < imagePixels.size(); i += 4)
        imagePixels[i] = 0;
    VxImageDescEx image;
    image.Width = image.Height = 257;
    image.BitsPerPixel = 32;
    image.BytesPerLine = 257 * 4;
    image.RedMask = 0x00ff0000;
    image.GreenMask = 0x0000ff00;
    image.BlueMask = 0x000000ff;
    image.AlphaMask = 0xff000000;
    image.Image = imagePixels.data();
    std::shared_ptr<void> bitmap(VxCreateBitmap(image), VxDeleteBitmap);
    Check(bool(bitmap), "snapshot fixture");
    Check(EditScriptData(document, script.created, "Snapshot",
                         [&](InterfaceData &d) { d.header.SetSnapshot(bitmap.get(), bitmap); }),
          "set model snapshot");
    document.History().Clear();
    document.MarkSaved();
    document.History().MarkSavePoint();
    auto picture = BuildSchematicModel(*document.Session(script.created), context, {}, true);
    Check(picture.script.snapshot && picture.script.snapshot->width == 257 &&
              picture.script.snapshot->pixels.size() == imagePixels.size(),
          "model did not copy snapshot pixels");
    const auto revision = document.Session(script.created)->Revision(),
               viewRevision = document.Session(script.created)->ViewRevision();
    first = 0;
    unsigned picturePages = 0;
    std::vector<unsigned char> copiedPixels;
    do {
        const auto result = client.Query(
            "schematic.snapshot",
            {ObjectArgument("target", target), IntegerArgument("open", 1), IntegerArgument("revision", revision),
             IntegerArgument("topology", document.TopologyRevision()), IntegerArgument("view-revision", viewRevision),
             IntegerArgument("first", first), IntegerArgument("refresh", 2)});
        Check(result.status == EDITOR_OK, "snapshot page failed");
        for (auto pixel : Field(result, "pixels").integers)
            for (unsigned byte = 0; byte < 4; ++byte)
                copiedPixels.push_back(uint8_t(pixel >> (8 * byte)));
        first = EditorClient::Integer(result, "next");
        ++picturePages;
    } while (first < 257 * 257);
    Check(picturePages == 2 && copiedPixels == picture.script.snapshot->pixels && copiedPixels[3] == 255,
          "snapshot paging changed pixels or alpha");
    auto retainedPicture = picture.script.snapshot;
    document.Close();
    Check(retainedPicture->pixels == copiedPixels, "document close invalidated copied drawing pixels");
    Check(document.New(&error), error);
    app.Attach(document.Token(), document.TopologyRevision(), true, true);
    const auto staleState =
        client.Query("schematic.stack", {stackArguments.data(), stackArguments.data() + stackArguments.size()});
    Check(staleState.status == EDITOR_OK && Field(staleState, "all").objects.empty(),
          "retained stack state read objects of a new document");
    const auto oldPositions =
        client.Query("schematic.validate-positions",
                     {borrowedPositions.data(), borrowedPositions.data() + borrowedPositions.size()});
    Check(oldPositions.status == EDITOR_OK && Field(oldPositions, "elements").fields.empty(),
          "old positions survived document replacement");
    picture.script.snapshot.reset();
    std::thread release([copy = std::move(retainedPicture)]() mutable { copy.reset(); });
    release.join();
    app.CloseScope(view);
}
