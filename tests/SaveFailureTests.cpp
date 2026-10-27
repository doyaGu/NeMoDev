#include "VirtoolsDevTestSupport.h"

#include <SDL3/SDL_timer.h>
#include <memory>
#include <stdexcept>
#include "core/runtime/SdkCompat.h"

namespace vdevtest {

class SaveFailureManager : public CKBaseManager {
public:
    enum Mode { None, WriteFailure, VerificationFailure, ReplacementFailure, DestinationAppeared } mode = None;
    std::filesystem::path target, written;
    std::string error;
    int saves = 0;
    std::function<void()> afterSave;
    std::function<void()> beforeWrite;
#ifdef _WIN32
    HANDLE locked = INVALID_HANDLE_VALUE;
#else
    std::filesystem::perms permissions = std::filesystem::perms::unknown;
#endif

    explicit SaveFailureManager(CKContext *context, CKGUID guid = CKGUID(0x64b63c49, 0x122922e1))
        : CKBaseManager(context, guid, "Save failure regression") {}

    CKDWORD GetValidFunctionsMask() override { return CKMANAGER_FUNC_PostSave; }
    CKStateChunk *SaveData(CKFile *file) override {
        ++saves;
        written = std::filesystem::path(file->m_FileName);
        if (beforeWrite) beforeWrite();
        if (mode == WriteFailure) {
            std::error_code failure;
            std::filesystem::permissions(written, std::filesystem::perms::owner_read,
                                         std::filesystem::perm_options::replace, failure);
            if (failure)
                error = failure.message();
        }
        return nullptr;
    }
    CKERROR PostSave() override {
        std::error_code failure;
        if (mode == WriteFailure) {
            std::filesystem::permissions(written, std::filesystem::perms::owner_all,
                                         std::filesystem::perm_options::replace, failure);
            if (failure)
                error = failure.message();
        } else if (mode == VerificationFailure) {
            std::ofstream file(written, std::ios::binary | std::ios::trunc);
            file << "Incomplete composition";
            file.close();
            if (!file)
                error = "Could not corrupt the saved candidate";
        } else if (mode == DestinationAppeared) {
            std::ofstream file(target, std::ios::binary);
            file << "Created by another writer";
            file.close();
            if (!file)
                error = "Could not create the competing destination";
        } else if (mode == ReplacementFailure) {
#ifdef _WIN32
            // Readers and writers may open the old target, but replacing it is denied.
            locked = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (locked == INVALID_HANDLE_VALUE)
                error = "Could not lock the replacement target";
#else
            const auto directory = target.parent_path();
            permissions = std::filesystem::status(directory, failure).permissions();
            if (!failure)
                std::filesystem::permissions(directory,
                                             std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
                                             std::filesystem::perm_options::replace, failure);
            if (failure)
                error = failure.message();
#endif
        }
        if (afterSave) afterSave();
        return CK_OK;
    }
    void Reset() {
#ifdef _WIN32
        if (locked != INVALID_HANDLE_VALUE)
            CloseHandle(locked);
        locked = INVALID_HANDLE_VALUE;
#else
        if (permissions != std::filesystem::perms::unknown) {
            std::error_code ignored;
            std::filesystem::permissions(target.parent_path(), permissions, std::filesystem::perm_options::replace,
                                         ignored);
        }
        permissions = std::filesystem::perms::unknown;
#endif
        mode = None;
        afterSave = nullptr;
        beforeWrite = nullptr;
    }
    ~SaveFailureManager() override { Reset(); }
};

class SaveFailureFolder {
public:
    std::filesystem::path directory;
    SaveFailureManager *manager;
    explicit SaveFailureFolder(CKContext *context, CKGUID guid = CKGUID(0x64b63c49, 0x122922e1))
        : directory(std::filesystem::absolute("save-failures-" + std::to_string(SDL_GetTicksNS()))),
          manager(new SaveFailureManager(context, guid)) {
        CHECK(std::filesystem::create_directory(directory));
        CHECK(context->RegisterNewManager(manager) == CK_OK);
    }
    ~SaveFailureFolder() {
        manager->Reset();
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};

void SaveCallbacks(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    SaveFailureFolder folder(document.Context(), CKGUID(0x63a54911, 0x32816274));
    auto &manager = *folder.manager;
    SaveOptions options;
    options.allowOverwrite = true;
    SaveReport report;
    for (bool display : {false, true}) {
        CHECK(document.New(nullptr));
        auto *scene = document.Context()->GetCurrentLevel()->GetLevelScene();
        scene->SetFogEnd(100);
        const auto owner = CreateLayoutObject(document, LayoutObject::Frame3D);
        CHECK(owner.ok);
        const auto script = CreateScript(document, owner.created, "Saved Script");
        CHECK(script.ok);
        CHECK(AddLocalParameter(document, script.created, script.created, "Saved value", CKPGUID_INT, Point(100, 100)).ok);
        ScriptSession *session = document.Session(script.created);
        CHECK(session && !session->ReadOnly());
        const auto color = session->Data().header.color;
        const auto path = folder.directory / (display ? "display.cmo" : "topology.cmo");
        manager.afterSave = [&] {
            if (display) {
                InterfaceData data = session->Data();
                data.header.color = color ^ 0x00FFFFFF;
                CHECK(session->Assign(data));
            } else {
                scene->SetFogEnd(777);
                document.MarkTopologyChanged();
            }
        };
        const bool written = SaveDocumentAs(document, path.u8string(), options, report);
        for (const auto &error : report.errors) std::cerr << error << '\n';
        CHECK(written);
        CHECK(report.saved && !report.documentSaved && document.Dirty());
        CHECK(display ? session->Data().header.color != color : scene->GetFogEnd() == 777);
        manager.afterSave = nullptr;
        CHECK(document.Open(path.u8string(), nullptr));
        CHECK(document.Context()->GetCurrentLevel()->GetLevelScene()->GetFogEnd() == 100);
        CHECK(document.Scripts().size() == 1);
        CHECK(document.Session(document.Scripts().front().id)->Data().header.color == color);
        CHECK(!document.Dirty());
    }
    // Merely opening a clean session or changing its view must not prevent saving.
    auto *session = document.Session(document.Scripts().front().id);
    manager.afterSave = [&] { session->SetOpen(!session->IsOpen()); };
    const auto path = folder.directory / "view.cmo";
    CHECK(SaveDocumentAs(document, path.u8string(), options, report));
    CHECK(report.saved && report.documentSaved && !document.Dirty());
    manager.afterSave = nullptr;
    auto *scene = document.Context()->GetCurrentLevel()->GetLevelScene();
    auto environment = ReadSceneEnvironment(scene);
    environment.fogEnd = 222;
    CHECK(SetSceneEnvironment(document, scene->GetID(), environment, "After save").ok);
    CHECK(document.History().Undo() && !document.Dirty());
    CHECK(document.History().Redo() && document.Dirty());

    // A callback undoing an edit can reach the old save point while EndSave
    // is still running. The replaced file contains the newer value instead.
    CHECK(document.New(nullptr));
    scene = document.Context()->GetCurrentLevel()->GetLevelScene();
    scene->SetFogEnd(100);
    const auto historyPath = folder.directory / "history.cmo";
    CHECK(SaveDocumentAs(document, historyPath.u8string(), options, report));
    CHECK(report.documentSaved);
    environment = ReadSceneEnvironment(scene);
    environment.fogEnd = 200;
    CHECK(SetSceneEnvironment(document, scene->GetID(), environment, "Before callback").ok);
    manager.afterSave = [&] { CHECK(document.History().Undo()); };
    CHECK(SaveDocumentAs(document, historyPath.u8string(), options, report));
    manager.afterSave = nullptr;
    CHECK(report.saved && !report.documentSaved && document.Dirty() && scene->GetFogEnd() == 100);
    CHECK(document.History().Redo() && document.Dirty() && scene->GetFogEnd() == 200);
    CHECK(document.History().Undo() && document.Dirty() && scene->GetFogEnd() == 100);
    CHECK(document.Open(historyPath.u8string(), nullptr));
    CHECK(document.Context()->GetCurrentLevel()->GetLevelScene()->GetFogEnd() == 200);
}

static std::vector<unsigned char> SavedFileBytes(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.is_open());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void EmptyScriptSave(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    SaveFailureFolder folder(document.Context(), CKGUID(0x41627851, 0x71265231));
    const auto owner = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(owner.ok);
    const auto made = CreateScript(document, owner.created, "Empty Script");
    CHECK(made.ok);
    auto *script = CKBehavior::Cast(document.Context()->GetObject(made.created));
    CHECK(script && script->GetSubBehaviorCount() == 0 && script->GetLocalParameterCount() == 0);
    CHECK(script->GetInputParameterCount() == 0 && script->GetOutputParameterCount() == 0);
    CHECK(script->GetInputCount() == 1 && std::string(script->GetInput(0)->GetName()) == "Start");
    const auto undo = document.History().UndoCount();
    const auto color = document.Session(made.created)->Data().header.color;
    const auto path = (folder.directory / "empty-script.cmo").u8string();
    SaveReport report;
    const bool saved = SaveDocumentAs(document, path, {}, report);
    for (const auto &error : report.errors) std::cerr << error << '\n';
    CHECK(saved && report.saved && report.documentSaved && report.errors.empty());
    CHECK(report.parametersVerified == 0 && !document.Dirty());
    CHECK(document.History().UndoCount() == undo);
    CHECK(document.Open(path, nullptr) && document.Scripts().size() == 1);
    script = CKBehavior::Cast(document.Context()->GetObject(document.Scripts().front().id));
    CHECK(script && std::string(script->GetName()) == "Empty Script" && script->GetOwner());
    CHECK(script->GetInputCount() == 1 && std::string(script->GetInput(0)->GetName()) == "Start");
    CHECK(script->GetSubBehaviorCount() == 0 && script->GetLocalParameterCount() == 0);
    CHECK(script->GetInputParameterCount() == 0 && script->GetOutputParameterCount() == 0);
    auto *session = document.Session(script->GetID());
    CHECK(session && !session->ReadOnly() && session->Data().header.color == color);
    CHECK(!document.Dirty());

    // Editing only the display still leaves the native parameter set empty,
    // and exercises generation and verification of an empty InterfaceChunk.
    CHECK(EditScriptData(document, script->GetID(), "Empty Script color",
                         [](InterfaceData &data) { data.header.color ^= 0x00FFFFFF; }));
    const auto displayPath = (folder.directory / "empty-display.cmo").u8string();
    CHECK(SaveDocumentAs(document, displayPath, {}, report));
    CHECK(report.parametersVerified == 0 && report.scriptsVerified == 1 && report.chunksWritten == 1);
    CHECK(document.Open(displayPath, nullptr));
    script = CKBehavior::Cast(document.Context()->GetObject(document.Scripts().front().id));
    session = document.Session(script->GetID());
    CHECK(session && session->Origin() == SessionOrigin::LoadedChunk && session->Data().header.color == (color ^ 0x00FFFFFF));

    // An untouched loaded empty Script also saves without manufacturing a
    // parameter or rewriting its original display chunk.
    const auto chunk = ChunkBytes(script->GetInterfaceChunk());
    SaveOptions options;
    options.allowOverwrite = true;
    const auto resaved = (folder.directory / "resaved.cmo").u8string();
    CHECK(SaveDocumentAs(document, resaved, options, report));
    CHECK(report.parametersVerified == 0 && report.chunksWritten == 0 && !document.Dirty());
    CHECK(ChunkBytes(script->GetInterfaceChunk()) == chunk && CompareReloadedChunks(document, resaved) == 1);

    // Zero parameters must still go through independent file verification.
    auto &manager = *folder.manager;
    manager.target = resaved;
    const auto previousFile = SavedFileBytes(manager.target);
    manager.mode = SaveFailureManager::VerificationFailure;
    const bool corrupted = SaveDocumentAs(document, resaved, options, report);
    manager.Reset();
    CHECK(!corrupted && !report.saved && !document.Dirty());
    CHECK(!report.errors.empty() && report.errors.front().find("The saved file does not load") == 0);
    CHECK(SavedFileBytes(manager.target) == previousFile && !std::filesystem::exists(manager.written));

    // A real capture failure can also leave the result empty. It must still
    // refuse publication and preserve the old file and dirty state.
    auto *untyped = CKParameterLocal::Cast(document.Context()->CreateObject(CKCID_PARAMETERLOCAL, (CKSTRING) "Untyped value"));
    CHECK(untyped && !document.Context()->GetParameterManager()->GetParameterTypeDescription(untyped->GetGUID()));
    document.AdoptObjects({untyped->GetID()});
    CHECK(!SaveDocumentAs(document, resaved, options, report));
    CHECK(!report.saved && document.Dirty() && !report.errors.empty());
    CHECK(report.errors.front().find("Cannot capture saved parameter types: Unregistered parameter type:") == 0);
    CHECK(SavedFileBytes(manager.target) == previousFile && !std::filesystem::exists(manager.written));
    CHECK(document.New(nullptr));

    // Multiple empty Scripts, on the level and on an entity, still have no
    // parameters. The player export follows the same capture contract.
    const auto frame = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(frame.ok);
    CHECK(CreateScript(document, frame.created, "Entity Script").ok);
    CHECK(CreateScript(document, document.Context()->GetCurrentLevel()->GetID(), "Level Script").ok);
    const auto emptyScripts = document.Scripts();
    for (const auto &info : emptyScripts)
        CHECK(EditScriptData(document, info.id, "Empty Script display",
                             [](InterfaceData &data) { data.header.color ^= 0x0000FFFF; }));
    const auto multiple = (folder.directory / "multiple.cmo").u8string();
    CHECK(SaveDocumentAs(document, multiple, options, report));
    CHECK(report.documentSaved && report.parametersVerified == 0 && report.scriptsVerified == 2);
    CHECK(document.Open(multiple, nullptr) && document.Scripts().size() == 2 && !document.Dirty());
    for (const auto &info : document.Scripts()) {
        auto *empty = CKBehavior::Cast(document.Context()->GetObject(info.id));
        CHECK(empty && empty->GetSubBehaviorCount() == 0 && empty->GetLocalParameterCount() == 0);
        CHECK(empty->GetInputParameterCount() == 0 && empty->GetOutputParameterCount() == 0);
    }
    options.markSaved = false;
    options.writeMode = CKFILE_FORVIEWER | CKFILE_WHOLECOMPRESSED;
    const auto player = (folder.directory / "empty-scripts.vmo").u8string();
    CHECK(SaveDocumentAs(document, player, options, report));
    CHECK(report.saved && !report.documentSaved && report.parametersVerified == 0 && !document.Dirty());
    CHECK(IsPlayerFile(player) && document.Open(player, nullptr) && document.Scripts().size() == 2);
    std::cout << "Empty Scripts save and reload without bypassing capture failures or file verification\n";
}

static void CheckExportFailures(CkRuntimeHost &host, bool nms) {
    namespace fs = std::filesystem;
    Document document(host);
    CHECK(document.New(nullptr));
    SaveFailureFolder folder(document.Context(), CKGUID(0x719a3781, nms ? 0x14386321 : 0x24386321));
    const auto owner = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(owner.ok);
    const auto script = CreateScript(document, owner.created, "Export Script");
    CHECK(script.ok);
    const auto graph = CreateBehaviorGraph(document, script.created, script.created, "Export Graph", Point(200, 100));
    CHECK(graph.ok);
    const auto parameter = AddLocalParameter(document, script.created, graph.created, "Value", CKPGUID_INT, Point(100, 100));
    CHECK(parameter.ok && SetParameterText(document, script.created, parameter.created, "137").ok);
    SaveReport report;
    CHECK(SaveDocumentAs(document, (folder.directory / "source.cmo").u8string(), {}, report));
    CHECK(!document.Dirty());
    const auto path = (folder.directory / (nms ? "previous.nms" : "previous.nmo")).u8string();
    auto save = [&](const std::string &output, bool hidden, std::string *error) {
        return nms ? SaveBehaviorsAsNms(document, {graph.created, graph.created}, output, error, hidden)
                   : SaveObjectsAs(document, {owner.created, owner.created}, output, error, hidden);
    };
    CKContext *context = document.Context();
    auto *behavior = CKBehavior::Cast(context->GetObject(graph.created));
    auto *scriptBehavior = CKBehavior::Cast(context->GetObject(script.created));
    auto *session = document.Session(script.created);
    const auto data = Capture(document, script.created);
    const auto graphFlags = behavior->GetFlags();
    const auto graphChunk = ChunkBytes(behavior->GetInterfaceChunk());
    const auto scriptChunk = ChunkBytes(scriptBehavior->GetInterfaceChunk());
    const auto revision = session->Revision();
    const auto undo = document.History().UndoCount();
    const auto writeMode = context->GetFileWriteMode();
    std::map<CK_ID, CKDWORD> objectFlags;
    const auto &allObjects = context->GetObjectListByType(CKCID_OBJECT, TRUE);
    for (int i = 0; i < allObjects.Size(); ++i)
        objectFlags.emplace(allObjects[i]->GetID(), allObjects[i]->GetObjectFlags());
    auto &manager = *folder.manager;
    manager.target = path;
    {
        std::ofstream file(manager.target, std::ios::binary);
        file << "Original file must survive a failed export";
        CHECK(file.good());
    }
    const auto original = SavedFileBytes(manager.target);
#ifndef _WIN32
    const auto permissions = fs::perms::owner_read | fs::perms::owner_write;
    fs::permissions(manager.target, permissions);
#endif
    std::string error;
    auto unchanged = [&] {
        CHECK(document.Context() == context && !document.Dirty() && session->Revision() == revision);
        CHECK(document.History().UndoCount() == undo && context->GetFileWriteMode() == writeMode && !context->m_Saving);
        CHECK(behavior->GetFlags() == graphFlags && ChunkBytes(behavior->GetInterfaceChunk()) == graphChunk);
        CHECK(ChunkBytes(scriptBehavior->GetInterfaceChunk()) == scriptChunk);
        for (const auto &entry : objectFlags)
            CHECK(context->GetObject(entry.first) && context->GetObject(entry.first)->GetObjectFlags() == entry.second);
        ExpectSame(data, Capture(document, script.created), "export preserves source layout");
    };
    const int failureCount =
#if defined(_WIN32) && defined(_MSC_VER)
        6;
#else
        5;
#endif
    for (bool hidden : {false, true})
    for (int failure = 0; failure < failureCount; ++failure) {
        manager.error.clear();
        error.clear();
        if (failure == 0 || failure == 5) manager.afterSave = [failure] {
#if defined(_WIN32) && defined(_MSC_VER)
            if (failure == 5) RaiseException(0xE0421068, 0, 0, nullptr);
#endif
            throw std::runtime_error("Injected export PostSave failure");
        };
        else if (failure == 4) manager.beforeWrite = [] { throw std::runtime_error("Injected export SaveData failure"); };
        else manager.mode = failure == 1 ? SaveFailureManager::WriteFailure
                            : failure == 2 ? SaveFailureManager::VerificationFailure : SaveFailureManager::ReplacementFailure;
        const bool saved = save(path, hidden, &error);
        manager.Reset();
        CHECK(!saved && !error.empty() && manager.error.empty());
        const char *stage = failure == 1 ? "Native export failed:"
                            : failure == 2 ? "could not be verified"
                            : failure == 3 ? "Cannot replace the destination file" : "raised an exception";
        CHECK(error.find(stage) != std::string::npos);
        CHECK(SavedFileBytes(manager.target) == original);
        CHECK(manager.written != manager.target && manager.written.parent_path() == manager.target.parent_path());
        CHECK(manager.written.extension() == manager.target.extension());
        unchanged();
        if (fs::exists(manager.written)) {
#ifndef _WIN32
            CHECK(failure == 3 && error.find("Cannot remove temporary save file") != std::string::npos);
            CHECK(fs::remove(manager.written));
#else
            CHECK(false);
#endif
        }
        CHECK(std::distance(fs::directory_iterator(folder.directory), fs::directory_iterator()) == 2);
    }
    // A failed export to a new path must not publish even a partial file.
    const auto absent = (folder.directory / (nms ? "new.nms" : "new.nmo")).u8string();
    for (bool corrupt : {false, true}) {
        if (corrupt) manager.mode = SaveFailureManager::VerificationFailure;
        else manager.afterSave = [] { throw std::runtime_error("Injected new export failure"); };
        const bool saved = save(absent, true, &error);
        manager.Reset();
        CHECK(!saved && !fs::exists(absent) && !fs::exists(manager.written));
        unchanged();
    }
    const int saves = manager.saves;
    CHECK(!save((folder.directory / "missing" / "file.nmo").u8string(), true, &error));
    CHECK(manager.saves == saves && SavedFileBytes(manager.target) == original);
    unchanged();
    // A subsequent successful export replaces only the output. Reopening it
    // proves its graph, parameters and hidden-display option survived writing.
    for (bool hidden : {false, true}) {
        const bool saved = save(path, hidden, &error);
        if (!saved) std::cerr << "Export retry failed: " << error << '\n';
        CHECK(saved);
        CHECK(!fs::exists(manager.written) && SavedFileBytes(manager.target) != original);
#ifndef _WIN32
        CHECK(fs::status(manager.target).permissions() == permissions);
#endif
        unchanged();
        CKContext *loaded = nullptr;
        CHECK(CreateCKContext(&loaded, nullptr) == CK_OK && loaded);
        std::unique_ptr<CKContext, void (*)(CKContext *)> close(loaded, [](CKContext *context) {
            context->SetInterfaceMode(FALSE, nullptr, nullptr);
            CKCloseContext(context);
        });
        loaded->SetInterfaceMode(TRUE, nullptr, nullptr);
        std::unique_ptr<CKObjectArray, decltype(&DeleteCKObjectArray)> objects(CreateCKObjectArray(), &DeleteCKObjectArray);
        std::unique_ptr<CKFile> file(loaded->CreateCKFile());
        CHECK(file->Load(const_cast<CKSTRING>(path.c_str()), objects.get(), CK_LOAD_DEFAULT) == CK_OK);
        auto *graphCopy = CKBehavior::Cast(loaded->GetObjectByNameAndClass((CKSTRING) "Export Graph", CKCID_BEHAVIOR));
        auto *scriptCopy = CKBehavior::Cast(loaded->GetObjectByNameAndClass((CKSTRING) "Export Script", CKCID_BEHAVIOR));
        CHECK(graphCopy && graphCopy->GetLocalParameterCount() == 1);
        int value = 0;
        CHECK(graphCopy->GetLocalParameter(0)->GetValue(&value, FALSE) == CK_OK && value == 137);
        if (hidden) {
            CKBehavior *display = nms ? graphCopy : scriptCopy;
            InterfaceData restored;
            CHECK(display && restored.LoadFromChunk(display, display->GetInterfaceChunk()) == CK_OK);
            CHECK((restored.rootBehavior.flags & 0x20000) != 0);
        }
    }
}

void ObjectExportFailures(CkRuntimeHost &host) { CheckExportFailures(host, false); }
void NmsExportFailures(CkRuntimeHost &host) { CheckExportFailures(host, true); }

void SaveScriptSnapshots(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    SaveFailureFolder folder(document.Context(), CKGUID(0x382a7861, 0x64261894));
    const auto owner = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(owner.ok);
    const auto script = CreateScript(document, owner.created, "Snapshot Script");
    CHECK(script.ok);
    CHECK(AddLocalParameter(document, script.created, script.created, "Value", CKPGUID_INT, Point(100, 100)).ok);
    CKDWORD pixels[] = {0xFF102030, 0xFF234567, 0xFF89ABCD, 0xFFABCDEF, 0xFF543210, 0xFF765432};
    VxImageDescEx image;
    image.Width = 3; image.Height = 2; image.BitsPerPixel = 32; image.BytesPerLine = 3 * 4;
    image.RedMask = 0x00FF0000; image.GreenMask = 0x0000FF00;
    image.BlueMask = 0x000000FF; image.AlphaMask = 0xFF000000;
    image.Image = reinterpret_cast<XBYTE *>(pixels);
    void *bitmap = VxCreateBitmap(image);
    CHECK(bitmap);
    document.KeepBitmap(bitmap);
    CHECK(SetScriptSnapshot(document, script.created, bitmap));
    auto *session = document.Session(script.created);
    CHECK(session && session->Data().header.snapshot == bitmap);
    const auto withSnapshot = (folder.directory / "with-snapshot.cmo").u8string();
    const auto withoutSnapshot = (folder.directory / "without-snapshot.cmo").u8string();
    const auto enabledAgain = (folder.directory / "enabled-again.cmo").u8string();
    CKDWORD color = session->Data().header.color;
    SaveOptions options;
    options.allowOverwrite = true;
    SaveReport report;
    auto verify = [&](const std::string &path, bool expected) {
        CKContext *context = nullptr;
        CHECK(CreateCKContext(&context, nullptr) == CK_OK && context);
        std::unique_ptr<CKContext, void (*)(CKContext *)> verification(context, [](CKContext *context) {
            context->SetInterfaceMode(FALSE, nullptr, nullptr);
            CKCloseContext(context);
        });
        context->SetInterfaceMode(TRUE, nullptr, nullptr);
        std::unique_ptr<CKObjectArray, decltype(&DeleteCKObjectArray)> objects(CreateCKObjectArray(), &DeleteCKObjectArray);
        std::unique_ptr<CKFile> file(context->CreateCKFile());
        CHECK(file->Load(const_cast<CKSTRING>(path.c_str()), objects.get(), CK_LOAD_DEFAULT) == CK_OK);
        auto *script = CKBehavior::Cast(context->GetObjectByNameAndClass((CKSTRING) "Snapshot Script", CKCID_BEHAVIOR));
        InterfaceData loaded;
        CHECK(script && loaded.LoadFromChunk(script, script->GetInterfaceChunk()) == CK_OK);
        CHECK((loaded.header.snapshot != nullptr) == expected && loaded.header.color == color);
        if (expected) {
            VxImageDescEx description;
            std::unique_ptr<XBYTE[]> copy(VxConvertBitmap(loaded.header.snapshot, description));
            CHECK(copy && description.Width == 3 && description.Height == 2);
            for (int y = 0; y < 2; ++y)
                CHECK(std::memcmp(copy.get() + y * description.BytesPerLine, pixels + y * 3, 3 * 4) == 0);
        }
    };
    CHECK(SaveDocumentAs(document, withSnapshot, options, report));
    CHECK(report.documentSaved && !document.Dirty());
    verify(withSnapshot, true);
    auto *native = CKBehavior::Cast(document.Context()->GetObject(script.created));
    const auto originalChunk = ChunkBytes(native->GetInterfaceChunk());
    CHECK(!originalChunk.empty());
    const auto revision = session->Revision();
    const auto undoCount = document.History().UndoCount();
    options.saveScriptSnapshots = false;
    CHECK(SaveDocumentAs(document, withoutSnapshot, options, report));
    CHECK(!document.Dirty() && !session->Dirty() && session->Data().header.snapshot == bitmap);
    CHECK(ChunkBytes(native->GetInterfaceChunk()) == originalChunk);
    verify(withoutSnapshot, false);
    options.saveScriptSnapshots = true;
    CHECK(SaveDocumentAs(document, withSnapshot, options, report));
    CHECK(report.chunksWritten == 0 && ChunkBytes(native->GetInterfaceChunk()) == originalChunk);
    verify(withSnapshot, true);
    CHECK(!document.Dirty() && session->Data().header.snapshot == bitmap);
    CHECK(session->Revision() == revision && document.History().UndoCount() == undoCount);

    // Omitting snapshots must also work before opening any Script session.
    CHECK(document.Open(withSnapshot, nullptr) && document.Sessions().empty());
    native = CKBehavior::Cast(document.Context()->GetObject(document.Scripts().front().id));
    const auto loadedChunk = ChunkBytes(native->GetInterfaceChunk());
    options.saveScriptSnapshots = false;
    CHECK(SaveDocumentAs(document, withoutSnapshot, options, report));
    session = document.ExistingSession(native->GetID());
    CHECK(session && !session->Dirty() && session->Data().header.snapshot);
    bitmap = session->Data().header.snapshot;
    CHECK(ChunkBytes(native->GetInterfaceChunk()) == loadedChunk);
    verify(withoutSnapshot, false);
    options.saveScriptSnapshots = true;
    CHECK(SaveDocumentAs(document, enabledAgain, options, report));
    CHECK(report.chunksWritten == 0 && CompareReloadedChunks(document, enabledAgain) == 1);
    verify(enabledAgain, true);

    // Exports do not change the save point or poison later composition saves.
    options.markSaved = false;
    options.saveScriptSnapshots = false;
    CHECK(SaveDocumentAs(document, withoutSnapshot, options, report));
    CHECK(report.saved && !report.documentSaved && !document.Dirty());
    CHECK(ChunkBytes(native->GetInterfaceChunk()) == loadedChunk);
    verify(withoutSnapshot, false);
    options.markSaved = options.saveScriptSnapshots = true;
    CHECK(SaveDocumentAs(document, enabledAgain, options, report));
    verify(enabledAgain, true);

    // Failed native writes, verification and publication must restore the
    // complete chunk as well. A retry with snapshots enabled stays lossless.
    auto &manager = *folder.manager;
    manager.target = withoutSnapshot;
    const auto previousFile = SavedFileBytes(manager.target);
    const int failures =
#if defined(_WIN32) && defined(_MSC_VER)
        4;
#else
        3;
#endif
    for (int failure = 0; failure < failures; ++failure) {
        options.saveScriptSnapshots = false;
        if (failure == 0 || failure == 3) manager.afterSave = [failure] {
#if defined(_WIN32) && defined(_MSC_VER)
            if (failure == 3) RaiseException(0xE0421067, 0, 0, nullptr);
#endif
            throw std::runtime_error("Injected snapshot save failure");
        };
        else manager.mode = failure == 1 ? SaveFailureManager::VerificationFailure : SaveFailureManager::ReplacementFailure;
        const bool saved = SaveDocumentAs(document, withoutSnapshot, options, report);
        manager.Reset();
        CHECK(!saved && !report.errors.empty());
        CHECK(!document.Dirty() && !session->Dirty() && session->Data().header.snapshot == bitmap);
        CHECK(ChunkBytes(native->GetInterfaceChunk()) == loadedChunk);
        CHECK(SavedFileBytes(manager.target) == previousFile);
        options.saveScriptSnapshots = true;
        CHECK(SaveDocumentAs(document, enabledAgain, options, report));
        CHECK(report.chunksWritten == 0);
        verify(enabledAgain, true);
    }

    // A dirty Script needs its edited layout retained along with its bitmap,
    // even though this particular file deliberately omits that bitmap.
    InterfaceData edited = session->Data();
    color = edited.header.color ^= 0x00FFFFFF;
    CHECK(session->Assign(edited) && document.Dirty());
    options.saveScriptSnapshots = false;
    CHECK(SaveDocumentAs(document, withoutSnapshot, options, report) && report.documentSaved);
    CHECK(!document.Dirty() && session->Data().header.snapshot == bitmap);
    verify(withoutSnapshot, false);
    options.saveScriptSnapshots = true;
    CHECK(SaveDocumentAs(document, enabledAgain, options, report));
    CHECK(report.chunksWritten == 0 && CompareReloadedChunks(document, enabledAgain) == 1);
    verify(enabledAgain, true);

    // Restoring the complete cache must not undo edits made by a save
    // callback. They remain dirty and are included by the following save.
    manager.afterSave = [&] {
        InterfaceData changed = session->Data();
        changed.header.color = color ^ 0x0000FFFF;
        CHECK(session->Assign(changed));
    };
    options.saveScriptSnapshots = false;
    const bool callbackSaved = SaveDocumentAs(document, withoutSnapshot, options, report);
    manager.Reset();
    CHECK(callbackSaved && report.saved && !report.documentSaved && document.Dirty());
    CHECK(session->Data().header.snapshot == bitmap);
    verify(withoutSnapshot, false);
    color = session->Data().header.color;
    options.saveScriptSnapshots = true;
    CHECK(SaveDocumentAs(document, enabledAgain, options, report) && !document.Dirty());
    verify(enabledAgain, true);

    // A Script with no snapshot remains valid under either option.
    CHECK(SetScriptSnapshot(document, native->GetID(), nullptr));
    CHECK(SaveDocumentAs(document, enabledAgain, options, report));
    verify(enabledAgain, false);
    const auto emptyChunk = ChunkBytes(native->GetInterfaceChunk());
    options.saveScriptSnapshots = false;
    CHECK(SaveDocumentAs(document, withoutSnapshot, options, report));
    CHECK(report.chunksWritten == 0 && ChunkBytes(native->GetInterfaceChunk()) == emptyChunk);
    verify(withoutSnapshot, false);
    std::cout << "Snapshot save options preserve full chunks, pixels and save points across toggles and failures\n";
}

#ifdef _WIN32
static std::vector<unsigned char> SavedFileSecurity(const std::filesystem::path &path) {
    DWORD size = 0;
    GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &size);
    CHECK(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
    std::vector<unsigned char> security(size);
    CHECK(GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION,
                           reinterpret_cast<PSECURITY_DESCRIPTOR>(security.data()), size, &size));
    return security;
}
#endif

void SaveFailures(CkRuntimeHost &host) {
    Document document(host);
    CHECK(document.New(nullptr));
    SaveFailureFolder folder(document.Context());
    auto &manager = *folder.manager;
    manager.target = folder.directory / "previous.cmo";
    SaveOptions options;
    options.allowOverwrite = true;
    SaveReport report;
    CHECK(SaveDocumentAs(document, manager.target.u8string(), options, report));
    CHECK(report.saved && !document.Dirty());
    document.History().MarkSavePoint();
#ifdef _WIN32
    auto security = SavedFileSecurity(manager.target);
    CHECK(SetFileSecurityW(manager.target.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                           reinterpret_cast<PSECURITY_DESCRIPTOR>(security.data())));
    security = SavedFileSecurity(manager.target);
#else
    const auto permissions = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
    std::filesystem::permissions(manager.target, permissions);
#endif
    const auto original = SavedFileBytes(manager.target);
    CHECK(!original.empty());
    auto *scene = document.Context()->GetCurrentLevel()->GetLevelScene();
    const auto before = ReadSceneEnvironment(scene);
    const auto writeMode = document.Context()->GetFileWriteMode();
    auto changed = before;
    changed.fogEnd = before.fogEnd + 123;
    CHECK(SetSceneEnvironment(document, scene->GetID(), changed, "Save failure value").ok);
    CHECK(document.Dirty());

    for (auto mode : {SaveFailureManager::WriteFailure, SaveFailureManager::VerificationFailure,
                      SaveFailureManager::ReplacementFailure}) {
        manager.error.clear();
        manager.mode = mode;
        CHECK(!SaveDocumentAs(document, manager.target.u8string(), options, report));
        CHECK(manager.error.empty() && !report.errors.empty());
        const char *stage = mode == SaveFailureManager::WriteFailure ? "CKFile save failed:"
                            : mode == SaveFailureManager::VerificationFailure
                                ? "The saved file does not load"
                                : "Cannot replace the destination file";
        CHECK(report.errors.front().find(stage) == 0);
        CHECK(SavedFileBytes(manager.target) == original);
        CHECK(manager.written != manager.target && manager.written.parent_path() == manager.target.parent_path());
        CHECK(manager.written.extension() == manager.target.extension());
        CHECK(!report.saved && document.Dirty());
        CHECK(document.Context()->GetFileWriteMode() == writeMode);
        CHECK(ReadSceneEnvironment(scene).fogEnd == changed.fogEnd);
        CHECK(document.History().Undo() && !document.Dirty());
        CHECK(ReadSceneEnvironment(scene).fogEnd == before.fogEnd);
        CHECK(document.History().Redo() && document.Dirty());
        CHECK(ReadSceneEnvironment(scene).fogEnd == changed.fogEnd);
        manager.Reset();
        // An unwritable directory also denies candidate cleanup. The saver must
        // report that leftover; once access is restored the fixture removes it.
        if (std::filesystem::exists(manager.written)) {
#ifndef _WIN32
            CHECK(mode == SaveFailureManager::ReplacementFailure);
            CHECK(std::any_of(report.notes.begin(), report.notes.end(),
                              [](const auto &note) { return note.find("Cannot remove temporary save file") == 0; }));
            CHECK(std::filesystem::remove(manager.written));
#else
            CHECK(false);
#endif
        }
        CHECK(std::distance(std::filesystem::directory_iterator(folder.directory),
                            std::filesystem::directory_iterator()) == 1);
    }

    const int saves = manager.saves;
    options.allowOverwrite = false;
    CHECK(!SaveDocumentAs(document, manager.target.u8string(), options, report));
    CHECK(!report.saved && document.Dirty() && manager.saves == saves);
    CHECK(SavedFileBytes(manager.target) == original);
    CHECK(!SaveDocumentAs(document, (folder.directory / "absent" / "new.cmo").u8string(), options, report));
    CHECK(!report.saved && document.Dirty() && manager.saves == saves);

    const auto previous = manager.target;
    manager.target = folder.directory / "new.cmo";
    manager.mode = SaveFailureManager::VerificationFailure;
    CHECK(!SaveDocumentAs(document, manager.target.u8string(), options, report));
    CHECK(!report.saved && !std::filesystem::exists(manager.target) && document.Dirty());
    CHECK(!std::filesystem::exists(manager.written));
    manager.Reset();

    manager.mode = SaveFailureManager::DestinationAppeared;
    CHECK(!SaveDocumentAs(document, manager.target.u8string(), options, report));
    CHECK(!report.saved && document.Dirty() && manager.error.empty());
    const auto competing = SavedFileBytes(manager.target);
    CHECK(std::string(competing.begin(), competing.end()) == "Created by another writer");
    CHECK(!std::filesystem::exists(manager.written));
    manager.Reset();
    CHECK(std::filesystem::remove(manager.target));
    manager.target = previous;

    options.allowOverwrite = true;
    options.markSaved = false;
    options.writeMode = CKFILE_FORVIEWER | CKFILE_WHOLECOMPRESSED;
    const auto exported = folder.directory / "export.vmo";
    CHECK(SaveDocumentAs(document, exported.u8string(), options, report));
    CHECK(report.saved && IsPlayerFile(exported.u8string()) && document.Dirty());
    CHECK(document.Context()->GetFileWriteMode() == writeMode);
    CHECK(!std::filesystem::exists(manager.written));
    CHECK(SavedFileBytes(manager.target) == original);

#ifndef _WIN32
    options.writeMode = 0;
    const auto links = folder.directory / "links";
    CHECK(std::filesystem::create_directory(links));
    const auto alias = links / "alias.cmo";
    std::filesystem::create_symlink("../previous.cmo", alias);
    CHECK(SaveDocumentAs(document, alias.u8string(), options, report));
    CHECK(report.saved && document.Dirty() && std::filesystem::is_symlink(alias));
    CHECK(std::filesystem::equivalent(alias, manager.target));
    CHECK(manager.written.parent_path() == folder.directory && !std::filesystem::exists(manager.written));
    CHECK(std::filesystem::status(manager.target).permissions() == permissions);
    const auto dangling = links / "dangling.cmo";
    std::filesystem::create_symlink("../referent.cmo", dangling);
    options.allowOverwrite = false;
    CHECK(SaveDocumentAs(document, dangling.u8string(), options, report));
    CHECK(report.saved && document.Dirty() && std::filesystem::is_symlink(dangling));
    CHECK(std::filesystem::equivalent(dangling, folder.directory / "referent.cmo"));
#endif

    options.allowOverwrite = true;
    options.markSaved = true;
    options.writeMode = 0;
    CHECK(SaveDocumentAs(document, manager.target.u8string(), options, report));
    CHECK(report.saved && report.errors.empty() && !document.Dirty());
    CHECK(!std::filesystem::exists(manager.written));
    CHECK(SavedFileBytes(manager.target) != original);
#ifdef _WIN32
    CHECK(SavedFileSecurity(manager.target) == security);
#else
    CHECK(std::filesystem::status(manager.target).permissions() == permissions);
#endif
    std::string error;
    CHECK(document.Open(manager.target.u8string(), &error));
    scene = document.Context()->GetCurrentLevel()->GetLevelScene();
    CHECK(ReadSceneEnvironment(scene).fogEnd == changed.fogEnd && !document.Dirty());
    const auto savedBytes = SavedFileBytes(manager.target);
    CHECK(!SaveDocumentAs(document, manager.target.u8string(), options, report));
    CHECK(!report.saved && !document.Dirty() && SavedFileBytes(manager.target) == savedBytes);
    manager.target = folder.directory / "clean-failure.cmo";
    manager.mode = SaveFailureManager::VerificationFailure;
    CHECK(!SaveDocumentAs(document, manager.target.u8string(), options, report));
    CHECK(!report.saved && !document.Dirty() && !std::filesystem::exists(manager.target));
    CHECK(!std::filesystem::exists(manager.written));
    manager.Reset();
}

} // namespace vdevtest
