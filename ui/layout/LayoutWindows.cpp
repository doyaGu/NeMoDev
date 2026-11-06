#include "ui/layout/LayoutWindows.h"
#include "editor/ImGuiDrawing.h"
#include "imgui_internal.h"

namespace vdev {
LayoutWindows::LayoutWindows(CkRuntimeHost &runtime, ParameterCopies &copies, EditorApplication &editor,
                             EditorWorkspace &workspace)
    : m_Runtime(runtime), m_Copies(copies), m_Editor(editor), m_Workspace(workspace),
      m_Primary(runtime, copies, editor) {
    m_Primary.m_Windows = this;
}
bool LayoutWindows::IsPrimary(EditorView id) const {
    return Find(id) == &Primary();
}
LayoutWindow *LayoutWindows::Find(EditorView id) const {
    const auto found = m_Instances.find(id);
    return found == m_Instances.end() ? nullptr : found->second.window;
}
LayoutWindow &LayoutWindows::Primary() {
    if (auto *window = Find(m_Workspace.Role("main.layout")))
        return *window;
    if (!m_Primary.m_Window && !m_Instances.empty())
        return *m_Instances.begin()->second.window;
    return m_Primary;
}
const LayoutWindow &LayoutWindows::Primary() const {
    return const_cast<LayoutWindows *>(this)->Primary();
}
LayoutWindow &LayoutWindows::Active() {
    if (auto *window = Find(m_Workspace.LastFocused("layout.viewport")))
        return *window;
    return Primary();
}
const LayoutWindow &LayoutWindows::Active() const {
    return const_cast<LayoutWindows *>(this)->Active();
}
LayoutTarget LayoutWindows::Target(EditorView source) {
    const auto id = m_Workspace.ActionView("layout.viewport", source);
    auto *window = Find(id);
    return window ? LayoutTarget{id, window->m_View.get(), window} : LayoutTarget{};
}
void *LayoutWindows::Create(EditorView id) {
    Entry entry;
    if (!m_Primary.m_Window)
        entry.window = &m_Primary;
    else {
        entry.owned = std::make_unique<LayoutWindow>(m_Runtime, m_Copies, m_Editor);
        entry.window = entry.owned.get();
    }
    auto &window = *entry.window;
    window.m_Windows = this;
    window.m_MainWindow = m_MainWindow;
    window.createObject = [this, id](LayoutObject kind) {
        if (createObject)
            createObject(id, kind);
    };
    auto state = std::make_unique<LayoutWindow::WindowState>(window, id);
    m_Instances.emplace(id, std::move(entry));
    window.m_Window = state.release();
    return window.m_Window;
}
void LayoutWindows::Destroy(void *data) {
    auto *state = static_cast<LayoutWindow::WindowState *>(data);
    auto &window = *state->owner;
    if (m_InputOwner == &window) {
        window.ReleaseInputViewport();
        m_InputOwner = nullptr;
    }
    const auto id = state->id;
    window.m_Window = nullptr;
    window.m_ViewFrame = -1;
    delete state;
    m_Instances.erase(id);
}
bool LayoutWindows::Start(SDL_Window *main, std::string *error) {
    m_MainWindow = main;
    m_Primary.m_MainWindow = main;
    if (m_Instances.empty()) return true;
    bool result = true;
    ForEach([&](LayoutTarget target) {
        auto &window = *static_cast<LayoutWindow *>(target.viewport);
        window.m_MainRenderContext = IsPrimary(target.id) || !target.id;
        std::string message;
        if (!window.Start(main, &message)) {
            result = false;
            if (error)
                *error += message;
        }
    });
    return result;
}
void LayoutWindows::Stop() {
    ForEach([&](LayoutTarget target) { static_cast<LayoutWindow *>(target.viewport)->Stop(); });
    m_InputOwner = nullptr;
}
void LayoutWindows::DocumentClosed() {
    ForEach([](LayoutTarget target) { static_cast<LayoutWindow *>(target.viewport)->DocumentClosed(); });
}
void LayoutWindows::CancelEdits() {
    ForEach([](LayoutTarget target) { static_cast<LayoutWindow *>(target.viewport)->CancelEdits(); });
}
bool LayoutWindows::HandleEvent(const SDL_Event &event) {
    bool handled = false;
    ForEach([&](LayoutTarget target) {
        handled = static_cast<LayoutWindow *>(target.viewport)->HandleEvent(event) || handled;
    });
    return handled;
}
bool LayoutWindows::Busy() const {
    for (const auto &entry : m_Instances)
        if (entry.second.window->Busy())
            return true;
    return false;
}
bool LayoutWindows::FullScreen() const {
    for (const auto &entry : m_Instances)
        if (entry.second.window->FullScreen())
            return true;
    return false;
}
void LayoutWindows::PrepareFullScreen(LayoutWindow &window) {
    if (m_InputOwner != &window && m_InputOwner)
        m_InputOwner->ReleaseInputViewport();
    m_InputOwner = &window;
}
void LayoutWindows::UpdateInput() {
    LayoutWindow *next = nullptr;
    if (auto *focused = Find(m_Workspace.Focused()))
        next = focused;
    else
        next = &Active();
    for (const auto &entry : m_Instances)
        if (entry.second.window->FullScreen())
            next = entry.second.window;
    if (!next->m_Started || (!next->FullScreen() && next->m_ViewFrame != ImGui::GetFrameCount()))
        next = nullptr;
    if (m_InputOwner != next && m_InputOwner)
        m_InputOwner->ReleaseInputViewport();
    m_InputOwner = next;
    if (next)
        next->ApplyInputViewport();
}
bool LayoutWindows::Register(std::string &error, SDL_Window *mainWindow) {
    m_MainWindow = mainWindow;
    m_Primary.m_MainWindow = mainWindow;
    using WindowState = LayoutWindow::WindowState;
    EditorViewDesc view{};
    view.size = sizeof(view);
    view.imguiVersion = EDITOR_IMGUI_VERSION;
    view.id = EDITOR_TEXT("layout.viewport");
    view.provider = EDITOR_TEXT("builtin.layout-window");
    view.title = EDITOR_TEXT("3D Layout");
    view.singleInstance = 0;
    view.defaultTool = EDITOR_TEXT("layout.select");
    view.area = EDITOR_DOCUMENT;
    view.data = this;
    view.create = [](void *data, const EditorHost *, EditorView id) -> void * {
        return static_cast<LayoutWindows *>(data)->Create(id);
    };
    view.closing = [](void *data, const EditorHost *, EditorView) {
        static_cast<WindowState *>(data)->owner->Closing();
    };
    view.destroy = [](void *data) {
        auto *state = static_cast<WindowState *>(data);
        state->owner->m_Windows->Destroy(state);
    };
    view.draw = [](void *data, const EditorHost *, EditorView, EditorViewFrame *frame) {
        auto &owner = *static_cast<WindowState *>(data)->owner;
        if (owner.Window().needsStart && owner.m_MainWindow) {
            std::string error;
            owner.m_MainRenderContext = owner.m_Windows->IsPrimary(owner.Window().id);
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
    static const EditorParameter settings[] = {{EDITOR_TEXT("tool"), EDITOR_INTEGER_VALUE},
                                               {EDITOR_TEXT("scale-mode"), EDITOR_INTEGER_VALUE},
                                               {EDITOR_TEXT("portal-mode"), EDITOR_INTEGER_VALUE},
                                               {EDITOR_TEXT("zoom-mode"), EDITOR_INTEGER_VALUE},
                                               {EDITOR_TEXT("orbit-mode"), EDITOR_INTEGER_VALUE},
                                               {EDITOR_TEXT("explorer-shown"), EDITOR_INTEGER_VALUE},
                                               {EDITOR_TEXT("snapshot-open"), EDITOR_INTEGER_VALUE},
                                               {EDITOR_TEXT("explorer-filter"), EDITOR_TEXT_VALUE},
                                               {EDITOR_TEXT("scroll"), EDITOR_REAL_LIST},
                                               {EDITOR_TEXT("zoom-extent"), EDITOR_REAL_VALUE},
                                               {EDITOR_TEXT("transforms"), EDITOR_INTEGER_LIST}};
    view.settings = settings;
    view.settingCount = uint32_t(std::size(settings));
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
    module.data = this;
    module.stop = [](void *data, const EditorHost *) { static_cast<LayoutWindows *>(data)->Stop(); };
    module.document = [](void *data, const EditorHost *, const EditorDocumentChange *change) {
        return static_cast<LayoutWindows *>(data)->DocumentChange(*change);
    };
    module.views = &view;
    module.viewCount = 1;
    return RegisterControls(module, error);
}
} // namespace vdev
