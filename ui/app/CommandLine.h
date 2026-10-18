#pragma once

// The composition path and runtime override. The regression executable also
// accepts capture, startup actions and injected input for UI checks.

#include <string>
#include <vector>

#ifdef VIRTOOLSDEV_UI_TESTS
#include "ui/app/EditorApp.h"
#endif

namespace vdev {

struct CommandLine {
    std::wstring fileToOpen, badOption;
    void Parse(const std::vector<std::wstring> &args);
#ifdef VIRTOOLSDEV_UI_TESTS
    std::wstring capturePath;
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
    vdev::StartupOptions startup;
    // VirtoolsDevUiTests (tests/ui): --ui-tests FILTER, the tests to run (the
    // test engine's filter: "all", "app", "schematic,-move"); --ui-results
    // FILE, a JUnit XML of the results; --ui-show, the window shown and the
    // tests at a human's pace. Always isolated (StartupOptions::isolated).
    std::string uiTests;
    std::wstring uiResults;
    bool uiShow = false;
    bool UiTests() const { return !uiTests.empty(); }

    // The arguments after the program's name (--runtime is ResolveRuntime's).
    bool Capture() const { return !capturePath.empty(); }
    // No window shown, no message box: the capture mode and the UI tests (but --ui-show).
    bool Headless() const {
        if (UiTests()) return !uiShow;
        return Capture();
    }
    // The capture mode's input events of frame frameCount, before ImGui::NewFrame.
    void FeedInput(int frameCount, EditorApp &app) const;
#endif
};

} // namespace vdev
