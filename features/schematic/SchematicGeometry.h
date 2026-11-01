#pragma once
#include "application/EditorApplication.h"
#include <memory>
namespace vdev {
// Continuous display edits share the command host's interaction lifetime.
class SchematicGeometry {
public:
    explicit SchematicGeometry(std::function<Document *()>);
    ~SchematicGeometry();
    bool Register(EditorApplication &, std::string &error);

private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
