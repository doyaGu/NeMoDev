#include "core/schematic/LayoutCalculator.h"

#include <map>
#include <algorithm>
#include <set>
#include <tuple>
#include <limits>

#include "CKAll.h"
#include "core/schematic/BehaviorDisplayGeometry.h"

#undef min
#undef max

//==============================================================================
// Construction
//==============================================================================

LayoutCalculator::LayoutCalculator(InterfaceData &targetData, CKContext *context)
    : m_Data(targetData), m_Context(context) {}

//==============================================================================
// Public Interface
//==============================================================================

void LayoutCalculator::CalculateRuntimeGraphLayout(CKBehavior *script) {
    const auto behaviorIds = GetBehaviorIds();

    if (script && m_Context) {
        CalculateBehaviorSizes(behaviorIds);
    }

    const Point topLevelOffset = TopLevelBehaviorCursorOffset();
    Point topLevelCursor(m_Data.rootBehavior.rect.hPos + topLevelOffset.h,
                         m_Data.rootBehavior.rect.vPos + topLevelOffset.v);
    PlaceRuntimeGraphChildren(m_Data.rootBehavior, topLevelCursor);
    CalculateRuntimeGraphElementPositions(behaviorIds);
    PlaceScriptMultiConsumerLocalParameters();
    PlaceScriptOutputDestinations();
    PlaceGraphOutputDestinationShortcuts();
    SeparateScriptParameterShortcutNames();
    FinalizeRuntimeGraphScriptLayout();
    m_Data.NotifyObservers(nullptr, InterfaceData::ElementAction::Modified);
}

//==============================================================================
// Layout Process Coordination
//==============================================================================

void LayoutCalculator::CalculateBehaviorSizes(const std::vector<CK_ID> &behaviorIds) {
    if (!m_Context) return;

    // Group behaviors by depth to ensure parents are processed before children
    std::map<int, std::vector<std::pair<BehaviorData *, CKBehavior *>>> behaviorsByDepth;

    for (const CK_ID behaviorId : behaviorIds) {
        auto *behaviorData = GetBehaviorData(behaviorId);
        auto *behavior = static_cast<CKBehavior *>(m_Context->GetObject(behaviorId));

        if (!behaviorData || !behavior) {
            m_Context->OutputToConsoleEx((CKSTRING)"Behavior not found: %d", behaviorId);
            continue;
        }

        behaviorsByDepth[behaviorData->depth].emplace_back(behaviorData, behavior);
    }

    for (std::map<int, std::vector<std::pair<BehaviorData *, CKBehavior *>>>::const_iterator it = behaviorsByDepth.begin();
         it != behaviorsByDepth.end();
         ++it) {
        const std::vector<std::pair<BehaviorData *, CKBehavior *>> &behaviors = it->second;
        for (std::vector<std::pair<BehaviorData *, CKBehavior *>>::const_iterator jt = behaviors.begin();
             jt != behaviors.end();
             ++jt) {
            BehaviorData *data = jt->first;
            CKBehavior *behavior = jt->second;
            CalculateBehaviorSize(*data, behavior);
        }
    }

}

void LayoutCalculator::CalculateBehaviorSize(BehaviorData &behaviorData, CKBehavior *behavior) {
    if (behaviorData.depth <= 0) return;

    // Calculate height based on IO count
    const int inputCount = behavior->GetInputCount();
    const int outputCount = behavior->GetOutputCount();
    const int height = std::max({inputCount, outputCount, 1});

    // Calculate width based on parameters and name length
    const int paramInputCount = behavior->GetInputParameterCount();
    const int paramOutputCount = behavior->GetOutputParameterCount();

    const char *name = behavior->GetName();
    const size_t nameLength = name ? strlen(name) : 0;
    const int nameWidth = BehaviorNameColumnEstimate(nameLength);

    int width = std::max({
        paramInputCount,
        paramOutputCount,
        nameWidth,
        DefaultBehaviorContentColumns()
    });

    // Set size
    behaviorData.rect.hSize = static_cast<float>(width) * HORIZONTAL_SPACING;
    behaviorData.rect.vSize = static_cast<float>(height) * VERTICAL_SPACING;

    if (behaviorData.isUsingTarget) {
        behaviorData.rect.hSize += HORIZONTAL_SPACING;
    }

    // Set expanded size for behavior graphs
    if (behaviorData.isBehaviorGraph) {
        float expansionFactor = BEHAVIOR_EXPANSION_FACTOR;
        if (behavior->GetSubBehaviorCount() > 10) {
            expansionFactor *= 1.5f;  // More space for complex graphs
        }

        behaviorData.hExpandSize = behaviorData.rect.hSize * expansionFactor;
        behaviorData.vExpandSize = behaviorData.rect.vSize * expansionFactor;
    }
}

void LayoutCalculator::CalculateRuntimeGraphElementPositions(const std::vector<CK_ID> &behaviorIds) {
    for (const CK_ID behaviorId : behaviorIds) {
        auto *behaviorData = GetBehaviorData(behaviorId);
        if (!behaviorData || !behaviorData->isBehaviorGraph) continue;

        const bool parentIsScriptUi = behaviorData == &m_Data.rootBehavior;
        ApplyParameterOperationLayout(*behaviorData, parentIsScriptUi);
        ApplyFallbackLocalParameterLayout(*behaviorData, parentIsScriptUi);
        CalculateLocalParameterPositions(*behaviorData);
    }
}

void LayoutCalculator::FinalizeRuntimeGraphScriptLayout() {
    float maxBottom = m_Data.rootBehavior.rect.vPos + m_Data.rootBehavior.rect.vSize;

    // 0x4FAE80 measures only the Script element's direct children
    // (CUIKElement::CollectChildrenOfType is not recursive). Behaviors inside
    // a child graph belong to that graph's display.
    for (const auto &behavior : m_Data.behaviors) {
        if (behavior.parentId != m_Data.rootBehavior.id) continue;
        maxBottom = std::max(maxBottom, behavior.rect.vPos + behavior.rect.vSize);
    }

    for (const auto &operation : m_Data.rootBehavior.operations) {
        maxBottom = std::max(maxBottom, operation.vPos + VERTICAL_SPACING);
    }

    // Start belongs to the behavior flow. An unused parameter farther down
    // the Script must enlarge its display range without moving Start down.
    const float startVertical =
        (maxBottom + VERTICAL_SPACING * EXPANSION_PADDING - VERTICAL_SPACING) / 2.0f;

    // Dev's Script minimum-height calculation (0x4FAE80) includes parameter
    // display bounds, not just behavior/operation/comment rectangles. NAME,
    // COLLAPSED and the reconstructed shortcut displays occupy one grid row.
    for (const auto* parameters : {&m_Data.rootBehavior.localParams,
                                   &m_Data.rootBehavior.sharedParams}) {
        for (const auto& parameter : *parameters) {
            maxBottom = std::max(maxBottom,
                (static_cast<float>(parameter.vPos) + 1.0f) * VERTICAL_SPACING);
        }
    }

    for (const auto &comment : m_Data.rootBehavior.comments) {
        maxBottom = std::max(maxBottom, comment.vPos + comment.height);
    }

    const float boundsBehaviorHeight = maxBottom + VERTICAL_SPACING * EXPANSION_PADDING;
    m_Data.rootBehavior.folded = true;
    SetStart(m_Data.rootBehavior, startVertical, boundsBehaviorHeight);
}

//==============================================================================
// Element Lookup
//==============================================================================

BehaviorData *LayoutCalculator::GetBehaviorData(CK_ID id) const {
    return m_Data.FindBehavior(id);
}

Operation *LayoutCalculator::GetOperation(CK_ID id) const {
    return m_Data.FindOperation(id);
}

bool LayoutCalculator::IsOperation(CK_ID id) const {
    if (m_Data.FindOperation(id)) return true;
    if (!m_Context) return false;
    auto *obj = m_Context->GetObject(id);
    return obj && obj->GetClassID() == CKCID_PARAMETEROPERATION;
}

std::vector<CK_ID> LayoutCalculator::GetBehaviorIds() const {
    std::vector<CK_ID> ids;
    ids.reserve(1 + m_Data.behaviors.size());

    ids.push_back(m_Data.rootBehavior.id);
    for (const auto &behavior : m_Data.behaviors) {
        ids.push_back(behavior.id);
    }

    return ids;
}

//==============================================================================
// Size and Position Calculation
//==============================================================================

float LayoutCalculator::PlaceRuntimeGraphChildren(BehaviorData &parent, Point cursor) {
    std::unordered_set<CK_ID> visited;
    return PlaceRuntimeGraphFlowChildren(parent, parent.id, cursor, visited, true);
}

float LayoutCalculator::PlaceRuntimeGraphFlowChildren(BehaviorData &parent,
                                                 CK_ID currentBehaviorId,
                                                 Point cursor,
                                                 std::unordered_set<CK_ID> &visited,
                                                 bool includeFallbackChildren) {
    float totalHeight = 0.0f;

    auto linkedChildren = CollectRuntimeGraphLinkedChildren(parent, currentBehaviorId, visited);
    for (auto it = linkedChildren.rbegin(); it != linkedChildren.rend(); ++it) {
        BehaviorData *child = *it;
        if (!child || visited.find(child->id) != visited.end()) continue;

        const Point childCursor(cursor.h, cursor.v + totalHeight);
        totalHeight += PlaceRuntimeGraphChildAndFlowDescendants(parent, *child, childCursor, visited);
    }

    if (includeFallbackChildren) {
        auto fallbackChildren = CollectRuntimeGraphFallbackChildren(parent, visited);
        for (BehaviorData *child : fallbackChildren) {
            if (!child || visited.find(child->id) != visited.end()) continue;

            const Point childCursor(cursor.h, cursor.v + totalHeight);
            totalHeight += PlaceRuntimeGraphChildAndFlowDescendants(parent, *child, childCursor, visited);
        }
    }

    return totalHeight;
}

float LayoutCalculator::PlaceRuntimeGraphChildAndFlowDescendants(BehaviorData &parent,
                                                            BehaviorData &child,
                                                            Point cursor,
                                                            std::unordered_set<CK_ID> &visited) {
    if (!visited.insert(child.id).second) {
        return 0.0f;
    }

    PlaceBehaviorAtCursor(child, cursor, child.rect.hSize, child.rect.vSize);

    if (child.isBehaviorGraph) {
        PlaceRuntimeGraphChildren(child, ChildGraphCursor(child));
    }

    const int layoutColumnCount = LayoutColumnCount(child.rect.hSize);
    const int outputRowIndex = OutputRowIndex(child.rect.vSize);
    const Point downstreamCursor(
        cursor.h + BehaviorCursorHorizontalAdvance(layoutColumnCount),
        cursor.v);
    const float flowHeight = PlaceRuntimeGraphFlowChildren(parent, child.id, downstreamCursor, visited, false);
    return std::max(flowHeight, BehaviorCursorMinimumVerticalAdvance(outputRowIndex));
}

std::vector<BehaviorData *> LayoutCalculator::CollectRuntimeGraphLinkedChildren(
    BehaviorData &parent,
    CK_ID currentBehaviorId,
    const std::unordered_set<CK_ID> &visited) {

    std::vector<BehaviorData *> children;
    std::unordered_set<CK_ID> queued;

    for (const Link &link : parent.links) {
        if (!link.IsBehaviorLink()) continue;

        bool matchesCurrent = false;
        if (currentBehaviorId == parent.id) {
            matchesCurrent = link.start.IsStartBehaviorInput() ||
                (link.start.type == ENDPOINT_BIN && link.start.id == parent.id);
        } else {
            matchesCurrent = link.start.type == ENDPOINT_BOUT && link.start.id == currentBehaviorId;
        }

        if (!matchesCurrent) continue;

        BehaviorData *child = GetBehaviorData(link.end.id);
        if (!child || child == &parent) continue;
        if (child->parentId != parent.id) continue;
        if (visited.find(child->id) != visited.end()) continue;
        if (!queued.insert(child->id).second) continue;

        children.push_back(child);
    }

    return children;
}

std::vector<BehaviorData *> LayoutCalculator::CollectRuntimeGraphFallbackChildren(
    BehaviorData &parent,
    const std::unordered_set<CK_ID> &visited) {

    struct ChildCandidate {
        BehaviorData *behavior = nullptr;
        int vectorIndex = -1;
    };

    std::vector<ChildCandidate> candidates;
    candidates.reserve(m_Data.behaviors.size());
    for (int i = 0, count = static_cast<int>(m_Data.behaviors.size()); i < count; ++i) {
        BehaviorData &behavior = m_Data.behaviors[i];
        if (behavior.parentId != parent.id) continue;
        if (visited.find(behavior.id) != visited.end()) continue;
        candidates.push_back(ChildCandidate{&behavior, i});
    }

    std::sort(candidates.begin(), candidates.end(), [](const ChildCandidate &a, const ChildCandidate &b) {
        const int aRuntimeIndex = a.behavior ? a.behavior->runtimeSiblingIndex : -1;
        const int bRuntimeIndex = b.behavior ? b.behavior->runtimeSiblingIndex : -1;
        if (aRuntimeIndex >= 0 && bRuntimeIndex >= 0 && aRuntimeIndex != bRuntimeIndex) {
            return aRuntimeIndex > bRuntimeIndex;
        }
        return a.vectorIndex > b.vectorIndex;
    });

    std::vector<BehaviorData *> children;
    children.reserve(candidates.size());
    for (const ChildCandidate &candidate : candidates) {
        children.push_back(candidate.behavior);
    }
    return children;
}

void LayoutCalculator::SetStart(BehaviorData &script, float verticalStartPos, float verticalSize) {
    auto &header = m_Data.header;
    header.id = script.id;
    header.vSize = verticalSize;
    header.vStartPos = verticalStartPos;
    header.vStart = 0;
}

//==============================================================================
// Operation and Parameter Layout
//==============================================================================

Rect LayoutCalculator::GetScriptParameterDisplayRect(const Parameter& parameter,
                                                      float nameHorizontalSpacing) const {
    Rect display = ParameterDisplayGeometry::EstimateBounds(parameter, m_Context, true);
    if (parameter.style == PARAM_STYLE_NAME) {
        display.hPos -= nameHorizontalSpacing;
        display.hSize += 2.0f * nameHorizontalSpacing;
    }
    return display;
}

std::vector<Rect> LayoutCalculator::CollectScriptDisplayBounds(
    const std::unordered_set<const Parameter*>& omitted,
    float nameHorizontalSpacing) const {
    std::vector<Rect> occupied;
    for (const auto* parameters : {&m_Data.rootBehavior.localParams, &m_Data.rootBehavior.sharedParams}) {
        for (const auto& parameter : *parameters) {
            if (omitted.find(&parameter) == omitted.end()) {
                occupied.push_back(GetScriptParameterDisplayRect(parameter, nameHorizontalSpacing));
            }
        }
    }
    AppendGraphElementBounds(m_Data.rootBehavior, Point(0.0f, 0.0f), occupied);
    for (const auto& comment : m_Data.rootBehavior.comments) occupied.push_back(comment.GetRect());
    return occupied;
}

void LayoutCalculator::AppendGraphElementBounds(const BehaviorData& graph,
                                                const Point& origin,
                                                std::vector<Rect>& occupied) const {
    for (const auto& behavior : m_Data.behaviors) {
        if (behavior.parentId != graph.id) continue;
        Rect content;
        if (!BehaviorDisplayGeometry::TryGetContentBounds(behavior, content)) continue;
        // A placement reservation includes one grid row/column around the
        // content. BehaviorIO centers extend beyond the content's left/right;
        // NAME displays must not start at a body edge and cover those centers.
        occupied.emplace_back(behavior.rect.hPos + content.hPos - HORIZONTAL_SPACING - origin.h,
                              behavior.rect.vPos + content.vPos - VERTICAL_SPACING - origin.v,
                              content.hSize + 2.0f * HORIZONTAL_SPACING,
                              content.vSize + 2.0f * VERTICAL_SPACING);
    }
    for (const auto& operation : graph.operations) {
        Rect display;
        if (!ParameterDisplayGeometry::TryGetOperationBounds(operation, display)) continue;
        display.hPos -= origin.h;
        display.vPos -= origin.v;
        occupied.push_back(display);
    }
}

bool LayoutCalculator::TryGetOutputParameterPosition(const LinkEndpoint& endpoint, Point& position) const {
    if (endpoint.type != ENDPOINT_POUT || endpoint.index < 0) return false;
    if (const auto* behavior = GetBehaviorData(endpoint.id)) {
        position = BehaviorOutputParameterPosition(behavior->rect, endpoint.index, behavior->isUsingTarget);
        return true;
    }
    if (const auto* operation = GetOperation(endpoint.id)) {
        position = Point(operation->hPos, operation->vPos + VERTICAL_SPACING);
        return true;
    }
    return false;
}

Point LayoutCalculator::AveragePosition(const std::vector<Point>& positions) {
    // Several producer POUTs may share one display; callers pass a non-empty list.
    Point average;
    for (const auto& position : positions) {
        average.h += position.h;
        average.v += position.v;
    }
    average.h /= static_cast<float>(positions.size());
    average.v /= static_cast<float>(positions.size());
    return average;
}

Rect LayoutCalculator::FindFreeParameterDisplay(const Rect& preferred,
                                                const std::vector<Rect>& occupied,
                                                bool searchAbove) const {
    const auto overlaps = [](const Rect& a, const Rect& b) {
        return a.hPos < b.Right() && a.Right() > b.hPos &&
               a.vPos < b.Bottom() && a.Bottom() > b.vPos;
    };
    const auto isFree = [&occupied, &overlaps](const Rect& candidate) {
        return std::none_of(occupied.begin(), occupied.end(), [&candidate, &overlaps](const Rect& used) {
            return overlaps(candidate, used);
        });
    };
    Rect candidate = preferred;
    bool placed = isFree(candidate);
    // Shortcut names prefer rows above their input. Output destinations keep
    // the row below the output and scan right when that display is occupied.
    for (int row = 1; searchAbove && !placed && row <= 4; ++row) {
        candidate = preferred;
        candidate.vPos -= static_cast<float>(row) * VERTICAL_SPACING;
        if (candidate.vPos < 0.0f) break;
        placed = isFree(candidate);
    }
    if (!placed) {
        candidate = preferred;
        if (searchAbove) candidate.vPos = std::max(0.0f, preferred.vPos - VERTICAL_SPACING);
        // Each collision advances past an occupied right edge: a finite,
        // deterministic scan shared by both parameter-placement operations.
        while (!isFree(candidate)) {
            float right = candidate.hPos;
            for (const auto& used : occupied) {
                if (overlaps(candidate, used)) right = std::max(right, used.Right());
            }
            candidate.hPos = std::ceil(right / HORIZONTAL_SPACING) * HORIZONTAL_SPACING;
        }
    }
    return candidate;
}

void LayoutCalculator::PlaceScriptOutputDestinations() {
    using DisplayKey = std::pair<EndpointType, int>;
    auto parameterAt = [&](const DisplayKey& key) -> Parameter* {
        auto* table = key.first == ENDPOINT_PLOCAL ? &m_Data.rootBehavior.localParams :
            key.first == ENDPOINT_POUT_SHORTCUT ? &m_Data.rootBehavior.sharedParams : nullptr;
        if (!table || key.second < 0 || key.second >= static_cast<int>(table->size())) return nullptr;
        return &(*table)[key.second];
    };
    std::set<DisplayKey> shortcutSources;
    for (const auto& link : m_Data.rootBehavior.links) {
        if (link.type == LINK_TYPE_PARAMETER && link.start.id == m_Data.rootBehavior.id &&
            link.start.type == ENDPOINT_POUT_SHORTCUT)
            shortcutSources.insert({link.start.type, link.start.index});
    }
    std::map<DisplayKey, std::vector<Point>> outputPositions;
    for (const auto& link : m_Data.rootBehavior.links) {
        if (link.type != LINK_TYPE_PARAMETER || link.end.id != m_Data.rootBehavior.id) continue;
        const DisplayKey destination{link.end.type, link.end.index};
        const Parameter* parameter = parameterAt(destination);
        if (!parameter || link.start.index < 0) continue;
        // A local already aligned with an input stays there. Only standalone
        // NAME locals are moved away from their SDK-slot fallback row.
        if (destination.first == ENDPOINT_PLOCAL && parameter->style != PARAM_STYLE_NAME) continue;
        // A shortcut that also supplies inputs keeps its consumer placement.
        // Destination-only shortcuts (including NAMEVALUE) otherwise remain
        // at the constructor's (0,0), forcing a return to the Script origin.
        if (shortcutSources.count(destination)) continue;
        Point output;
        if (link.start.type == ENDPOINT_POUT) {
            if (!TryGetOutputParameterPosition(link.start, output)) continue;
        } else if (link.start.type == ENDPOINT_POUT_SHORTCUT &&
                   link.start.id == m_Data.rootBehavior.id &&
                   link.start.index < static_cast<int>(m_Data.rootBehavior.sharedParams.size())) {
            const Rect display = GetScriptParameterDisplayRect(m_Data.rootBehavior.sharedParams[link.start.index]);
            output = Point(display.hPos, display.Bottom());
        } else {
            continue;
        }
        outputPositions[destination].push_back(output);
    }
    std::unordered_set<const Parameter*> omitted;
    for (const auto& entry : outputPositions) {
        omitted.insert(parameterAt(entry.first));
    }
    auto occupied = CollectScriptDisplayBounds(omitted);
    for (const auto& entry : outputPositions) {
        const Point output = AveragePosition(entry.second);
        Parameter& parameter = *parameterAt(entry.first);
        Rect preferred = GetScriptParameterDisplayRect(parameter);
        preferred.hPos = std::max(std::round(output.h / HORIZONTAL_SPACING) * HORIZONTAL_SPACING,
            static_cast<float>(ScriptLocalParameterMinimumGridColumn()) * HORIZONTAL_SPACING);
        preferred.vPos = std::max(0.0f,
            std::round(output.v / VERTICAL_SPACING) * VERTICAL_SPACING + VERTICAL_SPACING);
        const Rect placed = FindFreeParameterDisplay(preferred, occupied, false);
        MoveParameterToPosition(parameter, Point(placed.hPos, placed.vPos));
        occupied.push_back(placed);
    }
}

void LayoutCalculator::PlaceGraphOutputDestinationShortcuts() {
    for (auto& graph : m_Data.behaviors) {
        if (!graph.isBehaviorGraph || graph.sharedParams.empty()) continue;
        // Child-graph parameter cells are relative to the graph window, while
        // behavior and operation positions are Script pixels.
        const Point origin(graph.rect.hPos, graph.rect.vPos);
        std::set<int> shortcutSources;
        for (const auto& link : graph.links) {
            if (link.type == LINK_TYPE_PARAMETER && link.start.id == graph.id &&
                link.start.type == ENDPOINT_POUT_SHORTCUT)
                shortcutSources.insert(link.start.index);
        }
        std::map<int, std::vector<Point>> outputPositions;
        for (const auto& link : graph.links) {
            if (link.type != LINK_TYPE_PARAMETER || link.end.id != graph.id ||
                link.end.type != ENDPOINT_POUT_SHORTCUT) continue;
            const int index = link.end.index;
            // A shortcut that also supplies inputs keeps its consumer placement.
            // The graph's own outputs are not drawn as a child box here.
            if (index < 0 || index >= static_cast<int>(graph.sharedParams.size()) ||
                shortcutSources.count(index) || link.start.id == graph.id) continue;
            Point output;
            if (!TryGetOutputParameterPosition(link.start, output)) continue;
            outputPositions[index].push_back(output);
        }
        if (outputPositions.empty()) continue;

        std::vector<Rect> occupied;
        for (const auto* parameters : {&graph.localParams, &graph.sharedParams}) {
            for (int i = 0; i < static_cast<int>(parameters->size()); ++i) {
                if (parameters == &graph.sharedParams && outputPositions.count(i)) continue;
                occupied.push_back(ParameterDisplayGeometry::EstimateBounds((*parameters)[i], m_Context, false));
            }
        }
        AppendGraphElementBounds(graph, origin, occupied);

        for (const auto& entry : outputPositions) {
            const Point output = AveragePosition(entry.second);
            Parameter& parameter = graph.sharedParams[entry.first];
            Rect preferred = ParameterDisplayGeometry::EstimateBounds(parameter, m_Context, false);
            preferred.hPos = std::max(0.0f,
                std::round((output.h - origin.h) / HORIZONTAL_SPACING) * HORIZONTAL_SPACING);
            preferred.vPos = std::max(0.0f,
                std::round((output.v - origin.v) / VERTICAL_SPACING) * VERTICAL_SPACING + VERTICAL_SPACING);
            const Rect placed = FindFreeParameterDisplay(preferred, occupied, false);
            MoveParameterToPosition(parameter, Point(placed.hPos, placed.vPos));
            occupied.push_back(placed);
        }
    }
}

void LayoutCalculator::PlaceScriptMultiConsumerLocalParameters() {
    struct Consumers {
        std::set<std::tuple<CK_ID, int, int>> endpoints;
        float left = std::numeric_limits<float>::max();
    };
    std::map<int, Consumers> consumers;
    for (const auto& link : m_Data.rootBehavior.links) {
        if (link.type != LINK_TYPE_PARAMETER || link.start.type != ENDPOINT_PLOCAL ||
            link.start.id != m_Data.rootBehavior.id || link.start.index < 0 ||
            link.start.index >= static_cast<int>(m_Data.rootBehavior.localParams.size()) ||
            (link.end.type != ENDPOINT_PIN && link.end.type != ENDPOINT_TARGET_PIN)) continue;
        float left;
        if (const auto* behavior = GetBehaviorData(link.end.id)) {
            if (behavior->parentId != m_Data.rootBehavior.id) continue;
            Rect content;
            if (!BehaviorDisplayGeometry::TryGetContentBounds(*behavior, content)) continue;
            left = behavior->rect.hPos + content.hPos;
        } else if (const auto* operation = GetOperation(link.end.id)) {
            left = operation->hPos;
        } else {
            continue;
        }
        auto& targets = consumers[link.start.index];
        targets.endpoints.insert(std::make_tuple(link.end.id, link.end.index, static_cast<int>(link.end.type)));
        targets.left = std::min(targets.left, left);
    }
    std::unordered_set<const Parameter*> omitted;
    for (const auto& entry : consumers) {
        const auto& parameter = m_Data.rootBehavior.localParams[entry.first];
        if (entry.second.endpoints.size() > 1 && parameter.style == PARAM_STYLE_COLLAPSED)
            omitted.insert(&parameter);
    }
    auto occupied = CollectScriptDisplayBounds(omitted);
    for (const auto& entry : consumers) {
        auto& parameter = m_Data.rootBehavior.localParams[entry.first];
        if (!omitted.count(&parameter)) continue;
        Rect preferred = GetScriptParameterDisplayRect(parameter);
        // Preserve the first consumer's row, but leave a complete grid cell
        // before its placement reservation. A single-consumer local retains
        // the native input-aligned position. This is a library layout policy,
        // not a claim about Dev's reconstruction of multi-consumer sources.
        preferred.hPos = std::max(
            static_cast<float>(ScriptLocalParameterMinimumGridColumn()) * HORIZONTAL_SPACING,
            std::min(preferred.hPos, entry.second.left - 2.0f * HORIZONTAL_SPACING));
        const Rect placed = FindFreeParameterDisplay(preferred, occupied, false);
        MoveParameterToPosition(parameter, Point(placed.hPos, placed.vPos));
        occupied.push_back(placed);
    }
}

void LayoutCalculator::SeparateScriptParameterShortcutNames() {
    std::unordered_set<const Parameter*> omitted;
    for (const auto& parameter : m_Data.rootBehavior.sharedParams) {
        if (parameter.style == PARAM_STYLE_NAME) omitted.insert(&parameter);
    }
    // Adjacent NAME entries need a free grid column beside their endpoints.
    // Otherwise a first-row source can be trapped by the neighbor's clearance
    // and the input-parameter row once routing is confined to the Script origin.
    auto occupied = CollectScriptDisplayBounds(omitted, HORIZONTAL_SPACING);
    for (auto& parameter : m_Data.rootBehavior.sharedParams) {
        if (parameter.style != PARAM_STYLE_NAME) continue;
        const Rect candidate = FindFreeParameterDisplay(GetScriptParameterDisplayRect(parameter), occupied, true);
        MoveParameterToPosition(parameter, Point(candidate.hPos, candidate.vPos));
        occupied.push_back(GetScriptParameterDisplayRect(parameter, HORIZONTAL_SPACING));
    }
}

void LayoutCalculator::MoveParameterToPosition(Parameter &parameter, const Point &pixelPos) {
    if (std::isfinite(pixelPos.h) && std::isfinite(pixelPos.v)) {
        // Convert pixels to grid indices
        parameter.hPos = static_cast<int>(std::lround(pixelPos.h / HORIZONTAL_SPACING));
        parameter.vPos = static_cast<int>(std::lround(pixelPos.v / VERTICAL_SPACING));
    } else {
        m_Context->OutputToConsoleEx((CKSTRING)"Warning: Invalid parameter position (%f, %f)",
                                     pixelPos.h, pixelPos.v);
    }
}

void LayoutCalculator::MoveOperationToPosition(Operation &operation, const Point &pixelPos) {
    operation.hPos = SnapToGrid(pixelPos.h, HORIZONTAL_SPACING);
    operation.vPos = SnapToGrid(pixelPos.v, VERTICAL_SPACING);
}

void LayoutCalculator::ApplyFallbackLocalParameterLayout(BehaviorData &behaviorGraph, bool parentIsScriptUi) {
    for (size_t i = 0; i < behaviorGraph.localParams.size(); ++i) {
        Parameter &parameter = behaviorGraph.localParams[i];
        const Point gridPos = FallbackLocalParameterGridPosition(static_cast<int>(i));
        parameter.hPos = parentIsScriptUi
            ? std::max(static_cast<int>(gridPos.h), ScriptLocalParameterMinimumGridColumn())
            : static_cast<int>(gridPos.h);
        parameter.vPos = static_cast<int>(gridPos.v);
    }
}
void LayoutCalculator::ApplyParameterOperationLayout(BehaviorData &behaviorGraph, bool parentIsScriptUi) {
    for (size_t i = 0; i < behaviorGraph.operations.size(); ++i) {
        Operation &operation = behaviorGraph.operations[i];
        MoveOperationToPosition(
            operation,
            ParameterOperationPosition(behaviorGraph.rect, static_cast<int>(i), parentIsScriptUi));
    }
}

Point LayoutCalculator::GetInputParamPosition(CK_ID targetId, int inputIndex) {
    Point position;

    if (IsOperation(targetId)) {
        if (auto *operation = GetOperation(targetId)) {
            position.h = operation->hPos + static_cast<float>(inputIndex) * HORIZONTAL_SPACING * 2.0f;
            position.v = operation->vPos;
        }
    } else if (auto *behaviorData = GetBehaviorData(targetId)) {
        if (inputIndex >= 0) {
            position = BehaviorInputParameterPosition(behaviorData->rect, inputIndex);
        } else {
            position.h = behaviorData->rect.hPos - HORIZONTAL_SPACING;
            position.v = behaviorData->rect.vPos - VERTICAL_SPACING;
        }
    }

    return position;
}

void LayoutCalculator::CalculateLocalParameterPositions(BehaviorData &behaviorGraph) {
    // Maintain a set of parameters we've already positioned to avoid overwrites
    std::unordered_set<Parameter *> processedParams;

    for (auto &paramLink : behaviorGraph.links) {
        if (paramLink.type != LINK_TYPE_PARAMETER)
            continue;

        if (paramLink.end.type != ENDPOINT_PIN && paramLink.end.type != ENDPOINT_TARGET_PIN)
            continue;

        Parameter *startParam = nullptr;

        if (paramLink.start.type == ENDPOINT_POUT_SHORTCUT) {
            if (paramLink.start.index >= 0 &&
                paramLink.start.index < static_cast<int>(behaviorGraph.sharedParams.size())) {
                startParam = &behaviorGraph.sharedParams[paramLink.start.index];
            }
        } else if (paramLink.start.type == ENDPOINT_PLOCAL) {
            if (paramLink.start.index >= 0 &&
                paramLink.start.index < static_cast<int>(behaviorGraph.localParams.size())) {
                startParam = &behaviorGraph.localParams[paramLink.start.index];
            }
        }

        if (startParam && processedParams.find(startParam) == processedParams.end()) {
            const bool targetExists =
                GetBehaviorData(paramLink.end.id) != nullptr ||
                GetOperation(paramLink.end.id) != nullptr ||
                IsOperation(paramLink.end.id);
            if (!targetExists) {
                continue;
            }

            const int inputIndex = paramLink.end.type == ENDPOINT_TARGET_PIN ? -1 : paramLink.end.index;
            MoveParameterToPosition(*startParam, GetInputParamPosition(paramLink.end.id, inputIndex));
            if (paramLink.start.type == ENDPOINT_PLOCAL) {
                // Dev 0x53A450 collapses type-9 local sources before placing
                // them at the destination. A name cannot fit 20px port spacing.
                startParam->style = PARAM_STYLE_COLLAPSED;
            }
            processedParams.insert(startParam);
        }
    }
}
