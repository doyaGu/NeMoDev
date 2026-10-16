#pragma once

#include <string>
#include <utility>
#include <vector>

#include "CKAll.h"

#include "core/document/Document.h"

namespace vdev {

// Dev's parameter UIs: the dialogs Managers\CK2UI.dll makes for a type (its
// UI creators, chosen by sub_24E52360: the flags, enum and structure types
// first, then the type's GUID, then the type it derives from) with their
// dialog template.
enum class ParameterUI {
    None,         // no UI in CK2UI: the value's text
    Number,       // 2001: Float, Percentage, Integer (one centered edit)
    Angle,        // 154: a VIAngleCtrl (Turn, Degree, the dial)
    Time,         // 2002: Min, S, Ms
    Key,          // 157: "Click Here and Push a Key"
    Bool,         // 138: a check box
    String,       // 135: an edit
    Vector,       // 2004: X, Y, Z
    Quaternion,   // 2008: X, Y, Z, W
    EulerAngles,  // 154: three VIAngleCtrl
    Vector2D,     // 134: X, Y
    Matrix,       // 159: Right, Up, Dir, Position
    Color,        // 2005: the color's button, R, G, B, A
    Box,          // 143: Min and Max X, Y, Z
    Rect,         // 2003: Top,Left and Bottom,Right X, Y
    Object,       // 2011: the objects of the class (and its class filter)
    Script,       // 158: the owner, its class, its Scripts
    Message,      // 140: the message types
    Attribute,    // 155: the attribute types and their category
    Animation,    // 2006: the animations
    Synchro,      // 148: the synchro objects and their number of users
    Enum,         // 152: the enumeration's values
    Struct,       // 151: the members, each with its own UI
    Flags,        // 2007: a check box per flag
    Dependencies, // 156: Copy / Delete / Save / Replace dependencies
    ObjectArray,  // 2010: a list of objects
    Curve2D,      // 102: the 2D curve editor
    LodOptions    // 2012: the LOD Manager's LOD Options (MeshModifiers' CKPGUID_LODOPTIONS)
};

struct ParameterUIInfo {
    ParameterUI ui = ParameterUI::None;
    int dialog = 0;         // CK2UI.dll's dialog template
    int width = 0;          // its size in dialog units
    int height = 0;
    int pick = 0;           // CreateParameterDialog's pick modes (Dev's target button)
    CK_CLASSID pickClass = 0;
    bool expand = false;    // the wrapper's second button (an Object's class filter)
};

ParameterUIInfo ParameterUIOf(CKParameterManager *manager, CKParameterType type);

// The lists of CK2UI's combo boxes (CB_ADDSTRING into sorted combos: by name,
// "NULL" (0) sorted with them).
using ObjectChoices = std::vector<std::pair<CK_ID, std::string>>;
// The lists take the composition's live objects (Document::LiveObjects): a
// deleted object is not offered.
// An Object UI (sub_24E6CE00): the objects of a class (and of the classes
// derived from it), not private, named, but the level scene; filter: the
// lower case text their lower case name must hold.
ObjectChoices ObjectsOfClass(const Document &document, CK_CLASSID cid, bool derived, const std::string &filter);
// A Script UI (sub_24E71CA0): the behavioral objects of a class with
// Scripts, but the level scene.
ObjectChoices ScriptOwners(const Document &document, CK_CLASSID cid);
// Its Scripts (sub_24E71A90): the owner's Scripts, or all Scripts and
// "NULL" without one; interface and private Scripts are left out.
ObjectChoices ScriptsOf(const Document &document, CK_ID owner);
// The classes derived from one (CK2UI lists class ids below 56), by name.
std::vector<std::pair<CK_CLASSID, std::string>> DerivedClasses(CK_CLASSID cid);

// The Dependencies UI (dialog 156): the operation of a dependencies type
// (CK2UI sub_24E6A330 ... sub_24E6A790: Copy, Delete, Save, Replace), 0 for
// another type.
CK_DEPENDENCIES_OPMODE DependenciesModeOf(CKParameterManager *manager, CKParameterType type);
// Its tree (sub_24E6ACB0): from CKCID_OBJECT, the classes with options in
// the mode, each under its nearest listed ancestor, brothers by name
// (TVI_SORT); depth-first.
struct DependencyClass {
    CK_CLASSID cid = 0;
    std::string name;
    int depth = 0;
};
std::vector<DependencyClass> DependencyClasses(CK_DEPENDENCIES_OPMODE mode);
// A class's options in the mode (its check boxes, sub_24E6B350: bit i).
std::vector<std::string> DependencyOptions(CK_CLASSID cid, CK_DEPENDENCIES_OPMODE mode);

// A value held by pointer (dependencies, an object array) set from one in
// the editor's memory: the type's copy function copies it into the
// parameter's own, in CK's heap (the containers' inline code must not grow
// or free what CK allocated). False when the parameter holds no pointer.
bool SetHeldValue(CKParameter *parameter, void *value);

} // namespace vdev
