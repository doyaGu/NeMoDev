#include "ui/print/Printer.h"
#include "ui/print/PrintDocument.h"
#import <AppKit/AppKit.h>
#include <cmath>

@interface VirtoolsSchematicPrintView : NSView
@property(nonatomic, strong) NSPDFImageRep *pdf;
@property(nonatomic) NSSize pageSize;
@end
@implementation VirtoolsSchematicPrintView
- (BOOL)isFlipped { return YES; }
- (BOOL)knowsPageRange:(NSRange *)range { *range = NSMakeRange(1, self.pdf.pageCount); return YES; }
- (NSRect)rectForPage:(NSInteger)page { return NSMakeRect(0, (page-1)*self.pageSize.height, self.pageSize.width, self.pageSize.height); }
- (void)drawRect:(NSRect)dirty {
    NSInteger first = MAX(0, (NSInteger)floor(NSMinY(dirty) / self.pageSize.height));
    NSInteger last = MIN(self.pdf.pageCount-1, (NSInteger)floor((NSMaxY(dirty)-0.01) / self.pageSize.height));
    for (NSInteger page = first; page <= last; ++page) {
        self.pdf.currentPage = page;
        [self.pdf drawInRect:NSMakeRect(0, page*self.pageSize.height, self.pageSize.width, self.pageSize.height)];
    }
}
@end

namespace vdev {
struct Printer::Impl {
    PrinterSettings settings;
    NSPrintInfo *info;
    PrintDocument document;
    std::string title;
};
Printer::Printer() : m_Impl(new Impl) {}
Printer::~Printer() = default;
const PrinterSettings &Printer::Settings() const { return m_Impl->settings; }
PrinterSelection Printer::Select(SDL_Window *) {
    @autoreleasepool {
        PrinterSelection result;
        std::shared_ptr<Printer> printer(new Printer);
        printer->m_Impl->info = [[NSPrintInfo sharedPrintInfo] copy];
        if ([[NSPrintPanel printPanel] runModalWithPrintInfo:printer->m_Impl->info] != NSModalResponseOK) return result;
        NSSize size = printer->m_Impl->info.paperSize;
        printer->m_Impl->settings.paper[0] = int(size.width * 254.0 / 72.0 + 0.5);
        printer->m_Impl->settings.paper[1] = int(size.height * 254.0 / 72.0 + 0.5);
        result.status = PrinterSelectionStatus::Selected; result.printer = std::move(printer); return result;
    }
}
PrinterStartStatus Printer::BeginDocument(const std::string &title, std::string &error) {
    error.clear();
    m_Impl->title = title;
    return m_Impl->document.Begin(m_Impl->settings, error) ? PrinterStartStatus::Started : PrinterStartStatus::Failed;
}
bool Printer::BeginPage(std::string &error) { return m_Impl->document.BeginPage(error); }
bool Printer::DrawTile(const unsigned char *pixels, int w, int h, int l, int t, int r, int b, std::string &error) {
    return m_Impl->document.Tile(pixels, w, h, l, t, r, b, error);
}
bool Printer::EndPage(std::string &) { return true; }
bool Printer::Submit(std::string &job, std::string &error) {
    if (!m_Impl->document.Finish(error)) return false;
    @autoreleasepool {
        NSString *path = [NSString stringWithUTF8String:m_Impl->document.Path().c_str()];
        NSPDFImageRep *pdf = [NSPDFImageRep imageRepWithData:[NSData dataWithContentsOfFile:path]];
        if (!pdf) { error = "Cannot read the printable document"; return false; }
        NSSize size = m_Impl->info.paperSize;
        VirtoolsSchematicPrintView *view = [[VirtoolsSchematicPrintView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height*pdf.pageCount)];
        view.pdf = pdf; view.pageSize = size;
        m_Impl->info.leftMargin = 0; m_Impl->info.rightMargin = 0;
        m_Impl->info.topMargin = 0; m_Impl->info.bottomMargin = 0;
        NSPrintOperation *operation = [NSPrintOperation printOperationWithView:view printInfo:m_Impl->info];
        operation.showsPrintPanel = NO;
        operation.showsProgressPanel = NO;
        operation.jobTitle = [NSString stringWithUTF8String:m_Impl->title.c_str()];
        if (![operation runOperation]) { error = "Printer job submission failed"; return false; }
        job = m_Impl->title; return true;
    }
}
void Printer::Cancel() { m_Impl->document.Cancel(); }
}
