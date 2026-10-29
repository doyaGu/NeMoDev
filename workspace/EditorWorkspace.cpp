#include "workspace/EditorWorkspace.h"

#include <algorithm>
#include <set>
#include "editor/ImGuiDrawing.h"
#include "imgui_internal.h"

namespace vdev {
namespace {
struct Drawing {
    EditorApplication &app;
    explicit Drawing(EditorApplication &app) : app(app) { app.EnterDraw(); }
    ~Drawing() { app.LeaveDraw(); }
};
std::string Text(EditorString value) { return std::string(value.data ? value.data : "", value.size); }
} // namespace
EditorWorkspace::EditorWorkspace(EditorApplication &application) : m_Application(application) {
    m_Application.ViewNavigation(this);
}
bool EditorWorkspace::Register(std::string &error) {
    const EditorMenuLocation location{EDITOR_TEXT("main.edit"), EDITOR_TEXT("Views menu")};
    const EditorParameter type[] = {{EDITOR_TEXT("type"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("role"), EDITOR_TEXT_VALUE, 1}, {EDITOR_TEXT("pinned"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter instance{EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, 1};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("workspace.open-view"), EDITOR_TEXT("builtin.workspace"),
         EDITOR_TEXT("Open View"), type, 3, OpenCommand, this, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("workspace.focus-view"), EDITOR_TEXT("builtin.workspace"),
         EDITOR_TEXT("Focus View"), &instance, 1, FocusCommand, this, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("workspace.close-view"), EDITOR_TEXT("builtin.workspace"),
         EDITOR_TEXT("Close View"), &instance, 1, CloseCommand, this, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE}};
    const EditorParameter fields[] = {{EDITOR_TEXT("views"), EDITOR_INTEGER_LIST},
        {EDITOR_TEXT("types"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("providers"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("titles"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("focused"), EDITOR_INTEGER_VALUE}};
    const EditorParameter viewFields[] = {{EDITOR_TEXT("type"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("provider"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("window"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("focused"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("workspace.views"), EDITOR_TEXT("builtin.workspace"),
         nullptr, 0, ListQuery, this, EDITOR_QUERY_EACH_READ, EDITOR_WORKSPACE, fields, 5},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("workspace.view"), EDITOR_TEXT("builtin.workspace"),
         &instance, 1, ViewQuery, this, EDITOR_QUERY_EACH_READ, EDITOR_WORKSPACE, viewFields, 4}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EditorLiteral("workspace.views");
    module.locations = &location;
    module.locationCount = 1;
    module.commands = commands;
    module.commandCount = 3;
    module.queries = queries;
    module.queryCount = 2;
    return m_Application.Register(module, error);
}
EditorWorkspace::~EditorWorkspace() {
    CloseAll();
    CloseOwner(m_MenuOwner);
    m_Application.ViewNavigation(nullptr);
}
EditorStatus EditorWorkspace::OpenCommand(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto &self = *static_cast<EditorWorkspace *>(data);
    std::string type, role;
    bool pinned = false;
    for (uint32_t i = 0; i < request->count; ++i) {
        const auto &value = request->values[i];
        const auto key = Text(value.key);
        if (key == "type") type = Text(value.text);
        if (key == "role") role = Text(value.text);
        if (key == "pinned") { if (value.integer > 1) return EDITOR_INVALID; pinned = value.integer != 0; }
    }
    const auto descriptions = self.m_Application.Views();
    const auto found = std::find_if(descriptions.begin(), descriptions.end(),
        [&](const EditorViewDesc &desc) { return Text(desc.id) == type; });
    if (found == descriptions.end()) return EDITOR_INVALID;
    if (found->area == EDITOR_DOCUMENT && !self.m_Application.NativeAvailable()) return EDITOR_REFUSED;
    const auto id = self.Open(type, role, pinned);
    if (!id) return EDITOR_ERROR;
    const EditorValue value{EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, {}, {}, id};
    if (sink.write) { try { sink.write(sink.data, &value, 1); } catch (...) {} }
    return EDITOR_OK;
}
EditorStatus EditorWorkspace::FocusCommand(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
    const auto id = request->values[0].integer ? request->values[0].integer : request->scope.view;
    return static_cast<EditorWorkspace *>(data)->Focus(id) ? EDITOR_OK : EDITOR_STALE;
}
EditorStatus EditorWorkspace::CloseCommand(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
    auto &self = *static_cast<EditorWorkspace *>(data);
    const auto id = request->values[0].integer ? request->values[0].integer : request->scope.view;
    if (!self.m_Views.count(id)) return EDITOR_STALE;
    return self.Close(id) ? EDITOR_OK : EDITOR_REFUSED;
}
EditorStatus EditorWorkspace::ListQuery(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
    const auto &self = *static_cast<EditorWorkspace *>(data);
    std::vector<uint64_t> ids;
    std::vector<EditorString> types, providers, titles;
    for (const auto &entry : self.m_Views) {
        ids.push_back(entry.first);
        types.push_back(entry.second.desc.id);
        providers.push_back(entry.second.desc.provider);
        titles.push_back(entry.second.desc.title);
    }
    const EditorValue fields[] = {
        {EDITOR_TEXT("views"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {ids.data(), uint32_t(ids.size())}},
        {EDITOR_TEXT("types"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {types.data(), uint32_t(types.size())}},
        {EDITOR_TEXT("providers"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {providers.data(), uint32_t(providers.size())}},
        {EDITOR_TEXT("titles"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {titles.data(), uint32_t(titles.size())}},
        {EDITOR_TEXT("focused"), EDITOR_INTEGER_VALUE, {}, {}, self.Focused()}};
    return sink.write ? sink.write(sink.data, fields, 5) : EDITOR_INVALID;
}
EditorStatus EditorWorkspace::ViewQuery(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    const auto &self = *static_cast<EditorWorkspace *>(data);
    const auto id = request->values[0].integer ? request->values[0].integer : request->scope.view;
    const auto found = self.m_Views.find(id);
    if (found == self.m_Views.end()) return EDITOR_STALE;
    const auto name = self.WindowName(id);
    const EditorValue fields[] = {{EDITOR_TEXT("type"), EDITOR_TEXT_VALUE, {}, found->second.desc.id},
        {EDITOR_TEXT("provider"), EDITOR_TEXT_VALUE, {}, found->second.desc.provider},
        {EDITOR_TEXT("window"), EDITOR_TEXT_VALUE, {}, EditorText(name)},
        {EDITOR_TEXT("focused"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(self.Focused() == id)}};
    return sink.write ? sink.write(sink.data, fields, 4) : EDITOR_INVALID;
}
bool EditorWorkspace::Begin(const char *property, ObjectRef object, EditorEdit &edit, EditorView &owner) {
    if (edit)
        Finish(edit, false);
    if (!owner)
        owner = m_Application.OpenScope();
    return m_Application.BeginEdit(property, m_Application.Scope(owner), object, edit) == EDITOR_OK;
}
bool EditorWorkspace::DrawInput(EditorEdit &edit, const char *label, bool focus, bool *active) {
    if (active)
        *active = false;
    if (!edit)
        return false;
    if (!m_Application.EditTarget(edit)) {
        edit = 0;
        return false;
    }
    Drawing drawing(m_Application);
    const auto result = m_Application.DrawEdit(edit, EditorLiteral(label), focus ? 1u : 0u);
    if (active)
        *active = (result & EDITOR_INPUT_ACTIVE) != 0;
    if (result & EDITOR_INPUT_CANCEL) {
        Finish(edit, false);
        return false;
    }
    return (result & EDITOR_INPUT_ACCEPT) != 0;
}
bool EditorWorkspace::Finish(EditorEdit &edit, bool commit) {
    const auto result = edit ? m_Application.FinishEdit(edit, commit) : EDITOR_CANCELLED;
    if (result == EDITOR_QUEUED || result == EDITOR_CANCELLED || result == EDITOR_STALE) {
        edit = 0;
        return true;
    }
    m_Error = "Finish the current operation before applying the name";
    return false;
}
void EditorWorkspace::CloseOwner(EditorView &owner) {
    if (owner)
        m_Application.CloseScope(owner);
    owner = 0;
}
bool EditorWorkspace::Shortcut(const std::string &property, bool consume) const {
    const auto &io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return false;
    const auto found = m_Application.Shortcuts().find(property);
    if (found == m_Application.Shortcuts().end())
        return false;
    for (const auto &shortcut : found->second) {
    if (io.KeyCtrl != bool(shortcut.modifiers & 1) || io.KeyAlt != bool(shortcut.modifiers & 2) ||
        io.KeyShift != bool(shortcut.modifiers & 4) || io.KeySuper != bool(shortcut.modifiers & 8))
        continue;
    ImGuiKey key = ImGuiKey_None;
    if (shortcut.key >= 'A' && shortcut.key <= 'Z')
        key = static_cast<ImGuiKey>(ImGuiKey_A + shortcut.key - 'A');
    else if (shortcut.key >= '0' && shortcut.key <= '9')
        key = static_cast<ImGuiKey>(ImGuiKey_0 + shortcut.key - '0');
    else if (shortcut.key > 256 && shortcut.key <= 280)
        key = static_cast<ImGuiKey>(ImGuiKey_F1 + shortcut.key - 257);
    else if (shortcut.key >= 512 && shortcut.key < 527) {
        const ImGuiKey names[] = {ImGuiKey_Space,     ImGuiKey_Enter,      ImGuiKey_Tab,      ImGuiKey_Escape,
                                  ImGuiKey_Delete,    ImGuiKey_Insert,     ImGuiKey_Home,     ImGuiKey_End,
                                  ImGuiKey_PageUp,    ImGuiKey_PageDown,   ImGuiKey_UpArrow,  ImGuiKey_DownArrow,
                                  ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Backspace};
        key = names[shortcut.key - 512];
    }
    if (key != ImGuiKey_None && ImGui::IsKeyPressed(key, ImGuiInputFlags_None, ImGuiKeyOwner_NoOwner)) {
        if (consume) ImGui::SetKeyOwner(key, ImGui::GetCurrentWindow()->ID, ImGuiInputFlags_LockThisFrame);
        return true;
    }
    }
    return false;
}
const char *EditorWorkspace::ShortcutLabel(const char *property) const {
    for (auto &b : m_Application.Bindings())
        if (b.first == property)
            return b.second.c_str();
    return nullptr;
}
bool EditorWorkspace::EditMenu(const char *location, ObjectRef target, EditorEdit &edit, EditorView &owner) {
    bool opened = false;
    for (auto &m : m_Application.Menus())
        if (m.location == location) {
            const auto label = m.label + "###" + m.id;
            if (m_Application.IsCommand(m.action)) {
                const EditorRequest request{m_Application.Scope(owner), nullptr, 0};
                if (ImGui::MenuItem(m.label.c_str(), ShortcutLabel(m.action.c_str()), false,
                                    m_Application.CommandState(m.action, request) == EDITOR_OK)) {
                    const auto result = m_Application.Execute(m.action, request);
                    if (!result.message.empty()) m_Error = result.message;
                }
            } else if (ImGui::MenuItem(label.c_str(), ShortcutLabel(m.action.c_str()), false,
                                      m_Application.CanEdit(m.action, m_Application.Scope(owner), target) == EDITOR_OK))
                opened = Begin(m.action.c_str(), target, edit, owner) || opened;
        }
    return opened;
}
EditorStatus EditorWorkspace::Menu(const std::string &location, EditorScope scope, ObjectRef target, EditorEdit &edit) {
    if (!m_Views.count(scope.view)) return EDITOR_STALE;
    auto owner=scope.view;
    return EditMenu(location.c_str(),target,edit,owner) ? EDITOR_OK : EDITOR_NO_CHANGE;
}
void EditorWorkspace::CommandMenu(const char *location) {
    if (EditMenu(location, m_Application.Selection(), m_MenuEdit, m_MenuOwner)) m_OpenMenuEdit = true;
}
void EditorWorkspace::CommandShortcuts() {
    for (const auto &binding : m_Application.Shortcuts())
        if (m_Application.IsCommand(binding.first) && Shortcut(binding.first)) {
            const auto result = m_Application.Execute(binding.first, {m_Application.Scope(), nullptr, 0});
            if (!result.message.empty()) m_Error = result.message;
        }
}
EditorView EditorWorkspace::Role(const std::string &role) const {
    for (const auto &entry : m_Views) if (entry.second.role == role && !role.empty()) return entry.first;
    return 0;
}
void EditorWorkspace::CloseFloatingViews() {
    std::vector<EditorView> closed;
    for (const auto &entry : m_Views) {
        const auto &view = entry.second;
        const auto dock = m_DefaultDocks.find(Text(view.desc.id));
        if (view.desc.kind != EDITOR_WINDOW_VIEW || dock == m_DefaultDocks.end() || !dock->second) continue;
        const auto name = WindowName(view.id);
        const auto *window = ImGui::FindWindowByName(name.c_str());
        if (!view.pinned && window && window->WasActive && !window->DockIsActive)
            closed.push_back(view.id);
    }
    for (auto id : closed) Close(id);
}
void EditorWorkspace::DockViews() {
    for (const auto &entry : m_Views) {
        const auto &view = entry.second;
        const auto dock = m_DefaultDocks.find(Text(view.desc.id));
        if (view.desc.kind != EDITOR_WINDOW_VIEW || dock == m_DefaultDocks.end() || !dock->second) continue;
        ImGui::DockBuilderDockWindow(WindowName(view.id).c_str(), dock->second);
    }
}
EditorView EditorWorkspace::Open(const std::string &type, const std::string &role, bool pinned) {
    if (!role.empty())
        if (auto id = Role(role)) {
            if (Text(m_Views.at(id).desc.id) != type) return 0;
            Focus(id);
            return id;
        }
    auto descriptions = m_Application.Views();
    auto d = std::find_if(descriptions.begin(), descriptions.end(),
                          [&](const EditorViewDesc &d) { return Text(d.id) == type; });
    if (d == descriptions.end())
        return 0;
    if (d->area == EDITOR_DOCUMENT && !m_Application.NativeAvailable()) return 0;
    if (d->singleInstance)
        for (const auto &view : m_Views) if (Text(view.second.desc.id) == type) {
            Focus(view.first);
            return view.first;
        }
    const auto id = Create(*d);
    if (id) { m_Views.at(id).role = role; m_Views.at(id).pinned = pinned; }
    return id;
}
EditorView EditorWorkspace::Create(const EditorViewDesc &desc) {
    const auto id = m_Application.OpenScope();
    if (!id)
        return 0;
    void *data = m_Application.CreateView(desc, id);
    if (!data) {
        m_Error = "View could not be opened";
        m_Application.CloseScope(id);
        return 0;
    }
    const auto saved = m_ClosedStates.find(Text(desc.id));
    if (desc.singleInstance && saved != m_ClosedStates.end()) {
        if (saved->second.version && (saved->second.version != desc.stateVersion ||
            m_Application.RestoreViewState(desc, data, m_Application.Scope(id), saved->second.fields) != EDITOR_OK)) {
            m_Application.CloseScope(id);
            m_Application.DestroyView(desc, data);
            m_Error = "View state could not be restored";
            return 0;
        }
    }
    const auto placement = desc.singleInstance && saved != m_ClosedStates.end() ? saved->second.placement : View::Placement{};
    m_Views.emplace(id, View{id, desc, data, true, placement});
    m_LastFocusRequest = id;
    return id;
}
EditorStatus EditorWorkspace::Navigate(const EditorViewDesc &desc, EditorView id,
                                       const EditorRequest &request, EditorSink sink) {
    bool created = false;
    if (!id)
        for (const auto &entry : m_Views)
            if (Text(entry.second.desc.id) == Text(desc.id)) { id = entry.first; break; }
    if (!id) {
        id = Create(desc);
        created = true;
    }
    auto found = m_Views.find(id);
    if (found == m_Views.end()) return created ? EDITOR_ERROR : EDITOR_STALE;
    if (Text(found->second.desc.id) != Text(desc.id)) return EDITOR_INVALID;
    auto &view = found->second;
    EditorStatus status = EDITOR_OK;
    if (view.desc.navigate) {
        const EditorRequest navigation{m_Application.Scope(id), request.values, request.count, {}, request.operation};
        try { Drawing drawing(m_Application); status = view.desc.navigate(view.data, m_Application.Host(), &navigation); }
        catch (...) { status = EDITOR_ERROR; }
    }
    if (status != EDITOR_OK && status != EDITOR_NO_CHANGE) {
        if (created) Destroy(id);
        return status;
    }
    Focus(id);
    const EditorValue value{EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, {}, {}, id};
    if (sink.write) {
        try { sink.write(sink.data, &value, 1); } catch (...) {}
    }
    return EDITOR_OK;
}
std::string EditorWorkspace::WindowName(EditorView id) const {
    const auto found = m_Views.find(id);
    return found == m_Views.end() || found->second.desc.kind == EDITOR_OVERLAY_VIEW ? std::string() :
        Text(found->second.desc.title) + "###editor-view-" + std::to_string(id);
}
bool EditorWorkspace::Focus(EditorView id) {
    const auto found = m_Views.find(id);
    if (found == m_Views.end()) return false;
    found->second.focus = true;
    m_LastFocusRequest = id;
    return true;
}
EditorView EditorWorkspace::Focused() const {
    if (!ImGui::GetCurrentContext()) return 0;
    const auto *nav = ImGui::GetCurrentContext()->NavWindow;
    for (const auto &entry : m_Views) {
        const auto name = WindowName(entry.first);
        const auto *window = name.empty() ? nullptr : ImGui::FindWindowByName(name.c_str());
        for (auto *focused = nav; focused; focused = focused->ParentWindow)
            if (focused == window && window) return entry.first;
    }
    return 0;
}
const EditorResponse &EditorWorkspace::Inspect(EditorView id) {
    const auto found = m_Views.find(id);
    if (found == m_Views.end()) return m_NoView;
    auto &view = found->second;
    try {
        const auto version = m_Application.InspectRevision(view.desc, view.data);
        const auto document = m_Application.Scope().document;
        if (!view.inspected || !view.desc.inspectRevision || version != view.inspectionRevision ||
            document != view.inspectionDocument) {
            view.inspection = m_Application.InspectView(view.desc, view.data, m_Application.Scope(id));
            view.inspected = view.inspection.status == EDITOR_OK;
            view.inspectionRevision = version;
            view.inspectionDocument = document;
        }
    } catch (...) { view.inspected = false; view.inspection = {EDITOR_ERROR}; }
    return view.inspection;
}
EditorStatus EditorWorkspace::Inspect(EditorView id, EditorSink sink) {
    const auto &result = Inspect(id);
    if (result.status != EDITOR_OK) return result.status;
    std::vector<EditorValue> values;
    std::vector<EditorString> texts;
    size_t count = 0;
    for (const auto &field : result.fields) count += field.texts.size();
    texts.reserve(count);
    for (const auto &field : result.fields) {
        const auto first = texts.size();
        for (const auto &text : field.texts) texts.push_back(EditorText(text));
        values.push_back({EditorText(field.key), field.type, field.object, EditorText(field.text), field.integer,
            {field.objects.data(), uint32_t(field.objects.size())}, {field.integers.data(), uint32_t(field.integers.size())},
            {field.texts.empty() ? nullptr : texts.data() + first, uint32_t(field.texts.size())}, field.real});
    }
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
}
EditorStatus EditorWorkspace::PickRegions(EditorView id, const EditorPickRegion *regions, uint32_t count) {
    const auto found = m_Views.find(id);
    if (found == m_Views.end()) return EDITOR_STALE;
    auto &view = found->second;
    view.picks.clear();
    if (count) view.picks.assign(regions, regions + count);
    view.pickFrame = ImGui::GetFrameCount();
    return EDITOR_OK;
}
ObjectRef EditorWorkspace::Pick(float x, float y, bool *handled) const {
    if (handled) *handled=false;
    if (!ImGui::GetCurrentContext()) return {};
    const auto &g=*ImGui::GetCurrentContext();
    // Native parameter picking deliberately passes over the modal editor.
    // Resolve the topmost visible view at the point, including docked tabs.
    for(int i=g.Windows.Size-1;i>=0;--i){
        const auto *window=g.Windows[i];
        if(!window->WasActive||window->Hidden||(window->Flags&(ImGuiWindowFlags_Popup|ImGuiWindowFlags_Tooltip))||
           ((window->Flags&ImGuiWindowFlags_ChildWindow)&&!window->DockIsActive))continue;
        if(window->DockNode&&(!window->DockTabIsVisible||window->DockNode->VisibleWindow!=window))continue;
        if(!window->Rect().Contains(ImVec2(x,y)))continue;
        for(const auto &entry:m_Views){
            const auto &view=entry.second;
            if(view.desc.kind!=EDITOR_WINDOW_VIEW||ImGui::FindWindowByName(WindowName(view.id).c_str())!=window)continue;
            if(handled)*handled=true;
            if(ImGui::GetFrameCount()-view.pickFrame>1)return {};
            for(const auto &region:view.picks)if(region.object.document==m_Application.Scope().document&&x>=region.left&&y>=region.top&&x<region.right&&y<region.bottom)return region.object;
            return {};
        }
        // A legacy window at this point remains responsible for its own picker.
        return {};
    }
    return {};
}
bool EditorWorkspace::Interacting() const {
    for (const auto &view : m_Views) if (view.second.active) return true;
    return false;
}
bool EditorWorkspace::Close(EditorView id) {
    const auto found = m_Views.find(id);
    if (found == m_Views.end()) return false;
    const auto &view = found->second;
    if (view.pinned) return false;
    if (view.desc.area == EDITOR_DOCUMENT && !m_Application.NativeAvailable()) return false;
    if (view.desc.singleInstance) {
        SavedView saved{0, {}, {}};
        if (view.desc.saveState) {
            const auto state = m_Application.SaveViewState(view.desc, view.data, m_Application.Scope(id));
            if (state.status != EDITOR_OK) { m_Error = "View state could not be saved"; return false; }
            saved.version = view.desc.stateVersion;
            saved.fields = state.fields;
        }
        const auto name = WindowName(id);
        if (auto *window = name.empty() || !ImGui::GetCurrentContext() ? nullptr : ImGui::FindWindowByName(name.c_str()))
            saved.placement = {window->Pos.x, window->Pos.y, window->Size.x, window->Size.y, window->DockId, true};
        m_ClosedStates[Text(view.desc.id)] = std::move(saved);
    }
    Destroy(id);
    return true;
}
void EditorWorkspace::Destroy(EditorView id) {
    auto found = m_Views.find(id);
    if (found == m_Views.end())
        return;
    if (m_LastFocusRequest == id) m_LastFocusRequest = 0;
    m_Application.CloseScope(id);
    if (!m_Application.DestroyView(found->second.desc, found->second.data)) {
        m_Error = "View could not be closed";
    }
    m_Views.erase(found);
}
void EditorWorkspace::CloseAll() {
    while (!m_Views.empty())
        Destroy(m_Views.begin()->first);
    m_ClosedStates.clear();
}
void EditorWorkspace::Draw(bool nativeAvailable) {
    const auto focus = m_LastFocusRequest;
    bool focusDrawn = false;
    std::vector<EditorView> closed;
    for (auto &v : m_Views) {
        auto &view = v.second;
        if (!nativeAvailable && view.desc.area == EDITOR_DOCUMENT) continue;
        if (view.id == focus) focusDrawn = true;
        const auto name = WindowName(view.id);
        EditorViewFrame frame{EditorText(name), uint32_t(view.focus), 0, uint32_t(view.placement.apply),
            view.placement.dock, view.placement.x, view.placement.y, view.placement.width, view.placement.height,
            uint32_t(!view.pinned), 0};
        if (const auto dock = m_DefaultDocks.find(Text(view.desc.id)); dock != m_DefaultDocks.end()) frame.initialDock = dock->second;
        try {
            Drawing drawing(m_Application);
            view.desc.draw(view.data, m_Application.Host(), view.id, &frame);
        } catch (...) {
            m_Error = "View raised an exception";
        }
        view.focus = false;
        view.active = frame.active != 0;
        view.placement.apply = false;
        if (frame.close)
            closed.push_back(view.id);
    }
    for (auto id : closed)
        Close(id);
    // Each request also selects that view's dock tab. Drawing order must not
    // replace the final focus request with the highest-numbered instance.
    if (focusDrawn) {
        const auto name = WindowName(focus);
        if (!name.empty()) ImGui::SetWindowFocus(name.c_str());
        if (m_LastFocusRequest == focus) m_LastFocusRequest = 0;
    }
    if (m_OpenMenuEdit) {
        ImGui::OpenPopup("Rename##workspace");
        m_OpenMenuEdit = false;
    }
    if (ImGui::BeginPopupModal("Rename##workspace", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool enter = DrawInput(m_MenuEdit, "##name", ImGui::IsWindowAppearing());
        if (ImGui::Button("OK") || enter) {
            if (Finish(m_MenuEdit, true))
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape) || !m_MenuEdit) {
            Finish(m_MenuEdit, false);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
bool EditorWorkspace::Configure(const EditorConfiguration &configuration, std::string &error) {
    const auto previous = m_Application.Configuration();
    if (!m_Application.Configure(configuration, error)) return false;
    struct Replacement { EditorView old; View next; };
    std::vector<Replacement> replacements;
    const auto rollback = [&] {
        for (auto &replacement : replacements) {
            m_Application.CloseScope(replacement.next.id);
            m_Application.DestroyView(replacement.next.desc, replacement.next.data);
        }
        std::string ignored;
        m_Application.Configure(previous, ignored);
        error = "View could not be replaced";
        return false;
    };
    for (const auto &saved : m_ClosedStates)
        for (const auto &desc : m_Application.Views())
            if (Text(desc.id) == saved.first && saved.second.version &&
                (saved.second.version != desc.stateVersion || !desc.restoreState)) return rollback();
    for (const auto &entry : m_Views) {
        for (const auto &description : m_Application.Views()) {
            if (Text(description.id) != Text(entry.second.desc.id) ||
                Text(description.provider) == Text(entry.second.desc.provider)) continue;
            const auto &old = entry.second;
            if (old.desc.area == EDITOR_DOCUMENT && !m_Application.NativeAvailable()) return rollback();
            const auto state = m_Application.SaveViewState(old.desc, old.data, m_Application.Scope(old.id));
            if (state.status != EDITOR_OK && state.status != EDITOR_NO_CHANGE) return rollback();
            if (state.status == EDITOR_OK && (old.desc.stateVersion != description.stateVersion || !description.restoreState))
                return rollback();
            const auto id = m_Application.OpenScope();
            void *data = m_Application.CreateView(description, id);
            if (!data) {
                m_Application.CloseScope(id);
                return rollback();
            }
            View replacement{id, description, data, entry.second.focus, {}};
            replacement.role = old.role;
            replacement.pinned = old.pinned;
            const auto name = WindowName(entry.first);
            if (auto *window = name.empty() || !ImGui::GetCurrentContext() ? nullptr : ImGui::FindWindowByName(name.c_str()))
                replacement.placement = {window->Pos.x, window->Pos.y, window->Size.x, window->Size.y, window->DockId, true};
            replacement.focus = replacement.focus || Focused() == entry.first;
            replacements.push_back({entry.first, std::move(replacement)});
            if (state.status == EDITOR_OK &&
                m_Application.RestoreViewState(description, data, m_Application.Scope(id), state.fields) != EDITOR_OK)
                return rollback();
        }
    }
    const auto pendingFocus = m_LastFocusRequest, focused = Focused();
    EditorView replacementFocus = 0;
    for (auto &replacement : replacements) {
        if (replacement.old == pendingFocus || (!pendingFocus && replacement.old == focused))
            replacementFocus = replacement.next.id;
        Destroy(replacement.old);
        m_Views.emplace(replacement.next.id, std::move(replacement.next));
    }
    if (replacementFocus) m_LastFocusRequest = replacementFocus;
    return true;
}
void EditorWorkspace::Menu() {
    if (!ImGui::BeginMenu("Views"))
        return;
    for (auto &d : m_Application.Views())
        if (d.kind == EDITOR_WINDOW_VIEW && ImGui::MenuItem(Text(d.title).c_str())) {
            const EditorValue type{EDITOR_TEXT("type"), EDITOR_TEXT_VALUE, {}, d.id};
            m_Application.Execute("workspace.open-view", {m_Application.Scope(), &type, 1});
        }
    if (ImGui::BeginMenu("Text Editors")) {
        for (auto &editor : m_Application.TextEditors()) {
            const auto id = Text(editor.id), provider = Text(editor.provider);
            const auto label = Text(editor.title);
            if (ImGui::MenuItem(label.c_str())) {
                auto config = m_Application.Configuration();
                config.providers.erase(
                    std::remove_if(config.providers.begin(), config.providers.end(),
                                   [&](const ProviderChoice &p) { return p.kind == "editor" && p.id == id; }),
                    config.providers.end());
                config.providers.push_back({"editor", id, provider});
                Configure(config, m_Error);
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Shortcuts")) {
        const auto bindings = m_Application.Bindings();
        std::set<std::string> shown;
        for (auto &binding : bindings) {
            if (!shown.insert(binding.first).second) continue;
            const auto *title = m_Application.ActionTitle(binding.first);
            if (title && ImGui::BeginMenu(title)) {
                for (const char *chord : {"F2", "Ctrl+R", "Alt+R"})
                    if (ImGui::MenuItem(chord, nullptr, binding.second == chord)) {
                        auto config = m_Application.Configuration();
                        config.bindings.erase(std::remove_if(config.bindings.begin(), config.bindings.end(),
                                                             [&](const auto &b) { return b.first == binding.first; }),
                                              config.bindings.end());
                        config.bindings.emplace_back(binding.first, chord);
                        Configure(config, m_Error);
                    }
                ImGui::EndMenu();
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View Providers")) {
        for (const auto &view : m_Application.Views(true)) {
            const auto id = Text(view.id), provider = Text(view.provider);
            const auto label = Text(view.title) + " (" + provider + ")";
            if (ImGui::MenuItem(label.c_str())) {
                auto config = m_Application.Configuration();
                config.providers.erase(std::remove_if(config.providers.begin(), config.providers.end(),
                    [&](const ProviderChoice &choice) { return choice.kind == "view" && choice.id == id; }), config.providers.end());
                config.providers.push_back({"view", id, provider});
                Configure(config, m_Error);
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Menu Entries")) {
        const auto entries = m_Application.Menus();
        for (auto &entry : entries) {
            ImGui::PushID(entry.id.c_str());
            if (ImGui::BeginMenu(entry.label.c_str())) {
                for (auto &location : m_Application.MenuLocations())
                    if (ImGui::MenuItem(location.second.c_str(), nullptr, entry.location == location.first)) {
                        auto config = m_Application.Configuration();
                        config.menus.erase(std::remove_if(config.menus.begin(), config.menus.end(),
                                                          [&](const MenuPlacement &m) { return m.id == entry.id; }),
                                           config.menus.end());
                        config.menus.push_back({entry.id, location.first, entry.order});
                        Configure(config, m_Error);
                    }
                ImGui::EndMenu();
            }
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }
    if (EditMenu("main.edit", m_Application.Selection(), m_MenuEdit, m_MenuOwner))
        m_OpenMenuEdit = true;
    if (!m_Error.empty())
        ImGui::TextUnformatted(m_Error.c_str());
    ImGui::EndMenu();
}
} // namespace vdev

namespace vdev {
void EditorWorkspace::DocumentChanging() {
    std::vector<EditorView> close;
    for (const auto &item : m_Views)
        if (item.second.desc.area == EDITOR_DOCUMENT && item.second.desc.kind == EDITOR_OVERLAY_VIEW)
            close.push_back(item.first);
    for (auto id : close) Close(id);
}
} // namespace vdev
