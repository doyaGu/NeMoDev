#pragma once

#include <vector>

namespace vdev {

// Dev's Snapshot Buffer (Dev.exe sub_4C5860): 36 images of 52 x 40 pixels
// (32-bit BGRA rows), kept for the session across compositions.
class SnapshotBuffer {
public:
    static constexpr int COUNT = 36;
    static constexpr int WIDTH = 52;
    static constexpr int HEIGHT = 40;

    bool Has(int slot) const { return slot >= 0 && slot < COUNT && !m_Slots[slot].empty(); }
    // WIDTH x HEIGHT BGRA; empty when the slot holds none.
    const std::vector<unsigned char> &Pixels(int slot) const;
    // Changes with each image put in or removed (textures follow it).
    unsigned Revision(int slot) const { return slot >= 0 && slot < COUNT ? m_Revision[slot] : 0; }
    // The first empty slot (sub_4C59F0), -1 when the buffer is full.
    int FirstFree() const;
    // An image (WIDTH x HEIGHT BGRA) in slot when it is empty, else in the
    // first empty one (sub_4C5970, Load Bitmap on a full slot too). Returns
    // the slot, -1 when full ("No more room for Snapshots").
    int Put(std::vector<unsigned char> pixels, int slot = -1);
    void Remove(int slot);

private:
    std::vector<unsigned char> m_Slots[COUNT];
    unsigned m_Revision[COUNT] = {};
};

// Take (Dev.exe sub_4C5B20): the view stretched to width x height
// (StretchBlt in HALFTONE mode: each pixel the average of the area it covers).
std::vector<unsigned char> ShrinkImage(const unsigned char *bgra, int width, int height, int pitch, int toWidth,
                                       int toHeight);
// Capture (Dev.exe sub_4C5C00): the 104 x 80 pixels from (x, y) of the view,
// each 2 x 2 block averaged into one; pixels outside the view are white
// (GetPixel's CLR_INVALID).
std::vector<unsigned char> CaptureArea(const unsigned char *bgra, int width, int height, int pitch, int x, int y);

} // namespace vdev
