#pragma once
#include "application/EditorApplication.h"
#include "application/RuntimeOperation.h"
#include "core/runtime/CkRuntimeHost.h"

namespace vdev {
// Open library contents belong to the application, independently of documents.
// Views keep only navigation and selections; queries read copied directory data.
class DataResourceCommands {
  public:
    DataResourceCommands(EditorApplication &, RuntimeOperation &, CkRuntimeHost &);
    ~DataResourceCommands();
    bool Register(std::string &error);
    void Poll();
    void Stop();
    const std::vector<std::string> &OpenLibraries() const;
    std::function<void()> changed;

  private:
    struct State;
    std::unique_ptr<State> m_State;
};
} // namespace vdev
