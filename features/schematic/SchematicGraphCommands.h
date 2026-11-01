#pragma once
#include "application/EditorApplication.h"
#include <memory>

namespace vdev {
struct InvalidLink;
class Clipboard;
class SchematicGraphCommands {
public:
    SchematicGraphCommands(std::function<Document *()>, std::function<bool()> testParameterTypes, Clipboard &);
    ~SchematicGraphCommands();
    bool Register(EditorApplication &, std::string &error);
    void LoadedLinks(const std::vector<InvalidLink> &);
    static const std::vector<ProviderChoice> &DefaultProviders();
private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
