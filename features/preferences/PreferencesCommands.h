#pragma once
#include "application/EditorApplication.h"
#include "application/Preferences.h"
#include <functional>
#include <memory>

namespace vdev {
class CkRuntimeHost;
struct SceneEnvironment;
class PreferencesCommands {
public:
    using Save = std::function<bool(const Preferences &, std::string &)>;
    PreferencesCommands(EditorApplication &, CkRuntimeHost &, Preferences &, std::function<bool()> available,
                        Save, std::function<void()> changed);
    ~PreferencesCommands();
    bool Register(std::string &error);
    void Poll();
    void InvalidateCatalog();
    void Stop();
    bool ApplyRuntimeSettings(std::string &error);
    SceneEnvironment NewSceneEnvironment() const;
    static const std::vector<ProviderChoice> &DefaultProviders();
private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
