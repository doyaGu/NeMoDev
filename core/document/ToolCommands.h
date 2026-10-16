#pragma once

#include <string>
#include <vector>

#include "CKAll.h"
#include "core/document/Document.h"
#include "core/document/EditResult.h"

namespace vdev {

class Document;

// Dev's Resources, Editors and Options menu tools that are not Schematic
// edits (Dev.exe and the language pack [Resources], [Check Objects],
// [Plugin Manager], [Profiler]).

// ------------------------------------------------------------ Data Resources
// A Data Resource is a file "<name>.rsc" holding "Virtools RSC File 1.0" and
// the folder "<name>" beside it. Every folder of the resource has a hidden
// "_marker_" file, "<class id>\n<class name>\n" (older resources:
// "_nemo.dir"), telling the class of the files it holds.
struct DataResourceFolder {
    std::string name;
    std::string path;      // absolute
    CK_CLASSID cid = 0;    // 0: no marker
    std::vector<DataResourceFolder> folders;
    std::vector<std::string> files; // names, sorted
};

struct DataResource {
    std::string file;          // the .rsc file
    DataResourceFolder root;   // the folder beside it
};

// Create New Data Resource (Dev.exe sub_57AC20): the .rsc file, its folder
// and the folders Dev makes, with their markers: 2D Sprites (28), Behavior
// Graphs (8), Characters (40) with Animations (16), 3D Entities (33) with
// Animations (16), 3D Sprites (37), Textures (31) and Sounds (24).
bool CreateDataResource(const std::string &file, std::string *error);
// Reads the folder tree of a .rsc file (refused unless the file starts with
// "Virtools RSC File").
bool ReadDataResource(const std::string &file, DataResource &resource, std::string *error);
// The class a folder's marker names, else 0 (Dev.exe sub_563570).
// A folder's files leave out the behavior files' descriptions (.dsc).
CK_CLASSID DataResourceFolderClass(const std::string &folder);
// Opening a Data Resource adds its folders to the CK path manager (Dev.exe
// sub_561750): sound folders (24, 25, 26) to the sound paths, sprite,
// texture and 3D sprite folders (28, 31, 37) to the bitmap paths, the other
// marked folders to the data paths. Returns the number of paths added.
int AddDataResourcePaths(CKContext *context, const DataResource &resource);

// ------------------------------------------------------- Behavior Information
// A behavior file's description (Dev's Behavior Information, Dev.exe
// sub_581A40, sub_581CB0, sub_581D30): beside the .nms, its name without
// the last 3 letters and "dsc"; the class the behavior applies to (int32)
// and the version (float, -1 without one), then the behavior's name and
// the author each on a line, the description the rest of the file (Dev
// writes a newline after it and drops the last byte when reading; its
// lines end with "\r\n", here with "\n"). Texts ANSI.
struct BehaviorDescription {
    CK_CLASSID cid = 0;
    float version = -1.0f;
    std::string name, author, description;
};
std::string BehaviorDescriptionPath(const std::string &nms);
bool ReadBehaviorDescription(const std::string &nms, BehaviorDescription &description);
bool WriteBehaviorDescription(const std::string &nms, const BehaviorDescription &description);

// A file of a Data Resource folder of class cid loaded into the composition,
// as Dev does when one is dropped on the level (Dev.exe sub_56B380):
// textures (sub_56BE30), 2D sprites (sub_56BFB0), 3D sprites with their
// texture and material (sub_56C0E0), wave or MIDI sounds (sub_56C2C0), 3D
// entities and characters (LoadNmo, sub_56BC90), animations onto a
// character (CKContext::LoadAnimationOnCharacter), Scripts of behavior
// graph files onto an object (LoadScripts, one undo step). The other loads
// are not undoable; their objects join the level and the composition.
// duplicates: the Duplicate Names Found choices of an imported file.
EditResult LoadResourceFile(Document &document, const std::string &path, CK_CLASSID cid,
                            CK_ID target, std::vector<CK_ID> *loaded = nullptr,
                            const std::vector<DuplicateName> *duplicates = nullptr);
// Whether LoadResourceFile imports a file of a folder of class cid (Dev
// loads it with CK_LOAD_DODIALOG, sub_56BC90): 3D entities, characters and
// the classes without a load of their own.
bool ResourceFileIsImported(CK_CLASSID cid);

// A Data Resources file dropped onto a Level Manager row (Dev.exe
// sub_56B120, sub_56B380): row is the row's object (a Script, an object,
// the level, a scene; 0 for a folder), above the objects of the rows it is
// under (the nearest first), scene the scene of the scene section it is in
// (0 in the level's).
struct LevelDropTarget {
    CK_ID row = 0;
    std::vector<CK_ID> above;
    CK_ID scene = 0;
};
// What Dev does with the file, by the class of its folder:
// - behavior files (and files of folders without a class; sub_56BAB0): on
//   an object (or a Script: its owner) the file's Scripts are given to it
//   (LoadScripts); a file of graphs and Building Blocks goes into the Script
//   dropped on, else the object's first Script with a graph, else a new
//   Script (sub_575330, sub_56B590, sub_5714B0; ImportBehaviorGraph). Refused
//   on folders (1519), objects that are not entities (1520), Scripts whose
//   graph does not load (1517);
// - animations (sub_56C430): onto the character the row is or is under
//   (LoadAnimationOnCharacter); onto another 3D entity its object animations
//   take it (named "<entity> Animation" when unnamed); else the animations
//   join the level;
// - textures, 2D and 3D sprites, sounds (sub_56B510): created, in the level
//   and in the scene of a scene section;
// - 3D entities and characters (sub_56BC90, sub_56E210): imported; on a
//   scene row the places and behavioral objects join that scene, on a place
//   row the 3D entities become its children, on a group row the places and
//   behavioral objects join the group, and (place and group rows) the scene
//   objects join the scene of a scene section.
// Not undoable but for the Scripts and graphs (one undo step).
EditResult DropResourceFile(Document &document, const std::string &path, CK_CLASSID cid,
                            const LevelDropTarget &target, const std::vector<DuplicateName> *duplicates = nullptr);

// ------------------------------------------------------ Unused Object Explorer
// The objects of CKContext::CKFillObjectsUnused (walked again here: CK's
// needs a player render context) as Dev lists them (Dev.exe sub_58C870):
// meshes, materials, textures, animations, object animations and curve
// points that nothing uses, private objects left out.
struct UnusedObject {
    CK_ID id = 0;
    CK_CLASSID cid = 0;
    std::string name; // ANSI
};
// None when the walk cannot be done (error tells why): what it does not
// reach would be listed, all of it unused, and deleting it is not undone.
std::vector<UnusedObject> UnusedObjects(Document &document, std::string *error = nullptr);

// ------------------------------------------------- Building Block Version Test
// Dev.exe sub_536C70: every Building Block of the composition whose
// prototype changed (another version, or other input, output or parameter
// counts where the behavior does not vary them), every obsolete one, and
// the Building Blocks without a prototype.
struct VersionTestResult {
    std::string report;                            // Dev's text, ANSI
    std::vector<std::pair<CK_ID, CK_ID>> changed;  // (Script, behavior) to replace
    int obsolete = 0;
    int withoutPrototype = 0;
};
VersionTestResult TestBuildingBlockVersions(Document &document);

// The behavioral objects the behavior manager processes (Profiler details;
// in the core: CKBehaviorManager::GetObject is exported as GetObjectA, which
// a UNICODE build of the SDK headers cannot name).
std::vector<CK_ID> BehavioralObjects(CKContext *context);

// ---------------------------------------------------------------- Plugins
struct PluginRow {
    std::string name;        // CKPluginInfo summary
    std::string description;
    std::string dll;         // file name without extension
    std::string author;
    int version = 0;
    int active = -1;         // 1 active, 0 inactive, -1 none shown
    int managerIndex = -1;   // managers that can be switched: index in CKContext::GetManager
    int dllIndex = -1;       // CKPluginManager::GetPluginDllInfo (Temporary Unload, Reload)
};
struct PluginCategory {
    std::string name;
    std::vector<PluginRow> plugins;
};
// The categories in Dev's order (Bitmap Readers, Movie Readers, Sound
// Readers, Model Readers, Render Engines, Managers, BuildingBlocks,
// Extensions; Dev.exe sub_5A1290).
std::vector<PluginCategory> InstalledPlugins(CKContext *context);

} // namespace vdev
