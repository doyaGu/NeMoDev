#include "features/environment/EnvironmentCommands.h"
#include "core/document/ToolCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <set>
#include <stdexcept>
#include <limits>

namespace vdev {
namespace {
std::string Text(EditorString value) { return {value.data ? value.data : "", value.size}; }
const EditorValue *Find(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i) if (Text(request.values[i].key) == key) return &request.values[i];
    return nullptr;
}
uint64_t Number(const EditorRequest &request, const char *key) { const auto *v = Find(request, key); return v ? v->integer : 0; }
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto values = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
}
bool ReadPaths(CKContext *context, PathCategories &out) {
    auto *paths = context ? context->GetPathManager() : nullptr;
    if (!paths || paths->GetCategoryCount() < 0 || paths->GetCategoryCount() > 4096) return false;
    PathCategories next;
    for (int c = 0; c < paths->GetCategoryCount(); ++c) {
        XString name;
        if (paths->GetCategoryName(c, name) != CK_OK) return false;
        next.emplace_back(name.CStr() ? name.CStr() : "", std::vector<std::string>{});
        const auto count = paths->GetPathCount(c);
        if (count < 0 || count > 65536) return false;
        for (int i = 0; i < count; ++i) {
            XString path;
            if (paths->GetPathName(c, i, path) != CK_OK) return false;
            next.back().second.emplace_back(path.CStr() ? path.CStr() : "");
        }
    }
    out = std::move(next); return true;
}
bool ReplacePaths(CKContext *context, const PathCategories &categories, bool create = false) {
    auto *paths = context ? context->GetPathManager() : nullptr;
    if (!paths) return false;
    for (const auto &category : categories) {
        XString name(category.first.c_str());
        int index = paths->GetCategoryIndex(name);
        if (index < 0 && create) index = paths->AddCategory(name);
        if (index < 0) return false;
        while (paths->GetPathCount(index)) if (paths->RemovePath(index, 0) != CK_OK) return false;
        for (const auto &value : category.second) {
            XString path(value.c_str());
            if (create && paths->GetPathIndex(index, path) >= 0) continue;
            if (paths->AddPath(index, path) < 0) return false;
        }
    }
    return true;
}
uint64_t Guid(CKGUID guid) { return (uint64_t(guid.d1) << 32) | guid.d2; }
bool StringValid(const std::string &value) {
    return value.size() <= 32768 && value.find_first_of(std::string("\0\r\n", 3)) == std::string::npos;
}
} // namespace

struct EnvironmentCommands::Data {
    enum class Action { Unload, Reload, Manager, Paths, Folder, Respond, Retry, Refresh, Links };
    struct Handler { Data *owner; Action action; };
    struct Unloaded { EditorView owner = 0; bool attempted = false; std::string error; };
    EditorApplication &editor;
    CkRuntimeHost &runtime;
    std::function<bool()> nativeAvailable;
    SavePaths savePaths;
    std::function<void()> pluginsChanged;
    std::function<LinkOptions()> readLinks;
    std::function<bool(const LinkOptions &, std::string &)> saveLinks;
    LinkOptions links;
    uint64_t linkRevision = 1;
    bool linksReady = false;
    std::deque<Handler> handlers;
    std::vector<PluginRow> plugins;
    std::vector<EditorField> pluginFields;
    PathCategories paths;
    std::map<std::string, Unloaded> unloaded;
    uint64_t pluginRevision = 1, pathRevision = 1, generation = UINT64_MAX;
    bool pluginReady = false, pathsReady = false, stopped = false;
    std::set<EditorView> pluginReaders, pathReaders, linkReaders;
    std::chrono::steady_clock::time_point sampled{};
    struct Folder { EditorOperation operation = 0; EditorScope origin{}; uint64_t input = 0; } folder;
    uint64_t nextInput = 1;
    Data(EditorApplication &editor, CkRuntimeHost &runtime, std::function<bool()> available, SavePaths save, std::function<void()> changed)
        : editor(editor), runtime(runtime), nativeAvailable(std::move(available)), savePaths(std::move(save)), pluginsChanged(std::move(changed)) {}
    bool Ready() const { return !stopped && editor.NativeAvailable() && (!nativeAvailable || nativeAvailable()) && runtime.Context(); }
    void CheckGeneration() {
        if (generation == runtime.Generation()) return;
        generation = runtime.Generation(); pluginReady = pathsReady = linksReady = false; ++pluginRevision; ++pathRevision; ++linkRevision;
    }
    bool ReadLinks() {
        if (!Ready() || !readLinks) return false;
        CheckGeneration();
        auto next = readLinks();
        if (auto *manager = runtime.Context()->GetBehaviorManager()) next.iterations = manager->GetBehaviorMaxIteration();
        if (!linksReady || !(next == links)) { links = next; ++linkRevision; }
        linksReady = true; return true;
    }
    bool ReadPathModel() {
        if (!Ready()) return false;
        CheckGeneration(); PathCategories next;
        if (!ReadPaths(runtime.Context(), next)) return false;
        if (!pathsReady || next != paths) { paths = std::move(next); ++pathRevision; }
        pathsReady = true; return true;
    }
    void ReadPlugins() {
        if (!Ready()) return;
        CheckGeneration();
        std::vector<PluginRow> next;
        std::vector<EditorField> fields;
        for (const auto &category : InstalledPlugins(runtime.Context())) {
            if (category.plugins.empty()) {
                EditorField empty{std::to_string(next.size()), EDITOR_RECORD};
                empty.fields = {{"category", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(category.name)}, {"id", EDITOR_INTEGER_VALUE}};
                fields.push_back(std::move(empty)); next.emplace_back();
            }
            for (const auto &plugin : category.plugins) {
                const auto tracked = unloaded.find(plugin.dllPath);
                EditorField row{std::to_string(next.size()), EDITOR_RECORD};
                row.fields = {{"id", EDITOR_INTEGER_VALUE, {}, {}, next.size() + 1},
                    {"category", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(category.name)},
                    {"name", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(plugin.name)},
                    {"description", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(plugin.description)},
                    {"dll", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(plugin.dll)},
                    {"author", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(plugin.author)},
                    {"version", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(plugin.version)},
                    {"active", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(plugin.active))},
                    {"manager", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(plugin.switchable)},
                    {"loaded", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(plugin.loaded)},
                    {"loadable", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(plugin.loadable)},
                    {"unloaded", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(tracked != unloaded.end())},
                    {"error", EDITOR_TEXT_VALUE, {}, tracked == unloaded.end() ? "" : tracked->second.error},
                    {"guid", EDITOR_INTEGER_VALUE, {}, {}, Guid(plugin.guid)},
                    {"manager-guid", EDITOR_INTEGER_VALUE, {}, {}, Guid(plugin.managerGuid)},
                    {"path", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(plugin.dllPath)}};
                fields.push_back(std::move(row)); next.push_back(plugin);
            }
        }
        if (!pluginReady || fields != pluginFields) { pluginFields = std::move(fields); ++pluginRevision; }
        plugins = std::move(next); pluginReady = true;
    }
    void CompleteFolder(EditorStatus status, const std::string &path = {}, const std::string &message = {}) {
        const auto operation = folder.operation; folder = {};
        EditorResponse result; result.status = status; result.message = message;
        if (!path.empty()) result.fields.push_back({"path", EDITOR_TEXT_VALUE, {}, path});
        editor.CompleteOperation(operation, std::move(result));
    }
    void PruneFolder() {
        const auto scope = editor.Scope(folder.origin.view);
        if (folder.operation && (scope.view != folder.origin.view || scope.document != folder.origin.document)) CompleteFolder(EDITOR_CANCELLED);
    }
    bool Restore(const std::string &file, Unloaded &entry) {
        auto *manager = CKGetPluginManager();
        CKERROR error = CKERR_INVALIDPARAMETER;
        bool completed = GuardedCall([&] {
            for (int i = 0; manager && i < manager->GetPluginDllCount(); ++i) {
                auto *dll = manager->GetPluginDllInfo(i);
                if (dll && file == (dll->m_DllFileName.CStr() ? dll->m_DllFileName.CStr() : "")) {
                    error = manager->ReLoadPluginDll(i); break;
                }
            }
        }, &runtime, "Reload plugin");
        if (pluginsChanged) pluginsChanged();
        if (completed && error == CK_OK) { entry.error.clear(); return true; }
        entry.error = "Cannot reload " + AnsiToUtf8(file);
        runtime.Log(LogLevel::Warning, entry.error); return false;
    }
    void RestoreClosed(bool all = false, bool retry = false) {
        if (!Ready() || (!all && editor.HasEdits())) return;
        for (auto it = unloaded.begin(); it != unloaded.end();) {
            const bool closed = editor.Scope(it->second.owner).view != it->second.owner;
            if (!(all || closed) || (it->second.attempted && !retry)) { ++it; continue; }
            it->second.attempted = true;
            if (Restore(it->first, it->second)) it = unloaded.erase(it);
            else ++it;
            pluginReady = false; ++pluginRevision;
        }
    }
    EditorStatus Status(Action action, const EditorRequest &request) const {
        if (stopped) return EDITOR_REFUSED;
        if (action == Action::Folder) return !request.scope.view || folder.operation ? EDITOR_REFUSED : EDITOR_OK;
        if (action == Action::Respond) {
            const auto scope = editor.Scope(folder.origin.view);
            return folder.operation && Number(request, "input") == folder.input && scope.view == folder.origin.view &&
                scope.document == folder.origin.document ? EDITOR_OK : EDITOR_STALE;
        }
        if (!Ready() || editor.HasEdits()) return EDITOR_REFUSED;
        if (action == Action::Links) return !readLinks || !saveLinks ? EDITOR_REFUSED :
            Number(request, "revision") != linkRevision ? EDITOR_STALE :
            Number(request, "delay") > 32764 || Number(request, "prevent") > 1 || !Number(request, "iterations") ||
            Number(request, "iterations") > uint64_t(std::numeric_limits<int>::max()) ? EDITOR_INVALID : EDITOR_OK;
        if (action == Action::Retry || action == Action::Refresh) return EDITOR_OK;
        if (Number(request, "revision") != (action == Action::Paths ? pathRevision : pluginRevision)) return EDITOR_STALE;
        if (action == Action::Paths) return EDITOR_OK;
        const auto id = Number(request, "plugin");
        if (!id || id > plugins.size()) return EDITOR_INVALID;
        const auto &plugin = plugins[size_t(id - 1)];
        if (action == Action::Manager) return !plugin.switchable || Number(request, "active") > 1 ? EDITOR_INVALID : EDITOR_OK;
        if (!plugin.loadable) return EDITOR_REFUSED;
        if (action == Action::Unload) return request.scope.view && plugin.loaded && !unloaded.count(plugin.dllPath) ? EDITOR_OK : EDITOR_REFUSED;
        return unloaded.count(plugin.dllPath) ? EDITOR_OK : EDITOR_REFUSED;
    }
    EditorStatus SetPaths(const EditorRequest &request, EditorSink sink) {
        const auto *categories = Find(request, "categories");
        if (!categories || categories->fields.count != paths.size()) return EDITOR_INVALID;
        PathCategories next; size_t count = 0;
        for (uint32_t i = 0; i < categories->fields.count; ++i) {
            const auto &record = categories->fields.data[i];
            if (record.type != EDITOR_RECORD || record.fields.count != 2) return EDITOR_INVALID;
            const EditorRequest item{{}, record.fields.data, record.fields.count};
            const auto *name = Find(item, "name"), *values = Find(item, "paths");
            if (!name || !values || name->type != EDITOR_TEXT_VALUE || values->type != EDITOR_TEXT_LIST ||
                Text(name->text) != AnsiToUtf8(paths[i].first)) return EDITOR_STALE;
            next.emplace_back(paths[i].first, std::vector<std::string>{});
            std::set<std::string> unique;
            for (uint32_t j = 0; j < values->texts.count; ++j) {
                const auto path = Text(values->texts.data[j]);
                if (path.empty() || !StringValid(path) || ++count > 65536) return EDITOR_INVALID;
                const auto native = Utf8ToAnsi(path);
                if (!unique.insert(native).second) return EDITOR_INVALID;
                next.back().second.push_back(native);
            }
        }
        if (next == paths) return EDITOR_NO_CHANGE;
        const auto before = paths;
        std::string error;
        bool applied = false;
        const bool completed = GuardedCall([&] {
            applied = ReplacePaths(runtime.Context(), next);
            if (applied && savePaths) applied = savePaths(next, error);
        }, &runtime, "Apply search paths");
        if (!completed || !applied) {
            bool restored = false;
            const bool recovered = GuardedCall([&] { restored = ReplacePaths(runtime.Context(), before); }, &runtime, "Restore search paths");
            ReadPathModel();
            if (error.empty()) error = "Cannot apply search paths";
            if (!recovered || !restored) error += "; previous search paths could not be restored";
            Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, error}}); return EDITOR_ERROR;
        }
        ReadPathModel(); return EDITOR_OK;
    }
    EditorStatus Run(Action action, const EditorRequest &request, EditorSink sink) {
        PruneFolder();
        if (action == Action::Folder) {
            folder = {request.operation, request.scope, nextInput++}; return EDITOR_QUEUED;
        }
        if (action == Action::Respond) {
            const auto choice = Text(Find(request, "choice")->text);
            if (choice == "cancel" || choice == "error") {
                const auto *message = Find(request, "message");
                CompleteFolder(choice == "cancel" ? EDITOR_CANCELLED : EDITOR_ERROR, {}, message ? Text(message->text) : ""); return EDITOR_OK;
            }
            const auto *path = Find(request, "path");
            if (choice != "accept" || !path || Text(path->text).empty() || !StringValid(Text(path->text))) return EDITOR_INVALID;
            CompleteFolder(EDITOR_OK, Text(path->text)); return EDITOR_OK;
        }
        if (action == Action::Links) ReadLinks();
        else if (action == Action::Paths) ReadPathModel(); else ReadPlugins();
        const auto status = Status(action, request);
        if (status != EDITOR_OK) return status;
        if (action == Action::Links) {
            const LinkOptions next{int(Number(request, "delay")), Number(request, "prevent") != 0, int(Number(request, "iterations"))};
            if (next == links) return EDITOR_NO_CHANGE;
            auto *manager = runtime.Context()->GetBehaviorManager();
            if (!manager) return EDITOR_REFUSED;
            const auto before = links;
            bool saved = false;
            std::string error;
            const bool completed = GuardedCall([&] {
                manager->SetBehaviorMaxIteration(next.iterations);
                saved = saveLinks(next, error);
            }, &runtime, "Apply behavior link properties");
            if (!completed || !saved) {
                GuardedCall([&] { manager->SetBehaviorMaxIteration(before.iterations); }, &runtime, "Restore behavior link properties");
                ReadLinks();
                Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, error.empty() ? "Behavior link properties could not be saved." : error}});
                return EDITOR_ERROR;
            }
            ReadLinks(); return EDITOR_OK;
        }
        if (action == Action::Retry) { RestoreClosed(false, true); ReadPlugins(); return EDITOR_OK; }
        if (action == Action::Refresh) { ReadPathModel(); if (pluginsChanged) pluginsChanged(); return EDITOR_OK; }
        if (action == Action::Paths) return SetPaths(request, sink);
        const auto plugin = plugins[size_t(Number(request, "plugin") - 1)];
        if (action == Action::Manager) {
            auto *manager = runtime.Context()->GetManagerByName(plugin.name.c_str());
            if (!manager || manager->GetGuid() != plugin.managerGuid) return EDITOR_STALE;
            const bool completed = GuardedCall([&] { runtime.Context()->ActivateManager(manager, Number(request, "active") != 0); }, &runtime, "Set manager active");
            if (pluginsChanged) pluginsChanged();
            const bool success = bool(runtime.Context()->IsManagerActive(manager)) == bool(Number(request, "active"));
            ReadPlugins(); return completed && success ? EDITOR_OK : EDITOR_REFUSED;
        }
        if (action == Action::Reload) {
            auto it = unloaded.find(plugin.dllPath);
            const bool result = Restore(it->first, it->second);
            if (result) unloaded.erase(it);
            ReadPlugins(); return result ? EDITOR_OK : EDITOR_ERROR;
        }
        // Reserve the recovery entry before unloading code, so allocation
        // failure cannot lose the information needed to restore it.
        unloaded.emplace(plugin.dllPath, Unloaded{request.scope.view});
        CKERROR error = CKERR_INVALIDPARAMETER;
        const bool completed = GuardedCall([&] { error = CKGetPluginManager()->UnLoadPluginDll(plugin.dllIndex); }, &runtime, "Unload plugin");
        if (completed && error != CK_OK) unloaded.erase(plugin.dllPath);
        if (pluginsChanged) pluginsChanged();
        ReadPlugins();
        if (!completed || error != CK_OK) {
            Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, "Cannot unload " + AnsiToUtf8(plugin.dll)}}); return EDITOR_ERROR;
        }
        return EDITOR_OK;
    }
    static EditorStatus Check(void *data, const EditorHost *, const EditorRequest *request) {
        auto &handler = *static_cast<Handler *>(data); return handler.owner->Status(handler.action, *request);
    }
    static EditorStatus Execute(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &handler = *static_cast<Handler *>(data); return handler.owner->Run(handler.action, *request, sink);
    }
    static EditorStatus Query(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        return Write(sink, {{"available", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(self.Ready() && !self.editor.HasEdits())},
            {"plugins", EDITOR_INTEGER_VALUE, {}, {}, self.pluginRevision}, {"paths", EDITOR_INTEGER_VALUE, {}, {}, self.pathRevision},
            {"recoveries", EDITOR_INTEGER_VALUE, {}, {}, self.unloaded.size()}});
    }
    static EditorStatus Plugins(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        if (request->scope.view) self.pluginReaders.insert(request->scope.view);
        if (!self.pluginReady) self.ReadPlugins();
        const auto first = Number(*request, "first");
        if (first > self.pluginFields.size()) return EDITOR_INVALID;
        if (Number(*request, "revision") && Number(*request, "revision") != self.pluginRevision) return EDITOR_STALE;
        const auto end = std::min(self.pluginFields.size(), size_t(first) + 128);
        EditorField rows{"rows", EDITOR_RECORD};
        rows.fields.assign(self.pluginFields.begin() + size_t(first), self.pluginFields.begin() + end);
        return Write(sink, {{"revision", EDITOR_INTEGER_VALUE, {}, {}, self.pluginRevision},
            {"total", EDITOR_INTEGER_VALUE, {}, {}, self.pluginFields.size()}, {"next", EDITOR_INTEGER_VALUE, {}, {}, end}, rows});
    }
    static EditorStatus Paths(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        if (request->scope.view) self.pathReaders.insert(request->scope.view);
        if (!self.pathsReady) self.ReadPathModel();
        EditorField categories{"categories", EDITOR_RECORD};
        for (size_t i = 0; i < self.paths.size(); ++i) {
            EditorField category{std::to_string(i), EDITOR_RECORD};
            category.fields = {{"name", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(self.paths[i].first)}, {"paths", EDITOR_TEXT_LIST}};
            for (const auto &path : self.paths[i].second) category.fields[1].texts.push_back(AnsiToUtf8(path));
            categories.fields.push_back(std::move(category));
        }
        return Write(sink, {{"revision", EDITOR_INTEGER_VALUE, {}, {}, self.pathRevision}, categories});
    }
};
EnvironmentCommands::EnvironmentCommands(EditorApplication &editor, CkRuntimeHost &runtime, std::function<bool()> available, SavePaths save,
                                         std::function<void()> changed, const PathCategories *initialPaths)
    : m_Data(std::make_unique<Data>(editor, runtime, std::move(available), std::move(save), std::move(changed))), m_InitialPaths(initialPaths) {}
EnvironmentCommands::~EnvironmentCommands() = default;
void EnvironmentCommands::ConfigureLinks(std::function<LinkOptions()> read,
    std::function<bool(const LinkOptions &, std::string &)> save) {
    m_Data->readLinks = std::move(read); m_Data->saveLinks = std::move(save);
    m_Data->linksReady = false; ++m_Data->linkRevision;
}
bool EnvironmentCommands::InitializePaths(const PathCategories &paths, std::string &error) {
    if (!m_Data->Ready()) { error = "The runtime is busy"; return false; }
    bool result = false;
    const bool completed = GuardedCall([&] { result = ReplacePaths(m_Data->runtime.Context(), paths, true); }, &m_Data->runtime, "Initialize search paths");
    if (!completed || !result) error = "Cannot initialize saved search paths";
    m_Data->ReadPathModel(); return completed && result;
}
void EnvironmentCommands::Poll() {
    auto &self = *m_Data;
    if (self.stopped) return;
    self.PruneFolder(); self.RestoreClosed();
    const auto prune = [&](std::set<EditorView> &readers) {
        for (auto it = readers.begin(); it != readers.end();)
            if (self.editor.Scope(*it).view != *it) it = readers.erase(it); else ++it;
    };
    prune(self.pathReaders); prune(self.pluginReaders); prune(self.linkReaders);
    if (!self.Ready()) return;
    const auto now = std::chrono::steady_clock::now();
    const bool changed = self.generation != self.runtime.Generation();
    if (changed) self.CheckGeneration();
    if (changed || now - self.sampled >= std::chrono::milliseconds(250)) {
        self.sampled = now;
        if (!self.pathReaders.empty()) self.ReadPathModel();
        if (!self.pluginReaders.empty() || !self.unloaded.empty()) self.ReadPlugins();
        if (!self.linkReaders.empty()) self.ReadLinks();
    }
}
void EnvironmentCommands::Stop() {
    if (m_Data->stopped) return;
    if (m_Data->folder.operation) m_Data->CompleteFolder(EDITOR_CANCELLED);
    m_Data->RestoreClosed(true, true); m_Data->stopped = true;
}
bool EnvironmentCommands::Register(std::string &error) {
    auto &self = *m_Data;
    const EditorParameter pluginArgs[] = {{EDITOR_TEXT("plugin"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}};
    const EditorParameter managerArgs[] = {pluginArgs[0], pluginArgs[1], {EDITOR_TEXT("active"), EDITOR_INTEGER_VALUE}};
    const EditorParameter pathArgs[] = {pluginArgs[1], {EDITOR_TEXT("categories"), EDITOR_RECORD}};
    const EditorParameter linkArgs[] = {pluginArgs[1], {EDITOR_TEXT("delay"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("prevent"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("iterations"), EDITOR_INTEGER_VALUE}};
    const EditorParameter response[] = {{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("choice"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, 1}, {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, 1}};
    const struct { Data::Action action; const char *id, *title; const EditorParameter *args; uint32_t count; } descriptions[] = {
        {Data::Action::Unload, "plugins.unload", "Temporary Unload", pluginArgs, 2},
        {Data::Action::Reload, "plugins.reload", "Reload Plugin", pluginArgs, 2},
        {Data::Action::Manager, "plugins.set-manager-active", "Set Manager Active", managerArgs, 3},
        {Data::Action::Paths, "paths.apply", "Apply Search Paths", pathArgs, 2},
        {Data::Action::Folder, "paths.choose-folder", "Choose Search Path", nullptr, 0},
        {Data::Action::Respond, "paths.folder-response", "Choose Search Path Result", response, 4},
        {Data::Action::Retry, "plugins.retry-reloads", "Retry Plugin Reloads", nullptr, 0},
        {Data::Action::Refresh, "environment.refresh", "Refresh Plugins and Search Paths", nullptr, 0},
        {Data::Action::Links, "environment.set-link-options", "Behavior Link Properties", linkArgs, 4}};
    std::vector<EditorCommandDesc> commands;
    for (const auto &description : descriptions) {
        self.handlers.push_back({&self, description.action});
        const bool folder = description.action == Data::Action::Folder || description.action == Data::Action::Respond;
        commands.push_back({sizeof(EditorCommandDesc), EditorLiteral(description.id), EDITOR_TEXT("builtin.environment"),
            EditorLiteral(description.title), description.args, description.count, Data::Execute, &self.handlers.back(),
            EDITOR_APPLICATION, folder ? EDITOR_SESSION_CHANGE : EDITOR_RUNTIME_CONTROL, Data::Check});
    }
    const EditorParameter stateFields[] = {{EDITOR_TEXT("available"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("plugins"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("paths"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("recoveries"), EDITOR_INTEGER_VALUE}};
    const EditorParameter page[] = {{EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter pluginFields[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("rows"), EDITOR_RECORD}};
    const EditorParameter pathFields[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("categories"), EDITOR_RECORD}};
    const EditorParameter folderFields[] = {{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE}};
    const EditorParameter linkFields[] = {pluginArgs[1], linkArgs[1], linkArgs[2], linkArgs[3]};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("environment.link-options"), EDITOR_TEXT("builtin.environment"), nullptr, 0,
            [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
                auto &self = *static_cast<Data *>(data);
                const bool first = request->scope.view && self.linkReaders.insert(request->scope.view).second;
                if (!self.linksReady || first) self.ReadLinks();
                if (!self.linksReady) return EDITOR_REFUSED;
                return Write(sink, {{"revision", EDITOR_INTEGER_VALUE, {}, {}, self.linkRevision},
                    {"delay", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(self.links.delay))},
                    {"prevent", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(self.links.preventLoop)},
                    {"iterations", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(self.links.iterations))}});
            }, &self, EDITOR_QUERY_VERSIONED, EDITOR_APPLICATION, linkFields, 4,
            [](void *data, const EditorHost *) { return static_cast<Data *>(data)->linkRevision; }},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("environment.state"), EDITOR_TEXT("builtin.environment"), nullptr, 0, Data::Query, &self, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, stateFields, 4},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("plugins.list"), EDITOR_TEXT("builtin.environment"), page, 2, Data::Plugins, &self, EDITOR_QUERY_VERSIONED, EDITOR_APPLICATION, pluginFields, 4,
            [](void *data, const EditorHost *) { return static_cast<Data *>(data)->pluginRevision; }},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("paths.list"), EDITOR_TEXT("builtin.environment"), nullptr, 0, Data::Paths, &self, EDITOR_QUERY_VERSIONED, EDITOR_APPLICATION, pathFields, 2,
            [](void *data, const EditorHost *) { return static_cast<Data *>(data)->pathRevision; }},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("paths.folder-input"), EDITOR_TEXT("builtin.environment"), nullptr, 0,
            [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
                const auto &folder = static_cast<Data *>(data)->folder;
                if (!folder.operation) return EDITOR_NO_CHANGE;
                return Write(sink, {{"input", EDITOR_INTEGER_VALUE, {}, {}, folder.input},
                    {"operation", EDITOR_INTEGER_VALUE, {}, {}, folder.operation}, {"view", EDITOR_INTEGER_VALUE, {}, {}, folder.origin.view}});
            }, &self, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, folderFields, 3}};
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION; module.id = EDITOR_TEXT("environment.settings");
    module.data = this;
    module.start = [](void *data, const EditorHost *) {
        auto &self = *static_cast<EnvironmentCommands *>(data);
        std::string error;
        if (self.m_InitialPaths && !self.InitializePaths(*self.m_InitialPaths, error) && self.m_Data->editor.report)
            self.m_Data->editor.report({EDITOR_ERROR, error});
        return EDITOR_OK;
    };
    module.update = [](void *data, const EditorHost *, EditorModulePhase phase) {
        if (phase == EDITOR_PREPARE_FRAME) static_cast<EnvironmentCommands *>(data)->Poll();
    };
    module.stop = [](void *data, const EditorHost *) { static_cast<EnvironmentCommands *>(data)->Stop(); };
    module.commands = commands.data(); module.commandCount = uint32_t(commands.size());
    module.queries = queries; module.queryCount = uint32_t(std::size(queries));
    return self.editor.Register(module, error);
}
} // namespace vdev
