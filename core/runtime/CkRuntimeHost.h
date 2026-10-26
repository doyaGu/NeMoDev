#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "CKAll.h"

struct SDL_Window;

namespace vdev {

// The runtime's Virtools compatibility version, recorded from the source SDK
// alongside its libraries. False for absent or incompatible runtime metadata.
bool RuntimeVersion(const std::string &runtimeDirectory, int &major, int &minor);

enum class LogLevel { Info, Warning, Error };

using LogSink = std::function<void(LogLevel, const std::string &)>;

struct RuntimePluginCounts {
    int behaviorDlls = 0;
    int managerDlls = 0;
    int renderEngines = 0;
    int modelReaders = 0;
    int behaviorPrototypes = 0;
};

// Owns the CK runtime for one editor process. The Schematic edits real CK
// objects, so Building Block and manager plugins must be discovered before
// the CKContext is created (managers are instantiated with the context).
class CkRuntimeHost {
public:
    CkRuntimeHost() = default;
    ~CkRuntimeHost();
    CkRuntimeHost(const CkRuntimeHost &) = delete;
    CkRuntimeHost &operator=(const CkRuntimeHost &) = delete;

    // Runtime Bin directory from this source build. Plugin category folders
    // are beside Bin. The main SDL window is borrowed until Stop(); the host
    // creates and owns a fallback SDL window when none is supplied.
    bool Start(const std::string &runtimeDirectory, LogSink sink, std::string *error, SDL_Window *mainWindow = nullptr);
    void Stop();
    // The render engine the context is created with (Dev's "CK2 Settings\Render
    // Engine": a render engine plugin's dll name, as "ck2_3d"); unknown or
    // empty: the first one. Set before Start.
    void SetRenderEngine(const std::string &engine) { m_RenderEngine = engine; }

    CKContext *Context() const { return m_Context; }
    // CKContext::ClearAll, the managers left active as they were. CK2's
    // ClearAll activates every inactive manager (CKContext::ActivateManager),
    // and a manager CKContext::RegisterNewManager refused because another
    // has its GUID (CKERR_ALREADYPRESENT: kept inactive) then takes that
    // one's place: TT_ParticleSystems_RT.dll's "Particle Manager" has the
    // GUID of ParticleSystems.dll's, whose Building Blocks would then write
    // their emitters into the other's object and delete them twice (the heap
    // corruption of a composition with particle systems loaded a second
    // time). The managers displaced are activated again.
    void ClearAll();
    // Counts ClearAll (and Stop): CK gives the ids of the objects it destroyed
    // to the objects made next, so an id kept from an earlier generation names
    // another object or none.
    std::uint64_t Generation() const { return m_Generation; }
    const RuntimePluginCounts &Counts() const { return m_Counts; }
    const std::string &RuntimeDirectory() const { return m_RuntimeDirectory; }
    void Log(LogLevel level, const std::string &message) const;

private:
    static CKERROR UiCallback(CKUICallbackStruct &param, void *data);

    CKContext *m_Context = nullptr;
    bool m_StartedUp = false;
    std::uint64_t m_Generation = 0;
    RuntimePluginCounts m_Counts;
    std::string m_RuntimeDirectory;
    std::string m_RenderEngine;
    LogSink m_Sink;
    SDL_Window *m_FallbackWindow = nullptr;
    bool m_OwnsVideo = false;
#if defined(__linux__)
    std::optional<std::string> m_PreviousDataDirectories;
    std::string m_FontDataDirectories;
#endif
};

// Runs fn, converting a structured exception raised inside Building Block or
// runtime code into false. The SEH frame lives in a separate non-inlined
// function so C++ objects are not unwound across it.
bool GuardedCall(const std::function<void()> &fn, const CkRuntimeHost *host, const char *what);

} // namespace vdev
