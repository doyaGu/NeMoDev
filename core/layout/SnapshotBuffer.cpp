#include "core/layout/SnapshotBuffer.h"

#include <algorithm>
#include <utility>

namespace vdev {

const std::vector<unsigned char> &SnapshotBuffer::Pixels(int slot) const {
    static const std::vector<unsigned char> none;
    return slot >= 0 && slot < COUNT ? m_Slots[slot] : none;
}

int SnapshotBuffer::FirstFree() const {
    for (int i = 0; i < COUNT; ++i)
        if (m_Slots[i].empty()) return i;
    return -1;
}

int SnapshotBuffer::Put(std::vector<unsigned char> pixels, int slot) {
    if (pixels.size() != static_cast<size_t>(WIDTH) * HEIGHT * 4) return -1;
    const int at = slot >= 0 && slot < COUNT && m_Slots[slot].empty() ? slot : FirstFree();
    if (at < 0) return -1;
    m_Slots[at] = std::move(pixels);
    ++m_Revision[at];
    return at;
}

void SnapshotBuffer::Remove(int slot) {
    if (!Has(slot)) return;
    m_Slots[slot].clear();
    ++m_Revision[slot];
}

std::vector<unsigned char> ShrinkImage(const unsigned char *bgra, int width, int height, int pitch, int toWidth,
                                       int toHeight) {
    std::vector<unsigned char> out(static_cast<size_t>(std::max(0, toWidth)) * std::max(0, toHeight) * 4, 0);
    if (!bgra || width <= 0 || height <= 0 || toWidth <= 0 || toHeight <= 0) return out;
    for (int y = 0; y < toHeight; ++y) {
        const int y0 = y * height / toHeight, y1 = std::max(y0 + 1, (y + 1) * height / toHeight);
        for (int x = 0; x < toWidth; ++x) {
            const int x0 = x * width / toWidth, x1 = std::max(x0 + 1, (x + 1) * width / toWidth);
            unsigned sum[3] = {};
            for (int sy = y0; sy < y1 && sy < height; ++sy) {
                const unsigned char *row = bgra + static_cast<size_t>(sy) * pitch;
                for (int sx = x0; sx < x1 && sx < width; ++sx)
                    for (int c = 0; c < 3; ++c) sum[c] += row[sx * 4 + c];
            }
            const unsigned count = static_cast<unsigned>((std::min(y1, height) - y0) * (std::min(x1, width) - x0));
            unsigned char *pixel = &out[(static_cast<size_t>(y) * toWidth + x) * 4];
            for (int c = 0; c < 3; ++c) pixel[c] = static_cast<unsigned char>(count ? sum[c] / count : 0);
            pixel[3] = 255;
        }
    }
    return out;
}

std::vector<unsigned char> CaptureArea(const unsigned char *bgra, int width, int height, int pitch, int x, int y) {
    const int w = SnapshotBuffer::WIDTH, h = SnapshotBuffer::HEIGHT;
    std::vector<unsigned char> out(static_cast<size_t>(w) * h * 4);
    auto at = [&](int px, int py, int c) -> unsigned {
        if (!bgra || px < 0 || py < 0 || px >= width || py >= height) return 255;
        return bgra[static_cast<size_t>(py) * pitch + px * 4 + c];
    };
    for (int row = 0; row < h; ++row)
        for (int column = 0; column < w; ++column) {
            const int px = x + 2 * column, py = y + 2 * row;
            unsigned char *pixel = &out[(static_cast<size_t>(row) * w + column) * 4];
            for (int c = 0; c < 3; ++c)
                pixel[c] = static_cast<unsigned char>(
                    (at(px, py, c) + at(px + 1, py, c) + at(px + 1, py + 1, c) + at(px, py + 1, c)) >> 2);
            pixel[3] = 255;
        }
    return out;
}

} // namespace vdev
