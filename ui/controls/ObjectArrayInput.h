#pragma once
#include "workspace/EditorClient.h"
#include <set>
namespace vdev {
// Per-widget selection and chooser state. The value and clipboard contain
// object references; this control never owns or resolves native objects.
class ObjectArrayInput {
  public:
    uint32_t Draw(const EditorHost *, const EditorValueInput &);

  private:
    std::set<int> m_Chosen;
    int m_Anchor = -1;
    EditorField m_Object{"value", EDITOR_OBJECT};
    std::vector<EditorField> m_Classes;
    uint64_t m_Class = 0;
    bool m_Derived = true;
    std::string m_Filter;
};
} // namespace vdev
