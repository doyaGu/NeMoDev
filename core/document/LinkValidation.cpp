#include "core/document/LinkValidation.h"

#include <algorithm>
#include <map>

#include "core/document/Document.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/ScriptSession.h"

namespace vdev {

namespace {

// The parameter type at a link endpoint: an input's own type, else the
// parameter's (outputs, locals, shortcut sources).
CKParameterType EndpointType(CKObject *object) {
    if (CKParameterIn *input = CKParameterIn::Cast(object)) return input->GetType();
    if (CKParameter *parameter = CKParameter::Cast(object)) return parameter->GetType();
    return -1;
}

void Check(CKContext *context, CK_ID script, const BehaviorData &graph, bool testTypes, std::vector<InvalidLink> &out) {
    CKParameterManager *manager = context->GetParameterManager();
    for (int i = 0; i < static_cast<int>(graph.links.size()); ++i) {
        const Link &link = graph.links[i];
        InvalidLink invalid;
        invalid.script = script;
        invalid.graph = graph.id;
        invalid.slot = i;
        invalid.link = link.id;
        invalid.behaviorLink = link.IsBehaviorLink();
        CKObject *from = ResolveEndpointObject(context, graph, link.start);
        CKObject *to = ResolveEndpointObject(context, graph, link.end);
        if (!from || !to) {
            invalid.reason = !from ? "its start designates no object" : "its end designates no object";
        } else if (invalid.behaviorLink) {
            if (!LinkMatchesRuntime(context, graph, link)) invalid.reason = "no behavior link joins its IOs";
        } else if (testTypes && manager && !HiddenConversion(context, graph, link)) {
            const CKParameterType source = EndpointType(from), target = EndpointType(to);
            if (source >= 0 && target >= 0 && !manager->IsTypeCompatible(source, target)) {
                CKParameterTypeDesc *from = manager->GetParameterTypeDescription(source);
                CKParameterTypeDesc *to = manager->GetParameterTypeDescription(target);
                invalid.reason = std::string("incompatible types: ") + (from ? from->TypeName.CStr() : "?") + " to " +
                                 (to ? to->TypeName.CStr() : "?");
            }
        }
        if (!invalid.reason.empty()) out.push_back(invalid);
    }
}

} // namespace

std::vector<InvalidLink> FindInvalidLinks(Document &document, bool testParameterTypes) {
    std::vector<InvalidLink> invalid;
    CKContext *context = document.Context();
    if (!context) return invalid;
    for (const ScriptInfo &info : document.Scripts()) {
        if (!info.hasInterfaceChunk || document.IsDiscarded(info.id)) continue;
        ScriptSession *session = document.Session(info.id);
        if (!session || session->ReadOnly()) continue;
        const InterfaceData &data = session->Data();
        Check(context, info.id, data.rootBehavior, testParameterTypes, invalid);
        for (const BehaviorData &record : data.behaviors) Check(context, info.id, record, testParameterTypes, invalid);
    }
    return invalid;
}

int RemoveInvalidLinks(Document &document, const std::vector<InvalidLink> &links) {
    // Per record, the slots from the last so that the others stay valid.
    std::map<std::pair<CK_ID, CK_ID>, std::vector<int>> slots;
    for (const InvalidLink &link : links) slots[{link.script, link.graph}].push_back(link.slot);
    int removed = 0;
    for (auto &entry : slots) {
        ScriptSession *session = document.Session(entry.first.first);
        if (!session || session->ReadOnly()) continue;
        InterfaceData &data = session->Data();
        BehaviorData *record = data.rootBehavior.id == entry.first.second ? &data.rootBehavior : nullptr;
        for (BehaviorData &candidate : data.behaviors)
            if (!record && candidate.id == entry.first.second) record = &candidate;
        if (!record) continue;
        std::vector<int> &indices = entry.second;
        std::sort(indices.rbegin(), indices.rend());
        for (int index : indices)
            if (index >= 0 && index < static_cast<int>(record->links.size())) {
                record->links.erase(record->links.begin() + index);
                ++removed;
            }
        session->Touch();
    }
    return removed;
}

} // namespace vdev
