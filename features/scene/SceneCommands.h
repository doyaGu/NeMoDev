#pragma once
#include "application/EditorApplication.h"
#include "core/document/Clipboard.h"
#include "core/schematic/SchematicNavigation.h"
#include "features/scene/SceneModel.h"
#include <functional>
#include <memory>
#include <set>

namespace vdev {
class SceneCommands {
  public:
    SceneCommands(EditorApplication &, std::function<Document *()>, ObjectClipboard &, Clipboard &,
                  std::function<SchematicNavigation &()> navigation, std::function<CKDependencies &()> copyOptions,
                  std::function<bool()> showDynamic = {});
    ~SceneCommands();
    bool Register(std::string &error);
    // Only published editor-thread values are used by version callbacks.
    void Publish(uint64_t documentRevision, int runtimeFrame, int runtimeState, bool dynamic);
    void RefreshCatalog();
    void Stop();

  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
    std::function<bool()> m_ShowDynamic;
    void Poll();
};
} // namespace vdev
