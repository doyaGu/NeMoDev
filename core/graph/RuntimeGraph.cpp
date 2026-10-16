#include "core/graph/RuntimeGraph.h"

#include <cstring>
#include <map>
#include <sstream>

namespace vdev {

namespace {

CKBehavior *BehaviorOf(CKContext *context, CK_ID id) {
    return context ? CKBehavior::Cast(context->GetObject(id)) : nullptr;
}

std::string EndpointText(const LinkEndpoint &endpoint) {
    return std::to_string(endpoint.id) + ":" + std::to_string(endpoint.index) + ":" +
           std::to_string(static_cast<int>(endpoint.type));
}

std::string LinkText(const BehaviorData &graph, const Link &link) {
    return "graph " + std::to_string(graph.id) + (link.IsBehaviorLink() ? " behavior link " : " parameter link ") +
           std::to_string(link.id) + " " + EndpointText(link.start) + "->" + EndpointText(link.end);
}

std::vector<CKBehavior *> Tree(CKBehavior *script) {
    std::vector<CKBehavior *> tree;
    if (!script) return tree;
    tree.push_back(script);
    for (size_t i = 0; i < tree.size(); ++i)
        for (int c = 0; c < tree[i]->GetSubBehaviorCount(); ++c)
            if (CKBehavior *child = tree[i]->GetSubBehavior(c)) tree.push_back(child);
    return tree;
}

const char *NameOf(CKObject *object) {
    return object && object->GetName() ? object->GetName() : "";
}

CK_ID IdOf(CKObject *object) {
    return object ? object->GetID() : 0;
}

// Direct source (output or local parameter) or shared source (input).
bool TakesSourceFrom(CKParameterIn *input, CKObject *source) {
    if (!input || !source) return false;
    if (CKParameterIn *shared = CKParameterIn::Cast(source)) return input->GetSharedSource() == shared;
    return input->GetDirectSource() == CKParameter::Cast(source);
}

// A Target parameter is recreated by every UseTarget(TRUE), so inputs are
// named by owner and slot (-2 for the Target) rather than by CK_ID.
std::string InputSlotText(CKParameterIn *input) {
    if (!input) return "0";
    CKObject *owner = input->GetOwner();
    if (CKBehavior *behavior = CKBehavior::Cast(owner)) {
        const int slot = behavior->GetTargetParameter() == input ? -2 : behavior->GetInputParameterPosition(input);
        if (slot != -1) return std::to_string(owner->GetID()) + "#" + std::to_string(slot);
    } else if (CKParameterOperation *operation = CKParameterOperation::Cast(owner)) {
        return std::to_string(owner->GetID()) + "#" + (operation->GetInParameter1() == input ? "0" : "1");
    }
    return std::to_string(input->GetID());
}

std::string IoSlotText(CKBehaviorIO *io) {
    CKBehavior *owner = io ? io->GetOwner() : nullptr;
    if (!owner) return "0";
    const int input = owner->GetInputPosition(io);
    return std::to_string(owner->GetID()) + (input >= 0 ? "i" + std::to_string(input)
                                                        : "o" + std::to_string(owner->GetOutputPosition(io)));
}

} // namespace

CKObject *ResolveEndpointObject(CKContext *context, const BehaviorData &graph, const LinkEndpoint &endpoint) {
    if (!context) return nullptr;
    if (endpoint.type == ENDPOINT_POUT_SHORTCUT) {
        if (endpoint.index < 0 || endpoint.index >= static_cast<int>(graph.sharedParams.size())) return nullptr;
        return CKParameter::Cast(context->GetObject(graph.sharedParams[endpoint.index].sourceId));
    }
    CKObject *owner = context->GetObject(endpoint.id);
    if (!owner) return nullptr;
    if (endpoint.type == ENDPOINT_START_BIN) return CKBehaviorIO::Cast(owner);
    if (CKParameterOperation *operation = CKParameterOperation::Cast(owner)) {
        if (endpoint.type == ENDPOINT_PIN)
            return endpoint.index == 0 ? operation->GetInParameter1()
                 : endpoint.index == 1 ? operation->GetInParameter2()
                                       : nullptr;
        if (endpoint.type == ENDPOINT_POUT && endpoint.index == 0) return operation->GetOutParameter();
        return nullptr;
    }
    CKBehavior *behavior = CKBehavior::Cast(owner);
    if (!behavior) return nullptr;
    const int index = endpoint.index;
    switch (endpoint.type) {
    case ENDPOINT_BIN:
        return index >= 0 && index < behavior->GetInputCount() ? behavior->GetInput(index) : nullptr;
    case ENDPOINT_BOUT:
        return index >= 0 && index < behavior->GetOutputCount() ? behavior->GetOutput(index) : nullptr;
    case ENDPOINT_PIN:
        return index >= 0 && index < behavior->GetInputParameterCount() ? behavior->GetInputParameter(index) : nullptr;
    case ENDPOINT_TARGET_PIN:
        // The Target pin is displayed only while the behavior uses it.
        return behavior->IsUsingTarget() ? behavior->GetTargetParameter() : nullptr;
    case ENDPOINT_POUT:
        return index >= 0 && index < behavior->GetOutputParameterCount() ? behavior->GetOutputParameter(index)
                                                                          : nullptr;
    case ENDPOINT_PLOCAL:
        return index >= 0 && index < behavior->GetLocalParameterCount() ? behavior->GetLocalParameter(index) : nullptr;
    default:
        return nullptr;
    }
}

bool LinkMatchesRuntime(CKContext *context, const BehaviorData &graph, const Link &link) {
    CKObject *start = ResolveEndpointObject(context, graph, link.start);
    CKObject *end = ResolveEndpointObject(context, graph, link.end);
    if (!start || !end) return false;
    if (link.IsBehaviorLink()) {
        CKBehaviorLink *runtime = CKBehaviorLink::Cast(context->GetObject(link.id));
        CKBehavior *owner = BehaviorOf(context, graph.id);
        if (!runtime || !owner) return false;
        bool owned = false;
        for (int i = 0; i < owner->GetSubBehaviorLinkCount() && !owned; ++i)
            owned = owner->GetSubBehaviorLink(i) == runtime;
        return owned && runtime->GetInBehaviorIO() == start && runtime->GetOutBehaviorIO() == end;
    }
    if (start == end) return true;
    if (CKParameterIn *input = CKParameterIn::Cast(end)) {
        if (TakesSourceFrom(input, start)) return true;
        // Dev links mismatched types through an undisplayed conversion
        // operation and draws the link from the operation's source (Curve
        // Follow, Get Nearest Floors samples).
        return HiddenConversion(context, graph, link) != nullptr;
    }
    CKParameterOut *source = CKParameterOut::Cast(start);
    CKParameter *destination = CKParameter::Cast(end);
    if (!source || !destination) return false;
    for (int i = 0; i < source->GetDestinationCount(); ++i)
        if (source->GetDestination(i) == destination) return true;
    return false;
}

CKParameterOperation *HiddenConversion(CKContext *context, const BehaviorData &graph, const Link &link) {
    if (link.IsBehaviorLink()) return nullptr;
    CKObject *start = ResolveEndpointObject(context, graph, link.start);
    CKParameterIn *input = CKParameterIn::Cast(ResolveEndpointObject(context, graph, link.end));
    if (!start || !input) return nullptr;
    CKParameter *direct = input->GetDirectSource();
    CKParameterOperation *conversion = direct ? CKParameterOperation::Cast(direct->GetOwner()) : nullptr;
    if (!conversion) return nullptr;
    for (const auto &operation : graph.operations)
        if (operation.id == conversion->GetID()) return nullptr;
    return TakesSourceFrom(conversion->GetInParameter1(), start) || TakesSourceFrom(conversion->GetInParameter2(), start)
               ? conversion
               : nullptr;
}

std::set<CK_ID> HiddenConversions(CKContext *context, const InterfaceData &data) {
    std::set<CK_ID> conversions;
    auto visit = [&](const BehaviorData &record) {
        for (const auto &link : record.links)
            if (CKParameterOperation *conversion = HiddenConversion(context, record, link))
                conversions.insert(conversion->GetID());
    };
    visit(data.rootBehavior);
    for (const auto &record : data.behaviors) visit(record);
    return conversions;
}

std::set<std::string> RuntimeConsistencyIssues(CKContext *context, const InterfaceData &data, CKBehavior *script) {
    std::set<std::string> issues;
    if (!context || !script) {
        issues.insert("Script object missing");
        return issues;
    }
    std::map<CK_ID, const BehaviorData *> records;
    records[data.rootBehavior.id] = &data.rootBehavior;
    for (const auto &record : data.behaviors)
        if (!records.emplace(record.id, &record).second) issues.insert("duplicate record " + std::to_string(record.id));
    if (data.rootBehavior.id != script->GetID()) issues.insert("root record is not the Script");

    // Dev keeps a parameter operation link's conversion in the graph's
    // operation list without displaying it (2D Text.cmo "Convert").
    const std::set<CK_ID> conversions = HiddenConversions(context, data);
    std::set<CK_ID> inTree;
    for (CKBehavior *behavior : Tree(script)) {
        const CK_ID id = behavior->GetID();
        inTree.insert(id);
        auto found = records.find(id);
        if (found == records.end()) {
            issues.insert("behavior " + std::to_string(id) + " has no record");
            continue;
        }
        const BehaviorData &record = *found->second;
        if (behavior->GetType() == CKBEHAVIORTYPE_BASE) continue;
        std::set<CK_ID> runtimeLinks, storedLinks;
        for (int i = 0; i < behavior->GetSubBehaviorLinkCount(); ++i)
            if (CKBehaviorLink *link = behavior->GetSubBehaviorLink(i)) runtimeLinks.insert(link->GetID());
        for (const auto &link : record.links)
            if (link.IsBehaviorLink()) storedLinks.insert(link.id);
        for (CK_ID id : runtimeLinks)
            if (!storedLinks.count(id)) issues.insert("graph " + std::to_string(record.id) + " behavior link " +
                                                      std::to_string(id) + " not displayed");
        std::set<CK_ID> runtimeOperations, storedOperations;
        for (int i = 0; i < behavior->GetParameterOperationCount(); ++i)
            if (CKParameterOperation *operation = behavior->GetParameterOperation(i))
                if (!conversions.count(operation->GetID())) runtimeOperations.insert(operation->GetID());
        for (const auto &operation : record.operations) storedOperations.insert(operation.id);
        if (runtimeOperations != storedOperations)
            issues.insert("graph " + std::to_string(record.id) + " operations differ");
        if (static_cast<int>(record.localParams.size()) != behavior->GetLocalParameterCount())
            issues.insert("graph " + std::to_string(record.id) + " local parameter count differs");
    }
    for (const auto &entry : records) {
        if (!inTree.count(entry.first)) {
            issues.insert("record " + std::to_string(entry.first) + " is not in the Script tree");
            continue;
        }
        const BehaviorData &record = *entry.second;
        for (const auto &link : record.links)
            if (!LinkMatchesRuntime(context, record, link)) issues.insert(LinkText(record, link) + " not in CK");
    }
    return issues;
}

std::set<CK_ID> CollectOwnedObjects(CKContext *context, const std::set<CK_ID> &roots) {
    std::set<CK_ID> owned;
    std::vector<CKObject *> pending;
    for (CK_ID id : roots)
        if (CKObject *object = context->GetObject(id)) pending.push_back(object);
    while (!pending.empty()) {
        CKObject *object = pending.back();
        pending.pop_back();
        if (!object || !owned.insert(object->GetID()).second) continue;
        if (CKBehavior *behavior = CKBehavior::Cast(object)) {
            for (int i = 0; i < behavior->GetInputCount(); ++i) pending.push_back(behavior->GetInput(i));
            for (int i = 0; i < behavior->GetOutputCount(); ++i) pending.push_back(behavior->GetOutput(i));
            for (int i = 0; i < behavior->GetInputParameterCount(); ++i)
                pending.push_back(behavior->GetInputParameter(i));
            for (int i = 0; i < behavior->GetOutputParameterCount(); ++i)
                pending.push_back(behavior->GetOutputParameter(i));
            for (int i = 0; i < behavior->GetLocalParameterCount(); ++i)
                pending.push_back(behavior->GetLocalParameter(i));
            if (behavior->GetTargetParameter()) pending.push_back(behavior->GetTargetParameter());
            for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i) pending.push_back(behavior->GetSubBehavior(i));
            for (int i = 0; i < behavior->GetSubBehaviorLinkCount(); ++i)
                pending.push_back(behavior->GetSubBehaviorLink(i));
            for (int i = 0; i < behavior->GetParameterOperationCount(); ++i)
                pending.push_back(behavior->GetParameterOperation(i));
        } else if (CKParameterOperation *operation = CKParameterOperation::Cast(object)) {
            pending.push_back(operation->GetInParameter1());
            pending.push_back(operation->GetInParameter2());
            pending.push_back(operation->GetOutParameter());
        }
    }
    return owned;
}

std::string TopologyFingerprint(CKBehavior *script) {
    std::ostringstream out;
    auto input = [&](CKParameterIn *parameter) {
        if (!parameter) {
            out << " -";
            return;
        }
        out << " " << InputSlotText(parameter) << "'" << NameOf(parameter) << "' t" << parameter->GetType() << " <"
            << IdOf(parameter->GetDirectSource()) << "/" << InputSlotText(parameter->GetSharedSource()) << ">";
    };
    auto parameter = [&](CKParameter *parameter) {
        if (!parameter) {
            out << " -";
            return;
        }
        out << " " << parameter->GetID() << "'" << NameOf(parameter) << "' t" << parameter->GetType();
        // Local values are edit data (value holders of inputs); outputs are
        // computed at run time and are not part of the fingerprint.
        if (CKParameterLocal::Cast(parameter)) {
            const int size = parameter->GetStringValue(nullptr, FALSE);
            std::string value(static_cast<size_t>(size > 0 && size < 4096 ? size : 0), '\0');
            if (!value.empty()) parameter->GetStringValue(&value[0], FALSE);
            out << "=" << value.c_str();
        }
        if (CKParameterOut *output = CKParameterOut::Cast(parameter)) {
            out << " [";
            for (int i = 0; i < output->GetDestinationCount(); ++i) out << " " << IdOf(output->GetDestination(i));
            out << " ]";
        }
    };
    for (CKBehavior *behavior : Tree(script)) {
        out << "B " << behavior->GetID() << " '" << NameOf(behavior) << "' type " << behavior->GetType()
            << " parent " << IdOf(behavior->GetParent()) << " target " << (behavior->IsUsingTarget() ? 1 : 0)
            << " priority " << behavior->GetPriority() << "\n";
        // DeleteInput/DeleteOutput destroy the IO, so IOs are named by slot.
        out << " io";
        for (int i = 0; i < behavior->GetInputCount(); ++i) out << " i'" << NameOf(behavior->GetInput(i)) << "'";
        for (int i = 0; i < behavior->GetOutputCount(); ++i) out << " o'" << NameOf(behavior->GetOutput(i)) << "'";
        out << "\n pin";
        for (int i = 0; i < behavior->GetInputParameterCount(); ++i) input(behavior->GetInputParameter(i));
        if (behavior->IsUsingTarget()) input(behavior->GetTargetParameter());
        out << "\n pout";
        for (int i = 0; i < behavior->GetOutputParameterCount(); ++i) parameter(behavior->GetOutputParameter(i));
        out << "\n local";
        for (int i = 0; i < behavior->GetLocalParameterCount(); ++i) parameter(behavior->GetLocalParameter(i));
        out << "\n sub";
        for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i) out << " " << IdOf(behavior->GetSubBehavior(i));
        out << "\n links";
        for (int i = 0; i < behavior->GetSubBehaviorLinkCount(); ++i) {
            CKBehaviorLink *link = behavior->GetSubBehaviorLink(i);
            if (!link) continue;
            out << " " << link->GetID() << "(" << IoSlotText(link->GetInBehaviorIO()) << ">"
                << IoSlotText(link->GetOutBehaviorIO()) << " d" << link->GetInitialActivationDelay() << ")";
        }
        out << "\n ops";
        for (int i = 0; i < behavior->GetParameterOperationCount(); ++i) {
            CKParameterOperation *operation = behavior->GetParameterOperation(i);
            if (!operation) continue;
            const CKGUID guid = operation->GetOperationGuid();
            out << " {" << operation->GetID() << " " << guid.d1 << "-" << guid.d2;
            input(operation->GetInParameter1());
            input(operation->GetInParameter2());
            parameter(operation->GetOutParameter());
            out << " }";
        }
        out << "\n";
    }
    return out.str();
}

bool ManagesMemory(CKContext *context, CKParameterType type) {
    CKParameterManager *manager = context ? context->GetParameterManager() : nullptr;
    CKParameterTypeDesc *description = manager ? manager->GetParameterTypeDescription(type) : nullptr;
    if (!description) return false;
    if (description->DeleteFunction) return true;
    // Ordinary scalar and buffer copiers own no resources. Restoring them
    // through display text would lose float precision and buffer bytes.
    const CKParameterTypeDesc *scalar = manager->GetParameterTypeDescription(manager->ParameterGuidToType(CKPGUID_FLOAT));
    const CKParameterTypeDesc *buffer = manager->GetParameterTypeDescription(manager->ParameterGuidToType(CKPGUID_VOIDBUF));
    const CKParameterTypeDesc *vector = manager->GetParameterTypeDescription(manager->ParameterGuidToType(CKPGUID_VECTOR));
    return description->CopyFunction &&
        (!scalar || description->CopyFunction != scalar->CopyFunction) &&
        (!buffer || description->CopyFunction != buffer->CopyFunction) &&
        (!vector || description->CopyFunction != vector->CopyFunction);
}

std::string ParameterText(CKParameter *parameter) {
    if (!parameter) return std::string();
    const int size = parameter->GetStringValue(nullptr, FALSE);
    if (size <= 0 || size > 65536) return std::string();
    std::string text(static_cast<size_t>(size), '\0');
    parameter->GetStringValue(&text[0], FALSE);
    text.resize(std::strlen(text.c_str()));
    return text;
}

} // namespace vdev
