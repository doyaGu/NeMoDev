#pragma once
#include "application/EditorApplication.h"
#include <memory>

namespace vdev {
class SchematicScriptCommands {
  public:
    explicit SchematicScriptCommands(std::function<Document *()>);
    ~SchematicScriptCommands();
    bool Register(EditorApplication &, std::string &);
    static const std::vector<ProviderChoice> &DefaultProviders();

  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
