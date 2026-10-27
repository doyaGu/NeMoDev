#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/schematic/ElementRef.h"
#include "core/interface/InterfaceData.h"

namespace vdev {

class ScriptSession;

// Script header column width. Dev places Start at x=140 and clamps Script
// parameters to x>=180 (2*20+140); the header column itself is a library
// estimate, not recovered UI geometry.
constexpr float SCRIPT_HEADER_WIDTH = 140.0f;
// Height of a closed Script: one row with the open triangle and the name
// (Dev screenshot, 2D Text.cmo with "Topic - 2D Text" closed: ~20 px).
constexpr float CLOSED_SCRIPT_HEIGHT = 20.0f;
// Smallest open Script band (library choice; header name and owner rows).
constexpr float MIN_OPEN_SCRIPT_HEIGHT = 40.0f;

struct ScriptItem {
    ElementRef ref;
    CK_ID id = 0;
    std::string name;
    std::string ownerName;
    CK_CLASSID ownerClass = 0; // header icon
    bool isScript = true; // false for a non-Script root (Building Block chunk)
    bool open = true;     // ScriptSession::IsOpen (view state)
    bool hidden = false;  // UI flag 0x20000
    bool initialConditions = false; // the current scene holds an initial value (header "IC")
    CKDWORD color = 0;
    int priority = 0; // the Script's (the header's "Priority = %d" with Show Priorities)
    void *snapshot = nullptr; // header SDK bitmap, borrowed from the session
    std::shared_ptr<void> snapshotOwner;
    Rect header;          // header column
    Rect bounds;          // whole Script area in Script-local pixels
    float contentHeight = 0.0f; // the height its contents need (open Scripts)
};

struct BehaviorItem {
    ElementRef ref;
    CK_ID id = 0;
    CK_ID parent = 0;
    int depth = 0;
    std::string name;
    std::string prototype;
    bool graph = false;       // CKBehavior has a graph body
    bool expandable = false;  // graph without the Building Block flag
    bool folded = true;
    bool buildingBlock = false;
    bool usingTarget = false;
    bool missingRuntime = false;
    int priority = 0;
    bool settings = false; // Building Block with settings (its own local parameters): Dev's "s" mark
    bool variable = false; // CKBEHAVIOR_VARIABLE* I/O or parameters: Dev's "v" mark
    // Dev's other marks (documentation "Behavior Building Block"): "C" a
    // custom dialog box (CKBEHAVIOR_CUSTOMEDITDIALOG / CUSTOMSETTINGSEDITDIALOG),
    // the Sender and Receiver icons (CKBEHAVIOR_MESSAGESENDER / RECEIVER).
    bool customDialog = false, sender = false, receiver = false;
    // Building Block state shown with Dev's preference colors.
    bool prototypeMissing = false; // no registered prototype for its GUID
    bool obsolete = false;         // prototype flagged CK_BEHAVIORPROTOTYPE_OBSOLETE
    bool outdated = false;         // behavior version differs from the declaration
    bool breakpoint = false;       // Dev breakpoint (InterfaceChunk ExtraData)
    Rect box;                 // content bounds in Script-local pixels
};

struct PinItem {
    ElementRef ref;
    CK_ID owner = 0; // behavior or operation
    PinKind kind = PinKind::None;
    int index = 0;
    LinkEndpoint endpoint;
    Point center;
    std::string name;
    std::string typeName;
    bool onOperation = false;
    bool onScript = false;
};

struct LinkItem {
    ElementRef ref;
    CK_ID owner = 0;
    int slot = -1;
    bool behaviorLink = false;
    bool highlight = false;
    bool operationLink = false; // an end on a parameter operation
    bool resolved = false;
    // Dev hides a link to a disabled input (CK_PARAMETERIN_DISABLED, Dev 0x4DC615).
    bool hidden = false;
    std::string conversion; // name of the undisplayed conversion operation the link passes through
    // From a collapsed local straight (no control point) to a parameter input:
    // hidden with such locals by Dev's Show/Hide Local Parameters.
    bool fromDirectLocal = false;
    int delay = 0;
    bool breakpoint = false;
    LinkEndpoint start;
    LinkEndpoint end;
    std::vector<Point> path; // start, control points, end
};

struct OperationItem {
    ElementRef ref;
    CK_ID owner = 0;
    CK_ID id = 0;
    std::string name;
    Rect bounds;
};

// Dev's display mode of a local parameter or shortcut (Dev.exe 0x4E1470,
// as ParameterDisplayGeometry): style & 0x3E00 must be exactly NAME, VALUE
// or NAME+VALUE; anything else (0, 0x400, 0x800, combinations) is drawn
// collapsed, as a small bar.
inline int ParamDisplayMode(int style) {
    const int mode = style & 0x3E00;
    return mode == PARAM_STYLE_NAME || mode == PARAM_STYLE_VALUE || mode == PARAM_STYLE_NAMEVALUE ? mode
                                                                                                 : PARAM_STYLE_COLLAPSED;
}

struct ParamItem {
    ElementRef ref;
    CK_ID owner = 0;
    int slot = -1;
    bool shortcut = false;
    bool shortcutSource = false; // a local some shortcut of the Script displays (Dev's black icon)
    // The value holder of a disabled input: Dev hides it (Dev 0x4D15C0, 0x4DC615).
    bool hidden = false;
    // Collapsed and linked straight (no control point) to a parameter input:
    // what Dev's Show/Hide Local Parameters hides (Dev documentation).
    bool directInput = false;
    CK_ID parameter = 0; // local id, or shortcut source id
    ParameterStyle style = PARAM_STYLE_NAME;
    bool breakpoint = false;
    std::string name;
    std::string value;
    std::string typeName;
    Rect bounds;  // layout reservation (ParameterDisplayGeometry::EstimateBounds)
    Rect display; // drawn and picked area; VALUE/NAMEVALUE shrink to measured text
    Point center;
};

// Dev's Show/Hide Local Parameters hides collapsed locals linked straight to
// a parameter input, and those links (Dev documentation, Schematic toolbar).
inline bool IsParamShown(const ParamItem &param, bool showLocalParams) {
    return !param.hidden && (showLocalParams || !param.directInput);
}
inline bool IsLinkShown(const LinkItem &link, bool showLocalParams) {
    return !link.hidden && (showLocalParams || !link.fromDirectLocal);
}

struct CommentItem {
    ElementRef ref;
    CK_ID owner = 0;
    int slot = -1;
    std::string text;
    bool collapsed = false;
    Rect bounds;
};

struct StartItem {
    ElementRef ref;
    bool present = false;
    Rect bounds;
    Point center;
};

// Text width in Script-local pixels at zoom 1; optional.
using TextMeasure = std::function<float(const std::string &utf8)>;

// Drawable and hit-testable view of one Script, rebuilt whenever the
// session revision or CK topology changes. Coordinates are Script-local
// pixels as stored in InterfaceData (Behavior rects are flattened; parameter
// cells are converted with their owning window origin). Strings are UTF-8.
class SchematicModel {
public:
    void Build(const ScriptSession &session, CKContext *context, const TextMeasure &measure = TextMeasure());

    ScriptItem script;
    StartItem start;
    std::vector<BehaviorItem> behaviors;
    std::vector<PinItem> pins;
    std::vector<LinkItem> links;
    std::vector<OperationItem> operations;
    std::vector<ParamItem> params;
    std::vector<CommentItem> comments;
    Rect contentBounds;
    int unresolvedEndpoints = 0;

    const BehaviorItem *FindBehavior(CK_ID id) const;
    const PinItem *FindPin(const LinkEndpoint &endpoint) const;
    // Graphs whose contents are displayed (open Script root, expanded graphs).
    bool IsOpenGraph(CK_ID id) const {
        auto found = m_OpenGraphs.find(id);
        return found != m_OpenGraphs.end() && found->second;
    }
    // Deepest open graph containing a Script-local point.
    CK_ID GraphAt(const Point &point) const;

private:
    std::map<CK_ID, size_t> m_BehaviorIndex;
    std::map<CK_ID, bool> m_OpenGraphs;
};

} // namespace vdev
