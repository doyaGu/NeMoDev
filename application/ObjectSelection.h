#pragma once

#include "editor/Identity.h"
#include <vector>

namespace vdev {
class Document;
class SelectionCommands;

// The shared object selection. IDs are a derived view for existing rendering
// code; references keep a replaced native slot out of the selection.
class ObjectSelection {
  public:
    bool Locked() const { return m_Locked; }
    unsigned Revision() const { return m_Revision; }
    const std::vector<uint32_t> &Objects() const { return m_Ids; }
    const std::vector<ObjectRef> &References() const { return m_References; }
    bool Contains(uint32_t id) const;
    void Validate(const Document &document);
    void Reset();
    // Document attachment restores saved state as one change.
    void Restore(const Document &document, const std::vector<uint32_t> &ids, bool locked = false);

  private:
    void Set(const Document &document, const std::vector<uint32_t> &ids);
    bool m_Locked = false;
    unsigned m_Revision = 0;
    std::vector<ObjectRef> m_References;
    std::vector<uint32_t> m_Ids;
    friend class SelectionCommands;
};
} // namespace vdev
