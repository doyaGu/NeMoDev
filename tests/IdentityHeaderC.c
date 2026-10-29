#include "editor/Scene.h"
#include "editor/Resources.h"
typedef char BuildingBlockDragLayout[(sizeof(EditorBuildingBlock) == 8 && sizeof(EditorBuildingBlockDragHeader) == 12) ? 1 : -1];

ObjectRef IdentityFromC(DocumentToken document, uint32_t object, uint64_t lifetime) {
    ObjectRef reference = {document, object, lifetime};
    return reference;
}

uint32_t IdentitySizeFromC(void) { return (uint32_t)sizeof(ObjectRef); }

uint32_t EditorModuleSizeFromC(void) { return (uint32_t)sizeof(EditorModuleDesc); }
uint32_t EditorRequestSizeFromC(void) { return (uint32_t)sizeof(EditorRequest); }
uint32_t EditorHostSizeFromC(void) { return (uint32_t)sizeof(EditorHost); }
EditorValue IntegerFromC(uint64_t value) {
    EditorValue result = {0};
    result.type = EDITOR_INTEGER_VALUE;
    result.integer = value;
    return result;
}
EditorValue ObjectsFromC(const EditorObject *objects, uint32_t count) {
    EditorValue value = {0};
    value.type = EDITOR_OBJECT_LIST;
    value.objects.data = objects;
    value.objects.count = count;
    return value;
}

EditorValue TextsFromC(const EditorString *data, uint32_t count) {
    EditorValue result = {0};
    result.type = EDITOR_TEXT_LIST;
    result.texts.data = data;
    result.texts.count = count;
    return result;
}

EditorValue RealFromC(double value) {
    EditorValue result = {0};
    result.type = EDITOR_REAL_VALUE;
    result.real = value;
    return result;
}

uint32_t EditorViewFrameSizeFromC(void) { return (uint32_t)sizeof(EditorViewFrame); }
uint32_t EditorViewDescSizeFromC(void) { return (uint32_t)sizeof(EditorViewDesc); }
uint32_t EditorQueryDescSizeFromC(void) { return (uint32_t)sizeof(EditorQueryDesc); }

EditorAttributePayload AttributeFromC(ObjectRef parameter, uint64_t catalog) {
    EditorAttributePayload payload = {7, parameter, catalog};
    return payload;
}
uint32_t EditorPickRegionSizeFromC(void) { return (uint32_t)sizeof(EditorPickRegion); }
