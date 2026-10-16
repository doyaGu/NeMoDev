#pragma once
#include <memory>
#include <string>
struct SDL_Window;
namespace vdev {
struct PrinterSettings {
    int paper[2] = {2100, 2970}; // tenths of a millimetre, orientation applied
    int dpi[2] = {600, 600};
};
enum class PrinterSelectionStatus { Selected, Cancelled, Failed };
enum class PrinterStartStatus { Started, Cancelled, Failed };
struct PrinterSelection;
class Printer {
public:
    ~Printer();
    static PrinterSelection Select(SDL_Window *owner);
    const PrinterSettings &Settings() const;
    // Native destinations may ask for an output file here. Cancellation does
    // not start a job; a failure supplies an error. Only Started permits pages.
    PrinterStartStatus BeginDocument(const std::string &title, std::string &error);
    bool BeginPage(std::string &error);
    // Coordinates are device pixels inside the page's 5 mm margin. Adapters
    // apply that physical margin and any native printable-area offset.
    bool DrawTile(const unsigned char *bgra, int width, int height, int left, int top, int right, int bottom, std::string &error);
    bool EndPage(std::string &error);
    bool Submit(std::string &job, std::string &error);
    void Cancel();
private:
    struct Impl;
    Printer();
    std::unique_ptr<Impl> m_Impl;
};
struct PrinterSelection {
    PrinterSelectionStatus status = PrinterSelectionStatus::Cancelled;
    std::shared_ptr<Printer> printer;
    std::string error;
};
}
