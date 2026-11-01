#pragma once
#include <vector>
namespace vdev {
// Top-down, opaque BGRA rows. The caller releases a returned SDK bitmap.
void *BitmapFromPixels(const void *bgra, int width, int height);
bool PixelsOfBitmap(void *bitmap, std::vector<unsigned char> &bgra, int &width, int &height);
} // namespace vdev
