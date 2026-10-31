#pragma once

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

#include "CKAll.h"

namespace vdev::detail {
// State chunks can embed object ID sequences and nested chunks. Keep native
// lifetimes alongside the serialized IDs so replay never resolves a reused ID.
inline void ReadStateReferences(CKContext &context, CKStateChunk &chunk, std::vector<CKObjectHandle> &references,
                                unsigned depth = 0) {
    if (depth > 256)
        throw std::runtime_error("Object state nesting is too deep");
    chunk.StartRead();
    const auto remember = [&] {
        const auto handle = context.GetObjectHandle(chunk.ReadObjectID());
        if (handle.Id && std::none_of(references.begin(), references.end(), [&](auto other) {
                return other.Id == handle.Id && other.Lifetime == handle.Lifetime;
            }))
            references.push_back(handle);
    };
    if (auto *ids = chunk.GetIds())
        for (int i = 0; i < ids->Size; ++i) {
            if (ids->Data[i] >= 0) {
                chunk.Goto(ids->Data[i]);
                remember();
            } else if (++i < ids->Size) {
                chunk.Goto(ids->Data[i]);
                const int count = chunk.StartReadSequence();
                for (int j = 0; j < count; ++j)
                    remember();
            }
        }
    if (auto *chunks = chunk.GetChunks())
        for (int i = 0; i < chunks->Size; ++i) {
            int count = 1;
            if (chunks->Data[i] >= 0)
                chunk.Goto(chunks->Data[i]);
            else {
                if (++i >= chunks->Size)
                    break;
                chunk.Goto(chunks->Data[i]);
                count = chunk.StartReadSequence();
            }
            for (int j = 0; j < count; ++j) {
                std::unique_ptr<CKStateChunk, decltype(&DeleteCKStateChunk)> child(chunk.ReadSubChunk(),
                                                                                   DeleteCKStateChunk);
                if (child)
                    ReadStateReferences(context, *child, references, depth + 1);
            }
        }
}
} // namespace vdev::detail
