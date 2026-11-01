#include "core/document/LinkValidation.h"

#include <algorithm>
#include <map>

#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/schematic/SchematicEdits.h"
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
    for (auto &link : invalid)
        link.address = ElementAddress::Capture(document, link.script,
            {ElementKind::Link, link.graph, link.link, link.slot});
    return invalid;
}

namespace {
using LinkSlots = std::map<CK_ID, std::map<CK_ID, std::vector<int>>>;
LinkSlots ResolveLinks(Document &document, const std::vector<InvalidLink> &links) {
    LinkSlots slots;
    for (const auto &link : links) {
        CK_ID script = 0; ElementRef ref;
        if (link.address.Resolve(document, script, ref)) slots[script][ref.owner].push_back(ref.index);
    }
    return slots;
}
int EraseLinks(InterfaceData &data, const std::map<CK_ID, std::vector<int>> &graphs) {
    int removed = 0;
    for (const auto &entry : graphs) {
        auto *record = FindRecord(data, entry.first); if (!record) continue;
        auto indices = entry.second;
        std::sort(indices.rbegin(), indices.rend()); indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        for (auto index : indices) if (index >= 0 && size_t(index) < record->links.size()) {
            record->links.erase(record->links.begin() + index); ++removed;
        }
    }
    return removed;
}
} // namespace
int RemoveInvalidLinks(Document &document, const std::vector<InvalidLink> &links) {
    const auto slots = ResolveLinks(document, links); int removed = 0;
    for (const auto &entry : slots) {
        auto *session = document.Session(entry.first); if (!session || session->ReadOnly()) continue;
        // Loading keeps the original per-graph assignment and revision behavior.
        for (const auto &graph : entry.second) {
            InterfaceData data = session->Data(); const auto count = EraseLinks(data, {{graph.first, graph.second}});
            if (count && session->Assign(data)) removed += count;
        }
    }
    return removed;
}
EditResult DeleteInvalidLinks(Document &document, const std::vector<InvalidLink> &links) {
    for (const auto &link : links) {
        CK_ID script = 0; ElementRef ref;
        if (!link.address.Resolve(document, script, ref) || ref.kind != ElementKind::Link)
            return EditResult::Refused("The invalid link selection has changed");
        auto *session = document.Session(script);
        if (!session || session->ReadOnly()) return EditResult::Refused("The Script display is read-only");
    }
    // Resolve the complete selection before ShowScript changes a revision.
    const auto slots = ResolveLinks(document, links);
    Edit edit(document, links.size() > 1 ? "Delete Invalid Links" : "Delete Invalid Link", Edit::Marks::Scripts);
    for (const auto &entry : slots) {
        InterfaceData after = document.Session(entry.first)->Data();
        if (EraseLinks(after, entry.second) && !edit.ShowScript(entry.first, after)) return edit.Fail("Cannot remove the invalid links");
    }
    return edit.Commit();
}

} // namespace vdev
