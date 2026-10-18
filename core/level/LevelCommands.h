#pragma once

#include <set>
#include <string>
#include <vector>

#include "CKAll.h"
#include "core/document/EditResult.h"

namespace vdev {

class Document;

// Level Manager commands (Dev language pack [Level View]: toolbar "Create
// Scene (C)", "Create Group (G)", "Create Place (P)", "Create Script (S)";
// popup "Send To Scene", "Send To Group", "Send to Place", "Remove From
// Place", "Load Script...", "Rename"). Each is one undo step;
// objects they create join the composition (saved) and go to the graveyard
// while undone.

// A new empty Script (Start input, graph) for an object.
EditResult CreateScript(Document &document, CK_ID owner, const std::string &name);

// Dev.exe sub_4C3510 (a Building Block dropped on an object in the 3D
// Layout): into the object's first Script not in skip (the Schematic's
// hidden ones), else a new "<object> Script"; right of what the Script
// holds, on its top row (the Start's row when empty). created: the block;
// script: its Script. Refused with CreateScript's or InsertBuildingBlock's
// reason (inserted: whether the Script was there, the block refused).
EditResult DropBuildingBlockOn(Document &document, CK_ID object, const CKGUID &guid, const std::set<CK_ID> &skip,
                               CK_ID *script = nullptr, bool *insertRefused = nullptr);
// A Building Block dropped on a Script (its Level Manager row, Dev.exe
// sub_56B590): where DropBuildingBlockOn puts it; InsertBuildingBlock's
// result.
EditResult DropBuildingBlockInto(Document &document, CK_ID script, const CKGUID &guid);
// A new empty Script for each object (Create Script on a selection), as one
// undo step; each one's result, in order.
std::vector<EditResult> CreateScriptsOn(Document &document, const std::vector<CK_ID> &owners);

// The Scripts of an .nms file (saved with Save As on Script headers) are
// given to owner, with their own InterfaceChunks; other loaded objects join
// the composition. Refused when the file holds no Script (the message then
// starts with NO_SCRIPT_IN_FILE).
EditResult LoadScripts(Document &document, const std::string &path, CK_ID owner);
constexpr const char *NO_SCRIPT_IN_FILE = "The file holds no Script";

// Scenes of the current level. A new scene takes the objects given.
EditResult CreateScene(Document &document, const std::string &name,
                       const std::vector<CK_ID> &objects);
// groupMembers: a group's members go with it (CKScene::AddObjectToScene's
// dependencies; Dev.exe sub_572AF0 asks "Add every Object in Group to
// Scene ?"). Undo takes out what came into the scene.
EditResult SendToScene(Document &document, const std::vector<CK_ID> &objects, CK_ID scene, bool groupMembers = true);
// Dev's Delete in a scene's section (sub_5734A0): the objects leave the
// scene (dependencies: a group's members too, "Remove every Object in Group
// from Scene ?"); undo puts back what left, with its flags and initial
// conditions.
EditResult RemoveFromScene(Document &document, const std::vector<CK_ID> &objects, CK_ID scene, bool dependencies);

// Groups (CKGroup, added to the level).
// scene: the scene the group goes into too (Dev.exe sub_576910: the current
// scene, when it is not the level's).
EditResult CreateGroup(Document &document, const std::string &name,
                       const std::vector<CK_ID> &objects, CK_ID scene = 0);
EditResult SendToGroup(Document &document, const std::vector<CK_ID> &objects, CK_ID group);
// Dev's Delete on a group's member rows in the level (sub_5734A0): they
// leave the group; undo puts each back in its place.
EditResult RemoveGroupMembers(Document &document, CK_ID group, const std::vector<CK_ID> &objects);
// The Character Setup's Add Body Part / Remove Body Part (Dev.exe sub_44F330
// 1216, 204): body parts of no character join it, its own leave it; undo
// takes back each (and gives back the root body part).
EditResult AddBodyParts(Document &document, CK_ID character, const std::vector<CK_ID> &parts);
EditResult RemoveBodyParts(Document &document, CK_ID character, const std::vector<CK_ID> &parts);
// Dev's Level Manager drops (sub_5761C0, CKMoveScript): the Scripts go to
// the object when its class is one they take (GetCompatibleClassID);
// refused: the names of the others. Undo gives them back to their owners.
EditResult MoveScripts(Document &document, const std::vector<CK_ID> &scripts, CK_ID target,
                       std::vector<std::string> *refused = nullptr);
// The 3D Layout's selection groups (Dev.exe sub_4B95C0, a name typed in the
// Selection Groups combo): a group of the objects selected, an interface
// object as Dev makes it (flags 0x23: private, not saved), in the level and
// the scene.
EditResult CreateSelectionGroup(Document &document, CK_ID scene, const std::string &name,
                                const std::vector<CK_ID> &objects);

// Places: 3D entities become children of the place (keeping their world
// position); "Remove From Place" gives them back their former parent (none).
EditResult CreatePlace(Document &document, const std::string &name,
                       const std::vector<CK_ID> &objects);
EditResult SendToPlace(Document &document, const std::vector<CK_ID> &objects, CK_ID place);
EditResult RemoveFromPlace(Document &document, const std::vector<CK_ID> &objects);

// "Create Array (R)": an empty CKDataArray of the level.
EditResult CreateArray(Document &document, const std::string &name);

// A new object of a class added to the level (CK2UI's Synchro UI makes the
// synchro object, critical section or state typed in, sub_24E732D0);
// created: its ID.
EditResult CreateNamedObject(Document &document, CK_CLASSID cid, const std::string &name);

// "Show Selected in 3D Layout (V)" / "Hide Selected in 3D Layout (H)"
// (CKObject::Show; the former show state comes back with undo).
EditResult ShowObjects(Document &document, const std::vector<CK_ID> &objects, bool show);
// The render objects shown as option says (CKSHOW, CKHIDE, CKHIERARCHICALHIDE:
// the Level Manager's Visible cell, Dev.exe sub_574C10), one undo step.
EditResult ShowObjectsAs(Document &document, const std::vector<CK_ID> &objects, CK_OBJECT_SHOWOPTION option);
// The Level Manager's Active Now cell (Dev.exe sub_574C10): the scene
// objects activated in the scene now (CKScene::Activate, their Scripts not
// reset) or deactivated (CKScene::DeActivate); the level and the scene
// itself left. Undo gives each its state back.
EditResult SetActiveNow(Document &document, const std::vector<CK_ID> &objects, CK_ID scene, bool active);
// The Level Manager's Priority cell (Dev.exe sub_569960): the objects' and
// Scripts' priority (CKBeObject / CKBehavior::SetPriority), one undo step.
EditResult SetPriorities(Document &document, const std::vector<CK_ID> &objects, int priority);

// "Set Save Options": textures and sprites (CK_BITMAP_SAVEOPTIONS) or sounds
// (CK_SOUND_SAVEOPTIONS take the same 0..3 codes: raw, external, ...).
EditResult SetSaveOptions(Document &document, const std::vector<CK_ID> &objects,
                          int option);
// The save option of an object with save options, else -1.
int SaveOptionOf(CKObject *object);

// "Add Attributes": attribute types (CKAttributeManager) the objects' class
// accepts; Setup removes them or edits their value text.
// each: every object without the attribute, by name (ANSI), and whether it
// takes it (Dev.exe sub_594BA0: "Attribute %s added to object %s", "Can't
// set Attribute %s on Object %s, please check attribute compatibility").
EditResult AddAttribute(Document &document, const std::vector<CK_ID> &objects,
                        CKAttributeType type, std::vector<std::pair<std::string, bool>> *each = nullptr);
EditResult RemoveAttribute(Document &document, CK_ID object, CKAttributeType type);
EditResult SetAttributeText(Document &document, CK_ID object, CKAttributeType type,
                            const std::string &text);
// Several attributes off an object (the Setup's Attribute page Delete, Dev.exe
// sub_44DC60), one attribute off several objects (the Attribute Manager's
// Remove Attribute from Objects, sub_55CA90): one undo step each.
EditResult RemoveAttributes(Document &document, CK_ID object, const std::vector<CKAttributeType> &types);
EditResult RemoveAttributeFromObjects(Document &document, const std::vector<CK_ID> &objects, CKAttributeType type);
// The Attribute page's Paste Attributes (sub_44D990): each attribute (by
// name) set on the object with the value of its parameter; one undo step.
EditResult PasteAttributes(Document &document, CK_ID object, const std::vector<std::string> &attributes,
                           const std::vector<CK_ID> &values);
// The Attribute Manager's Paste Value On Selected Items (Dev.exe sub_55C4B0):
// the value of source (a CKParameter, CopyValue) on the objects' attribute
// parameters of the same type, one undo step; the others are left, the
// result's message then says so.
EditResult PasteAttributeValue(Document &document, CK_ID source, const std::vector<CK_ID> &objects,
                               CKAttributeType type);
// An attribute dragged from the Attribute Manager onto objects (its
// documentation: from the attribute list, or with the value of an object's
// from the object list): the objects (not Scripts) without it take it
// (AddAttribute, each as there), then, with value (a parameter), every one
// takes its value (PasteAttributeValue); one undo step.
EditResult DropAttribute(Document &document, const std::vector<CK_ID> &objects, CKAttributeType type, CK_ID value,
                         std::vector<std::pair<std::string, bool>> *each = nullptr);

// Dev "Move Up (+)" / "Move Down (-)" of an object in a group's ordered
// list (CKGroup::MoveObjectUp/MoveObjectDown; Dev documentation, Level
// Manager shortcut menu "One Object In a Group").
EditResult MoveInGroup(Document &document, CK_ID group, CK_ID object, int step);

// "Save Objects as...", and an entity's "Save as...": the objects (with
// their dependencies) in a Virtools file (CKContext::Save); their edited
// Scripts with their current display. hideScripts: documentation "Level
// Manager Shortcut Menu", "Hide script representation in Schematic view"
// (their Scripts' display flagged hidden in the file only). The composition
// is not changed.
bool SaveObjectsAs(Document &document, const std::vector<CK_ID> &objects, const std::string &path,
                   std::string *error, bool hideScripts = false);

// "Delete": objects destroyed with CKContext::DestroyObjects, which clears
// the references other objects hold. Not undoable (callers clear the undo
// history, as Dev warns "Undo stack will be cleared"). Levels, scenes and
// behaviors are refused; Scripts are deleted from their header.
// dependencies: Dev's Delete Options (null: CK's default delete dependencies,
// which CKDependenciesContext also takes for null). The history is cleared
// once anything was destroyed (its steps name the objects; CK may give their
// ids to new objects).
EditResult DestroyLevelObjects(Document &document, const std::vector<CK_ID> &objects,
                               CKDependencies *dependencies = nullptr);

// The render objects' Render Priority (Hierarchy Manager), one step. Their
// values are kept and set back: an object state read back does not give a
// 3D entity its priority again (EditObjectsState would not undo it).
EditResult SetRenderPriority(Document &document, const std::vector<CK_ID> &objects, int priority);

// Any object of the composition (Level Manager F2).
EditResult RenameLevelObject(Document &document, CK_ID object, const std::string &name);

// Attribute Manager ("Create New Category", "Create New Attribute Type",
// "Remove All User Attributes"). Attribute types are not objects: these are
// not undone (the composition becomes modified); the types created are user
// types (CK_ATTRIBUT_USER, saved with the composition). created: the type.
EditResult CreateAttributeCategory(Document &document, const std::string &name);
EditResult CreateAttributeType(Document &document, const std::string &name, CKGUID parameter,
                               const std::string &category, CK_CLASSID compatible = CKCID_BEOBJECT);
// The history is cleared once anything was removed (objects lose these
// attributes).
EditResult RemoveUserAttributes(Document &document);
// The Attribute Manager's Rename and Delete (Dev.exe sub_55D410 /
// sub_55D480), for the types and categories that allow it
// (CK_ATTRIBUT_CAN_MODIFY / CK_ATTRIBUT_CAN_DELETE); not undone as above.
// Deleting a type takes it off every object (the history is cleared);
// deleting a category moves its types to no category.
EditResult RenameAttributeType(Document &document, CKAttributeType type, const std::string &name);
EditResult RenameAttributeCategory(Document &document, const std::string &category, const std::string &name);
EditResult DeleteAttributeType(Document &document, CKAttributeType type);
EditResult DeleteAttributeCategory(Document &document, const std::string &category);

// Curve Setup "Add Point" / "Insert Point" (a new CKCurvePoint after the
// control point at index after; -1 or the last: at the end) and "Delete
// Point" (the point leaves the level; undo gives it back).
EditResult AddCurvePoint(Document &document, CK_ID curve, int after);
EditResult RemoveCurvePoint(Document &document, CK_ID curve, int index);

// The 3D Layout's curve tools (Curve Setup Add / Insert / Edit / Delete
// Points; Dev.exe sub_49ACC0, sub_49CD00, sub_49B650, sub_49B400). Each is
// one undo step, and ends as Dev's sub_4AC040: the points 0.4 in the world,
// the curve's base at their center (the points staying where they are).
// A new control point "<curve>_Point%04d" (the count + 1) at a world
// position, at index (the count or more: at the end), linear as the curve
// type; it joins the level. created: the point.
EditResult AddCurvePointAt(Document &document, CK_ID curve, int index, const VxVector &position,
                           bool linear, const std::string &label);
// The curve's control points become points, in order; removed (one of them
// before) leaves the level (graveyard; undo gives it back).
EditResult EditCurvePoints(Document &document, CK_ID curve, const std::vector<CK_ID> &points,
                           CK_ID removed, const std::string &label);

// Hierarchy Manager drop: a 3D entity under a 3D entity (0: 3D Root, world
// position kept), a 2D entity under a 2D entity or a root (0 with
// background 1: 2D Background Root, 0: 2D Foreground Root, -1: unchanged).
EditResult SetHierarchyParent(Document &document, CK_ID object, CK_ID parent,
                              int background = -1);
// A character's body parts keep their hierarchy ("Cannot modify character
// hierarchy!", Dev.exe sub_565DD0).
// The same for several objects (dragged together), as one undo step; each
// one's result, in order. Entities of different depths are refused
// together (Dev.exe sub_565DD0: "Cannot move multiple entities with
// different depths!").
std::vector<EditResult> SetHierarchyParents(Document &document, const std::vector<CK_ID> &objects, CK_ID parent,
                                            int background = -1);

// Dev 3D Layout creation tools (language pack [3D Layout] "Create Camera (1)",
// ... "Create Portal (9)"; Dev.exe sub_4A9960 and the functions after it):
// the object is named "New Camera", "New Camera.0001", ... (NewObjectName),
// placed relative to the camera of reference (the 3D Layout's camera) and
// joins the level and the current scene. A portal is made around selection
// when it is a 3D entity.
enum class LayoutObject { Camera, Light, Frame3D, Curve, Grid, Frame2D, Material, Texture, Portal };
// camera: the 3D Layout's camera, which new objects are placed by (0: the
// current scene's starting camera, else the level scene's, else the first).
EditResult CreateLayoutObject(Document &document, LayoutObject kind, CK_ID selection = 0,
                              CK_ID camera = 0);
// options: 1 frames only, 2 the level (not the current scene), 4 portals only.
std::string NewObjectName(CKContext *context, CK_CLASSID cid, const std::string &base, int options);

// Dev 3D Layout "Duplicate" and Ctrl+C / Ctrl+V (Dev.exe sub_4B0780 2 and 3;
// sub_5BDD60): CKContext::CopyObjects with the copy dependencies of Dev's
// Copy Options dialog (null: CK's default ones), the copies named with
// ".Copy" appended. Curve points are not copied. The copies join the level
// and the scenes of their originals; undo takes every object the copy made
// out of them. copies: (original, copy) for each object copied.
EditResult DuplicateObjects(Document &document, const std::vector<CK_ID> &objects,
                            std::vector<std::pair<CK_ID, CK_ID>> *copies = nullptr,
                            CKDependencies *dependencies = nullptr);

// A material dropped on a face in the 3D Layout (Dev.exe sub_4C0A30): the
// mesh's faces of the face's material take the new one
// (CKMesh::ReplaceMaterial). One undo step (the faces and material channels
// that had the old material get it back).
EditResult ReplaceMeshMaterial(Document &document, CK_ID mesh, CK_ID from, CK_ID to);

// Level / Scene Setup (language pack [Scene Setup]: "Colors", "Ambient",
// "Fog", "Background", "Use settings at Scene Start", "Background Image",
// "Starting Camera"; [Preferences] <FogType>).
struct SceneEnvironment {
    CKDWORD ambient = 0, fogColor = 0, background = 0;
    int fogMode = 0; // VXFOG_MODE
    float fogStart = 0.0f, fogEnd = 0.0f, fogDensity = 0.0f;
    CK_ID backgroundTexture = 0, startingCamera = 0;
    bool useAtStart = false;
};
SceneEnvironment ReadSceneEnvironment(CKScene *scene);
void ApplySceneEnvironment(CKContext *context, CKScene *scene, const SceneEnvironment &environment);
// Records the change from before to the scene's current settings (one undo
// step; refused when nothing changed).
EditResult CommitSceneEnvironment(Document &document, CK_ID scene,
                                  const SceneEnvironment &before, const std::string &label);
// Cancel a live environment edit without adding a history step. An
// incomplete restoration invalidates the history and marks the document.
bool CancelSceneEnvironment(Document &document, CK_ID scene, const SceneEnvironment &before);
EditResult SetSceneEnvironment(Document &document, CK_ID scene,
                               const SceneEnvironment &environment, const std::string &label);

// Place Setup "Portals and other Places": the places a place sees and the
// portal each is seen through (0: none, always visible). They are not part
// of the place's object state (CKSaveObjectState leaves them out), so a
// change is a command holding the whole list before and after.
struct PlaceLink {
    CK_ID place = 0, portal = 0;
};
std::vector<PlaceLink> PlaceLinksOf(CKPlace *place);
EditResult SetPlaceLinks(Document &document, CK_ID place, const std::vector<PlaceLink> &links,
                         const std::string &label);
// The Place Setup's Automatic Portal (Dev.exe sub_471A60, with the place
// picked): a portal "Portal <place>-<other>" on the places' common box
// (CKPlace::ComputeBestFitBBox; none: where the 3D Layout makes one), the
// other place seen through it. One undo step; created: the portal.
EditResult CreateAutomaticPortal(Document &document, CK_ID place, CK_ID other);
// The 3D Layout's Automatic Portals (documentation "Automatic Portals"):
// for each two places of the level that touch (ComputeBestFitBBox finds
// their shared vertices) and have no portal yet, a portal "Portal A-B"
// (CreateAutomaticPortal); one undo step. Refused when none touch.
EditResult CreateAutomaticPortals(Document &document);

// Sprite Text font settings stored by Ballanced: face, size, weight, italic,
// underline. Positive sizes are points; negative sizes are pixel heights.
struct SpriteTextFont {
    std::string face;
    int size = 12, weight = 400;
    bool italic = false, underline = false;
};
bool ReadSpriteTextFont(CKSpriteText *text, SpriteTextFont *font);
bool ReadSpriteTextFont(CKStateChunk *state, SpriteTextFont *font);

} // namespace vdev
