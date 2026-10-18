#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/render/UiRenderer.h"
#include <SDL3/SDL.h>

#include "imgui.h"

#include "ui/app/EditorState.h"
#include "core/layout/LayoutInterface.h"
#include "core/layout/LayoutTools.h"
#include "core/layout/LayoutView.h"
#include "core/level/LevelCommands.h"
#include "ui/controls/ParameterBox.h"
#include "ui/controls/ValueEditor.h"



namespace vdev {

// Dev's 3D Layout tools: the command ids of its toolbar (Dev.exe sub_4B6C70)
// and of its tool factory (sub_4C4530).
enum class LayoutTool { Select = 25, Translate = 9, Scale = 10, Rotate = 11, Orbit = 20, Dolly = 23, Roll = 24,
                        Fov = 26, Zoom = 27, Pan = 28, Fly = 68,
                        // The Curve Setup's (EditorState::curveTool).
                        AddPoints = 128, InsertPoints = 129, EditPoints = 130, DeletePoints = 131 };

// Dev's names in its menus and messages: the class (Dev.exe sub_5D3550: 3D
// Frame, Portal, the light types, 2D Frame, Script; else the CK class) and
// the object (sub_5D3880: its name, else "<class>_<id>").
std::string DevClassName(CKObject *object);
std::string DevObjectName(CKObject *object);

// The 3D Layout window: Dev's top bar (Snapshot, 3D Layout Explorer,
// Selection Groups, the camera combo, General Preferences), its left
// toolbar and the view. LayoutWindow.cpp: the window, the toolbar, the view
// and the preferences; LayoutInput.cpp: the tools, selection, keys, flycam
// and curve tools; LayoutFullScreen.cpp: fullscreen; LayoutMenu.cpp: the
// shortcut menu; LayoutDrop.cpp: drag and drop;
// LayoutExplorer.cpp: Selection Groups and the 3D Layout Explorer;
// LayoutSnapshot.cpp: the Snapshot Buffer.
class LayoutWindow {
public:
    explicit LayoutWindow(EditorState &state);
    ~LayoutWindow();
    LayoutWindow(const LayoutWindow &) = delete;
    LayoutWindow &operator=(const LayoutWindow &) = delete;

    // The render context (a hidden window owned by window).
    bool Start(SDL_Window * window, std::string *error);
    void Stop();
    bool HandleEvent(const SDL_Event &event) { return m_View.HandleEvent(event); }
    // openPopups: false while the application moves the focus (it would close them).
    void Draw(ImGuiID dock, bool openPopups = true);
    // --layout-debug (capture checks): the On-screen Debug Data for the
    // session, the preference left as it is.
    bool debugData = false;
    LayoutView &View() { return m_View; }
    const LayoutView &View() const { return m_View; }
    LayoutTool Tool() const { return m_Tool; }
    // Dev's Player Mode (Dev.exe sub_4A11D0, sub_4A1E00): while the
    // composition plays, the Player Mode preferences, no navigation but the
    // left button's tools, no shortcut menu, clicks sent to the objects.
    bool PlayerMode() const;
#ifdef VIRTOOLSDEV_UI_TESTS
    // The view's rectangle on screen when it was drawn at the last frame
    // (the UI tests click in it); false when it was not.
    bool ShownView(ImVec2 &min, ImVec2 &max) const {
        if (!m_Started || ImGui::GetFrameCount() - m_ViewFrame > 1) return false;
        min = m_ViewMin;
        max = m_ViewMax;
        return true;
    }
    // --layout-tool / --layout-view (capture checks): a tool by name, a camera by name.
    void StartupOptions(const std::string &tool, const std::string &camera);
    // --drop-block: a Building Block dropped at a client point once the view has drawn.
    void StartupDrop(const CKGUID &guid, float x, float y);
    // --layout-select / --layout-group / --layout-command: objects selected by
    // name, a name entered in the Selection Groups combo, commands by Dev's id.
    void StartupSelection(const std::vector<std::string> &names, const std::string &group, const std::vector<int> &commands);
#endif
    // The status bar's action text (Dev's <Action Info>); empty when the
    // 3D Layout was not the last window used.
    std::string ActionText() const;
    // The status bar's selection text (<Actions>: "'%s' selected", "%d objects selected", "Nothing selected").
    std::string SelectionText() const;
    // The status bar's X / Y / Z (Dev.exe ids 262 to 273, shown by sub_424CB0
    // once a tool of the 3D Layout is chosen): false while the 3D Layout was
    // not the last window used. values: what the last transformation showed
    // ("%8.2f": the grabbed object's world position, Euler angles in degrees
    // or scale; a 2D entity's position), empty before.
    bool Coordinates(std::string values[3]) const;
    // The status bar's info text (Dev.exe id 292, CKUIM_OUTTOINFOBAR): what
    // a drop on the view would do.
    const std::string &InfoText() const { return m_DropText; }

    // A creation tool (the Create menu's commands).
    std::function<void(LayoutObject kind)> createObject;
    // Dev's Player/Author Mode keys: Ctrl+Space Play / Stop, Alt+Space Do One
    // Step, Ctrl+Backspace or Alt+Backspace Restore Initial Conditions.
    // Pause: the application left in fullscreen (Dev.exe sub_5CD3D0).
    enum class PlayCommand { PlayStop, Step, Reset, Pause };
    std::function<void(PlayCommand command)> play;
    // A Building Block dropped elsewhere (a Level Manager row, Dev.exe
    // sub_578C30) asks for its inputs as a drop here does: drop makes it
    // (its Script and block; 0 when refused) in one undo step, committed
    // with OK, taken back by Cancel. Returns drop's.
    std::pair<CK_ID, CK_ID> DropAsking(const std::function<std::pair<CK_ID, CK_ID>()> &drop);
    // The view is fullscreen (the editor's window is hidden behind it but
    // keeps going: the composition plays on).
    bool FullScreen() const { return m_View.FullScreen(); }
    // A camera, transform or 2D gesture or a selection rectangle going on
    // (Sticky Mode: with no button held).
    bool Busy() const {
        return m_Gesture.Active() || m_TransformGesture.Active() || m_Gesture2D.Active() || m_Selecting;
    }
    // The composition is closed: the objects waiting for a dialog (Copy
    // Options, a dropped Building Block's parameters) go.
    void DocumentClosed();

    // The composition's 3D Layout data (Dev.exe sub_4B46C0 on opening, sub_4B4E50
    // on saving): the selection group shown, the selection, the lock; the
    // rest is kept as the file had it.
    void ApplyLayoutData(const LayoutInterfaceData &data);
    LayoutInterfaceData LayoutData() const;

private:
    // ------------------------------------------------- window and toolbar
    // A button of Dev's toolbar: its rectangle in Dev's window coordinates,
    // its bitmaps (resvis.dll: normal, hover, checked, checked hover, disabled).
    struct Button {
        int command;
        int x0, y0, x1, y1;
        int bitmaps[5];
        const char *tooltip;
    };
    // An entry of a menu button (pressed and held: Dev's pop-up of icons).
    struct Item {
        int command;
        int bitmaps[3]; // normal, hover, checked
        const char *name;
    };
    void SyncComposition();
    void ApplyPreferences();
    // Alt+Enter (Dev.exe sub_5C5CB0, command 0x803C): fullscreen in Player
    // Mode with the Fullscreen Render Device and Render Mode, windowed again.
    // Leaving Player Mode leaves fullscreen and puts the editor's window
    // back where it was (sub_4A11D0).
    void ToggleFullScreen();
    enum class FullScreenExit { ReturnToEditor, Deactivated };
    void LeaveFullScreen(FullScreenExit reason = FullScreenExit::ReturnToEditor);
    // The view window's input in fullscreen: clicks sent to the objects,
    // Dev's keys, the application left (Dev.exe sub_4B95C0, WM_ACTIVATEAPP).
    void FullScreenEvent(const SDL_Event &event);
    void FullScreenCommands();
    void TopBar(const ImVec2 &origin, float width);
    void LeftBar(const ImVec2 &origin, float height);
    bool ToolbarButton(const Button &button, bool checked, bool enabled = true);
    bool MenuButton(const Button &button, const std::vector<Item> &items, int &current, bool checked,
                    bool enabled = true);
    void Command(int command);
    void RightClick(int command);
    void ToggleGuide(bool screen);
    void Shortcuts();

    // ------------------------------------------------- the view
    void Viewport(const ImVec2 &at, const ImVec2 &size);
    // Show On-screen Debug Data (3D Layout\Debug Mode; Dev.exe sub_4A01F0
    // fills its "Virtools_SpriteDebug" text at (10, 10)).
    void DebugData(const ImVec2 &at, int x, int y);
    void ViewInput(const ImVec2 &at, bool hovered);
    bool Upload(const std::vector<unsigned char> &pixels, int width, int height);
    void ReleaseTexture();
    // Camera tools.
    CameraSteps Steps() const;
    bool BeginCamera(CameraAction action, int x, int y, ImGuiMouseButton button);
    void NextCamera(bool previous);
    void ZoomOn(bool scene);
    // Selection (Dev.exe sub_4B0780: 10 clear, 11 select all, 12 invert;
    // sub_499870: a click or a rectangle).
    void SelectAll(bool invert);
    void ClearSelection();
    void ApplyClick(CK_ID picked);
    void ApplyRect(const VxRect &rect);
    void SetSelection(std::vector<CK_ID> objects);
    void ValidateSelection();
    std::vector<CK_ID> AliveSelection() const;
    // Transformations.
    TransformSettings &Settings();
    TransformSteps TransformStepsOf() const;
    bool BeginTransform(int x, int y);
    // The 2D entities (Dev.exe sub_4C4A70): a selected one's handle starts a
    // Translate (top-left) or a Scale (the others) whatever the tool, but
    // Translate; Snap & Align for them.
    int HandleAt(int x, int y, CK_ID *entity) const;
    bool BeginHandle(int x, int y);
    Snap2D Snap2DOf() const;
    // The curve tools (Dev.exe sub_49ACC0, sub_49CD00, sub_49B870, sub_49B400).
    bool CurveToolActive() const;
    void SyncCurveTool();
    void CurveClick(int x, int y, bool right);
    bool BeginCurveEdit(int x, int y);
    // Flycam (Y; Dev.exe sub_48C060): on until Y again or another tool; one
    // undo step for the flight.
    void BeginFly();
    void EndFly();
    void FlyFrame(int x, int y);

    // ------------------------------------------------- shortcut menu (LayoutMenu.cpp)
    // Dev's shortcut menu (Dev.exe sub_4C1BB0), made when it opens: the
    // objects under the mouse (the 2D or 3D entity, a curve point's curve,
    // the character, mesh, material, texture, place) and the current camera
    // when it is not a default view.
    struct MenuObject {
        CK_ID object = 0;
        std::string cls, name; // UTF-8
    };
    struct ContextMenu {
        std::vector<MenuObject> objects;
        CK_ID camera = 0;     // the current camera's entry
        CK_ID frame = 0;      // the 3D Frame under the mouse (Change Representation)
        bool initial = false; // one of them has initial conditions in the scene
    };
    void OpenContextMenu(int x, int y);
    void ContextMenuPopup();
    void InitialConditionsOn(int action);
    void CreateScriptOn(CK_ID object);
    void CreateScriptsOnSelection();
    void ShowScriptInSchematic(CK_ID script);
    void ResetCamera();
    void SetRepresentation(int representation);
    void Duplicate(const std::vector<CK_ID> &objects);
    // Dev's Copy Options (Dev.exe sub_5BDD60 with mode -1): asked before
    // Duplicate and Paste copy the objects.
    void CopyOptionsDialog();
    void DeleteSelection();

    // ------------------------------------------------- drag and drop (LayoutDrop.cpp)
    // Building Blocks and Data Resources files dropped on the view (Dev.exe
    // sub_4C0A30, sub_4BFD20, sub_4C2F30, sub_4C0F60).
    void DropTarget(const ImVec2 &at);
    CK_ID BlockTarget(const LayoutPickInfo &info, CK_CLASSID cid, bool ctrl) const;
    void DropBuildingBlock(int x, int y, const CKGUID &guid);
    bool DropBlockOn(CK_ID object, const CKGUID &guid, CK_ID &script, CK_ID &block);
    void DropResource(int x, int y, const ResourceFilePayload &file);
    // Dev's "%s - Edit Parameters" after the drop (Dev.exe sub_4BFD20,
    // dialog 714): OK keeps the Building Block with the values set, Cancel
    // takes it out again.
    void DropParametersDialog();

    // ------------------------------------------------- groups and explorer (LayoutExplorer.cpp)
    // Selection Groups (Dev.exe id 18): an editable combo; Enter on a new
    // name makes a group of the selection, on a group's name selects it.
    void SelectionGroups(const ImVec2 &at, float width);
    void EnterGroup(const std::string &name);
    void ChooseGroup(CK_ID group);
    void SyncGroup();
    // The 3D Layout Explorer (Dev.exe sub_59D400, dialog 734): modeless.
    void OpenExplorer();
    void ExplorerWindow();
    void CloseExplorer(bool cancel);

    // ------------------------------------------------- snapshots (LayoutSnapshot.cpp)
    // The Snapshot Buffer (Dev.exe sub_5BBB20, dialog 733): Take, Capture
    // (a 104 x 80 box following the mouse until a click), Close.
    void SnapshotWindow();
    void TakeSnapshot();
    void CaptureInput(const ImVec2 &at);
    void LoadSnapshot(int slot);
    void SaveSnapshot(int slot);

    // What names the open composition's objects (CK gives their ids to the
    // next document's objects): made anew when it is closed (DocumentClosed);
    // a field naming an object goes here.
    struct DocumentState {
        std::vector<CK_ID> playerSelection; // shown in Player Mode: the selection of Play
        CK_ID lastLevel = 0;                // the level shown (another: the composition changed)
        CK_ID flyCamera = 0;                // the camera flown (the flight's state before: m_FlyUndo)
        // Duplicate / Paste waiting for the Copy Options.
        std::vector<CK_ID> pendingCopy;
        bool openCopyOptions = false;
        // A drop waiting for its Edit Parameters: the drop and the values set
        // there, one undo step at OK, taken back by Cancel.
        CK_ID dropBlock = 0, dropScript = 0;
        std::unique_ptr<Edit> dropEdit;
        bool openDropParameters = false;
        // The 3D Layout Explorer open: the selection and lock to restore with
        // Cancel, the list's anchor for Shift clicks, the list's own
        // selection (the 3D Layout's again at each change of it, sub_59EC90,
        // with the count shown; a click or a key makes it the 3D Layout's).
        bool explorerOpen = false;
        std::vector<CK_ID> explorerSaved;
        bool explorerSavedLock = false;
        CK_ID explorerAnchor = 0;
        bool explorerRefused = false;
        std::vector<CK_ID> explorerList;
        unsigned explorerListRevision = ~0u;
        int explorerCount = 0;
        CK_ID explorerFocus = 0;
    };

    // ------------------------------------------------- state
    EditorState &m_State;
    LayoutView m_View;
    bool m_Started = false;
    std::string m_StartError;
    SDL_Window * m_MainWindow = nullptr; // ImGui's coordinates are its client's
    bool m_PlayerMode = false;   // the Player Mode preferences are applied
    DocumentState m_Doc;
    // Fullscreen: the editor's window placement before, the keys of the
    // view window waiting for the next frame (done out of its messages).
    bool m_MainHiddenForFullScreen = false;
    bool m_Switching = false; // its messages are not handled meanwhile (Dev.exe dword_648138)
    bool m_PendingToggle = false;
    bool m_PendingDeactivate = false;
    std::vector<PlayCommand> m_PendingPlay;
    // The next click picks the referent (Pivot Axis Object, Referential Pick Obj.).
    bool m_PickingReferent = false;
    // The Selection tool's hover (documentation "Selection Tool": "The cursor
    // changes to the select cursor"): an entity under the mouse, picked
    // again only when the mouse moves.
    ImVec2 m_HoverAt = ImVec2(-1.0f, -1.0f);
    bool m_HoverEntity = false;
    // The axes the current view allows (Constrain X 1, Y 2, Z 4).
    int AllowedAxes() const;
    float m_Scale = 1.0f;     // Dev pixels to editor pixels
    ImVec2 m_Origin;          // the window's content origin
    bool m_LastUsed = false;  // the 3D Layout was the last window used
    // The frame shown.
    std::vector<unsigned char> m_Pixels;
    std::unique_ptr<UiTexture> m_Texture;
    int m_TextureWidth = 0;
    int m_TextureHeight = 0;
    // Tools and their settings.
    LayoutTool m_Tool = LayoutTool::Select;
    // Each transformation tool's settings (Dev's defaults: Translate X+Z in
    // the world, Rotate Y and Scale X+Y about each object's axis, the
    // hierarchy following).
    TransformSettings m_Transform[3] = {{5, 1, 1, true, false}, {2, 0, 1, true, false}, {3, 0, 1, true, false}};
    // Edit Points' own (Dev.exe byte_6418D4 / D5: X+Z in the world).
    TransformSettings m_CurveEdit = {5, 1, 1, false, false};
    int m_ScaleMode = 0;   // the Scale menu: 0 uniform, 1 volumetric, 2 the constrained axes (V)
    int m_PortalMode = 1;  // Create Portal: 0 portals around the selection, 1 a portal
    int m_ZoomMode = 0;    // Zoom: 0 zoom, 1 on selection, 2 on scene
    int m_OrbitMode = 0;   // Orbit: 0 around the target / pivot / selection, 1 around the camera
    float m_ZoomExtent = 5.0f; // the wheel's step (Dev.exe +716)
    // Menu button being held (its command) and when.
    int m_HeldMenu = 0;
    double m_HeldSince = 0.0;
    // The view's rectangle when it was drawn last (the parameter UIs' pick).
    ImVec2 m_ViewMin, m_ViewMax;
    ImVec2 m_ScreenScroll; // Screen Size larger than the view: the part shown
    int m_ViewFrame = -1;
    // The status bar's X / Y / Z.
    std::string m_Coordinates[3];
    void UpdateCoordinates();
    // Gestures in the view: the camera tool being dragged (with its button),
    // the transformation (a curve point's with Edit Points), the select
    // tool's press (and whether it became a rectangle).
    CameraGesture m_Gesture;
    ImGuiMouseButton m_GestureButton = ImGuiMouseButton_Left;
    TransformGesture m_TransformGesture;
    Transform2DGesture m_Gesture2D;
    bool m_CurveEditing = false;
    bool m_Selecting = false;
    bool m_Rectangle = false;
    ImVec2 m_PressAt;
    // The right button went down in the view with nothing being dragged.
    bool m_RightPressed = false;
    // The flight: its mode, where the offsets count from, the orbit's
    // center, the buttons held, the camera's state before (the camera:
    // m_Doc.flyCamera).
    FlyMode m_FlyMode = FlyMode::Advance;
    ImVec2 m_FlyStart, m_FlyLast;
    VxVector m_FlyCenter;
    double m_FlyTime = 0.0;
    bool m_FlyLeft = false, m_FlyRight = false;
    ObjectsGesture m_FlyUndo;
    // The shortcut menu open.
    ContextMenu m_Menu;
    // The Copy Options dialog's copy (what waits for it: m_Doc.pendingCopy).
    bool m_OpenPopups = true;
    CKDependencies m_CopyOptions;
    // Drag and drop: the status bar while something is dragged over the
    // view; the --drop-block drop waiting for the view.
    std::string m_DropText;
    ParameterBox m_DropBox{m_State};
    ParameterBox m_CopyBox{m_State}; // Copy Options
#ifdef VIRTOOLSDEV_UI_TESTS
    CKGUID m_StartupDrop;
    ImVec2 m_StartupDropAt;
    int m_StartupDropFrames = 0;
#endif
    // Selection Groups: the group shown (Dev.exe view +60), the combo's
    // text, whether it is being typed in, the selection it was matched with.
    CK_ID m_Group = 0;
    char m_GroupText[256] = "";
    bool m_GroupEditing = false;
    unsigned m_GroupRevision = ~0u;
    LayoutInterfaceData m_Interface; // as read on opening
    // The 3D Layout Explorer: the types shown (Dev keeps them between
    // openings) and the text filter (lower case); the rest is the
    // document's (m_Doc).
    unsigned m_ExplorerShown = 0x1FF;
    char m_ExplorerFilter[256] = "";
    std::string m_ExplorerTyped; // the list view's incremental search
    double m_ExplorerTypedTime = 0.0;
    // The Snapshot Buffer: open, the slots selected, the slot of the
    // shortcut menu; Capture: 1 waits for the buttons to be released, 2
    // follows the mouse.
    bool m_SnapshotOpen = false;
    std::vector<int> m_SnapshotSelection;
    int m_SnapshotMenu = -1;
    int m_Capturing = 0;
    unsigned m_SnapshotRevisions[36] = {};
};

} // namespace vdev
