#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "core/runtime/CkRuntimeHost.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Document.h"
#include "core/schematic/SchematicHitTest.h"
#include "core/schematic/LinkRouting.h"
#include "core/schematic/SchematicModelBuilder.h"
#include "core/schematic/SchematicSearch.h"
#include "core/schematic/SchematicNavigation.h"
#include "core/document/Edit.h"
#include "core/document/LinkValidation.h"
#include "VirtoolsRuntimeCheck.h"

using namespace vdev;

namespace {

std::string RuntimeDirectory() {
    const char *path = std::getenv("VIRTOOLS_PATH");
    return path ? path : ".";
}

CKBehavior *CreateScript(CKContext *context, const char *name) {
    auto *script = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING)name));
    assert(script);
    script->UseGraph();
    script->SetType(CKBEHAVIORTYPE_SCRIPT);
    return script;
}

CKBehavior *AddBehavior(CKContext *context, CKBehavior *parent, const char *name, bool graph) {
    auto *behavior = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING)name));
    assert(behavior);
    if (graph) behavior->UseGraph(); else behavior->UseFunction();
    assert(parent->AddSubBehavior(behavior) == CK_OK);
    return behavior;
}

size_t CountPins(const SchematicModel &model, CK_ID owner, PinKind kind) {
    size_t count = 0;
    for (const auto &pin : model.pins)
        if (pin.owner == owner && pin.kind == kind) ++count;
    return count;
}

void SyntheticScriptModel(CKContext *context) {
    CKBehavior *script = CreateScript(context, "Model Script");
    CKBehaviorIO *start = script->CreateInput((CKSTRING)"Start");
    CKBehavior *graph = AddBehavior(context, script, "Graph", true);
    CKBehavior *inner = AddBehavior(context, graph, "Inner", false);
    CKBehavior *block = AddBehavior(context, script, "Block", false);
    CKBehaviorIO *graphIn = graph->CreateInput((CKSTRING)"In");
    graph->CreateOutput((CKSTRING)"Out");
    block->CreateInput((CKSTRING)"In");
    block->CreateOutput((CKSTRING)"Out");
    block->CreateInputParameter((CKSTRING)"Value", CKPGUID_INT);
    block->CreateOutputParameter((CKSTRING)"Result", CKPGUID_INT);
    inner->CreateInput((CKSTRING)"In");
    assert(start && graphIn);
    auto *link = CKBehaviorLink::Cast(context->CreateObject(CKCID_BEHAVIORLINK));
    assert(link && link->SetInBehaviorIO(start) == CK_OK && link->SetOutBehaviorIO(graphIn) == CK_OK);
    assert(script->AddSubBehaviorLink(link) == CK_OK);

    ScriptSession session;
    assert(session.Open(context, script->GetID()));
    assert(session.Origin() == SessionOrigin::Reconstructed && !session.Dirty());

    SchematicModel model;
    model = BuildSchematicModel(session, context);
    assert(model.script.open && model.start.present);
    assert(model.unresolvedEndpoints == 0);
    assert(model.FindBehavior(graph->GetID()) && model.FindBehavior(block->GetID()));
    // Reconstructed child graphs are folded: their contents stay hidden.
    assert(!model.FindBehavior(inner->GetID()));
    assert(CountPins(model, block->GetID(), PinKind::BehaviorIn) == 1);
    assert(CountPins(model, block->GetID(), PinKind::BehaviorOut) == 1);
    assert(CountPins(model, block->GetID(), PinKind::ParamIn) == 1);
    assert(CountPins(model, block->GetID(), PinKind::ParamOut) == 1);
    assert(model.links.size() == 1 && model.links[0].resolved && model.links[0].path.size() == 2);

    // Picking: a pin wins over its behavior box; the box body selects the behavior.
    const PinItem *pin = model.FindPin(LinkEndpoint(block->GetID(), 0, ENDPOINT_BIN));
    assert(pin);
    ElementRef picked = PickElement(model, pin->center, 0.0f);
    assert(picked.kind == ElementKind::Pin && picked.object == block->GetID() && picked.pin == PinKind::BehaviorIn);
    const BehaviorItem *blockItem = model.FindBehavior(block->GetID());
    picked = PickElement(model, Point(blockItem->box.CenterX(), blockItem->box.CenterY()), 0.0f);
    assert(picked.kind == ElementKind::Behavior && picked.object == block->GetID());
    const auto inRect = ElementsInRect(model, Rect(blockItem->box.hPos - 1, blockItem->box.vPos - 1,
                                                   blockItem->box.hSize + 2, blockItem->box.vSize + 2));
    assert(inRect.size() == 1 && inRect[0].object == block->GetID());

    // Dev does not show disabled inputs (CK_PARAMETERIN_DISABLED, Dev 0x4D15C0).
    CKParameterIn *disabled = block->CreateInputParameter((CKSTRING)"Disabled", CKPGUID_INT);
    assert(disabled);
    disabled->Enable(FALSE);
    model = BuildSchematicModel(session, context);
    assert(CountPins(model, block->GetID(), PinKind::ParamIn) == 1);
    disabled->Enable(TRUE);
    model = BuildSchematicModel(session, context);
    assert(CountPins(model, block->GetID(), PinKind::ParamIn) == 2);

    // Expanding the graph shows its contents inside the expanded frame.
    InterfaceData expanded = session.Data();
    expanded.FindBehavior(graph->GetID())->folded = false;
    assert(session.Assign(expanded));
    model = BuildSchematicModel(session, context);
    assert(model.FindBehavior(inner->GetID()) && model.IsOpenGraph(graph->GetID()));
    assert(model.FindBehavior(inner->GetID())->depth == 2);

    // A closed Script (view state) draws its header only.
    session.SetOpen(false);
    model = BuildSchematicModel(session, context);
    assert(!model.script.open && model.behaviors.empty() && model.links.empty() && !model.start.present);
    context->ClearAll();
    std::cout << "Synthetic Script model passed\n";
}

void ExplorerSearch(CkRuntimeHost &host) {
    CKContext *context = host.Context();
    CKBehavior *script = CreateScript(context, "Search Script");
    CKBehavior *block = AddBehavior(context, script, "Alpha Block", false);
    CKParameterIn *speed = block->CreateInputParameter((CKSTRING)"Speed", CKPGUID_FLOAT);
    CKParameterLocal *counter = script->CreateLocalParameter((CKSTRING)"Counter", CKPGUID_INT);
    CKParameterManager *manager = context->GetParameterManager();
    const CKGUID addition = manager->OperationNameToGuid((CKSTRING)"Addition");
    CKParameterOperation *operation = context->CreateCKParameterOperation(
        (CKSTRING)"Sum", addition, CKPGUID_INT, CKPGUID_INT, CKPGUID_INT);
    assert(speed && counter && operation && script->AddParameterOperation(operation) == CK_OK);

    Document document(host);
    document.RefreshScripts();
    ScriptSession *session = document.Session(script->GetID());
    assert(session);
    Comment note;
    note.text = "Remember the alpha path";
    InterfaceData annotated = session->Data();
    annotated.rootBehavior.AddComment(note);
    assert(session->Assign(annotated));

    auto run = [&](const std::string &text, CK_ID id = 0, unsigned kinds = SearchAll) {
        SearchQuery query;
        query.text = text;
        query.id = id;
        query.kinds = kinds;
        return SearchDocument(document, query);
    };
    auto results = run("alpha");
    assert(results.size() == 2);
    assert(results[0].ref.kind == ElementKind::Behavior && results[0].ref.object == block->GetID());
    assert(results[1].ref.kind == ElementKind::Comment && results[1].ref.index == 0);
    assert(run("alpha", 0, SearchBehaviors).size() == 1);
    results = run("SPEED");
    assert(results.size() == 1 && results[0].ref.kind == ElementKind::Pin &&
           results[0].ref.pin == PinKind::ParamIn && results[0].focus == block->GetID());
    results = run("", counter->GetID());
    assert(results.size() == 1 && results[0].ref.kind == ElementKind::LocalParam && results[0].ref.index == 0);
    results = run("addition");
    assert(results.size() == 1 && results[0].ref.kind == ElementKind::Operation &&
           results[0].ref.object == operation->GetID());
    assert(run("").empty() && run("no such element").empty());

    // Dev's list (listAll): every element of the kinds; the text and CK ID
    // narrow it; the Parameter Filter by type, then by value; Replace Value.
    auto list = [&](unsigned kinds, SearchFilter filter = SearchFilter::None, int type = -1, CK_ID value = 0,
                    const std::string &text = std::string()) {
        SearchQuery query;
        query.listAll = true;
        query.kinds = kinds;
        query.filter = filter;
        query.type = type;
        query.value = value;
        query.text = text;
        return SearchDocument(document, query);
    };
    results = list(SearchScripts);
    assert(results.size() == 1 && results[0].ref.kind == ElementKind::Script && results[0].object == script->GetID());
    assert(list(SearchBuildingBlocks).size() == 1 && list(SearchGraphs).empty());
    assert(list(SearchBuildingBlocks, SearchFilter::None, -1, 0, "ALPHA").size() == 1);
    assert(list(SearchBuildingBlocks, SearchFilter::None, -1, 0, "beta").empty());
    const int intType = manager->ParameterGuidToType(CKPGUID_INT), floatType = manager->ParameterGuidToType(CKPGUID_FLOAT);
    // Filter Type: the parameters of the type only (no Script, no Building Block).
    results = list(SearchAll, SearchFilter::Type, floatType);
    assert(results.size() == 1 && results[0].object == speed->GetID());
    results = list(SearchLocalParameters, SearchFilter::Type, intType);
    assert(results.size() == 1 && results[0].object == counter->GetID());
    // Filter Value: the locals of the type holding the value.
    const int five = 5, seven = 7;
    counter->SetValue(&five);
    CKParameterOut *value = context->CreateCKParameterOut((CKSTRING) "Value", CKPGUID_INT, TRUE);
    value->SetValue(&seven);
    assert(list(SearchAll, SearchFilter::Value, intType, value->GetID()).empty());
    value->SetValue(&five);
    results = list(SearchAll, SearchFilter::Value, intType, value->GetID());
    assert(results.size() == 1 && results[0].object == counter->GetID());
    // Replace Value: the listed parameters take the replace value (one undo step).
    CKParameterOut *replace = context->CreateCKParameterOut((CKSTRING) "Replace", CKPGUID_INT, TRUE);
    replace->SetValue(&seven);
    assert(ReplaceParameterValues(document, {{script->GetID(), counter->GetID()}}, replace->GetID()).ok);
    int now = 0;
    counter->GetValue(&now);
    assert(now == 7);
    assert(document.History().Undo());
    counter->GetValue(&now);
    assert(now == 5);
    context->DestroyObject(value);
    context->DestroyObject(replace);
    context->ClearAll();
    std::cout << "Explorer search passed\n";
}

void RetainedElementAddresses(CkRuntimeHost &host) {
    auto *context = host.Context();
    auto *script = CreateScript(context, "Retained positions");
    auto *block = AddBehavior(context, script, "Stable behavior", false);
    auto *input = block->CreateInputParameter((CKSTRING)"Original pin", CKPGUID_INT);
    auto *local = script->CreateLocalParameter((CKSTRING)"Local", CKPGUID_INT);
    assert(input && local);
    Document document(host);
    document.RefreshScripts();
    auto *session = document.Session(script->GetID());
    assert(session);
    auto data = session->Data();
    Comment a, b; a.text = "First"; b.text = "Second";
    data.rootBehavior.comments = {a, b};
    Link link; link.points = {Point(10, 10), Point(20, 20)};
    data.rootBehavior.links.push_back(link);
    data.rootBehavior.sharedParams.push_back(data.rootBehavior.localParams.front());
    assert(session->Assign(data));
    const CK_ID scriptId = script->GetID();
    const ElementRef behavior{ElementKind::Behavior, scriptId, block->GetID()};
    const auto stable = ElementAddress::Capture(document, scriptId, behavior);
    const std::vector<ElementRef> slots = {
        {ElementKind::Comment, scriptId, 0, 0}, {ElementKind::LocalParam, scriptId, 0, 0},
        {ElementKind::Shortcut, scriptId, 0, 0}, {ElementKind::Link, scriptId, 0, 0},
        {ElementKind::ControlPoint, scriptId, 0, 0, 0},
        {ElementKind::Pin, block->GetID(), block->GetID(), 0, -1, PinKind::ParamIn}};
    std::vector<ElementAddress> retained;
    for (const auto &ref : slots) {
        retained.push_back(ElementAddress::Capture(document, scriptId, ref));
        assert(retained.back());
        CK_ID resolved = 0; ElementRef element;
        assert(retained.back().Resolve(document, resolved, element) && element == ref && resolved == scriptId);
    }
    Selection selection;
    selection.Bind(
        [&](uint32_t script, const ElementRef &ref) { return ElementAddress::Capture(document, script, ref); });
    selection.Set(scriptId, slots.front());
    selection.Add(scriptId, behavior);
    SearchQuery query; query.text = "First"; query.kinds = SearchComments;
    const auto results = SearchDocument(document, query);
    assert(results.size() == 1 && results.front().address);
    // The same slot now contains a different comment. An old selection,
    // search result, link, point, local, shortcut or pin index cannot follow it.
    assert(EditScriptData(document, scriptId, "Replace first comment", [](InterfaceData &value) {
        value.rootBehavior.comments.erase(value.rootBehavior.comments.begin());
    }));
    for (const auto &address : retained) {
        CK_ID resolved = 123; ElementRef element = behavior;
        assert(!address.Resolve(document, resolved, element));
        assert(resolved == 123 && element == behavior);
    }
    InvalidLink staleLink;
    staleLink.script = staleLink.graph = scriptId;
    staleLink.slot = 0;
    staleLink.address = retained[3];
    const auto linksBefore = session->Data().rootBehavior.links.size();
    assert(RemoveInvalidLinks(document, {staleLink}) == 0);
    assert(session->Data().rootBehavior.links.size() == linksBefore);
    selection.Validate([&](const ElementAddress &address) {
        uint32_t script = 0;
        ElementRef ref;
        return address.Resolve(document, script, ref);
    });
    assert(selection.items.size() == 1 && selection.items.front() == behavior);
    CK_ID resolved = 0; ElementRef element;
    assert(stable.Resolve(document, resolved, element));
    assert(!results.front().address.Resolve(document, resolved, element));
    assert(document.History().Undo());
    assert(!retained.front().Resolve(document, resolved, element));
    assert(document.History().Redo());
    assert(!retained.front().Resolve(document, resolved, element));
    // Native pin replacement can happen without a display-data revision.
    const ElementRef pin{ElementKind::Pin, block->GetID(), block->GetID(), 0, -1, PinKind::ParamIn};
    const auto originalPin = ElementAddress::Capture(document, scriptId, pin);
    const auto revision = session->Revision();
    auto *removed = block->RemoveInputParameter(0);
    assert(removed == input);
    context->DestroyObject(removed, CK_DESTROY_NONOTIFY);
    assert(block->CreateInputParameter((CKSTRING)"Replacement pin", CKPGUID_INT));
    assert(session->Revision() == revision);
    assert(!originalPin.Resolve(document, resolved, element));
    assert(ElementAddress::Capture(document, scriptId, pin).Resolve(document, resolved, element));
    // Invalid table positions are rejected at capture, before entering a queue.
    assert(!ElementAddress::Capture(document, scriptId, {ElementKind::Comment, scriptId, 0, -1}));
    assert(!ElementAddress::Capture(document, scriptId, {ElementKind::ControlPoint, scriptId, 0, 0, 999}));
    SchematicNavigation navigation;
    navigation.hiddenScripts.insert(stable.script);
    navigation.scriptOrder = {stable.script};
    navigation.stackScript = stable.script;
    context->ClearAll();
    assert(navigation.HiddenIds(document).empty());
    assert(!stable.Resolve(document, resolved, element));
    selection.Validate([&](const ElementAddress &address) {
        uint32_t script = 0;
        ElementRef ref;
        return address.Resolve(document, script, ref);
    });
    assert(selection.Empty());
    std::cout << "Retained Schematic positions passed\n";
}

// Dev's record order compared with the runtime tree: breadth-first (as
// GraphBuilder writes) or depth-first pre-order. Used to place new records.
enum class RecordOrder { Both, BreadthFirst, DepthFirst, Neither };

RecordOrder ClassifyRecordOrder(const InterfaceData &data, CKBehavior *script) {
    std::vector<CK_ID> breadth, depth;
    std::vector<CKBehavior *> queue{script};
    for (size_t i = 0; i < queue.size(); ++i)
        for (int c = 0; c < queue[i]->GetSubBehaviorCount(); ++c)
            if (CKBehavior *child = queue[i]->GetSubBehavior(c)) {
                queue.push_back(child);
                breadth.push_back(child->GetID());
            }
    std::vector<CKBehavior *> stack{script};
    while (!stack.empty()) {
        CKBehavior *current = stack.back();
        stack.pop_back();
        if (current != script) depth.push_back(current->GetID());
        for (int c = current->GetSubBehaviorCount() - 1; c >= 0; --c)
            if (CKBehavior *child = current->GetSubBehavior(c)) stack.push_back(child);
    }
    std::vector<CK_ID> stored;
    for (const auto &record : data.behaviors) stored.push_back(record.id);
    // Relaxed: any child order. Breadth-first means depths never decrease;
    // pre-order means every record follows its parent's subtree start.
    std::map<CK_ID, CK_ID> parentOf;
    for (CKBehavior *node : queue)
        for (int c = 0; c < node->GetSubBehaviorCount(); ++c)
            if (CKBehavior *child = node->GetSubBehavior(c)) parentOf[child->GetID()] = node->GetID();
    auto depthOf = [&](CK_ID id) { int n = 0; while (parentOf.count(id) && id != script->GetID()) { id = parentOf[id]; ++n; } return n; };
    bool levels = stored.size() == breadth.size(), pre = levels;
    std::vector<CK_ID> path{script->GetID()};
    for (size_t i = 0; i < stored.size() && (levels || pre); ++i) {
        if (i && depthOf(stored[i]) < depthOf(stored[i - 1])) levels = false;
        while (!path.empty() && path.back() != parentOf[stored[i]]) path.pop_back();
        if (path.empty()) pre = false; else path.push_back(stored[i]);
    }
    const bool b = stored == breadth || levels, d = stored == depth || pre;
    return b && d ? RecordOrder::Both : b ? RecordOrder::BreadthFirst : d ? RecordOrder::DepthFirst : RecordOrder::Neither;
}

// Every saved InterfaceChunk in the sample list must open and resolve every
// link endpoint. Reconstructed Scripts are counted separately. Paths in the
// list are relative to the working directory.
int SampleListModels(CkRuntimeHost &host, const std::string &listPath) {
    std::ifstream list(listPath);
    if (!list) {
        std::cerr << "Cannot read sample list " << listPath << '\n';
        return 2;
    }
    // Sample lists hold repository-relative paths; run from the repository root.
    int files = 0, loaded = 0, reconstructed = 0, failed = 0, unresolved = 0;
    int orderCounts[4] = {};
    int links = 0, mismatched = 0;
    int hiddenLinks = 0, hiddenLocals = 0, disabledShown = 0, conversions = 0;
    int openScripts = 0, overflowing = 0;
    int routeTried = 0, routeOk = 0;
    int conversionInGraph = 0, conversionElsewhere = 0, conversionUnlisted = 0;
    int inconsistentScripts = 0;
    std::string line;
    while (std::getline(list, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        // SCHEMATIC_MODEL_TRACE: name each file first (locates a crashing one).
        if (std::getenv("SCHEMATIC_MODEL_TRACE")) std::cerr << "file " << line << std::endl;
        Document document(host);
        std::string error;
        if (!document.Open(line, &error)) {
            std::cerr << error << '\n';
            return 2;
        }
        ++files;
        for (const auto &info : document.Scripts()) {
            ScriptSession *session = document.Session(info.id);
            assert(session);
            if (session->Origin() == SessionOrigin::LoadFailed) {
                std::cerr << line << " / " << info.name << ": " << session->LoadMessage() << '\n';
                ++failed;
                continue;
            }
            session->Origin() == SessionOrigin::LoadedChunk ? ++loaded : ++reconstructed;
            if (session->Origin() == SessionOrigin::LoadedChunk)
                ++orderCounts[static_cast<int>(ClassifyRecordOrder(
                    session->Data(), CKBehavior::Cast(host.Context()->GetObject(info.id))))];
            if (session->Origin() == SessionOrigin::LoadedChunk) {
                // Every stored link names a CK relation (RuntimeGraph.h);
                // topology commands rely on this to prune replaced links.
                auto check = [&](const BehaviorData &record) {
                    for (const auto &link : record.links) {
                        ++links;
                        if (LinkMatchesRuntime(host.Context(), record, link)) continue;
                        std::cerr << line << " / " << info.name << ": graph " << record.id << " link "
                                  << link.start.type << "->" << link.end.type << " has no CK relation\n";
                        ++mismatched;
                    }
                };
                check(session->Data().rootBehavior);
                for (const auto &record : session->Data().behaviors) check(record);
            }
            if (session->Origin() == SessionOrigin::LoadedChunk && std::getenv("SCHEMATIC_ROUTE_STATS")) {
                // The editor's P command: one link routed alone (on a copy).
                InterfaceData copy = session->Data();
                copy.observers.clear();
                CKBehavior *script = CKBehavior::Cast(host.Context()->GetObject(info.id));
                auto route = [&](BehaviorData &record) {
                    for (auto &link : record.links) {
                        Link alone = link;
                        alone.points.clear();
                        ++routeTried;
                        routeOk += RouteSingleLink(copy, record, alone, script) ? 1 : 0;
                    }
                };
                route(copy.rootBehavior);
                for (auto &record : copy.behaviors) route(record);
            }
            // Open every graph so all stored links are resolved.
            InterfaceData expanded = session->Data();
            expanded.rootBehavior.folded = false;
            for (auto &record : expanded.behaviors) record.folded = false;
            const bool assigned = session->Assign(expanded);
            if (!assigned) ++inconsistentScripts;
            SchematicModel model;
            model = BuildSchematicModel(*session, host.Context());
            // Shown parameter input pins are enabled; hidden links end at a
            // disabled input.
            for (const auto &pin : model.pins) {
                if (pin.kind != PinKind::ParamIn || pin.onOperation || pin.onScript) continue;
                CKBehavior *owner = CKBehavior::Cast(host.Context()->GetObject(pin.owner));
                CKParameterIn *input = owner ? owner->GetInputParameter(pin.index) : nullptr;
                if (input && !input->IsEnabled()) {
                    std::cerr << line << " / " << info.name << ": disabled input shown\n";
                    ++disabledShown;
                }
            }
            for (const auto &link : model.links) {
                if (!link.hidden) continue;
                ++hiddenLinks;
                CKObject *owner = host.Context()->GetObject(link.end.id);
                CKBehavior *behavior = CKBehavior::Cast(owner);
                CKParameterIn *input = behavior && link.end.type == ENDPOINT_PIN ? behavior->GetInputParameter(link.end.index)
                                       : behavior && link.end.type == ENDPOINT_TARGET_PIN ? behavior->GetTargetParameter()
                                                                                           : nullptr;
                if (!input || input->IsEnabled()) {
                    std::cerr << line << " / " << info.name << ": hidden link to an enabled input\n";
                    ++disabledShown;
                }
            }
            for (const auto &param : model.params) hiddenLocals += param.hidden ? 1 : 0;
            if (session->Origin() == SessionOrigin::LoadedChunk && model.script.open) {
                // Contents below the stored Script height (Dev clips them to the band).
                ++openScripts;
                const float top = session->Data().rootBehavior.rect.vPos;
                if (model.contentBounds.Bottom() > top + session->Data().header.vSize + 0.5f) ++overflowing;
            }
            for (const auto &link : model.links) conversions += link.conversion.empty() ? 0 : 1;
            if (session->Origin() == SessionOrigin::LoadedChunk) {
                CKBehavior *root = CKBehavior::Cast(host.Context()->GetObject(info.id));
                const auto issues = RuntimeConsistencyIssues(host.Context(), session->Data(), root);
                if (!issues.empty()) {
                    ++inconsistentScripts;
                    if (std::getenv("SCHEMATIC_ISSUES")) std::cerr << line << " / " << info.name << ": " << *issues.begin() << '\n';
                }
            }
            if (session->Origin() == SessionOrigin::LoadedChunk) {
                // Where Dev keeps a hidden conversion operation: attached to which behavior's list.
                auto inspect = [&](const BehaviorData &record) {
                    for (const auto &link : record.links) {
                        CKParameterOperation *conversion = HiddenConversion(host.Context(), record, link);
                        if (!conversion) continue;
                        CKBehavior *owner = CKBehavior::Cast(conversion->GetOwner());
                        bool listed = false;
                        for (int i = 0; owner && i < owner->GetParameterOperationCount(); ++i)
                            listed = listed || owner->GetParameterOperation(i) == conversion;
                        if (owner && owner->GetID() == record.id && listed) ++conversionInGraph;
                        else if (owner && listed) ++conversionElsewhere;
                        else ++conversionUnlisted;
                    }
                };
                inspect(session->Data().rootBehavior);
                for (const auto &record : session->Data().behaviors) inspect(record);
            }
            if (model.unresolvedEndpoints && session->Origin() == SessionOrigin::LoadedChunk) {
                std::cerr << line << " / " << info.name << ": " << model.unresolvedEndpoints
                          << " unresolved link endpoints\n";
                unresolved += model.unresolvedEndpoints;
            }
        }
    }
    std::cout << "Sample models: files=" << files << " loadedChunks=" << loaded << " reconstructed=" << reconstructed
              << " loadFailures=" << failed << " unresolvedEndpoints=" << unresolved << '\n';
    std::cout << "Record order: trivial=" << orderCounts[0] << " breadthFirst=" << orderCounts[1]
              << " depthFirst=" << orderCounts[2] << " other=" << orderCounts[3] << '\n';
    std::cout << "Stored links: " << links << " without a CK relation: " << mismatched << '\n';
    if (routeTried) std::cout << "Single-link routing: " << routeOk << " / " << routeTried << " routed" << std::endl;
    std::cout << "Scripts with display/runtime issues: " << inconsistentScripts << std::endl;
    std::cout << "Hidden conversion operations: in the storing graph's list=" << conversionInGraph
              << " in another behavior's list=" << conversionElsewhere << " unlisted=" << conversionUnlisted << std::endl;
    std::cout << "Stored Script heights: open=" << openScripts << " contents below the height=" << overflowing << std::endl;
    std::cout << "Disabled inputs: hidden links=" << hiddenLinks << " hidden value locals=" << hiddenLocals
              << " errors=" << disabledShown << "; conversion links=" << conversions << '\n';
    return failed == 0 && unresolved == 0 && mismatched == 0 && disabledShown == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
    std::string error;
    if (!vdevtest::RuntimeAvailable(error)) {
        std::cerr << "skipped: " << error << '\n';
        return vdevtest::SKIPPED;
    }
    CkRuntimeHost host;
    const bool started = host.Start(RuntimeDirectory(), [](LogLevel level, const std::string &message) {
        if (level != LogLevel::Info) std::cerr << message << '\n';
    }, &error);
    if (!started) {
        std::cerr << "skipped: " << error << '\n';
        return vdevtest::SKIPPED;
    }
    const RuntimePluginCounts &counts = host.Counts();
    std::cout << "Plugins: behaviorDlls=" << counts.behaviorDlls << " prototypes=" << counts.behaviorPrototypes
              << " managers=" << counts.managerDlls << " renderEngines=" << counts.renderEngines
              << " readers=" << counts.modelReaders << '\n';
    assert(counts.behaviorDlls > 0 && counts.behaviorPrototypes > 0 && counts.managerDlls > 0);

    int result = 0;
    if (argc == 3 && std::string(argv[1]) == "--dump") {
        // Stored Script header state of one file, for comparison with Dev.
        Document document(host);
        std::string error;
        if (!document.Open(argv[2], &error)) {
            std::cerr << error << std::endl;
            return 2;
        }
        for (const auto &info : document.Scripts()) {
            ScriptSession *session = document.Session(info.id);
            const InterfaceData &data = session->Data();
            CKBehavior *script = CKBehavior::Cast(host.Context()->GetObject(info.id));
            std::cout << info.name << " | owner " << info.ownerName << " | folded " << data.rootBehavior.folded
                      << " flags 0x" << std::hex << data.rootBehavior.flags << " color 0x" << data.header.color
                      << std::dec << " scriptIndex " << data.header.scriptIndex << " vSize " << data.header.vSize
                      << " rect " << data.rootBehavior.rect.hPos << "," << data.rootBehavior.rect.vPos
                      << " active " << (script && script->IsActive()) << " ckflags 0x" << std::hex
                      << (script ? script->GetFlags() : 0) << std::dec << std::endl;
        }
        document.Close();
    } else if (argc == 3 && std::string(argv[1]) == "--sample-list") {
        result = SampleListModels(host, argv[2]);
    } else {
        SyntheticScriptModel(host.Context());
        ExplorerSearch(host);
        RetainedElementAddresses(host);
        std::cout << "SchematicModelTests passed\n";
    }
    host.Stop();
    return result;
}
