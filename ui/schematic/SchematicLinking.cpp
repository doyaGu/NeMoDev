// Linking from a multiselection and exported parameters (Dev documentation,
// link.html): Shift+drag export/re-import of pins, local parameters dragged
// across graph borders, links from several selected pins at once.

#include "ui/schematic/SchematicCanvas.h"

#include <algorithm>
#include <cmath>

#include "core/graph/BehaviorCommands.h"
#include "core/graph/ExportCommands.h"
#include "core/graph/LinkCommands.h"
#include "core/schematic/SchematicEdits.h"

namespace vdev {

namespace {

constexpr float GRID_SIZE = 20.0f;

bool Encloses(const InterfaceData &data, CK_ID graph, CK_ID into) {
    for (CK_ID at = into; at; at = FindRecord(data, at) ? FindRecord(data, at)->parentId : 0)
        if (at == graph) return true;
    return false;
}

} // namespace

void SchematicCanvas::BeginExportDrag() {
    m_Doc.exportPins.clear();
    if (!m_Doc.press.view || m_Doc.press.ref.kind != ElementKind::Pin) return;
    const CK_ID script = m_Doc.press.view->id;
    const bool group = IsSelected(script, m_Doc.press.ref);
    for (const auto &pin : m_Doc.press.view->model.pins) {
        if (pin.onOperation || pin.kind == PinKind::Target) continue;
        const bool pressed = pin.ref == m_Doc.press.ref;
        if (pressed || (group && pin.kind == m_Doc.press.ref.pin && IsSelected(script, pin.ref))) m_Doc.exportPins.push_back(pin);
    }
    if (m_Doc.exportPins.empty()) return;
    m_Doc.exportScript = script;
    m_Tool = Tool::ExportDrag;
}

void SchematicCanvas::UpdateExportDrag(const ImVec2 &mouse) {
    ScriptView *view = ViewFor(m_Doc.exportScript);
    if (!view || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_Tool = Tool::Idle;
        return;
    }
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    for (const auto &pin : m_Doc.exportPins) {
        const ImVec2 from = view->transform.ToScreen(pin.center.h, pin.center.v + view->offsetY);
        drawList->AddLine(from, mouse, m_State.style.highlightLink, 2.0f);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) EndExportDrag(mouse);
}

void SchematicCanvas::EndExportDrag(const ImVec2 &mouse) {
    m_Tool = Tool::Idle;
    ScriptView *view = ViewFor(m_Doc.exportScript);
    if (!view || !m_State.document->Session(view->id)) return;
    const Point world = view->transform.ToWorld(mouse);
    const Point local(world.h, world.v - view->offsetY);
    if (GraphAt(*view, local))
        for (const EditResult &refused : ExportPinsAt(*m_State.document, view->id, m_Doc.exportPins, local))
            Report(refused, "Export");
    m_Doc.exportPins.clear();
}

// Dev: local parameters (of one graph) dropped outside their graph export
// the input they feed; dropped into the graph exporting it, re-import it.
// Otherwise the move stays an ordinary move. True when it ended the move
// gesture (exported, or recorded as a move after a refusal).
bool SchematicCanvas::ExportMovedLocals() {
    ScriptView *view = ViewFor(m_Doc.moveScript);
    ScriptSession *session = view ? m_State.document->Session(m_Doc.moveScript) : nullptr;
    if (!session || m_Doc.moveItems.empty()) return false;
    CK_ID graph = 0;
    for (const auto &item : m_Doc.moveItems) {
        if (item.kind != ElementKind::LocalParam || (graph && item.owner != graph)) return false;
        graph = item.owner;
    }
    const Point world = view->transform.ToWorld(ImGui::GetIO().MousePos);
    const Point mouse(world.h, world.v - view->offsetY);
    const CK_ID into = GraphAt(*view, mouse);
    if (!into || into == graph) return false;
    // Where each local was dropped (its dragged cell).
    InterfaceData moved = session->Data();
    moved.observers.clear();
    std::vector<std::pair<CK_ID, Point>> locals;
    if (const BehaviorData *record = FindRecord(moved, graph))
        for (const auto &item : m_Doc.moveItems)
            if (item.index >= 0 && item.index < static_cast<int>(record->localParams.size())) {
                const Parameter &cell = record->localParams[item.index];
                locals.emplace_back(item.object, Point(record->rect.hPos + cell.hPos * GRID_SIZE + 1.0f,
                                                       record->rect.vPos + cell.vPos * GRID_SIZE + 1.0f));
            }
    // The move is taken back; the exports are the undo step.
    m_Doc.gesture->Cancel();
    size_t steps = 0;
    std::string refusal;
    MoveLocalsAcrossGraphs(*m_State.document, m_Doc.moveScript, graph, locals, into, steps, refusal);
    if (steps) return true;
    // Not an export: the ordinary move.
    Edit move(*m_State.document, "Move", Edit::Marks::Scripts);
    move.ShowScript(m_Doc.moveScript, moved);
    move.Commit();
    if (Encloses(moved, into, graph)) m_State.Log(LogLevel::Info, "Export local parameter: " + refusal);
    return true;
}

// Dev "Link from a multiselection": the selected pins of the pressed pin's
// kind link to the pins of the destination's owner at the same offsets, or
// (Alt) all to the destination. Dropped on a behavior, they get new pins of
// a behavior graph or variable Building Block. Only allowed links are made.
bool SchematicCanvas::MultiLink(const Hit &hit) {
    ScriptView *view = ViewFor(m_Doc.linkScript);
    if (!view || !hit.view || hit.view != view || m_Doc.relinkLink) return false;
    const CK_ID script = view->id;
    Document &document = *m_State.document;
    CKContext *context = document.Context();
    struct Source {
        LinkEndpoint endpoint;
        Point position;
        PinKind kind = PinKind::None;
        std::string name;
        CKGUID type;
    };
    const ElementRef pressed = m_Doc.press.ref;
    const bool group = m_Doc.press.view == view && IsSelected(script, pressed);
    std::vector<Source> sources;
    auto add = [&](const ElementRef &ref) {
        Hit at;
        at.view = view;
        at.ref = ref;
        Source source;
        bool behaviorLink = false;
        if (!EndpointOf(at, source.endpoint, behaviorLink, source.position) || behaviorLink != m_LinkBehavior) return;
        source.kind = ref.kind == ElementKind::Pin ? ref.pin : PinKind::ParamOut;
        CKObject *object = context->GetObject(ref.object);
        if (ref.kind == ElementKind::Pin) {
            for (const auto &pin : view->model.pins)
                if (pin.ref == ref) source.name = pin.name;
            CKParameter *parameter = nullptr;
            if (CKBehavior *behavior = CKBehavior::Cast(object))
                if (ref.pin == PinKind::ParamOut && ref.index < behavior->GetOutputParameterCount())
                    parameter = behavior->GetOutputParameter(ref.index);
            if (CKParameterOperation *operation = CKParameterOperation::Cast(object)) parameter = operation->GetOutParameter();
            if (parameter) source.type = parameter->GetGUID();
        } else {
            for (const auto &param : view->model.params)
                if (param.ref == ref) {
                    source.name = param.name;
                    if (CKParameter *parameter = CKParameter::Cast(context->GetObject(param.parameter)))
                        source.type = parameter->GetGUID();
                }
        }
        sources.push_back(source);
    };
    if (group) {
        for (const auto &item : m_State.selection.items)
            if (item.kind == pressed.kind && (item.kind != ElementKind::Pin || item.pin == pressed.pin)) add(item);
    } else {
        add(pressed);
    }
    if (sources.empty()) return false;
    const bool behaviorIo = m_LinkBehavior;
    std::stable_sort(sources.begin(), sources.end(), [&](const Source &a, const Source &b) {
        return behaviorIo ? (a.position.v != b.position.v ? a.position.v < b.position.v : a.position.h < b.position.h)
                          : (a.position.h != b.position.h ? a.position.h < b.position.h : a.position.v < b.position.v);
    });
    int pressedRank = 0;
    for (size_t i = 0; i < sources.size(); ++i)
        if (sources[i].endpoint.id == m_Doc.linkFrom.id && sources[i].endpoint.index == m_Doc.linkFrom.index &&
            sources[i].endpoint.type == m_Doc.linkFrom.type)
            pressedRank = static_cast<int>(i);

    const Preferences &preferences = m_State.preferences;
    std::vector<std::pair<LinkEndpoint, LinkEndpoint>> pairs;
    LinkEndpoint to;
    bool behaviorLink = false;
    Point position;
    if (EndpointOf(hit, to, behaviorLink, position)) {
        if (sources.size() < 2 || behaviorLink != m_LinkBehavior) return false;
        if (ImGui::GetIO().KeyAlt) {
            for (const auto &source : sources) pairs.emplace_back(source.endpoint, to);
        } else {
            if (hit.ref.kind != ElementKind::Pin) return false;
            std::vector<const PinItem *> targets;
            for (const auto &pin : view->model.pins)
                if (pin.owner == hit.ref.object && pin.kind == hit.ref.pin) targets.push_back(&pin);
            std::sort(targets.begin(), targets.end(), [](const PinItem *a, const PinItem *b) { return a->index < b->index; });
            int dropped = -1;
            for (size_t i = 0; i < targets.size(); ++i)
                if (targets[i]->ref == hit.ref) dropped = static_cast<int>(i);
            if (dropped < 0) return false;
            for (size_t i = 0; i < sources.size(); ++i) {
                const int k = dropped + static_cast<int>(i) - pressedRank;
                if (k >= 0 && k < static_cast<int>(targets.size())) pairs.emplace_back(sources[i].endpoint, targets[k]->endpoint);
            }
        }
        if (!LinkPairs(document, script, pairs, m_LinkBehavior, preferences.defaultLinkDelay,
                       preferences.autoLoopLinkDelay))
            m_State.Log(LogLevel::Info, "Link: none of these links is allowed");
        return true;
    }
    if (hit.ref.kind != ElementKind::Behavior || !CKBehavior::Cast(context->GetObject(hit.ref.object))) return false;
    // Automatic creation of the destination pins.
    std::vector<LinkSource> linked;
    for (const auto &source : sources) linked.push_back({source.endpoint, source.kind, source.name, source.type});
    bool kindRefused = false;
    std::string failed;
    const int made = LinkToNewPins(document, script, linked, hit.ref.object, m_LinkBehavior, preferences.defaultLinkDelay,
                                   preferences.autoLoopLinkDelay, &kindRefused, &failed);
    if (kindRefused) m_State.Log(LogLevel::Info, "Link: the Building Block cannot create that kind of pin");
    if (!failed.empty()) Report(EditResult::Refused(failed), "Link");
    return made != 0;
}

} // namespace vdev
