#include "editor/Scene.h"
#include "editor/Resources.h"
static_assert(sizeof(EditorBuildingBlock) == 8 && sizeof(EditorBuildingBlockDragHeader) == 12, "C Building Block drag layout");

static_assert(__is_standard_layout(EditorModuleDesc), "C layout");
static_assert(__is_trivially_copyable(EditorModuleDesc), "Borrowed descriptors");
static_assert(__is_standard_layout(EditorRequest), "C request layout");
static_assert(__is_trivially_copyable(EditorValue), "Copied command values");
static_assert(__is_standard_layout(EditorStrings), "C text array");
static_assert(__is_standard_layout(EditorObjects), "C object array");
static_assert(__is_trivially_copyable(EditorIntegers), "Borrowed integer array");

// This consumer links neither CK nor the editor and includes no STL headers.
static_assert(__is_standard_layout(vdev::ObjectRef), "C-compatible layout");
static_assert(__is_trivially_copyable(vdev::ObjectRef), "Non-owning value");
extern "C" vdev::ObjectRef IdentityFromC(vdev::DocumentToken, uint32_t, uint64_t);
extern "C" uint32_t IdentitySizeFromC(void);
extern "C" uint32_t EditorModuleSizeFromC(void);
extern "C" uint32_t EditorRequestSizeFromC(void);
extern "C" uint32_t EditorHostSizeFromC(void);
extern "C" EditorStatus BeginPositionedEditFromC(const EditorHost *, EditorString, const EditorRequest *, EditorEdit *);
extern "C" uint32_t EditorViewFrameSizeFromC(void);
extern "C" uint32_t EditorViewDescSizeFromC(void);
extern "C" uint32_t EditorQueryDescSizeFromC(void);
static_assert(__is_standard_layout(EditorViewFrame), "C window drawing values");
static_assert(__is_trivially_copyable(EditorViewDesc), "Borrowed view callbacks");
extern "C" EditorValue IntegerFromC(uint64_t);
extern "C" EditorValue RealFromC(double);
extern "C" EditorValue RealsFromC(const double *, uint32_t);
extern "C" EditorValue RecordFromC(const EditorValue *, uint32_t);
static_assert(__is_standard_layout(EditorFields), "C record member array");
static_assert(__is_standard_layout(EditorReals), "C numeric component array");
extern "C" EditorValue TextsFromC(const EditorString *, uint32_t);
extern "C" EditorValue ObjectsFromC(const EditorObject *, uint32_t);

static_assert(__is_standard_layout(EditorPickRegion), "C picking rectangle");
static_assert(__is_trivially_copyable(EditorAttributePayload), "Attribute identity and catalog version");
extern "C" EditorAttributePayload AttributeFromC(vdev::ObjectRef, uint64_t);
extern "C" uint32_t EditorPickRegionSizeFromC(void);
extern "C" uint32_t EditorValueInputSizeFromC(void);
extern "C" uint32_t EditorValueEditorSizeFromC(void);
static_assert(__is_standard_layout(EditorValueInput), "C value input");
static_assert(__is_trivially_copyable(EditorValueEditorDesc), "Borrowed typed editor");
extern "C" EditorParameter NullableObjectFromC(void);
extern "C" uint32_t EditorParameterSizeFromC(void);
extern "C" EditorCommandDesc PreviewCommandFromC(void *);
static_assert(EDITOR_VERSION == 15, "Editor live-preview property version");
int main() {
    int previewValue = 17;
    void *previewState = nullptr;
    const auto command = PreviewCommandFromC(&previewValue);
    if (command.size != sizeof(EditorCommandDesc) || !command.preview ||
        command.preview(command.data, nullptr, nullptr, EDITOR_PREVIEW_UPDATE, &previewState) != EDITOR_OK ||
        previewState != &previewValue ||
        command.preview(command.data, nullptr, nullptr, EDITOR_PREVIEW_CANCEL, &previewState) != EDITOR_OK ||
        previewState)
        return 1;
    EditorHost host{};
    host.beginEdit=[](void *,EditorString,const EditorRequest *request,EditorEdit *edit) {
        if (!request || request->count != 1 || request->values[0].type != EDITOR_INTEGER_LIST) return EDITOR_INVALID;
        *edit=request->values[0].integers.data[0]; return EDITOR_OK;
    };
    const uint64_t row=17;
    EditorValue position{}; position.type=EDITOR_INTEGER_LIST; position.integers={&row,1};
    const EditorRequest request{{},&position,1}; EditorEdit edit=0;
    if (BeginPositionedEditFromC(&host,{},&request,&edit) != EDITOR_OK || edit != 17) return 1;
    const auto nullable = NullableObjectFromC();
    if (nullable.type != EDITOR_OBJECT || nullable.optional || nullable.nullable != 1 || sizeof(EditorParameter) != EditorParameterSizeFromC()) return 1;
    if (sizeof(EditorValueInput) != EditorValueInputSizeFromC() || sizeof(EditorValueEditorDesc) != EditorValueEditorSizeFromC()) return 1;
    const auto attribute = AttributeFromC({{3}, 7, 11}, 17);
    if(attribute.type != 7 || attribute.value.lifetime != 11 || attribute.catalog != 17 ||
       sizeof(EditorPickRegion) != EditorPickRegionSizeFromC()) return 1;
    if (sizeof(EditorModuleDesc) != EditorModuleSizeFromC()) return 1;
    if (sizeof(EditorRequest) != EditorRequestSizeFromC() || sizeof(EditorHost) != EditorHostSizeFromC()) return 1;
    if (sizeof(EditorViewFrame) != EditorViewFrameSizeFromC() || sizeof(EditorViewDesc) != EditorViewDescSizeFromC()) return 1;
    if (sizeof(EditorQueryDesc) != EditorQueryDescSizeFromC()) return 1;
    const EditorValue member = IntegerFromC(7);
    const auto record = RecordFromC(&member, 1);
    if (record.type != EDITOR_RECORD || record.fields.data != &member || record.fields.count != 1) return 1;
    const double components[] = {1, 2, 3};
    const auto array = RealsFromC(components, 3);
    if (array.type != EDITOR_REAL_LIST || array.reals.data != components || array.reals.count != 3) return 1;
    const auto real = RealFromC(0.125);
    if (real.type != EDITOR_REAL_VALUE || real.real != 0.125) return 1;
    const EditorString item = EDITOR_TEXT("file.nms");
    const auto texts = TextsFromC(&item, 1);
    if (texts.type != EDITOR_TEXT_LIST || texts.texts.data != &item || texts.texts.count != 1) return 1;
    const auto integer = IntegerFromC(0x123456789abcdef0ULL);
    if (integer.type != EDITOR_INTEGER_VALUE || integer.integer != 0x123456789abcdef0ULL) return 1;
    const auto reference = IdentityFromC({7}, 19, 31);
    const auto objects = ObjectsFromC(&reference, 1);
    if (objects.type != EDITOR_OBJECT_LIST || objects.objects.data != &reference || objects.objects.count != 1) return 1;
    if (sizeof(reference) != IdentitySizeFromC() || reference.document.value != 7 ||
        reference.object != 19 || reference.lifetime != 31 || !reference) return 1;
    auto stale = reference;
    ++stale.lifetime;
    return stale != reference && reference < stale && !vdev::ObjectRef{} ? 0 : 1;
}
