#include "editor/ImGuiDrawing.h"
#include "imgui_internal.h"
#include "workspace/EditorClient.h"
#include "workspace/EditorWorkspace.h"
#include <algorithm>
#include <charconv>
#include <set>
#include <sstream>

namespace vdev {
namespace {
std::string Text(EditorString s) {
    return {s.data ? s.data : "", s.size};
}
struct Missing {
    WorkspaceViewState saved;
};
bool DockLayout(const std::string &text) {
    bool docking = false;
    std::map<unsigned, unsigned> parents;
    const auto id = [](const std::string &line, const char *key, unsigned &value) {
        auto pos = line.find(key);
        if (pos == std::string::npos) {
            value = 0;
            return true;
        }
        pos += std::char_traits<char>::length(key);
        auto end = line.find_first_of(" \t\r", pos);
        const auto *first = line.data() + pos,
                   *last = end == std::string::npos ? line.data() + line.size() : line.data() + end;
        const auto parsed = std::from_chars(first, last, value, 16);
        return parsed.ec == std::errc{} && parsed.ptr == last;
    };
    std::istringstream input(text);
    for (std::string line; std::getline(input, line);) {
        if (!line.empty() && line.front() == '[') {
            docking = line == "[Docking][Data]";
            continue;
        }
        if (!docking || line.find("Dock") == std::string::npos)
            continue;
        unsigned node = 0, parent = 0;
        if (!id(line, "ID=0x", node) || !node || !id(line, "Parent=0x", parent) ||
            !parents.emplace(node, parent).second)
            return false;
    }
    for (const auto &entry : parents) {
        std::set<unsigned> seen;
        unsigned node = entry.first;
        while (node) {
            const auto found = parents.find(node);
            if (found == parents.end() || !seen.insert(node).second || seen.size() > 128)
                return false;
            node = found->second;
        }
    }
    return true;
}
} // namespace
bool EditorWorkspace::LoadLayout(const std::string &text, std::string &error) {
    if (text.size() > 1024 * 1024 || text.find('\0') != std::string::npos || !DockLayout(text)) {
        error = "Invalid saved docking layout";
        return false;
    }
    if (ImGui::GetCurrentContext()) {
        ImGui::ClearIniSettings();
        ImGui::LoadIniSettingsFromMemory(text.data(), text.size());
    }
    m_RegionDocks.clear();
    m_LayoutPrepared = false;
    m_LayoutChanged = false;
    error.clear();
    return true;
}
bool EditorWorkspace::Unavailable(EditorView id) const {
    const auto it = m_Views.find(id);
    return it != m_Views.end() && it->second.unavailable;
}
EditorWorkspace::View EditorWorkspace::UnavailableView(const WorkspaceViewState &saved, EditorView id) {
    auto *data = new Missing{saved};
    EditorViewDesc desc{};
    desc.size = sizeof(desc);
    desc.imguiVersion = EDITOR_IMGUI_VERSION;
    desc.id = EditorText(data->saved.type);
    desc.provider = EditorText(data->saved.provider);
    desc.title = EditorText(data->saved.title);
    desc.kind = EDITOR_WINDOW_VIEW;
    desc.area = EDITOR_WORKSPACE;
    desc.singleInstance = saved.single;
    desc.stateVersion = saved.version;
    desc.destroy = [](void *p) { delete static_cast<Missing *>(p); };
    desc.draw = [](void *p, const EditorHost *host, EditorView id, EditorViewFrame *frame) {
        EditorImGuiWindow window(*frame);
        if (!window)
            return;
        const auto &view = static_cast<Missing *>(p)->saved;
        ImGui::TextUnformatted("View provider unavailable");
        ImGui::TextUnformatted(view.type.c_str());
        ImGui::TextWrapped(
            "Its saved settings are retained. Enable the owning module and restart to restore this view.");
        if (!view.module.empty() && ImGui::Button("Enable Module on Next Start")) {
            EditorField module{"module", EDITOR_RECORD};
            module.fields = {{"id", EDITOR_TEXT_VALUE, {}, view.module}, {"enabled", EDITOR_INTEGER_VALUE, {}, {}, 1}};
            const std::vector<EditorField> owned{std::move(module)};
            const auto values = BorrowEditorFields(owned);
            const EditorRequest request{host->scope(host->data, id), values.data(), uint32_t(values.size())};
            host->execute(host->data, EDITOR_TEXT("profile.set-module"), &request, {});
        }
    };
    desc.saveState = [](void *p, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto values = BorrowEditorFields(static_cast<Missing *>(p)->saved.settings);
        return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
    };
    View result{id, desc, data, false, {}};
    result.instance = saved.instance;
    result.role = saved.role;
    result.pinned = saved.pinned;
    result.unavailable = true;
    return result;
}
EditorView EditorWorkspace::Reopen(uint64_t instance, std::string &error) {
    const auto found = m_Saved.find(instance);
    if (found == m_Saved.end())
        return 0;
    const auto &saved = found->second;
    for (const auto &view : m_Views)
        if (view.second.instance == instance || (!saved.role.empty() && view.second.role == saved.role)) {
            Focus(view.first);
            return view.first;
        }
    const auto descriptions = m_Application.Views();
    const auto desc =
        std::find_if(descriptions.begin(), descriptions.end(), [&](const auto &d) { return Text(d.id) == saved.type; });
    if (desc != descriptions.end() && (desc->documentBound || desc->kind != EDITOR_WINDOW_VIEW))
        return 0;
    if (desc != descriptions.end() && desc->singleInstance)
        for (const auto &view : m_Views)
            if (Text(view.second.desc.id) == saved.type) {
                Focus(view.first);
                return view.first;
            }
    const auto id = m_Application.OpenScope();
    if (!id)
        return 0;
    View view{id, {}, nullptr, false, {}};
    if (desc == descriptions.end())
        view = UnavailableView(saved, id);
    else {
        auto *data = m_Application.CreateView(*desc, id);
        if (!data) {
            m_Application.CloseScope(id);
            return 0;
        }
        view.desc = *desc;
        view.data = data;
        const auto previous = m_ClosedStates.find({saved.type, desc->singleInstance ? std::string{} : saved.role});
        bool restored = false;
        if (previous == m_ClosedStates.end())
            restored = ApplySettings(saved, *desc, data, id, error);
        else if (!previous->second.version)
            restored = true;
        else if (previous->second.version == desc->stateVersion)
            restored = m_Application.RestoreViewState(*desc, data, m_Application.Scope(id), previous->second.fields) == EDITOR_OK;
        if (!restored) {
            if (error.empty())
                error = "View state could not be restored";
            m_Application.CloseScope(id);
            m_Application.DestroyView(*desc, data);
            return 0;
        }
    }
    view.instance = instance;
    view.role = saved.role;
    view.pinned = saved.pinned;
    view.placement = {saved.x, saved.y, saved.width, saved.height, saved.dock, saved.placed};
    m_Views.emplace(id, std::move(view));
    m_Saved.at(instance).open = true;
    ++m_ViewsRevision;
    Focus(id);
    return id;
}
bool EditorWorkspace::ApplySettings(const WorkspaceViewState &saved, const EditorViewDesc &desc, void *data,
                                    EditorView id, std::string &error) {
    if (saved.version && (saved.version != desc.stateVersion || !desc.restoreState)) {
        error = "Incompatible saved view settings: " + saved.type;
        return false;
    }
    if (m_Application.RestoreViewSettings(desc, data, m_Application.Scope(id), saved.settings) != EDITOR_OK) {
        error = "View settings could not be restored: " + saved.type;
        return false;
    }
    return true;
}
bool EditorWorkspace::Save(WorkspaceState &result, std::string &error) const {
    return Capture(m_Views, m_Configuration, result, error);
}
bool EditorWorkspace::CaptureView(const View &view, const WorkspaceConfiguration &configuration,
                                  WorkspaceViewState &state, std::string &error) const {
    if (auto previous = m_Saved.find(view.instance); previous != m_Saved.end())
        state = previous->second;
    if (view.unavailable)
        state = static_cast<const Missing *>(view.data)->saved;
    else {
        const auto settings = m_Application.SaveViewSettings(view.desc, view.data, m_Application.Scope(view.id));
        if (settings.status != EDITOR_OK) {
            error = "View settings could not be saved: " + Text(view.desc.id);
            return false;
        }
        state.settings = settings.fields;
        state.version = view.desc.settingCount ? view.desc.stateVersion : 0;
        state.provider = Text(view.desc.provider);
        state.module = m_Application.ProviderModule({"view", Text(view.desc.id), state.provider});
    }
    state.instance = view.instance;
    state.type = Text(view.desc.id);
    state.title = Text(view.desc.title);
    state.role = view.role;
    state.open = true;
    state.pinned = view.pinned;
    state.single = view.desc.singleInstance;
    auto name = WindowName(view.id);
    if (name.empty()) {
        const auto placement = std::find_if(configuration.views.begin(), configuration.views.end(), [&](const auto &v) {
            return v.type == state.type &&
                   ((!view.role.empty() && v.role == view.role) || (view.role.empty() && v.role.empty()));
        });
        name = state.title + "###" +
               (placement != configuration.views.end() && placement->windowId == WorkspaceWindowId::Named &&
                        (placement->role.empty() || placement->role == view.role)
                    ? placement->windowName
                : placement != configuration.views.end() && placement->windowId == WorkspaceWindowId::Role
                    ? view.role
                    : "editor-view-" + std::to_string(view.instance));
    }
    if (ImGui::GetCurrentContext())
        if (const auto *window = ImGui::FindWindowByName(name.c_str())) {
            state.placed = true;
            state.x = window->Pos.x;
            state.y = window->Pos.y;
            state.width = window->Size.x;
            state.height = window->Size.y;
            state.dock = window->DockId;
        }
    return true;
}
bool EditorWorkspace::Capture(const std::map<EditorView, View> &views, const WorkspaceConfiguration &configuration,
                              WorkspaceState &result, std::string &error) const {
    if (m_RestoreFailed) {
        error = "Discard the unrestored workspace before saving a replacement";
        return false;
    }
    WorkspaceState next;
    next.configuration = configuration;
    next.configuration.contributions = {};
    auto saved = m_Saved;
    for (auto &row : saved)
        row.second.open = false;
    for (const auto &entry : views) {
        const auto &view = entry.second;
        if (view.desc.documentBound || view.desc.kind != EDITOR_WINDOW_VIEW)
            continue;
        WorkspaceViewState state;
        if (!CaptureView(view, configuration, state, error))
            return false;
        saved[state.instance] = std::move(state);
    }
    for (const auto &row : saved)
        next.views.push_back(row.second);
    for (const auto id : {m_LastFocusRequest, Focused(), m_LastWindowFocus}) {
        const auto old = m_Views.find(id);
        if (old != m_Views.end()) {
            for (const auto &entry : views)
                if (entry.second.instance == old->second.instance && !entry.second.desc.documentBound &&
                    entry.second.desc.kind == EDITOR_WINDOW_VIEW)
                    next.focused = entry.second.instance;
        }
        if (next.focused)
            break;
    }
    if (!next.focused)
        for (const auto &entry : views)
            if (entry.second.focus && !entry.second.desc.documentBound && entry.second.desc.kind == EDITOR_WINDOW_VIEW)
                next.focused = entry.second.instance;
    if (ImGui::GetCurrentContext()) {
        size_t size = 0;
        const auto *ini = ImGui::SaveIniSettingsToMemory(&size);
        next.layout.assign(ini, size);
    }
    if (!ValidateWorkspaceState(next, error))
        return false;
    result = std::move(next);
    error.clear();
    return true;
}
bool EditorWorkspace::Restore(const WorkspaceState &state, std::string &error) {
    if (!ValidateWorkspaceState(state, error) || !DockLayout(state.layout) ||
        !Validate(state.configuration, true, error)) {
        if (error.empty())
            error = "Invalid saved docking layout";
        m_RestoreFailed = m_Views.empty();
        return false;
    }
    if (!m_Application.NativeAvailable() || m_Application.HasEdits() || Interacting() ||
        (ImGui::GetCurrentContext() && ImGui::GetTopMostPopupModal())) {
        error = "Finish the current operation before restoring a workspace";
        return false;
    }
    std::vector<View> prepared;
    const auto before = m_Application.Configuration();
    const auto rollback = [&] {
        for (auto &view : prepared) {
            m_Application.CloseScope(view.id);
            m_Application.DestroyView(view.desc, view.data);
        }
        std::string ignored;
        m_Application.Configure(before, ignored);
        m_RestoreFailed = m_Views.empty();
        return false;
    };
    if (!m_Application.Configure(state.configuration.contributions, error)) {
        m_RestoreFailed = m_Views.empty();
        return false;
    }
    const auto descriptions = m_Application.Views();
    std::set<std::string> singletons;
    for (const auto &saved : state.views) {
        if (!saved.open)
            continue;
        const auto desc = std::find_if(descriptions.begin(), descriptions.end(),
                                       [&](const auto &d) { return Text(d.id) == saved.type; });
        if (desc != descriptions.end() && (desc->documentBound || desc->kind != EDITOR_WINDOW_VIEW ||
                                           (desc->singleInstance && !singletons.insert(saved.type).second))) {
            error = "Invalid restored view: " + saved.type;
            return rollback();
        }
        const auto id = m_Application.OpenScope();
        if (!id) {
            error = "Cannot allocate view scope";
            return rollback();
        }
        if (desc == descriptions.end())
            prepared.push_back(UnavailableView(saved, id));
        else {
            auto *data = m_Application.CreateView(*desc, id);
            if (!data) {
                m_Application.CloseScope(id);
                error = "View could not be prepared: " + saved.type;
                return rollback();
            }
            prepared.push_back(View{id, *desc, data, false, {}});
            if (!ApplySettings(saved, *desc, data, id, error))
                return rollback();
        }
        auto &view = prepared.back();
        view.instance = saved.instance;
        view.role = saved.role;
        view.pinned = saved.pinned;
        view.placement = {saved.x, saved.y, saved.width, saved.height, saved.dock, saved.placed};
        view.focus = saved.instance == state.focused;
    }
    for (const auto &placement : state.configuration.views) {
        if (!placement.startup)
            continue;
        const auto desc = std::find_if(descriptions.begin(), descriptions.end(),
                                       [&](const auto &d) { return Text(d.id) == placement.type; });
        if (desc == descriptions.end() || desc->kind != EDITOR_OVERLAY_VIEW)
            continue;
        const auto id = m_Application.OpenScope();
        auto *data = m_Application.CreateView(*desc, id);
        if (!data) {
            m_Application.CloseScope(id);
            error = "Overlay could not be prepared: " + placement.type;
            return rollback();
        }
        prepared.push_back(View{id, *desc, data, false, {}});
        prepared.back().role = placement.role;
        prepared.back().pinned = placement.pinned;
    }
    // Every instance is prepared before touching old windows or ImGui layout.
    while (!m_Views.empty())
        Destroy(m_Views.begin()->first);
    m_ClosedStates.clear();
    m_LastFocused.clear();
    m_LastWindowFocus = 0;
    m_LastFocusRequest = 0;
    m_Saved.clear();
    m_Configuration = state.configuration;
    m_Configuration.contributions = {};
    m_NextInstance = 1;
    for (const auto &saved : state.views) {
        m_Saved.emplace(saved.instance, saved);
        m_NextInstance = std::max(m_NextInstance, saved.instance + 1);
    }
    for (auto &view : prepared) {
        if (view.focus) {
            m_LastFocusRequest = view.id;
            m_LastWindowFocus = view.id;
        }
        m_Views.emplace(view.id, std::move(view));
    }
    LoadLayout(state.layout, error); // Validated before preparing any views.
    m_RestoreFailed = false;
    ++m_ViewsRevision;
    error.clear();
    return true;
}
} // namespace vdev
