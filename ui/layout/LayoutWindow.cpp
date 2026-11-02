#include "ui/layout/LayoutWindow.h"
#include "ui/schematic/Snapshots.h"

#include "ui/render/UiRenderer.h"
#include <SDL3/SDL.h>

#include "imgui_internal.h"
#include "editor/ImGuiDrawing.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/TypeAhead.h"
#include "core/runtime/TextEncoding.h"



namespace vdev {

namespace {

// Dev's 3D Layout preferences ("3D Layout\..." keys, General Preferences).
float Component(const std::string &text, int index, char separator) {
    size_t start = 0;
    for (int i = 0; i < index; ++i) {
        const size_t next = text.find(separator, start);
        if (next == std::string::npos) return 0.0f;
        start = next + 1;
    }
    return static_cast<float>(std::atof(text.c_str() + start));
}

// Dev's colors (screenshots): the top bar strip, the left toolbar.
const ImU32 TOP_BAR = IM_COL32(65, 65, 65, 255);
const ImU32 TOP_BAR_BORDER = IM_COL32(157, 157, 157, 255);
const ImU32 LEFT_BAR = IM_COL32(128, 128, 128, 255);
const ImU32 LEFT_BAR_BORDER = IM_COL32(21, 21, 21, 255);
const ImU32 PANEL = IM_COL32(100, 100, 100, 255);

const char *const REFERENTIALS[] = {"Local", "World", "View", "Parent", "Ref. Guide", "Pick Obj."};

} // namespace





void LayoutWindow::Report(EditorStatus status, const std::string &message) {
    if (m_CommandEditor.report)
        m_CommandEditor.report({status, message});
}
bool LayoutWindow::Register(std::string &error) {
    EditorViewDesc view{};
    view.size = sizeof(view);
    view.imguiVersion = EDITOR_IMGUI_VERSION;
    view.id = EDITOR_TEXT("layout.viewport");
    view.provider = EDITOR_TEXT("builtin.layout-window");
    view.title = EDITOR_TEXT("3D Layout");
    view.singleInstance = 1;
    view.area = EDITOR_DOCUMENT;
    view.data = this;
    view.create = [](void *data, const EditorHost *, EditorView id) -> void * {
        auto &owner = *static_cast<LayoutWindow *>(data);
        if (owner.m_Window)
            return nullptr;
        owner.m_Window = new WindowState(owner, id);
        return owner.m_Window;
    };
    view.closing = [](void *data, const EditorHost *, EditorView) {
        static_cast<WindowState *>(data)->owner->Closing();
    };
    view.destroy = [](void *data) {
        auto *state = static_cast<WindowState *>(data);
        auto &owner = *state->owner;
        if (owner.m_Window == state) {
            owner.m_Window = nullptr;
            owner.m_ViewFrame = -1;
        }
        delete state;
    };
    view.draw = [](void *data, const EditorHost *, EditorView, EditorViewFrame *frame) {
        auto &owner = *static_cast<WindowState *>(data)->owner;
        if (owner.Window().needsStart && owner.m_MainWindow) {
            std::string error;
            owner.Start(owner.m_MainWindow, &error);
        }
        owner.Draw(*frame);
        frame->active = owner.Busy();
    };
    const EditorParameter fields[] = {{EDITOR_TEXT("tool"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("started"), EDITOR_INTEGER_VALUE}};
    view.fields = fields;
    view.fieldCount = 2;
    view.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &state = *static_cast<WindowState *>(data);
        const EditorValue values[] = {IntegerArgument("tool", uint64_t(state.tool)),
                                      IntegerArgument("started", state.owner->m_Started)};
        return sink.write(sink.data, values, 2);
    };
    view.pick = [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
        const auto &state = *static_cast<WindowState *>(data);
        const auto &owner = *state.owner;
        double x = 0, y = 0;
        uint64_t objectClass = 0;
        for (uint32_t i = 0; i < request->count; ++i) {
            const auto &v = request->values[i];
            if (std::string(v.key.data, v.key.size) == "x")
                x = v.real;
            if (std::string(v.key.data, v.key.size) == "y")
                y = v.real;
            if (std::string(v.key.data, v.key.size) == "class")
                objectClass = v.integer;
        }
        std::vector<ObjectRef> objects;
        if (owner.m_Started && ImGui::GetFrameCount() - owner.m_ViewFrame <= 1 && x >= owner.m_ViewMin.x &&
            y >= owner.m_ViewMin.y && x < owner.m_ViewMax.x && y < owner.m_ViewMax.y) {
            EditorValue px{EDITOR_TEXT("x"), EDITOR_REAL_VALUE}, py{EDITOR_TEXT("y"), EDITOR_REAL_VALUE};
            px.real = x - owner.m_ViewMin.x;
            py.real = y - owner.m_ViewMin.y;
            const auto picked =
                owner.Client().Query("layout.pick-object", {px, py, IntegerArgument("class", objectClass)});
            if (const auto *value = EditorClient::Find(picked, "object");
                picked.status == EDITOR_OK && value && value->object)
                objects.push_back(value->object);
        }
        const EditorValue result{
            EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {objects.data(), uint32_t(objects.size())}};
        return sink.write(sink.data, &result, 1);
    };
    view.stateVersion = 1;
    view.saveState = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        return static_cast<WindowState *>(data)->owner->SaveWindow(sink);
    };
    view.restoreState = [](void *data, const EditorHost *, const EditorRequest *request) {
        return static_cast<WindowState *>(data)->owner->RestoreWindow(*request);
    };
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("layout.window");
    module.views = &view;
    module.viewCount = 1;
    return m_CommandEditor.Register(module, error);
}
void LayoutWindow::Closing() {
    if (!m_Window)
        return;
    CancelEdits();
    if (Window().document.explorerOpen)
        CloseExplorer(true);
    const auto drop = Window().document.drop;
    if (drop) {
        const EditorValue args[] = {IntegerArgument("drop", drop), IntegerArgument("accept", 0)};
        Complete(m_CommandEditor.ApplyEdit("layout.finish-drop", {Client().Scope(), args, 2}), AfterCommand::None);
    }
    if (m_Window)
        Window().capturing = 0;
    Window().dropBox.Clear();
    Window().copyBox.Clear();
    Window().document.pendingCopy.clear();
    Window().document.drop = 0;
    Window().snapshotOpen = false;
    Window().pickingReferent = false;
    Window().dropText.clear();
    Window().dropHighlight = {};
    if (m_Started)
        ClearDropHighlight();
    auto texture = std::move(m_GpuTexture);
    std::string error;
    if (!ExitFullScreen(true, error) && !error.empty())
        Report(EDITOR_ERROR, error);
    Stop();
    RetireTexture(std::move(texture));
    // A failed unbind can force Context teardown. The main editor still has to
    // become visible when its fullscreen rendering window has gone.
    if (m_MainHiddenForFullScreen && m_MainWindow) {
        SDL_ShowWindow(m_MainWindow);
        m_MainHiddenForFullScreen = false;
    }
    m_ViewFrame = -1;
}
EditorStatus LayoutWindow::SaveWindow(EditorSink sink) const {
    const auto &s = Window();
    std::vector<EditorField> fields;
    const auto number = [&](const char *key, uint64_t value) {
        fields.push_back({key, EDITOR_INTEGER_VALUE, {}, {}, value});
    };
    number("tool", uint64_t(s.tool == LayoutTool::Fly ? LayoutTool::Select : s.tool));
    number("scale-mode", s.scaleMode);
    number("portal-mode", s.portalMode);
    number("zoom-mode", s.zoomMode);
    number("orbit-mode", s.orbitMode);
    number("explorer-shown", s.explorerShown);
    number("snapshot-open", s.snapshotOpen);
    fields.push_back({"explorer-filter", EDITOR_TEXT_VALUE, {}, s.explorerFilter});
    EditorField scroll{"scroll", EDITOR_REAL_LIST};
    scroll.reals = {s.screenScroll.x, s.screenScroll.y};
    fields.push_back(std::move(scroll));
    EditorField extent{"zoom-extent", EDITOR_REAL_VALUE};
    extent.real = s.zoomExtent;
    fields.push_back(std::move(extent));
    EditorField slots{"snapshot-selection", EDITOR_INTEGER_LIST};
    for (auto n : s.snapshotSelection)
        slots.integers.push_back(n);
    fields.push_back(std::move(slots));
    EditorField tools{"transforms", EDITOR_INTEGER_LIST};
    for (int i = 0; i < 4; ++i) {
        const auto &t = i < 3 ? s.transform[i] : s.curveEdit;
        tools.integers.insert(tools.integers.end(), {uint64_t(t.axes), uint64_t(t.referential), uint64_t(t.pivot),
                                                     uint64_t(t.hierarchy), uint64_t(t.snap)});
    }
    fields.push_back(std::move(tools));
    const auto values = BorrowEditorFields(fields);
    return sink.write(sink.data, values.data(), uint32_t(values.size()));
}
EditorStatus LayoutWindow::RestoreWindow(const EditorRequest &request) {
    std::vector<EditorField> fields;
    if (CopyEditorFields(fields, request.values, request.count) != EDITOR_OK)
        return EDITOR_INVALID;
    // Validate every field before replacing any navigation state.
    for (const auto &v : fields) {
        if (v.key == "explorer-filter") {
            if (v.type != EDITOR_TEXT_VALUE || v.text.size() >= sizeof(Window().explorerFilter) ||
                v.text.find('\0') != std::string::npos)
                return EDITOR_INVALID;
        } else if (v.key == "scroll") {
            if (v.type != EDITOR_REAL_LIST || v.reals.size() != 2 || !std::isfinite(v.reals[0]) ||
                !std::isfinite(v.reals[1]) || v.reals[0] < 0 || v.reals[1] < 0)
                return EDITOR_INVALID;
        } else if (v.key == "zoom-extent") {
            if (v.type != EDITOR_REAL_VALUE || !std::isfinite(v.real) || v.real <= 0)
                return EDITOR_INVALID;
        } else if (v.key == "snapshot-selection") {
            if (v.type != EDITOR_INTEGER_LIST ||
                std::any_of(v.integers.begin(), v.integers.end(), [](uint64_t n) { return n >= 36; }))
                return EDITOR_INVALID;
        } else if (v.key == "transforms") {
            if (v.type != EDITOR_INTEGER_LIST || v.integers.size() != 20)
                return EDITOR_INVALID;
            for (int i = 0; i < 4; ++i)
                if (v.integers[i * 5] > 7 || v.integers[i * 5 + 1] > 5 || v.integers[i * 5 + 2] > 2 ||
                    v.integers[i * 5 + 3] > 1 || v.integers[i * 5 + 4] > 1)
                    return EDITOR_INVALID;
        } else {
            if (v.type != EDITOR_INTEGER_VALUE)
                return EDITOR_INVALID;
            if (v.key == "tool") {
                switch (LayoutTool(v.integer)) {
                case LayoutTool::Select:
                case LayoutTool::Translate:
                case LayoutTool::Scale:
                case LayoutTool::Rotate:
                case LayoutTool::Orbit:
                case LayoutTool::Dolly:
                case LayoutTool::Roll:
                case LayoutTool::Fov:
                case LayoutTool::Zoom:
                case LayoutTool::Pan:
                case LayoutTool::Fly:
                case LayoutTool::AddPoints:
                case LayoutTool::InsertPoints:
                case LayoutTool::EditPoints:
                case LayoutTool::DeletePoints:
                    break;
                default:
                    return EDITOR_INVALID;
                }
            } else if (v.key == "scale-mode" || v.key == "zoom-mode") {
                if (v.integer > 2)
                    return EDITOR_INVALID;
            } else if (v.key == "portal-mode" || v.key == "orbit-mode" || v.key == "snapshot-open") {
                if (v.integer > 1)
                    return EDITOR_INVALID;
            } else if (v.key == "explorer-shown") {
                if (v.integer > 0x1ff)
                    return EDITOR_INVALID;
            } else
                return EDITOR_INVALID;
        }
    }
    auto &s = Window();
    for (const auto &v : fields) {
        if (v.key == "tool")
            s.tool = LayoutTool(v.integer);
        else if (v.key == "scale-mode")
            s.scaleMode = int(v.integer);
        else if (v.key == "portal-mode")
            s.portalMode = int(v.integer);
        else if (v.key == "zoom-mode")
            s.zoomMode = int(v.integer);
        else if (v.key == "orbit-mode")
            s.orbitMode = int(v.integer);
        else if (v.key == "explorer-shown")
            s.explorerShown = unsigned(v.integer);
        else if (v.key == "snapshot-open")
            s.snapshotOpen = v.integer != 0;
        else if (v.key == "explorer-filter")
            std::memcpy(s.explorerFilter, v.text.c_str(), v.text.size() + 1);
        else if (v.key == "scroll")
            s.screenScroll = {float(v.reals[0]), float(v.reals[1])};
        else if (v.key == "zoom-extent")
            s.zoomExtent = float(v.real);
        else if (v.key == "snapshot-selection")
            s.snapshotSelection.assign(v.integers.begin(), v.integers.end());
        else if (v.key == "transforms")
            for (int i = 0; i < 4; ++i) {
                auto &t = i < 3 ? s.transform[i] : s.curveEdit;
                t.axes = int(v.integers[i * 5]);
                t.referential = int(v.integers[i * 5 + 1]);
                t.pivot = int(v.integers[i * 5 + 2]);
                t.hierarchy = v.integers[i * 5 + 3] != 0;
                t.snap = v.integers[i * 5 + 4] != 0;
            }
    }
    return EDITOR_OK;
}





// Dev's CUIKApp +0x100: set by Play (sub_5CD5D0), cleared by Pause and
// Stop (sub_5CD3D0) and when the context no longer plays (sub_5CD650).
bool LayoutWindow::PlayerMode() const {
    return EditorClient::Integer(Client().Query("layout.presentation"), "player") != 0;
}
bool LayoutWindow::FullScreen() const {
    return EditorClient::Integer(Client().Query("layout.presentation"), "fullscreen") != 0;
}
void LayoutWindow::ApplyPreferences() {
    const auto revision = Client().Revision("preferences.values");
    if (!revision || revision != m_PreferencesRevision) {
        const auto values = Client().Query("preferences.values");
        const auto *record = EditorClient::Find(values, "value");
        std::string error;
        if (values.status == EDITOR_OK && record && record->type == EDITOR_RECORD &&
            m_Preferences.ReadValues(record->fields, error))
            m_PreferencesRevision = EditorClient::Integer(values, "revision");
    }
    m_Presentation = Client().Query("layout.presentation");
    m_PlayerMode = m_Presentation.status == EDITOR_OK && EditorClient::Integer(m_Presentation, "player");
}

// Dev makes its interface objects again after ClearAll and adds its render
// context to each new level; here the window notices the level change.
void LayoutWindow::SyncComposition() {
    if (m_Presentation.status != EDITOR_OK)
        return;
    const auto revision = EditorClient::Integer(m_Presentation, "composition");
    if (revision != m_Composition) {
        m_Composition = revision;
        m_Group = {};
        m_GroupRevision = ~0u;
    }
}

void LayoutWindow::Draw(EditorViewFrame &frame) {
    PollCommands();
    if (!ReadLayoutSelection(Client(), Window().selectionData))
        Window().selectionData = {};
    // Set again by the view while the composition keeps the cursor.
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    // Every frame, shown or not: Player Mode starts with the selection of Play.
    ApplyPreferences();
    FullScreenCommands();
    // Alt+Enter is Dev's application's accelerator, whatever window has the focus.
    const ImGuiIO &io = ImGui::GetIO();
    if (m_Started && !io.WantTextInput && io.KeyAlt && !io.KeyCtrl && !io.KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        ToggleFullScreen();
    // Fullscreen, the frame goes to the screen, the 3D Layout shown or not.
    RenderPlayer();
    Window().openPopups = !frame.focus;
    EditorPrepareViewWindow(frame);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, PANEL);
    bool open = true;
    const bool visible = BeginDevWindow(frame.window.data, frame.closable ? &open : nullptr,
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (frame.closable)
        DevTabClose(&open);
    frame.close = !open;
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!visible) {
        if (m_Window)
            Window().capturing = 0;
        CancelEdits();
        ImGui::End();
        ExplorerWindow();
        SnapshotWindow();
        CopyOptionsDialog();
        DropParametersDialog();
        return;
    }
    SyncComposition();
    SyncCurveTool();
    // Dev's status bar shows the 3D Layout's action after it was used last.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if ((Busy() || Window().tool == LayoutTool::Fly) && ImGui::IsKeyPressed(ImGuiKey_Escape))
            CancelEdits();
        Window().lastUsed = true;
        Shortcuts();
    } else if (ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
        CancelEdits();
        Window().lastUsed = false;
    }
    Window().scale = ImGui::GetFontSize() / 13.0f;
    Window().origin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    // Dev's layout (sub_4B6C70): the top bar 21 high, the left toolbar from
    // x 6 to 76, the view from (76, 21) to the window's corner.
    const float top = 21.0f * Window().scale, left = 76.0f * Window().scale;
    TopBar(Window().origin, available.x);
    LeftBar(Window().origin, available.y);
    const ImVec2 viewAt(Window().origin.x + left, Window().origin.y + top);
    const ImVec2 viewSize(std::max(1.0f, available.x - left - 1.0f), std::max(1.0f, available.y - top - 1.0f));
    Viewport(viewAt, viewSize);
    ImGui::End();
    ExplorerWindow();
    SnapshotWindow();
    CopyOptionsDialog();
    DropParametersDialog();
}

// ------------------------------------------------------------------ toolbar

bool LayoutWindow::ToolbarButton(const Button &button, bool checked, bool enabled) {
    const float s = Window().scale;
    const ImVec2 a(Window().origin.x + button.x0 * s, Window().origin.y + button.y0 * s);
    const ImVec2 size((button.x1 - button.x0) * s, (button.y1 - button.y0) * s);
    ImGui::SetCursorScreenPos(a);
    ImGui::PushID(button.command);
    if (!enabled) ImGui::BeginDisabled();
    const bool clicked = ImGui::InvisibleButton("##tool", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool right = clicked && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    if (!enabled) ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const bool held = ImGui::IsItemActive();
    ImGui::PopID();
    const int bitmap = !enabled ? button.bitmaps[4]
                       : (checked || held) ? button.bitmaps[hovered ? 3 : 2]
                                           : button.bitmaps[hovered ? 1 : 0];
    ImVec2 bitmapSize;
    if (ImTextureID texture = DevBitmap(bitmap, &bitmapSize))
        ImGui::GetWindowDrawList()->AddImage(texture, a, ImVec2(a.x + bitmapSize.x * s, a.y + bitmapSize.y * s));
    else
        ImGui::GetWindowDrawList()->AddRect(a, ImVec2(a.x + size.x, a.y + size.y), checked ? IM_COL32_WHITE : IM_COL32(40, 40, 40, 255));
    if (hovered && button.tooltip) ImGui::SetTooltip("%s", button.tooltip);
    if (right) {
        RightClick(button.command);
        return false;
    }
    return clicked && enabled;
}

// A Dev menu button: a click runs its current entry; pressed and held, it
// shows its entries' icons to choose another.
bool LayoutWindow::MenuButton(const Button &button, const std::vector<Item> &items, int &current, bool checked,
                              bool enabled) {
    const float s = Window().scale;
    current = std::clamp(current, 0, static_cast<int>(items.size()) - 1);
    const Item &item = items[current];
    Button shown = button;
    shown.bitmaps[0] = item.bitmaps[0];
    shown.bitmaps[1] = item.bitmaps[1];
    shown.bitmaps[2] = shown.bitmaps[3] = item.bitmaps[2];
    shown.bitmaps[4] = item.bitmaps[0];
    const ImVec2 a(Window().origin.x + button.x0 * s, Window().origin.y + button.y0 * s);
    bool run = ToolbarButton(shown, checked, enabled);
    const char *popup = "##menubutton";
    ImGui::PushID(button.command);
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (Window().heldMenu != button.command) {
            Window().heldMenu = button.command;
            Window().heldSince = ImGui::GetTime();
        } else if (ImGui::GetTime() - Window().heldSince > 0.3) {
            ImGui::OpenPopup(popup);
            Window().heldMenu = 0;
        }
    } else if (Window().heldMenu == button.command && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        Window().heldMenu = 0;
    }
    ImGui::SetNextWindowPos(a);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, LEFT_BAR);
    if (ImGui::BeginPopup(popup)) {
        run = false;
        const ImVec2 itemSize((button.x1 - button.x0) * s, (button.y1 - button.y0) * s);
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            ImGui::PushID(i);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const bool chosen = ImGui::InvisibleButton("##item", itemSize);
            const bool hovered = ImGui::IsItemHovered();
            ImVec2 bitmapSize;
            if (ImTextureID texture = DevBitmap(items[i].bitmaps[i == current ? 2 : hovered ? 1 : 0], &bitmapSize))
                ImGui::GetWindowDrawList()->AddImage(texture, at, ImVec2(at.x + bitmapSize.x * s, at.y + bitmapSize.y * s));
            if (hovered && items[i].name) ImGui::SetTooltip("%s", items[i].name);
            if (chosen) {
                current = i;
                run = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return run;
}

void LayoutWindow::TopBar(const ImVec2 &origin, float width) {
    const float s = Window().scale;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(origin.x + 2.0f * s, origin.y), ImVec2(origin.x + width - 2.0f * s, origin.y + 21.0f * s),
                      TOP_BAR, 3.0f * s);
    dl->AddRect(ImVec2(origin.x + 2.0f * s, origin.y), ImVec2(origin.x + width - 2.0f * s, origin.y + 21.0f * s),
                TOP_BAR_BORDER, 3.0f * s);
    static const Button snapshot = {56, 7, 1, 26, 20, {809, 811, 810, 810, 809}, "Snapshot"};
    static const Button explorer = {41, 27, 1, 46, 20, {777, 779, 778, 778, 777}, "3D Layout Explorer"};
    static const Button preferences = {40, 347, 1, 366, 20, {804, 806, 805, 805, 804}, "General Preferences (Ctrl+P)"};
    const bool usable = m_Started && Client().Scope().document.value;
    if (ToolbarButton(snapshot, Window().snapshotOpen, m_Started))
        Command(snapshot.command);
    if (ToolbarButton(explorer, Window().document.explorerOpen, usable))
        Command(explorer.command);
    if (ToolbarButton(preferences, false)) Command(preferences.command);
    // The two combos: dark, white text and arrow (screenshots).
    ImGui::PushStyleColor(ImGuiCol_FrameBg, TOP_BAR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(80, 80, 80, 255));
    ImGui::PushStyleColor(ImGuiCol_Button, TOP_BAR);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 80, 80, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(236, 236, 236, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * s, std::max(0.0f, (19.0f * s - ImGui::GetFontSize()) * 0.5f)));
    // Selection Groups (Dev.exe id 18).
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(80, 80, 80, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(80, 80, 80, 255));
    SelectionGroups(ImVec2(origin.x + 51.0f * s, origin.y + 1.0f * s), 140.0f * s);
    ImGui::PopStyleColor(2);
    // The separator (Dev.exe sub_4B6C70's element "3": bitmap 705 at 196, 1).
    if (ImTextureID separator = DevBitmap(705))
        dl->AddImage(separator, ImVec2(origin.x + 196.0f * s, origin.y + 1.0f * s), ImVec2(origin.x + 197.0f * s, origin.y + 20.0f * s));
    // Available Cameras (id 19): Dev's views, then the composition's cameras.
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 202.0f * s, origin.y + 1.0f * s));
    ImGui::SetNextItemWidth(140.0f * s);
    auto &cameras = Window().cameraData;
    const bool camerasReady = ReadLayoutScene(Client(), "cameras", cameras);
    const auto *current = camerasReady ? cameras.Find(cameras.camera.object) : nullptr;
    const std::string currentName = current ? current->name : std::string();
    ImGui::PopStyleColor(); // the list keeps the menu's text color
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(236, 236, 236, 255));
    const bool open = ImGui::BeginCombo("##cameras", currentName.c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && !open) ImGui::SetTooltip("Available Cameras");
    if (open) {
        TypeAhead typeAhead;
        if (camerasReady) for (const auto &camera : cameras.rows) {
            ImGui::PushID(int(camera.object.object));
            if (typeAhead.Selectable(camera.name.c_str(), current && current->object == camera.object))
                Execute("layout.set-camera", {ObjectArgument("target", camera.object)});
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
}

void LayoutWindow::LeftBar(const ImVec2 &origin, float height) {
    const float s = Window().scale;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 a(origin.x + 6.0f * s, origin.y + 21.0f * s), b(origin.x + 76.0f * s, origin.y + height);
    dl->AddRectFilled(a, b, LEFT_BAR);
    dl->AddRect(a, b, LEFT_BAR_BORDER);
    auto line = [&](int y) {
        dl->AddLine(ImVec2(origin.x + 10.0f * s, origin.y + y * s), ImVec2(origin.x + 72.0f * s, origin.y + y * s),
                    IM_COL32(100, 100, 100, 255));
        dl->AddLine(ImVec2(origin.x + 10.0f * s, origin.y + (y + 1) * s), ImVec2(origin.x + 72.0f * s, origin.y + (y + 1) * s),
                    IM_COL32(150, 150, 150, 255));
    };
    const bool transforming = Window().tool == LayoutTool::Translate || Window().tool == LayoutTool::Rotate ||
                              Window().tool == LayoutTool::Scale || Window().tool == LayoutTool::EditPoints;
    // Selection.
    static const Button select = {25, 8, 24, 29, 45, {774, 776, 775, 775, 774}, "Select (A)"};
    static const Button lock = {58, 30, 25, 51, 46, {692, 694, 693, 693, 692}, "Lock Selection (Spacebar)"};
    static const Button mode = {61, 52, 24, 73, 45, {1152, 1153, 1151, 1150, 1151}, "Selection Mode"};
    if (ToolbarButton(select, Window().tool == LayoutTool::Select))
        Command(select.command);
    if (ToolbarButton(lock, Window().selectionData.locked))
        Command(lock.command);
    if (ToolbarButton(mode, m_Preferences.Bool("3D Layout\\Strict Selection")))
        Command(mode.command);
    line(49);
    // Transformations.
    static const Button move = {9, 8, 55, 29, 76, {699, 701, 700, 700, 699}, "Select and Translate (X)"};
    static const Button rotate = {11, 30, 55, 51, 76, {727, 729, 728, 728, 727}, "Select and Rotate (C)"};
    static const Button scale = {10, 52, 55, 73, 76, {}, "Select and Scale (V)"};
    // Mode 0 scales the three axes alike (Uniform), 2 the constrained ones (Normal).
    static const std::vector<Item> scales = {{46, {730, 732, 731}, "Uniform"}, {47, {736, 738, 737}, "Volumetric"},
                                             {48, {733, 735, 734}, "Normal"}};
    if (ToolbarButton(move, Window().tool == LayoutTool::Translate))
        Command(move.command);
    if (ToolbarButton(rotate, Window().tool == LayoutTool::Rotate))
        Command(rotate.command);
    if (MenuButton(scale, scales, Window().scaleMode, Window().tool == LayoutTool::Scale))
        Command(scales[Window().scaleMode].command);
    // Transformation settings, shown with the transformation tools only, the
    // active tool's own (Dev.exe sub_492AC0, sub_496600).
    if (transforming) {
        LayoutTransformSettings &settings = Settings();
        static const Button axes[3] = {{12, 9, 79, 30, 100, {590, 592, 591, 591, 1187}, "Constrain X Axis (F5)"},
                                       {13, 31, 79, 52, 100, {596, 598, 597, 597, 1188}, "Constrain Y Axis (F6)"},
                                       {14, 53, 79, 74, 100, {602, 604, 603, 603, 1189}, "Constrain Z Axis (F7)"}};
        const int allowed = AllowedAxes();
        for (int i = 0; i < 3; ++i)
            if (ToolbarButton(axes[i], (settings.axes & (1 << i)) != 0, (allowed & (1 << i)) != 0)) Command(axes[i].command);
        static const Button plane = {39, 23, 100, 56, 115, {}, "Constrain Plane (F8)"};
        static const std::vector<Item> planes = {{42, {593, 595, 594}, "XY"}, {43, {599, 601, 600}, "YZ"},
                                                 {44, {605, 607, 606}, "XZ"}};
        static const int planeAxes[3] = {3, 6, 5};
        int planeIndex = settings.axes == 6 ? 1 : settings.axes == 5 ? 2 : 0;
        if (MenuButton(plane, planes, planeIndex, settings.axes == planeAxes[planeIndex]))
            Command(planes[planeIndex].command);
        // Referential Axis (id 15): a small combo; Rotate and Scale show it
        // about each object's axis only.
        if (Window().tool == LayoutTool::Translate || Window().tool == LayoutTool::EditPoints || settings.pivot == 1) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + 9.0f * s, origin.y + 116.0f * s));
            ImGui::SetNextItemWidth(64.0f * s);
            ImGui::PushFont(nullptr, 10.0f * s);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * s, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(200, 200, 200, 255));
            if (ImGui::BeginCombo("##referential", REFERENTIALS[std::clamp(settings.referential, 0, 5)])) {
                TypeAhead typeAhead;
                for (int i = 0; i < 6; ++i)
                    if (typeAhead.Selectable(REFERENTIALS[i], i == settings.referential)) {
                        settings.referential = i;
                        // Pick Obj.: the next click in the view picks it.
                        if (i == 5) {
                            Window().pickingReferent = true;
                            Client().Log(EDITOR_LOG_INFO, "Pick the referential object in the 3D Layout");
                        }
                    }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Referential Axis");
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImGui::PopFont();
        }
        static const Button snap = {66, 8, 137, 29, 158, {1240, 1238, 1239, 1239, 1240}, "Toggle Snap (S)"};
        static const Button hierarchy = {70, 30, 137, 51, 158, {1256, 1255, 1254, 1257, 1256}, "Toggle Hierarchy (H)"};
        static const Button pivot = {71, 52, 137, 73, 158, {}, "Pivot Axis (F9)"};
        static const std::vector<Item> pivots = {{72, {1262, 1263, 1261}, "Object"}, {73, {1265, 1266, 1264}, "Axis"},
                                                 {74, {1268, 1269, 1267}, "Selection"}};
        if (ToolbarButton(snap, settings.snap)) Command(snap.command);
        if (ToolbarButton(hierarchy, settings.hierarchy)) Command(hierarchy.command);
        // Translate has no pivot.
        if (Window().tool != LayoutTool::Translate && Window().tool != LayoutTool::EditPoints &&
            MenuButton(pivot, pivots, settings.pivot, false))
            Command(pivots[settings.pivot].command);
    }
    line(160);
    // Guides.
    static const Button guide = {29, 31, 166, 52, 187, {1258, 1259, 1260, 1260, 1258}, "Toggle Reference Guide (G)"};
    static const Button screenGuide = {67, 53, 166, 74, 187, {678, 680, 679, 679, 678}, "Toggle Screen Guide (Shift+G)"};
    if (ToolbarButton(guide, m_Preferences.Bool("3D Layout\\Author Mode\\Show Grid"))) Command(guide.command);
    if (ToolbarButton(screenGuide, m_Preferences.Bool("3D Layout\\Show Screen Grid")))
        Command(screenGuide.command);
    line(191);
    // Creation.
    static const Button creates[8] = {{31, 9, 197, 30, 218, {615, 617, 616, 616, 615}, "Create Camera (1)"},
                                      {32, 31, 197, 52, 218, {624, 626, 625, 625, 624}, "Create Light (2)"},
                                      {34, 53, 197, 74, 218, {1278, 1276, 1277, 1277, 1278}, "Create 3D Frame (3)"},
                                      {33, 9, 218, 30, 239, {618, 620, 619, 619, 618}, "Create Curve (4)"},
                                      {60, 31, 218, 52, 239, {1148, 1147, 1146, 1146, 1147}, "Create Grid (5)"},
                                      {75, 53, 218, 74, 239, {621, 622, 623, 623, 621}, "Create 2D Frame (6)"},
                                      {80, 9, 239, 30, 260, {1324, 1325, 1326, 1326, 1324}, "Create Material (7)"},
                                      {81, 31, 239, 52, 260, {1327, 1328, 1329, 1329, 1327}, "Create Texture (8)"}};
    const bool document = Client().Scope().document.value != 0;
    for (const Button &button : creates)
        if (ToolbarButton(button, false, document)) Command(button.command);
    static const Button portal = {77, 53, 239, 74, 260, {}, "Create Portal (9)"};
    static const std::vector<Item> portals = {{79, {1340, 1338, 1339}, "Create Portals"}, {78, {1311, 1309, 1310}, "Create Portal"}};
    if (MenuButton(portal, portals, Window().portalMode, false, document))
        Command(portals[Window().portalMode].command);
    line(264);
    // Navigation.
    const bool perspective = EditorClient::Integer(m_Presentation, "perspective");
    const bool targeted = EditorClient::Integer(m_Presentation, "targeted");
    static const Button dolly = {23, 9, 270, 30, 291, {664, 666, 665, 665, 664}, "Camera Dolly (D)"};
    static const Button fov = {26, 31, 270, 52, 291, {674, 676, 675, 675, 674}, "Camera Field of View (F)"};
    static const Button zoom = {27, 53, 270, 74, 291, {}, "Camera Zoom (Z)"};
    static const std::vector<Item> zooms = {{51, {836, 838, 837}, "Zoom"}, {52, {842, 844, 843}, "Zoom on Selection"},
                                            {57, {839, 841, 840}, "Zoom on Scene"}};
    static const Button roll = {24, 9, 291, 30, 312, {724, 726, 725, 725, 724}, "Roll Camera (R)"};
    static const Button pan = {28, 31, 291, 52, 312, {715, 717, 716, 716, 715}, "Camera Pan (P)"};
    static const Button orbit = {20, 53, 291, 74, 312, {}, "Orbit Target/Orbit Around (O)"};
    static const std::vector<Item> orbits = {{49, {706, 708, 707}, "Orbit"}, {50, {709, 711, 710}, "Orbit Around Camera"}};
    if (ToolbarButton(dolly, Window().tool == LayoutTool::Dolly))
        Command(dolly.command);
    // Dev disables the field of view of orthographic cameras and the roll of target cameras.
    if (ToolbarButton(fov, Window().tool == LayoutTool::Fov, perspective))
        Command(fov.command);
    if (MenuButton(zoom, zooms, Window().zoomMode, Window().tool == LayoutTool::Zoom))
        Command(zooms[Window().zoomMode].command);
    if (ToolbarButton(roll, Window().tool == LayoutTool::Roll, !targeted))
        Command(roll.command);
    if (ToolbarButton(pan, Window().tool == LayoutTool::Pan))
        Command(pan.command);
    if (MenuButton(orbit, orbits, Window().orbitMode, Window().tool == LayoutTool::Orbit))
        Command(orbits[Window().orbitMode].command);
}

// Toolbar and keyboard commands (Dev's command ids).
void LayoutWindow::Command(int command) {
    const bool flying = Window().tool == LayoutTool::Fly;
    if (command == 68) {
        if (flying) EndFly();
        else BeginFly();
        return;
    }
    switch (command) {
    case 25:
        Window().tool = LayoutTool::Select;
        break;
    case 9:
        Window().tool = LayoutTool::Translate;
        break;
    case 11:
        Window().tool = LayoutTool::Rotate;
        break;
    case 46: case 47: case 48:
        Window().scaleMode = command - 46;
        Window().tool = LayoutTool::Scale;
        break;
    case 58:
        // Dev locks a non-empty selection only.
        if (Window().selectionData.locked || !Window().selectionData.objects.empty())
            LockSelection(!Window().selectionData.locked);
        break;
    case 61:
        // Selection Mode: Dev's "Strict Selection" (+540), RectPick's Intersect.
        TogglePreference("3D Layout\\Strict Selection");
        break;
    case 12: case 13: case 14: {
        // Not an axis the view refuses (AllowedAxes).
        if (!(AllowedAxes() & (1 << (command - 12)))) break;
        // At least one axis stays constrained.
        const int axes = Settings().axes ^ (1 << (command - 12));
        if (axes & 7) Settings().axes = axes;
        break;
    }
    case 42: Settings().axes = 3; break; // XY
    case 43: Settings().axes = 6; break; // YZ
    case 44: Settings().axes = 5; break; // XZ
    case 66: Settings().snap = !Settings().snap; break;
    case 70: Settings().hierarchy = !Settings().hierarchy; break;
    case 72: case 73: case 74:
        Settings().pivot = command - 72;
        // Pivot Axis Object: the next click in the view picks the referent.
        if (command == 72) {
            Window().pickingReferent = true;
            Client().Log(EDITOR_LOG_INFO, "Pick the referent object in the 3D Layout");
        }
        break;
    case 29: ToggleGuide(false); break;
    case 67: ToggleGuide(true); break;
    case 31: if (createObject) createObject(LayoutObject::Camera); break;
    case 32: if (createObject) createObject(LayoutObject::Light); break;
    case 34: if (createObject) createObject(LayoutObject::Frame3D); break;
    case 33: if (createObject) createObject(LayoutObject::Curve); break;
    case 60: if (createObject) createObject(LayoutObject::Grid); break;
    case 75: if (createObject) createObject(LayoutObject::Frame2D); break;
    case 80: if (createObject) createObject(LayoutObject::Material); break;
    case 81: if (createObject) createObject(LayoutObject::Texture); break;
    case 78: if (createObject) createObject(LayoutObject::Portal); break;
    case 79:
        // Automatic Portals (documentation "Automatic Portals"): a portal
        // between each two contiguous places.
        if (Client().Scope().document.value) {
            const auto made = Client().Command("object.create-automatic-portals");
            if (made.status != EDITOR_QUEUED)
                Client().Log(made.status == EDITOR_OK ? EDITOR_LOG_INFO : EDITOR_LOG_WARNING, "Automatic Portals: " + made.message);
        }
        break;
    case 23:
        Window().tool = LayoutTool::Dolly;
        break;
    case 26:
        Window().tool = LayoutTool::Fov;
        break;
    case 51:
        Window().tool = LayoutTool::Zoom;
        break;
    case 52: ZoomOn(false); break;
    case 57: ZoomOn(true); break;
    case 24:
        Window().tool = LayoutTool::Roll;
        break;
    case 28:
        Window().tool = LayoutTool::Pan;
        break;
    case 49:
    case 50:
        Window().tool = LayoutTool::Orbit;
        break;
    case 40: {
        Execute("preferences.open");
        break;
    }
    case 41: OpenExplorer(); break;
    case 180: {
        LayoutSelectionData selection; if (ReadLayoutSelection(Client(), selection)) Duplicate(selection.objects); break;
    } // the shortcut menu's Duplicate
    case 56:
        if (m_Started)
            Window().snapshotOpen = true;
        break;
    default: break;
    }
    if (!CurveToolActive())
        ClearCurveTool();
    if (flying && Window().tool != LayoutTool::Fly) {
        const LayoutTool next = Window().tool;
        Window().tool = LayoutTool::Fly;
        EndFly();
        Window().tool = next;
    }
}

// Right-clicking a button opens its General Preferences page (Dev.exe
// sub_4B95C0, WM_NOTIFY): the tools the Actions page, the creation tools
// and the screen guide the Interface page, the reference guide its page,
// snap the Snap & Align page, the sphere the Rendering page.
void LayoutWindow::RightClick(int command) {
    auto openPreferences = [this](const char *page) { Execute("preferences.open", {TextArgument("page", page)}); };
    switch (command) {
    case 9: case 10: case 11: case 20: case 23: case 24: case 26: case 27: case 28: case 68: case 25:
        openPreferences("actions");
        break;
    case 31: case 32: case 33: case 34: case 60: case 67: case 75:
        openPreferences("interface");
        break;
    case 29: openPreferences("reference-guide"); break;
    case 66: openPreferences("snap-align"); break;
    case 40: openPreferences("rendering"); break;
    default: break;
    }
}

// G / Shift+G (Dev.exe sub_4B95C0): the guide flips and Dev stores it at once.
void LayoutWindow::TogglePreference(const char *key) {
    const auto values = Client().Query("preferences.values");
    if (values.status != EDITOR_OK)
        return;
    const auto *record = EditorClient::Find(values, "value");
    if (!record || record->type != EDITOR_RECORD)
        return;
    const auto found = std::find_if(record->fields.begin(), record->fields.end(),
                                    [key](const EditorField &field) { return field.key == key; });
    if (found == record->fields.end() || found->type != EDITOR_INTEGER_VALUE)
        return;
    EditorField changed{"value", EDITOR_RECORD};
    changed.fields = {*found};
    changed.fields.front().integer = !found->integer;
    const std::vector<EditorField> fields{
        {"revision", EDITOR_INTEGER_VALUE, {}, {}, EditorClient::Integer(values, "revision")}, std::move(changed)};
    const auto args = BorrowEditorFields(fields);
    Execute("preferences.apply", {args.data(), args.data() + args.size()});
    ApplyPreferences();
}
void LayoutWindow::ToggleGuide(bool screen) {
    TogglePreference(screen ? "3D Layout\\Show Screen Grid" : "3D Layout\\Author Mode\\Show Grid");
}

// ------------------------------------------------------------------- view

void LayoutWindow::Viewport(const ImVec2 &viewAt, const ImVec2 &viewSize) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    // Dev.exe sub_4B8CE0 (Environment\Screen Size): a size other than 0|0
    // renders at that size, centered in a larger view; a smaller view gets
    // scrollbars (the horizontal one first, as Dev) and shows the part
    // scrolled to. at / size: the render context's rectangle on the screen.
    const std::string screenSize = m_Preferences.Text("3D Layout\\Environment\\Screen Size");
    const int fixedW = static_cast<int>(Component(screenSize, 0, '|')), fixedH = static_cast<int>(Component(screenSize, 1, '|'));
    ImVec2 at = viewAt, size = viewSize, visible = viewSize;
    bool hBar = false, vBar = false;
    if (m_Started && fixedW > 0 && fixedH > 0) {
        const float bar = ImGui::GetStyle().ScrollbarSize;
        hBar = visible.x < fixedW;
        if (hBar) visible.y -= bar;
        vBar = visible.y < fixedH;
        if (vBar) {
            visible.x -= bar;
            hBar = visible.x < fixedW;
        }
        visible = ImVec2(std::max(1.0f, visible.x), std::max(1.0f, visible.y));
        Window().screenScroll.x = hBar ? std::clamp(Window().screenScroll.x, 0.0f, fixedW - visible.x) : 0.0f;
        Window().screenScroll.y = vBar ? std::clamp(Window().screenScroll.y, 0.0f, fixedH - visible.y) : 0.0f;
        at = ImVec2(viewAt.x + (hBar ? -Window().screenScroll.x : std::floor((visible.x - fixedW) * 0.5f)),
                    viewAt.y + (vBar ? -Window().screenScroll.y : std::floor((visible.y - fixedH) * 0.5f)));
        size = ImVec2(static_cast<float>(fixedW), static_cast<float>(fixedH));
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (hBar) {
            ImS64 scroll = static_cast<ImS64>(Window().screenScroll.x);
            const ImRect bb(viewAt.x, viewAt.y + visible.y, viewAt.x + visible.x, viewAt.y + visible.y + bar);
            if (ImGui::ScrollbarEx(bb, window->GetID("##screenh"), ImGuiAxis_X, &scroll, static_cast<ImS64>(visible.x), fixedW))
                Window().screenScroll.x = static_cast<float>(scroll);
        }
        if (vBar) {
            ImS64 scroll = static_cast<ImS64>(Window().screenScroll.y);
            const ImRect bb(viewAt.x + visible.x, viewAt.y, viewAt.x + visible.x + bar, viewAt.y + visible.y);
            if (ImGui::ScrollbarEx(bb, window->GetID("##screenv"), ImGuiAxis_Y, &scroll, static_cast<ImS64>(visible.y), fixedH))
                Window().screenScroll.y = static_cast<float>(scroll);
        }
        // Around the render context: Dev's editor view background.
        dl->AddRectFilled(viewAt, ImVec2(viewAt.x + visible.x, viewAt.y + visible.y), PANEL);
    }
    const ImVec2 visibleMax(viewAt.x + visible.x, viewAt.y + visible.y);
    const int width = static_cast<int>(size.x), height = static_cast<int>(size.y);
    m_ViewMin = at;
    m_ViewMax = ImVec2(std::min(at.x + size.x, visibleMax.x), std::min(at.y + size.y, visibleMax.y));
    m_ViewFrame = ImGui::GetFrameCount();
    if (m_Started && m_MainWindow) {
        InputViewport(ImVec4(at.x, at.y, at.x + size.x, at.y + size.y), width, height);
    }
    ImGui::SetCursorScreenPos(viewAt);
    ImGui::InvisibleButton("##view", visible,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    // Dev.exe sub_4B95C0 (WM_SETCURSOR): in Player Mode without Update
    // Cursor In Player Mode the view leaves the cursor to the composition's
    // Building Blocks (Main.cpp does not put the arrow back).
    if (m_PlayerMode && ImGui::IsItemHovered() && !m_Preferences.Bool("3D Layout\\Player Mode\\Update Cursor"))
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    if (m_Started && Window().capturing)
        CaptureInput(at);
    else if (m_Started) ViewInput(at, ImGui::IsItemHovered());
    if (m_Started) DropTarget(at);
#ifdef VIRTOOLSDEV_UI_TESTS
    if (m_Started && m_StartupDropFrames > 0 && --m_StartupDropFrames == 0)
        DropBuildingBlocks(static_cast<int>(m_StartupDropAt.x - at.x), static_cast<int>(m_StartupDropAt.y - at.y),
                           {m_StartupDrop});
#endif
    if (!m_Started) {
        dl->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), LEFT_BAR);
        const std::string text = "No 3D Layout: " + AnsiToUtf8(m_StartError);
        dl->AddText(ImVec2(at.x + 8.0f, at.y + 8.0f), IM_COL32(20, 20, 20, 255), text.c_str());
        return;
    }
    ContextMenuPopup();
    dl->PushClipRect(viewAt, visibleMax, true);
    UiTexture *frame = RenderViewport(width, height);
    if (frame)
        dl->AddImage(frame->Id(), at, ImVec2(at.x + frame->Width(), at.y + frame->Height()));
    else
        dl->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), LEFT_BAR);
    // The selection rectangle being dragged.
    if (Window().rectangle) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        dl->AddRect(ImVec2(at.x + Window().pressAt.x, at.y + Window().pressAt.y), mouse, IM_COL32(255, 255, 255, 255));
    }
    if (debugData || m_Preferences.Bool("3D Layout\\Debug Mode")) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        DebugData(at, static_cast<int>(mouse.x - at.x), static_cast<int>(mouse.y - at.y));
    }
    dl->PopClipRect();
    // Lock Selection: "a thin green border around the preview pane".
    if (Window().selectionData.locked)
        dl->AddRect(viewAt, visibleMax, IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);
}

// Dev.exe sub_4A01F0: the mouse (and the frame rate while playing), then
// what is under it: the 2D entity, character and 3D entity (" (S)" when
// selected, " (D)" when dropped on), the mesh with the face's normal, its
// vertex nearest to the point, the texture coordinates (and the vertex
// colors of prelit meshes), the material, the texture. Unnamed objects are
// "Unknown" (Dev's string 51).
void LayoutWindow::DebugData(const ImVec2 &at, int x, int y) {
    EditorValue px{EDITOR_TEXT("x"), EDITOR_REAL_VALUE}, py{EDITOR_TEXT("y"), EDITOR_REAL_VALUE};
    px.real = x;
    py.real = y;
    const auto result = Client().Query("layout.debug-data", {px, py});
    if (result.status != EDITOR_OK)
        return;
    const auto &io = ImGui::GetIO();
    const int buttons = (ImGui::IsMouseDown(ImGuiMouseButton_Left) ? 1 : 0) |
                        (ImGui::IsMouseDown(ImGuiMouseButton_Right) ? 2 : 0) | (io.KeyShift ? 4 : 0) |
                        (io.KeyCtrl ? 8 : 0) | (ImGui::IsMouseDown(ImGuiMouseButton_Middle) ? 16 : 0);
    char line[128];
    if (EditorClient::Integer(m_Presentation, "playing"))
        std::snprintf(line, sizeof(line), "X:%5d Y:%5d M:%3d  FPS:%5d\n", x, y, buttons, int(io.Framerate + 0.5f));
    else
        std::snprintf(line, sizeof(line), "X:%5d Y:%5d M:%3d\n", x, y, buttons);
    const auto text = line + EditorClient::Text(result, "text");
    ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + 10.0f * Window().scale, at.y + 10.0f * Window().scale),
                                        IM_COL32(255, 255, 255, 255), text.c_str());
}

// "3D Layout\Actions\..." and the World Size (Interface page).


std::string LayoutWindow::ActionText() const {
    if (!m_Window || !Window().lastUsed)
        return std::string();
    const bool targeted = EditorClient::Integer(InputState(), "targeted") != 0;
    if (GestureActive(0))
        return CameraActionText(CameraAction(Window().gestureAction), true, targeted,
                                EditorClient::Integer(Window().interaction, "around-selection") != 0);
    if (GestureActive(2))
        return TransformAction(Window().gestureAction) == TransformAction::Translate ? "Moving Objects"
                                                                                     : "Scaling Objects (Free)";
    if (GestureActive(1) && !Window().curveEditing) {
        static const char *const scaling[] = {"Scaling Objects (Uniform)", "Scaling Objects (Volumetric)",
                                              "Scaling Objects (Free)"};
        switch (TransformAction(Window().gestureAction)) {
        case TransformAction::Translate: return "Moving Objects";
        case TransformAction::Rotate: return "Rotating Objects";
        case TransformAction::Scale:
            return scaling[std::clamp(Window().scaleMode, 0, 2)];
        }
    }
    if (Window().rectangle)
        return "Select Objects";
    switch (Window().tool) {
    case LayoutTool::Fly: return "Navigation active";
    case LayoutTool::AddPoints: return "Add curve points";
    case LayoutTool::InsertPoints: return "Insert points on curve";
    case LayoutTool::EditPoints:
        return GestureActive(1) ? "Moving curve points" : "Edit curve points";
    case LayoutTool::DeletePoints: return "Delete curve points";
    case LayoutTool::Dolly: return CameraActionText(CameraAction::Dolly, false, targeted, false);
    case LayoutTool::Zoom: return CameraActionText(CameraAction::Zoom, false, targeted, false);
    case LayoutTool::Pan: return CameraActionText(CameraAction::Pan, false, targeted, false);
    case LayoutTool::Orbit:
        return CameraActionText(Window().orbitMode ? CameraAction::OrbitCamera : CameraAction::Orbit, false, targeted,
                                false);
    case LayoutTool::Roll: return CameraActionText(CameraAction::Roll, false, targeted, false);
    case LayoutTool::Fov: return CameraActionText(CameraAction::Fov, false, targeted, false);
    case LayoutTool::Translate: return "Select or Move Objects";
    case LayoutTool::Rotate: return "Select or Rotate Objects";
    case LayoutTool::Scale: return "Select or Scale Objects";
    default: return "Select Objects";
    }
}

bool LayoutWindow::Coordinates(std::string values[3]) const {
    if (!m_Window) {
        for (int i = 0; i < 3; ++i)
            values[i].clear();
        return false;
    }
    for (int i = 0; i < 3; ++i)
        values[i] = Window().coordinates[i];
    return Window().lastUsed;
}

// Dev's tools after each move (Dev.exe sub_48EED0, sub_4952A0, sub_497270,
// sub_48E630): the grabbed object's world position, its world matrix's
// Euler angles in degrees, its scale; a 2D entity's position and no Z.
void LayoutWindow::UpdateCoordinates() {
    const auto *coordinates = EditorClient::Find(Window().interaction, "coordinates");
    if (!coordinates || coordinates->type != EDITOR_REAL_LIST || coordinates->reals.size() < 2)
        return;
    for (size_t i = 0; i < 3; ++i) {
        if (i >= coordinates->reals.size())
            Window().coordinates[i] = "NA";
        else {
            char text[32];
            std::snprintf(text, sizeof(text), "%8.2f", coordinates->reals[i]);
            Window().coordinates[i] = text;
        }
    }
}

std::string LayoutWindow::SelectionText() const {
    return EditorClient::Text(Client().Query("layout.presentation"), "selection-text");
}

// "3D Layout\Actions\Object ..." and the Snap & Align steps.


// "3D Layout\Snap\..." for 2D entities and the Screen Guide (Interface page).



















// Private window and GPU resource operations used by the Layout commands.




} // namespace vdev
