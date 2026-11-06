#include "application/OutputCommands.h"
#include "core/document/FileCommands.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace vdev {
namespace {
std::string Text(EditorString value) {
    return {value.data ? value.data : "", value.size};
}
const EditorValue &Field(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (Text(request.values[i].key) == key) return request.values[i];
    throw std::logic_error("Missing output argument");
}
EditorValue Number(const char *key, uint64_t value) {
    return {EditorLiteral(key), EDITOR_INTEGER_VALUE, {}, {}, value};
}
EditorValue String(const char *key, const std::string &value) {
    return {EditorLiteral(key), EDITOR_TEXT_VALUE, {}, EditorText(value)};
}
int Ceil(float value, int part) {
    return std::max(1, int(std::ceil(double(value) / std::max(1, part))));
}
bool Positive(uint64_t value) {
    return value && value <= uint64_t(std::numeric_limits<int>::max());
}
} // namespace
struct OutputCommands::Data {
    EditorApplication &editor;
    DocumentFiles &files;
    RuntimeOperation &runtime;
    std::function<Document *()> current;
    Presentation &presentation;
    bool snapshots = true;
    uint64_t nextInput = 0;
    std::string lastPage, lastTitle = "Virtools Generated Page";
    SchematicPrint printOptions;
    struct Job {
        Result result;
        RuntimeOperation::DocumentScope scope;
        uint64_t input = 0;
        EditorInputKind kind = EDITOR_WEB_PAGE_INPUT;
        DocumentFiles::WebPage web;
        bool preview = false, writing = false;
        bool finished = false;
        std::unique_ptr<SchematicPrintContent> content;
        std::shared_ptr<PrintDestination> destination;
        int paper[2]{}, device[2]{}, perMm[2]{4, 4}, resolution[2]{}, pages[2]{1, 1};
    };
    std::shared_ptr<Job> active;
    struct Command {
        Data *owner;
        bool print;
    };
    Command webCommand{this, false}, printCommand{this, true};
    Data(EditorApplication &editor, DocumentFiles &files, RuntimeOperation &runtime,
         std::function<Document *()> current, Presentation &presentation)
        : editor(editor), files(files), runtime(runtime), current(std::move(current)), presentation(presentation) {}
    void Ask(EditorInputKind kind) {
        active->kind = kind;
        active->input = ++nextInput;
    }
    void Finish(const std::shared_ptr<Job> &job, EditorStatus status) {
        if (job->finished) return;
        job->finished = true;
        job->result.status = status;
        EditorResponse response;
        response.status = status;
        response.message = job->result.message;
        response.fields = {{"path", EDITOR_TEXT_VALUE, {}, job->result.path},
                           {"written", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(job->result.written.size())},
                           {"printer-job", EDITOR_TEXT_VALUE, {}, job->result.printerJob}};
        EditorField outputs{"outputs", EDITOR_TEXT_LIST};
        outputs.texts = job->result.written;
        response.fields.push_back(std::move(outputs));
        if (active == job) active.reset();
        // Application-owned completion reports actual external effects even if
        // the initiating view or document disappeared during native work.
        try {
            presentation.OutputFinished(job->result);
        } catch (...) {
            if (!response.message.empty()) response.message += "; ";
            response.message += "Cannot show the output result";
        }
        editor.CompleteOperation(job->result.operation, std::move(response));
    }
    static EditorStatus Check(void *data, const EditorHost *, const EditorRequest *request) {
        auto &command = *static_cast<Command *>(data);
        auto &d = *command.owner;
        if (d.active || d.runtime.Busy() || !d.runtime.CanQueue("document output") || !d.current() ||
            !d.current()->IsOpen())
            return EDITOR_REFUSED;
        if (!command.print) {
            const auto width = Field(*request, "width").integer, height = Field(*request, "height").integer;
            if ((width && !Positive(width)) || (height && !Positive(height)) || Field(*request, "cmo").integer > 1 ||
                Field(*request, "preview").integer > 1 || Field(*request, "ask-options").integer > 1)
                return EDITOR_INVALID;
        }
        return EDITOR_OK;
    }
    static EditorStatus Start(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
        auto &command = *static_cast<Command *>(data);
        auto &d = *command.owner;
        auto job = std::make_shared<Job>();
        job->scope = d.runtime.CurrentDocument();
        job->result.operation = request->operation;
        job->result.print = command.print;
        job->result.path = Text(Field(*request, "path").text);
        d.active = job;
        try {
            if (command.print) {
                job->content = d.presentation.CapturePrint(job->result.message);
                if (!job->content || !std::isfinite(job->content->width) || !std::isfinite(job->content->height) ||
                    job->content->width <= 0 || job->content->height <= 0 || job->content->width > 100000000 ||
                    job->content->height > 100000000) {
                    if (job->result.message.empty()) job->result.message = "No visible Script to print";
                    d.Finish(job, EDITOR_ERROR);
                    return EDITOR_QUEUED;
                }
                auto selected = d.presentation.ChoosePrinter(job->result.path);
                if (selected.status != PrinterSelectionStatus::Selected || !selected.destination) {
                    job->result.message =
                        selected.status == PrinterSelectionStatus::Cancelled ? "Print cancelled" : selected.error;
                    d.Finish(job,
                             selected.status == PrinterSelectionStatus::Cancelled ? EDITOR_CANCELLED : EDITOR_ERROR);
                    return EDITOR_QUEUED;
                }
                job->destination = std::move(selected.destination);
                const auto &settings = job->destination->Settings();
                for (int axis = 0; axis < 2; ++axis) {
                    if (settings.paper[axis] < 10 || settings.paper[axis] > 100000 || settings.dpi[axis] <= 0 ||
                        settings.dpi[axis] > 100000)
                        throw std::runtime_error("Invalid printer settings");
                    job->paper[axis] = settings.paper[axis];
                    const int mm = std::max(1, settings.paper[axis] / 10 - 10);
                    job->device[axis] = std::max(1, int(mm * double(settings.dpi[axis]) / 25.4));
                    job->resolution[axis] = 4 * mm;
                }
                job->content->width = std::max(job->content->width, float(job->resolution[0]));
                job->pages[0] = Ceil(job->content->width, job->resolution[0]);
                job->pages[1] = Ceil(job->content->height, job->resolution[1]);
                d.Ask(EDITOR_PRINT_INPUT);
            } else {
                auto &web = job->web;
                web.path = job->result.path.empty() ? d.lastPage : job->result.path;
                if (web.path.empty()) web.path = (std::filesystem::current_path() / "Virtools.htm").u8string();
                web.title = Text(Field(*request, "title").text);
                if (web.title.empty()) web.title = d.lastTitle;
                const auto width = Field(*request, "width").integer, height = Field(*request, "height").integer;
                web.width = width ? int(width) : 320;
                web.height = height ? int(height) : 240;
                web.cmo = Field(*request, "cmo").integer != 0;
                web.saveScriptSnapshots = d.snapshots;
                job->preview = Field(*request, "preview").integer != 0;
                if (job->result.path.empty() || Field(*request, "ask-options").integer)
                    d.Ask(EDITOR_WEB_PAGE_INPUT);
                else
                    d.WriteWeb(job);
            }
        } catch (const std::exception &error) {
            job->result.message = error.what();
            if (job->destination) job->destination->Cancel();
            d.Finish(job, EDITOR_ERROR);
        } catch (...) {
            job->result.message = "Preparing document output failed";
            if (job->destination) job->destination->Cancel();
            d.Finish(job, EDITOR_ERROR);
        }
        return EDITOR_QUEUED;
    }
    void WriteWeb(const std::shared_ptr<Job> &job) {
        job->web.layout = presentation.CaptureLayout();
        job->result.path = job->web.path;
        job->writing = true;
        job->input = 0;
        if (!files.ExportWebPage(job->web, [this, job](const DocumentFiles::WebResult &result) {
                job->result.webFailure = result.failure;
                job->result.written = result.written;
                job->result.message = result.message;
                if (result.saved && result.completed && job->preview) {
                    std::string error;
                    try {
                        if (!presentation.OpenOutput(job->web.path, error))
                            job->result.message += (job->result.message.empty() ? "" : "; ") + error;
                    } catch (...) {
                        job->result.message += "; Cannot open the web page preview";
                    }
                }
                Finish(job, result.saved && result.completed ? EDITOR_OK : EDITOR_ERROR);
            }))
            Finish(job, EDITOR_REFUSED);
    }
    void Print(const std::shared_ptr<Job> &job) {
        auto &out = *job->destination;
        std::string error;
        bool submitted = false;
        try {
            const uint64_t count = uint64_t(job->pages[0]) * job->pages[1];
            const double scale =
                std::max(1.0, out.ImageFiles() ? std::min(2.0, double(job->device[0]) / job->resolution[0])
                                               : double(job->device[0]) / job->resolution[0]);
            const double w = job->resolution[0] * scale + 0.5, h = job->resolution[1] * scale + 0.5;
            if (!count || count > 10000 || w > 1000000 || h > 1000000)
                throw std::runtime_error("Print dimensions are too large");
            const int width = std::max(1, int(w)), height = std::max(1, int(h));
            if (!out.PreparePages(int(count), width, height, error)) throw std::runtime_error(error);
            const auto begun = out.BeginDocument("Schematic View", error);
            if (begun != PrinterStartStatus::Started) {
                out.Cancel();
                job->result.written = out.WrittenFiles();
                job->result.message = begun == PrinterStartStatus::Cancelled ? "Print cancelled" : error;
                Finish(job, begun == PrinterStartStatus::Cancelled ? EDITOR_CANCELLED : EDITOR_ERROR);
                return;
            }
            const double dx = out.ImageFiles() ? 1 : double(job->device[0]) / width;
            const double dy = out.ImageFiles() ? 1 : double(job->device[1]) / height;
            for (uint64_t page = 0; page < count; ++page) {
                if (!out.BeginPage(error)) throw std::runtime_error(error);
                const float x = float(double(job->resolution[0]) * (page % job->pages[0]));
                const float y = float(double(job->resolution[1]) * (page / job->pages[0]));
                for (int ty = 0; ty < height; ty += 2048)
                    for (int tx = 0; tx < width; tx += 2048) {
                        const int tw = std::min(2048, width - tx), th = std::min(2048, height - ty);
                        std::vector<unsigned char> pixels;
                        if (!job->content->Render(x + float(tx / scale), y + float(ty / scale), tw, th, float(scale),
                                                   printOptions, pixels, error) ||
                            pixels.size() != size_t(tw) * size_t(th) * 4)
                            throw std::runtime_error(error.empty() ? "Cannot render a Schematic print tile" : error);
                        if (!out.DrawTile(pixels.data(), tw, th, int(tx * dx + 0.5), int(ty * dy + 0.5),
                                          int((tx + tw) * dx + 0.5), int((ty + th) * dy + 0.5), error))
                            throw std::runtime_error(error);
                    }
                if (!out.EndPage(error)) throw std::runtime_error(error);
            }
            submitted = out.Submit(job->result.printerJob, error);
        } catch (const std::exception &failure) {
            error = failure.what();
        } catch (...) {
            error = "Printing failed";
        }
        if (!submitted) {
            out.Cancel();
            if (error.empty()) error = "Printing failed";
        }
        job->result.written = out.WrittenFiles();
        job->result.message = submitted ? out.SubmittedMessage(job->result.printerJob) : error;
        Finish(job, submitted ? EDITOR_OK : EDITOR_ERROR);
    }
    static EditorStatus Reply(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
        auto &d = *static_cast<Data *>(data);
        const auto job = d.active;
        if (!job || !job->input || job->input != Field(*request, "input").integer) return EDITOR_STALE;
        if (!job->scope.IsCurrent()) {
            d.Finish(job, EDITOR_STALE);
            return EDITOR_STALE;
        }
        const auto choice = Text(Field(*request, "choice").text);
        if (choice == "cancel" && (job->kind == EDITOR_FILE_INPUT || job->kind == EDITOR_CONFIRM_INPUT)) {
            d.Ask(EDITOR_WEB_PAGE_INPUT);
            return EDITOR_OK;
        }
        if (choice == "cancel" || choice == "error") {
            if (job->destination) job->destination->Cancel();
            job->result.message = choice == "error"   ? Text(Field(*request, "message").text)
                                  : job->result.print ? "Print cancelled"
                                                      : std::string();
            d.Finish(job, choice == "error" ? EDITOR_ERROR : EDITOR_CANCELLED);
            return EDITOR_OK;
        }
        if (choice == "destination" && job->kind == EDITOR_WEB_PAGE_INPUT) {
            const auto width = Field(*request, "width").integer, height = Field(*request, "height").integer;
            if (width > uint64_t(std::numeric_limits<int>::max()) ||
                height > uint64_t(std::numeric_limits<int>::max()) || Field(*request, "cmo").integer > 1 ||
                Field(*request, "preview").integer > 1)
                return EDITOR_INVALID;
            job->web.path = Text(Field(*request, "path").text);
            job->web.title = Text(Field(*request, "title").text);
            job->web.width = int(width);
            job->web.height = int(height);
            job->web.cmo = Field(*request, "cmo").integer != 0;
            job->preview = Field(*request, "preview").integer != 0;
            d.Ask(EDITOR_FILE_INPUT);
            return EDITOR_OK;
        }
        if (choice != "accept") return EDITOR_INVALID;
        try {
            if (job->kind == EDITOR_FILE_INPUT) {
                const auto path = Text(Field(*request, "path").text);
                if (path.empty()) return EDITOR_INVALID;
                job->web.path = path;
                d.Ask(EDITOR_WEB_PAGE_INPUT);
                return EDITOR_OK;
            }
            if (job->kind == EDITOR_CONFIRM_INPUT) {
                d.WriteWeb(job);
                return EDITOR_OK;
            }
            if (job->kind == EDITOR_WEB_PAGE_INPUT) {
                const auto width = Field(*request, "width").integer, height = Field(*request, "height").integer;
                const auto cmo = Field(*request, "cmo").integer, preview = Field(*request, "preview").integer;
                const auto path = Text(Field(*request, "path").text);
                if (!Positive(width) || !Positive(height) || cmo > 1 || preview > 1 || path.empty())
                    return EDITOR_INVALID;
                job->web.path = path;
                job->web.title = Text(Field(*request, "title").text);
                job->web.width = int(width);
                job->web.height = int(height);
                job->web.cmo = cmo != 0;
                job->preview = preview != 0;
                d.lastPage = path;
                d.lastTitle = job->web.title;
                std::error_code error;
                if (std::filesystem::exists(std::filesystem::u8path(WebPageCompositionPath(path, job->web.cmo)), error))
                    d.Ask(EDITOR_CONFIRM_INPUT);
                else
                    d.WriteWeb(job);
                return EDITOR_OK;
            }
            if (job->kind == EDITOR_PRINT_INPUT) {
                job->input = 0;
                d.Print(job);
                return EDITOR_OK;
            }
        } catch (const std::exception &error) {
            job->result.message = error.what();
            d.Finish(job, EDITOR_ERROR);
            return EDITOR_ERROR;
        } catch (...) {
            job->result.message = "Document output failed";
            d.Finish(job, EDITOR_ERROR);
            return EDITOR_ERROR;
        }
        return EDITOR_INVALID;
    }
    static EditorStatus Setting(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
        auto &d = *static_cast<Data *>(data);
        const auto job = d.active;
        if (!job || job->input != Field(*request, "input").integer || job->kind != EDITOR_PRINT_INPUT)
            return EDITOR_STALE;
        if (!job->scope.IsCurrent()) return EDITOR_STALE;
        const auto name = Text(Field(*request, "field").text);
        const char *fields[] = {"per-mm-h",    "per-mm-v",  "resolution-h", "resolution-v", "pages-h",    "pages-v",
                                "fill-header", "fill-body", "fill-shapes",  "bitmaps",      "color-links"};
        const auto found = std::find(std::begin(fields), std::end(fields), name);
        if (found == std::end(fields)) return EDITOR_INVALID;
        const auto index = size_t(found - std::begin(fields));
        const auto value = Field(*request, "value").integer;
        if (index < 6 ? !Positive(value) : value > 1) return EDITOR_INVALID;
        if (index >= 6) {
            bool *options[] = {&d.printOptions.fillHeader, &d.printOptions.fillBody, &d.printOptions.fillShapes,
                               &d.printOptions.bitmaps, &d.printOptions.colorLinks};
            *options[index - 6] = value != 0;
            return EDITOR_OK;
        }
        const auto axis = index % 2;
        const auto mm = std::max(1, job->paper[axis] / 10 - 10);
        const auto total = axis ? job->content->height : job->content->width;
        if (index < 2 && value > uint64_t(std::numeric_limits<int>::max() / mm)) return EDITOR_INVALID;
        if (index < 2) {
            job->perMm[axis] = int(value);
            job->resolution[axis] = int(value) * mm;
            job->pages[axis] = Ceil(total, job->resolution[axis]);
        } else if (index < 4) {
            job->resolution[axis] = int(value);
            job->perMm[axis] = int(value) / mm;
            job->pages[axis] = Ceil(total, int(value));
        } else {
            job->pages[axis] = int(value);
            job->resolution[axis] = std::max(1, int(total) / int(value));
            job->perMm[axis] = job->resolution[axis] / mm;
        }
        return EDITOR_OK;
    }
    static EditorStatus ReadInput(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &d = *static_cast<Data *>(data);
        const auto j = d.active;
        if (!j || !j->input) return EDITOR_NO_CHANGE;
        const std::string title = j->kind == EDITOR_CONFIRM_INPUT ? "Overwrite File"
                                  : j->kind == EDITOR_FILE_INPUT  ? "Select file to save"
                                                                  : std::string();
        const std::string message = j->kind == EDITOR_CONFIRM_INPUT
                                        ? "The Virtools file already exists, do you want to overwrite it ?"
                                        : std::string();
        const EditorValue values[] = {Number("input", j->input),  Number("operation", j->result.operation),
                                      Number("kind", j->kind),    String("title", title),
                                      String("message", message), String("path", j->web.path)};
        return sink.write ? sink.write(sink.data, values, 6) : EDITOR_INVALID;
    }
    static EditorStatus ReadOptions(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        const auto &d = *static_cast<Data *>(data);
        const auto j = d.active;
        if (!j || !j->input || j->input != Field(*request, "input").integer) return EDITOR_STALE;
        const EditorValue values[] = {String("path", j->web.path),
                                      String("title", j->web.title),
                                      Number("width", j->web.width),
                                      Number("height", j->web.height),
                                      Number("cmo", j->web.cmo),
                                      Number("preview", j->preview),
                                      Number("per-mm-h", j->perMm[0]),
                                      Number("per-mm-v", j->perMm[1]),
                                      Number("resolution-h", j->resolution[0]),
                                      Number("resolution-v", j->resolution[1]),
                                      Number("pages-h", j->pages[0]),
                                      Number("pages-v", j->pages[1]),
                                      Number("fill-header", d.printOptions.fillHeader),
                                      Number("fill-body", d.printOptions.fillBody),
                                      Number("fill-shapes", d.printOptions.fillShapes),
                                      Number("bitmaps", d.printOptions.bitmaps),
                                      Number("color-links", d.printOptions.colorLinks)};
        return sink.write ? sink.write(sink.data, values, 17) : EDITOR_INVALID;
    }
};
OutputCommands::OutputCommands(EditorApplication &editor, DocumentFiles &files, RuntimeOperation &runtime,
                               std::function<Document *()> current, Presentation &presentation)
    : m_Data(std::make_unique<Data>(editor, files, runtime, std::move(current), presentation)) {}
OutputCommands::~OutputCommands() = default;
void OutputCommands::SaveScriptSnapshots(bool enabled) {
    m_Data->snapshots = enabled;
}
bool OutputCommands::Busy() const {
    return bool(m_Data->active);
}
void OutputCommands::Poll() {
    if (m_Data->active && !m_Data->runtime.Busy() && !m_Data->active->scope.IsCurrent()) Stop();
}
void OutputCommands::Stop() {
    const auto job = m_Data->active;
    if (!job || (job->writing && m_Data->runtime.Busy())) return;
    if (job->destination) job->destination->Cancel();
    job->result.message = job->result.print ? "Print cancelled" : std::string();
    m_Data->Finish(job, job->scope.IsCurrent() ? EDITOR_CANCELLED : EDITOR_STALE);
}
bool OutputCommands::Register(std::string &error) {
    const EditorParameter web[] = {
        {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, 1},          {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE, 1},
        {EDITOR_TEXT("width"), EDITOR_INTEGER_VALUE, 1},      {EDITOR_TEXT("height"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("cmo"), EDITOR_INTEGER_VALUE, 1},        {EDITOR_TEXT("preview"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("ask-options"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter reply[] = {
        {EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE},    {EDITOR_TEXT("choice"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, 1},     {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE, 1},
        {EDITOR_TEXT("width"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("height"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("cmo"), EDITOR_INTEGER_VALUE, 1},   {EDITOR_TEXT("preview"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, 1}};
    const EditorParameter setting[] = {{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE},
                                       {EDITOR_TEXT("field"), EDITOR_TEXT_VALUE},
                                       {EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE}};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("document.export-web"), EDITOR_TEXT("builtin.outputs"),
         EDITOR_TEXT("Create Web Page..."), web, 7, &Data::Start, &m_Data->webCommand, EDITOR_DOCUMENT,
         EDITOR_FILE_OPERATION, &Data::Check},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("document.print"), EDITOR_TEXT("builtin.outputs"),
         EDITOR_TEXT("Print Schematic..."), web, 1, &Data::Start, &m_Data->printCommand, EDITOR_DOCUMENT,
         EDITOR_FILE_OPERATION, &Data::Check},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("output.respond"), EDITOR_TEXT("builtin.outputs"),
         EDITOR_TEXT("Answer output request"), reply, 9, &Data::Reply, m_Data.get(), EDITOR_APPLICATION,
         EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("output.print-setting"), EDITOR_TEXT("builtin.outputs"),
         EDITOR_TEXT("Set print option"), setting, 3, &Data::Setting, m_Data.get(), EDITOR_APPLICATION,
         EDITOR_SESSION_CHANGE}};
    const EditorParameter inputFields[] = {
        {EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE},  {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE},  {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE}};
    const EditorParameter input{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE};
    const EditorParameter optionFields[] = {{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE},
                                            {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE},
                                            {EDITOR_TEXT("width"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("height"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("cmo"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("preview"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("per-mm-h"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("per-mm-v"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("resolution-h"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("resolution-v"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("pages-h"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("pages-v"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("fill-header"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("fill-body"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("fill-shapes"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("bitmaps"), EDITOR_INTEGER_VALUE},
                                            {EDITOR_TEXT("color-links"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("output.input"), EDITOR_TEXT("builtin.outputs"), nullptr, 0,
         &Data::ReadInput, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, inputFields, 6},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("output.options"), EDITOR_TEXT("builtin.outputs"), &input, 1,
         &Data::ReadOptions, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, optionFields, 17}};
    const EditorMenuLocation locations[] = {{EDITOR_TEXT("file.web"), EDITOR_TEXT("File: Web Page")},
                                            {EDITOR_TEXT("file.print"), EDITOR_TEXT("File: Print")}};
    const EditorMenuDesc menus[] = {{EDITOR_TEXT("menu.document.export-web"), EDITOR_TEXT("document.export-web"),
                                     EDITOR_TEXT("file.web"), EDITOR_TEXT("Create Web Page..."), 0},
                                    {EDITOR_TEXT("menu.document.print"), EDITOR_TEXT("document.print"),
                                     EDITOR_TEXT("file.print"), EDITOR_TEXT("Print Schematic..."), 1}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("documents.outputs");
    module.data = this;
    module.update = [](void *data, const EditorHost *, EditorModulePhase phase) {
        if (phase == EDITOR_PREPARE_FRAME) static_cast<OutputCommands *>(data)->Poll();
    };
    module.stop = [](void *data, const EditorHost *) { static_cast<OutputCommands *>(data)->Stop(); };
    module.commands = commands;
    module.commandCount = 4;
    module.queries = queries;
    module.queryCount = 2;
    module.locations = locations;
    module.locationCount = 2;
    module.menus = menus;
    module.menuCount = 2;
    return m_Data->editor.Register(module, error);
}
} // namespace vdev
