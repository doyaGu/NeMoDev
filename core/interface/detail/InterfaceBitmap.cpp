#include "core/interface/detail/InterfaceBitmap.h"
#include "VxWindowFunctions.h"

#include <memory>
#include <cstring>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace {
bool ReadPixels(void *bitmap, int &width, int &height, std::vector<CKDWORD> &pixels) {
    if (!bitmap) return false;
    VxImageDescEx description;
    std::unique_ptr<XBYTE[]> image(VxConvertBitmap(bitmap, description));
    if (!image || description.Width <= 0 || description.Height <= 0 ||
        description.Width > 8192 || description.Height > 8192 || description.BitsPerPixel != 32)
        return false;
    width = description.Width;
    height = description.Height;
    pixels.resize(static_cast<size_t>(width) * height);
    for (int row = 0; row < height; ++row)
        std::memcpy(pixels.data() + static_cast<size_t>(row) * width,
                    image.get() + static_cast<size_t>(row) * description.BytesPerLine,
                    static_cast<size_t>(width) * sizeof(CKDWORD));
    return true;
}
} // namespace

std::shared_ptr<InterfaceBitmap> InterfaceBitmap::Read(CKStateChunk *chunk) {
    const int start = chunk->GetCurrentPos();
    // GetDataSize is bytes; GetCurrentPos and LockReadBuffer use DWORDs.
    const int available = chunk->GetDataSize() / static_cast<int>(sizeof(CKDWORD)) - start;
    if (available < 2) return nullptr;
    const CKDWORD *data = static_cast<const CKDWORD *>(chunk->LockReadBuffer());
    if (!data) return nullptr;
    const int length = static_cast<int>(data[1]);
    // CK2 2.5 ReadBitmap reads a sequence int, then a size-prefixed buffer;
    // it copies five bytes before checking the signature or TGA fallback.
    if (length < 0 || (length > 0 && length < 5)) return nullptr;
    const uint64_t payloadWords = (static_cast<uint64_t>(length) + 3) / 4;
    if (payloadWords > static_cast<uint64_t>(available - 2)) return nullptr;
    const int words = 2 + static_cast<int>(payloadWords);
    auto bitmap = std::make_shared<InterfaceBitmap>();
    bitmap->dwords.assign(data, data + words);
    void *decoded = chunk->ReadBitmap();
    if (decoded) bitmap->handle = std::shared_ptr<void>(decoded, VxDeleteBitmap);
    if (chunk->GetCurrentPos() != start + words) return nullptr;
    if (decoded)
        bitmap->hasPixels = ReadPixels(decoded, bitmap->width, bitmap->height, bitmap->pixels);
    return bitmap;
}

bool InterfaceBitmap::HasDataForSave(void *bitmap, const std::shared_ptr<InterfaceBitmap> &original) {
    if (bitmap) return true;
    if (!original || bitmap != original->GetHandle()) return false;
    // An unavailable decoder leaves a null handle, not an empty encoded image.
    // Also retain noncanonical empty records with a nonzero first DWORD.
    return std::any_of(original->dwords.begin(), original->dwords.end(),
                       [](CKDWORD word) { return word != 0; });
}

void InterfaceBitmap::Write(CKStateChunk *chunk, void *bitmap,
                          const std::shared_ptr<InterfaceBitmap> &original) {
    bool unchanged = false;
    if (original && bitmap == original->GetHandle() && !original->dwords.empty()) {
        if (!bitmap) {
            // Preserve a valid record even if its image reader is unavailable.
            unchanged = true;
        } else if (original->hasPixels) {
            int width = 0, height = 0;
            std::vector<CKDWORD> pixels;
            unchanged = ReadPixels(bitmap, width, height, pixels) &&
                width == original->width && height == original->height && pixels == original->pixels;
        }
    }
    if (unchanged) {
        for (CKDWORD value : original->dwords) chunk->WriteDword(value);
    } else {
        chunk->WriteBitmap(bitmap);
    }
}

std::shared_ptr<InterfaceBitmap> InterfaceBitmap::WithoutEncoding(void *bitmap) const {
    if (!bitmap || bitmap != GetHandle()) return nullptr;
    auto retained = std::make_shared<InterfaceBitmap>();
    retained->handle = handle;
    return retained;
}
