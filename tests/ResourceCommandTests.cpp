#include "application/DocumentCommands.h"
#include "application/ObjectSelection.h"
#include "core/document/BehaviorDescription.h"
#include "core/document/SaveFileOperations.h"
#include "core/interface/InterfaceDataDiff.h"
#include "core/graph/NmsCommands.h"
#include "core/graph/TopologyCommands.h"
#include "core/layout/LayoutCommands.h"
#include "core/level/LevelCommands.h"
#include "features/objects/ObjectCommands.h"
#include "editor/ImGuiDrawing.h"
#include "tests/ResourceFiles.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace {
using namespace vdev;
void Check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
std::string Bytes(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
#ifndef _WIN32
class CleanupFailureManager : public CKBaseManager {
  public:
    std::filesystem::path folder;
    bool armed = false;
    explicit CleanupFailureManager(CKContext *context)
        : CKBaseManager(context, CKGUID(0x5a926740, 0x32bf1769), "Export cleanup regression") {}
    CKDWORD GetValidFunctionsMask() override { return CKMANAGER_FUNC_PostSave; }
    CKERROR PostSave() override {
        if (armed) {
            armed = false;
            std::filesystem::permissions(folder, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
        }
        return CK_OK;
    }
};
#endif
struct Presentation : DocumentFiles::Observer, DocumentCommands::Presentation {
    DocumentCommands::Result last;
    int completions = 0;
    void ReplacingDocument(bool) override {}
    void ReplacementCancelled() override {}
    void DocumentReplaced() override {}
    void Log(LogLevel, const std::string &) override {}
    LayoutInterfaceData CaptureLayout() const override { return {}; }
    void FileFinished(const DocumentCommands::Result &result) override { last = result; ++completions; }
    void HistoryChanged() override {}
    void Quit() override {}
};
class FailingNmsPublication : public SaveFileOperations {
public:
    enum Fault { None, Error, Exception, Unknown } recovery = Exception, publication = Error, cleanup = None;
    bool armed = false;
    std::filesystem::path nms, dsc, backup, stagedNms;
    unsigned dscReplacements = 0, nmsFailures = 0, recoveryAttempts = 0;
    std::error_code Replace(const std::filesystem::path &source,
                           const std::filesystem::path &destination, bool overwrite) override {
        if (armed && destination == nms) {
            stagedNms = source;
            ++nmsFailures;
            if (publication == Unknown) throw 11;
            if (publication == Exception)
                throw std::filesystem::filesystem_error("Injected NMS publication failure", source, destination,
                                                       std::make_error_code(std::errc::io_error));
            return std::make_error_code(std::errc::io_error);
        }
        if (armed && destination == dsc && ++dscReplacements > 1) {
            backup = source;
            ++recoveryAttempts;
            if (const auto error = RecoveryFailure(destination)) return error;
        }
        return SaveFileOperations::Replace(source, destination, overwrite);
    }
    std::error_code Remove(const std::filesystem::path &path) override {
        if (armed && path == dsc) {
            ++recoveryAttempts;
            if (const auto error = RecoveryFailure(path)) return error;
        }
        if (armed && cleanup != None && path == stagedNms) {
            if (cleanup == Unknown) throw 5;
            if (cleanup == Exception)
                throw std::filesystem::filesystem_error("Injected candidate cleanup failure", path,
                                                       std::make_error_code(std::errc::permission_denied));
            return std::make_error_code(std::errc::permission_denied);
        }
        return SaveFileOperations::Remove(path);
    }
private:
    std::error_code RecoveryFailure(const std::filesystem::path &path) {
        if (recovery == Exception)
            throw std::filesystem::filesystem_error("Injected DSC recovery failure", path,
                                                   std::make_error_code(std::errc::permission_denied));
        if (recovery == Unknown) throw 7;
        return recovery == Error ? std::make_error_code(std::errc::permission_denied) : std::error_code{};
    }
};
}

void TestResourceCommands(vdev::CkRuntimeHost &host, const std::string &modulePath) {
    using namespace vdev;
    namespace fs = std::filesystem;
    Document document(host);
    std::string error;
    Check(document.New(&error), error.c_str());
    bool failAfterExport = false;
    RuntimeOperation runtime({}, [&](const std::string &label, const std::function<void()> &work) {
        work();
        if (failAfterExport && label == "Saving objects") throw std::runtime_error("After export publication");
    });
    DocumentCommands *owner = nullptr;
    EditorApplication editor([&] { return &document; }, [&] { return !runtime.Busy() && (!owner || !owner->Busy()); });
    Presentation presentation;
    FailingNmsPublication publication;
    DocumentFiles files([&] { return &document; }, runtime, presentation, &publication);
    DocumentCommands commands(editor, files, runtime, [&] { return &document; }, presentation);
    owner = &commands;
    ObjectCommands objects([&] { return &document; });
    Check(objects.Register(editor, error) && commands.Register(error) && editor.Load(modulePath, error), error.c_str());
    EditorConfiguration configuration;
    configuration.providers = {{"command", "object.rename", "builtin.objects"},
                               {"command", "object.create-frame3d", "builtin.objects"}};
    configuration.providers.push_back({"command", "resource.load", "builtin.documents"});
    Check(editor.Activate(EDITOR_IMGUI_VERSION, error, configuration), error.c_str());
    const auto attach = [&] { editor.Attach(document.Token(), document.TopologyRevision(), true, true); };
    const auto drain = [&] { editor.EndFrame(); runtime.Finish(); attach(); commands.Poll(); };
    const auto status = [&](EditorOperation operation, uint64_t *written = nullptr) {
        const EditorValue arg{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, operation};
        const auto result = editor.Query("application.operation", {editor.Scope(), &arg, 1});
        Check(result.status == EDITOR_OK, "file operation result disappeared");
        EditorStatus status = EDITOR_INVALID;
        for (const auto &field : result.fields) {
            if (field.key == "status") status = EditorStatus(field.integer);
            if (written && field.key == "written") *written = field.integer;
        }
        return status;
    };
    const auto outputs = [&](EditorOperation operation) {
        const EditorValue args[] = {{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, operation},
                                     {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE, {}, {}, 2}};
        const auto result = editor.Query("application.operation-output", {editor.Scope(), args, 2});
        Check(result.status == EDITOR_OK, "export outputs are not queryable");
        for (const auto &field : result.fields) if (field.key == "texts") return field.texts;
        throw std::runtime_error("text array output missing");
    };
    const auto exportObjects = [&](std::vector<ObjectRef> refs, const std::string &path, bool external = false,
                                   EditorView view = 0, bool options = false) {
        const EditorValue args[] = {
            {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {refs.data(), uint32_t(refs.size())}},
            {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, EditorText(path)},
            {EDITOR_TEXT("ask-options"), EDITOR_INTEGER_VALUE, {}, {}, options ? 1u : 0u}};
        return editor.Execute(external ? "example.export-objects" : "object.export", {editor.Scope(view), args, 3});
    };
    const auto load = [&](std::vector<std::string> paths, std::vector<uint64_t> classes, std::vector<ObjectRef> targets,
                           bool external = false, bool drop = false) {
        std::vector<EditorString> names;
        for (const auto &path : paths) names.push_back(EditorText(path));
        const EditorValue args[] = {
            {EDITOR_TEXT("paths"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {names.data(), uint32_t(names.size())}},
            {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {classes.data(), uint32_t(classes.size())}},
            {EDITOR_TEXT("targets"), EDITOR_OBJECT_LIST, {}, {}, 0, {targets.data(), uint32_t(targets.size())}}};
        return editor.Execute(external ? "example.load-resources" : drop ? "resource.drop" : "resource.load",
                              {editor.Scope(), args, 3});
    };
    const auto root = vdevtest::ResourceTestRoot("resource-command-");
    Check(fs::create_directory(root), "cannot create resource test folder");
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    const auto first = CreateLayoutObject(document, LayoutObject::Frame3D);
    const auto second = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(first.ok && second.ok, "cannot create resource receivers");
    const auto firstRef = document.Reference(first.created), secondRef = document.Reference(second.created);
    const auto script = CreateScript(document, first.created, "Export Script");
    Check(script.ok, "cannot create export script");
    const auto graph = CreateBehaviorGraph(document, script.created, script.created, "Resource Graph", Point(100, 100));
    Check(graph.ok, "cannot create export graph");
    const auto longFolder = vdevtest::LongResourceDirectory(root);
    const auto nms = (longFolder / "scripts.nms").u8string();
    Check(SaveBehaviorsAsNms(document, {script.created}, nms, &error), error.c_str());
    ObjectSelection selection;
    selection.Restore(document, {first.created, second.created});
    const auto selected = selection.References();
    const auto history = document.History().UndoCount();
    const auto redo = document.History().CanRedo();
    const auto dirty = document.Dirty();
    const auto token = document.Token();
    const auto unchanged = [&] {
        Check(document.Token() == token && document.Dirty() == dirty && document.History().UndoCount() == history &&
              document.History().CanRedo() == redo && selection.References() == selected,
              "file output changed the document, selection or undo history");
    };
    attach();
    const auto nmo = (root / "objects.nmo").u8string();
    auto exported = exportObjects({firstRef}, nmo, true);
    Check(exported.status == EDITOR_QUEUED && exported.operation, "external object export did not use async file owner");
    drain();
    uint64_t written = 0;
    Check(status(exported.operation, &written) == EDITOR_OK && written == 1 && !Bytes(nmo).empty(), "object export failed");
    Check(outputs(exported.operation) == std::vector<std::string>{nmo}, "output paths were not copied");
    unchanged();
    const auto previous = Bytes(nmo);
    failAfterExport = true;
    exported = exportObjects({firstRef}, nmo);
    drain();
    Check(status(exported.operation, &written) == EDITOR_ERROR && written == 1 && outputs(exported.operation).size() == 1,
          "executor failure concealed an already published export");
    failAfterExport = false;
    unchanged();
#ifndef _WIN32
    // Cleanup diagnostics, including recovery locations, are command results;
    // they must not disappear into a log only the original UI can display.
    const auto cleanupFolder = root / "cleanup-failure";
    Check(fs::create_directory(cleanupFolder), "cannot create cleanup-failure folder");
    struct Permissions {
        fs::path folder;
        ~Permissions() { std::error_code ec; fs::permissions(folder, fs::perms::owner_all, ec); }
    } permissions{cleanupFolder};
    auto *cleanupManager = new CleanupFailureManager(document.Context());
    cleanupManager->folder = cleanupFolder;
    Check(document.Context()->RegisterNewManager(cleanupManager) == CK_OK, "cannot register cleanup failure manager");
    cleanupManager->armed = true;
    const auto cleanupPath = (cleanupFolder / "object.nmo").u8string();
    const auto cleanupFailure = exportObjects({firstRef}, cleanupPath);
    drain();
    fs::permissions(cleanupFolder, fs::perms::owner_all);
    const EditorValue cleanupId{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, cleanupFailure.operation};
    const auto cleanupResult = editor.Query("application.operation", {editor.Scope(), &cleanupId, 1});
    bool diagnosed = false;
    for (const auto &field : cleanupResult.fields)
        if (field.key == "message") diagnosed = field.text.find("Cannot remove temporary save file") != std::string::npos;
    Check(status(cleanupFailure.operation, &written) == EDITOR_ERROR && written == 0 && diagnosed,
          "export cleanup diagnostics were not retained in the command result");
    unchanged();
#endif
    // The exporting view may close after acceptance. Its output belongs to the
    // application and remains available to other views.
    const auto view = editor.OpenScope();
    const auto detached = (root / "detached.nmo").u8string();
    exported = exportObjects({firstRef}, detached, false, view);
    editor.CloseScope(view);
    drain();
    Check(status(exported.operation) == EDITOR_OK && fs::exists(detached), "accepted export died with its view");
    unchanged();
    // Both dialogs are replaceable queries and commands, with no native object
    // pointer or a view-owned continuation in their inputs.
    exported = exportObjects({firstRef}, {}, false, 0, true);
    auto input = commands.PendingInput();
    Check(input && input->kind == DocumentCommands::Input::Kind::ObjectExport, "missing object export options");
    const auto inputId = input->id;
    const EditorValue query{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE, {}, {}, inputId};
    Check(editor.Query("document.export-options", {editor.Scope(), &query, 1}).status == EDITOR_OK, "export options unavailable");
    const EditorValue reply[] = {{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE, {}, {}, inputId},
        {EDITOR_TEXT("choice"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("accept")},
        {EDITOR_TEXT("hide"), EDITOR_INTEGER_VALUE, {}, {}, 1}};
    Check(editor.Execute("document.respond", {editor.Scope(), reply, 3}).status == EDITOR_OK, "options answer refused");
    input = commands.PendingInput();
    Check(input && input->kind == DocumentCommands::Input::Kind::File && input->extension == "nmo", "missing NMO dialog");
    Check(commands.Respond(input->id, "cancel") == EDITOR_CANCELLED && status(exported.operation) == EDITOR_CANCELLED,
          "export cancellation failed");
    unchanged();
    // Invalid array shapes, SDK class IDs, UTF-8 and resource requests on the
    // wrong thread fail before the native load starts.
    Check(load({nms}, {}, {firstRef}).status == EDITOR_INVALID, "mismatched file classes accepted");
    Check(load({nms}, {uint64_t(-1)}, {}).status == EDITOR_INVALID, "unknown class reached the SDK class table");
    Check(load({std::string("\xc0\x80", 2)}, {CKCID_BEHAVIOR}, {}).status == EDITOR_INVALID, "invalid list UTF-8 accepted");
    const EditorValue invalid{EDITOR_TEXT("paths"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {nullptr, 1}};
    Check(editor.Execute("resource.load", {editor.Scope(), &invalid, 1}).status == EDITOR_INVALID, "null text array accepted");
    EditorStatus worker = EDITOR_OK;
    std::thread wrong([&] { worker = load({nms}, {CKCID_BEHAVIOR}, {firstRef}).status; });
    wrong.join();
    Check(worker == EDITOR_REFUSED && !runtime.Busy() && !commands.Busy(), "wrong-thread resource request admitted");
    unchanged();
    // A batch copies all file names, classes and receivers before returning.
    // The two file names here are intentionally temporary caller storage.
    auto *a = CKBeObject::Cast(document.Resolve(firstRef));
    auto *b = CKBeObject::Cast(document.Resolve(secondRef));
    const auto countA = a->GetScriptCount(), countB = b->GetScriptCount();
    auto batch = load({nms, nms}, {CKCID_BEHAVIOR, CKCID_BEHAVIOR}, {firstRef, secondRef}, true);
    drain();
    Check(status(batch.operation) == EDITOR_OK && presentation.last.resources.items.size() == 4 &&
          a->GetScriptCount() == countA + 2 && b->GetScriptCount() == countB + 2 &&
          document.History().UndoCount() == history + 4, "multi-file/multi-receiver load lost items or undo semantics");
    Check(selection.References() == selected, "resource loading changed the object selection");
    const auto beforeDeferred = a->GetScriptCount();
    editor.EnterDraw();
    const auto deferred = load({nms}, {CKCID_BEHAVIOR}, {firstRef});
    editor.LeaveDraw();
    Check(deferred.status == EDITOR_QUEUED && !runtime.Busy(), "view resource command was not deferred");
    drain();
    Check(status(deferred.operation) == EDITOR_OK && a->GetScriptCount() == beforeDeferred + 1,
          "deferred resource command borrowed a caller's text list");
    Check(presentation.last.resources.items.size() == 1 && presentation.last.resources.items.front().path == nms,
          "long resource path was truncated while the command was deferred");
    const auto count = a->GetScriptCount();
    batch = load({nms, (root / "missing.nms").u8string()}, {CKCID_BEHAVIOR, CKCID_BEHAVIOR}, {firstRef});
    drain();
    Check(status(batch.operation) == EDITOR_ERROR && presentation.last.resources.items.size() == 2 &&
          presentation.last.resources.items[0].loaded && !presentation.last.resources.items[1].loaded &&
          a->GetScriptCount() == count + 1, "partial batch results were hidden or rolled back incorrectly");
    const EditorValue partialArgs[] = {{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, batch.operation},
                                       {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE, {}, {}, 6}};
    const auto partial = editor.Query("application.operation-output", {editor.Scope(), partialArgs, 2});
    Check(partial.status == EDITOR_OK, "partial resource output unavailable");
    bool aligned = false;
    for (const auto &field : partial.fields)
        if (field.key == "objects") aligned = field.objects.size() == 2 && field.objects[0] && !field.objects[1];
    Check(aligned, "created resource results lost alignment with failed file items");
    // Dropping a classless NMS follows the existing Level Manager graph/script
    // load rules, rather than treating class zero as an SDK class table index.
    batch = load({nms}, {0}, {secondRef}, false, true);
    drain();
    Check(status(batch.operation) == EDITOR_OK, "classless behavior drop failed");
    // Objects can expire while a filename dialog or duplicate-name dialog is
    // open. No replacement object may inherit the delayed request.
    auto disposable = CreateLayoutObject(document, LayoutObject::Frame3D);
    auto stale = document.Reference(disposable.created);
    attach();
    auto delayed = exportObjects({stale}, {});
    input = commands.PendingInput();
    Check(input, "missing delayed export file input");
    const auto delayedInput = input->id;
    document.Context()->DestroyObject(stale.object, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID);
    auto *replacement = document.Context()->CreateObject(CKCID_3DENTITY, (CKSTRING) "Replacement");
    Check(replacement && replacement->GetID() == stale.object && !document.Resolve(stale), "did not reuse expired ID");
    const auto stalePath = (root / "stale.nmo").u8string();
    commands.Respond(delayedInput, "accept", stalePath);
    drain();
    Check(status(delayed.operation) == EDITOR_ERROR && !fs::exists(stalePath), "expired export wrote a replacement object");
    const auto expiredTarget = CreateLayoutObject(document, LayoutObject::Frame3D);
    const auto expiredRef = document.Reference(expiredTarget.created);
    attach();
    batch = load({nmo}, {CKCID_3DENTITY}, {expiredRef});
    drain();
    Check(files.PendingDuplicates(), "duplicate choices missing for receiver expiry test");
    document.Context()->DestroyObject(expiredRef.object, CK_DESTROY_NONOTIFY | CK_DESTROY_FREEID);
    files.ResolveDuplicates(true);
    drain();
    Check(status(batch.operation) == EDITOR_STALE, "receiver expired during duplicate choices was used");
    batch = load({nmo}, {CKCID_3DENTITY}, {firstRef});
    drain();
    Check(files.PendingDuplicates(), "fixture did not produce duplicate-name choices");
    runtime.DocumentChanged();
    files.DocumentChanged();
    commands.Poll();
    Check(status(batch.operation) == EDITOR_STALE && !commands.Busy(), "old duplicate prompt retained the file operation");
    // NMS + DSC preparation and publication preserve both previous outputs.
    const auto graphRef = document.Reference(graph.created);
    const auto saveNms = [&](const std::string &path, bool ask = false, EditorView view = 0) {
        const EditorValue args[] = {
            {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0, {&graphRef, 1}},
            {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, EditorText(path)},
            {EDITOR_TEXT("write-description"), EDITOR_INTEGER_VALUE, {}, {}, 1},
            {EDITOR_TEXT("ask-description"), EDITOR_INTEGER_VALUE, {}, {}, ask ? 1u : 0u},
            {EDITOR_TEXT("author"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("Author")},
            {EDITOR_TEXT("version"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("1.25")},
            {EDITOR_TEXT("description"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("First line\nSecond line")},
            {EDITOR_TEXT("hide"), EDITOR_INTEGER_VALUE, {}, {}, 1}};
        return editor.Execute("script.export", {editor.Scope(view), args, 8});
    };
    const auto output = (root / "graphs.nms").u8string();
    auto scriptExport = saveNms(output);
    drain();
    Check(status(scriptExport.operation, &written) == EDITOR_OK && written == 2, "NMS+DSC export failed");
    BehaviorDescription description;
    Check(ReadBehaviorDescription(output, description) && description.author == "Author" && description.version == 1.25f &&
          description.description == "First line\nSecond line", "DSC content changed");
    const auto nmsBefore = Bytes(output), dscBefore = Bytes(BehaviorDescriptionPath(output));
    // Retain a real Redo branch while exports fail and are retried.
    Check(RenameLevelObject(document, firstRef, "Recovery redo marker").ok && document.History().Undo(),
          "prepare recovery Redo branch");
    const auto recoveryToken = document.Token();
    const auto recoveryRevision = document.ChangeRevision(), recoveryTopology = document.TopologyRevision();
    const auto recoveryHistory = document.History().UndoCount();
    const auto recoveryRedo = document.History().RedoLabel();
    const auto recoverySelection = selection.References();
    const auto recoveryDirty = document.Dirty();
    const auto context = document.Context();
    const auto generation = host.Generation();
    auto *session = document.Session(script.created);
    const auto sessionRevision = session->Revision();
    const InterfaceData originalData = session->Data();
    const auto chunkBytes = [](CKStateChunk *chunk) {
        std::vector<unsigned char> bytes(chunk ? size_t(chunk->ConvertToBuffer(nullptr)) : 0);
        if (!bytes.empty()) chunk->ConvertToBuffer(bytes.data());
        return bytes;
    };
    auto *behavior = CKBehavior::Cast(document.Resolve(graphRef));
    const auto flags = behavior->GetFlags();
    const auto chunk = chunkBytes(behavior->GetInterfaceChunk());
    const auto writeMode = context->GetFileWriteMode();
    const auto source = document.SourcePath();
    const auto preserved = [&] {
        Check(document.Token() == recoveryToken && document.Context() == context && host.Generation() == generation &&
                  document.ChangeRevision() == recoveryRevision && document.TopologyRevision() == recoveryTopology &&
                  document.Dirty() == recoveryDirty && document.History().UndoCount() == recoveryHistory &&
                  document.History().CanRedo() && document.History().RedoLabel() == recoveryRedo &&
                  selection.References() == recoverySelection && document.SourcePath() == source,
              "failed NMS recovery changed editing state");
        Check(session->Revision() == sessionRevision && InterfaceDataDiff().Compare(originalData, session->Data()).identical &&
                  behavior->GetFlags() == flags && chunkBytes(behavior->GetInterfaceChunk()) == chunk &&
                  context->GetFileWriteMode() == writeMode && !context->m_Saving && !document.EditOpen(),
              "failed NMS recovery changed source chunks, flags or display data");
    };
    const std::string oldDescription = "Previous description for recovery";
    std::vector<fs::path> retainedBackups;
    unsigned recoveryCase = 0;
    const auto checkRecovery = [&](bool existed, FailingNmsPublication::Fault writeFault,
                                   FailingNmsPublication::Fault fault, FailingNmsPublication::Fault cleanupFailure) {
        const auto recovery = (root / ("recovery-" + std::to_string(recoveryCase++) + ".nms")).u8string();
        { std::ofstream file(recovery, std::ios::binary); file << nmsBefore; }
        const auto recoveryDsc = BehaviorDescriptionPath(recovery);
        if (existed) { std::ofstream file(recoveryDsc, std::ios::binary); file << oldDescription; }
        publication = {};
        publication.nms = fs::weakly_canonical(fs::u8path(recovery));
        publication.dsc = fs::weakly_canonical(fs::u8path(recoveryDsc));
        publication.publication = writeFault;
        publication.recovery = fault;
        publication.cleanup = cleanupFailure;
        publication.armed = true;
        const auto requester = editor.OpenScope();
        const auto failure = saveNms(recovery, false, requester);
        Check(failure.status == EDITOR_QUEUED && failure.operation, "accept NMS recovery operation");
        editor.CloseScope(requester);
        drain();
        publication.armed = false;
        Check(status(failure.operation, &written) == EDITOR_ERROR && publication.nmsFailures == 1 &&
                  publication.recoveryAttempts == 1 && !commands.Busy(),
              "NMS publication and recovery were not attempted exactly once");
        Check(Bytes(recovery) == nmsBefore, "failed NMS publication overwrote the previous NMS");
        const auto remaining = fault == FailingNmsPublication::None ? 0u : 1u;
        Check(written == remaining && outputs(failure.operation) == (remaining ? std::vector<std::string>{recoveryDsc}
                                                                            : std::vector<std::string>{}),
              "failed DSC recovery concealed the remaining output");
        const EditorValue operation{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, failure.operation};
        const auto completed = editor.Query("application.operation", {editor.Scope(), &operation, 1});
        std::string message;
        for (const auto &field : completed.fields) if (field.key == "message") message = field.text;
        if (remaining) {
            BehaviorDescription actual;
            Check(ReadBehaviorDescription(recovery, actual) && actual.author == "Author", "remaining DSC is not the published output");
            Check(message.find(std::string(existed ? "Cannot restore the previous output: " : "Cannot remove the new output: ") + recoveryDsc) != std::string::npos,
                  "completed operation omitted the recovery failure");
            if (existed) {
                Check(fs::is_regular_file(publication.backup) && Bytes(publication.backup) == oldDescription,
                      "failed NMS publication and DSC recovery discarded the previous DSC backup");
                Check(message.find("Previous output saved at " + publication.backup.u8string()) != std::string::npos,
                      "closed requesting view lost the backup location");
                retainedBackups.push_back(publication.backup);
            } else Check(message.find("Previous output saved at") == std::string::npos, "new DSC claimed an old backup");
        } else Check(existed ? Bytes(recoveryDsc) == oldDescription : !fs::exists(recoveryDsc), "DSC recovery did not restore its prior state");
        if (cleanupFailure) {
            Check(message.find("Cannot remove temporary save file") != std::string::npos &&
                      fs::is_regular_file(publication.stagedNms), "cleanup failure concealed the unremoved candidate");
            Check(fs::remove(publication.stagedNms), "remove injected cleanup residue");
        } else Check(!fs::exists(publication.stagedNms), "unpublished NMS candidate leaked");
        preserved();
        // Retrying may replace the remaining DSC, but cannot collect an earlier
        // recovery backup. A user can still restore that backup afterwards.
        const auto retry = saveNms(recovery);
        drain();
        Check(status(retry.operation, &written) == EDITOR_OK && written == 2 &&
                  outputs(retry.operation) == std::vector<std::string>({recovery, recoveryDsc}), "NMS export retry failed");
        for (const auto &backup : retainedBackups)
            Check(Bytes(backup) == oldDescription, "subsequent export discarded an earlier recovery backup");
        preserved();
    };
    for (bool existed : {false, true})
        for (auto writeFault : {FailingNmsPublication::Error, FailingNmsPublication::Exception, FailingNmsPublication::Unknown})
            for (auto fault : {FailingNmsPublication::None, FailingNmsPublication::Error,
                               FailingNmsPublication::Exception, FailingNmsPublication::Unknown})
                checkRecovery(existed, writeFault, fault, FailingNmsPublication::None);
    for (auto cleanup : {FailingNmsPublication::Error, FailingNmsPublication::Exception, FailingNmsPublication::Unknown})
        checkRecovery(true, FailingNmsPublication::Exception, FailingNmsPublication::Exception, cleanup);
    for (const auto &entry : fs::directory_iterator(root))
        if (entry.path().filename().u8string().find(".nemodev-save-") == 0)
            Check(std::find(retainedBackups.begin(), retainedBackups.end(), fs::weakly_canonical(entry.path())) != retainedBackups.end(),
                  "NMS recovery leaked an unreported temporary file");
    const auto blocked = (root / "blocked.nms").u8string();
    fs::create_directory(blocked);
    { std::ofstream file(BehaviorDescriptionPath(blocked)); file << "Keep previous description"; }
    const auto originalDsc = Bytes(BehaviorDescriptionPath(blocked));
    scriptExport = saveNms(blocked);
    drain();
    Check(status(scriptExport.operation, &written) == EDITOR_ERROR && written == 0 && fs::is_directory(blocked) &&
          Bytes(BehaviorDescriptionPath(blocked)) == originalDsc, "NMS publication failure did not restore previous DSC");
    // With no previous DSC, rollback removes the newly published sidecar.
    fs::remove(BehaviorDescriptionPath(blocked));
    scriptExport = saveNms(blocked);
    drain();
    Check(status(scriptExport.operation, &written) == EDITOR_ERROR && written == 0 &&
          !fs::exists(BehaviorDescriptionPath(blocked)), "new DSC survived failed NMS publication");
    const auto alias = (root / "same.dsc").u8string();
    { std::ofstream file(alias, std::ios::binary); file << "Original alias"; }
    scriptExport = saveNms(alias);
    drain();
    Check(status(scriptExport.operation, &written) == EDITOR_ERROR && written == 0 && Bytes(alias) == "Original alias",
          "NMS and DSC were published to the same path");
    const auto blockedDsc = (root / "blocked-dsc.nms").u8string();
    { std::ofstream file(blockedDsc, std::ios::binary); file << nmsBefore; }
    fs::create_directory(BehaviorDescriptionPath(blockedDsc));
    scriptExport = saveNms(blockedDsc);
    drain();
    Check(status(scriptExport.operation, &written) == EDITOR_ERROR && written == 0 && Bytes(blockedDsc) == nmsBefore,
          "DSC preparation failure overwrote NMS");
    scriptExport = saveNms(output, true);
    drain();
    input = commands.PendingInput();
    Check(input && input->kind == DocumentCommands::Input::Kind::BehaviorExport && input->behavior.author == "Author",
          "behavior export did not project existing DSC fields");
    const auto descriptionId = input->id;
    const EditorValue badVersion[] = {{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE, {}, {}, descriptionId},
        {EDITOR_TEXT("choice"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("accept")},
        {EDITOR_TEXT("version"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("nan")}};
    Check(editor.Execute("document.respond", {editor.Scope(), badVersion, 3}).status == EDITOR_INVALID && commands.PendingInput(),
          "invalid behavior version consumed its input");
    commands.Respond(descriptionId, "cancel");
    Check(status(scriptExport.operation) == EDITOR_CANCELLED && Bytes(output) == nmsBefore &&
          Bytes(BehaviorDescriptionPath(output)) == dscBefore, "description cancellation changed output files");
    Check(Bytes(nmo) == previous, "later commands changed an earlier export");
    runtime.Finish();
    commands.Stop();
}
