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
    struct LinkOptions {
        int delay = 0;
        bool preventLoop = true;
        int iterations = 1;
        bool operator==(const LinkOptions &other) const { return delay == other.delay && preventLoop == other.preventLoop && iterations == other.iterations; }
    };
    using SavePaths = std::function<bool(const PathCategories &, std::string &)>;
    EnvironmentCommands(EditorApplication &, CkRuntimeHost &, std::function<bool()> nativeAvailable, SavePaths = {},
                        std::function<void()> pluginsChanged = {});
    ~EnvironmentCommands();
    bool Register(std::string &error);
    bool InitializePaths(const PathCategories &, std::string &error);
    void ConfigureLinks(std::function<LinkOptions()> read,
                        std::function<bool(const LinkOptions &, std::string &)> save);
    void Poll();
    void Stop();
  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
