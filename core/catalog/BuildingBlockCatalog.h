#pragma once

#include <map>
#include <string>
#include <vector>

#include "CKAll.h"

namespace vdev {

struct BuildingBlockEntry {
    CKGUID guid;
    std::string name;
    std::string category;    // "Logics/Calculator"
    std::string description;
    std::string author;
    CKDWORD version = 0;     // the declaration's
    bool targetable = false; // CKBEHAVIOR_TARGETABLE (Dev's "T" column)
    CK_CLASSID compatibleClass = 0;
    std::string compatibleClassName;
};

// The category paths as a tree ("Logics/Calculator": Logics, then
// Calculator; an empty part is "(Uncategorized)"), the entries at each node.
struct BuildingBlockCategory {
    std::map<std::string, BuildingBlockCategory> children;
    std::vector<size_t> entries;
};

// Behavior prototype declarations registered by the BuildingBlocks DLLs,
// grouped by category path as in Dev's Building Blocks resource tree.
class BuildingBlockCatalog {
public:
    void Refresh();
    const std::vector<BuildingBlockEntry> &Entries() const { return m_Entries; }
    // Category path -> entry indices, both sorted by name.
    const std::map<std::string, std::vector<size_t>> &Categories() const { return m_Categories; }
    // The same as a tree (the Building Blocks panel, the Schematic's menus).
    const BuildingBlockCategory &CategoryTree() const { return m_Tree; }
    const BuildingBlockEntry *Find(const CKGUID &guid) const;

private:
    std::vector<BuildingBlockEntry> m_Entries;
    std::map<std::string, std::vector<size_t>> m_Categories;
    BuildingBlockCategory m_Tree;
};

} // namespace vdev
