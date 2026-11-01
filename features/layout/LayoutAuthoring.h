#pragma once
#include "application/EditorApplication.h"
#include <memory>
namespace vdev {
class LayoutView;
class ObjectSelection;
class Preferences;
// Copied creation settings and scene receivers for Layout tools.
class LayoutAuthoring {
  public:
    LayoutAuthoring(EditorApplication &, std::function<Document *()>, std::function<LayoutView &()>, ObjectSelection &,
                    const Preferences &);
    ~LayoutAuthoring();
    bool Register(std::string &error);
    static const std::vector<ProviderChoice> &DefaultProviders();

  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
