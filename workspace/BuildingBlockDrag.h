#pragma once
#include "editor/Resources.h"
#include <cstring>
#include <vector>
#include <utility>

namespace vdev {
inline std::vector<unsigned char> WriteBuildingBlockDrag(EditorBuildingBlock source,
                                                         const std::vector<EditorBuildingBlock> &blocks) {
    if (blocks.empty() || blocks.size() > 65536)
        return {};
    const EditorBuildingBlockDragHeader header{source, uint32_t(blocks.size())};
    std::vector<unsigned char> bytes(sizeof(header) + blocks.size() * sizeof(blocks[0]));
    std::memcpy(bytes.data(), &header, sizeof(header));
    std::memcpy(bytes.data() + sizeof(header), blocks.data(), blocks.size() * sizeof(blocks[0]));
    return bytes;
}
inline bool ReadBuildingBlockDrag(const void *bytes, size_t size, EditorBuildingBlock &source,
                                  std::vector<EditorBuildingBlock> &blocks) {
    if (!bytes || size < sizeof(EditorBuildingBlockDragHeader))
        return false;
    EditorBuildingBlockDragHeader header;
    std::memcpy(&header, bytes, sizeof(header));
    if (!header.count || header.count > 65536 ||
        size != sizeof(header) + size_t(header.count) * sizeof(EditorBuildingBlock))
        return false;
    std::vector<EditorBuildingBlock> next(header.count);
    std::memcpy(next.data(), static_cast<const unsigned char *>(bytes) + sizeof(header), next.size() * sizeof(next[0]));
    bool found = false;
    for (const auto &block : next)
        found |= block.first == header.source.first && block.second == header.source.second;
    if (!found)
        return false;
    source = header.source;
    blocks = std::move(next);
    return true;
}
} // namespace vdev
