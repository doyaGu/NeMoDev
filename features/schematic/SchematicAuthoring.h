#pragma once

#include "application/EditorApplication.h"
#include <memory>

namespace vdev {
class SchematicAuthoring {
  public:
    explicit SchematicAuthoring(std::function<Document *()>);
    ~SchematicAuthoring();
    bool Register(EditorApplication &, std::string &error);
    static const std::vector<ProviderChoice> &DefaultProviders();

  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
