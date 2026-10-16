#pragma once

#include "core/interface/InterfaceData.h"

class CKBehavior;

enum class LinkRoutingPolicy {
    PreserveOnly,
    Straight,
    OrthogonalBeautify
};

struct LinkRoutingDiagnostics {
    int linkCount = 0;
    int changedPointListCount = 0;
    int unresolvedEndpointCount = 0;
    int routingFailureCount = 0;
    // A link is counted once, even if both estimated endpoints are blocked.
    int blockedEndpointLinkCount = 0;
};

struct LinkRoutingOptions {
    LinkRoutingPolicy policy = LinkRoutingPolicy::PreserveOnly;
    bool preserveParsedControlPoints = true;
    bool normalizeControlPointFloats = false;
    float obstacleMargin = 10.0f;
    float crossingPenalty = 300.0f;
    // Parameter centers may lie beside a BB, inside its clearance margin.
    // Allow terminal exit/entry only; never pass through the BB rectangle.
    bool allowEndpointInClearance = true;
};

bool ApplyLinkRoutingPolicy(
    InterfaceData &data,
    CKBehavior *script,
    LinkRoutingPolicy policy,
    LinkRoutingDiagnostics *diagnostics = nullptr);

// OrthogonalBeautify commits all generated point lists together. On failure,
// every original point list is retained; diagnostics describe the failure.
bool ApplyLinkRoutingPolicy(
    InterfaceData &data,
    CKBehavior *script,
    const LinkRoutingOptions &options,
    LinkRoutingDiagnostics *diagnostics = nullptr);

// Best path for one stored link (the Schematic editor's P command): the
// obstacles and router of OrthogonalBeautify, for this link only, in the
// graph that stores it. False (link unchanged) when an endpoint does not
// resolve or no path is found.
bool RouteSingleLink(InterfaceData &data, BehaviorData &graph, Link &link, CKBehavior *script,
                     const LinkRoutingOptions &options = LinkRoutingOptions());

std::vector<Point> ToInterfaceChunkControlPoints(
    const std::vector<Point> &fullPath,
    const Point &start,
    const Point &end);
