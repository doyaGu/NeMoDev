#pragma once

#include <tuple>
#include "editor/Schematic.h"

#include "CKAll.h"

namespace vdev {

enum class ElementKind {
    None = EDITOR_SCHEMATIC_NONE,
    Script = EDITOR_SCHEMATIC_SCRIPT,       // Script header / body background
    Behavior = EDITOR_SCHEMATIC_BEHAVIOR,     // object = behavior
    Pin = EDITOR_SCHEMATIC_PIN,          // object = pin owner (behavior or operation), pin/index
    Link = EDITOR_SCHEMATIC_LINK,         // owner = graph storing the link, index = link slot
    ControlPoint = EDITOR_SCHEMATIC_CONTROL_POINT, // as Link, sub = point index
    Operation = EDITOR_SCHEMATIC_OPERATION,    // owner = graph, object = operation
    LocalParam = EDITOR_SCHEMATIC_LOCAL_PARAMETER,   // owner = graph, index = localParams slot
    Shortcut = EDITOR_SCHEMATIC_SHORTCUT,     // owner = graph, index = sharedParams slot
    Comment = EDITOR_SCHEMATIC_COMMENT,      // owner = graph, index = comments slot
    Start = EDITOR_SCHEMATIC_START,        // owner = Script
};

enum class PinKind {
    None = EDITOR_SCHEMATIC_NO_PIN,
    BehaviorIn = EDITOR_SCHEMATIC_BEHAVIOR_INPUT,
    BehaviorOut = EDITOR_SCHEMATIC_BEHAVIOR_OUTPUT,
    ParamIn = EDITOR_SCHEMATIC_PARAMETER_INPUT,
    ParamOut = EDITOR_SCHEMATIC_PARAMETER_OUTPUT,
    Target = EDITOR_SCHEMATIC_TARGET
};

// Identifies one Schematic element without pointers. Slots index the
// InterfaceData record tables and are only valid for one session revision;
// commands restore whole snapshots, so a revision change revalidates them.
struct ElementRef {
    ElementKind kind = ElementKind::None;
    CK_ID owner = 0;
    CK_ID object = 0;
    int index = -1;
    int sub = -1;
    PinKind pin = PinKind::None;

    bool IsValid() const { return kind != ElementKind::None; }
    auto Key() const { return std::make_tuple(static_cast<int>(kind), owner, object, index, sub, static_cast<int>(pin)); }
    bool operator==(const ElementRef &other) const { return Key() == other.Key(); }
    bool operator!=(const ElementRef &other) const { return !(*this == other); }
    bool operator<(const ElementRef &other) const { return Key() < other.Key(); }
};

} // namespace vdev
