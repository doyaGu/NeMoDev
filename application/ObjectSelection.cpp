#include "application/ObjectSelection.h"
#include "core/document/Document.h"
#include <algorithm>
#include <unordered_set>

namespace vdev {
bool ObjectSelection::Contains(uint32_t id) const { return std::find(m_Ids.begin(), m_Ids.end(), id) != m_Ids.end(); }
void ObjectSelection::Set(const Document &document, const std::vector<uint32_t> &ids) {
    std::vector<ObjectRef> references;
    std::vector<uint32_t> live;
    references.reserve(ids.size());
    live.reserve(ids.size());
    std::unordered_set<uint32_t> included;
    included.reserve(ids.size());
    for (auto id : ids) {
        const auto ref = document.Reference(id);
        if (!document.Resolve(ref) || !included.insert(id).second)
            continue;
        references.push_back(ref);
        live.push_back(id);
    }
    m_References = std::move(references);
    m_Ids = std::move(live);
    if (m_Ids.empty())
        m_Locked = false;
    ++m_Revision;
}
void ObjectSelection::Restore(const Document &document, const std::vector<uint32_t> &ids, bool locked) {
    Set(document, ids);
    m_Locked = locked && !m_Ids.empty();
}
void ObjectSelection::Reset() {
    m_References.clear();
    m_Ids.clear();
    m_Locked = false;
    ++m_Revision;
}
void ObjectSelection::Validate(const Document &document) {
    size_t live = 0;
    for (ObjectRef ref : m_References) {
        const auto id = document.NativeId(ref);
        if (!id)
            continue;
        m_References[live] = ref;
        m_Ids[live++] = id;
    }
    if (live == m_References.size())
        return;
    m_References.resize(live);
    m_Ids.resize(live);
    if (m_Ids.empty())
        m_Locked = false;
    ++m_Revision;
}
} // namespace vdev
