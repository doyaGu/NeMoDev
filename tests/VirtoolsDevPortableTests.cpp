#include "core/runtime/TextEncoding.h"
#include "VxWindowFunctions.h"
#include "VxImageDescEx.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <cstdio>
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
void EncodingChecks() {
    using namespace vdev;
    const std::string previous = VxGetTextEncoding();
    struct Restore { std::string encoding; ~Restore() { SetLossyTextHandler(nullptr); VxSetTextEncoding(encoding.c_str()); } } restore{previous};
    std::vector<std::string> reports;
    SetLossyTextHandler([&](const std::string &text) { reports.push_back(text); });
    Require(VxSetTextEncoding("windows-936"), "select CP936");
    const std::string chinese = "\xE4\xB8\xAD\xE6\x96\x87";
    const std::string gbk = "\xD6\xD0\xCE\xC4";
    Require(Utf8ToAnsi(chinese) == gbk && AnsiToUtf8(gbk) == chinese, "CP936 round trip");
    const std::string path = chinese + "/saved.cmo";
    Require(WideToUtf8(Utf8ToWide(path)) == path, "Chinese UTF-8 paths do not use legacy conversion");
    const std::string supplementary = "\xF0\x9F\x98\x80";
    Require(WideToUtf8(Utf8ToWide(supplementary)) == supplementary, "wide supplementary character round trip");
    std::string original = gbk;
    bool lossy = false, rejected = false;
    try { original = Utf8ToAnsi(supplementary, &lossy); } catch (const TextEncodingError &) { rejected = true; }
    Require(rejected && lossy && original == gbk && reports.size() == 1, "unencodable write leaves original bytes");
    const std::string invalid(1, '\x81');
    const std::string display = AnsiToUtf8(invalid);
    Require(!display.empty() && invalid == std::string(1, '\x81') && reports.size() == 2, "invalid CK byte is reported for display only");
    Require(VxSetTextEncoding("windows-1252"), "select CP1252");
    const std::string western = "Caf\xC3\xA9 \xE2\x82\xAC";
    Require(Utf8ToAnsi(western) == "Caf\xE9 \x80" && AnsiToUtf8("Caf\xE9 \x80") == western, "CP1252 round trip");
    rejected = false;
    try { Utf8ToWide("\xC0\xAF"); } catch (const TextEncodingError &) { rejected = true; }
    Require(rejected, "reject overlong UTF-8");
    const std::string embedded("A\0*.a\0", 6);
    Require(WideToUtf8(Utf8ToWide(embedded)) == embedded, "embedded NUL round trip");
    Require(!VxSetTextEncoding("does-not-exist") && std::string(VxGetTextEncoding()) == "windows-1252", "invalid encoding leaves selection unchanged");
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
    const std::string previous = VxGetTextEncoding();
    struct Restore { std::string encoding; ~Restore() { VxSetTextEncoding(encoding.c_str()); } } restore{previous};
    Require(VxSetTextEncoding("windows-936"), "font encoding");
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
    const auto chinese = Draw("\xD6\xD0\xCE\xC4", 400, false, false, VXTEXT_LEFT | VXTEXT_TOP);
    Require(InkBounds(chinese, 640).ink > 100 && chinese != regular, "Chinese glyph fallback produces pixels");
    std::vector<XDWORD> blank(640 * 160, 0xffffffff);
    Bitmap bitmap(Description(640, 160, blank.data()));
    FONT_HANDLE first = VxCreateFont("Noto Sans", -32, 400, FALSE, FALSE);
    FONT_HANDLE second = VxCreateFont("Noto Sans", -32, 400, FALSE, FALSE);
    struct Release { FONT_HANDLE font; ~Release() { VxDeleteFont(font); } } releaseFirst{first}, releaseSecond{second};
    Require(first && second, "two fonts may share loaded faces");
    VxDeleteFont(first);
    releaseFirst.font = nullptr;
    CKRECT rect{20, 10, 620, 150};
    Require(VxDrawBitmapText(bitmap.handle, second, "\xD6\xD0\xCE\xC4", &rect,
                            VXTEXT_LEFT | VXTEXT_TOP, 0xffffff, 0x0000ff), "second font survives first font release");
    Require(Pixels(bitmap.handle) == chinese, "shared font data retains glyph pixels");
    const auto before = Pixels(bitmap.handle);
    Require(!VxDrawBitmapText(bitmap.handle, second, "\x81", &rect, VXTEXT_LEFT | VXTEXT_TOP, 0, 0), "invalid encoded text is rejected");
    Require(Pixels(bitmap.handle) == before, "rejected font text leaves bitmap unchanged");
    for (int i = 0; i < 8; ++i) Require(Draw("Ag Test", 400, false, false, VXTEXT_LEFT | VXTEXT_TOP) == regular, "repeated font creation drawing and release");
}
}
int main() {
    try {
        Require(SDL_Init(0), "SDL initialization");
        {
            const auto path = std::filesystem::temp_directory_path() /
                std::filesystem::u8path(std::string("virtoolsdev-") + std::to_string(SDL_GetPerformanceCounter()) +
                                       u8"-\u4e2d\u6587-\u20ac-\U0001f600.bin");
            struct RemovePath {
                std::filesystem::path path;
                ~RemovePath() { std::error_code ignored; std::filesystem::remove(path, ignored); }
            } remove{path};
            FILE *file = VxFOpen(path.u8string().c_str(), "wb");
            Require(file != nullptr, "UTF-8 filename outside CP936 opens for writing");
            const unsigned char expected[] = {0, 0x81, 0xFE, 0xFF};
            const size_t written = std::fwrite(expected, 1, sizeof(expected), file);
            const int closed = std::fclose(file);
            Require(written == sizeof(expected) && closed == 0, "UTF-8 file contents written and closed");
            VxMemoryMappedFile loaded(path.u8string().c_str());
            Require(loaded.IsValid() && loaded.GetFileSize() == sizeof(expected) &&
                    std::memcmp(loaded.GetBase(), expected, sizeof(expected)) == 0, "UTF-8 file reload preserves bytes");
            const auto copied = std::filesystem::u8path(path.u8string() + ".copy");
            RemovePath removeCopy{copied};
            Require(VxCopyFile(path.u8string().c_str(), copied.u8string().c_str(), TRUE) &&
                    !VxCopyFile(path.u8string().c_str(), copied.u8string().c_str(), TRUE), "UTF-8 file copy and overwrite refusal");
            std::vector<std::string> names;
            Require(VxListDirectory(path.parent_path().u8string().c_str(), path.filename().u8string().c_str(), FALSE,
                [](const VxDirectoryEntry *entry, void *data) -> XBOOL {
                    static_cast<std::vector<std::string> *>(data)->emplace_back(entry->Name.CStr());
                    return TRUE;
                }, &names) && names.size() == 1 && names.front() == path.filename().u8string(), "UTF-8 directory enumeration");
            Require(VxDeleteFile(copied.u8string().c_str()) && !std::filesystem::exists(copied), "UTF-8 file deletion");
        }
        BitmapChecks(); EncodingChecks(); FontChecks();
        SDL_Quit();
        std::cout << "Bitmap ownership, encoding and font pixels passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n'; SDL_Quit(); return 1;
    }
}
