#pragma once
#include "editor/Editor.h"
#include <string_view>

namespace vdev {
// Current-document file presentation, read only after a successful load notice.
// Stored view state belongs to each view; this value never retains CK objects.
struct FileLoadState {
    bool opened = false, composition = false, imported = false;
    EditorObject script{};
};
inline bool ReadFileLoadState(const EditorHost *host, FileLoadState &result) {
    FileLoadState next;
    EditorSink sink{&next, [](void *data, const EditorValue *values, uint32_t count) {
        if (count != 4) return EDITOR_INVALID;
        auto &next = *static_cast<FileLoadState *>(data);
        unsigned seen = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const auto &v = values[i];
            const std::string_view key(v.key.data, v.key.size);
            const unsigned bit = key == "script" ? 1u : key == "opened" ? 2u : key == "composition" ? 4u : key == "imported" ? 8u : 0u;
            if (!bit || (seen & bit)) return EDITOR_INVALID;
            seen |= bit;
            if (key == "script" && v.type == EDITOR_OBJECT) next.script = v.object;
            else if (v.type == EDITOR_INTEGER_VALUE && v.integer <= 1) {
                if (key == "opened") next.opened = v.integer != 0;
                else if (key == "composition") next.composition = v.integer != 0;
                else if (key == "imported") next.imported = v.integer != 0;
                else return EDITOR_INVALID;
            } else return EDITOR_INVALID;
        }
        return seen == 15 ? EDITOR_OK : EDITOR_INVALID;
    }};
    const EditorRequest request{host->scope(host->data, 0)};
    if (host->query(host->data, EditorString EDITOR_TEXT("document.file-state"), &request, sink) != EDITOR_OK) return false;
    if (next.script.object && next.script.document != request.scope.document) return false;
    result = next; return true;
}
} // namespace vdev
