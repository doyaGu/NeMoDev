#pragma once

#include <string>
#include <vector>

#include "core/document/EditResult.h"

namespace vdev {

class Document;

// Dev "Replacing BBs and BGs" (Dev documentation, replace_behavior.html).
// "The interface tries to keep all parameters and event links": a pin of
// the replaced behavior goes to the new pin of the same name (else of the
// same slot) when the types are compatible (an input: the old input type
// accepted by the new one; an output: the new type accepted where the old
// was). Behavior links keep their link object and delay. A parameter link
// that cannot be kept is removed, with its local value holder. The new
// behavior's unfed inputs get value locals (Building Block defaults; graphs:
// the type's null value). The new behavior takes the old one's place,
// priority and, when it can, its Target.

// The Building Block replaced by a new instance of prototype (the same
// prototype updates it to the latest version). A renamed Building Block
// keeps its name.
EditResult ReplaceBuildingBlock(Document &document, CK_ID script, CK_ID behavior,
                                const CKGUID &prototype);

// "Replace with existing Graph": a copy of graph `source` (any Script of
// the composition) replaces graph `behavior` (one undo step).
EditResult ReplaceBehaviorGraph(Document &document, CK_ID script, CK_ID behavior,
                                CK_ID source);

// "Replace with File Graph": the behavior graph of an .nms file replaces it.
EditResult ReplaceWithFileGraph(Document &document, CK_ID script, CK_ID behavior,
                                const std::string &path);

// "Apply to All" / "Replace All": the behaviors of the composition with the
// same name and kind (Building Block or graph) as behavior, by Script.
std::vector<std::pair<CK_ID, CK_ID>> SameNamedBehaviors(Document &document, CK_ID behavior);

// Each target (Script, behavior) replaced as above, as one undo step (Dev's
// Apply to All / Replace All); each one's result, in order: one refused
// leaves the others. A graph is not replaced by itself (NoChange).
std::vector<EditResult> ReplaceBuildingBlocks(Document &document, const std::vector<std::pair<CK_ID, CK_ID>> &targets,
                                              const CKGUID &prototype);
std::vector<EditResult> ReplaceBehaviorGraphs(Document &document, const std::vector<std::pair<CK_ID, CK_ID>> &targets,
                                              CK_ID source);

} // namespace vdev
