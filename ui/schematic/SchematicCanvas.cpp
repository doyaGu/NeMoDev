#include <climits>
#include <stdexcept>
#include "workspace/BuildingBlockDrag.h"
#include "workspace/ResourceDrag.h"
#include "ui/schematic/SchematicCanvas.h"
#include "ui/schematic/SchematicViews.h"
#include "editor/ImGuiDrawing.h"

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <iterator>

#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/TypeAhead.h"
#include "core/schematic/LinkRouting.h"
#include "ui/schematic/SchematicCanvasDetail.h"
#include "core/schematic/SchematicGrid.h"
#include "core/schematic/SchematicHitTest.h"
#include "ui/schematic/Snapshots.h"
#include "core/runtime/TextEncoding.h"
#include "workspace/EditorClient.h"
#include "features/schematic/SchematicDiagnosticViews.h"
#include "features/schematic/SchematicPosition.h"

namespace vdev {

namespace {

using namespace canvas;

constexpr float SCRIPT_GAP = 0.0f;    // Dev stacks Script bands without a gap
constexpr float WORLD_FONT_SIZE = 13.0f;

EndpointType PinEndpointType(PinKind kind) {
    switch (kind) {
    case PinKind::BehaviorIn: return ENDPOINT_BIN;
    case PinKind::BehaviorOut: return ENDPOINT_BOUT;
    case PinKind::ParamIn: return ENDPOINT_PIN;
    case PinKind::ParamOut: return ENDPOINT_POUT;
    case PinKind::Target: return ENDPOINT_TARGET_PIN;
    default: return ENDPOINT_PIN;
    }
}

Point Center(const Rect &rect) {
    return Point(rect.CenterX(), rect.CenterY());
}

// Script-local center of a referenced element, if it is in the model.
bool FindCenter(const SchematicModel &model, const ElementRef &ref, Point &center) {
    switch (ref.kind) {
    case ElementKind::Script:
        center = Center(model.script.header);
        return true;
    case ElementKind::Behavior:
        if (const BehaviorItem *behavior = model.FindBehavior(ref.object)) {
            center = Center(behavior->box);
            return true;
        }
        return false;
    case ElementKind::Pin:
        if (const PinItem *pin = model.FindPin(LinkEndpoint(ref.object, ref.index, PinEndpointType(ref.pin)))) {
            center = pin->center;
            return true;
        }
        return false;
    case ElementKind::Operation:
        for (const auto &operation : model.operations)
            if (operation.id == ref.object) {
                center = Center(operation.bounds);
                return true;
            }
        return false;
    case ElementKind::LocalParam:
    case ElementKind::Shortcut:
        for (const auto &param : model.params)
            if (param.ref.kind == ref.kind && param.owner == ref.owner && param.slot == ref.index) {
                center = Center(param.display);
                return true;
            }
        return false;
    case ElementKind::Comment:
        for (const auto &comment : model.comments)
            if (comment.owner == ref.owner && comment.slot == ref.index) {
                center = Center(comment.bounds);
                return true;
            }
        return false;
    case ElementKind::Start:
        if (model.start.present) {
            center = Center(model.start.bounds);
            return true;
        }
        return false;
    default:
        return false;
    }
}

} // namespace

SchematicCanvas::ScriptView *SchematicCanvas::ViewFor(CK_ID script) {
    for (auto &view : m_Views)
        if (view.id == script) return &view;
    return nullptr;
}

SchematicCanvas::ScriptView *SchematicCanvas::ViewAtScreen(const ImVec2 &screen) {
    for (auto &view : m_Views) {
        const Rect &bounds = view.model.script.bounds;
        const float top = view.frame.ToScreen(0.0f, bounds.vPos + view.offsetY).y;
        const float bottom = view.frame.ToScreen(0.0f, bounds.Bottom() + view.offsetY).y;
        if (screen.y >= top && screen.y <= bottom) return &view;
    }
    return nullptr;
}

// Bands follow each other at the view zoom (frame transforms). A body draws
// at the view zoom times its Script's scale about the Script's anchor (the
// Start tip), shifted left by the shared horizontal scroll; at 100% and
// unscrolled, body and frame coincide.
void SchematicCanvas::UpdateTransforms() {
    float cursor = 0.0f;
    for (auto &view : m_Views) {
        const ScriptItem &script = view.model.script;
        const Rect &bounds = script.bounds;
        view.stackTop = cursor;
        view.offsetY = cursor - bounds.vPos;
        cursor += bounds.vSize + SCRIPT_GAP;
        view.frame = m_View;
        view.anchor = view.model.start.present ? view.model.start.center
                                               : Point(bounds.hPos + script.header.hSize, bounds.vPos);
        ViewTransform &body = view.transform;
        body.origin = view.frame.ToScreen(view.anchor.h - m_ScrollX, view.anchor.v + view.offsetY);
        body.zoom = m_View.zoom * view.scale;
        body.panX = view.anchor.h;
        body.panY = view.anchor.v + view.offsetY;
        const float left = m_Navigation.options.showHeaders ? bounds.hPos + script.header.hSize : bounds.hPos;
        view.bodyMin = view.frame.ToScreen(left, bounds.vPos + view.offsetY);
        view.bodyMax = view.frame.ToScreen(bounds.Right(), bounds.Bottom() + view.offsetY);
        view.bodyMax.x = std::max(view.bodyMax.x, m_View.origin.x + m_CanvasSize.x);
    }
}

// Dev.exe sub_504BE0 (Automatic Scroll, Autoscroll Delay): while linking,
// moving, selecting, copying, exporting or resizing, every Autoscroll Delay
// ms the view scrolls one grid step towards an edge the mouse is within half
// a step of; what is dragged follows the mouse.
void SchematicCanvas::AutoScroll(const ImVec2 &canvasMin, const ImVec2 &canvasMax) {
    const bool dragging = m_Tool == Tool::Link || m_Tool == Tool::Move || m_Tool == Tool::RectSelect ||
                          m_Tool == Tool::CopyDrag || m_Tool == Tool::ExportDrag || m_Tool == Tool::ResizeElement;
    if (!dragging || !m_State.preferences.Bool("Schematic\\Automatic Scroll")) return;
    const double now = ImGui::GetTime();
    if ((now - m_AutoScrollTime) * 1000.0 < std::max(1, m_State.preferences.Int("Schematic\\Autoscroll Delay"))) return;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float step = SchematicGrid::SPACING * m_View.zoom, margin = step * 0.5f;
    float dx = 0.0f, dy = 0.0f;
    if (mouse.x < canvasMin.x + margin) dx = step;
    else if (mouse.x >= canvasMax.x - margin) dx = -step;
    if (mouse.y < canvasMin.y + margin) dy = step;
    else if (mouse.y >= canvasMax.y - margin) dy = -step;
    if (dx == 0.0f && dy == 0.0f) return;
    m_AutoScrollTime = now;
    PanBy(dx, dy);
}

// The scrollbars move the bodies sideways (m_ScrollX, frame units) and the
// stack of Scripts (PanBy's range); without them the view scrolls by dragging.
void SchematicCanvas::Scrollbars(const ImVec2 &canvasMin, const ImVec2 &canvasMax, bool horizontal, bool vertical) {
    ImGuiWindow *window = ImGui::GetCurrentWindow();
    const float size = ImGui::GetStyle().ScrollbarSize, zoom = std::max(0.01f, m_View.zoom);
    if (horizontal) {
        float widest = 0.0f;
        for (const ScriptView &view : m_Views) widest = std::max(widest, view.model.script.bounds.hSize * view.scale);
        const ImS64 avail = static_cast<ImS64>((canvasMax.x - canvasMin.x) / zoom);
        const ImS64 contents = std::max(avail, static_cast<ImS64>(widest + m_ScrollX));
        ImS64 scroll = static_cast<ImS64>(m_ScrollX);
        const ImRect bb(canvasMin.x, canvasMax.y, canvasMax.x, canvasMax.y + size);
        if (ImGui::ScrollbarEx(bb, window->GetID("##schematich"), ImGuiAxis_X, &scroll, avail, contents) &&
            static_cast<float>(scroll) != m_ScrollX)
            PanBy((m_ScrollX - static_cast<float>(scroll)) * zoom, 0.0f);
    }
    if (vertical && !m_Views.empty()) {
        const float top = m_Views.front().stackTop;
        const float bottom = m_Views.back().stackTop + m_Views.back().model.script.bounds.vSize;
        const ImS64 avail = static_cast<ImS64>((canvasMax.y - canvasMin.y) / zoom);
        const ImS64 contents = std::max(avail, static_cast<ImS64>(bottom - top));
        ImS64 scroll = static_cast<ImS64>(m_View.panY - top);
        const ImRect bb(canvasMax.x, canvasMin.y, canvasMax.x + size, canvasMax.y);
        if (ImGui::ScrollbarEx(bb, window->GetID("##schematicv"), ImGuiAxis_Y, &scroll, avail, contents) &&
            static_cast<float>(scroll) != m_View.panY - top)
            PanBy(0.0f, (m_View.panY - top - static_cast<float>(scroll)) * zoom);
    }
}

void SchematicCanvas::PanBy(float dx, float dy) {
    m_View.panY -= dy / m_View.zoom;
    // The stack scrolls between its top (the first Script at the top of the
    // view, no empty space above it) and its bottom (the last Script ending
    // at the bottom of the view).
    if (!m_Views.empty()) {
        const float top = m_Views.front().stackTop;
        const ScriptView &last = m_Views.back();
        const float bottom = last.stackTop + last.model.script.bounds.vSize;
        const float lowest = std::max(top, bottom - m_CanvasSize.y / std::max(0.01f, m_View.zoom));
        m_View.panY = std::max(top, std::min(lowest, m_View.panY));
    }
    m_ScrollX = std::max(0.0f, m_ScrollX - dx / m_View.zoom);
    UpdateTransforms();
}

// Zooms one Script about its Start (the header, the band and Start stay).
void SchematicCanvas::ZoomScript(ScriptView &view, float scale) {
    const float total = std::max(MIN_ZOOM, std::min(MAX_ZOOM, m_View.zoom * scale));
    view.scale = total / m_View.zoom;
    if (std::fabs(view.scale - 1.0f) < 0.03f) view.scale = 1.0f; // snap back to 100%
    UpdateTransforms();
}

#ifdef VIRTOOLSDEV_UI_TESTS
std::vector<SchematicCanvas::ShownBehavior> SchematicCanvas::ShownBehaviors() const {
    // Drawn as SchematicRenderer does: the children of an open graph, not
    // open themselves.
    std::vector<ShownBehavior> shown;
    // Not drawn at the last frame (a tab behind another, a window closed).
    if (m_PickFrame < ImGui::GetFrameCount() - 1) return shown;
    const ImVec2 canvasMax(m_View.origin.x + m_CanvasSize.x, m_View.origin.y + m_CanvasSize.y);
    for (const ScriptView &view : m_Views) {
        if (!view.model.script.open) continue;
        const ImVec2 clipMin(std::max(view.bodyMin.x, m_View.origin.x), std::max(view.bodyMin.y, m_View.origin.y));
        const ImVec2 clipMax(std::min(view.bodyMax.x, canvasMax.x), std::min(view.bodyMax.y, canvasMax.y));
        for (const BehaviorItem &behavior : view.model.behaviors) {
            if (!view.model.IsOpenGraph(behavior.parent) || view.model.IsOpenGraph(behavior.id)) continue;
            const ImVec2 min = view.transform.ToScreen(behavior.box.hPos, behavior.box.vPos + view.offsetY);
            const ImVec2 max = view.transform.ToScreen(behavior.box.Right(), behavior.box.Bottom() + view.offsetY);
            if (min.x < clipMin.x || min.y < clipMin.y || max.x > clipMax.x || max.y > clipMax.y) continue;
            shown.push_back({view.id, behavior.id, behavior.name, behavior.buildingBlock, min, max});
        }
    }
    return shown;
}

std::vector<SchematicCanvas::ShownPin> SchematicCanvas::ShownPins() const {
    std::vector<ShownPin> shown;
    if (m_PickFrame < ImGui::GetFrameCount() - 1) return shown;
    const ImVec2 canvasMax(m_View.origin.x + m_CanvasSize.x, m_View.origin.y + m_CanvasSize.y);
    for (const ScriptView &view : m_Views) {
        if (!view.model.script.open) continue;
        const ImVec2 clipMin(std::max(view.bodyMin.x, m_View.origin.x), std::max(view.bodyMin.y, m_View.origin.y));
        const ImVec2 clipMax(std::min(view.bodyMax.x, canvasMax.x), std::min(view.bodyMax.y, canvasMax.y));
        for (const PinItem &pin : view.model.pins) {
            if (pin.onOperation || pin.onScript) continue;
            const BehaviorItem *owner = view.model.FindBehavior(pin.owner);
            if (!owner || !view.model.IsOpenGraph(owner->parent) || view.model.IsOpenGraph(owner->id)) continue;
            const ImVec2 center = view.transform.ToScreen(pin.center.h, pin.center.v + view.offsetY);
            if (center.x < clipMin.x || center.y < clipMin.y || center.x > clipMax.x || center.y > clipMax.y) continue;
            shown.push_back({view.id, pin.owner, pin.kind, pin.index, center});
        }
    }
    return shown;
}

std::vector<SchematicCanvas::ShownScript> SchematicCanvas::ShownScripts() const {
    std::vector<ShownScript> shown;
    if (m_PickFrame < ImGui::GetFrameCount() - 1) return shown;
    const ImVec2 canvasMax(m_View.origin.x + m_CanvasSize.x, m_View.origin.y + m_CanvasSize.y);
    for (const ScriptView &view : m_Views) {
        if (!view.model.script.open) continue;
        const auto &bounds = view.model.script.bounds;
        const ImVec2 min = view.transform.ToScreen(bounds.hPos, bounds.vPos + view.offsetY);
        const ImVec2 max = view.transform.ToScreen(bounds.Right(), bounds.Bottom() + view.offsetY);
        const ImVec2 clipMin(std::max({min.x, view.bodyMin.x, m_View.origin.x}),
                             std::max({min.y, view.bodyMin.y, m_View.origin.y}));
        const ImVec2 clipMax(std::min({max.x, view.bodyMax.x, canvasMax.x}),
                             std::min({max.y, view.bodyMax.y, canvasMax.y}));
        const auto &header = view.model.script.header;
        const StartItem &start = view.model.start;
        if (clipMin.x < clipMax.x && clipMin.y < clipMax.y)
            shown.push_back({view.id, clipMin, clipMax, view.frame.ToScreen(header.hPos, header.vPos + view.offsetY),
                             view.frame.ToScreen(header.Right(), header.Bottom() + view.offsetY), start.present,
                             view.frame.ToScreen(start.center.h, start.center.v + view.offsetY)});
    }
    return shown;
}

#endif

SchematicCanvas::ScriptView *SchematicCanvas::BoundaryAt(const ImVec2 &screen) {
    for (auto &view : m_Views) {
        if (!view.model.script.open) continue;
        const Rect &bounds = view.model.script.bounds;
        const float bottom = view.frame.ToScreen(0.0f, bounds.Bottom() + view.offsetY).y;
        if (std::fabs(screen.y - bottom) <= 3.0f) return &view;
    }
    return nullptr;
}

void SchematicCanvas::ShowScript(CK_ID script, const ElementRef &ref, CK_ID focusBehavior, bool focus) {
    if (!Client().Scope().document.value)
        return;
    if (ScriptReferences({script}).empty())
        RefreshStack();
    const auto address = CapturePosition(script, ref.IsValid() ? ref : Selection::HeaderOf(script));
    if (!address)
        return;
    auto position = SchematicPosition(address);
    if (focusBehavior) {
        const auto info = GraphState(script, focusBehavior);
        const auto *target = EditorClient::Find(info, "object");
        if (!target || !target->object)
            return;
        position.fields.push_back({"focus-target", EDITOR_OBJECT, target->object});
    }
    position.fields.push_back({"background", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(!focus)});
    const auto values = BorrowEditorFields(position.fields);
    Client().Command("script.navigate", {values.data(), values.data() + values.size()});
}

void SchematicCanvas::RefreshStack() {
    if (!Client().Scope().document.value)
        return;
    auto args = std::vector<EditorField>{
        {"target", EDITOR_OBJECT, m_Navigation.stackScript},
        {"visible", EDITOR_OBJECT_LIST},
        {"hidden", EDITOR_OBJECT_LIST},
        {"order", EDITOR_OBJECT_LIST},
        {"options",
         EDITOR_INTEGER_VALUE,
         {},
         {},
         uint64_t(m_Navigation.options.showAllScripts) | (uint64_t(m_Navigation.options.sceneFilter) << 1) |
             (uint64_t(m_State.preferences.Bool("3D Layout\\Show Dynamic Objects")) << 2)}};
    args[1].objects = m_VisibleScripts;
    args[2].objects.assign(m_Navigation.hiddenScripts.begin(), m_Navigation.hiddenScripts.end());
    args[3].objects = m_Navigation.scriptOrder;
    EditorField state{"state", EDITOR_RECORD};
    state.fields = std::move(args);
    const std::vector<EditorField> input{state};
    const auto values = BorrowEditorFields(input);
    const auto result = Client().Query("schematic.stack", {values.data(), values.data() + values.size()});
    if (result.status != EDITOR_OK) {
        return;
    }
    const auto *all = EditorClient::Find(result, "all"), *scripts = EditorClient::Find(result, "scripts"),
               *rows = EditorClient::Find(result, "rows"), *target = EditorClient::Find(result, "target");
    if (!all || !scripts || !rows || !target)
        return;
    if (m_VisibleScripts != scripts->objects || m_Navigation.stackScript != target->object)
        ++m_StackRevision;

    m_AllScripts = all->objects;
    m_VisibleScripts = scripts->objects;
    m_ScriptRows = rows->fields;
    m_Navigation.visibleScripts.clear();
    for (auto ref : m_VisibleScripts)
        m_Navigation.visibleScripts.push_back(ref.object);
    m_Navigation.stackScript = target->object;
    m_Topology = EditorClient::Integer(result, "topology");
    m_ContentRevision = EditorClient::Integer(result, "document-revision");
    m_ScriptsRevision = EditorClient::Integer(result, "scripts-revision");
    m_SceneExcluded = EditorClient::Integer(result, "scene-excluded");
    const auto live = [&](ObjectRef ref) {
        return std::find(m_AllScripts.begin(), m_AllScripts.end(), ref) != m_AllScripts.end();
    };
    for (auto *set : {&m_Navigation.hiddenScripts, &m_Navigation.closedScripts})
        for (auto it = set->begin(); it != set->end();)
            if (!live(*it))
                it = set->erase(it);
            else
                ++it;
    auto &order = m_Navigation.scriptOrder;
    order.erase(std::remove_if(order.begin(), order.end(), [&](ObjectRef ref) { return !live(ref); }), order.end());
}

ElementAddress SchematicCanvas::CapturePosition(uint32_t script, const ElementRef &ref) {
    ElementAddress address;
    auto targets = ScriptReferences({script});
    if (targets.empty()) {
        RefreshStack();
        targets = ScriptReferences({script});
    }
    if (targets.empty())
        return address;
    const auto state =
        Client().Query("schematic.script-state", {ObjectArgument("target", targets.front()), IntegerArgument("id", 0)});
    if (state.status != EDITOR_OK)
        return address;
    const auto revision = EditorClient::Integer(state, "revision"), topology = EditorClient::Integer(state, "topology");
    if (const auto *view = ViewFor(script);
        view && view->identity == targets.front() && ref.kind != ElementKind::Script) {
        auto key = ref;
        if (key.kind == ElementKind::ControlPoint)
            key.kind = ElementKind::Link;
        if (key.kind == ElementKind::Link) {
            key.object = 0; // The record slot also represents links without a native object.
            key.sub = -1;
        }
        const auto found = view->positions.find(key);
        if (found != view->positions.end()) {
            auto retained = found->second;
            if (ref.kind == ElementKind::ControlPoint)
                retained.element = ref;
            return ValidPosition(retained) ? retained : ElementAddress{};
        }
        const bool indexed = ref.kind == ElementKind::Pin || ref.kind == ElementKind::LocalParam ||
                             ref.kind == ElementKind::Shortcut || ref.kind == ElementKind::Link ||
                             ref.kind == ElementKind::ControlPoint || ref.kind == ElementKind::Comment;
        if (indexed)
            return {};
    }
    const auto result =
        Client().Query("schematic.position",
                       {ObjectArgument("target", targets.front()), IntegerArgument("revision", revision),
                        IntegerArgument("topology", topology), IntegerArgument("kind", uint64_t(ref.kind)),
                        IntegerArgument("owner-id", ref.owner), IntegerArgument("object-id", ref.object),
                        IntegerArgument("index", uint64_t(int64_t(ref.index))),
                        IntegerArgument("sub", uint64_t(int64_t(ref.sub))), IntegerArgument("pin", uint64_t(ref.pin))});
    if (result.status == EDITOR_OK)
        if (const auto *position = EditorClient::Find(result, "address")) {
            ReadSchematicPosition(*position, address);
        }
    return address;
}
bool SchematicCanvas::ValidPosition(const ElementAddress &address) const {
    if (!address)
        return false;
    EditorField elements{"elements", EDITOR_RECORD};
    elements.fields = {SchematicPosition(address, "0")};
    const std::vector<EditorField> ownedElements{elements};
    const auto raw = BorrowEditorFields(ownedElements);
    const auto result = Client().Query("schematic.validate-positions", {raw.data(), raw.data() + raw.size()});
    const auto *valid = EditorClient::Find(result, "elements");
    return result.status == EDITOR_OK && valid && valid->fields.size() == 1;
}
void SchematicCanvas::ApplyNavigation(const ElementAddress &address, const std::vector<ObjectRef> &parents, bool focus,
                                      bool reveal) {
    ++m_StackRevision;
    if (focus)
        m_Navigation.hiddenScripts.erase(address.script);
    m_Navigation.stackScript = address.script;
    RefreshStack();
    if (!focus)
        return;
    if (m_SceneExcluded)
        m_State.Log(LogLevel::Warning, "Script is not in current scene. Remove Schematic scene filter to show it");
    if (reveal)
        m_Navigation.closedScripts.erase(address.script);
    if (auto *view = ViewFor(address.script.object))
        view->viewRevision = ~0ull;
    m_Navigation.focus.address = address;
    m_Navigation.focus.pending = true;
    m_FocusParents = parents;
}
bool SchematicCanvas::ReadModel(ScriptView &view, const EditorField &row, ImFont *font) {
    EditorResponse meta;
    meta.fields = row.fields;
    const auto revision = EditorClient::Integer(meta, "revision"),
               viewRevision = EditorClient::Integer(meta, "view-revision");
    const auto source = Client().Revision("schematic.model");
    if (view.revision == revision && view.topology == m_Topology && view.viewRevision == viewRevision &&
        view.model.script.open == ScriptOpen(view.id) && view.sourceVersion == source && view.refresh == m_ModelRefresh)
        return true;
    EditorField model;
    uint64_t first = 0, total = 0;
    const auto arguments = [&](uint64_t offset) {
        return std::vector<EditorValue>{
            ObjectArgument("target", view.identity),   IntegerArgument("open", ScriptOpen(view.id)),
            IntegerArgument("first", offset),          IntegerArgument("revision", revision),
            IntegerArgument("topology", m_Topology),   IntegerArgument("view-revision", viewRevision),
            IntegerArgument("refresh", m_ModelRefresh)};
    };
    do {
        const auto result = Client().Query("schematic.model", arguments(first));
        const auto *page = EditorClient::Find(result, "model");
        if (result.status != EDITOR_OK || !page || EditorClient::Integer(result, "revision") != revision ||
            EditorClient::Integer(result, "view-revision") != viewRevision ||
            EditorClient::Integer(result, "topology") != m_Topology)
            return false;
        const auto pageTotal = EditorClient::Integer(result, "total"), next = EditorClient::Integer(result, "next");
        if ((first && pageTotal != total) || next < first || next > pageTotal || next - first > 128 ||
            (!(next > first) && first < pageTotal))
            return false;
        total = pageTotal;
        if (!first)
            model = *page;
        else {
            auto *items = static_cast<EditorField *>(nullptr);
            const EditorField *extra = nullptr;
            for (auto &f : model.fields)
                if (f.key == "items")
                    items = &f;
            for (const auto &f : page->fields)
                if (f.key == "items")
                    extra = &f;
            if (!items || !extra)
                return false;
            items->fields.insert(items->fields.end(), extra->fields.begin(), extra->fields.end());
        }
        first = next;
    } while (first < total);
    SchematicModel next;
    if (!ReadSchematicModel(model, next) || next.script.id != view.id || SchematicModelSize(next) != total) {
        return false;
    }
    if (next.script.snapshot) {
        auto image = std::make_shared<SchematicSnapshot>(*next.script.snapshot);
        const auto expected = size_t(image->width) * image->height;
        image->pixels.reserve(expected * 4);
        first = 0;
        do {
            const auto result = Client().Query("schematic.snapshot", arguments(first));
            const auto *pixels = EditorClient::Find(result, "pixels");
            const auto end = EditorClient::Integer(result, "next");
            if (result.status != EDITOR_OK || !pixels || EditorClient::Integer(result, "total") != expected ||
                end < first || end > expected || end - first != pixels->integers.size() ||
                (end == first && first < expected))
                return false;
            for (auto pixel : pixels->integers) {
                if (pixel > UINT32_MAX)
                    return false;
                for (unsigned i = 0; i < 4; ++i)
                    image->pixels.push_back(uint8_t(pixel >> (i * 8)));
            }
            first = end;
        } while (first < expected);
        next.script.snapshot = std::move(image);
    }
    std::map<ElementRef, ElementAddress> positions;
    for (const auto &field : model.fields)
        if (field.key == "items")
            for (const auto &item : field.fields)
                for (const auto &f : item.fields)
                    if (f.key == "address") {
                        ElementAddress a;
                        if (!ReadSchematicPosition(f, a))
                            return false;
                        auto key = a.element;
                        if (key.kind == ElementKind::Link)
                            key.object = 0;
                        positions[key] = a;
                    }
    next.MeasureText([font](const std::string &text) {
        return font ? font->CalcTextSizeA(WORLD_FONT_SIZE, FLT_MAX, 0, text.c_str()).x : 0;
    });
    const auto content = next.script.contentHeight;
    const bool preview = EditorClient::Integer(meta, "preview");
    const bool changed = !preview && view.revision != ~0ull && view.content > 0 && view.content != content;
    if (!preview)
        view.content = content;
    const auto stored = EditorClient::Find(meta, "height");
    if (changed && next.script.open && stored && stored->real > 0 && !EditorClient::Integer(meta, "readonly") &&
        (content > stored->real ||
         (content < stored->real && m_State.preferences.Bool("Schematic\\Auto Resize Scripts"))))
        EditorClient(Client().Host(), 0)
            .Command("schematic.fit-content",
                     {ObjectArgument("target", view.identity), IntegerArgument("revision", revision),
                      IntegerArgument("topology", m_Topology),
                      EditorValue{EDITOR_TEXT("height"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, content}});

    view.positions = std::move(positions);
    view.model = std::move(next);
    view.rootIsScript = view.model.script.isScript;
    view.revision = revision;
    view.topology = m_Topology;
    view.viewRevision = viewRevision;
    view.sourceVersion = source;
    view.refresh = m_ModelRefresh;
    return true;
}

void SchematicCanvas::SyncViews(ImFont *font) {
    if (!Client().Scope().document.value) {
        m_Views.clear();
        m_ViewInitialized = false;
        return;
    }
    RefreshStack();
    uint32_t pressScript = 0, menuScript = 0;
    if (m_Doc.press.view) {
        if (ValidPosition(m_PressAddress))
            pressScript = m_PressAddress.script.object;
        else {
            CancelInteraction();
            m_Doc.press = {};
        }
    }
    if (m_Doc.menuHit.view) {
        if (ValidPosition(m_MenuAddress))
            menuScript = m_MenuAddress.script.object;
        else {
            m_Doc.menuHit = {};
            m_MenuStale = true;
        }
    }
    std::vector<ScriptView> next;
    for (auto target : m_VisibleScripts) {
        const EditorField *row = nullptr;
        for (const auto &r : m_ScriptRows)
            for (const auto &f : r.fields)
                if (f.key == "target" && f.object == target) {
                    row = &r;
                    break;
                }
        if (!row)
            continue;
        ScriptView view;
        if (auto *existing = ViewFor(target.object); existing && existing->identity == target)
            view = std::move(*existing);
        view.id = target.object;
        view.identity = target;
        if (!ReadModel(view, *row, font)) {
            if (view.model.script.id)
                next.push_back(std::move(view));
            continue;
        }
        next.push_back(std::move(view));
    }
    m_Views = std::move(next);
    m_Doc.press.view = pressScript ? ViewFor(pressScript) : nullptr;
    m_Doc.menuHit.view = menuScript ? ViewFor(menuScript) : nullptr;
    if (m_ViewInitialized)
        m_PendingZoom.erase(std::remove_if(m_PendingZoom.begin(), m_PendingZoom.end(),
                                           [this](const auto &zoom) {
                                               auto *v = ViewFor(zoom.first);
                                               if (!v)
                                                   return false;
                                               v->scale = zoom.second;
                                               return true;
                                           }),
                            m_PendingZoom.end());
    UpdateTransforms();
}

SchematicCanvas::Hit SchematicCanvas::HitAt(const ImVec2 &mouse) {
    Hit hit;
    hit.view = ViewAtScreen(mouse);
    if (!hit.view) return hit;
    const bool header = mouse.x < hit.view->bodyMin.x;
    const ViewTransform &transform = header ? hit.view->frame : hit.view->transform;
    const Point world = transform.ToWorld(mouse);
    hit.local = Point(world.h, world.v - hit.view->offsetY);
    hit.onHeader = header;
    if (header) {
        hit.ref = hit.view->model.script.ref;
        return hit;
    }
    // Start is attached to the header (frame coordinates).
    const StartItem &start = hit.view->model.start;
    if (start.present) {
        const Point world = hit.view->frame.ToWorld(mouse);
        const Point framed(world.h, world.v - hit.view->offsetY);
        const Rect area(start.bounds.hPos - 2, start.bounds.vPos - 2, start.bounds.hSize + 4, start.bounds.vSize + 4);
        if (area.Contains(framed)) {
            hit.local = framed;
            hit.onStart = true;
            hit.ref = start.ref;
            return hit;
        }
    }
    hit.ref = PickElement(hit.view->model, hit.local, 3.0f / transform.zoom);
    if (hit.ref.kind == ElementKind::Start) hit.ref = ElementRef(); // only where it is drawn
    // The body never picks the header column (it may lie under scrolled content).
    if (hit.ref.kind == ElementKind::Script) hit.ref = ElementRef();
    if (!m_Navigation.options.showLocalParams && hit.ref.kind == ElementKind::LocalParam) {
        for (const auto &param : hit.view->model.params)
            if (param.ref == hit.ref && !IsParamShown(param, false)) hit.ref = ElementRef();
    } else if (!m_Navigation.options.showLocalParams && hit.ref.kind == ElementKind::Link) {
        for (const auto &link : hit.view->model.links)
            if (link.ref == hit.ref && !IsLinkShown(link, false)) hit.ref = ElementRef();
    }
    return hit;
}

// Dev.exe sub_51E0C0 (message 0x607): the first of the class among the
// object a parameter under the mouse holds (assumed: sub_4D8D00 of a
// parameter element), the element's own object, its Script, the Script's
// owner.
EditorStatus SchematicCanvas::PickObjectsAt(const ImVec2 &mouse, EditorSink sink) {
    const auto empty = [&] {
        const EditorValue objects{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST};
        return sink.write ? sink.write(sink.data, &objects, 1) : EDITOR_INVALID;
    };
    if (!Client().Scope().document.value || ImGui::GetFrameCount() - m_PickFrame > 1 || mouse.x < m_PickMin.x ||
        mouse.y < m_PickMin.y || mouse.x >= m_PickMax.x || mouse.y >= m_PickMax.y)
        return empty();
    const Hit hit = HitAt(mouse);
    if (!hit.view)
        return empty();
    std::vector<EditorField> fields{{"target", EDITOR_OBJECT, hit.view->identity}};
    if (hit.ref.IsValid()) {
        const auto address = CapturePosition(hit.view->id, hit.ref);
        if (!address)
            return EDITOR_STALE;
        fields.push_back(SchematicPosition(address));
    }
    const auto arguments = BorrowEditorFields(fields);
    const auto result =
        Client().Query("schematic.pick-objects", {arguments.data(), arguments.data() + arguments.size()});
    if (result.status != EDITOR_OK)
        return result.status;
    const auto values = BorrowEditorFields(result.fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
}

void SchematicCanvas::ResetView() {
    // 100%, the Script stack at the top-left corner of the view (Dev: the
    // Script column touches the view's left edge). Every Script back to 1.
    m_View.zoom = 1.0f;
    m_ScrollX = 0.0f;
    for (auto &view : m_Views) view.scale = 1.0f;
    UpdateTransforms();
    float minX = 0.0f, minY = 0.0f;
    for (size_t i = 0; i < m_Views.size(); ++i) {
        const Rect &bounds = m_Views[i].model.script.bounds;
        minX = i ? std::min(minX, bounds.hPos) : bounds.hPos;
        minY = i ? std::min(minY, bounds.vPos + m_Views[i].offsetY) : bounds.vPos + m_Views[i].offsetY;
    }
    m_View.panX = minX;
    m_View.panY = minY;
    UpdateTransforms();
}

void SchematicCanvas::FitAll() {
    if (m_Views.empty() || m_CanvasSize.x < 10 || m_CanvasSize.y < 10) {
        ResetView();
        return;
    }
    m_ScrollX = 0.0f;
    for (auto &view : m_Views) view.scale = 1.0f;
    UpdateTransforms();
    float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
    for (const auto &view : m_Views) {
        const Rect &bounds = view.model.script.bounds;
        minX = std::min(minX, bounds.hPos);
        maxX = std::max(maxX, bounds.Right());
        minY = std::min(minY, bounds.vPos + view.offsetY);
        maxY = std::max(maxY, bounds.Bottom() + view.offsetY);
    }
    const float width = std::max(1.0f, maxX - minX + 40.0f), height = std::max(1.0f, maxY - minY + 40.0f);
    m_View.zoom = std::max(MIN_ZOOM, std::min(1.5f, std::min(m_CanvasSize.x / width, m_CanvasSize.y / height)));
    m_View.panX = minX - 20.0f;
    m_View.panY = minY; // no empty space above the first Script
    UpdateTransforms();
}

void SchematicCanvas::ApplyFocus(const ImVec2 &size) {
    auto &focus = m_Navigation.focus;
    if (!focus.pending)
        return;
    if (!ValidPosition(focus.address)) {
        focus = {};
        return;
    }
    const auto script = focus.address.script.object;
    auto *view = ViewFor(script);
    if (!view)
        return;
    focus.pending = false;
    Point center;
    ElementRef shown = focus.address.element;
    bool found = FindCenter(view->model, shown, center);
    if (!found) {
        for (auto parent : m_FocusParents)
            if (const auto *behavior = view->model.FindBehavior(parent.object)) {
                center = Center(behavior->box);
                shown = behavior->ref;
                found = true;
                break;
            }
        if (!found) {
            center = Center(view->model.script.header);
            shown = view->model.script.ref;
        }
        m_State.Log(LogLevel::Info, "The element is inside a folded graph or a closed Script; showing its container");
    }
    if (shown.kind == ElementKind::Script) {
        const auto &bounds = view->model.script.bounds;
        const auto at = view->frame.ToScreen(bounds.hPos, bounds.vPos + view->offsetY);
        m_View.panX += (at.x - m_View.origin.x) / m_View.zoom;
        PanBy(0, m_View.origin.y - at.y);
    } else {
        const auto top = view->frame.ToScreen(0, view->model.script.bounds.vPos + view->offsetY);
        PanBy(0, m_View.origin.y - top.y);
        if (auto *again = ViewFor(script)) {
            const auto at = again->transform.ToScreen(center.h, center.v + again->offsetY);
            PanBy((again->bodyMin.x + m_View.origin.x + size.x) * .5f - at.x, 0);
        }
    }
    UpdateTransforms();
    m_Navigation.selection.Set(script, shown);
}

void SchematicCanvas::ToggleFold(CK_ID script, CK_ID behavior) {
    ExecuteGraph("schematic.toggle-fold", script, behavior, "object");
}

void SchematicCanvas::ToggleScriptOpen(CK_ID script) {
    const auto refs = ScriptReferences({script});
    if (refs.empty())
        return;
    if (ScriptOpen(script))
        m_Navigation.closedScripts.insert(refs.front());
    else
        m_Navigation.closedScripts.erase(refs.front());
    if (auto *view = ViewFor(script))
        view->viewRevision = ~0ull;
}

void SchematicCanvas::AddComment(CK_ID script, const Point &local) {
    auto *view = ViewFor(script);
    const auto graph = view ? GraphAt(*view, local) : 0;
    if (!graph)
        return;
    auto fields = AuthoringArguments(script, graph, local);
    ExecuteAuthoring("schematic.add-comment", std::move(fields), GraphFollow::Comment);
}

void SchematicCanvas::DeleteSelection(bool graphsConfirmed) {
    Selection &selection = m_Navigation.selection;
    if (selection.Empty() || !Client().Scope().document.value)
        return;
    // Dev.exe sub_50E430: behavior graphs (behaviors that are no Building
    // Block) are deleted once "Confirm Graph(es) Deletion" is answered.
    if (!graphsConfirmed)
        for (const auto &item : selection.items)
            if (item.kind == ElementKind::Behavior)
                if (EditorClient::Integer(GraphState(selection.script, item.object), "graph")) {
                    m_Doc.openDeleteGraphs = true; return;
                }
    if (selection.OnlyHeaders()) {
        m_Doc.deleteScripts = ScriptReferences(selection.Scripts());
        m_Doc.openDeleteScript = true;
        return;
    }
    bool onlyPins = !selection.items.empty();
    for (const auto &item : selection.items) onlyPins &= item.kind == ElementKind::Pin;
    EditorField elements{"elements", EDITOR_RECORD};
    for (const auto &address : selection.Addresses())
        if (onlyPins || address.element.kind != ElementKind::Pin)
            elements.fields.push_back(SchematicPosition(address, std::to_string(elements.fields.size()).c_str()));
    ExecuteGraph("schematic.delete-elements", selection.script, 0, nullptr, {std::move(elements)}, GraphFollow::ClearSelection);
}

EditorResponse SchematicCanvas::GraphState(uint32_t script, uint32_t object) {
    ObjectRef target{};
    if (const auto *view = ViewFor(script)) target = view->identity;
    if (!target && m_Navigation.stackScript.object == script) target = m_Navigation.stackScript;
    if (!target)
        for (const auto &address : m_Navigation.selection.Addresses())
            if (address.script.object == script) { target = address.script; break; }
    if (!target)
        for (auto ref : m_AllScripts)
            if (ref.object == script) {
                target = ref;
                break;
            }
    if (!target) return {};
    return Client().Query("schematic.script-state", {ObjectArgument("target", target), IntegerArgument("id", object)});
}
std::vector<ObjectRef> SchematicCanvas::ScriptReferences(const std::vector<uint32_t> &scripts) const {
    std::vector<ObjectRef> result;
    for (const auto id : scripts) {
        ObjectRef ref{};
        for (const auto &view : m_Views) if (view.id == id) { ref = view.identity; break; }
        if (!ref && m_Navigation.stackScript.object == id) ref = m_Navigation.stackScript;
        if (!ref)
            for (auto candidate : m_AllScripts)
                if (candidate.object == id) {
                    ref = candidate;
                    break;
                }
        if (ref) result.push_back(ref);
    }
    return result;
}
void SchematicCanvas::Execute(const char *id, const std::vector<EditorValue> &arguments, GraphFollow follow,
                              ObjectRef script, ObjectRef owner) {
    const auto result = Client().Command(id, arguments);
    const GraphCommand command{result.operation, Client().Scope().document, script, owner, follow};
    if (result.status == EDITOR_QUEUED) m_GraphCommands.push_back(command);
    else CompleteGraph(result, command);
}
void SchematicCanvas::ExecuteGraph(const char *id, uint32_t script, uint32_t object, const char *objectKey,
                                    std::vector<EditorField> extra, GraphFollow follow) {
    const auto state = GraphState(script, object);
    if (state.status != EDITOR_OK || EditorClient::Integer(state, "readonly")) return;
    const auto *target = EditorClient::Find(state, "target"), *item = EditorClient::Find(state, "object");
    if (!target || target->type != EDITOR_OBJECT || (objectKey && (!item || !item->object))) return;
    std::vector<EditorField> fields{{"target", EDITOR_OBJECT, target->object},
        {"revision", EDITOR_INTEGER_VALUE, {}, {}, EditorClient::Integer(state, "revision")},
        {"topology", EDITOR_INTEGER_VALUE, {}, {}, EditorClient::Integer(state, "topology")}};
    if (objectKey) fields.push_back({objectKey, EDITOR_OBJECT, item->object});
    fields.insert(fields.end(), std::make_move_iterator(extra.begin()), std::make_move_iterator(extra.end()));
    const auto values = BorrowEditorFields(fields);
    Execute(id, std::vector<EditorValue>(values.data(), values.data() + values.size()), follow,
        target->object, objectKey && item ? item->object : ObjectRef{});
}
void SchematicCanvas::CompleteGraph(const EditorResponse &result, const GraphCommand &command) {
    if (command.follow == GraphFollow::Geometry && result.documentRevision) {
        const bool succeeded = result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE;
        const bool keep =
            succeeded ? (result.status == EDITOR_NO_CHANGE || EditorClient::Integer(result, "selection-unchanged"))
                      : result.documentRevision == command.documentRevision;
        if (keep) {
            EditorField elements{"elements", EDITOR_RECORD};
            for (const auto &address : command.selection)
                elements.fields.push_back(SchematicPosition(address, std::to_string(elements.fields.size()).c_str()));
            const std::vector<EditorField> fields{
                elements, {"document-revision", EDITOR_INTEGER_VALUE, {}, {}, result.documentRevision}};
            const auto values = BorrowEditorFields(fields);
            const auto positions =
                Client().Query("schematic.positions", {values.data(), values.data() + values.size()});
            if (positions.status == EDITOR_OK) {
                std::vector<ElementAddress> selection;
                if (const auto *rows = EditorClient::Find(positions, "elements")) {
                    const auto owned = BorrowEditorFields(rows->fields);
                    for (const auto &value : owned.values) {
                        ElementAddress address;
                        if (ReadSchematicPosition(value, address))
                            selection.push_back(address);
                    }
                }
                m_Navigation.selection.Assign(selection);
            }
        } else if (succeeded)
            m_Navigation.selection.Clear();
    }
    const bool file = command.follow == GraphFollow::FileCreated || command.follow == GraphFollow::FileReplaced;
    if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE) {
        if (!file && !result.message.empty())
            m_State.Log(LogLevel::Warning, result.message);
        return;
    }
    if (!file && !result.message.empty())
        m_State.Log(LogLevel::Info, result.message);
    if (command.follow == GraphFollow::ClearSelection || command.follow == GraphFollow::FileReplaced)
        m_Navigation.selection.Clear();
    if (command.follow == GraphFollow::Block)
        if (const auto *prototypes = EditorClient::Find(result, "prototypes"))
            RememberBlocks(prototypes->integers);
    if (command.follow != GraphFollow::Created && command.follow != GraphFollow::Block &&
        command.follow != GraphFollow::Comment)
        return;
    EditorField elements{"elements", EDITOR_RECORD};
    if (const auto *rows = EditorClient::Find(result, "elements"))
        elements.fields = rows->fields;
    else if (const auto *address = EditorClient::Find(result, "address"))
        elements.fields.push_back(*address);
    if (!elements.fields.empty() && result.documentRevision) {
        const std::vector<EditorField> fields{
            elements, {"document-revision", EDITOR_INTEGER_VALUE, {}, {}, result.documentRevision}};
        const auto values = BorrowEditorFields(fields);
        const auto positions = Client().Query("schematic.positions", {values.data(), values.data() + values.size()});
        if (positions.status != EDITOR_OK)
            return;
        std::vector<ElementAddress> selection;
        if (const auto *rows = EditorClient::Find(positions, "elements")) {
            const auto values = BorrowEditorFields(rows->fields);
            for (const auto &value : values.values) {
                ElementAddress address;
                if (ReadSchematicPosition(value, address))
                    selection.push_back(address);
            }
        }
        m_Navigation.selection.Assign(selection);
        if (command.follow == GraphFollow::Comment && selection.size() == 1) {
            m_Doc.commentAddress = selection.front();
            m_Doc.openCommentEditor = true;
        }
    } else if (command.follow == GraphFollow::Created)
        if (const auto *created = EditorClient::Find(result, "created"); created && created->type == EDITOR_OBJECT &&
            created->object && created->object.document == command.document &&
            Client().Query("object.details", {ObjectArgument("target", created->object)}).status == EDITOR_OK)
            m_Navigation.selection.Set(command.script.object, {ElementKind::Behavior, command.owner.object, created->object.object});
}
void SchematicCanvas::PollGraphCommands() {
    auto pending = std::move(m_GraphCommands); m_GraphCommands.clear();
    for (const auto &command : pending) {
        if (command.document != Client().Scope().document) continue;
        const auto result = Client().Completion(command.operation);
        if (result.status == EDITOR_QUEUED) m_GraphCommands.push_back(command);
        else CompleteGraph(result, command);
    }
}

CK_ID SchematicCanvas::GraphAt(ScriptView &view, const Point &local) const {
    if (!view.model.script.open) return 0;
    const CK_ID graph = view.model.GraphAt(local);
    return graph ? graph : view.id;
}

std::vector<EditorField> SchematicCanvas::AuthoringArguments(CK_ID script, CK_ID owner, const Point &at) {
    const auto state = GraphState(script, owner);
    const auto *target = EditorClient::Find(state, "target"), *object = EditorClient::Find(state, "object");
    if (state.status != EDITOR_OK || !target || !object || !object->object || EditorClient::Integer(state, "readonly"))
        return {};
    const auto *view = ViewFor(script);
    if (view && view->revision != ~0ull &&
        (view->revision != EditorClient::Integer(state, "revision") ||
         view->topology != EditorClient::Integer(state, "topology")))
        return {};
    EditorField point{"at", EDITOR_REAL_LIST};
    point.reals = {at.h, at.v};
    return {{"target", EDITOR_OBJECT, target->object},
            {"owner", EDITOR_OBJECT, object->object},
            {"revision", EDITOR_INTEGER_VALUE, {}, {}, EditorClient::Integer(state, "revision")},
            {"topology", EDITOR_INTEGER_VALUE, {}, {}, EditorClient::Integer(state, "topology")},
            std::move(point)};
}
void SchematicCanvas::ExecuteAuthoring(const char *id, std::vector<EditorField> fields, GraphFollow follow) {
    if (fields.empty())
        return;
    ObjectRef script{}, owner{};
    for (const auto &field : fields) {
        if (field.key == "target")
            script = field.object;
        if (field.key == "owner" || field.key == "graph")
            owner = field.object;
    }
    const auto values = BorrowEditorFields(fields);
    Execute(id, {values.data(), values.data() + values.size()}, follow, script, owner);
}
EditorResponse SchematicCanvas::ElementInfo(CK_ID script, const ElementRef &ref) {
    ElementAddress address;
    if (auto *view = ViewFor(script)) {
        Hit hit;
        hit.view = view;
        hit.ref = ref;
        RetainHit(hit, address);
    }
    if (!address)
        for (const auto &selected : m_Navigation.selection.Addresses())
            if (selected.script.object == script && selected.element == ref) {
                address = selected;
                break;
            }
    if (!address)
        return {};
    const std::vector<EditorField> fields{SchematicPosition(address)};
    const auto values = BorrowEditorFields(fields);
    return Client().Query("schematic.element-info", {values.data(), values.data() + values.size()});
}
EditorResponse SchematicCanvas::ObjectInfo(CK_ID script, CK_ID id) {
    const auto state = GraphState(script, id);
    const auto *object = EditorClient::Find(state, "object");
    return state.status == EDITOR_OK && object && object->object
               ? Client().Query("schematic.object-info", {ObjectArgument("target", object->object)})
               : EditorResponse{};
}
void SchematicCanvas::RememberBlocks(const std::vector<uint64_t> &prototypes) {
    EditorField values{"prototypes", EDITOR_INTEGER_LIST};
    values.integers = prototypes;
    const std::vector<EditorField> fields{values};
    const auto borrowed = BorrowEditorFields(fields);
    Client().Command("building-block.remember", {borrowed.data(), borrowed.data() + borrowed.size()});
}
std::vector<uint64_t> SchematicCanvas::RecentBlocks() {
    const auto result = Client().Query("building-block.recent");
    if (result.status == EDITOR_OK)
        if (const auto *prototypes = EditorClient::Find(result, "prototypes"))
            return prototypes->integers;
    return {};
}
void SchematicCanvas::LoadParameterTypes() {
    m_Types.clear();
    const auto result = Client().Query("schematic.parameter-types");
    if (result.status == EDITOR_OK)
        if (const auto *types = EditorClient::Find(result, "types"))
            for (const auto &row : types->fields) {
                EditorResponse item;
                item.fields = row.fields;
                m_Types.push_back({EditorClient::Integer(item, "type"), EditorClient::Text(item, "name")});
            }
}
void SchematicCanvas::OpenParameterEditor(ObjectRef script, ObjectRef object, unsigned mode) {
    if (!m_State.workspace || !script || !object)
        return;
    if (!m_ParameterDialog)
        m_ParameterDialog =
            std::make_unique<SchematicParameterDialog>(m_State.workspace->Application(), m_EditorHost, m_Instance);
    m_ParameterDialog->Open(script, object, mode);
    const auto message = m_ParameterDialog->TakeMessage();
    if (!message.empty())
        m_State.Log(LogLevel::Warning, message);
}

void SchematicCanvas::ToggleTargetOfSelection() {
    std::vector<ObjectRef> objects;
    for (const auto &address : m_Navigation.selection.Addresses())
        if ((address.element.kind == ElementKind::Behavior || address.element.kind == ElementKind::Script) && address.object)
            objects.push_back(address.object);
    if (objects.empty()) { m_State.Log(LogLevel::Info, "Select a behavior to add or remove its Target parameter (T)"); return; }
    EditorField targets{"objects", EDITOR_OBJECT_LIST}; targets.objects = std::move(objects);
    ExecuteGraph("schematic.toggle-targets", m_Navigation.selection.script, 0, nullptr, {std::move(targets)});
}

void SchematicCanvas::BeginRename() {
    const auto &selection = m_Navigation.selection;
    if (selection.items.size() != 1)
        return;
    const auto info = ElementInfo(selection.script, selection.items.front());
    const auto *object = EditorClient::Find(info, "object");
    if (info.status != EDITOR_OK || !object || !object->object || selection.items.front().kind == ElementKind::Shortcut)
        return;
    m_RenameTarget = object->object;
    m_Doc.openRename = true;
}

CK_ID SchematicCanvas::SelectedGraph() {
    const auto &selection = m_Navigation.selection;
    if (selection.items.size() != 1 || selection.items.front().kind != ElementKind::Behavior)
        return 0;
    const auto info = ElementInfo(selection.script, selection.items.front());
    return info.status == EDITOR_OK && EditorClient::Integer(info, "graph") ? selection.items.front().object : 0;
}

CK_ID SchematicCanvas::SelectedVariableBlock(PinKind kind) {
    const auto &selection = m_Navigation.selection;
    if (selection.items.size() != 1 || selection.items.front().kind != ElementKind::Behavior)
        return 0;
    const auto info = ElementInfo(selection.script, selection.items.front());
    return info.status == EDITOR_OK && !EditorClient::Integer(info, "graph") &&
                   (EditorClient::Integer(info, "pin-kinds") & (1ull << unsigned(kind)))
               ? selection.items.front().object
               : 0;
}

void SchematicCanvas::AddGraphIo(bool input) {
    const auto kind = input ? PinKind::BehaviorIn : PinKind::BehaviorOut;
    auto owner = SelectedVariableBlock(kind);
    if (!owner)
        owner = SelectedGraph();
    if (!owner) {
        m_State.Log(LogLevel::Info, "Select a behavior graph to add a behavior input (I) or output (O)");
        return;
    }
    auto fields = AuthoringArguments(m_Navigation.selection.script, owner);
    if (fields.empty())
        return;
    fields.pop_back();
    fields.push_back({"kind", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(kind)});
    ExecuteAuthoring("schematic.add-pin", std::move(fields));
}

void SchematicCanvas::OpenGraphParameterPicker(bool input) {
    CK_ID owner = SelectedGraph(), script = m_Navigation.selection.script;
    if (!owner) owner = SelectedVariableBlock(input ? PinKind::ParamIn : PinKind::ParamOut);
    if (!owner && m_Doc.hoveredScript)
        owner = script = m_Doc.hoveredScript;
    if (!owner) {
        m_State.Log(LogLevel::Info, "Select a behavior graph, or point at a Script, to add a parameter");
        return;
    }
    m_Doc.creation = AuthoringArguments(script, owner);
    if (m_Doc.creation.empty())
        return;
    m_Doc.creation.pop_back();
    m_TypePurpose = input ? TypePurpose::GraphInput : TypePurpose::GraphOutput;
    LoadParameterTypes();
    m_PickerFilter.clear();
    m_LocalName = input ? "pIn" : "pOut";
    m_TypeIndex = -1;
    m_Doc.openLocalPicker = true;
}

void SchematicCanvas::CreateGraphAt(CK_ID script, const Point &local) {
    auto *view = ViewFor(script); const auto graph = view ? GraphAt(*view, local) : 0;
    if (!graph) return;
    EditorField point{"at", EDITOR_REAL_LIST}; point.reals = {local.h, local.v};
    ExecuteGraph("schematic.create-graph", script, graph, "owner", {std::move(point)}, GraphFollow::Created);
}

void SchematicCanvas::OpenLocalParameterPicker(CK_ID script, const Point &local) {
    auto *view = ViewFor(script);
    const auto graph = view ? GraphAt(*view, local) : 0;
    m_Doc.creation = graph ? AuthoringArguments(script, graph, local) : std::vector<EditorField>{};
    if (m_Doc.creation.empty()) {
        m_State.Log(LogLevel::Info, "Open the Script to add a local parameter");
        return;
    }
    m_TypePurpose = TypePurpose::Local;
    LoadParameterTypes();
    m_PickerFilter.clear();
    m_LocalName = "Local";
    m_TypeIndex = -1;
    m_Doc.openLocalPicker = true;
}

void SchematicCanvas::OpenOperationPicker(CK_ID script, const Point &local) {
    auto *view = ViewFor(script);
    const auto graph = view ? GraphAt(*view, local) : 0;
    m_Doc.creation = graph ? AuthoringArguments(script, graph, local) : std::vector<EditorField>{};
    if (m_Doc.creation.empty()) {
        m_State.Log(LogLevel::Info, "Open the Script to add a parameter operation");
        return;
    }
    m_Doc.modifyOperation = {};
    m_Doc.openOperationPicker = true;
}

bool SchematicCanvas::EndpointOf(const Hit &hit, LinkEndpoint &endpoint, bool &behaviorLink, Point &position) const {
    if (!hit.view) return false;
    const SchematicModel &model = hit.view->model;
    switch (hit.ref.kind) {
    case ElementKind::Pin:
        for (const auto &pin : model.pins) {
            if (pin.ref != hit.ref) continue;
            endpoint = pin.endpoint;
            behaviorLink = pin.kind == PinKind::BehaviorIn || pin.kind == PinKind::BehaviorOut;
            position = pin.center;
            return true;
        }
        return false;
    case ElementKind::Start: {
        if (!model.start.endpoint.id)
            return false;
        endpoint = model.start.endpoint;
        behaviorLink = true;
        position = model.start.center;
        return true;
    }
    case ElementKind::LocalParam:
    case ElementKind::Shortcut:
        for (const auto &param : model.params) {
            if (param.ref != hit.ref) continue;
            endpoint = LinkEndpoint(param.owner, param.slot,
                                    param.shortcut ? ENDPOINT_POUT_SHORTCUT : ENDPOINT_PLOCAL);
            behaviorLink = false;
            position = param.center;
            return true;
        }
        return false;
    default:
        return false;
    }
}

ElementAddress SchematicCanvas::EndpointAddress(CK_ID script, const LinkEndpoint &endpoint) {
    auto *view = ViewFor(script);
    if (!view)
        return {};
    const auto same = [&](const LinkEndpoint &other) {
        return endpoint.id == other.id && endpoint.index == other.index && endpoint.type == other.type;
    };
    Hit hit;
    hit.view = view;
    if (same(view->model.start.endpoint))
        hit.ref = view->model.start.ref;
    for (const auto &pin : view->model.pins)
        if (same(pin.endpoint)) {
            hit.ref = pin.ref;
            break;
        }
    if (!hit.ref.IsValid())
        for (const auto &param : view->model.params)
            if (same(
                    LinkEndpoint(param.owner, param.slot, param.shortcut ? ENDPOINT_POUT_SHORTCUT : ENDPOINT_PLOCAL))) {
                hit.ref = param.ref;
                break;
            }
    ElementAddress address;
    if (hit.ref.IsValid())
        RetainHit(hit, address);
    return address;
}

void SchematicCanvas::RetainLinkSources() {
    m_Doc.linkSources.clear();
    m_Doc.linkAddress = {};
    m_Doc.linkFixedAddress = {};
    if (!m_Doc.press.view)
        return;
    if (m_Doc.relinkLink) {
        auto hit = m_Doc.press;
        hit.ref = m_Doc.relinkRef;
        RetainHit(hit, m_Doc.linkAddress);
        m_Doc.linkFixedAddress = EndpointAddress(hit.view->id, m_Doc.linkFrom);
    } else {
        const bool group = IsSelected(m_Doc.linkScript, m_Doc.press.ref);
        const auto items = group ? m_Navigation.selection.items : std::vector<ElementRef>{m_Doc.press.ref};
        for (const auto &item : items) {
            if (item.kind != m_Doc.press.ref.kind || (item.kind == ElementKind::Pin && item.pin != m_Doc.press.ref.pin))
                continue;
            auto hit = m_Doc.press;
            hit.ref = item;
            ElementAddress address;
            RetainHit(hit, address);
            if (address)
                m_Doc.linkSources.push_back(address);
        }
    }
}

void SchematicCanvas::EndLink(const Hit &hit) {
    m_Tool = Tool::Idle;
    const bool relink = m_Doc.relinkLink != 0;
    m_Doc.relinkLink = 0;
    if (!hit.view || hit.view->id != m_Doc.linkScript || !m_PressAddress)
        return;
    ElementAddress destination;
    RetainHit(hit, destination);
    if (!destination)
        return;
    m_Doc.connection = {
        {"target", EDITOR_OBJECT, m_PressAddress.script},
        {"revision", EDITOR_INTEGER_VALUE, {}, {}, m_PressAddress.revision},
        {"topology", EDITOR_INTEGER_VALUE, {}, {}, m_PressAddress.topology},
        SchematicPosition(m_PressAddress, "from"),
        SchematicPosition(destination, "to"),
        {"alt", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(ImGui::GetIO().KeyAlt)},
        {"delay", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(m_State.preferences.defaultLinkDelay)},
        {"auto-loop", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(m_State.preferences.autoLoopLinkDelay)},
        {"route",
         EDITOR_INTEGER_VALUE,
         {},
         {},
         uint64_t(m_LinkPoints.empty() && m_State.preferences.searchBestPath != ImGui::GetIO().KeyCtrl)}};
    EditorField selected{"selected", EDITOR_RECORD}, points{"points", EDITOR_REAL_LIST};
    for (const auto &address : m_Doc.linkSources)
        selected.fields.push_back(SchematicPosition(address, std::to_string(selected.fields.size()).c_str()));
    for (const auto &point : m_LinkPoints) {
        points.reals.push_back(point.h);
        points.reals.push_back(point.v);
    }
    m_LinkPoints.clear();
    m_Doc.connection.push_back(std::move(selected));
    m_Doc.connection.push_back(std::move(points));
    if (relink) {
        if (!m_Doc.linkAddress || !m_Doc.linkFixedAddress)
            return;
        for (auto &field : m_Doc.connection)
            if (field.key == "from")
                field = SchematicPosition(m_Doc.linkFixedAddress, "from");
        m_Doc.connection.push_back(SchematicPosition(m_Doc.linkAddress, "link"));
        m_Doc.connection.push_back({"reverse", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(m_Doc.linkReverse)});
    }
    ContinueConnection(relink);
}

void SchematicCanvas::ContinueConnection(bool clearSelection) {
    const auto values = BorrowEditorFields(m_Doc.connection);
    const auto state = Client().Query("schematic.connection", {values.data(), values.data() + values.size()});
    if (state.status != EDITOR_OK) {
        if (state.status == EDITOR_REFUSED)
            m_State.Log(LogLevel::Warning, "Link: these endpoints cannot be connected");
        return;
    }
    if (EditorClient::Integer(state, "replace")) {
        m_ReplaceInputName = EditorClient::Text(state, "input-name");
        m_OpenReplaceSource = true;
        return;
    }
    const auto *choices = EditorClient::Find(state, "choices");
    if (choices && choices->fields.size() > 1) {
        m_Doc.choices = choices->fields;
        m_ChoiceMode = OperationChoice::NewLink;
        m_ChoicePick = 0;
        m_Doc.openOperationChoice = true;
        return;
    }
    Execute("schematic.connect", {values.data(), values.data() + values.size()},
            clearSelection ? GraphFollow::ClearSelection : GraphFollow::None);
}

void SchematicCanvas::ReplaceSourceDialog() {
    const char *id = "Link###ReplaceSource";
    if (m_OpenReplaceSource) {
        m_OpenReplaceSource = false;
        ImGui::OpenPopup(id);
    }
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("The parameter input <%s> is already linked.", m_ReplaceInputName.c_str());
    ImGui::TextUnformatted("Delete the previous link?");
    if (ImGui::Button("Delete Link", ImVec2(110, 0))) {
        ImGui::CloseCurrentPopup();
        m_Doc.connection.push_back({"replace", EDITOR_INTEGER_VALUE, {}, {}, 1});
        const auto values = BorrowEditorFields(m_Doc.connection);
        Execute("schematic.connect", {values.data(), values.data() + values.size()});
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void SchematicCanvas::AcceptBuildingBlockDrop() {
    if (!ImGui::BeginDragDropTarget())
        return;
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(EDITOR_BUILDING_BLOCK_PAYLOAD)) {
        EditorBuildingBlock source{};
        std::vector<EditorBuildingBlock> blocks;
        if (ReadBuildingBlockDrag(payload->Data, size_t(payload->DataSize), source, blocks)) {
            const CKGUID guid(source.first, source.second);
            const Hit hit = HitAt(ImGui::GetIO().MousePos);
            const CK_ID graph = hit.view ? GraphAt(*hit.view, hit.local) : 0;
            if (IsBuildingBlockAt(hit)) {
                // Dev: a Building Block dropped on another replaces it.
                const auto info = ElementInfo(hit.view->id, hit.ref);
                if (info.status == EDITOR_OK)
                    if (const auto *object = EditorClient::Find(info, "object"))
                        OpenReplaceBlock(hit.view->identity, object->object, guid);
            } else if (!graph) {
                m_State.Log(LogLevel::Info, "Drop Building Blocks into an open Script or an expanded graph");
            } else {
                auto fields = AuthoringArguments(hit.view->id, graph, hit.local);
                if (!fields.empty()) {
                    EditorField prototypes{"prototypes", EDITOR_INTEGER_LIST};
                    for (const auto &block : blocks)
                        prototypes.integers.push_back((uint64_t(block.first) << 32) | block.second);
                    fields.push_back(std::move(prototypes));
                    ExecuteAuthoring("schematic.insert-block", std::move(fields), GraphFollow::Block);
                }
            }
        }
    }
    // A Data Resources file: as a file dropped from Explorer (behavior graph
    // files on graphs or bodies, images on Script headers).
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(EDITOR_RESOURCE_FILE_PAYLOAD)) {
        uint32_t source = 0;
        std::vector<ResourceFile> files;
        if (ReadResourceDrag(payload->Data, size_t(payload->DataSize), source, files))
        for (const ResourceFile &each : files)
            if (!DropFile(Utf8ToWide(each.path), each.path, ImGui::GetIO().MousePos))
                m_State.Log(LogLevel::Info, "Drop behavior graph files on a graph or a Script, images on a Script header");
    }
    // A 3D Layout snapshot (Dev.exe sub_53BF70, "Bitmap"): the Script's snapshot.
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(SNAPSHOT_PAYLOAD)) {
        int slot = -1;
        memcpy(&slot, payload->Data, sizeof(slot));
        const Hit hit = HitAt(ImGui::GetIO().MousePos);
        if (!hit.view || !Client().Scope().document.value || !m_State.snapshots.Has(slot)) {
            m_State.Log(LogLevel::Info, "Drop snapshots on a Script");
        } else {
            // Scaled into the header's snapshot place (48 x 36) as Load Snapshot does: 47 x 36.
            const int height = 36;
            const int width = std::min(48, (SnapshotBuffer::WIDTH * height + SnapshotBuffer::HEIGHT / 2) / SnapshotBuffer::HEIGHT);
            const std::vector<unsigned char> pixels = ShrinkImage(m_State.snapshots.Pixels(slot).data(), SnapshotBuffer::WIDTH,
                                                                  SnapshotBuffer::HEIGHT, SnapshotBuffer::WIDTH * 4, width, height);
            SetSnapshotPixels(hit.view->identity, pixels, width, height);
        }
    }
    ImGui::EndDragDropTarget();
}

std::vector<EditorField> SchematicCanvas::SelectedPositions() {
    const auto &selection = m_Navigation.selection;
    if (selection.Empty())
        return {};
    EditorField elements{"elements", EDITOR_RECORD};
    const ElementAddress *first = nullptr;
    for (const auto &address : selection.Addresses()) {
        if (address.script.object != selection.script) continue;
        if (!address)
            return {};
        if (!first) first = &address;
        elements.fields.push_back(SchematicPosition(address, std::to_string(elements.fields.size()).c_str()));
    }
    if (!first || elements.fields.size() != selection.items.size()) return {};
    const auto state = Client().Query("schematic.script-state", {ObjectArgument("target", first->script)});
    if (state.status != EDITOR_OK) return {};
    return {{"target", EDITOR_OBJECT, first->script},
            {"revision", EDITOR_INTEGER_VALUE, {}, {}, EditorClient::Integer(state, "revision")},
            {"topology", EDITOR_INTEGER_VALUE, {}, {}, EditorClient::Integer(state, "topology")},
            std::move(elements)};
}

void SchematicCanvas::Copy() {
    const auto fields = SelectedPositions();
    if (fields.empty())
        return;
    const auto values = BorrowEditorFields(fields);
    Execute("schematic.copy", {values.data(), values.data() + values.size()});
}

void SchematicCanvas::PasteShortcuts() { PasteClipboard(true); }

void SchematicCanvas::GroupSelection() {
    EditorField objects{"objects", EDITOR_OBJECT_LIST};
    for (const auto &item : m_Navigation.selection.items)
        if (item.kind == ElementKind::Behavior) {
            const auto state = GraphState(m_Navigation.selection.script, item.object);
            if (state.status != EDITOR_OK)
                return;
            const auto *object = EditorClient::Find(state, "object");
            if (!object || !object->object)
                return;
            objects.objects.push_back(object->object);
        }
    if (objects.objects.empty()) {
        if (m_Doc.mouseScript)
            CreateGraphAt(m_Doc.mouseScript, m_Doc.mouseLocal);
        return;
    }
    ExecuteGraph("schematic.group", m_Navigation.selection.script, 0, nullptr, {std::move(objects)},
                 GraphFollow::Created);
}

void SchematicCanvas::UngroupSelection() {
    EditorField objects{"objects", EDITOR_OBJECT_LIST};
    for (const auto &item : m_Navigation.selection.items)
        if (item.kind == ElementKind::Behavior) {
            const auto state = GraphState(m_Navigation.selection.script, item.object);
            if (state.status != EDITOR_OK)
                return;
            const auto *object = EditorClient::Find(state, "object");
            if (!object || !object->object)
                return;
            objects.objects.push_back(object->object);
        }
    if (objects.objects.empty()) {
        m_State.Log(LogLevel::Info, "Shift+Del: select behavior graphs to remove while keeping their contents");
        return;
    }
    ExecuteGraph("schematic.ungroup", m_Navigation.selection.script, 0, nullptr, {std::move(objects)},
                 GraphFollow::ClearSelection);
}

void SchematicCanvas::Cut() {
    const auto fields = SelectedPositions();
    if (fields.empty())
        return;
    const auto values = BorrowEditorFields(fields);
    Execute("schematic.cut", {values.data(), values.data() + values.size()}, GraphFollow::ClearSelection);
}

void SchematicCanvas::Paste() { PasteClipboard(false); }
void SchematicCanvas::PasteClipboard(bool shortcuts) {
    const auto clipboard = Client().Query("schematic.clipboard");
    if (clipboard.status != EDITOR_OK || !EditorClient::Integer(clipboard, "available"))
        return;
    CK_ID script = m_Doc.mouseScript;
    Point at = m_Doc.mouseLocal;
    if (!script && !m_Views.empty() && !shortcuts &&
        (EditorClient::Integer(clipboard, "behaviors") || EditorClient::Integer(clipboard, "comments"))) {
        script = m_Views.front().id;
        at = Point(m_Views.front().model.script.bounds.hPos + 200, m_Views.front().model.script.bounds.vPos + 40);
    }
    auto *view = ViewFor(script);
    if (!view || !GraphAt(*view, at))
        return;
    EditorField point{"at", EDITOR_REAL_LIST};
    point.reals = {at.h, at.v};
    ExecuteGraph(
        shortcuts ? "schematic.paste-shortcuts" : "schematic.paste", script, 0, nullptr,
        {point, {"clipboard-revision", EDITOR_INTEGER_VALUE, {}, {}, EditorClient::Integer(clipboard, "revision")}},
        GraphFollow::Created);
}

void SchematicCanvas::Duplicate() {
    const auto fields = SelectedPositions();
    if (fields.empty())
        return;
    auto *view = ViewFor(m_Navigation.selection.script);
    float left = FLT_MAX, top = FLT_MAX;
    for (const auto &item : m_Navigation.selection.items)
        if (const auto *behavior =
                item.kind == ElementKind::Behavior ? view->model.FindBehavior(item.object) : nullptr) {
            left = std::min(left, behavior->box.hPos);
            top = std::min(top, behavior->box.vPos);
        }
    if (left == FLT_MAX) return;
    auto args = fields;
    EditorField at{"at", EDITOR_REAL_LIST};
    at.reals = {left + 40, top + 40};
    args.push_back(std::move(at));
    m_Doc.mouseScript = view->id;
    m_Doc.mouseLocal = Point(left + 40, top + 40);
    const auto values = BorrowEditorFields(args);
    Execute("schematic.duplicate", {values.data(), values.data() + values.size()}, GraphFollow::Created,
            view->identity);
}

void SchematicCanvas::Refresh() {
    Client().Command("schematic.refresh");
    Invalidate();
    m_State.Log(LogLevel::Info, "Schematic refreshed");
}

// Dev's Edit Parameter (Dev.exe sub_523480 opens dialog 354): the name and
// the type (Dev's default; not of <This>), the value of a local or of an
// input's value holder; other values are only shown.
void SchematicCanvas::OpenValueDialog(CK_ID script, ObjectRef parameter) {
    const auto scripts = ScriptReferences({script});
    if (!scripts.empty())
        OpenParameterEditor(scripts.front(), parameter, 0);
}

void SchematicCanvas::OpenParametersDialog() {
    const auto &selection = m_Navigation.selection;
    if (selection.items.size() != 1)
        return;
    const auto &item = selection.items.front();
    if (item.kind != ElementKind::Behavior && item.kind != ElementKind::Operation)
        return;
    const auto info = ElementInfo(selection.script, item);
    const auto *object = EditorClient::Find(info, "object");
    if (info.status != EDITOR_OK || !object)
        return;
    const auto scripts = ScriptReferences({selection.script});
    if (!scripts.empty())
        OpenParameterEditor(scripts.front(), object->object, 1);
}

void SchematicCanvas::OpenParametersOf(CK_ID script, CK_ID object) {
    const auto info = ObjectInfo(script, object);
    if (info.status != EDITOR_OK)
        return;
    m_Navigation.selection.Set(
        script, {EditorClient::Integer(info, "operation") ? ElementKind::Operation : ElementKind::Behavior, 0, object});
    const auto scripts = ScriptReferences({script});
    const auto *target = EditorClient::Find(info, "object");
    if (!scripts.empty() && target)
        OpenParameterEditor(scripts.front(), target->object, 1);
}

void SchematicCanvas::OpenPriorityDialog() {
    const auto &selection = m_Navigation.selection;
    m_PriorityTargets.clear();
    if (selection.OnlyHeaders()) m_PriorityTargets = ScriptReferences(selection.Scripts());
    else for (const auto &address : selection.Addresses())
        if (address.element.kind == ElementKind::Behavior && address.object) m_PriorityTargets.push_back(address.object);
    m_OpenPriority = !m_PriorityTargets.empty();
    if (!m_OpenPriority) m_State.Log(LogLevel::Info, "Select behaviors to edit their priority (Y)");
}

void SchematicCanvas::InsertBlockAt(const CKGUID &guid, CK_ID script, const Point &local) {
    auto *view = ViewFor(script);
    const auto graph = view ? GraphAt(*view, local) : 0;
    if (!graph) {
        m_State.Log(LogLevel::Info, "Insert Building Blocks into an open Script or an expanded graph");
        return;
    }
    auto fields = AuthoringArguments(script, graph, local);
    if (fields.empty())
        return;
    fields.push_back({"prototype", EDITOR_INTEGER_VALUE, {}, {}, (uint64_t(guid.d1) << 32) | guid.d2});
    ExecuteAuthoring("schematic.insert-block", std::move(fields), GraphFollow::Block);
}

void SchematicCanvas::BeginCopyDrag() {
    const auto fields = SelectedPositions();
    if (fields.empty())
        return;
    const auto values = BorrowEditorFields(fields);
    const auto state = Client().Query("schematic.copy-selection", {values.data(), values.data() + values.size()});
    const auto *bounds = EditorClient::Find(state, "bounds");
    if (state.status != EDITOR_OK || !bounds || bounds->reals.size() != 4)
        return;
    m_Doc.copyScript = m_Navigation.selection.script;
    m_Doc.copySelection = fields;
    m_Doc.copyBounds =
        Rect(float(bounds->reals[0]), float(bounds->reals[1]), float(bounds->reals[2]), float(bounds->reals[3]));
    m_Tool = Tool::CopyDrag;
}

void SchematicCanvas::EndCopyDrag(const Point &world) {
    m_Tool = Tool::Idle;
    auto *view = ViewFor(m_Doc.copyScript);
    if (!view)
        return;
    Point to(m_Doc.copyBounds.hPos + world.h - m_PressWorld.h, m_Doc.copyBounds.vPos + world.v - m_PressWorld.v);
    const auto target = HitAt(ImGui::GetIO().MousePos);
    if (target.view && target.view->id != m_Doc.copyScript) {
        const Point press(m_PressWorld.h, m_PressWorld.v - view->offsetY);
        to = Point(m_Doc.copyBounds.hPos + target.local.h - press.h, m_Doc.copyBounds.vPos + target.local.v - press.v);
        view = target.view;
    }
    if (!view->model.script.open)
        return;
    auto fields = m_Doc.copySelection;
    EditorField at{"at", EDITOR_REAL_LIST};
    at.reals = {to.h, to.v};
    fields.push_back(std::move(at));
    fields.push_back({"destination", EDITOR_OBJECT, view->identity});
    fields.push_back({"destination-revision", EDITOR_INTEGER_VALUE, {}, {}, view->revision});
    const auto values = BorrowEditorFields(fields);
    Execute("schematic.copy-elements", {values.data(), values.data() + values.size()}, GraphFollow::Created,
            view->identity);
    m_Doc.copySelection.clear();
}

void SchematicCanvas::SelectAll() {
    CK_ID script = m_Navigation.selection.script;
    if (!script && !m_Views.empty()) script = m_Views.front().id;
    ScriptView *view = ViewFor(script);
    if (!view) return;
    m_Navigation.selection.Clear();
    const SchematicModel &model = view->model;
    for (const auto &behavior : model.behaviors) m_Navigation.selection.Add(script, behavior.ref);
    for (const auto &operation : model.operations) m_Navigation.selection.Add(script, operation.ref);
    for (const auto &param : model.params)
        if (IsParamShown(param, m_Navigation.options.showLocalParams)) m_Navigation.selection.Add(script, param.ref);
    for (const auto &comment : model.comments) m_Navigation.selection.Add(script, comment.ref);
}

void SchematicCanvas::Click(const Hit &hit) {
    const ImGuiIO &io = ImGui::GetIO();
    if (!hit.view || !hit.ref.IsValid()) {
        if (!io.KeyCtrl && !io.KeyShift) m_Navigation.selection.Clear();
        return;
    }
    // Dev: a click on the header row (triangle and name) opens or closes the Script.
    if (hit.ref.kind == ElementKind::Script && hit.onHeader && !io.KeyCtrl && !io.KeyShift &&
        hit.local.v < hit.view->model.script.header.vPos + CLOSED_SCRIPT_HEIGHT) {
        ToggleScriptOpen(hit.view->id);
        return;
    }
    if (io.KeyCtrl) m_Navigation.selection.Toggle(hit.view->id, hit.ref);
    else if (io.KeyShift) m_Navigation.selection.Add(hit.view->id, hit.ref);
    else m_Navigation.selection.Set(hit.view->id, hit.ref);
}

void SchematicCanvas::DoubleClick(const Hit &hit) {
    if (!hit.view) return;
    if (hit.ref.kind == ElementKind::Script && hit.onHeader && hit.view->model.script.open &&
        hit.local.v >= hit.view->model.script.header.vPos + CLOSED_SCRIPT_HEIGHT &&
        hit.local.v < hit.view->model.script.header.vPos + 36.0f) {
        // Dev: double-click on the Script owner shows it in the Level Manager.
        const auto info = ObjectInfo(hit.view->id, hit.view->id);
        const auto *owner = EditorClient::Find(info, "owner");
        if (info.status == EDITOR_OK && owner && owner->object)
            Client().Command("scene.show-object", {ObjectArgument("target", owner->object)});
    } else if (hit.ref.kind == ElementKind::Script) {
        // The second click of a double-click on the header row toggles again
        // (every click there toggles); elsewhere on the header it toggles once.
        ToggleScriptOpen(hit.view->id);
    } else if (hit.ref.kind == ElementKind::Behavior) {
        // Dev: double-click opens/closes a graph and edits a Building Block's parameters.
        const BehaviorItem *behavior = hit.view->model.FindBehavior(hit.ref.object);
        if (behavior && behavior->expandable) {
            ToggleFold(hit.view->id, behavior->id);
        } else if (behavior) {
            m_Navigation.selection.Set(hit.view->id, hit.ref);
            OpenParametersDialog();
        }
    } else if (hit.ref.kind == ElementKind::Operation) {
        m_Navigation.selection.Set(hit.view->id, hit.ref);
        OpenParametersDialog();
    } else if (hit.ref.kind == ElementKind::Link) {
        for (const auto &link : hit.view->model.links)
            if (link.ref == hit.ref && link.behaviorLink) {
                OpenLinkDelay(hit.view->id, link.ref);
            }
    } else if (hit.ref.kind == ElementKind::LocalParam || hit.ref.kind == ElementKind::Shortcut ||
               hit.ref.kind == ElementKind::Pin) {
        const auto info = ElementInfo(hit.view->id, hit.ref);
        const auto *object = EditorClient::Find(info, "object");
        if (info.status == EDITOR_OK && object &&
            (hit.ref.kind != ElementKind::Pin || hit.ref.pin == PinKind::ParamIn || hit.ref.pin == PinKind::ParamOut ||
             hit.ref.pin == PinKind::Target))
            OpenValueDialog(hit.view->id, object->object);
    } else if (hit.ref.kind == ElementKind::Comment) {
        RetainHit(hit, m_Doc.commentAddress);
        m_Doc.openCommentEditor = bool(m_Doc.commentAddress);
    }
}

bool SchematicCanvas::BeginGeometry(CK_ID script, uint64_t mode, const std::vector<ElementRef> &items,
                                    const ElementRef &primary) {
    auto *view = ViewFor(script);
    if (!view || !m_EditorHost)
        return false;
    EditorField elements{"elements", EDITOR_RECORD};
    for (const auto &item : items) {
        Hit hit;
        hit.view = view;
        hit.ref = item;
        ElementAddress address;
        RetainHit(hit, address);
        if (!address)
            return false;
        elements.fields.push_back(SchematicPosition(address, std::to_string(elements.fields.size()).c_str()));
    }
    std::vector<EditorField> args{{"target", EDITOR_OBJECT, view->identity},
                                  {"mode", EDITOR_INTEGER_VALUE, {}, {}, mode},
                                  std::move(elements),
                                  {"coherence",
                                   EDITOR_INTEGER_VALUE,
                                   {},
                                   {},
                                   uint64_t(m_State.preferences.Bool("Schematic\\Move Item Coherence Test"))},
                                  {"auto-resize",
                                   EDITOR_INTEGER_VALUE,
                                   {},
                                   {},
                                   uint64_t(m_State.preferences.Bool("Schematic\\Auto Resize Scripts"))}};
    if (mode == 0) {
        Hit hit;
        hit.view = view;
        hit.ref = primary;
        ElementAddress address;
        RetainHit(hit, address);
        if (!address)
            return false;
        args.push_back(SchematicPosition(address, "primary"));
    }
    const auto values = BorrowEditorFields(args);
    const auto state = Client().Query("schematic.geometry", {values.data(), values.data() + values.size()});
    if (state.status != EDITOR_OK)
        return false;
    const auto initial = BorrowEditorFields(state.fields);
    if (Client().BeginCommand("schematic.edit-geometry", {initial.data(), initial.data() + initial.size()},
                              m_Doc.gesture) != EDITOR_OK)
        return false;
    m_Doc.geometryValue = *EditorClient::Find(state, "value");
    m_Doc.geometrySelection = m_Navigation.selection.Addresses();
    m_Doc.geometryRevision = EditorClient::Integer(GraphState(script), "document-revision");
    m_Doc.moveScript = script;
    m_Doc.moveChanged = false;
    return true;
}

bool SchematicCanvas::UpdateGeometry(EditorField value) {
    if (!m_Doc.gesture)
        return false;
    const std::vector<EditorField> owned{value};
    const auto fields = BorrowEditorFields(owned);
    const auto status = m_EditorHost->editValue(m_EditorHost->data, m_Doc.gesture, fields.data());
    if (status != EDITOR_OK && status != EDITOR_NO_CHANGE) {
        EndMove(false);
        return false;
    }
    m_Doc.geometryValue = std::move(value);
    return true;
}

void SchematicCanvas::BeginMove() {
    if (!m_Doc.press.view)
        return;
    const CK_ID script = m_Doc.press.view->id;
    if (!IsSelected(script, m_Doc.press.ref)) m_Navigation.selection.Set(script, m_Doc.press.ref);
    std::vector<ElementRef> items;
    for (const auto &item : m_Navigation.selection.items)
        if (Movable(item.kind))
            items.push_back(item);
    if (items.empty() || !BeginGeometry(script, 0, items, m_Doc.press.ref))
        return;
    m_Tool = Tool::Move;
}

void SchematicCanvas::UpdateMove(const Point &world) {
    if (!m_Doc.gesture)
        return;
    EditorField value{"value", EDITOR_RECORD}, delta{"delta", EDITOR_REAL_LIST};
    delta.reals = {world.h - m_PressWorld.h, world.v - m_PressWorld.v};
    value.fields.push_back(std::move(delta));
    m_Doc.moveChanged = UpdateGeometry(std::move(value));
}

void SchematicCanvas::EndMove(bool commit) {
    if (m_Doc.gesture && commit && m_Tool == Tool::Move) {
        auto value = m_Doc.geometryValue;
        value.fields.push_back({"route",
                                EDITOR_INTEGER_VALUE,
                                {},
                                {},
                                uint64_t(m_State.preferences.searchBestPath != ImGui::GetIO().KeyCtrl)});
        if (const auto *view = ViewFor(m_Doc.moveScript)) {
            const auto world = view->transform.ToWorld(ImGui::GetIO().MousePos);
            EditorField drop{"drop", EDITOR_REAL_LIST};
            drop.reals = {world.h, world.v - view->offsetY};
            value.fields.push_back(std::move(drop));
        }
        commit = UpdateGeometry(std::move(value));
    }
    if (m_Doc.gesture) {
        EditorOperation operation = 0;
        const auto status = m_EditorHost->finishEdit(m_EditorHost->data, m_Doc.gesture, commit ? 1 : 0, &operation);
        if (status == EDITOR_QUEUED) {
            GraphCommand command{operation, Client().Scope().document, {}, {}, GraphFollow::Geometry};
            command.selection = m_Doc.geometrySelection;
            command.documentRevision = m_Doc.geometryRevision;
            m_GraphCommands.push_back(std::move(command));
        }
        if (status != EDITOR_OK && status != EDITOR_QUEUED && status != EDITOR_NO_CHANGE && status != EDITOR_CANCELLED)
            Client().CancelEdit(m_Doc.gesture);
        m_Doc.gesture = 0;
    }
    m_Doc.geometryValue = {};
    m_Doc.geometrySelection.clear();
    m_Doc.geometryRevision = 0;
    m_Doc.moveScript = 0;
    m_Tool = Tool::Idle;
}

void SchematicCanvas::CancelInteraction() {
    EndMove(false);
    m_Doc.press = {};
    m_Doc.relinkLink = 0;
    m_Doc.resizeScript = 0;
    m_LinkPoints.clear();
    m_DrawGraphMode = false;
}

// ContextMenu: SchematicMenus.cpp.

void SchematicCanvas::Tooltip(const Hit &hit) {
    if (!hit.view || !hit.ref.IsValid() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    const SchematicModel &model = hit.view->model;
    ImGui::BeginTooltip();
    switch (hit.ref.kind) {
    case ElementKind::Behavior:
        if (const BehaviorItem *behavior = model.FindBehavior(hit.ref.object)) {
            ImGui::TextUnformatted(behavior->name.c_str());
            if (!behavior->prototype.empty()) ImGui::TextDisabled("%s", behavior->prototype.c_str());
            ImGui::TextDisabled("%s  CK_ID %u", behavior->buildingBlock ? "Building Block"
                                                : behavior->graph       ? "Behavior Graph"
                                                                        : "Behavior",
                                behavior->id);
        }
        break;
    case ElementKind::Pin:
        for (const auto &pin : model.pins)
            if (pin.ref == hit.ref) {
                ImGui::TextUnformatted(pin.name.c_str());
                if (!pin.typeName.empty()) ImGui::TextDisabled("%s", pin.typeName.c_str());
            }
        break;
    case ElementKind::LocalParam:
    case ElementKind::Shortcut:
        for (const auto &param : model.params)
            if (param.ref == hit.ref) {
                ImGui::Text("%s%s", param.shortcut ? "Shortcut: " : "", param.name.c_str());
                ImGui::TextDisabled("%s = %s", param.typeName.c_str(), param.value.c_str());
            }
        break;
    case ElementKind::Operation:
        for (const auto &operation : model.operations)
            if (operation.ref == hit.ref) ImGui::Text("Parameter operation: %s", operation.name.c_str());
        break;
    case ElementKind::Link:
        for (const auto &link : model.links)
            if (link.ref == hit.ref) {
                ImGui::Text("%s link", link.behaviorLink ? "Behavior" : "Parameter");
                if (link.behaviorLink) ImGui::TextDisabled("Delay %d", link.delay);
                if (!link.resolved) ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Unresolved endpoint");
            }
        break;
    case ElementKind::Comment:
        for (const auto &comment : model.comments)
            if (comment.ref == hit.ref) ImGui::TextUnformatted(comment.text.c_str());
        break;
    case ElementKind::Script:
        ImGui::Text("%s (%s)", model.script.name.c_str(), model.script.ownerName.c_str());
        ImGui::TextDisabled("Double-click to %s", model.script.open ? "close" : "open");
        break;
    case ElementKind::Start:
        ImGui::TextUnformatted("Start");
        break;
    case ElementKind::ControlPoint:
        ImGui::TextUnformatted("Control point");
        break;
    default:
        break;
    }
    ImGui::EndTooltip();
}

const char *SchematicCanvas::ActionText() const {
    switch (m_Tool) {
    case Tool::Pan: return "Scrolling";
    case Tool::Zoom: return "Zooming";
    case Tool::RectSelect: return "Selecting";
    case Tool::Move: return "Moving";
    case Tool::CopyDrag: return "Copying";
    case Tool::ExportDrag: return "Exporting";
    case Tool::Link: return m_Doc.relinkLink ? "Moving a link end" : "Linking";
    default:
        return m_Doc.pickConditionRow >= 0 ? "Pick a parameter"
               : m_Doc.pickBreakpoint ? "Pick a behavior, a link or a parameter for a breakpoint"
               : m_Doc.pickShortcutFrom ? "Pick the new shortcut source"
               : m_DrawGraphMode  ? "Draw Behavior Graph"
               : m_LinkMode       ? "Link mode"
                                  : "No action";
    }
}

// The Script the Expand/Collapse/Hide buttons act on: the selected one or,
// failing that, the current one.
CK_ID SchematicCanvas::ToolbarScript() const {
    return m_Navigation.selection.script ? m_Navigation.selection.script : m_Doc.currentScript;
}

std::vector<CK_ID> SchematicCanvas::ToolbarScripts() const {
    std::vector<CK_ID> scripts = m_Navigation.selection.Scripts();
    if (scripts.empty())
        if (const CK_ID script = ToolbarScript()) scripts.push_back(script);
    return scripts;
}

void SchematicCanvas::Toolbar() {
    ViewOptions &options = m_Navigation.options;
    // Dev's Schematic toolbar (Dev.exe sub_51CA80, tooltips from the language
    // pack): a strip 21 pixels high (the view below it, this + 988), from x 9
    // the buttons 4 apart: resvis bitmaps (normal, hover, pressed, pressed
    // hover, disabled) 17 or 19 wide at y 1, text buttons 15 high at y 3,
    // separators (bitmap 705) between the groups.
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 stripMin = ImGui::GetCursorScreenPos();
    const float stripHeight = IM_ROUND(21.0f * s);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(stripMin, ImVec2(stripMin.x + ImGui::GetContentRegionAvail().x, stripMin.y + stripHeight),
                      DEV_CHROME);
    PushDarkChrome();
    const bool document = Client().Scope().document.value != 0;
    const CK_ID script = ToolbarScript();
    float x = 9.0f;
    auto at = [&](float y) { return ImVec2(IM_ROUND(stripMin.x + x * s), IM_ROUND(stripMin.y + y * s)); };
    auto icon = [&](const char *id, const int (&bitmaps)[5], bool on, bool enabled, const char *tooltip, float width = 17.0f,
                    float y = 1.0f) {
        const bool clicked = DevBitmapButton(id, at(y), bitmaps, on, enabled, s, tooltip);
        x += width + 4.0f;
        return clicked;
    };
    auto pill = [&](const char *id, const char *label, bool on, bool enabled, const char *tooltip, float width) {
        const ImVec2 a = at(3.0f);
        const bool clicked = DevToolbarPill(id, label, a, ImVec2(a.x + IM_ROUND(width * s), a.y + IM_ROUND(15.0f * s)), on,
                                            tooltip, enabled);
        x += width + 4.0f;
        return clicked;
    };
    auto separator = [&] {
        if (ImTextureID line = DevBitmap(705)) dl->AddImage(line, at(1.0f), ImVec2(at(1.0f).x + s, at(1.0f).y + 19.0f * s));
        x += 1.0f + 4.0f;
    };
    static const int EXPAND[5] = {976, 977, 973, 973, 976};
    static const int COLLAPSE[5] = {978, 974, 975, 975, 978};
    static const int EXPLORER[5] = {777, 779, 778, 778, 777};
    static const int MESSAGES[5] = {1293, 1291, 1292, 1290, 1293};
    static const int FIND[5] = {1353, 1346, 1347, 1347, 1353};
    static const int FILTER[5] = {1355, 1356, 1357, 1354, 1357};
    static const int LOCALS[5] = {1301, 1299, 1300, 1298, 1301};
    static const int HEADERS[5] = {989, 990, 991, 985, 989};
    static const int POINTS[5] = {992, 986, 987, 988, 992};
    static const int INFO[5] = {1002, 1003, 1004, 1005, 1002};
    static const int LINK_PROPERTIES[5] = {1308, 1307, 1306, 1306, 1308};
    static const int PRIORITIES[5] = {972, 970, 971, 969, 972};
    static const int RESET[5] = {877, 878, 879, 880, 877};
    static const int ZOOM[5] = {893, 894, 895, 845, 893};
    static const int DEBUGGER[5] = {1080, 1078, 1079, 1077, 1080};
    static const int LINK_ERRORS[5] = {1335, 1334, 1333, 1333, 1335};
    auto setOpen = [&](bool open) {
        for (CK_ID each : document ? ToolbarScripts() : std::vector<CK_ID>())
            if (ScriptOpen(each) != open) ToggleScriptOpen(each);
    };
    if (icon("expand", EXPAND, false, script != 0, "Expand Selected Scripts (Right Arrow)")) setOpen(true);
    if (icon("collapse", COLLAPSE, false, script != 0, "Collapse Selected Scripts (Left Arrow)")) setOpen(false);
    if (icon("explorer", EXPLORER, false, true, "Schematic Explorer (Ctrl+Shift+F)", 19.0f))
        m_State.events.Post(OpenSchematicExplorer{});
    if (icon("messages", MESSAGES, DiagnosticOpen(false), true, "Message Explorer (H)")) ToggleDiagnostic(false);
    if (icon("find", FIND, false, true, "Find (Ctrl+F)")) OpenFind();
    separator();
    // Dev shows "Auto Link" instead of "Link" while automatic linking is on.
    if (pill("link", options.autoLink ? "Auto Link" : "Link", m_LinkMode, true, "Link Mode (L)", 66.0f))
        m_LinkMode = !m_LinkMode;
    separator();
    if (icon("filter", FILTER, options.sceneFilter, true,
             options.sceneFilter ? "Scene filter: only Scripts of the active scene are shown"
                                 : "Level filter: no filter on the shown Scripts",
             19.0f)) {
        options.sceneFilter = !options.sceneFilter;
        RefreshStack();
    }
    if (pill("showall", "Show All", false, document, "Show All Scripts", 70.0f)) {
        m_Navigation.hiddenScripts.clear();
        options.showAllScripts = true;
        RefreshStack();
    }
    if (pill("hideall", "Hide All", false, document, "Hide All Scripts", 70.0f)) {
        for (auto ref : m_AllScripts)
            m_Navigation.hiddenScripts.insert(ref);
        RefreshStack();
    }
    if (pill("hidesel", "Hide Selected", false, script != 0, "Hide Selected Scripts", 70.0f)) {
        for (auto ref : ScriptReferences(ToolbarScripts()))
            m_Navigation.hiddenScripts.insert(ref);
        m_Navigation.selection.Clear();
        m_Doc.currentScript = 0;
        RefreshStack();
    }
    if (pill("hideother", "Hide Other", false, script != 0, "Hide Unselected Scripts", 70.0f)) {
        const std::vector<CK_ID> kept = ToolbarScripts();
        for (auto ref : m_AllScripts)
            if (std::find(kept.begin(), kept.end(), ref.object) == kept.end())
                m_Navigation.hiddenScripts.insert(ref);
        for (auto ref : ScriptReferences(kept))
            m_Navigation.hiddenScripts.erase(ref);
        RefreshStack();
    }
    separator();
    // Pressed while what they show is hidden (Dev.exe sub_537CA0: the
    // preferences Simplified, Show Script Left, Show Control Points, Show
    // Link Delay, Show Priorities).
    if (icon("locals", LOCALS, !options.showLocalParams, true, "Show/Hide Local Parameters"))
        options.showLocalParams = !options.showLocalParams;
    if (icon("headers", HEADERS, !options.showHeaders, true, "Show/Hide Scripts Headers"))
        options.showHeaders = !options.showHeaders;
    if (icon("points", POINTS, !options.showControlPoints, true, "Show/Hide Control Points"))
        options.showControlPoints = !options.showControlPoints;
    if (icon("info", INFO, !options.showLinkInfo, true, "Show/Hide Link Informations"))
        options.showLinkInfo = !options.showLinkInfo;
    if (icon("linkprops", LINK_PROPERTIES, false, true, "Behavior Link Properties (default link delay)")) {
        auto &editor = m_State.workspace->Application();
        editor.Execute("environment.open-link-options", {editor.Scope(m_Instance), nullptr, 0});
    }
    if (icon("priorities", PRIORITIES, !options.showPriorities, true, "Show/Hide Priorities"))
        options.showPriorities = !options.showPriorities;
    separator();
    if (icon("reset", RESET, false, true, "Reset Schematic Zoom and Position")) ResetView();
    if (icon("zoom", ZOOM, options.zoomMode, true, "Zoom Mode")) options.zoomMode = !options.zoomMode;
    separator();
    auto &application = m_State.workspace->Application();
    const EditorClient runtime(application.Host(), m_Instance);
    const EditorValue role{EDITOR_TEXT("role"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("main.debugger")};
    const bool debuggerOpen = EditorClient::Integer(application.ReadQuery("workspace.role", {application.Scope(m_Instance), &role, 1}), "view") != 0;
    if (icon("debugger", DEBUGGER, debuggerOpen, true, "Script Debugger")) runtime.Command("runtime.toggle-debugger");
    const auto &execution = application.ReadQuery("runtime.state", {application.Scope(m_Instance), nullptr, 0});
    if (EditorClient::Integer(execution, "available") && pill("trace", "Trace", EditorClient::Integer(execution, "trace") != 0, true, "Trace Mode (Ctrl+T)", 66.0f))
        m_State.workspace->Application().Execute("runtime.toggle-trace", {m_State.workspace->Application().Scope(m_Instance), nullptr, 0});
    // Dev: right of Trace, the Invalid Link Explorer's icon, shown only when
    // the composition has invalid links kept after loading.
    if (EditorClient::Integer(Client().Query("schematic.invalid-summary"), "total") &&
        icon("linkerrors", LINK_ERRORS, DiagnosticOpen(true), true, "Browse links with error found while loading", 17.0f, 2.0f))
        ToggleDiagnostic(true);
    // The zoom of the Script under the mouse (the editor's).
    const ScriptView *zoomed = ViewFor(m_Doc.hoveredScript ? m_Doc.hoveredScript : m_Doc.currentScript);
    ImGui::SetCursorScreenPos(ImVec2(at(0).x + 4.0f * s, stripMin.y + (stripHeight - ImGui::GetFontSize()) * 0.5f));
    ImGui::TextDisabled("%.0f%%", (zoomed ? zoomed->Zoom() : m_View.zoom) * 100.0f);
    PopDarkChrome();
    ImGui::SetCursorScreenPos(ImVec2(stripMin.x, stripMin.y + stripHeight));
}

void SchematicCanvas::Draw(const char *title, bool *open) {
    PollGraphCommands();
    // Asked whether the Schematic shows or not (a Level Manager Script saved).
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool visible = BeginDevWindow(title, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!visible) {
        CancelEdits();
        ImGui::End();
        return;
    }
    ImFont *font = ImGui::GetFont();
    ViewOptions &options = m_Navigation.options;
    Toolbar();

    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    // Dev's Horizontal / Vertical Scrollbar (Schematic preferences): the
    // canvas leaves them their room.
    const float scrollbar = ImGui::GetStyle().ScrollbarSize;
    const bool hScroll = m_State.preferences.Bool("Schematic\\Horizontal Scrollbar");
    const bool vScroll = m_State.preferences.Bool("Schematic\\Vertical Scrollbar");
    if (hScroll) size.y -= scrollbar;
    if (vScroll) size.x -= scrollbar;
    size.x = std::max(size.x, 50.0f);
    size.y = std::max(size.y, 50.0f);
    m_CanvasSize = size;
    const ImVec2 canvasMax(canvasMin.x + size.x, canvasMin.y + size.y);
    ImGui::InvisibleButton("canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!focused && Busy()) CancelInteraction();
    m_PickMin = canvasMin;
    m_PickMax = canvasMax;
    m_PickFrame = ImGui::GetFrameCount();
    if (Client().Scope().document.value)
        AcceptBuildingBlockDrop();
    if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
        ImGui::SetWindowFocus();

    m_View.origin = canvasMin;
    m_Font = font;
    SyncViews(font);
    if (m_Navigation.selection.script) m_Doc.currentScript = m_Navigation.selection.script;
    if (!m_ViewInitialized && !m_Views.empty()) {
        ResetView();
        m_ViewInitialized = true;
    }
    ApplyFocus(size);
    if (m_Doc.editAddress) {
        const auto address = m_Doc.editAddress;
        if (ViewFor(address.script.object) && ValidPosition(address)) {
            m_Doc.editAddress = {};
            m_Navigation.selection.Assign({address});
            EditElement(address.script.object, address.element);
        } else if (!ValidPosition(address) || std::find(m_VisibleScripts.begin(), m_VisibleScripts.end(),
                                                        address.script) == m_VisibleScripts.end())
            m_Doc.editAddress = {};
    }

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(canvasMin, canvasMax, true);
    drawList->AddRectFilled(canvasMin, canvasMax, m_State.style.canvas);
    if (options.showGrid) DrawGrid(drawList, m_View, canvasMin, canvasMax, m_State.style);

    DrawOptions draw;
    draw.style = &m_State.style;
    draw.font = font;
    draw.boldFont = BoldFont();
    draw.fontSize = WORLD_FONT_SIZE;
    draw.showLocalParams = options.showLocalParams;
    draw.showLinkInfo = options.showLinkInfo;
    draw.showControlPoints = options.showControlPoints;
    draw.showPriorities = options.showPriorities;
    draw.showHeaders = options.showHeaders;
    draw.clipMin = canvasMin;
    draw.clipMax = canvasMax;
    const auto invalidSummary = Client().Query("schematic.invalid-summary");
    if (invalidSummary.status == EDITOR_OK && EditorClient::Integer(invalidSummary, "revision") != m_InvalidRevision)
        ReadSchematicRows(Client(), "schematic.invalid-links", m_InvalidRows, m_InvalidRevision);
    const auto execution = Client().Query("runtime.state");
    for (const auto &view : m_Views) {
        std::set<ElementRef> selected;
        if (m_Navigation.selection.script == view.id)
            selected.insert(m_Navigation.selection.items.begin(), m_Navigation.selection.items.end());
        if (std::find(m_Navigation.selection.headers.begin(), m_Navigation.selection.headers.end(), view.id) !=
            m_Navigation.selection.headers.end())
            selected.insert(view.model.script.ref);
        draw.selection = &selected;
        std::set<ElementRef> invalid;
        for (const auto &row : m_InvalidRows)
            for (const auto &field : row.fields)
                if (field.key == "address") {
                    ElementAddress address;
                    if (!ReadSchematicPosition(field, address) || address.script != view.identity ||
                        address.revision != view.revision || address.topology != view.topology)
                        continue;
                    auto key = address.element;
                    key.object = 0;
                    const auto retained = view.positions.find(key);
                    if (retained != view.positions.end() && retained->second.owner == address.owner &&
                        retained->second.object == address.object)
                        invalid.insert(retained->second.element);
                }
        draw.invalid = &invalid;
        draw.hovered = m_Doc.hoveredScript == view.id ? m_Doc.hovered : ElementRef();
        draw.current = view.id == m_Doc.currentScript;
        draw.scriptZoom = view.scale;
        EditorResponse activity;
        if (execution.status == EDITOR_OK && EditorClient::Integer(execution, "state") != EDITOR_EXECUTION_STOPPED)
            activity = Client().Query("schematic.activity", {ObjectArgument("target", view.identity)});
        ElementAddress source;
        source.script = view.identity;
        source.revision = view.revision;
        source.topology = view.topology;
        const auto marks = ReadSchematicActivity(execution, activity, source, view.positions);
        draw.activity = &marks;
        DrawScript(drawList, view.model, view.offsetY, view.frame, view.transform, draw);
    }
    HandleInput(canvasMin, canvasMax, hovered, focused);
    if (!Client().Scope().document.value || m_Views.empty()) {
        const char *message = !Client().Scope().document.value ? "File > Open a composition (.cmo, .nmo, .vmo)"
                                                               : "Select an object or Script in the Level Manager";
        const ImVec2 textSize = ImGui::CalcTextSize(message);
        drawList->AddText(ImVec2(canvasMin.x + (size.x - textSize.x) * 0.5f, canvasMin.y + (size.y - textSize.y) * 0.5f),
                          IM_COL32(230, 230, 230, 255), message);
    }
    drawList->PopClipRect();
    Scrollbars(canvasMin, canvasMax, hScroll, vScroll);

    if (m_MenuRequested && m_ViewInitialized && --m_MenuDelay <= 0) {
        m_MenuRequested = false;
        m_Doc.menuHit = HitAt(ImVec2(canvasMin.x + m_MenuRequest.x, canvasMin.y + m_MenuRequest.y));
        RetainHit(m_Doc.menuHit, m_MenuAddress);
        m_MenuStale = false;
        if (m_Doc.menuHit.view && m_Doc.menuHit.ref.IsValid()) m_Navigation.selection.Set(m_Doc.menuHit.view->id, m_Doc.menuHit.ref);
        ImGui::SetNextWindowPos(ImVec2(canvasMin.x + m_MenuRequest.x, canvasMin.y + m_MenuRequest.y));
        ImGui::OpenPopup("SchematicContext");
    }
    ContextMenu();
    FindDialog();
    ReplaceSourceDialog();
    ConfirmScriptDeletion();
    ConfirmGraphDeletion();
    ReplaceBlockDialog();
    ReplaceGraphDialog();
    BreakpointConfigDialog();
    BreakpointListDialog();
    OperationChoiceDialog();
    CkPropertiesDialog();
    ShortcutAnalysisDialog();
    QuickAddDialog();
    ScriptColorDialog();
    CommentEditor();
    RenameDialog();
    DelayDialog();
    LocalParameterDialog();
    OperationDialog();
    ParametersDialog();
    PriorityDialog();
    BlockMenus();
    // Dev "Enable Tooltips" / "Tooltips Delay(ms)".
    if (m_Doc.hovered != m_Doc.tooltipRef) {
        m_Doc.tooltipRef = m_Doc.hovered;
        m_TooltipSince = ImGui::GetTime();
    }
    const double delay = m_State.preferences.tooltipDelayMs / 1000.0;
    if (hovered && m_Tool == Tool::Idle && m_State.preferences.tooltips && ImGui::GetTime() - m_TooltipSince >= delay)
        Tooltip(HitAt(ImGui::GetIO().MousePos));
    ImGui::End();
}

} // namespace vdev

namespace vdev {

void SchematicCanvas::UpdateLinkPaths() {
    const auto &selection = m_Navigation.selection;
    auto *view = ViewFor(selection.script);
    if (!view)
        return;
    std::set<CK_ID> owners;
    std::set<ElementRef> selected;
    for (const auto &item : selection.items) {
        if (item.kind == ElementKind::Link)
            selected.insert(item);
        if (item.kind == ElementKind::Behavior || item.kind == ElementKind::Operation) owners.insert(item.object);
    }
    EditorField elements{"elements", EDITOR_RECORD};
    for (const auto &link : view->model.links)
        if (!link.hidden && (selected.count(link.ref) || owners.count(link.start.id) || owners.count(link.end.id))) {
            Hit hit;
            hit.view = view;
            hit.ref = link.ref;
            ElementAddress address;
            RetainHit(hit, address);
            if (!address)
                return;
            elements.fields.push_back(SchematicPosition(address, std::to_string(elements.fields.size()).c_str()));
        }
    if (elements.fields.empty()) {
        m_State.Log(LogLevel::Info, "P: select links, or behaviors and operations whose links to update");
        return;
    }
    ExecuteGraph("schematic.route-links", view->id, 0, nullptr, {elements});
}

bool SchematicCanvas::ResizeHandleAt(ScriptView &view, const Point &local, ElementRef &ref, int &edges) const {
    const float tolerance = 4.0f / std::max(0.05f, view.Zoom());
    auto test = [&](const Rect &rect, bool rightAndBottom, bool cornerOnly) {
        const bool inside = local.h >= rect.hPos - tolerance && local.h <= rect.Right() + tolerance &&
                            local.v >= rect.vPos - tolerance && local.v <= rect.Bottom() + tolerance;
        if (!inside) return 0;
        const bool right = std::fabs(local.h - rect.Right()) <= tolerance;
        const bool bottom = std::fabs(local.v - rect.Bottom()) <= tolerance;
        if (cornerOnly) {
            // Dev: the bottom-right corner (three diagonal lines) of an expanded
            // graph: the drawn grip (14 px at 100%), at least 12 screen pixels.
            const float corner = std::max(14.0f, 12.0f / std::max(0.05f, view.Zoom()));
            return local.h >= rect.Right() - corner && local.v >= rect.Bottom() - corner ? 3 : 0;
        }
        return rightAndBottom ? (right ? 1 : 0) | (bottom ? 2 : 0) : 0;
    };
    for (const auto &comment : view.model.comments) {
        if (comment.collapsed) continue;
        const int found = test(comment.bounds, true, false);
        if (found) {
            ref = comment.ref;
            edges = found;
            return true;
        }
    }
    // The grip of the expanded graph on top at that point (a graph hidden
    // under a later frame cannot be resized there).
    if (const BehaviorItem *frame = TopGraphFrameAt(view.model, local)) {
        const int found = test(frame->box, false, true);
        if (found) {
            ref = frame->ref;
            edges = found;
            return true;
        }
    }
    return false;
}

void SchematicCanvas::SelectAdjacentScript(int step) {
    if (m_Views.empty()) return;
    int at = -1;
    for (int i = 0; i < static_cast<int>(m_Views.size()); ++i)
        if (m_Views[i].id == (m_Navigation.selection.script ? m_Navigation.selection.script : m_Doc.currentScript)) at = i;
    const int next = std::max(0, std::min(static_cast<int>(m_Views.size()) - 1, at < 0 ? 0 : at + step));
    const ScriptView &view = m_Views[next];
    m_Doc.currentScript = view.id;
    ShowScript(view.id, view.model.script.ref);
}

void SchematicCanvas::MoveHighlightedScript(int step) {
    auto order = m_VisibleScripts;
    const auto id = m_Doc.currentScript ? m_Doc.currentScript : m_Navigation.selection.script;
    auto at = std::find_if(order.begin(), order.end(), [&](ObjectRef ref) { return ref.object == id; });
    if (at == order.end())
        return;
    const auto other = (at - order.begin()) + step;
    if (other < 0 || other >= std::ptrdiff_t(order.size()))
        return;
    std::swap(*at, order[size_t(other)]);
    m_Navigation.scriptOrder = std::move(order);
    RefreshStack();
}

void SchematicCanvas::ScrollToEnd(bool end) {
    if (m_Views.empty()) return;
    m_ScrollX = 0.0f;
    if (!end) {
        m_View.panY = m_Views.front().stackTop;
    } else {
        const ScriptView &last = m_Views.back();
        const float bottom = last.stackTop + last.model.script.bounds.vSize;
        m_View.panY = std::max(m_Views.front().stackTop, bottom - m_CanvasSize.y / m_View.zoom);
    }
    UpdateTransforms();
}

} // namespace vdev

namespace vdev {

void SchematicCanvas::InitialConditionsOfScripts(int action) {
    std::vector<uint32_t> scripts = m_Navigation.selection.Scripts();
    if (scripts.empty() && m_Doc.currentScript) scripts.push_back(m_Doc.currentScript);
    const auto objects = ScriptReferences(scripts);
    if (!objects.empty()) Execute("scene.initial-conditions", {ObjectsArgument("objects", objects),
        IntegerArgument("mode", action == 0 ? 0 : action == 1 ? 2 : 1), IntegerArgument("hierarchy", 0)});
}

} // namespace vdev

namespace vdev {

void SchematicCanvas::OpenOperationChoice(OperationChoice mode, CK_ID script, const LinkEndpoint &from,
                                          const LinkEndpoint &to, const ElementRef &link) {
    auto *view = ViewFor(script);
    if (!view || !link.IsValid())
        return;
    Hit hit;
    hit.view = view;
    hit.ref = link;
    ElementAddress address;
    RetainHit(hit, address);
    if (!address)
        return;
    const auto source = EndpointAddress(script, from), destination = EndpointAddress(script, to);
    if (!source || !destination)
        return;
    m_Doc.connection = {{"target", EDITOR_OBJECT, address.script},
                        {"revision", EDITOR_INTEGER_VALUE, {}, {}, address.revision},
                        {"topology", EDITOR_INTEGER_VALUE, {}, {}, address.topology},
                        SchematicPosition(address, "link"),
                        SchematicPosition(source, "from"),
                        SchematicPosition(destination, "to"),
                        {"mode", EDITOR_INTEGER_VALUE, {}, {}, mode == OperationChoice::InsertInLink ? 2u : 1u}};
    const auto values = BorrowEditorFields(m_Doc.connection);
    const auto state = Client().Query("schematic.connection", {values.data(), values.data() + values.size()});
    const auto *choices = EditorClient::Find(state, "choices");
    if (state.status != EDITOR_OK || !choices || choices->fields.empty()) {
        m_State.Log(LogLevel::Info, "No parameter operation fits these parameter types");
        return;
    }
    m_Doc.choices = choices->fields;
    m_ChoiceMode = mode;
    m_ChoicePick = 0;
    m_Doc.openOperationChoice = true;
}

void SchematicCanvas::OperationChoiceDialog() {
    const char *title = m_ChoiceMode == OperationChoice::InsertInLink ? "Add Parameter Operation in Link###OpChoice"
                        : m_ChoiceMode == OperationChoice::ReplaceConversion ? "Edit Parameter Operation Link###OpChoice"
                                                                             : "Parameter Operation Link###OpChoice";
    if (m_Doc.openOperationChoice) {
        ImGui::OpenPopup(title);
        m_Doc.openOperationChoice = false;
    }
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(m_ChoiceMode == OperationChoice::InsertInLink
                               ? "Operation to insert between the source and the destination:"
                               : "The parameter types differ; convert through:");
    bool go = false;
    if (ImGui::IsWindowAppearing()) ImGui::SetNextWindowFocus();
    if (ImGui::BeginListBox("##choices", ImVec2(460, 260))) {
        TypeAhead typeAhead;
        for (int i = 0; i < static_cast<int>(m_Doc.choices.size()); ++i)
            if (typeAhead.Selectable(
                    (EditorClient::Text(EditorResponse{EDITOR_OK, {}, m_Doc.choices[i].fields}, "label") + "##" +
                     std::to_string(i))
                        .c_str(),
                    i == m_ChoicePick, ImGuiSelectableFlags_AllowDoubleClick)) {
                m_ChoicePick = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) go = true;
            }
        ImGui::EndListBox();
    }
    if (ImGui::Button("OK", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) go = true;
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    if (go && m_ChoicePick >= 0 && m_ChoicePick < static_cast<int>(m_Doc.choices.size())) {
        const auto signature = m_Doc.choices[m_ChoicePick];
        ImGui::CloseCurrentPopup();
        ApplyOperationChoice(signature);
    }
    ImGui::EndPopup();
}

void SchematicCanvas::ApplyOperationChoice(const EditorField &choice) {
    for (const auto &field : choice.fields)
        if (field.key == "signature")
            m_Doc.connection.push_back(field);
    const auto values = BorrowEditorFields(m_Doc.connection);
    Execute("schematic.connect", {values.data(), values.data() + values.size()});
}

void SchematicCanvas::OpenModifyOperation() {
    const auto &selection = m_Navigation.selection;
    if (selection.items.size() != 1 || selection.items.front().kind != ElementKind::Operation) {
        m_State.Log(LogLevel::Info, "M: select a parameter operation to modify");
        return;
    }
    const auto info = ElementInfo(selection.script, selection.items.front());
    const auto *object = EditorClient::Find(info, "object"), *owner = EditorClient::Find(info, "owner"),
               *signature = EditorClient::Find(info, "signature");
    if (info.status != EDITOR_OK || !object || !owner || !signature || signature->integers.size() != 4)
        return;
    m_Doc.creation = AuthoringArguments(selection.script, owner->object.object);
    if (m_Doc.creation.empty())
        return;
    m_Doc.modifyOperation = object->object;
    std::copy_n(signature->integers.begin(), 4, m_OperationParts);
    m_Doc.openOperationPicker = true;
}

// Modify Parameter Operation (ReplaceParameterOperation): one undo step; the
// links that did not fit the new types are logged.

} // namespace vdev

namespace vdev {

void SchematicCanvas::ConvertToShortcutSource(ObjectRef source, const std::vector<CK_ID> &scripts) {
    if (!source)
        return;
    const auto refs = ScriptReferences(scripts);
    if (refs.size() != scripts.size())
        return;
    Execute("schematic.shortcutize-locals", {ObjectArgument("source", source), ObjectsArgument("scripts", refs)});
}

void SchematicCanvas::BeginPickShortcutSource(ObjectRef from) {
    m_Doc.pickShortcutFrom = from;
    m_State.Log(LogLevel::Info, "Set Shortcut Source: click the new source (Esc cancels)");
}

void SchematicCanvas::RetargetAllShortcuts(ObjectRef from, ObjectRef to) {
    Execute("schematic.retarget-shortcuts", {ObjectArgument("source", from), ObjectArgument("destination", to)});
}

// ---------------------------------------------------------------- Find

void SchematicCanvas::FindNext(bool backward) {
    if (!Client().Scope().document.value)
        return;
    if (m_FindText.empty()) {
        OpenFind();
        return;
    }
    RefreshStack();
    auto args =
        std::vector<EditorField>{{"text", EDITOR_TEXT_VALUE, {}, m_FindText},
                                 {"order", EDITOR_OBJECT_LIST},
                                 {"hidden", EDITOR_OBJECT_LIST},
                                 {"closed", EDITOR_OBJECT_LIST},
                                 {"options",
                                  EDITOR_INTEGER_VALUE,
                                  {},
                                  {},
                                  uint64_t(m_FindClosed) | (uint64_t(m_FindHidden) << 1) | (uint64_t(backward) << 2)},
                                 {"previous", EDITOR_RECORD}};
    args[1].objects = m_VisibleScripts;
    args[2].objects.assign(m_Navigation.hiddenScripts.begin(), m_Navigation.hiddenScripts.end());
    args[3].objects.assign(m_Navigation.closedScripts.begin(), m_Navigation.closedScripts.end());
    if (m_FoundAddress)
        args[5] = SchematicPosition(m_FoundAddress, "previous");
    const auto values = BorrowEditorFields(args);
    const auto result = Client().Query("schematic.find", {values.data(), values.data() + values.size()});
    if (result.status != EDITOR_OK)
        return;
    if (!EditorClient::Integer(result, "found")) {
        m_State.Log(LogLevel::Warning, "Passed the end of Schematic");
        m_Navigation.selection.Clear();
        m_FoundScript = 0;
        m_FoundRef = {};
        m_FoundAddress = {};
        return;
    }
    const auto *position = EditorClient::Find(result, "address"), *focus = EditorClient::Find(result, "focus-target");
    if (!position)
        return;
    const std::vector<EditorField> owned{*position};
    const auto raw = BorrowEditorFields(owned);
    ElementAddress address;
    if (!ReadSchematicPosition(*raw.data(), address))
        return;
    m_FoundAddress = address;
    m_FoundScript = address.script.object;
    m_FoundRef = address.element;
    auto args2 = position->fields;
    args2.push_back({"reveal", EDITOR_INTEGER_VALUE, {}, {}, 1});
    if (focus && focus->object)
        args2.push_back(*focus);
    const auto navigation = BorrowEditorFields(args2);
    Client().Command("script.navigate", {navigation.data(), navigation.data() + navigation.size()});
}

void SchematicCanvas::FindSelected(bool backward) {
    const auto &selection = m_Navigation.selection;
    if (selection.items.size() != 1 || !Client().Scope().document.value) {
        m_State.Log(LogLevel::Info, "Ctrl+F3: select one element");
        return;
    }
    const auto info = ElementInfo(selection.script, selection.items.front());
    if (info.status != EDITOR_OK)
        return;
    m_FindText = EditorClient::Text(info, "name");
    if (m_FindText.empty())
        return;
    m_FoundAddress = CapturePosition(selection.script, selection.items.front());
    if (!m_FoundAddress)
        return;
    m_FoundScript = selection.script;
    m_FoundRef = selection.items.front();
    FindNext(backward);
}

void SchematicCanvas::FindDialog() {
    const char *id = "Find###SchematicFind";
    if (m_OpenFind) {
        m_OpenFind = false;
        ImGui::OpenPopup(id);
    }
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(280);
    const bool entered = ImGui::InputText("Find", &m_FindText, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Checkbox("Search Closed Scripts/Graphs", &m_FindClosed);
    ImGui::Checkbox("Search Hidden Scripts", &m_FindHidden);
    ImGui::Checkbox("Search Backward", &m_FindBackward);
    if (ImGui::Button("Find Next", ImVec2(90, 0)) || entered) {
        ImGui::CloseCurrentPopup();
        m_FoundScript = 0;
        m_FoundRef = ElementRef();
        // From the element selected, as Dev goes on from it.
        if (m_Navigation.selection.items.size() == 1) {
            m_FoundScript = m_Navigation.selection.script;
            m_FoundRef = m_Navigation.selection.items.front();
            m_FoundAddress = CapturePosition(m_FoundScript, m_FoundRef);
        }
        FindNext(m_FindBackward);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace vdev

namespace vdev {
namespace {
const EditorParameter schematicNavigation[] = {
    {EDITOR_TEXT("target"), EDITOR_OBJECT, 1},          {EDITOR_TEXT("owner"), EDITOR_OBJECT, 1},
    {EDITOR_TEXT("object"), EDITOR_OBJECT, 1},          {EDITOR_TEXT("endpoint"), EDITOR_OBJECT, 1},
    {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE, 1},     {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE, 1},
    {EDITOR_TEXT("sub"), EDITOR_INTEGER_VALUE, 1},      {EDITOR_TEXT("pin"), EDITOR_INTEGER_VALUE, 1},
    {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE, 1},
    {EDITOR_TEXT("focus-target"), EDITOR_OBJECT, 1},    {EDITOR_TEXT("background"), EDITOR_INTEGER_VALUE, 1},
    {EDITOR_TEXT("reveal"), EDITOR_INTEGER_VALUE, 1}};
const EditorParameter schematicFields[] = {
    {EDITOR_TEXT("selection"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("scripts"), EDITOR_OBJECT_LIST},
    {EDITOR_TEXT("target"), EDITOR_OBJECT}, {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE},
    {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("zoom"), EDITOR_REAL_VALUE}};
}
struct SchematicViews::Data {
    EditorState &state;
    ViewOptions defaults;
    std::multimap<EditorView, SchematicCanvas *> canvases;
    mutable EditorView current = 0;
    SchematicCanvas *Canvas(EditorView id) const {
        auto &views = *state.schematics;
        return views.Find(views.Find(id) ? id : views.CurrentId());
    }
};
SchematicViews::SchematicViews(EditorState &state) : m_Data(new Data{state, {}, {}}) { state.schematics = this; }
SchematicViews::~SchematicViews() { m_Data->state.schematics = nullptr; }
ViewOptions &SchematicViews::Defaults() { return m_Data->defaults; }
SchematicCanvas *SchematicViews::Find(EditorView id) const {
    const auto range = m_Data->canvases.equal_range(id);
    return range.first == range.second ? nullptr : std::prev(range.second)->second;
}
EditorView SchematicViews::CurrentId() const {
    auto &data = *m_Data;
    if (auto *workspace = data.state.workspace) {
        const auto focused = workspace->Focused();
        if (Find(focused)) data.current = focused;
        if (!Find(data.current)) data.current = workspace->Role("main.schematic");
    }
    if (!Find(data.current) && !data.canvases.empty()) data.current = data.canvases.begin()->first;
    return data.current;
}
SchematicCanvas &SchematicViews::Current() const {
    auto *canvas = Find(CurrentId());
    if (!canvas) throw std::logic_error("No Schematic view is open");
    const auto scope = canvas->Client().Scope();
    if (scope.view == canvas->m_Instance && canvas->m_Document != scope.document)
        canvas->ValidateNavigation();
    return *canvas;
}
SchematicNavigation &EditorState::Schematic() const {
    if (!schematics) throw std::logic_error("Schematic views are not available");
    return schematics->Current().Navigation();
}
std::string SchematicViews::Window(EditorView id) const {
    return m_Data->state.workspace->WindowName(id ? id : CurrentId());
}
void SchematicViews::Focus(EditorView id) { m_Data->state.workspace->Focus(id ? id : CurrentId()); }
void SchematicViews::ApplyOptions(const ViewOptions &options) {
    m_Data->defaults = options;
    for (auto &entry : m_Data->canvases) entry.second->Navigation().options = options;
    RefreshStacks();
}
void SchematicViews::Validate() { for (auto &entry : m_Data->canvases) entry.second->ValidateNavigation(); }
void SchematicViews::CancelInteraction() { for (auto &entry : m_Data->canvases) entry.second->CancelInteraction(); }
void SchematicViews::DocumentClosed() { for (auto &entry : m_Data->canvases) entry.second->DocumentClosed(); }
void SchematicViews::Invalidate() { for (auto &entry : m_Data->canvases) entry.second->Invalidate(); }
void SchematicViews::RefreshValues() { for (auto &entry : m_Data->canvases) entry.second->RefreshValues(); }
void SchematicViews::RefreshStacks() { for (auto &entry : m_Data->canvases) entry.second->RefreshStack(); }
void SchematicViews::ClearSelections() { for (auto &entry : m_Data->canvases) entry.second->Navigation().selection.Clear(); }
bool SchematicViews::Busy() const {
    for (const auto &entry : m_Data->canvases) if (entry.second->Busy()) return true;
    return false;
}
bool SchematicViews::DropFile(const std::wstring &path, const std::string &ansi, float x, float y) {
    for (auto &entry : m_Data->canvases)
        if (entry.second->DropFile(path, ansi, ImVec2(x, y))) return true;
    return false;
}
EditorStatus SchematicViews::Show(const ElementAddress &address, ObjectRef focus, bool select, EditorView id) {
    if (!address) return EDITOR_STALE;
    auto &state = m_Data->state;
    if (!id) id = CurrentId();
    if (!Find(id)) return EDITOR_STALE;
    const auto &ref = address.element;
    const EditorValue values[] = {
        {EDITOR_TEXT("target"), EDITOR_OBJECT, address.script}, {EDITOR_TEXT("owner"), EDITOR_OBJECT, address.owner},
        {EDITOR_TEXT("object"), EDITOR_OBJECT, address.object}, {EDITOR_TEXT("endpoint"), EDITOR_OBJECT, address.endpoint},
        {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(ref.kind)},
        {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(ref.index))},
        {EDITOR_TEXT("sub"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(ref.sub))},
        {EDITOR_TEXT("pin"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(ref.pin)},
        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, address.revision},
        {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE, {}, {}, address.topology},
        {EDITOR_TEXT("focus-target"), EDITOR_OBJECT, focus},
        {EDITOR_TEXT("background"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(!select)}};
    auto &editor = state.workspace->Application();
    const EditorRequest request{editor.Scope(id), values, uint32_t(std::size(values))};
    return editor.Execute("script.navigate", request).status;
}
bool SchematicViews::Register(EditorApplication &editor, std::string &error) {
    EditorViewDesc view{};
    view.size = sizeof(view);
    view.imguiVersion = EDITOR_IMGUI_VERSION;
    view.id = EDITOR_TEXT("script.schematic");
    view.provider = EDITOR_TEXT("builtin.schematic");
    view.title = EDITOR_TEXT("Schematic");
    view.area = EDITOR_DOCUMENT;
    view.data = m_Data.get();
    view.create = [](void *data, const EditorHost *, EditorView id) -> void * {
        auto &module = *static_cast<Data *>(data);
        auto canvas = std::make_unique<SchematicCanvas>(module.state, id);
        canvas->Navigation().options = module.defaults;
        canvas->RefreshStack();
        module.canvases.emplace(id, canvas.get());
        return canvas.release();
    };
    view.destroy = [](void *data) {
        auto *canvas = static_cast<SchematicCanvas *>(data);
        auto &module = *canvas->m_State.schematics->m_Data;
        canvas->CancelEdits();
        const auto range = module.canvases.equal_range(canvas->m_Instance);
        for (auto it = range.first; it != range.second; ++it)
            if (it->second == canvas) { module.canvases.erase(it); break; }
        delete canvas;
    };
    view.draw = [](void *data, const EditorHost *, EditorView, EditorViewFrame *frame) {
        static_cast<SchematicCanvas *>(data)->Draw(*frame);
    };
    view.pick = [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        return static_cast<SchematicCanvas *>(data)->PickObjectsAt(
            ImVec2(float(request->values[0].real), float(request->values[1].real)), sink);
    };
    view.parameters = schematicNavigation;
    view.count = uint32_t(std::size(schematicNavigation));
    view.navigate = [](void *data, const EditorHost *, const EditorRequest *request) {
        return static_cast<SchematicCanvas *>(data)->Navigate(*request);
    };
    view.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        return static_cast<SchematicCanvas *>(data)->Inspect(sink);
    };
    view.fields = schematicFields;
    view.fieldCount = uint32_t(std::size(schematicFields));
    view.stateVersion = 1;
    view.saveState = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        return static_cast<SchematicCanvas *>(data)->Save(sink);
    };
    view.restoreState = [](void *data, const EditorHost *, const EditorRequest *request) {
        return static_cast<SchematicCanvas *>(data)->Restore(*request);
    };
    EditorCommandDesc commands[] = {{sizeof(EditorCommandDesc), EDITOR_TEXT("script.navigate"),
        EDITOR_TEXT("builtin.schematic"), EDITOR_TEXT("Show Schematic Element"), schematicNavigation,
        uint32_t(std::size(schematicNavigation)),
        [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
            const auto &views = *static_cast<Data *>(data)->state.schematics;
            const auto id = views.Find(request->scope.view) ? request->scope.view : views.CurrentId();
            return host->navigateView(host->data, EditorString EDITOR_TEXT("script.schematic"), id, request, sink);
        }, m_Data.get(), EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("script.pick-breakpoint"), EDITOR_TEXT("builtin.schematic"),
            EDITOR_TEXT("Add Breakpoint"), nullptr, 0, nullptr, m_Data.get(), EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("script.configure-breakpoints"), EDITOR_TEXT("builtin.schematic"),
            EDITOR_TEXT("Configure Breakpoints"), nullptr, 0, nullptr, m_Data.get(), EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("script.open-breakpoints"), EDITOR_TEXT("builtin.schematic"),
            EDITOR_TEXT("Breakpoint List"), nullptr, 0, nullptr, m_Data.get(), EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("script.show-parameter"), EDITOR_TEXT("builtin.schematic"),
            EDITOR_TEXT("Go to Parameter Source"), nullptr, 0, nullptr, m_Data.get(), EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE}};
    const EditorParameter parameter{EDITOR_TEXT("target"), EDITOR_OBJECT};
    commands[4].parameters = &parameter; commands[4].count = 1;
    for (size_t i = 1; i < std::size(commands); ++i)
        commands[i].state = [](void *data, const EditorHost *, const EditorRequest *request) {
            const auto &module = *static_cast<Data *>(data);
            if (!module.state.workspace->Application().NativeAvailable()) return EDITOR_REFUSED;
            return module.state.HasDocument() && module.Canvas(request->scope.view) ? EDITOR_OK : EDITOR_REFUSED;
        };
    commands[1].run = [](void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
        static_cast<Data *>(data)->Canvas(request->scope.view)->PickBreakpoint(); return EDITOR_OK;
    };
    commands[2].run = [](void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
        static_cast<Data *>(data)->Canvas(request->scope.view)->ConfigureBreakpoints(); return EDITOR_OK;
    };
    commands[3].run = [](void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
        static_cast<Data *>(data)->Canvas(request->scope.view)->OpenBreakpointList(); return EDITOR_OK;
    };
    commands[4].run = [](void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
        auto &module = *static_cast<Data *>(data);
        const auto target = request->values[0].object;
        if (!module.state.document->Resolve(target, CKCID_PARAMETER)) return EDITOR_INVALID;
        module.Canvas(request->scope.view)->GoToParameter(target);
        return EDITOR_OK;
    };
    std::vector<EditorCommandDesc> all(std::begin(commands), std::end(commands));
    EditorCommandDesc edit{sizeof(EditorCommandDesc),
                           EDITOR_TEXT("script.edit-element"),
                           EDITOR_TEXT("builtin.schematic"),
                           EDITOR_TEXT("Edit Schematic Element"),
                           schematicNavigation,
                           uint32_t(std::size(schematicNavigation)),
                           [](void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
                               auto *canvas = static_cast<Data *>(data)->Canvas(request->scope.view);
                               if (!canvas)
                                   return EDITOR_STALE;
                               for (uint32_t i = 0; i < request->count; ++i)
                                   if (std::string(request->values[i].key.data, request->values[i].key.size) ==
                                           "background" &&
                                       request->values[i].integer)
                                       return EDITOR_INVALID;
                               const auto status = canvas->Navigate(*request);
                               if (status == EDITOR_OK)
                                   canvas->m_Doc.editAddress = canvas->m_Navigation.focus.address;
                               return status;
                           },
                           m_Data.get(),
                           EDITOR_DOCUMENT,
                           EDITOR_SESSION_CHANGE};
    edit.state = commands[1].state;
    all.push_back(edit);
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("schematic.views");
    module.commands = all.data();
    module.commandCount = uint32_t(all.size());
    module.views = &view;
    module.viewCount = 1;
    return editor.Register(module, error);
}
SchematicCanvas::SchematicCanvas(EditorState &state, EditorView instance) : m_State(state), m_Instance(instance) {
    m_EditorHost = state.workspace->Application().Host();
    m_Navigation.selection.Bind(
        [this](uint32_t script, const ElementRef &ref) { return CapturePosition(script, ref); });
    m_Document = Client().Scope().document;
}
SchematicCanvas::~SchematicCanvas() { m_State.fileDialogs.Cancel(this); }
void SchematicCanvas::RequestFile(const FileDialogOptions &options, FileDialogs::Completion completion) {
    m_State.RequestFile(options, std::move(completion), false, this);
}
bool SchematicCanvas::ScriptOpen(CK_ID id) const {
    const auto refs = ScriptReferences({id});
    return !refs.empty() && !m_Navigation.closedScripts.count(refs.front());
}
void SchematicCanvas::DocumentClosed() {
    CancelEdits();
    // Navigation contains value identities only. Keep it while document
    // replacement can still fail; a new document token resets it below.
    for (const auto &view : m_Views) m_PendingZoom.emplace_back(view.id, view.scale);
    Invalidate();
    m_Doc = {};
    m_InvalidRows.clear(); m_InvalidRevision = 0;
    m_QuerySelection.clear();
    m_QueryScripts.clear();
    m_QueryScript = {};
    m_QueryElement = {};
    ++m_StackRevision;
}
void SchematicCanvas::Invalidate() {
    // No retained hit may outlive the model storage it points into.
    m_Doc.press = {};
    m_Doc.menuHit = {};
    m_MenuStale = true;
    m_PressAddress = {};
    m_MenuAddress = {};
    m_Views.clear();
    m_PickFrame = -1;
}
void SchematicCanvas::RetainHit(const Hit &hit, ElementAddress &address) {
    address = {};
    if (!hit.view || !m_EditorHost)
        return;
    const auto &view = *hit.view;
    const auto ref = hit.ref.IsValid() ? hit.ref : Selection::HeaderOf(view.id);
    const auto result =
        Client().Query("schematic.position",
                       {ObjectArgument("target", view.identity), IntegerArgument("revision", view.revision),
                        IntegerArgument("topology", view.topology), IntegerArgument("kind", uint64_t(ref.kind)),
                        IntegerArgument("owner-id", ref.owner), IntegerArgument("object-id", ref.object),
                        IntegerArgument("index", uint64_t(int64_t(ref.index))),
                        IntegerArgument("sub", uint64_t(int64_t(ref.sub))), IntegerArgument("pin", uint64_t(ref.pin))});
    const auto *field = EditorClient::Find(result, "address");
    if (result.status == EDITOR_OK && field) {
        const std::vector<EditorField> owned{*field};
        const auto values = BorrowEditorFields(owned);
        ReadSchematicPosition(*values.data(), address);
    }
}
void SchematicCanvas::ValidateNavigation() {
    const auto document = Client().Scope().document;
    if (document != m_Document) {
        auto options = m_Navigation.options;
        m_Navigation = {};
        m_Navigation.options = options;
        m_Navigation.selection.Bind(
            [this](uint32_t script, const ElementRef &ref) { return CapturePosition(script, ref); });
        m_Document = document;
        m_PendingZoom.clear();
        m_FoundAddress = {};
        m_FoundRef = {};
        m_FoundScript = 0;
        m_ViewInitialized = false;
        m_AllScripts.clear();
        m_VisibleScripts.clear();
        m_ScriptRows.clear();
        m_Views.clear();
        m_FocusParents.clear();
        Invalidate();
        RefreshStack();
    }
    if (!document.value) {
        PublishNavigation();
        return;
    }
    EditorField elements{"elements", EDITOR_RECORD};
    const auto &selected = m_Navigation.selection.Addresses();
    for (size_t i = 0; i < selected.size(); ++i)
        if (selected[i])
            elements.fields.push_back(SchematicPosition(selected[i], std::to_string(i).c_str()));
    if (m_Navigation.focus.pending)
        elements.fields.push_back(SchematicPosition(m_Navigation.focus.address, "focus"));
    if (m_FoundAddress)
        elements.fields.push_back(SchematicPosition(m_FoundAddress, "found"));
    const std::vector<EditorField> ownedElements{elements};
    const auto values = BorrowEditorFields(ownedElements);
    const auto result = Client().Query("schematic.validate-positions", {values.data(), values.data() + values.size()});
    if (result.status != EDITOR_OK)
        return;
    const auto *valid = EditorClient::Find(result, "elements");
    if (!valid)
        return;
    std::vector<ElementAddress> addresses;
    bool focus = false, found = false;
    for (const auto &item : valid->fields) {
        ElementAddress a;
        if (!ReadSchematicPosition(item, a))
            continue;
        if (item.key == "focus")
            focus = true;
        else if (item.key == "found")
            found = true;
        else
            addresses.push_back(a);
    }
    m_Navigation.selection.Validate([&](const ElementAddress &a) {
        return std::any_of(addresses.begin(), addresses.end(), [&](const ElementAddress &v) {
            return a.script == v.script && a.element == v.element && a.endpoint == v.endpoint;
        });
    });
    if (m_Navigation.focus.pending && !focus) {
        m_Navigation.focus = {};
        m_FocusParents.clear();
    }
    if (m_FoundAddress && !found) {
        m_FoundAddress = {};
        m_FoundScript = 0;
        m_FoundRef = {};
    }
    PublishNavigation();
}
EditorStatus SchematicCanvas::Navigate(const EditorRequest &request) {
    if (!Client().Scope().document.value)
        return EDITOR_STALE;
    EditorField position{"position", EDITOR_RECORD};
    ObjectRef target{}, focus{};
    bool background = false, kind = false, reveal = false;
    for (uint32_t i = 0; i < request.count; ++i) {
        const auto &v = request.values[i];
        const std::string key(v.key.data, v.key.size);
        if (key == "target")
            target = v.object;
        else if (key == "focus-target") {
            focus = v.object;
            continue;
        } else if (key == "background") {
            background = v.integer != 0;
            continue;
        } else if (key == "reveal") {
            if (v.integer > 1)
                return EDITOR_INVALID;
            reveal = v.integer != 0;
            continue;
        }
        if (key == "kind")
            kind = v.integer != 0;
        std::vector<EditorField> copied;
        if (CopyEditorFields(copied, &v, 1) != EDITOR_OK)
            return EDITOR_INVALID;
        position.fields.push_back(std::move(copied.front()));
    }
    if (!target) {
        RefreshStack();
        return EDITOR_OK;
    }
    if (!kind)
        position.fields.clear();
    const auto owned =
        std::vector<EditorField>{{"target", EDITOR_OBJECT, target}, position, {"focus-target", EDITOR_OBJECT, focus}};
    const auto values = BorrowEditorFields(owned);
    const auto result = Client().Query("schematic.navigation-target", {values.data(), values.data() + values.size()});
    if (result.status != EDITOR_OK)
        return result.status;
    const auto *item = EditorClient::Find(result, "address"), *parents = EditorClient::Find(result, "parents");
    if (!item || !parents)
        return EDITOR_INVALID;
    ElementAddress address;
    if (!ReadSchematicPosition(*item, address))
        return EDITOR_INVALID;
    ApplyNavigation(address, parents->objects, !background, reveal);
    PublishNavigation();
    return EDITOR_OK;
}
void SchematicCanvas::PublishNavigation() {
    const auto version = std::make_tuple(Client().Scope().document.value, m_Topology, m_ScriptsRevision,
                                         m_Navigation.selection.Revision(), m_StackRevision);
    if (version == m_QueryVersion)
        return;
    m_QueryVersion = version;
    m_QuerySelection.clear();
    m_QueryScripts.clear();
    m_QueryScript = {};
    m_QueryElement = {};
    if (!Client().Scope().document.value)
        return;
    for (const auto &address : m_Navigation.selection.Addresses()) {
        const auto ref = address.object ? address.object : address.owner;
        if (ref)
            m_QuerySelection.push_back(ref);
    }
    m_QueryScripts = m_VisibleScripts;
    m_QueryScript = m_Navigation.stackScript;
    if (!m_Navigation.selection.items.empty())
        m_QueryElement = m_Navigation.selection.items.front();
}
EditorStatus SchematicCanvas::Inspect(EditorSink sink) {
    const EditorValue values[] = {
        {EDITOR_TEXT("selection"), EDITOR_OBJECT_LIST, {}, {}, 0, {m_QuerySelection.data(), uint32_t(m_QuerySelection.size())}},
        {EDITOR_TEXT("scripts"), EDITOR_OBJECT_LIST, {}, {}, 0, {m_QueryScripts.data(), uint32_t(m_QueryScripts.size())}},
        {EDITOR_TEXT("target"), EDITOR_OBJECT, m_QueryScript},
        {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(m_QueryElement.kind)},
        {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(m_QueryElement.index))},
        {EDITOR_TEXT("zoom"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, double(m_View.zoom)}};
    return sink.write ? sink.write(sink.data, values, uint32_t(std::size(values))) : EDITOR_INVALID;
}
EditorStatus SchematicCanvas::Save(EditorSink sink) const {
    const auto &o = m_Navigation.options;
    const uint64_t options = o.showLocalParams | (o.showLinkInfo << 1) | (o.showControlPoints << 2) | (o.showGrid << 3) |
        (o.showAllScripts << 4) | (o.showPriorities << 5) | (o.showHeaders << 6) | (o.sceneFilter << 7) |
        (o.zoomMode << 8) | (o.autoLink << 9);
    const EditorValue fields[] = {
        {EDITOR_TEXT("options"), EDITOR_INTEGER_VALUE, {}, {}, options},
        {EDITOR_TEXT("zoom"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, double(m_View.zoom)},
        {EDITOR_TEXT("pan-x"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, double(m_View.panX)},
        {EDITOR_TEXT("pan-y"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, double(m_View.panY)}};
    return sink.write ? sink.write(sink.data, fields, uint32_t(std::size(fields))) : EDITOR_INVALID;
}
EditorStatus SchematicCanvas::Restore(const EditorRequest &request) {
    auto options = m_Navigation.options;
    auto transform = m_View;
    for (uint32_t i = 0; i < request.count; ++i) {
        const auto &field = request.values[i];
        const std::string key(field.key.data, field.key.size);
        if (key == "options") {
            if (field.type != EDITOR_INTEGER_VALUE || field.integer > 1023) return EDITOR_INVALID;
            const auto v = field.integer;
            options.showLocalParams = v & 1; options.showLinkInfo = v & 2;
            options.showControlPoints = v & 4; options.showGrid = v & 8;
            options.showAllScripts = v & 16; options.showPriorities = v & 32;
            options.showHeaders = v & 64; options.sceneFilter = v & 128;
            options.zoomMode = v & 256; options.autoLink = v & 512;
        } else if (key == "zoom" || key == "pan-x" || key == "pan-y") {
            if (field.type != EDITOR_REAL_VALUE || !std::isfinite(field.real) || std::abs(field.real) > FLT_MAX)
                return EDITOR_INVALID;
            if (key == "zoom") transform.zoom = std::clamp(float(field.real), MIN_ZOOM, MAX_ZOOM);
            else if (key == "pan-x") transform.panX = float(field.real);
            else transform.panY = float(field.real);
        }
    }
    m_Navigation.options = options;
    m_View = transform;
    RefreshStack();
    m_ViewInitialized = true;
    return EDITOR_OK;
}
void SchematicCanvas::Draw(EditorViewFrame &frame) {
    EditorPrepareViewWindow(frame);
    bool open = true;
    Draw(frame.window.data, frame.closable ? &open : nullptr);
    PublishNavigation();
    frame.close = !open;
    frame.active = Busy();
}
} // namespace vdev
