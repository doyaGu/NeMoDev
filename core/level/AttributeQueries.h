#pragma once

#include <map>
#include <string>
#include <vector>

#include "CKAll.h"
#include "editor/Identity.h"

namespace vdev {
class Document;

// Owned read projections. All text is CK encoding; no native pointers, cached
// parameter descriptions or object arrays escape the query. Refresh after
// edits or runtime work; retaining these values never retains CK resources.
struct AttributeTypeInfo {
    CKAttributeType type = -1;
    std::string name, parameterName, compatibleName;
    CKDWORD flags = 0;
};
struct AttributeCategoryInfo {
    std::string name;
    CKDWORD flags = 0;
    std::vector<CKAttributeType> types;
};
struct AttributeCatalog {
    bool available = false;
    std::map<CKAttributeType, AttributeTypeInfo> types;
    std::vector<AttributeCategoryInfo> categories;
    const AttributeTypeInfo *Find(CKAttributeType type) const;
};
// Hidden types are absent from categories. Empty targets means all types;
// otherwise categories list types accepted by at least one live target.
AttributeCatalog ReadAttributeCatalog(const Document &document, const std::vector<CK_ID> &targets = {});

struct AttributeObjectInfo {
    ObjectRef reference{}, parameterReference{};
    CKAttributeType type = -1;
    CK_ID object = 0, parameter = 0;
    std::string name, value, parameterName, attributeName, category;
};
// Only the requested types. Empty objects lists all objects in native order;
// otherwise only those IDs are queried, in input order. Dynamic and discarded
// objects are omitted. A parameterless attribute still has a row.
std::vector<AttributeObjectInfo> ReadAttributeObjects(const Document &document, const std::vector<CKAttributeType> &types,
                                                     const std::vector<CK_ID> &objects = {});
// Attached attributes in object order, including hidden types and dynamic objects.
std::vector<AttributeObjectInfo> ReadObjectAttributes(const Document &, CK_ID object);
} // namespace vdev
