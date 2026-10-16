#include "core/schematic/SchematicHitTest.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

namespace vdev {

namespace {

constexpr float PIN_RADIUS = 6.0f;
constexpr float CONTROL_POINT_RADIUS = 5.0f;

float DistanceToSegment(const Point &a, const Point &b, const Point &p) {
    const float dx = b.h - a.h, dy = b.v - a.v;
    const float length = dx * dx + dy * dy;
    float t = length > 0.0f ? ((p.h - a.h) * dx + (p.v - a.v) * dy) / length : 0.0f;
    t = std::max(0.0f, std::min(1.0f, t));
    const float x = a.h + t * dx - p.h, y = a.v + t * dy - p.v;
    return std::sqrt(x * x + y * y);
}

bool Inside(const Rect &outer, const Rect &inner) {
    return inner.hPos >= outer.hPos && inner.vPos >= outer.vPos && inner.Right() <= outer.Right() &&
           inner.Bottom() <= outer.Bottom();
}

Rect Expanded(const Rect &rect, float margin) {
    return Rect(rect.hPos - margin, rect.vPos - margin, rect.hSize + 2.0f * margin, rect.vSize + 2.0f * margin);
}

} // namespace

float DistanceToPath(const std::vector<Point> &path, const Point &point) {
    float best = 1e30f;
    for (size_t i = 1; i < path.size(); ++i) best = std::min(best, DistanceToSegment(path[i - 1], path[i], point));
    return best;
}

namespace {

// Drawing order of expanded graphs (SchematicRenderer): a graph's contents,
// then each expanded child graph, its frame hiding what lies under it. An
// element is visible at a point unless a graph drawn after it covers the point.
class Layers {
public:
    explicit Layers(const SchematicModel &model) : m_Model(model) {
        for (const auto &behavior : model.behaviors) {
            m_Parent[behavior.id] = behavior.parent;
            if (model.IsOpenGraph(behavior.id) && behavior.id != model.script.id)
                m_Children[behavior.parent].push_back(&behavior);
        }
        for (const auto &operation : model.operations) m_OperationGraph[operation.id] = operation.owner;
        int time = 0;
        Visit(model.script.id, time);
    }
    // Time a graph's contents are drawn at; its own pins come after its subtree.
    float ContentsTime(CK_ID graph) const {
        auto found = m_Pre.find(graph);
        return found != m_Pre.end() ? found->second + 0.5f : 0.5f;
    }
    float PinTime(const PinItem &pin) const {
        if (m_Model.IsOpenGraph(pin.owner)) {
            auto found = m_Post.find(pin.owner);
            return found != m_Post.end() ? found->second + 0.5f : 0.5f;
        }
        auto behavior = m_Parent.find(pin.owner);
        if (behavior != m_Parent.end()) return ContentsTime(behavior->second);
        auto operation = m_OperationGraph.find(pin.owner);
        return ContentsTime(operation != m_OperationGraph.end() ? operation->second : m_Model.script.id);
    }
    // True when a graph frame drawn after time covers the point.
    bool Covered(const Point &point, float time) const {
        for (const auto &frame : m_Frames)
            if (frame.first > time && frame.second->box.Contains(point)) return true;
        return false;
    }
    // The last drawn expanded graph whose frame contains the point.
    const BehaviorItem *TopFrame(const Point &point) const {
        const BehaviorItem *top = nullptr;
        int best = -1;
        for (const auto &frame : m_Frames)
            if (frame.second->box.Contains(point) && frame.first > best) {
                best = frame.first;
                top = frame.second;
            }
        return top;
    }

private:
    void Visit(CK_ID graph, int &time) {
        m_Pre[graph] = time++;
        auto children = m_Children.find(graph);
        if (children != m_Children.end())
            for (const BehaviorItem *child : children->second) {
                m_Frames.emplace_back(time, child);
                Visit(child->id, time);
            }
        m_Post[graph] = time++;
    }
    const SchematicModel &m_Model;
    std::map<CK_ID, CK_ID> m_Parent, m_OperationGraph;
    std::map<CK_ID, std::vector<const BehaviorItem *>> m_Children;
    std::map<CK_ID, int> m_Pre, m_Post;
    std::vector<std::pair<int, const BehaviorItem *>> m_Frames; // frame drawing time, graph
};

} // namespace

const BehaviorItem *TopGraphFrameAt(const SchematicModel &model, const Point &point) {
    return Layers(model).TopFrame(point);
}

ElementRef PickElement(const SchematicModel &model, const Point &point, float tolerance) {
    const Layers layers(model);
    // Pins: nearest within radius.
    const PinItem *nearestPin = nullptr;
    float pinDistance = PIN_RADIUS + tolerance;
    for (const auto &pin : model.pins) {
        const float distance = pin.center.DistanceTo(point);
        if (distance <= pinDistance && !layers.Covered(pin.center, layers.PinTime(pin))) {
            pinDistance = distance;
            nearestPin = &pin;
        }
    }
    if (nearestPin) return nearestPin->ref;

    for (const auto &link : model.links) {
        if (link.hidden) continue;
        // Interior path points are the stored control points.
        for (size_t i = 1; i + 1 < link.path.size(); ++i) {
            if (link.path[i].DistanceTo(point) <= CONTROL_POINT_RADIUS + tolerance &&
                !layers.Covered(link.path[i], layers.ContentsTime(link.owner))) {
                ElementRef ref = link.ref;
                ref.kind = ElementKind::ControlPoint;
                ref.sub = static_cast<int>(i) - 1;
                return ref;
            }
        }
    }

    for (const auto &operation : model.operations)
        if (Expanded(operation.bounds, tolerance).Contains(point) && !layers.Covered(point, layers.ContentsTime(operation.owner)))
            return operation.ref;
    for (const auto &param : model.params)
        if (!param.hidden && Expanded(param.display, tolerance).Contains(point) &&
            !layers.Covered(point, layers.ContentsTime(param.owner)))
            return param.ref;

    // Folded behaviors and Building Blocks (the deepest one).
    const BehaviorItem *best = nullptr;
    for (const auto &behavior : model.behaviors) {
        if (model.IsOpenGraph(behavior.id) || !Expanded(behavior.box, tolerance).Contains(point)) continue;
        if (layers.Covered(point, layers.ContentsTime(behavior.parent))) continue;
        if (!best || behavior.depth > best->depth) best = &behavior;
    }
    if (best) return best->ref;

    for (auto it = model.comments.rbegin(); it != model.comments.rend(); ++it)
        if (Expanded(it->bounds, tolerance).Contains(point) && !layers.Covered(point, layers.ContentsTime(it->owner)))
            return it->ref;

    const LinkItem *nearestLink = nullptr;
    float linkDistance = 4.0f + tolerance;
    for (const auto &link : model.links) {
        if (link.hidden) continue;
        const float distance = DistanceToPath(link.path, point);
        if (distance <= linkDistance && !layers.Covered(point, layers.ContentsTime(link.owner))) {
            linkDistance = distance;
            nearestLink = &link;
        }
    }
    if (nearestLink) return nearestLink->ref;

    // An expanded graph has no title bar (Dev): its frame and empty interior
    // select it.
    if (const BehaviorItem *frame = layers.TopFrame(point)) return frame->ref;

    if (model.start.present && Expanded(model.start.bounds, tolerance).Contains(point)) return model.start.ref;
    if (model.script.header.Contains(point)) return model.script.ref;
    return ElementRef();
}

std::vector<ElementRef> ElementsInRect(const SchematicModel &model, const Rect &rect, bool strict) {
    std::vector<ElementRef> result;
    auto take = [&](const Rect &bounds) {
        if (strict) return Inside(rect, bounds);
        return bounds.hPos <= rect.Right() && bounds.Right() >= rect.hPos && bounds.vPos <= rect.Bottom() &&
               bounds.Bottom() >= rect.vPos;
    };
    for (const auto &behavior : model.behaviors)
        if (take(behavior.box)) result.push_back(behavior.ref);
    for (const auto &operation : model.operations)
        if (take(operation.bounds)) result.push_back(operation.ref);
    for (const auto &param : model.params)
        if (!param.hidden && take(param.display)) result.push_back(param.ref);
    for (const auto &comment : model.comments)
        if (take(comment.bounds)) result.push_back(comment.ref);
    for (const auto &link : model.links) {
        if (link.path.empty() || link.hidden) continue;
        bool inside = true, touching = false;
        for (const auto &point : link.path) {
            inside = inside && rect.Contains(point);
            touching = touching || rect.Contains(point);
        }
        if (strict ? inside : touching) result.push_back(link.ref);
    }
    if (model.start.present && take(model.start.bounds)) result.push_back(model.start.ref);
    // Dev's multiselection tool also selects pins (multi link creation).
    for (const auto &pin : model.pins)
        if (pin.kind != PinKind::Target && rect.Contains(pin.center)) result.push_back(pin.ref);
    return result;
}

} // namespace vdev
