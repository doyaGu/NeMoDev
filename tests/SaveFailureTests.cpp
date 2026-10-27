#include "VirtoolsDevTestSupport.h"

#include <SDL3/SDL_timer.h>
#include <memory>
#include <stdexcept>

namespace vdevtest {

class SaveFailureManager : public CKBaseManager {
public:
    enum Mode { None, WriteFailure, VerificationFailure, ReplacementFailure, DestinationAppeared } mode = None;
    std::filesystem::path target, written;
    std::string error;
    int saves = 0;
    std::function<void()> afterSave;
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
                                : "Cannot replace the destination composition";
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
                              [](const auto &note) { return note.find("Cannot remove temporary composition") == 0; }));
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
