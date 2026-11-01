#pragma once
#include "application/EditorApplication.h"
#include <memory>

namespace vdev {
class LayoutView;
class ObjectSelection;
class Preferences;
class SchematicNavigation;

// Creation settings, drop receivers and the pending behavior's parameter edit.
// The native value editor joins that edit through EditorApplication::ApplyEdit.
class LayoutAuthoring {
  public:
    LayoutAuthoring(EditorApplication &, std::function<Document *()>, std::function<LayoutView &()>, ObjectSelection &,
                    const Preferences &, std::function<SchematicNavigation &()>);
    ~LayoutAuthoring();
    bool Register(std::string &error);
    void DocumentClosing();
    void Poll();
    static const std::vector<ProviderChoice> &DefaultProviders();

    std::function<void(ObjectRef script, ObjectRef block)> showBehavior;
    std::function<void(uint64_t prototype)> rememberBehavior;

  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
