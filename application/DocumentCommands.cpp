#include "application/DocumentCommands.h"
#include "core/catalog/ClassInfo.h"

#include "core/document/FileCommands.h"
#include "core/runtime/TextEncoding.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace vdev {
namespace {
std::string Text(EditorString value) {
    return {value.data ? value.data : "", value.size};
}
const EditorValue *Field(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (Text(request.values[i].key) == key) return &request.values[i];
    return nullptr;
}
bool Exists(const std::string &path) {
    std::error_code error;
    return std::filesystem::exists(std::filesystem::u8path(path), error);
}
} // namespace

struct DocumentCommands::State {
    EditorApplication &editor;
    DocumentFiles &files;
    RuntimeOperation &runtime;
    std::function<Document *()> current;
    Presentation &presentation;
    Settings settings;
    uint64_t next = 1;
    struct Command {
        State *owner;
        Action action;
        const char *id, *title;
    };
    std::deque<Command> commands;
    struct Operation {
        Result result;
        RuntimeOperation::Request native;
        RuntimeOperation::DocumentScope scope;
        Settings settings;
        DocumentFiles::ResourceLoad resources;
        DocumentFiles::ExportRequest exportRequest;
        DocumentFiles::GraphRequest graphRequest;
        DocumentFiles::SnapshotRequest snapshotRequest;
        EditorView view = 0;
        std::string initial;
        bool askOptions = false, askDescription = false;
        bool saveBeforeReplace = false;
        bool reversionConfirmed = false, finished = false;
    };
    std::shared_ptr<Operation> active;
    std::unique_ptr<Input> input;
    std::string lastSaved;
    struct DocumentInfoData {
        uint64_t document = 0;
        bool open = false, dirty = false, separateScene = false;
        std::string path, level, scene;
    } documentInfo;
    uint64_t documentInfoRevision = 1;
    std::shared_ptr<Operation> waitingQuit;
    State(EditorApplication &editor, DocumentFiles &files, RuntimeOperation &runtime,
          std::function<Document *()> current, Presentation &presentation)
        : editor(editor), files(files), runtime(runtime), current(std::move(current)), presentation(presentation) { PublishDocumentInfo(); }
    bool HasDocument() const { return current() && current()->IsOpen(); }
    static bool ApplicationAction(Action action) {
        return action == Action::New || action == Action::Open || action == Action::Quit;
    }
    static bool Replacement(Action action) {
        return action == Action::New || action == Action::Open || action == Action::Close || action == Action::Quit;
    }
    static bool GraphFile(Action action) { return action == Action::ImportGraph || action == Action::ReplaceGraph; }
    static bool SourceViewRequired(Action action) { return GraphFile(action) || action == Action::LoadSnapshot; }
    std::string CurrentPath() const {
        return !lastSaved.empty() ? lastSaved : HasDocument() ? current()->SourcePath() : std::string();
    }
    void Ask(Input::Kind kind, Input::File file = Input::File::Composition, bool save = false, std::string title = {},
             std::string initial = {}, std::string text = {}) {
        input = std::make_unique<Input>(Input{next++, active->result.operation, kind, file, save, std::move(title),
                                              std::move(text), std::move(initial)});
        input->action = active->result.action;
        if (kind == Input::Kind::Unsaved) input->initial = lastSaved;
        if (kind == Input::Kind::File) {
            if (file == Input::File::Player) {
                input->extension = "vmo";
                input->filters = {{"Virtools Player File(*.vmo)", "*.vmo"}};
            } else if (file == Input::File::Objects) {
                input->extension = "nmo";
                input->filters = {{"Virtools Files (*.nmo)", "*.nmo"}, {"All files (*.*)", "*.*"}};
            } else if (file == Input::File::Behaviors) {
                input->extension = "nms";
                input->filters = {{"Virtools Behaviors (*.nms)", "*.nms"}, {"All files (*.*)", "*.*"}};
            } else if (file == Input::File::Image) {
                input->filters = {{"Images (*.bmp;*.png;*.jpg;*.jpeg;*.gif;*.tif;*.tiff)",
                                   "*.bmp;*.png;*.jpg;*.jpeg;*.gif;*.tif;*.tiff"},
                                  {"All files (*.*)", "*.*"}};
            } else if (file == Input::File::Composition) {
                input->extension = "cmo";
                input->filters = {{"Virtools compositions (*.cmo;*.nmo;*.vmo)", "*.cmo;*.nmo;*.vmo"},
                                  {"All files (*.*)", "*.*"}};
            } else {
                std::string all;
                if (auto *plugins = CKGetPluginManager())
                    for (int i = 0; i < plugins->GetPluginCount(CKPLUGIN_MODEL_READER); ++i) {
                        auto *entry = plugins->GetPluginInfo(CKPLUGIN_MODEL_READER, i);
                        if (!entry) continue;
                        std::string extension = entry->m_PluginInfo.m_Extension.m_Data;
                        for (auto &c : extension)
                            c = char(std::tolower(static_cast<unsigned char>(c)));
                        if (extension.empty() || extension == "vmo" || extension == "nms" || extension == "cmo")
                            continue;
                        all += (all.empty() ? "*." : ";*.") + extension;
                        input->filters.emplace_back(AnsiToUtf8(entry->m_PluginInfo.m_Description.CStr()
                                                                   ? entry->m_PluginInfo.m_Description.CStr()
                                                                   : "") +
                                                        " (*." + extension + ")",
                                                    "*." + extension);
                    }
                input->filters.insert(input->filters.begin(), {"All Known Types (" + all + ")", all});
                input->filters.emplace_back("All files (*.*)", "*.*");
                static const char *titles[] = {"Import File", "Import as Character", "Import as Place",
                                               "Import as Scene"};
                input->title = titles[int(input->action) - int(Action::Import)];
            }
        }
    }
    void PublishDocumentInfo() {
        if (runtime.Busy()) return;
        const auto *document = current();
        const bool open = document && document->IsOpen();
        std::string levelName, sceneName;
        bool separateScene = false;
        if (open)
            if (auto *level = document->Context()->GetCurrentLevel()) {
                levelName = AnsiToUtf8(level->GetName() ? level->GetName() : "Level");
                auto *scene = level->GetCurrentScene();
                separateScene = scene && scene != level->GetLevelScene();
                if (separateScene) sceneName = AnsiToUtf8(scene->GetName() ? scene->GetName() : "Scene");
            }
        const std::string empty;
        const auto &path = !open ? empty : !lastSaved.empty() ? lastSaved : document->SourcePath();
        const auto token = open ? document->Token().value : 0;
        const bool dirty = open && document->Dirty();
        if (documentInfo.open != open || documentInfo.document != token || documentInfo.dirty != dirty ||
            documentInfo.path != path || documentInfo.level != levelName || documentInfo.scene != sceneName ||
            documentInfo.separateScene != separateScene) {
            documentInfo = {token, open, dirty, separateScene, path, std::move(levelName), std::move(sceneName)};
            ++documentInfoRevision;
        }
    }
    static EditorStatus DocumentInfo(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &info = static_cast<State *>(data)->documentInfo;
        const EditorValue fields[] = {
            {EDITOR_TEXT("open"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(info.open)},
            {EDITOR_TEXT("document"), EDITOR_INTEGER_VALUE, {}, {}, info.document},
            {EDITOR_TEXT("dirty"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(info.dirty)},
            {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, EditorText(info.path)},
            {EDITOR_TEXT("level"), EDITOR_TEXT_VALUE, {}, EditorText(info.level)},
            {EDITOR_TEXT("scene"), EDITOR_TEXT_VALUE, {}, EditorText(info.scene)},
            {EDITOR_TEXT("separate-scene"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(info.separateScene)}};
        return sink.write ? sink.write(sink.data, fields, uint32_t(std::size(fields))) : EDITOR_INVALID;
    }
    void PublishDuplicates() {
        const auto *pending = files.PendingDuplicates();
        if (!pending) {
            if (input && input->kind == Input::Kind::DuplicateNames) input.reset();
            return;
        }
        if (runtime.Busy()) return;
        if (active && SourceViewRequired(active->result.action) && active->view &&
            !editor.Scope(active->view).document.value) {
            files.ResolveDuplicates(false);
            return;
        }
        if (input && (input->kind != Input::Kind::DuplicateNames || input->duplicateSerial == pending->serial)) return;
        auto nextInput = std::make_unique<Input>();
        nextInput->id = next++;
        nextInput->operation = active ? active->result.operation : 0;
        nextInput->kind = Input::Kind::DuplicateNames;
        nextInput->title = "Duplicate Names Found";
        nextInput->duplicateSerial = pending->serial;
        struct Category { CK_CLASSID cid; const char *name; };
        static const Category categories[] = {
            {CKCID_CHARACTER, "Characters"}, {CKCID_SPRITE3D, "3D Sprites"}, {CKCID_BODYPART, "Bodyparts"},
            {CKCID_3DOBJECT, "3D Objects"}, {CKCID_GRID, "Grids"}, {CKCID_CAMERA, "Cameras"},
            {CKCID_LIGHT, "Lights"}, {CKCID_3DENTITY, "3D Frames"}, {CKCID_DATAARRAY, "Arrays"},
            {CKCID_MESH, "Meshes"}, {CKCID_MATERIAL, "Materials"}, {CKCID_TEXTURE, "Textures"},
            {CKCID_GROUP, "Groups"}, {CKCID_SPRITE, "2D Sprites"}, {CKCID_OBJECT, "Others"}};
        for (const auto &duplicate : pending->duplicates) {
            size_t category = 0;
            while (category + 1 < std::size(categories) && !CKIsChildClassOf(duplicate.cid, categories[category].cid))
                ++category;
            EditorField row{std::to_string(nextInput->duplicates.size()), EDITOR_RECORD};
            row.fields = {{"name", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(duplicate.name)},
                          {"icon", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(ClassIconImage(duplicate.cid))},
                          {"category-icon", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(ClassIconImage(categories[category].cid))},
                          {"class", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(duplicate.cid)},
                          {"category", EDITOR_INTEGER_VALUE, {}, {}, category},
                          {"category-name", EDITOR_TEXT_VALUE, {}, categories[category].name},
                          {"category-class", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(categories[category].cid)},
                          {"action", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(duplicate.action)}};
            nextInput->duplicates.push_back(std::move(row));
        }
        input = std::move(nextInput);
    }
    void Finish(std::shared_ptr<Operation> operation, EditorStatus status) {
        if (operation->finished) return;
        operation->finished = true;
        if (active == operation && input && input->kind == Input::Kind::DuplicateNames) {
            const auto *pending = files.PendingDuplicates();
            if (pending && input->duplicateSerial == pending->serial) files.ResolveDuplicates(false);
        }
        operation->result.status = status;
        EditorResponse response;
        response.status = status;
        response.message = !operation->result.message.empty()        ? operation->result.message
                           : !operation->result.opened.error.empty() ? operation->result.opened.error
                           : !operation->result.loaded.error.empty() ? operation->result.loaded.error
                           : !operation->result.saved.report.errors.empty()
                               ? AnsiToUtf8(operation->result.saved.report.errors.front())
                           : status == EDITOR_ERROR ? "File operation failed"
                                                    : std::string();
        response.fields = {{"path", EDITOR_TEXT_VALUE, {}, operation->result.path},
                           {"written", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(operation->result.written.size())}};
        EditorField outputs{"outputs", EDITOR_TEXT_LIST};
        outputs.texts = operation->result.written;
        response.fields.push_back(std::move(outputs));
        EditorField loadedPaths{"paths", EDITOR_TEXT_LIST}, messages{"messages", EDITOR_TEXT_LIST};
        EditorField loaded{"loaded", EDITOR_INTEGER_LIST}, created{"created", EDITOR_OBJECT_LIST};
        for (const auto &item : operation->result.resources.items) {
            loadedPaths.texts.push_back(item.path);
            messages.texts.push_back(item.message);
            loaded.integers.push_back(item.loaded ? 1 : 0);
            created.objects.push_back(item.created);
        }
        response.fields.push_back(std::move(loadedPaths));
        response.fields.push_back(std::move(messages));
        response.fields.push_back(std::move(loaded));
        response.fields.push_back(std::move(created));
        if (GraphFile(operation->result.action)) {
            response.documentRevision = operation->result.graph.documentRevision;
            response.fields.push_back({"graph", EDITOR_OBJECT, operation->result.graph.created});
            response.fields.push_back({"owner", EDITOR_OBJECT, operation->result.graph.owner});
        }
        if (active == operation) {
            active.reset();
            input.reset();
        }
        operation->native.Reset();
        // The completed operation remains queryable even when its receiver
        // disappeared. Only current-document results may change views.
        if (operation->scope.IsCurrent() || ((Replacement(operation->result.action) ||
            operation->result.action == Action::Revert) && current() &&
            current()->Token() == operation->result.document)) {
            try {
                presentation.FileFinished(operation->result);
            } catch (const std::exception &error) {
                if (!response.message.empty()) response.message += "; ";
                response.message += std::string("Cannot restore document views: ") + error.what();
            } catch (...) {
                if (!response.message.empty()) response.message += "; ";
                response.message += "Cannot restore document views";
            }
        }
        editor.CompleteOperation(operation->result.operation, std::move(response));
    }
    EditorStatus Status(Action action) const {
        if (action == Action::Quit && waitingQuit) return EDITOR_NO_CHANGE;
        if (action == Action::Quit && (!runtime.Available() || active)) return EDITOR_OK;
        if (active) return EDITOR_REFUSED;
        if (!runtime.CanQueue("file operations")) return EDITOR_REFUSED;
        if (!ApplicationAction(action) && !HasDocument()) return EDITOR_REFUSED;
        if (action == Action::Undo && !current()->History().CanUndo()) return EDITOR_NO_CHANGE;
        if (action == Action::Redo && !current()->History().CanRedo()) return EDITOR_NO_CHANGE;
        if (action == Action::Purge && !current()->History().CanUndo() && !current()->History().CanRedo())
            return EDITOR_NO_CHANGE;
        if (action == Action::Revert && CurrentPath().empty()) return EDITOR_REFUSED;
        return EDITOR_OK;
    }
    static EditorStatus Check(void *data, const EditorHost *, const EditorRequest *request) {
        const auto &command = *static_cast<Command *>(data);
        const auto status = command.owner->Status(command.action);
        if (status != EDITOR_OK) return status;
        return command.owner->ValidArguments(command.action, *request) ? EDITOR_OK : EDITOR_INVALID;
    }
    static EditorStatus Execute(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        const auto &command = *static_cast<Command *>(data);
        auto &self = *command.owner;
        if (!self.ValidArguments(command.action, *request)) return EDITOR_INVALID;
        if (command.action == Action::Undo || command.action == Action::Redo || command.action == Action::Purge) {
            auto &history = self.current()->History();
            bool ok = true;
            if (command.action == Action::Undo)
                ok = history.Undo();
            else if (command.action == Action::Redo)
                ok = history.Redo();
            else
                history.Clear();
            self.presentation.HistoryChanged();
            return ok ? EDITOR_OK : EDITOR_ERROR;
        }
        auto operation = std::make_shared<Operation>();
        operation->result.operation = request->operation;
        operation->result.action = command.action;
        operation->result.document = request->scope.document;
        operation->scope = self.runtime.CurrentDocument();
        operation->settings = self.settings;
        operation->view = request->scope.view;
        if (const auto *path = Field(*request, "path")) operation->result.path = Text(path->text);
        if (command.action == Action::LoadResources || command.action == Action::DropResources) {
            const auto paths = Field(*request, "paths")->texts;
            const auto classes = Field(*request, "classes")->integers;
            for (uint32_t i = 0; i < paths.count; ++i)
                operation->resources.files.push_back({Text(paths.data[i]), static_cast<CK_CLASSID>(classes.data[i])});
            const auto targets = Field(*request, "targets")->objects;
            if (targets.count) operation->resources.targets.assign(targets.data, targets.data + targets.count);
            if (command.action == Action::DropResources) {
                operation->resources.drop = true;
                const auto above = Field(*request, "above")->objects;
                if (above.count) operation->resources.above.assign(above.data, above.data + above.count);
                operation->resources.scene = Field(*request, "scene")->object;
            }
        } else if (command.action == Action::LoadScripts) {
            operation->resources.targets.push_back(Field(*request, "target")->object);
        } else if (command.action == Action::LoadSnapshot) {
            operation->snapshotRequest.script = Field(*request, "target")->object;
            operation->snapshotRequest.revision = Field(*request, "revision")->integer;
            operation->snapshotRequest.topology = Field(*request, "topology")->integer;
        } else if (GraphFile(command.action)) {
            auto &graph = operation->graphRequest;
            graph.script = Field(*request, "target")->object;
            graph.graph = Field(*request, "graph")->object;
            graph.revision = Field(*request, "revision")->integer;
            graph.topology = Field(*request, "topology")->integer;
            graph.replace = command.action == Action::ReplaceGraph;
            const auto point = Field(*request, "at")->reals;
            graph.h = float(point.data[0]);
            graph.v = float(point.data[1]);
        } else if (command.action == Action::ExportObjects || command.action == Action::ExportBehaviors) {
            auto &output = operation->exportRequest;
            const auto objects = Field(*request, "objects")->objects;
            output.objects.assign(objects.data, objects.data + objects.count);
            output.behaviors = command.action == Action::ExportBehaviors;
            output.hide = Field(*request, "hide")->integer != 0;
            operation->initial = Text(Field(*request, "initial")->text);
            if (operation->initial.empty()) operation->initial = output.behaviors ? "Behaviors.nms" : "Objects.nmo";
            if (output.behaviors) {
                operation->askDescription = Field(*request, "ask-description")->integer != 0;
                output.description = Field(*request, "write-description")->integer != 0;
                output.author = Text(Field(*request, "author")->text);
                output.version = Text(Field(*request, "version")->text);
                output.text = Text(Field(*request, "description")->text);
                output.text.erase(std::remove(output.text.begin(), output.text.end(), '\r'), output.text.end());
            } else
                operation->askOptions = Field(*request, "ask-options")->integer != 0;
        }
        if (command.action == Action::Quit && (!self.runtime.Available() || self.active)) {
            self.waitingQuit = operation;
            return EDITOR_QUEUED;
        }
        operation->native = self.runtime.Begin("file operations");
        if (!operation->native) return EDITOR_REFUSED;
        auto submitting = operation->native.Enter();
        self.active = operation;
        if (Replacement(command.action) && self.HasDocument() && self.current()->Dirty())
            self.Ask(Input::Kind::Unsaved);
        else
            self.Start();
        return EDITOR_QUEUED;
    }
    bool ValidArguments(Action action, const EditorRequest &request) const {
        if (action == Action::LoadSnapshot) {
            if (const auto *path = Field(request, "path"); path && Text(path->text).find('\0') != std::string::npos)
                return false;
        }
        if (GraphFile(action)) {
            const auto *point = Field(request, "at");
            if (!point || point->reals.count != 2)
                return false;
            for (uint32_t i = 0; i < 2; ++i)
                if (!std::isfinite(point->reals.data[i]) || std::abs(point->reals.data[i]) > 100000000)
                    return false;
            if (const auto *path = Field(request, "path"); path && Text(path->text).find('\0') != std::string::npos)
                return false;
        }
        if (action == Action::LoadResources || action == Action::DropResources) {
            const auto paths = Field(request, "paths")->texts;
            const auto classes = Field(request, "classes")->integers;
            const auto targets = Field(request, "targets")->objects;
            // Bound both the Cartesian batch size and SDK class-table accesses.
            if (!paths.count || paths.count != classes.count ||
                uint64_t(paths.count) * std::max(1u, targets.count) > 4096) return false;
            uint64_t pathBytes = 0;
            for (uint32_t i = 0; i < paths.count; ++i) {
                if (!paths.data[i].size || classes.data[i] >= uint64_t(CKGetClassCount())) return false;
                pathBytes += paths.data[i].size;
            }
            if (pathBytes * std::max(1u, targets.count) > 256u * 1024u) return false;
        }
        if (action == Action::ExportObjects || action == Action::ExportBehaviors) {
            if (!Field(request, "objects")->objects.count || Field(request, "hide")->integer > 1) return false;
            if (action == Action::ExportObjects) return Field(request, "ask-options")->integer <= 1;
            if (Field(request, "ask-description")->integer > 1 || Field(request, "write-description")->integer > 1)
                return false;
            return DescriptionValid(Text(Field(request, "author")->text), Text(Field(request, "version")->text));
        }
        return true;
    }
    static bool DescriptionValid(const std::string &author, const std::string &version) {
        if (author.find_first_of("\r\n") != std::string::npos) return false;
        if (version.empty()) return true;
        char *end = nullptr;
        const float value = std::strtof(version.c_str(), &end);
        return end == version.c_str() + version.size() && std::isfinite(value);
    }
    void Export() {
        const auto operation = active;
        auto &request = operation->exportRequest;
        request.path = operation->result.path;
        if (!files.Export(request, [this, operation](const DocumentFiles::ExportResult &result) {
            operation->result.exported = result;
            operation->result.written.insert(operation->result.written.end(), result.written.begin(), result.written.end());
            operation->result.message = result.message;
            Finish(operation, result.saved && result.completed ? EDITOR_OK : EDITOR_ERROR);
        })) Finish(operation, EDITOR_REFUSED);
    }
    void LoadResources() {
        const auto operation = active;
        if (!files.LoadResources(operation->resources, [this, operation](const DocumentFiles::ResourceResult &result) {
            operation->result.resources = result;
            const bool ok = result.completed && std::all_of(result.items.begin(), result.items.end(),
                                                            [](const auto &item) { return item.loaded; });
            Finish(operation, ok ? EDITOR_OK : result.stop == DocumentFiles::LoadResult::Stop::Cancelled ? EDITOR_CANCELLED
                                            : result.stop == DocumentFiles::LoadResult::Stop::Stale ? EDITOR_STALE
                                                                                                  : EDITOR_ERROR);
        })) Finish(operation, EDITOR_REFUSED);
    }
    void Save(bool ask, bool beforeReplace = false) {
        active->saveBeforeReplace = beforeReplace;
        if (ask || ((beforeReplace || active->result.path.empty()) && lastSaved.empty())) {
            auto suggestion = std::filesystem::u8path(current()->SourcePath());
            if (suggestion.empty())
                suggestion = "New Composition.cmo";
            else
                suggestion.replace_filename(suggestion.stem().u8string() + "-edited" +
                                            suggestion.extension().u8string());
            Ask(Input::Kind::File, Input::File::Composition, true, {},
                lastSaved.empty() ? suggestion.u8string() : lastSaved);
            return;
        }
        SaveTo(beforeReplace || active->result.path.empty() ? lastSaved : active->result.path);
    }
    void SaveTo(const std::string &path) {
        const auto operation = active;
        const bool player = operation->result.action == Action::Player;
        SaveOptions options;
        options.allowOverwrite = true;
        options.saveScriptSnapshots = operation->settings.saveScriptSnapshots;
        options.markSaved = !player;
        options.writeMode = player ? CKFILE_FORVIEWER | CKFILE_WHOLECOMPRESSED : 0;
        const auto layout = presentation.CaptureLayout();
        files.Save(path, layout, options, [this, operation, path](const DocumentFiles::SaveResult &saved) {
            operation->result.saved = saved;
            if (saved.saved) operation->result.written.push_back(path);
            if (saved.report.documentSaved && operation->scope.IsCurrent()) lastSaved = path;
            if (operation->saveBeforeReplace) {
                if (saved.clean && operation->scope.IsCurrent()) {
                    operation->saveBeforeReplace = false;
                    Start();
                } else
                    Finish(operation, saved.completed && saved.saved ? EDITOR_REFUSED : EDITOR_ERROR);
                return;
            }
            operation->result.path = path;
            Finish(operation, saved.saved && saved.completed ? EDITOR_OK : EDITOR_ERROR);
        });
    }
    void Start() {
        const auto operation = active;
        if (!operation) return;
        auto submitting = operation->native.Enter();
        try { StartOperation(); }
        catch (const std::exception &error) {
            operation->result.message = error.what();
            Finish(operation, EDITOR_ERROR);
        } catch (...) {
            operation->result.message = "Preparing the file operation failed";
            Finish(operation, EDITOR_ERROR);
        }
    }
    void StartOperation() {
        if (!active) return;
        const auto operation = active;
        auto &result = operation->result;
        switch (result.action) {
        case Action::Quit:
            presentation.Quit();
            Finish(operation, EDITOR_OK);
            return;
        case Action::New:
            if (!files.New(operation->settings.open.environment, [this, operation](bool ok) {
                    if (ok) {
                        lastSaved.clear();
                        operation->result.document = current()->Token();
                    }
                    Finish(operation, ok ? EDITOR_OK : EDITOR_ERROR);
                }))
                Finish(operation, EDITOR_REFUSED);
            return;
        case Action::Close:
            if (!files.Close([this, operation](bool ok) {
                    if (ok) {
                        lastSaved.clear();
                        operation->result.document = {};
                    }
                    Finish(operation, ok ? EDITOR_OK : EDITOR_ERROR);
                }))
                Finish(operation, EDITOR_REFUSED);
            return;
        case Action::Open:
            if (result.path.empty()) {
                Ask(Input::Kind::File);
                return;
            }
            if (!files.Open(result.path, operation->settings.open,
                            [this, operation](const DocumentFiles::OpenResult &opened) {
                                operation->result.opened = opened;
                                if (opened.opened) {
                                    lastSaved.clear();
                                    operation->result.document = current()->Token();
                                }
                                Finish(operation, opened.opened ? EDITOR_OK : EDITOR_ERROR);
                            }))
                Finish(operation, EDITOR_REFUSED);
            return;
        case Action::Save:
            Save(false);
            return;
        case Action::SaveAs:
            if (result.path.empty())
                Save(true);
            else
                SaveTo(result.path);
            return;
        case Action::SaveVersion: {
            if (CurrentPath().empty()) {
                Save(true);
                return;
            }
            result.path = VersionedPath(CurrentPath(), 1);
            if (result.path.empty()) {
                Finish(operation, EDITOR_REFUSED);
                return;
            }
            if (Exists(result.path)) {
                Ask(Input::Kind::Confirm, Input::File::Composition, false, "File Exists", {},
                    result.path + " Exists! Do you want to overwrite it ?");
            } else
                SaveTo(result.path);
            return;
        }
        case Action::Revert:
            if (operation->reversionConfirmed) {
                if (!files.Open(result.path, operation->settings.open,
                                [this, operation](const DocumentFiles::OpenResult &opened) {
                                    operation->result.opened = opened;
                                    if (opened.opened) {
                                        lastSaved.clear();
                                        operation->result.document = current()->Token();
                                    }
                                    Finish(operation, opened.opened ? EDITOR_OK : EDITOR_ERROR);
                                }))
                    Finish(operation, EDITOR_REFUSED);
                return;
            }
            result.path = VersionedPath(CurrentPath(), -1);
            if (result.path.empty() || !Exists(result.path)) {
                Finish(operation, EDITOR_ERROR);
                return;
            }
            Ask(Input::Kind::Confirm, Input::File::Composition, false, "Revert to Saved", {},
                "Revert to last saved version of " + CurrentPath() + "?");
            return;
        case Action::Merge:
        case Action::MergeScene:
            if (result.path.empty()) {
                Ask(Input::Kind::File);
                return;
            }
            files.Merge(result.path, result.action == Action::MergeScene,
                        [this, operation](const DocumentFiles::LoadResult &loaded) {
                            operation->result.loaded = loaded;
                            Finish(operation, loaded.loaded ? EDITOR_OK
                                              : loaded.stop == DocumentFiles::LoadResult::Stop::Cancelled
                                                  ? EDITOR_CANCELLED
                                              : loaded.stop == DocumentFiles::LoadResult::Stop::Stale ? EDITOR_STALE
                                                                                                      : EDITOR_ERROR);
                        });
            return;
        case Action::Import:
        case Action::ImportCharacter:
        case Action::ImportPlace:
        case Action::ImportScene:
            if (result.path.empty()) {
                Ask(Input::Kind::File, Input::File::Import);
                return;
            }
            files.Import(
                result.path, static_cast<Document::ImportMode>(int(result.action) - int(Action::Import)),
                operation->settings.open.environment, [this, operation](const DocumentFiles::LoadResult &loaded) {
                    operation->result.loaded = loaded;
                    Finish(operation, loaded.loaded                                               ? EDITOR_OK
                                      : loaded.stop == DocumentFiles::LoadResult::Stop::Cancelled ? EDITOR_CANCELLED
                                      : loaded.stop == DocumentFiles::LoadResult::Stop::Stale     ? EDITOR_STALE
                                                                                                  : EDITOR_ERROR);
                });
            return;
        case Action::LoadResources:
        case Action::DropResources:
            LoadResources();
            return;
        case Action::LoadScripts:
            if (result.path.empty()) {
                Ask(Input::Kind::File, Input::File::Behaviors, false, "Load Script");
                return;
            }
            operation->resources.files = {{result.path, CKCID_BEHAVIOR}};
            LoadResources();
            return;
        case Action::LoadSnapshot:
            if (result.path.empty()) {
                Ask(Input::Kind::File, Input::File::Image, false, "Load Snapshot for Script");
                return;
            }
            operation->snapshotRequest.path = result.path;
            if (!files.LoadScriptSnapshot(
                    operation->snapshotRequest, [this, operation](const DocumentFiles::SnapshotResult &snapshot) {
                        operation->result.message = AnsiToUtf8(snapshot.edit.message);
                        Finish(operation, snapshot.stop == DocumentFiles::LoadResult::Stop::Stale ? EDITOR_STALE
                                          : !snapshot.completed                                   ? EDITOR_ERROR
                                          : snapshot.edit.ok                                      ? EDITOR_OK
                                          : snapshot.edit.Unchanged()                             ? EDITOR_NO_CHANGE
                                          : snapshot.edit.Status() == EditStatus::Refused         ? EDITOR_REFUSED
                                                                                                  : EDITOR_ERROR);
                    }))
                Finish(operation, EDITOR_REFUSED);
            return;
        case Action::ImportGraph:
        case Action::ReplaceGraph:
            if (result.path.empty()) {
                Ask(Input::Kind::File, Input::File::Behaviors, false,
                    result.action == Action::ReplaceGraph ? "Replace with File Graph" : "Import Behavior Graph");
                return;
            }
            operation->graphRequest.path = result.path;
            if (!files.EditGraph(operation->graphRequest, [this, operation](const DocumentFiles::GraphResult &graph) {
                    operation->result.graph = graph;
                    operation->result.message = AnsiToUtf8(graph.edit.message);
                    Finish(operation, graph.stop == DocumentFiles::LoadResult::Stop::Stale ? EDITOR_STALE
                                      : !graph.completed                                   ? EDITOR_ERROR
                                      : graph.edit.ok                                      ? EDITOR_OK
                                      : graph.edit.Unchanged()                             ? EDITOR_NO_CHANGE
                                      : graph.edit.Status() == EditStatus::Refused         ? EDITOR_REFUSED
                                                                                           : EDITOR_ERROR);
                }))
                Finish(operation, EDITOR_REFUSED);
            return;
        case Action::ExportObjects:
        case Action::ExportBehaviors:
            if (operation->askOptions) {
                Ask(Input::Kind::ObjectExport);
                input->objects = operation->exportRequest.objects.size();
                input->hide = operation->exportRequest.hide;
                return;
            }
            if (result.path.empty()) {
                Ask(Input::Kind::File, result.action == Action::ExportObjects ? Input::File::Objects : Input::File::Behaviors,
                    true, {}, operation->initial);
                return;
            }
            operation->exportRequest.path = result.path;
            if (operation->askDescription) {
                if (!files.DescribeExport(operation->exportRequest,
                    [this, operation](bool ok, const DocumentFiles::ExportInfo &info) {
                        if (operation->finished) return;
                        if (!ok || !operation->scope.IsCurrent()) {
                            Finish(operation, operation->scope.IsCurrent() ? EDITOR_ERROR : EDITOR_STALE);
                            return;
                        }
                        Ask(Input::Kind::BehaviorExport);
                        input->behavior = info;
                        operation->exportRequest.name = info.name;
                        input->hide = operation->exportRequest.hide;
                    })) Finish(operation, EDITOR_REFUSED);
                return;
            }
            Export();
            return;
        case Action::Player: {
            if (!result.path.empty()) {
                SaveTo(result.path);
                return;
            }
            auto initial = std::filesystem::u8path(CurrentPath());
            initial.replace_extension("vmo");
            Ask(Input::Kind::File, Input::File::Player, true, {}, initial.u8string());
            return;
        }
        default:
            Finish(operation, EDITOR_INVALID);
            return;
        }
    }
    EditorStatus Respond(uint64_t id, const std::string &choice, const std::string &path,
                         const EditorIntegers *actions = nullptr, const std::string &message = {}) {
        const auto operation = active;
        auto submitting = operation ? operation->native.Enter() : RuntimeOperation::Request{}.Enter();
        try { return RespondToInput(id, choice, path, actions, message); }
        catch (const std::exception &error) {
            if (operation) { operation->result.message = error.what(); Finish(operation, EDITOR_ERROR); }
        } catch (...) {
            if (operation) { operation->result.message = "Preparing the file operation failed"; Finish(operation, EDITOR_ERROR); }
        }
        if (input && input->id == id) input.reset();
        return EDITOR_ERROR;
    }
    EditorStatus InputStatus(uint64_t id) const {
        if (!input || input->id != id) return EDITOR_STALE;
        if (!active) return input->kind == Input::Kind::DuplicateNames ? EDITOR_OK : EDITOR_STALE;
        if (SourceViewRequired(active->result.action) && active->view &&
            !editor.Scope(active->view).document.value) return EDITOR_CANCELLED;
        return active->scope.IsCurrent() ? EDITOR_OK : EDITOR_STALE;
    }
    EditorStatus RespondToInput(uint64_t id, const std::string &choice, const std::string &path, const EditorIntegers *actions,
                                const std::string &message) {
        const auto status = InputStatus(id);
        if (status != EDITOR_OK) {
            if (active && input && input->id == id) Finish(active, status);
            return status;
        }
        if (input->kind == Input::Kind::DuplicateNames) {
            const auto *pending = files.PendingDuplicates();
            if (!pending || pending->serial != input->duplicateSerial) return EDITOR_STALE;
            if (choice != "accept" && choice != "cancel" && choice != "error") return EDITOR_INVALID;
            std::vector<DuplicateAction> choices;
            if (choice == "accept") {
                if (!actions || actions->count != pending->duplicates.size()) return EDITOR_INVALID;
                for (uint32_t i = 0; i < actions->count; ++i) {
                    if (actions->data[i] > uint64_t(DuplicateAction::UseCurrent)) return EDITOR_INVALID;
                    choices.push_back(static_cast<DuplicateAction>(actions->data[i]));
                }
            }
            if (active) active->result.message = message;
            if (choice == "error" && active) {
                Finish(active, EDITOR_ERROR);
                return EDITOR_ERROR;
            }
            if (!files.ResolveDuplicates(choice == "accept", &choices)) return EDITOR_STALE;
            if (input && input->id == id) input.reset();
            return choice == "accept" ? EDITOR_QUEUED : choice == "error" ? EDITOR_ERROR : EDITOR_CANCELLED;
        }
        const auto operation = active;
        const Input request = *input;
        input.reset();
        if (choice == "error") {
            Finish(operation, EDITOR_ERROR);
            return EDITOR_ERROR;
        }
        if (choice == "cancel") {
            Finish(operation, EDITOR_CANCELLED);
            return EDITOR_CANCELLED;
        }
        if (request.kind == Input::Kind::Unsaved) {
            if (choice == "save")
                Save(false, true);
            else if (choice == "save-as")
                Save(true, true);
            else if (choice == "discard")
                Start();
            else {
                input = std::make_unique<Input>(request);
                return EDITOR_INVALID;
            }
        } else if (request.kind == Input::Kind::Confirm) {
            if (choice != "accept") {
                input = std::make_unique<Input>(request);
                return EDITOR_INVALID;
            }
            if (operation->result.action == Action::Revert) {
                operation->reversionConfirmed = true;
                Start();
            } else
                SaveTo(operation->result.path);
        } else if (request.kind == Input::Kind::ObjectExport || request.kind == Input::Kind::BehaviorExport) {
            if (choice != "accept") { input = std::make_unique<Input>(request); return EDITOR_INVALID; }
            if (request.kind == Input::Kind::ObjectExport) operation->askOptions = false;
            else operation->askDescription = false;
            Start();
        } else {
            if (choice != "accept" || path.empty()) {
                input = std::make_unique<Input>(request);
                return EDITOR_INVALID;
            }
            if (operation->result.action == Action::ExportObjects || operation->result.action == Action::ExportBehaviors) {
                operation->result.path = path;
                Start();
            } else if (request.save)
                SaveTo(path);
            else {
                operation->result.path = path;
                Start();
            }
        }
        return EDITOR_QUEUED;
    }
    static EditorStatus Reply(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
        auto &self = *static_cast<State *>(data);
        if (self.active && self.input && self.input->id == Field(*request, "input")->integer &&
            self.input->kind != Input::Kind::DuplicateNames) {
            const auto kind = self.input->kind;
            if (Text(Field(*request, "choice")->text) == "accept" &&
                (kind == Input::Kind::ObjectExport || kind == Input::Kind::BehaviorExport)) {
                const auto hide = Field(*request, "hide")->integer;
                const auto author = Text(Field(*request, "author")->text), version = Text(Field(*request, "version")->text);
                if (hide > 1 || (kind == Input::Kind::BehaviorExport && !DescriptionValid(author, version))) return EDITOR_INVALID;
                auto &output = self.active->exportRequest;
                output.hide = hide != 0;
                if (kind == Input::Kind::BehaviorExport) {
                    output.description = true;
                    output.author = author;
                    output.version = version;
                    output.text = Text(Field(*request, "description")->text);
                    output.text.erase(std::remove(output.text.begin(), output.text.end(), '\r'), output.text.end());
                }
            }
            self.active->result.message = Text(Field(*request, "message")->text);
        }
        const auto status = self.Respond(Field(*request, "input")->integer, Text(Field(*request, "choice")->text),
                                         Text(Field(*request, "path")->text),
                                         Field(*request, "actions") ? &Field(*request, "actions")->integers : nullptr,
                                         Text(Field(*request, "message")->text));
        return status == EDITOR_QUEUED ? EDITOR_OK : status;
    }
    static EditorStatus InputData(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &self = *static_cast<State *>(data);
        if (!self.input) return EDITOR_NO_CHANGE;
        const auto &input = *self.input;
        if (input.kind == Input::Kind::DuplicateNames) {
            const auto *pending = self.files.PendingDuplicates();
            if (!pending || pending->serial != input.duplicateSerial) return EDITOR_NO_CHANGE;
        }
        const EditorValue values[] = {
            {EditorLiteral("input"), EDITOR_INTEGER_VALUE, {}, {}, input.id},
            {EditorLiteral("operation"), EDITOR_INTEGER_VALUE, {}, {}, input.operation},
            {EditorLiteral("kind"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(input.kind)},
            {EditorLiteral("save"), EDITOR_INTEGER_VALUE, {}, {}, input.save ? 1u : 0u},
            {EditorLiteral("title"), EDITOR_TEXT_VALUE, {}, EditorText(input.title)},
            {EditorLiteral("message"), EDITOR_TEXT_VALUE, {}, EditorText(input.text)},
            {EditorLiteral("initial"), EDITOR_TEXT_VALUE, {}, EditorText(input.initial)},
            {EditorLiteral("extension"), EDITOR_TEXT_VALUE, {}, EditorText(input.extension)},
            {EditorLiteral("filters"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(input.filters.size())},
            {EditorLiteral("icon"), EDITOR_INTEGER_VALUE, {}, {}, input.action == Action::Revert ? 2u : 6u}};
        return sink.write ? sink.write(sink.data, values, 10) : EDITOR_INVALID;
    }
    static EditorStatus DuplicateData(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        const auto &self = *static_cast<State *>(data);
        const auto *pending = self.files.PendingDuplicates();
        if (!self.input || self.input->kind != Input::Kind::DuplicateNames ||
            self.input->id != Field(*request, "input")->integer || !pending ||
            self.input->duplicateSerial != pending->serial) return EDITOR_STALE;
        const auto &all = self.input->duplicates;
        const auto *start = Field(*request, "first");
        const auto first = start ? start->integer : 0;
        if (first > all.size()) return EDITOR_INVALID;
        const auto end = std::min<uint64_t>(first + 128, all.size());
        EditorField rows{"rows", EDITOR_RECORD};
        rows.fields.assign(all.begin() + first, all.begin() + end);
        const std::vector<EditorField> fields{rows, {"total", EDITOR_INTEGER_VALUE, {}, {}, all.size()},
                                            {"next", EDITOR_INTEGER_VALUE, {}, {}, end}};
        const auto values = BorrowEditorFields(fields);
        return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
    }
    static EditorStatus ExportData(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        const auto &self = *static_cast<State *>(data);
        if (!self.input || self.input->id != Field(*request, "input")->integer) return EDITOR_STALE;
        const auto &input = *self.input;
        if (input.kind != Input::Kind::ObjectExport && input.kind != Input::Kind::BehaviorExport) return EDITOR_INVALID;
        const EditorValue values[] = {
            {EDITOR_TEXT("objects"), EDITOR_INTEGER_VALUE, {}, {}, input.objects},
            {EDITOR_TEXT("hide"), EDITOR_INTEGER_VALUE, {}, {}, input.hide ? 1u : 0u},
            {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE, {}, EditorText(input.behavior.name)},
            {EDITOR_TEXT("class"), EDITOR_TEXT_VALUE, {}, EditorText(input.behavior.className)},
            {EDITOR_TEXT("author"), EDITOR_TEXT_VALUE, {}, EditorText(input.behavior.author)},
            {EDITOR_TEXT("version"), EDITOR_TEXT_VALUE, {}, EditorText(input.behavior.version)},
            {EDITOR_TEXT("description"), EDITOR_TEXT_VALUE, {}, EditorText(input.behavior.text)}};
        return sink.write ? sink.write(sink.data, values, 7) : EDITOR_INVALID;
    }
    static EditorStatus FilterData(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        const auto &self = *static_cast<State *>(data);
        const auto index = Field(*request, "index")->integer;
        if (!self.input || self.input->id != Field(*request, "input")->integer) return EDITOR_STALE;
        if (index >= self.input->filters.size()) return EDITOR_INVALID;
        const auto &filter = self.input->filters[index];
        const EditorValue values[] = {{EditorLiteral("label"), EDITOR_TEXT_VALUE, {}, EditorText(filter.first)},
                                      {EditorLiteral("pattern"), EDITOR_TEXT_VALUE, {}, EditorText(filter.second)}};
        return sink.write ? sink.write(sink.data, values, 2) : EDITOR_INVALID;
    }
};

DocumentCommands::DocumentCommands(EditorApplication &editor, DocumentFiles &files, RuntimeOperation &runtime,
                                   std::function<Document *()> current, Presentation &presentation)
    : m_State(std::make_unique<State>(editor, files, runtime, std::move(current), presentation)) {
}
DocumentCommands::~DocumentCommands() = default;
void DocumentCommands::Configure(Settings settings) {
    m_State->settings = std::move(settings);
}
bool DocumentCommands::Busy() const {
    return bool(m_State->active);
}
const DocumentCommands::Input *DocumentCommands::PendingInput() const {
    return m_State->input.get();
}
EditorStatus DocumentCommands::Respond(uint64_t input, const std::string &choice, const std::string &path) {
    return m_State->Respond(input, choice, path);
}
std::string DocumentCommands::CurrentFilePath() const {
    return m_State->CurrentPath();
}
void DocumentCommands::Poll() {
    if (m_State->active && m_State->input && m_State->SourceViewRequired(m_State->active->result.action) &&
        m_State->active->view && !m_State->editor.Scope(m_State->active->view).document.value)
        m_State->Finish(m_State->active, EDITOR_CANCELLED);
    if (m_State->active && !m_State->active->scope.IsCurrent() && !m_State->runtime.Busy())
        m_State->Finish(m_State->active, EDITOR_STALE);
    if (m_State->waitingQuit && !m_State->active && m_State->runtime.CanQueue("Closing application")) {
        auto native = m_State->runtime.Begin("Closing application");
        if (!native) return;
        m_State->active = std::move(m_State->waitingQuit);
        m_State->active->native = std::move(native);
        m_State->active->scope = m_State->runtime.CurrentDocument();
        m_State->active->result.document = m_State->editor.Scope().document;
        if (m_State->HasDocument() && m_State->current()->Dirty())
            m_State->Ask(Input::Kind::Unsaved);
        else
            m_State->Start();
    }
    m_State->PublishDuplicates();
    m_State->PublishDocumentInfo();
}
void DocumentCommands::Stop() {
    if (m_State->active) m_State->Finish(m_State->active, EDITOR_CANCELLED);
    if (m_State->waitingQuit) {
        auto operation = std::move(m_State->waitingQuit);
        m_State->Finish(operation, EDITOR_CANCELLED);
    }
}
bool DocumentCommands::Register(std::string &error) {
    using A = Action;
    struct Description {
        const char *id, *title;
        A action;
        const char *location = nullptr;
    };
    const Description descriptions[] = {
        {"document.new", "New Composition", A::New, "file.new"},
        {"document.open", "Load Composition...", A::Open, "file.document"},
        {"document.save", "Save Composition", A::Save, "file.document"},
        {"document.save-as", "Save As...", A::SaveAs, "file.document"},
        {"document.save-version", "Save Version", A::SaveVersion, "file.document"},
        {"document.revert", "Revert to Saved", A::Revert, "file.document"},
        {"document.close", "Close", A::Close, "file.close"},
        {"application.quit", "Exit", A::Quit, "file.close"},
        {"document.merge", "Merge Composition...", A::Merge, "file.merge"},
        {"document.merge-scene", "Merge Composition as New Scene...", A::MergeScene, "file.merge"},
        {"document.import", "Import File", A::Import, "resources.import"},
        {"document.import-character", "Character", A::ImportCharacter, "resources.import-as"},
        {"document.import-place", "Place", A::ImportPlace, "resources.import-as"},
        {"document.import-scene", "Scene", A::ImportScene, "resources.import-as"},
        {"document.export-player", "Export to Virtools Player...", A::Player, "file.export"},
        {"history.undo", "Undo (3d Layout/Schematic only)", A::Undo, "file.history"},
        {"history.redo", "Redo (3d Layout/Schematic only)", A::Redo, "file.history"},
        {"history.purge", "Purge Undo", A::Purge, "file.history"},
        {"resource.load", "Load resources", A::LoadResources},
        {"resource.drop", "Drop resources", A::DropResources},
        {"script.load", "Load Script...", A::LoadScripts},
        {"schematic.import-graph", "Import Behavior Graph", A::ImportGraph},
        {"schematic.replace-file-graph", "Replace with File Graph", A::ReplaceGraph},
        {"object.export", "Save Objects as...", A::ExportObjects},
        {"script.export", "Save behaviors as...", A::ExportBehaviors},
        {"schematic.load-snapshot", "Load Snapshot for Script", A::LoadSnapshot}};
    std::vector<EditorCommandDesc> commands;
    const EditorParameter path{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, 1};
    const EditorParameter resources[] = {{EDITOR_TEXT("paths"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST}, {EDITOR_TEXT("targets"), EDITOR_OBJECT_LIST, 1}};
    const EditorParameter drop[] = {{EDITOR_TEXT("paths"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST}, {EDITOR_TEXT("targets"), EDITOR_OBJECT_LIST, 1},
        {EDITOR_TEXT("above"), EDITOR_OBJECT_LIST, 1}, {EDITOR_TEXT("scene"), EDITOR_OBJECT, 1}};
    const EditorParameter script[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT}, path};
    const EditorParameter graph[] = {script[0],
                                     {EDITOR_TEXT("graph"), EDITOR_OBJECT},
                                     {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
                                     {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE},
                                     {EDITOR_TEXT("at"), EDITOR_REAL_LIST},
                                     path};
    const EditorParameter snapshot[] = {script[0],
                                        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
                                        {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE},
                                        path};
    const EditorParameter objects[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}, path,
        {EDITOR_TEXT("initial"), EDITOR_TEXT_VALUE, 1}, {EDITOR_TEXT("hide"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("ask-options"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter behaviors[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}, path,
        {EDITOR_TEXT("initial"), EDITOR_TEXT_VALUE, 1}, {EDITOR_TEXT("hide"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("ask-description"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("write-description"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("author"), EDITOR_TEXT_VALUE, 1}, {EDITOR_TEXT("version"), EDITOR_TEXT_VALUE, 1},
        {EDITOR_TEXT("description"), EDITOR_TEXT_VALUE, 1}};
    for (const auto &description : descriptions) {
        m_State->commands.push_back({m_State.get(), description.action, description.id, description.title});
        auto &command = m_State->commands.back();
        const bool history = command.action == A::Undo || command.action == A::Redo || command.action == A::Purge;
        EditorCommandDesc descriptor{sizeof(descriptor),
                                     EditorLiteral(command.id),
                                     EditorLiteral("builtin.documents"),
                                     EditorLiteral(command.title),
                                     history ? nullptr : &path,
                                     history ? 0u : 1u,
                                     &State::Execute,
                                     &command};
        if (command.action == A::LoadResources) { descriptor.parameters = resources; descriptor.count = 3; }
        if (command.action == A::DropResources) { descriptor.parameters = drop; descriptor.count = 5; }
        if (command.action == A::LoadScripts) { descriptor.parameters = script; descriptor.count = 2; }
        if (command.action == A::LoadSnapshot) {
            descriptor.parameters = snapshot;
            descriptor.count = 4;
        }
        if (State::GraphFile(command.action)) {
            descriptor.parameters = graph;
            descriptor.count = 6;
        }
        if (command.action == A::ExportObjects) { descriptor.parameters = objects; descriptor.count = 5; }
        if (command.action == A::ExportBehaviors) { descriptor.parameters = behaviors; descriptor.count = 9; }
        descriptor.area = State::ApplicationAction(command.action) ? EDITOR_APPLICATION : EDITOR_DOCUMENT;
        descriptor.effect = history                     ? EDITOR_HISTORY_CHANGE
                            : command.action == A::Quit ? EDITOR_SESSION_CHANGE
                                                        : EDITOR_FILE_OPERATION;
        descriptor.state = &State::Check;
        commands.push_back(descriptor);
    }
    const EditorParameter reply[] = {{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE},
                                     {EDITOR_TEXT("choice"), EDITOR_TEXT_VALUE},
                                     {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, 1},
                                     {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, 1},
                                     {EDITOR_TEXT("hide"), EDITOR_INTEGER_VALUE, 1},
                                     {EDITOR_TEXT("author"), EDITOR_TEXT_VALUE, 1},
                                     {EDITOR_TEXT("version"), EDITOR_TEXT_VALUE, 1},
                                     {EDITOR_TEXT("description"), EDITOR_TEXT_VALUE, 1},
                                     {EDITOR_TEXT("actions"), EDITOR_INTEGER_LIST, 1}};
    commands.push_back({sizeof(EditorCommandDesc), EDITOR_TEXT("document.respond"), EDITOR_TEXT("builtin.documents"),
                        EDITOR_TEXT("Answer file request"), reply, 9, &State::Reply, m_State.get(), EDITOR_APPLICATION,
                        EDITOR_SESSION_CHANGE, nullptr});
    const EditorParameter inputFields[] = {
        {EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE},   {EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE},    {EDITOR_TEXT("save"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE},      {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("initial"), EDITOR_TEXT_VALUE},    {EDITOR_TEXT("extension"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("filters"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("icon"), EDITOR_INTEGER_VALUE}};
    const EditorParameter filterArgs[] = {{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE},
                                          {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE}};
    const EditorParameter filterFields[] = {{EDITOR_TEXT("label"), EDITOR_TEXT_VALUE},
                                            {EDITOR_TEXT("pattern"), EDITOR_TEXT_VALUE}};
    const EditorParameter exportArg{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE};
    const EditorParameter exportFields[] = {{EDITOR_TEXT("objects"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("hide"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("class"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("author"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("version"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("description"), EDITOR_TEXT_VALUE}};
    const EditorParameter duplicateArgs[] = {{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE},
                                              {EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter duplicateFields[] = {{EDITOR_TEXT("rows"), EDITOR_RECORD},
                                                {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE},
                                                {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE}};
    const EditorParameter documentFields[] = {
        {EDITOR_TEXT("open"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("document"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("dirty"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("level"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("scene"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("separate-scene"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("document.info"), EDITOR_TEXT("builtin.documents"), nullptr, 0,
         &State::DocumentInfo, m_State.get(), EDITOR_QUERY_VERSIONED, EDITOR_APPLICATION, documentFields, 7,
         [](void *data, const EditorHost *) { return static_cast<State *>(data)->documentInfoRevision; }},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("document.duplicate-rows"), EDITOR_TEXT("builtin.documents"), duplicateArgs, 2,
         &State::DuplicateData, m_State.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, duplicateFields, 3},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("document.input"), EDITOR_TEXT("builtin.documents"), nullptr, 0,
         &State::InputData, m_State.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, inputFields, 10},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("document.file-filter"), EDITOR_TEXT("builtin.documents"), filterArgs, 2,
         &State::FilterData, m_State.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, filterFields, 2},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("document.export-options"), EDITOR_TEXT("builtin.documents"), &exportArg, 1,
         &State::ExportData, m_State.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, exportFields, 7}};
    const EditorMenuLocation locations[] = {{EDITOR_TEXT("file.new"), EDITOR_TEXT("File: New")},
                                            {EDITOR_TEXT("file.document"), EDITOR_TEXT("File: Open and Save")},
                                            {EDITOR_TEXT("file.history"), EDITOR_TEXT("File: Undo")},
                                            {EDITOR_TEXT("file.merge"), EDITOR_TEXT("File: Merge")},
                                            {EDITOR_TEXT("file.export"), EDITOR_TEXT("File: Export")},
                                            {EDITOR_TEXT("file.close"), EDITOR_TEXT("File: Close")},
                                            {EDITOR_TEXT("resources.import"), EDITOR_TEXT("Resources: Import")},
                                            {EDITOR_TEXT("resources.import-as"), EDITOR_TEXT("Resources: Import As")}};
    std::deque<std::string> menuIds;
    std::vector<EditorMenuDesc> menus;
    int order = 0;
    for (const auto &description : descriptions) {
        const char *location = description.location;
        if (!location)
            continue;
        menuIds.emplace_back(std::string("menu.") + description.id);
        menus.push_back({EditorText(menuIds.back()), EditorLiteral(description.id), EditorLiteral(location),
                         EditorLiteral(description.title), order++});
    }
    const EditorBindingDesc bindings[] = {{EDITOR_TEXT("document.open"), EDITOR_TEXT("Ctrl+L")},
                                          {EDITOR_TEXT("document.open"), EDITOR_TEXT("Ctrl+O")},
                                          {EDITOR_TEXT("document.save"), EDITOR_TEXT("Ctrl+S")},
                                          {EDITOR_TEXT("document.save-version"), EDITOR_TEXT("Ctrl+Shift+S")},
                                          {EDITOR_TEXT("document.revert"), EDITOR_TEXT("Ctrl+Shift+Z")},
                                          {EDITOR_TEXT("history.undo"), EDITOR_TEXT("Ctrl+Z")},
                                          {EDITOR_TEXT("history.redo"), EDITOR_TEXT("Ctrl+Y")}};
    // Preserve the original accelerator handling of extra Alt/Super keys;
    // Open and Redo also ignored Shift. An override replaces all aliases.
    std::deque<std::string> chords;
    std::vector<EditorBindingDesc> acceleratorBindings;
    for (const auto &binding : bindings) {
        const auto id = Text(binding.action);
        const int shifts = id == "document.open" || id == "history.redo" ? 2 : 1;
        for (int shift = 0; shift < shifts; ++shift)
            for (int extra = 0; extra < 4; ++extra) {
                chords.push_back(std::string(shift ? "Shift+" : "") + (extra & 1 ? "Alt+" : "") +
                                 (extra & 2 ? "Super+" : "") + Text(binding.chord));
                acceleratorBindings.push_back({binding.action, EditorText(chords.back())});
            }
    }
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EditorLiteral("documents.operations");
    module.data = this;
    module.update = [](void *data, const EditorHost *, EditorModulePhase phase) {
        if (phase == EDITOR_PREPARE_FRAME) static_cast<DocumentCommands *>(data)->Poll();
    };
    module.stop = [](void *data, const EditorHost *) { static_cast<DocumentCommands *>(data)->Stop(); };
    module.commands = commands.data();
    module.commandCount = uint32_t(commands.size());
    module.queries = queries;
    module.queryCount = uint32_t(std::size(queries));
    module.locations = locations;
    module.locationCount = uint32_t(std::size(locations));
    module.menus = menus.data();
    module.menuCount = uint32_t(menus.size());
    module.bindings = acceleratorBindings.data();
    module.bindingCount = uint32_t(acceleratorBindings.size());
    return m_State->editor.Register(module, error);
}

} // namespace vdev
