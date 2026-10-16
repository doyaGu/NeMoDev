#include "core/runtime/CkRuntimeHost.h"

#if defined(_WIN32) && defined(_MSC_VER)
#include <windows.h>
#endif
#include <exception>
#include <filesystem>
#include <fstream>
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>

#include "core/runtime/VirtoolsRuntimeLayout.h"
#include "core/runtime/SdkCompat.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

namespace {

#if defined(_WIN32) && defined(_MSC_VER)
// What the exception was and where: its code, the module and offset of the
// instruction, the address read or written (an access violation).
struct GuardedFault {
    DWORD code = 0;
    void *at = nullptr;
    ULONG_PTR access[2] = {};
    bool hasAccess = false;
};

int RecordFault(EXCEPTION_POINTERS *pointers, GuardedFault *fault) {
    if (pointers && pointers->ExceptionRecord) {
        const EXCEPTION_RECORD &record = *pointers->ExceptionRecord;
        fault->code = record.ExceptionCode;
        fault->at = record.ExceptionAddress;
        if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
            fault->access[0] = record.ExceptionInformation[0];
            fault->access[1] = record.ExceptionInformation[1];
            fault->hasAccess = true;
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// A fault in the runtime's x87 code (CK2 and CK2_3D 2.1 are x87 code) leaves
// the values it had loaded on the FPU's register stack: the __except block
// does not pop them. A few such faults fill the stack, and every x87 load
// after them overflows to NaN (every float CK returns in ST0, the sizes and
// indices it computes from floats: the 2.1 heap corruption after
// CKRenderContext::Pick had faulted). The stack is emptied, the control word
// kept as it was before the call.
int InvokeGuarded(const std::function<void()> *fn, GuardedFault *fault) {
#if defined(_M_IX86)
    unsigned short control = 0;
    __asm fnstcw control
#endif
    __try {
        (*fn)();
        return 1;
    } __except (RecordFault(GetExceptionInformation(), fault)) {
#if defined(_M_IX86)
        __asm {
            fninit
            fldcw control
        }
#endif
        return 0;
    }
}

std::string DescribeFault(const GuardedFault &fault) {
    char text[MAX_PATH + 64];
    HMODULE module = nullptr;
    char path[MAX_PATH] = {};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(fault.at), &module) &&
        GetModuleFileNameA(module, path, MAX_PATH)) {
        const char *name = std::strrchr(path, '\\');
        const auto offset = static_cast<unsigned long>(static_cast<const char *>(fault.at) -
                                                       reinterpret_cast<const char *>(module));
        std::snprintf(text, sizeof(text), " (0x%08lX at %s+0x%lX", fault.code, name ? name + 1 : path, offset);
    } else {
        std::snprintf(text, sizeof(text), " (0x%08lX at %p", fault.code, fault.at);
    }
    std::string described = text;
    if (fault.hasAccess) {
        const char *access = fault.access[0] == 1 ? "writing" : fault.access[0] == 8 ? "executing" : "reading";
        std::snprintf(text, sizeof(text), ", %s %p", access, reinterpret_cast<void *>(fault.access[1]));
        described += text;
    }
    return described + ")";
}

#endif

int CountPlugins(CKPluginManager *plugins, int category) {
    return plugins ? plugins->GetPluginCount(category) : 0;
}

} // namespace

bool GuardedCall(const std::function<void()> &fn, const CkRuntimeHost *host, const char *what) {
    #if defined(_WIN32) && defined(_MSC_VER)
    GuardedFault fault;
    if (InvokeGuarded(&fn, &fault)) return true;
    if (host) host->Log(LogLevel::Error, std::string("Runtime exception in ") + what + DescribeFault(fault));
    return false;
#else
    try { fn(); return true; }
    catch (const std::exception &error) {
        if (host) host->Log(LogLevel::Error, std::string("Runtime exception in ") + what + ": " + error.what());
    }
    catch (...) { if (host) host->Log(LogLevel::Error, std::string("Runtime exception in ") + what); }
    return false;
#endif
}

CkRuntimeHost::~CkRuntimeHost() {
    Stop();
}

bool RuntimeVersion(const std::string &runtimeDirectory, int &major, int &minor) {
    std::ifstream file(std::filesystem::u8path(runtimeDirectory) / "BallancedRuntime.txt");
    std::string signature;
    CKDWORD ckVersion = 0, devVersion = 0;
    if (!(file >> signature >> std::hex >> ckVersion >> devVersion) || signature != "BallancedSDK" ||
        ckVersion != CKGetVersion() || devVersion != DEVVERSION) return false;
    major = static_cast<int>((devVersion >> 24) & 0xFF);
    minor = static_cast<int>((devVersion >> 16) & 0xFF);
    return true;
}

bool CkRuntimeHost::SetTextEncoding(const std::string &encoding) {
    const std::string previous = VxGetTextEncoding();
    if (!VxSetTextEncoding(encoding.c_str())) return false;
    if (!m_StartedUp && m_PreviousTextEncoding.empty()) VxSetTextEncoding(previous.c_str());
    m_TextEncoding = encoding;
    return true;
}

void CkRuntimeHost::Log(LogLevel level, const std::string &message) const {
    if (m_Sink) m_Sink(level, message);
}

bool CkRuntimeHost::Start(const std::string &runtimeDirectory, LogSink sink, std::string *error, SDL_Window *mainWindow) {
    Stop();
    m_Sink = std::move(sink);
    m_RuntimeDirectory = runtimeDirectory;
    auto fail = [&](const std::string &message) {
        Log(LogLevel::Error, message);
        if (error) *error = message;
        Stop();
        return false;
    };

    int runtimeMajor = 0, runtimeMinor = 0;
    if (!RuntimeVersion(runtimeDirectory, runtimeMajor, runtimeMinor))
        return fail("Only the Ballanced runtime staged by this source build is supported: " + runtimeDirectory);

    m_PreviousTextEncoding = VxGetTextEncoding();
    if (!SetTextEncoding(m_TextEncoding)) return fail("The CK text encoding is unavailable");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return fail(std::string("SDL video initialization failed: ") + SDL_GetError());
    m_OwnsVideo = true;
    if (!mainWindow) {
        m_FallbackWindow = SDL_CreateWindow("Virtools runtime", 64, 64, SDL_WINDOW_HIDDEN);
        if (!m_FallbackWindow) return fail(std::string("Runtime window creation failed: ") + SDL_GetError());
        mainWindow = m_FallbackWindow;
    }
    if (CKStartUp() != CK_OK) return fail("CKStartUp failed");
    m_StartedUp = true;

    // Same directory set as the SDK Standalone Player, under the plugin root
    // (beside CK2.dll, or above it: Ballance's Bin). Plugin discovery must
    // precede CKCreateContext: managers are created with the context and
    // CK2_3D registers object parameter types at that point. The DLLs of
    // Ballance's mod loader are left out (VirtoolsIsIgnoredPlugin).
    CKPluginManager *plugins = CKGetPluginManager();
    if (!plugins) return fail("CKGetPluginManager returned null");
    const std::string root = VirtoolsPluginRoot(runtimeDirectory);
    for (const char *folder : VIRTOOLS_PLUGIN_FOLDERS) {
        std::vector<std::string> skipped;
        const int found = VirtoolsParsePlugins(plugins, (std::filesystem::u8path(root) / folder).u8string(), &skipped);
        Log(LogLevel::Info, std::string("Parsed ") + folder + ": " + std::to_string(found) + " plugin DLLs");
        for (const std::string &dll : skipped) Log(LogLevel::Info, "Not loaded (Ballance's mod loader): " + dll);
    }
    m_Counts.behaviorDlls = CountPlugins(plugins, CKPLUGIN_BEHAVIOR_DLL);
    m_Counts.managerDlls = CountPlugins(plugins, CKPLUGIN_MANAGER_DLL);
    m_Counts.renderEngines = CountPlugins(plugins, CKPLUGIN_RENDERENGINE_DLL);
    m_Counts.modelReaders = CountPlugins(plugins, CKPLUGIN_MODEL_READER);
    m_Counts.behaviorPrototypes = CKGetPrototypeDeclarationCount();
    if (m_Counts.behaviorDlls == 0 || m_Counts.managerDlls == 0 || m_Counts.modelReaders == 0)
        return fail("Required Virtools plugins were not found under " + root);

    // The render engine chosen (its index among the render engine plugins).
    int engine = 0;
    for (int i = 0; !m_RenderEngine.empty() && i < plugins->GetPluginCount(CKPLUGIN_RENDERENGINE_DLL); ++i) {
        CKPluginEntry *entry = plugins->GetPluginInfo(CKPLUGIN_RENDERENGINE_DLL, i);
        CKPluginDll *dll = entry ? plugins->GetPluginDllInfo(entry->m_PluginDllIndex) : nullptr;
        if (!dll || !dll->m_DllFileName.CStr()) continue;
        std::string name = dll->m_DllFileName.CStr();
        const size_t slash = name.find_last_of("\\/");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        const size_t dot = name.find_last_of('.');
        if (dot != std::string::npos) name = name.substr(0, dot);
        if (SDL_strcasecmp(name.c_str(), m_RenderEngine.c_str()) == 0) engine = i;
    }
    const CKERROR created = CreateCKContext(&m_Context, mainWindow, engine);
    if (created != CK_OK || !m_Context) {
        m_Context = nullptr;
        return fail("CKCreateContext failed: " + std::to_string(created));
    }
    // Interface mode keeps CKBehavior InterfaceChunks when files are loaded.
    m_Context->SetInterfaceMode(TRUE, &CkRuntimeHost::UiCallback, this);
    Log(LogLevel::Info, "Runtime started: " + std::to_string(m_Counts.behaviorDlls) + " Building Block DLLs, " +
                            std::to_string(m_Counts.behaviorPrototypes) + " prototypes, " +
                            std::to_string(m_Counts.managerDlls) + " managers, " +
                            std::to_string(m_Counts.modelReaders) + " file readers");
    // The runtime's CK version (a build date) and the headers built with
    // (CKVERSION, DEVVERSION): another runtime than the SDK's is noted.
    char version[96];
    std::snprintf(version, sizeof(version), "CK version 0x%08X (built with 0x%08X, Dev %X.%X)",
                  static_cast<unsigned>(CKGetVersion()), static_cast<unsigned>(CKVERSION), DEVVERSION >> 24,
                  (DEVVERSION >> 16) & 0xFF);
    Log(CKGetVersion() == CKVERSION ? LogLevel::Info : LogLevel::Warning,
        std::string(version) + (CKGetVersion() == CKVERSION ? "" : ": the runtime is not the SDK's"));
    return true;
}

void CkRuntimeHost::Stop() {
    if (m_Context) {
        m_Context->SetInterfaceMode(FALSE, nullptr, nullptr);
        CKCloseContext(m_Context);
        m_Context = nullptr;
        ++m_Generation;
    }
    if (m_StartedUp) {
        CKShutdown();
        m_StartedUp = false;
    }
    if (m_FallbackWindow) {
        SDL_DestroyWindow(m_FallbackWindow);
        m_FallbackWindow = nullptr;
    }
    if (m_OwnsVideo) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        m_OwnsVideo = false;
    }
    if (!m_PreviousTextEncoding.empty()) {
        VxSetTextEncoding(m_PreviousTextEncoding.c_str());
        m_PreviousTextEncoding.clear();
    }
}

void CkRuntimeHost::ClearAll() {
    if (!m_Context) return;
    ++m_Generation;
    std::vector<CKBaseManager *> active;
    for (int i = 0; i < m_Context->GetManagerCount(); ++i)
        if (CKBaseManager *manager = m_Context->GetManager(i)) active.push_back(manager);
    // The objects' destructors and the managers' callbacks run here.
    GuardedCall([&] { m_Context->ClearAll(); }, this, "CKContext::ClearAll");
    for (CKBaseManager *manager : active)
        if (m_Context->GetManagerByGuid(manager->GetGuid()) != manager) {
            CKBaseManager *displaced = m_Context->GetManagerByGuid(manager->GetGuid());
            m_Context->ActivateManager(manager, TRUE);
            Log(LogLevel::Info, std::string("Manager ") + (manager->GetName() ? manager->GetName() : "?") +
                                    " kept active (another with its GUID: " +
                                    (displaced && displaced->GetName() ? displaced->GetName() : "?") + ")");
        }
}

CKERROR CkRuntimeHost::UiCallback(CKUICallbackStruct &param, void *data) {
    auto *host = static_cast<CkRuntimeHost *>(data);
    if (!host) return CK_OK;
    switch (param.Reason) {
    case CKUIM_OUTTOCONSOLE:
    case CKUIM_OUTTOINFOBAR:
        if (param.ConsoleString) host->Log(LogLevel::Info, AnsiToUtf8(param.ConsoleString));
        break;
    default:
        // Dev-only requests (setup dialogs, object choosers, VSL) have no
        // equivalent here; CK treats CK_OK as "handled with no change".
        break;
    }
    return CK_OK;
}

} // namespace vdev
