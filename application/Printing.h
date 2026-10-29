#pragma once

#include <memory>
#include <string>
#include <vector>

namespace vdev {
struct PrinterSettings {
    int paper[2] = {2100, 2970}; // tenths of a millimetre, orientation applied
    int dpi[2] = {600, 600};
};
enum class PrinterSelectionStatus { Selected, Cancelled, Failed };
enum class PrinterStartStatus { Started, Cancelled, Failed };
struct SchematicPrint {
    bool fillHeader = false, fillBody = false, fillShapes = false, bitmaps = false, colorLinks = false;
};
// An owned copy of the shown Scripts, display options and image data. Render
// never reads a document or a live Schematic view. The UI renderer owns fonts
// and device resources for the duration of a print operation.
class SchematicPrintContent {
  public:
    virtual ~SchematicPrintContent() = default;
    float width = 0, height = 0;
    virtual bool Render(float x, float y, int width, int height, float scale, const SchematicPrint &options,
                        std::vector<unsigned char> &bgra, std::string &error) const = 0;
};
class PrintDestination {
  public:
    virtual ~PrintDestination() = default;
    virtual const PrinterSettings &Settings() const = 0;
    virtual bool PreparePages(int count, int width, int height, std::string &error) { return true; }
    virtual PrinterStartStatus BeginDocument(const std::string &title, std::string &error) = 0;
    virtual bool BeginPage(std::string &error) = 0;
    virtual bool DrawTile(const unsigned char *bgra, int width, int height, int left, int top, int right, int bottom,
                          std::string &error) = 0;
    virtual bool EndPage(std::string &error) = 0;
    virtual bool Submit(std::string &job, std::string &error) = 0;
    virtual void Cancel() = 0;
    virtual std::vector<std::string> WrittenFiles() const { return {}; }
    virtual bool ImageFiles() const { return false; }
    virtual std::string SubmittedMessage(const std::string &job) const { return "Printer job submitted: " + job; }
};
struct PrintChoice {
    PrinterSelectionStatus status = PrinterSelectionStatus::Cancelled;
    std::shared_ptr<PrintDestination> destination;
    std::string error;
};
} // namespace vdev
