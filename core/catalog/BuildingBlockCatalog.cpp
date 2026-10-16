#include "core/catalog/BuildingBlockCatalog.h"

#include <algorithm>

namespace vdev {

namespace {

std::string Text(const char *value) {
    return value ? value : std::string();
}

} // namespace

void BuildingBlockCatalog::Refresh() {
    m_Entries.clear();
    m_Categories.clear();
    const int count = CKGetPrototypeDeclarationCount();
    for (int i = 0; i < count; ++i) {
        CKObjectDeclaration *declaration = CKGetPrototypeDeclaration(i);
        if (!declaration || declaration->GetType() != CKDLL_BEHAVIORPROTOTYPE) continue;
        BuildingBlockEntry entry;
        entry.guid = declaration->GetGuid();
        entry.name = Text(declaration->GetName());
        entry.category = Text(declaration->GetCategory());
        entry.description = Text(declaration->GetDescription());
        entry.author = Text(declaration->GetAuthorName());
        entry.version = declaration->GetVersion();
        if (CKBehaviorPrototype *prototype = CKGetPrototypeFromGuid(entry.guid))
            entry.targetable = (prototype->GetBehaviorFlags() & CKBEHAVIOR_TARGETABLE) != 0;
        entry.compatibleClass = declaration->GetCompatibleClassId();
        entry.compatibleClassName = Text(CKClassIDToString(entry.compatibleClass));
        if (entry.category.empty()) entry.category = "(Uncategorized)";
        m_Entries.push_back(entry);
    }
    std::sort(m_Entries.begin(), m_Entries.end(), [](const BuildingBlockEntry &a, const BuildingBlockEntry &b) {
        return a.category != b.category ? a.category < b.category : a.name < b.name;
    });
    for (size_t i = 0; i < m_Entries.size(); ++i) m_Categories[m_Entries[i].category].push_back(i);
    m_Tree = BuildingBlockCategory();
    for (const auto &category : m_Categories) {
        BuildingBlockCategory *node = &m_Tree;
        const std::string &path = category.first;
        for (size_t start = 0; start <= path.size();) {
            const size_t slash = std::min(path.find('/', start), path.size());
            const std::string part = path.substr(start, slash - start);
            node = &node->children[part.empty() ? "(Uncategorized)" : part];
            start = slash + 1;
        }
        node->entries.insert(node->entries.end(), category.second.begin(), category.second.end());
    }
}

const BuildingBlockEntry *BuildingBlockCatalog::Find(const CKGUID &guid) const {
    for (const auto &entry : m_Entries)
        if (entry.guid == guid) return &entry;
    return nullptr;
}

} // namespace vdev
