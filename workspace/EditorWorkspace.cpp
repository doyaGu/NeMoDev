#include "workspace/EditorWorkspace.h"
#include "workspace/EditorClient.h"

#include <algorithm>
#include <set>
#include <cmath>
#include <iterator>
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
bool OwnsWindow(const ImGuiWindow *owner, const ImGuiWindow *window) {
    if (!owner)
        return false;
    for (; window; window = window->ParentWindow)
        if (window == owner)
            return true;
    return false;
}
int OwnedPopup(const ImGuiWindow *window, bool modalOnly = false) {
    const auto &popups = ImGui::GetCurrentContext()->OpenPopupStack;
    for (int i = 0; i < popups.Size; ++i) {
        const auto &popup = popups[i];
        if (modalOnly && (!popup.Window || !(popup.Window->Flags & ImGuiWindowFlags_Modal)))
            continue;
        if (OwnsWindow(window, popup.Window) || (!popup.Window && OwnsWindow(window, popup.RestoreNavWindow)))
            return i;
    }
    return popups.Size;
}
void CloseWindowInput(ImGuiWindow *window) {
    auto &context = *ImGui::GetCurrentContext();
    if (OwnsWindow(window, context.ActiveIdWindow)) {
        // The payload may outlive its source item while the button is held.
        // Destroying that view cancels the drag before another view can use it.
        if (context.DragDropActive && context.DragDropPayload.SourceId == context.ActiveId)
            ImGui::ClearDragDrop();
        // ClearActiveID saves text for the next draw. A destroyed view must not
        // apply that text when another instance reuses its window ID.
        const auto active = context.ActiveId;
        ImGui::ClearActiveID();
        if (context.InputTextDeactivatedState.ID == active)
            context.InputTextDeactivatedState.ID = 0;
    }
    const auto popup = OwnedPopup(window);
    if (popup < context.OpenPopupStack.Size)
        ImGui::ClosePopupToLevel(popup, false);
}
} // namespace
EditorWorkspace::EditorWorkspace(EditorApplication &application) : m_Application(application) {
    m_Application.ViewNavigation(this);
}
bool EditorWorkspace::Register(std::string &error) {
    const EditorMenuLocation location{EDITOR_TEXT("main.edit"), EDITOR_TEXT("Views menu")};
    const EditorParameter type[] = {{EDITOR_TEXT("type"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("role"), EDITOR_TEXT_VALUE, 1}, {EDITOR_TEXT("pinned"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter instance{EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, 1};
    const EditorParameter role{EDITOR_TEXT("role"), EDITOR_TEXT_VALUE};
    const EditorParameter savedInstance{EDITOR_TEXT("instance"), EDITOR_INTEGER_VALUE};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("workspace.reopen-instance"), EDITOR_TEXT("builtin.workspace"),
         EDITOR_TEXT("Reopen Saved View"), &savedInstance, 1,
         [](void *p, const EditorHost *, const EditorRequest *r, EditorSink sink) {
             auto &self = *static_cast<EditorWorkspace *>(p);
             std::string error;
             const auto id = self.Reopen(r->values[0].integer, error);
             if (!id)
                 return EDITOR_ERROR;
             const auto value = IntegerArgument("view", id);
             if (sink.write)
                 sink.write(sink.data, &value, 1);
             return EDITOR_OK;
         },
         this, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("workspace.save"), EDITOR_TEXT("builtin.workspace"),
         EDITOR_TEXT("Save Workspace"), nullptr, 0,
         [](void *p, const EditorHost *, const EditorRequest *, EditorSink sink) {
             auto &self = *static_cast<EditorWorkspace *>(p);
             if (!self.m_WriteConfiguration)
                 return EDITOR_REFUSED;
             WorkspaceState state;
             std::string error;
             const bool saved =
                 self.Save(state, error) && self.m_WriteConfiguration(self.m_Application.Configuration(), state, error);
             if (!saved && sink.write) {
                 const auto message = TextArgument("message", error);
                 sink.write(sink.data, &message, 1);
             }
             if (saved && ImGui::GetCurrentContext()) {
                 ImGui::GetIO().WantSaveIniSettings = false;
                 ImGui::GetCurrentContext()->SettingsDirtyTimer = 0;
             }
             return saved ? EDITOR_OK : EDITOR_ERROR;
         },
         this, EDITOR_WORKSPACE, EDITOR_FILE_OPERATION},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("workspace.discard-saved-state"), EDITOR_TEXT("builtin.workspace"),
         EDITOR_TEXT("Discard Unrestored Workspace"), nullptr, 0,
         [](void *data, const EditorHost *, const EditorRequest *, EditorSink) {
             auto &self = *static_cast<EditorWorkspace *>(data);
             self.m_RestoreFailed = false;
             self.m_Saved.clear();
             self.m_ClosedStates.clear();
             ++self.m_ViewsRevision;
             return EDITOR_OK;
         },
         this, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("workspace.open-view"), EDITOR_TEXT("builtin.workspace"),
         EDITOR_TEXT("Open View"), type, 3, OpenCommand, this, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("workspace.focus-view"), EDITOR_TEXT("builtin.workspace"),
         EDITOR_TEXT("Focus View"), &instance, 1, FocusCommand, this, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("workspace.close-view"), EDITOR_TEXT("builtin.workspace"),
         EDITOR_TEXT("Close View"), &instance, 1, CloseCommand, this, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("workspace.open-role"), EDITOR_TEXT("builtin.workspace"),
         EDITOR_TEXT("Open Primary View"), &role, 1,
         [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
             const auto id = static_cast<EditorWorkspace *>(data)->OpenRole(Text(request->values[0].text));
             if (!id)
                 return EDITOR_INVALID;
             const EditorValue field{EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, {}, {}, id};
             if (sink.write)
                 sink.write(sink.data, &field, 1);
             return EDITOR_OK;
         },
         this, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE}};
    const EditorParameter fields[] = {{EDITOR_TEXT("views"), EDITOR_INTEGER_LIST},
        {EDITOR_TEXT("types"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("providers"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("titles"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("focused"), EDITOR_INTEGER_VALUE}};
    const EditorParameter viewFields[] = {{EDITOR_TEXT("type"), EDITOR_TEXT_VALUE},
                                          {EDITOR_TEXT("provider"), EDITOR_TEXT_VALUE},
                                          {EDITOR_TEXT("window"), EDITOR_TEXT_VALUE},
                                          {EDITOR_TEXT("focused"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("available"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("workspace.views"), EDITOR_TEXT("builtin.workspace"), nullptr, 0,
         ListQuery, this, EDITOR_QUERY_EACH_READ, EDITOR_WORKSPACE, fields, 5},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("workspace.view"), EDITOR_TEXT("builtin.workspace"), &instance, 1,
         ViewQuery, this, EDITOR_QUERY_EACH_READ, EDITOR_WORKSPACE, viewFields, 5}};
    const EditorParameter instanceFields[] = {
        {EDITOR_TEXT("views"), EDITOR_INTEGER_LIST},  {EDITOR_TEXT("types"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("providers"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("titles"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("roles"), EDITOR_TEXT_LIST},     {EDITOR_TEXT("available"), EDITOR_INTEGER_LIST}};
    const EditorQueryDesc instances{
        sizeof(EditorQueryDesc),
        EDITOR_TEXT("workspace.instances"),
        EDITOR_TEXT("builtin.workspace"),
        nullptr,
        0,
        InstanceQuery,
        this,
        EDITOR_QUERY_VERSIONED,
        EDITOR_WORKSPACE,
        instanceFields,
        6,
        [](void *data, const EditorHost *) { return static_cast<EditorWorkspace *>(data)->m_ViewsRevision; }};
    std::vector<EditorQueryDesc> allQueries(std::begin(queries), std::end(queries));
    allQueries.push_back(instances);
    const EditorParameter roleField{EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE};
    allQueries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("workspace.role"), EDITOR_TEXT("builtin.workspace"),
        &role, 1, [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
            const auto id = static_cast<EditorWorkspace *>(data)->Role(Text(request->values[0].text));
            const EditorValue value{EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, {}, {}, id};
            return sink.write ? sink.write(sink.data, &value, 1) : EDITOR_INVALID;
        }, this, EDITOR_QUERY_VERSIONED, EDITOR_WORKSPACE, &roleField, 1,
        [](void *data, const EditorHost *) { return static_cast<EditorWorkspace *>(data)->m_ViewsRevision; }});
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EditorLiteral("workspace.views");
    module.locations = &location;
    module.locationCount = 1;
    module.commands = commands;
    module.commandCount = uint32_t(std::size(commands));
    module.queries = allQueries.data();
    module.queryCount = uint32_t(allQueries.size());
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
    const EditorValue fields[] = {
        {EDITOR_TEXT("type"), EDITOR_TEXT_VALUE, {}, found->second.desc.id},
        {EDITOR_TEXT("provider"), EDITOR_TEXT_VALUE, {}, found->second.desc.provider},
        {EDITOR_TEXT("window"), EDITOR_TEXT_VALUE, {}, EditorText(name)},
        {EDITOR_TEXT("focused"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(self.Focused() == id)},
        {EDITOR_TEXT("available"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(!found->second.unavailable)}};
    return sink.write ? sink.write(sink.data, fields, 5) : EDITOR_INVALID;
}
EditorStatus EditorWorkspace::InstanceQuery(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
    const auto &self = *static_cast<EditorWorkspace *>(data);
    std::vector<uint64_t> ids, available;
    std::vector<EditorString> types, providers, titles, roles;
    for (const auto &entry : self.m_Views) {
        ids.push_back(entry.first);
        available.push_back(!entry.second.unavailable);
        types.push_back(entry.second.desc.id);
        providers.push_back(entry.second.desc.provider); titles.push_back(entry.second.desc.title);
        roles.push_back(EditorText(entry.second.role));
    }
    const EditorValue fields[] = {
        {EDITOR_TEXT("views"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {ids.data(), uint32_t(ids.size())}},
        {EDITOR_TEXT("types"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {types.data(), uint32_t(types.size())}},
        {EDITOR_TEXT("providers"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {providers.data(), uint32_t(providers.size())}},
        {EDITOR_TEXT("titles"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {titles.data(), uint32_t(titles.size())}},
        {EDITOR_TEXT("roles"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {roles.data(), uint32_t(roles.size())}},
        {EDITOR_TEXT("available"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {available.data(), uint32_t(available.size())}}};
    return sink.write ? sink.write(sink.data, fields, 6) : EDITOR_INVALID;
}
bool EditorWorkspace::Begin(const char *property, ObjectRef object, EditorEdit &edit, EditorView &owner,
                            const std::vector<EditorField> &arguments) {
    if (edit)
        Finish(edit, false);
    if (!owner)
        owner = m_Application.OpenScope();
    std::vector<EditorField> fields{{"target", EDITOR_OBJECT, object}};
    fields.insert(fields.end(), arguments.begin(), arguments.end());
    const auto values = BorrowEditorFields(fields);
    return m_Application.BeginEdit(property,
                                   {m_Application.Scope(owner), values.data(), uint32_t(values.values.size())},
                                   edit) == EDITOR_OK;
}
bool EditorWorkspace::DrawInput(EditorEdit &edit, const char *label, bool focus, bool *active, EditorValueStyle style,
                                float width, float height) {
    if (active)
        *active = false;
    if (!edit)
        return false;
    if (!m_Application.EditTarget(edit)) {
        edit = 0;
        return false;
    }
    Drawing drawing(m_Application);
    const auto result = m_Application.DrawEdit(edit, EditorLiteral(label), focus ? 1u : 0u, style, width, height);
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
    m_Error = "Finish the current operation before applying the value";
    return false;
}
void EditorWorkspace::CloseOwner(EditorView &owner) {
    if (owner)
        m_Application.CloseScope(owner);
    owner = 0;
}
bool EditorWorkspace::Shortcut(const std::string &property, bool consume) const {
    return Binding(property, EDITOR_KEY_PRESSED, consume);
}
bool EditorWorkspace::Binding(const std::string &property, EditorInputPhase phase, bool consume) const {
    const auto &io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return false;
    const auto found = m_Application.Shortcuts().find(property);
    if (found == m_Application.Shortcuts().end()) return false;
    for (const auto &shortcut : found->second) {
        const unsigned modifiers =
            (io.KeyCtrl ? 1u : 0u) | (io.KeyAlt ? 2u : 0u) | (io.KeyShift ? 4u : 0u) | (io.KeySuper ? 8u : 0u);
        // A held navigation key stays held when another modifier is added.
        // Press accelerators keep their exact chord, as before.
        if (phase == EDITOR_KEY_PRESSED ? modifiers != shortcut.modifiers
                                        : (modifiers & shortcut.modifiers) != shortcut.modifiers)
            continue;
        ImGuiKey key = ImGuiKey_None;
        if (shortcut.key >= 'A' && shortcut.key <= 'Z')
            key = static_cast<ImGuiKey>(ImGuiKey_A + shortcut.key - 'A');
        else if (shortcut.key >= '0' && shortcut.key <= '9')
            key = static_cast<ImGuiKey>(ImGuiKey_0 + shortcut.key - '0');
        else if (shortcut.key > 256 && shortcut.key <= 280)
            key = static_cast<ImGuiKey>(ImGuiKey_F1 + shortcut.key - 257);
        else if (shortcut.key >= 512 && shortcut.key < 535) {
            const ImGuiKey names[] = {
                ImGuiKey_Space,     ImGuiKey_Enter,       ImGuiKey_Tab,        ImGuiKey_Escape,
                ImGuiKey_Delete,    ImGuiKey_Insert,      ImGuiKey_Home,       ImGuiKey_End,
                ImGuiKey_PageUp,    ImGuiKey_PageDown,    ImGuiKey_UpArrow,    ImGuiKey_DownArrow,
                ImGuiKey_LeftArrow, ImGuiKey_RightArrow,  ImGuiKey_Backspace,  ImGuiKey_Equal,
                ImGuiKey_Minus,     ImGuiKey_KeypadEnter, ImGuiKey_KeypadAdd,  ImGuiKey_KeypadSubtract,
                ImGuiKey_MouseLeft, ImGuiKey_MouseRight,  ImGuiKey_MouseMiddle};
            key = names[shortcut.key - 512];
        }
        const bool matched =
            key != ImGuiKey_None &&
            (phase == EDITOR_KEY_RELEASED ? ImGui::IsKeyReleased(key, ImGuiKeyOwner_NoOwner)
             : phase == EDITOR_KEY_DOWN   ? ImGui::IsKeyDown(key, ImGuiKeyOwner_NoOwner)
                                          : ImGui::IsKeyPressed(key, ImGuiInputFlags_None, ImGuiKeyOwner_NoOwner));
        if (matched) {
            if (consume) ImGui::SetKeyOwner(key, ImGui::GetCurrentWindow()->ID, ImGuiInputFlags_LockThisFrame);
            return true;
        }
    }
    return false;
}
const char *EditorWorkspace::ShortcutLabel(const char *property) const {
    for (auto &b : m_Application.Bindings())
        if (b.first == property) return b.second.c_str();
    return nullptr;
}
void EditorWorkspace::CommandResult(const EditorResponse &result) {
    if (result.message.empty()) return;
    m_Error = result.message;
    // Deferred commands are reported once by EndFrame after they run.
    if (result.status != EDITOR_QUEUED && m_Application.report)
        m_Application.report(result);
}
bool EditorWorkspace::EditMenu(const char *location, ObjectRef target, EditorEdit &edit, EditorView &owner) {
    bool opened = false;
    for (auto &m : m_Application.Menus())
        if (m.location == location) {
            const auto label = m.label + "###" + m.id;
            if (m_Application.IsCommand(m.action)) {
                const EditorRequest request{m_Application.Scope(owner), nullptr, 0};
                if (ImGui::MenuItem(m.label.c_str(), ShortcutLabel(m.action.c_str()), Checked(m, request.scope),
                                    m_Application.CommandState(m.action, request) == EDITOR_OK)) {
                    const auto result = m_Application.Execute(m.action, request);
                    CommandResult(result);
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
bool EditorWorkspace::Checked(const MenuEntry &menu, EditorScope scope) {
    if (menu.checked.empty()) return false;
    const auto &state = m_Application.ReadQuery(menu.checked, {scope, nullptr, 0});
    return state.status == EDITOR_OK && state.fields.size() == 1 && state.fields[0].integer == 1;
}
void EditorWorkspace::CommandShortcuts() {
    for (const auto &binding : m_Application.Shortcuts())
        if (m_Application.ActionView(binding.first).empty() && m_Application.IsCommand(binding.first) &&
            Binding(binding.first, m_Application.BindingPhase(binding.first), false) &&
            m_Application.CommandState(binding.first, {m_Application.Scope()}) == EDITOR_OK &&
            Binding(binding.first, m_Application.BindingPhase(binding.first), true)) {
            const auto result = m_Application.Execute(binding.first, {m_Application.Scope(), nullptr, 0});
            CommandResult(result);
        }
}
EditorView EditorWorkspace::ActionView(const std::string &type, EditorView source) const {
    if (source) {
        const auto found = m_Views.find(source);
        return found != m_Views.end() && !found->second.unavailable && Text(found->second.desc.id) == type
                   ? source : 0;
    }
    if (const auto id = LastFocused(type); id && !m_Views.at(id).unavailable) return id;
    for (const auto &entry : m_Views)
        if (!entry.second.unavailable && Text(entry.second.desc.id) == type) return entry.first;
    return 0;
}
bool EditorWorkspace::InputBinding(EditorScope scope, const std::string &action, EditorInputPhase phase,
                                   bool consume) const {
    const auto type = m_Application.ActionView(action);
    if (!type.empty()) {
        const auto found = m_Views.find(scope.view);
        if (Focused() != scope.view || found == m_Views.end() || Text(found->second.desc.id) != type) return false;
    }
    return Binding(action, phase, consume);
}
void EditorWorkspace::Shortcuts(EditorScope scope) {
    if (Focused() != scope.view) return;
    const auto found = m_Views.find(scope.view);
    if (found == m_Views.end()) return;
    const auto type = Text(found->second.desc.id);
    for (const auto &binding : m_Application.Shortcuts())
        if (m_Application.ActionView(binding.first) == type &&
            Binding(binding.first, m_Application.BindingPhase(binding.first), false) &&
            m_Application.CommandState(binding.first, {scope}) == EDITOR_OK &&
            Binding(binding.first, m_Application.BindingPhase(binding.first), true)) {
            const auto result = m_Application.Execute(binding.first, {scope});
            CommandResult(result);
        }
}
EditorStatus EditorWorkspace::Toolbar(const std::string &location, EditorScope scope, const EditorToolbarFrame &area) {
    const auto declared = m_Application.Toolbars().find(location);
    if (declared == m_Application.Toolbars().end() || area.size != sizeof(area) || !std::isfinite(area.left) ||
        !std::isfinite(area.top) || !std::isfinite(area.width) || !std::isfinite(area.height) ||
        !std::isfinite(area.scale) || area.scale <= 0 || area.width < 0 || area.height < 0)
        return EDITOR_INVALID;
    const auto owner = m_Views.find(scope.view);
    if (declared->second.view.size &&
        (owner == m_Views.end() || Text(owner->second.desc.id) != Text(declared->second.view)))
        return EDITOR_REFUSED;
    for (const auto &item : m_Application.ToolbarItems()) {
        if (item.location != location) continue;
        EditorToolbarFrame frame = area;
        frame.left =
            area.left + ((item.flags & EDITOR_TOOLBAR_RIGHT) ? area.width - item.x * area.scale : item.x * area.scale);
        frame.top = area.top + item.y * area.scale;
        frame.width = (item.flags & EDITOR_TOOLBAR_STRETCH)
                          ? std::max(1.0f, area.width - (item.x + item.width) * area.scale)
                          : item.width * area.scale;
        frame.height = item.height * area.scale;
        frame.action = EditorText(item.action);
        frame.title = EditorText(item.title);
        frame.tooltip = EditorText(item.tooltip);
        const auto *shortcut = ShortcutLabel(item.action.c_str());
        frame.shortcut = shortcut ? EditorLiteral(shortcut) : EditorString{};
        frame.enabled = 1;
        frame.checked = 0;
        const auto values = BorrowEditorFields(item.arguments);
        const EditorRequest request{scope, values.data(), uint32_t(values.size())};
        if (!item.action.empty()) frame.enabled = m_Application.CommandState(item.action, request) == EDITOR_OK;
        if (const auto *tool = m_Application.ActiveTool(scope.view); tool && Text(tool->id) == item.action)
            frame.checked = 1;
        if (!item.checked.empty()) {
            const auto checked = m_Application.Query(item.checked, {scope});
            if (checked.status == EDITOR_OK) frame.checked = EditorClient::Integer(checked, "checked") != 0;
        }
        bool clicked = false;
        if (item.draw) {
            try {
                clicked = item.draw(item.data, m_Application.Host(), &request, &frame) != 0;
            } catch (...) {
                m_Error = "Toolbar drawing failed: " + item.id;
            }
        } else {
            ImGui::PushID(item.id.c_str());
            ImGui::SetCursorScreenPos(ImVec2(frame.left, frame.top));
            ImGui::BeginDisabled(!frame.enabled);
            clicked = ImGui::Button(item.title.empty() ? item.action.c_str() : item.title.c_str(),
                                    ImVec2(frame.width, frame.height));
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (clicked && !item.action.empty()) {
            const auto result = m_Application.Execute(item.action, request);
            CommandResult(result);
        }
    }
    return EDITOR_OK;
}
EditorView EditorWorkspace::Role(const std::string &role) const {
    for (const auto &entry : m_Views) if (entry.second.role == role && !role.empty()) return entry.first;
    return 0;
}
void EditorWorkspace::CloseFloatingViews() {
    std::vector<EditorView> closed;
    for (const auto &entry : m_Views) {
        const auto &view = entry.second;
        const auto dock = ViewDock(view);
        if (view.desc.kind != EDITOR_WINDOW_VIEW || !dock) continue;
        const auto name = WindowName(view.id);
        const auto *window = ImGui::FindWindowByName(name.c_str());
        const auto *placement = Placement(view);
        const bool keep = placement && placement->keepOnReset;
        if (!view.pinned && !keep && window && window->WasActive && !window->DockIsActive)
            closed.push_back(view.id);
    }
    for (auto id : closed) Close(id);
}
void EditorWorkspace::DockViews() {
    for (const auto &entry : m_Views) {
        const auto &view = entry.second;
        const auto dock = ViewDock(view);
        if (view.desc.kind != EDITOR_WINDOW_VIEW || !dock) continue;
        ImGui::DockBuilderDockWindow(WindowName(view.id).c_str(), dock);
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
        for (auto &view : m_Views) if (Text(view.second.desc.id) == type) {
            if (!role.empty()) { view.second.role = role; view.second.pinned = pinned; ++m_ViewsRevision; }
            Focus(view.first);
            return view.first;
        }
    const auto id = Create(*d, role);
    if (id) { m_Views.at(id).role = role; m_Views.at(id).pinned = pinned; }
    return id;
}
EditorView EditorWorkspace::Create(const EditorViewDesc &desc, const std::string &role) {
    const auto id = m_Application.OpenScope();
    if (!id)
        return 0;
    void *data = m_Application.CreateView(desc, id);
    if (!data) {
        m_Error = "View could not be opened";
        m_Application.CloseScope(id);
        return 0;
    }
    const auto saved = m_ClosedStates.find({Text(desc.id), desc.singleInstance ? std::string{} : role});
    if (saved != m_ClosedStates.end()) {
        if (saved->second.version && (saved->second.version != desc.stateVersion ||
            m_Application.RestoreViewState(desc, data, m_Application.Scope(id), saved->second.fields) != EDITOR_OK)) {
            m_Application.CloseScope(id);
            m_Application.DestroyView(desc, data);
            m_Error = "View state could not be restored";
            return 0;
        }
    }
    if (saved != m_ClosedStates.end() && !saved->second.tool.empty()) m_Application.RestoreTool(id, saved->second.tool);
    const auto placement = saved != m_ClosedStates.end() ? saved->second.placement : View::Placement{};
    View view{id, desc, data, true, placement};
    view.instance = m_NextInstance = std::max(m_NextInstance, id);
    ++m_NextInstance;
    for (const auto &entry : m_Saved)
        if (!entry.second.open && entry.second.type == Text(desc.id) &&
            (desc.singleInstance || (!role.empty() && entry.second.role == role))) {
            if (saved == m_ClosedStates.end() && !ApplySettings(entry.second, desc, data, id, m_Error)) {
                m_Application.CloseScope(id);
                m_Application.DestroyView(desc, data);
                return 0;
            }
            view.instance = entry.first;
            view.placement = {entry.second.x,      entry.second.y,    entry.second.width,
                              entry.second.height, entry.second.dock, entry.second.placed};
            break;
        }
    m_Views.emplace(id, std::move(view));
    ++m_ViewsRevision;
    m_LastFocusRequest = id;
    return id;
}
EditorStatus EditorWorkspace::Navigate(const EditorViewDesc &desc, EditorView id,
                                       const EditorRequest &request, EditorSink sink) {
    bool background = false;
    for (uint32_t i = 0; i < request.count; ++i) {
        const auto &value = request.values[i];
        if (Text(value.key) == "background" && value.type == EDITOR_INTEGER_VALUE)
            background = value.integer != 0;
    }
    const auto previousFocus = m_LastFocusRequest;
    bool created = false;
    const auto requested = id;
    id = ActionView(Text(desc.id), id);
    if (requested && !id) {
        const auto found = m_Views.find(requested);
        return found == m_Views.end() || found->second.unavailable ? EDITOR_STALE : EDITOR_INVALID;
    }
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
        if (created) { Destroy(id); m_LastFocusRequest = previousFocus; }
        return status;
    }
    if (!background) Focus(id);
    else if (created) {
        view.focus = false;
        m_LastFocusRequest = previousFocus;
    }
    ++m_ViewsRevision;
    const EditorValue value{EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, {}, {}, id};
    if (sink.write) {
        try { sink.write(sink.data, &value, 1); } catch (...) {}
    }
    return EDITOR_OK;
}
std::string EditorWorkspace::WindowName(EditorView id) const {
    const auto found = m_Views.find(id);
    return found == m_Views.end() || found->second.desc.kind == EDITOR_OVERLAY_VIEW ? std::string() : [&] {
        const auto &view = found->second;
        std::string title = Text(view.desc.title);
        if (view.inspected)
            for (const auto &field : view.inspection.fields)
                if (field.key == "title" && field.type == EDITOR_TEXT_VALUE && !field.text.empty() &&
                    field.text.find('\0') == std::string::npos)
                    title = field.text;
        const auto *placement = Placement(view);
        return title + "###" +
               (placement && placement->windowId == WorkspaceWindowId::Named &&
                        (placement->role.empty() || placement->role == view.role)
                    ? placement->windowName
                : placement && placement->windowId == WorkspaceWindowId::Role && !view.role.empty()
                    ? view.role
                    : "editor-view-" + std::to_string(view.instance));
    }();
}
bool EditorWorkspace::Focus(EditorView id) {
    const auto found = m_Views.find(id);
    if (found == m_Views.end()) return false;
    found->second.focus = true;
    m_LastFocused[Text(found->second.desc.id)] = id;
    m_LastFocusRequest = id;
    if (found->second.desc.kind == EDITOR_WINDOW_VIEW && !found->second.desc.documentBound)
        m_LastWindowFocus = id;
    return true;
}
EditorView EditorWorkspace::LastFocused(const std::string &type) const {
    const auto found = m_LastFocused.find(type);
    return found != m_LastFocused.end() && m_Views.count(found->second) ? found->second : 0;
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
    const auto values = BorrowEditorFields(result.fields);
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
std::vector<ObjectRef> EditorWorkspace::Pick(float x, float y, bool *handled, uint32_t objectClass) const {
    if (handled) *handled=false;
    if (!ImGui::GetCurrentContext() || !std::isfinite(x) || !std::isfinite(y))
        return {};
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
            if (view.desc.pick) {
                if (view.drawnFrame < ImGui::GetFrameCount() - 1)
                    return {};
                auto result =
                    m_Application.PickView(view.desc, view.data, m_Application.Scope(view.id), x, y, objectClass);
                return result.status == EDITOR_OK ? std::move(result.fields.front().objects) : std::vector<ObjectRef>{};
            }
            if(ImGui::GetFrameCount()-view.pickFrame>1)return {};
            for (const auto &region : view.picks)
                if (region.object.document == m_Application.Scope().document && x >= region.left && y >= region.top &&
                    x < region.right && y < region.bottom)
                    return {region.object};
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
    const bool session =
        (view.desc.singleInstance || !view.role.empty()) && !view.desc.documentBound && !view.unavailable;
    SavedView saved{0, {}, {}, m_Application.ToolChoice(id)};
    if (session) {
        if (view.desc.saveState) {
            const auto state = m_Application.SaveViewState(view.desc, view.data, m_Application.Scope(id));
            if (state.status != EDITOR_OK) { m_Error = "View state could not be saved"; return false; }
            saved.version = view.desc.stateVersion;
            saved.fields = state.fields;
        }
        const auto name = WindowName(id);
        if (auto *window = name.empty() || !ImGui::GetCurrentContext() ? nullptr : ImGui::FindWindowByName(name.c_str()))
            saved.placement = {window->Pos.x, window->Pos.y, window->Size.x, window->Size.y, window->DockId, true};
    }
    WorkspaceViewState persistent;
    const bool retain = !view.desc.documentBound && view.desc.kind == EDITOR_WINDOW_VIEW &&
                        (view.unavailable || view.desc.singleInstance || !view.role.empty());
    if (retain && !CaptureView(view, m_Configuration, persistent, m_Error))
        return false;
    if (!PrepareClose(view, m_Error))
        return false;
    if (session)
        m_ClosedStates[{Text(view.desc.id), view.desc.singleInstance ? std::string{} : view.role}] = std::move(saved);
    if (retain) {
        persistent.open = false;
        m_Saved[persistent.instance] = std::move(persistent);
    } else
        m_Saved.erase(view.instance);
    Destroy(id);
    return true;
}
bool EditorWorkspace::PrepareClose(const View &view, std::string &error) {
    const auto status = m_Application.CanCloseView(view.desc, view.data, view.id);
    if (status == EDITOR_OK || status == EDITOR_NO_CHANGE)
        return true;
    error = "View close preparation failed: " + Text(view.desc.id) + " (" + Text(view.desc.provider) + ")";
    return false;
}
void EditorWorkspace::Destroy(EditorView id) {
    auto found = m_Views.find(id);
    if (found == m_Views.end())
        return;
    const auto source = Text(found->second.desc.id) + " (" + Text(found->second.desc.provider) + ")";
    std::string error;
    if (!m_Application.ClosingView(found->second.desc, found->second.data, id))
        error = "View cleanup raised an exception: " + source;
    const auto name = WindowName(id);
    if (ImGui::GetCurrentContext()) {
        auto &context = *ImGui::GetCurrentContext();
        if (m_DragOwner == id && context.DragDropActive && context.DragDropPayload.SourceId == m_DragId)
            ImGui::ClearDragDrop();
        if (!name.empty())
            CloseWindowInput(ImGui::FindWindowByName(name.c_str()));
        for (const auto id : found->second.windows)
            if (auto *window = ImGui::FindWindowByID(id); window && window->LastFrameActive == found->second.drawnFrame)
                CloseWindowInput(window);
    }
    if (m_DragOwner == id) {
        m_DragOwner = 0;
        m_DragId = 0;
    }
    if (m_LastFocusRequest == id) m_LastFocusRequest = 0;
    if (m_LastWindowFocus == id)
        m_LastWindowFocus = 0;
    m_Application.CloseScope(id);
    if (!m_Application.DestroyView(found->second.desc, found->second.data)) {
        if (!error.empty()) error += "; ";
        error += "View destruction raised an exception: " + source;
    }
    if (LastFocused(Text(found->second.desc.id)) == id)
        m_LastFocused.erase(Text(found->second.desc.id));
    m_Views.erase(found);
    ++m_ViewsRevision;
    if (!error.empty()) {
        m_Error = std::move(error);
        if (m_Application.report) {
            // A diagnostic cannot interrupt the remaining committed releases.
            try { m_Application.report({EDITOR_ERROR, m_Error}); } catch (...) {}
        }
    }
}
void EditorWorkspace::CloseAll() {
    while (!m_Views.empty())
        Destroy(m_Views.begin()->first);
    m_ClosedStates.clear();
    m_Saved.clear();
    m_LastFocused.clear();
    m_LastWindowFocus = 0;
}
void EditorWorkspace::Draw(bool nativeAvailable) {
    UpdateRegionDocks();
    auto &context = *ImGui::GetCurrentContext();
    if (!context.DragDropActive || context.DragDropPayload.SourceId != m_DragId) {
        m_DragOwner = 0;
        m_DragId = 0;
    }
    const auto focus = m_LastFocusRequest;
    bool focusDrawn = false;
    std::vector<EditorView> closed;
    for (auto &v : m_Views) {
        auto &view = v.second;
        if ((!nativeAvailable || m_Detached) && view.desc.area == EDITOR_DOCUMENT) continue;
        const bool grantFocus = (view.focus || view.id == focus) && !ImGui::GetTopMostPopupModal();
        if (view.id == focus && grantFocus) focusDrawn = true;
        if (view.desc.inspect &&
            std::any_of(view.desc.fields, view.desc.fields + view.desc.fieldCount, [](const auto &field) {
                return Text(field.key) == "title" && field.type == EDITOR_TEXT_VALUE;
            }))
            Inspect(view.id);
        const auto name = WindowName(view.id);
        EditorViewFrame frame{EditorText(name), uint32_t(grantFocus), 0, uint32_t(view.placement.apply),
            view.placement.dock, view.placement.x, view.placement.y, view.placement.width, view.placement.height,
            uint32_t(!view.pinned), 0};
        frame.initialDock = ViewDock(view);
        // Picking can be requested by an editor drawn inside this same view.
        view.drawnFrame = ImGui::GetFrameCount();
        const auto firstWindow = ImGui::GetCurrentContext()->WindowsActiveCount;
        const auto sourceFrame = context.DragDropSourceFrameCount;
        try {
            Drawing drawing(m_Application);
            view.desc.draw(view.data, m_Application.Host(), view.id, &frame);
        } catch (...) {
            view.drawnFrame = -1;
            m_Error = "View raised an exception";
        }
        if (context.DragDropActive && context.DragDropSourceFrameCount != sourceFrame &&
            context.DragDropSourceFrameCount == context.FrameCount) {
            // Retain ownership even if focus later clears the active widget.
            m_DragOwner = view.id;
            m_DragId = context.DragDropPayload.SourceId;
        }
        view.windows.clear();
        for (const auto *window : ImGui::GetCurrentContext()->Windows)
            if (window->LastFrameActive == ImGui::GetFrameCount() && window->BeginOrderWithinContext >= firstWindow &&
                !(window->Flags & (ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_ChildMenu)) && !window->DockNodeAsHost &&
                (!(window->Flags & ImGuiWindowFlags_ChildWindow) || window->DockIsActive))
                view.windows.push_back(window->ID);
        if (Focused() == view.id) {
            m_LastFocused[Text(view.desc.id)] = view.id;
            if (view.desc.kind == EDITOR_WINDOW_VIEW && !view.desc.documentBound)
                m_LastWindowFocus = view.id;
        }
        if (grantFocus) view.focus = false;
        const auto *window = name.empty() ? nullptr : ImGui::FindWindowByName(name.c_str());
        if (Focused() == view.id && m_Application.ToolCaptured(view.id) && !ImGui::GetIO().WantTextInput &&
            ImGui::IsKeyPressed(ImGuiKey_Escape, ImGuiInputFlags_None, ImGuiKeyOwner_NoOwner))
            m_Application.CancelTool(view.id);
        view.active =
            frame.active != 0 || m_Application.ToolCaptured(view.id) ||
            (view.desc.documentBound && OwnedPopup(window, true) < ImGui::GetCurrentContext()->OpenPopupStack.Size);
        view.placement.apply = false;
        if (frame.close)
            closed.push_back(view.id);
    }
    for (auto id : closed)
        Close(id);
    // Each request also selects that view's dock tab. Drawing order must not
    // replace the final focus request with the highest-numbered instance.
    if (focusDrawn && !ImGui::GetTopMostPopupModal()) {
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
    return Configure(configuration, nullptr, error);
}
bool EditorWorkspace::Configure(const EditorConfiguration &configuration, const WorkspaceConfiguration *workspace, std::string &error) {
    const auto previous = m_Application.Configuration();
    if (!m_Application.Configure(configuration, error)) return false;
    struct Replacement { EditorView old; View next; };
    std::vector<Replacement> replacements;
    std::vector<std::pair<EditorView, const WorkspaceViewPlacement *>> assigned;
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
            if (Text(desc.id) == saved.first.first && saved.second.version &&
                (saved.second.version != desc.stateVersion || !desc.restoreState))
                return rollback();
    for (const auto &entry : m_Views) {
        for (const auto &description : m_Application.Views()) {
            if (Text(description.id) != Text(entry.second.desc.id) ||
                (Text(description.provider) == Text(entry.second.desc.provider) && !entry.second.unavailable))
                continue;
            const auto &old = entry.second;
            if (old.desc.documentBound != description.documentBound)
                return rollback();
            if ((old.desc.documentBound || old.desc.closing || old.desc.canClose) && ImGui::GetCurrentContext())
                for (const auto id : old.windows)
                    if (const auto *window = ImGui::FindWindowByID(id);
                        window && window->LastFrameActive == old.drawnFrame &&
                        OwnedPopup(window) < ImGui::GetCurrentContext()->OpenPopupStack.Size)
                        return rollback();
            const auto oldName = WindowName(old.id);
            if ((old.desc.documentBound || old.desc.closing || old.desc.canClose) &&
                (old.active || (ImGui::GetCurrentContext() && !oldName.empty() &&
                                OwnedPopup(ImGui::FindWindowByName(oldName.c_str())) <
                                    ImGui::GetCurrentContext()->OpenPopupStack.Size)))
                return rollback();
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
            replacement.instance = old.instance;
            replacement.role = old.role;
            replacement.pinned = old.pinned;
            const auto name = WindowName(entry.first);
            if (auto *window = name.empty() || !ImGui::GetCurrentContext() ? nullptr : ImGui::FindWindowByName(name.c_str()))
                replacement.placement = {window->Pos.x, window->Pos.y, window->Size.x, window->Size.y, window->DockId, true};
            replacement.focus = replacement.focus || Focused() == entry.first;
            replacements.push_back({entry.first, std::move(replacement)});
            if (state.status == EDITOR_OK &&
                (old.unavailable
                     ? m_Application.RestoreViewSettings(description, data, m_Application.Scope(id), state.fields)
                     : m_Application.RestoreViewState(description, data, m_Application.Scope(id), state.fields)) !=
                    EDITOR_OK)
                return rollback();
            m_Application.RestoreTool(id, m_Application.ToolChoice(old.id));
        }
    }
    if (workspace) {
        for (const auto &placement : workspace->views) {
            if (!placement.startup || (!placement.role.empty() && Role(placement.role))) continue;
            const auto descriptions = m_Application.Views();
            const auto desc = std::find_if(descriptions.begin(), descriptions.end(), [&](const auto &item) { return Text(item.id) == placement.type; });
            if (desc == descriptions.end()) return rollback();
            if (desc->area == EDITOR_DOCUMENT && !m_Application.NativeAvailable()) return rollback();
            if (desc->singleInstance) {
                const auto existing = std::find_if(m_Views.begin(), m_Views.end(), [&](const auto &view) { return Text(view.second.desc.id) == placement.type; });
                if (existing != m_Views.end()) { assigned.push_back({existing->first, &placement}); continue; }
            }
            const auto id = m_Application.OpenScope();
            void *data = m_Application.CreateView(*desc, id);
            if (!data) { m_Application.CloseScope(id); return rollback(); }
            View next{id, *desc, data, true, {}};
            next.instance = m_NextInstance = std::max(m_NextInstance, id);
            ++m_NextInstance;
            next.role = placement.role; next.pinned = placement.pinned;
            replacements.push_back({0, std::move(next)});
            const auto saved =
                m_ClosedStates.find({placement.type, desc->singleInstance ? std::string{} : placement.role});
            if (saved != m_ClosedStates.end()) {
                replacements.back().next.placement = saved->second.placement;
                if (!saved->second.tool.empty()) m_Application.RestoreTool(id, saved->second.tool);
                if (saved->second.version && m_Application.RestoreViewState(*desc, data, m_Application.Scope(id), saved->second.fields) != EDITOR_OK) return rollback();
            }
        }
    }
    for (const auto &replacement : replacements)
        if (replacement.old && !PrepareClose(m_Views.at(replacement.old), error)) {
            const auto message = error;
            rollback();
            error = message;
            return false;
        }
    if (m_WriteConfiguration) {
        std::string message;
        bool saved = false;
        auto candidate = m_Views;
        for (const auto &replacement : replacements) {
            candidate.erase(replacement.old);
            candidate.emplace(replacement.next.id, replacement.next);
        }
        for (const auto &assignment : assigned) {
            auto it = candidate.find(assignment.first);
            if (it != candidate.end()) {
                it->second.role = assignment.second->role;
                it->second.pinned = assignment.second->pinned;
            }
        }
        WorkspaceState snapshot;
        try {
            saved = Capture(candidate, workspace ? *workspace : m_Configuration, snapshot, message) &&
                    m_WriteConfiguration(configuration, snapshot, message);
        } catch (const std::exception &failure) {
            message = failure.what();
        } catch (...) {
            message = "Configuration writer raised an exception";
        }
        if (!saved) {
            rollback();
            error = message.empty() ? "Configuration could not be saved" : message;
            return false;
        }
    }
    const auto pendingFocus = m_LastFocusRequest, focused = Focused();
    EditorView replacementFocus = 0;
    for (auto &replacement : replacements) {
        if (replacement.old == pendingFocus || (!pendingFocus && replacement.old == focused))
            replacementFocus = replacement.next.id;
        const bool lastFocused = replacement.old && LastFocused(Text(replacement.next.desc.id)) == replacement.old;
        const bool lastWindowFocus = replacement.old && m_LastWindowFocus == replacement.old;
        if (replacement.old) Destroy(replacement.old);
        if (lastWindowFocus)
            m_LastWindowFocus = replacement.next.id;
        if (lastFocused)
            m_LastFocused[Text(replacement.next.desc.id)] = replacement.next.id;
        m_Views.emplace(replacement.next.id, std::move(replacement.next));
        ++m_ViewsRevision;
        for (auto &assignment : assigned) if (assignment.first == replacement.old) assignment.first = replacement.next.id;
    }
    for (const auto &assignment : assigned) {
        auto &view = m_Views.at(assignment.first);
        view.role = assignment.second->role; view.pinned = assignment.second->pinned;
        ++m_ViewsRevision;
    }
    if (replacementFocus) m_LastFocusRequest = replacementFocus;
    return true;
}
WorkspaceConfiguration EditorWorkspace::Configuration() const {
    auto result = m_Configuration;
    result.contributions = m_Application.Configuration();
    return result;
}
bool EditorWorkspace::Configure(const WorkspaceConfiguration &configuration, std::string &error) {
    if (!m_Application.NativeAvailable()) {
        error = "Finish the current operation before applying a workspace layout";
        return false;
    }
    if (ImGui::GetCurrentContext() && ImGui::GetTopMostPopupModal()) {
        error = "Finish the active dialog before applying a workspace layout";
        return false;
    }
    if (!Validate(configuration, false, error))
        return false;
    auto next = configuration;
    next.contributions = {};
    if (!Configure(configuration.contributions, &next, error))
        return false;
    for (auto &entry : m_Views) {
        auto &view = entry.second;
        const auto placement = std::find_if(next.views.begin(), next.views.end(), [&](const auto &item) {
            return item.role == view.role && item.type == Text(view.desc.id);
        });
        if (placement != next.views.end())
            view.pinned = placement->pinned;
    }
    m_Configuration = std::move(next);
    m_RegionDocks.clear();
    m_LayoutChanged = m_LayoutPrepared;
    for (auto &entry : m_Views)
        entry.second.placement.apply = false;
    ++m_ViewsRevision;
    return true;
}
bool EditorWorkspace::Validate(const WorkspaceConfiguration &configuration, bool restoring, std::string &error) const {
    // Validate and copy before selecting providers or creating any instances.
    const auto &next = configuration;
    std::set<std::string> nodes{"root"}, leaves{"root"}, regions, roles, defaultTypes, startupSingletons;
    const auto descriptions = m_Application.Views(true);
    auto invalid = [&] { error = "Invalid workspace layout or view placement"; return false; };
    for (const auto &split : next.splits) {
        if (!leaves.erase(split.source) || split.first.empty() || split.remainder.empty() ||
            !nodes.insert(split.first).second || !nodes.insert(split.remainder).second ||
            !std::isfinite(split.fraction) || split.fraction <= 0 || split.fraction >= 1 ||
            split.direction < WorkspaceDirection::Left || split.direction > WorkspaceDirection::Down) return invalid();
        leaves.insert(split.first); leaves.insert(split.remainder);
    }
    for (const auto &region : next.regions)
        if (!leaves.count(region.id) || !regions.insert(region.id).second ||
            (!region.anchorRole.empty() && !region.anchorWindow.empty())) return invalid();
    std::set<std::string> namedWindows;
    for (const auto &view : next.views) {
        if (view.windowId == WorkspaceWindowId::Named &&
            (view.windowName.empty() || view.windowName.find('\0') != std::string::npos ||
             !namedWindows.insert(view.windowName).second ||
             (view.role.empty() && std::any_of(descriptions.begin(), descriptions.end(), [&](const auto &desc) {
                  return Text(desc.id) == view.type && !desc.singleInstance;
              }))))
            return invalid();
        if (view.windowId != WorkspaceWindowId::Named && !view.windowName.empty())
            return invalid();
        if (view.windowId < WorkspaceWindowId::Instance || view.windowId > WorkspaceWindowId::Named)
            return invalid();
        if ((view.role.empty() && !defaultTypes.insert(view.type).second) ||
            (!view.region.empty() && !regions.count(view.region)) ||
            (!view.role.empty() && !roles.insert(view.role).second) ||
            (!restoring && std::none_of(descriptions.begin(), descriptions.end(),
                                        [&](const auto &desc) { return Text(desc.id) == view.type; })))
            return invalid();
        if (auto id = Role(view.role);
            !restoring && !view.role.empty() && id && Text(m_Views.at(id).desc.id) != view.type)
            return invalid();
        if (view.startup)
            for (const auto &desc : descriptions)
                if (Text(desc.id) == view.type && desc.singleInstance && !startupSingletons.insert(view.type).second) {
                    // Multiple providers of the same singleton share one declaration.
                    if (std::count_if(next.views.begin(), next.views.end(), [&](const auto &item) { return item.startup && item.type == view.type; }) > 1) return invalid();
                }
    }
    for (const auto &region : next.regions) if (!region.anchorRole.empty() && !roles.count(region.anchorRole)) return invalid();
    std::set<std::string> legacyNames;
    for (const auto &window : next.legacyWindows)
        if (window.name.empty() || !regions.count(window.region) || !legacyNames.insert(window.name).second) return invalid();
    return true;
}
EditorView EditorWorkspace::OpenRole(const std::string &role) {
    for (const auto &placement : m_Configuration.views)
        if (placement.role == role && !role.empty()) return Open(placement.type, role, placement.pinned);
    return 0;
}
unsigned EditorWorkspace::RegionDock(const std::string &region) const {
    const auto found = m_RegionDocks.find(region);
    if (found != m_RegionDocks.end()) return found->second;
    if (!ImGui::GetCurrentContext()) return 0;
    for (const auto &entry : m_Configuration.regions) if (entry.id == region) {
        const auto name = entry.anchorRole.empty() ? entry.anchorWindow : WindowName(Role(entry.anchorRole));
        const auto *window = name.empty() ? nullptr : ImGui::FindWindowByName(name.c_str());
        return window ? window->DockId : 0;
    }
    return 0;
}
const WorkspaceViewPlacement *EditorWorkspace::Placement(const View &view) const {
    const WorkspaceViewPlacement *first = nullptr, *defaultPlacement = nullptr;
    for (const auto &placement : m_Configuration.views) {
        if (placement.type.size() != view.desc.id.size ||
            placement.type.compare(0, placement.type.size(), view.desc.id.data, view.desc.id.size)) continue;
        if (!view.role.empty() && placement.role == view.role) return &placement;
        if (!first) first = &placement;
        if (placement.role.empty()) defaultPlacement = &placement;
    }
    return defaultPlacement ? defaultPlacement : first;
}
unsigned EditorWorkspace::ViewDock(const View &view) const {
    const auto *placement = Placement(view);
    return placement ? RegionDock(placement->region) : 0;
}
void EditorWorkspace::UpdateRegionDocks() {
    for (const auto &region : m_Configuration.regions) {
        const auto name = region.anchorRole.empty() ? region.anchorWindow : WindowName(Role(region.anchorRole));
        const auto *window = name.empty() ? nullptr : ImGui::FindWindowByName(name.c_str());
        if (window && window->DockId) m_RegionDocks[region.id] = window->DockId;
    }
}
std::vector<unsigned> EditorWorkspace::BuildLayout(unsigned dockspace, bool closeSeparated) {
    struct Window { std::string name, region; };
    std::vector<Window> redocked;
    std::vector<unsigned> separated;
    for (const auto *window : ImGui::GetCurrentContext()->Windows) {
        if (!window->WasActive || window->ParentWindow ||
            (window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoDocking))) continue;
        const std::string name = window->Name;
        for (const auto &rule : m_Configuration.legacyWindows)
            if ((!rule.contains && name == rule.name) || (rule.contains && name.find(rule.name) != std::string::npos)) {
                if (closeSeparated && !rule.keepOnReset && !window->DockIsActive) separated.push_back(window->ID);
                else redocked.push_back({name, rule.region});
                break;
            }
    }
    if (closeSeparated) CloseFloatingViews();
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);
    std::map<std::string, unsigned> nodes{{"root", dockspace}};
    for (const auto &split : m_Configuration.splits) {
        const ImGuiDir directions[] = {ImGuiDir_Left, ImGuiDir_Right, ImGuiDir_Up, ImGuiDir_Down};
        unsigned remainder = 0;
        nodes[split.first] = ImGui::DockBuilderSplitNode(nodes.at(split.source), directions[int(split.direction)], split.fraction, nullptr, &remainder);
        nodes[split.remainder] = remainder;
    }
    for (const auto &region : m_Configuration.regions) m_RegionDocks[region.id] = nodes.at(region.id);
    for (const auto &rule : m_Configuration.legacyWindows)
        if (!rule.contains) ImGui::DockBuilderDockWindow(rule.name.c_str(), RegionDock(rule.region));
    DockViews();
    for (const auto &window : redocked) ImGui::DockBuilderDockWindow(window.name.c_str(), RegionDock(window.region));
    ImGui::DockBuilderFinish(dockspace);
    return separated;
}
EditorWorkspace::LayoutUpdate EditorWorkspace::UpdateLayout(unsigned dockspace, bool reset, bool closeSeparated) {
    if (dockspace != m_Dockspace) { m_Dockspace = dockspace; m_LayoutPrepared = false; }
    LayoutUpdate result;
    if (m_LayoutPrepared && !reset && !m_LayoutChanged) return result;
    if (!reset && !m_LayoutChanged) {
        const auto focused = m_Views.find(m_LastWindowFocus);
        if (focused != m_Views.end() && m_Saved.count(focused->second.instance))
            result.focus = focused->first;
    }
    auto *node = ImGui::DockBuilderGetNode(dockspace);
    if (reset || m_LayoutChanged || !node || node->IsLeafNode()) result.closeLegacy = BuildLayout(dockspace, closeSeparated);
    else UpdateRegionDocks();
    m_LayoutPrepared = true;
    m_LayoutChanged = false;
    result.changed = true;
    return result;
}
void EditorWorkspace::Menu() {
    if (!ImGui::BeginMenu("Views"))
        return;
    for (auto &d : m_Application.Views())
        if (d.kind == EDITOR_WINDOW_VIEW && ImGui::MenuItem(Text(d.title).c_str())) {
            const EditorValue type{EDITOR_TEXT("type"), EDITOR_TEXT_VALUE, {}, d.id};
            m_Application.Execute("workspace.open-view", {m_Application.Scope(), &type, 1});
        }
    if (ImGui::BeginMenu("Saved Views")) {
        const EditorClient client(m_Application.Host(), 0);
        for (const auto &entry : m_Saved) {
            if (entry.second.open || std::any_of(m_Views.begin(), m_Views.end(),
                                                 [&](const auto &v) { return v.second.instance == entry.first; }))
                continue;
            const auto label = (entry.second.title.empty() ? entry.second.type : entry.second.title) +
                               "###saved-view-" + std::to_string(entry.first);
            if (ImGui::MenuItem(label.c_str()))
                client.Command("workspace.reopen-instance", {IntegerArgument("instance", entry.first)});
        }
        ImGui::EndMenu();
    }
    if (m_RestoreFailed && ImGui::MenuItem("Discard Unrestored Workspace"))
        EditorClient(m_Application.Host(), 0).Command("workspace.discard-saved-state");
    if (ImGui::BeginMenu("Modules (Next Start)")) {
        const EditorClient client(m_Application.Host(), 0);
        const auto current = client.Query("profile.modules");
        if (const auto *rows = EditorClient::Find(current, "modules")) for (const auto &row : rows->fields) {
            const EditorResponse value{EDITOR_OK, {}, row.fields};
            const auto id = EditorClient::Text(value, "id");
            const bool enabled = EditorClient::Integer(value, "enabled") != 0;
            if (ImGui::MenuItem(id.c_str(), nullptr, enabled, EditorClient::Integer(value, "editable") != 0)) {
                EditorField module{"module", EDITOR_RECORD};
                module.fields = {{"id", EDITOR_TEXT_VALUE, {}, id},
                    {"enabled", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(!enabled)},
                    {"path", EDITOR_TEXT_VALUE, {}, EditorClient::Text(value, "path")},
                    {"builtin", EDITOR_INTEGER_VALUE, {}, {}, EditorClient::Integer(value, "builtin")}};
                const std::vector<EditorField> owned{std::move(module)};
                const auto args = BorrowEditorFields(owned);
                const std::vector<EditorValue> fields(args.values.begin(), args.values.end());
                client.Command("profile.set-module", fields);
            }
        }
        ImGui::EndMenu();
    }
    const auto editorMenu = [&](const char *title, bool text) {
        if (!ImGui::BeginMenu(title)) return;
        for (const auto &editor : m_Application.ValueEditors()) {
            if ((editor.type == EDITOR_TEXT_VALUE) != text) continue;
            const auto id = Text(editor.id), provider = Text(editor.provider);
            EditorValueEditorDesc selected{};
            const bool checked = m_Application.ValueEditor(id, selected) && Text(selected.provider) == provider;
            if (ImGui::MenuItem(Text(editor.title).c_str(), nullptr, checked)) {
                auto config = m_Application.Configuration();
                config.providers.erase(std::remove_if(config.providers.begin(), config.providers.end(),
                    [&](const ProviderChoice &choice) { return choice.kind == "editor" && choice.id == id; }), config.providers.end());
                config.providers.push_back({"editor", id, provider});
                Configure(config, m_Error);
            }
        }
        ImGui::EndMenu();
    };
    editorMenu("Text Editors", true);
    editorMenu("Value Editors", false);
    ToolMenus();
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
void EditorWorkspace::DetachDocument(bool replaceContext) {
    m_Application.NativeAvailable(true);
    if (!m_Application.NativeAvailable() || m_Detached || Interacting())
        throw std::runtime_error("Finish the current document interaction");
    std::string error;
    for (const auto &entry : m_Views) {
        const auto &view = entry.second;
        if (view.desc.area == EDITOR_DOCUMENT &&
            (view.desc.documentBound || view.desc.kind == EDITOR_OVERLAY_VIEW) && !PrepareClose(view, error))
            throw std::runtime_error(error);
    }
    m_DetachedDocument = m_Application.Scope().document;
    m_ReplacingContext = replaceContext;
    m_Detached = true;
    const EditorDocumentChange change{sizeof(change), EDITOR_DOCUMENT_DETACH, m_DetachedDocument,
                                      m_Application.Revision(), uint32_t(replaceContext)};
    if (!m_Application.ChangeDocument(change, error)) throw std::runtime_error(error);
}
void EditorWorkspace::RestoreDocument() {
    m_Application.NativeAvailable(true);
    if (!m_Application.NativeAvailable()) throw std::runtime_error("The native runtime is unavailable");
    if (!m_Detached || m_Application.Scope().document != m_DetachedDocument) return;
    const EditorDocumentChange change{sizeof(change), EDITOR_DOCUMENT_RESTORE, m_DetachedDocument,
                                      m_Application.Revision(), uint32_t(m_ReplacingContext)};
    std::string error;
    if (!m_Application.ChangeDocument(change, error)) throw std::runtime_error(error);
    m_Detached = false;
    m_DetachedDocument = {};
}
void EditorWorkspace::DocumentReplaced(DocumentToken document, uint64_t revision, bool open) {
    // Retain every old instance through the native commit. A rejected dispatch
    // restores the original windows, including providers with no saved state.
    // Once committed, old document-bound windows cannot be used in the new one.
    m_Application.Attach(document, revision, true, open);
    DocumentChanging();
    m_Detached = false;
    m_DetachedDocument = {};
    const EditorDocumentChange change{sizeof(change), EDITOR_DOCUMENT_REPLACED, document,
                                      revision, uint32_t(m_ReplacingContext)};
    std::string error;
    if (!m_Application.ChangeDocument(change, error)) throw std::runtime_error(error);
}
void EditorWorkspace::DocumentChanging() {
    std::vector<EditorView> close;
    for (const auto &item : m_Views)
        if (item.second.desc.area == EDITOR_DOCUMENT &&
            (item.second.desc.kind == EDITOR_OVERLAY_VIEW || item.second.desc.documentBound))
            close.push_back(item.first);
    for (auto id : close) {
        const auto found = m_Views.find(id);
        if (found != m_Views.end())
            m_ClosedStates.erase(
                {Text(found->second.desc.id), found->second.desc.singleInstance ? std::string{} : found->second.role});
        Destroy(id);
    }
}
} // namespace vdev
