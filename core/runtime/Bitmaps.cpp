#include "core/runtime/Bitmaps.h"
#include "CKAll.h"
#include <cstring>
#include <memory>
#include <limits>
namespace vdev {
void *BitmapFromPixels(const void *bgra, int width, int height) {
    if (!bgra || width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() / 4)
        return nullptr;
    VxImageDescEx image;
    image.Width = width;
    image.Height = height;
    image.BitsPerPixel = 32;
    image.BytesPerLine = width * 4;
    image.RedMask = 0x00ff0000;
    image.GreenMask = 0x0000ff00;
    image.BlueMask = 0x000000ff;
    image.AlphaMask = 0xff000000;
    image.Image = const_cast<XBYTE *>(static_cast<const XBYTE *>(bgra));
    return VxCreateBitmap(image);
}
bool PixelsOfBitmap(void *bitmap, std::vector<unsigned char> &bgra, int &width, int &height) {
    if (!bitmap)
        return false;
    VxImageDescEx image;
    std::unique_ptr<XBYTE[]> copy(VxConvertBitmap(bitmap, image));
    if (!copy || image.Width <= 0 || image.Height <= 0 || image.BitsPerPixel != 32 ||
        image.Width > std::numeric_limits<int>::max() / 4 || image.BytesPerLine < image.Width * 4)
        return false;
    width = image.Width;
    height = image.Height;
    bgra.resize(static_cast<size_t>(width) * height * 4);
    for (int row = 0; row < height; ++row)
        std::memcpy(bgra.data() + static_cast<size_t>(row) * width * 4,
                    copy.get() + static_cast<size_t>(row) * image.BytesPerLine, static_cast<size_t>(width) * 4);
    for (size_t i = 3; i < bgra.size(); i += 4)
        bgra[i] = 255;
    return true;
}
} // namespace vdev
