#pragma once
#include "application/EditorApplication.h"
#include "features/layout/LayoutViewport.h"
#include <memory>
namespace vdev {
class LayoutView;
class ObjectSelection;
class LayoutGestures {
public:
  LayoutGestures(std::function<Document *()>, LayoutTargetLookup, ObjectSelection &);
  ~LayoutGestures();
  bool Register(EditorApplication &, std::string &error);
  void Poll(EditorApplication &);

private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
