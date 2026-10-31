#pragma once
#include "application/EditorApplication.h"
#include "application/RuntimeOperation.h"

namespace vdev {
class CkRuntimeHost;
class DiagnosticCommands {
public:
    DiagnosticCommands(EditorApplication &, RuntimeOperation &, CkRuntimeHost &,
                       std::function<Document *()> current, std::function<bool()> nativeAvailable);
    ~DiagnosticCommands();
    bool Register(std::string &error);
    static const std::vector<ProviderChoice> &DefaultProviders();
    void Poll();
    void InvalidateVersions();
    void Stop();
private:
    struct Data;
    std::shared_ptr<Data> m_Data;
};
} // namespace vdev
