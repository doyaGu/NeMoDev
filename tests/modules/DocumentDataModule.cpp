#include "editor/Editor.h"

namespace {
EditorStatus Upgrade(void *, const EditorDocumentData *old, EditorDocumentDataSink sink) {
    auto value = *old;
    value.version = 2;
    return sink.write(sink.data, &value);
}
EditorStatus Merge(void *, const EditorDocumentData *, const EditorDocumentData *incoming, EditorDocumentDataSink sink) {
    return sink.write(sink.data, incoming);
}
EditorStatus Set(void *fail, const EditorHost *host, const EditorRequest *request, EditorSink) {
    const unsigned char bytes[] = {0, static_cast<unsigned char>(request->values[0].integer), 255};
    EditorObject refs[2]{};
    if (request->count > 1) refs[0] = request->values[1].object;
    const EditorDocumentData value{sizeof(value), 2, {}, bytes, sizeof(bytes), {refs, 2}};
    const auto result = host->writeDocumentData(host->data, request->scope, EditorString EDITOR_TEXT("fixture.notes"), {}, &value);
    return result == EDITOR_OK && fail ? EDITOR_ERROR : result;
}
}
extern "C" EDITOR_MODULE_EXPORT EditorStatus EDITOR_CALL NeMoDevModule(const EditorHost *host, EditorModuleDesc *out) {
    if (!host || host->size != sizeof(*host) || host->version != EDITOR_VERSION || !out ||
        out->size != sizeof(*out) || out->version != EDITOR_VERSION) return EDITOR_INVALID;
    static const EditorDocumentDataDesc data{sizeof(data), EDITOR_TEXT("fixture.notes"), EDITOR_TEXT("fixture.external"),
        2, Upgrade, Merge, nullptr};
    static const EditorParameter arguments[] = {{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("object"), EDITOR_OBJECT, 1, 1}};
    static const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("fixture.notes.set"), EDITOR_TEXT("fixture.external"),
            EDITOR_TEXT("Set module notes"), arguments, 2, Set, nullptr},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("fixture.notes.fail"), EDITOR_TEXT("fixture.external"),
            EDITOR_TEXT("Fail module notes"), arguments, 2, Set, reinterpret_cast<void *>(1)}
    };
    out->id = EDITOR_TEXT("fixture.document-data");
    out->documentData = &data; out->documentDataCount = 1;
    out->commands = commands; out->commandCount = 2;
    return EDITOR_OK;
}
