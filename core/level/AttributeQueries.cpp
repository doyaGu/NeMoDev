#include "core/level/AttributeQueries.h"

#include <algorithm>

#include "core/document/Document.h"
#include "core/graph/RuntimeGraph.h"

namespace vdev {
const AttributeTypeInfo *AttributeCatalog::Find(CKAttributeType type) const {
    const auto found = types.find(type);
    return found == types.end() ? nullptr : &found->second;
}

AttributeCatalog ReadAttributeCatalog(const Document &document, const std::vector<CK_ID> &targets) {
    AttributeCatalog result;
    if (!document.IsOpen()) return result;
    auto *attributes = document.Context()->GetAttributeManager();
    auto *parameters = document.Context()->GetParameterManager();
    if (!attributes || !parameters) return result;
    result.available = true;
    std::vector<CK_CLASSID> targetClasses;
    for (CK_ID id : targets)
        if (document.IsLive(id))
            if (auto *object = CKBeObject::Cast(document.Context()->GetObject(id))) targetClasses.push_back(object->GetClassID());
    std::sort(targetClasses.begin(), targetClasses.end());
    targetClasses.erase(std::unique(targetClasses.begin(), targetClasses.end()), targetClasses.end());
    std::map<std::string, AttributeCategoryInfo> categories;
    for (int c = 0; c < attributes->GetCategoriesCount(); ++c)
        if (attributes->IsCategoryIndexValid(c))
            if (const char *name = attributes->GetCategoryName(c)) categories[name].flags = attributes->GetCategoryFlags(c);
    for (CKAttributeType type = 0; type < attributes->GetAttributeCount(); ++type) {
        if (!attributes->IsAttributeIndexValid(type)) continue;
        AttributeTypeInfo info;
        info.type = type;
        if (const char *name = attributes->GetAttributeNameByType(type)) info.name = name;
        info.flags = attributes->GetAttributeFlags(type);
        const auto parameter = attributes->GetAttributeParameterType(type);
        if (auto *desc = parameter >= 0 ? parameters->GetParameterTypeDescription(parameter) : nullptr)
            info.parameterName = desc->TypeName.CStr() ? desc->TypeName.CStr() : "";
        const auto compatible = attributes->GetAttributeCompatibleClassId(type);
        if (const char *name = CKClassIDToString(compatible)) info.compatibleName = name;
        const auto flags = info.flags;
        result.types.emplace(type, std::move(info));
        if (flags & CK_ATTRIBUT_HIDDEN) continue;
        if (!targets.empty()) {
            const bool accepted = std::any_of(targetClasses.begin(), targetClasses.end(), [compatible](CK_CLASSID cid) {
                return !compatible || CKIsChildClassOf(cid, compatible);
            });
            if (!accepted) continue;
        }
        const char *category = attributes->GetAttributeCategory(type);
        categories[category ? category : ""].types.push_back(type);
    }
    for (auto &entry : categories) {
        auto &category = entry.second;
        category.name = entry.first;
        std::stable_sort(category.types.begin(), category.types.end(), [&](CKAttributeType a, CKAttributeType b) {
            return _stricmp(result.types.at(a).name.c_str(), result.types.at(b).name.c_str()) < 0;
        });
        result.categories.push_back(std::move(category));
    }
    return result;
}

namespace {
AttributeObjectInfo AttributeValue(const Document &document, CKAttributeManager &attributes,
                                   CKParameterManager &parameters, CKBeObject &object, CKAttributeType type) {
    AttributeObjectInfo info;
    info.object = object.GetID();
    info.reference = document.Reference(info.object);
    info.type = type;
    info.name = object.GetName() ? object.GetName() : "";
    if (const auto *name = attributes.GetAttributeNameByType(type))
        info.attributeName = name;
    if (const auto *category = attributes.GetAttributeCategory(type))
        info.category = category;
    if (auto *value = object.GetAttributeParameter(type)) {
        info.parameter = value->GetID();
        info.parameterReference = document.Reference(info.parameter);
        info.value = ParameterText(value);
        if (const auto *name = parameters.ParameterTypeToName(value->GetType()))
            info.parameterName = name;
    }
    return info;
}
} // namespace
std::vector<AttributeObjectInfo> ReadObjectAttributes(const Document &document, CK_ID id) {
    if (!document.IsOpen() || !document.IsLive(id))
        return {};
    auto *context = document.Context();
    auto *object = CKBeObject::Cast(context->GetObject(id));
    auto *attributes = context->GetAttributeManager();
    auto *parameters = context->GetParameterManager();
    if (!object || !attributes || !parameters)
        return {};
    std::vector<AttributeObjectInfo> result;
    for (int i = 0; i < object->GetAttributeCount(); ++i)
        result.push_back(AttributeValue(document, *attributes, *parameters, *object, object->GetAttributeType(i)));
    return result;
}

std::vector<AttributeObjectInfo> ReadAttributeObjects(const Document &document, const std::vector<CKAttributeType> &types,
                                                     const std::vector<CK_ID> &targets) {
    std::vector<AttributeObjectInfo> result;
    if (!document.IsOpen()) return result;
    auto *attributes = document.Context()->GetAttributeManager();
    auto *parameters = document.Context()->GetParameterManager();
    if (!attributes || !parameters) return result;
    for (auto type : types) {
        if (!attributes->IsAttributeIndexValid(type)) continue;
        auto append = [&](CKBeObject *object) {
            if (!object || object->IsDynamic() || !document.IsLive(object->GetID()) || !object->HasAttribute(type)) return;
            result.push_back(AttributeValue(document, *attributes, *parameters, *object, type));
        };
        if (targets.empty()) {
            const auto &objects = attributes->GetGlobalAttributeListPtr(type);
            for (int i = 0; i < objects.Size(); ++i) append(CKBeObject::Cast(objects[i]));
        } else {
            for (CK_ID id : targets) append(CKBeObject::Cast(document.Context()->GetObject(id)));
        }
    }
    return result;
}
} // namespace vdev
