#include "tests/ui/UiTests.h"
#include "core/runtime/TextEncoding.h"
#include "ui/print/Printer.h"

#include <filesystem>
#include <fstream>

// Interactive desktop regressions. The test drives the editor and checks its
// results; a user or desktop driver answers the real Windows dialogs. Run one
// category with --ui-show. No native dialog or printer boundary is replaced.
namespace vdevuitest {
namespace {
namespace fs = std::filesystem;
SDL_Window *MainWindow() {
    return SDL_GetWindowFromID(static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(ImGui::GetMainViewport()->PlatformHandle)));
}
fs::path Output(const char *name) {
    return fs::u8path(SDL_GetBasePath()) / "test-windows-desktop" / fs::u8path(name);
}
void SettleDesktop(ImGuiTestContext *ctx, const char *panel = nullptr) {
    OnMainThread(ctx, [] {
        SDL_SetWindowSize(MainWindow(), 1600, 960);
        SDL_ShowWindow(MainWindow()); SDL_RaiseWindow(MainWindow()); SDL_SyncWindow(MainWindow());
    });
    Settle(ctx, panel);
    // The system dialog moves the real cursor independently of the test
    // engine. Match both positions before the next simulated menu action.
    ctx->MouseMoveToPos(ImVec2(30, 30));
    OnMainThread(ctx, [] { SDL_WarpMouseInWindow(MainWindow(), 30, 30); });
    ctx->Yield(3);
}
template<class F> bool Until(ImGuiTestContext *ctx, F done) {
    const Uint64 deadline = SDL_GetTicks() + 180000;
    while (!done() && SDL_GetTicks() < deadline) ctx->Sleep(0.1f);
    return done();
}
bool PrintParameters(ImGuiTestContext *ctx) {
    ImGuiWindow *window = ctx->GetWindowByRef("//###PrintParameters");
    return window && window->WasActive;
}
struct LogStart {
    size_t entries = App().State().log.size();
    int repeats = entries ? App().State().log.back().repeats : 0;
};
bool Logged(const LogStart &from, const std::string &text, vdev::LogLevel level = vdev::LogLevel::Info) {
    const auto &log = App().State().log;
    // Consecutive identical messages increment the existing entry. A cancel
    // must emit a new message or increment that entry during this operation.
    for (size_t i = from.entries ? from.entries - 1 : 0; i < log.size(); ++i) {
        if (i + 1 == from.entries && log[i].repeats <= from.repeats) continue;
        if (log[i].level == level && log[i].text.find(text) != std::string::npos) return true;
    }
    return false;
}
struct FileAnswer { bool called = false, mainThread = false; vdev::FileDialogResult result; };
fs::path InputFile() {
    const auto path = Output(u8"\u5bf9\u8bdd\u6846/\u4e2d\u6587 \u7ec4\u5408.cmo");
    fs::create_directories(path.parent_path());
    if (App().State().document && !App().State().document->SourcePath().empty())
        fs::copy_file(fs::u8path(App().State().document->SourcePath()), path, fs::copy_options::overwrite_existing);
    return path;
}
void Request(ImGuiTestContext *ctx, const char *name, const fs::path &path,
             std::shared_ptr<FileAnswer> answer, bool save = false) {
    OnMainThread(ctx, [&] {
        const std::wstring title = vdev::Utf8ToWide(std::string("VirtoolsDev Windows ") + name);
        const wchar_t material[] = L"Compositions\0*.cmo;*.nmo;*.vmo\0All files\0*.*\0";
        const std::wstring filters(material, sizeof(material) / sizeof(material[0]));
        vdev::FileDialogOptions options;
        options.owner = MainWindow(); options.title = title.c_str(); options.filter = filters.c_str();
        options.initial = vdev::Utf8ToWide(path.parent_path().u8string());
        options.save = save; options.extension = save ? L"cmo" : nullptr;
        App().State().RequestFile(options, [answer](const vdev::FileDialogResult &result) {
            answer->result = result; answer->called = true; answer->mainThread = SDL_IsMainThread();
        });
    }); // title, filters and options intentionally die before the user answers.
}
void FourPages(ImGuiTestContext *ctx) {
    ctx->ItemInput("//###PrintParameters/##pagesH"); ctx->KeyCharsReplace("2");
    ctx->ItemInput("//###PrintParameters/##pagesV"); ctx->KeyCharsReplace("2");
    ctx->ItemClick("//###PrintParameters/OK/##dialogbutton"); ctx->Yield(3);
}
struct ContentState {
    bool dirty = App().State().document->Dirty();
    size_t changes = App().State().document->ChangesOutsideUndo();
    size_t steps = App().State().History().UndoCount();
    bool redo = App().State().History().CanRedo();
    bool Unchanged() const {
        return dirty == App().State().document->Dirty() && changes == App().State().document->ChangesOutsideUndo() &&
            steps == App().State().History().UndoCount() && redo == App().State().History().CanRedo() &&
            !App().State().document->EditOpen();
    }
};
}

void RegisterWindowsDialogTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = IM_REGISTER_TEST(engine, "windowsfiles", "native_select_cancel_save");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        SettleDesktop(ctx);
        const auto input = InputFile();
        const ContentState content;
        for (const char *action : {"select", "cancel", "save"}) {
            const bool save = std::string(action) == "save", cancel = std::string(action) == "cancel";
            const auto expected = save ? input.parent_path() / fs::u8path(u8"\u4fdd\u5b58 \u7ec4\u5408.cmo") : input;
            auto answer = std::make_shared<FileAnswer>();
            ctx->LogInfo("Native %s: %s", action, expected.u8string().c_str());
            Request(ctx, action, input, answer, save);
            IM_CHECK(Until(ctx, [&] { return answer->called; }));
            IM_CHECK(answer->mainThread);
            IM_CHECK(answer->result.error.empty());
            if (cancel) {
                IM_CHECK(answer->result.status == vdev::FileDialogResult::Status::Cancelled);
                IM_CHECK(answer->result.paths.empty());
            } else {
                IM_CHECK(answer->result.status == vdev::FileDialogResult::Status::Selected);
                IM_CHECK_EQ(answer->result.paths.size(), size_t(1));
                IM_CHECK(fs::u8path(answer->result.paths.front()) == expected);
                IM_CHECK_EQ(answer->result.filterIndex, 1);
            }
            IM_CHECK(content.Unchanged());
        }
    };

    t = IM_REGISTER_TEST(engine, "windowsfiles", "native_document_replaced_closed");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        SettleDesktop(ctx);
        const auto input = InputFile();
        for (const char *operation : {"New Composition", "Close"}) {
            const auto gate = Output("dialog-continue"); fs::remove(gate);
            const auto answered = Output("dialog-answered"); fs::remove(answered);
            fs::remove(Output("document-changed"));
            auto answer = std::make_shared<FileAnswer>();
            Request(ctx, operation, input, answer);
            // The desktop driver observes the native chooser, then creates
            // this test-owned marker. The editor switches before selection.
            IM_CHECK(Until(ctx, [&] { return fs::exists(gate); }));
            ctx->MenuClick((std::string("//##MainMenuBar/File/") + operation).c_str()); ctx->Yield(3);
            if (std::string(operation) == "Close") IM_CHECK(!App().State().HasDocument());
            else {
                IM_CHECK(App().State().HasDocument());
                IM_CHECK(App().State().document->SourcePath().empty());
                IM_CHECK(App().State().document->Scripts().empty());
            }
            { std::ofstream changed(Output("document-changed")); changed << operation; }
            // Native completion is still delivered; EditorState must discard
            // the completion belonging to the replaced/closed document.
            IM_CHECK(Until(ctx, [&] { return fs::exists(answered); }));
            IM_CHECK(Until(ctx, [&] { return answer.use_count() == 1; }));
            IM_CHECK(!answer->called);
            IM_CHECK(!App().State().document || !App().State().document->EditOpen());
        }
    };

    t = IM_REGISTER_TEST(engine, "windowsfiles", "native_error");
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

    t = IM_REGISTER_TEST(engine, "windowsprint", "native_orientation_resolution");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        SettleDesktop(ctx);
        vdev::PrinterSelection selected;
        ctx->LogInfo("Select Microsoft Print to PDF, A4, landscape.");
        OnMainThread(ctx, [&] { selected = vdev::Printer::Select(MainWindow()); });
        IM_CHECK(selected.status == vdev::PrinterSelectionStatus::Selected);
        const auto &settings = selected.printer->Settings();
        ctx->LogInfo("Selected paper=%dx%d dpi=%dx%d", settings.paper[0], settings.paper[1], settings.dpi[0], settings.dpi[1]);
        IM_CHECK(std::abs(settings.paper[0] - 2970) <= 1); IM_CHECK(std::abs(settings.paper[1] - 2100) <= 1);
        IM_CHECK_EQ(settings.dpi[0], 600); IM_CHECK_EQ(settings.dpi[1], 600);
    };

    for (const char *action : {"setup_cancel", "parameters_cancel", "destination_cancel", "pdf_pagination"}) {
        t = IM_REGISTER_TEST(engine, "windowsprint", action);
        t->GuiFunc = MainThreadGui;
        t->TestFunc = [](ImGuiTestContext *ctx) {
            const char *action = ctx->Test->Name;
            SettleDesktop(ctx, "//Schematic");
            const ContentState content;
            const LogStart log;
            const auto output = Output(u8"\u4e2d\u6587\u6253\u5370.pdf");
            if (std::string(action) == "pdf_pagination") {
                // This scenario also runs alone on a fresh build, before any
                // file-dialog scenario has prepared the output directory.
                fs::create_directories(output.parent_path());
                fs::remove(output);
            }
            ctx->LogInfo("Native print %s; output %s", action, output.u8string().c_str());
            ctx->MenuClick("//##MainMenuBar/File/Print Schematic..."); ctx->Yield(3);
            if (std::string(action) != "setup_cancel") {
                IM_CHECK(PrintParameters(ctx));
                if (std::string(action) == "parameters_cancel") { ctx->KeyPress(ImGuiKey_Escape); ctx->Yield(3); }
                else FourPages(ctx);
            }
            IM_CHECK(!PrintParameters(ctx));
            LogEditor(ctx, log.entries);
            if (log.entries && App().State().log[log.entries-1].repeats > log.repeats)
                ctx->LogInfo("editor: %s (repeats %d -> %d)", App().State().log[log.entries-1].text.c_str(),
                    log.repeats, App().State().log[log.entries-1].repeats);
            if (std::string(action) == "pdf_pagination") {
                IM_CHECK(Until(ctx, [&] { return fs::is_regular_file(output); }));
                IM_CHECK_GT(fs::file_size(output), 1000u);
                IM_CHECK(Logged(log, "Printer job submitted: "));
            } else {
                IM_CHECK(Logged(log, "Print cancelled"));
                IM_CHECK(!Logged(log, "Printer job submitted: "));
                IM_CHECK(!Logged(log, "Cannot start the printer job", vdev::LogLevel::Error));
            }
            IM_CHECK(content.Unchanged());
        };
    }
}
}
