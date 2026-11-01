#pragma once
#include "application/EditorApplication.h"
#include <memory>
namespace vdev {
class LayoutViewport;
class RuntimeOperation;
class SaveFileOperations;
class SnapshotCommands {
  public:
    SnapshotCommands(EditorApplication &, RuntimeOperation &, LayoutViewport &, std::function<Document *()>);
    ~SnapshotCommands();
    bool Register(std::string &error);
    void Poll();
    void Stop();
    static const std::vector<ProviderChoice> &DefaultProviders();
    void FileOperations(SaveFileOperations *);

  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
