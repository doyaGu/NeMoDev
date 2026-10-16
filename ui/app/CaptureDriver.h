#pragma once

// The command line (Main.cpp): the file to open, the runtime, and the
// capture mode's options for headless UI checks: the frames to render, the
// startup actions (StartupOptions, run by EditorStartup.cpp) and the input
// events injected into ImGui. None of them is used in an ordinary session.

#include <string>
#include <vector>

#include "ui/app/EditorApp.h"

namespace vdev {

struct CommandLine {
    std::wstring fileToOpen, capturePath;
    int captureFrames = 20;
    // --drag x0 y0 x1 y1 (capture mode): a left-button drag in client pixels,
    // injected as ImGui mouse events from frame DRAG_START on (UI checks).
    bool drag = false;
    float dragFrom[2] = {0, 0}, dragTo[2] = {0, 0};
    static constexpr int DRAG_START = 30, DRAG_STEPS = 10;
    // --double-click x y (capture mode): two left clicks at a client point
    // from frame DRAG_START on.
    bool doubleClick = false;
    float clickAt[2] = {0, 0};
    // --right-click x y (capture mode): a right click at a client point from
    // frame DRAG_START on (the 3D Layout's shortcut menu).
    bool rightClick = false;
    float rightAt[2] = {0, 0};
    // --then-click x y (capture mode): a left click at a client point 20
    // frames after DRAG_START (an item of the menu a right click opened).
    bool thenClick = false;
    float thenAt[2] = {0, 0};
    // --wheel x y notches [ctrl|shift] (capture mode): mouse wheel notches at
    // a client point, one per frame from frame DRAG_START on.
    bool wheel = false;
    float wheelAt[2] = {0, 0};
    int wheelNotches = 0;
    std::wstring wheelModifier;
    // --keys "k1 k2 ..." (capture mode): key presses, one every 3 frames from
    // 30 frames after DRAG_START (after --then-click): a key name (down, up,
    // home, end, pageup, pagedown, space, enter, escape, delete, f1..f12) or a
    // character, with "shift+", "ctrl+", "alt+" before it.
    std::vector<std::wstring> keys;
    std::wstring badOption;
    vdev::StartupOptions startup;
#ifdef VIRTOOLSDEV_UI_TESTS
    // VirtoolsDevUiTests (tests/ui): --ui-tests FILTER, the tests to run (the
    // test engine's filter: "all", "app", "schematic,-move"); --ui-results
    // FILE, a JUnit XML of the results; --ui-show, the window shown and the
    // tests at a human's pace. Always isolated (StartupOptions::isolated).
    std::string uiTests;
    std::wstring uiResults;
    bool uiShow = false;
    bool UiTests() const { return !uiTests.empty(); }
#else
    bool UiTests() const { return false; }
#endif

    // The arguments after the program's name (--runtime is ResolveRuntime's).
    void Parse(const std::vector<std::wstring> &args);
    bool Capture() const { return !capturePath.empty(); }
    // No window shown, no message box: the capture mode and the UI tests (but --ui-show).
    bool Headless() const {
#ifdef VIRTOOLSDEV_UI_TESTS
        if (UiTests()) return !uiShow;
#endif
        return Capture();
    }
    // The capture mode's input events of frame frameCount, before ImGui::NewFrame.
    void FeedInput(int frameCount, EditorApp &app) const;
};

} // namespace vdev
