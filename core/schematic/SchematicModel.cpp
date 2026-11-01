#include "core/schematic/SchematicModel.h"
#include <algorithm>
namespace vdev {
void SchematicModel::Reindex() {
    m_BehaviorIndex.clear();
    m_OpenGraphs.clear();
    m_OpenGraphs[script.id] = script.open;
    for (size_t i = 0; i < behaviors.size(); ++i) {
        m_BehaviorIndex[behaviors[i].id] = i;
        m_OpenGraphs[behaviors[i].id] = behaviors[i].expandable && !behaviors[i].folded;
    }
}
void SchematicModel::MeasureText(const TextMeasure &measure) {
    for (auto &item : params) {
        item.display = item.bounds;
        const auto mode = ParamDisplayMode(item.style);
        if (measure && (mode == PARAM_STYLE_VALUE || mode == PARAM_STYLE_NAMEVALUE)) {
            const auto text = mode == PARAM_STYLE_VALUE ? item.value : item.name + ": " + item.value;
            item.display.hSize = std::min(item.bounds.hSize, std::max(20.0f, measure(text) + 30.0f));
        }
    }
}

const BehaviorItem *SchematicModel::FindBehavior(uint32_t id) const {
    auto found = m_BehaviorIndex.find(id);
    return found != m_BehaviorIndex.end() ? &behaviors[found->second] : nullptr;
}

const PinItem *SchematicModel::FindPin(const LinkEndpoint &endpoint) const {
    for (const auto &pin : pins)
        if (pin.endpoint.id == endpoint.id && pin.endpoint.index == endpoint.index &&
            pin.endpoint.type == endpoint.type)
            return &pin;
    return nullptr;
}

uint32_t SchematicModel::GraphAt(const Point &point) const {
    uint32_t best = 0;
    int bestDepth = -1;
    for (const auto &behavior : behaviors) {
        if (!IsOpenGraph(behavior.id) || behavior.depth <= bestDepth) continue;
        if (behavior.box.Contains(point)) {
            best = behavior.id;
            bestDepth = behavior.depth;
        }
    }
    if (best) return best;
    return script.open && script.bounds.Contains(point) ? script.id : 0;
}
}
