#include "core/graph/LinkCommands.h"

#include "core/document/Document.h"
#include "core/graph/BehaviorCommands.h"
#include "core/graph/TopologyCommands.h"

namespace vdev {

namespace {

int Link(Document &document, CK_ID script, const std::vector<std::pair<LinkEndpoint, LinkEndpoint>> &pairs,
         bool behaviorLinks, int delay, bool autoLoopDelay) {
    int made = 0;
    for (const auto &pair : pairs) {
        const EditResult result = behaviorLinks
                                      ? CreateBehaviorLink(document, script, pair.first, pair.second, delay, autoLoopDelay)
                                      : CreateParameterLink(document, script, pair.first, pair.second);
        if (result.ok) ++made;
    }
    return made;
}

} // namespace

int LinkPairs(Document &document, CK_ID script, const std::vector<std::pair<LinkEndpoint, LinkEndpoint>> &pairs,
              bool behaviorLinks, int delay, bool autoLoopDelay) {
    EditGroup group(document, "Link");
    return Link(document, script, pairs, behaviorLinks, delay, autoLoopDelay);
}

int LinkToNewPins(Document &document, CK_ID script, const std::vector<LinkSource> &sources, CK_ID targetId,
                  bool behaviorLinks, int delay, bool autoLoopDelay, bool *kindRefused, std::string *failed) {
    if (kindRefused) *kindRefused = false;
    if (failed) failed->clear();
    CKBehavior *target = CKBehavior::Cast(document.Context()->GetObject(targetId));
    if (!target) return 0;
    const bool buildingBlock = (target->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK) != 0;
    EditGroup group(document, "Link");
    int made = 0;
    std::vector<std::pair<LinkEndpoint, LinkEndpoint>> pairs;
    for (const LinkSource &source : sources) {
        PinKind kind = PinKind::None;
        if (source.kind == PinKind::BehaviorOut || source.endpoint.type == ENDPOINT_START_BIN)
            kind = PinKind::BehaviorIn;
        else if (!behaviorLinks && source.kind != PinKind::ParamIn)
            kind = PinKind::ParamIn;
        if (kind == PinKind::None) continue;
        if (buildingBlock && !AcceptsVariablePin(target, kind)) {
            if (kindRefused) *kindRefused = true;
            break;
        }
        const EditResult created =
            buildingBlock ? AddBuildingBlockPin(document, script, targetId, kind, source.name, source.type)
                          : AddGraphPin(document, script, targetId, kind, source.name, source.type);
        if (!created.ok) {
            if (failed) *failed = created.message;
            break;
        }
        ++made;
        const int index = kind == PinKind::BehaviorIn ? target->GetInputCount() - 1 : target->GetInputParameterCount() - 1;
        pairs.emplace_back(source.endpoint,
                           LinkEndpoint(targetId, index, kind == PinKind::BehaviorIn ? ENDPOINT_BIN : ENDPOINT_PIN));
    }
    return made + Link(document, script, pairs, behaviorLinks, delay, autoLoopDelay);
}

} // namespace vdev
