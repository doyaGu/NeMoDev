#include "ui/print/PrintDocument.h"
#include <hpdf.h>
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <vector>

namespace vdev {
struct PrintDocument::Impl {
    HPDF_Doc document = nullptr;
    HPDF_Page page = nullptr;
    PrinterSettings settings;
    std::string path;
    HPDF_STATUS failure = HPDF_OK;
    ~Impl() { if (document) HPDF_Free(document); if (!path.empty()) { std::error_code e; std::filesystem::remove(std::filesystem::u8path(path), e); } }
};
PrintDocument::PrintDocument() : m_Impl(new Impl) {}
PrintDocument::~PrintDocument() = default;
bool PrintDocument::Begin(const PrinterSettings &settings, std::string &error) {
    Cancel(); m_Impl.reset(new Impl); m_Impl->settings = settings;
    m_Impl->document = HPDF_New([](HPDF_STATUS failure, HPDF_STATUS, void *userdata) {
        static_cast<Impl *>(userdata)->failure = failure;
    }, m_Impl.get());
    if (!m_Impl->document) { error = "Cannot allocate the printable document"; return false; }
    HPDF_SetCompressionMode(m_Impl->document, HPDF_COMP_ALL);
    char *folder = SDL_GetPrefPath("Virtools", "VirtoolsDev");
    if (!folder) { error = SDL_GetError(); return false; }
    m_Impl->path = (std::filesystem::u8path(folder) / ("print-" + std::to_string(SDL_GetPerformanceCounter()) + ".pdf")).u8string();
    SDL_free(folder); return true;
}
bool PrintDocument::BeginPage(std::string &error) {
    m_Impl->page = HPDF_AddPage(m_Impl->document);
    if (!m_Impl->page) { error = "Cannot create the printable page"; return false; }
    HPDF_Page_SetWidth(m_Impl->page, float(m_Impl->settings.paper[0] * 72.0 / 254.0));
    HPDF_Page_SetHeight(m_Impl->page, float(m_Impl->settings.paper[1] * 72.0 / 254.0));
    return m_Impl->failure == HPDF_OK;
}
bool PrintDocument::Tile(const unsigned char *bgra, int width, int height, int left, int top, int right, int bottom, std::string &error) {
    std::vector<unsigned char> rgb(static_cast<size_t>(width) * height * 3);
    for (size_t i = 0, j = 0; j < rgb.size(); i += 4, j += 3) {
        rgb[j] = bgra[i+2]; rgb[j+1] = bgra[i+1]; rgb[j+2] = bgra[i];
    }
    HPDF_Image image = HPDF_LoadRawImageFromMem(m_Impl->document, rgb.data(), width, height, HPDF_CS_DEVICE_RGB, 8);
    if (!image) { error = "Cannot encode a printable image tile"; return false; }
    const double sx = 72.0 / m_Impl->settings.dpi[0], sy = 72.0 / m_Impl->settings.dpi[1];
    const double margin = 5.0 * 72.0 / 25.4;
    const double y = m_Impl->settings.paper[1] * 72.0 / 254.0 - margin - bottom * sy;
    HPDF_Page_DrawImage(m_Impl->page, image, float(margin + left * sx), float(y), float((right-left)*sx), float((bottom-top)*sy));
    if (m_Impl->failure != HPDF_OK) { error = "Printable image encoding failed"; return false; }
    return true;
}
bool PrintDocument::Finish(std::string &error) {
    if (HPDF_SaveToStream(m_Impl->document) != HPDF_OK) { error = "Cannot finish the printable document"; return false; }
    HPDF_ResetStream(m_Impl->document);
    std::ofstream file(std::filesystem::u8path(m_Impl->path), std::ios::binary);
    if (!file) { error = "Cannot open the printable document file"; return false; }
    unsigned char buffer[65536];
    for (;;) {
        HPDF_UINT32 size = sizeof(buffer);
        const HPDF_STATUS status = HPDF_ReadFromStream(m_Impl->document, buffer, &size);
        if (size) file.write(reinterpret_cast<const char *>(buffer), size);
        if (status == HPDF_STREAM_EOF) break;
        if (status != HPDF_OK || !file) { error = "Writing the printable document failed"; return false; }
    }
    file.close();
    if (!file.good()) { error = "Closing the printable document failed"; return false; }
    return true;
}
const std::string &PrintDocument::Path() const { return m_Impl->path; }
void PrintDocument::Cancel() { m_Impl.reset(new Impl); }
}
