#pragma once

#include <string>
#include <vector>

#include "core/interface/InterfaceData.h"

class CKBehavior;
class CKContext;

enum class InterfaceDataValidationStage {
    AfterBuildGraph,
    BeforeSaveToChunk
};

enum class LinkEndpointSide {
    None,
    Start,
    End
};

struct InterfaceDataValidationIssue {
    CK_ID behaviorId = 0;
    int linkIndex = -1;
    LinkEndpointSide endpointSide = LinkEndpointSide::None;
    std::string field;
    std::string reason;
};

std::vector<InterfaceDataValidationIssue> ValidateInterfaceData(
    const InterfaceData &data,
    CKBehavior *script,
    InterfaceDataValidationStage stage);

const char *LinkEndpointSideName(LinkEndpointSide side);

void ReportInterfaceDataValidationIssues(
    CKContext *context,
    const std::vector<InterfaceDataValidationIssue> &issues);
