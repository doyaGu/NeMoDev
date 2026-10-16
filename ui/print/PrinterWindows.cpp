#include "ui/print/Printer.h"
#include "core/runtime/TextEncoding.h"
#include <SDL3/SDL.h>
#include <windows.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>

namespace vdev {
struct Printer::Impl {
    PrinterSettings settings;
    HDC dc = nullptr;
    HGLOBAL mode = nullptr, names = nullptr;
    int job = 0;
    bool active = false;
    ~Impl() {
        if (active && dc) AbortDoc(dc);
        if (dc) DeleteDC(dc);
        if (mode) GlobalFree(mode);
        if (names) GlobalFree(names);
    }
};
Printer::Printer() : m_Impl(new Impl) {}
Printer::~Printer() = default;
const PrinterSettings &Printer::Settings() const { return m_Impl->settings; }
PrinterSelection Printer::Select(SDL_Window *owner) {
    PrinterSelection result;
    std::shared_ptr<Printer> printer(new Printer);
    PRINTDLGW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner ? static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(owner), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)) : nullptr;
    dialog.Flags = PD_PRINTSETUP | PD_RETURNDC;
    const BOOL accepted = PrintDlgW(&dialog);
    auto &data = *printer->m_Impl;
    data.dc = dialog.hDC; data.mode = dialog.hDevMode; data.names = dialog.hDevNames;
    if (!accepted) {
        const DWORD error = CommDlgExtendedError();
        if (error) { result.status = PrinterSelectionStatus::Failed; result.error = "Printer setup failed: " + std::to_string(error); }
        return result;
    }
    if (!data.dc) { result.status = PrinterSelectionStatus::Failed; result.error = "Printer setup returned no printing device"; return result; }
    data.settings.dpi[0] = std::max(1, GetDeviceCaps(data.dc, LOGPIXELSX));
    data.settings.dpi[1] = std::max(1, GetDeviceCaps(data.dc, LOGPIXELSY));
    data.settings.paper[0] = std::max(1, GetDeviceCaps(data.dc, PHYSICALWIDTH) * 254 / data.settings.dpi[0]);
    data.settings.paper[1] = std::max(1, GetDeviceCaps(data.dc, PHYSICALHEIGHT) * 254 / data.settings.dpi[1]);
    result.status = PrinterSelectionStatus::Selected; result.printer = std::move(printer);
    return result;
}
PrinterStartStatus Printer::BeginDocument(const std::string &title, std::string &error) {
    error.clear();
    auto &data = *m_Impl;
    const std::wstring wide = Utf8ToWide(title);
    DOCINFOW info = {}; info.cbSize = sizeof(info); info.lpszDocName = wide.c_str();
    SetLastError(ERROR_SUCCESS);
    data.job = StartDocW(data.dc, &info);
    const DWORD startError = GetLastError();
    data.active = data.job > 0;
    if (!data.active) {
        if (startError == ERROR_CANCELLED || startError == ERROR_PRINT_CANCELLED)
            return PrinterStartStatus::Cancelled;
        error = "Cannot start the printer job (Windows error " + std::to_string(startError) + ")";
        return PrinterStartStatus::Failed;
    }
    SetStretchBltMode(data.dc, HALFTONE); SetBrushOrgEx(data.dc, 0, 0, nullptr);
    return PrinterStartStatus::Started;
}
bool Printer::BeginPage(std::string &error) {
    if (StartPage(m_Impl->dc) > 0) return true;
    error = "Cannot start a printer page"; return false;
}
bool Printer::DrawTile(const unsigned char *bgra, int width, int height, int left, int top, int right, int bottom, std::string &error) {
    BITMAPINFO info = {}; info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    // GDI's origin is the printable area's top left, while pagination uses
    // the physical sheet. Match the CUPS/AppKit page writer's 5 mm margins.
    const int x = int(std::lround(5.0 * m_Impl->settings.dpi[0] / 25.4)) - GetDeviceCaps(m_Impl->dc, PHYSICALOFFSETX);
    const int y = int(std::lround(5.0 * m_Impl->settings.dpi[1] / 25.4)) - GetDeviceCaps(m_Impl->dc, PHYSICALOFFSETY);
    const int copied = StretchDIBits(m_Impl->dc, x+left, y+top, right-left, bottom-top, 0, 0, width, height, bgra, &info, DIB_RGB_COLORS, SRCCOPY);
    if (copied > 0 && copied != GDI_ERROR) return true;
    error = "Cannot send a rendered tile to the printer"; return false;
}
bool Printer::EndPage(std::string &error) {
    if (::EndPage(m_Impl->dc) > 0) return true;
    error = "Cannot finish a printer page"; return false;
}
bool Printer::Submit(std::string &job, std::string &error) {
    if (EndDoc(m_Impl->dc) <= 0) { error = "Printer job submission failed"; return false; }
    m_Impl->active = false;
    job = std::to_string(m_Impl->job); return true;
}
void Printer::Cancel() {
    if (m_Impl->active) { AbortDoc(m_Impl->dc); m_Impl->active = false; }
}
}
