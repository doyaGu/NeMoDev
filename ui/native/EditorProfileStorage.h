#pragma once

#include "application/EditorProfile.h"

namespace vdev {
class EditorWorkspace;
class SaveFileOperations;

struct EditorProfilePaths {
    std::string file, userDirectory, applicationDirectory, runtimeDirectory;
    bool isolated = false;
};

// Owns live preferences and the last saved profile for one editor lifetime.
// JSON and view state remain owned by EditorProfile and EditorWorkspace.
class EditorProfileStorage {
  public:
    using Prepare = std::function<bool(std::string &)>;
    EditorProfileStorage(EditorApplication &, EditorWorkspace &, Prepare = {}, SaveFileOperations * = nullptr);
    ~EditorProfileStorage();
    EditorProfileStorage(const EditorProfileStorage &) = delete;
    EditorProfileStorage &operator=(const EditorProfileStorage &) = delete;
    Preferences &Settings() { return m_Settings; }
    const Preferences &Settings() const { return m_Settings; }
    EditorProfile &Profile() { return m_Profile; }
    const EditorProfile &Profile() const { return m_Profile; }
    bool Load(EditorProfilePaths, std::string &error, std::vector<std::string> *imported = nullptr);
    bool DiscoverModules(const std::vector<std::string> &files, const std::vector<std::string> &directories,
                         std::vector<EditorModuleSource> &, std::string &error) const;
    // Failed saved views are retained by the workspace; defaults can still be
    // opened without replacing their saved state. Diagnostics are nonfatal.
    bool RestoreWorkspace(const WorkspaceConfiguration &defaults, std::string &diagnostic, std::string &error);
    bool Start(const std::vector<std::string> &files, const std::vector<std::string> &directories, std::string &error);
    void Stop();
    bool Save(std::string &error);
    bool Save(const Preferences &, std::string &error);
    bool Save(EditorProfile &candidate, std::string &error);
    bool SaveWorkspaceIfNeeded(std::string &error);

  private:
    bool SaveConfiguration(const Preferences &, const EditorConfiguration &, const WorkspaceState *, std::string &error);
    bool Write(EditorProfile &, const Preferences &, const EditorConfiguration &, const WorkspaceState *, std::string &error);
    EditorApplication &m_Editor;
    EditorWorkspace &m_Workspace;
    Prepare m_Prepare;
    SaveFileOperations *m_FileOperations;
    Preferences m_Settings;
    EditorProfile m_Profile;
    EditorProfilePaths m_Paths;
    bool m_Ready = false;
    double m_RetryAt = 0;
};
} // namespace vdev
