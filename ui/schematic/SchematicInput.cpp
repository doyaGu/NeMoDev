// The Schematic's mouse and keyboard (SchematicCanvas::HandleInput): the
// tools, selection, links, moves and the shortcuts.
#include "ui/schematic/SchematicCanvas.h"

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>

#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include "ui/controls/DevTheme.h"
#include "ui/schematic/SchematicCanvasDetail.h"
#include "core/schematic/SchematicGrid.h"
#include "core/schematic/SchematicHitTest.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

using namespace canvas;

void SchematicCanvas::HandleInput(const ImVec2 &canvasMin, const ImVec2 &canvasMax, bool hovered, bool focused) {
    ImGuiIO &io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const Hit hit = hovered ? HitAt(mouse) : Hit();

    // Picking a breakpoint condition's parameter (documentation "Configure
    // Breakpoint": "Pick Schematic Parameter in Schematic", the name of the
    // parameter under the mouse).
    if (m_Doc.pickConditionRow >= 0 && hovered) {
        const auto under = ConditionParameterAt(hit);
        const auto info = under ? Client().Query("schematic.breakpoint-parameter", {ObjectArgument("parameter", under)})
                                : EditorResponse{};
        const auto name = EditorClient::Text(info, "name");
        ImGui::SetTooltip("%s in Schematic%s%s",
                          m_Doc.pickReference ? "Pick Reference Parameter" : "Pick Schematic Parameter",
                          name.empty() ? "" : ": ", name.c_str());
    }
    m_Doc.hovered = hit.ref;
    m_Doc.hoveredScript = hit.view ? hit.view->id : 0;
    if (hit.view) {
        m_Doc.mouseScript = hit.view->id;
        m_Doc.mouseLocal = hit.local;
    }
    if ((Busy() || m_DrawGraphMode) && Client().Binding("schematic.view.cancel", EDITOR_KEY_PRESSED, true)) {
        const bool linking = m_Tool == Tool::Link;
        CancelInteraction();
        if (linking) m_LinkMode = false;
        return;
    }

    if (hovered && io.MouseWheel != 0.0f && m_Tool != Tool::Move) {
        // The wheel scrolls the stack (Shift: sideways); Ctrl+wheel zooms the
        // Script under the cursor about its Start (Dev zooms Scripts on their own).
        if (io.KeyCtrl && hit.view && !hit.onHeader)
            ZoomScript(*hit.view, hit.view->scale * std::pow(1.15f, io.MouseWheel));
        else if (io.KeyShift)
            PanBy(io.MouseWheel * 60.0f, 0.0f);
        else if (!io.KeyCtrl)
            PanBy(0.0f, io.MouseWheel * 60.0f);
    }
    if (hovered && io.MouseWheelH != 0.0f) PanBy(io.MouseWheelH * 40.0f, 0.0f);

    const bool spaceDown = focused && Client().Binding("schematic.view.space-action", EDITOR_KEY_DOWN);
    switch (m_Tool) {
    case Tool::Idle: {
        if (!hovered) break;
        // A Script boundary (the band's bottom edge) resizes the Script.
        ScriptView *edge = m_DrawGraphMode ? nullptr : BoundaryAt(mouse);
        if (edge) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        ElementRef handle;
        int handleEdges = 0;
        if (!edge && hit.view && !hit.onHeader && !m_DrawGraphMode &&
            ResizeHandleAt(*hit.view, hit.local, handle, handleEdges))
            ImGui::SetMouseCursor(handleEdges == 3 ? ImGuiMouseCursor_ResizeNWSE
                                  : handleEdges == 1 ? ImGuiMouseCursor_ResizeEW
                                                     : ImGuiMouseCursor_ResizeNS);
        if (handleEdges && !spaceDown && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (BeginGeometry(hit.view->id, handle.kind == ElementKind::Behavior ? 2 : 3, {handle})) {
                m_Doc.resizeRef = handle;
                m_ResizeEdges = handleEdges;
                m_PressWorld = hit.local;
                m_ResizeStart = Rect();
                for (const auto &comment : hit.view->model.comments)
                    if (comment.ref == handle) m_ResizeStart = comment.bounds;
                if (const BehaviorItem *graph = hit.view->model.FindBehavior(handle.object))
                    if (handle.kind == ElementKind::Behavior) m_ResizeStart = graph->box;
                m_Navigation.selection.Set(hit.view->id, handle);
                m_Doc.geometrySelection = m_Navigation.selection.Addresses();
                m_Tool = Tool::ResizeElement;
                break;
            }
        }
        if (edge && !spaceDown && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (BeginGeometry(edge->id, 1, {})) {
                m_Doc.resizeScript = edge->id;
                m_ResizeTop = edge->frame.ToScreen(0.0f, edge->model.script.bounds.vPos + edge->offsetY).y;
                m_Tool = Tool::Resize;
                break;
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            // A click opens Dev's Building Block category menu; a drag pans.
            m_Tool = Tool::Pending;
            m_DragButton = ImGuiMouseButton_Middle;
            m_Doc.press = hit;
            RetainHit(m_Doc.press, m_PressAddress);
            m_PressScreen = mouse;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_Tool = Tool::Pending;
            m_DragButton = ImGuiMouseButton_Right;
            m_Doc.press = hit;
            RetainHit(m_Doc.press, m_PressAddress);
            m_PressScreen = mouse;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (spaceDown) {
                m_Tool = Tool::Pan;
                m_DragButton = ImGuiMouseButton_Left;
                m_SpacePanned = true;
            } else if (m_Doc.pickBreakpoint) {
                // Add Breakpoint (Script Debugger): the element clicked.
                m_Doc.pickBreakpoint = false;
                if (hit.view && hit.ref.IsValid()) m_Navigation.selection.Set(hit.view->id, hit.ref);
                else m_Navigation.selection.Clear();
                ToggleBreakpointOfSelection();
            } else if (m_Doc.pickConditionRow >= 0) {
                // Breakpoint condition: a local, a shortcut or a behavior parameter.
                const auto picked = ConditionParameterAt(hit);
                if (picked) PickConditionParameter(picked);
                else Client().Log(EDITOR_LOG_INFO, "Pick a parameter: a local parameter, a shortcut or a parameter pin");
            } else if (m_Doc.pickShortcutFrom) {
                ObjectRef picked{};
                if (hit.view && (hit.ref.kind == ElementKind::LocalParam || hit.ref.kind == ElementKind::Shortcut ||
                                 (hit.ref.kind == ElementKind::Pin && hit.ref.pin == PinKind::ParamOut))) {
                    const auto info = ElementInfo(hit.view->id, hit.ref);
                    if (info.status == EDITOR_OK)
                        if (const auto *object = EditorClient::Find(info, "object"))
                            picked = object->object;
                }
                const auto from = m_Doc.pickShortcutFrom;
                m_Doc.pickShortcutFrom = {};
                if (picked)
                    RetargetAllShortcuts(from, picked);
                else
                    Client().Log(EDITOR_LOG_INFO, "Set Shortcut Source: pick a local parameter, a shortcut or an output");
            } else if (io.KeyCtrl && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hit.view && !hit.onHeader) {
                // Dev: Ctrl+double-click adds a Building Block by name.
                OpenQuickAdd(hit.view->id, hit.local);
            } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                DoubleClick(hit);
            } else if (m_DrawGraphMode && hit.view && !hit.onHeader) {
                m_Tool = Tool::RectSelect;
                m_DragButton = ImGuiMouseButton_Left;
                m_PressScreen = mouse;
            } else if (m_Navigation.options.zoomMode) {
                m_Tool = Tool::Zoom;
                m_DragButton = ImGuiMouseButton_Left;
                m_PressScreen = mouse;
                m_PressWorld = hit.view ? hit.view->transform.ToWorld(mouse) : m_View.ToWorld(mouse);
                m_Doc.zoomScript = hit.view ? hit.view->id : 0;
                m_ZoomStart = hit.view ? hit.view->scale : 1.0f;
            } else {
                if (hit.view) m_Doc.currentScript = hit.view->id;
                m_Tool = Tool::Pending;
                m_DragButton = ImGuiMouseButton_Left;
                m_Doc.press = hit;
            RetainHit(m_Doc.press, m_PressAddress);
                m_PressScreen = mouse;
                m_PressWorld = !hit.view    ? m_View.ToWorld(mouse)
                               : hit.onStart ? hit.view->frame.ToWorld(mouse)
                                             : hit.view->transform.ToWorld(mouse);
            }
        }
        break;
    }
    case Tool::Pending: {
        if (ImGui::IsMouseReleased(m_DragButton)) {
            // Dev.exe sub_505B80: Start is the Script's behavior input (an I/O,
            // element type 26): a click on it begins a link as on any I/O (a
            // drag moves it up or down, Alt+drag links).
            const bool linkable = m_Doc.press.ref.kind == ElementKind::Pin || m_Doc.press.ref.kind == ElementKind::Start ||
                                  ((io.KeyAlt || m_LinkMode) && (m_Doc.press.ref.kind == ElementKind::LocalParam ||
                                                                 m_Doc.press.ref.kind == ElementKind::Shortcut));
            if (m_DragButton == ImGuiMouseButton_Middle) {
                m_Tool = Tool::Idle;
                if (m_Doc.press.view) {
                    m_Doc.blockMenuScript = m_Doc.press.view->id;
                    m_Doc.blockMenuLocal = m_Doc.press.local;
                    // Dev: Shift+middle click on a Building Block replaces it.
                    m_Doc.replaceBehavior =
                        io.KeyShift && IsBuildingBlockAt(m_Doc.press) ? m_PressAddress.object : ObjectRef{};
                    m_Doc.replaceScript = m_Doc.press.view->identity;
                    m_Doc.blockCreation = AuthoringArguments(
                        m_Doc.blockMenuScript, GraphAt(*m_Doc.press.view, m_Doc.blockMenuLocal), m_Doc.blockMenuLocal);
                    m_Doc.openBlockMenu = true;
                }
                break;
            }
            if (m_DragButton == ImGuiMouseButton_Left && linkable && !io.KeyCtrl &&
                (m_Navigation.options.autoLink || m_LinkMode) &&
                EndpointOf(m_Doc.press, m_Doc.linkFrom, m_LinkBehavior, m_LinkFromPoint)) {
                // Click on an I/O: the link follows the mouse until the next click.
                Click(m_Doc.press);
                m_Doc.linkScript = m_Doc.press.view->id;
                m_LinkClick = true;
                m_LinkPoints.clear();
                m_Tool = Tool::Link;
                RetainLinkSources();
                break;
            }
            if (m_DragButton == ImGuiMouseButton_Left) {
                Click(m_Doc.press);
            } else if (io.KeyShift && m_Doc.press.view) {
                m_Doc.blockMenuScript = m_Doc.press.view->id;
                m_Doc.blockMenuLocal = m_Doc.press.local;
                // Dev: Shift+right click on a Building Block replaces it by a recent one.
                m_Doc.replaceBehavior = IsBuildingBlockAt(m_Doc.press) ? m_PressAddress.object : ObjectRef{};
                m_Doc.replaceScript = m_Doc.press.view->identity;
                m_Doc.blockCreation = AuthoringArguments(
                    m_Doc.blockMenuScript, GraphAt(*m_Doc.press.view, m_Doc.blockMenuLocal), m_Doc.blockMenuLocal);
                m_Doc.openRecentMenu = true;
            } else {
                m_Doc.menuHit = m_Doc.press;
                m_MenuAddress = m_PressAddress;
                m_MenuStale = false;
                m_Doc.menuScripts.clear();
                // Ctrl + right-click on a local keeps the Script headers selected (its menu
                // converts to a shortcut source in those Scripts only).
                const bool keepHeaders = io.KeyCtrl && m_Doc.press.ref.kind == ElementKind::LocalParam &&
                                         m_Navigation.selection.OnlyHeaders();
                if (keepHeaders) m_Doc.menuScripts = m_Navigation.selection.Scripts();
                else if (m_Doc.press.view && m_Doc.press.ref.IsValid() && !IsSelected(m_Doc.press.view->id, m_Doc.press.ref))
                    m_Navigation.selection.Set(m_Doc.press.view->id, m_Doc.press.ref);
                ImGui::OpenPopup("SchematicContext");
            }
            m_Tool = Tool::Idle;
            break;
        }
        const float dx = mouse.x - m_PressScreen.x, dy = mouse.y - m_PressScreen.y;
        if (dx * dx + dy * dy < DRAG_THRESHOLD * DRAG_THRESHOLD) break;
        const bool linkFrom = !io.KeyCtrl &&
                              (m_Doc.press.ref.kind == ElementKind::Pin ||
                               ((io.KeyAlt || m_LinkMode) && (m_Doc.press.ref.kind == ElementKind::Start ||
                                                              m_Doc.press.ref.kind == ElementKind::LocalParam ||
                                                              m_Doc.press.ref.kind == ElementKind::Shortcut)));
        if (m_DragButton == ImGuiMouseButton_Left && m_Doc.press.view && m_Doc.press.onHeader &&
            m_Doc.press.ref.kind == ElementKind::Script) {
            // Dev: a Script header dragged up or down changes the Script order.
            m_Doc.reorderScript = m_Doc.press.view->id;
            m_Tool = Tool::Reorder;
        } else if (m_DragButton == ImGuiMouseButton_Right || m_DragButton == ImGuiMouseButton_Middle) {
            m_Tool = Tool::Pan;
        } else if (io.KeyShift && m_Doc.press.ref.kind == ElementKind::Pin && m_Doc.press.view) {
            // Dev: Shift+drag of pins exports them out of their graph, re-imports
            // them, or moves a graph parameter along its border.
            BeginExportDrag();
            if (m_Tool != Tool::ExportDrag) m_Tool = Tool::Idle;
        } else if (io.KeyAlt && m_Doc.press.ref.kind == ElementKind::Pin && m_Doc.press.view) {
            // Dev: Alt+drag of an I/O moves the end of its link.
            LinkEndpoint pressed;
            bool behaviorLink = false;
            Point position;
            m_Doc.relinkLink = 0;
            if (EndpointOf(m_Doc.press, pressed, behaviorLink, position))
                for (const auto &link : m_Doc.press.view->model.links) {
                    auto same = [&](const LinkEndpoint &e) {
                        return e.id == pressed.id && e.index == pressed.index && e.type == pressed.type;
                    };
                    if (!link.resolved || link.path.size() < 2 || (!same(link.start) && !same(link.end))) continue;
                    const bool movingEnd = same(link.end);
                    m_Doc.linkFrom = movingEnd ? link.start : link.end;
                    m_Doc.linkReverse = !movingEnd;
                    m_LinkFromPoint = movingEnd ? link.path.front() : link.path.back();
                    m_LinkBehavior = link.behaviorLink;
                    m_Doc.relinkLink = 1;
                    m_Doc.relinkRef = link.ref;
                    break;
                }
            if (m_Doc.relinkLink) {
                m_Doc.linkScript = m_Doc.press.view->id;
                m_LinkClick = false;
                m_LinkPoints.clear();
                m_Tool = Tool::Link;
                RetainLinkSources();
            } else {
                m_Tool = Tool::Idle;
            }
        } else if (linkFrom && EndpointOf(m_Doc.press, m_Doc.linkFrom, m_LinkBehavior, m_LinkFromPoint)) {
            m_Doc.linkScript = m_Doc.press.view->id;
            m_LinkClick = false;
            m_LinkPoints.clear();
            m_Doc.relinkLink = 0;
            m_Tool = Tool::Link;
            RetainLinkSources();
        } else if (io.KeyShift && m_Doc.press.view && IsSelected(m_Doc.press.view->id, m_Doc.press.ref) &&
                   (m_Doc.press.ref.kind == ElementKind::Behavior || m_Doc.press.ref.kind == ElementKind::Comment ||
                    m_Doc.press.ref.kind == ElementKind::LocalParam || m_Doc.press.ref.kind == ElementKind::Shortcut ||
                    m_Doc.press.ref.kind == ElementKind::Operation)) {
            // Dev: Shift+drag copies the moved selection.
            BeginCopyDrag();
            if (m_Tool != Tool::CopyDrag) m_Tool = Tool::Idle;
        } else if (m_Doc.press.view && m_Doc.press.ref.IsValid() && Movable(m_Doc.press.ref.kind) && !io.KeyCtrl) {
            BeginMove();
            if (m_Tool != Tool::Move) m_Tool = Tool::RectSelect;
        } else {
            m_Tool = Tool::RectSelect;
        }
        break;
    }
    case Tool::Zoom: {
        // Zoom Mode: dragging up/down zooms the pressed Script about the press point.
        if (ScriptView *view = ViewFor(m_Doc.zoomScript))
            ZoomScript(*view, m_ZoomStart * std::pow(1.01f, m_PressScreen.y - mouse.y));
        if (!ImGui::IsMouseDown(m_DragButton)) m_Tool = Tool::Idle;
        break;
    }
    case Tool::Pan:
        PanBy(io.MouseDelta.x, io.MouseDelta.y);
        if (!ImGui::IsMouseDown(m_DragButton)) m_Tool = Tool::Idle;
        break;
    case Tool::Resize: {
        // Dev: dragging a Script boundary changes its height (header vSize, 20 px steps).
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (!m_Doc.gesture) {
            EndMove(false);
            m_Doc.resizeScript = 0;
            break;
        }
        const float raw = (mouse.y - m_ResizeTop) / m_View.zoom;
        EditorField value{"value", EDITOR_RECORD}, size{"size", EDITOR_REAL_LIST};
        size.reals = {0, raw};
        value.fields.push_back(std::move(size));
        if (!UpdateGeometry(std::move(value)))
            break;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            EndMove(true);
            m_Doc.resizeScript = 0;
        }
        break;
    }
    case Tool::Reorder: {
        // The insertion point: before the band whose upper half is under the mouse.
        size_t insert = m_Views.size();
        float lineY = 0.0f;
        for (size_t i = 0; i < m_Views.size(); ++i) {
            const ScriptView &view = m_Views[i];
            const float top = view.frame.ToScreen(0.0f, view.model.script.bounds.vPos + view.offsetY).y;
            const float bottom = view.frame.ToScreen(0.0f, view.model.script.bounds.Bottom() + view.offsetY).y;
            if (mouse.y < (top + bottom) * 0.5f) {
                insert = i;
                lineY = top;
                break;
            }
            lineY = bottom;
        }
        ImGui::GetWindowDrawList()->AddLine(ImVec2(canvasMin.x, lineY), ImVec2(canvasMax.x, lineY),
                                            m_Style.selection, 3.0f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            std::vector<uint32_t> order;
            for (const auto &view : m_Views) order.push_back(view.id);
            auto from = std::find(order.begin(), order.end(), m_Doc.reorderScript);
            if (from != order.end()) {
                size_t at = static_cast<size_t>(from - order.begin());
                order.erase(from);
                if (insert > at) --insert;
                order.insert(order.begin() + std::min(insert, order.size()), m_Doc.reorderScript);
                // Hidden Scripts keep their place after the shown ones.
                for (uint32_t id : m_Navigation.visibleScripts)
                    if (std::find(order.begin(), order.end(), id) == order.end()) order.push_back(id);
                m_Navigation.scriptOrder = ScriptReferences(order);
                RefreshStack();
            }
            m_Tool = Tool::Idle;
        }
        break;
    }
    case Tool::ResizeElement: {
        ScriptView *view = ViewFor(m_Doc.moveScript);
        if (!view || !m_Doc.gesture) {
            EndMove(false);
            break;
        }
        ImGui::SetMouseCursor(m_ResizeEdges == 3 ? ImGuiMouseCursor_ResizeNWSE
                              : m_ResizeEdges == 1 ? ImGuiMouseCursor_ResizeEW
                                                   : ImGuiMouseCursor_ResizeNS);
        const Point world = view->transform.ToWorld(mouse);
        const Point local(world.h, world.v - view->offsetY);
        const bool graph = m_Doc.resizeRef.kind == ElementKind::Behavior;
        const float minimum = graph ? 60.0f : 20.0f;
        auto snap = [](float value) { return std::round(value / 20.0f) * 20.0f; };
        float width = m_ResizeStart.hSize, height = m_ResizeStart.vSize;
        if (m_ResizeEdges & 1) width = std::max(minimum, snap(m_ResizeStart.hSize + local.h - m_PressWorld.h));
        if (m_ResizeEdges & 2) height = std::max(minimum, snap(m_ResizeStart.vSize + local.v - m_PressWorld.v));
        m_Doc.moveChanged = width != m_ResizeStart.hSize || height != m_ResizeStart.vSize;
        EditorField value{"value", EDITOR_RECORD}, size{"size", EDITOR_REAL_LIST};
        size.reals = {width, height};
        value.fields.push_back(std::move(size));
        if (!UpdateGeometry(std::move(value)))
            break;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) EndMove(m_Doc.moveChanged);
        break;
    }
    case Tool::RectSelect: {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImVec2 a(std::min(m_PressScreen.x, mouse.x), std::min(m_PressScreen.y, mouse.y));
        const ImVec2 b(std::max(m_PressScreen.x, mouse.x), std::max(m_PressScreen.y, mouse.y));
        drawList->AddRectFilled(a, b, m_Style.rubberBand);
        drawList->AddRect(a, b, m_Style.rubberBandBorder);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ScriptView *view = ViewAtScreen(m_PressScreen);
            if (m_DrawGraphMode) {
                if (view) {
                    const Point wa = view->transform.ToWorld(a), wb = view->transform.ToWorld(b);
                    EndDrawGraph(*view, Rect(wa.h, wa.v - view->offsetY, wb.h - wa.h, wb.v - wa.v));
                }
                m_DrawGraphMode = false;
                m_Tool = Tool::Idle;
                break;
            }
            if (!io.KeyCtrl && !io.KeyShift) m_Navigation.selection.Clear();
            if (view) {
                const Point wa = view->transform.ToWorld(a), wb = view->transform.ToWorld(b);
                const Rect local(wa.h, wa.v - view->offsetY, wb.h - wa.h, wb.v - wa.v);
                for (const auto &ref : ElementsInRect(view->model, local, m_Preferences.Bool("Schematic\\Strict MultiSelection"))) {
                    if (ref.kind == ElementKind::LocalParam && !m_Navigation.options.showLocalParams) {
                        bool shown = true;
                        for (const auto &param : view->model.params)
                            if (param.ref == ref) shown = IsParamShown(param, false);
                        if (!shown) continue;
                    }
                    m_Navigation.selection.Add(view->id, ref);
                }
            }
            m_Tool = Tool::Idle;
        }
        break;
    }
    case Tool::Link: {
        ScriptView *view = ViewFor(m_Doc.linkScript);
        // Documentation "Schematic Keyboard Shortcuts": "ESC or right-click
        // or L - End link mode if in link mode".
        const bool endByL = Client().Binding("schematic.view.link-mode", EDITOR_KEY_PRESSED, true);
        if (!view || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || endByL) {
            m_Tool = Tool::Idle;
            m_Doc.relinkLink = 0;
            if (endByL) {
                m_LinkMode = false;
                m_LinkEndedFrame = ImGui::GetFrameCount();
            }
            break;
        }
        LinkEndpoint candidate;
        bool behaviorLink = false;
        Point target;
        const bool onEndpoint = hit.view == view && EndpointOf(hit, candidate, behaviorLink, target) &&
                                behaviorLink == m_LinkBehavior;
        ImVec2 from = view->transform.ToScreen(m_LinkFromPoint.h, m_LinkFromPoint.v + view->offsetY);
        const ImVec2 to = onEndpoint ? view->transform.ToScreen(target.h, target.v + view->offsetY) : mouse;
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImU32 color = onEndpoint ? m_Style.validLink : IM_COL32(240, 240, 240, 200);
        // Through the control points clicked so far.
        for (const Point &point : m_LinkPoints) {
            const ImVec2 at = view->transform.ToScreen(point.h, point.v + view->offsetY);
            drawList->AddLine(from, at, color, 2.0f);
            drawList->AddRectFilled(ImVec2(at.x - 2.0f, at.y - 2.0f), ImVec2(at.x + 3.0f, at.y + 3.0f), color);
            from = at;
        }
        drawList->AddLine(from, to, color, 2.0f);
        drawList->AddCircleFilled(to, 4.0f, color);
        if (m_LinkClick ? ImGui::IsMouseClicked(ImGuiMouseButton_Left) : !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // On nothing in the Script: a control point (a drag goes on as
            // clicks, documentation "Link"); else the link ends there.
            if (!m_Doc.relinkLink && hit.view == view && !hit.ref.IsValid() && !hit.onHeader) {
                m_LinkPoints.push_back(hit.local);
                m_LinkClick = true;
            } else {
                EndLink(hit);
            }
        }
        break;
    }
    case Tool::ExportDrag:
        UpdateExportDrag(mouse);
        break;
    case Tool::CopyDrag: {
        ScriptView *view = ViewFor(m_Doc.copyScript);
        if (!view) {
            m_Tool = Tool::Idle;
            break;
        }
        const Point world = view->transform.ToWorld(mouse);
        const float dx = world.h - m_PressWorld.h, dy = world.v - m_PressWorld.v;
        const ImVec2 a = view->transform.ToScreen(m_Doc.copyBounds.hPos + dx, m_Doc.copyBounds.vPos + dy + view->offsetY);
        const ImVec2 b = view->transform.ToScreen(m_Doc.copyBounds.Right() + dx, m_Doc.copyBounds.Bottom() + dy + view->offsetY);
        ImGui::GetWindowDrawList()->AddRect(a, b, m_Style.selection, 0.0f, 0, 2.0f);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) EndCopyDrag(world);
        break;
    }
    case Tool::Move: {
        const ScriptView *view = ViewFor(m_Doc.moveScript);
        // Moving Start alone: frame units (it does not zoom with the body).
        const bool startOnly = m_Doc.press.ref.kind == ElementKind::Start && m_Navigation.selection.items.size() == 1;
        const Point world = !view       ? m_View.ToWorld(mouse)
                            : startOnly ? view->frame.ToWorld(mouse)
                                        : view->transform.ToWorld(mouse);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            UpdateMove(world);
            EndMove(true);
        } else {
            UpdateMove(world);
        }
        break;
    }
    }
    AutoScroll(canvasMin, canvasMax);

    if (!focused || m_Tool != Tool::Idle || io.WantTextInput ||
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return;
    Client().Shortcuts();
}

} // namespace vdev
