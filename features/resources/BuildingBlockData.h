#pragma once
#include "workspace/EditorClient.h"
#include <map>
#include <algorithm>
#include <set>

namespace vdev {
struct BuildingBlockInfo {
    uint64_t prototype = 0;
    uint32_t version = 0;
    bool targetable = false;
    std::string name, category, description, author, compatible;
};
struct BuildingBlockCategories {
    std::map<std::string, BuildingBlockCategories> children;
    std::vector<size_t> entries;
};
// Resource panels and creation menus read the same paginated catalog. A failed
// or mixed-version read leaves the last complete catalog in place.
class BuildingBlockData {
  public:
    EditorStatus Refresh(const EditorClient &client) {
        const auto version = client.Revision("catalog.building-blocks");
        if (!version)
            return EDITOR_REFUSED;
        if (version == m_SourceVersion)
            return EDITOR_OK;
        std::vector<BuildingBlockInfo> next;
        uint64_t first = 0, total = 0, revision = 0;
        std::set<uint64_t> prototypesSeen;
        do {
            const auto page = client.Query("catalog.building-blocks", {IntegerArgument("first", first)});
            if (page.status != EDITOR_OK)
                return page.status;
            if (!first) {
                revision = EditorClient::Integer(page, "revision");
                total = EditorClient::Integer(page, "total");
            }
            if (EditorClient::Integer(page, "revision") != revision)
                return EDITOR_STALE;
            if (EditorClient::Integer(page, "total") != total) return EDITOR_STALE;
            const auto *names = EditorClient::Find(page, "names"), *paths = EditorClient::Find(page, "categories"),
                       *descriptions = EditorClient::Find(page, "descriptions"),
                       *authors = EditorClient::Find(page, "authors"),
                       *compatible = EditorClient::Find(page, "compatible"),
                       *prototypes = EditorClient::Find(page, "prototypes"),
                       *versions = EditorClient::Find(page, "versions"),
                       *targetable = EditorClient::Find(page, "targetable");
            if (!names || !paths || !descriptions || !authors || !compatible || !prototypes || !versions || !targetable)
                return EDITOR_INVALID;
            const auto count = names->texts.size();
            if (paths->texts.size() != count || descriptions->texts.size() != count || authors->texts.size() != count ||
                compatible->texts.size() != count || prototypes->integers.size() != count ||
                versions->integers.size() != count || targetable->integers.size() != count || first + count > total ||
                total > 1000000 || (!count && first < total))
                return EDITOR_INVALID;
            for (size_t i = 0; i < count; ++i) {
                if (!prototypesSeen.insert(prototypes->integers[i]).second) return EDITOR_INVALID;
                next.push_back({prototypes->integers[i], uint32_t(versions->integers[i]), targetable->integers[i] != 0,
                                names->texts[i], paths->texts[i], descriptions->texts[i], authors->texts[i],
                                compatible->texts[i]});
            }
            first += count;
        } while (first < total);
        if (client.Revision("catalog.building-blocks") != version) return EDITOR_STALE;
        BuildingBlockCategories tree;
        for (size_t i = 0; i < next.size(); ++i) {
            auto *node = &tree;
            const auto &path = next[i].category;
            for (size_t start = 0; start <= path.size();) {
                const auto end = std::min(path.find('/', start), path.size());
                const auto part = path.substr(start, end - start);
                node = &node->children[part.empty() ? "(Uncategorized)" : part];
                start = end + 1;
            }
            node->entries.push_back(i);
        }
        m_Entries = std::move(next);
        m_Categories = std::move(tree);
        m_Revision = revision;
        m_SourceVersion = version;
        return EDITOR_OK;
    }
    uint64_t Revision() const { return m_Revision; }
    uint64_t SourceVersion() const { return m_SourceVersion; }
    const std::vector<BuildingBlockInfo> &Entries() const { return m_Entries; }
    const BuildingBlockCategories &Categories() const { return m_Categories; }
    const BuildingBlockInfo *Find(uint64_t prototype) const {
        for (const auto &entry : m_Entries)
            if (entry.prototype == prototype)
                return &entry;
        return nullptr;
    }

  private:
    uint64_t m_Revision = 0, m_SourceVersion = 0;
    std::vector<BuildingBlockInfo> m_Entries;
    BuildingBlockCategories m_Categories;
};
} // namespace vdev
