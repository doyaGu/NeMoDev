#include "ui/schematic/SchematicCanvas.h"

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>

#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/TypeAhead.h"
#include "core/schematic/LinkRouting.h"
#include "core/level/SceneCommands.h"
#include "ui/schematic/SchematicCanvasDetail.h"
#include "core/graph/ShortcutCommands.h"
#include "core/schematic/SchematicEdits.h"
#include "core/schematic/SchematicGrid.h"
#include "core/schematic/SchematicHitTest.h"
#include "core/graph/OperationCommands.h"
#include "core/graph/ScriptCommands.h"
#include "ui/schematic/Snapshots.h"
#include "core/runtime/TextEncoding.h"
#include "core/graph/ValueCommands.h"

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
        const float left = m_State.view.showHeaders ? bounds.hPos + script.header.hSize : bounds.hPos;
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

bool SchematicCanvas::PrintSize(float &width, float &height) const {
    if (m_Views.empty()) return false;
    // Dev: the open Scripts' widths less 100, at least 140; the height of
    // the stack down to the last Script's bottom.
    width = 0.0f;
    for (const ScriptView &view : m_Views)
        if (view.model.script.open) width = std::max(width, view.model.script.bounds.hSize - 100.0f);
    if (width < 140.0f) width = 140.0f;
    height = m_Views.back().stackTop + m_Views.back().model.script.bounds.vSize - m_Views.front().stackTop;
    return true;
}

void SchematicCanvas::DrawPrintPage(ImDrawList *drawList, float x, float y, const ImVec2 &size, float scale,
                                    const SchematicPrint &print) const {
    const ImVec2 max(size.x * scale, size.y * scale);
    drawList->AddRectFilled(ImVec2(0.0f, 0.0f), max,
                            print.fillBody ? m_State.style.canvas : IM_COL32(255, 255, 255, 255));
    if (!m_Font || m_Views.empty()) return;
    DrawOptions draw;
    draw.style = &m_State.style;
    draw.font = m_Font;
    draw.boldFont = BoldFont();
    draw.fontSize = WORLD_FONT_SIZE;
    draw.showLocalParams = m_State.view.showLocalParams;
    draw.showLinkInfo = m_State.view.showLinkInfo;
    draw.showControlPoints = m_State.view.showControlPoints;
    draw.showPriorities = m_State.view.showPriorities;
    draw.showHeaders = m_State.view.showHeaders;
    draw.clipMin = ImVec2(0.0f, 0.0f);
    draw.clipMax = max;
    draw.print = &print;
    // UpdateTransforms at 100%, the stack's top at the page's y, unscrolled.
    ViewTransform page;
    page.origin = ImVec2(-x * scale, -y * scale);
    page.zoom = scale;
    page.panY = m_Views.front().stackTop;
    for (const ScriptView &view : m_Views) {
        ViewTransform body;
        body.origin = page.ToScreen(view.anchor.h, view.anchor.v + view.offsetY);
        body.zoom = scale * view.scale;
        body.panX = view.anchor.h;
        body.panY = view.anchor.v + view.offsetY;
        draw.scriptZoom = view.scale;
        DrawScript(drawList, view.model, view.offsetY, page, body, draw);
    }
}

SchematicCanvas::ScriptView *SchematicCanvas::BoundaryAt(const ImVec2 &screen) {
    for (auto &view : m_Views) {
        if (!view.model.script.open) continue;
        const Rect &bounds = view.model.script.bounds;
        const float bottom = view.frame.ToScreen(0.0f, bounds.Bottom() + view.offsetY).y;
        if (std::fabs(screen.y - bottom) <= 3.0f) return &view;
    }
    return nullptr;
}

void SchematicCanvas::SyncViews(ImFont *font) {
    if (!m_State.HasDocument()) {
        m_Views.clear();
        m_ViewInitialized = false; // the next composition starts at the top of its stack
        return;
    }
    Document &document = *m_State.document;
    // Hits kept across frames point into m_Views, which is rebuilt below:
    // re-point them by Script id afterwards.
    const CK_ID pressScript = m_Doc.press.view ? m_Doc.press.view->id : 0;
    const CK_ID menuScript = m_Doc.menuHit.view ? m_Doc.menuHit.view->id : 0;
    std::vector<ScriptView> next;
    for (CK_ID id : m_State.visibleScripts) {
        ScriptSession *session = document.Session(id);
        if (!session) continue;
        ScriptView view;
        if (ScriptView *existing = ViewFor(id)) view = std::move(*existing);
        view.id = id;
        if (view.revision != session->Revision() || view.topology != document.TopologyRevision() ||
            view.viewRevision != session->ViewRevision()) {
            const TextMeasure measure = [font](const std::string &text) {
                return font ? font->CalcTextSizeA(WORLD_FONT_SIZE, FLT_MAX, 0.0f, text.c_str()).x : 0.0f;
            };
            view.model.Build(*session, document.Context(), measure);
            // Dev.exe sub_4FB3A0, after its contents change: a Script grows to
            // what they need, and shrinks to it with Auto Resize Scripts. The
            // height belongs to the edit: undo restores the data before it,
            // and the contents then need what they did, so undoing back to
            // the save point leaves the document unmodified (TouchUndoable).
            const float content = view.model.script.contentHeight, stored = session->Data().header.vSize;
            const bool changed = view.revision != ~0ull && view.content > 0.0f && content != view.content;
            view.content = content;
            if (changed && view.model.script.open && stored > 0.0f && !session->ReadOnly() &&
                (content > stored || (content < stored && m_State.preferences.Bool("Schematic\\Auto Resize Scripts")))) {
                session->Data().header.vSize = content;
                session->TouchUndoable();
                view.model.Build(*session, document.Context(), measure);
            }
            CKBehavior *script = CKBehavior::Cast(document.Context()->GetObject(id));
            view.rootIsScript = script && (script->GetType() & CKBEHAVIORTYPE_SCRIPT);
            view.revision = session->Revision();
            view.topology = document.TopologyRevision();
            view.viewRevision = session->ViewRevision();
        }
        next.push_back(std::move(view));
    }
    m_Views = std::move(next);
    m_Doc.press.view = pressScript ? ViewFor(pressScript) : nullptr;
    m_Doc.menuHit.view = menuScript ? ViewFor(menuScript) : nullptr;
    // After the first ResetView (which sets every Script to 1).
    if (m_ViewInitialized)
        m_PendingZoom.erase(std::remove_if(m_PendingZoom.begin(), m_PendingZoom.end(),
                                       [this](const std::pair<CK_ID, float> &zoom) {
                                           ScriptView *view = ViewFor(zoom.first);
                                           if (!view) return false;
                                           view->scale = zoom.second;
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
    if (!m_State.view.showLocalParams && hit.ref.kind == ElementKind::LocalParam) {
        for (const auto &param : hit.view->model.params)
            if (param.ref == hit.ref && !IsParamShown(param, false)) hit.ref = ElementRef();
    } else if (!m_State.view.showLocalParams && hit.ref.kind == ElementKind::Link) {
        for (const auto &link : hit.view->model.links)
            if (link.ref == hit.ref && !IsLinkShown(link, false)) hit.ref = ElementRef();
    }
    return hit;
}

// Dev.exe sub_51E0C0 (message 0x607): the first of the class among the
// object a parameter under the mouse holds (assumed: sub_4D8D00 of a
// parameter element), the element's own object, its Script, the Script's
// owner.
CK_ID SchematicCanvas::PickObjectAt(const ImVec2 &mouse, CK_CLASSID cid) {
    if (!m_State.HasDocument() || ImGui::GetFrameCount() - m_PickFrame > 2 || mouse.x < m_PickMin.x ||
        mouse.y < m_PickMin.y || mouse.x >= m_PickMax.x || mouse.y >= m_PickMax.y)
        return 0;
    const Hit hit = HitAt(mouse);
    if (!hit.view) return 0;
    CKContext *context = m_State.document->Context();
    std::vector<CKObject *> candidates;
    CKObject *element = context->GetObject(hit.ref.object);
    CKParameter *parameter = nullptr;
    if (hit.ref.kind == ElementKind::LocalParam || hit.ref.kind == ElementKind::Shortcut) {
        parameter = CKParameter::Cast(element);
    } else if (hit.ref.kind == ElementKind::Pin) {
        CKBehavior *behavior = CKBehavior::Cast(element);
        CKParameterOperation *operation = CKParameterOperation::Cast(element);
        CKParameterIn *input = nullptr;
        if (hit.ref.pin == PinKind::ParamIn)
            input = behavior ? behavior->GetInputParameter(hit.ref.index)
                  : operation ? (hit.ref.index == 0 ? operation->GetInParameter1() : operation->GetInParameter2())
                              : nullptr;
        else if (hit.ref.pin == PinKind::Target && behavior)
            input = behavior->GetTargetParameter();
        if (input) parameter = input->GetRealSource();
        else if (hit.ref.pin == PinKind::ParamOut)
            parameter = behavior ? static_cast<CKParameter *>(behavior->GetOutputParameter(hit.ref.index))
                      : operation ? static_cast<CKParameter *>(operation->GetOutParameter())
                                  : nullptr;
    }
    // An object parameter (its type has a class).
    CKParameterManager *manager = context->GetParameterManager();
    if (parameter && manager->TypeToClassID(parameter->GetType()) != 0) {
        CK_ID held = 0;
        if (parameter->GetDataSize() == static_cast<int>(sizeof(held))) parameter->GetValue(&held, FALSE);
        candidates.push_back(context->GetObject(held));
    }
    candidates.push_back(element);
    CKBehavior *script = CKBehavior::Cast(context->GetObject(hit.view->id));
    candidates.push_back(script);
    if (script) candidates.push_back(script->GetOwner());
    for (CKObject *candidate : candidates)
        if (candidate && CKIsChildClassOf(candidate, cid)) return candidate->GetID();
    return 0;
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
    FocusRequest &focus = m_State.focus;
    if (!focus.pending) return;
    ScriptView *view = ViewFor(focus.script);
    if (!view) return; // the application adds the Script to the stack first
    focus.pending = false;
    Point center;
    ElementRef shown = focus.ref;
    bool found = FindCenter(view->model, shown, center);
    if (!found) {
        // Inside a folded graph: show the nearest visible enclosing behavior.
        ScriptSession *session = m_State.document->Session(focus.script);
        CK_ID id = focus.behavior;
        for (int guard = 0; !found && id && session && guard < 64; ++guard) {
            if (const BehaviorItem *behavior = view->model.FindBehavior(id)) {
                center = Center(behavior->box);
                shown = behavior->ref;
                found = true;
                break;
            }
            const BehaviorData *record = FindRecord(session->Data(), id);
            id = record ? record->parentId : 0;
        }
        if (!found) {
            center = Center(view->model.script.header);
            shown = view->model.script.ref;
        }
        m_State.Log(LogLevel::Info, "The element is inside a folded graph or a closed Script; showing its container");
    }
    if (shown.kind == ElementKind::Script) {
        // A Script: its band at the top-left of the view, as Dev scrolls to it.
        const Rect &bounds = view->model.script.bounds;
        const ImVec2 at = view->frame.ToScreen(bounds.hPos, bounds.vPos + view->offsetY);
        m_View.panX += (at.x - m_View.origin.x) / m_View.zoom;
        PanBy(0.0f, m_View.origin.y - at.y);
    } else {
        // Its band at the top of the view, the element centered in its body.
        const ImVec2 top = view->frame.ToScreen(0.0f, view->model.script.bounds.vPos + view->offsetY);
        PanBy(0.0f, m_View.origin.y - top.y);
        if (ScriptView *again = ViewFor(focus.script)) {
            // Horizontally centered by the shared scroll.
            const ImVec2 at = again->transform.ToScreen(center.h, center.v + again->offsetY);
            PanBy((again->bodyMin.x + m_View.origin.x + size.x) * 0.5f - at.x, 0.0f);
            view = again;
        }
    }
    UpdateTransforms();
    m_State.selection.Set(focus.script, shown);
}

void SchematicCanvas::ToggleFold(CK_ID script, CK_ID behavior) {
    ScriptSession *session = m_State.document->Session(script);
    const BehaviorData *record = session ? FindRecord(session->Data(), behavior) : nullptr;
    if (!record) return;
    const bool fold = !record->folded;
    EditScriptData(*m_State.document, script, fold ? "Collapse graph" : "Expand graph",
                   [&](InterfaceData &data) { SetBehaviorFolded(data, behavior, fold); });
}

void SchematicCanvas::ToggleScriptOpen(CK_ID script) {
    // View state, as Dev's open/close: not an edit, not saved.
    if (ScriptSession *session = m_State.document->Session(script)) session->SetOpen(!session->IsOpen());
}

void SchematicCanvas::AddComment(CK_ID script, const Point &local) {
    ScriptView *view = ViewFor(script);
    if (!view || !view->model.script.open) return;
    CK_ID graph = view->model.GraphAt(local);
    if (!graph) graph = script;
    int slot = -1;
    const bool added = EditScriptData(*m_State.document, script, "Add Comment", [&](InterfaceData &data) {
        BehaviorData *record = FindRecord(data, graph);
        if (!record) return;
        Comment comment;
        comment.hPos = SchematicGrid::SnapToGrid(local.h, SchematicGrid::SPACING);
        comment.vPos = SchematicGrid::SnapToGrid(local.v, SchematicGrid::SPACING);
        comment.width = 160.0f;
        comment.height = 60.0f;
        comment.text = "Comment";
        record->AddComment(comment);
        slot = static_cast<int>(record->comments.size()) - 1;
    });
    if (!added || slot < 0) return;
    m_Doc.commentScript = script;
    m_Doc.commentRef = {ElementKind::Comment, graph, 0, slot};
    m_CommentText = "Comment";
    m_Doc.openCommentEditor = true;
    m_State.selection.Set(script, m_Doc.commentRef);
}

void SchematicCanvas::DeleteSelection(bool graphsConfirmed) {
    Selection &selection = m_State.selection;
    if (selection.Empty() || !m_State.HasDocument()) return;
    // Dev.exe sub_50E430: behavior graphs (behaviors that are no Building
    // Block) are deleted once "Confirm Graph(es) Deletion" is answered.
    if (!graphsConfirmed)
        for (const auto &item : selection.items)
            if (item.kind == ElementKind::Behavior)
                if (CKBehavior *behavior = CKBehavior::Cast(m_State.document->Context()->GetObject(item.object)))
                    if (behavior->GetType() & CKBEHAVIORTYPE_BEHAVIOR) { // a graph (CKBEHAVIORTYPE_BASE is 0)
                        m_Doc.openDeleteGraphs = true;
                        return;
                    }
    if (selection.OnlyHeaders()) {
        m_Doc.deleteScripts = selection.Scripts();
        m_Doc.openDeleteScript = true;
        return;
    }
    bool displayOnly = true;
    for (const auto &item : selection.items)
        displayOnly = displayOnly && (item.kind == ElementKind::Comment || item.kind == ElementKind::ControlPoint);
    if (!displayOnly) {
        // Behaviors, links, operations and parameters: CK and display data.
        // Pins selected with other elements (rectangle selection, for
        // multi links) are not deleted with them.
        std::vector<ElementRef> items;
        bool onlyPins = true;
        for (const auto &item : selection.items) onlyPins = onlyPins && item.kind == ElementKind::Pin;
        for (const auto &item : selection.items)
            if (onlyPins || item.kind != ElementKind::Pin) items.push_back(item);
        Report(DeleteElements(*m_State.document, selection.script, items), "Delete");
        selection.Clear();
        return;
    }
    std::vector<ElementRef> comments, points;
    for (const auto &item : selection.items)
        (item.kind == ElementKind::Comment ? comments : points).push_back(item);
    // Remove higher slots first so lower slots stay valid.
    auto bySlot = [](const ElementRef &a, const ElementRef &b) {
        return a.owner != b.owner ? a.owner < b.owner : a.index != b.index ? a.index > b.index : a.sub > b.sub;
    };
    std::sort(comments.begin(), comments.end(), bySlot);
    std::sort(points.begin(), points.end(), bySlot);
    EditScriptData(*m_State.document, selection.script, "Delete", [&](InterfaceData &data) {
        for (const auto &ref : points) {
            BehaviorData *record = FindRecord(data, ref.owner);
            if (record && ref.index >= 0 && ref.index < static_cast<int>(record->links.size()))
                record->links[ref.index].RemoveControlPoint(ref.sub);
        }
        for (const auto &ref : comments) {
            BehaviorData *record = FindRecord(data, ref.owner);
            if (record && ref.index >= 0 && ref.index < static_cast<int>(record->comments.size()))
                record->comments.erase(record->comments.begin() + ref.index);
        }
    });
    selection.Clear();
}

void SchematicCanvas::Report(const EditResult &result, const char *what) {
    if (result.ok) {
        if (!result.message.empty()) m_State.Log(LogLevel::Info, AnsiToUtf8(result.message));
    } else if (!result.Unchanged()) { // nothing to do is not reported
        m_State.Log(LogLevel::Warning, std::string(what) + ": " + AnsiToUtf8(result.message));
    }
}

CK_ID SchematicCanvas::GraphAt(ScriptView &view, const Point &local) const {
    if (!view.model.script.open) return 0;
    const CK_ID graph = view.model.GraphAt(local);
    return graph ? graph : view.id;
}

void SchematicCanvas::ToggleTargetOfSelection() {
    const Selection selection = m_State.selection;
    if (!m_State.HasDocument()) return;
    bool any = false;
    for (const auto &item : selection.items) {
        if (item.kind != ElementKind::Behavior && item.kind != ElementKind::Script) continue;
        CKBehavior *behavior = CKBehavior::Cast(m_State.document->Context()->GetObject(item.object));
        if (!behavior) continue;
        any = true;
        Report(SetTargetUsed(*m_State.document, selection.script, item.object, !behavior->IsUsingTarget()),
               "Target");
    }
    if (!any) m_State.Log(LogLevel::Info, "Select a behavior to add or remove its Target parameter (T)");
}

void SchematicCanvas::BeginRename() {
    const Selection &selection = m_State.selection;
    if (selection.items.size() != 1 || !m_State.HasDocument()) return;
    const ElementRef &item = selection.items[0];
    CK_ID object = 0;
    switch (item.kind) {
    case ElementKind::Behavior:
    case ElementKind::Operation:
    case ElementKind::LocalParam:
    case ElementKind::Script:
        object = item.object;
        break;
    case ElementKind::Pin: {
        CKObject *owner = m_State.document->Context()->GetObject(item.object);
        if (CKBehavior *behavior = CKBehavior::Cast(owner)) {
            CKObject *pin = nullptr;
            switch (item.pin) {
            case PinKind::BehaviorIn: pin = behavior->GetInput(item.index); break;
            case PinKind::BehaviorOut: pin = behavior->GetOutput(item.index); break;
            case PinKind::ParamIn: pin = behavior->GetInputParameter(item.index); break;
            case PinKind::ParamOut: pin = behavior->GetOutputParameter(item.index); break;
            default: break;
            }
            object = pin ? pin->GetID() : 0;
        }
        break;
    }
    default:
        break;
    }
    CKObject *runtime = object ? m_State.document->Context()->GetObject(object) : nullptr;
    if (!runtime) return;
    m_Doc.dialogScript = selection.script;
    m_Doc.renameObject = object;
    m_RenameText = AnsiToUtf8(runtime->GetName() ? runtime->GetName() : "");
    m_Doc.openRename = true;
}

CK_ID SchematicCanvas::SelectedGraph() const {
    const Selection &selection = m_State.selection;
    if (selection.items.size() != 1 || selection.items[0].kind != ElementKind::Behavior) return 0;
    CKBehavior *behavior = CKBehavior::Cast(m_State.document->Context()->GetObject(selection.items[0].object));
    const bool editable = behavior && behavior->GetType() != CKBEHAVIORTYPE_BASE &&
                          !(behavior->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK);
    return editable ? behavior->GetID() : 0;
}

CK_ID SchematicCanvas::SelectedVariableBlock(PinKind kind) const {
    const Selection &selection = m_State.selection;
    if (selection.items.size() != 1 || selection.items[0].kind != ElementKind::Behavior || !m_State.HasDocument())
        return 0;
    CKBehavior *behavior = CKBehavior::Cast(m_State.document->Context()->GetObject(selection.items[0].object));
    return AcceptsVariablePin(behavior, kind) ? behavior->GetID() : 0;
}

void SchematicCanvas::AddGraphIo(bool input) {
    if (!m_State.HasDocument()) return;
    const PinKind kind = input ? PinKind::BehaviorIn : PinKind::BehaviorOut;
    if (const CK_ID block = SelectedVariableBlock(kind)) {
        Report(AddBuildingBlockPin(*m_State.document, m_State.selection.script, block, kind),
               input ? "Add behavior input" : "Add behavior output");
        return;
    }
    const CK_ID graph = SelectedGraph();
    if (!graph) {
        m_State.Log(LogLevel::Info, "Select a behavior graph to add a behavior input (I) or output (O)");
        return;
    }
    CKBehavior *behavior = CKBehavior::Cast(m_State.document->Context()->GetObject(graph));
    const std::string name = (input ? "In " : "Out ") +
                             std::to_string(input ? behavior->GetInputCount() : behavior->GetOutputCount());
    Report(AddGraphPin(*m_State.document, m_State.selection.script, graph,
                       input ? PinKind::BehaviorIn : PinKind::BehaviorOut, name),
           input ? "Add behavior input" : "Add behavior output");
}

void SchematicCanvas::OpenGraphParameterPicker(bool input) {
    if (!m_State.HasDocument()) return;
    CK_ID owner = SelectedGraph();
    if (!owner) owner = SelectedVariableBlock(input ? PinKind::ParamIn : PinKind::ParamOut);
    CK_ID script = m_State.selection.script;
    if (!owner && m_Doc.hoveredScript) owner = script = m_Doc.hoveredScript; // Script parameters
    if (!owner) {
        m_State.Log(LogLevel::Info, "Select a behavior graph, or point at a Script, to add a parameter");
        return;
    }
    m_Doc.dialogScript = script;
    m_Doc.pinOwner = owner;
    m_TypePurpose = input ? TypePurpose::GraphInput : TypePurpose::GraphOutput;
    if (m_Types.empty()) m_Types = ListParameterTypes(m_State.document->Context());
    m_PickerFilter.clear();
    m_LocalName = input ? "pIn" : "pOut";
    m_TypeIndex = -1;
    m_Doc.openLocalPicker = true;
}

void SchematicCanvas::CreateGraphAt(CK_ID script, const Point &local) {
    ScriptView *view = ViewFor(script);
    const CK_ID graph = view ? GraphAt(*view, local) : 0;
    if (!graph) return;
    const EditResult result =
        CreateBehaviorGraph(*m_State.document, script, graph, "Behavior Graph", local);
    Report(result, "Create behavior graph");
    if (result.ok) m_State.selection.Set(script, ElementRef{ElementKind::Behavior, graph, result.created});
}

void SchematicCanvas::OpenLocalParameterPicker(CK_ID script, const Point &local) {
    ScriptView *view = ViewFor(script);
    if (!view || !(m_Doc.dialogGraph = GraphAt(*view, local))) {
        m_State.Log(LogLevel::Info, "Open the Script to add a local parameter");
        return;
    }
    m_Doc.dialogScript = script;
    m_Doc.dialogPosition = local;
    m_TypePurpose = TypePurpose::Local;
    if (m_Types.empty()) m_Types = ListParameterTypes(m_State.document->Context());
    m_PickerFilter.clear();
    m_LocalName = "Local";
    m_TypeIndex = -1;
    m_Doc.openLocalPicker = true;
}

void SchematicCanvas::OpenOperationPicker(CK_ID script, const Point &local) {
    ScriptView *view = ViewFor(script);
    if (!view || !(m_Doc.dialogGraph = GraphAt(*view, local))) {
        m_State.Log(LogLevel::Info, "Open the Script to add a parameter operation");
        return;
    }
    m_Doc.dialogScript = script;
    m_Doc.dialogPosition = local;
    m_Doc.modifyOperation = 0;
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
        CKBehavior *script = CKBehavior::Cast(m_State.document->Context()->GetObject(hit.view->id));
        CKBehaviorIO *start = script && script->GetInputCount() > 0 ? script->GetInput(0) : nullptr;
        if (!start) return false;
        endpoint = LinkEndpoint(start->GetID(), 0, ENDPOINT_START_BIN);
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

void SchematicCanvas::EndLink(const Hit &hit) {
    m_Tool = Tool::Idle;
    const bool relink = m_Doc.relinkLink != 0;
    m_Doc.relinkLink = 0;
    if (!relink && MultiLink(hit)) return;
    LinkEndpoint to;
    bool behaviorLink = false;
    Point position;
    if (!hit.view || hit.view->id != m_Doc.linkScript || !EndpointOf(hit, to, behaviorLink, position)) return;
    if (behaviorLink != m_LinkBehavior) {
        m_State.Log(LogLevel::Warning, "Link: a behavior pin links to a behavior pin, a parameter to a parameter");
        return;
    }
    if (relink) {
        // Delete the old link and create the new one as one undo step.
        Edit edit(*m_State.document, "Move Link");
        const EditResult removed = DeleteElements(*m_State.document, m_Doc.linkScript, {m_Doc.relinkRef});
        if (!removed.ok) {
            Report(removed, "Move link");
            return;
        }
        const Preferences &preferences = m_State.preferences;
        const EditResult created =
            behaviorLink ? CreateBehaviorLink(*m_State.document, m_Doc.linkScript, m_Doc.linkFrom, to,
                                              preferences.defaultLinkDelay, preferences.autoLoopLinkDelay)
                         : CreateParameterLink(*m_State.document, m_Doc.linkScript, m_Doc.linkFrom, to);
        if (!created.ok) {
            Report(created, "Move link"); // the old link comes back with the edit
            return;
        }
        edit.Commit();
        m_State.selection.Clear();
        return;
    }
    const std::vector<Point> points = m_LinkPoints;
    m_LinkPoints.clear();
    const bool route = points.empty() && (m_State.preferences.searchBestPath != ImGui::GetIO().KeyCtrl);
    // A pIn that has a source: asked first.
    if (!behaviorLink) {
        CKParameterIn *input = InputOf(to) ? InputOf(to) : InputOf(m_Doc.linkFrom);
        if (input && (input->GetDirectSource() || input->GetSharedSource())) {
            m_ReplaceScript = m_Doc.linkScript;
            m_ReplaceFrom = m_Doc.linkFrom;
            m_ReplaceTo = to;
            m_ReplacePoints = points;
            m_ReplaceRoute = route;
            m_OpenReplaceSource = true;
            return;
        }
    }
    // The link and its best path (RouteNewLink) or the points clicked: one undo step.
    EditGroup group(*m_State.document, "Link");
    const EditResult result =
        behaviorLink ? CreateBehaviorLink(*m_State.document, m_Doc.linkScript, m_Doc.linkFrom, to,
                                          m_State.preferences.defaultLinkDelay, m_State.preferences.autoLoopLinkDelay)
                     : CreateParameterLink(*m_State.document, m_Doc.linkScript, m_Doc.linkFrom, to);
    if (!result.ok && !behaviorLink) {
        // Dev: incompatible types link through a parameter operation; one
        // candidate is used directly, several are offered.
        const auto conversions = ParameterLinkConversions(*m_State.document, m_Doc.linkScript, m_Doc.linkFrom, to);
        if (conversions.size() == 1) {
            const EditResult converted = CreateParameterLink(*m_State.document, m_Doc.linkScript, m_Doc.linkFrom,
                                                             to, &conversions.front());
            Report(converted, "Parameter operation link");
            return;
        }
        if (!conversions.empty()) {
            OpenOperationChoice(OperationChoice::NewLink, m_Doc.linkScript, m_Doc.linkFrom, to);
            return;
        }
    }
    Report(result, "Link");
    // Dev "Search Best Path Link" (Ctrl while linking inverts it): the new
    // link takes the best path, in the same undo step.
    if (result.ok && route) RouteNewLink(m_Doc.linkScript, m_Doc.linkFrom, to);
    if (result.ok && !points.empty()) SetNewLinkPoints(m_Doc.linkScript, m_Doc.linkFrom, to, points);
}

void SchematicCanvas::SetNewLinkPoints(CK_ID scriptId, const LinkEndpoint &from, const LinkEndpoint &to,
                                       const std::vector<Point> &points) {
    auto same = [](const LinkEndpoint &x, const LinkEndpoint &y) { return x.id == y.id && x.index == y.index && x.type == y.type; };
    EditScriptData(*m_State.document, scriptId, "Link", [&](InterfaceData &data) {
        auto visit = [&](BehaviorData &record) {
            for (Link &link : record.links) {
                if (!link.points.empty()) continue;
                if (same(link.start, from) && same(link.end, to)) {
                    link.points = points;
                    return true;
                }
                if (same(link.start, to) && same(link.end, from)) {
                    link.points.assign(points.rbegin(), points.rend());
                    return true;
                }
            }
            return false;
        };
        if (visit(data.rootBehavior)) return;
        for (BehaviorData &record : data.behaviors)
            if (visit(record)) return;
    });
}

CKParameterIn *SchematicCanvas::InputOf(const LinkEndpoint &endpoint) const {
    if (endpoint.type != ENDPOINT_PIN && endpoint.type != ENDPOINT_TARGET_PIN) return nullptr;
    CKObject *owner = m_State.document->Context()->GetObject(endpoint.id);
    if (CKBehavior *behavior = CKBehavior::Cast(owner))
        return endpoint.type == ENDPOINT_TARGET_PIN ? behavior->GetTargetParameter()
                                                    : endpoint.index < behavior->GetInputParameterCount()
                                                          ? behavior->GetInputParameter(endpoint.index)
                                                          : nullptr;
    if (CKParameterOperation *operation = CKParameterOperation::Cast(owner))
        return endpoint.index == 0 ? operation->GetInParameter1() : endpoint.index == 1 ? operation->GetInParameter2() : nullptr;
    return nullptr;
}

void SchematicCanvas::CreateParameterLinkReplacing(CK_ID scriptId, const LinkEndpoint &from, const LinkEndpoint &to,
                                                   const std::vector<Point> &points, bool route) {
    CKParameterIn *input = InputOf(to) ? InputOf(to) : InputOf(from);
    if (!input) return;
    // A local that was the source of this input alone goes with the link.
    CKParameterLocal *local = CKParameterLocal::Cast(input->GetDirectSource());
    ElementRef localRef;
    if (local) {
        int uses = 0;
        CKBehavior *graph = CKBehavior::Cast(local->GetOwner());
        auto count = [&](CKBehavior *behavior) {
            for (int i = 0; behavior && i < behavior->GetInputParameterCount(); ++i)
                if (behavior->GetInputParameter(i) && behavior->GetInputParameter(i)->GetDirectSource() == local) ++uses;
            if (behavior && behavior->GetTargetParameter() && behavior->GetTargetParameter()->GetDirectSource() == local) ++uses;
        };
        if (graph) {
            for (int i = 0; i < graph->GetSubBehaviorCount(); ++i) count(graph->GetSubBehavior(i));
            for (int i = 0; i < graph->GetParameterOperationCount(); ++i)
                if (CKParameterOperation *operation = graph->GetParameterOperation(i)) {
                    if (operation->GetInParameter1() && operation->GetInParameter1()->GetDirectSource() == local) ++uses;
                    if (operation->GetInParameter2() && operation->GetInParameter2()->GetDirectSource() == local) ++uses;
                }
        }
        if (uses == 1)
            if (const ScriptView *view = ViewFor(scriptId))
                for (const auto &param : view->model.params)
                    if (param.ref.kind == ElementKind::LocalParam && param.ref.object == local->GetID()) localRef = param.ref;
    }
    EditGroup group(*m_State.document, "Link");
    const EditResult result = CreateParameterLink(*m_State.document, scriptId, from, to);
    Report(result, "Link");
    if (!result.ok) return;
    if (localRef.IsValid()) Report(DeleteElements(*m_State.document, scriptId, {localRef}), "Delete");
    if (route) RouteNewLink(scriptId, from, to);
    if (!points.empty()) SetNewLinkPoints(scriptId, from, to, points);
}

// "There is already an existing link on a pIn": delete the previous link
// (and its local), or cancel.
void SchematicCanvas::ReplaceSourceDialog() {
    const char *id = "Link###ReplaceSource";
    if (m_OpenReplaceSource) {
        m_OpenReplaceSource = false;
        ImGui::OpenPopup(id);
    }
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    CKParameterIn *input = m_State.HasDocument() ? (InputOf(m_ReplaceTo) ? InputOf(m_ReplaceTo) : InputOf(m_ReplaceFrom)) : nullptr;
    ImGui::Text("The parameter input <%s> is already linked.", input && input->GetName() ? AnsiToUtf8(input->GetName()).c_str() : "");
    ImGui::TextUnformatted("Delete the previous link?");
    if (ImGui::Button("Delete Link", ImVec2(110, 0))) {
        ImGui::CloseCurrentPopup();
        if (input) CreateParameterLinkReplacing(m_ReplaceScript, m_ReplaceFrom, m_ReplaceTo, m_ReplacePoints, m_ReplaceRoute);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void SchematicCanvas::RouteNewLink(CK_ID scriptId, const LinkEndpoint &a, const LinkEndpoint &b) {
    auto same = [](const LinkEndpoint &x, const LinkEndpoint &y) {
        return x.id == y.id && x.index == y.index && x.type == y.type;
    };
    ScriptSession *session = m_State.document->Session(scriptId);
    if (!session) return;
    CKBehavior *script = CKBehavior::Cast(m_State.document->Context()->GetObject(scriptId));
    // Route on a copy first; commit only a found path.
    InterfaceData copy = session->Data();
    copy.observers.clear();
    CK_ID owner = 0;
    int slot = -1;
    std::vector<Point> points;
    auto visit = [&](BehaviorData &record) {
        for (int i = 0; i < static_cast<int>(record.links.size()) && slot < 0; ++i) {
            Link &link = record.links[i];
            if (!link.points.empty() ||
                !((same(link.start, a) && same(link.end, b)) || (same(link.start, b) && same(link.end, a))))
                continue;
            if (RouteSingleLink(copy, record, link, script) && !link.points.empty()) {
                owner = record.id;
                slot = i;
                points = link.points;
            }
        }
    };
    visit(copy.rootBehavior);
    for (auto &record : copy.behaviors) visit(record);
    if (slot < 0) return;
    // In the link's undo step (EndLink's group).
    EditScriptData(*m_State.document, scriptId, "Link", [&](InterfaceData &data) {
        if (BehaviorData *record = FindRecord(data, owner))
            if (slot < static_cast<int>(record->links.size())) record->links[slot].points = points;
    });
}

void SchematicCanvas::AcceptBuildingBlockDrop() {
    if (!ImGui::BeginDragDropTarget()) return;
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(BUILDING_BLOCK_PAYLOAD)) {
        CKGUID guid;
        memcpy(&guid, payload->Data, sizeof(guid));
        const Hit hit = HitAt(ImGui::GetIO().MousePos);
        const CK_ID graph = hit.view ? GraphAt(*hit.view, hit.local) : 0;
        if (IsBuildingBlockAt(hit)) {
            // Dev: a Building Block dropped on another replaces it.
            OpenReplaceBlock(hit.view->id, hit.ref.object, guid);
        } else if (!graph) {
            m_State.Log(LogLevel::Info, "Drop Building Blocks into an open Script or an expanded graph");
        } else {
            const CK_ID script = hit.view->id;
            // Several Building Blocks dragged: one under the other, all selected.
            const std::vector<CKGUID> blocks = m_State.DraggedBlocks(guid);
            m_State.selection.Clear();
            for (size_t i = 0; i < blocks.size(); ++i) {
                Point at = hit.local;
                at.v += 60.0f * static_cast<float>(i);
                const EditResult result = InsertBuildingBlock(*m_State.document, script, graph, blocks[i], at);
                Report(result, "Insert Building Block");
                if (result.ok) {
                    m_State.RememberBlock(blocks[i]);
                    m_State.selection.Add(script, ElementRef{ElementKind::Behavior, graph, result.created});
                }
            }
        }
    }
    // A Data Resources file: as a file dropped from Explorer (behavior graph
    // files on graphs or bodies, images on Script headers).
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(RESOURCE_FILE_PAYLOAD)) {
        ResourceFilePayload file;
        memcpy(&file, payload->Data, sizeof(file));
        for (const ResourceFilePayload &each : m_State.DraggedResources(file))
            if (!DropFile(Utf8ToWide(each.path), each.path, ImGui::GetIO().MousePos))
                m_State.Log(LogLevel::Info, "Drop behavior graph files on a graph or a Script, images on a Script header");
    }
    // A 3D Layout snapshot (Dev.exe sub_53BF70, "Bitmap"): the Script's snapshot.
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(SNAPSHOT_PAYLOAD)) {
        int slot = -1;
        memcpy(&slot, payload->Data, sizeof(slot));
        const Hit hit = HitAt(ImGui::GetIO().MousePos);
        if (!hit.view || !m_State.HasDocument() || !m_State.snapshots.Has(slot)) {
            m_State.Log(LogLevel::Info, "Drop snapshots on a Script");
        } else {
            // Scaled into the header's snapshot place (48 x 36) as Load Snapshot does: 47 x 36.
            const int height = 36;
            const int width = std::min(48, (SnapshotBuffer::WIDTH * height + SnapshotBuffer::HEIGHT / 2) / SnapshotBuffer::HEIGHT);
            const std::vector<unsigned char> pixels = ShrinkImage(m_State.snapshots.Pixels(slot).data(), SnapshotBuffer::WIDTH,
                                                                  SnapshotBuffer::HEIGHT, SnapshotBuffer::WIDTH * 4, width, height);
            if (void *bitmap = BitmapFromPixels(pixels.data(), width, height)) {
                m_State.document->KeepBitmap(bitmap);
                if (!SetScriptSnapshot(*m_State.document, hit.view->id, bitmap))
                    m_State.Log(LogLevel::Warning, "Snapshot: the Script's display is read-only");
            }
        }
    }
    ImGui::EndDragDropTarget();
}

void SchematicCanvas::Copy() {
    const Selection &selection = m_State.selection;
    ScriptSession *session = m_State.HasDocument() ? m_State.document->Session(selection.script) : nullptr;
    if (!session || selection.Empty()) return;
    Clipboard clipboard;
    clipboard.data = session->Data();
    clipboard.data.observers.clear();
    for (const auto &item : selection.items) {
        if (item.kind == ElementKind::Behavior) clipboard.behaviors.push_back(item.object);
        if (item.kind == ElementKind::Operation && item.object) clipboard.operations.push_back(item.object);
        if ((item.kind == ElementKind::LocalParam || item.kind == ElementKind::Shortcut) && item.object)
            clipboard.parameters.push_back(item.object);
        if (item.kind == ElementKind::Pin && item.pin == PinKind::ParamOut) {
            CKObject *owner = m_State.document->Context()->GetObject(item.object);
            CKParameter *output = CKBehavior::Cast(owner) ? static_cast<CKParameter *>(
                                                               CKBehavior::Cast(owner)->GetOutputParameter(item.index))
                                  : CKParameterOperation::Cast(owner)
                                      ? static_cast<CKParameter *>(CKParameterOperation::Cast(owner)->GetOutParameter())
                                      : nullptr;
            if (output) clipboard.parameters.push_back(output->GetID());
        }
        if (item.kind == ElementKind::Comment) {
            const BehaviorData *record = FindRecord(clipboard.data, item.owner);
            if (record && item.index >= 0 && item.index < static_cast<int>(record->comments.size()))
                clipboard.comments.push_back(record->comments[item.index]);
        }
    }
    clipboard.valid = !clipboard.behaviors.empty() || !clipboard.comments.empty() || !clipboard.parameters.empty() ||
                      !clipboard.operations.empty();
    if (!clipboard.valid) {
        m_State.Log(LogLevel::Info, "Copy: select behaviors, Comments or parameters");
        return;
    }
    m_State.clipboard = std::move(clipboard);
    m_State.Log(LogLevel::Info, "Copied " + std::to_string(m_State.clipboard.behaviors.size()) + " behaviors, " +
                                    std::to_string(m_State.clipboard.comments.size()) + " Comments");
}

void SchematicCanvas::PasteShortcuts() {
    const Clipboard &clipboard = m_State.clipboard;
    ScriptView *view = ViewFor(m_Doc.mouseScript);
    const CK_ID graph = view ? GraphAt(*view, m_Doc.mouseLocal) : 0;
    if (!clipboard.valid || clipboard.parameters.empty() || !graph) {
        m_State.Log(LogLevel::Info,
                    "Paste shortcut: copy local parameters, outputs or attribute values, then point at an open graph");
        return;
    }
    // A shortcut displays a parameter of another graph (sharedParams); no CK change.
    const Point at = m_Doc.mouseLocal;
    EditScriptData(*m_State.document, m_Doc.mouseScript, "Paste Shortcut", [&](InterfaceData &data) {
        BehaviorData *record = FindRecord(data, graph);
        if (!record) return;
        int row = 0;
        for (CK_ID id : clipboard.parameters) {
            Parameter shortcut(id, static_cast<int>(std::floor((at.h - record->rect.hPos) / SchematicGrid::SPACING)),
                               static_cast<int>(std::floor((at.v - record->rect.vPos) / SchematicGrid::SPACING)) + row++,
                               PARAM_STYLE_NAME);
            shortcut.sourceId = id;
            if (record == &data.rootBehavior && view->rootIsScript) {
                shortcut.hPos = std::max(shortcut.hPos, 9);
                shortcut.vPos = std::max(shortcut.vPos, 0);
            }
            record->sharedParams.push_back(shortcut);
        }
    });
}

void SchematicCanvas::GroupSelection() {
    if (!m_State.HasDocument()) return;
    std::vector<CK_ID> behaviors;
    for (const auto &item : m_State.selection.items)
        if (item.kind == ElementKind::Behavior) behaviors.push_back(item.object);
    if (behaviors.empty()) {
        if (m_Doc.mouseScript) CreateGraphAt(m_Doc.mouseScript, m_Doc.mouseLocal);
        return;
    }
    const CK_ID script = m_State.selection.script;
    const EditResult result = GroupBehaviors(*m_State.document, script, behaviors);
    Report(result, "Create behavior graph");
    if (result.ok) m_State.selection.Set(script, ElementRef{ElementKind::Behavior, 0, result.created});
}

void SchematicCanvas::UngroupSelection() {
    if (!m_State.HasDocument()) return;
    const Selection selection = m_State.selection;
    bool any = false;
    for (const auto &item : selection.items)
        if (item.kind == ElementKind::Behavior) {
            any = true;
            Report(UngroupGraph(*m_State.document, selection.script, item.object), "Delete graph");
        }
    if (!any) m_State.Log(LogLevel::Info, "Shift+Del: select behavior graphs to remove while keeping their contents");
    m_State.selection.Clear();
}

void SchematicCanvas::Cut() {
    Copy();
    if (m_State.clipboard.valid) DeleteSelection();
}

void SchematicCanvas::Paste() {
    const Clipboard &clipboard = m_State.clipboard;
    if (!clipboard.valid || !m_State.HasDocument()) return;
    if (clipboard.behaviors.empty() && clipboard.comments.empty()) {
        // Parameters alone (documentation "Local Parameter"): new locals of
        // their name, type and value; operations: new ones of theirs.
        PasteParameters();
        return;
    }
    CK_ID script = m_Doc.mouseScript;
    Point at = m_Doc.mouseLocal;
    if (!script && !m_Views.empty()) {
        script = m_Views.front().id;
        at = Point(m_Views.front().model.script.bounds.hPos + 200.0f, m_Views.front().model.script.bounds.vPos + 40.0f);
    }
    ScriptView *view = ViewFor(script);
    const CK_ID graph = view ? GraphAt(*view, at) : 0;
    if (!graph) {
        m_State.Log(LogLevel::Info, "Paste: point at an open Script or an expanded graph");
        return;
    }
    const EditResult result = PasteBehaviors(*m_State.document, clipboard.data, clipboard.behaviors,
                                             clipboard.comments, script, graph, at);
    Report(result, "Paste");
    if (result.ok && result.created) m_State.selection.Set(script, ElementRef{ElementKind::Behavior, graph, result.created});
}

void SchematicCanvas::PasteParameters() {
    const Clipboard &clipboard = m_State.clipboard;
    ScriptView *view = ViewFor(m_Doc.mouseScript);
    const CK_ID graph = view ? GraphAt(*view, m_Doc.mouseLocal) : 0;
    if (!graph) {
        m_State.Log(LogLevel::Info, "Paste: point at an open Script or an expanded graph");
        return;
    }
    // One under the other from the mouse.
    Point at = m_Doc.mouseLocal;
    std::vector<std::pair<CK_ID, Point>> locals, operations;
    for (CK_ID id : clipboard.parameters) {
        locals.emplace_back(id, at);
        at.v += SchematicGrid::SPACING * 2.0f;
    }
    for (CK_ID id : clipboard.operations) {
        operations.emplace_back(id, at);
        at.v += SchematicGrid::SPACING * 4.0f;
    }
    CopyParametersTo(m_Doc.mouseScript, locals, operations, "Paste");
}

void SchematicCanvas::CopyParametersTo(CK_ID script, const std::vector<std::pair<CK_ID, Point>> &locals,
                                       const std::vector<std::pair<CK_ID, Point>> &operations, const char *label) {
    ScriptView *view = ViewFor(script);
    if (!view || (locals.empty() && operations.empty())) return;
    CKContext *context = m_State.document->Context();
    EditGroup group(*m_State.document, label);
    for (const auto &local : locals) {
        CKParameter *source = CKParameter::Cast(context->GetObject(local.first));
        const CK_ID graph = GraphAt(*view, local.second);
        if (!source || !graph) continue;
        const EditResult made = AddLocalParameter(*m_State.document, script, graph, source->GetName() ? source->GetName() : "",
                                                  source->GetGUID(), local.second);
        Report(made, label);
        if (!made.ok) continue;
        const std::vector<unsigned char> bytes = ParameterBytes(source);
        if (!bytes.empty()) SetParameterBytes(*m_State.document, script, made.created, bytes);
    }
    for (const auto &operation : operations) {
        CKParameterOperation *source = CKParameterOperation::Cast(context->GetObject(operation.first));
        const CK_ID graph = GraphAt(*view, operation.second);
        if (!source || !source->GetOutParameter() || !graph) continue;
        const CKGUID none(0, 0);
        const EditResult made = AddParameterOperation(
            *m_State.document, script, graph, source->GetOperationGuid(), source->GetOutParameter()->GetGUID(),
            source->GetInParameter1() ? source->GetInParameter1()->GetGUID() : none,
            source->GetInParameter2() ? source->GetInParameter2()->GetGUID() : none, operation.second);
        Report(made, label);
    }
}

void SchematicCanvas::Duplicate() {
    const Selection &selection = m_State.selection;
    ScriptView *view = ViewFor(selection.script);
    if (!view || selection.Empty()) return;
    // Next to the selection: one grid step plus its own offset (library choice).
    float left = FLT_MAX, top = FLT_MAX;
    for (const auto &item : selection.items)
        if (const BehaviorItem *behavior = item.kind == ElementKind::Behavior ? view->model.FindBehavior(item.object) : nullptr) {
            left = std::min(left, behavior->box.hPos);
            top = std::min(top, behavior->box.vPos);
        }
    if (left == FLT_MAX) return;
    Copy();
    m_Doc.mouseScript = selection.script;
    m_Doc.mouseLocal = Point(left + 40.0f, top + 40.0f);
    Paste();
}

void SchematicCanvas::Refresh() {
    if (!m_State.HasDocument()) return;
    m_State.document->RefreshScripts();
    Invalidate();
    m_State.Log(LogLevel::Info, "Schematic refreshed");
}

// Dev's Edit Parameter (Dev.exe sub_523480 opens dialog 354): the name and
// the type (Dev's default; not of <This>), the value of a local or of an
// input's value holder; other values are only shown.
void SchematicCanvas::OpenValueDialog(CK_ID script, CKObject *parameter) {
    if (!parameter || !m_State.HasDocument()) return;
    m_Doc.dialogScript = script;
    m_Doc.valueParameter = parameter->GetID();
    m_Doc.invalidLinks.clear();
    m_InvalidLinksAnswered = false;
    m_ValueName = AnsiToUtf8(parameter->GetName() ? parameter->GetName() : "");
    CKParameterIn *input = CKParameterIn::Cast(parameter);
    CKParameter *value = CKParameter::Cast(parameter);
    m_ValueType = input ? input->GetGUID() : value ? value->GetGUID() : CKGUID();
    CKParameterLocal *holder = input ? EditableValueOf(input) : CKParameterLocal::Cast(parameter);
    const bool self = holder && holder->IsMyselfParameter();
    m_Doc.valueHolder = holder && !self ? holder->GetID() : 0;
    m_ValueNameEditable = !self;
    // Dev.exe sub_523480 leaves the type editable (Drive 1) but for one
    // Building Block window case; an operation's pins keep theirs.
    const bool operation = (input && CKParameterOperation::Cast(input->GetOwner())) ||
                           (CKParameterOut::Cast(parameter) && CKParameterOperation::Cast(CKParameterOut::Cast(parameter)->GetOwner()));
    m_ValueTypeEditable = !self && !operation;
    m_ParameterBox.Clear();
    if (m_Doc.valueHolder) m_ParameterBox.Add(holder, parameter->GetName() ? parameter->GetName() : "");
    m_Doc.openValue = true;
}

void SchematicCanvas::OpenParametersDialog() {
    const Selection &selection = m_State.selection;
    if (selection.items.size() != 1 || !m_State.HasDocument()) return;
    const ElementRef &item = selection.items[0];
    if (item.kind != ElementKind::Behavior && item.kind != ElementKind::Operation) return;
    m_Doc.dialogScript = selection.script;
    m_Doc.parametersObject = item.object;
    // Dev.exe sub_582930: a row per input (the Target first) whose value
    // holder is a local parameter (not <This>, the input not disabled),
    // named after the local; a Building Block's title is its path from the
    // Script ("Edit Parameters: Script / ... / Block", OK, Apply, Cancel),
    // an operation's "%s - Edit Parameters" (OK, Cancel).
    CKContext *context = m_State.document->Context();
    CKObject *object = context->GetObject(item.object);
    CKBehavior *behavior = CKBehavior::Cast(object);
    CKParameterOperation *operation = CKParameterOperation::Cast(object);
    m_ParameterBox.Clear();
    m_ParameterBox.AddInputs(object);
    if (behavior) {
        std::vector<CKBehavior *> path;
        for (CKBehavior *b = behavior; b; b = b->GetParent()) path.insert(path.begin(), b);
        m_ParametersTitle = "Edit Parameters: ";
        for (size_t i = 0; i < path.size(); ++i)
            m_ParametersTitle += (i ? " / " : "") + AnsiToUtf8(path[i]->GetName() ? path[i]->GetName() : "");
        m_ParametersApply = true;
    } else if (operation) {
        m_ParametersTitle = AnsiToUtf8(operation->GetName() ? operation->GetName() : "") + " - Edit Parameters";
        m_ParametersApply = false;
    }
    if (m_ParameterBox.Empty()) {
        m_State.Log(LogLevel::Info, "No parameter to edit (the inputs are linked)");
        return;
    }
    m_Doc.openParameters = true;
}

void SchematicCanvas::OpenParametersOf(CK_ID script, CK_ID object) {
    CKObject *found = m_State.HasDocument() ? m_State.document->Context()->GetObject(object) : nullptr;
    if (!found) return;
    ElementRef ref;
    ref.kind = CKParameterOperation::Cast(found) ? ElementKind::Operation : ElementKind::Behavior;
    ref.object = object;
    m_State.selection.Set(script, ref);
    OpenParametersDialog();
}

void SchematicCanvas::OpenPriorityDialog() {
    const Selection &selection = m_State.selection;
    if (!m_State.HasDocument()) return;
    m_PriorityScripts.clear();
    // Script headers (documentation "Script Header": Edit Priority edits the
    // Script's priority), one or several.
    if (selection.OnlyHeaders()) {
        m_PriorityScripts = selection.Scripts();
        CKBehavior *first = m_PriorityScripts.empty() ? nullptr
                                                      : CKBehavior::Cast(m_State.document->Context()->GetObject(m_PriorityScripts.front()));
        if (first) {
            m_PriorityValue = first->GetPriority();
            m_OpenPriority = true;
            return;
        }
        m_PriorityScripts.clear();
    }
    for (const auto &item : selection.items) {
        if (item.kind != ElementKind::Behavior) continue;
        CKBehavior *behavior = CKBehavior::Cast(m_State.document->Context()->GetObject(item.object));
        if (!behavior) continue;
        m_Doc.dialogScript = selection.script;
        m_PriorityValue = behavior->GetPriority();
        m_OpenPriority = true;
        return;
    }
    m_State.Log(LogLevel::Info, "Select behaviors to edit their priority (Y)");
}

void SchematicCanvas::InsertBlockAt(const CKGUID &guid, CK_ID script, const Point &local) {
    ScriptView *view = ViewFor(script);
    const CK_ID graph = view ? GraphAt(*view, local) : 0;
    if (!graph) {
        m_State.Log(LogLevel::Info, "Insert Building Blocks into an open Script or an expanded graph");
        return;
    }
    const EditResult result = InsertBuildingBlock(*m_State.document, script, graph, guid, local);
    Report(result, "Insert Building Block");
    if (!result.ok) return;
    m_State.RememberBlock(guid);
    m_State.selection.Set(script, ElementRef{ElementKind::Behavior, graph, result.created});
}

void SchematicCanvas::BeginCopyDrag() {
    const Selection &selection = m_State.selection;
    ScriptView *view = ViewFor(selection.script);
    ScriptSession *session = m_State.document->Session(selection.script);
    if (!view || !session) return;
    m_Doc.copyBehaviors.clear();
    m_Doc.copyComments.clear();
    m_Doc.copyLocals.clear();
    m_Doc.copyOperations.clear();
    float left = FLT_MAX, top = FLT_MAX, right = -FLT_MAX, bottom = -FLT_MAX;
    auto grow = [&](const Rect &r) {
        left = std::min(left, r.hPos);
        top = std::min(top, r.vPos);
        right = std::max(right, r.Right());
        bottom = std::max(bottom, r.Bottom());
    };
    for (const auto &item : selection.items) {
        if (item.kind == ElementKind::Behavior) {
            if (const BehaviorItem *behavior = view->model.FindBehavior(item.object)) {
                m_Doc.copyBehaviors.push_back(item.object);
                grow(behavior->box);
            }
        } else if ((item.kind == ElementKind::LocalParam || item.kind == ElementKind::Shortcut) && item.object) {
            // Documentation "Schematic Keyboard Shortcuts": the selection
            // dragged with Shift copied, its parameters too.
            for (const ParamItem &param : view->model.params)
                if (param.ref == item) {
                    m_Doc.copyLocals.emplace_back(item.object, Point(param.bounds.hPos, param.bounds.vPos));
                    grow(param.bounds);
                    break;
                }
        } else if (item.kind == ElementKind::Operation && item.object) {
            for (const OperationItem &operation : view->model.operations)
                if (operation.ref == item) {
                    m_Doc.copyOperations.emplace_back(item.object, Point(operation.bounds.hPos, operation.bounds.vPos));
                    grow(operation.bounds);
                    break;
                }
        } else if (item.kind == ElementKind::Comment) {
            const BehaviorData *record = FindRecord(session->Data(), item.owner);
            if (record && item.index >= 0 && item.index < static_cast<int>(record->comments.size())) {
                m_Doc.copyComments.push_back(record->comments[item.index]);
                grow(record->comments[item.index].GetRect());
            }
        }
    }
    if (m_Doc.copyBehaviors.empty() && m_Doc.copyComments.empty() && m_Doc.copyLocals.empty() && m_Doc.copyOperations.empty())
        return;
    m_Doc.copySource = session->Data();
    m_Doc.copySource.observers.clear();
    m_Doc.copyScript = selection.script;
    m_Doc.copyBounds = Rect(left, top, right - left, bottom - top);
    m_Tool = Tool::CopyDrag;
}

void SchematicCanvas::EndCopyDrag(const Point &world) {
    m_Tool = Tool::Idle;
    ScriptView *view = ViewFor(m_Doc.copyScript);
    if (!view) return;
    Point to(m_Doc.copyBounds.hPos + world.h - m_PressWorld.h, m_Doc.copyBounds.vPos + world.v - m_PressWorld.v);
    // Released over another Script (documentation "Script": "drag the
    // selection into another script"): the copy goes there.
    CK_ID into = m_Doc.copyScript;
    const Hit target = HitAt(ImGui::GetIO().MousePos);
    if (target.view && target.view->id != m_Doc.copyScript) {
        const Point press(m_PressWorld.h, m_PressWorld.v - view->offsetY);
        to = Point(m_Doc.copyBounds.hPos + target.local.h - press.h, m_Doc.copyBounds.vPos + target.local.v - press.v);
        into = target.view->id;
        view = target.view;
    }
    EditGroup group(*m_State.document, "Copy");
    // The parameters and operations moved as the rest of the selection.
    const float dh = to.h - m_Doc.copyBounds.hPos, dv = to.v - m_Doc.copyBounds.vPos;
    auto moved = [&](const std::vector<std::pair<CK_ID, Point>> &items) {
        std::vector<std::pair<CK_ID, Point>> placed;
        for (const auto &item : items) placed.emplace_back(item.first, Point(item.second.h + dh, item.second.v + dv));
        return placed;
    };
    CopyParametersTo(into, moved(m_Doc.copyLocals), moved(m_Doc.copyOperations), "Copy");
    if (m_Doc.copyBehaviors.empty() && m_Doc.copyComments.empty()) return;
    const CK_ID graph = GraphAt(*view, Point(to.h + 1.0f, to.v + 1.0f));
    if (!graph) return;
    const EditResult result = PasteBehaviors(*m_State.document, m_Doc.copySource, m_Doc.copyBehaviors,
                                             m_Doc.copyComments, into, graph, to);
    Report(result, "Copy");
    if (result.ok && result.created) m_State.selection.Set(into, ElementRef{ElementKind::Behavior, graph, result.created});
}

void SchematicCanvas::SelectAll() {
    CK_ID script = m_State.selection.script;
    if (!script && !m_Views.empty()) script = m_Views.front().id;
    ScriptView *view = ViewFor(script);
    if (!view) return;
    m_State.selection.Clear();
    const SchematicModel &model = view->model;
    for (const auto &behavior : model.behaviors) m_State.selection.Add(script, behavior.ref);
    for (const auto &operation : model.operations) m_State.selection.Add(script, operation.ref);
    for (const auto &param : model.params)
        if (IsParamShown(param, m_State.view.showLocalParams)) m_State.selection.Add(script, param.ref);
    for (const auto &comment : model.comments) m_State.selection.Add(script, comment.ref);
}

void SchematicCanvas::Click(const Hit &hit) {
    const ImGuiIO &io = ImGui::GetIO();
    if (!hit.view || !hit.ref.IsValid()) {
        if (!io.KeyCtrl && !io.KeyShift) m_State.selection.Clear();
        return;
    }
    // Dev: a click on the header row (triangle and name) opens or closes the Script.
    if (hit.ref.kind == ElementKind::Script && hit.onHeader && !io.KeyCtrl && !io.KeyShift &&
        hit.local.v < hit.view->model.script.header.vPos + CLOSED_SCRIPT_HEIGHT) {
        ToggleScriptOpen(hit.view->id);
        return;
    }
    if (io.KeyCtrl) m_State.selection.Toggle(hit.view->id, hit.ref);
    else if (io.KeyShift) m_State.selection.Add(hit.view->id, hit.ref);
    else m_State.selection.Set(hit.view->id, hit.ref);
}

void SchematicCanvas::DoubleClick(const Hit &hit) {
    if (!hit.view) return;
    if (hit.ref.kind == ElementKind::Script && hit.onHeader && hit.view->model.script.open &&
        hit.local.v >= hit.view->model.script.header.vPos + CLOSED_SCRIPT_HEIGHT &&
        hit.local.v < hit.view->model.script.header.vPos + 36.0f) {
        // Dev: double-click on the Script owner shows it in the Level Manager.
        CKBehavior *script = CKBehavior::Cast(m_State.document->Context()->GetObject(hit.view->id));
        if (script && script->GetOwner()) m_State.events.Post(ShowInLevelManager{script->GetOwner()->GetID()});
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
            m_State.selection.Set(hit.view->id, hit.ref);
            OpenParametersDialog();
        }
    } else if (hit.ref.kind == ElementKind::Operation) {
        m_State.selection.Set(hit.view->id, hit.ref);
        OpenParametersDialog();
    } else if (hit.ref.kind == ElementKind::Link) {
        for (const auto &link : hit.view->model.links)
            if (link.ref == hit.ref && link.behaviorLink) {
                m_Doc.dialogScript = hit.view->id;
                m_Doc.delayLink = link.ref.object;
                m_DelayValue = link.delay;
                m_Doc.openDelay = true;
            }
    } else if (hit.ref.kind == ElementKind::LocalParam || hit.ref.kind == ElementKind::Shortcut) {
        CKParameter *parameter = CKParameter::Cast(m_State.document->Context()->GetObject(hit.ref.object));
        OpenValueDialog(hit.view->id, parameter);
    } else if (hit.ref.kind == ElementKind::Pin) {
        CKObject *owner = m_State.document->Context()->GetObject(hit.ref.object);
        CKBehavior *behavior = CKBehavior::Cast(owner);
        CKParameterOperation *operation = CKParameterOperation::Cast(owner);
        CKParameterIn *input = nullptr;
        if (hit.ref.pin == PinKind::ParamIn)
            input = behavior ? behavior->GetInputParameter(hit.ref.index)
                  : operation ? (hit.ref.index == 0 ? operation->GetInParameter1() : operation->GetInParameter2())
                              : nullptr;
        else if (hit.ref.pin == PinKind::Target && behavior)
            input = behavior->GetTargetParameter();
        if (input) {
            OpenValueDialog(hit.view->id, input);
        } else if (hit.ref.pin == PinKind::ParamOut) {
            CKParameter *output = behavior ? static_cast<CKParameter *>(behavior->GetOutputParameter(hit.ref.index))
                                : operation ? static_cast<CKParameter *>(operation->GetOutParameter())
                                            : nullptr;
            OpenValueDialog(hit.view->id, output);
        }
    } else if (hit.ref.kind == ElementKind::Comment) {
        for (const auto &comment : hit.view->model.comments) {
            if (comment.ref != hit.ref) continue;
            m_Doc.commentScript = hit.view->id;
            m_Doc.commentRef = hit.ref;
            m_CommentText = comment.text;
            m_Doc.openCommentEditor = true;
        }
    }
}

void SchematicCanvas::BeginMove() {
    if (!m_Doc.press.view || !m_State.HasDocument()) return;
    ScriptSession *session = m_State.document->Session(m_Doc.press.view->id);
    if (!session || session->ReadOnly()) return;
    const CK_ID script = m_Doc.press.view->id;
    if (!IsSelected(script, m_Doc.press.ref)) m_State.selection.Set(script, m_Doc.press.ref);
    m_Doc.moveItems.clear();
    for (const auto &item : m_State.selection.items)
        if (Movable(item.kind)) m_Doc.moveItems.push_back(item);
    if (m_Doc.moveItems.empty()) return;
    m_Doc.moveScript = script;
    m_Doc.moveOriginal = session->Data();
    m_Doc.moveOriginal.observers.clear();
    m_Doc.movePrimary = m_Doc.press.ref;
    m_Doc.moveChanged = false;
    m_Doc.gesture = std::make_unique<Edit>(*m_State.document, "Move", Edit::Marks::Scripts);
    m_Tool = Tool::Move;
    // Dev.exe CUIKSchematicView_HandleMouseMove with Move Item Coherence Test
    // (sub_52E550): what is moved stays inside the expanded Behavior Graph it
    // belongs to, a grid step from its border; the move stops for all.
    m_Doc.moveLimits.clear();
    if (!m_State.preferences.Bool("Schematic\\Move Item Coherence Test")) return;
    const SchematicModel &model = m_Doc.press.view->model;
    auto moved = [&](CK_ID id) {
        return std::any_of(m_Doc.moveItems.begin(), m_Doc.moveItems.end(),
                           [&](const ElementRef &ref) { return ref.kind == ElementKind::Behavior && ref.object == id; });
    };
    // The innermost expanded graph holding owner that is not moved too.
    auto container = [&](CK_ID owner, Rect &box) {
        for (CK_ID id = owner; id && id != script;) {
            const BehaviorItem *graph = model.FindBehavior(id);
            if (!graph) return false;
            if (moved(id)) return false;
            if (graph->graph && !graph->folded) {
                box = graph->box;
                return true;
            }
            id = graph->parent;
        }
        return false;
    };
    for (const ElementRef &ref : m_Doc.moveItems) {
        Rect item, box;
        CK_ID owner = 0;
        bool found = false;
        if (ref.kind == ElementKind::Behavior) {
            if (const BehaviorItem *behavior = model.FindBehavior(ref.object)) {
                item = behavior->box;
                owner = behavior->parent;
                found = true;
            }
        }
        for (const ParamItem &param : model.params)
            if (!found && param.ref == ref) item = param.bounds, owner = param.owner, found = true;
        for (const OperationItem &operation : model.operations)
            if (!found && operation.ref == ref) item = operation.bounds, owner = operation.owner, found = true;
        for (const CommentItem &comment : model.comments)
            if (!found && comment.ref == ref) item = comment.bounds, owner = comment.owner, found = true;
        if (found && container(owner, box)) m_Doc.moveLimits.push_back({item, box});
    }
}

void SchematicCanvas::UpdateMove(const Point &world) {
    ScriptView *view = ViewFor(m_Doc.moveScript);
    if (!m_Doc.gesture || !view) return;
    Point delta(world.h - m_PressWorld.h, world.v - m_PressWorld.v);
    // Move Item Coherence Test: the largest move keeping every limited item
    // inside its graph (none along an axis it already crosses).
    const float step = SchematicGrid::SPACING;
    for (const auto &limit : m_Doc.moveLimits) {
        const Rect &item = limit.first, &box = limit.second;
        const float minH = box.hPos + step - item.hPos, maxH = box.Right() - step - item.Right();
        const float minV = box.vPos + step - item.vPos, maxV = box.Bottom() - step - item.Bottom();
        delta.h = minH > maxH ? 0.0f : std::clamp(delta.h, std::min(0.0f, minH), std::max(0.0f, maxH));
        delta.v = minV > maxV ? 0.0f : std::clamp(delta.v, std::min(0.0f, minV), std::max(0.0f, maxV));
    }
    InterfaceData *data = m_Doc.gesture->ScriptData(m_Doc.moveScript);
    if (!data) return;
    MoveElements(*data, m_Doc.moveOriginal, view->rootIsScript, m_Doc.moveItems, m_Doc.movePrimary, delta);
    m_Doc.moveChanged = true;
}

void SchematicCanvas::EndMove(bool commit) {
    // Dev documentation "Link Options": with Search Best Path the links of
    // a moved selection take the best path (Ctrl inverts it), in the move's step.
    if (m_Doc.gesture && commit && !m_Doc.gesture->Empty() && m_Doc.moveScript &&
        m_State.preferences.searchBestPath != ImGui::GetIO().KeyCtrl) {
        std::set<CK_ID> moved;
        for (const ElementRef &item : m_Doc.moveItems)
            if (item.kind == ElementKind::Behavior || item.kind == ElementKind::Operation) moved.insert(item.object);
        CKBehavior *script = CKBehavior::Cast(m_State.document->Context()->GetObject(m_Doc.moveScript));
        if (!moved.empty() && script)
            EditScriptData(*m_State.document, m_Doc.moveScript, "Move", [&](InterfaceData &data) {
                auto visit = [&](BehaviorData &record) {
                    for (Link &link : record.links) {
                        if (!moved.count(link.start.id) && !moved.count(link.end.id)) continue;
                        const std::vector<Point> before = link.points;
                        link.points.clear();
                        if (!RouteSingleLink(data, record, link, script)) link.points = before;
                    }
                };
                visit(data.rootBehavior);
                for (BehaviorData &record : data.behaviors) visit(record);
            });
    }
    // Locals dropped into another graph export instead (ExportMovedLocals ends the gesture).
    if (m_Doc.gesture && commit && !m_Doc.gesture->Empty() && !ExportMovedLocals()) m_Doc.gesture->Commit();
    m_Doc.gesture.reset(); // not committed: taken back
    m_Doc.moveItems.clear();
    m_Doc.moveScript = 0;
    m_Tool = Tool::Idle;
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
    return m_State.selection.script ? m_State.selection.script : m_Doc.currentScript;
}

std::vector<CK_ID> SchematicCanvas::ToolbarScripts() const {
    std::vector<CK_ID> scripts = m_State.selection.Scripts();
    if (scripts.empty())
        if (const CK_ID script = ToolbarScript()) scripts.push_back(script);
    return scripts;
}

void SchematicCanvas::Toolbar() {
    ViewOptions &options = m_State.view;
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
    const bool document = m_State.HasDocument();
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
            if (ScriptSession *session = m_State.document->Session(each)) session->SetOpen(open);
    };
    if (icon("expand", EXPAND, false, script != 0, "Expand Selected Scripts (Right Arrow)")) setOpen(true);
    if (icon("collapse", COLLAPSE, false, script != 0, "Collapse Selected Scripts (Left Arrow)")) setOpen(false);
    if (icon("explorer", EXPLORER, false, true, "Schematic Explorer (Ctrl+Shift+F)", 19.0f))
        m_State.events.Post(OpenSchematicExplorer{});
    if (icon("messages", MESSAGES, m_ShowMessages, true, "Message Explorer (H)")) m_ShowMessages = !m_ShowMessages;
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
        m_State.events.Post(RefreshScriptStack{});
    }
    if (pill("showall", "Show All", false, document, "Show All Scripts", 70.0f)) {
        m_State.hiddenScripts.clear();
        options.showAllScripts = true;
        m_State.events.Post(RefreshScriptStack{});
    }
    if (pill("hideall", "Hide All", false, document, "Hide All Scripts", 70.0f)) {
        for (const auto &info : m_State.document->Scripts()) m_State.hiddenScripts.insert(info.id);
        m_State.events.Post(RefreshScriptStack{});
    }
    if (pill("hidesel", "Hide Selected", false, script != 0, "Hide Selected Scripts", 70.0f)) {
        for (CK_ID each : ToolbarScripts()) m_State.hiddenScripts.insert(each);
        m_State.selection.Clear();
        m_Doc.currentScript = 0;
        m_State.events.Post(RefreshScriptStack{});
    }
    if (pill("hideother", "Hide Other", false, script != 0, "Hide Unselected Scripts", 70.0f)) {
        const std::vector<CK_ID> kept = ToolbarScripts();
        for (const auto &info : m_State.document->Scripts())
            if (std::find(kept.begin(), kept.end(), info.id) == kept.end()) m_State.hiddenScripts.insert(info.id);
        for (CK_ID each : kept) m_State.hiddenScripts.erase(each);
        m_State.events.Post(RefreshScriptStack{});
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
    if (icon("linkprops", LINK_PROPERTIES, false, true, "Behavior Link Properties (default link delay)"))
        m_State.events.Post(OpenLinkProperties{});
    if (icon("priorities", PRIORITIES, !options.showPriorities, true, "Show/Hide Priorities"))
        options.showPriorities = !options.showPriorities;
    separator();
    if (icon("reset", RESET, false, true, "Reset Schematic Zoom and Position")) ResetView();
    if (icon("zoom", ZOOM, options.zoomMode, true, "Zoom Mode")) options.zoomMode = !options.zoomMode;
    separator();
    if (icon("debugger", DEBUGGER, m_State.showDebugger, true, "Script Debugger"))
        m_State.showDebugger = !m_State.showDebugger;
    if (m_State.debugger && pill("trace", "Trace", m_State.debugger->trace, true, "Trace Mode (Ctrl+T)", 66.0f))
        m_State.debugger->trace = !m_State.debugger->trace;
    // Dev: right of Trace, the Invalid Link Explorer's icon, shown only when
    // the composition has invalid links kept after loading.
    if (!m_State.invalidLinks.empty() &&
        icon("linkerrors", LINK_ERRORS, m_ShowLinkErrors, true, "Browse links with error found while loading", 17.0f, 2.0f))
        m_ShowLinkErrors = !m_ShowLinkErrors;
    // The zoom of the Script under the mouse (the editor's).
    const ScriptView *zoomed = ViewFor(m_Doc.hoveredScript ? m_Doc.hoveredScript : m_Doc.currentScript);
    ImGui::SetCursorScreenPos(ImVec2(at(0).x + 4.0f * s, stripMin.y + (stripHeight - ImGui::GetFontSize()) * 0.5f));
    ImGui::TextDisabled("%.0f%%", (zoomed ? zoomed->Zoom() : m_View.zoom) * 100.0f);
    PopDarkChrome();
    ImGui::SetCursorScreenPos(ImVec2(stripMin.x, stripMin.y + stripHeight));
}

void SchematicCanvas::Draw(const char *title, bool *open) {
    // Asked whether the Schematic shows or not (a Level Manager Script saved).
    SaveBehaviorInfo();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool visible = BeginDevWindow(title, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }
    ImFont *font = ImGui::GetFont();
    ViewOptions &options = m_State.view;
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
    m_PickMin = canvasMin;
    m_PickMax = canvasMax;
    m_PickFrame = ImGui::GetFrameCount();
    m_State.objectPick[title] = [this](float x, float y, CK_CLASSID cid) { return PickObjectAt(ImVec2(x, y), cid); };
    if (m_State.HasDocument()) AcceptBuildingBlockDrop();
    if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
        ImGui::SetWindowFocus();

    m_View.origin = canvasMin;
    m_Font = font;
    SyncViews(font);
    if (m_State.selection.script) m_Doc.currentScript = m_State.selection.script;
    if (!m_ViewInitialized && !m_Views.empty()) {
        ResetView();
        m_ViewInitialized = true;
    }
    ApplyFocus(size);

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
    for (const auto &view : m_Views) {
        std::set<ElementRef> selected;
        if (m_State.selection.script == view.id)
            selected.insert(m_State.selection.items.begin(), m_State.selection.items.end());
        if (std::find(m_State.selection.headers.begin(), m_State.selection.headers.end(), view.id) !=
            m_State.selection.headers.end())
            selected.insert(view.model.script.ref);
        draw.selection = &selected;
        std::set<ElementRef> invalid;
        for (const InvalidLink &link : m_State.invalidLinks)
            if (link.script == view.id) invalid.insert({ElementKind::Link, link.graph, link.link, link.slot});
        draw.invalid = &invalid;
        draw.hovered = m_Doc.hoveredScript == view.id ? m_Doc.hovered : ElementRef();
        draw.current = view.id == m_Doc.currentScript;
        draw.scriptZoom = view.scale;
        // Script Debugger: behaviors running now.
        std::set<CK_ID> running, firing;
        draw.running = nullptr;
        draw.debugBehavior = 0;
        draw.trace = false;
        draw.activeLinks = nullptr;
        draw.scriptActive = false;
        if (m_State.debugger && m_State.debugger->GetState() != Debugger::State::Stopped && m_State.HasDocument()) {
            CKContext *context = m_State.document->Context();
            for (const auto &behavior : view.model.behaviors)
                if (CKBehavior *runtime = CKBehavior::Cast(context->GetObject(behavior.id)))
                    if (runtime->IsActive()) running.insert(behavior.id);
            draw.running = &running;
            draw.debugBehavior = m_State.debugger->CurrentBehavior();
            if (m_State.debugger->trace) {
                draw.trace = true;
                for (const auto &link : view.model.links)
                    if (CKBehaviorLink *runtime = link.behaviorLink ? CKBehaviorLink::Cast(context->GetObject(link.ref.object)) : nullptr)
                        if (CKBehaviorIO *in = runtime->GetInBehaviorIO())
                            if (in->IsActive()) firing.insert(link.ref.object);
                draw.activeLinks = &firing;
                CKBehavior *root = CKBehavior::Cast(context->GetObject(view.id));
                draw.scriptActive = root && root->IsActive();
            }
        }
        DrawScript(drawList, view.model, view.offsetY, view.frame, view.transform, draw);
    }
    HandleInput(canvasMin, canvasMax, hovered, focused);
    if (!m_State.HasDocument() || m_Views.empty()) {
        const char *message = !m_State.HasDocument() ? "File > Open a composition (.cmo, .nmo, .vmo)"
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
        if (m_Doc.menuHit.view && m_Doc.menuHit.ref.IsValid()) m_State.selection.Set(m_Doc.menuHit.view->id, m_Doc.menuHit.ref);
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
    MessageExplorer();
    LinkErrorsExplorer();
    SettingsDialog();
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
    ValueDialog();
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
    const Selection &selection = m_State.selection;
    ScriptView *view = ViewFor(selection.script);
    if (!view || !m_State.HasDocument()) return;
    std::set<CK_ID> owners; // selected behaviors and operations
    std::set<ElementRef> links;
    for (const auto &item : selection.items) {
        if (item.kind == ElementKind::Link) links.insert(item);
        if (item.kind == ElementKind::Behavior || item.kind == ElementKind::Operation) owners.insert(item.object);
    }
    std::vector<std::pair<CK_ID, int>> targets; // storing graph, slot
    for (const auto &link : view->model.links) {
        if (link.hidden) continue;
        if (links.count(link.ref) || owners.count(link.start.id) || owners.count(link.end.id))
            targets.emplace_back(link.owner, link.slot);
    }
    if (targets.empty()) {
        m_State.Log(LogLevel::Info, "P: select links, or behaviors and operations whose links to update");
        return;
    }
    CKBehavior *script = CKBehavior::Cast(m_State.document->Context()->GetObject(view->id));
    int routed = 0, failed = 0;
    EditScriptData(*m_State.document, view->id, "Update Link Path", [&](InterfaceData &data) {
        for (const auto &target : targets) {
            BehaviorData *record = FindRecord(data, target.first);
            if (!record || target.second < 0 || target.second >= static_cast<int>(record->links.size())) continue;
            Link &link = record->links[target.second];
            if (!link.points.empty()) {
                link.points.clear(); // direct path
            } else if (RouteSingleLink(data, *record, link, script)) {
                ++routed;
            } else {
                ++failed;
            }
        }
    });
    if (failed) m_State.Log(LogLevel::Info, "P: no path found for " + std::to_string(failed) + " link(s)");
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
        if (m_Views[i].id == (m_State.selection.script ? m_State.selection.script : m_Doc.currentScript)) at = i;
    const int next = std::max(0, std::min(static_cast<int>(m_Views.size()) - 1, at < 0 ? 0 : at + step));
    const ScriptView &view = m_Views[next];
    m_Doc.currentScript = view.id;
    m_State.focus = FocusRequest{true, view.id, view.model.script.ref, 0};
}

void SchematicCanvas::MoveHighlightedScript(int step) {
    std::vector<CK_ID> order = m_State.visibleScripts;
    auto at = std::find(order.begin(), order.end(), m_Doc.currentScript ? m_Doc.currentScript : m_State.selection.script);
    if (at == order.end()) return;
    const auto index = at - order.begin();
    const auto other = index + step;
    if (other < 0 || other >= static_cast<long long>(order.size())) return;
    std::swap(order[index], order[other]);
    m_State.scriptOrder = order;
    m_State.events.Post(RefreshScriptStack{});
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
    if (!m_State.HasDocument()) return;
    std::vector<CK_ID> scripts = m_State.selection.Scripts();
    if (scripts.empty() && m_Doc.currentScript) scripts.push_back(m_Doc.currentScript);
    const InitialConditions what = action == 0   ? InitialConditions::Set
                                   : action == 1 ? InitialConditions::Remove
                                                 : InitialConditions::Restore;
    const EditResult result = ApplyInitialConditions(*m_State.document, scripts, what);
    Report(result, action == 0 ? "Set Initial Conditions" : action == 1 ? "Remove Initial Conditions"
                                                                         : "Restore Initial Conditions");
}

} // namespace vdev

namespace vdev {

void SchematicCanvas::OpenOperationChoice(OperationChoice mode, CK_ID script, const LinkEndpoint &from,
                                          const LinkEndpoint &to, const ElementRef &link) {
    m_Doc.choices = mode == OperationChoice::InsertInLink ? ParameterLinkOperations(*m_State.document, script, from, to)
                                                      : ParameterLinkConversions(*m_State.document, script, from, to);
    if (m_Doc.choices.empty()) {
        m_State.Log(LogLevel::Info, "No parameter operation fits these parameter types");
        return;
    }
    m_ChoiceMode = mode;
    m_Doc.choiceScript = script;
    m_Doc.choiceFrom = from;
    m_Doc.choiceTo = to;
    m_Doc.choiceLink = link;
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
            if (typeAhead.Selectable((AnsiToUtf8(m_Doc.choices[i].label) + "##" + std::to_string(i)).c_str(), i == m_ChoicePick,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                m_ChoicePick = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) go = true;
            }
        ImGui::EndListBox();
    }
    if (ImGui::Button("OK", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) go = true;
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    if (go && m_ChoicePick >= 0 && m_ChoicePick < static_cast<int>(m_Doc.choices.size())) {
        const OperationSignature signature = m_Doc.choices[m_ChoicePick];
        ImGui::CloseCurrentPopup();
        ApplyOperationChoice(signature);
    }
    ImGui::EndPopup();
}

void SchematicCanvas::ApplyOperationChoice(const OperationSignature &signature) {
    Document &document = *m_State.document;
    const CK_ID script = m_Doc.choiceScript;
    switch (m_ChoiceMode) {
    case OperationChoice::NewLink:
        Report(CreateParameterLink(document, script, m_Doc.choiceFrom, m_Doc.choiceTo, &signature), "Parameter operation link");
        break;
    case OperationChoice::ReplaceConversion:
        // The link stays when it fails.
        Report(ReplaceLinkConversion(document, script, m_Doc.choiceLink, m_Doc.choiceFrom, m_Doc.choiceTo, signature),
               "Edit Parameter Operation Link");
        break;
    case OperationChoice::InsertInLink:
        Report(InsertOperationInLink(document, script, m_Doc.choiceLink, m_Doc.choiceFrom, m_Doc.choiceTo, signature),
               "Add Parameter Operation in Link");
        break;
    }
}

void SchematicCanvas::OpenModifyOperation() {
    const Selection &selection = m_State.selection;
    if (selection.items.size() != 1 || selection.items[0].kind != ElementKind::Operation || !m_State.HasDocument()) {
        m_State.Log(LogLevel::Info, "M: select a parameter operation to modify");
        return;
    }
    m_Doc.dialogScript = selection.script;
    m_Doc.dialogGraph = selection.items[0].owner;
    m_Doc.modifyOperation = selection.items[0].object;
    // The parts of the operation modified.
    if (auto *runtime = CKParameterOperation::Cast(m_State.document->Context()->GetObject(m_Doc.modifyOperation))) {
        auto guidOf = [](CKParameter *parameter) { return parameter ? parameter->GetGUID() : CKPGUID_NONE; };
        m_OperationParts[0] = runtime->GetInParameter1() ? runtime->GetInParameter1()->GetGUID() : CKPGUID_NONE;
        m_OperationParts[1] = runtime->GetInParameter2() ? runtime->GetInParameter2()->GetGUID() : CKPGUID_NONE;
        m_OperationParts[2] = guidOf(runtime->GetOutParameter());
        m_OperationParts[3] = runtime->GetOperationGuid();
    }
    m_Doc.openOperationPicker = true;
}

// Modify Parameter Operation (ReplaceParameterOperation): one undo step; the
// links that did not fit the new types are logged.
void SchematicCanvas::ReplaceOperation(CK_ID script, CK_ID operationId, const OperationSignature &signature) {
    int dropped = 0;
    const EditResult result = ReplaceParameterOperation(*m_State.document, script, operationId, signature, &dropped);
    if (!result.ok) {
        Report(result, "Modify Parameter Operation");
        return;
    }
    if (dropped > 0)
        m_State.Log(LogLevel::Info, "Modify Parameter Operation: " + std::to_string(dropped) +
                                        " link(s) did not fit the new types");
}

} // namespace vdev

namespace vdev {

void SchematicCanvas::ConvertToShortcutSource(CK_ID source, const std::vector<CK_ID> &scripts) {
    if (!m_State.HasDocument()) return;
    const EditResult result = scripts.empty() ? ShortcutizeLocalsInAllScripts(*m_State.document, source)
                                              : ShortcutizeLocalsInScripts(*m_State.document, scripts, source);
    if (!result.ok) {
        m_State.Log(LogLevel::Info, "Convert to Shortcut Source: no other local with the same name, type and value");
        return;
    }
    m_State.Log(LogLevel::Info, "Convert to Shortcut Source: " + AnsiToUtf8(result.message) + " changed");
}

void SchematicCanvas::BeginPickShortcutSource(CK_ID from) {
    m_Doc.pickShortcutFrom = from;
    m_State.Log(LogLevel::Info, "Set Shortcut Source: click the new source (Esc cancels)");
}

void SchematicCanvas::RetargetAllShortcuts(CK_ID from, CK_ID to) {
    // All or nothing: a Script refusing the new source leaves every Script as it was.
    const EditResult result = RetargetShortcutsInAllScripts(*m_State.document, from, to);
    if (!result.ok && !result.Unchanged())
        m_State.Log(LogLevel::Warning, "Set Shortcut Source refused: " + AnsiToUtf8(result.message));
}

// ---------------------------------------------------------------- Find

void SchematicCanvas::FindNext(bool backward) {
    if (!m_State.HasDocument()) return;
    if (m_FindText.empty()) {
        OpenFind();
        return;
    }
    SearchQuery query;
    query.text = Utf8ToAnsi(m_FindText);
    query.kinds = SearchAll;
    std::vector<SearchResult> results = SearchDocument(*m_State.document, query);
    // The Schematic's order of the Scripts: the stack's (with a Sort By's),
    // the others after; in a Script the order found.
    std::vector<CK_ID> order = m_State.visibleScripts;
    for (const ScriptInfo &info : m_State.document->Scripts())
        if (std::find(order.begin(), order.end(), info.id) == order.end()) order.push_back(info.id);
    auto rank = [&](CK_ID script) { return static_cast<size_t>(std::find(order.begin(), order.end(), script) - order.begin()); };
    std::stable_sort(results.begin(), results.end(), [&](const SearchResult &a, const SearchResult &b) { return rank(a.script) < rank(b.script); });
    // Hidden Scripts, closed Scripts and folded graphs when asked only.
    results.erase(std::remove_if(results.begin(), results.end(),
                                 [&](const SearchResult &result) {
                                     if (!m_FindHidden && m_State.hiddenScripts.count(result.script)) return true;
                                     if (m_FindClosed) return false;
                                     ScriptSession *session = m_State.document->Session(result.script);
                                     if (!session || !session->IsOpen()) return true;
                                     const ScriptView *view = ViewFor(result.script);
                                     if (!view || !result.focus || result.focus == result.script) return false;
                                     return !view->model.IsOpenGraph(result.focus) && !view->model.FindBehavior(result.focus);
                                 }),
                  results.end());
    // After the one found last (else from the start, or the end).
    int at = -1;
    for (size_t i = 0; i < results.size(); ++i)
        if (results[i].script == m_FoundScript && results[i].ref == m_FoundRef) at = static_cast<int>(i);
    const int count = static_cast<int>(results.size());
    const int next = at < 0 ? (backward ? count - 1 : 0) : at + (backward ? -1 : 1);
    if (next < 0 || next >= count) {
        m_State.Log(LogLevel::Warning, "Passed the end of Schematic");
        m_State.selection.Clear();
        m_FoundScript = 0;
        m_FoundRef = ElementRef();
        return;
    }
    const SearchResult &found = results[next];
    m_FoundScript = found.script;
    m_FoundRef = found.ref;
    if (ScriptSession *session = m_State.document->Session(found.script))
        if (!session->IsOpen()) session->SetOpen(true);
    m_State.events.Post(ShowElement{found.script, found.ref, found.focus});
}

void SchematicCanvas::FindSelected(bool backward) {
    const Selection &selection = m_State.selection;
    if (selection.items.size() != 1 || !m_State.HasDocument()) {
        m_State.Log(LogLevel::Info, "Ctrl+F3: select one element");
        return;
    }
    const ElementRef &item = selection.items.front();
    CKObject *object = m_State.document->Context()->GetObject(item.kind == ElementKind::Script ? selection.script : item.object);
    if (!object || !object->GetName()) return;
    m_FindText = AnsiToUtf8(object->GetName());
    m_FoundScript = selection.script;
    m_FoundRef = item;
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
        if (m_State.selection.items.size() == 1) {
            m_FoundScript = m_State.selection.script;
            m_FoundRef = m_State.selection.items.front();
        }
        FindNext(m_FindBackward);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace vdev
