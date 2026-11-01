#pragma once
#include "application/EditorApplication.h"
#include <memory>
namespace vdev {
class LayoutView;
class ObjectSelection;
class LayoutGestures {
public:
    LayoutGestures(std::function<Document *()>, std::function<LayoutView &()>, ObjectSelection &);
    ~LayoutGestures();
    bool Register(EditorApplication &, std::string &error);

private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
