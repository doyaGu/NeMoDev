#pragma once
#include "editor/Resources.h"
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace vdev {
struct ResourceFile {
    std::string path;
    uint32_t cid = 0;
};
inline std::vector<unsigned char> WriteResourceDrag(uint32_t source, const std::vector<ResourceFile> &files) {
    if (files.empty() || files.size() > 4096 || source >= files.size())
        return {};
    size_t size = sizeof(EditorResourceDragHeader);
    for (const auto &file : files) {
        if (file.path.empty() || file.path.find('\0') != std::string::npos || file.path.size() > 1048576 ||
            file.cid > INT32_MAX)
            return {};
        size += sizeof(EditorResourceFileHeader) + file.path.size();
        if (size > 1048576)
            return {};
    }
    std::vector<unsigned char> bytes(size);
    const EditorResourceDragHeader header{source, uint32_t(files.size())};
    std::memcpy(bytes.data(), &header, sizeof(header));
    size_t offset = sizeof(header);
    for (const auto &file : files) {
        const EditorResourceFileHeader entry{file.cid, uint32_t(file.path.size())};
        std::memcpy(bytes.data() + offset, &entry, sizeof(entry));
        offset += sizeof(entry);
        std::memcpy(bytes.data() + offset, file.path.data(), file.path.size());
        offset += file.path.size();
    }
    return bytes;
}
inline bool ReadResourceDrag(const void *bytes, size_t size, uint32_t &source, std::vector<ResourceFile> &files) {
    if (!bytes || size < sizeof(EditorResourceDragHeader) || size > 1048576)
        return false;
    EditorResourceDragHeader header;
    std::memcpy(&header, bytes, sizeof(header));
    if (!header.count || header.count > 4096 || header.source >= header.count)
        return false;
    std::vector<ResourceFile> next;
    size_t offset = sizeof(header);
    for (uint32_t i = 0; i < header.count; ++i) {
        if (size - offset < sizeof(EditorResourceFileHeader))
            return false;
        EditorResourceFileHeader entry;
        std::memcpy(&entry, static_cast<const unsigned char *>(bytes) + offset, sizeof(entry));
        offset += sizeof(entry);
        if (!entry.pathSize || entry.pathSize > size - offset || entry.classId > INT32_MAX)
            return false;
        std::string path(static_cast<const char *>(bytes) + offset, entry.pathSize);
        if (path.find('\0') != std::string::npos)
            return false;
        next.push_back({std::move(path), entry.classId});
        offset += entry.pathSize;
    }
    if (offset != size)
        return false;
    source = header.source;
    files = std::move(next);
    return true;
}
} // namespace vdev
