#include "features/resources/DataResourceCommands.h"
#include "core/document/BehaviorDescription.h"
#include "core/document/DocumentSaver.h"
#include "core/document/OutputFiles.h"
#include "core/document/ToolCommands.h"
#include "core/runtime/TextEncoding.h"
#include "workspace/EditorClient.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <deque>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <set>
#include <stdexcept>

namespace vdev {
namespace {
std::string Text(EditorString value) { return {value.data ? value.data : "", value.size}; }
const EditorValue &Field(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (Text(request.values[i].key) == key)
            return request.values[i];
    throw std::invalid_argument("Missing resource argument");
}
std::string Absolute(const std::string &path) {
    if (path.empty())
        return {};
    return std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal().u8string();
}
std::string ClassName(CK_CLASSID cid) {
    const auto *name = cid ? CKClassIDToString(cid) : nullptr;
    return name ? AnsiToUtf8(name) : "";
}
std::string Version(float version) {
    char text[64];
    std::snprintf(text, sizeof(text), "%g", version);
    return text;
}
EditorStatus Write(EditorSink sink, const std::vector<EditorValue> &values) {
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
}
} // namespace
struct DataResourceCommands::State {
    struct File {
        std::string name, path, type, author, description, version;
        BehaviorDescription native;
        bool described = false, behavior = false;
    };
    struct Folder {
        std::string name, path, type;
        uint64_t parent = 0;
        CK_CLASSID cid = 0;
        std::vector<File> files;
    };
    struct Library {
        DataResource native;
        std::vector<Folder> folders;
        bool shown = false;
    };
    enum class Action { Open, Create, Refresh, Information, Respond, Browse };
    struct Pending {
        EditorOperation operation = 0;
        EditorScope origin{};
        uint64_t id = 0;
        Action action = Action::Open;
        std::string path, title;
        File file;
    } pending;
    struct Handler {
        State *self;
        Action action;
    };
    struct QueryHandler {
        State *self;
        std::string id;
    };
    struct ScanResult { Library library; std::string error; bool ok = false, written = false; };
    struct Scan {
        EditorOperation operation = 0;
        EditorScope origin{};
        EditorView view = 0;
        std::string path;
        bool refresh = false;
        std::shared_ptr<std::atomic<bool>> cancelled;
        std::future<ScanResult> result;
        std::function<ScanResult()> work;
    } scan;
    std::deque<Scan> scans;
    EditorApplication &editor;
    CkRuntimeHost &runtime;
    std::function<bool()> nativeAvailable;
    std::map<std::string, Library> libraries;
    std::vector<std::string> open;
    uint64_t revision = 1, nextInput = 1, viewsRevision = 0;
    bool stopped = false;
    std::vector<Handler> handlers;
    std::vector<QueryHandler> queries;
    std::vector<EditorField> answer;
    State(EditorApplication &editor, CkRuntimeHost &runtime, std::function<bool()> available)
        : editor(editor), runtime(runtime), nativeAvailable(std::move(available)) {}
    bool NativeReady() const { return nativeAvailable ? nativeAvailable() : editor.NativeAvailable(); }
    bool CanRun(Action action, const EditorRequest &request) const {
        if (stopped) return false;
        if (action == Action::Respond || action == Action::Browse) return true;
        if (pending.operation) return false;
        if (scan.operation && (action == Action::Information ||
            ((action == Action::Open || action == Action::Create) && !Field(request, "path").text.size))) return false;
        return true;
    }

    static bool Append(Library &library, const DataResourceFolder &native, const std::function<bool()> &cancelled,
                       uint64_t parent = 0) {
        if (cancelled && cancelled()) return false;
        Folder folder;
        folder.name = native.name;
        folder.path = native.path;
        folder.parent = parent;
        folder.cid = native.cid;
        for (const auto &name : native.files) {
            if (cancelled && cancelled()) return false;
            File file;
            file.name = name;
            file.path = (std::filesystem::u8path(native.path) / std::filesystem::u8path(name)).u8string();
            auto extension = std::filesystem::u8path(name).extension().u8string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            file.behavior = extension == ".nms";
            file.described = file.behavior && ReadBehaviorDescription(file.path, file.native);
            file.author = AnsiToUtf8(file.native.author);
            file.description = AnsiToUtf8(file.native.description);
            if (file.described)
                file.version = Version(file.native.version);
            folder.files.push_back(std::move(file));
        }
        library.folders.push_back(std::move(folder));
        const auto index = library.folders.size();
        for (const auto &child : native.folders)
            if (!Append(library, child, cancelled, index)) return false;
        return true;
    }
    static bool Read(const std::string &path, Library &library, std::string &error,
                     const std::function<bool()> &cancelled = {}) {
        if (!ReadDataResource(path, library.native, &error, cancelled))
            return false;
        return Append(library, library.native.root, cancelled);
    }
    EditorStatus BeginScan(const EditorRequest &request, std::string path, EditorView view, bool create, bool refresh) {
        if (create) {
            auto extension = std::filesystem::u8path(path).extension().u8string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (extension != ".rsc") path += ".rsc";
        }
        auto cancelled = std::make_shared<std::atomic<bool>>(false);
        std::function<ScanResult()> work = [path, create, cancelled] {
            ScanResult result;
            if (cancelled->load()) return result;
            if (create) result.written = CreateDataResource(path, &result.error);
            if ((!create || result.written) && !cancelled->load())
                result.ok = Read(path, result.library, result.error, [cancelled] { return cancelled->load(); });
            return result;
        };
        Scan next{request.operation, request.scope, view, std::move(path), refresh, std::move(cancelled), {}, std::move(work)};
        if (scan.operation) scans.push_back(std::move(next));
        else {
            next.result = std::async(std::launch::async, std::move(next.work));
            scan = std::move(next);
        }
        return EDITOR_QUEUED;
    }
    EditorStatus Open(const EditorHost *host, const EditorRequest &request, const std::string &path, EditorView view,
                      EditorSink sink, bool create) {
        std::string file = Absolute(path), error;
        if (file.empty())
            return EDITOR_INVALID;
        if (create) return BeginScan(request, file, view, true, false);
        auto found = libraries.find(file);
        if (found == libraries.end()) {
            found = std::find_if(libraries.begin(), libraries.end(),
                                 [&](const auto &entry) { return _stricmp(entry.first.c_str(), file.c_str()) == 0; });
            if (found != libraries.end())
                file = found->first;
        }
        if (found == libraries.end()) return BeginScan(request, file, view, false, false);
        const bool added = !found->second.shown;
        bool created = false;
        if (!view) {
            EditorClient client(host, request.scope.view);
            const auto instances = client.Query("workspace.instances");
            const auto *ids = EditorClient::Find(instances, "views"), *types = EditorClient::Find(instances, "types");
            if (instances.status == EDITOR_OK && ids && types)
                for (size_t i = 0; i < std::min(ids->integers.size(), types->texts.size()); ++i)
                    if (types->texts[i] == "resource.data" &&
                        EditorClient::Text(client.Inspect(ids->integers[i]), "library") == file) {
                        view = ids->integers[i];
                        break;
                    }
            if (!view) {
                const auto opened = client.Command("workspace.open-view", {TextArgument("type", "resource.data")});
                view = EditorClient::Integer(opened, "view");
                created = view != 0;
            }
        }
        const auto value = TextArgument("path", file);
        const EditorRequest navigation{request.scope, &value, 1, {}, request.operation};
        EditorStatus status =
            view ? host->navigateView(host->data, EditorString EDITOR_TEXT("resource.data"), view, &navigation, sink)
                 : EDITOR_ERROR;
        if (status != EDITOR_OK) {
            if (created)
                EditorClient(host, 0).Command("workspace.close-view", {IntegerArgument("view", view)});
            if (added) {
                libraries.erase(found);
                ++revision;
            }
        } else {
            found->second.shown = true;
            const auto count = AddDataResourcePaths(runtime.Context(), found->second.native);
            if (added) {
                const auto message =
                    "Opened Data Resource " + file + " (" + std::to_string(count) + " paths added to the path manager)";
                Write(sink, {TextArgument("message", message)});
            }
        }
        if (status != EDITOR_OK) {
            const auto message = "Cannot open the Data Resource view: " + file;
            Write(sink, {TextArgument("message", message)});
        }
        Write(sink, {TextArgument("path", file), IntegerArgument("written", create)});
        return status;
    }
    void Publish(EditorOperation operation, EditorResponse result) {
        editor.CompleteOperation(operation, result);
        if (!result.message.empty() && editor.report) {
            try {
                editor.report(result);
            } catch (...) {
            }
        }
    }
    void Complete(EditorStatus status, const std::string &message = {}, const std::string &written = {}) {
        const auto operation = pending.operation;
        pending = {};
        answer.clear();
        EditorResponse result;
        result.status = status;
        result.message = message;
        if (!written.empty())
            result.fields = {{"path", EDITOR_TEXT_VALUE, {}, written}, {"written", EDITOR_INTEGER_VALUE, {}, {}, 1}};
        Publish(operation, std::move(result));
    }
    EditorStatus Run(Action action, const EditorHost *host, const EditorRequest &request, EditorSink sink) {
        if (!CanRun(action, request))
            return EDITOR_REFUSED;
        if (action == Action::Browse) {
            const EditorValue values[] = {Field(request, "path"), Field(request, "folder"), Field(request, "files")};
            const EditorRequest navigation{request.scope, values, 3, {}, request.operation};
            return host->navigateView(host->data, EditorString EDITOR_TEXT("resource.data"),
                                      Field(request, "view").integer, &navigation, sink);
        }
        if (action == Action::Respond) {
            if (!pending.operation || Field(request, "input").integer != pending.id)
                return EDITOR_STALE;
            const auto choice = Text(Field(request, "choice").text);
            if (choice == "cancel" || choice == "error") {
                Complete(choice == "cancel" ? EDITOR_CANCELLED : EDITOR_ERROR, Text(Field(request, "message").text));
                return EDITOR_OK;
            }
            if (choice != "accept")
                return EDITOR_INVALID;
            if (!NativeReady()) {
                if (!answer.empty())
                    return EDITOR_REFUSED;
                return CopyEditorFields(answer, request.values, request.count);
            }
            if (pending.action == Action::Information) {
                auto description = pending.file.native;
                const auto versionText = Text(Field(request, "version").text);
                char *end = nullptr;
                const auto version = std::strtof(versionText.c_str(), &end);
                if ((!versionText.empty() && end != versionText.c_str() + versionText.size()) ||
                    !std::isfinite(version))
                    return EDITOR_INVALID;
                description.name = description.name.empty()
                                       ? Utf8ToAnsi(std::filesystem::u8path(pending.path).stem().u8string())
                                       : description.name;
                description.author = Utf8ToAnsi(Text(Field(request, "author").text));
                description.version = version;
                description.description = Utf8ToAnsi(Text(Field(request, "description").text));
                SaveReport report;
                OutputFiles outputs(report);
                const auto staged = outputs.Add(BehaviorDescriptionPath(pending.path));
                // The description writer derives .dsc from its NMS argument.
                const auto stagedNms = staged.empty() ? "" : staged.substr(0, staged.size() - 3) + "nms";
                BehaviorDescription read;
                std::vector<std::string> written;
                const bool ok = !staged.empty() && WriteBehaviorDescription(stagedNms, description) &&
                                ReadBehaviorDescription(stagedNms, read) && read.name == description.name &&
                                read.author == description.author && read.version == description.version &&
                                read.description == description.description && outputs.Publish(written);
                const auto path = pending.path;
                if (ok) {
                    for (auto &library : libraries) for (auto &folder : library.second.folders)
                        for (auto &file : folder.files) if (file.path == path) {
                            file.native = description; file.described = true;
                            file.author = AnsiToUtf8(description.author);
                            file.description = AnsiToUtf8(description.description);
                            file.version = Version(description.version);
                        }
                    ++revision;
                }
                Complete(ok ? EDITOR_OK : EDITOR_ERROR, ok ? "" : "Cannot write Behavior Information: " + path,
                         ok ? BehaviorDescriptionPath(path) : "");
                return EDITOR_OK;
            }
            const auto path = Text(Field(request, "path").text);
            const auto saved = pending;
            EditorResponse result;
            EditorSink output{&result, [](void *data, const EditorValue *values, uint32_t count) {
                                  auto &result = *static_cast<EditorResponse *>(data);
                                  std::vector<EditorField> fields;
                                  const auto status = CopyEditorFields(fields, values, count);
                                  result.fields.insert(result.fields.end(), fields.begin(), fields.end());
                                  return status;
                              }};
            EditorRequest original{saved.origin, nullptr, 0, {}, saved.operation};
            result.status = Open(host, original, path, 0, output, saved.action == Action::Create);
            for (auto it = result.fields.begin(); it != result.fields.end(); ++it)
                if (it->key == "message") {
                    result.message = it->text;
                    result.fields.erase(it);
                    break;
                }
            pending = {};
            if (result.status != EDITOR_QUEUED) Publish(saved.operation, std::move(result));
            return EDITOR_OK;
        }
        if (pending.operation)
            return EDITOR_REFUSED;
        const auto path = Absolute(Text(Field(request, "path").text));
        if (action == Action::Open || action == Action::Create) {
            if (!path.empty())
                return Open(host, request, path, Field(request, "view").integer, sink, action == Action::Create);
            pending = {request.operation,
                       request.scope,
                       nextInput++,
                       action,
                       {},
                       action == Action::Create ? "Create Data Resource..." : "Open Data Resource...",
                       {}};
            return EDITOR_QUEUED;
        }
        if (action == Action::Refresh) {
            const auto found = libraries.find(path);
            if (found == libraries.end())
                return EDITOR_STALE;
            return BeginScan(request, path, 0, false, true);
        }
        for (const auto &library : libraries)
            for (const auto &folder : library.second.folders)
                for (const auto &file : folder.files)
                    if (file.path == path && file.behavior) {
                        pending = {request.operation,      request.scope, nextInput++, action, path,
                                   "Behavior Information", file};
                        return EDITOR_QUEUED;
                    }
        return EDITOR_STALE;
    }
    EditorStatus Query(const std::string &id, const EditorRequest &request, EditorSink sink) const;
};
EditorStatus DataResourceCommands::State::Query(const std::string &id, const EditorRequest &request,
                                                EditorSink sink) const {
    if (stopped)
        return EDITOR_STALE;
    if (id == "resource.input") {
        if (!pending.operation)
            return EDITOR_NO_CHANGE;
        return Write(sink,
                     {IntegerArgument("input", pending.id), IntegerArgument("operation", pending.operation),
                      IntegerArgument("kind", pending.action == Action::Information ? EDITOR_BEHAVIOR_INFORMATION_INPUT
                                                                                    : EDITOR_FILE_INPUT),
                      IntegerArgument("save", pending.action == Action::Create), TextArgument("title", pending.title),
                      TextArgument("initial", ""), TextArgument("extension", "rsc"), IntegerArgument("filters", 1)});
    }
    if (id == "resource.file-filter") {
        if (Field(request, "input").integer != pending.id || !pending.operation)
            return EDITOR_STALE;
        if (Field(request, "index").integer)
            return EDITOR_INVALID;
        return Write(sink, {TextArgument("label", "Virtools Resources (*.rsc)"), TextArgument("pattern", "*.rsc")});
    }
    if (id == "resource.export-options") {
        if (Field(request, "input").integer != pending.id || pending.action != Action::Information ||
            !pending.operation)
            return EDITOR_STALE;
        const auto name = pending.file.described ? AnsiToUtf8(pending.file.native.name)
                                                 : std::filesystem::u8path(pending.path).stem().u8string();
        return Write(sink, {TextArgument("name", name), TextArgument("class", pending.file.type),
                            TextArgument("author", pending.file.author), TextArgument("version", pending.file.version),
                            TextArgument("description", pending.file.description), IntegerArgument("hide", 0),
                            IntegerArgument("objects", 0)});
    }
    const auto found = libraries.find(Text(Field(request, "path").text));
    if (found == libraries.end())
        return EDITOR_STALE;
    const auto &library = found->second;
    if (id == "resource.library")
        return Write(sink,
                     {IntegerArgument("revision", revision), TextArgument("path", library.native.file),
                      TextArgument("root", library.native.root.path), TextArgument("title", library.native.root.name)});
    const bool folders = id == "resource.folders";
    const Folder *folder = nullptr;
    if (!folders) {
        const auto path = Text(Field(request, "folder").text);
        for (const auto &entry : library.folders)
            if (entry.path == path) {
                folder = &entry;
                break;
            }
        if (!folder)
            return EDITOR_STALE;
    }
    const auto total = folders ? library.folders.size() : folder->files.size();
    const auto first = Field(request, "first").integer;
    if (first > total)
        return EDITOR_INVALID;
    const auto end = std::min(total, size_t(first) + 64);
    std::vector<EditorString> names, paths, types, authors, versions, descriptions;
    std::vector<uint64_t> classes, parents, behaviors;
    for (size_t i = size_t(first); i < end; ++i) {
        if (folders) {
            const auto &item = library.folders[i];
            names.push_back(EditorText(item.name));
            paths.push_back(EditorText(item.path));
            types.push_back(EditorText(item.type));
            classes.push_back(uint64_t(item.cid));
            parents.push_back(item.parent);
        } else {
            const auto &item = folder->files[i];
            names.push_back(EditorText(item.name));
            paths.push_back(EditorText(item.path));
            types.push_back(EditorText(item.type));
            authors.push_back(EditorText(item.author));
            versions.push_back(EditorText(item.version));
            descriptions.push_back(EditorText(item.description));
            behaviors.push_back(item.behavior);
        }
    }
    auto texts = [](const char *key, const std::vector<EditorString> &values) {
        return EditorValue{
            EditorLiteral(key), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {values.data(), uint32_t(values.size())}};
    };
    auto integers = [](const char *key, const std::vector<uint64_t> &values) {
        return EditorValue{
            EditorLiteral(key), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {values.data(), uint32_t(values.size())}};
    };
    std::vector<EditorValue> values{IntegerArgument("revision", revision), IntegerArgument("total", total),
                                    texts("names", names), texts("paths", paths), texts("types", types)};
    if (folders) {
        values.push_back(integers("classes", classes));
        values.push_back(integers("parents", parents));
    } else {
        values.push_back(IntegerArgument("class", uint64_t(folder->cid)));
        values.push_back(texts("authors", authors));
        values.push_back(texts("versions", versions));
        values.push_back(texts("descriptions", descriptions));
        values.push_back(integers("behaviors", behaviors));
    }
    return Write(sink, values);
}
bool DataResourceCommands::Register(std::string &error) {
    auto &s = *m_State;
    const EditorParameter open[] = {{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, 1},
                                    {EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter path{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE};
    const EditorParameter browse[] = {{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, 1},
                                      {EDITOR_TEXT("folder"), EDITOR_TEXT_VALUE, 1},
                                      {EDITOR_TEXT("files"), EDITOR_TEXT_LIST, 1},
                                      {EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter respond[] = {
        {EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE},      {EDITOR_TEXT("choice"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, 1},       {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, 1},
        {EDITOR_TEXT("author"), EDITOR_TEXT_VALUE, 1},     {EDITOR_TEXT("version"), EDITOR_TEXT_VALUE, 1},
        {EDITOR_TEXT("description"), EDITOR_TEXT_VALUE, 1}};
    s.handlers = {{&s, State::Action::Open},        {&s, State::Action::Create},  {&s, State::Action::Refresh},
                  {&s, State::Action::Information}, {&s, State::Action::Respond}, {&s, State::Action::Browse}};
    const char *ids[] = {"resource.open-library",         "resource.create-library", "resource.refresh-library",
                         "resource.behavior-information", "resource.respond",        "resource.browse"};
    const char *titles[] = {"Open Data Resource",      "Create New Data Resource",  "Refresh Data Resource",
                            "Behavior Information...", "Respond to resource input", "Browse Data Resource"};
    std::vector<EditorCommandDesc> commands;
    for (size_t i = 0; i < std::size(ids); ++i) {
        commands.push_back({sizeof(EditorCommandDesc), EditorLiteral(ids[i]), EDITOR_TEXT("builtin.resources"),
                            EditorLiteral(titles[i]),
                            i == 5   ? browse
                            : i == 4 ? respond
                            : i < 2  ? open
                                     : &path,
                            i == 5   ? 4u
                            : i == 4 ? 7u
                            : i < 2  ? 2u
                                     : 1u,
                            [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
                                auto &handler = *static_cast<State::Handler *>(data);
                                return handler.self->Run(handler.action, host, *request, sink);
                            },
                            &s.handlers[i], EDITOR_WORKSPACE, i >= 4 ? EDITOR_SESSION_CHANGE : EDITOR_FILE_OPERATION,
                            [](void *data, const EditorHost *, const EditorRequest *request) {
                                const auto &handler = *static_cast<State::Handler *>(data);
                                return handler.self->CanRun(handler.action, *request) ? EDITOR_OK : EDITOR_REFUSED;
                            }});
    }
    const EditorParameter inputFields[] = {
        {EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE},  {EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE},   {EDITOR_TEXT("save"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE},     {EDITOR_TEXT("initial"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("extension"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("filters"), EDITOR_INTEGER_VALUE}};
    const EditorParameter input{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE};
    const EditorParameter filter[] = {input, {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE}};
    const EditorParameter filterFields[] = {{EDITOR_TEXT("label"), EDITOR_TEXT_VALUE},
                                            {EDITOR_TEXT("pattern"), EDITOR_TEXT_VALUE}};
    const EditorParameter infoFields[] = {
        {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE},        {EDITOR_TEXT("class"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("author"), EDITOR_TEXT_VALUE},      {EDITOR_TEXT("version"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("description"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("hide"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("objects"), EDITOR_INTEGER_VALUE}};
    const EditorParameter libraryFields[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
                                             {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE},
                                             {EDITOR_TEXT("root"), EDITOR_TEXT_VALUE},
                                             {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE}};
    const EditorParameter folders[] = {path, {EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter files[] = {
        path, {EDITOR_TEXT("folder"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter folderFields[] = {
        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("names"), EDITOR_TEXT_LIST},        {EDITOR_TEXT("paths"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("types"), EDITOR_TEXT_LIST},        {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST},
        {EDITOR_TEXT("parents"), EDITOR_INTEGER_LIST}};
    const EditorParameter fileFields[] = {
        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("names"), EDITOR_TEXT_LIST},        {EDITOR_TEXT("paths"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("types"), EDITOR_TEXT_LIST},        {EDITOR_TEXT("class"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("authors"), EDITOR_TEXT_LIST},      {EDITOR_TEXT("versions"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("descriptions"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("behaviors"), EDITOR_INTEGER_LIST}};
    s.queries = {{&s, "resource.input"},   {&s, "resource.file-filter"}, {&s, "resource.export-options"},
                 {&s, "resource.library"}, {&s, "resource.folders"},     {&s, "resource.files"}};
    std::vector<EditorQueryDesc> queries;
    const EditorParameter *parameters[] = {nullptr, filter, &input, &path, folders, files};
    const uint32_t counts[] = {0, 2, 1, 1, 2, 3};
    const EditorParameter *fields[] = {inputFields, filterFields, infoFields, libraryFields, folderFields, fileFields};
    const uint32_t fieldCounts[] = {8, 2, 7, 4, 7, 10};
    for (size_t i = 0; i < s.queries.size(); ++i)
        queries.push_back(
            {sizeof(EditorQueryDesc), EditorText(s.queries[i].id), EDITOR_TEXT("builtin.resources"), parameters[i],
             counts[i],
             [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
                 const auto &query = *static_cast<State::QueryHandler *>(data);
                 return query.self->Query(query.id, *request, sink);
             },
             &s.queries[i], i < 3 ? EDITOR_QUERY_EACH_READ : EDITOR_QUERY_VERSIONED, EDITOR_WORKSPACE, fields[i],
             fieldCounts[i],
             i < 3 ? nullptr
                   : +[](void *data,
                         const EditorHost *) { return static_cast<State::QueryHandler *>(data)->self->revision; }});
    const EditorParameter importArgs[] = {{EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST},
                                          {EDITOR_TEXT("image-kind"), EDITOR_INTEGER_VALUE}};
    const EditorParameter importFields[] = {{EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST},
                                            {EDITOR_TEXT("images"), EDITOR_INTEGER_LIST}};
    queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("resource.import-classes"), EDITOR_TEXT("builtin.resources"),
        importArgs, 2, [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
            if (static_cast<State *>(data)->stopped) return EDITOR_STALE;
            const auto kind = Field(*request, "image-kind").integer;
            const auto input = Field(*request, "classes").integers;
            if (kind > 4) return EDITOR_INVALID;
            const uint64_t targets[] = {0, CKCID_TEXTURE, CKCID_SPRITE, CKCID_SPRITE3D, CKCID_2DENTITY};
            std::vector<uint64_t> classes, images;
            for (uint32_t i = 0; i < input.count; ++i) {
                const auto cid = input.data[i];
                if (cid > INT32_MAX) return EDITOR_INVALID;
                const bool image = cid == CKCID_TEXTURE || cid == CKCID_SPRITE || cid == CKCID_SPRITE3D;
                classes.push_back(image && kind ? targets[kind] : cid); images.push_back(image);
            }
            return Write(sink, {
                {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {classes.data(), uint32_t(classes.size())}},
                {EDITOR_TEXT("images"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {images.data(), uint32_t(images.size())}}});
        }, &s, EDITOR_QUERY_EACH_READ, EDITOR_WORKSPACE, importFields, 2});
    const EditorMenuLocation location{EDITOR_TEXT("resources.library"), EDITOR_TEXT("Data Resources menu")};
    const EditorMenuDesc menus[] = {{EDITOR_TEXT("resource.open-library"), EDITOR_TEXT("resource.open-library"),
                                     EDITOR_TEXT("resources.library"), EDITOR_TEXT("Open Data Resource"), 0},
                                    {EDITOR_TEXT("resource.create-library"), EDITOR_TEXT("resource.create-library"),
                                     EDITOR_TEXT("resources.library"), EDITOR_TEXT("Create New Data Resource"), 1}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("resources.libraries");
    module.data = this;
    module.update = [](void *data, const EditorHost *, EditorModulePhase phase) {
        if (phase == EDITOR_PREPARE_FRAME) static_cast<DataResourceCommands *>(data)->Poll();
    };
    module.stop = [](void *data, const EditorHost *) { static_cast<DataResourceCommands *>(data)->Stop(); };
    static const EditorString environment = EDITOR_TEXT("environment.settings");
    module.optionalDependencies = &environment; module.optionalDependencyCount = 1;
    module.commands = commands.data();
    module.commandCount = uint32_t(commands.size());
    module.queries = queries.data();
    module.queryCount = uint32_t(queries.size());
    module.locations = &location;
    module.locationCount = 1;
    module.menus = menus;
    module.menuCount = 2;
    return s.editor.Register(module, error);
}
DataResourceCommands::DataResourceCommands(EditorApplication &editor, CkRuntimeHost &runtime,
                                           std::function<bool()> available)
    : m_State(std::make_unique<State>(editor, runtime, std::move(available))) {}
bool DataResourceCommands::Busy() const { return m_State->pending.operation != 0; }
DataResourceCommands::~DataResourceCommands() { Stop(); }
const std::vector<std::string> &DataResourceCommands::OpenLibraries() const { return m_State->open; }
void DataResourceCommands::Stop() {
    if (m_State->stopped)
        return;
    if (m_State->pending.operation)
        m_State->Complete(EDITOR_CANCELLED);
    if (m_State->scan.operation) {
        m_State->scan.cancelled->store(true);
        EditorResponse result; result.status = EDITOR_CANCELLED;
        try {
            const auto read = m_State->scan.result.get();
            if (read.written) result.fields = {{"path", EDITOR_TEXT_VALUE, {}, m_State->scan.path},
                {"written", EDITOR_INTEGER_VALUE, {}, {}, 1}};
        } catch (const std::exception &error) { result.message = error.what(); }
        m_State->Publish(m_State->scan.operation, std::move(result));
        m_State->scan = {};
    }
    for (const auto &scan : m_State->scans) {
        EditorResponse result; result.status = EDITOR_CANCELLED;
        m_State->Publish(scan.operation, std::move(result));
    }
    m_State->scans.clear();
    m_State->libraries.clear();
    m_State->open.clear();
    m_State->stopped = true;
    ++m_State->revision;
}
void DataResourceCommands::Poll() {
    auto &s = *m_State;
    if (s.stopped)
        return;
    if (s.scan.operation) {
        if (s.scan.origin.view && s.editor.Query("workspace.view", {s.scan.origin, nullptr, 0}).status == EDITOR_STALE)
            s.scan.cancelled->store(true);
        if ((s.scan.cancelled->load() || s.NativeReady()) &&
            s.scan.result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto scan = std::move(s.scan);
            s.scan = {};
            EditorResponse result;
            try {
                auto read = scan.result.get();
                result.status = scan.cancelled->load() ? EDITOR_CANCELLED : read.ok ? EDITOR_OK : EDITOR_ERROR;
                result.message = read.error;
                result.fields = {{"path", EDITOR_TEXT_VALUE, {}, scan.path},
                                 {"written", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(read.written)}};
                if (result.status == EDITOR_OK) {
                    for (auto &folder : read.library.folders) {
                        folder.type = ClassName(folder.cid);
                        for (auto &file : folder.files) file.type = ClassName(file.native.cid);
                    }
                    if (scan.refresh) {
                        const auto old = s.libraries.find(scan.path);
                        if (old == s.libraries.end()) result.status = EDITOR_STALE;
                        else { read.library.shown = old->second.shown; old->second = std::move(read.library); ++s.revision; }
                    } else {
                        s.libraries.insert_or_assign(scan.path, std::move(read.library));
                        ++s.revision;
                        const EditorSink output{&result, [](void *data, const EditorValue *values, uint32_t count) {
                            std::vector<EditorField> fields;
                            const auto status = CopyEditorFields(fields, values, count);
                            auto &result = *static_cast<EditorResponse *>(data);
                            for (auto &field : fields) {
                                if (field.key == "message") result.message = field.text;
                                else {
                                    const auto found = std::find_if(result.fields.begin(), result.fields.end(),
                                        [&](const auto &old) { return old.key == field.key; });
                                    if (found == result.fields.end()) result.fields.push_back(std::move(field));
                                }
                            }
                            return status;
                        }};
                        // Navigation runs as the same registered command used
                        // by menus and external modules. The completed scan is
                        // already in the cache, so this call performs no I/O.
                        const auto shown = EditorClient(s.editor.Host(), scan.origin.view).Command("resource.open-library",
                            {TextArgument("path", scan.path), IntegerArgument("view", scan.view)});
                        result.status = shown.status;
                        const auto borrowed = BorrowEditorFields(shown.fields);
                        output.write(output.data, borrowed.data(), uint32_t(borrowed.size()));
                        if (result.message.empty()) result.message = shown.message;
                    }
                }
            } catch (const std::exception &error) { result.status = EDITOR_ERROR; result.message = error.what(); }
            catch (...) { result.status = EDITOR_ERROR; result.message = "Cannot read Data Resource"; }
            s.Publish(scan.operation, std::move(result));
        }
    }
    if (!s.scan.operation && !s.scans.empty()) {
        s.scan = std::move(s.scans.front()); s.scans.pop_front();
        if (s.scan.origin.view && s.editor.Query("workspace.view", {s.scan.origin, nullptr, 0}).status == EDITOR_STALE)
            s.scan.cancelled->store(true);
        try { s.scan.result = std::async(std::launch::async, std::move(s.scan.work)); }
        catch (const std::exception &error) {
            const auto operation = s.scan.operation;
            s.scan = {};
            s.Publish(operation, {EDITOR_ERROR, error.what()});
        }
    }
    if (s.pending.origin.view &&
        s.editor.Query("workspace.view", {s.pending.origin, nullptr, 0}).status == EDITOR_STALE)
        s.Complete(EDITOR_CANCELLED);
    if (!s.answer.empty() && s.NativeReady()) {
        const auto answer = std::move(s.answer);
        s.answer.clear();
        std::vector<EditorValue> values;
        for (const auto &field : answer)
            values.push_back({EditorText(field.key), field.type, {}, EditorText(field.text), field.integer});
        s.editor.Execute("resource.respond", {s.editor.Scope(), values.data(), uint32_t(values.size())});
    }
    uint64_t revision = 0;
    if (s.editor.QueryRevision("workspace.instances", revision) != EDITOR_OK || revision == s.viewsRevision)
        return;
    s.viewsRevision = revision;
    EditorClient client(s.editor.Host(), 0);
    const auto instances = client.Query("workspace.instances");
    const auto *ids = EditorClient::Find(instances, "views"), *types = EditorClient::Find(instances, "types");
    if (instances.status != EDITOR_OK || !ids || !types || ids->integers.size() != types->texts.size())
        return;
    std::vector<std::string> open;
    for (size_t i = 0; i < ids->integers.size(); ++i)
        if (types->texts[i] == "resource.data") {
            const auto path = EditorClient::Text(client.Inspect(ids->integers[i]), "library");
            if (!path.empty() && std::find(open.begin(), open.end(), path) == open.end())
                open.push_back(path);
        }
    for (auto it = s.libraries.begin(); it != s.libraries.end();)
        if (std::find(open.begin(), open.end(), it->first) == open.end()) {
            it = s.libraries.erase(it);
            ++s.revision;
        } else
            ++it;
    if (open != s.open) {
        s.open = std::move(open);
        if (changed)
            changed();
    }
}
} // namespace vdev
