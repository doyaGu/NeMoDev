#pragma once
#include "workspace/EditorClient.h"
#include "editor/ImGuiDrawing.h"
namespace vdev {
namespace environment {
inline const EditorField *Field(const EditorField &record, const char *key) {
    for (const auto &field : record.fields) if (field.key == key) return &field;
    return nullptr;
}
inline std::string Text(const EditorField &record, const char *key) {
    const auto *field = Field(record, key); return field ? field->text : "";
}
inline uint64_t Number(const EditorField &record, const char *key) {
    const auto *field = Field(record, key); return field ? field->integer : 0;
}
inline bool Available(const EditorClient &client) {
    const auto state = client.Query("environment.state");
    return state.status == EDITOR_OK && EditorClient::Integer(state, "available");
}
EditorViewDesc PluginView();
EditorViewDesc PathView();
EditorViewDesc LinkOptionsView();
} // namespace environment
} // namespace vdev
