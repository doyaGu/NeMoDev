#include "core/graph/OperationCommands.h"

#include <utility>
#include <vector>

#include "core/document/Document.h"
#include "core/document/ScriptSession.h"
#include "core/graph/TopologyCommands.h"
#include "core/schematic/SchematicModel.h"

namespace vdev {

namespace {

// The Script's links and operations as the Schematic has them.
bool ModelOf(Document &document, CK_ID script, SchematicModel &model) {
    ScriptSession *session = document.Session(script);
    if (!session) return false;
    model.Build(*session, document.Context());
    return true;
}

} // namespace

EditResult ReplaceParameterOperation(Document &document, CK_ID script, CK_ID operation,
                                     const OperationSignature &signature, int *dropped) {
    if (dropped) *dropped = 0;
    SchematicModel model;
    if (!ModelOf(document, script, model)) return EditResult::Refused("The Script is not displayed");
    const OperationItem *found = nullptr;
    for (const auto &item : model.operations)
        if (item.id == operation) found = &item;
    if (!found) return EditResult::Refused("The parameter operation is not displayed");
    const OperationItem previous = *found;
    // Its parameter links (0: the new operation), those to a disabled input too.
    std::vector<std::pair<LinkEndpoint, LinkEndpoint>> relinks;
    for (const auto &link : model.links) {
        if (link.behaviorLink) continue;
        if (link.end.id == operation && link.end.type == ENDPOINT_PIN)
            relinks.push_back({link.start, LinkEndpoint(0, link.end.index, ENDPOINT_PIN)});
        else if (link.start.id == operation && link.start.type == ENDPOINT_POUT)
            relinks.push_back({LinkEndpoint(0, 0, ENDPOINT_POUT), link.end});
    }
    Edit edit(document, "Modify Parameter Operation");
    const EditResult created = AddParameterOperation(document, script, previous.owner, signature.operation,
                                                     signature.result, signature.input1, signature.input2,
                                                     Point(previous.bounds.hPos, previous.bounds.vPos));
    if (!created.ok) return created;
    const EditResult removed = DeleteElements(document, script, {previous.ref});
    if (!removed.ok) return removed; // the new operation goes with the edit
    int kept = 0;
    for (auto relink : relinks) {
        if (relink.first.id == 0) relink.first.id = created.created;
        if (relink.second.id == 0) relink.second.id = created.created;
        if (CreateParameterLink(document, script, relink.first, relink.second).ok) ++kept;
    }
    if (dropped) *dropped = static_cast<int>(relinks.size()) - kept;
    return edit.Commit(std::string(), created.created);
}

EditResult ReplaceLinkConversion(Document &document, CK_ID script, const ElementRef &link, const LinkEndpoint &a,
                                 const LinkEndpoint &b, const OperationSignature &signature) {
    Edit edit(document, "Edit Parameter Operation Link");
    const EditResult removed = DeleteElements(document, script, {link});
    if (!removed.ok) return removed;
    const EditResult created = CreateParameterLink(document, script, a, b, &signature);
    if (!created.ok) return created;
    return edit.Commit();
}

EditResult InsertOperationInLink(Document &document, CK_ID script, const ElementRef &link, const LinkEndpoint &a,
                                 const LinkEndpoint &b, const OperationSignature &signature) {
    // At the middle of the link as the Schematic draws it.
    Point middle;
    SchematicModel model;
    if (ModelOf(document, script, model))
        for (const auto &item : model.links)
            if (item.ref == link && item.path.size() >= 2) {
                const Point &from = item.path.front(), &to = item.path.back();
                middle = Point((from.h + to.h) * 0.5f - 30.0f, (from.v + to.v) * 0.5f - 30.0f);
            }
    Edit edit(document, "Add Parameter Operation in Link");
    const EditResult removed = DeleteElements(document, script, {link});
    if (!removed.ok) return removed;
    const EditResult operation = AddParameterOperation(document, script, link.owner, signature.operation,
                                                       signature.result, signature.input1, signature.input2, middle);
    if (!operation.ok) return operation;
    // A side that does not link leaves the operation unlinked there.
    CreateParameterLink(document, script, a, LinkEndpoint(operation.created, 0, ENDPOINT_PIN));
    CreateParameterLink(document, script, LinkEndpoint(operation.created, 0, ENDPOINT_POUT), b);
    return edit.Commit(std::string(), operation.created);
}

} // namespace vdev
