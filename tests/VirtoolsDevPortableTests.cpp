#include "core/runtime/TextEncoding.h"
#include "VxWindowFunctions.h"
#include "VxImageDescEx.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <cstdio>
#include <clocale>
#include "VxMemoryMappedFile.h"
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
struct Bitmap {
    BITMAP_HANDLE handle;
    explicit Bitmap(const VxImageDescEx &desc) : handle(VxCreateBitmap(desc)) { Require(handle != nullptr, "create bitmap"); }
    ~Bitmap() { VxDeleteBitmap(handle); }
};
VxImageDescEx Description(int width, int height, void *pixels) {
    VxImageDescEx desc;
    desc.Width = width; desc.Height = height; desc.BytesPerLine = width * 4;
    desc.BitsPerPixel = 32; desc.Image = static_cast<XBYTE *>(pixels);
    desc.RedMask = 0xff0000; desc.GreenMask = 0xff00; desc.BlueMask = 0xff; desc.AlphaMask = 0xff000000;
    return desc;
}
std::vector<XDWORD> Pixels(BITMAP_HANDLE bitmap) {
    VxImageDescEx desc;
    std::unique_ptr<XBYTE[]> copy(VxConvertBitmap(bitmap, desc));
    Require(copy && desc.Image == copy.get(), "owned bitmap pixels");
    Require(desc.BitsPerPixel == 32 && desc.RedMask == 0xff0000 && desc.GreenMask == 0xff00 &&
            desc.BlueMask == 0xff && desc.AlphaMask == 0xff000000, "bitmap color masks");
    std::vector<XDWORD> pixels(static_cast<size_t>(desc.Width) * desc.Height);
    for (int y = 0; y < desc.Height; ++y)
        std::memcpy(pixels.data() + y * desc.Width, desc.Image + y * desc.BytesPerLine, desc.Width * 4);
    return pixels;
}
void BitmapChecks() {
    XDWORD source[] = {0xff102030, 0xffabcdef, 0xff334455, 0xffa0b0c0, 0xff778899, 0xfffedcba};
    auto desc = Description(2, 3, source);
    auto bitmap = std::make_unique<Bitmap>(desc);
    const auto first = Pixels(bitmap->handle);
    Require(std::equal(first.begin(), first.end(), source), "bitmap rows and colors survive create/convert");
    VxImageDescEx converted;
    std::unique_ptr<XBYTE[]> independent(VxConvertBitmap(bitmap->handle, converted));
    Require(independent != nullptr, "second conversion");
    independent[0] ^= 255;
    Require(Pixels(bitmap->handle) == first, "converted pixels are independent of bitmap");
    std::vector<XDWORD> copied(6);
    auto destination = Description(2, 3, copied.data());
    Require(VxCopyBitmap(bitmap->handle, destination) && copied == first, "copy goes from bitmap to caller");
    bitmap.reset();
    Require(independent[4] == reinterpret_cast<XBYTE *>(source)[4], "converted pixels survive bitmap destruction");
}
struct Bounds { int left = 640, top = 160, right = -1, bottom = -1; size_t ink = 0; };
Bounds InkBounds(const std::vector<XDWORD> &pixels, int width) {
    Bounds bounds;
    for (size_t i = 0; i < pixels.size(); ++i) {
        if ((pixels[i] & 0xffffff) == 0xffffff) continue;
        const int x = static_cast<int>(i % width), y = static_cast<int>(i / width);
        bounds.left = std::min(bounds.left, x); bounds.right = std::max(bounds.right, x);
        bounds.top = std::min(bounds.top, y); bounds.bottom = std::max(bounds.bottom, y); ++bounds.ink;
    }
    return bounds;
}
std::vector<XDWORD> Draw(const char *text, int weight, bool italic, bool underline, XDWORD alignment) {
    const int width = 640, height = 160;
    std::vector<XDWORD> source(width * height, 0xffffffff);
    const auto desc = Description(width, height, source.data());
    Bitmap bitmap(desc);
    FONT_HANDLE font = VxCreateFont("Noto Sans", -32, weight, italic, underline);
    Require(font != nullptr, "create font");
    struct Release { FONT_HANDLE font; ~Release() { VxDeleteFont(font); } } release{font};
    VXFONTINFO info;
    Require(VxGetFontInfo(font, info) && info.Height == 32 && info.Weight == weight &&
            bool(info.Italic) == italic && bool(info.Underline) == underline, "font size and style metadata");
    CKRECT rect{20, 10, 620, 150};
    Require(VxDrawBitmapText(bitmap.handle, font, text, &rect, alignment, 0xffffff, 0x0000ff), "draw font text");
    return Pixels(bitmap.handle);
}
void FontChecks() {
    const auto regular = Draw("Ag Test", 400, false, false, VXTEXT_LEFT | VXTEXT_TOP);
    const Bounds topLeft = InkBounds(regular, 640);
    Require(topLeft.ink > 80 && topLeft.left >= 20 && topLeft.top >= 10, "font produces visible pixels within rectangle");
    bool red = false;
    for (XDWORD pixel : regular) {
        Require((pixel & 255) == ((pixel >> 8) & 255), "font red color preserves equal green and blue");
        red = red || ((pixel >> 16) & 255) > (pixel & 255);
    }
    Require(red, "font color is red");
    const Bounds center = InkBounds(Draw("Ag Test", 400, false, false, VXTEXT_HCENTER | VXTEXT_VCENTER), 640);
    const Bounds bottomRight = InkBounds(Draw("Ag Test", 400, false, false, VXTEXT_RIGHT | VXTEXT_BOTTOM), 640);
    Require(center.left > topLeft.left && bottomRight.left > center.left && center.top > topLeft.top &&
            bottomRight.top > center.top && bottomRight.right < 620 && bottomRight.bottom < 150, "horizontal and vertical font alignment");
    Require(center.ink == topLeft.ink && bottomRight.ink == topLeft.ink, "alignment retains glyph pixels");
    const auto bold = Draw("Ag Test", 700, false, false, VXTEXT_LEFT | VXTEXT_TOP);
    const auto italic = Draw("Ag Test", 400, true, false, VXTEXT_LEFT | VXTEXT_TOP);
    const auto underline = Draw("Ag Test", 400, false, true, VXTEXT_LEFT | VXTEXT_TOP);
    Require(bold != regular && italic != regular && underline != regular, "bold italic and underline alter pixels");
    Require(InkBounds(bold, 640).ink > topLeft.ink && InkBounds(underline, 640).ink > topLeft.ink, "bold and underline add ink");
    std::string native;
    try { native = vdev::Utf8ToAnsi(u8"\u4E2D\u6587"); }
    catch (const vdev::TextEncodingError &) { std::cout << "The native character set cannot represent the CJK sample\n"; }
    std::vector<XDWORD> chinese;
    if (!native.empty()) {
        chinese = Draw(native.c_str(), 400, false, false, VXTEXT_LEFT | VXTEXT_TOP);
        Require(InkBounds(chinese, 640).ink > 100 && chinese != regular, "Chinese glyph fallback produces pixels");
    }
    std::vector<XDWORD> blank(640 * 160, 0xffffffff);
    Bitmap bitmap(Description(640, 160, blank.data()));
    FONT_HANDLE first = VxCreateFont("Noto Sans", -32, 400, FALSE, FALSE);
    FONT_HANDLE second = VxCreateFont("Noto Sans", -32, 400, FALSE, FALSE);
    struct Release { FONT_HANDLE font; ~Release() { VxDeleteFont(font); } } releaseFirst{first}, releaseSecond{second};
    Require(first && second, "two fonts may share loaded faces");
    VxDeleteFont(first);
    releaseFirst.font = nullptr;
    CKRECT rect{20, 10, 620, 150};
    Require(VxDrawBitmapText(bitmap.handle, second, native.empty() ? "Ag Test" : native.c_str(), &rect,
                            VXTEXT_LEFT | VXTEXT_TOP, 0xffffff, 0x0000ff), "second font survives first font release");
    Require(Pixels(bitmap.handle) == (native.empty() ? regular : chinese), "shared font data retains glyph pixels");
    for (int i = 0; i < 8; ++i) Require(Draw("Ag Test", 400, false, false, VXTEXT_LEFT | VXTEXT_TOP) == regular, "repeated font creation drawing and release");
}
}
int main() {
    try {
#ifndef _WIN32
        Require(std::setlocale(LC_CTYPE, "") != nullptr, "native test locale");
#endif
        Require(SDL_Init(0), "SDL initialization");
        BitmapChecks(); FontChecks();
        SDL_Quit();
        std::cout << "Bitmap ownership and font pixels passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n'; SDL_Quit(); return 1;
    }
}
