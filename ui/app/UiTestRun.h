#pragma once
#ifndef VIRTOOLSDEV_UI_TESTS
#error ImGui Test Engine integration must only be compiled in the UI test executable.
#endif

// VirtoolsDevUiTests (VIRTOOLSDEV_UI_TESTS): Dear ImGui Test Engine driving
// the editor through its UI. Main.cpp makes the run after the editor
// starts; the engine and the tests are tests/ui's (UiTestRun.cpp).

#include <string>

namespace vdev {

class EditorApp;

class UiTestRun {
public:
    // Registers the tests and queues those of filter (the engine's filter
    // syntax); results: a JUnit XML of the results (empty: none); show: the
    // tests at a human's pace.
    UiTestRun(EditorApp &app, const std::string &filter, const std::wstring &results, bool show);
    ~UiTestRun();
    UiTestRun(const UiTestRun &) = delete;
    UiTestRun &operator=(const UiTestRun &) = delete;

    // Around the frame's Present.
    void PreSwap();
    void PostSwap();
    // The tests want the frames as fast as they come (no vsync).
    bool MaxSpeed() const;
    // Every queued test has run.
    bool Finished() const;
    // 0 every test passed, 1 one failed, 2 the filter named none.
    int ExitCode() const;
    // Before the editor shuts down: the test coroutine ends.
    void Stop();
    // After ImGui::DestroyContext (the engine's settings hook goes with it).
    void Destroy();

private:
    struct Engine;
    Engine *m_Engine = nullptr;
};

} // namespace vdev
