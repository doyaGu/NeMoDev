#pragma once

#include <string>
#include <vector>

#include "CKAll.h"
#include "core/document/EditResult.h"
#include "core/schematic/Geometry.h"

namespace vdev {

class Document;

// Dev "Save As..." (Alt+S): behaviors - Building Blocks, behavior graphs or
// Scripts - in a Virtools behavior file (.nms), with their dependencies. A
// Script whose display was edited gets its current InterfaceChunk first
// (as saving the composition would). The composition is not changed.
// hideRepresentation: Dev's "Hide script representation in schematic view"
// (Dev.exe dialog 709, sub_514180): the behaviors' InterfaceChunks in the
// file are flagged hidden (UI flag 0x20000; a graph's own made for it);
// the composition's are left as they were.
// Writes beside the destination, reloads independently, then atomically
// publishes. Failure leaves an existing destination unchanged.
bool SaveBehaviorsAsNms(Document &document, const std::vector<CK_ID> &behaviors, const std::string &path,
                        std::string *error, bool hideRepresentation = false);

// Dev "Import Behavior Graph": the top-level behaviors of an .nms file are
// pasted into an editable graph at position (one undo step). Their display
// comes from their own InterfaceChunk, else it is reconstructed; graphs come
// in folded. Scripts in the file are refused.
EditResult ImportBehaviorGraph(Document &document, CK_ID script, CK_ID graph,
                               const std::string &path, const Point &position);

} // namespace vdev
