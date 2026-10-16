#include "core/graph/TopologyCommands.h"

#include <algorithm>
#include <cfloat>
#include <map>

#include "core/runtime/CkRuntimeHost.h"
#include "core/interface/InterfaceChunkFormat.h"
#include "core/schematic/LayoutCalculator.h"
#include "core/schematic/LinkEndpointGeometry.h"
#include "core/schematic/ParameterDisplayGeometry.h"
#include "core/catalog/ParameterCatalog.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Document.h"
#include "core/schematic/SchematicEdits.h"
#include "core/schematic/SchematicGrid.h"
#include "core/graph/detail/TopologyInternal.h"
#include "core/runtime/SdkCompat.h"

namespace vdev {

using namespace detail;

// ---------------------------------------------------------------------------
// TopologyEdit

TopologyEdit::TopologyEdit(Document &document, CK_ID script, const std::string &label)
    : m_Document(document), m_Edit(document, label), m_Script(script) {
    ScriptSession *session = document.Session(script);
    if (!session || !Script()) {
        m_Error = "Script not found";
    } else if (session->ReadOnly()) {
        m_Error = "The Script's Schematic could not be loaded; rebuild its layout before editing";
    } else if (InterfaceChunkFormat::VersionUsesSections(session->Data().version)) {
        m_Error = "Topology edits of Scripts saved in a pre-2.5 InterfaceChunk layout are not supported";
    }
    if (!session) return;
    m_After = session->Data();
    m_After.observers.clear();
    if (Valid()) {
        m_LinksBefore = MatchingLinks(Context(), m_After);
        m_IssuesBefore = RuntimeConsistencyIssues(Context(), m_After, Script());
        m_ConversionsBefore = HiddenConversions(Context(), m_After);
        for (CK_ID id : TreeIds(Script()))
            if (CKBehavior *behavior = BehaviorOf(Context(), id))
                for (int i = 0; i < behavior->GetLocalParameterCount(); ++i)
                    if (CKParameterLocal *local = behavior->GetLocalParameter(i))
                        m_LocalsBefore[local->GetID()] = {local->GetGUID(), ParameterText(local)};
    }
}

CKContext *TopologyEdit::Context() const {
    return m_Document.Context();
}

CKBehavior *TopologyEdit::Script() const {
    return BehaviorOf(Context(), m_Script);
}

EditResult TopologyEdit::Commit(CK_ID created) {
    if (!Valid()) return Fail(m_Error);
    CKContext *context = Context();
    CKBehavior *script = Script();
    std::set<CK_ID> removed;
    if (!m_Buried.empty()) removed = CollectOwnedObjects(context, m_Buried);
    Prune(context, script, m_After, m_LinksBefore, removed);
    // A hidden conversion no stored link uses any more (its link was deleted
    // or its destination got another source) leaves with it, undoably.
    const std::set<CK_ID> conversionsAfter = HiddenConversions(context, m_After);
    for (CK_ID id : m_ConversionsBefore) {
        if (conversionsAfter.count(id)) continue;
        CKParameterOperation *operation = CKParameterOperation::Cast(context->GetObject(id));
        CKBehavior *owner = operation ? CKBehavior::Cast(operation->GetOwner()) : nullptr;
        if (!owner || OperationPosition(owner, operation) < 0) continue;
        const CK_ID graphId = owner->GetID();
        const int at = OperationPosition(owner, operation);
        // What still reads its result goes without (corpus: Set Pickable.cmo,
        // the conversion's source deleted): an input left on the output of an
        // operation no longer in the composition saves that output alone.
        if (CKParameterOut *result = operation->GetOutParameter())
            for (CKParameterIn *reader : detail::ObjectsOfClass<CKParameterIn>(context, CKCID_PARAMETERIN)) {
                if (reader->GetDirectSource() != result) continue;
                const detail::InputSource before = detail::SourceOf(reader);
                const detail::InputRef ref = before.input;
                if (!Do(
                        [context, ref] {
                            CKParameterIn *input = detail::Resolve(context, ref);
                            return input && input->SetDirectSource(nullptr) == CK_OK;
                        },
                        [context, before] { return detail::RestoreSource(context, before); }))
                    return Fail("Cannot disconnect the hidden conversion operation");
            }
        if (!Do([context, graphId, id] { return DetachOperation(context, graphId, id); },
                [context, graphId, id, at] { return AttachOperation(context, graphId, id, at); }))
            return Fail("Cannot remove the hidden conversion operation");
        Bury(id);
    }
    // A value a callback wrote (corpus: Frustum Object Intersection.cmo, a
    // pasted Change Value If setting a local it no longer reads) goes back
    // with the undo; a local whose type changed has its own steps.
    for (const auto &entry : m_LocalsBefore) {
        CKParameterLocal *local = CKParameterLocal::Cast(context->GetObject(entry.first));
        if (!local || local->GetGUID() != entry.second.type) continue;
        const std::string now = ParameterText(local);
        if (now == entry.second.text) continue;
        const CK_ID id = entry.first;
        const std::string was = entry.second.text;
        auto write = [context, id](const std::string &text) {
            CKParameterLocal *target = CKParameterLocal::Cast(context->GetObject(id));
            return target && target->SetStringValue(const_cast<CKSTRING>(text.c_str())) == CK_OK;
        };
        Done([write, now] { return write(now); }, [write, was] { return write(was); });
    }
    // Another Script's shortcut of a removed parameter (corpus: Frustum Object
    // Intersection.cmo, Off Topic reading a deleted local) leaves its display
    // with it: CK disconnected the readers.
    if (!removed.empty())
        for (const ScriptInfo &info : std::vector<ScriptInfo>(m_Document.Scripts())) {
            if (info.id == m_Script) continue;
            ScriptSession *other = m_Document.Session(info.id);
            if (!other || other->ReadOnly()) continue;
            bool shows = false;
            ForEachRecord(other->Data(), [&](BehaviorData &record) {
                for (const auto &shared : record.sharedParams) shows = shows || removed.count(shared.sourceId);
            });
            if (!shows) continue;
            InterfaceData data = other->Data();
            data.observers.clear();
            Prune(context, BehaviorOf(context, info.id), data, {}, removed);
            if (!m_Edit.ShowScript(info.id, data)) return Fail("Script not found");
        }

    if (!m_Edit.ShowScript(m_Script, m_After)) return Fail("Script not found");
    const InterfaceData &shown = m_Document.ExistingSession(m_Script)->Data();
    std::vector<std::string> added;
    for (const auto &issue : RuntimeConsistencyIssues(context, shown, script))
        if (!m_IssuesBefore.count(issue)) added.push_back(issue);
    // Refused: the edit takes back the runtime, then the display (refreshed against the graph before).
    if (!added.empty()) return Fail("The edit would leave the Schematic inconsistent with the runtime graph: " + added.front());
    return m_Edit.Commit(std::string(), created);
}

// ---------------------------------------------------------------------------
// Edits

EditResult InsertBuildingBlock(Document &document, CK_ID scriptId, CK_ID graphId,
                               const CKGUID &prototype, const Point &position) {
    TopologyEdit edit(document, scriptId, "Insert Building Block");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    CKBehavior *graph = BehaviorOf(context, graphId);
    if (!EditableGraph(graph) || !TreeIds(edit.Script()).count(graphId))
        return edit.Fail("Building Blocks can only be added to the Script or an expanded behavior graph");
    BehaviorData *parent = FindRecord(edit.Data(), graphId);
    CKObjectDeclaration *declaration = CKGetObjectDeclarationFromGuid(prototype);
    if (!parent || !declaration) return edit.Fail("Unknown Building Block prototype");

    std::string error;
    CKBehavior *behavior = NewBuildingBlock(edit, prototype, error);
    if (!behavior) return edit.Fail(error);
    const CK_ID id = behavior->GetID();
    // An owner of another class acts through the Target (CK refuses the
    // behavior otherwise); a new behavior is not yet used anywhere.
    if (!OwnerAccepts(graph, behavior)) {
        if (!behavior->IsTargetable() || behavior->UseTarget(TRUE) != CK_OK)
            return edit.Fail(std::string(declaration->GetName()) + " does not apply to the Script owner's class");
    }
    const int at = graph->GetSubBehaviorCount();
    if (!edit.Do([context, graphId, id, at] { return AttachSubBehavior(context, graphId, id, at); },
                 [context, graphId, id] { return DetachSubBehavior(context, graphId, id); }))
        return edit.Fail("CKBehavior::AddSubBehavior failed");

    BehaviorData record(id);
    record.parentId = graphId;
    record.depth = parent == &edit.Data().rootBehavior ? 1 : parent->depth + 1;
    record.isBehaviorGraph = behavior->GetType() != CKBEHAVIORTYPE_BASE;
    record.folded = true;
    record.UpdateRuntimeState(behavior);
    LayoutCalculator::CalculateBehaviorSize(record, behavior);
    record.rect.hPos = SchematicGrid::SnapToGrid(position.h, GRID);
    record.rect.vPos = SchematicGrid::SnapToGrid(position.v, GRID);
    // Dev stores records level by level (all 111 multi-level Scripts in the
    // sample corpus): append at the end of the new record's depth.
    auto &records = edit.Data().behaviors;
    auto level = std::find_if(records.begin(), records.end(),
                              [&](const BehaviorData &other) { return other.depth > record.depth; });
    records.insert(level, record);
    edit.Data().behaviorCount = static_cast<int>(records.size());
    if (!AddInputValueLocals(edit, graphId, behavior, record))
        return edit.Fail("Cannot create the input parameter values");
    return edit.Commit(id);
}

EditResult CreateBehaviorGraph(Document &document, CK_ID scriptId, CK_ID graphId,
                               const std::string &name, const Point &position) {
    TopologyEdit edit(document, scriptId, "Create Behavior Graph");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    CKBehavior *graph = BehaviorOf(context, graphId);
    BehaviorData *parent = FindRecord(edit.Data(), graphId);
    if (!EditableGraph(graph) || !parent || !TreeIds(edit.Script()).count(graphId))
        return edit.Fail("Behavior graphs can only be added to the Script or an expanded behavior graph");
    const std::string label = name.empty() ? "Behavior Graph" : name;
    CKBehavior *behavior = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, const_cast<CKSTRING>(label.c_str())));
    if (!behavior) return edit.Fail("CKContext::CreateObject failed");
    behavior->UseGraph();
    behavior->SetType(CKBEHAVIORTYPE_BEHAVIOR);
    behavior->CreateInput((CKSTRING) "In");
    behavior->CreateOutput((CKSTRING) "Out");
    const CK_ID id = behavior->GetID();
    edit.Created(id);
    const int at = graph->GetSubBehaviorCount();
    if (!edit.Do([context, graphId, id, at] { return AttachSubBehavior(context, graphId, id, at); },
                 [context, graphId, id] { return DetachSubBehavior(context, graphId, id); }))
        return edit.Fail("CKBehavior::AddSubBehavior failed");
    BehaviorData record(id);
    record.parentId = graphId;
    record.depth = parent == &edit.Data().rootBehavior ? 1 : parent->depth + 1;
    record.isBehaviorGraph = true;
    record.folded = false;
    record.UpdateRuntimeState(behavior);
    LayoutCalculator::CalculateBehaviorSize(record, behavior);
    record.hExpandSize = 200.0f;
    record.vExpandSize = 120.0f;
    record.rect.hPos = SchematicGrid::SnapToGrid(position.h, GRID);
    record.rect.vPos = SchematicGrid::SnapToGrid(position.v, GRID);
    auto &records = edit.Data().behaviors;
    auto level = std::find_if(records.begin(), records.end(),
                              [&](const BehaviorData &other) { return other.depth > record.depth; });
    records.insert(level, record);
    edit.Data().behaviorCount = static_cast<int>(records.size());
    return edit.Commit(id);
}

namespace {

struct LinkRole {
    CK_ID graph = 0;
    CKObject *object = nullptr;
    LinkEndpoint endpoint;
};

// Behavior link roles: see CreateBehaviorLink.
bool BehaviorStart(const InterfaceData &data, CKContext *context, CK_ID script, const LinkEndpoint &e, LinkRole &role) {
    if (e.type == ENDPOINT_START_BIN) {
        CKBehaviorIO *io = CKBehaviorIO::Cast(context->GetObject(e.id));
        CKBehavior *owner = io ? io->GetOwner() : nullptr;
        if (!owner || owner->GetID() != script) return false;
        role = {script, io, LinkEndpoint(io->GetID(), owner->GetInputPosition(io), ENDPOINT_START_BIN)};
        return true;
    }
    CKBehavior *behavior = BehaviorOf(context, e.id);
    if (!behavior) return false;
    if (e.type == ENDPOINT_BOUT && e.index >= 0 && e.index < behavior->GetOutputCount()) {
        role = {ParentOf(data, e.id), behavior->GetOutput(e.index), e};
        return role.graph != 0;
    }
    if (e.type == ENDPOINT_BIN && e.index >= 0 && e.index < behavior->GetInputCount() &&
        behavior->GetType() != CKBEHAVIORTYPE_BASE) {
        CKBehaviorIO *io = behavior->GetInput(e.index);
        role = {e.id, io, e.id == script ? LinkEndpoint(io->GetID(), e.index, ENDPOINT_START_BIN) : e};
        return true;
    }
    return false;
}

bool BehaviorEnd(const InterfaceData &data, CKContext *context, CK_ID script, const LinkEndpoint &e, LinkRole &role) {
    CKBehavior *behavior = BehaviorOf(context, e.id);
    if (!behavior) return false;
    if (e.type == ENDPOINT_BIN && e.id != script && e.index >= 0 && e.index < behavior->GetInputCount()) {
        role = {ParentOf(data, e.id), behavior->GetInput(e.index), e};
        return role.graph != 0;
    }
    if (e.type == ENDPOINT_BOUT && behavior->GetType() != CKBEHAVIORTYPE_BASE && e.index >= 0 &&
        e.index < behavior->GetOutputCount()) {
        role = {e.id, behavior->GetOutput(e.index), e};
        return true;
    }
    return false;
}

CK_ID OwnerGraph(const InterfaceData &data, CKContext *context, CK_ID owner) {
    if (auto *operation = CKParameterOperation::Cast(context->GetObject(owner)))
        return operation->GetOwner() ? operation->GetOwner()->GetID() : 0;
    return ParentOf(data, owner);
}

// Parameter link roles: see CreateParameterLink.
bool ParameterSource(const InterfaceData &data, CKContext *context, const LinkEndpoint &e, LinkRole &role) {
    const BehaviorData *record = nullptr;
    switch (e.type) {
    case ENDPOINT_POUT:
        role.graph = OwnerGraph(data, context, e.id);
        record = FindRecord(data, role.graph);
        break;
    case ENDPOINT_PLOCAL:
    case ENDPOINT_POUT_SHORTCUT:
        role.graph = e.id;
        record = FindRecord(data, e.id);
        break;
    case ENDPOINT_PIN: {
        CKBehavior *behavior = BehaviorOf(context, e.id);
        if (!behavior || behavior->GetType() == CKBEHAVIORTYPE_BASE) return false; // a graph's own pIn, inside
        role.graph = e.id;
        record = FindRecord(data, e.id);
        break;
    }
    default:
        return false;
    }
    if (!record || !role.graph) return false;
    role.object = ResolveEndpointObject(context, *record, e);
    role.endpoint = e;
    return role.object != nullptr;
}

bool ParameterDestination(const InterfaceData &data, CKContext *context, const LinkEndpoint &e, LinkRole &role) {
    switch (e.type) {
    case ENDPOINT_PIN:
    case ENDPOINT_TARGET_PIN:
        role.graph = OwnerGraph(data, context, e.id);
        break;
    case ENDPOINT_PLOCAL:
        role.graph = e.id;
        break;
    case ENDPOINT_POUT: {
        CKBehavior *behavior = BehaviorOf(context, e.id);
        if (!behavior || behavior->GetType() == CKBEHAVIORTYPE_BASE) return false; // a graph's own pOut, inside
        role.graph = e.id;
        break;
    }
    default:
        return false;
    }
    const BehaviorData *record = FindRecord(data, role.graph);
    if (!record || !role.graph) return false;
    role.object = ResolveEndpointObject(context, *record, e);
    role.endpoint = e;
    if (e.type == ENDPOINT_TARGET_PIN) role.endpoint.index = -2;
    return role.object != nullptr;
}

// A shortcut of `shown` in graph (the one already there, else a new one two
// rows above `beside`, library placement); its endpoint.
bool ShortcutIn(TopologyEdit &edit, CK_ID graphId, CKObject *shown, const LinkEndpoint &beside, LinkEndpoint &endpoint) {
    BehaviorData *record = FindRecord(edit.Data(), graphId);
    if (!record || !shown) return false;
    for (int i = 0; i < static_cast<int>(record->sharedParams.size()); ++i)
        if (record->sharedParams[i].sourceId == shown->GetID()) {
            endpoint = LinkEndpoint(graphId, i, ENDPOINT_POUT_SHORTCUT);
            return true;
        }
    Point pin;
    const bool rootIsScript = (edit.Script()->GetType() & CKBEHAVIORTYPE_SCRIPT) != 0;
    if (!TryGetLinkEndpointPosition(edit.Data(), beside, pin, rootIsScript)) pin = Point(record->rect.hPos, record->rect.vPos);
    const Point cell = GridCell(*record, IsScriptUi(edit, *record), Point(pin.h - GRID * 0.5f, pin.v - GRID * 2.0f));
    // Dev saves these shortcuts with NAMEVALUE (dev-2.5-cross-graph-parameter-destinations.json).
    // A shortcut's own id is not stored (it reads back 0); sourceId names the parameter.
    Parameter shortcut(0, static_cast<int>(cell.h), static_cast<int>(cell.v), PARAM_STYLE_NAMEVALUE);
    shortcut.sourceId = shown->GetID();
    record->sharedParams.push_back(shortcut);
    endpoint = LinkEndpoint(graphId, static_cast<int>(record->sharedParams.size()) - 1, ENDPOINT_POUT_SHORTCUT);
    return true;
}

// A parameter link between two graphs. Dev links only inside a graph
// (CUIKLink::ValidateLinkConnection 0x4DC750); such links come from
// shortcuts, and Dev saves them so: an input fed from another graph is
// linked from a shortcut of its source in the input's graph; an output
// writing a parameter of another graph is linked to a shortcut of that
// parameter in the output's graph. The command makes the shortcut and the
// link in one step.
EditResult CrossGraphParameterLink(TopologyEdit &edit, const LinkEndpoint &a, const LinkEndpoint &b,
                                   const OperationSignature *conversion) {
    CKContext *context = edit.Context();
    const InterfaceData &data = edit.Data();
    LinkRole source, destination;
    bool found = false;
    for (int order = 0; order < 2 && !found; ++order) {
        const LinkEndpoint &from = order == 0 ? a : b;
        const LinkEndpoint &to = order == 0 ? b : a;
        found = ParameterSource(data, context, from, source) && ParameterDestination(data, context, to, destination) &&
                source.object != destination.object;
    }
    if (!found) return edit.Fail("These parameters cannot be linked");
    if (!EditableGraph(BehaviorOf(context, source.graph)) || !EditableGraph(BehaviorOf(context, destination.graph)))
        return edit.Fail("Links cannot be added inside a Building Block");
    if (conversion) return edit.Fail("A parameter operation link stays inside one graph");
    CKParameterManager *manager = context->GetParameterManager();
    auto typeOf = [](CKObject *object) {
        if (CKParameterIn *input = CKParameterIn::Cast(object)) return input->GetType();
        if (CKParameter *parameter = CKParameter::Cast(object)) return parameter->GetType();
        return -1;
    };
    const CKParameterType sourceType = typeOf(source.object), destinationType = typeOf(destination.object);
    if (!manager || sourceType < 0 || destinationType < 0 || !manager->IsTypeCompatible(sourceType, destinationType))
        return edit.Fail("Parameter types are not compatible");
    if (CKParameterIn *input = CKParameterIn::Cast(destination.object)) {
        // CK: the input reads the parameter directly.
        CKParameter *from = CKParameter::Cast(source.object);
        if (!from) return edit.Fail("A graph's input parameter feeds only the inputs of its own graph");
        LinkEndpoint shortcut;
        if (!ShortcutIn(edit, destination.graph, from, destination.endpoint, shortcut))
            return edit.Fail("Cannot show the source in the input's graph");
        const InputSource before = SourceOf(input);
        const InputRef targetRef = before.input;
        const CK_ID sourceId = from->GetID();
        if (!edit.Do(
                [context, targetRef, sourceId] {
                    CKParameterIn *target = Resolve(context, targetRef);
                    CKParameter *value = CKParameter::Cast(context->GetObject(sourceId));
                    return target && value && target->SetDirectSource(value) == CK_OK;
                },
                [context, before] { return RestoreSource(context, before); }))
            return edit.Fail("The parameter source was rejected");
        FindRecord(edit.Data(), destination.graph)
            ->links.push_back(Link(0, LINK_TYPE_PARAMETER, shortcut, destination.endpoint));
        return edit.Commit();
    }
    // CK: the output writes the parameter (a local or a graph's output).
    CKParameterOut *output = CKParameterOut::Cast(source.object);
    CKParameter *written = CKParameter::Cast(destination.object);
    if (!output || !written) return edit.Fail("Only an output parameter can write to a local or output parameter");
    const std::vector<CK_ID> before = DestinationsOf(output);
    const CK_ID outputId = output->GetID(), writtenId = written->GetID();
    if (std::find(before.begin(), before.end(), writtenId) != before.end())
        return edit.Fail("These parameters are already linked");
    LinkEndpoint shortcut;
    if (!ShortcutIn(edit, source.graph, written, source.endpoint, shortcut))
        return edit.Fail("Cannot show the parameter in the output's graph");
    if (!edit.Do(
            [context, outputId, writtenId] {
                CKParameterOut *from = CKParameterOut::Cast(context->GetObject(outputId));
                CKParameter *to = CKParameter::Cast(context->GetObject(writtenId));
                return from && to && from->AddDestination(to, TRUE) == CK_OK;
            },
            [context, outputId, before] { return RestoreDestinations(context, outputId, before); }))
        return edit.Fail("The parameter destination was rejected");
    FindRecord(edit.Data(), source.graph)->links.push_back(Link(0, LINK_TYPE_PARAMETER, source.endpoint, shortcut));
    return edit.Commit();
}

} // namespace

EditResult CreateBehaviorLink(Document &document, CK_ID scriptId, const LinkEndpoint &a,
                              const LinkEndpoint &b, int delay, bool autoLoopDelay) {
    TopologyEdit edit(document, scriptId, "Create Link");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    const InterfaceData &data = edit.Data();
    LinkRole start, end;
    bool found = false;
    for (int order = 0; order < 2 && !found; ++order) {
        const LinkEndpoint &from = order == 0 ? a : b;
        const LinkEndpoint &to = order == 0 ? b : a;
        found = BehaviorStart(data, context, scriptId, from, start) && BehaviorEnd(data, context, scriptId, to, end) &&
                start.graph == end.graph;
    }
    if (!found) return edit.Fail("These behavior pins cannot be linked: both ends must be in the same graph");
    CKBehavior *graph = BehaviorOf(context, start.graph);
    if (!EditableGraph(graph)) return edit.Fail("Links cannot be added inside a Building Block");
    for (int i = 0; i < graph->GetSubBehaviorLinkCount(); ++i) {
        CKBehaviorLink *existing = graph->GetSubBehaviorLink(i);
        if (existing && existing->GetInBehaviorIO() == start.object && existing->GetOutBehaviorIO() == end.object)
            return edit.Fail("These pins are already linked");
    }
    CKBehaviorLink *link = CKBehaviorLink::Cast(context->CreateObject(CKCID_BEHAVIORLINK));
    if (!link) return edit.Fail("CKContext::CreateObject failed");
    const CK_ID id = link->GetID();
    edit.Created(id);
    if (link->SetInBehaviorIO(CKBehaviorIO::Cast(start.object)) != CK_OK ||
        link->SetOutBehaviorIO(CKBehaviorIO::Cast(end.object)) != CK_OK)
        return edit.Fail("The behavior link endpoints were rejected");
    delay = std::max(0, std::min(32764, delay));
    CKBehavior *from = CKBehaviorIO::Cast(start.object)->GetOwner();
    CKBehavior *to = CKBehaviorIO::Cast(end.object)->GetOwner();
    // Dev checks only links between two child behaviors (UI element type 21).
    if (delay == 0 && autoLoopDelay && from != graph && to != graph && to->GetShortestDelay(from) == 0) delay = 1;
    link->SetInitialActivationDelay(delay);
    link->SetActivationDelay(delay);
    const CK_ID graphId = start.graph;
    const int at = graph->GetSubBehaviorLinkCount();
    const IoRef in = IoRefOf(CKBehaviorIO::Cast(start.object)), out = IoRefOf(CKBehaviorIO::Cast(end.object));
    if (!edit.Do([context, graphId, id, at, in, out] { return AttachLink(context, graphId, id, at, in, out); },
                 [context, graphId, id] { return DetachLink(context, graphId, id); }))
        return edit.Fail("CKBehavior::AddSubBehaviorLink failed");
    FindRecord(edit.Data(), graphId)->links.push_back(Link(id, LINK_TYPE_BEHAVIOR, start.endpoint, end.endpoint));
    return edit.Commit(id);
}

EditResult CreateParameterLink(Document &document, CK_ID scriptId, const LinkEndpoint &a,
                               const LinkEndpoint &b, const OperationSignature *conversion) {
    TopologyEdit edit(document, scriptId, "Create Parameter Link");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    const InterfaceData &data = edit.Data();
    LinkRole source, destination;
    bool found = false;
    for (int order = 0; order < 2 && !found; ++order) {
        const LinkEndpoint &from = order == 0 ? a : b;
        const LinkEndpoint &to = order == 0 ? b : a;
        found = ParameterSource(data, context, from, source) && ParameterDestination(data, context, to, destination) &&
                source.graph == destination.graph && source.object != destination.object;
    }
    if (!found) return CrossGraphParameterLink(edit, a, b, conversion);
    if (!EditableGraph(BehaviorOf(context, source.graph))) return edit.Fail("Links cannot be added inside a Building Block");

    CKParameterManager *manager = context->GetParameterManager();
    auto typeOf = [](CKObject *object) {
        if (CKParameterIn *input = CKParameterIn::Cast(object)) return input->GetType();
        if (CKParameter *parameter = CKParameter::Cast(object)) return parameter->GetType();
        return -1;
    };
    const CKParameterType sourceType = typeOf(source.object), destinationType = typeOf(destination.object);
    if (!manager || sourceType < 0 || destinationType < 0) return edit.Fail("Parameter types are not compatible");
    if (!manager->IsTypeCompatible(sourceType, destinationType)) {
        if (!conversion) return edit.Fail("Parameter types are not compatible");
        CKParameterIn *input = CKParameterIn::Cast(destination.object);
        CKParameter *from = CKParameter::Cast(source.object);
        if (!input || !from)
            return edit.Fail("A parameter operation link goes from an output, local or shortcut to an input");
        CKBehavior *graph = BehaviorOf(context, source.graph);
        CKParameterOperation *operation = nullptr;
        CKSTRING name = manager->OperationGuidToName(conversion->operation);
        GuardedCall([&] {
            operation = context->CreateCKParameterOperation(name, conversion->operation, conversion->result,
                                                            conversion->input1, conversion->input2);
        }, &document.Host(), "CreateCKParameterOperation");
        if (!operation || !operation->GetInParameter1() || !operation->GetOutParameter())
            return edit.Fail("CKContext::CreateCKParameterOperation failed");
        const CK_ID id = operation->GetID();
        edit.Created(id);
        const CK_ID graphId = source.graph;
        const int at = graph->GetParameterOperationCount();
        if (!edit.Do([context, graphId, id, at] { return AttachOperation(context, graphId, id, at); },
                     [context, graphId, id] { return DetachOperation(context, graphId, id); }))
            return edit.Fail("CKBehavior::AddParameterOperation failed");
        const CK_ID fromId = from->GetID();
        const InputRef in1 = RefOf(operation->GetInParameter1());
        const InputSource in1Before = SourceOf(operation->GetInParameter1());
        if (!edit.Do(
                [context, in1, fromId] {
                    CKParameterIn *target = Resolve(context, in1);
                    CKParameter *value = CKParameter::Cast(context->GetObject(fromId));
                    return target && value && target->SetDirectSource(value) == CK_OK;
                },
                [context, in1Before] { return RestoreSource(context, in1Before); }))
            return edit.Fail("The conversion operation refused the source");
        const InputSource before = SourceOf(input);
        const InputRef targetRef = before.input;
        const CK_ID outId = operation->GetOutParameter()->GetID();
        if (!edit.Do(
                [context, targetRef, outId] {
                    CKParameterIn *target = Resolve(context, targetRef);
                    CKParameter *value = CKParameter::Cast(context->GetObject(outId));
                    return target && value && target->SetDirectSource(value) == CK_OK;
                },
                [context, before] { return RestoreSource(context, before); }))
            return edit.Fail("The destination refused the conversion");
        FindRecord(edit.Data(), source.graph)
            ->links.push_back(Link(0, LINK_TYPE_PARAMETER, source.endpoint, destination.endpoint));
        return edit.Commit(id);
    }

    const CK_ID sourceId = source.object->GetID(), destinationId = destination.object->GetID();
    if (CKParameterIn *input = CKParameterIn::Cast(destination.object)) {
        const InputSource before = SourceOf(input);
        const InputRef targetRef = before.input;
        const InputRef sharedRef = RefOf(CKParameterIn::Cast(source.object));
        const bool shared = sharedRef.id != 0;
        if (!edit.Do(
                [context, sourceId, targetRef, sharedRef, shared] {
                    CKParameterIn *target = Resolve(context, targetRef);
                    if (!target) return false;
                    if (shared) {
                        CKParameterIn *from = Resolve(context, sharedRef);
                        return from && target->ShareSourceWith(from) == CK_OK;
                    }
                    CKParameter *from = CKParameter::Cast(context->GetObject(sourceId));
                    return from && target->SetDirectSource(from) == CK_OK;
                },
                [context, before] { return RestoreSource(context, before); }))
            return edit.Fail("The parameter source was rejected");
    } else {
        CKParameterOut *output = CKParameterOut::Cast(source.object);
        if (!output) return edit.Fail("Only an output parameter can write to a local or output parameter");
        const std::vector<CK_ID> before = DestinationsOf(output);
        if (std::find(before.begin(), before.end(), destinationId) != before.end())
            return edit.Fail("These parameters are already linked");
        if (!edit.Do(
                [context, sourceId, destinationId] {
                    CKParameterOut *from = CKParameterOut::Cast(context->GetObject(sourceId));
                    CKParameter *to = CKParameter::Cast(context->GetObject(destinationId));
                    return from && to && from->AddDestination(to, TRUE) == CK_OK;
                },
                [context, sourceId, before] { return RestoreDestinations(context, sourceId, before); }))
            return edit.Fail("The parameter destination was rejected");
    }
    FindRecord(edit.Data(), source.graph)
        ->links.push_back(Link(0, LINK_TYPE_PARAMETER, source.endpoint, destination.endpoint));
    return edit.Commit();
}

namespace detail {

// Creates a local parameter of graph (appended) and its display record at
// a grid cell; returns the local or null. Undo removes it from the list.
CKParameterLocal *CreateLocal(TopologyEdit &edit, CK_ID graphId, const std::string &name, const CKGUID &type,
                              int column, int row, ParameterStyle style) {
    CKContext *context = edit.Context();
    CKBehavior *graph = BehaviorOf(context, graphId);
    BehaviorData *record = FindRecord(edit.Data(), graphId);
    if (!graph || !record) return nullptr;
    CKParameterLocal *local = nullptr;
    GuardedCall([&] { local = graph->CreateLocalParameter(const_cast<CKSTRING>(name.c_str()), type); }, nullptr,
                "CreateLocalParameter");
    if (!local) return nullptr;
    const CK_ID id = local->GetID();
    const int slot = graph->GetLocalParameterPosition(local);
    edit.Created(id);
    edit.Done(
        [context, graphId, id, slot] {
            CKBehavior *owner = BehaviorOf(context, graphId);
            CKParameterLocal *parameter = CKParameterLocal::Cast(context->GetObject(id));
            if (!owner || !parameter) return false;
            return InsertLocalParameter(owner, slot, parameter);
        },
        [context, graphId, id, slot] {
            CKBehavior *owner = BehaviorOf(context, graphId);
            return owner && owner->RemoveLocalParameter(slot) == context->GetObject(id);
        });
    // Dev-saved local tables do not store the CK_ID (loaded records hold 0);
    // reconstructed data names it.
    Parameter display(edit.Data().generatedFromRuntimeGraph ? id : 0, column, row, style);
    record->localParams.insert(record->localParams.begin() + std::min<size_t>(slot, record->localParams.size()),
                               display);
    return local;
}

bool IsScriptUi(TopologyEdit &edit, const BehaviorData &record) {
    return &record == &edit.Data().rootBehavior && (edit.Script()->GetType() & CKBEHAVIORTYPE_SCRIPT);
}

// Dev gives every input parameter of a dropped Building Block a value: a
// local parameter of the parent graph, named after the input, linked to it
// (PLOCAL -> PIN; every unlinked input in the samples, e.g. 2D Flow Layout).
// Values are the prototype defaults. The local is collapsed one row above
// the input, as the reconstruction places type-9 sources (Dev 0x53A450).
// The Target keeps no source, so it still means the owner object.
bool AddInputValueLocals(TopologyEdit &edit, CK_ID graphId, CKBehavior *behavior, const BehaviorData &placed,
                         const ValueBytes *values) {
    CKContext *context = edit.Context();
    CKBehaviorPrototype *prototype = behavior->GetPrototype();
    BehaviorData *graph = FindRecord(edit.Data(), graphId);
    if (!graph) return false;
    const bool scriptUi = IsScriptUi(edit, *graph);
    for (int i = 0; i < behavior->GetInputParameterCount(); ++i) {
        CKParameterIn *input = behavior->GetInputParameter(i);
        if (!input || input->GetDirectSource() || input->GetSharedSource()) continue;
        const Point pixel = LayoutCalculator::BehaviorInputParameterPosition(placed.rect, i);
        int column = static_cast<int>(std::lround((pixel.h - graph->rect.hPos) / GRID));
        int row = static_cast<int>(std::lround((pixel.v - graph->rect.vPos) / GRID));
        if (scriptUi) {
            column = std::max(column, ParameterDisplayGeometry::SCRIPT_MINIMUM_COLUMN);
            row = std::max(row, 0);
        }
        CKParameterLocal *local =
            CreateLocal(edit, graphId, input->GetName() ? input->GetName() : "", input->GetGUID(), column, row,
                        PARAM_STYLE_COLLAPSED);
        if (!local) return false;
        const auto copied = values ? values->find(i) : ValueBytes::const_iterator();
        if (values && copied != values->end()) {
            // CopyValue uses the type's copy function (pointer-valued types).
            if (CKParameter *from = CKParameter::Cast(context->GetObject(copied->second))) local->CopyValue(from, FALSE);
        } else if (const CKPARAMETER_DESC *description = PrototypeInParameter(prototype, i)) {
            // Raw default bytes of a memory-managing type (2D Curve, ...)
            // are the prototype's own pointer; only its text form is safe.
            if (description->DefaultValueString)
                local->SetStringValue(description->DefaultValueString);
            else if (description->DefaultValue && description->DefaultValueSize > 0 &&
                     !ManagesMemory(context, local->GetType()))
                local->SetValue(description->DefaultValue, description->DefaultValueSize);
        }
        const CK_ID localId = local->GetID();
        const InputSource before = SourceOf(input);
        const InputRef ref = before.input;
        if (!edit.Do(
                [context, ref, localId] {
                    CKParameterIn *target = Resolve(context, ref);
                    CKParameter *source = CKParameter::Cast(context->GetObject(localId));
                    return target && source && target->SetDirectSource(source) == CK_OK;
                },
                [context, before] { return RestoreSource(context, before); }))
            return false;
        graph = FindRecord(edit.Data(), graphId);
        const int slot = static_cast<int>(graph->localParams.size()) - 1;
        graph->links.push_back(Link(0, LINK_TYPE_PARAMETER, LinkEndpoint(graphId, slot, ENDPOINT_PLOCAL),
                                    LinkEndpoint(behavior->GetID(), i, ENDPOINT_PIN)));
    }
    return true;
}

} // namespace detail

EditResult AddLocalParameter(Document &document, CK_ID scriptId, CK_ID graphId,
                             const std::string &name, const CKGUID &type, const Point &position) {
    TopologyEdit edit(document, scriptId, "Add Local Parameter");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKBehavior *graph = BehaviorOf(edit.Context(), graphId);
    BehaviorData *record = FindRecord(edit.Data(), graphId);
    if (!EditableGraph(graph) || !record) return edit.Fail("Local parameters belong to the Script or a behavior graph");
    const Point cell = GridCell(*record, IsScriptUi(edit, *record), position);
    CKParameterLocal *local = CreateLocal(edit, graphId, name, type, static_cast<int>(cell.h),
                                          static_cast<int>(cell.v), PARAM_STYLE_NAME);
    if (!local) return edit.Fail("CKBehavior::CreateLocalParameter failed (unknown type?)");
    return edit.Commit(local->GetID());
}

EditResult AddThisParameter(Document &document, CK_ID scriptId, CK_ID graphId,
                            const Point &position) {
    TopologyEdit edit(document, scriptId, "Add This Parameter");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    CKBehavior *graph = BehaviorOf(context, graphId);
    BehaviorData *record = FindRecord(edit.Data(), graphId);
    if (!EditableGraph(graph) || !record) return edit.Fail("<This> parameters belong to the Script or a behavior graph");
    CKBeObject *owner = edit.Script()->GetOwner();
    CKParameterManager *manager = context->GetParameterManager();
    if (!owner || !manager) return edit.Fail("The Script has no owner");
    const CKGUID type = manager->ClassIDToGuid(owner->GetClassID());
    const Point cell = GridCell(*record, IsScriptUi(edit, *record), position);
    CKParameterLocal *local =
        CreateLocal(edit, graphId, "This", type, static_cast<int>(cell.h), static_cast<int>(cell.v), PARAM_STYLE_NAME);
    if (!local) return edit.Fail("CKBehavior::CreateLocalParameter failed for the owner class");
    local->SetAsMyselfParameter(TRUE);
    return edit.Commit(local->GetID());
}

EditResult AddParameterOperation(Document &document, CK_ID scriptId, CK_ID graphId,
                                 const CKGUID &operationGuid, const CKGUID &result, const CKGUID &input1,
                                 const CKGUID &input2, const Point &position) {
    TopologyEdit edit(document, scriptId, "Add Parameter Operation");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    CKBehavior *graph = BehaviorOf(context, graphId);
    BehaviorData *record = FindRecord(edit.Data(), graphId);
    CKParameterManager *manager = context->GetParameterManager();
    if (!EditableGraph(graph) || !record || !manager)
        return edit.Fail("Parameter operations belong to the Script or a behavior graph");
    CKSTRING name = manager->OperationGuidToName(operationGuid);
    CKParameterOperation *operation = nullptr;
    GuardedCall([&] { operation = context->CreateCKParameterOperation(name, operationGuid, result, input1, input2); },
                &document.Host(), "CreateCKParameterOperation");
    if (!operation) return edit.Fail("CKContext::CreateCKParameterOperation failed");
    const CK_ID id = operation->GetID();
    edit.Created(id);
    const int at = graph->GetParameterOperationCount();
    if (!edit.Do([context, graphId, id, at] { return AttachOperation(context, graphId, id, at); },
                 [context, graphId, id] { return DetachOperation(context, graphId, id); }))
        return edit.Fail("CKBehavior::AddParameterOperation failed");
    record->operations.push_back(Operation(id, SchematicGrid::SnapToGrid(position.h, GRID),
                                           SchematicGrid::SnapToGrid(position.v, GRID)));
    return edit.Commit(id);
}

EditResult AddGraphPin(Document &document, CK_ID scriptId, CK_ID behaviorId, PinKind kind,
                       const std::string &name, const CKGUID &type) {
    TopologyEdit edit(document, scriptId, "Add Pin");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    CKBehavior *behavior = BehaviorOf(context, behaviorId);
    BehaviorData *record = FindRecord(edit.Data(), behaviorId);
    if (!EditableGraph(behavior) || !record || !TreeIds(edit.Script()).count(behaviorId))
        return edit.Fail("Pins can be added to the Script or a behavior graph, not to a Building Block");
    const bool io = kind == PinKind::BehaviorIn || kind == PinKind::BehaviorOut;
    if (io && behaviorId == scriptId) return edit.Fail("A Script has a single Start input and no outputs");
    const std::string label = name.empty() ? (io ? "In" : "pIn") : name;
    CK_ID created = 0;
    switch (kind) {
    case PinKind::BehaviorIn:
    case PinKind::BehaviorOut: {
        const bool input = kind == PinKind::BehaviorIn;
        CKBehaviorIO *added = input ? behavior->CreateInput(const_cast<CKSTRING>(label.c_str()))
                                    : behavior->CreateOutput(const_cast<CKSTRING>(label.c_str()));
        if (!added) return edit.Fail("Cannot add the behavior pin");
        const CK_ID id = added->GetID();
        const int slot = input ? behavior->GetInputCount() - 1 : behavior->GetOutputCount() - 1;
        edit.Created(id);
        edit.Done([context, behaviorId, input, slot, id] { return InsertIo(context, behaviorId, input, slot, id); },
                  [context, behaviorId, input, slot, id] { return RemoveIo(context, behaviorId, input, slot, id); });
        created = id;
        break;
    }
    case PinKind::ParamIn: {
        CKParameterIn *parameter = behavior->CreateInputParameter(const_cast<CKSTRING>(label.c_str()), type);
        if (!parameter) return edit.Fail("CKBehavior::CreateInputParameter failed (unknown type?)");
        const CK_ID id = parameter->GetID();
        const int slot = behavior->GetInputParameterPosition(parameter);
        edit.Created(id);
        edit.Done(
            [context, behaviorId, id, slot] {
                CKBehavior *owner = BehaviorOf(context, behaviorId);
                CKParameterIn *in = CKParameterIn::Cast(context->GetObject(id));
                if (!owner || !in) return false;
                return InsertInputParameter(owner, slot, in);
            },
            [context, behaviorId, id, slot] {
                CKBehavior *owner = BehaviorOf(context, behaviorId);
                return owner && owner->RemoveInputParameter(slot) == context->GetObject(id);
            });
        created = id;
        break;
    }
    case PinKind::ParamOut: {
        CKParameterOut *parameter = behavior->CreateOutputParameter(const_cast<CKSTRING>(label.c_str()), type);
        if (!parameter) return edit.Fail("CKBehavior::CreateOutputParameter failed (unknown type?)");
        const CK_ID id = parameter->GetID();
        const int slot = behavior->GetOutputParameterPosition(parameter);
        edit.Created(id);
        edit.Done(
            [context, behaviorId, id, slot] {
                CKBehavior *owner = BehaviorOf(context, behaviorId);
                CKParameterOut *out = CKParameterOut::Cast(context->GetObject(id));
                if (!owner || !out) return false;
                return InsertOutputParameter(owner, slot, out);
            },
            [context, behaviorId, id, slot] {
                CKBehavior *owner = BehaviorOf(context, behaviorId);
                return owner && owner->RemoveOutputParameter(slot) == context->GetObject(id);
            });
        created = id;
        break;
    }
    default:
        return edit.Fail("Unknown pin kind");
    }
    if (behaviorId != scriptId) {
        const float rows = static_cast<float>(std::max(behavior->GetInputCount(), behavior->GetOutputCount()));
        const float columns = static_cast<float>(
            std::max(behavior->GetInputParameterCount(), behavior->GetOutputParameterCount()) +
            (behavior->IsUsingTarget() ? 1 : 0));
        record = FindRecord(edit.Data(), behaviorId);
        record->rect.vSize = std::max(record->rect.vSize, rows * GRID);
        record->rect.hSize = std::max(record->rect.hSize, columns * GRID);
    }
    return edit.Commit(created);
}

EditResult SetTargetUsed(Document &document, CK_ID scriptId, CK_ID behaviorId, bool use) {
    TopologyEdit edit(document, scriptId, use ? "Use Target" : "Remove Target");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    CKBehavior *behavior = BehaviorOf(context, behaviorId);
    if (!behavior || !TreeIds(edit.Script()).count(behaviorId) || behaviorId == scriptId)
        return edit.Fail("Select a behavior of this Script");
    if (!behavior->IsTargetable()) return edit.Fail("This behavior cannot use a Target");
    if ((behavior->IsUsingTarget() != FALSE) == use) return edit.Fail(use ? "The Target is already used" : "No Target");
    CKBeObject *owner = edit.Script()->GetOwner();
    if (!use && owner && !CKIsChildClassOf(owner, behavior->GetCompatibleClassID()))
        return edit.Fail("The Script owner is not compatible with this behavior; it needs its Target");
    // UseTarget(FALSE) destroys the Target parameter: disconnect it first so
    // undo can reconnect the recreated one (steps name it by slot -2).
    if (!use) {
        CKParameterIn *target = behavior->GetTargetParameter();
        if (target && (target->GetDirectSource() || target->GetSharedSource()) && !ClearSource(edit, target))
            return edit.Fail("Cannot disconnect the Target parameter");
        if (target && !DisconnectParameter(edit, target, {}))
            return edit.Fail("Cannot disconnect the Target parameter");
    }
    if (!edit.Do(
            [context, behaviorId, use] {
                CKBehavior *runtime = BehaviorOf(context, behaviorId);
                return runtime && runtime->UseTarget(use) == CK_OK;
            },
            [context, behaviorId, use] {
                CKBehavior *runtime = BehaviorOf(context, behaviorId);
                return runtime && runtime->UseTarget(!use) == CK_OK;
            }))
        return edit.Fail("CKBehavior::UseTarget failed");
    return edit.Commit();
}

EditResult RenameObject(Document &document, CK_ID scriptId, CK_ID objectId,
                        const std::string &name) {
    TopologyEdit edit(document, scriptId, "Rename");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    CKObject *object = context->GetObject(objectId);
    if (!object) return edit.Fail("Object not found");
    const std::string before = NameOf(object);
    if (before == name) return edit.Unchanged("The name is unchanged");
    auto rename = [context, objectId](const std::string &text) {
        CKObject *runtime = context->GetObject(objectId);
        if (!runtime) return false;
        runtime->SetName(const_cast<CKSTRING>(text.c_str()));
        return true;
    };
    edit.Do([rename, name] { return rename(name); }, [rename, before] { return rename(before); });
    return edit.Commit();
}

EditResult SetBehaviorPriority(Document &document, CK_ID scriptId, CK_ID behaviorId,
                               int priority) {
    TopologyEdit edit(document, scriptId, "Set Priority");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    CKBehavior *behavior = BehaviorOf(context, behaviorId);
    // The Script itself too (Dev's Script header menu: Edit Priority).
    if (!behavior || (behaviorId != scriptId && !TreeIds(edit.Script()).count(behaviorId)))
        return edit.Fail("Select a behavior of this Script");
    const int before = behavior->GetPriority();
    if (before == priority) return edit.Unchanged("The priority is unchanged");
    auto set = [context, behaviorId](int value) {
        CKBehavior *runtime = BehaviorOf(context, behaviorId);
        if (!runtime) return false;
        SetPriorityKeepingTies(runtime, value);
        return true;
    };
    edit.Do([set, priority] { return set(priority); }, [set, before] { return set(before); });
    return edit.Commit();
}

EditResult SetLinkDelay(Document &document, CK_ID scriptId, CK_ID linkId, int delay) {
    TopologyEdit edit(document, scriptId, "Set Link Delay");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    CKBehaviorLink *link = CKBehaviorLink::Cast(context->GetObject(linkId));
    if (!link) return edit.Fail("Behavior link not found");
    // CKBehaviorLink::SetInitialActivationDelay: 0 <= delay < 32765.
    delay = std::max(0, std::min(32764, delay));
    const int before = link->GetInitialActivationDelay();
    if (before == delay) return edit.Unchanged("The delay is unchanged");
    auto set = [context, linkId](int value) {
        CKBehaviorLink *runtime = CKBehaviorLink::Cast(context->GetObject(linkId));
        if (!runtime) return false;
        runtime->SetInitialActivationDelay(value);
        runtime->SetActivationDelay(value);
        return true;
    };
    edit.Do([set, delay] { return set(delay); }, [set, before] { return set(before); });
    return edit.Commit();
}

EditResult DeleteElements(Document &document, CK_ID scriptId,
                          const std::vector<ElementRef> &elements) {
    TopologyEdit edit(document, scriptId, "Delete");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    InterfaceData &data = edit.Data();
    const std::set<CK_ID> tree = TreeIds(edit.Script());

    std::set<CK_ID> behaviors, operations;
    std::set<std::pair<CK_ID, int>> locals, shortcuts, comments, linkSlots;
    std::set<std::tuple<CK_ID, int, int>> points;
    std::set<std::tuple<CK_ID, int, int>> pins; // owner, PinKind, slot
    for (const auto &ref : elements) {
        switch (ref.kind) {
        case ElementKind::Behavior:
            if (ref.object != scriptId && tree.count(ref.object)) behaviors.insert(ref.object);
            break;
        case ElementKind::Operation: operations.insert(ref.object); break;
        case ElementKind::LocalParam: locals.insert({ref.owner, ref.index}); break;
        case ElementKind::Shortcut: shortcuts.insert({ref.owner, ref.index}); break;
        case ElementKind::Comment: comments.insert({ref.owner, ref.index}); break;
        case ElementKind::Link: linkSlots.insert({ref.owner, ref.index}); break;
        case ElementKind::ControlPoint: points.insert({ref.owner, ref.index, ref.sub}); break;
        case ElementKind::Pin: pins.insert({ref.object, static_cast<int>(ref.pin), ref.index}); break;
        default: break;
        }
    }
    // A selected graph takes its selected descendants with it.
    for (auto it = behaviors.begin(); it != behaviors.end();) {
        bool nested = false;
        for (CK_ID parent = ParentOf(data, *it); parent && !nested; parent = ParentOf(data, parent))
            nested = behaviors.count(parent) != 0;
        it = nested ? behaviors.erase(it) : std::next(it);
    }
    for (const auto &shortcut : shortcuts) {
        const BehaviorData *record = FindRecord(data, shortcut.first);
        if (!record) continue;
        for (int i = 0; i < static_cast<int>(record->links.size()); ++i)
            for (const LinkEndpoint *endpoint : {&record->links[i].start, &record->links[i].end})
                if (IsEndpoint(*endpoint, ENDPOINT_POUT_SHORTCUT, shortcut.first, shortcut.second))
                    linkSlots.insert({shortcut.first, i});
    }

    // Objects leaving the graph, and everything they own.
    std::set<CK_ID> roots = behaviors;
    roots.insert(operations.begin(), operations.end());
    for (const auto &local : locals) {
        CKBehavior *graph = BehaviorOf(context, local.first);
        if (graph && local.second >= 0 && local.second < graph->GetLocalParameterCount())
            roots.insert(graph->GetLocalParameter(local.second)->GetID());
    }
    const std::set<CK_ID> owned = CollectOwnedObjects(context, roots);
    std::string error;

    // 1. Selected links.
    std::set<std::pair<CK_ID, CK_ID>> behaviorLinks; // graph, link
    for (const auto &slot : linkSlots) {
        const BehaviorData *record = FindRecord(data, slot.first);
        if (!record || slot.second < 0 || slot.second >= static_cast<int>(record->links.size())) continue;
        const Link &link = record->links[slot.second];
        if (link.IsBehaviorLink()) {
            if (CKBehaviorLink::Cast(context->GetObject(link.id))) behaviorLinks.insert({slot.first, link.id});
        } else if (!DisconnectParameterLink(edit, *record, link, error)) {
            return edit.Fail(error.empty() ? "Cannot remove the parameter link" : error);
        }
    }
    // 2. Behaviors: refuse exported parameters; collect their behavior links.
    for (CK_ID id : behaviors) {
        CKBehavior *behavior = BehaviorOf(context, id);
        CKBehavior *parent = BehaviorOf(context, ParentOf(data, id));
        if (!behavior || !parent) return edit.Fail("Behavior parent not found");
        for (int i = 0; i < behavior->GetInputParameterCount(); ++i)
            if (parent->GetInputParameterPosition(behavior->GetInputParameter(i)) >= 0)
                return edit.Fail("A behavior whose parameters are exported to its graph cannot be deleted yet");
        for (int i = 0; i < behavior->GetOutputParameterCount(); ++i)
            if (parent->GetOutputParameterPosition(behavior->GetOutputParameter(i)) >= 0)
                return edit.Fail("A behavior whose parameters are exported to its graph cannot be deleted yet");
        for (int i = 0; i < parent->GetSubBehaviorLinkCount(); ++i) {
            CKBehaviorLink *link = parent->GetSubBehaviorLink(i);
            CKBehaviorIO *in = link ? link->GetInBehaviorIO() : nullptr;
            CKBehaviorIO *out = link ? link->GetOutBehaviorIO() : nullptr;
            if ((in && in->GetOwner() == behavior) || (out && out->GetOwner() == behavior))
                behaviorLinks.insert({parent->GetID(), link->GetID()});
        }
    }
    // 3. References from outside to removed parameters.
    for (CK_ID id : owned) {
        CKObject *object = context->GetObject(id);
        if ((CKParameterIn::Cast(object) || CKParameter::Cast(object)) && !DisconnectParameter(edit, object, owned))
            return edit.Fail("Cannot disconnect a parameter of a deleted element");
    }
    // 3b. Pins of editable graphs: disconnect, then detach (step 4b).
    for (const auto &pin : pins) {
        const CK_ID ownerId = std::get<0>(pin);
        const PinKind kind = static_cast<PinKind>(std::get<1>(pin));
        const int slot = std::get<2>(pin);
        CKBehavior *owner = BehaviorOf(context, ownerId);
        if (!EditableGraph(owner) || !tree.count(ownerId) || behaviors.count(ownerId))
            return edit.Fail("Only the pins of the Script or of a behavior graph can be deleted");
        if (kind == PinKind::BehaviorIn || kind == PinKind::BehaviorOut) {
            if (ownerId == scriptId) return edit.Fail("The Script's Start input cannot be deleted");
            const bool input = kind == PinKind::BehaviorIn;
            CKBehaviorIO *io = input ? owner->GetInput(slot) : owner->GetOutput(slot);
            if (!io) return edit.Fail("Pin not found");
            for (CKBehavior *graph : {owner, BehaviorOf(context, ParentOf(data, ownerId))}) {
                for (int i = 0; graph && i < graph->GetSubBehaviorLinkCount(); ++i) {
                    CKBehaviorLink *link = graph->GetSubBehaviorLink(i);
                    if (link && (link->GetInBehaviorIO() == io || link->GetOutBehaviorIO() == io))
                        behaviorLinks.insert({graph->GetID(), link->GetID()});
                }
            }
        } else if (kind == PinKind::ParamIn || kind == PinKind::ParamOut) {
            CKObject *parameter = kind == PinKind::ParamIn ? static_cast<CKObject *>(owner->GetInputParameter(slot))
                                                           : static_cast<CKObject *>(owner->GetOutputParameter(slot));
            CKObject *parameterOwner = CKParameterIn::Cast(parameter) ? CKParameterIn::Cast(parameter)->GetOwner()
                                     : CKParameter::Cast(parameter)   ? CKParameter::Cast(parameter)->GetOwner()
                                                                      : nullptr;
            if (!parameter) return edit.Fail("Pin not found");
            if (parameterOwner != owner) return edit.Fail("An exported parameter cannot be deleted here");
            if (CKParameterIn *input = CKParameterIn::Cast(parameter))
                if ((input->GetDirectSource() || input->GetSharedSource()) && !ClearSource(edit, input))
                    return edit.Fail("Cannot disconnect the parameter");
            if (!DisconnectParameter(edit, parameter, {}))
                return edit.Fail("Cannot disconnect the parameter");
        } else {
            return edit.Fail("The Target is removed with Use/Remove Target (T)");
        }
    }

    // 4. Detach, in an order whose reverse restores positions.
    for (const auto &entry : behaviorLinks) {
        const CK_ID graphId = entry.first, linkId = entry.second;
        CKBehavior *graph = BehaviorOf(context, graphId);
        CKBehaviorLink *link = CKBehaviorLink::Cast(context->GetObject(linkId));
        const int at = graph && link ? LinkPosition(graph, link) : -1;
        if (at < 0) continue;
        const IoRef in = IoRefOf(link->GetInBehaviorIO()), out = IoRefOf(link->GetOutBehaviorIO());
        if (!edit.Do([context, graphId, linkId] { return DetachLink(context, graphId, linkId); },
                     [context, graphId, linkId, at, in, out] {
                         return AttachLink(context, graphId, linkId, at, in, out);
                     }))
            return edit.Fail("CKBehavior::RemoveSubBehaviorLink failed");
        edit.Bury(linkId);
    }
    for (auto it = locals.rbegin(); it != locals.rend(); ++it) {
        const CK_ID graphId = it->first;
        const int slot = it->second;
        CKBehavior *graph = BehaviorOf(context, graphId);
        if (!graph || slot < 0 || slot >= graph->GetLocalParameterCount()) continue;
        const CK_ID localId = graph->GetLocalParameter(slot)->GetID();
        if (!edit.Do(
                [context, graphId, slot, localId] {
                    CKBehavior *owner = BehaviorOf(context, graphId);
                    return owner && owner->RemoveLocalParameter(slot) == context->GetObject(localId);
                },
                [context, graphId, slot, localId] {
                    CKBehavior *owner = BehaviorOf(context, graphId);
                    CKParameterLocal *local = CKParameterLocal::Cast(context->GetObject(localId));
                    if (!owner || !local) return false;
                    return InsertLocalParameter(owner, slot, local);
                }))
            return edit.Fail("CKBehavior::RemoveLocalParameter failed");
        edit.Bury(localId);
    }
    for (CK_ID operationId : operations) {
        CKParameterOperation *operation = CKParameterOperation::Cast(context->GetObject(operationId));
        CKBehavior *graph = operation ? operation->GetOwner() : nullptr;
        if (!graph) continue;
        const CK_ID graphId = graph->GetID();
        const int at = OperationPosition(graph, operation);
        if (!edit.Do([context, graphId, operationId] { return DetachOperation(context, graphId, operationId); },
                     [context, graphId, operationId, at] {
                         return AttachOperation(context, graphId, operationId, at);
                     }))
            return edit.Fail("CKBehavior::RemoveParameterOperation failed");
        edit.Bury(operationId);
    }
    for (CK_ID id : behaviors) {
        const CK_ID parentId = ParentOf(data, id);
        CKBehavior *parent = BehaviorOf(context, parentId);
        const int at = parent ? SubBehaviorPosition(parent, BehaviorOf(context, id)) : -1;
        if (at < 0) return edit.Fail("Behavior is not attached to its graph");
        if (!edit.Do([context, parentId, id] { return DetachSubBehavior(context, parentId, id); },
                     [context, parentId, id, at] { return AttachSubBehavior(context, parentId, id, at); }))
            return edit.Fail("CKBehavior::RemoveSubBehavior failed");
        edit.Bury(id);
    }

    for (auto it = pins.rbegin(); it != pins.rend(); ++it) {
        const CK_ID ownerId = std::get<0>(*it);
        const PinKind kind = static_cast<PinKind>(std::get<1>(*it));
        const int slot = std::get<2>(*it);
        CKBehavior *owner = BehaviorOf(context, ownerId);
        bool ok = false;
        if (kind == PinKind::BehaviorIn || kind == PinKind::BehaviorOut) {
            const bool input = kind == PinKind::BehaviorIn;
            CKBehaviorIO *io = input ? owner->GetInput(slot) : owner->GetOutput(slot);
            if (!io) return edit.Fail("Cannot remove the behavior pin");
            const CK_ID id = io->GetID();
            ok = edit.Do([context, ownerId, input, slot, id] { return RemoveIo(context, ownerId, input, slot, id); },
                         [context, ownerId, input, slot, id] { return InsertIo(context, ownerId, input, slot, id); });
            if (ok) edit.Bury(id);
        } else if (kind == PinKind::ParamIn) {
            const CK_ID id = owner->GetInputParameter(slot)->GetID();
            ok = edit.Do(
                [context, ownerId, slot, id] {
                    CKBehavior *runtime = BehaviorOf(context, ownerId);
                    return runtime && runtime->RemoveInputParameter(slot) == context->GetObject(id);
                },
                [context, ownerId, slot, id] {
                    CKBehavior *runtime = BehaviorOf(context, ownerId);
                    CKParameterIn *in = CKParameterIn::Cast(context->GetObject(id));
                    if (!runtime || !in) return false;
                    return InsertInputParameter(runtime, slot, in);
                });
            if (ok) edit.Bury(id);
        } else {
            const CK_ID id = owner->GetOutputParameter(slot)->GetID();
            ok = edit.Do(
                [context, ownerId, slot, id] {
                    CKBehavior *runtime = BehaviorOf(context, ownerId);
                    return runtime && runtime->RemoveOutputParameter(slot) == context->GetObject(id);
                },
                [context, ownerId, slot, id] {
                    CKBehavior *runtime = BehaviorOf(context, ownerId);
                    CKParameterOut *out = CKParameterOut::Cast(context->GetObject(id));
                    if (!runtime || !out) return false;
                    return InsertOutputParameter(runtime, slot, out);
                });
            if (ok) edit.Bury(id);
        }
        if (!ok) return edit.Fail("Cannot remove the pin");
    }

    // 5. Display records addressed by slot, highest slots first.
    for (auto it = points.rbegin(); it != points.rend(); ++it) {
        BehaviorData *record = FindRecord(data, std::get<0>(*it));
        const int slot = std::get<1>(*it);
        if (record && slot >= 0 && slot < static_cast<int>(record->links.size()))
            record->links[slot].RemoveControlPoint(std::get<2>(*it));
    }
    for (auto it = comments.rbegin(); it != comments.rend(); ++it) {
        BehaviorData *record = FindRecord(data, it->first);
        if (record && it->second >= 0 && it->second < static_cast<int>(record->comments.size()))
            record->comments.erase(record->comments.begin() + it->second);
    }
    for (auto it = linkSlots.rbegin(); it != linkSlots.rend(); ++it) {
        BehaviorData *record = FindRecord(data, it->first);
        if (record && it->second >= 0 && it->second < static_cast<int>(record->links.size()))
            record->links.erase(record->links.begin() + it->second);
    }
    for (auto it = locals.rbegin(); it != locals.rend(); ++it)
        if (BehaviorData *record = FindRecord(data, it->first)) RemoveLocalSlot(data, *record, it->second);
    for (auto it = shortcuts.rbegin(); it != shortcuts.rend(); ++it)
        if (BehaviorData *record = FindRecord(data, it->first)) RemoveSharedSlot(*record, it->second);
    for (auto it = pins.rbegin(); it != pins.rend(); ++it)
        RemovePinSlot(data, std::get<0>(*it), static_cast<PinKind>(std::get<1>(*it)), std::get<2>(*it));
    return edit.Commit();
}

// PasteBehaviors: TopologyPaste.cpp.

namespace {

std::vector<OperationSignature> LinkOperations(Document &document, CK_ID scriptId, const LinkEndpoint &a,
                                               const LinkEndpoint &b, bool conversions) {
    std::vector<OperationSignature> result;
    ScriptSession *session = document.Session(scriptId);
    CKContext *context = document.Context();
    CKParameterManager *manager = context ? context->GetParameterManager() : nullptr;
    if (!session || !manager) return result;
    const InterfaceData &data = session->Data();
    LinkRole source, destination;
    bool found = false;
    for (int order = 0; order < 2 && !found; ++order) {
        const LinkEndpoint &from = order == 0 ? a : b;
        const LinkEndpoint &to = order == 0 ? b : a;
        found = ParameterSource(data, context, from, source) && ParameterDestination(data, context, to, destination) &&
                source.graph == destination.graph && source.object != destination.object;
    }
    CKParameter *from = found ? CKParameter::Cast(source.object) : nullptr;
    CKParameterIn *to = found ? CKParameterIn::Cast(destination.object) : nullptr;
    if (!from || !to) return result;
    const CKParameterType sourceType = from->GetType(), destinationType = to->GetType();
    if (conversions && manager->IsTypeCompatible(sourceType, destinationType)) return result;
    for (const auto &operation : ListParameterOperations(context))
        for (const auto &signature : ListOperationSignatures(context, operation.guid)) {
            if (conversions && signature.input2 != CKPGUID_NONE) continue;
            const CKParameterType in1 = manager->ParameterGuidToType(signature.input1);
            const CKParameterType out = manager->ParameterGuidToType(signature.result);
            if (in1 < 0 || out < 0) continue;
            if (manager->IsTypeCompatible(sourceType, in1) && manager->IsTypeCompatible(out, destinationType))
                result.push_back(signature);
        }
    return result;
}

} // namespace

std::vector<OperationSignature> ParameterLinkConversions(Document &document, CK_ID scriptId,
                                                         const LinkEndpoint &a, const LinkEndpoint &b) {
    return LinkOperations(document, scriptId, a, b, true);
}

std::vector<OperationSignature> ParameterLinkOperations(Document &document, CK_ID scriptId,
                                                        const LinkEndpoint &a, const LinkEndpoint &b) {
    return LinkOperations(document, scriptId, a, b, false);
}

} // namespace vdev
