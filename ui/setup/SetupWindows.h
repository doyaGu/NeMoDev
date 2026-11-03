#pragma once

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "imgui.h"

#include "VxMatrix.h"
#include "VxBbox.h"
#include "VxColor.h"
#include "VxRect.h"
#include "application/Preferences.h"
#include "features/layout/SnapshotData.h"
#include "ui/render/UiRenderer.h"
#include "ui/controls/FileDialogs.h"
#include "features/arrays/ArrayTable.h"
#include "features/setup/SetupViewData.h"

namespace vdev {
class EditorWorkspace;

// Dev's Setup panels (screenshots: Grid, 2D Frame, 3D Object, Target Camera,
// Point Light, Material, Mesh, Texture, Curve, Character, Sound, Array and
// Group Setup): one tab per kind of object,
// docked with the Schematic and the Level Manager. A tab has Dev's frame -
// the Name list of the objects of that class, Parameter / Attribute pages,
// Set / Remove / Restore IC - and the page of its class. Every edit is one
// undo step (object state before and after, ObjectEditCommands).
class SetupWindows {
public:
    explicit SetupWindows(EditorWorkspace &workspace, FileDialogs &files) : m_Workspace(workspace), m_Files(files) {}
    bool Register(std::string &error);

    // Opens (or brings forward) the Setup of an object.
    void Open(ObjectRef object);
    // Setups opened from now on show their Attribute page (capture mode).
    bool attributesFirst = false;
    void CloseAll();
    // The Setups open, in their order (the 3D Layout's data saves them, Dev.exe
    // sub_4B4E50): the object, the Curve Setup's control point, the one in front.
    struct OpenSetup {
        ObjectRef object{}, other{};
        EditorView view = 0;
        bool front = false;
    };
    std::vector<OpenSetup> OpenSetups() const;

private:
    friend struct SetupEditTests;
    using Kind = SetupKind;
    struct Setup {
        SetupWindows *owner = nullptr;
        Kind kind = Kind::Other;
        ObjectRef reference{};
        SetupPageData page;
        EditorView nameOwner = 0;
        EditorEdit renameEdit = 0;
        bool openRename = false;
        bool filePending = false;
        uint64_t inspectionRevision = 1;
        bool attributes = false; // Attribute page shown
        // The header's Level / Current Scene filter (pressed: the current
        // scene's objects in the Name combo, Dev.exe sub_44DD30).
        bool sceneFilter = true;
        // 3D Object Setup: the Object Meshes selected, Add Mesh's chooser.
        std::set<ObjectRef> meshRows;
        // Character Setup: the body parts' rows selected; Add Body Part asked.
        std::set<ObjectRef> bodyPartRows;
        bool openAddBodyPart = false;
        bool openAddMesh = false;
        // Material Setup: Custom chosen in Mode (Dev's combo keeps it while
        // the settings it would show are another mode's).
        bool materialCustom = false;
        // Mesh Setup: the channels selected, Add Channel's chooser.
        std::set<int> channelRows;
        bool openAddChannel = false;
        std::set<uint64_t> attributeRows; // its rows selected
        uint64_t attributeCatalog = 0;
        ObjectPictureData picture;
        uint64_t pictureRevision = 0;
        std::unique_ptr<UiTexture> pictureTexture;
        // Texture Setup
        ObjectRef shownTexture{};
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
        int droppedAlphaSlot = 0;
        uint64_t droppedAlphaRevision = 0;
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
        std::vector<ObjectRef> squareTargets;
        uint64_t squareRevision = 0;
        int layerType = 0;         // the Global Layer List's row chosen (F2, menu)
        int classification = -1;   // the Global Classification List's row chosen
        bool openGridRename = false;
        bool renamingClassification = false;
        std::string gridRenameText;
        uint64_t gridRenameRevision = 0;
        int gridRenameIndex = 0;
        // Curve Setup: the current control point
        int point = 0;
        ArrayTable array;
    };

    EditorClient Client(EditorView owner = 0) const;
    void ApplyPreferences();
    EditorStatus Show(ObjectRef target, EditorSink);
    EditorStatus Navigate(Setup &, ObjectRef, int);
    std::vector<EditorView> Instances() const;
    EditorStatus Save(const Setup &, EditorSink) const;
    EditorStatus Restore(Setup &, const EditorRequest &);
    void PrepareFrame();
    void ChangeTarget(Setup &, ObjectRef, int point = -1);
    void Close(Setup &);
    // Dev.exe sub_4A01F0 (RealTime Notifications): the Setups show the 3D
    // entities' transforms of the last notification. Without Realtime Setup
    // Updates a 3D Layout transformation notifies when it ends; in Player
    // Mode the Setups are notified every FrameSkip frames, or not at all
    // without Realtime setup updates every.
    struct ShownTransform {
        VxMatrix world, local;
        VxBbox box; // world bounding box
    };
    bool m_Notify = true; // this frame refreshes m_Shown
    int m_PlayFrames = 0;
    const ShownTransform &Shown(ObjectRef target);
    void DrawSetup(Setup &setup, EditorViewFrame &frame);
    void Header(Setup &, const SetupPageData &);
    void Sidebar(Setup &, const SetupPageData &);
    void Picture(Setup &);
    void ReplaceFile(Setup &, SetupFileKind);
    void DrawObjectPage(Setup &);
    void AttributePage(Setup &setup);
    // The transform and, when provided, the object's mesh and selection controls.
    void Entity3DPage(ObjectRef target, Setup *setup = nullptr, const std::function<void()> &fourth = nullptr);
    void TransformFields(ObjectRef target, const char *field, float *world, float *local, float divisor,
                         const char *worldId);
    void Entity3DTransform(ObjectRef target, bool sized = false, const std::function<void()> &fourth = nullptr);
    void CameraPage(ObjectRef target);
    void LightPage(ObjectRef target);
    // sprite: the lower part of the 2D Sprite Setup (Cropping instead of
    // Material and UVMapping).
    void Entity2DPage(ObjectRef target, bool sprite = false);
    void SpritePage(Setup &setup, ObjectRef target);
    // A Texture / 2D Sprite Setup preview (the image key, in a box side
    // wide, zoomed) and its slot menu (Dev.exe sub_485C10 / sub_476FA0):
    // the image's, or (alpha) the alpha channel's.
    bool ReadPixels(ObjectRef, const char *, uint64_t, int &, int &, std::vector<uint32_t> &) const;
    void SlotPreview(Setup &setup, ObjectRef target, unsigned long long key, int width, int height, float side,
                     bool alpha);
    void SlotMenu(Setup &setup, ObjectRef target, bool alpha);
    // The Image Format save option's format (Dev's Image Saver, Dev.exe
    // sub_5889B0): a bitmap reader that saves, with its default options;
    // whether the alpha channel goes with it.
    void SaveFormatControls(ObjectRef target);
    void BitmapInfo(Setup &, ObjectRef, SetupFields, SetupFields);
    // The slot menu's commands, and their keys (Dev.exe sub_485C10 /
    // sub_476FA0, sub_487640: O add, R replace, Shift+R replace alpha, S
    // save, n / p the next / previous slot, + - / * the zoom).
    enum class SlotCommand { Add, Replace, ReplaceAlpha, Save, SaveAlpha, Next, Previous, ZoomIn, ZoomOut, Zoom1, ZoomFit };
    void RunSlotCommand(Setup &setup, ObjectRef target, SlotCommand command);
    void SlotKeys(Setup &setup, ObjectRef target);
    void RemoveSlotDialog(Setup &setup, ObjectRef target);
    void MaterialPage(Setup &setup, ObjectRef target);
    void MeshPage(Setup &setup, ObjectRef target);
    void TexturePage(Setup &setup, ObjectRef target);
    void GridPage(Setup &setup, ObjectRef target);
    void CurvePage(Setup &setup, ObjectRef target);
    void CharacterPage(Setup &setup, ObjectRef target);
    void SoundPage(ObjectRef target);
    void ArrayPage(Setup &setup);
    void ScenePage(ObjectRef target);
    // 3D Frame, Portal and Place Setup (World / Local transforms).
    void FramePage(ObjectRef target);
    void PlaceLinks(ObjectRef target);
    void Warn(const std::string &);
    void Report(EditorStatus, const std::string &);
    void Sprite3DPage(ObjectRef target);
    // Classes Dev has no Setup for (Dev.exe sub_4B0010 opens none): pages
    // after the class definitions (SDK headers).
    void AnimationPage(ObjectRef target);
    void ObjectAnimationPage(ObjectRef target);
    void MidiSoundPage(ObjectRef target);
    void SpriteTextPage(ObjectRef target);
    // A name and a button opening the object's Setup.
    // The classes that are not behavioral objects (Dev has no Setup for them
    // either): pages after their class definitions (SDK headers).
    void KinematicChainPage(ObjectRef target);
    void LayerPage(ObjectRef target);
    // Local and output parameters (CKParameter), input parameters.
    void ParameterPage(ObjectRef target);
    void ParameterInPage(ObjectRef target);
    void OperationPage(ObjectRef target);
    void BehaviorPage(ObjectRef target);
    void BehaviorIOPage(ObjectRef target);
    void BehaviorLinkPage(ObjectRef target);
    void SynchroPage(ObjectRef target);
    void StatePage(ObjectRef target);
    void CriticalSectionPage(ObjectRef target);
    void InterfaceObjectsPage(ObjectRef target);
    // The behaviors whose parameters hold the object (Synchro, State,
    // Critical Section, Kinematic Chain parameters); a double click shows
    // the behavior's Setup.
    void ParameterUsers(ObjectRef target);
    // The parameter's value text and Edit Value... (Dev's Edit Parameter).
    void ParameterValue(const SetupFields &, bool editable);
    // Shows the behavior's Script in the Schematic with the element selected
    // (the behavior when ref is empty).
    void ShowInSchematic(const SetupFields &);
    void OtherPage(ObjectRef target);

    bool ReadDetails(ObjectRef target, const char *section, SetupDetails &data,
                     const std::vector<uint64_t> &indices = {}) const;
    bool ReadProperties(ObjectRef target, const std::vector<const char *> &, SetupDetails &data,
                        const std::vector<uint64_t> &indices = {}) const;
    void SetupLink(const char *label, ObjectRef target, const std::string &name);
    void UsedByRows(const char *title, const std::vector<EditorField> &, float height);
    bool ObjectChoice(const char *label, uint32_t classId, ObjectRef current, const std::string &name,
                      ObjectRef *chosen, float width = 190.0f);
    EditorResponse RunSetupCommand(const char *, const std::vector<EditorValue> & = {});
    void ChooseFile(const FileDialogOptions &, std::function<void(const FileDialogResult &)>, bool multiple = false);
    void PreviewProperty(ObjectRef target, const char *property, EditorField value, std::vector<uint64_t> indices = {},
                         ObjectRef relative = {}, std::vector<ObjectRef> targets = {});
    template <class T> void PreviewValue(ObjectRef target, const char *property, const T &value) {
        EditorField field{"value"};
        if constexpr (std::is_same_v<T, ObjectRef>) {
            field.type = EDITOR_OBJECT;
            field.object = value;
        } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
            field.type = EDITOR_INTEGER_VALUE;
            field.integer = uint64_t(int64_t(value));
        } else if constexpr (std::is_floating_point_v<T>) {
            field.type = EDITOR_REAL_VALUE;
            field.real = value;
        } else if constexpr (std::is_same_v<T, std::string>) {
            field.type = EDITOR_TEXT_VALUE;
            field.text = value;
        } else {
            field.type = EDITOR_REAL_LIST;
            if constexpr (std::is_same_v<T, VxColor>)
                field.reals = {value.r, value.g, value.b, value.a};
            else if constexpr (std::is_same_v<T, VxVector>)
                field.reals = {value.x, value.y, value.z};
            else if constexpr (std::is_same_v<T, VxRect>)
                field.reals = {value.left, value.top, value.right, value.bottom};
            else
                field.reals = {value.x, value.y};
        }
        PreviewProperty(target, property, std::move(field));
    }
    void PreviewNumbers(ObjectRef target, const char *property, const float *numbers, int count, float divisor = 1.0f) {
        EditorField value{"value", EDITOR_REAL_LIST};
        for (int i = 0; i < count; ++i)
            value.reals.push_back(numbers[i] / divisor);
        PreviewProperty(target, property, std::move(value));
    }
    void PreviewFont(ObjectRef target, const SetupFont &font) {
        EditorField value{"value", EDITOR_RECORD};
        value.fields = {{"face", EDITOR_TEXT_VALUE, {}, font.face},
                        {"size", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(font.size))},
                        {"weight", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(int64_t(font.weight))},
                        {"italic", EDITOR_INTEGER_VALUE, {}, {}, font.italic},
                        {"underline", EDITOR_INTEGER_VALUE, {}, {}, font.underline}};
        PreviewProperty(target, "spritetext.font", std::move(value));
    }
    bool EditObjectList(ObjectRef target, const char *property, const std::vector<ObjectRef> &add,
                        const std::vector<ObjectRef> &remove = {});
    // Indexed edits must still describe the same source rows when editing starts.
    void PreviewIndexedProperty(ObjectRef target, const char *property, EditorField value,
                                std::vector<uint64_t> indices, uint64_t revision, std::vector<ObjectRef> targets = {});
    bool FinishProperty(bool commit);
    void SetProperty(ObjectRef target, EditorView owner, const char *property, EditorField value);
    bool FlushPending();
    void CancelPending();
    void UsedByList(const char *id, const std::vector<EditorField> &rows, const ImVec2 &size);

    EditorWorkspace &m_Workspace;
    FileDialogs &m_Files;
    Preferences m_Preferences;
    uint64_t m_PreferencesRevision = 0;
    // Workspace owns each Setup; this index supports shared tools without a second owner.
    std::map<EditorView, Setup *> m_Instances;
    std::map<ObjectRef, ShownTransform> m_Shown;
    ObjectRef m_GridLayer{};
    int m_PreparedFrame = -1;
    std::map<ObjectRef, std::string> m_FormatInfo, m_FormatInfoKey;
    EditorEdit m_PropertyEdit = 0;
    EditorOperation m_SubmittedEdit = 0;
    EditorView m_CurrentOwner = 0, m_PropertyOwner = 0;
    ObjectRef m_PropertyTarget{}, m_PropertySetup{}, m_CurrentTarget{};
    std::string m_PropertyKey;
    unsigned m_PropertyControl = 0, m_PropertyActive = 0, m_CancelledControl = 0;
    int m_CancelledFrame = 0;
    int m_PlaceMenuRow = -1; // Place Setup: the link row the context menu opened on
    // Place Setup's Automatic Portal: the place whose portal waits for the
    // other place to be picked (a click in a window that answers, Escape or
    // a right click gives up).
    ObjectRef m_PortalPlace{};
    EditorView m_PortalOwner = 0;
    void PortalPick();
    // The header's picker (Dev.exe sub_4436E0 20): the Setup of this object
    // takes the next object of its class clicked (Escape, a right click or
    // the picker again gives up).
    ObjectRef m_PickSetup{};
    EditorView m_PickSetupView = 0;
    void SetupPick();
};

} // namespace vdev
