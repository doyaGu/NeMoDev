#pragma once

#include <string>
#include <utility>
#include <vector>

namespace vdev {
#ifdef VIRTOOLSDEV_UI_TESTS
// Command-line display options for the regression executable.
struct StartupOptions {
    bool openScripts = false;  // open every shown Script (an undoable display edit)
    bool expandGraphs = false; // expand every behavior graph of the shown Scripts
    bool showAllScripts = true; // Dev: every Script in one Schematic (--owner-scripts: only the owner's)
    bool fit = false;
    std::string insertBlock; // --insert-block <name>: insert into the first shown Script, link from Start
    std::string showScript;  // --show-script <name>: scroll the Schematic to this Script (UTF-8)
    std::string focusWindow; // --focus-window <name>[|<name>...]: bring these panels to front (capture checks)
    bool messages = false;   // --messages: open the Message Explorer
    int playFrames = 0;      // --play N: show the Script Debugger and run N traced frames
    std::vector<std::string> setups; // --setup NAME: open the Setup of the object NAME (repeatable)
    bool expandLevel = false; // --expand-level: open every Level Manager node
    bool newComposition = false; // --new: File > New Composition first
    std::vector<std::pair<std::string, bool>> merges; // --merge FILE / --merge-scene FILE (as a new scene)
    // --import FILE, --import-character / --import-place / --import-scene FILE
    std::vector<std::pair<std::string, int>> imports; // Document::ImportMode
    std::vector<std::string> resources; // --resource FILE.rsc: open a Data Resource
    // --resource-folder PATH: the folder of the last one shown (its path ends with PATH)
    std::string resourceFolder;
    // --tool NAME: open a window (prefs[:page], plugins, paths, unused, versiontest,
    // profiler, watch, about, linkprops) for capture checks
    std::vector<std::string> tools;
    // --layout-tool NAME: the 3D Layout's tool (select, translate, rotate, scale,
    // orbit, orbitcamera, dolly, zoom, pan, roll, fov); --layout-view NAME: its camera
    std::string layoutTool;
    std::string layoutView;
    // --layout-select NAME (repeatable): objects selected in the 3D Layout;
    // --layout-group NAME: that name entered in its Selection Groups combo;
    // --layout-command ID (repeatable): a 3D Layout command by Dev's id (41
    // the 3D Layout Explorer, 56 the Snapshot Buffer, ...).
    std::vector<std::string> layoutSelect;
    std::string layoutGroup;
    std::vector<int> layoutCommands;
    bool layoutDebug = false; // --layout-debug: the 3D Layout's On-screen Debug Data
    // --drop-block NAME X Y: a Building Block dropped on the 3D Layout at a client point.
    std::string dropBlock;
    float dropX = 0.0f, dropY = 0.0f;
    std::vector<std::string> creates; // --create camera|light|frame|curve|grid|2d|material|texture|portal
    bool menuAt = false;     // --menu-at <x> <y>: context menu at a canvas point
    float menuX = 0.0f, menuY = 0.0f;
    std::vector<std::string> closeScripts;                 // --close-script <name> (repeatable)
    // --edit-parameters NAME: Dev's Edit Parameters of the first Building
    // Block or parameter operation named NAME (ANSI), once the window is up.
    std::string editParameters;
    // --edit-value TYPE: Dev's Edit Parameter dialog (value only) on a new
    // parameter of the type named TYPE (ANSI): a type's UI alone.
    std::string editValue;
    std::vector<std::pair<std::string, float>> zoomScripts; // --zoom-script <name> <scale>
    // --isolated (and the UI tests): Dev's default preferences, none read
    // from or saved to the preferences file or Dev's registry, no window
    // layout file (Main.cpp): the same start on every machine.
    bool isolated = false;
};
#endif

} // namespace vdev
