#pragma once
#include <cstdint>

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
    int32_t pickClass = 0;
    bool expand = false;    // the wrapper's second button (an Object's class filter)
};

} // namespace vdev
