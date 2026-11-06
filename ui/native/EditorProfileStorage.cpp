#include "ui/native/EditorProfileStorage.h"
#include "workspace/EditorWorkspace.h"
#include "workspace/EditorClient.h"
#include "core/document/SaveReport.h"
#include "core/document/OutputFiles.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace vdev {
namespace {
namespace fs = std::filesystem;
fs::path LegacyFile(const EditorProfilePaths &paths, const std::vector<const char *> &names) {
    std::error_code error;
    if (!paths.userDirectory.empty()) {
        const auto file = fs::u8path(paths.userDirectory) / names.front();
        if (fs::exists(file, error)) return file;
    }
    if (!paths.applicationDirectory.empty())
        for (const auto *name : names) {
            const auto file = fs::u8path(paths.applicationDirectory) / name;
            if (fs::exists(file, error)) return file;
        }
    return {};
}
}

EditorProfileStorage::EditorProfileStorage(EditorApplication &editor, EditorWorkspace &workspace,
                                         Prepare prepare, SaveFileOperations *operations)
    : m_Editor(editor), m_Workspace(workspace), m_Prepare(std::move(prepare)), m_FileOperations(operations) {}
EditorProfileStorage::~EditorProfileStorage() { Stop(); }

bool EditorProfileStorage::Load(EditorProfilePaths paths, std::string &error, std::vector<std::string> *imported) try {
    if (m_Ready) { error = "Stop profile saving before loading another profile"; return false; }
    if (!paths.isolated) {
        if (paths.userDirectory.empty())
            if (char *directory = SDL_GetPrefPath("Virtools", "VirtoolsDev")) {
                paths.userDirectory = directory;
                SDL_free(directory);
            }
        if (paths.applicationDirectory.empty())
            if (const char *base = SDL_GetBasePath()) paths.applicationDirectory = base;
        if (paths.file.empty()) {
            if (paths.userDirectory.empty()) { error = "Cannot locate the user profile directory"; return false; }
            paths.file = (fs::u8path(paths.userDirectory) / "NeMoDev.json").u8string();
        }
    }
    if (!paths.file.empty()) paths.file = fs::absolute(fs::u8path(paths.file)).u8string();
    EditorProfile next;
    std::vector<std::string> messages;
    auto status = EditorProfile::ReadResult::Missing;
    if (!paths.file.empty()) status = next.Load(paths.file, error);
    if (status == EditorProfile::ReadResult::Failed) { error = paths.file + ": " + error; return false; }
    Preferences settings;
    if (status == EditorProfile::ReadResult::Loaded) settings = next.Settings();
    else if (!paths.isolated) {
        const auto file = LegacyFile(paths, {"VirtoolsDevPreferences.ini", "SchematicEditorPreferences.ini"});
        if (file.empty() || !settings.Load(file.u8string())) {
            const int count = settings.ImportFromDev() + settings.ImportDevIni(paths.runtimeDirectory);
            if (count) messages.push_back("Imported " + std::to_string(count) + " Virtools Dev preferences");
        } else if (!settings.settingsSaved) {
            settings.ImportDevSettings(); settings.ImportDevIni(paths.runtimeDirectory);
        }
    }
    if (!paths.isolated && !settings.pathsSaved && settings.ImportDevPaths())
        messages.push_back("Path Manager: Virtools Dev's paths");
    m_Profile = std::move(next);
    m_Settings = std::move(settings);
    m_Paths = std::move(paths);
    m_RetryAt = 0;
    if (imported) *imported = std::move(messages);
    error.clear();
    return true;
} catch (const std::exception &failure) { error = failure.what(); return false; }

bool EditorProfileStorage::DiscoverModules(const std::vector<std::string> &files,
    const std::vector<std::string> &directories, std::vector<EditorModuleSource> &result, std::string &error) const try {
    std::vector<EditorModuleDirectory> search;
    if (!m_Paths.isolated) {
        if (!m_Paths.applicationDirectory.empty()) search.push_back({EditorModuleDirectoryAt(m_Paths.applicationDirectory)});
        if (!m_Paths.userDirectory.empty()) search.push_back({(fs::u8path(m_Paths.userDirectory) / "modules").u8string()});
    }
    for (const auto &path : m_Profile.Directories()) search.push_back({path, true});
    for (const auto &path : directories) search.push_back({path, true});
    auto selected = m_Profile.Files(); selected.insert(selected.end(), files.begin(), files.end());
    const auto modules = m_Profile.ResolvedModules();
    selected.erase(std::remove_if(selected.begin(), selected.end(), [&](const auto &file) {
        const auto path = fs::weakly_canonical(fs::u8path(file)).u8string();
        return std::any_of(modules.begin(), modules.end(), [&](const auto &module) {
            return !module.enabled && !module.path.empty() && module.path == path;
        });
    }), selected.end());
    return DiscoverEditorModules(search, selected, result, error);
} catch (const std::exception &failure) { error = std::string("Editor module paths: ") + failure.what(); return false; }

bool EditorProfileStorage::RestoreWorkspace(const WorkspaceConfiguration &defaults, std::string &diagnostic,
                                          std::string &error) {
    diagnostic.clear();
    if (const auto *saved = m_Profile.Workspace()) {
        auto next = *saved;
        next.configuration.contributions = m_Editor.Configuration();
        if (m_Workspace.Restore(next, error)) return true;
        diagnostic = error;
    }
    if (!m_Workspace.Configure(defaults, error)) return false;
    m_Workspace.OpenRole("main.level");
    if (!m_Paths.isolated && !m_Profile.Workspace()) {
        const auto file = LegacyFile(m_Paths, {"VirtoolsDevInterface.ini"});
        std::error_code failure;
        if (!file.empty() && fs::is_regular_file(file, failure) && fs::file_size(file, failure) <= 1024 * 1024) {
            std::ifstream input(file, std::ios::binary);
            const std::string text{std::istreambuf_iterator<char>(input), {}};
            if (input && !m_Workspace.LoadLayout(text, error)) diagnostic = error;
        }
    }
    error.clear();
    return true;
}

bool EditorProfileStorage::Start(const std::vector<std::string> &files,
                               const std::vector<std::string> &directories, std::string &error) {
    auto next = m_Profile;
    if (!next.AddSources(files, directories, error) ||
        (!m_Workspace.SavingBlocked() && !Write(next, m_Settings, m_Editor.Configuration(), nullptr, error))) return false;
    m_Profile = std::move(next);
    m_Ready = true;
    m_Workspace.ConfigurationWriter([this](const auto &configuration, const auto &state, auto &failure) {
        return SaveConfiguration(m_Settings, configuration, &state, failure);
    });
    error.clear();
    return true;
}
void EditorProfileStorage::Stop() {
    if (m_Ready) m_Workspace.ConfigurationWriter({});
    m_Ready = false;
}

bool EditorProfileStorage::Write(EditorProfile &profile, const Preferences &settings,
    const EditorConfiguration &configuration, const WorkspaceState *saved, std::string &error) try {
    auto next = profile;
    WorkspaceState state;
    bool captured = saved != nullptr;
    if (!saved) {
        if (!m_Editor.NativeAvailable() && !m_Workspace.SavingBlocked()) saved = profile.Workspace();
        else {
            if (!m_Workspace.Save(state, error)) return false;
            saved = &state;
            captured = true;
        }
    }
    if (saved && !next.SetWorkspace(*saved, error)) return false;
    if (!next.Capture(settings, configuration, m_Editor.DefaultConfiguration(),
        [this](const auto &choice) { return m_Editor.ProviderModule(choice); }, error)) return false;
    if (!m_Paths.file.empty()) {
        std::string text;
        if (!next.Encode(text, error)) return false;
        SaveReport report;
        OutputFiles output(report, m_FileOperations);
        const auto staged = output.Add(m_Paths.file);
        std::vector<std::string> written;
        bool published = false;
        if (!staged.empty()) {
            std::ofstream stream(fs::u8path(staged), std::ios::binary);
            stream.write(text.data(), std::streamsize(text.size())); stream.close();
            published = stream && output.Publish(written);
        }
        if (!published) {
            error = "Profile could not be saved: " + m_Paths.file;
            for (const auto &message : report.errors) error += "\n" + message;
            return false;
        }
    }
    profile = std::move(next);
    if (captured && ImGui::GetCurrentContext()) {
        ImGui::GetIO().WantSaveIniSettings = false;
        ImGui::GetCurrentContext()->SettingsDirtyTimer = 0;
    }
    m_RetryAt = 0;
    error.clear();
    return true;
} catch (const std::exception &failure) { error = failure.what(); return false; }
  catch (...) { error = "Profile saving raised an exception"; return false; }

bool EditorProfileStorage::SaveConfiguration(const Preferences &settings, const EditorConfiguration &configuration,
    const WorkspaceState *state, std::string &error) try {
    if (!m_Ready) { error.clear(); return true; }
    if (m_Prepare && !m_Prepare(error)) return false;
    return Write(m_Profile, settings, configuration, state, error);
} catch (const std::exception &failure) { error = failure.what(); return false; }
  catch (...) { error = "Configuration preparation raised an exception"; return false; }
bool EditorProfileStorage::Save(std::string &error) {
    return SaveConfiguration(m_Settings, m_Editor.Configuration(), nullptr, error);
}
bool EditorProfileStorage::Save(const Preferences &settings, std::string &error) {
    // Keep the live object at a stable address for commands that borrow it.
    auto next = settings;
    if (m_Ready && !Write(m_Profile, next, m_Editor.Configuration(), nullptr, error)) return false;
    m_Settings = std::move(next);
    error.clear();
    return true;
}
bool EditorProfileStorage::Save(EditorProfile &candidate, std::string &error) {
    if (!m_Ready) { error = "Profile saving has not started"; return false; }
    return Write(candidate, m_Settings, m_Editor.Configuration(), nullptr, error);
}
bool EditorProfileStorage::SaveWorkspaceIfNeeded(std::string &error) {
    error.clear();
    if (!ImGui::GetCurrentContext() || !m_Ready || m_Paths.file.empty()) return true;
    const auto &io = ImGui::GetIO();
    if (!io.WantSaveIniSettings || !m_Editor.NativeAvailable() || m_Editor.HasEdits() ||
        m_Workspace.Interacting() || m_Workspace.SavingBlocked() || ImGui::GetTime() < m_RetryAt) return true;
    const auto result = EditorClient(m_Editor.Host(), 0).Command("workspace.save");
    if (result.status == EDITOR_OK) return true;
    error = result.message.empty() ? "Workspace could not be saved" : result.message;
    m_RetryAt = ImGui::GetTime() + 5;
    return false;
}
} // namespace vdev
