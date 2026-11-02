#pragma once
#include "application/EditorApplication.h"
#include "features/layout/LayoutViewport.h"
#include <memory>
namespace vdev {
class LayoutView;
class LayoutViewport;
class ObjectSelection;
class Preferences;
// Player display settings, composition changes and fullscreen commands share
// the same owner. Views receive copied state and cannot change CK objects.
class LayoutPresentation {
  public:
    LayoutPresentation(EditorApplication &, std::function<Document *()>, LayoutTargetLookup, ObjectSelection &,
                       const Preferences &, std::function<bool()> running);
    ~LayoutPresentation();
    bool Register(std::string &error);
    void Poll(EditorView source = 0);
    static const std::vector<ProviderChoice> &DefaultProviders();

  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
