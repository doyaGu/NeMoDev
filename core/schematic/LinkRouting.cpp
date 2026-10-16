#include "core/schematic/LinkRouting.h"

#include "CKAll.h"
#include "core/schematic/BehaviorDisplayGeometry.h"
#include "core/schematic/LinkEndpointGeometry.h"
#include "core/schematic/detail/OrthogonalRouter.h"
#include "core/schematic/ParameterDisplayGeometry.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr float GRID_SIZE = 20.0f;

bool SamePoint(const Point &left, const Point &right) {
    return left == right;
}

bool SamePoints(const std::vector<Point> &left, const std::vector<Point> &right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t i = 0; i < left.size(); ++i) {
        if (left[i].h != right[i].h || left[i].v != right[i].v) {
            return false;
        }
    }
    return true;
}

bool AreControlPointsFinite(const Link &link) {
    for (const auto &point : link.points) {
        if (!std::isfinite(point.h) || !std::isfinite(point.v)) {
            return false;
        }
    }
    return true;
}

float NormalizeControlPointCoordinate(float value) {
    if (!std::isfinite(value) || value == 0.0f) {
        return 0.0f;
    }
    return value;
}


bool CollectObstacles(const InterfaceData &data,
                      const BehaviorData &behavior,
                      const Link &link,
                      CKContext *context,
                      bool scriptUi,
                      std::vector<Rect> &obstacles) {
    for (const auto &candidate : data.behaviors) {
        if (candidate.parentId != behavior.id ||
            candidate.id == link.start.id ||
            candidate.id == link.end.id) {
            continue;
        }
        Rect content;
        if (!BehaviorDisplayGeometry::TryGetContentBounds(candidate, content)) return false;
        content.hPos += candidate.rect.hPos;
        content.vPos += candidate.rect.vPos;
        obstacles.push_back(content);
    }
    const auto isParameterEndpoint = [&](const LinkEndpoint& endpoint, EndpointType type, size_t index) {
        return endpoint.id == behavior.id && endpoint.type == type &&
               endpoint.index >= 0 && static_cast<size_t>(endpoint.index) == index;
    };
    const auto appendParameters = [&](const std::vector<Parameter>& parameters, EndpointType type) {
        for (size_t index = 0; index < parameters.size(); ++index) {
            // The display identity is owner + table + index, not CK source ID.
            if (isParameterEndpoint(link.start, type, index) ||
                isParameterEndpoint(link.end, type, index)) continue;
            Rect display = ParameterDisplayGeometry::EstimateBounds(parameters[index], context, scriptUi);
            display.hPos += behavior.rect.hPos;
            display.vPos += behavior.rect.vPos;
            obstacles.push_back(display);
        }
    };
    appendParameters(behavior.localParams, ENDPOINT_PLOCAL);
    appendParameters(behavior.sharedParams, ENDPOINT_POUT_SHORTCUT);
    for (const auto& operation : behavior.operations) {
        if (operation.id == link.start.id || operation.id == link.end.id) continue;
        Rect display;
        if (!ParameterDisplayGeometry::TryGetOperationBounds(operation, display)) return false;
        obstacles.push_back(display);
    }
    for (const auto &comment : behavior.comments) {
        obstacles.push_back(comment.GetRect());
    }
    return true;
}

std::vector<std::vector<Point>> CapturePointLists(const InterfaceData &data) {
    std::vector<std::vector<Point>> pointLists;
    pointLists.reserve(data.rootBehavior.links.size());
    for (const auto &link : data.rootBehavior.links) {
        pointLists.push_back(link.points);
    }
    for (const auto &behavior : data.behaviors) {
        for (const auto &link : behavior.links) {
            pointLists.push_back(link.points);
        }
    }
    return pointLists;
}

void RecordDiagnostics(const InterfaceData &data,
                       const std::vector<std::vector<Point>> &before,
                       int unresolvedEndpointCount,
                       int routingFailureCount,
                       int blockedEndpointLinkCount,
                       LinkRoutingDiagnostics *diagnostics) {
    if (!diagnostics) {
        return;
    }
    diagnostics->linkCount = 0;
    diagnostics->changedPointListCount = 0;
    diagnostics->unresolvedEndpointCount = unresolvedEndpointCount;
    diagnostics->routingFailureCount = routingFailureCount;
    diagnostics->blockedEndpointLinkCount = blockedEndpointLinkCount;
    size_t index = 0;
    auto record = [&](const BehaviorData &behavior) {
        for (const auto &link : behavior.links) {
            ++diagnostics->linkCount;
            if (index >= before.size() || !SamePoints(before[index], link.points)) {
                ++diagnostics->changedPointListCount;
            }
            ++index;
        }
    };
    record(data.rootBehavior);
    for (const auto &behavior : data.behaviors) {
        record(behavior);
    }
}

} // namespace

std::vector<Point> ToInterfaceChunkControlPoints(
    const std::vector<Point> &fullPath,
    const Point &start,
    const Point &end) {
    auto isOrthogonalSegment = [](const Point &left, const Point &right) {
        return left.h == right.h || left.v == right.v;
    };
    auto addIfDistinct = [](std::vector<Point> &points, const Point &point) {
        if (points.empty() || !SamePoint(points.back(), point)) {
            points.push_back(point);
        }
    };
    auto simplify = [&addIfDistinct](const std::vector<Point> &points) {
        if (points.size() <= 2) {
            return points;
        }
        std::vector<Point> simplified;
        simplified.reserve(points.size());
        simplified.push_back(points.front());
        for (size_t i = 1; i + 1 < points.size(); ++i) {
            const Point &previous = simplified.back();
            const Point &current = points[i];
            const Point &next = points[i + 1];
            const bool collinearH =
                previous.v == current.v && current.v == next.v;
            const bool collinearV =
                previous.h == current.h && current.h == next.h;
            if (!collinearH && !collinearV && !SamePoint(previous, current)) {
                simplified.push_back(current);
            }
        }
        addIfDistinct(simplified, points.back());
        return simplified;
    };

    std::vector<Point> controlPoints;
    controlPoints.reserve(fullPath.size());
    for (size_t i = 0; i < fullPath.size(); ++i) {
        if (i == 0 || i + 1 == fullPath.size()) {
            continue;
        }
        const Point &point = fullPath[i];
        if (SamePoint(point, start) || SamePoint(point, end)) {
            continue;
        }
        addIfDistinct(controlPoints, point);
    }

    controlPoints = simplify(controlPoints);
    if (controlPoints.empty()) {
        if (!isOrthogonalSegment(start, end)) {
            addIfDistinct(controlPoints, Point(start.h, end.v));
        }
    } else {
        if (!isOrthogonalSegment(start, controlPoints.front())) {
            const Point dogleg(controlPoints.front().h, start.v);
            if (!SamePoint(dogleg, start) &&
                !SamePoint(dogleg, controlPoints.front())) {
                controlPoints.insert(controlPoints.begin(), dogleg);
            }
        }
        if (!isOrthogonalSegment(controlPoints.back(), end)) {
            const Point dogleg(end.h, controlPoints.back().v);
            if (!SamePoint(dogleg, end) &&
                !SamePoint(dogleg, controlPoints.back())) {
                controlPoints.push_back(dogleg);
            }
        }
    }

    controlPoints.erase(
        std::remove_if(
            controlPoints.begin(),
            controlPoints.end(),
            [&start, &end](const Point &point) {
                return SamePoint(point, start) || SamePoint(point, end);
            }),
        controlPoints.end());
    return simplify(controlPoints);
}

bool ApplyLinkRoutingPolicy(
    InterfaceData &data,
    CKBehavior *script,
    LinkRoutingPolicy policy,
    LinkRoutingDiagnostics *diagnostics) {
    LinkRoutingOptions options;
    options.policy = policy;
    return ApplyLinkRoutingPolicy(data, script, options, diagnostics);
}

bool ApplyLinkRoutingPolicy(
    InterfaceData &data,
    CKBehavior *script,
    const LinkRoutingOptions &options,
    LinkRoutingDiagnostics *diagnostics) {
    // Null is used by synthetic Script fixtures; real calls supply the root.
    const bool rootIsScript = !script || script->GetType() == CKBEHAVIORTYPE_SCRIPT;
    const auto before = CapturePointLists(data);
    int unresolvedEndpointCount = 0;
    int routingFailureCount = 0;
    int blockedEndpointLinkCount = 0;

    OrthogonalRouter::Config routerConfig;
    routerConfig.obstacleMargin = options.obstacleMargin;
    routerConfig.gridSnap = GRID_SIZE;
    routerConfig.crossingPenalty = options.crossingPenalty;
    routerConfig.allowEndpointInClearance = options.allowEndpointInClearance;
    OrthogonalRouter router(routerConfig);
    std::vector<std::pair<Link *, std::vector<Point>>> pending;

    auto apply = [&](BehaviorData &behavior) {
        routerConfig.constrainToMinimumPosition = rootIsScript && &behavior == &data.rootBehavior;
        routerConfig.minimumPosition = Point(behavior.rect.hPos, behavior.rect.vPos);
        router.SetConfig(routerConfig);
        std::vector<std::vector<Point>> previousPaths;
        for (auto &link : behavior.links) {
            if (options.policy == LinkRoutingPolicy::PreserveOnly) {
                if (options.normalizeControlPointFloats) {
                    for (auto &point : link.points) {
                        point.h = NormalizeControlPointCoordinate(point.h);
                        point.v = NormalizeControlPointCoordinate(point.v);
                    }
                }
                continue;
            }

            if (options.policy == LinkRoutingPolicy::Straight) {
                if (data.generatedFromRuntimeGraph) {
                    link.points.clear();
                }
                continue;
            }

            if (!data.generatedFromRuntimeGraph &&
                options.preserveParsedControlPoints &&
                AreControlPointsFinite(link)) {
                Point start, end;
                if (TryGetLinkEndpointPosition(data, link.start, start, rootIsScript) &&
                    TryGetLinkEndpointPosition(data, link.end, end, rootIsScript)) {
                    std::vector<Point> path{start};
                    path.insert(path.end(), link.points.begin(), link.points.end());
                    path.push_back(end);
                    previousPaths.push_back(std::move(path));
                }
                continue;
            }

            Point start;
            Point end;
            if (!TryGetLinkEndpointPosition(data, link.start, start, rootIsScript) ||
                !TryGetLinkEndpointPosition(data, link.end, end, rootIsScript)) {
                ++unresolvedEndpointCount;
                continue;
            }

            std::vector<Rect> obstacles;
            if (!CollectObstacles(data, behavior, link, script ? script->GetCKContext() : nullptr,
                                  rootIsScript && &behavior == &data.rootBehavior, obstacles)) {
                ++routingFailureCount;
                continue;
            }
            const auto fullPath = router.Route(start, end, obstacles, previousPaths);
            if (!router.GetLastStats().success) {
                ++routingFailureCount;
                if (router.GetLastStats().failure == OrthogonalRouter::Failure::BlockedEndpoint)
                    ++blockedEndpointLinkCount;
                continue;
            }
            pending.push_back({&link, ToInterfaceChunkControlPoints(fullPath, start, end)});
            previousPaths.push_back(fullPath);
        }
    };

    apply(data.rootBehavior);
    for (auto &behavior : data.behaviors) {
        apply(behavior);
    }

    if (unresolvedEndpointCount == 0 && routingFailureCount == 0) {
        for (auto &entry : pending) entry.first->points = std::move(entry.second);
    }

    RecordDiagnostics(
        data,
        before,
        unresolvedEndpointCount,
        routingFailureCount,
        blockedEndpointLinkCount,
        diagnostics);
    return unresolvedEndpointCount == 0 && routingFailureCount == 0;
}

bool RouteSingleLink(InterfaceData &data, BehaviorData &graph, Link &link, CKBehavior *script,
                     const LinkRoutingOptions &options) {
    const bool rootIsScript = !script || script->GetType() == CKBEHAVIORTYPE_SCRIPT;
    Point start;
    Point end;
    if (!TryGetLinkEndpointPosition(data, link.start, start, rootIsScript) ||
        !TryGetLinkEndpointPosition(data, link.end, end, rootIsScript))
        return false;
    const bool scriptUi = rootIsScript && &graph == &data.rootBehavior;
    std::vector<Rect> obstacles;
    if (!CollectObstacles(data, graph, link, script ? script->GetCKContext() : nullptr, scriptUi, obstacles))
        return false;
    OrthogonalRouter::Config config;
    config.obstacleMargin = options.obstacleMargin;
    config.gridSnap = GRID_SIZE;
    config.crossingPenalty = options.crossingPenalty;
    config.allowEndpointInClearance = options.allowEndpointInClearance;
    config.constrainToMinimumPosition = scriptUi;
    config.minimumPosition = Point(graph.rect.hPos, graph.rect.vPos);
    OrthogonalRouter router(config);
    const std::vector<Point> path = router.Route(start, end, obstacles);
    if (!router.GetLastStats().success) return false;
    link.points = ToInterfaceChunkControlPoints(path, start, end);
    return true;
}
