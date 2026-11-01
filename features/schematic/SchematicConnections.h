#pragma once
#include "application/EditorApplication.h"
#include <memory>

namespace vdev {
class SchematicConnections {
public:
    explicit SchematicConnections(std::function<Document *()>);
    ~SchematicConnections();
    bool Register(EditorApplication &, std::string &error);

private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
