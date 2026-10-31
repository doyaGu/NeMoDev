#pragma once
#include "workspace/EditorClient.h"

namespace vdev {
namespace runtime {
inline ObjectRef Object(const EditorResponse &response, const char *key) {
    const auto *field = EditorClient::Find(response, key);
    return field ? field->object : ObjectRef{};
}
inline double Real(const EditorResponse &response, const char *key) {
    const auto *field = EditorClient::Find(response, key);
    return field ? field->real : 0;
}
inline uint64_t Integer(const EditorResponse &response, const char *key) { return EditorClient::Integer(response, key); }
inline bool Ready(const EditorClient &client) {
    const EditorRequest request{client.Scope(), nullptr, 0};
    return client.Host()->commandState(client.Host()->data, EditorString EDITOR_TEXT("runtime.clear-log"), &request) == EDITOR_OK;
}
inline void Show(const EditorClient &client, ObjectRef script, ObjectRef behavior = {}, ObjectRef object = {}) {
    if (script) client.Command("script.navigate", {ObjectArgument("target", script),
        ObjectArgument("focus-target", behavior), ObjectArgument("object", object)});
}
EditorViewDesc DebuggerView();
EditorViewDesc ProfilerView();
EditorViewDesc ParameterDebuggerView();
} // namespace runtime
} // namespace vdev
