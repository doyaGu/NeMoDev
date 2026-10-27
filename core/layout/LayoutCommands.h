#pragma once

#include <string>
#include <vector>

#include "core/document/EditResult.h"

namespace vdev {
class Document;

enum class LayoutList { Cones, PlayerShown, HiddenCurvePoints };

// Persistent per-object display metadata, recorded in the document's history.
EditResult SetLayoutMembership(Document &document, LayoutList list, CK_ID object,
                               bool listed, const std::string &label);
EditResult SetFrameRepresentation(Document &document, const std::vector<CK_ID> &frames, int representation);

} // namespace vdev
