#include "tests/ui/UiTests.h"
#include "core/runtime/TextEncoding.h"
#include "ui/print/Printer.h"

#include <filesystem>
#include <fstream>

#if defined(__linux__)
#include <gtk/gtkunixprint.h>
#include <dlfcn.h>
namespace {
bool recordHelp = false;
std::vector<std::string> helpUrls;
}
// Observe the real launcher boundary, then forward to SDL. These desktop
// regressions still open the user's actual browser.
extern "C" bool SDLCALL SDL_OpenURL(const char *url) {
    if (recordHelp) helpUrls.emplace_back(url);
    using Open = bool (SDLCALL *)(const char *);
    static Open real = reinterpret_cast<Open>(dlsym(RTLD_NEXT, "SDL_OpenURL"));
    return real ? real(url) : SDL_SetError("Cannot resolve SDL's browser launcher");
}
#endif

namespace vdevuitest {
#if defined(_WIN32)
void RegisterWindowsDialogTests(ImGuiTestEngine *engine);
#endif
#if defined(__linux__)
namespace {
namespace fs = std::filesystem;
SDL_Window *MainWindow() {
    return SDL_GetWindowFromID(static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(ImGui::GetMainViewport()->PlatformHandle)));
}
fs::path Output(const char *name) {
    return fs::u8path(SDL_GetBasePath()) / "test-desktop" / fs::u8path(name);
}
void SettleDesktop(ImGuiTestContext *ctx, const char *panel = nullptr) {
    OnMainThread(ctx, [] { SDL_SetWindowSize(MainWindow(), 1600, 960); SDL_SyncWindow(MainWindow()); });
    Settle(ctx, panel);
}
template<class F> bool Until(ImGuiTestContext *ctx, F done) {
    const Uint64 deadline = SDL_GetTicks() + 45000;
    while (!done() && SDL_GetTicks() < deadline) ctx->Sleep(0.05f);
    return done();
}
bool PrintParameters(ImGuiTestContext *ctx) {
    ImGuiWindow *window = ctx->GetWindowByRef("//###PrintParameters");
    return window && window->WasActive;
}
bool Logged(size_t from, const std::string &text) {
    const auto &log = App().State().log;
    for (size_t i = from; i < log.size(); ++i)
        if (log[i].text.find(text) != std::string::npos) return true;
    return false;
}
struct FileAnswer { bool called = false, mainThread = false; vdev::FileDialogResult result; };
struct DialogDriver {
    SDL_Process *process = nullptr;
    explicit DialogDriver(const std::string &title, const char *action, const fs::path &ready = {}, const fs::path &gate = {}) {
        const std::string name = u8"\u4e2d\u6587 \u7ec4\u5408.cmo", shown = ready.u8string(), proceed = gate.u8string();
        std::vector<const char *> args = {"/usr/bin/python3", VIRTOOLSDEV_DIALOG_DRIVER, "--title", title.c_str(),
            "--action", action, "--name", name.c_str()};
        if (!shown.empty()) { args.push_back("--ready"); args.push_back(shown.c_str()); }
        if (!proceed.empty()) { args.push_back("--gate"); args.push_back(proceed.c_str()); }
        args.push_back(nullptr);
        process = SDL_CreateProcess(args.data(), false);
    }
    bool Finished(int &result) { return process && SDL_WaitProcess(process, false, &result); }
    ~DialogDriver() {
        if (process) { int result = 0; if (!SDL_WaitProcess(process, false, &result)) SDL_KillProcess(process, true); SDL_DestroyProcess(process); }
    }
};
fs::path InputFile() {
    const auto input = Output(u8"\u5bf9\u8bdd\u6846/\u4e2d\u6587 \u7ec4\u5408.cmo");
    fs::create_directories(input.parent_path());
    if (App().State().document && !App().State().document->SourcePath().empty())
        fs::copy_file(fs::u8path(App().State().document->SourcePath()), input, fs::copy_options::overwrite_existing);
    return input;
}
void Request(ImGuiTestContext *ctx, const std::string &title, const fs::path &input, std::shared_ptr<FileAnswer> answer) {
    OnMainThread(ctx, [&] {
        // All request material here dies before the native selection completes.
        const std::wstring caption = vdev::Utf8ToWide(title);
        const wchar_t material[] = L"Compositions\0*.cmo;*.nmo;*.vmo\0All files\0*.*\0";
        const std::wstring filters(material, sizeof(material) / sizeof(material[0]));
        vdev::FileDialogOptions options;
        options.owner = MainWindow(); options.title = caption.c_str(); options.filter = filters.c_str();
        options.initial = vdev::Utf8ToWide(input.parent_path().u8string());
        App().State().RequestFile(options, [answer](const vdev::FileDialogResult &result) {
            answer->result = result; answer->called = true; answer->mainThread = SDL_IsMainThread();
        });
    });
}

struct PrintResponse {
    bool cancel = false, answered = false, configured = false;
    std::string path;
    std::string printerName = "Print to File";
    int dpiX = 144, dpiY = 144;
    bool landscape = false;
    Uint64 deadline = 0;
    guint timer = 0;
    static gboolean Answer(gpointer value) {
        auto &choice = *static_cast<PrintResponse *>(value);
        GList *windows = gtk_window_list_toplevels();
        GtkPrintUnixDialog *dialog = nullptr;
        for (GList *it = windows; it; it = it->next)
            if (GTK_IS_PRINT_UNIX_DIALOG(it->data)) dialog = GTK_PRINT_UNIX_DIALOG(it->data);
        g_list_free(windows);
        if (!dialog) return G_SOURCE_CONTINUE;
        if (!choice.deadline) choice.deadline = SDL_GetTicks() + 15000;
        if (choice.cancel || SDL_GetTicks() >= choice.deadline) {
            gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
            choice.answered = choice.cancel; choice.timer = 0;
            return G_SOURCE_REMOVE;
        }
        GtkPrinter *printer = gtk_print_unix_dialog_get_selected_printer(dialog);
        if (!choice.configured || !printer || std::string(gtk_printer_get_name(printer)) != choice.printerName) {
            GtkPrintSettings *settings = gtk_print_settings_new();
            gtk_print_settings_set_printer(settings, choice.printerName.c_str());
            gchar *uri = g_filename_to_uri(choice.path.c_str(), nullptr, nullptr);
            gtk_print_settings_set(settings, GTK_PRINT_SETTINGS_OUTPUT_URI, uri);
            gtk_print_settings_set(settings, GTK_PRINT_SETTINGS_OUTPUT_FILE_FORMAT, "pdf");
            gtk_print_settings_set_resolution_xy(settings, choice.dpiX, choice.dpiY);
            gtk_print_unix_dialog_set_settings(dialog, settings);
            GtkPageSetup *page = gtk_page_setup_copy(gtk_print_unix_dialog_get_page_setup(dialog));
            GtkPaperSize *paper = gtk_paper_size_new(GTK_PAPER_NAME_A4);
            gtk_page_setup_set_paper_size(page, paper); gtk_paper_size_free(paper);
            gtk_page_setup_set_orientation(page, choice.landscape ? GTK_PAGE_ORIENTATION_LANDSCAPE : GTK_PAGE_ORIENTATION_PORTRAIT);
            gtk_print_unix_dialog_set_page_setup(dialog, page);
            g_object_unref(page);
            choice.configured = true;
            g_free(uri); g_object_unref(settings);
            return G_SOURCE_CONTINUE;
        }
        GtkWidget *accept = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
        if (!accept || !gtk_widget_get_sensitive(accept)) return G_SOURCE_CONTINUE;
        choice.answered = true; choice.timer = 0;
        gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
        return G_SOURCE_REMOVE;
    }
    explicit PrintResponse(std::string file = {}) : cancel(file.empty()), path(std::move(file)) {
        timer = g_timeout_add(100, &Answer, this);
    }
    ~PrintResponse() { if (timer) g_source_remove(timer); }
};
void FourPages(ImGuiTestContext *ctx) {
    ctx->ItemInput("//###PrintParameters/##pagesH"); ctx->KeyCharsReplace("2");
    ctx->ItemInput("//###PrintParameters/##pagesV"); ctx->KeyCharsReplace("2");
    ctx->ItemClick("//###PrintParameters/OK/##dialogbutton"); ctx->Yield(3);
}
std::set<fs::path> TemporaryPdfs() {
    std::set<fs::path> files;
    char *folder = SDL_GetPrefPath("Virtools", "VirtoolsDev");
    if (folder) {
        for (const auto &file : fs::directory_iterator(fs::u8path(folder)))
            if (file.path().filename().u8string().find("print-") == 0 && file.path().extension() == ".pdf") files.insert(file.path());
        SDL_free(folder);
    }
    return files;
}
}
#endif

void RegisterDesktopTests(ImGuiTestEngine *engine) {
#if defined(__linux__)
    ImGuiTest *t = IM_REGISTER_TEST(engine, "desktopfiles", "portal_select_cancel");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        SettleDesktop(ctx);
        const auto input = InputFile();
        for (const char *action : {"select", "cancel"}) {
            const std::string title = std::string("VirtoolsDev regression ") + action;
            auto answer = std::make_shared<FileAnswer>();
            DialogDriver driver(title, action);
            IM_CHECK(driver.process != nullptr);
            Request(ctx, title, input, answer);
            IM_CHECK(Until(ctx, [&] { return answer->called; }));
            IM_CHECK(answer->mainThread);
            IM_CHECK(answer->result.error.empty());
            if (std::string(action) == "select") {
                IM_CHECK(answer->result.status == vdev::FileDialogResult::Status::Selected);
                IM_CHECK(answer->result.paths == std::vector<std::string>{input.u8string()});
            } else {
                IM_CHECK(answer->result.status == vdev::FileDialogResult::Status::Cancelled);
                IM_CHECK(answer->result.paths.empty());
            }
            int exit = -1;
            IM_CHECK(Until(ctx, [&] { return driver.Finished(exit); })); IM_CHECK_EQ(exit, 0);
        }
    };

    t = IM_REGISTER_TEST(engine, "desktopfiles", "portal_document_replaced_closed");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        SettleDesktop(ctx);
        const auto input = InputFile();
        for (const char *operation : {"New Composition", "Close"}) {
            const std::string title = std::string("VirtoolsDev regression stale ") + operation;
            const auto shown = Output("dialog-shown"), gate = Output("dialog-continue");
            fs::remove(shown); fs::remove(gate);
            auto answer = std::make_shared<FileAnswer>();
            DialogDriver driver(title, "select", shown, gate);
            IM_CHECK(driver.process != nullptr);
            Request(ctx, title, input, answer);
            IM_CHECK(Until(ctx, [&] { return fs::exists(shown); }));
            ctx->MenuClick((std::string("//##MainMenuBar/File/") + operation).c_str());
            ctx->Yield(3);
            if (std::string(operation) == "Close") IM_CHECK(!App().State().HasDocument());
            else {
                IM_CHECK(App().State().HasDocument());
                IM_CHECK(App().State().document->SourcePath().empty());
                IM_CHECK(App().State().document->Scripts().empty());
            }
            { std::ofstream proceed(gate); proceed << "continue"; }
            int exit = -1;
            IM_CHECK(Until(ctx, [&] { return driver.Finished(exit); })); IM_CHECK_EQ(exit, 0);
            ctx->Sleep(1.0f);
            IM_CHECK(!answer->called);
        }
    };

    t = IM_REGISTER_TEST(engine, "desktophelp", "building_block_f1");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        SettleDesktop(ctx, "//Building Blocks");
        ctx->ItemInputValue("//Building Blocks/##filter", "Rotate");
        ctx->MouseMove("//Building Blocks/**/Rotate");
        for (int action = 0; action < 3; ++action) {
            helpUrls.clear(); recordHelp = true;
            if (action == 0) ctx->KeyPress(ImGuiKey_F1);
            else if (action == 1) ctx->ItemDoubleClick("//Building Blocks/**/Rotate");
            else {
                ctx->ItemClick("//Building Blocks/**/Rotate", ImGuiMouseButton_Right);
                ctx->ItemClick("//$FOCUSED/Html Help");
            }
            ctx->Yield(3);
            recordHelp = false;
            for (const auto &url : helpUrls) ctx->LogInfo("Browser: %s", url.c_str());
            IM_CHECK_EQ(helpUrls.size(), size_t(1));
            IM_CHECK(helpUrls.front().find("behaviors/3D%20Transformations/Rotate.html") != std::string::npos);
        }
        helpUrls.clear(); recordHelp = true;
        ctx->MenuClick("//##MainMenuBar/Help/Online Reference"); ctx->Yield(3);
        recordHelp = false;
        for (const auto &url : helpUrls) ctx->LogInfo("Browser: %s", url.c_str());
        IM_CHECK_EQ(helpUrls.size(), size_t(1));
        IM_CHECK(helpUrls.front().find("/VirtoolsDevHelp.html") != std::string::npos);
        ctx->WindowFocus("//Level Manager"); ctx->MouseMoveToPos(ImVec2(10, 10));
        helpUrls.clear(); recordHelp = true;
        ctx->KeyPress(ImGuiKey_F1); ctx->Yield(3);
        recordHelp = false;
        for (const auto &url : helpUrls) ctx->LogInfo("Browser: %s", url.c_str());
        IM_CHECK_EQ(helpUrls.size(), size_t(1));
        IM_CHECK(helpUrls.front().find("/VirtoolsDevHelp.html") != std::string::npos);
    };

    t = IM_REGISTER_TEST(engine, "desktopfiles", "native_error");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        auto answer = std::make_shared<FileAnswer>();
        OnMainThread(ctx, [&] {
            vdev::FileDialogOptions options;
            options.owner = MainWindow(); options.filter = L"Invalid\0*.cmo;bad/path\0";
            App().State().fileDialogs.Request(options, [answer](const vdev::FileDialogResult &result) {
                answer->result = result; answer->called = true; answer->mainThread = SDL_IsMainThread();
            });
        });
        IM_CHECK(Until(ctx, [&] { return answer->called; }));
        IM_CHECK(answer->mainThread);
        IM_CHECK(answer->result.status == vdev::FileDialogResult::Status::Error);
        IM_CHECK(!answer->result.error.empty()); IM_CHECK(answer->result.paths.empty());
    };

    t = IM_REGISTER_TEST(engine, "desktopprint", "system_orientation_resolution");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        SettleDesktop(ctx);
        PrintResponse response(Output("settings.pdf").u8string());
        response.landscape = true; response.dpiX = response.dpiY = 300;
        vdev::PrinterSelection selected;
        OnMainThread(ctx, [&] { selected = vdev::Printer::Select(MainWindow()); });
        IM_CHECK(response.answered);
        IM_CHECK(selected.status == vdev::PrinterSelectionStatus::Selected);
        const auto &settings = selected.printer->Settings();
        ctx->LogInfo("Selected paper=%dx%d dpi=%dx%d", settings.paper[0], settings.paper[1], settings.dpi[0], settings.dpi[1]);
        IM_CHECK_EQ(settings.paper[0], 2970); IM_CHECK_EQ(settings.paper[1], 2100);
        IM_CHECK_EQ(settings.dpi[0], 300); IM_CHECK_EQ(settings.dpi[1], 300);
    };

    t = IM_REGISTER_TEST(engine, "desktopprint", "system_cancel");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        SettleDesktop(ctx, "//Schematic");
        const size_t log = App().State().log.size();
        PrintResponse response;
        ctx->MenuClick("//##MainMenuBar/File/Print Schematic...");
        ctx->Yield(3);
        IM_CHECK(response.answered);
        IM_CHECK(!PrintParameters(ctx));
        IM_CHECK(Logged(log, "Print cancelled"));
    };

    t = IM_REGISTER_TEST(engine, "desktopprint", "system_pdf_pagination");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        SettleDesktop(ctx, "//Schematic");
        const auto output = Output(u8"\u4e2d\u6587\u6253\u5370.pdf");
        fs::create_directories(output.parent_path());
        fs::remove(output);
        const bool dirty = App().State().document->Dirty();
        const auto changes = App().State().document->ChangesOutsideUndo();
        const size_t steps = App().State().History().UndoCount(), log = App().State().log.size();
        PrintResponse response(output.u8string());
        ctx->MenuClick("//##MainMenuBar/File/Print Schematic...");
        ctx->Yield(3);
        IM_CHECK(response.answered);
        IM_CHECK(PrintParameters(ctx));
        FourPages(ctx);
        LogEditor(ctx, log);
        IM_CHECK(!PrintParameters(ctx));
        IM_CHECK(fs::is_regular_file(output));
        IM_CHECK_GT(fs::file_size(output), 1000u);
        IM_CHECK(Logged(log, "Printer job submitted: Print to File"));
        IM_CHECK_EQ(App().State().document->Dirty(), dirty);
        IM_CHECK_EQ(App().State().document->ChangesOutsideUndo(), changes);
        IM_CHECK_EQ(App().State().History().UndoCount(), steps);
    };

    t = IM_REGISTER_TEST(engine, "desktopprint", "system_parameters_cancel");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        SettleDesktop(ctx, "//Schematic");
        const auto output = Output("cancelled.pdf");
        fs::remove(output);
        const auto temporary = TemporaryPdfs();
        const size_t log = App().State().log.size();
        PrintResponse response(output.u8string());
        ctx->MenuClick("//##MainMenuBar/File/Print Schematic..."); ctx->Yield(3);
        IM_CHECK(response.answered && PrintParameters(ctx));
        ctx->KeyPress(ImGuiKey_Escape); ctx->Yield(3);
        IM_CHECK(!PrintParameters(ctx)); IM_CHECK(!fs::exists(output));
        IM_CHECK(Logged(log, "Print cancelled"));
        IM_CHECK(TemporaryPdfs() == temporary);
    };

    t = IM_REGISTER_TEST(engine, "desktopprint", "system_submission_failure");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        SettleDesktop(ctx, "//Schematic");
        const auto output = Output("blocked.pdf");
        fs::remove(output);
        const auto temporary = TemporaryPdfs();
        const size_t log = App().State().log.size();
        PrintResponse response(output.u8string());
        ctx->MenuClick("//##MainMenuBar/File/Print Schematic..."); ctx->Yield(3);
        IM_CHECK(response.answered && PrintParameters(ctx));
        fs::create_directory(output); // becomes unwritable after destination selection
        FourPages(ctx);
        bool failed = false;
        for (size_t i = log; i < App().State().log.size(); ++i)
            failed |= App().State().log[i].level == vdev::LogLevel::Error;
        LogEditor(ctx, log);
        IM_CHECK(failed); IM_CHECK(!Logged(log, "Printer job submitted"));
        IM_CHECK(fs::is_directory(output)); IM_CHECK(TemporaryPdfs() == temporary);
        fs::remove(output);
    };

    t = IM_REGISTER_TEST(engine, "desktopcups", "cups_pagination");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        SettleDesktop(ctx, "//Schematic");
        const char *capture = SDL_getenv("VIRTOOLSDEV_CUPS_CAPTURE_DIR");
        IM_CHECK(capture != nullptr);
        const fs::path directory = fs::u8path(capture);
        const auto count = [&] { return std::distance(fs::directory_iterator(directory), fs::directory_iterator()); };
        const auto before = count();
        const auto temporary = TemporaryPdfs();
        const size_t log = App().State().log.size();
        PrintResponse response(Output("cups.pdf").u8string());
        response.printerName = "VirtoolsDev-test";
        ctx->MenuClick("//##MainMenuBar/File/Print Schematic..."); ctx->Yield(3);
        IM_CHECK(response.answered && PrintParameters(ctx));
        FourPages(ctx);
        LogEditor(ctx, log);
        IM_CHECK(Logged(log, "Printer job submitted: VirtoolsDev-test"));
        IM_CHECK(Until(ctx, [&] { return count() == before+1; }));
        IM_CHECK(TemporaryPdfs() == temporary);
    };

    t = IM_REGISTER_TEST(engine, "desktopcups", "cups_orientation_resolution");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        SettleDesktop(ctx);
        PrintResponse response(Output("cups-settings.pdf").u8string());
        response.printerName = "VirtoolsDev-test";
        response.landscape = true;
        vdev::PrinterSelection selected;
        OnMainThread(ctx, [&] { selected = vdev::Printer::Select(MainWindow()); });
        IM_CHECK(response.answered);
        IM_CHECK(selected.status == vdev::PrinterSelectionStatus::Selected);
        const auto &settings = selected.printer->Settings();
        ctx->LogInfo("CUPS paper=%dx%d dpi=%dx%d", settings.paper[0], settings.paper[1], settings.dpi[0], settings.dpi[1]);
        IM_CHECK_EQ(settings.paper[0], 2970); IM_CHECK_EQ(settings.paper[1], 2100);
        // This PDF capture queue exposes no resolution in GTK's settings;
        // GTK's resolution getters supply their 300 DPI default.
        IM_CHECK_EQ(settings.dpi[0], 300); IM_CHECK_EQ(settings.dpi[1], 300);
    };
#elif defined(_WIN32)
    RegisterWindowsDialogTests(engine);
#else
    (void)engine;
#endif
}
}
