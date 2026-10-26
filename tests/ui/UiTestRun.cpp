// VirtoolsDevUiTests' run (ui/app/UiTestRun.h): the engine, its tests, the
// results. The engine's log goes to stdout; a failed test logs its check.

#include "ui/app/UiTestRun.h"

#include <cstdio>
#include <filesystem>

#include "imgui_te_exporters.h"

#include "core/runtime/TextEncoding.h"
#include "tests/ui/UiTests.h"
#include "ui/controls/DevBitmaps.h"

namespace vdevuitest {

namespace {
vdev::EditorApp *g_App = nullptr;
std::function<void()> g_Work; // OnMainThread's, until MainThreadGui runs it
}

vdev::EditorApp &App() { return *g_App; }

void OnMainThread(ImGuiTestContext *ctx, const std::function<void()> &work) {
    g_Work = work;
    // The next frame's GuiFunc runs it; one more if the test has no MainThreadGui.
    const auto deadline = SDL_GetTicks() + 10000;
    while (g_Work && SDL_GetTicks() < deadline) ctx->Yield();
    IM_CHECK_NO_RET(!g_Work);
    g_Work = nullptr;
}

void MainThreadGui(ImGuiTestContext *) {
    if (g_App->State().runtimeOperation.Busy()) return;
    if (!g_Work) return;
    g_Work();
    g_Work = nullptr;
}

} // namespace vdevuitest

namespace vdev {

struct UiTestRun::Engine {
    ImGuiTestEngine *engine = nullptr;
    std::string results; // UTF-8, kept while the engine may export
    bool queued = false;
    bool stopped = false;
};

UiTestRun::UiTestRun(EditorApp &app, const std::string &filter, const std::wstring &results, bool show)
    : m_Engine(new Engine) {
    vdevuitest::g_App = &app;
    ImGuiTestEngine *engine = ImGuiTestEngine_CreateContext();
    m_Engine->engine = engine;
    ImGuiTestEngineIO &io = ImGuiTestEngine_GetIO(engine);
    io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
    io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    io.ConfigLogToTTY = true;
    io.ConfigRunSpeed = show ? ImGuiTestRunSpeed_Normal : ImGuiTestRunSpeed_Fast;
    io.ConfigNoThrottle = !show;
    io.ConfigSavedSettings = false; // no .ini (isolated)
    io.ConfigCaptureEnabled = false;
    if (!results.empty()) {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::u8path(WideToUtf8(results)).parent_path(), error);
        m_Engine->results = WideToUtf8(results);
        io.ExportResultsFilename = m_Engine->results.c_str();
        io.ExportResultsFormat = ImGuiTestEngineExportFormat_JUnitXml;
    }
    ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
    vdevuitest::RegisterAppTests(engine);
    vdevuitest::RegisterFileMenuTests(engine);
    vdevuitest::RegisterSchematicTests(engine);
    vdevuitest::RegisterLayoutTests(engine);
    vdevuitest::RegisterLevelTests(engine);
    vdevuitest::RegisterSetupTests(engine);
    vdevuitest::RegisterHierarchyTests(engine);
    vdevuitest::RegisterBlocksTests(engine);
    vdevuitest::RegisterAttributesTests(engine);
    vdevuitest::RegisterCreateTests(engine);
    vdevuitest::RegisterSoundTests(engine);
    vdevuitest::RegisterCharacterTests(engine);
    vdevuitest::RegisterDebuggerTests(engine);
    vdevuitest::RegisterToolsTests(engine);
    vdevuitest::RegisterPrintTests(engine);
    vdevuitest::RegisterPlayerTests(engine);
    vdevuitest::RegisterHelpTests(engine);
    vdevuitest::RegisterHtmlTests(engine);
    vdevuitest::RegisterDesktopTests(engine);
    ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Tests, filter.c_str(), ImGuiTestRunFlags_RunFromCommandLine);
    m_Engine->queued = !ImGuiTestEngine_IsTestQueueEmpty(engine);
    if (!m_Engine->queued) std::fprintf(stderr, "no UI test matches '%s'\n", filter.c_str());
}

UiTestRun::~UiTestRun() {
    Stop();
    Destroy();
    delete m_Engine;
}

void UiTestRun::PreSwap() {
    if (m_Engine->engine) ImGuiTestEngine_PreSwap(m_Engine->engine);
}

void UiTestRun::PostSwap() {
    if (m_Engine->engine) ImGuiTestEngine_PostSwap(m_Engine->engine);
}

bool UiTestRun::MaxSpeed() const {
    return m_Engine->engine && ImGuiTestEngine_GetIO(m_Engine->engine).IsRequestingMaxAppSpeed;
}

bool UiTestRun::Finished() const {
    return !m_Engine->engine || ImGuiTestEngine_IsTestQueueEmpty(m_Engine->engine);
}

int UiTestRun::ExitCode() const {
    if (!m_Engine->queued) return 2;
    ImGuiTestEngineResultSummary summary;
    ImGuiTestEngine_GetResultSummary(m_Engine->engine, &summary);
    std::printf("UI tests: %d of %d passed\n", summary.CountSuccess, summary.CountTested);
    // A bitmap or icon drawn that the program does not have: missing from
    // resources/images.txt.
    const std::set<std::string> &missing = vdev::MissingResources();
    for (const std::string &name : missing) std::printf("UI tests: no Dev resource %s in the program\n", name.c_str());
    return summary.CountTested > 0 && summary.CountSuccess == summary.CountTested && summary.CountInQueue == 0 &&
                   missing.empty()
               ? 0
               : 1;
}

void UiTestRun::Stop() {
    if (!m_Engine->engine || m_Engine->stopped) return;
    ImGuiTestEngine_Stop(m_Engine->engine); // exports the results
    m_Engine->stopped = true;
}

void UiTestRun::Destroy() {
    if (!m_Engine->engine) return;
    ImGuiTestEngine_DestroyContext(m_Engine->engine);
    m_Engine->engine = nullptr;
    vdevuitest::g_App = nullptr;
}

} // namespace vdev
