#pragma once

// Building blocks of the topology commands, shared by TopologyCommands.cpp
// and GroupCommands.cpp. Not part of the editor's public core API.

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/document/Document.h"
#include "core/schematic/SchematicGrid.h"
#include "core/graph/TopologyCommands.h"
#include "core/runtime/CkArrays.h"

namespace vdev {
namespace detail {

constexpr float GRID = SchematicGrid::SPACING;

CKBehavior *BehaviorOf(CKContext *context, CK_ID id);
std::set<CK_ID> TreeIds(CKBehavior *script);
bool EditableGraph(CKBehavior *graph);
CK_ID ParentOf(const InterfaceData &data, CK_ID behavior);
const char *NameOf(CKObject *object);
bool IsScriptUi(TopologyEdit &edit, const BehaviorData &record);

template <typename Data, typename Fn> void ForEachRecord(Data &data, Fn fn) {
    fn(data.rootBehavior);
    for (auto &record : data.behaviors) fn(record);
}

template <typename T> std::vector<T *> ObjectsOfClass(CKContext *context, CK_CLASSID cid) {
    std::vector<T *> objects;
    const int count = context->GetObjectsCountByClassID(cid);
    const CK_ID *ids = context->GetObjectsListByClassID(cid);
    for (int i = 0; i < count; ++i)
        if (T *object = T::Cast(context->GetObject(ids[i]))) objects.push_back(object);
    return objects;
}

// Without the discarded ones (Document::IsDiscarded): what decides whether a
// parameter is used counts the composition only. Rewiring every reference
// (a parameter going away) still takes ObjectsOfClass.
template <typename T> std::vector<T *> LiveObjectsOfClass(const Document &document, CK_CLASSID cid) {
    std::vector<T *> objects = ObjectsOfClass<T>(document.Context(), cid);
    objects.erase(std::remove_if(objects.begin(), objects.end(),
                                 [&](T *object) { return document.IsDiscarded(object->GetID()); }),
                  objects.end());
    return objects;
}

// A new Building Block of prototype (CKContext::CreateObject, then
// InitFromGuid guarded: its creation callback runs), recorded as made by the
// edit (rolled back with it). Null, error telling why, when it fails.
CKBehavior *NewBuildingBlock(TopologyEdit &edit, const CKGUID &prototype, std::string &error);
// What else uses local than input in the composition (an input reading it,
// an output writing it; the discarded skipped): empty when nothing does, as
// for Dev's value holder of input.
std::string OtherUseOf(const Document &document, CKParameterLocal *local, CKParameterIn *input);

// --- Sub-behaviors, links, operations (order-preserving attach/detach).
bool OwnerAccepts(CKBehavior *graph, CKBehavior *behavior);
bool AttachSubBehavior(CKContext *context, CK_ID parentId, CK_ID childId, int position);
bool DetachSubBehavior(CKContext *context, CK_ID parentId, CK_ID childId);
int SubBehaviorPosition(CKBehavior *parent, CKBehavior *child);

// A behavior IO named by owner and slot.
struct IoRef {
    CK_ID owner = 0;
    bool input = true;
    int index = -1;
};
IoRef IoRefOf(CKBehaviorIO *io);
CKBehaviorIO *Resolve(CKContext *context, const IoRef &ref);
bool AttachLink(CKContext *context, CK_ID graphId, CK_ID linkId, int position, const IoRef &in, const IoRef &out);
bool DetachLink(CKContext *context, CK_ID graphId, CK_ID linkId);
int LinkPosition(CKBehavior *graph, CKBehaviorLink *link);

bool AttachOperation(CKContext *context, CK_ID graphId, CK_ID operationId, int position);
bool DetachOperation(CKContext *context, CK_ID graphId, CK_ID operationId);
int OperationPosition(CKBehavior *graph, CKParameterOperation *operation);

// --- Parameters. Inputs are named by owner and slot (-2 Target).
struct InputRef {
    CK_ID id = 0;
    CK_ID owner = 0; // behavior or operation; 0 uses id
    int slot = 0;    // input slot, -2 Target, operation 0/1
};
InputRef RefOf(CKParameterIn *input);
CKParameterIn *Resolve(CKContext *context, const InputRef &ref);

struct InputSource {
    InputRef input;
    CK_ID direct = 0;
    InputRef shared;
};
InputSource SourceOf(CKParameterIn *input);
bool RestoreSource(CKContext *context, const InputSource &source);
std::vector<CK_ID> DestinationsOf(CKParameterOut *output);
bool RestoreDestinations(CKContext *context, CK_ID outputId, const std::vector<CK_ID> &destinations);

bool ClearSource(TopologyEdit &edit, CKParameterIn *input);
bool RemoveDestination(TopologyEdit &edit, CKParameterOut *output, CKParameter *destination);
bool DisconnectParameter(TopologyEdit &edit, CKObject *parameter, const std::set<CK_ID> &excluded);

// Behavior IO list entries (CK2's undeclared RemoveInput/RemoveOutput keep the IO alive).
bool RemoveIo(CKContext *context, CK_ID ownerId, bool input, int slot, CK_ID ioId);
bool InsertIo(CKContext *context, CK_ID ownerId, bool input, int slot, CK_ID ioId);

// --- Display records.
bool IsEndpoint(const LinkEndpoint &endpoint, EndpointType type, CK_ID id, int index);
void RemoveSharedSlot(BehaviorData &record, int slot);
void RemoveLocalSlot(InterfaceData &data, BehaviorData &graph, int slot);
// Links on pin slot `index` of `owner` go; later slots are renumbered.
void RemovePinSlot(InterfaceData &data, CK_ID owner, PinKind kind, int index);
// Grid cell of a Script-local position in graph (Script columns clamped).
Point GridCell(const BehaviorData &graph, bool scriptUi, const Point &position);

// Pairs a behavior with its CKContext::CopyObjects copy, object by object
// (IOs, parameters, locals, links, operations, sub-behaviors).
bool MapBehaviorCopy(CKBehavior *original, CKBehavior *copy, std::map<CK_ID, CK_ID> &map);

// Value locals (Dev's value holders) for the unfed inputs of a behavior
// placed in graph: input slot -> parameter whose value the local copies,
// else the prototype default.
using ValueBytes = std::map<int, CK_ID>;
bool AddInputValueLocals(TopologyEdit &edit, CK_ID graphId, CKBehavior *behavior, const BehaviorData &placed,
                         const ValueBytes *values = nullptr);

// Creates a local parameter of graph (appended) and its display record.
CKParameterLocal *CreateLocal(TopologyEdit &edit, CK_ID graphId, const std::string &name, const CKGUID &type,
                              int column, int row, ParameterStyle style);

// TopologyEdit's checks (TopologyPrimitives.cpp): the stored links that CK
// shows too; the display records a CK edit invalidated dropped (see
// TopologyCommands.h).
std::set<std::string> MatchingLinks(CKContext *context, const InterfaceData &data);
void Prune(CKContext *context, CKBehavior *script, InterfaceData &data, const std::set<std::string> &linksBefore,
           const std::set<CK_ID> &removed);
// Removes the CK relation a stored parameter link shows (Delete).
bool DisconnectParameterLink(TopologyEdit &edit, const BehaviorData &record, const Link &link, std::string &error);

} // namespace detail
} // namespace vdev
