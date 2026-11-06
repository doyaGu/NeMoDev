#include "features/layout/SnapshotCommands.h"
#include "features/layout/LayoutViewport.h"
#include "application/RuntimeOperation.h"
#include "core/document/Document.h"
#include "core/document/DocumentSaver.h"
#include "core/document/OutputFiles.h"
#include "core/document/ObjectPictures.h"
#include "core/document/ScriptSession.h"
#include "core/graph/ScriptCommands.h"
#include "core/layout/SnapshotBuffer.h"
#include "core/runtime/Bitmaps.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <climits>
#include <cctype>
#include <string_view>
#include <cmath>
#include <deque>
#include <filesystem>
#include <set>
#include <utility>

namespace vdev {
namespace {
const EditorValue *Arg(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string_view(request.values[i].key.data, request.values[i].key.size) == key)
            return request.values + i;
    return nullptr;
}
uint64_t Number(const EditorRequest &request, const char *key, uint64_t fallback = 0) {
    const auto *value = Arg(request, key);
    return value ? value->integer : fallback;
}
std::string Text(const EditorRequest &request, const char *key) {
    const auto *value = Arg(request, key);
    return value ? std::string(value->text.data ? value->text.data : "", value->text.size) : std::string();
}
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto values = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
}
struct Bitmap {
    void *handle = nullptr;
    ~Bitmap() {
        if (handle)
            VxDeleteBitmap(handle);
    }
};
struct Filter {
    std::string label, pattern, extension;
};
std::vector<Filter> Filters(bool load) {
    std::vector<Filter> filters;
    auto *plugins = CKGetPluginManager();
    for (int i = 0; plugins && i < plugins->GetPluginCount(CKPLUGIN_BITMAP_READER); ++i) {
        const auto *reader = plugins->GetPluginInfo(CKPLUGIN_BITMAP_READER, i);
        if (!reader || !reader->m_PluginInfo.m_Extension.m_Data[0])
            continue;
        const std::string extension = reader->m_PluginInfo.m_Extension.m_Data;
        auto lower = extension;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (lower.find("vmo") != std::string::npos || lower.find("nms") != std::string::npos ||
            lower.find("cmo") != std::string::npos)
            continue;
        const auto *description = reader->m_PluginInfo.m_Description.CStr();
        filters.push_back(
            {AnsiToUtf8(description ? description : "") + " (*." + extension + ")", "*." + extension, extension});
    }
    if (load && !filters.empty()) {
        Filter all;
        all.label = "All Known Types (";
        for (const auto &filter : filters) {
            all.label += "." + filter.extension + ";";
            all.pattern += filter.pattern + ";";
        }
        all.label.back() = ')';
        all.pattern.pop_back();
        filters.insert(filters.begin(), std::move(all));
    }
    return filters;
}
} // namespace
struct SnapshotCommands::Data {
    EditorApplication &editor;
    RuntimeOperation &operations;
    std::function<LayoutViewport *(EditorView)> viewport;
    std::function<Document *()> current;
    SnapshotBuffer buffer;
    SaveFileOperations *fileOperations = nullptr;
    enum Action { Take, Crop, Remove, Apply, Load, Save, LoadPicture, RemovePicture };
    struct Entry {
        Data *owner;
        Action action;
    } entries[8]{{this, Take}, {this, Crop}, {this, Remove},      {this, Apply},
                 {this, Load}, {this, Save}, {this, LoadPicture}, {this, RemovePicture}};
    struct Pending {
        EditorOperation operation = 0;
        EditorScope scope{};
        ObjectRef picture{};
        uint64_t input = 0;
        bool save = false, running = false, answered = false;
        int slot = -1;
        uint32_t revision = 0;
        std::vector<unsigned char> pixels;
        std::vector<Filter> filters;
        std::string path;
    };
    std::deque<Pending> pending;
    uint64_t nextInput = 1, revision = 1;
    bool stopped = false;
    EditorStatus Version(const EditorRequest &request, int &slot, bool requireImage) const {
        const auto number = Number(request, "slot", UINT64_MAX);
        if (number >= SnapshotBuffer::COUNT)
            return EDITOR_INVALID;
        slot = int(number);
        if (Number(request, "revision", UINT64_MAX) != buffer.Revision(slot))
            return EDITOR_STALE;
        return !requireImage || buffer.Has(slot) ? EDITOR_OK : EDITOR_REFUSED;
    }
    EditorStatus Put(std::vector<unsigned char> pixels, int preferred, EditorSink sink) {
        const int slot = buffer.Put(std::move(pixels), preferred);
        if (slot < 0) {
            Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, "No more room for Snapshots"}});
            return EDITOR_REFUSED;
        }
        ++revision;
        Write(sink, {{"slot", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(slot)},
                     {"revision", EDITOR_INTEGER_VALUE, {}, {}, buffer.Revision(slot)}});
        return EDITOR_OK;
    }
    void Finish(EditorStatus status, std::string message = {}, int slot = -1, const std::string &path = {}) {
        const auto operation = pending.front().operation;
        pending.pop_front();
        EditorResponse result;
        result.status = status;
        result.message = std::move(message);
        if (slot >= 0)
            result.fields.push_back({"slot", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(slot)});
        if (!path.empty())
            result.fields.push_back({"path", EDITOR_TEXT_VALUE, {}, path});
        editor.CompleteOperation(operation, result);
        if (!result.message.empty() && editor.report)
            editor.report(result);
    }
    Document *PictureDocument(const Pending &job) const {
        auto *doc = current();
        const auto scope = editor.Scope(job.scope.view);
        return doc && doc->IsOpen() && doc->Token() == job.picture.document && scope.document == job.scope.document &&
                       (!job.scope.view || scope.view == job.scope.view) && doc->Resolve(job.picture)
                   ? doc
                   : nullptr;
    }
    static EditorStatus ReadPicture(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.current();
        const auto target = Arg(*request, "target")->object;
        if (!doc || doc->Token() != request->scope.document || !doc->Resolve(target))
            return EDITOR_STALE;
        const auto found = doc->Pictures().find(target.object);
        const bool present = found != doc->Pictures().end() && bool(found->second);
        EditorField pixels{"pixels", EDITOR_INTEGER_LIST};
        if (present) {
            std::vector<unsigned char> source;
            int width = 0, height = 0;
            if (!PixelsOfBitmap(found->second.get(), source, width, height))
                return EDITOR_ERROR;
            const auto image =
                ShrinkImage(source.data(), width, height, width * 4, SnapshotBuffer::WIDTH, SnapshotBuffer::HEIGHT);
            for (size_t i = 0; i < image.size(); i += 4)
                pixels.integers.push_back(uint32_t(image[i]) | uint32_t(image[i + 1]) << 8 |
                                          uint32_t(image[i + 2]) << 16 | 0xff000000u);
        }
        return Write(
            sink,
            {{"target", EDITOR_OBJECT, target}, {"present", EDITOR_INTEGER_VALUE, {}, {}, present}, std::move(pixels)});
    }
    static EditorStatus ReadSlots(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        EditorField slots{"slots", EDITOR_RECORD};
        for (int i = 0; i < SnapshotBuffer::COUNT; ++i) {
            EditorField row{std::to_string(i), EDITOR_RECORD};
            row.fields = {{"revision", EDITOR_INTEGER_VALUE, {}, {}, self.buffer.Revision(i)},
                          {"present", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(self.buffer.Has(i))}};
            slots.fields.push_back(std::move(row));
        }
        return Write(sink, {std::move(slots)});
    }
    static EditorStatus ReadPixels(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        int slot = -1;
        const auto valid = self.Version(*request, slot, true);
        if (valid != EDITOR_OK)
            return valid;
        EditorField field{"pixels", EDITOR_INTEGER_LIST};
        const auto &pixels = self.buffer.Pixels(slot);
        for (size_t i = 0; i < pixels.size(); i += 4)
            field.integers.push_back(uint32_t(pixels[i]) | uint32_t(pixels[i + 1]) << 8 |
                                     uint32_t(pixels[i + 2]) << 16 | 0xff000000u);
        return Write(sink, {std::move(field)});
    }
    static EditorStatus Input(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        if (self.pending.empty() || self.pending.front().running || self.pending.front().answered)
            return EDITOR_NO_CHANGE;
        const auto &job = self.pending.front();
        return Write(sink, {{"input", EDITOR_INTEGER_VALUE, {}, {}, job.input},
                            {"operation", EDITOR_INTEGER_VALUE, {}, {}, job.operation},
                            {"kind", EDITOR_INTEGER_VALUE, {}, {}, EDITOR_FILE_INPUT},
                            {"save", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(job.save)},
                            {"title",
                             EDITOR_TEXT_VALUE,
                             {},
                             job.save      ? "Save Bitmap"
                             : job.picture ? "Load Snapshot"
                                           : "Load Bitmap"},
                            {"initial", EDITOR_TEXT_VALUE},
                            {"extension", EDITOR_TEXT_VALUE},
                            {"filters", EDITOR_INTEGER_VALUE, {}, {}, job.filters.size()}});
    }
    static EditorStatus FileFilter(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        if (self.pending.empty() || self.pending.front().input != Number(*request, "input"))
            return EDITOR_STALE;
        const auto &job = self.pending.front();
        const auto index = Number(*request, "index", UINT64_MAX);
        if (job.running || job.answered || index >= job.filters.size())
            return EDITOR_INVALID;
        return Write(sink, {{"label", EDITOR_TEXT_VALUE, {}, job.filters[index].label},
                            {"pattern", EDITOR_TEXT_VALUE, {}, job.filters[index].pattern}});
    }
    static EditorStatus Respond(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
        auto &self = *static_cast<Data *>(data);
        if (self.pending.empty() || self.pending.front().input != Number(*request, "input"))
            return EDITOR_STALE;
        auto &job = self.pending.front();
        if (job.running || job.answered)
            return EDITOR_STALE;
        const auto choice = Text(*request, "choice");
        if (choice == "cancel" || choice == "error") {
            self.Finish(choice == "cancel" ? EDITOR_CANCELLED : EDITOR_ERROR, Text(*request, "message"));
            return EDITOR_OK;
        }
        if (choice != "accept")
            return EDITOR_INVALID;
        auto path = Text(*request, "path");
        if (path.empty() || path.find('\0') != std::string::npos)
            return EDITOR_INVALID;
        const auto type = Number(*request, "filter-index", 1);
        const auto index = type ? type - 1 : 0;
        if ((!job.filters.empty() && index >= job.filters.size()) || (job.filters.empty() && index))
            return EDITOR_INVALID;
        if (job.save && index < job.filters.size() && std::filesystem::u8path(path).extension().empty() &&
            !job.filters[index].extension.empty())
            path += "." + job.filters[index].extension;
        job.path = std::move(path);
        job.answered = true;
        return EDITOR_OK;
    }
    static EditorStatus Run(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &entry = *static_cast<Entry *>(data);
        auto &self = *entry.owner;
        if (self.stopped)
            return EDITOR_REFUSED;
        if (entry.action == Take || entry.action == Crop) {
            int x = 0, y = 0;
            if (entry.action == Crop) {
                const auto *px = Arg(*request, "x"), *py = Arg(*request, "y");
                if (!px || !py || !std::isfinite(px->real) || !std::isfinite(py->real) || px->real < INT_MIN ||
                    px->real > INT_MAX - 104 || py->real < INT_MIN || py->real > INT_MAX - 80)
                    return EDITOR_INVALID;
                x = int(px->real);
                y = int(py->real);
            }
            if (self.buffer.FirstFree() < 0)
                return self.Put({}, -1, sink);
            std::vector<unsigned char> pixels;
            int width = 0, height = 0;
            auto *viewport = self.viewport(request->scope.view);
            if (!viewport) return EDITOR_STALE;
            if (!viewport->CapturePixels(pixels, width, height) || width <= 0 || height <= 0 ||
                width > INT_MAX / 4 || pixels.size() != size_t(width) * size_t(height) * 4)
                return EDITOR_REFUSED;
            return self.Put(entry.action == Take ? ShrinkImage(pixels.data(), width, height, width * 4,
                                                               SnapshotBuffer::WIDTH, SnapshotBuffer::HEIGHT)
                                                 : CaptureArea(pixels.data(), width, height, width * 4, x, y),
                            -1, sink);
        }
        if (entry.action == Remove) {
            const auto *slots = Arg(*request, "slots");
            if (!slots || !slots->fields.count)
                return EDITOR_NO_CHANGE;
            std::set<int> indices;
            for (uint32_t i = 0; i < slots->fields.count; ++i) {
                const auto &field = slots->fields.data[i];
                const std::string key(field.key.data, field.key.size);
                int index = -1;
                for (int j = 0; j < SnapshotBuffer::COUNT; ++j)
                    if (key == std::to_string(j))
                        index = j;
                if (index < 0 || field.type != EDITOR_INTEGER_VALUE || !indices.insert(index).second)
                    return EDITOR_INVALID;
                if (self.buffer.Revision(index) != field.integer)
                    return EDITOR_STALE;
            }
            bool changed = false;
            for (int slot : indices) {
                changed = changed || self.buffer.Has(slot);
                self.buffer.Remove(slot);
            }
            if (changed)
                ++self.revision;
            return changed ? EDITOR_OK : EDITOR_NO_CHANGE;
        }
        if (entry.action == LoadPicture || entry.action == RemovePicture) {
            Pending job;
            job.picture = Arg(*request, "target")->object;
            job.scope = request->scope;
            auto *doc = self.PictureDocument(job);
            if (!doc)
                return EDITOR_STALE;
            if (entry.action == RemovePicture) {
                const auto result = SetObjectPicture(*doc, job.picture.object, nullptr);
                return result.ok ? EDITOR_OK : result.Unchanged() ? EDITOR_NO_CHANGE : EDITOR_ERROR;
            }
            job.operation = request->operation;
            job.input = self.nextInput++;
            job.filters = Filters(true);
            self.pending.push_back(std::move(job));
            return EDITOR_QUEUED;
        }
        int slot = -1;
        const auto valid = self.Version(*request, slot, entry.action != Load);
        if (valid != EDITOR_OK)
            return valid;
        if (entry.action == Apply) {
            auto *doc = self.current();
            const auto *target = Arg(*request, "target");
            const auto script = Number(*request, "script");
            if (script > 1)
                return EDITOR_INVALID;
            auto *object = doc && doc->IsOpen() && doc->Token() == request->scope.document && target
                               ? doc->Resolve(target->object)
                               : nullptr;
            if (!object)
                return EDITOR_STALE;
            const auto &pixels = self.buffer.Pixels(slot);
            if (script) {
                auto *session = doc->Session(object->GetID());
                if (!session || session->ReadOnly())
                    return EDITOR_REFUSED;
                const int height = 36,
                          width = std::min(48, (SnapshotBuffer::WIDTH * height + SnapshotBuffer::HEIGHT / 2) /
                                                   SnapshotBuffer::HEIGHT);
                auto image = ShrinkImage(pixels.data(), SnapshotBuffer::WIDTH, SnapshotBuffer::HEIGHT,
                                         SnapshotBuffer::WIDTH * 4, width, height);
                auto *bitmap = BitmapFromPixels(image.data(), width, height);
                if (!bitmap)
                    return EDITOR_ERROR;
                auto owner = doc->KeepBitmap(bitmap);
                return SetScriptSnapshot(*doc, object->GetID(), bitmap) ? EDITOR_OK : EDITOR_NO_CHANGE;
            }
            auto *bitmap = BitmapFromPixels(pixels.data(), SnapshotBuffer::WIDTH, SnapshotBuffer::HEIGHT);
            if (!bitmap)
                return EDITOR_ERROR;
            const auto result = SetObjectPicture(*doc, object->GetID(), bitmap);
            return result.Unchanged() ? EDITOR_NO_CHANGE : result.ok ? EDITOR_OK : EDITOR_ERROR;
        }
        Pending job;
        job.operation = request->operation;
        job.input = self.nextInput++;
        job.slot = slot;
        job.revision = self.buffer.Revision(slot);
        job.save = entry.action == Save;
        if (job.save)
            job.pixels = self.buffer.Pixels(slot);
        job.filters = Filters(!job.save);
        self.pending.push_back(std::move(job));
        return EDITOR_QUEUED;
    }
    void Poll() {
        if (stopped || pending.empty() || pending.front().running || !pending.front().answered ||
            !editor.NativeAvailable() || editor.HasEdits())
            return;
        auto &job = pending.front();
        if (job.picture && !PictureDocument(job)) {
            Finish(EDITOR_STALE);
            return;
        }
        if (!job.picture && !job.save && buffer.Revision(job.slot) != job.revision) {
            Finish(EDITOR_STALE);
            return;
        }
        struct Result {
            Bitmap picture;
            std::vector<unsigned char> pixels;
            bool saved = false;
            std::string error;
        };
        auto result = std::make_shared<Result>();
        const auto copy = job;
        auto *files = fileOperations;
        job.running = true;
        operations.Queue(
            job.save ? "Save Bitmap" : "Load Bitmap",
            [copy, result, files] {
                if (!copy.save) {
                    Bitmap bitmap{CKLoadBitmap(const_cast<CKSTRING>(copy.path.c_str()))};
                    if (copy.picture) {
                        if (!bitmap.handle)
                            result->error = "Load Snapshot: " + copy.path + " cannot be read";
                        else
                            result->picture.handle = std::exchange(bitmap.handle, nullptr);
                        return;
                    }
                    std::vector<unsigned char> pixels;
                    int width = 0, height = 0;
                    if (!PixelsOfBitmap(bitmap.handle, pixels, width, height)) {
                        result->error = "Load Bitmap: " + copy.path + " cannot be read";
                        return;
                    }
                    result->pixels = ShrinkImage(pixels.data(), width, height, width * 4, SnapshotBuffer::WIDTH,
                                                 SnapshotBuffer::HEIGHT);
                } else {
                    SaveReport report;
                    {
                        OutputFiles outputs(report, files);
                        const auto stage = outputs.Add(copy.path);
                        Bitmap bitmap{
                            BitmapFromPixels(copy.pixels.data(), SnapshotBuffer::WIDTH, SnapshotBuffer::HEIGHT)};
                        if (stage.empty() || !bitmap.handle ||
                            !CKSaveBitmap(const_cast<CKSTRING>(stage.c_str()), bitmap.handle))
                            report.errors.push_back("Save Bitmap: " + copy.path + " cannot be written");
                        else {
                            std::vector<std::string> written;
                            result->saved = outputs.Publish(written);
                        }
                    }
                    for (const auto &error : report.errors)
                        result->error += error + "\n";
                    for (const auto &note : report.notes)
                        result->error += note + "\n";
                }
            },
            [this, result, copy](const std::string &error) {
                const auto message = result->error + (error.empty() ? "" : error);
                if (copy.save && result->saved) {
                    Finish(EDITOR_OK, message, -1, copy.path);
                    return;
                }
                if (!message.empty()) {
                    Finish(EDITOR_ERROR, message);
                    return;
                }
                if (copy.save) {
                    Finish(EDITOR_ERROR, "Save Bitmap failed");
                    return;
                }
                if (stopped) {
                    Finish(EDITOR_CANCELLED);
                    return;
                }
                if (copy.picture) {
                    auto *doc = PictureDocument(copy);
                    if (!doc) {
                        Finish(EDITOR_STALE);
                        return;
                    }
                    const auto resultCode =
                        SetObjectPicture(*doc, copy.picture.object, std::exchange(result->picture.handle, nullptr));
                    Finish(resultCode.ok            ? EDITOR_OK
                           : resultCode.Unchanged() ? EDITOR_NO_CHANGE
                                                    : EDITOR_ERROR,
                           resultCode.message);
                    return;
                }
                if (buffer.Revision(copy.slot) != copy.revision) {
                    Finish(EDITOR_STALE);
                    return;
                }
                const auto slot = buffer.Put(std::move(result->pixels), copy.slot);
                if (slot >= 0)
                    ++revision;
                Finish(slot >= 0 ? EDITOR_OK : EDITOR_REFUSED, slot >= 0 ? "" : "No more room for Snapshots", slot);
            },
            RuntimeOperation::Delivery::Always, RuntimeOperation::Lifetime::Application);
    }
};
SnapshotCommands::SnapshotCommands(EditorApplication &editor, RuntimeOperation &operations,
                                   std::function<LayoutViewport *(EditorView)> viewport,
                                   std::function<Document *()> current)
    : m_Data(new Data{editor, operations, std::move(viewport), std::move(current)}) {}
SnapshotCommands::~SnapshotCommands() = default;
void SnapshotCommands::Poll() { m_Data->Poll(); }
void SnapshotCommands::Stop() {
    m_Data->stopped = true;
    // The application drains runtime work before shutting down the modules.
    auto &self = *m_Data;
    if (!self.pending.empty() && self.pending.front().running) {
        while (self.pending.size() > 1) {
            const auto operation = self.pending.back().operation;
            self.pending.pop_back();
            EditorResponse result;
            result.status = EDITOR_CANCELLED;
            self.editor.CompleteOperation(operation, result);
        }
    } else
        while (!self.pending.empty())
            self.Finish(EDITOR_CANCELLED);
}
void SnapshotCommands::FileOperations(SaveFileOperations *files) { m_Data->fileOperations = files; }
const std::vector<ProviderChoice> &SnapshotCommands::DefaultProviders() {
    static const auto providers = [] {
        std::vector<ProviderChoice> choices;
        for (auto *id : {"snapshot.take", "snapshot.capture", "snapshot.remove", "snapshot.apply", "snapshot.load",
                         "snapshot.save", "snapshot.respond", "object.load-picture", "object.remove-picture"})
            choices.push_back({"command", id, "builtin.snapshots"});
        for (auto *id :
             {"snapshot.slots", "snapshot.pixels", "snapshot.input", "snapshot.file-filter", "object.picture"})
            choices.push_back({"query", id, "builtin.snapshots"});
        choices.push_back({"view", "snapshot.file-dialogs", "builtin.snapshot-dialogs"});
        return choices;
    }();
    return providers;
}
bool SnapshotCommands::Register(std::string &error) {
    const EditorParameter version[] = {{EDITOR_TEXT("slot"), EDITOR_INTEGER_VALUE},
                                       {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}};
    const EditorParameter point[] = {{EDITOR_TEXT("x"), EDITOR_REAL_VALUE}, {EDITOR_TEXT("y"), EDITOR_REAL_VALUE}};
    const EditorParameter slots{EDITOR_TEXT("slots"), EDITOR_RECORD};
    const EditorParameter apply[] = {
        version[0], version[1], {EDITOR_TEXT("target"), EDITOR_OBJECT}, {EDITOR_TEXT("script"), EDITOR_INTEGER_VALUE}};
    const EditorParameter answer[] = {{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("choice"), EDITOR_TEXT_VALUE},
                                      {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, 1},
                                      {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, 1},
                                      {EDITOR_TEXT("filter-index"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter object{EDITOR_TEXT("target"), EDITOR_OBJECT};
    const EditorCommandDesc commands[] = {{sizeof(EditorCommandDesc), EDITOR_TEXT("object.load-picture"),
                                           EDITOR_TEXT("builtin.snapshots"), EDITOR_TEXT("Load Snapshot"), &object, 1,
                                           Data::Run, &m_Data->entries[6], EDITOR_DOCUMENT, EDITOR_FILE_OPERATION},
                                          {sizeof(EditorCommandDesc), EDITOR_TEXT("object.remove-picture"),
                                           EDITOR_TEXT("builtin.snapshots"), EDITOR_TEXT("Remove Snapshot"), &object, 1,
                                           Data::Run, &m_Data->entries[7], EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
                                          {sizeof(EditorCommandDesc), EDITOR_TEXT("snapshot.take"),
                                           EDITOR_TEXT("builtin.snapshots"), EDITOR_TEXT("Take Snapshot"), nullptr, 0,
                                           Data::Run, &m_Data->entries[0], EDITOR_APPLICATION, EDITOR_RUNTIME_CONTROL},
                                          {sizeof(EditorCommandDesc), EDITOR_TEXT("snapshot.capture"),
                                           EDITOR_TEXT("builtin.snapshots"), EDITOR_TEXT("Capture Snapshot"), point, 2,
                                           Data::Run, &m_Data->entries[1], EDITOR_APPLICATION, EDITOR_RUNTIME_CONTROL},
                                          {sizeof(EditorCommandDesc), EDITOR_TEXT("snapshot.remove"),
                                           EDITOR_TEXT("builtin.snapshots"), EDITOR_TEXT("Delete Snapshots"), &slots, 1,
                                           Data::Run, &m_Data->entries[2], EDITOR_APPLICATION, EDITOR_SESSION_CHANGE},
                                          {sizeof(EditorCommandDesc), EDITOR_TEXT("snapshot.apply"),
                                           EDITOR_TEXT("builtin.snapshots"), EDITOR_TEXT("Apply Snapshot"), apply, 4,
                                           Data::Run, &m_Data->entries[3], EDITOR_DOCUMENT, EDITOR_DOCUMENT_CHANGE},
                                          {sizeof(EditorCommandDesc), EDITOR_TEXT("snapshot.load"),
                                           EDITOR_TEXT("builtin.snapshots"), EDITOR_TEXT("Load Bitmap"), version, 2,
                                           Data::Run, &m_Data->entries[4], EDITOR_APPLICATION, EDITOR_RUNTIME_CONTROL},
                                          {sizeof(EditorCommandDesc), EDITOR_TEXT("snapshot.save"),
                                           EDITOR_TEXT("builtin.snapshots"), EDITOR_TEXT("Save Bitmap"), version, 2,
                                           Data::Run, &m_Data->entries[5], EDITOR_APPLICATION, EDITOR_RUNTIME_CONTROL},
                                          {sizeof(EditorCommandDesc), EDITOR_TEXT("snapshot.respond"),
                                           EDITOR_TEXT("builtin.snapshots"), EDITOR_TEXT("Answer Snapshot Dialog"),
                                           answer, 5, Data::Respond, m_Data.get(), EDITOR_APPLICATION,
                                           EDITOR_SESSION_CHANGE}};
    const EditorParameter pixels{EDITOR_TEXT("pixels"), EDITOR_INTEGER_LIST};
    const EditorParameter inputFields[] = {
        {EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE},  {EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE},   {EDITOR_TEXT("save"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE},     {EDITOR_TEXT("initial"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("extension"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("filters"), EDITOR_INTEGER_VALUE}};
    const EditorParameter filterArgs[] = {{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE}};
    const EditorParameter filterFields[] = {{EDITOR_TEXT("label"), EDITOR_TEXT_VALUE},
                                            {EDITOR_TEXT("pattern"), EDITOR_TEXT_VALUE}};
    const EditorParameter pictureFields[] = {object, {EDITOR_TEXT("present"), EDITOR_INTEGER_VALUE}, pixels};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("object.picture"), EDITOR_TEXT("builtin.snapshots"), &object, 1,
         Data::ReadPicture, m_Data.get(), EDITOR_QUERY_DOCUMENT, EDITOR_DOCUMENT, pictureFields, 3},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("snapshot.slots"), EDITOR_TEXT("builtin.snapshots"), nullptr, 0,
         Data::ReadSlots, m_Data.get(), EDITOR_QUERY_VERSIONED, EDITOR_APPLICATION, &slots, 1,
         [](void *data, const EditorHost *) { return static_cast<Data *>(data)->revision; }},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("snapshot.pixels"), EDITOR_TEXT("builtin.snapshots"), version, 2,
         Data::ReadPixels, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, &pixels, 1},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("snapshot.input"), EDITOR_TEXT("builtin.snapshots"), nullptr, 0,
         Data::Input, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, inputFields, 8},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("snapshot.file-filter"), EDITOR_TEXT("builtin.snapshots"), filterArgs, 2,
         Data::FileFilter, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, filterFields, 2}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("layout.snapshots");
    module.data = this;
    module.update = [](void *data, const EditorHost *, EditorModulePhase phase) {
        if (phase == EDITOR_PREPARE_FRAME) static_cast<SnapshotCommands *>(data)->Poll();
    };
    module.stop = [](void *data, const EditorHost *) { static_cast<SnapshotCommands *>(data)->Stop(); };
    module.commands = commands;
    module.commandCount = uint32_t(std::size(commands));
    module.queries = queries;
    module.queryCount = uint32_t(std::size(queries));
    return m_Data->editor.Register(module, error);
}
} // namespace vdev
