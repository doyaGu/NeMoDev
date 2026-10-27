#pragma once

#include <memory>
#include <vector>

#include "CKStateChunk.h"

// Internal storage for the SDK bitmap record and its decoded handle.
// Public Header/BehaviorData bitmap fields remain source-compatible.
class InterfaceBitmap {
public:
    static std::shared_ptr<InterfaceBitmap> Read(CKStateChunk *chunk);
    static std::shared_ptr<InterfaceBitmap> Retain(const std::shared_ptr<void> &bitmap);
    static void Write(CKStateChunk *chunk, void *bitmap,
                      const std::shared_ptr<InterfaceBitmap> &original);
    static bool HasDataForSave(void *bitmap, const std::shared_ptr<InterfaceBitmap> &original);

    void *GetHandle() const { return handle.get(); }
    std::shared_ptr<void> GetOwner() const { return handle; }
    std::shared_ptr<InterfaceBitmap> WithoutEncoding(void *bitmap) const;

private:
    std::shared_ptr<void> handle;
    std::vector<CKDWORD> dwords;
    std::vector<CKDWORD> pixels;
    int width = 0;
    int height = 0;
    bool hasPixels = false;
};
