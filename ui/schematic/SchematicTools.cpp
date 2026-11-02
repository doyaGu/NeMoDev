#include "ui/schematic/SchematicViews.h"
#include "ui/schematic/SchematicCanvas.h"
#include "ui/schematic/SchematicCanvasDetail.h"
#include "editor/ImGuiDrawing.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "imgui_internal.h"
#include <algorithm>

namespace vdev {
using namespace canvas;
namespace {
struct Control {
    const char *id, *title;
    int icons[5];
    float width = 17, height = 19;
    bool pill = false;
};
const Control controls[] = {{"expand", "Expand Selected Scripts", {976, 977, 973, 973, 976}},
                            {"collapse", "Collapse Selected Scripts", {978, 974, 975, 975, 978}},
                            {"explorer", "Schematic Explorer", {777, 779, 778, 778, 777}, 19},
                            {"messages", "Message Explorer", {1293, 1291, 1292, 1290, 1293}},
                            {"find", "Find", {1353, 1346, 1347, 1347, 1353}},
                            {"separator1", "", {}, 1},
                            {"link-mode", "Link Mode", {}, 66, 15, true},
                            {"separator2", "", {}, 1},
                            {"filter", "Scene/Level Filter", {1355, 1356, 1357, 1354, 1357}, 19},
                            {"showall", "Show All", {}, 70, 15, true},
                            {"hideall", "Hide All", {}, 70, 15, true},
                            {"hidesel", "Hide Selected", {}, 70, 15, true},
                            {"hideother", "Hide Other", {}, 70, 15, true},
                            {"separator3", "", {}, 1},
                            {"locals", "Show/Hide Local Parameters", {1301, 1299, 1300, 1298, 1301}},
                            {"headers", "Show/Hide Script Headers", {989, 990, 991, 985, 989}},
                            {"points", "Show/Hide Control Points", {992, 986, 987, 988, 992}},
                            {"info", "Show/Hide Link Information", {1002, 1003, 1004, 1005, 1002}},
                            {"linkprops", "Behavior Link Properties", {1308, 1307, 1306, 1306, 1308}},
                            {"priorities", "Show/Hide Priorities", {972, 970, 971, 969, 972}},
                            {"separator4", "", {}, 1},
                            {"reset", "Reset Schematic Zoom and Position", {877, 878, 879, 880, 877}},
                            {"zoom", "Zoom Mode", {893, 894, 895, 845, 893}},
                            {"separator5", "", {}, 1},
                            {"debugger", "Script Debugger", {1080, 1078, 1079, 1077, 1080}},
                            {"trace", "Trace", {}, 66, 15, true},
                            {"linkerrors", "Browse Invalid Links", {1335, 1334, 1333, 1333, 1335}},
                            {"zoom-label", "", {}, 60}};
const char *ExistingAction(const std::string &id) {
    if (id == "explorer") return "schematic.open-explorer";
    if (id == "debugger") return "runtime.toggle-debugger";
    if (id == "trace") return "runtime.toggle-trace";
    if (id == "linkprops") return "environment.open-link-options";
    return nullptr;
}
struct Key {
    const char *action, *chord;
    EditorInputPhase phase = EDITOR_KEY_PRESSED;
};
const Key keys[] = {{"find", "Ctrl+F"},
                    {"find-next", "F3"},
                    {"find-previous", "Shift+F3"},
                    {"find-selection", "Ctrl+F3"},
                    {"find-selection-backward", "Ctrl+Shift+F3"},
                    {"select-all", "Ctrl+A"},
                    {"copy", "Ctrl+C"},
                    {"cut", "Ctrl+X"},
                    {"paste", "Ctrl+V"},
                    {"paste-shortcuts", "Ctrl+Shift+V"},
                    {"duplicate", "Ctrl+D"},
                    {"remove-initial", "Ctrl+Q"},
                    {"restore-initial", "Alt+Q"},
                    {"save-selection", "Alt+S"},
                    {"execute-scripts", "Alt+X"},
                    {"select-current", "Ctrl+B"},
                    {"move-up", "Ctrl+Up"},
                    {"move-down", "Ctrl+Down"},
                    {"add-local", "Alt+L"},
                    {"add-operation", "Alt+P"},
                    {"add-this", "Alt+T"},
                    {"parameter-in", "Alt+I"},
                    {"parameter-out", "Alt+O"},
                    {"set-initial", "Shift+Q"},
                    {"previous-script", "Shift+Up"},
                    {"next-script", "Shift+Down"},
                    {"refresh", "Shift+R"},
                    {"ungroup", "Shift+Delete"},
                    {"breakpoint-config", "Shift+F9"},
                    {"breakpoint", "F9"},
                    {"draw-graph", "G"},
                    {"help", "F1"},
                    {"properties", "K"},
                    {"toggle-target", "T"},
                    {"add-input", "I"},
                    {"add-output", "O"},
                    {"edit", "E"},
                    {"edit", "Enter"},
                    {"edit", "KeypadEnter"},
                    {"link-paths", "P"},
                    {"modify-operation", "M"},
                    {"messages", "H"},
                    {"priority", "Y"},
                    {"settings", "S"},
                    {"link-mode", "L"},
                    {"auto-link", "A"},
                    {"manual-link", "Z"},
                    {"reset", "R"},
                    {"home", "Home"},
                    {"end", "End"},
                    {"zoom-in", "Equal"},
                    {"zoom-in", "KeypadAdd"},
                    {"zoom-out", "Minus"},
                    {"zoom-out", "KeypadSubtract"},
                    {"cancel", "Escape"},
                    {"delete", "Delete"},
                    {"comment", "C"},
                    {"rename", "F2"},
                    {"left", "Left"},
                    {"right", "Right"},
                    {"up", "Up"},
                    {"down", "Down"},
                    {"page-up", "PageUp"},
                    {"page-down", "PageDown"},
                    {"space-action", "Space", EDITOR_KEY_RELEASED}};
} // namespace
bool SchematicViews::RegisterControls(EditorApplication &editor, EditorModuleDesc module, std::string &error) {
    m_ToolActions.clear();
    std::vector<EditorCommandDesc> commands(module.commands, module.commands + module.commandCount);
    std::vector<EditorToolbarItemDesc> items;
    std::vector<EditorBindingDesc> bindings;
    const EditorToolbarLocation location{EDITOR_TEXT("schematic.tools"), EDITOR_TEXT("Schematic tools"),
                                         EDITOR_TEXT("script.schematic")};
    auto add = [&](const std::string &id, const char *title) {
        if (std::any_of(m_ToolActions.begin(), m_ToolActions.end(), [&](const auto &a) { return a.id == id; })) return;
        m_ToolActions.push_back({this, id});
        auto &a = m_ToolActions.back();
        EditorCommandDesc d{};
        d.size = sizeof(d);
        d.id = EditorText(a.id);
        d.provider = EDITOR_TEXT("builtin.schematic-tools");
        d.title = EditorLiteral(title);
        d.view = EDITOR_TEXT("script.schematic");
        d.area = EDITOR_WORKSPACE;
        d.effect = EDITOR_SESSION_CHANGE;
        d.data = &a;
        d.run = [](void *data, const EditorHost *, const EditorRequest *r, EditorSink) {
            auto &a = *static_cast<ToolAction *>(data);
            auto *canvas = a.views->Find(r->scope.view);
            if (!canvas) return EDITOR_STALE;
            canvas->RunToolAction(a.id.substr(15));
            return EDITOR_OK;
        };
        d.state = [](void *data, const EditorHost *, const EditorRequest *r) {
            auto &a = *static_cast<ToolAction *>(data);
            auto *canvas = a.views->Find(r->scope.view);
            if (!canvas) return EDITOR_STALE;
            const auto id = a.id.substr(15);
            if ((id == "find-next" || id == "find-previous") && !canvas->Finding()) return EDITOR_REFUSED;
            if (id == "add-this" || id == "add-local" || id == "add-operation" || id == "comment") {
                if (!ImGui::IsMouseHoveringRect(canvas->m_PickMin, canvas->m_PickMax, false) ||
                    !canvas->HitAt(ImGui::GetIO().MousePos).view)
                    return EDITOR_REFUSED;
            }
            return EDITOR_OK;
        };
        commands.push_back(d);
    };
    for (const auto &key : keys) {
        const std::string id = "schematic.view." + std::string(key.action);
        add(id, key.action);
        bindings.push_back({EditorText(m_ToolActions.back().id), EditorLiteral(key.chord),
                            EDITOR_TEXT("script.schematic"), key.phase});
        // An action can have several chords; use its own stable ID, not the last inserted action.
        bindings.back().action = std::find_if(commands.begin(), commands.end(), [&](const auto &d) {
                                     return std::string(d.id.data, d.id.size) == id;
                                 })->id;
    }
    float x = 9;
    for (const auto &control : controls) {
        const std::string id = "schematic.view." + std::string(control.id);
        const bool action = control.title[0] != 0;
        const auto *existing = ExistingAction(control.id);
        if (action && !existing) add(id, control.title);
        m_ToolActions.push_back({this, id});
        auto &a = m_ToolActions.back();
        EditorToolbarItemDesc d{};
        d.size = sizeof(d);
        d.imguiVersion = EDITOR_IMGUI_VERSION;
        d.id = EditorText(a.id);
        d.provider = EDITOR_TEXT("builtin.schematic-toolbar");
        d.location = location.id;
        d.title = EditorLiteral(control.title);
        if (action) d.action = existing ? EditorLiteral(existing) : d.id;
        d.x = x;
        d.y = control.pill ? 3 : 1;
        d.width = control.width;
        d.height = control.height;
        d.data = &a;
        d.draw = [](void *data, const EditorHost *, const EditorRequest *r, const EditorToolbarFrame *frame) {
            auto &a = *static_cast<ToolAction *>(data);
            auto *canvas = a.views->Find(r->scope.view);
            return canvas ? canvas->DrawToolControl(a.id.substr(15), *frame) : 0u;
        };
        items.push_back(d);
        x += control.width + 4;
    }
    std::vector<EditorToolDesc> tools;
    const char *toolIds[] = {"schematic.view.edit-tool", "schematic.view.link-tool", "schematic.view.zoom-tool",
                             "schematic.view.draw-tool"};
    const char *toolNames[] = {"Edit Schematic", "Link Schematic", "Zoom Schematic", "Draw Behavior Graph"};
    for (unsigned i = 0; i < 4; ++i) {
        m_ToolActions.push_back({this, toolIds[i]});
        auto &action = m_ToolActions.back();
        EditorToolDesc tool{};
        tool.size = sizeof(tool);
        tool.id = EditorText(action.id);
        tool.provider = EDITOR_TEXT("builtin.schematic-tools");
        tool.title = EditorLiteral(toolNames[i]);
        tool.view = location.view;
        tool.data = &action;
        tool.input = [](void *data, const EditorHost *, const EditorRequest *r, const EditorToolInput *input,
                        uint32_t *flags) {
            auto &action = *static_cast<ToolAction *>(data);
            auto *canvas = action.views->Find(r->scope.view);
            if (!canvas) return EDITOR_STALE;
            if (input->phase == EDITOR_TOOL_ENTER) {
                if (action.id == "schematic.view.link-tool") canvas->m_LinkMode = true;
                if (action.id == "schematic.view.zoom-tool") canvas->m_Navigation.options.zoomMode = true;
                if (action.id == "schematic.view.draw-tool") {
                    canvas->m_DrawGraphMode = true;
                    canvas->Client().Log(
                        EDITOR_LOG_INFO,
                        "Draw Behavior Graph: drag a rectangle around the behaviors to group (Esc cancels)");
                }
                *flags = 0;
                return EDITOR_OK;
            }
            *flags = canvas->ToolFrame(*input);
            return EDITOR_OK;
        };
        tools.push_back(tool);
    }
    module.commands = commands.data();
    module.commandCount = uint32_t(commands.size());
    module.bindings = bindings.data();
    module.bindingCount = uint32_t(bindings.size());
    module.tools = tools.data();
    module.toolCount = uint32_t(tools.size());
    module.toolbars = &location;
    module.toolbarCount = 1;
    module.toolbarItems = items.data();
    module.toolbarItemCount = uint32_t(items.size());
    return editor.Register(module, error);
}

uint32_t SchematicCanvas::ToolFrame(const EditorToolInput &input) {
    if (input.phase == EDITOR_TOOL_CANCEL || input.phase == EDITOR_TOOL_LEAVE) CancelInteraction();
    if (input.phase == EDITOR_TOOL_FRAME) HandleInput(m_PickMin, m_PickMax, input.hovered != 0, input.focused != 0);
    const auto selected = Client().ActiveTool();
    const bool done = (selected == "schematic.view.draw-tool" && !m_DrawGraphMode) ||
                      (selected == "schematic.view.link-tool" && !m_LinkMode) ||
                      (selected == "schematic.view.zoom-tool" && !m_Navigation.options.zoomMode);
    return (Busy() ? EDITOR_TOOL_CAPTURED : 0) | (done ? EDITOR_TOOL_DEFAULT : 0);
}

uint32_t SchematicCanvas::DrawToolControl(const std::string &id, const EditorToolbarFrame &frame) {
    const auto c = std::find_if(std::begin(controls), std::end(controls), [&](const auto &c) { return id == c.id; });
    if (c == std::end(controls)) return 0;
    const ImVec2 at{IM_ROUND(frame.left), IM_ROUND(frame.top)};
    if (id.find("separator") == 0) {
        if (auto image = DevBitmap(705))
            ImGui::GetWindowDrawList()->AddImage(image, at, {at.x + frame.width, at.y + frame.height});
        return 0;
    }
    if (id == "zoom-label") {
        const auto *view = ViewFor(m_Doc.hoveredScript ? m_Doc.hoveredScript : m_Doc.currentScript);
        ImGui::SetCursorScreenPos(at);
        ImGui::TextDisabled("%.0f%%", (view ? view->Zoom() : m_View.zoom) * 100);
        return 0;
    }
    auto &options = m_Navigation.options;
    bool enabled = frame.enabled, checked = frame.checked;
    if (id == "expand" || id == "collapse" || id == "hidesel" || id == "hideother")
        enabled = enabled && ToolbarScript() != 0;
    if (id == "showall" || id == "hideall") enabled = enabled && Client().Scope().document.value;
    if (id == "messages") checked = DiagnosticOpen(false);
    if (id == "link-mode") checked = m_LinkMode;
    if (id == "filter") checked = options.sceneFilter;
    if (id == "locals") checked = !options.showLocalParams;
    if (id == "headers") checked = !options.showHeaders;
    if (id == "points") checked = !options.showControlPoints;
    if (id == "info") checked = !options.showLinkInfo;
    if (id == "priorities") checked = !options.showPriorities;
    if (id == "zoom") checked = options.zoomMode;
    if (id == "debugger")
        checked = EditorClient::Integer(Client().Query("workspace.role", {TextArgument("role", "main.debugger")}),
                                        "view") != 0;
    if (id == "trace") {
        const auto state = Client().Query("runtime.state");
        if (!EditorClient::Integer(state, "available")) return 0;
        checked = EditorClient::Integer(state, "trace") != 0;
    }
    if (id == "linkerrors") {
        if (!EditorClient::Integer(Client().Query("schematic.invalid-summary"), "total")) return 0;
        checked = DiagnosticOpen(true);
    }
    const char *tooltip = frame.tooltip.size ? frame.tooltip.data : c->title;
    const char *widget = id == "link-mode" ? "link" : id.c_str();
    const char *label = id == "link-mode" ? (options.autoLink ? "Auto Link" : "Link") : c->title;
    if (c->pill)
        return DevToolbarPill(widget, label, at, {at.x + frame.width, at.y + frame.height}, checked, tooltip, enabled);
    return DevBitmapButton(widget, at, c->icons, checked, enabled, frame.scale, tooltip);
}

void SchematicCanvas::RunToolAction(const std::string &id) {
    auto &options = m_Navigation.options;
    const bool hovered = ImGui::IsMouseHoveringRect(m_PickMin, m_PickMax, false);
    const auto hit = hovered ? HitAt(ImGui::GetIO().MousePos) : Hit{};
    const float page = m_CanvasSize.y * .8f;
    if (id == "find-next" || id == "find-previous")
        FindNext(id == "find-previous");
    else if (id == "find-selection" || id == "find-selection-backward")
        FindSelected(id == "find-selection-backward");
    else if (id == "select-all")
        SelectAll();
    else if (id == "copy")
        Copy();
    else if (id == "cut")
        Cut();
    else if (id == "paste")
        Paste();
    else if (id == "paste-shortcuts")
        PasteShortcuts();
    else if (id == "duplicate")
        Duplicate();
    else if (id == "remove-initial")
        InitialConditionsOfScripts(1);
    else if (id == "restore-initial")
        InitialConditionsOfScripts(2);
    else if (id == "set-initial")
        InitialConditionsOfScripts(0);
    else if (id == "save-selection")
        SaveSelectionAsNms();
    else if (id == "execute-scripts")
        ExecuteSelectedScripts();
    else if (id == "select-current") {
        auto *view = ViewFor(m_Doc.currentScript);
        if (view && IsSelected(view->id, view->model.script.ref))
            m_Navigation.selection.Clear();
        else if (view)
            m_Navigation.selection.Set(view->id, view->model.script.ref);
    } else if (id == "move-up" || id == "move-down") {
        const int step = id == "move-up" ? -1 : 1;
        const auto &items = m_Navigation.selection.items;
        if (std::any_of(items.begin(), items.end(), [](const auto &v) { return v.kind == ElementKind::Script; }))
            MoveHighlightedScript(step);
        else
            PanBy(0, -step * page);
    } else if (id == "add-local" && hit.view)
        OpenLocalParameterPicker(hit.view->id, hit.local);
    else if (id == "add-operation" && hit.view)
        OpenOperationPicker(hit.view->id, hit.local);
    else if (id == "add-this" && hit.view)
        AddThisAt(hit.view->id, hit.local);
    else if (id == "parameter-in")
        OpenGraphParameterPicker(true);
    else if (id == "parameter-out")
        OpenGraphParameterPicker(false);
    else if (id == "previous-script")
        SelectAdjacentScript(-1);
    else if (id == "next-script")
        SelectAdjacentScript(1);
    else if (id == "refresh")
        Refresh();
    else if (id == "ungroup")
        UngroupSelection();
    else if (id == "breakpoint-config")
        OpenBreakpointConfig();
    else if (id == "breakpoint")
        ToggleBreakpointOfSelection();
    else if (id == "draw-graph")
        BeginDrawGraph();
    else if (id == "help" || id == "properties") {
        const auto &selection = m_Navigation.selection;
        if (selection.items.size() == 1) {
            const auto &item = selection.items[0];
            if (id == "help" && item.kind == ElementKind::Behavior) OpenHtmlHelp(item.object);
            if (id == "properties" && (item.kind == ElementKind::LocalParam || item.kind == ElementKind::Shortcut ||
                                       item.kind == ElementKind::Pin)) {
                const auto info = ElementInfo(selection.script, item);
                if (info.status == EDITOR_OK)
                    if (const auto *object = EditorClient::Find(info, "object")) {
                        m_Doc.ckPropertiesObject = object->object;
                        m_Doc.openCkProperties = true;
                    }
            }
        }
    } else if (id == "toggle-target")
        ToggleTargetOfSelection();
    else if (id == "add-input")
        AddGraphIo(true);
    else if (id == "add-output")
        AddGraphIo(false);
    else if (id == "edit") {
        const auto &selection = m_Navigation.selection;
        auto *view = ViewFor(selection.script);
        const auto item = selection.items.size() == 1 ? selection.items[0] : ElementRef{};
        if (view && item.kind == ElementKind::Link) {
            for (const auto &link : view->model.links)
                if (link.ref == item) {
                    if (!link.conversion.empty())
                        OpenOperationChoice(OperationChoice::ReplaceConversion, view->id, link.start, link.end,
                                            link.ref);
                    if (link.behaviorLink) OpenLinkDelay(view->id, link.ref);
                }
        } else if (view && (item.kind == ElementKind::LocalParam || item.kind == ElementKind::Shortcut)) {
            const auto info = ElementInfo(view->id, item);
            if (info.status == EDITOR_OK)
                if (const auto *object = EditorClient::Find(info, "object")) OpenValueDialog(view->id, object->object);
        } else
            OpenParametersDialog();
    } else if (id == "link-paths")
        UpdateLinkPaths();
    else if (id == "modify-operation")
        OpenModifyOperation();
    else if (id == "priority")
        OpenPriorityDialog();
    else if (id == "settings")
        OpenSettingsDialog();
    else if (id == "auto-link")
        options.autoLink = true;
    else if (id == "manual-link")
        options.autoLink = false;
    else if (id == "link-mode") {
        if (m_LinkEndedFrame != ImGui::GetFrameCount()) {
            const auto tool = Client().ActiveTool();
            const bool builtin = tool == "schematic.view.edit-tool" || tool == "schematic.view.link-tool" ||
                                 tool == "schematic.view.zoom-tool" || tool == "schematic.view.draw-tool";
            m_LinkMode = builtin ? !m_LinkMode : true;
            Client().SelectTool(m_LinkMode ? "schematic.view.link-tool" : "schematic.view.edit-tool");
        }
    } else if (id == "home")
        ScrollToEnd(false);
    else if (id == "end")
        ScrollToEnd(true);
    else if (id == "zoom-in" || id == "zoom-out") {
        const float before = std::max(.01f, m_View.zoom);
        const float after = id == "zoom-in" ? (before < .75f ? .75f : 1.f) : (before <= .75f ? .5f : .75f);
        m_ScrollX = std::max(0.f, m_ScrollX + m_CanvasSize.x * .5f / before - m_CanvasSize.x * .5f / after);
        m_View.panY += m_CanvasSize.y * .5f / before - m_CanvasSize.y * .5f / after;
        m_View.zoom = after;
        PanBy(0, 0);
    } else if (id == "cancel") {
        const bool picking = m_Doc.pickConditionRow >= 0;
        m_Doc.pickShortcutFrom = {};
        m_Doc.pickBreakpoint = false;
        m_Doc.pickConditionRow = -1;
        if (picking) m_Doc.openBreakpointConfig = true;
        if (m_LinkMode)
            m_LinkMode = false;
        else
            m_Navigation.selection.Clear();
    } else if (id == "delete")
        DeleteSelection();
    else if (id == "comment" && hit.view)
        AddComment(hit.view->id, hit.local);
    else if (id == "rename") {
        const auto &selection = m_Navigation.selection;
        if (selection.items.size() == 1 && selection.items[0].kind != ElementKind::Comment) BeginRename();
        if (selection.items.size() == 1 && selection.items[0].kind == ElementKind::Comment)
            if (auto *view = ViewFor(selection.script))
                for (const auto &comment : view->model.comments)
                    if (comment.ref == selection.items[0]) {
                        Hit selected;
                        selected.view = view;
                        selected.ref = comment.ref;
                        RetainHit(selected, m_Doc.commentAddress);
                        m_Doc.openCommentEditor = bool(m_Doc.commentAddress);
                    }
    } else if (id == "left" || id == "right" || id == "expand" || id == "collapse") {
        const bool open = id == "right" || id == "expand";
        const auto selected = m_Navigation.selection.Scripts();
        const auto scripts = (id == "expand" || id == "collapse") ? ToolbarScripts() : selected;
        if (scripts.empty() && (id == "left" || id == "right")) PanBy(open ? -60.f : 60.f, 0);
        for (auto script : scripts)
            if (ScriptOpen(script) != open) ToggleScriptOpen(script);
    } else if (id == "up")
        PanBy(0, 60);
    else if (id == "down")
        PanBy(0, -60);
    else if (id == "page-up")
        PanBy(0, page);
    else if (id == "page-down")
        PanBy(0, -page);
    else if (id == "space-action") {
        if (!m_SpacePanned) SpaceAction();
        m_SpacePanned = false;
    } else if (id == "messages")
        ToggleDiagnostic(false);
    else if (id == "find")
        OpenFind();
    else if (id == "filter") {
        options.sceneFilter = !options.sceneFilter;
        RefreshStack();
    } else if (id == "showall") {
        m_Navigation.hiddenScripts.clear();
        options.showAllScripts = true;
        RefreshStack();
    } else if (id == "hideall") {
        for (auto ref : m_AllScripts) m_Navigation.hiddenScripts.insert(ref);
        RefreshStack();
    } else if (id == "hidesel") {
        for (auto ref : ScriptReferences(ToolbarScripts())) m_Navigation.hiddenScripts.insert(ref);
        m_Navigation.selection.Clear();
        m_Doc.currentScript = 0;
        RefreshStack();
    } else if (id == "hideother") {
        const auto kept = ToolbarScripts();
        for (auto ref : m_AllScripts)
            if (std::find(kept.begin(), kept.end(), ref.object) == kept.end()) m_Navigation.hiddenScripts.insert(ref);
        for (auto ref : ScriptReferences(kept)) m_Navigation.hiddenScripts.erase(ref);
        RefreshStack();
    } else if (id == "locals")
        options.showLocalParams = !options.showLocalParams;
    else if (id == "headers")
        options.showHeaders = !options.showHeaders;
    else if (id == "points")
        options.showControlPoints = !options.showControlPoints;
    else if (id == "info")
        options.showLinkInfo = !options.showLinkInfo;
    else if (id == "priorities")
        options.showPriorities = !options.showPriorities;
    else if (id == "reset")
        ResetView();
    else if (id == "zoom") {
        options.zoomMode = !options.zoomMode;
        Client().SelectTool(options.zoomMode ? "schematic.view.zoom-tool" : "schematic.view.edit-tool");
    } else if (id == "linkerrors")
        ToggleDiagnostic(true);
}
} // namespace vdev
