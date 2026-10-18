#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "imgui.h"

#include "ui/app/EditorState.h"
#include "core/level/LevelCommands.h"
#include "core/level/ObjectProperties.h"
#include "core/level/ObjectEditCommands.h"
#include "ui/controls/ParameterBox.h"

namespace vdev {

// Dev's Setup panels (screenshots: Grid, 2D Frame, 3D Object, Target Camera,
// Point Light, Material, Mesh, Texture, Curve, Character, Sound, Array and
// Group Setup): one tab per kind of object,
// docked with the Schematic and the Level Manager. A tab has Dev's frame -
// the Name list of the objects of that class, Parameter / Attribute pages,
// Set / Remove / Restore IC - and the page of its class. Every edit is one
// undo step (object state before and after, ObjectEditCommands).
namespace setup {
struct DevPage;
}

class SetupWindows {
public:
    explicit SetupWindows(EditorState &state) : m_State(state) {}

    // Opens (or brings forward) the Setup of an object.
    void Open(CK_ID object);
    // Setups opened from now on show their Attribute page (capture mode).
    bool attributesFirst = false;
    // dock: where a Setup opened for the first time docks (the Schematic's node).
    void Draw(ImGuiID dock);
    void CloseAll();
    // The Setups open, in their order (the 3D Layout's data saves them, Dev.exe
    // sub_4B4E50): the object, the Curve Setup's control point, the one in front.
    struct OpenSetup {
        CK_ID object = 0, other = 0;
        bool front = false;
    };
    std::vector<OpenSetup> OpenSetups() const;

private:
    friend struct SetupEditTests;
    enum class Kind { Entity3D, Camera, Light, Entity2D, Material, Mesh, Texture, Grid, Other, Curve, Character, Sound, Array, Group, Scene, Sprite3D, Sprite2D,
                      Animation, ObjectAnimation, MidiSound, SpriteText, KinematicChain, Layer, Parameter, ParameterIn,
                      Operation, Behavior, BehaviorIO, BehaviorLink, Synchro, State, CriticalSection, InterfaceObjects };
    struct Setup {
        Kind kind = Kind::Other;
        CK_ID object = 0;
        bool attributes = false; // Attribute page shown
        // The header's Level / Current Scene filter (pressed: the current
        // scene's objects in the Name combo, Dev.exe sub_44DD30).
        bool sceneFilter = true;
        // 3D Object Setup: the Object Meshes selected, Add Mesh's chooser.
        std::set<CK_ID> meshRows;
        // Character Setup: the body parts' rows selected; Add Body Part asked.
        std::set<CK_ID> bodyPartRows;
        bool openAddBodyPart = false;
        bool openAddMesh = false;
        // Material Setup: Custom chosen in Mode (Dev's combo keeps it while
        // the settings it would show are another mode's).
        bool materialCustom = false;
        // Mesh Setup: the channels selected, Add Channel's chooser.
        std::set<int> channelRows;
        bool openAddChannel = false;
        std::set<CKAttributeType> attributeRows; // its rows selected
        bool open = true;
        bool focus = false;
        int slot = 0; // other Setups of the type (Show New Setups): 1, 2, ...
        // Texture Setup
        CK_ID shownTexture = 0;
        int shownSlot = -1;
        int textureFrames = 0;
        // Texture / 2D Sprite Setup: the previews' zoom (0: Best Fit), the
        // Resize Slot dialog's size.
        float zoom = 0.0f;
        // The previews scrolled (left drag, pixels at the zoom) and zoomed
        // (right drag; Dev: 7% to 5000%); a right drag shows no menu.
        ImVec2 scroll = ImVec2(0.0f, 0.0f);
        bool previewDragged = false;
        // Pick a color on the texture (Dev.exe sub_485C10 400): the next
        // press on the preview, and the drag after it, set the color key.
        bool pickingColor = false;
        // An image dropped on the alpha preview with Ctrl or Shift: its
        // channels' menu (Dev.exe sub_48AC30 825..828).
        std::string droppedAlpha;
        bool openResize = false;
        int resizeWidth = 0, resizeHeight = 0;
        // Remove slot asked ("Remove current texture slot ?").
        bool confirmRemoveSlot = false;
        // Grid Setup
        int layer = 0;      // selected layer (index)
        std::set<int> layers; // the layers painted (indices; the selected one with them)
        int paint = 255;    // value painted
        int hoverX = -1, hoverY = -1;
        bool grid3d = false;       // the 3D Properties shown (documentation: "Setup", not "Grid Setup")
        int squareX = -1, squareY = -1;
        bool openSquare = false;   // the Square Value dialog (a square double-clicked)
        int squareValue = 0;
        int layerType = 0;         // the Global Layer List's row chosen (F2, menu)
        int classification = -1;   // the Global Classification List's row chosen
        bool openGridRename = false;
        bool renamingClassification = false;
        std::string gridRenameText;
        // Curve Setup: the current control point
        int point = 0;
        // Array Setup: the cell being edited, the column dialogs
        int editRow = -1, editColumn = -1;
        std::string editText;
        char columnName[128] = "";
        int columnType = 1;       // CK_ARRAYTYPE
        int columnParameter = -1; // CKParameterType of a parameter column
        // Array Setup: the rows selected (Shift's anchor), the column last
        // clicked, a cell to edit (Edit), Edit Multiple's column and text.
        std::set<int> arrayRows;
        int arrayAnchor = -1, arrayColumn = -1;
        int focusRow = -1, focusColumn = -1;
        int multiColumn = -1;
        bool openMultiple = false;
        char multiText[512] = "";
    };

    static Kind KindOf(CKObject *object);
    // Dev.exe sub_4A01F0 (RealTime Notifications): the Setups show the 3D
    // entities' transforms of the last notification. Without Realtime Setup
    // Updates a 3D Layout transformation notifies when it ends; in Player
    // Mode the Setups are notified every FrameSkip frames, or not at all
    // without Realtime setup updates every.
    struct ShownTransform {
        VxMatrix world, local;
        VxBbox box; // world bounding box
    };
    bool m_Notify = true; // this frame refreshes m_Doc.shown
    int m_PlayFrames = 0;
    const ShownTransform &Shown(CK3dEntity *entity);
    std::string TitleOf(const Setup &setup, CKObject *object) const;
    void DrawSetup(Setup &setup, ImGuiID dock);
    void Header(Setup &setup, CKObject *object);
    void Sidebar(Setup &setup, CKObject *object);
    void AttributePage(Setup &setup, CKObject *object);
    // sized: Scale and World Size (fourth: what replaces World Size, drawn
    // at Dev's x 620); details: meshes, Pickable.
    void Entity3DPage(CK3dEntity *entity, bool sized, bool details,
                      const std::function<void(const setup::DevPage &page)> &fourth = nullptr, Setup *setup = nullptr);
    void CameraPage(CKCamera *camera);
    void LightPage(CKLight *light);
    // sprite: the lower part of the 2D Sprite Setup (Cropping instead of
    // Material and UVMapping).
    void Entity2DPage(CK2dEntity *entity, bool sprite = false);
    void SpritePage(Setup &setup, CKSprite *sprite);
    // A Texture / 2D Sprite Setup preview (the image key, in a box side
    // wide, zoomed) and its slot menu (Dev.exe sub_485C10 / sub_476FA0):
    // the image's, or (alpha) the alpha channel's.
    void SlotPreview(Setup &setup, CKObject *bitmap, unsigned long long key, int width, int height, float side, bool alpha);
    void SlotMenu(Setup &setup, CKObject *bitmap, bool alpha);
    // The Image Format save option's format (Dev's Image Saver, Dev.exe
    // sub_5889B0): a bitmap reader that saves, with its default options;
    // whether the alpha channel goes with it.
    void SaveFormatControls(CKObject *bitmap);
    // The slot menu's commands, and their keys (Dev.exe sub_485C10 /
    // sub_476FA0, sub_487640: O add, R replace, Shift+R replace alpha, S
    // save, n / p the next / previous slot, + - / * the zoom).
    enum class SlotCommand { Add, Replace, ReplaceAlpha, Save, SaveAlpha, Next, Previous, ZoomIn, ZoomOut, Zoom1, ZoomFit };
    void RunSlotCommand(Setup &setup, CKObject *bitmap, SlotCommand command);
    void SlotKeys(Setup &setup, CKObject *bitmap);
    void RemoveSlotDialog(Setup &setup, CKObject *bitmap);
    void MaterialPage(Setup &setup, CKMaterial *material);
    void MeshPage(Setup &setup, CKMesh *mesh);
    void TexturePage(Setup &setup, CKTexture *texture);
    void GridPage(Setup &setup, CKGrid *grid);
    void CurvePage(Setup &setup, CKCurve *curve);
    void CharacterPage(Setup &setup, CKCharacter *character);
    void SoundPage(CKWaveSound *sound);
    void ArrayPage(Setup &setup, CKDataArray *array);
    void ScenePage(CKObject *object);
    // 3D Frame, Portal and Place Setup (World / Local transforms).
    void FramePage(CK3dEntity *frame);
    void PlaceLinks(CKPlace *place);
    void Report(const EditResult &result, const char *what);
    void Sprite3DPage(CKSprite3D *sprite);
    // Classes Dev has no Setup for (Dev.exe sub_4B0010 opens none): pages
    // after the class definitions (SDK headers).
    void AnimationPage(CKAnimation *animation);
    void ObjectAnimationPage(CKObjectAnimation *animation);
    void MidiSoundPage(CKMidiSound *sound);
    void SpriteTextPage(CKSpriteText *text);
    // A name and a button opening the object's Setup.
    void SetupLink(const char *label, CKObject *object);
    // The classes that are not behavioral objects (Dev has no Setup for them
    // either): pages after their class definitions (SDK headers).
    void KinematicChainPage(CKKinematicChain *chain);
    void LayerPage(CKLayer *layer);
    // Local and output parameters (CKParameter), input parameters.
    void ParameterPage(CKParameter *parameter);
    void ParameterInPage(CKParameterIn *input);
    void OperationPage(CKParameterOperation *operation);
    void BehaviorPage(CKBehavior *behavior);
    void BehaviorIOPage(CKBehaviorIO *io);
    void BehaviorLinkPage(CKBehaviorLink *link);
    void SynchroPage(CKSynchroObject *synchro);
    void StatePage(CKStateObject *state);
    void CriticalSectionPage(CKCriticalSectionObject *section);
    void InterfaceObjectsPage(CKInterfaceObjectManager *manager);
    // The behaviors whose parameters hold the object (Synchro, State,
    // Critical Section, Kinematic Chain parameters); a double click shows
    // the behavior's Setup.
    void ParameterUsers(CKObject *object);
    // The parameter's value text and Edit Value... (Dev's Edit Parameter).
    void ParameterValue(CKParameter *value, bool editable);
    // Shows the behavior's Script in the Schematic with the element selected
    // (the behavior when ref is empty).
    void ShowInSchematic(CKBehavior *behavior, const ElementRef &ref = ElementRef());
    void OtherPage(CKObject *object);

    // Edits: a change applied now; consecutive changes of one control (a
    // drag, typing) form one undo step, recorded when no control is active.
    void Live(CK_ID object, const char *label, const std::function<void(CKObject *)> &change);
    // A property's live edit (core ObjectProperties: its label, its setter).
    template <typename T> void LiveSet(CK_ID object, const ObjectProperty<T> &property, std::common_type_t<T> value,
                                       const char *label = nullptr) {
        Live(object, label ? label : property.label, [&property, value](CKObject *o) { property.set(o, value); });
    }
    // An action (core SetupCommands): the edit being made recorded first,
    // then the action's undo step; a refusal logged.
    template <typename Command> void Act(const char *what, Command command) {
        if (!FlushPending()) return;
        Report(command(), what);
    }
    bool FlushPending();
    void CancelPending();
    // The same for a scene's environment (not an object state: the scene
    // also holds the initial conditions of its objects).
    void LiveScene(CK_ID scene, const char *label, const std::function<void(CKScene *)> &change);
    void UsedBy(const char *title, const std::vector<CKObject *> &users, float height);
    void UsedByList(const char *id, const std::vector<CKObject *> &users, const ImVec2 &size);
    bool ObjectCombo(const char *label, CK_CLASSID cid, CKObject *current, CK_ID *chosen, float width = 190.0f);

    // What names the open composition's objects (CK gives their ids to the
    // next document's objects): made anew when it is closed; a field naming
    // an object goes here.
    struct DocumentState {
        std::vector<Setup> setups;
        CK_ID front = 0;                       // the object of the Setup focused last
        std::map<CK_ID, ShownTransform> shown; // refreshed when m_Notify
        CK_ID gridLayer = 0;   // a layer whose Grid Setup opens on it (Layer Setup)
        // F2 in a Setup (documentation "Setups": "Press F2 to edit the name").
        CK_ID renameObject = 0;
        std::string renameText;
        bool openRename = false;
        CK_ID valueScript = 0; // the Script of the parameter edited (Parameter Setups)
    };

    EditorState &m_State;
    DocumentState m_Doc;
    // An attribute's value in Dev's Edit Parameter (its type shown).
    ParameterBox m_AttributeBox{m_State};
    // The Image Format's options being edited (SaveFormatControls: Edit
    // Parameter of the reader's option parameter,
    // CKPluginManager::GetReaderOptionData; after m_State, which it keeps)
    // and Dev's lines on what the format gives (the image saved and read
    // back in a state chunk: its size, the times), made again when the
    // format changes.
    ParameterBox m_FormatBox{m_State};
    CK_ID m_FormatParameter = 0;
    bool m_OpenFormatOptions = false;
    std::map<CK_ID, std::string> m_FormatInfo, m_FormatInfoKey;
    std::string m_AttributeType;
    bool m_OpenAttributeValue = false;
    void AttributeValueDialog();
    ObjectsGesture m_Pending;
    CK_ID m_PendingObject = 0;
    std::string m_PendingLabel;
    bool m_PendingSceneActive = false;
    CK_ID m_PendingScene = 0;
    SceneEnvironment m_PendingSceneBefore;
    std::string m_SpriteTextEdit; // Sprite Text: the text or font face being typed
    CK_ID m_SpriteTextEditing = 0;
    float m_MergeFactor = -1.0f;  // Animation Setup: the merge factor being dragged
    int m_PlaceMenuRow = -1; // Place Setup: the link row the context menu opened on
    // Place Setup's Automatic Portal: the place whose portal waits for the
    // other place to be picked (a click in a window that answers, Escape or
    // a right click gives up).
    CK_ID m_PortalPlace = 0;
    void PortalPick();
    // The header's picker (Dev.exe sub_4436E0 20): the Setup of this object
    // takes the next object of its class clicked (Escape, a right click or
    // the picker again gives up).
    CK_ID m_PickSetup = 0;
    int m_PickSetupSlot = 0;
    void SetupPick();
};

} // namespace vdev
