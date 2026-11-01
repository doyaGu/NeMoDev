#include "editor/Schematic.h"
typedef char SchematicKindValues[(EDITOR_SCHEMATIC_START == 10) ? 1 : -1];
typedef char SchematicPinValues[(EDITOR_SCHEMATIC_TARGET == 5) ? 1 : -1];
#include "editor/Scene.h"
#include "editor/Resources.h"
typedef char BuildingBlockDragLayout[(sizeof(EditorBuildingBlock) == 8 && sizeof(EditorBuildingBlockDragHeader) == 12) ? 1 : -1];
typedef char ResourceDragLayout[(sizeof(EditorResourceDragHeader) == 8 && sizeof(EditorResourceFileHeader) == 8) ? 1 : -1];

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

uint32_t EditorValueInputSizeFromC(void) { return (uint32_t)sizeof(EditorValueInput); }
uint32_t EditorValueEditorSizeFromC(void) { return (uint32_t)sizeof(EditorValueEditorDesc); }

EditorValue RealsFromC(const double *data, uint32_t count) {
    EditorValue result = {0};
    result.type = EDITOR_REAL_LIST; result.reals.data = data; result.reals.count = count;
    return result;
}

EditorValue RecordFromC(const EditorValue *fields, uint32_t count) {
    EditorValue result = {0}; result.type = EDITOR_RECORD;
    result.fields.data = fields; result.fields.count = count; return result;
}

EditorParameter NullableObjectFromC(void) {
    EditorParameter parameter = {0};
    parameter.key = (EditorString)EDITOR_TEXT("value");
    parameter.type = EDITOR_OBJECT;
    parameter.nullable = 1;
    return parameter;
}
uint32_t EditorParameterSizeFromC(void) { return (uint32_t)sizeof(EditorParameter); }

EditorStatus BeginPositionedEditFromC(const EditorHost *host, EditorString property, const EditorRequest *request, EditorEdit *edit) {
    return host->beginEdit(host->data, property, request, edit);
}
uint32_t DrawPositionedPropertyFromC(const EditorHost *host, EditorString property, const EditorRequest *request, EditorEdit *edit, EditorString label) {
    return host->drawProperty(host->data, property, request, edit, label);
}

static EditorStatus EDITOR_CALL PreviewFromC(void *data, const struct EditorHost *host, const EditorRequest *request,
                                             EditorPreviewAction action, void **state) {
    (void)host;
    (void)request;
    if (!state)
        return EDITOR_INVALID;
    if (action == EDITOR_PREVIEW_UPDATE)
        *state = data;
    else if (action == EDITOR_PREVIEW_CANCEL)
        *state = 0;
    else
        return EDITOR_INVALID;
    return EDITOR_OK;
}
EditorCommandDesc PreviewCommandFromC(void *data) {
    EditorCommandDesc command = {0};
    command.size = sizeof(command);
    command.preview = PreviewFromC;
    command.data = data;
    return command;
}

static EditorStatus EDITOR_CALL CommitPreviewFromC(void *data, const EditorHost *host, const EditorRequest *request,
                                                   void *state, EditorSink sink) {
    (void)host;
    (void)request;
    (void)sink;
    return state == data ? EDITOR_OK : EDITOR_INVALID;
}
static EditorStatus EDITOR_CALL ContinuousPreviewFromC(void *data, const EditorHost *host, const EditorRequest *request,
                                                       EditorPreviewAction action, void **state) {
    if (action == EDITOR_PREVIEW_RELEASE) {
        if (!state)
            return EDITOR_INVALID;
        *state = 0;
        return EDITOR_OK;
    }
    return PreviewFromC(data, host, request, action, state);
}
EditorCommandDesc ContinuousCommandFromC(void *data) {
    EditorCommandDesc command = PreviewCommandFromC(data);
    command.preview = ContinuousPreviewFromC;
    command.commitPreview = CommitPreviewFromC;
    return command;
}
EditorStatus BeginCommandFromC(const EditorHost *host, EditorString command, const EditorRequest *request,
                               EditorEdit *edit) {
    return host->beginCommand(host->data, command, request, edit);
}
EditorStatus FinishCommandFromC(const EditorHost *host, EditorEdit edit, EditorOperation *operation) {
    return host->finishEdit(host->data, edit, 1, operation);
}
uint32_t EditorCommandSizeFromC(void) { return (uint32_t)sizeof(EditorCommandDesc); }
