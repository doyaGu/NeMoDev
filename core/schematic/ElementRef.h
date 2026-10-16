#pragma once

#include <tuple>

#include "CKAll.h"

namespace vdev {

enum class ElementKind {
    None,
    Script,       // Script header / body background
    Behavior,     // object = behavior
    Pin,          // object = pin owner (behavior or operation), pin/index
    Link,         // owner = graph storing the link, index = link slot
    ControlPoint, // as Link, sub = point index
    Operation,    // owner = graph, object = operation
    LocalParam,   // owner = graph, index = localParams slot
    Shortcut,     // owner = graph, index = sharedParams slot
    Comment,      // owner = graph, index = comments slot
    Start,        // owner = Script
};

enum class PinKind { None, BehaviorIn, BehaviorOut, ParamIn, ParamOut, Target };

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
