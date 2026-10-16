// The topology commands' primitives (TopologyInternal.h), shared with
// GroupCommands.cpp and ScriptCommands.cpp.
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

namespace vdev {

namespace detail {

CKBehavior *BehaviorOf(CKContext *context, CK_ID id) {
    return context ? CKBehavior::Cast(context->GetObject(id)) : nullptr;
}

std::string EndpointKey(const LinkEndpoint &endpoint) {
    return std::to_string(endpoint.id) + ":" + std::to_string(endpoint.index) + ":" +
           std::to_string(static_cast<int>(endpoint.type));
}

std::string LinkKey(const BehaviorData &graph, const Link &link) {
    return std::to_string(graph.id) + " " + std::to_string(static_cast<int>(link.type)) + " " +
           std::to_string(link.id) + " " + EndpointKey(link.start) + ">" + EndpointKey(link.end) +
           (link.highlight ? " h" : "");
}


std::set<std::string> MatchingLinks(CKContext *context, const InterfaceData &data) {
    std::set<std::string> keys;
    auto collect = [&](const BehaviorData &record) {
        for (const auto &link : record.links)
            if (LinkMatchesRuntime(context, record, link)) keys.insert(LinkKey(record, link));
    };
    collect(data.rootBehavior);
    for (const auto &record : data.behaviors) collect(record);
    return keys;
}

std::set<CK_ID> TreeIds(CKBehavior *script) {
    std::set<CK_ID> ids;
    std::vector<CKBehavior *> pending{script};
    while (!pending.empty()) {
        CKBehavior *behavior = pending.back();
        pending.pop_back();
        if (!behavior || !ids.insert(behavior->GetID()).second) continue;
        for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i) pending.push_back(behavior->GetSubBehavior(i));
    }
    return ids;
}

bool EditableGraph(CKBehavior *graph) {
    return graph && graph->GetType() != CKBEHAVIORTYPE_BASE && !(graph->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK);
}

bool IsEndpoint(const LinkEndpoint &endpoint, EndpointType type, CK_ID id, int index) {
    return endpoint.type == type && endpoint.id == id && endpoint.index == index;
}

// Removes a shortcut slot, its links, and renumbers later slots.
void RemoveSharedSlot(BehaviorData &record, int slot) {
    if (slot < 0 || slot >= static_cast<int>(record.sharedParams.size())) return;
    auto uses = [&](const LinkEndpoint &endpoint) {
        return endpoint.type == ENDPOINT_POUT_SHORTCUT && endpoint.id == record.id && endpoint.index == slot;
    };
    auto &links = record.links;
    links.erase(std::remove_if(links.begin(), links.end(),
                               [&](const Link &link) { return uses(link.start) || uses(link.end); }),
                links.end());
    for (auto &link : links)
        for (LinkEndpoint *endpoint : {&link.start, &link.end})
            if (endpoint->type == ENDPOINT_POUT_SHORTCUT && endpoint->id == record.id && endpoint->index > slot)
                --endpoint->index;
    record.sharedParams.erase(record.sharedParams.begin() + slot);
}

// Removes a local display slot of graph, its links, and renumbers later slots
// as CKBehavior::RemoveLocalParameter does.
void RemoveLocalSlot(InterfaceData &data, BehaviorData &graph, int slot) {
    if (slot < 0 || slot >= static_cast<int>(graph.localParams.size())) return;
    ForEachRecord(data, [&](BehaviorData &record) {
        auto &links = record.links;
        links.erase(std::remove_if(links.begin(), links.end(),
                                   [&](const Link &link) {
                                       return IsEndpoint(link.start, ENDPOINT_PLOCAL, graph.id, slot) ||
                                              IsEndpoint(link.end, ENDPOINT_PLOCAL, graph.id, slot);
                                   }),
                    links.end());
        for (auto &link : links)
            for (LinkEndpoint *endpoint : {&link.start, &link.end})
                if (endpoint->type == ENDPOINT_PLOCAL && endpoint->id == graph.id && endpoint->index > slot)
                    --endpoint->index;
    });
    graph.localParams.erase(graph.localParams.begin() + slot);
}

// Drops display records the CK edit invalidated (see TopologyCommands.h).
void Prune(CKContext *context, CKBehavior *script, InterfaceData &data, const std::set<std::string> &linksBefore,
           const std::set<CK_ID> &removed) {
    const std::set<CK_ID> tree = TreeIds(script);
    data.behaviors.erase(std::remove_if(data.behaviors.begin(), data.behaviors.end(),
                                        [&](const BehaviorData &record) { return !tree.count(record.id); }),
                         data.behaviors.end());
    data.behaviorCount = static_cast<int>(data.behaviors.size());
    ForEachRecord(data, [&](BehaviorData &record) {
        CKBehavior *graph = BehaviorOf(context, record.id);
        auto &operations = record.operations;
        operations.erase(std::remove_if(operations.begin(), operations.end(),
                                        [&](const Operation &operation) {
                                            auto *runtime =
                                                CKParameterOperation::Cast(context->GetObject(operation.id));
                                            return !runtime || runtime->GetOwner() != graph ||
                                                   removed.count(operation.id);
                                        }),
                         operations.end());
        auto &links = record.links;
        links.erase(std::remove_if(links.begin(), links.end(),
                                   [&](const Link &link) {
                                       if (removed.count(link.start.id) || removed.count(link.end.id)) return true;
                                       return linksBefore.count(LinkKey(record, link)) &&
                                              !LinkMatchesRuntime(context, record, link);
                                   }),
                    links.end());
    });
    ForEachRecord(data, [&](BehaviorData &record) {
        for (int slot = static_cast<int>(record.sharedParams.size()) - 1; slot >= 0; --slot)
            if (removed.count(record.sharedParams[slot].sourceId)) RemoveSharedSlot(record, slot);
    });
}

CKBehavior *NewBuildingBlock(TopologyEdit &edit, const CKGUID &prototype, std::string &error) {
    CKContext *context = edit.Context();
    CKObjectDeclaration *declaration = CKGetObjectDeclarationFromGuid(prototype);
    if (!declaration) {
        error = "Unknown Building Block prototype";
        return nullptr;
    }
    CKBehavior *behavior = CKBehavior::Cast(context->CreateObject(CKCID_BEHAVIOR, declaration->GetName()));
    if (!behavior) {
        error = "CKContext::CreateObject failed";
        return nullptr;
    }
    // CK2 2.5 InitFromPrototype ends with CallCallbackFunction(CKM_BEHAVIORCREATE)
    // and InitFromGuid sets CKBEHAVIOR_BUILDINGBLOCK (CK2 0x2400402d).
    CKERROR initialized = CKERR_INVALIDPARAMETER;
    const bool completed =
        GuardedCall([&] { initialized = behavior->InitFromGuid(prototype); }, &edit.Composition().Host(), "InitFromGuid");
    edit.Created(behavior->GetID());
    if (!completed || initialized != CK_OK) {
        error = std::string("Building Block initialization failed: ") + declaration->GetName();
        return nullptr;
    }
    return behavior;
}

std::string OtherUseOf(const Document &document, CKParameterLocal *local, CKParameterIn *input) {
    for (CKParameterIn *other : LiveObjectsOfClass<CKParameterIn>(document, CKCID_PARAMETERIN))
        if (other != input && other->GetDirectSource() == local) return "The local parameter feeds other inputs";
    for (CKParameterOut *output : LiveObjectsOfClass<CKParameterOut>(document, CKCID_PARAMETEROUT))
        for (int i = 0; i < output->GetDestinationCount(); ++i)
            if (output->GetDestination(i) == local) return "The local parameter is written by an output";
    return std::string();
}

// --- Order-preserving attach/detach. CK sorts the sub-behaviors when one is
// added; the order before is written back with the re-attached object at its
// place (AddSubBehaviorAt), so undo restores the original order.

// CKBehavior::AddSubBehavior returns CKERR_INCOMPATIBLECLASSID for a behavior
// that does not use its Target and whose compatible class the graph owner
// does not derive from.
bool OwnerAccepts(CKBehavior *graph, CKBehavior *behavior) {
    CKBeObject *owner = graph ? graph->GetOwner() : nullptr;
    return !owner || behavior->IsUsingTarget() || CKIsChildClassOf(owner, behavior->GetCompatibleClassID());
}

bool AttachSubBehavior(CKContext *context, CK_ID parentId, CK_ID childId, int position) {
    CKBehavior *parent = BehaviorOf(context, parentId);
    CKBehavior *child = BehaviorOf(context, childId);
    return parent && child && AddSubBehaviorAt(parent, child, position);
}

bool DetachSubBehavior(CKContext *context, CK_ID parentId, CK_ID childId) {
    CKBehavior *parent = BehaviorOf(context, parentId);
    CKBehavior *child = BehaviorOf(context, childId);
    return parent && child && parent->RemoveSubBehavior(child) == child;
}

int SubBehaviorPosition(CKBehavior *parent, CKBehavior *child) {
    for (int i = 0; i < parent->GetSubBehaviorCount(); ++i)
        if (parent->GetSubBehavior(i) == child) return i;
    return -1;
}


IoRef IoRefOf(CKBehaviorIO *io) {
    IoRef ref;
    CKBehavior *owner = io ? io->GetOwner() : nullptr;
    if (!owner) return ref;
    ref.owner = owner->GetID();
    ref.index = owner->GetInputPosition(io);
    if (ref.index < 0) {
        ref.input = false;
        ref.index = owner->GetOutputPosition(io);
    }
    return ref;
}

CKBehaviorIO *Resolve(CKContext *context, const IoRef &ref) {
    CKBehavior *owner = BehaviorOf(context, ref.owner);
    if (!owner || ref.index < 0) return nullptr;
    if (ref.input) return ref.index < owner->GetInputCount() ? owner->GetInput(ref.index) : nullptr;
    return ref.index < owner->GetOutputCount() ? owner->GetOutput(ref.index) : nullptr;
}

// Re-resolves the link's IOs before attaching (they may have been recreated).
bool AttachLink(CKContext *context, CK_ID graphId, CK_ID linkId, int position, const IoRef &in, const IoRef &out) {
    CKBehavior *graph = BehaviorOf(context, graphId);
    CKBehaviorLink *link = CKBehaviorLink::Cast(context->GetObject(linkId));
    CKBehaviorIO *from = Resolve(context, in), *to = Resolve(context, out);
    if (!graph || !link || !from || !to) return false;
    if (link->SetInBehaviorIO(from) != CK_OK || link->SetOutBehaviorIO(to) != CK_OK) return false;
    if (graph->AddSubBehaviorLink(link) != CK_OK) return false;
    std::vector<CKBehaviorLink *> after;
    for (int i = std::max(0, position); i < graph->GetSubBehaviorLinkCount() - 1; ++i)
        after.push_back(graph->GetSubBehaviorLink(i));
    for (CKBehaviorLink *sibling : after) {
        graph->RemoveSubBehaviorLink(sibling);
        graph->AddSubBehaviorLink(sibling);
    }
    return true;
}

// A detached link also leaves its IOs' link lists (SetInBehaviorIO removes
// the link from the previous IO, CK2 0x24007082); a link kept in the
// graveyard must never point at an IO. AttachLink sets the IOs again.
bool DetachLink(CKContext *context, CK_ID graphId, CK_ID linkId) {
    CKBehavior *graph = BehaviorOf(context, graphId);
    CKBehaviorLink *link = CKBehaviorLink::Cast(context->GetObject(linkId));
    if (!graph || !link || graph->RemoveSubBehaviorLink(link) != link) return false;
    link->SetInBehaviorIO(nullptr);
    link->SetOutBehaviorIO(nullptr);
    return true;
}

int LinkPosition(CKBehavior *graph, CKBehaviorLink *link) {
    for (int i = 0; i < graph->GetSubBehaviorLinkCount(); ++i)
        if (graph->GetSubBehaviorLink(i) == link) return i;
    return -1;
}

bool AttachOperation(CKContext *context, CK_ID graphId, CK_ID operationId, int position) {
    CKBehavior *graph = BehaviorOf(context, graphId);
    CKParameterOperation *operation = CKParameterOperation::Cast(context->GetObject(operationId));
    if (!graph || !operation || graph->AddParameterOperation(operation) != CK_OK) return false;
    std::vector<CKParameterOperation *> after;
    for (int i = std::max(0, position); i < graph->GetParameterOperationCount() - 1; ++i)
        after.push_back(graph->GetParameterOperation(i));
    for (CKParameterOperation *sibling : after) {
        graph->RemoveParameterOperation(sibling);
        graph->AddParameterOperation(sibling);
    }
    return true;
}

bool DetachOperation(CKContext *context, CK_ID graphId, CK_ID operationId) {
    CKBehavior *graph = BehaviorOf(context, graphId);
    CKParameterOperation *operation = CKParameterOperation::Cast(context->GetObject(operationId));
    return graph && operation && graph->RemoveParameterOperation(operation) == operation;
}

int OperationPosition(CKBehavior *graph, CKParameterOperation *operation) {
    for (int i = 0; i < graph->GetParameterOperationCount(); ++i)
        if (graph->GetParameterOperation(i) == operation) return i;
    return -1;
}

// --- Parameter sources and destinations.


InputRef RefOf(CKParameterIn *input) {
    InputRef ref;
    if (!input) return ref;
    ref.id = input->GetID();
    CKObject *owner = input->GetOwner();
    if (CKBehavior *behavior = CKBehavior::Cast(owner)) {
        ref.slot = behavior->GetTargetParameter() == input ? -2 : behavior->GetInputParameterPosition(input);
        if (ref.slot != -1) ref.owner = owner->GetID();
    } else if (CKParameterOperation *operation = CKParameterOperation::Cast(owner)) {
        ref.owner = owner->GetID();
        ref.slot = operation->GetInParameter1() == input ? 0 : 1;
    }
    return ref;
}

CKParameterIn *Resolve(CKContext *context, const InputRef &ref) {
    if (!ref.owner) return ref.id ? CKParameterIn::Cast(context->GetObject(ref.id)) : nullptr;
    CKObject *owner = context->GetObject(ref.owner);
    if (CKParameterOperation *operation = CKParameterOperation::Cast(owner))
        return ref.slot == 0 ? operation->GetInParameter1() : operation->GetInParameter2();
    CKBehavior *behavior = CKBehavior::Cast(owner);
    if (!behavior) return nullptr;
    if (ref.slot == -2) return behavior->GetTargetParameter();
    return ref.slot >= 0 && ref.slot < behavior->GetInputParameterCount() ? behavior->GetInputParameter(ref.slot)
                                                                         : nullptr;
}


InputSource SourceOf(CKParameterIn *input) {
    InputSource source;
    source.input = RefOf(input);
    if (CKParameter *direct = input->GetDirectSource()) source.direct = direct->GetID();
    if (CKParameterIn *shared = input->GetSharedSource()) source.shared = RefOf(shared);
    return source;
}

bool RestoreSource(CKContext *context, const InputSource &source) {
    CKParameterIn *input = Resolve(context, source.input);
    if (!input) return false;
    if (source.shared.id) {
        CKParameterIn *shared = Resolve(context, source.shared);
        return shared && input->ShareSourceWith(shared) == CK_OK;
    }
    CKParameter *direct = source.direct ? CKParameter::Cast(context->GetObject(source.direct)) : nullptr;
    if (source.direct && !direct) return false;
    return input->SetDirectSource(direct) == CK_OK;
}

std::vector<CK_ID> DestinationsOf(CKParameterOut *output) {
    std::vector<CK_ID> ids;
    for (int i = 0; i < output->GetDestinationCount(); ++i)
        if (CKParameter *destination = output->GetDestination(i)) ids.push_back(destination->GetID());
    return ids;
}

bool RestoreDestinations(CKContext *context, CK_ID outputId, const std::vector<CK_ID> &destinations) {
    CKParameterOut *output = CKParameterOut::Cast(context->GetObject(outputId));
    if (!output) return false;
    output->RemoveAllDestinations();
    for (CK_ID id : destinations) {
        CKParameter *destination = CKParameter::Cast(context->GetObject(id));
        if (!destination || output->AddDestination(destination, FALSE) != CK_OK) return false;
    }
    return true;
}

bool ClearSource(TopologyEdit &edit, CKParameterIn *input) {
    CKContext *context = edit.Context();
    const InputSource before = SourceOf(input);
    const InputRef ref = before.input;
    return edit.Do(
        [context, ref] {
            CKParameterIn *runtime = Resolve(context, ref);
            return runtime && runtime->SetDirectSource(nullptr) == CK_OK;
        },
        [context, before] { return RestoreSource(context, before); });
}

bool RemoveDestination(TopologyEdit &edit, CKParameterOut *output, CKParameter *destination) {
    CKContext *context = edit.Context();
    const std::vector<CK_ID> before = DestinationsOf(output);
    const CK_ID outputId = output->GetID(), destinationId = destination->GetID();
    return edit.Do(
        [context, outputId, destinationId] {
            CKParameterOut *runtime = CKParameterOut::Cast(context->GetObject(outputId));
            CKParameter *target = CKParameter::Cast(context->GetObject(destinationId));
            if (!runtime || !target) return false;
            runtime->RemoveDestination(target);
            return true;
        },
        [context, outputId, before] { return RestoreDestinations(context, outputId, before); });
}


// Disconnects every reference to parameter held by an object outside
// excluded: pIns taking it as direct or shared source, pOuts sending to it.
bool DisconnectParameter(TopologyEdit &edit, CKObject *parameter, const std::set<CK_ID> &excluded) {
    CKContext *context = edit.Context();
    if (!parameter) return true;
    for (CKParameterIn *input : ObjectsOfClass<CKParameterIn>(context, CKCID_PARAMETERIN)) {
        if (excluded.count(input->GetID())) continue;
        const bool uses = input->GetDirectSource() == CKParameter::Cast(parameter) && CKParameter::Cast(parameter);
        const bool shares = CKParameterIn::Cast(parameter) && input->GetSharedSource() == parameter;
        if ((uses || shares) && !ClearSource(edit, input)) return false;
    }
    if (CKParameter *target = CKParameter::Cast(parameter)) {
        for (CKParameterOut *output : ObjectsOfClass<CKParameterOut>(context, CKCID_PARAMETEROUT)) {
            if (excluded.count(output->GetID())) continue;
            const std::vector<CK_ID> destinations = DestinationsOf(output);
            if (std::find(destinations.begin(), destinations.end(), target->GetID()) != destinations.end() &&
                !RemoveDestination(edit, output, target))
                return false;
        }
    }
    return true;
}

// Removes the CK relation a stored parameter link shows. Links without a CK
// relation need no CK change.
bool DisconnectParameterLink(TopologyEdit &edit, const BehaviorData &record, const Link &link, std::string &error) {
    CKContext *context = edit.Context();
    CKObject *start = ResolveEndpointObject(context, record, link.start);
    CKObject *end = ResolveEndpointObject(context, record, link.end);
    if (!start || !end || !LinkMatchesRuntime(context, record, link)) return true;
    if (start == end) {
        error = "A parameter exported through a behavior graph cannot be deleted here";
        return false;
    }
    if (CKParameterIn *input = CKParameterIn::Cast(end)) return ClearSource(edit, input);
    CKParameterOut *output = CKParameterOut::Cast(start);
    CKParameter *destination = CKParameter::Cast(end);
    return output && destination && RemoveDestination(edit, output, destination);
}

CK_ID ParentOf(const InterfaceData &data, CK_ID behavior) {
    const BehaviorData *record = FindRecord(data, behavior);
    return record ? record->parentId : 0;
}

Point GridCell(const BehaviorData &graph, bool scriptUi, const Point &position) {
    int column = static_cast<int>(std::floor((position.h - graph.rect.hPos) / GRID));
    int row = static_cast<int>(std::floor((position.v - graph.rect.vPos) / GRID));
    if (scriptUi) {
        column = std::max(column, ParameterDisplayGeometry::SCRIPT_MINIMUM_COLUMN);
        row = std::max(row, 0);
    }
    return Point(static_cast<float>(column), static_cast<float>(row));
}

const char *NameOf(CKObject *object) {
    return object && object->GetName() ? object->GetName() : "";
}

// Detach the IO without destroying it: the current edit owns its undo material.
bool RemoveIo(CKContext *context, CK_ID ownerId, bool input, int slot, CK_ID ioId) {
    CKBehavior *owner = BehaviorOf(context, ownerId);
    if (!owner) return false;
    CKBehaviorIO *removed = input ? owner->RemoveInput(slot) : owner->RemoveOutput(slot);
    return removed && removed == context->GetObject(ioId);
}

bool InsertIo(CKContext *context, CK_ID ownerId, bool input, int slot, CK_ID ioId) {
    CKBehavior *owner = BehaviorOf(context, ownerId);
    CKBehaviorIO *io = CKBehaviorIO::Cast(context->GetObject(ioId));
    return owner && io && InsertBehaviorIo(owner, slot, io, input) &&
           (input ? owner->GetInput(slot) : owner->GetOutput(slot)) == io;
}

bool SameKind(EndpointType type, PinKind kind) {
    switch (kind) {
    case PinKind::BehaviorIn: return type == ENDPOINT_BIN;
    case PinKind::BehaviorOut: return type == ENDPOINT_BOUT;
    case PinKind::ParamIn: return type == ENDPOINT_PIN;
    case PinKind::ParamOut: return type == ENDPOINT_POUT;
    default: return false;
    }
}

// Display side of removing pin slot `index` of `owner`: links on it go,
// later slots of the same kind are renumbered, position table entries
// (parameters only) are erased.
void RemovePinSlot(InterfaceData &data, CK_ID owner, PinKind kind, int index) {
    ForEachRecord(data, [&](BehaviorData &record) {
        auto on = [&](const LinkEndpoint &endpoint) {
            return endpoint.id == owner && SameKind(endpoint.type, kind) && endpoint.index == index;
        };
        auto &links = record.links;
        links.erase(std::remove_if(links.begin(), links.end(),
                                   [&](const Link &link) { return on(link.start) || on(link.end); }),
                    links.end());
        for (auto &link : links)
            for (LinkEndpoint *endpoint : {&link.start, &link.end})
                if (endpoint->id == owner && SameKind(endpoint->type, kind) && endpoint->index > index)
                    --endpoint->index;
    });
    BehaviorData *record = FindRecord(data, owner);
    if (!record) return;
    auto erase = [index](std::vector<int> &table) {
        if (index < static_cast<int>(table.size())) table.erase(table.begin() + index);
    };
    if (kind == PinKind::ParamIn)
        for (auto *table : {&record->inwardInputs, &record->outwardInputs, &record->expandedInputRows,
                            &record->foldedInputRows})
            erase(*table);
    if (kind == PinKind::ParamOut)
        for (auto *table : {&record->inwardOutputs, &record->outwardOutputs, &record->expandedOutputRows,
                            &record->foldedOutputRows})
            erase(*table);
}

} // namespace detail

} // namespace vdev
