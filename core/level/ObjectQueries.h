#pragma once

#include <string>
#include <vector>

#include "CKAll.h"

namespace vdev {
class Document;

// Owned query values, with no native object lifetime. Names use CK encoding.
// Visible-object queries exclude the document's graveyard. Read again after an
// edit; a retained value describes the old state and is never an edit target.
struct ObjectSummary {
    CK_ID id = 0;
    CK_CLASSID cid = 0;
    std::string name;
    bool renderObject = false;
    int renderPriority = 0;
};
ObjectSummary DescribeObject(const Document &document, CK_ID object);
enum class ObjectQueryScope { Live, Existing }; // Existing also admits retained clipboard sources.
// Retains input order. Used for selections, picking and clipboard checks.
std::vector<CK_ID> FilterObjects(const Document &document, const std::vector<CK_ID> &objects,
                               CK_CLASSID cid = CKCID_OBJECT, ObjectQueryScope scope = ObjectQueryScope::Live);
} // namespace vdev
