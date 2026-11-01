#pragma once
#include "application/EditorApplication.h"
#include <memory>
namespace vdev {
class Clipboard;
class SchematicCopying {
public:
    SchematicCopying(std::function<Document *()>, Clipboard &);
    ~SchematicCopying();
    bool Register(EditorApplication &, std::string &error);

private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
