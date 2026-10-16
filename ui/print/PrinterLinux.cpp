#include "ui/print/Printer.h"
#include "ui/print/PrintDocument.h"
#include <gtk/gtkunixprint.h>
#include <SDL3/SDL.h>
#include <atomic>

namespace vdev {
struct Printer::Impl {
    PrinterSettings settings;
    GtkPrinter *printer = nullptr;
    GtkPrintSettings *options = nullptr;
    GtkPageSetup *page = nullptr;
    PrintDocument document;
    std::string title;
    ~Impl() {
        if (printer) g_object_unref(printer);
        if (options) g_object_unref(options);
        if (page) g_object_unref(page);
    }
};
Printer::Printer() : m_Impl(new Impl) {}
Printer::~Printer() = default;
const PrinterSettings &Printer::Settings() const { return m_Impl->settings; }
PrinterSelection Printer::Select(SDL_Window *) {
    PrinterSelection result;
    if (!gtk_init_check(nullptr, nullptr)) { result.status = PrinterSelectionStatus::Failed; result.error = "Cannot open the system printer dialog"; return result; }
    GtkWidget *dialog = gtk_print_unix_dialog_new("Print Schematic", nullptr);
    gtk_print_unix_dialog_set_manual_capabilities(GTK_PRINT_UNIX_DIALOG(dialog), GTK_PRINT_CAPABILITY_GENERATE_PDF);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK) { gtk_widget_destroy(dialog); return result; }
    std::shared_ptr<Printer> printer(new Printer);
    auto &data = *printer->m_Impl;
    GtkPrinter *selected = gtk_print_unix_dialog_get_selected_printer(GTK_PRINT_UNIX_DIALOG(dialog));
    if (selected) data.printer = GTK_PRINTER(g_object_ref(selected));
    data.options = gtk_print_unix_dialog_get_settings(GTK_PRINT_UNIX_DIALOG(dialog));
    data.page = gtk_page_setup_copy(gtk_print_unix_dialog_get_page_setup(GTK_PRINT_UNIX_DIALOG(dialog)));
    gtk_widget_destroy(dialog);
    if (!data.printer || !data.options || !data.page) { result.status = PrinterSelectionStatus::Failed; result.error = "No printer was selected"; return result; }
    data.settings.paper[0] = int(gtk_page_setup_get_paper_width(data.page, GTK_UNIT_MM) * 10.0 + 0.5);
    data.settings.paper[1] = int(gtk_page_setup_get_paper_height(data.page, GTK_UNIT_MM) * 10.0 + 0.5);
    const int dpi[2] = {gtk_print_settings_get_resolution_x(data.options), gtk_print_settings_get_resolution_y(data.options)};
    for (int axis = 0; axis < 2; ++axis)
        if (dpi[axis] > 0) data.settings.dpi[axis] = dpi[axis];
    result.status = PrinterSelectionStatus::Selected; result.printer = std::move(printer); return result;
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
bool Printer::Submit(std::string &identifier, std::string &error) {
    if (!m_Impl->document.Finish(error)) return false;
    GtkPrintJob *job = gtk_print_job_new(m_Impl->title.c_str(), m_Impl->printer, m_Impl->options, m_Impl->page);
    GError *failure = nullptr;
    if (!gtk_print_job_set_source_file(job, m_Impl->document.Path().c_str(), &failure)) {
        error = failure ? failure->message : "Cannot prepare the printer submission";
        if (failure) g_error_free(failure);
        g_object_unref(job); return false;
    }
    struct Submission { bool finished = false; std::string error; } submission;
    // GTK's Unix printer backend submits PDF through CUPS and also supports
    // the system's Print to File destination. Return after its submission callback.
    gtk_print_job_send(job, [](GtkPrintJob *, gpointer data, const GError *failure) {
        auto &result = *static_cast<Submission *>(data);
        if (failure) result.error = failure->message;
        result.finished = true;
    }, &submission, nullptr);
    while (!submission.finished) g_main_context_iteration(nullptr, TRUE);
    g_object_unref(job);
    if (!submission.error.empty()) { error = submission.error; return false; }
    identifier = gtk_printer_get_name(m_Impl->printer);
    return true;
}
void Printer::Cancel() { m_Impl->document.Cancel(); }
}
