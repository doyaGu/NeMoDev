#pragma once
#include <memory>
#include <string>
#include "ui/print/Printer.h"
namespace vdev {
// Internal page writer for the CUPS/AppKit adapters. Each rendered tile is
// compressed into its PDF page; no full printer-resolution bitmap is needed.
class PrintDocument {
public:
    PrintDocument();
    ~PrintDocument();
    bool Begin(const PrinterSettings &settings, std::string &error);
    bool BeginPage(std::string &error);
    bool Tile(const unsigned char *bgra, int width, int height, int left, int top, int right, int bottom, std::string &error);
    bool Finish(std::string &error);
    const std::string &Path() const;
    void Cancel();
private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
}
