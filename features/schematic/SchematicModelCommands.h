#pragma once
#include "application/EditorApplication.h"
#include <memory>
namespace vdev {
class SchematicModelCommands {
  public:
    explicit SchematicModelCommands(std::function<Document *()>);
    ~SchematicModelCommands();
    bool Register(EditorApplication &, std::string &);
    static const std::vector<ProviderChoice> &DefaultProviders();

  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
