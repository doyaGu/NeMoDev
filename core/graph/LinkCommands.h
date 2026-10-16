#pragma once

#include <string>
#include <utility>
#include <vector>

#include "CKAll.h"
#include "core/document/Edit.h" // LinkEndpoint (InterfaceData)
#include "core/document/EditResult.h"
#include "core/schematic/ElementRef.h"

namespace vdev {

class Document;

// Dev's "Link from a multiselection" (link.html): the links the Schematic
// makes from several selected pins at once.

// Each pair linked, one undo step: behavior links (with the link delay and
// Auto Loop Delay preferences) or parameter links. Only allowed links are
// made: how many were.
int LinkPairs(Document &document, CK_ID script, const std::vector<std::pair<LinkEndpoint, LinkEndpoint>> &pairs,
              bool behaviorLinks, int delay = 0, bool autoLoopDelay = true);

// A source dropped on a behavior: its pin, kind, name and type (the new
// pin's).
struct LinkSource {
    LinkEndpoint endpoint;
    PinKind kind = PinKind::None;
    std::string name;
    CKGUID type;
};

// Sources dropped on a behavior graph or a variable Building Block: each gets
// a new pin there (a behavior input for a behavior output or Start, else an
// input parameter), linked to it; one undo step. Stops at a Building Block
// that cannot create the kind of pin (kindRefused) or a pin it cannot make
// (failed: why). Returns the pins and links made.
int LinkToNewPins(Document &document, CK_ID script, const std::vector<LinkSource> &sources, CK_ID target,
                  bool behaviorLinks, int delay, bool autoLoopDelay, bool *kindRefused = nullptr,
                  std::string *failed = nullptr);

} // namespace vdev
