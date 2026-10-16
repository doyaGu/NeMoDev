// Grouping commands: ungroup a behavior graph (Dev Shift+Del) and group
// behaviors into a new graph (Dev G). See TopologyCommands.h.
#include "core/graph/detail/TopologyInternal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "core/schematic/BehaviorDisplayGeometry.h"
#include "core/schematic/LayoutCalculator.h"
#include "core/schematic/ParameterDisplayGeometry.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Document.h"
#include "core/schematic/SchematicEdits.h"

namespace vdev {

using namespace detail;

namespace {

int ClampDelay(int delay) {
    return std::max(0, std::min(32764, delay));
}

// Creates a behavior link from in to out in graph (attached at the end).
CK_ID CreateLink(TopologyEdit &edit, CK_ID graphId, CKBehaviorIO *in, CKBehaviorIO *out, int delay) {
    CKContext *context = edit.Context();
    CKBehavior *graph = BehaviorOf(context, graphId);
    CKBehaviorLink *link = CKBehaviorLink::Cast(context->CreateObject(CKCID_BEHAVIORLINK));
    if (!graph || !link || !in || !out) return 0;
    const CK_ID id = link->GetID();
    edit.Created(id);
    link->SetInitialActivationDelay(ClampDelay(delay));
    link->SetActivationDelay(ClampDelay(delay));
    const IoRef inRef = IoRefOf(in), outRef = IoRefOf(out);
    const int at = graph->GetSubBehaviorLinkCount();
    if (!edit.Do([context, graphId, id, at, inRef, outRef] { return AttachLink(context, graphId, id, at, inRef, outRef); },
                 [context, graphId, id] { return DetachLink(context, graphId, id); }))
        return 0;
    return id;
}

bool MoveLink(TopologyEdit &edit, CK_ID from, CK_ID to, CKBehaviorLink *link) {
    CKContext *context = edit.Context();
    CKBehavior *source = BehaviorOf(context, from);
    CKBehavior *target = BehaviorOf(context, to);
    if (!source || !target || !link) return false;
    const CK_ID id = link->GetID();
    const IoRef in = IoRefOf(link->GetInBehaviorIO()), out = IoRefOf(link->GetOutBehaviorIO());
    const int sourceAt = LinkPosition(source, link);
    const int targetAt = target->GetSubBehaviorLinkCount();
    return edit.Do(
        [context, from, to, id, targetAt, in, out] {
            return DetachLink(context, from, id) && AttachLink(context, to, id, targetAt, in, out);
        },
        [context, from, to, id, sourceAt, in, out] {
            return DetachLink(context, to, id) && AttachLink(context, from, id, sourceAt, in, out);
        });
}

bool BuryLink(TopologyEdit &edit, CK_ID graphId, CKBehaviorLink *link) {
    CKContext *context = edit.Context();
    CKBehavior *graph = BehaviorOf(context, graphId);
    if (!graph || !link) return false;
    const CK_ID id = link->GetID();
    const IoRef in = IoRefOf(link->GetInBehaviorIO()), out = IoRefOf(link->GetOutBehaviorIO());
    const int at = LinkPosition(graph, link);
    if (!edit.Do([context, graphId, id] { return DetachLink(context, graphId, id); },
                 [context, graphId, id, at, in, out] { return AttachLink(context, graphId, id, at, in, out); }))
        return false;
    edit.Bury(id);
    return true;
}

// Sets an input's source: a parameter (direct) or an input (shared), or none.
bool SetSource(TopologyEdit &edit, CKParameterIn *input, CKParameter *direct, CKParameterIn *shared) {
    CKContext *context = edit.Context();
    const InputSource before = SourceOf(input);
    const InputRef ref = before.input;
    const CK_ID directId = direct ? direct->GetID() : 0;
    const InputRef sharedRef = RefOf(shared);
    return edit.Do(
        [context, ref, directId, sharedRef] {
            CKParameterIn *target = Resolve(context, ref);
            if (!target) return false;
            if (sharedRef.id) {
                CKParameterIn *from = Resolve(context, sharedRef);
                return from && target->ShareSourceWith(from) == CK_OK;
            }
            CKParameter *from = directId ? CKParameter::Cast(context->GetObject(directId)) : nullptr;
            return target->SetDirectSource(from) == CK_OK;
        },
        [context, before] { return RestoreSource(context, before); });
}

bool AddDestination(TopologyEdit &edit, CKParameterOut *output, CKParameter *destination) {
    CKContext *context = edit.Context();
    const std::vector<CK_ID> before = DestinationsOf(output);
    const CK_ID outputId = output->GetID(), destinationId = destination->GetID();
    return edit.Do(
        [context, outputId, destinationId] {
            CKParameterOut *from = CKParameterOut::Cast(context->GetObject(outputId));
            CKParameter *to = CKParameter::Cast(context->GetObject(destinationId));
            return from && to && from->AddDestination(to, FALSE) == CK_OK;
        },
        [context, outputId, before] { return RestoreDestinations(context, outputId, before); });
}

// Moves a local parameter between graphs (list and owner).
bool MoveLocal(TopologyEdit &edit, CK_ID from, CK_ID to, CKParameterLocal *local) {
    CKContext *context = edit.Context();
    CKBehavior *source = BehaviorOf(context, from);
    CKBehavior *target = BehaviorOf(context, to);
    if (!source || !target || !local) return false;
    const CK_ID id = local->GetID();
    const int sourceAt = source->GetLocalParameterPosition(local);
    const int targetAt = target->GetLocalParameterCount();
    auto move = [context, id](CK_ID fromId, int fromAt, CK_ID toId, int toAt) {
        CKBehavior *a = BehaviorOf(context, fromId);
        CKBehavior *b = BehaviorOf(context, toId);
        CKParameterLocal *parameter = CKParameterLocal::Cast(context->GetObject(id));
        if (!a || !b || !parameter || a->RemoveLocalParameter(fromAt) != parameter) return false;
        if (!InsertLocalParameter(b, toAt, parameter)) return false;
        parameter->SetOwner(b);
        return b->GetLocalParameterPosition(parameter) == toAt;
    };
    return edit.Do([move, from, sourceAt, to, targetAt] { return move(from, sourceAt, to, targetAt); },
                   [move, from, sourceAt, to, targetAt] { return move(to, targetAt, from, sourceAt); });
}

bool MoveOperation(TopologyEdit &edit, CK_ID from, CK_ID to, CKParameterOperation *operation) {
    CKContext *context = edit.Context();
    CKBehavior *source = BehaviorOf(context, from);
    CKBehavior *target = BehaviorOf(context, to);
    if (!source || !target || !operation) return false;
    const CK_ID id = operation->GetID();
    const int sourceAt = OperationPosition(source, operation);
    const int targetAt = target->GetParameterOperationCount();
    return edit.Do(
        [context, from, to, id, targetAt] {
            return DetachOperation(context, from, id) && AttachOperation(context, to, id, targetAt);
        },
        [context, from, to, id, sourceAt] {
            return DetachOperation(context, to, id) && AttachOperation(context, from, id, sourceAt);
        });
}

bool MoveBehavior(TopologyEdit &edit, CK_ID from, CK_ID to, CK_ID child) {
    CKContext *context = edit.Context();
    CKBehavior *source = BehaviorOf(context, from);
    CKBehavior *target = BehaviorOf(context, to);
    CKBehavior *behavior = BehaviorOf(context, child);
    if (!source || !target || !behavior || !OwnerAccepts(target, behavior)) return false;
    const int sourceAt = SubBehaviorPosition(source, behavior);
    const int targetAt = target->GetSubBehaviorCount();
    return edit.Do(
        [context, from, to, child, targetAt] {
            return DetachSubBehavior(context, from, child) && AttachSubBehavior(context, to, child, targetAt);
        },
        [context, from, to, child, sourceAt] {
            return DetachSubBehavior(context, to, child) && AttachSubBehavior(context, from, child, sourceAt);
        });
}

// Endpoint of a parameter as a link source displayed by graph; a shortcut
// display is added when graph does not show the parameter itself.
bool SourceEndpoint(TopologyEdit &edit, CK_ID graphId, CKObject *source, const Point &anchor, LinkEndpoint &endpoint) {
    CKContext *context = edit.Context();
    InterfaceData &data = edit.Data();
    BehaviorData *graph = FindRecord(data, graphId);
    if (!graph || !source) return false;
    if (CKParameterIn *input = CKParameterIn::Cast(source)) {
        CKBehavior *owner = CKBehavior::Cast(input->GetOwner());
        if (!owner || owner->GetID() != graphId) return false;
        endpoint = LinkEndpoint(graphId, owner->GetInputParameterPosition(input), ENDPOINT_PIN);
        return endpoint.index >= 0;
    }
    CKParameter *parameter = CKParameter::Cast(source);
    if (!parameter) return false;
    CKObject *owner = parameter->GetOwner();
    if (CKParameterLocal *local = CKParameterLocal::Cast(parameter)) {
        CKBehavior *localOwner = CKBehavior::Cast(owner);
        const int slot = localOwner ? localOwner->GetLocalParameterPosition(local) : -1;
        if (localOwner && localOwner->GetID() == graphId && slot >= 0) {
            endpoint = LinkEndpoint(graphId, slot, ENDPOINT_PLOCAL);
            return true;
        }
    } else if (CKParameterOut *output = CKParameterOut::Cast(parameter)) {
        if (CKBehavior *behavior = CKBehavior::Cast(owner)) {
            if (ParentOf(data, behavior->GetID()) == graphId) {
                endpoint = LinkEndpoint(behavior->GetID(), behavior->GetOutputParameterPosition(output), ENDPOINT_POUT);
                return endpoint.index >= 0;
            }
        } else if (CKParameterOperation *operation = CKParameterOperation::Cast(owner)) {
            if (operation->GetOwner() && operation->GetOwner()->GetID() == graphId) {
                endpoint = LinkEndpoint(operation->GetID(), 0, ENDPOINT_POUT);
                return true;
            }
        }
    }
    // Shortcut of the parameter in this graph (reused when present).
    for (int i = 0; i < static_cast<int>(graph->sharedParams.size()); ++i)
        if (graph->sharedParams[i].sourceId == parameter->GetID()) {
            endpoint = LinkEndpoint(graphId, i, ENDPOINT_POUT_SHORTCUT);
            return true;
        }
    const bool scriptUi = IsScriptUi(edit, *graph);
    int column = static_cast<int>(std::floor((anchor.h - graph->rect.hPos) / GRID));
    int row = static_cast<int>(std::floor((anchor.v - graph->rect.vPos) / GRID));
    if (scriptUi) {
        column = std::max(column, ParameterDisplayGeometry::SCRIPT_MINIMUM_COLUMN);
        row = std::max(row, 0);
    }
    // A shortcut's own id is not stored (it reads back 0); sourceId names the parameter.
    Parameter shortcut(0, column, row, PARAM_STYLE_NAME);
    shortcut.sourceId = parameter->GetID();
    graph->sharedParams.push_back(shortcut);
    endpoint = LinkEndpoint(graphId, static_cast<int>(graph->sharedParams.size()) - 1, ENDPOINT_POUT_SHORTCUT);
    return true;
}

// Endpoint of an input (or a graph's own output, or a local) as a link
// destination displayed by graph.
bool DestinationEndpoint(TopologyEdit &edit, CK_ID graphId, CKObject *destination, LinkEndpoint &endpoint) {
    const InterfaceData &data = edit.Data();
    if (CKParameterIn *input = CKParameterIn::Cast(destination)) {
        CKObject *owner = input->GetOwner();
        if (CKBehavior *behavior = CKBehavior::Cast(owner)) {
            if (ParentOf(data, behavior->GetID()) != graphId) return false;
            if (behavior->GetTargetParameter() == input) {
                endpoint = LinkEndpoint(behavior->GetID(), -2, ENDPOINT_TARGET_PIN);
                return true;
            }
            endpoint = LinkEndpoint(behavior->GetID(), behavior->GetInputParameterPosition(input), ENDPOINT_PIN);
            return endpoint.index >= 0;
        }
        if (CKParameterOperation *operation = CKParameterOperation::Cast(owner)) {
            if (!operation->GetOwner() || operation->GetOwner()->GetID() != graphId) return false;
            endpoint = LinkEndpoint(operation->GetID(), operation->GetInParameter1() == input ? 0 : 1, ENDPOINT_PIN);
            return true;
        }
        return false;
    }
    CKParameter *parameter = CKParameter::Cast(destination);
    CKBehavior *owner = parameter ? CKBehavior::Cast(parameter->GetOwner()) : nullptr;
    if (!owner || owner->GetID() != graphId) return false;
    if (CKParameterLocal *local = CKParameterLocal::Cast(parameter)) {
        endpoint = LinkEndpoint(graphId, owner->GetLocalParameterPosition(local), ENDPOINT_PLOCAL);
        return endpoint.index >= 0;
    }
    if (CKParameterOut *output = CKParameterOut::Cast(parameter)) {
        endpoint = LinkEndpoint(graphId, owner->GetOutputParameterPosition(output), ENDPOINT_POUT);
        return endpoint.index >= 0;
    }
    return false;
}

void AddParameterLink(TopologyEdit &edit, CK_ID graphId, const LinkEndpoint &start, const LinkEndpoint &end) {
    if (BehaviorData *record = FindRecord(edit.Data(), graphId))
        record->links.push_back(Link(0, LINK_TYPE_PARAMETER, start, end));
}

std::set<CK_ID> Subtree(const InterfaceData &data, CK_ID root) {
    std::set<CK_ID> ids{root};
    bool grew = true;
    while (grew) {
        grew = false;
        for (const auto &record : data.behaviors)
            if (!ids.count(record.id) && ids.count(record.parentId)) grew = ids.insert(record.id).second || grew;
    }
    return ids;
}

} // namespace

EditResult UngroupGraph(Document &document, CK_ID scriptId, CK_ID graphId) {
    TopologyEdit edit(document, scriptId, "Delete Graph, Keep Contents");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    InterfaceData &data = edit.Data();
    CKBehavior *graph = BehaviorOf(context, graphId);
    const CK_ID parentId = ParentOf(data, graphId);
    CKBehavior *parent = BehaviorOf(context, parentId);
    if (!EditableGraph(graph) || graphId == scriptId || !parent || !EditableGraph(parent) ||
        !TreeIds(edit.Script()).count(graphId))
        return edit.Fail("Select a behavior graph (not a Building Block) inside the Script");
    BehaviorData *graphRecord = FindRecord(data, graphId);
    BehaviorData *parentRecord = FindRecord(data, parentId);
    if (!graphRecord || !parentRecord) return edit.Fail("Graph record not found");
    for (int i = 0; i < graph->GetInputParameterCount(); ++i)
        if (graph->GetInputParameter(i)->GetOwner() != graph)
            return edit.Fail("Graphs with exported parameters cannot be ungrouped yet");
    for (int i = 0; i < graph->GetOutputParameterCount(); ++i)
        if (graph->GetOutputParameter(i)->GetOwner() != graph)
            return edit.Fail("Graphs with exported parameters cannot be ungrouped yet");
    const Point graphOrigin(graphRecord->rect.hPos, graphRecord->rect.vPos);

    // --- Behavior links through the graph's own IOs become direct links.
    std::vector<CKBehaviorLink *> outsideIn, outsideOut, inside;
    for (int i = 0; i < parent->GetSubBehaviorLinkCount(); ++i) {
        CKBehaviorLink *link = parent->GetSubBehaviorLink(i);
        if (link && link->GetOutBehaviorIO() && link->GetOutBehaviorIO()->GetOwner() == graph) outsideIn.push_back(link);
        if (link && link->GetInBehaviorIO() && link->GetInBehaviorIO()->GetOwner() == graph) outsideOut.push_back(link);
    }
    for (int i = 0; i < graph->GetSubBehaviorLinkCount(); ++i)
        if (CKBehaviorLink *link = graph->GetSubBehaviorLink(i)) inside.push_back(link);
    struct NewLink {
        CKBehaviorIO *in;
        CKBehaviorIO *out;
        int delay;
    };
    std::vector<NewLink> combined;
    std::vector<CKBehaviorLink *> moved;
    for (CKBehaviorLink *link : inside) {
        CKBehaviorIO *in = link->GetInBehaviorIO(), *out = link->GetOutBehaviorIO();
        const bool fromGraph = in && in->GetOwner() == graph, toGraph = out && out->GetOwner() == graph;
        const int delay = link->GetInitialActivationDelay();
        if (!fromGraph && !toGraph) {
            moved.push_back(link);
        } else if (fromGraph && !toGraph) { // X -> graph input -> child
            for (CKBehaviorLink *before : outsideIn)
                if (before->GetOutBehaviorIO() == in)
                    combined.push_back({before->GetInBehaviorIO(), out, before->GetInitialActivationDelay() + delay});
        } else if (!fromGraph && toGraph) { // child -> graph output -> Y
            for (CKBehaviorLink *after : outsideOut)
                if (after->GetInBehaviorIO() == out)
                    combined.push_back({in, after->GetOutBehaviorIO(), delay + after->GetInitialActivationDelay()});
        } else { // pass-through
            for (CKBehaviorLink *before : outsideIn)
                if (before->GetOutBehaviorIO() == in)
                    for (CKBehaviorLink *after : outsideOut)
                        if (after->GetInBehaviorIO() == out)
                            combined.push_back({before->GetInBehaviorIO(), after->GetOutBehaviorIO(),
                                                before->GetInitialActivationDelay() + delay +
                                                    after->GetInitialActivationDelay()});
        }
    }

    // --- Parameter sources through the graph's own parameters.
    struct InputChange {
        CKParameterIn *input;
        CKParameter *direct;
        CKParameterIn *shared;
        CKParameterIn *graphInput; // the graph parameter it shared with
    };
    std::vector<InputChange> inputs;
    std::set<CK_ID> graphInputs;
    for (int i = 0; i < graph->GetInputParameterCount(); ++i) graphInputs.insert(graph->GetInputParameter(i)->GetID());
    for (CKParameterIn *input : ObjectsOfClass<CKParameterIn>(context, CKCID_PARAMETERIN)) {
        CKParameterIn *shared = input->GetSharedSource();
        if (!shared || !graphInputs.count(shared->GetID())) continue;
        inputs.push_back({input, shared->GetDirectSource(), shared->GetSharedSource(), shared});
    }
    struct OutputFeed {
        CKParameterOut *graphOutput;
        CKParameterOut *feeder; // child output writing the graph output
        std::vector<CKParameterOut *> others; // further writers: they stop writing it
    };
    // The feeder is a writer owned by a child of the graph (soft-deleted
    // objects may still list the destination); every writer leaves the graph
    // output, which goes with the graph (a destination left on it made the
    // saved file reference an unsaved parameter, CrashCube.cmo random edits).
    std::vector<OutputFeed> feeds;
    for (int i = 0; i < graph->GetOutputParameterCount(); ++i) {
        CKParameterOut *graphOutput = graph->GetOutputParameter(i);
        OutputFeed feed{graphOutput, nullptr, {}};
        for (CKParameterOut *output : ObjectsOfClass<CKParameterOut>(context, CKCID_PARAMETEROUT)) {
            const std::vector<CK_ID> destinations = DestinationsOf(output);
            if (std::find(destinations.begin(), destinations.end(), graphOutput->GetID()) == destinations.end()) continue;
            CKBehavior *writer = CKBehavior::Cast(output->GetOwner());
            CKParameterOperation *operation = CKParameterOperation::Cast(output->GetOwner());
            const bool child = (writer && writer->GetParent() == graph) || (operation && operation->GetOwner() == graph);
            if (!feed.feeder && child && !document.IsDiscarded(output->GetOwner()->GetID())) feed.feeder = output;
            else feed.others.push_back(output);
        }
        feeds.push_back(feed);
    }

    // --- CK steps.
    for (CKBehaviorLink *link : outsideIn)
        if (!BuryLink(edit, parentId, link)) return edit.Fail("Cannot remove a link to the graph");
    for (CKBehaviorLink *link : outsideOut)
        if (std::find(outsideIn.begin(), outsideIn.end(), link) == outsideIn.end() && !BuryLink(edit, parentId, link))
            return edit.Fail("Cannot remove a link from the graph");
    for (CKBehaviorLink *link : inside)
        if (std::find(moved.begin(), moved.end(), link) == moved.end() && !BuryLink(edit, graphId, link))
            return edit.Fail("Cannot remove a link of the graph");
    for (CKBehaviorLink *link : moved)
        if (!MoveLink(edit, graphId, parentId, link)) return edit.Fail("Cannot move a link of the graph");
    std::vector<CK_ID> children;
    for (int i = 0; i < graph->GetSubBehaviorCount(); ++i) children.push_back(graph->GetSubBehavior(i)->GetID());
    for (CK_ID child : children)
        if (!MoveBehavior(edit, graphId, parentId, child)) return edit.Fail("A behavior of the graph does not fit its new owner");
    const int localBase = parent->GetLocalParameterCount();
    std::vector<CKParameterLocal *> locals;
    for (int i = 0; i < graph->GetLocalParameterCount(); ++i) locals.push_back(graph->GetLocalParameter(i));
    for (CKParameterLocal *local : locals)
        if (!MoveLocal(edit, graphId, parentId, local)) return edit.Fail("Cannot move a local parameter");
    std::vector<CKParameterOperation *> operations;
    for (int i = 0; i < graph->GetParameterOperationCount(); ++i) operations.push_back(graph->GetParameterOperation(i));
    for (CKParameterOperation *operation : operations)
        if (!MoveOperation(edit, graphId, parentId, operation)) return edit.Fail("Cannot move a parameter operation");
    std::vector<CK_ID> newLinks;
    for (const NewLink &link : combined) {
        const CK_ID id = CreateLink(edit, parentId, link.in, link.out, link.delay);
        if (!id) return edit.Fail("Cannot reconnect a link through the graph");
        newLinks.push_back(id);
    }
    for (const InputChange &change : inputs)
        if (!SetSource(edit, change.input, change.direct, change.shared))
            return edit.Fail("Cannot reconnect a parameter through the graph");
    for (const OutputFeed &feed : feeds) {
        // Consumers and destinations of the graph output now use the feeder.
        for (CKParameterIn *input : ObjectsOfClass<CKParameterIn>(context, CKCID_PARAMETERIN))
            if (input->GetDirectSource() == feed.graphOutput &&
                !SetSource(edit, input, feed.feeder, nullptr))
                return edit.Fail("Cannot reconnect a graph output");
        if (feed.feeder) {
            for (CK_ID destination : DestinationsOf(feed.graphOutput))
                if (CKParameter *target = CKParameter::Cast(context->GetObject(destination)))
                    if (!AddDestination(edit, feed.feeder, target)) return edit.Fail("Cannot reconnect a graph output");
            if (!RemoveDestination(edit, feed.feeder, feed.graphOutput))
                return edit.Fail("Cannot disconnect a graph output");
        }
        for (CKParameterOut *other : feed.others)
            if (!RemoveDestination(edit, other, feed.graphOutput)) return edit.Fail("Cannot disconnect a graph output");
    }
    const int graphAt = SubBehaviorPosition(parent, graph);
    if (!edit.Do([context, parentId, graphId] { return DetachSubBehavior(context, parentId, graphId); },
                 [context, parentId, graphId, graphAt] { return AttachSubBehavior(context, parentId, graphId, graphAt); }))
        return edit.Fail("Cannot remove the graph");
    edit.Bury(graphId);

    // --- Display records.
    graphRecord = FindRecord(data, graphId);
    parentRecord = FindRecord(data, parentId);
    const std::set<CK_ID> subtree = Subtree(data, graphId);
    for (auto &record : data.behaviors) {
        if (record.id == graphId || !subtree.count(record.id)) continue;
        if (record.parentId == graphId) record.parentId = parentId;
        if (record.depth > 0) --record.depth;
    }
    // Locals: same pixel position, cells of the parent window.
    const bool scriptUi = IsScriptUi(edit, *parentRecord);
    for (const Parameter &local : graphRecord->localParams) {
        Parameter copy = local;
        const Point pixel = ParameterDisplayGeometry::GetPosition(local, false);
        copy.hPos = static_cast<int>(std::lround((graphOrigin.h + pixel.h - parentRecord->rect.hPos) / GRID));
        copy.vPos = static_cast<int>(std::lround((graphOrigin.v + pixel.v - parentRecord->rect.vPos) / GRID));
        if (scriptUi) {
            copy.hPos = std::max(copy.hPos, ParameterDisplayGeometry::SCRIPT_MINIMUM_COLUMN);
            copy.vPos = std::max(copy.vPos, 0);
        }
        parentRecord->localParams.push_back(copy);
    }
    std::map<int, int> shortcutSlots; // graph shortcut slot -> parent slot
    for (int i = 0; i < static_cast<int>(graphRecord->sharedParams.size()); ++i) {
        parentRecord->sharedParams.push_back(graphRecord->sharedParams[i]);
        shortcutSlots[i] = static_cast<int>(parentRecord->sharedParams.size()) - 1;
    }
    for (const auto &operation : graphRecord->operations) parentRecord->operations.push_back(operation);
    for (const auto &comment : graphRecord->comments) parentRecord->comments.push_back(comment);
    // Links between elements of the graph keep their geometry.
    auto onGraph = [&](const LinkEndpoint &endpoint) {
        return endpoint.id == graphId && endpoint.type != ENDPOINT_PLOCAL && endpoint.type != ENDPOINT_POUT_SHORTCUT;
    };
    for (Link link : graphRecord->links) {
        if (onGraph(link.start) || onGraph(link.end)) continue;
        for (LinkEndpoint *endpoint : {&link.start, &link.end}) {
            if (endpoint->type == ENDPOINT_PLOCAL && endpoint->id == graphId) {
                endpoint->id = parentId;
                endpoint->index += localBase;
            } else if (endpoint->type == ENDPOINT_POUT_SHORTCUT && endpoint->id == graphId) {
                endpoint->id = parentId;
                endpoint->index = shortcutSlots[endpoint->index];
            }
        }
        parentRecord->links.push_back(link);
    }
    // New direct behavior links.
    for (CK_ID id : newLinks) {
        CKBehaviorLink *link = CKBehaviorLink::Cast(context->GetObject(id));
        CKBehaviorIO *in = link->GetInBehaviorIO(), *out = link->GetOutBehaviorIO();
        CKBehavior *from = in->GetOwner(), *to = out->GetOwner();
        LinkEndpoint start(from->GetID(), from->GetOutputPosition(in), ENDPOINT_BOUT);
        if (start.index < 0) start = LinkEndpoint(in->GetID(), from->GetInputPosition(in), ENDPOINT_START_BIN);
        if (start.type == ENDPOINT_START_BIN && from->GetID() != scriptId)
            start = LinkEndpoint(from->GetID(), from->GetInputPosition(in), ENDPOINT_BIN);
        LinkEndpoint end(to->GetID(), to->GetInputPosition(out), ENDPOINT_BIN);
        if (end.index < 0) end = LinkEndpoint(to->GetID(), to->GetOutputPosition(out), ENDPOINT_BOUT);
        parentRecord->links.push_back(Link(id, LINK_TYPE_BEHAVIOR, start, end));
    }
    data.behaviors.erase(std::remove_if(data.behaviors.begin(), data.behaviors.end(),
                                        [&](const BehaviorData &record) { return record.id == graphId; }),
                         data.behaviors.end());
    data.behaviorCount = static_cast<int>(data.behaviors.size());
    // Display of the rewired parameters, in the parent graph.
    const Point anchor(graphOrigin.h, graphOrigin.v - GRID);
    // An input of a hidden conversion (Dev's undisplayed operation on a
    // parameter link: no record shows it) is shown through it, as the
    // stored link did: the links end at the inputs its output feeds. Ended
    // at the conversion's own pin, the conversion would look unused and
    // Commit would take it out of the graph under that link.
    auto shownInputs = [&](CKParameterIn *input) {
        std::vector<CKParameterIn *> shown;
        CKParameterOperation *conversion = input ? CKParameterOperation::Cast(input->GetOwner()) : nullptr;
        bool displayed = false;
        if (conversion)
            ForEachRecord(edit.Data(), [&](BehaviorData &record) {
                for (const auto &operation : record.operations) displayed = displayed || operation.id == conversion->GetID();
            });
        if (!conversion || displayed || !conversion->GetOutParameter()) {
            if (input) shown.push_back(input);
            return shown;
        }
        for (CKParameterIn *consumer : ObjectsOfClass<CKParameterIn>(context, CKCID_PARAMETERIN))
            if (consumer->GetDirectSource() == conversion->GetOutParameter()) shown.push_back(consumer);
        return shown;
    };
    for (const InputChange &change : inputs) {
        CKObject *source = change.shared ? static_cast<CKObject *>(change.shared) : change.direct;
        LinkEndpoint start;
        if (!source || !SourceEndpoint(edit, parentId, source, anchor, start)) continue;
        for (CKParameterIn *input : shownInputs(change.input)) {
            LinkEndpoint end;
            if (DestinationEndpoint(edit, parentId, input, end)) AddParameterLink(edit, parentId, start, end);
        }
    }
    for (const OutputFeed &feed : feeds) {
        if (!feed.feeder) continue;
        LinkEndpoint start;
        if (!SourceEndpoint(edit, parentId, feed.feeder, anchor, start)) continue;
        for (CKParameterIn *input : ObjectsOfClass<CKParameterIn>(context, CKCID_PARAMETERIN)) {
            if (input->GetDirectSource() != feed.feeder) continue;
            for (CKParameterIn *shown : shownInputs(input)) {
                LinkEndpoint end;
                if (DestinationEndpoint(edit, parentId, shown, end)) AddParameterLink(edit, parentId, start, end);
            }
        }
        for (CK_ID destination : DestinationsOf(feed.feeder)) {
            LinkEndpoint end;
            if (DestinationEndpoint(edit, parentId, context->GetObject(destination), end))
                AddParameterLink(edit, parentId, start, end);
        }
        // Shortcuts of the graph output elsewhere now show the feeder.
        ForEachRecord(edit.Data(), [&](BehaviorData &record) {
            for (auto &shortcut : record.sharedParams)
                if (shortcut.sourceId == feed.graphOutput->GetID()) shortcut.sourceId = feed.feeder->GetID();
        });
    }
    // Remove duplicates added for links that were already displayed.
    BehaviorData *finalParent = FindRecord(edit.Data(), parentId);
    std::vector<Link> unique;
    for (const auto &link : finalParent->links) {
        const bool duplicate = std::any_of(unique.begin(), unique.end(), [&](const Link &other) {
            return other.type == link.type && other.id == link.id && other.start.id == link.start.id &&
                   other.start.index == link.start.index && other.start.type == link.start.type &&
                   other.end.id == link.end.id && other.end.index == link.end.index && other.end.type == link.end.type;
        });
        if (!duplicate) unique.push_back(link);
    }
    finalParent->links = unique;
    return edit.Commit();
}

EditResult GroupBehaviors(Document &document, CK_ID scriptId,
                          const std::vector<CK_ID> &selected) {
    TopologyEdit edit(document, scriptId, "Create Behavior Graph");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    InterfaceData &data = edit.Data();
    const std::set<CK_ID> tree = TreeIds(edit.Script());
    std::vector<CK_ID> members;
    for (CK_ID id : selected)
        if (id != scriptId && tree.count(id) && std::find(members.begin(), members.end(), id) == members.end())
            members.push_back(id);
    if (members.empty()) return edit.Fail("Select behaviors to put in a new behavior graph");
    const CK_ID parentId = ParentOf(data, members.front());
    for (CK_ID id : members)
        if (ParentOf(data, id) != parentId) return edit.Fail("The selected behaviors must be in the same graph");
    CKBehavior *parent = BehaviorOf(context, parentId);
    if (!EditableGraph(parent)) return edit.Fail("Behaviors inside a Building Block cannot be grouped");
    const std::set<CK_ID> group(members.begin(), members.end());
    // Order as in the parent (keeps execution order among equal priorities).
    std::sort(members.begin(), members.end(), [&](CK_ID a, CK_ID b) {
        return SubBehaviorPosition(parent, BehaviorOf(context, a)) < SubBehaviorPosition(parent, BehaviorOf(context, b));
    });
    const std::set<CK_ID> owned = CollectOwnedObjects(context, group);

    // Bounds of the members (flattened coordinates).
    float left = FLT_MAX, top = FLT_MAX, right = -FLT_MAX, bottom = -FLT_MAX;
    for (CK_ID id : members)
        if (const BehaviorData *record = FindRecord(data, id)) {
            Rect content;
            Rect box = record->rect;
            if (BehaviorDisplayGeometry::TryGetContentBounds(*record, content))
                box = Rect(record->rect.hPos + content.hPos, record->rect.vPos + content.vPos, content.hSize,
                           content.vSize);
            left = std::min(left, box.hPos);
            top = std::min(top, box.vPos);
            right = std::max(right, box.Right());
            bottom = std::max(bottom, box.Bottom());
        }

    // --- The new graph. Its own IOs and parameters live and die with it.
    CKBehavior *graph = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, (CKSTRING) "Behavior Graph"));
    if (!graph) return edit.Fail("CKContext::CreateObject failed");
    graph->UseGraph();
    graph->SetType(CKBEHAVIORTYPE_BEHAVIOR);
    const CK_ID graphId = graph->GetID();
    edit.Created(graphId);
    {
        const int at = parent->GetSubBehaviorCount();
        if (!edit.Do([context, parentId, graphId, at] { return AttachSubBehavior(context, parentId, graphId, at); },
                     [context, parentId, graphId] { return DetachSubBehavior(context, parentId, graphId); }))
            return edit.Fail("CKBehavior::AddSubBehavior failed");
    }

    // --- Behavior links of the parent.
    struct Crossing {
        CKBehaviorLink *link;
        bool entering; // outside -> member (else member -> outside)
    };
    std::vector<CKBehaviorLink *> internal;
    std::vector<Crossing> crossing;
    for (int i = 0; i < parent->GetSubBehaviorLinkCount(); ++i) {
        CKBehaviorLink *link = parent->GetSubBehaviorLink(i);
        if (!link || !link->GetInBehaviorIO() || !link->GetOutBehaviorIO()) continue;
        const bool from = group.count(link->GetInBehaviorIO()->GetOwner()->GetID()) != 0;
        const bool to = group.count(link->GetOutBehaviorIO()->GetOwner()->GetID()) != 0;
        if (from && to) internal.push_back(link);
        else if (to) crossing.push_back({link, true});
        else if (from) crossing.push_back({link, false});
    }
    std::map<CKBehaviorIO *, CKBehaviorIO *> graphIo; // member IO -> graph IO
    for (const Crossing &c : crossing) {
        CKBehaviorIO *member = c.entering ? c.link->GetOutBehaviorIO() : c.link->GetInBehaviorIO();
        if (graphIo.count(member)) continue;
        const std::string name = NameOf(member);
        graphIo[member] = c.entering ? graph->CreateInput(const_cast<CKSTRING>(name.c_str()))
                                     : graph->CreateOutput(const_cast<CKSTRING>(name.c_str()));
    }

    // --- Parameters crossing the border (members only, not their insides).
    // Parent locals used only by the members move into the graph.
    std::vector<CKParameterLocal *> movingLocals;
    for (int i = 0; i < parent->GetLocalParameterCount(); ++i) {
        CKParameterLocal *local = parent->GetLocalParameter(i);
        bool used = false, outside = false;
        // A deleted behavior's inputs still name the local: not a use.
        for (CKParameterIn *input : LiveObjectsOfClass<CKParameterIn>(document, CKCID_PARAMETERIN))
            if (input->GetDirectSource() == local) (owned.count(input->GetID()) ? used : outside) = true;
        for (CKParameterOut *output : LiveObjectsOfClass<CKParameterOut>(document, CKCID_PARAMETEROUT)) {
            const std::vector<CK_ID> destinations = DestinationsOf(output);
            if (std::find(destinations.begin(), destinations.end(), local->GetID()) != destinations.end())
                (owned.count(output->GetID()) ? used : outside) = true;
        }
        if (used && !outside) movingLocals.push_back(local);
    }
    std::set<CK_ID> inside = owned;
    for (CKParameterLocal *local : movingLocals) inside.insert(local->GetID());
    struct InputRoute {
        CKParameterIn *input; // member input
        CKParameterIn *graphInput;
    };
    std::vector<InputRoute> inputRoutes;
    std::map<CK_ID, CKParameterIn *> bySource;
    for (CK_ID id : members) {
        CKBehavior *member = BehaviorOf(context, id);
        std::vector<CKParameterIn *> pins;
        for (int i = 0; i < member->GetInputParameterCount(); ++i) pins.push_back(member->GetInputParameter(i));
        if (member->IsUsingTarget() && member->GetTargetParameter()) pins.push_back(member->GetTargetParameter());
        for (CKParameterIn *input : pins) {
            if (!input) continue;
            CKParameter *direct = input->GetDirectSource();
            CKParameterIn *shared = input->GetSharedSource();
            CKObject *source = shared ? static_cast<CKObject *>(shared) : direct;
            if (!source || inside.count(source->GetID())) continue;
            CKParameterIn *&graphInput = bySource[source->GetID()];
            if (!graphInput) {
                graphInput = graph->CreateInputParameter(const_cast<CKSTRING>(NameOf(input)), input->GetGUID());
                if (!graphInput) return edit.Fail("Cannot create a graph input parameter");
                if (shared) graphInput->ShareSourceWith(shared);
                else graphInput->SetDirectSource(direct);
            }
            inputRoutes.push_back({input, graphInput});
        }
    }
    struct OutputRoute {
        CKParameterOut *output; // member output
        CKParameterOut *graphOutput;
        std::vector<CKParameterIn *> consumers;
        std::vector<CKParameter *> destinations;
    };
    std::vector<OutputRoute> outputRoutes;
    // Readers and destinations in this Script go through the graph output;
    // those of other Scripts (a shortcut of the output there, corpus: Open
    // File.cmo) keep the member's output: their display, not part of this
    // edit, shows that one, and CK reads a source at any depth.
    auto inThisScript = [&](CKObject *parameter) {
        CKObject *owner = nullptr;
        if (CKParameterIn *input = CKParameterIn::Cast(parameter)) owner = input->GetOwner();
        else if (CKParameter *value = CKParameter::Cast(parameter)) owner = value->GetOwner();
        if (CKParameterOperation *operation = CKParameterOperation::Cast(owner)) owner = operation->GetOwner();
        return owner && tree.count(owner->GetID()) != 0;
    };
    for (CK_ID id : members) {
        CKBehavior *member = BehaviorOf(context, id);
        for (int i = 0; i < member->GetOutputParameterCount(); ++i) {
            CKParameterOut *output = member->GetOutputParameter(i);
            if (!output) continue;
            OutputRoute route{output, nullptr, {}, {}};
            for (CKParameterIn *input : LiveObjectsOfClass<CKParameterIn>(document, CKCID_PARAMETERIN))
                if (input->GetDirectSource() == output && !inside.count(input->GetID()) && inThisScript(input))
                    route.consumers.push_back(input);
            for (CK_ID destination : DestinationsOf(output))
                if (!inside.count(destination))
                    if (CKParameter *target = CKParameter::Cast(context->GetObject(destination)))
                        if (inThisScript(target)) route.destinations.push_back(target);
            if (route.consumers.empty() && route.destinations.empty()) continue;
            route.graphOutput = graph->CreateOutputParameter(const_cast<CKSTRING>(NameOf(output)), output->GetGUID());
            if (!route.graphOutput) return edit.Fail("Cannot create a graph output parameter");
            outputRoutes.push_back(route);
        }
    }

    // --- CK steps.
    std::map<CK_ID, int> oldLocalSlots;
    for (int i = 0; i < parent->GetLocalParameterCount(); ++i) oldLocalSlots[parent->GetLocalParameter(i)->GetID()] = i;
    for (CK_ID id : members)
        if (!MoveBehavior(edit, parentId, graphId, id)) return edit.Fail("A selected behavior does not fit the new graph");
    std::vector<CKParameterLocal *> descending = movingLocals;
    std::sort(descending.begin(), descending.end(), [&](CKParameterLocal *a, CKParameterLocal *b) {
        return oldLocalSlots[a->GetID()] > oldLocalSlots[b->GetID()];
    });
    for (CKParameterLocal *local : descending)
        if (!MoveLocal(edit, parentId, graphId, local)) return edit.Fail("Cannot move a local parameter");
    for (CKBehaviorLink *link : internal)
        if (!MoveLink(edit, parentId, graphId, link)) return edit.Fail("Cannot move a link");
    std::vector<std::pair<CK_ID, bool>> newLinks; // link, stored in the graph (else the parent)
    std::set<CKBehaviorIO *> innerDone;
    for (const Crossing &c : crossing) {
        CKBehaviorIO *member = c.entering ? c.link->GetOutBehaviorIO() : c.link->GetInBehaviorIO();
        CKBehaviorIO *io = graphIo[member];
        CKBehaviorIO *outsideIo = c.entering ? c.link->GetInBehaviorIO() : c.link->GetOutBehaviorIO();
        const int delay = c.link->GetInitialActivationDelay();
        if (!BuryLink(edit, parentId, c.link)) return edit.Fail("Cannot reroute a link");
        const CK_ID outer = c.entering ? CreateLink(edit, parentId, outsideIo, io, delay)
                                       : CreateLink(edit, parentId, io, outsideIo, delay);
        if (!outer) return edit.Fail("Cannot reroute a link");
        newLinks.push_back({outer, false});
        if (innerDone.insert(member).second) {
            const CK_ID inner = c.entering ? CreateLink(edit, graphId, io, member, 0)
                                           : CreateLink(edit, graphId, member, io, 0);
            if (!inner) return edit.Fail("Cannot reroute a link");
            newLinks.push_back({inner, true});
        }
    }
    for (const InputRoute &route : inputRoutes)
        if (!SetSource(edit, route.input, nullptr, route.graphInput)) return edit.Fail("Cannot share a graph input");
    for (const OutputRoute &route : outputRoutes) {
        if (!AddDestination(edit, route.output, route.graphOutput)) return edit.Fail("Cannot feed a graph output");
        for (CKParameterIn *input : route.consumers)
            if (!SetSource(edit, input, route.graphOutput, nullptr)) return edit.Fail("Cannot reroute an output");
        for (CKParameter *target : route.destinations)
            if (!RemoveDestination(edit, route.output, target) || !AddDestination(edit, route.graphOutput, target))
                return edit.Fail("Cannot reroute an output destination");
    }

    // --- Display records.
    const BehaviorData *parentRecordNow = FindRecord(data, parentId);
    BehaviorData record(graphId);
    record.parentId = parentId;
    record.depth = parentRecordNow == &data.rootBehavior ? 1 : parentRecordNow->depth + 1;
    record.isBehaviorGraph = true;
    record.folded = false;
    record.UpdateRuntimeState(graph);
    LayoutCalculator::CalculateBehaviorSize(record, graph);
    const float margin = 2.0f * GRID;
    record.rect.hPos = SchematicGrid::SnapToGrid(left - margin, GRID);
    record.rect.vPos = SchematicGrid::SnapToGrid(top - margin, GRID);
    record.hExpandSize = std::max(record.rect.hSize, right + margin - record.rect.hPos);
    record.vExpandSize = std::max(record.rect.vSize, bottom + margin - record.rect.vPos);
    std::set<CK_ID> subtree;
    for (CK_ID id : members) {
        const std::set<CK_ID> part = Subtree(data, id);
        subtree.insert(part.begin(), part.end());
    }
    for (auto &other : data.behaviors) {
        if (!subtree.count(other.id)) continue;
        if (group.count(other.id)) other.parentId = graphId;
        ++other.depth;
    }
    data.behaviors.push_back(record);
    std::stable_sort(data.behaviors.begin(), data.behaviors.end(),
                     [](const BehaviorData &a, const BehaviorData &b) { return a.depth < b.depth; });
    data.behaviorCount = static_cast<int>(data.behaviors.size());
    BehaviorData *source = FindRecord(data, parentId);
    BehaviorData *target = FindRecord(data, graphId);
    // Locals: parent slot -> graph slot; the remaining parent slots close up.
    std::map<int, int> movedSlots, keptSlots;
    std::vector<Parameter> keptLocals;
    const bool sourceScriptUi = IsScriptUi(edit, *source);
    for (int i = 0; i < static_cast<int>(source->localParams.size()); ++i) {
        CKParameterLocal *local = nullptr;
        for (const auto &entry : oldLocalSlots)
            if (entry.second == i) local = CKParameterLocal::Cast(context->GetObject(entry.first));
        const bool moving = local && std::find(movingLocals.begin(), movingLocals.end(), local) != movingLocals.end();
        if (!moving) {
            keptSlots[i] = static_cast<int>(keptLocals.size());
            keptLocals.push_back(source->localParams[i]);
            continue;
        }
        const int slot = graph->GetLocalParameterPosition(local);
        movedSlots[i] = slot;
        Parameter copy = source->localParams[i];
        const Point pixel = ParameterDisplayGeometry::GetPosition(copy, sourceScriptUi);
        copy.hPos = static_cast<int>(std::lround((source->rect.hPos + pixel.h - target->rect.hPos) / GRID));
        copy.vPos = static_cast<int>(std::lround((source->rect.vPos + pixel.v - target->rect.vPos) / GRID));
        if (static_cast<int>(target->localParams.size()) <= slot) target->localParams.resize(slot + 1);
        target->localParams[slot] = copy;
    }
    source->localParams = keptLocals;
    // Parent links between members (or moved locals) move into the graph;
    // the others keep their place with renumbered local slots.
    auto inGroup = [&](const LinkEndpoint &endpoint) {
        if (endpoint.type == ENDPOINT_PLOCAL) return endpoint.id == parentId && movedSlots.count(endpoint.index) != 0;
        if (endpoint.type == ENDPOINT_POUT_SHORTCUT || endpoint.type == ENDPOINT_START_BIN) return false;
        return group.count(endpoint.id) != 0;
    };
    std::vector<Link> stay;
    for (Link link : source->links) {
        if (inGroup(link.start) && inGroup(link.end)) {
            for (LinkEndpoint *endpoint : {&link.start, &link.end})
                if (endpoint->type == ENDPOINT_PLOCAL) {
                    endpoint->id = graphId;
                    endpoint->index = movedSlots[endpoint->index];
                }
            target->links.push_back(link);
            continue;
        }
        for (LinkEndpoint *endpoint : {&link.start, &link.end})
            if (endpoint->type == ENDPOINT_PLOCAL && endpoint->id == parentId && keptSlots.count(endpoint->index))
                endpoint->index = keptSlots[endpoint->index];
        stay.push_back(link);
    }
    source->links = stay;
    // Rerouted parameters: the outside part of a link keeps its geometry.
    for (const InputRoute &route : inputRoutes) {
        const int graphSlot = graph->GetInputParameterPosition(route.graphInput);
        CKBehavior *owner = CKBehavior::Cast(route.input->GetOwner());
        const LinkEndpoint memberEnd =
            owner->GetTargetParameter() == route.input
                ? LinkEndpoint(owner->GetID(), -2, ENDPOINT_TARGET_PIN)
                : LinkEndpoint(owner->GetID(), owner->GetInputParameterPosition(route.input), ENDPOINT_PIN);
        for (auto &link : source->links)
            if (link.IsParameterLink() && link.end.id == memberEnd.id && link.end.type == memberEnd.type &&
                link.end.index == memberEnd.index)
                link.end = LinkEndpoint(graphId, graphSlot, ENDPOINT_PIN);
        target->links.push_back(
            Link(0, LINK_TYPE_PARAMETER, LinkEndpoint(graphId, graphSlot, ENDPOINT_PIN), memberEnd));
    }
    for (const OutputRoute &route : outputRoutes) {
        CKBehavior *owner = CKBehavior::Cast(route.output->GetOwner());
        const LinkEndpoint memberStart(owner->GetID(), owner->GetOutputParameterPosition(route.output), ENDPOINT_POUT);
        const LinkEndpoint graphOut(graphId, graph->GetOutputParameterPosition(route.graphOutput), ENDPOINT_POUT);
        for (auto &link : source->links)
            if (link.IsParameterLink() && link.start.id == memberStart.id && link.start.type == ENDPOINT_POUT &&
                link.start.index == memberStart.index)
                link.start = graphOut;
        target->links.push_back(Link(0, LINK_TYPE_PARAMETER, memberStart, graphOut));
        ForEachRecord(data, [&](BehaviorData &any) {
            for (auto &shortcut : any.sharedParams)
                if (shortcut.sourceId == route.output->GetID()) shortcut.sourceId = route.graphOutput->GetID();
        });
    }
    // New behavior links.
    for (const auto &entry : newLinks) {
        CKBehaviorLink *link = CKBehaviorLink::Cast(context->GetObject(entry.first));
        CKBehaviorIO *in = link->GetInBehaviorIO(), *out = link->GetOutBehaviorIO();
        CKBehavior *from = in->GetOwner(), *to = out->GetOwner();
        LinkEndpoint start(from->GetID(), from->GetOutputPosition(in), ENDPOINT_BOUT);
        if (start.index < 0)
            start = from->GetID() == scriptId ? LinkEndpoint(in->GetID(), from->GetInputPosition(in), ENDPOINT_START_BIN)
                                             : LinkEndpoint(from->GetID(), from->GetInputPosition(in), ENDPOINT_BIN);
        LinkEndpoint end(to->GetID(), to->GetInputPosition(out), ENDPOINT_BIN);
        if (end.index < 0) end = LinkEndpoint(to->GetID(), to->GetOutputPosition(out), ENDPOINT_BOUT);
        (entry.second ? target : source)->links.push_back(Link(entry.first, LINK_TYPE_BEHAVIOR, start, end));
    }
    return edit.Commit(graphId);
}

} // namespace vdev
