#include "ui/schematic/SchematicViews.h"
#include "ui/schematic/SchematicCanvas.h"
#include "editor/ImGuiDrawing.h"
#include <iterator>
#include <stdexcept>

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
    {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("zoom"), EDITOR_REAL_VALUE},
    {EDITOR_TEXT("selection-count"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("selection-target"), EDITOR_OBJECT},
    {EDITOR_TEXT("action-text"), EDITOR_TEXT_VALUE}};
}
struct SchematicViews::Data {
    SchematicViews &owner;
    EditorWorkspace &workspace;
    const SchematicStyle &style;
    ViewOptions defaults;
    std::multimap<EditorView, SchematicCanvas *> canvases;
    mutable EditorView current = 0;
    struct Navigation { ElementAddress address; ObjectRef focus; EditorView view; };
    std::vector<Navigation> requested;
    SchematicCanvas *Canvas(EditorView id) const {
        auto &views = owner;
        return views.Find(views.Find(id) ? id : views.CurrentId());
    }
};
SchematicViews::SchematicViews(EditorWorkspace &workspace, const SchematicStyle &style)
    : m_Data(new Data{*this, workspace, style, {}, {}}) {}
SchematicViews::~SchematicViews() = default;
ViewOptions &SchematicViews::Defaults() { return m_Data->defaults; }
SchematicCanvas *SchematicViews::Find(EditorView id) const {
    const auto range = m_Data->canvases.equal_range(id);
    return range.first == range.second ? nullptr : std::prev(range.second)->second;
}
EditorView SchematicViews::CurrentId() const {
    auto &data = *m_Data;
    {
        auto *workspace = &data.workspace;
        const auto focused = workspace->Focused();
        if (Find(focused)) data.current = focused;
        if (!Find(data.current)) data.current = workspace->Role("main.schematic");
    }
    if (!Find(data.current))
        data.current = data.canvases.empty() ? 0 : data.canvases.begin()->first;
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
std::string SchematicViews::Window(EditorView id) const {
    return m_Data->workspace.WindowName(id ? id : CurrentId());
}
void SchematicViews::Focus(EditorView id) { m_Data->workspace.Focus(id ? id : CurrentId()); }
void SchematicViews::ApplyOptions(const ViewOptions &options) {
    m_Data->defaults = options;
    for (auto &entry : m_Data->canvases) entry.second->Navigation().options = options;
    RefreshStacks();
}
void SchematicViews::Validate() { for (auto &entry : m_Data->canvases) entry.second->ValidateNavigation(); }
void SchematicViews::CancelInteraction() { for (auto &entry : m_Data->canvases) entry.second->CancelInteraction(); }
void SchematicViews::DocumentClosed() {
    m_Data->requested.clear();
    for (auto &entry : m_Data->canvases) entry.second->DocumentClosed();
}
void SchematicViews::RequestShow(const ElementAddress &address, ObjectRef focus, EditorView view) {
    m_Data->requested.push_back({address, focus, view});
}
void SchematicViews::ShowRequested() {
    auto requested = std::move(m_Data->requested);
    m_Data->requested.clear();
    for (const auto &navigation : requested) Show(navigation.address, navigation.focus, true, navigation.view);
}
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
    auto &workspace = m_Data->workspace;
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
    auto &editor = workspace.Application();
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
        auto canvas = std::make_unique<SchematicCanvas>(module.workspace, module.style, module.owner, id);
        canvas->Navigation().options = module.defaults;
        canvas->RefreshStack();
        module.canvases.emplace(id, canvas.get());
        return canvas.release();
    };
    view.destroy = [](void *data) {
        auto *canvas = static_cast<SchematicCanvas *>(data);
        auto &module = *canvas->m_Canvases.m_Data;
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
    static const EditorParameter settings[] = {{EDITOR_TEXT("options"), EDITOR_INTEGER_VALUE},
                                               {EDITOR_TEXT("zoom"), EDITOR_REAL_VALUE},
                                               {EDITOR_TEXT("pan-x"), EDITOR_REAL_VALUE},
                                               {EDITOR_TEXT("pan-y"), EDITOR_REAL_VALUE}};
    view.settings = settings;
    view.settingCount = uint32_t(std::size(settings));
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
            const auto &views = static_cast<Data *>(data)->owner;
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
            if (!module.workspace.Application().NativeAvailable()) return EDITOR_REFUSED;
            return module.workspace.Application().Scope().document.value != 0 && module.Canvas(request->scope.view) ? EDITOR_OK : EDITOR_REFUSED;
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
        const auto info = EditorClient(module.workspace.Application().Host(), request->scope.view).Query(
            "object.details", {ObjectArgument("target", target)});
        if (info.status != EDITOR_OK || !EditorClient::Integer(info, "is-parameter")) return EDITOR_INVALID;
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

} // namespace vdev
