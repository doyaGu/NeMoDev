#include "core/interface/InterfaceDataValidation.h"

#include "CKAll.h"

#include <cmath>
#include <queue>
#include <unordered_set>

namespace {

bool IsKnownEndpointType(EndpointType type) {
    switch (type) {
    case ENDPOINT_POUT_SHORTCUT:
    case ENDPOINT_PIN:
    case ENDPOINT_POUT:
    case ENDPOINT_PLOCAL:
    case ENDPOINT_TARGET_PIN:
    case ENDPOINT_BIN:
    case ENDPOINT_BOUT:
    case ENDPOINT_START_BIN:
        return true;
    default:
        return false;
    }
}

bool IsBehaviorEndpointType(EndpointType type) {
    return type == ENDPOINT_BIN || type == ENDPOINT_BOUT ||
           type == ENDPOINT_START_BIN;
}

bool IsParameterEndpointType(EndpointType type) {
    return type == ENDPOINT_POUT_SHORTCUT || type == ENDPOINT_PIN ||
           type == ENDPOINT_POUT || type == ENDPOINT_PLOCAL ||
           type == ENDPOINT_TARGET_PIN;
}

const BehaviorData *FindBehaviorData(const InterfaceData &data, CK_ID id) {
    if (data.rootBehavior.id == id) {
        return &data.rootBehavior;
    }
    for (const auto &behavior : data.behaviors) {
        if (behavior.id == id) {
            return &behavior;
        }
    }
    return nullptr;
}

void AddIssue(std::vector<InterfaceDataValidationIssue> &issues,
              CK_ID behaviorId,
              int linkIndex,
              LinkEndpointSide endpointSide,
              const char *field,
              const std::string &reason) {
    InterfaceDataValidationIssue issue;
    issue.behaviorId = behaviorId;
    issue.linkIndex = linkIndex;
    issue.endpointSide = endpointSide;
    issue.field = field;
    issue.reason = reason;
    issues.push_back(issue);
}

void ValidateEndpoint(const InterfaceData &data,
                      CKContext *context,
                      CK_ID behaviorId,
                      int linkIndex,
                      LinkEndpointSide side,
                      const LinkEndpoint &endpoint,
                      std::vector<InterfaceDataValidationIssue> &issues) {
    if (endpoint.hasLegacyTypeCode) {
        AddIssue(issues, behaviorId, linkIndex, side, "type",
                 "legacy endpoint code has no verified EndpointType mapping");
        return;
    }
    if (!IsKnownEndpointType(endpoint.type)) {
        AddIssue(issues, behaviorId, linkIndex, side, "type",
                 "unknown EndpointType");
        return;
    }

    if (endpoint.id == 0) {
        AddIssue(issues, behaviorId, linkIndex, side, "id",
                 "LinkEndpoint.id is zero");
        return;
    }

    if (endpoint.type == ENDPOINT_TARGET_PIN) {
        if (endpoint.index != -2) {
            AddIssue(issues, behaviorId, linkIndex, side, "index",
                     "ENDPOINT_TARGET_PIN requires index -2");
        }
    } else if (endpoint.index < 0) {
        AddIssue(issues, behaviorId, linkIndex, side, "index",
                 "LinkEndpoint.index is negative");
        return;
    }

    const BehaviorData *behaviorData = FindBehaviorData(data, endpoint.id);

    if (endpoint.type == ENDPOINT_PLOCAL) {
        if (!behaviorData) {
            AddIssue(issues, behaviorId, linkIndex, side, "id",
                     "ENDPOINT_PLOCAL does not resolve to BehaviorData");
            return;
        }
        if (endpoint.index >= static_cast<int>(behaviorData->localParams.size())) {
            AddIssue(issues, behaviorId, linkIndex, side, "index",
                     "ENDPOINT_PLOCAL index is outside BehaviorData.localParams");
        }
        return;
    }

    if (endpoint.type == ENDPOINT_POUT_SHORTCUT) {
        if (!behaviorData) {
            AddIssue(issues, behaviorId, linkIndex, side, "id",
                     "ENDPOINT_POUT_SHORTCUT does not resolve to BehaviorData");
            return;
        }
        if (endpoint.index >= static_cast<int>(behaviorData->sharedParams.size())) {
            AddIssue(issues, behaviorId, linkIndex, side, "index",
                     "ENDPOINT_POUT_SHORTCUT index is outside BehaviorData.sharedParams");
            return;
        }
        const CK_ID sourceId = behaviorData->sharedParams[endpoint.index].sourceId;
        if (sourceId == 0 || sourceId == static_cast<CK_ID>(-1)) {
            AddIssue(issues, behaviorId, linkIndex, side, "sourceId",
                     "parameter output shortcut has no source parameter ID");
        } else if (context && !CKParameter::Cast(context->GetObject(sourceId))) {
            AddIssue(issues, behaviorId, linkIndex, side, "sourceId",
                     "parameter output shortcut sourceId does not resolve to CKParameter");
        }
        return;
    }

    if (!context) {
        return;
    }

    CKObject *object = context->GetObject(endpoint.id);
    if (endpoint.type == ENDPOINT_START_BIN) {
        if (!CKBehaviorIO::Cast(object)) {
            AddIssue(issues, behaviorId, linkIndex, side, "id",
                     "ENDPOINT_START_BIN does not resolve to CKBehaviorIO");
        }
        return;
    }

    if (endpoint.type == ENDPOINT_BIN || endpoint.type == ENDPOINT_BOUT) {
        CKBehavior *behavior = CKBehavior::Cast(object);
        if (!behavior) {
            AddIssue(issues, behaviorId, linkIndex, side, "id",
                     "behavior endpoint does not resolve to CKBehavior");
            return;
        }
        const int count = endpoint.type == ENDPOINT_BIN
            ? behavior->GetInputCount()
            : behavior->GetOutputCount();
        if (endpoint.index >= count) {
            AddIssue(issues, behaviorId, linkIndex, side, "index",
                     endpoint.type == ENDPOINT_BIN
                         ? "ENDPOINT_BIN index is outside CKBehavior inputs"
                         : "ENDPOINT_BOUT index is outside CKBehavior outputs");
        }
        return;
    }

    if (CKBehavior *behavior = CKBehavior::Cast(object)) {
        if (endpoint.type == ENDPOINT_TARGET_PIN) {
            if (!behavior->IsUsingTarget()) {
                AddIssue(issues, behaviorId, linkIndex, side, "type",
                         "ENDPOINT_TARGET_PIN resolves to a CKBehavior without a target parameter");
            }
            return;
        }
        const int count = endpoint.type == ENDPOINT_PIN
            ? behavior->GetInputParameterCount()
            : behavior->GetOutputParameterCount();
        if (endpoint.index >= count) {
            AddIssue(issues, behaviorId, linkIndex, side, "index",
                     endpoint.type == ENDPOINT_PIN
                         ? "ENDPOINT_PIN index is outside CKBehavior input parameters"
                         : "ENDPOINT_POUT index is outside CKBehavior output parameters");
        }
        return;
    }

    if (CKParameterOperation *operation = CKParameterOperation::Cast(object)) {
        (void)operation;
        const bool validIndex = endpoint.type == ENDPOINT_PIN
            ? endpoint.index == 0 || endpoint.index == 1
            : endpoint.type == ENDPOINT_POUT && endpoint.index == 0;
        if (!validIndex) {
            AddIssue(issues, behaviorId, linkIndex, side, "index",
                     "parameter endpoint index is invalid for CKParameterOperation");
        }
        return;
    }

    AddIssue(issues, behaviorId, linkIndex, side, "id",
             "parameter endpoint does not resolve to CKBehavior or CKParameterOperation");
}

// A link stored in one BehaviorData can only address elements displayed by
// that graph: the graph itself, its direct sub-behaviors, its own operations
// and its own local/shortcut tables. CKBehavior::GetParent and
// CKParameterOperation::GetOwner are the SDK owners.
bool IsDisplayedByGraph(CKContext *context, CK_ID graphId, const LinkEndpoint &endpoint) {
    if (endpoint.type == ENDPOINT_PLOCAL || endpoint.type == ENDPOINT_POUT_SHORTCUT) {
        return endpoint.id == graphId;
    }
    CKObject *object = context->GetObject(endpoint.id);
    if (endpoint.type == ENDPOINT_START_BIN) {
        CKBehaviorIO *io = CKBehaviorIO::Cast(object);
        return io && io->GetOwner() && io->GetOwner()->GetID() == graphId;
    }
    if (CKBehavior *behavior = CKBehavior::Cast(object)) {
        CKBehavior *parent = behavior->GetParent();
        return endpoint.id == graphId || (parent && parent->GetID() == graphId);
    }
    if (CKParameterOperation *operation = CKParameterOperation::Cast(object)) {
        CKBehavior *owner = operation->GetOwner();
        return owner && owner->GetID() == graphId;
    }
    return false;
}

void ValidateBehavior(const InterfaceData &data,
                      CKContext *context,
                      const BehaviorData &behavior,
                      InterfaceDataValidationStage stage,
                      std::vector<InterfaceDataValidationIssue> &issues) {
    for (int i = 0; i < static_cast<int>(behavior.links.size()); ++i) {
        const Link &link = behavior.links[i];
        if (link.type != LINK_TYPE_BEHAVIOR && link.type != LINK_TYPE_PARAMETER) {
            AddIssue(issues, behavior.id, i, LinkEndpointSide::None, "type",
                     "unknown LinkType");
        } else if (link.type == LINK_TYPE_BEHAVIOR) {
            if (!link.start.hasLegacyTypeCode && !IsBehaviorEndpointType(link.start.type)) {
                AddIssue(issues, behavior.id, i, LinkEndpointSide::Start, "type",
                         "behavior link start does not use a behavior EndpointType");
            }
            if (!link.end.hasLegacyTypeCode && !IsBehaviorEndpointType(link.end.type)) {
                AddIssue(issues, behavior.id, i, LinkEndpointSide::End, "type",
                         "behavior link end does not use a behavior EndpointType");
            }
        } else {
            if (!link.start.hasLegacyTypeCode && !IsParameterEndpointType(link.start.type)) {
                AddIssue(issues, behavior.id, i, LinkEndpointSide::Start, "type",
                         "parameter link start does not use a parameter EndpointType");
            }
            if (!link.end.hasLegacyTypeCode && !IsParameterEndpointType(link.end.type)) {
                AddIssue(issues, behavior.id, i, LinkEndpointSide::End, "type",
                         "parameter link end does not use a parameter EndpointType");
            }
        }

        if (!link.end.hasLegacyTypeCode && link.end.type == ENDPOINT_START_BIN) {
            AddIssue(issues, behavior.id, i, LinkEndpointSide::End, "type",
                     "ENDPOINT_START_BIN is not valid as a destination endpoint");
        }

        for (const LinkEndpointSide side : {LinkEndpointSide::Start, LinkEndpointSide::End}) {
            const LinkEndpoint &endpoint = side == LinkEndpointSide::Start ? link.start : link.end;
            const size_t issueCount = issues.size();
            ValidateEndpoint(data, context, behavior.id, i, side, endpoint, issues);
            // Runtime-generated data is checked against SDK ownership. A
            // resolvable endpoint stored in the wrong graph is still invalid.
            if (data.generatedFromRuntimeGraph && context && issues.size() == issueCount &&
                !IsDisplayedByGraph(context, behavior.id, endpoint)) {
                AddIssue(issues, behavior.id, i, side, "id",
                         "LinkEndpoint is not displayed by the BehaviorData that stores the link");
            }
        }

        if (stage == InterfaceDataValidationStage::BeforeSaveToChunk) {
            for (int pointIndex = 0;
                 pointIndex < static_cast<int>(link.points.size());
                 ++pointIndex) {
                const Point &point = link.points[pointIndex];
                if (!std::isfinite(point.h) || !std::isfinite(point.v)) {
                    AddIssue(issues, behavior.id, i, LinkEndpointSide::None,
                             "points", "Link::points contains a non-finite coordinate");
                }
            }
        }
    }

    if (stage != InterfaceDataValidationStage::BeforeSaveToChunk) {
        return;
    }

    const float behaviorValues[] = {
        behavior.rect.hPos, behavior.rect.vPos,
        behavior.rect.hSize, behavior.rect.vSize,
        behavior.hExpandSize, behavior.vExpandSize
    };
    for (float value : behaviorValues) {
        if (!std::isfinite(value)) {
            AddIssue(issues, behavior.id, -1, LinkEndpointSide::None,
                     "rect", "BehaviorData contains a non-finite coordinate");
            break;
        }
    }

    for (const auto &operation : behavior.operations) {
        if (!std::isfinite(operation.hPos) || !std::isfinite(operation.vPos)) {
            AddIssue(issues, behavior.id, -1, LinkEndpointSide::None,
                     "operations", "Operation contains a non-finite coordinate");
        }
    }

    for (const auto &comment : behavior.comments) {
        if (!std::isfinite(comment.hPos) || !std::isfinite(comment.vPos) ||
            !std::isfinite(comment.width) || !std::isfinite(comment.height)) {
            AddIssue(issues, behavior.id, -1, LinkEndpointSide::None,
                     "comments", "Comment contains a non-finite coordinate");
        }
    }
}

void ValidateRuntimeObjects(const InterfaceData &data,
                            CKBehavior *script,
                            std::vector<InterfaceDataValidationIssue> &issues) {
    // Endpoint validity alone cannot detect a missing behavior, operation or
    // behavior link. Compare directly with SDK ownership, without rebuilding
    // another set of InterfaceChunk records.
    if (!data.generatedFromRuntimeGraph || !script) return;
    if (data.rootBehavior.id != script->GetID()) {
        AddIssue(issues, data.rootBehavior.id, -1, LinkEndpointSide::None,
                 "id", "root BehaviorData.id does not match the Script CKBehavior");
    }
    std::queue<CKBehavior *> pending;
    std::unordered_set<CK_ID> runtimeIds;
    pending.push(script);
    while (!pending.empty()) {
        CKBehavior *runtime = pending.front();
        pending.pop();
        if (!runtimeIds.insert(runtime->GetID()).second) continue;
        for (int i = 0; i < runtime->GetSubBehaviorCount(); ++i) {
            if (auto *child = runtime->GetSubBehavior(i)) pending.push(child);
        }
        const BehaviorData *record = FindBehaviorData(data, runtime->GetID());
        if (!record) {
            AddIssue(issues, runtime->GetID(), -1, LinkEndpointSide::None,
                     "id", "CKBehavior has no reconstructed BehaviorData");
            continue;
        }
        const CK_ID parentId = runtime->GetParent() ? runtime->GetParent()->GetID() : 0;
        if (record->parentId != parentId) {
            AddIssue(issues, record->id, -1, LinkEndpointSide::None,
                     "parentId", "BehaviorData.parentId does not match CKBehavior::GetParent");
        }
        const bool isGraph = runtime->GetType() != CKBEHAVIORTYPE_BASE;
        if (record->isBehaviorGraph != isGraph) {
            AddIssue(issues, record->id, -1, LinkEndpointSide::None,
                     "isBehaviorGraph", "BehaviorData graph kind does not match CKBehavior::GetType");
        }
        if (!isGraph) continue;

        std::unordered_set<CK_ID> linkIds;
        for (int i = 0; i < runtime->GetSubBehaviorLinkCount(); ++i) {
            if (auto *link = runtime->GetSubBehaviorLink(i)) linkIds.insert(link->GetID());
        }
        for (int i = 0; i < static_cast<int>(record->links.size()); ++i) {
            if (record->links[i].type == LINK_TYPE_BEHAVIOR && !linkIds.erase(record->links[i].id)) {
                AddIssue(issues, record->id, i, LinkEndpointSide::None,
                         "id", "behavior link is duplicated or not owned by this CKBehavior");
            }
        }
        for (CK_ID id : linkIds) {
            AddIssue(issues, record->id, -1, LinkEndpointSide::None,
                     "links", "CKBehaviorLink has no reconstructed behavior link: " + std::to_string(id));
        }

        std::unordered_set<CK_ID> operationIds;
        for (int i = 0; i < runtime->GetParameterOperationCount(); ++i) {
            if (auto *operation = runtime->GetParameterOperation(i)) operationIds.insert(operation->GetID());
        }
        for (const auto &operation : record->operations) {
            if (!operationIds.erase(operation.id)) {
                AddIssue(issues, record->id, -1, LinkEndpointSide::None,
                         "operations", "Operation is duplicated or not owned by this CKBehavior");
            }
        }
        if (!operationIds.empty()) {
            AddIssue(issues, record->id, -1, LinkEndpointSide::None,
                     "operations", "CKParameterOperation has no reconstructed Operation");
        }

        if (record->localParams.size() != static_cast<size_t>(runtime->GetLocalParameterCount())) {
            AddIssue(issues, record->id, -1, LinkEndpointSide::None,
                     "localParams", "local parameter display count does not match CKBehavior local parameters");
        } else {
            for (int i = 0; i < runtime->GetLocalParameterCount(); ++i) {
                auto *local = runtime->GetLocalParameter(i);
                if (!local || record->localParams[i].id != local->GetID()) {
                    AddIssue(issues, record->id, -1, LinkEndpointSide::None,
                             "localParams", "local parameter display slot does not match CKBehavior local parameter");
                    break;
                }
            }
        }
    }
    for (const auto &record : data.behaviors) {
        if (runtimeIds.find(record.id) == runtimeIds.end()) {
            AddIssue(issues, record.id, -1, LinkEndpointSide::None,
                     "id", "BehaviorData is not in this Script's sub-behavior tree");
        }
    }
}

} // namespace

std::vector<InterfaceDataValidationIssue> ValidateInterfaceData(
    const InterfaceData &data,
    CKBehavior *script,
    InterfaceDataValidationStage stage) {
    std::vector<InterfaceDataValidationIssue> issues;
    CKContext *context = script ? script->GetCKContext() : nullptr;

    std::unordered_set<CK_ID> behaviorIds;

    if (data.behaviorCount != static_cast<int>(data.behaviors.size())) {
        AddIssue(issues, data.rootBehavior.id, -1, LinkEndpointSide::None,
                 "behaviorCount", "behaviorCount does not match behaviors.size()");
    }
    if (!behaviorIds.insert(data.rootBehavior.id).second) {
        AddIssue(issues, data.rootBehavior.id, -1, LinkEndpointSide::None,
                 "id", "duplicate BehaviorData.id");
    }
    for (const auto &behavior : data.behaviors) {
        if (!behaviorIds.insert(behavior.id).second) {
            AddIssue(issues, behavior.id, -1, LinkEndpointSide::None,
                     "id", "duplicate BehaviorData.id");
        }
    }

    ValidateBehavior(data, context, data.rootBehavior, stage, issues);
    for (const auto &behavior : data.behaviors) {
        ValidateBehavior(data, context, behavior, stage, issues);
    }
    ValidateRuntimeObjects(data, script, issues);

    if (stage == InterfaceDataValidationStage::BeforeSaveToChunk) {
        const float headerValues[] = {
            data.header.vStart, data.header.hStartPos, data.header.vStartPos,
            data.header.hStartSize, data.header.vStartSize, data.header.vSize
        };
        for (float value : headerValues) {
            if (!std::isfinite(value)) {
                AddIssue(issues, data.rootBehavior.id, -1, LinkEndpointSide::None,
                         "header", "Header contains a non-finite coordinate");
                break;
            }
        }
    }

    return issues;
}

const char *LinkEndpointSideName(LinkEndpointSide side) {
    switch (side) {
    case LinkEndpointSide::Start:
        return "start";
    case LinkEndpointSide::End:
        return "end";
    default:
        return "none";
    }
}

void ReportInterfaceDataValidationIssues(
    CKContext *context,
    const std::vector<InterfaceDataValidationIssue> &issues) {
    if (!context) {
        return;
    }
    for (const auto &issue : issues) {
        context->OutputToConsoleEx(
            (CKSTRING)"InterfaceData validation: behaviorId=%d linkIndex=%d endpoint=%s field=%s: %s",
            issue.behaviorId,
            issue.linkIndex,
            LinkEndpointSideName(issue.endpointSide),
            issue.field.c_str(),
            issue.reason.c_str());
    }
}
