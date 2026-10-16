#pragma once

#include <vector>
#include "core/schematic/Geometry.h"

// Orthogonal paths for the optional readability policy, not Dev reconstruction.
class OrthogonalRouter {
public:
    struct Config {
        float obstacleMargin = 10.0f;
        float bendPenalty = 50.0f;
        int maxIterations = 10000;
        // Align clearance bounds outwards before searching; never round endpoints.
        // Zero disables grid alignment.
        float gridSnap = 20.0f;
        float crossingPenalty = 300.0f;
        float overlapPenalty = 1.0f; // Cost per pixel shared with a previous path.
        int maxNodes = 20000;
        // Permit only terminal segments to leave/enter a clearance region.
        // Raw rectangle interiors remain forbidden. Strict by default.
        bool allowEndpointInClearance = false;
        // Optional lower coordinate bounds. Script routing must not generate
        // control points above/left of its origin; this is not an upper extent.
        bool constrainToMinimumPosition = false;
        Point minimumPosition;
    };
    OrthogonalRouter();
    explicit OrthogonalRouter(const Config& config);
    // Empty on invalid input, blocked endpoints or exhausted search limits.
    std::vector<Point> Route(const Point& start, const Point& end,
                             const std::vector<Rect>& obstacles);
    std::vector<Point> Route(const Point& start, const Point& end,
                             const std::vector<Rect>& obstacles,
                             const std::vector<std::vector<Point>>& previousPaths);
    void SetConfig(const Config& config) { m_Config = config; }
    enum class Failure { None, InvalidInput, BlockedEndpoint, GraphLimit, SearchLimit, NoPath };
    struct Stats {
        int nodesExplored = 0;
        int pathLength = 0; // Point count, retained for API compatibility.
        int bendCount = 0;
        double executionTimeMs = 0.0;
        bool success = false;
        int crossingCount = 0; // Against previous paths, not a global optimum.
        Failure failure = Failure::None;
    };
    const Stats& GetLastStats() const { return m_LastStats; }

private:
    bool BuildGraph(const Point& start, const Point& end,
                    const std::vector<Rect>& clearance,
                    const std::vector<Rect>& obstacles,
                    const std::vector<std::vector<Point>>& previousPaths);
    Config m_Config;
    Stats m_LastStats;
    std::vector<Point> m_Nodes;
    std::vector<std::vector<int>> m_Neighbors;
};
