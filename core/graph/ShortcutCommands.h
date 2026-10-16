#pragma once

#include <vector>

#include "CKAll.h"
#include "core/document/EditResult.h"

namespace vdev {

class Document;

// Dev "Set Shortcut Source", in one Script: its shortcuts of `from` show `to`
// instead, and the inputs they feed (outputs that write them) follow. Types
// must fit every fed input. Refused when the Script has no such shortcut.
EditResult RetargetShortcuts(Document &document, CK_ID script, CK_ID from, CK_ID to);

// Dev "Convert to Shortcut Source", in one Script: every other local with the
// same name, type and value as `source` becomes a shortcut of it at the same
// place; its links start from the shortcut, every input it fed takes
// `source`, and the local is removed. Locals written by an output are kept.
// Refused when the Script has no such local.
EditResult ShortcutizeLocals(Document &document, CK_ID script, CK_ID source);

// The same in every Script of the composition, as one undo step.
// RetargetShortcutsInAllScripts is all or nothing: a Script that refuses
// takes the others back (Refused, its message); the Scripts without such a
// shortcut are left alone (NoChange when none has one). The message counts
// the Scripts changed.
EditResult RetargetShortcutsInAllScripts(Document &document, CK_ID from, CK_ID to);
EditResult ShortcutizeLocalsInAllScripts(Document &document, CK_ID source);
// Only in these Scripts (documentation "Local Parameter": "keep Ctrl key
// down and right click on the source ... in the selected scripts").
EditResult ShortcutizeLocalsInScripts(Document &document, const std::vector<CK_ID> &scripts, CK_ID source);

} // namespace vdev
