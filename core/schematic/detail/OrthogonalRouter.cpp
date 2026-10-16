#include "core/schematic/detail/OrthogonalRouter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>

#undef min
#undef max

namespace {
bool Finite(const Point& p) { return std::isfinite(p.h) && std::isfinite(p.v); }
bool Inside(const Point& p, const Rect& r) {
    return p.h > r.hPos && p.h < r.Right() && p.v > r.vPos && p.v < r.Bottom();
}
bool Enters(const Point& a, const Point& b, const Rect& r) {
    if (r.hSize == 0 || r.vSize == 0) return false;
    if (a.v == b.v)
        return a.v > r.vPos && a.v < r.Bottom() &&
            std::max(a.h, b.h) > r.hPos && std::min(a.h, b.h) < r.Right();
    return a.h > r.hPos && a.h < r.Right() &&
        std::max(a.v, b.v) > r.vPos && std::min(a.v, b.v) < r.Bottom();
}
bool Blocked(const Point& a, const Point& b, const std::vector<Rect>& clearance,
             const std::vector<Rect>& obstacles, const Point& start, const Point& end,
             bool allowEndpointInClearance) {
    for (const auto& r : obstacles) if (Enters(a, b, r)) return true;
    for (const auto& r : clearance) {
        if (!Enters(a, b, r)) continue;
        const bool terminalInside =
            ((a == start || a == end) && Inside(a, r)) ||
            ((b == start || b == end) && Inside(b, r));
        if (!allowEndpointInClearance || !terminalInside) return true;
    }
    return false;
}
float Distance(const Point& a, const Point& b) {
    return std::abs(a.h - b.h) + std::abs(a.v - b.v);
}
int Direction(const Point& a, const Point& b) {
    return a.v == b.v ? (b.h > a.h ? 1 : 3) : (b.v > a.v ? 2 : 4);
}

struct PathContact { int crossings = 0; float overlap = 0.0f; };
// Charge a transverse intersection on arrival, including a graph-node arrival.
// Splitting a crossing at a visibility node must not remove its cost.
PathContact Contacts(const Point& a, const Point& b, const Point& start, const Point& end,
                     const std::vector<std::vector<Point>>& paths) {
    PathContact result;
    const Point move = b - a;
    for (const auto& path : paths) {
        for (size_t i = 1; i < path.size(); ++i) {
            const Point c = path[i - 1], d = path[i], segment = d - c;
            const float denominator = move.Cross(segment);
            if (denominator != 0.0f) {
                const float t = (c - a).Cross(segment) / denominator;
                const float u = (c - a).Cross(move) / denominator;
                // Half-open previous segments count internal vertices once.
                if (t > 0.0f && t <= 1.0f && u >= 0.0f && u < 1.0f) {
                    const Point intersection = a + move * t;
                    const bool shared = (intersection == start || intersection == end) &&
                        (intersection == path.front() || intersection == path.back());
                    if (!shared) ++result.crossings;
                }
            } else if ((c - a).Cross(move) == 0.0f) {
                const bool horizontal = a.v == b.v;
                const float a0 = horizontal ? a.h : a.v, a1 = horizontal ? b.h : b.v;
                const float c0 = horizontal ? c.h : c.v, c1 = horizontal ? d.h : d.v;
                result.overlap += std::max(0.0f,
                    std::min(std::max(a0, a1), std::max(c0, c1)) -
                    std::max(std::min(a0, a1), std::min(c0, c1)));
            }
        }
    }
    return result;
}
std::vector<Point> Simplify(const std::vector<Point>& path) {
    std::vector<Point> result;
    for (const auto& point : path) {
        if (!result.empty() && result.back() == point) continue;
        while (result.size() >= 2 &&
               Direction(result[result.size() - 2], result.back()) == Direction(result.back(), point))
            result.pop_back();
        result.push_back(point);
    }
    return result;
}
struct SearchEntry {
    int state;
    float g, f;
    bool operator>(const SearchEntry& other) const {
        if (f != other.f) return f > other.f;
        if (g != other.g) return g > other.g;
        return state > other.state; // Reproducible tie-break.
    }
};
} // namespace

OrthogonalRouter::OrthogonalRouter() : OrthogonalRouter(Config()) {}

OrthogonalRouter::OrthogonalRouter(const Config& config) : m_Config(config) {}

std::vector<Point> OrthogonalRouter::Route(const Point& start, const Point& end,
                                          const std::vector<Rect>& obstacles) {
    return Route(start, end, obstacles, {});
}
std::vector<Point> OrthogonalRouter::Route(const Point& start, const Point& end,
                                          const std::vector<Rect>& obstacles,
                                          const std::vector<std::vector<Point>>& previousPaths) {
    const auto begin = std::chrono::steady_clock::now();
    m_LastStats = Stats();
    auto finish = [&](std::vector<Point> path, Failure failure = Failure::NoPath) {
        m_LastStats.executionTimeMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin).count();
        m_LastStats.pathLength = static_cast<int>(path.size());
        m_LastStats.success = !path.empty();
        m_LastStats.failure = path.empty() ? failure : Failure::None;
        return path;
    };
    if (!Finite(start) || !Finite(end) ||
        !std::isfinite(m_Config.obstacleMargin) || m_Config.obstacleMargin < 0 ||
        !std::isfinite(m_Config.gridSnap) || m_Config.gridSnap < 0 ||
        !std::isfinite(m_Config.bendPenalty) || m_Config.bendPenalty < 0 ||
        !std::isfinite(m_Config.crossingPenalty) || m_Config.crossingPenalty < 0 ||
        !std::isfinite(m_Config.overlapPenalty) || m_Config.overlapPenalty < 0 ||
        m_Config.maxIterations <= 0 || m_Config.maxNodes < 2) return finish({}, Failure::InvalidInput);
    if (m_Config.constrainToMinimumPosition &&
        (!Finite(m_Config.minimumPosition) ||
         start.h < m_Config.minimumPosition.h || end.h < m_Config.minimumPosition.h ||
         start.v < m_Config.minimumPosition.v || end.v < m_Config.minimumPosition.v))
        return finish({}, Failure::InvalidInput);
    std::vector<Rect> clearance;
    for (const auto& r : obstacles) {
        if (!Finite(Point(r.hPos, r.vPos)) || !std::isfinite(r.hSize) || !std::isfinite(r.vSize) ||
            r.hSize < 0 || r.vSize < 0) return finish({}, Failure::InvalidInput);
        if (r.hSize == 0 || r.vSize == 0) continue;
        float left = r.hPos - m_Config.obstacleMargin, right = r.Right() + m_Config.obstacleMargin;
        float top = r.vPos - m_Config.obstacleMargin, bottom = r.Bottom() + m_Config.obstacleMargin;
        if (m_Config.gridSnap > 0) {
            left = std::floor(left / m_Config.gridSnap) * m_Config.gridSnap;
            top = std::floor(top / m_Config.gridSnap) * m_Config.gridSnap;
            right = std::ceil(right / m_Config.gridSnap) * m_Config.gridSnap;
            bottom = std::ceil(bottom / m_Config.gridSnap) * m_Config.gridSnap;
        }
        if (!Finite(Point(left, top)) || !Finite(Point(right, bottom))) return finish({}, Failure::InvalidInput);
        const Rect inflated(left, top, right - left, bottom - top);
        if (Inside(start, r) || Inside(end, r) ||
            (!m_Config.allowEndpointInClearance && (Inside(start, inflated) || Inside(end, inflated))))
            return finish({}, Failure::BlockedEndpoint);
        clearance.push_back(inflated);
    }
    for (const auto& path : previousPaths)
        for (const auto& point : path) if (!Finite(point)) return finish({}, Failure::InvalidInput);
    if (start == end) return finish({start, end});
    if (!BuildGraph(start, end, clearance, obstacles, previousPaths)) return finish({}, Failure::GraphLimit);

    // Direction is part of the state: it determines the next bend cost.
    const size_t states = m_Nodes.size() * 5;
    std::vector<float> scores(states, std::numeric_limits<float>::infinity());
    std::vector<int> parents(states, -1);
    std::vector<bool> closed(states, false);
    std::priority_queue<SearchEntry, std::vector<SearchEntry>, std::greater<SearchEntry>> open;
    scores[0] = 0.0f;
    open.push({0, 0.0f, Distance(start, end)});
    while (!open.empty() && m_LastStats.nodesExplored < m_Config.maxIterations) {
        const auto current = open.top();
        open.pop();
        if (closed[current.state] || current.g != scores[current.state]) continue;
        closed[current.state] = true;
        ++m_LastStats.nodesExplored;
        const int node = current.state / 5, incoming = current.state % 5;
        if (node == 1) {
            std::vector<Point> path;
            for (int state = current.state; state >= 0; state = parents[state])
                path.push_back(m_Nodes[state / 5]);
            std::reverse(path.begin(), path.end());
            path = Simplify(path);
            m_LastStats.bendCount = static_cast<int>(path.size()) - 2;
            for (size_t i = 1; i < path.size(); ++i)
                m_LastStats.crossingCount += Contacts(path[i - 1], path[i], start, end, previousPaths).crossings;
            return finish(path);
        }
        for (int next : m_Neighbors[node]) {
            if (next == 0) continue; // A terminal exception cannot be a mid-path shortcut.
            const int direction = Direction(m_Nodes[node], m_Nodes[next]), nextState = next * 5 + direction;
            if (closed[nextState]) continue;
            const auto contact = Contacts(m_Nodes[node], m_Nodes[next], start, end, previousPaths);
            const float cost = current.g + Distance(m_Nodes[node], m_Nodes[next]) +
                ((incoming && incoming != direction) ? m_Config.bendPenalty : 0.0f) +
                contact.crossings * m_Config.crossingPenalty + contact.overlap * m_Config.overlapPenalty;
            if (cost < scores[nextState]) {
                scores[nextState] = cost;
                parents[nextState] = current.state;
                open.push({nextState, cost, cost + Distance(m_Nodes[next], end)});
            }
        }
    }
    return finish({}, open.empty() ? Failure::NoPath : Failure::SearchLimit);
}

bool OrthogonalRouter::BuildGraph(const Point& start, const Point& end,
                                  const std::vector<Rect>& clearance,
                                  const std::vector<Rect>& obstacles,
                                  const std::vector<std::vector<Point>>& previousPaths) {
    std::vector<float> xs{start.h, end.h}, ys{start.v, end.v};
    if (m_Config.constrainToMinimumPosition) {
        xs.push_back(m_Config.minimumPosition.h);
        ys.push_back(m_Config.minimumPosition.v);
    }
    for (const auto& r : clearance) {
        xs.push_back(r.hPos); xs.push_back(r.Right());
        ys.push_back(r.vPos); ys.push_back(r.Bottom());
    }
    // One outer bound offers detours without multiplying all previous coordinates.
    if (!previousPaths.empty()) {
        float left = start.h, right = start.h, top = start.v, bottom = start.v;
        for (const auto& path : previousPaths) for (const auto& p : path) {
            left = std::min(left, p.h); right = std::max(right, p.h);
            top = std::min(top, p.v); bottom = std::max(bottom, p.v);
        }
        const float step = m_Config.gridSnap > 0 ? m_Config.gridSnap : 20.0f;
        xs.push_back(left - step); xs.push_back(right + step);
        ys.push_back(top - step); ys.push_back(bottom + step);
    }
    auto unique = [](std::vector<float>& values) {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    };
    unique(xs); unique(ys);
    if (xs.size() > static_cast<size_t>(m_Config.maxNodes) / ys.size()) return false;
    m_Nodes = {start, end};
    std::vector<std::vector<int>> rows(ys.size()), columns(xs.size());
    for (size_t y = 0; y < ys.size(); ++y) for (size_t x = 0; x < xs.size(); ++x) {
        const Point point(xs[x], ys[y]);
        if (!Finite(point)) return false;
        if (m_Config.constrainToMinimumPosition &&
            (point.h < m_Config.minimumPosition.h || point.v < m_Config.minimumPosition.v)) continue;
        const bool terminal = point == start || point == end;
        if (!(m_Config.allowEndpointInClearance && terminal) &&
            std::any_of(clearance.begin(), clearance.end(), [&](const Rect& r) { return Inside(point, r); })) continue;
        int index = point == start ? 0 : point == end ? 1 : static_cast<int>(m_Nodes.size());
        if (index >= 2) m_Nodes.push_back(point);
        rows[y].push_back(index); columns[x].push_back(index);
    }
    m_Neighbors.assign(m_Nodes.size(), {});
    auto connect = [&](const std::vector<int>& line) {
        for (size_t i = 1; i < line.size(); ++i) {
            const int a = line[i - 1], b = line[i];
            if (!Blocked(m_Nodes[a], m_Nodes[b], clearance, obstacles, start, end,
                         m_Config.allowEndpointInClearance)) {
                m_Neighbors[a].push_back(b); m_Neighbors[b].push_back(a);
            }
        }
    };
    for (const auto& row : rows) connect(row);
    for (const auto& column : columns) connect(column);
    return true;
}
