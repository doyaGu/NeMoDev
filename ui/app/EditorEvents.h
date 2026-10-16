#pragma once

#include <utility>
#include <variant>
#include <vector>

#include "core/schematic/ElementRef.h"

namespace vdev {

// What a window asks of another (EditorState::events): posted while a window
// draws, handled by EditorApp::DispatchEvents at the start of the next frame,
// before any window draws. A handler sets its window's pending state (what
// it does when it draws next); one posted while they run waits for the next
// frame. They name the document's objects: closing it drops them
// (DocumentSession).

// An element shown in the Schematic, in front (once no popup is open): its
// Script's stack made, the view moved to ref (the Script when invalid;
// focusBehavior, or its shown ancestor, when ref is not shown), ref
// selected.
struct ShowElement {
    CK_ID script = 0;
    ElementRef ref;
    CK_ID focusBehavior = 0;
};

// The object's Setup shown (SetupWindows::Open: that of its curve for a
// curve point, of its camera or light for a target), in front.
struct ShowObjectSetup {
    CK_ID object = 0;
};

// The object's row shown in the Level Manager, in front (once no popup is
// open): the tree opened to it, scrolled to it, highlighted (not selected).
struct ShowInLevelManager {
    CK_ID object = 0;
};

// Dev's Add Attribute dialog for the objects (none: it only creates types);
// more: opened with More>> (the Attribute Manager's Create Attribute).
struct OpenAddAttribute {
    std::vector<CK_ID> targets;
    bool more = false;
};

// The Level Manager's Select Delete Options for the objects (the 3D
// Layout's Delete).
struct ConfirmDeleteObjects {
    std::vector<CK_ID> objects;
};

// General Preferences at a page (0 Rendering ... 4 Snap & Align; -1 the
// last one shown).
struct ShowGeneralPreferences {
    int page = -1;
};

// The Schematic in front (once no popup is open).
struct FocusSchematic {};

// The Schematic Explorer shown, its text focused (the Schematic toolbar's
// Explorer and Find).
struct OpenSchematicExplorer {};

// Behavior Link Properties (the Schematic toolbar).
struct OpenLinkProperties {};

// The Schematic's stack made again as what it shows changed while the
// Scripts did not (filters, the order, hidden Scripts; once however many are
// posted): Document::ScriptsRevision tells the Scripts' changes.
struct RefreshScriptStack {};

using EditorEvent =
    std::variant<ShowElement, ShowObjectSetup, ShowInLevelManager, OpenAddAttribute, ConfirmDeleteObjects,
                 ShowGeneralPreferences, FocusSchematic, OpenSchematicExplorer, OpenLinkProperties,
                 RefreshScriptStack>;

class EditorEvents {
public:
    template <typename E> void Post(E event) { m_Events.emplace_back(std::move(event)); }
    // The events posted, in order; none are left.
    std::vector<EditorEvent> Take() {
        std::vector<EditorEvent> taken;
        taken.swap(m_Events);
        return taken;
    }

private:
    std::vector<EditorEvent> m_Events;
};

} // namespace vdev
