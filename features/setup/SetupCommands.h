#pragma once
#include "application/EditorApplication.h"
namespace vdev {
class SetupCommands {
public:
    explicit SetupCommands(std::function<Document *()> current);
    ~SetupCommands();
    bool Register(EditorApplication &, std::string &error);
    static const std::vector<ProviderChoice> &DefaultProviders();

private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
