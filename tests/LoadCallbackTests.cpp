#include "VirtoolsDevTestSupport.h"

#include <stdexcept>
#include <SDL3/SDL_timer.h>

#include "core/runtime/SdkCompat.h"

namespace vdevtest {

namespace {

CK_LOADMODE OriginalCallback(CK_CLASSID, CKSTRING, CKSTRING, CKObject **, void *data) {
    ++*static_cast<int *>(data);
    return CKLOAD_OK;
}

class LoadCallbackManager : public CKBaseManager {
public:
    enum Fault { None, Cpp, Structured } fault = None;
    bool sawTemporaryCallback = false;
    std::function<void()> beforeLoad;

    explicit LoadCallbackManager(CKContext *context)
        : CKBaseManager(context, CKGUID(0x24f17a66, 0x52c34981), "Load callback regression") {}

    CKDWORD GetValidFunctionsMask() override { return CKMANAGER_FUNC_PreLoad; }
    CKERROR PreLoad() override {
        if (beforeLoad) beforeLoad();
        const Fault next = fault;
        fault = None;
        if (next != None) {
            sawTemporaryCallback = m_Context->m_UserLoadCallBack && m_Context->m_UserLoadCallBackArgs;
#if defined(_WIN32) && defined(_MSC_VER)
            if (next == Structured) RaiseException(0xE0421066, 0, 0, nullptr);
#endif
            throw std::runtime_error("Injected load callback failure");
        }
        return CK_OK;
    }
};

} // namespace

void LoadCallbacks(CkRuntimeHost &host) {
    namespace fs = std::filesystem;
    Document document(host);
    CHECK(document.New(nullptr));
    CKContext *context = document.Context();
    const fs::path folder = fs::current_path() / ("load-callbacks-" + std::to_string(SDL_GetTicksNS()));
    CHECK(fs::create_directory(folder));
    struct Cleanup {
        fs::path folder;
        ~Cleanup() { std::error_code error; fs::remove_all(folder, error); }
    } cleanup{folder};
    const fs::path path = folder / "objects.nmo";
    const EditResult object = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(object.ok);
    CKObject *original = context->GetObject(object.created);
    original->SetName((CKSTRING) "Load callback object");
    std::string error;
    CHECK(SaveObjectsAs(document, {object.created}, path.u8string(), &error));
    auto *manager = new LoadCallbackManager(context);
    CHECK(context->RegisterNewManager(manager) == CK_OK);

    CKFile file(context);
    CKFile nestedFile(context);
    CKObjectArray objects;
    const std::vector<DuplicateName> choices;
    context->SetUserLoadCallback(nullptr, nullptr);
    manager->fault = LoadCallbackManager::Cpp;
    CKERROR loaded = CK_OK;
    const bool completed = GuardedCall([&] {
        loaded = LoadFileChoosingDuplicates(host, &file, path.u8string(), &objects, CK_LOAD_DEFAULT, &choices);
    }, &host, "Load callback regression");
    CHECK(manager->sawTemporaryCallback);
    CHECK(completed && loaded == CKERR_INVALIDFILE);
    // Assert while file is still alive: CKFile's destructor clears the
    // callback too, which would hide a dangling pointer left by this helper.
    CHECK(context->m_UserLoadCallBack == nullptr && context->m_UserLoadCallBackArgs == nullptr);

    int originalCalls = 0;
    context->SetUserLoadCallback(&OriginalCallback, &originalCalls);
    auto checkRestored = [&] {
        CHECK(context->m_UserLoadCallBack == &OriginalCallback);
        CHECK(context->m_UserLoadCallBackArgs == &originalCalls);
        const int before = originalCalls;
        char name[512] = {};
        CKObject *existing = nullptr;
        CHECK(context->LoadVerifyObjectUnicity(original->GetName(), original->GetClassID(), name, &existing,
                                               sizeof(name)) == CKLOAD_OK);
        CHECK(existing == original && originalCalls == before + 1);
    };

    // Early failures need the same restoration as an exception. No choices
    // also preserves the caller's callback, while leaving its behavior intact.
    const fs::path corrupt = folder / "corrupt.nmo";
    { std::ofstream stream(corrupt, std::ios::binary); stream << "Invalid NMO header"; CHECK(stream.good()); }
    for (const auto &invalid : {folder / "missing.nmo", corrupt})
        for (const auto *selection : {&choices, static_cast<const std::vector<DuplicateName> *>(nullptr)}) {
            CHECK(LoadFileChoosingDuplicates(host, &file, invalid.u8string(), &objects, CK_LOAD_DEFAULT,
                                             selection) == CKERR_INVALIDFILE);
            checkRestored();
        }

    std::vector<LoadCallbackManager::Fault> faults{LoadCallbackManager::Cpp};
#if defined(_WIN32) && defined(_MSC_VER)
    faults.push_back(LoadCallbackManager::Structured);
#endif
    for (const auto fault : faults) {
        manager->fault = fault;
        CHECK(LoadFileChoosingDuplicates(host, &file, path.u8string(), &objects, CK_LOAD_DEFAULT,
                                         &choices) == CKERR_INVALIDFILE);
        CHECK(manager->sawTemporaryCallback);
        checkRestored();
    }

    // Subsequent real loads still honor all three choices. CKFile clears its
    // callback even on success; the helper must put back both saved fields.
    for (const auto action : {DuplicateAction::UseCurrent, DuplicateAction::Rename, DuplicateAction::Ignore}) {
        const std::vector<DuplicateName> selection{{-1, original->GetClassID(), original->GetName(), action}};
        objects.Clear();
        CHECK(LoadFileChoosingDuplicates(host, &file, path.u8string(), &objects, CK_LOAD_DEFAULT, &selection) == CK_OK);
        CKObject *imported = nullptr;
        for (int i = 0; i < file.m_FileObjects.Size(); ++i)
            if (file.m_FileObjects[i].Object == object.created) imported = file.m_FileObjects[i].ObjPtr;
        CHECK(imported);
        if (action == DuplicateAction::UseCurrent) CHECK(imported == original);
        else {
            CHECK(imported != original);
            CHECK((std::string(imported->GetName()) != original->GetName()) == (action == DuplicateAction::Rename));
        }
        checkRestored();
    }

    // A load without supplied choices uses the original callback too.
    const int before = originalCalls;
    CHECK(LoadFileChoosingDuplicates(host, &file, path.u8string(), &objects, CK_LOAD_CHECKDUPLICATES, nullptr) == CK_OK);
    CHECK(originalCalls > before);
    checkRestored();

    // Re-enter while the outer load's local choices are installed. Both
    // inner success and inner failure must restore that exact outer pair.
    struct ResetHook {
        LoadCallbackManager &manager;
        ~ResetHook() { manager.beforeLoad = nullptr; }
    } resetHook{*manager};
    for (const auto fault : {LoadCallbackManager::None, LoadCallbackManager::Cpp}) {
        bool entered = false, restoredOuter = false;
        CKERROR inner = CKERR_INVALIDFILE;
        manager->beforeLoad = [&] {
            if (entered) return;
            entered = true;
            const auto outerCallback = context->m_UserLoadCallBack;
            void *outerData = context->m_UserLoadCallBackArgs;
            manager->fault = fault;
            inner = LoadFileChoosingDuplicates(host, &nestedFile, path.u8string(), nullptr, CK_LOAD_DEFAULT, &choices);
            restoredOuter = context->m_UserLoadCallBack == outerCallback && context->m_UserLoadCallBackArgs == outerData;
        };
        CHECK(LoadFileChoosingDuplicates(host, &file, path.u8string(), &objects, CK_LOAD_DEFAULT, &choices) == CK_OK);
        CHECK(entered && restoredOuter);
        CHECK(inner == (fault == LoadCallbackManager::None ? CK_OK : CKERR_INVALIDFILE));
        checkRestored();
        manager->beforeLoad = nullptr;
    }
    std::cout << "Load callbacks restored after success, failure, exceptions and nested loads\n";
}

} // namespace vdevtest
