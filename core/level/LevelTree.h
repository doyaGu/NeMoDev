#pragma once

// What the Level Manager lists and the Schematic's Script sorts, apart from
// drawing them (ui/panels/LevelManager.cpp, ui/schematic/SchematicMenus.cpp).

#include <map>
#include <string>
#include <vector>

#include "CKAll.h"

namespace vdev {

class Document;

// Dev's names of the Global folders for common classes; other classes use
// the CK class name.
std::string LevelFolderName(CK_CLASSID cid);

// Case-insensitive name order, as Dev's Level Manager lists objects and Scripts.
bool LevelNameLess(CKObject *a, CKObject *b);

// The objects of a scene as Dev lists them: class folders (Global /
// Entities), the places (by name), and per place the 3D entities whose
// nearest place ancestor it is. Left out: the level, scenes, behaviors,
// object animations, private objects, dynamic ones unless showDynamic
// (General Preferences "Show Dynamic Objects"), and a character's
// animations and body parts (listed under it).
using LevelFolders = std::map<std::string, std::vector<CKSceneObject *>>;
struct SceneContents {
    LevelFolders global;
    std::vector<CKBeObject *> places;
    std::map<CK_ID, LevelFolders> placed;
};
SceneContents ListSceneContents(CKContext *context, CKScene *scene, CKLevel *level, bool showDynamic);

// Dev's Script header sorts (documentation): the stack's order by name, by
// the owner's name, by color, by the owner's class, or the order inverted.
enum class ScriptSort { Name, Owner, Color, ClassId, Invert };
std::vector<CK_ID> SortScripts(Document &document, std::vector<CK_ID> order, ScriptSort sort);

} // namespace vdev
