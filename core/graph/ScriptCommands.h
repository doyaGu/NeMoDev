#pragma once

#include <map>
#include <vector>

#include "CKAll.h"
#include "core/document/EditResult.h"
#include "core/interface/InterfaceData.h"

namespace vdev {

class Document;

// Dev "Delete" on a Script header (after confirmation): the Script leaves its
// owner and is soft-deleted (graveyard, never saved); undo gives it back to
// the owner (at the end of the owner's Script list).
EditResult DeleteScript(Document &document, CK_ID script);

// Dev Copy on a Script header, then Ctrl+V in the Level Manager: copies of
// the Scripts are given to owner. Each copy's display is the original's
// current display with the copied objects' ids (installed as its
// InterfaceChunk). Refused when the owner's class does not accept a Script.
// markCopies: a copy on its original's owner is named "<name>.Copy" (Dev's
// Level Manager paste, Dev.exe sub_5757C0 1486).
EditResult PasteScripts(Document &document, const std::vector<CK_ID> &scripts, CK_ID owner, bool markCopies = false);
// Copies on each owner (Ctrl+V on a Level Manager selection), as one undo
// step; each owner's result, in order.
std::vector<EditResult> PasteScriptsOn(Document &document, const std::vector<CK_ID> &scripts,
                                       const std::vector<CK_ID> &owners, bool markCopies = false);

// A Script copied with CKContext::CopyObjects (by PasteScripts, or with its
// owner): the original's current display with the copy's ids becomes the
// copy's InterfaceChunk (CopyObjects may give the copy the original's chunk).
void CopyScriptDisplay(Document &document, CKBehavior *original, CKBehavior *copy);

// Dev "Load Snapshot for Script": the header's snapshot bitmap (an SDK bitmap
// the document keeps, see Document::KeepBitmap; null removes it).
// Display data only.
bool SetScriptSnapshot(Document &document, CK_ID script, void *bitmap);

// Replaces ids of a Script's display data through map (records, links and
// their endpoints, operations, shortcut sources); ids not in map stay.
void RemapInterfaceData(InterfaceData &data, const std::map<CK_ID, CK_ID> &map);

} // namespace vdev
