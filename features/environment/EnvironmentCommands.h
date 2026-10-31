#pragma once
#include "application/EditorApplication.h"
#include <functional>
#include <memory>

namespace vdev {
class CkRuntimeHost;
// Native-encoded categories are used only at application assembly, matching
// the existing preferences format. Registered queries/commands use UTF-8.
using PathCategories = std::vector<std::pair<std::string, std::vector<std::string>>>;
class EnvironmentCommands {
  public:
    using SavePaths = std::function<bool(const PathCategories &, std::string &)>;
    EnvironmentCommands(EditorApplication &, CkRuntimeHost &, std::function<bool()> nativeAvailable, SavePaths = {},
                        std::function<void()> pluginsChanged = {});
    ~EnvironmentCommands();
    bool Register(std::string &error);
    bool InitializePaths(const PathCategories &, std::string &error);
    void Poll();
    void Stop();
  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
