#include "core/level/ObjectQueries.h"

#include "core/document/Document.h"

namespace vdev {
ObjectSummary DescribeObject(const Document &document, CK_ID id) {
    ObjectSummary result;
    if (!document.IsOpen() || !document.IsLive(id)) return result;
    CKObject *object = document.Context()->GetObject(id);
    result.id = id;
    result.reference = document.Reference(id);
    result.cid = object->GetClassID();
    result.name = object->GetName() ? object->GetName() : "";
    if (auto *render = CKRenderObject::Cast(object)) {
        result.renderObject = true;
        result.renderPriority = render->GetZOrder();
    }
    return result;
}

std::vector<CK_ID> FilterObjects(const Document &document, const std::vector<CK_ID> &objects, CK_CLASSID cid, ObjectQueryScope scope) {
    std::vector<CK_ID> result;
    if (!document.IsOpen()) return result;
    for (CK_ID id : objects) {
        CKObject *object = document.Context()->GetObject(id);
        if (object && (scope == ObjectQueryScope::Existing || !document.IsDiscarded(id)) && CKIsChildClassOf(object, cid)) result.push_back(id);
    }
    return result;
}
} // namespace vdev
