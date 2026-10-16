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
#include "core/schematic/LinkRouting.h"
#include "core/level/SceneCommands.h"
#include "ui/schematic/SchematicCanvasDetail.h"
#include "core/graph/ShortcutCommands.h"
#include "core/schematic/SchematicEdits.h"
#include "core/schematic/SchematicGrid.h"
#include "core/schematic/SchematicHitTest.h"
#include "core/graph/ScriptCommands.h"
#include "ui/schematic/Snapshots.h"
#include "core/runtime/TextEncoding.h"
#include "core/graph/ValueCommands.h"

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
        const CK_ID under = ConditionParameterAt(hit);
        CKObject *object = under ? m_State.document->Context()->GetObject(under) : nullptr;
        ImGui::SetTooltip("%s in Schematic%s%s", m_Doc.pickReference ? "Pick Reference Parameter" : "Pick Schematic Parameter",
                          object ? ": " : "", object ? AnsiToUtf8(object->GetName() ? object->GetName() : "?").c_str() : "");
    }
    m_Doc.hovered = hit.ref;
    m_Doc.hoveredScript = hit.view ? hit.view->id : 0;
    if (hit.view) {
        m_Doc.mouseScript = hit.view->id;
        m_Doc.mouseLocal = hit.local;
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

    const bool spaceDown = focused && ImGui::IsKeyDown(ImGuiKey_Space);
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
            ScriptSession *session = m_State.document->Session(hit.view->id);
            if (session && !session->ReadOnly()) {
                m_Doc.moveScript = hit.view->id;
                m_Doc.moveOriginal = session->Data();
                m_Doc.moveOriginal.observers.clear();
                m_Doc.moveChanged = false;
                m_Doc.gesture = std::make_unique<Edit>(
                    *m_State.document, handle.kind == ElementKind::Behavior ? "Resize Graph" : "Resize Comment",
                    Edit::Marks::Scripts);
                m_Doc.resizeRef = handle;
                m_ResizeEdges = handleEdges;
                m_PressWorld = hit.local;
                m_ResizeStart = Rect();
                for (const auto &comment : hit.view->model.comments)
                    if (comment.ref == handle) m_ResizeStart = comment.bounds;
                if (const BehaviorItem *graph = hit.view->model.FindBehavior(handle.object))
                    if (handle.kind == ElementKind::Behavior) m_ResizeStart = graph->box;
                m_State.selection.Set(hit.view->id, handle);
                m_Tool = Tool::ResizeElement;
                break;
            }
        }
        if (edge && !spaceDown && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ScriptSession *session = m_State.document->Session(edge->id);
            if (session && !session->ReadOnly()) {
                m_Doc.resizeScript = edge->id;
                m_Doc.moveScript = edge->id;
                m_Doc.moveOriginal = session->Data();
                m_Doc.moveOriginal.observers.clear();
                m_Doc.moveChanged = false;
                m_Doc.gesture = std::make_unique<Edit>(*m_State.document, "Resize Script", Edit::Marks::Scripts);
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
            m_PressScreen = mouse;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_Tool = Tool::Pending;
            m_DragButton = ImGuiMouseButton_Right;
            m_Doc.press = hit;
            m_PressScreen = mouse;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (spaceDown) {
                m_Tool = Tool::Pan;
                m_DragButton = ImGuiMouseButton_Left;
                m_SpacePanned = true;
            } else if (m_Doc.pickBreakpoint) {
                // Add Breakpoint (Script Debugger): the element clicked.
                m_Doc.pickBreakpoint = false;
                if (hit.view && hit.ref.IsValid()) m_State.selection.Set(hit.view->id, hit.ref);
                else m_State.selection.Clear();
                ToggleBreakpointOfSelection();
            } else if (m_Doc.pickConditionRow >= 0) {
                // Breakpoint condition: a local, a shortcut or a behavior parameter.
                const CK_ID picked = ConditionParameterAt(hit);
                if (picked) PickConditionParameter(picked);
                else m_State.Log(LogLevel::Info, "Pick a parameter: a local parameter, a shortcut or a parameter pin");
            } else if (m_Doc.pickShortcutFrom) {
                // Set Shortcut Source: a local, a shortcut or an output picked here.
                CK_ID picked = 0;
                if (hit.view && (hit.ref.kind == ElementKind::LocalParam || hit.ref.kind == ElementKind::Shortcut)) {
                    for (const auto &param : hit.view->model.params)
                        if (param.ref == hit.ref) picked = param.parameter;
                } else if (hit.view && hit.ref.kind == ElementKind::Pin && hit.ref.pin == PinKind::ParamOut) {
                    CKObject *owner = m_State.document->Context()->GetObject(hit.ref.object);
                    CKParameterOut *out = nullptr;
                    if (CKBehavior *behavior = CKBehavior::Cast(owner)) out = behavior->GetOutputParameter(hit.ref.index);
                    if (CKParameterOperation *operation = CKParameterOperation::Cast(owner)) out = operation->GetOutParameter();
                    picked = out ? out->GetID() : 0;
                }
                const CK_ID from = m_Doc.pickShortcutFrom;
                m_Doc.pickShortcutFrom = 0;
                if (picked) RetargetAllShortcuts(from, picked);
                else m_State.Log(LogLevel::Info, "Set Shortcut Source: pick a local parameter, a shortcut or an output");
            } else if (io.KeyCtrl && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hit.view && !hit.onHeader) {
                // Dev: Ctrl+double-click adds a Building Block by name.
                OpenQuickAdd(hit.view->id, hit.local);
            } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                DoubleClick(hit);
            } else if (m_DrawGraphMode && hit.view && !hit.onHeader) {
                m_Tool = Tool::RectSelect;
                m_DragButton = ImGuiMouseButton_Left;
                m_PressScreen = mouse;
            } else if (m_State.view.zoomMode) {
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
                    m_Doc.replaceBehavior = io.KeyShift && IsBuildingBlockAt(m_Doc.press) ? m_Doc.press.ref.object : 0;
                    m_Doc.openBlockMenu = true;
                }
                break;
            }
            if (m_DragButton == ImGuiMouseButton_Left && linkable && !io.KeyCtrl &&
                (m_State.view.autoLink || m_LinkMode) &&
                EndpointOf(m_Doc.press, m_Doc.linkFrom, m_LinkBehavior, m_LinkFromPoint)) {
                // Click on an I/O: the link follows the mouse until the next click.
                Click(m_Doc.press);
                m_Doc.linkScript = m_Doc.press.view->id;
                m_LinkClick = true;
                m_LinkPoints.clear();
                m_Tool = Tool::Link;
                break;
            }
            if (m_DragButton == ImGuiMouseButton_Left) {
                Click(m_Doc.press);
            } else if (io.KeyShift && m_Doc.press.view) {
                m_Doc.blockMenuScript = m_Doc.press.view->id;
                m_Doc.blockMenuLocal = m_Doc.press.local;
                // Dev: Shift+right click on a Building Block replaces it by a recent one.
                m_Doc.replaceBehavior = IsBuildingBlockAt(m_Doc.press) ? m_Doc.press.ref.object : 0;
                m_Doc.openRecentMenu = true;
            } else {
                m_Doc.menuHit = m_Doc.press;
                m_Doc.menuScripts.clear();
                // Ctrl + right-click on a local keeps the Script headers selected (its menu
                // converts to a shortcut source in those Scripts only).
                const bool keepHeaders = io.KeyCtrl && m_Doc.press.ref.kind == ElementKind::LocalParam &&
                                         m_State.selection.OnlyHeaders();
                if (keepHeaders) m_Doc.menuScripts = m_State.selection.Scripts();
                else if (m_Doc.press.view && m_Doc.press.ref.IsValid() && !IsSelected(m_Doc.press.view->id, m_Doc.press.ref))
                    m_State.selection.Set(m_Doc.press.view->id, m_Doc.press.ref);
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
            } else {
                m_Tool = Tool::Idle;
            }
        } else if (linkFrom && EndpointOf(m_Doc.press, m_Doc.linkFrom, m_LinkBehavior, m_LinkFromPoint)) {
            m_Doc.linkScript = m_Doc.press.view->id;
            m_LinkClick = false;
            m_LinkPoints.clear();
            m_Doc.relinkLink = 0;
            m_Tool = Tool::Link;
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
        ScriptSession *session = m_State.document ? m_State.document->Session(m_Doc.resizeScript) : nullptr;
        if (!session || !m_Doc.gesture || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            EndMove(false);
            m_Doc.resizeScript = 0;
            break;
        }
        const float raw = (mouse.y - m_ResizeTop) / m_View.zoom;
        const float height = std::max(MIN_OPEN_SCRIPT_HEIGHT, std::round(raw / 20.0f) * 20.0f);
        if (height != session->Data().header.vSize)
            if (InterfaceData *data = m_Doc.gesture->ScriptData(m_Doc.resizeScript)) {
                data->header.vSize = height;
                m_Doc.moveChanged = true;
            }
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
                                            m_State.style.selection, 3.0f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_Tool = Tool::Idle;
            break;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            std::vector<CK_ID> order;
            for (const auto &view : m_Views) order.push_back(view.id);
            auto from = std::find(order.begin(), order.end(), m_Doc.reorderScript);
            if (from != order.end()) {
                size_t at = static_cast<size_t>(from - order.begin());
                order.erase(from);
                if (insert > at) --insert;
                order.insert(order.begin() + std::min(insert, order.size()), m_Doc.reorderScript);
                // Hidden Scripts keep their place after the shown ones.
                for (CK_ID id : m_State.visibleScripts)
                    if (std::find(order.begin(), order.end(), id) == order.end()) order.push_back(id);
                m_State.scriptOrder = order;
                m_State.events.Post(RefreshScriptStack{});
            }
            m_Tool = Tool::Idle;
        }
        break;
    }
    case Tool::ResizeElement: {
        ScriptView *view = ViewFor(m_Doc.moveScript);
        if (!view || !m_Doc.gesture || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
        // The data from the original each frame (back to it when the size comes back).
        if (m_Doc.moveChanged || !m_Doc.gesture->Empty())
            if (InterfaceData *data = m_Doc.gesture->ScriptData(m_Doc.moveScript)) {
                *data = m_Doc.moveOriginal;
                if (graph) {
                    if (BehaviorData *record = FindRecord(*data, m_Doc.resizeRef.object)) {
                        record->hExpandSize += width - m_ResizeStart.hSize;
                        record->vExpandSize += height - m_ResizeStart.vSize;
                    }
                } else if (BehaviorData *record = FindRecord(*data, m_Doc.resizeRef.owner)) {
                    if (m_Doc.resizeRef.index >= 0 && m_Doc.resizeRef.index < static_cast<int>(record->comments.size())) {
                        Comment &comment = record->comments[m_Doc.resizeRef.index];
                        Rect rect = comment.GetRect();
                        rect.hSize = width;
                        rect.vSize = height;
                        comment.SetRect(rect);
                    }
                }
            }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) EndMove(m_Doc.moveChanged);
        break;
    }
    case Tool::RectSelect: {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImVec2 a(std::min(m_PressScreen.x, mouse.x), std::min(m_PressScreen.y, mouse.y));
        const ImVec2 b(std::max(m_PressScreen.x, mouse.x), std::max(m_PressScreen.y, mouse.y));
        drawList->AddRectFilled(a, b, m_State.style.rubberBand);
        drawList->AddRect(a, b, m_State.style.rubberBandBorder);
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
            if (!io.KeyCtrl && !io.KeyShift) m_State.selection.Clear();
            if (view) {
                const Point wa = view->transform.ToWorld(a), wb = view->transform.ToWorld(b);
                const Rect local(wa.h, wa.v - view->offsetY, wb.h - wa.h, wb.v - wa.v);
                for (const auto &ref : ElementsInRect(view->model, local, m_State.preferences.strictMultiSelection)) {
                    if (ref.kind == ElementKind::LocalParam && !m_State.view.showLocalParams) {
                        bool shown = true;
                        for (const auto &param : view->model.params)
                            if (param.ref == ref) shown = IsParamShown(param, false);
                        if (!shown) continue;
                    }
                    m_State.selection.Add(view->id, ref);
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
        const bool endByL = ImGui::IsKeyPressed(ImGuiKey_L, false) && !io.WantTextInput;
        if (!view || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || endByL) {
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
        const ImU32 color = onEndpoint ? m_State.style.validLink : IM_COL32(240, 240, 240, 200);
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
        if (!view || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_Tool = Tool::Idle;
            break;
        }
        const Point world = view->transform.ToWorld(mouse);
        const float dx = world.h - m_PressWorld.h, dy = world.v - m_PressWorld.v;
        const ImVec2 a = view->transform.ToScreen(m_Doc.copyBounds.hPos + dx, m_Doc.copyBounds.vPos + dy + view->offsetY);
        const ImVec2 b = view->transform.ToScreen(m_Doc.copyBounds.Right() + dx, m_Doc.copyBounds.Bottom() + dy + view->offsetY);
        ImGui::GetWindowDrawList()->AddRect(a, b, m_State.style.selection, 0.0f, 0, 2.0f);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) EndCopyDrag(world);
        break;
    }
    case Tool::Move: {
        const ScriptView *view = ViewFor(m_Doc.moveScript);
        // Moving Start alone: frame units (it does not zoom with the body).
        const bool startOnly = m_Doc.press.ref.kind == ElementKind::Start && m_State.selection.items.size() == 1;
        const Point world = !view       ? m_View.ToWorld(mouse)
                            : startOnly ? view->frame.ToWorld(mouse)
                                        : view->transform.ToWorld(mouse);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            EndMove(false);
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            UpdateMove(world);
            EndMove(true);
        } else {
            UpdateMove(world);
        }
        break;
    }
    }
    AutoScroll(canvasMin, canvasMax);

    if (!focused || m_Tool != Tool::Idle || io.WantTextInput) return;
    // Schematic shortcuts (UI reference section 8.4); library choices are noted.
    if (ImGui::IsKeyReleased(ImGuiKey_Space)) {
        // Dev Spacebar: display of selected locals, iconify comments, open/close graphs and Scripts.
        if (!m_SpacePanned) SpaceAction();
        m_SpacePanned = false;
    }
    if (m_DrawGraphMode && ImGui::IsKeyPressed(ImGuiKey_Escape)) m_DrawGraphMode = false;
    if (m_Doc.pickShortcutFrom && ImGui::IsKeyPressed(ImGuiKey_Escape)) m_Doc.pickShortcutFrom = 0;
    if (m_Doc.pickBreakpoint && ImGui::IsKeyPressed(ImGuiKey_Escape)) m_Doc.pickBreakpoint = false;
    if (m_Doc.pickConditionRow >= 0 && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_Doc.pickConditionRow = -1;
        m_Doc.openBreakpointConfig = true; // back to the dialog
    }
    if (io.KeyCtrl || io.KeyAlt) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) SelectAll();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) Copy();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X)) Cut();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) io.KeyShift ? PasteShortcuts() : Paste();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) Duplicate();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q)) InitialConditionsOfScripts(1); // Remove
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Q)) InitialConditionsOfScripts(2);  // Restore
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_S)) SaveSelectionAsNms();          // Dev: Save As...
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_X)) ExecuteSelectedScripts();      // Dev: Execute Script(s)
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_T) && m_State.debugger)           // Dev: Trace Mode
            m_State.debugger->trace = !m_State.debugger->trace;
        // Dev Ctrl+B: select/deselect the highlighted Script.
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_B) && m_Doc.currentScript) {
            ScriptView *current = ViewFor(m_Doc.currentScript);
            if (current && IsSelected(current->id, current->model.script.ref)) m_State.selection.Clear();
            else if (current) m_State.selection.Set(current->id, current->model.script.ref);
        }
        // Dev Ctrl+Up/Down: move the highlighted Script when a Script is selected, else page scroll.
        if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))) {
            const int step = ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1 : 1;
            bool script = false;
            for (const auto &item : m_State.selection.items) script = script || item.kind == ElementKind::Script;
            if (script) MoveHighlightedScript(step);
            else PanBy(0.0f, -step * (canvasMax.y - canvasMin.y) * 0.8f);
        }
        // Dev: Alt+L adds a local parameter, Alt+P a parameter operation.
        if (io.KeyAlt && hovered && hit.view && ImGui::IsKeyPressed(ImGuiKey_L))
            OpenLocalParameterPicker(hit.view->id, hit.local);
        if (io.KeyAlt && hovered && hit.view && ImGui::IsKeyPressed(ImGuiKey_P))
            OpenOperationPicker(hit.view->id, hit.local);
        if (io.KeyAlt && hovered && hit.view && ImGui::IsKeyPressed(ImGuiKey_T)) AddThisAt(hit.view->id, hit.local);
        // Alt+I / Alt+O: parameter input / output of the selected graph.
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_I)) OpenGraphParameterPicker(true);
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_O)) OpenGraphParameterPicker(false);
        return;
    }
    if (io.KeyShift) {
        // Dev Shift+Up/Down: select the previous / next Script and scroll to it.
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) InitialConditionsOfScripts(0); // Set
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) SelectAdjacentScript(-1);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) SelectAdjacentScript(1);
        if (ImGui::IsKeyPressed(ImGuiKey_R)) Refresh();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) UngroupSelection();
        if (ImGui::IsKeyPressed(ImGuiKey_F9)) OpenBreakpointConfig(); // Dev: Configure Breakpoint
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F9)) ToggleBreakpointOfSelection(); // Dev: Add/Remove Breakpoint
    // Dev G: Draw Behavior Graph mode (the next drag draws the graph).
    if (ImGui::IsKeyPressed(ImGuiKey_G)) BeginDrawGraph();
    if (ImGui::IsKeyPressed(ImGuiKey_F1) || ImGui::IsKeyPressed(ImGuiKey_K)) {
        const Selection &selection = m_State.selection;
        if (selection.items.size() == 1) {
            const ElementRef &item = selection.items[0];
            if (ImGui::IsKeyPressed(ImGuiKey_F1) && item.kind == ElementKind::Behavior) OpenHtmlHelp(item.object);
            if (ImGui::IsKeyPressed(ImGuiKey_K) &&
                (item.kind == ElementKind::LocalParam || item.kind == ElementKind::Shortcut)) {
                m_Doc.ckPropertiesObject = item.object;
                m_Doc.openCkProperties = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_K) && item.kind == ElementKind::Pin) {
                // A parameter input/output or the Target of a behavior, or a
                // parameter operation's inputs and output.
                CKObject *ownerObject = m_State.document->Context()->GetObject(item.object);
                CKBehavior *owner = CKBehavior::Cast(ownerObject);
                CKParameterOperation *operation = CKParameterOperation::Cast(ownerObject);
                CKObject *pin = owner ? (item.pin == PinKind::ParamIn    ? static_cast<CKObject *>(owner->GetInputParameter(item.index))
                                         : item.pin == PinKind::ParamOut ? static_cast<CKObject *>(owner->GetOutputParameter(item.index))
                                         : item.pin == PinKind::Target   ? static_cast<CKObject *>(owner->GetTargetParameter())
                                                                         : nullptr)
                                : operation ? (item.pin == PinKind::ParamIn
                                                   ? (item.index == 0   ? static_cast<CKObject *>(operation->GetInParameter1())
                                                      : item.index == 1 ? static_cast<CKObject *>(operation->GetInParameter2())
                                                                        : nullptr)
                                               : item.pin == PinKind::ParamOut ? static_cast<CKObject *>(operation->GetOutParameter())
                                                                               : nullptr)
                                            : nullptr;
                if (pin) {
                    m_Doc.ckPropertiesObject = pin->GetID();
                    m_Doc.openCkProperties = true;
                }
            }
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_T)) ToggleTargetOfSelection();
    if (ImGui::IsKeyPressed(ImGuiKey_I)) AddGraphIo(true);
    if (ImGui::IsKeyPressed(ImGuiKey_O)) AddGraphIo(false);
    if (ImGui::IsKeyPressed(ImGuiKey_E) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
        // Dev E / Return: the selected item's parameters - a behavior or operation,
        // a link's delay, a local's or a shortcut's value.
        const Selection &selection = m_State.selection;
        ScriptView *view = ViewFor(selection.script);
        const ElementRef item = selection.items.size() == 1 ? selection.items[0] : ElementRef();
        if (view && item.kind == ElementKind::Link) {
            for (const auto &link : view->model.links)
                if (link.ref == item && !link.conversion.empty())
                    OpenOperationChoice(OperationChoice::ReplaceConversion, view->id, link.start, link.end, link.ref);
            for (const auto &link : view->model.links)
                if (link.ref == item && link.behaviorLink) {
                    m_Doc.dialogScript = view->id;
                    m_Doc.delayLink = link.ref.object;
                    m_DelayValue = link.delay;
                    m_Doc.openDelay = true;
                }
        } else if (view && (item.kind == ElementKind::LocalParam || item.kind == ElementKind::Shortcut)) {
            OpenValueDialog(view->id, m_State.document->Context()->GetObject(item.object));
        } else {
            OpenParametersDialog();
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_P)) UpdateLinkPaths();
    if (ImGui::IsKeyPressed(ImGuiKey_M)) OpenModifyOperation(); // Dev: Modify Parameter Operation
    if (ImGui::IsKeyPressed(ImGuiKey_H)) m_ShowMessages = !m_ShowMessages; // Dev: Message Explorer
    if (ImGui::IsKeyPressed(ImGuiKey_Y)) OpenPriorityDialog();
    if (ImGui::IsKeyPressed(ImGuiKey_S)) OpenSettingsDialog(); // Dev: Edit Settings
    // Dev link mode: L links with clicks; A / Z switch auto-link on / off.
    if (ImGui::IsKeyPressed(ImGuiKey_L) && m_LinkEndedFrame != ImGui::GetFrameCount()) m_LinkMode = !m_LinkMode;
    if (ImGui::IsKeyPressed(ImGuiKey_A)) m_State.view.autoLink = true;
    if (ImGui::IsKeyPressed(ImGuiKey_Z)) m_State.view.autoLink = false;
    if (ImGui::IsKeyPressed(ImGuiKey_R)) ResetView();
    // Dev Home / End scroll to the first / last Script.
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) ScrollToEnd(false);
    if (ImGui::IsKeyPressed(ImGuiKey_End)) ScrollToEnd(true);
    // Dev +/- (Dev.exe sub_52C680, keys 107 and 109; documentation "3
    // predefined levels"): the global zoom of the whole Schematic set to
    // 50%, 75% or 100% - "+" 75% below it, else 100%; "-" 50% at or below
    // 75%, else 75% - about the view's center. Each Script keeps its own
    // scale on top.
    if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd) ||
        ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
        const bool in = ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd);
        const float before = std::max(0.01f, m_View.zoom);
        const float after = in ? (before < 0.75f ? 0.75f : 1.0f) : (before <= 0.75f ? 0.5f : 0.75f);
        const float halfW = m_CanvasSize.x * 0.5f, halfH = m_CanvasSize.y * 0.5f;
        m_ScrollX = std::max(0.0f, m_ScrollX + halfW / before - halfW / after);
        m_View.panY += halfH / before - halfH / after;
        m_View.zoom = after;
        PanBy(0.0f, 0.0f); // the stack kept in its bounds
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (m_LinkMode) m_LinkMode = false;
        else m_State.selection.Clear();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) DeleteSelection();
    if (ImGui::IsKeyPressed(ImGuiKey_C) && hovered && hit.view) AddComment(hit.view->id, hit.local);
    if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
        const Selection &selection = m_State.selection;
        if (selection.items.size() == 1 && selection.items[0].kind != ElementKind::Comment) BeginRename();
        if (selection.items.size() == 1 && selection.items[0].kind == ElementKind::Comment) {
            if (ScriptView *view = ViewFor(selection.script)) {
                for (const auto &comment : view->model.comments) {
                    if (comment.ref != selection.items[0]) continue;
                    m_Doc.commentScript = selection.script;
                    m_Doc.commentRef = comment.ref;
                    m_CommentText = comment.text;
                    m_Doc.openCommentEditor = true;
                }
            }
        }
    }
    const float step = 60.0f / m_View.zoom;
    // Dev: Right/Left open/close the selected Scripts; otherwise arrows scroll.
    const std::vector<CK_ID> selectedScripts = m_State.selection.Scripts();
    if (!selectedScripts.empty() &&
        (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_RightArrow))) {
        const bool open = ImGui::IsKeyPressed(ImGuiKey_RightArrow);
        for (CK_ID script : selectedScripts) {
            ScriptSession *session = m_State.document->Session(script);
            if (session && session->IsOpen() != open) ToggleScriptOpen(script);
        }
        return;
    }
    const float page = (canvasMax.y - canvasMin.y) * 0.8f;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) PanBy(step * m_View.zoom, 0.0f);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) PanBy(-step * m_View.zoom, 0.0f);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) PanBy(0.0f, step * m_View.zoom);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) PanBy(0.0f, -step * m_View.zoom);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) PanBy(0.0f, page);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) PanBy(0.0f, -page);
}

} // namespace vdev
