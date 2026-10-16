#include "core/graph/ExportCommands.h"

#include <algorithm>
#include <cmath>

#include "core/schematic/LinkEndpointGeometry.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Document.h"
#include "core/schematic/SchematicEdits.h"
#include "core/graph/detail/TopologyInternal.h"

namespace vdev {

using namespace detail;

namespace {

bool SameEndpoint(const LinkEndpoint &a, const LinkEndpoint &b) {
    return a.id == b.id && a.index == b.index && a.type == b.type;
}

// True when into is graph or lies inside it.
bool Encloses(const InterfaceData &data, CK_ID graph, CK_ID into) {
    for (CK_ID at = into; at; at = ParentOf(data, at))
        if (at == graph) return true;
    return false;
}

// Puts an input (output) parameter at slot of a graph's list without
// changing its owner: later entries are taken out and exported again, since
// adding it to the list (CKBehavior::AddInputParameter) makes the graph the owner.
bool ListParameterAt(CKContext *context, CK_ID graphId, bool input, int slot, CK_ID id) {
    CKBehavior *graph = BehaviorOf(context, graphId);
    CKObject *object = context->GetObject(id);
    if (!graph || !object || slot < 0) return false;
    if (input) {
        CKParameterIn *parameter = CKParameterIn::Cast(object);
        if (!parameter || slot > graph->GetInputParameterCount()) return false;
        std::vector<CKParameterIn *> after;
        while (graph->GetInputParameterCount() > slot)
            after.insert(after.begin(), graph->RemoveInputParameter(graph->GetInputParameterCount() - 1));
        graph->ExportInputParameter(parameter);
        for (CKParameterIn *other : after) graph->ExportInputParameter(other);
        return graph->GetInputParameter(slot) == parameter;
    }
    CKParameterOut *parameter = CKParameterOut::Cast(object);
    if (!parameter || slot > graph->GetOutputParameterCount()) return false;
    std::vector<CKParameterOut *> after;
    while (graph->GetOutputParameterCount() > slot)
        after.insert(after.begin(), graph->RemoveOutputParameter(graph->GetOutputParameterCount() - 1));
    graph->ExportOutputParameter(parameter);
    for (CKParameterOut *other : after) graph->ExportOutputParameter(other);
    return graph->GetOutputParameter(slot) == parameter;
}

bool UnlistParameter(CKContext *context, CK_ID graphId, bool input, int slot, CK_ID id) {
    CKBehavior *graph = BehaviorOf(context, graphId);
    CKObject *object = context->GetObject(id);
    if (!graph || !object) return false;
    if (input) {
        if (slot < 0 || slot >= graph->GetInputParameterCount() || graph->GetInputParameter(slot) != object)
            return false;
        return graph->RemoveInputParameter(slot) == object;
    }
    if (slot < 0 || slot >= graph->GetOutputParameterCount() || graph->GetOutputParameter(slot) != object)
        return false;
    return graph->RemoveOutputParameter(slot) == object;
}

// Adding a local to a list (CKBehavior::AddLocalParameter) makes the receiving graph the owner.
bool MoveLocal(CKContext *context, CK_ID fromId, int fromSlot, CK_ID toId, int toSlot, CK_ID id) {
    CKBehavior *from = BehaviorOf(context, fromId), *to = BehaviorOf(context, toId);
    CKParameterLocal *local = CKParameterLocal::Cast(context->GetObject(id));
    if (!from || !to || !local || from->GetLocalParameter(fromSlot) != local) return false;
    if (from->RemoveLocalParameter(fromSlot) != local) return false;
    return InsertLocalParameter(to, toSlot, local) && to->GetLocalParameter(toSlot) == local;
}

// The displayed source of an input that leaves (or enters) the graph
// showing it: nothing, a local value holder, or a shortcut, used by this
// input only.
struct SourceDisplay {
    enum Kind { None, Local, Shortcut } kind = None;
    int slot = -1;
    CK_ID local = 0;
    Parameter record;
};

bool TakeSourceDisplay(TopologyEdit &edit, CK_ID shownGraph, const LinkEndpoint &pin, CKParameterIn *input,
                       SourceDisplay &source, std::string &error) {
    CKContext *context = edit.Context();
    InterfaceData &data = edit.Data();
    const BehaviorData *shown = FindRecord(data, shownGraph);
    if (!shown) {
        error = "The input's graph has no display record";
        return false;
    }
    const Link *found = nullptr;
    for (const auto &link : shown->links)
        if (!link.highlight && !link.IsBehaviorLink() && SameEndpoint(link.end, pin)) found = &link;
    if (!found) {
        if (input->GetDirectSource() || input->GetSharedSource()) {
            error = "The input's source is not displayed";
            return false;
        }
        return true;
    }
    if (HiddenConversion(context, *shown, *found)) {
        error = "An input fed through a parameter operation link cannot leave its graph";
        return false;
    }
    const LinkEndpoint from = found->start;
    if (from.id != shownGraph || (from.type != ENDPOINT_PLOCAL && from.type != ENDPOINT_POUT_SHORTCUT)) {
        error = "Only an input fed by a local parameter or a shortcut (or not fed) can leave its graph";
        return false;
    }
    // The source display goes with the input: refuse it when shared.
    int uses = 0;
    ForEachRecord(data, [&](BehaviorData &record) {
        for (const auto &link : record.links)
            if (SameEndpoint(link.start, from) || SameEndpoint(link.end, from)) ++uses;
    });
    if (uses != 1) {
        error = "The input's source has other links";
        return false;
    }
    source.slot = from.index;
    if (from.type == ENDPOINT_POUT_SHORTCUT) {
        if (from.index < 0 || from.index >= static_cast<int>(shown->sharedParams.size())) return false;
        source.kind = SourceDisplay::Shortcut;
        source.record = shown->sharedParams[from.index];
        return true;
    }
    CKBehavior *graph = BehaviorOf(context, shownGraph);
    CKParameterLocal *local = graph ? graph->GetLocalParameter(from.index) : nullptr;
    if (!local || input->GetDirectSource() != local ||
        from.index >= static_cast<int>(shown->localParams.size())) {
        error = "The input's local parameter does not match the runtime graph";
        return false;
    }
    // The input's value holder only (a deleted behavior's inputs and outputs naming it are not uses).
    error = OtherUseOf(edit.Composition(), local, input);
    if (!error.empty()) return false;
    source.kind = SourceDisplay::Local;
    source.local = local->GetID();
    source.record = shown->localParams[from.index];
    return true;
}

bool RootIsScript(TopologyEdit &edit) {
    return (edit.Script()->GetType() & CKBEHAVIORTYPE_SCRIPT) != 0;
}

// Moves the source display from graph `from` to graph `to`, linked to pin.
bool MoveSourceDisplay(TopologyEdit &edit, CK_ID fromId, const SourceDisplay &source, CK_ID toId,
                       const LinkEndpoint &pin, const Point *position) {
    if (source.kind == SourceDisplay::None) return true;
    CKContext *context = edit.Context();
    InterfaceData &data = edit.Data();
    BehaviorData *from = FindRecord(data, fromId);
    BehaviorData *to = FindRecord(data, toId);
    CKBehavior *toGraph = BehaviorOf(context, toId);
    if (!from || !to || !toGraph) return false;
    Point at;
    if (position) {
        at = *position;
    } else {
        // Collapsed, two rows above the pin (library choice).
        Point pinPosition;
        if (!TryGetLinkEndpointPosition(data, pin, pinPosition, RootIsScript(edit))) pinPosition = Point(to->rect.hPos, to->rect.vPos);
        at = Point(pinPosition.h - GRID * 0.5f, pinPosition.v - GRID * 2.0f);
    }
    const Point cell = GridCell(*to, IsScriptUi(edit, *to), at);
    Parameter record = source.record;
    record.hPos = static_cast<int>(cell.h);
    record.vPos = static_cast<int>(cell.v);
    if (source.kind == SourceDisplay::Shortcut) {
        RemoveSharedSlot(*from, source.slot);
        to->sharedParams.push_back(record);
        const int slot = static_cast<int>(to->sharedParams.size()) - 1;
        to->links.push_back(Link(0, LINK_TYPE_PARAMETER, LinkEndpoint(toId, slot, ENDPOINT_POUT_SHORTCUT), pin));
        return true;
    }
    const int toSlot = toGraph->GetLocalParameterCount();
    const int fromSlot = source.slot;
    const CK_ID id = source.local;
    if (!edit.Do([context, fromId, fromSlot, toId, toSlot, id] {
            return MoveLocal(context, fromId, fromSlot, toId, toSlot, id);
        },
                 [context, fromId, fromSlot, toId, toSlot, id] {
                     return MoveLocal(context, toId, toSlot, fromId, fromSlot, id);
                 }))
        return false;
    RemoveLocalSlot(data, *from, fromSlot);
    to = FindRecord(data, toId);
    to->localParams.push_back(record);
    to->links.push_back(Link(0, LINK_TYPE_PARAMETER, LinkEndpoint(toId, toSlot, ENDPOINT_PLOCAL), pin));
    return true;
}

// A folded graph grows to show its pins (as AddGraphPin).
void FitPins(BehaviorData &record, CKBehavior *graph) {
    const float rows = static_cast<float>(std::max(graph->GetInputCount(), graph->GetOutputCount()));
    const float columns = static_cast<float>(std::max(graph->GetInputParameterCount(), graph->GetOutputParameterCount()) +
                                             (graph->IsUsingTarget() ? 1 : 0));
    record.rect.vSize = std::max(record.rect.vSize, rows * GRID);
    record.rect.hSize = std::max(record.rect.hSize, columns * GRID);
}

bool ExportSteps(TopologyEdit &edit, const LinkEndpoint &pin, CK_ID into, const Point *localPosition,
                 std::string &error) {
    CKContext *context = edit.Context();
    InterfaceData &data = edit.Data();
    const bool input = pin.type == ENDPOINT_PIN;
    CKBehavior *owner = BehaviorOf(context, pin.id);
    if ((!input && pin.type != ENDPOINT_POUT) || !owner || !TreeIds(edit.Script()).count(pin.id)) {
        error = "Only input and output parameters of behaviors can be exported";
        return false;
    }
    CKObject *parameter = nullptr;
    if (input && pin.index >= 0 && pin.index < owner->GetInputParameterCount())
        parameter = owner->GetInputParameter(pin.index);
    if (!input && pin.index >= 0 && pin.index < owner->GetOutputParameterCount())
        parameter = owner->GetOutputParameter(pin.index);
    if (!parameter) {
        error = "The parameter is not in the runtime graph";
        return false;
    }
    const std::vector<CK_ID> levels = GraphsBetween(data, pin.id, into);
    if (levels.empty()) {
        error = "Drop the parameter outside its behavior graph, in a graph enclosing it";
        return false;
    }
    for (CK_ID level : levels) {
        CKBehavior *graph = BehaviorOf(context, level);
        if (!EditableGraph(graph)) {
            error = "Parameters can only be exported through behavior graphs";
            return false;
        }
        const int listed = input ? graph->GetInputParameterPosition(CKParameterIn::Cast(parameter))
                                 : graph->GetOutputParameterPosition(CKParameterOut::Cast(parameter));
        if (listed >= 0) {
            error = "The parameter is already exported by this graph";
            return false;
        }
    }
    SourceDisplay source;
    if (input && !TakeSourceDisplay(edit, levels.front(), pin, CKParameterIn::Cast(parameter), source, error))
        return false;
    const CK_ID id = parameter->GetID();
    LinkEndpoint inner = pin;
    for (CK_ID level : levels) {
        CKBehavior *graph = BehaviorOf(context, level);
        const int slot = input ? graph->GetInputParameterCount() : graph->GetOutputParameterCount();
        if (!edit.Do([context, level, input, slot, id] { return ListParameterAt(context, level, input, slot, id); },
                     [context, level, input, slot, id] { return UnlistParameter(context, level, input, slot, id); })) {
            error = "CKBehavior::ExportInputParameter failed";
            return false;
        }
        BehaviorData *record = FindRecord(data, level);
        const LinkEndpoint outer(level, slot, pin.type);
        record->links.push_back(input ? Link(0, LINK_TYPE_PARAMETER, outer, inner, true)
                                      : Link(0, LINK_TYPE_PARAMETER, inner, outer, true));
        FitPins(*record, graph);
        inner = outer;
    }
    if (!MoveSourceDisplay(edit, levels.front(), source, into, inner, localPosition)) {
        error = "Cannot move the input's source";
        return false;
    }
    return true;
}

// The pin a graph's exported link reaches inside: input links go from the
// graph pin to the inner pin, output links the other way.
bool InnerPin(const InterfaceData &data, CK_ID graph, bool input, int slot, LinkEndpoint &inner) {
    const BehaviorData *record = FindRecord(data, graph);
    if (!record) return false;
    const LinkEndpoint outer(graph, slot, input ? ENDPOINT_PIN : ENDPOINT_POUT);
    for (const auto &link : record->links) {
        if (!link.highlight || link.IsBehaviorLink()) continue;
        if (input && SameEndpoint(link.start, outer) && link.end.id != graph) {
            inner = link.end;
            return true;
        }
        if (!input && SameEndpoint(link.end, outer) && link.start.id != graph) {
            inner = link.start;
            return true;
        }
    }
    return false;
}

bool ReimportSteps(TopologyEdit &edit, CK_ID graphId, PinKind kind, int slot, CK_ID into, const Point *localPosition,
                   std::string &error) {
    CKContext *context = edit.Context();
    InterfaceData &data = edit.Data();
    const bool input = kind == PinKind::ParamIn;
    CKBehavior *graph = BehaviorOf(context, graphId);
    if ((!input && kind != PinKind::ParamOut) || !EditableGraph(graph) || !TreeIds(edit.Script()).count(graphId)) {
        error = "Only parameters of behavior graphs can be re-imported";
        return false;
    }
    CKObject *parameter = nullptr;
    if (input && slot >= 0 && slot < graph->GetInputParameterCount()) parameter = graph->GetInputParameter(slot);
    if (!input && slot >= 0 && slot < graph->GetOutputParameterCount()) parameter = graph->GetOutputParameter(slot);
    CKObject *owner = parameter ? (input ? CKParameterIn::Cast(parameter)->GetOwner()
                                         : CKParameterOut::Cast(parameter)->GetOwner())
                                : nullptr;
    if (!parameter || !owner || owner == graph) {
        error = "Only an exported parameter can be re-imported";
        return false;
    }
    if (!Encloses(data, graphId, into)) {
        error = "Drop the exported parameter inside its graph";
        return false;
    }
    const CK_ID parentId = ParentOf(data, graphId);
    CKBehavior *parent = BehaviorOf(context, parentId);
    if (!parent) {
        error = "The graph has no parent graph";
        return false;
    }
    const int above = input ? parent->GetInputParameterPosition(CKParameterIn::Cast(parameter))
                            : parent->GetOutputParameterPosition(CKParameterOut::Cast(parameter));
    if (above >= 0) {
        error = "The parameter is exported further up: re-import it from the outermost graph";
        return false;
    }
    // Levels to remove, outermost first.
    std::vector<std::pair<CK_ID, int>> levels{{graphId, slot}};
    LinkEndpoint inner;
    if (!InnerPin(data, graphId, input, slot, inner)) {
        error = "The exported link is not displayed";
        return false;
    }
    while (inner.id != owner->GetID() && Encloses(data, inner.id, into)) {
        levels.emplace_back(inner.id, inner.index);
        if (!InnerPin(data, inner.id, input, inner.index, inner)) {
            error = "The exported link is not displayed";
            return false;
        }
    }
    const LinkEndpoint outerPin(graphId, slot, input ? ENDPOINT_PIN : ENDPOINT_POUT);
    SourceDisplay source;
    if (input) {
        if (!TakeSourceDisplay(edit, parentId, outerPin, CKParameterIn::Cast(parameter), source, error)) return false;
    } else {
        const BehaviorData *record = FindRecord(data, parentId);
        for (const auto &link : record ? record->links : std::vector<Link>())
            if (SameEndpoint(link.start, outerPin) || SameEndpoint(link.end, outerPin)) {
                error = "The exported output is used outside the graph: remove its links first";
                return false;
            }
    }
    const CK_ID id = parameter->GetID();
    for (const auto &level : levels) {
        const CK_ID levelId = level.first;
        const int at = level.second;
        if (!edit.Do([context, levelId, input, at, id] { return UnlistParameter(context, levelId, input, at, id); },
                     [context, levelId, input, at, id] { return ListParameterAt(context, levelId, input, at, id); })) {
            error = "Cannot remove the exported parameter from its graph";
            return false;
        }
    }
    // RemovePinSlot also drops the source's link to the outer pin.
    for (const auto &level : levels) RemovePinSlot(data, level.first, kind, level.second);
    if (!MoveSourceDisplay(edit, parentId, source, levels.back().first, inner, localPosition)) {
        error = "Cannot move the input's source";
        return false;
    }
    return true;
}

} // namespace

std::vector<CK_ID> GraphsBetween(const InterfaceData &data, CK_ID owner, CK_ID into) {
    std::vector<CK_ID> levels;
    CK_ID graph = ParentOf(data, owner);
    while (graph && graph != into && levels.size() < 256) {
        levels.push_back(graph);
        graph = ParentOf(data, graph);
    }
    if (graph != into || !graph) levels.clear();
    return levels;
}

EditResult ExportParameter(Document &document, CK_ID script, const LinkEndpoint &pin,
                           CK_ID into, const Point *localPosition) {
    TopologyEdit edit(document, script, "Export Parameter");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    std::string error;
    if (!ExportSteps(edit, pin, into, localPosition, error)) return edit.Fail(error);
    return edit.Commit();
}

EditResult ReimportParameter(Document &document, CK_ID script, CK_ID graph, PinKind kind,
                             int slot, CK_ID into, const Point *localPosition) {
    TopologyEdit edit(document, script, "Re-import Parameter");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    std::string error;
    if (!ReimportSteps(edit, graph, kind, slot, into, localPosition, error)) return edit.Fail(error);
    return edit.Commit();
}

EditResult MoveLocalAcrossGraphs(Document &document, CK_ID script, CK_ID graph, int slot,
                                 CK_ID into, const Point &position) {
    TopologyEdit edit(document, script, "Export Local Parameter");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    InterfaceData &data = edit.Data();
    const LinkEndpoint local(graph, slot, ENDPOINT_PLOCAL);
    const Link *only = nullptr;
    int uses = 0;
    ForEachRecord(data, [&](BehaviorData &record) {
        for (const auto &link : record.links)
            if (SameEndpoint(link.start, local) || SameEndpoint(link.end, local)) {
                ++uses;
                if (record.id == graph && SameEndpoint(link.start, local) && link.end.type == ENDPOINT_PIN)
                    only = &link;
            }
    });
    if (uses != 1 || !only) return edit.Fail("Only a local parameter linked to one input can leave its graph");
    const LinkEndpoint input = only->end;
    std::string error;
    if (into != graph && Encloses(data, into, graph)) {
        if (!ExportSteps(edit, input, into, &position, error)) return edit.Fail(error);
    } else if (input.id != graph && Encloses(data, input.id, into) && ParentOf(data, input.id) == graph) {
        if (!ReimportSteps(edit, input.id, PinKind::ParamIn, input.index, into, &position, error))
            return edit.Fail(error);
    } else {
        return edit.Fail("Drag the local up out of its graph, or into the graph exporting the input it feeds");
    }
    return edit.Commit();
}

EditResult ExportBehaviorIo(Document &document, CK_ID script, const LinkEndpoint &io,
                            CK_ID into) {
    TopologyEdit edit(document, script, "Export Behavior Pin");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    InterfaceData &data = edit.Data();
    const bool input = io.type == ENDPOINT_BIN;
    CKBehavior *owner = BehaviorOf(context, io.id);
    if ((!input && io.type != ENDPOINT_BOUT) || !owner || io.id == script || !TreeIds(edit.Script()).count(io.id))
        return edit.Fail("Only behavior inputs and outputs can be exported");
    CKBehaviorIO *pin = nullptr;
    if (input && io.index >= 0 && io.index < owner->GetInputCount()) pin = owner->GetInput(io.index);
    if (!input && io.index >= 0 && io.index < owner->GetOutputCount()) pin = owner->GetOutput(io.index);
    if (!pin) return edit.Fail("The pin is not in the runtime graph");
    const std::vector<CK_ID> levels = GraphsBetween(data, io.id, into);
    if (levels.empty()) return edit.Fail("Drop the pin outside its behavior graph, in a graph enclosing it");
    const std::string name = pin->GetName() ? pin->GetName() : (input ? "In" : "Out");
    LinkEndpoint inner = io;
    IoRef innerRef = IoRefOf(pin);
    for (CK_ID level : levels) {
        CKBehavior *graph = BehaviorOf(context, level);
        if (!EditableGraph(graph)) return edit.Fail("Pins can only be exported through behavior graphs");
        CKBehaviorIO *added = input ? graph->CreateInput(const_cast<CKSTRING>(name.c_str()))
                                    : graph->CreateOutput(const_cast<CKSTRING>(name.c_str()));
        if (!added) return edit.Fail("Cannot add the behavior pin");
        const CK_ID addedId = added->GetID();
        const int slot = input ? graph->GetInputCount() - 1 : graph->GetOutputCount() - 1;
        edit.Created(addedId);
        edit.Done([context, level, input, slot, addedId] { return InsertIo(context, level, input, slot, addedId); },
                  [context, level, input, slot, addedId] { return RemoveIo(context, level, input, slot, addedId); });
        CKBehaviorLink *link = CKBehaviorLink::Cast(context->CreateObject(CKCID_BEHAVIORLINK));
        if (!link) return edit.Fail("CKContext::CreateObject failed");
        const CK_ID linkId = link->GetID();
        edit.Created(linkId);
        link->SetInitialActivationDelay(0);
        link->SetActivationDelay(0);
        const IoRef addedRef = IoRefOf(added);
        const IoRef in = input ? addedRef : innerRef, out = input ? innerRef : addedRef;
        const int at = graph->GetSubBehaviorLinkCount();
        if (!edit.Do([context, level, linkId, at, in, out] { return AttachLink(context, level, linkId, at, in, out); },
                     [context, level, linkId] { return DetachLink(context, level, linkId); }))
            return edit.Fail("CKBehavior::AddSubBehaviorLink failed");
        const LinkEndpoint outer(level, slot, io.type);
        BehaviorData *record = FindRecord(data, level);
        record->links.push_back(input ? Link(linkId, LINK_TYPE_BEHAVIOR, outer, inner)
                                      : Link(linkId, LINK_TYPE_BEHAVIOR, inner, outer));
        FitPins(*record, graph);
        inner = outer;
        innerRef = addedRef;
    }
    return edit.Commit();
}

bool MoveGraphParameter(Document &document, CK_ID script, CK_ID graphId, PinKind kind,
                        int slot, int column, bool top) {
    const bool input = kind == PinKind::ParamIn;
    if ((!input && kind != PinKind::ParamOut) || slot < 0 || column < 0) return false;
    CKBehavior *graph = CKBehavior::Cast(document.Context()->GetObject(graphId));
    if (!graph || graph->GetType() == CKBEHAVIORTYPE_BASE) return false;
    if (slot >= (input ? graph->GetInputParameterCount() : graph->GetOutputParameterCount())) return false;
    bool changed = false;
    const bool edited = EditScriptData(document, script, "Move Parameter", [&](InterfaceData &data) {
        BehaviorData *record = FindRecord(data, graphId);
        if (!record) return;
        auto positions = input ? record->GetBehaviorInputPositions(record->folded)
                               : record->GetBehaviorOutputPositions(record->folded);
        // Earlier pins without an entry keep their default place: column
        // = slot, inputs on top, outputs at the bottom.
        while (static_cast<int>(positions.size()) <= slot) {
            const int index = static_cast<int>(positions.size());
            positions.emplace_back(index, input ? -1 : 1);
        }
        positions[slot] = BehaviorIOPosition(column, top ? -1 : 1);
        if (input) record->SetBehaviorInputPositions(record->folded, positions);
        else record->SetBehaviorOutputPositions(record->folded, positions);
        changed = true;
    });
    return edited && changed;
}

std::vector<EditResult> ExportPinsAt(Document &document, CK_ID script, const std::vector<PinItem> &pins,
                                     const Point &at) {
    constexpr float GRID_SIZE = 20.0f;
    std::vector<EditResult> refused;
    ScriptSession *session = document.Session(script);
    if (!session || pins.empty()) return refused;
    SchematicModel model;
    model.Build(*session, document.Context());
    if (!model.script.open) return refused;
    CK_ID into = model.GraphAt(at);
    if (!into) into = script;
    EditGroup group(document, "Export");
    for (const PinItem &pin : pins) {
        const InterfaceData &data = session->Data();
        const BehaviorItem *owner = model.FindBehavior(pin.owner);
        const bool parameter = pin.kind == PinKind::ParamIn || pin.kind == PinKind::ParamOut;
        EditResult result = EditResult::Refused(std::string());
        if (parameter && owner && owner->graph && !owner->buildingBlock) {
            // Dev: a graph parameter dropped on the graph's top or bottom
            // border takes that place (pIn/pOut position on a behavior graph).
            const Rect &box = owner->box;
            const float top = std::fabs(at.v - box.vPos), bottom = std::fabs(at.v - box.Bottom());
            const BehaviorData *record = FindRecord(data, pin.owner);
            if (pins.size() == 1 && record && at.h >= box.hPos - 10.0f && at.h <= box.Right() + 10.0f &&
                std::min(top, bottom) <= 12.0f) {
                const int column = std::max(0, static_cast<int>(std::floor((at.h - record->rect.hPos) / GRID_SIZE)));
                MoveGraphParameter(document, script, pin.owner, pin.kind, pin.index, column, top <= bottom);
                continue;
            }
            if (Encloses(data, pin.owner, into))
                result = ReimportParameter(document, script, pin.owner, pin.kind, pin.index, into);
            else
                result = ExportParameter(document, script, pin.endpoint, into);
        } else if (parameter) {
            result = ExportParameter(document, script, pin.endpoint, into);
        } else {
            result = ExportBehaviorIo(document, script, pin.endpoint, into);
        }
        if (!result.ok) refused.push_back(result);
    }
    return refused;
}

void MoveLocalsAcrossGraphs(Document &document, CK_ID script, CK_ID graph,
                            const std::vector<std::pair<CK_ID, Point>> &locals, CK_ID into, size_t &moved,
                            std::string &refusal) {
    moved = 0;
    refusal.clear();
    CKContext *context = document.Context();
    CKBehavior *owner = CKBehavior::Cast(context->GetObject(graph));
    EditGroup group(document, "Export Local Parameter");
    for (const auto &entry : locals) {
        CKParameterLocal *parameter = CKParameterLocal::Cast(context->GetObject(entry.first));
        const int slot = owner && parameter ? owner->GetLocalParameterPosition(parameter) : -1;
        if (slot < 0) continue;
        const EditResult result = MoveLocalAcrossGraphs(document, script, graph, slot, into, entry.second);
        if (result.ok) ++moved;
        else refusal = result.message;
    }
}

} // namespace vdev
