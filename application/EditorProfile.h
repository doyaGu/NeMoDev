#pragma once

#include "application/EditorModules.h"
#include "application/Preferences.h"
#include "application/WorkspaceState.h"

namespace vdev {

// The application's one writable configuration. Unknown JSON members remain
// owned here; consumers use typed settings and copied contribution choices.
class EditorProfile {
  public:
    enum class ReadResult { Missing, Loaded, Failed };
    EditorProfile();
    EditorProfile(const EditorProfile &);
    EditorProfile &operator=(const EditorProfile &);
    EditorProfile(EditorProfile &&) noexcept;
    EditorProfile &operator=(EditorProfile &&) noexcept;
    ~EditorProfile();
    ReadResult Load(const std::string &path, std::string &error);
    bool Decode(const std::string &text, const std::string &path, std::string &error);
    bool Encode(std::string &text, std::string &error) const;
    const Preferences &Settings() const;
    const std::vector<EditorModuleSetting> &Modules() const;
    std::vector<EditorModuleSetting> ResolvedModules() const;
    std::vector<std::string> Files() const;
    std::vector<std::string> Directories() const;
    EditorConfiguration Contributions() const;
    const WorkspaceState *Workspace() const;
    bool SetWorkspace(const WorkspaceState &, std::string &error);
    bool SetModule(const EditorModuleSetting &, std::string &error);
    bool AddSources(const std::vector<std::string> &files, const std::vector<std::string> &directories,
                    std::string &error);
    bool Capture(const Preferences &, const EditorConfiguration &, const EditorConfiguration &defaults,
                 const std::function<std::string(const ProviderChoice &)> &owner, std::string &error);
  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
