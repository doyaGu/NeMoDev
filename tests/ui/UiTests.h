#pragma once

// The UI tests of VirtoolsDevUiTests: Dear ImGui Test Engine moves the mouse
// and presses the keys in the editor's frames, the tests check what the
// editor did (its state, its windows). A test's TestFunc runs on the
// engine's coroutine at the end of a frame (ImGui's PreEndFrame) while the
// editor's thread waits; the editor's commands run in its frames, as a
// user's do. What calls CK (picking, an object's state) runs on the editor's
// thread with OnMainThread (or Read).
//
// Categories, one file each (--ui-tests "^name$" runs one; without the
// anchors the filter matches a substring of a test's name too): "app", the
// editor on its new composition; "schematic" and "debugger", the Schematic
// and the Script Debugger on a sample with a Building Block shown;
// "layout", "level", "setup", "hierarchy", "blocks" and
// "attributes", the 3D Layout, the Level Manager, the Setups, the Hierarchy
// Manager, the Building Blocks and the Attribute Manager on a sample with 3D
// entities and a Script; "create", objects made in the editor (3D Frame,
// Grid, Array, Group) and their Setups, and "tools", the Editors and Options
// menus' windows, on the same sample; "sound" and
// "character", the Sound, Array and Character Setups on samples that have
// them (the sample opened from the command line). "player" uses the unchanged
// Add Control Point sample and a real desktop window manager for fullscreen,
// focus, Script input and snapshots (--ui-show). The helpers they share:
// UiTests.cpp.
// On Windows, "windowsfiles" and "windowsprint" are interactive native
// dialog checks (--ui-show --open-scripts on 2D Flow Layout). Answer the
// system dialogs while the test window is shown; these categories
// deliberately are not unattended CTests.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"

#include "ui/app/EditorApp.h"

// winbase.h's Yield() would take ImGuiTestContext::Yield's place.
#undef Yield

namespace vdevuitest {

// The editor the tests drive (UiTestRun).
vdev::EditorApp &App();

void RegisterAppTests(ImGuiTestEngine *engine);
void RegisterFileMenuTests(ImGuiTestEngine *engine);
void RegisterSchematicTests(ImGuiTestEngine *engine);
void RegisterLayoutTests(ImGuiTestEngine *engine);
void RegisterLevelTests(ImGuiTestEngine *engine);
void RegisterSetupTests(ImGuiTestEngine *engine);
void RegisterHierarchyTests(ImGuiTestEngine *engine);
void RegisterBlocksTests(ImGuiTestEngine *engine);
void RegisterAttributesTests(ImGuiTestEngine *engine);
void RegisterCreateTests(ImGuiTestEngine *engine);
void RegisterSoundTests(ImGuiTestEngine *engine);
void RegisterCharacterTests(ImGuiTestEngine *engine);
void RegisterDebuggerTests(ImGuiTestEngine *engine);
void RegisterToolsTests(ImGuiTestEngine *engine);
void RegisterPrintTests(ImGuiTestEngine *engine);
void RegisterPlayerTests(ImGuiTestEngine *engine);
void RegisterDesktopTests(ImGuiTestEngine *engine);

// ---------------------------------------------------------------- the editor's thread

// Runs work on the editor's thread at the start of the next frame (in the
// test's GuiFunc, which must be MainThreadGui) and returns once it ran.
void OnMainThread(ImGuiTestContext *ctx, const std::function<void()> &work);
void MainThreadGui(ImGuiTestContext *ctx);

// What fn returns, run on the editor's thread.
template <typename T, typename F> T Read(ImGuiTestContext *ctx, F fn) {
    T value{};
    OnMainThread(ctx, [&] { value = fn(); });
    return value;
}

// The composition's object of that id as T (null when it is not one); on
// the editor's thread.
template <typename T> T *Object(CK_ID id) { return T::Cast(App().State().host.Context()->GetObject(id)); }

// An object's name (ANSI), whether it exists, a 3D entity's position, a
// BeObject's Script count (-1: not one), whether the object has initial
// conditions in the scene the editor's commands use (InitialConditionsScene).
std::string ObjectName(ImGuiTestContext *ctx, CK_ID id);
bool ObjectExists(ImGuiTestContext *ctx, CK_ID id);
VxVector Position(ImGuiTestContext *ctx, CK_ID entity);
int ScriptCount(ImGuiTestContext *ctx, CK_ID owner);
bool HasInitialConditions(ImGuiTestContext *ctx, CK_ID id);

// ---------------------------------------------------------------- input

// The editor's start done (the default layout, its focus changes), then
// the window of ref (e.g. "//3D Layout") in front and focused.
inline void Settle(ImGuiTestContext *ctx, const char *window = nullptr) {
    ctx->Yield(30);
    if (window) {
        ctx->WindowFocus(window);
        ctx->Yield(3);
    }
}

// Ctrl+Z and Ctrl+Y, their frames done.
inline void Undo(ImGuiTestContext *ctx) {
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    ctx->Yield(3);
}
inline void Redo(ImGuiTestContext *ctx) {
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    ctx->Yield(3);
}

// The middle of a screen rectangle.
inline ImVec2 Center(const ImVec2 &min, const ImVec2 &max) {
    return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
}

// The editor's Event Log from line from on, into the test's log (what a
// command refused said).
inline void LogEditor(ImGuiTestContext *ctx, size_t from) {
    const std::vector<vdev::LogEntry> &log = App().State().log;
    for (size_t i = from; i < log.size(); ++i) ctx->LogInfo("editor: %s", log[i].text.c_str());
}

// ---------------------------------------------------------------- windows

// The window of ref drawn at the last frame and not behind another tab of
// its dock node.
inline bool InFront(ImGuiTestContext *ctx, const char *ref) {
    ImGuiWindow *window = ctx->GetWindowByRef(ref);
    return window && window->WasActive && !window->Hidden;
}

// The Schematic: a point of the first open Script's graph (of in, when
// given) the farthest from the behaviors it shows, reserving room to its
// right and below for the item being inserted, with that
// Script; none when no Script is shown.
std::optional<std::pair<CK_ID, ImVec2>> EmptySchematicPoint(CK_ID in = 0, const ImVec2 &room = ImVec2(100, 60));

// The Level Manager: the first row of an object of class cid (exact: of
// that class, not a derived one), the closed rows opened with their arrows
// (the shallowest first) until one shows; ClickLevelRow clicks it on its
// name too. The Level Manager must be in front.
using LevelRow = vdev::EditorPanels::ShownRow;
std::optional<LevelRow> FindLevelRow(ImGuiTestContext *ctx, CK_CLASSID cid, bool exact = false);
std::optional<LevelRow> ClickLevelRow(ImGuiTestContext *ctx, CK_CLASSID cid, bool exact = false);

// The 3D Layout. An entity of the view: where a click picks it and its name
// (ANSI). FindEntity: the view's 3D entity with the most picked points of a
// 6-pixel grid (LayoutView::Pick, as the tools pick: the icons' owners too),
// only that one when wanted is given; entity 0 when none. SelectEntity: the
// 3D Layout focused with the mouse over its view,
// the scene zoomed on (W), an entity found and selected alone with a click
// (Select tool; the selection cleared first: a click on the one selected
// object deselects it).
struct InView {
    CK_ID entity = 0;
    ImVec2 at;
    std::string name;
};
InView FindEntity(ImGuiTestContext *ctx, CK_ID wanted = 0);
bool SelectEntity(ImGuiTestContext *ctx, InView &found);

// The Setups: whether an open Setup shows the object. ChooseInCombo: a
// combo of the Setup's page (window ref) opened, the item chosen in its list
// (ImGui::Combo pushes each item's index). BeginCombo does not give the test
// engine its label, so "**/" does not find it: its id is the label's in the
// page's child window ("page", SetupWindows::DrawSetup). TypeInCombo: the
// combo opened, chars typed in its list (the type-ahead's highlight), Enter
// (choose: the highlighted item chosen) or Escape (nothing chosen).
bool ShowsObject(CK_ID object);
// The object's Setup opened as the Level Manager's Enter does
// (ShowObjectSetup), its window (ref) shown and made the reference
// of the test's items; false when it is not. OpenSetupOf: that of the
// composition's first object of class cid (or derived, unless exact), 0
// when the sample has none.
bool ShowSetup(ImGuiTestContext *ctx, CK_ID id, const char *ref);
CK_ID OpenSetupOf(ImGuiTestContext *ctx, CK_CLASSID cid, const char *ref, bool exact = false);
void ChooseInCombo(ImGuiTestContext *ctx, const char *ref, const char *combo, const char *item);
void TypeInCombo(ImGuiTestContext *ctx, const char *ref, const char *combo, const char *chars, bool choose);

} // namespace vdevuitest
