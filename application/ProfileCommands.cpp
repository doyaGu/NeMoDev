#include "application/ProfileCommands.h"

#include <algorithm>
#include <filesystem>

namespace vdev {
EditorStatus ProfileCommands::Read(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
    auto &self = *static_cast<ProfileCommands *>(data);
    auto modules = self.m_Editor.Modules();
    for (const auto &setting : self.m_Profile.Modules())
        if (std::none_of(modules.begin(), modules.end(), [&](const auto &row) { return row.id == setting.id; }))
            modules.push_back({setting.id, {}, false});
    EditorField rows{"modules", EDITOR_RECORD};
    for (const auto &info : modules) {
        auto desired = info.enabled;
        auto path = info.file;
        bool builtin = info.file.empty();
        for (const auto &setting : self.m_Profile.Modules()) if (setting.id == info.id) {
            desired = setting.enabled; path = setting.path; builtin = setting.builtin;
            if (!builtin && path.empty()) { path = info.file; builtin = path.empty(); }
        }
        EditorField row{info.id, EDITOR_RECORD};
        row.fields = {{"id", EDITOR_TEXT_VALUE, {}, info.id}, {"path", EDITOR_TEXT_VALUE, {}, path},
            {"builtin", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(builtin)},
            {"active", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(info.enabled)},
            {"enabled", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(desired)},
            {"editable", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(info.id != "application.operations")}};
        rows.fields.push_back(std::move(row));
    }
    const std::vector<EditorField> values{std::move(rows)};
    const auto borrowed = BorrowEditorFields(values);
    return sink.write ? sink.write(sink.data, borrowed.data(), uint32_t(borrowed.size())) : EDITOR_INVALID;
}
EditorStatus ProfileCommands::Set(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto &self = *static_cast<ProfileCommands *>(data);
    if (!self.m_Write || request->count != 1) return EDITOR_REFUSED;
    std::vector<EditorField> values;
    if (CopyEditorFields(values, request->values, request->count) != EDITOR_OK || values.size() != 1) return EDITOR_INVALID;
    EditorModuleSetting setting;
    bool id = false, enabled = false;
    for (const auto &field : values.front().fields) if (field.key == "id" && field.type == EDITOR_TEXT_VALUE) {
        setting.id = field.text; id = true;
    }
    if (!id) return EDITOR_INVALID;
    for (const auto &current : self.m_Profile.Modules()) if (current.id == setting.id) setting = current;
    if (!setting.builtin && setting.path.empty())
        for (const auto &info : self.m_Editor.Modules()) if (info.id == setting.id) {
            setting.path = info.file; setting.builtin = info.file.empty();
        }
    bool pathProvided = false, builtinProvided = false;
    for (const auto &field : values.front().fields) {
        if (field.key == "id" && field.type == EDITOR_TEXT_VALUE) { setting.id = field.text; id = true; }
        else if (field.key == "enabled" && field.type == EDITOR_INTEGER_VALUE && field.integer <= 1) { setting.enabled = field.integer != 0; enabled = true; }
        else if (field.key == "path" && field.type == EDITOR_TEXT_VALUE) { setting.path = field.text; pathProvided = true; }
        else if (field.key == "builtin" && field.type == EDITOR_INTEGER_VALUE && field.integer <= 1) {
            setting.builtin = field.integer != 0; builtinProvided = true;
        }
        else return EDITOR_INVALID;
    }
    if (pathProvided && !builtinProvided) setting.builtin = false;
    if (builtinProvided && setting.builtin && !pathProvided) setting.path.clear();
    if (!id || !enabled) return EDITOR_INVALID;
    auto next = self.m_Profile;
    std::string error;
    if (!next.SetModule(setting, error)) return EDITOR_INVALID;
    if (!self.m_Write(next, error)) {
        const EditorValue message{EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, {}, EditorText(error)};
        if (sink.write) sink.write(sink.data, &message, 1);
        return EDITOR_ERROR;
    }
    self.m_Profile = std::move(next); ++self.m_Revision;
    return EDITOR_OK;
}
bool ProfileCommands::Register(std::string &error) {
    const EditorParameter input{EDITOR_TEXT("module"), EDITOR_RECORD};
    const EditorCommandDesc command{sizeof(EditorCommandDesc), EDITOR_TEXT("profile.set-module"),
        EDITOR_TEXT("builtin.profile"), EDITOR_TEXT("Configure Module for Next Start"), &input, 1, Set, this,
        EDITOR_APPLICATION, EDITOR_FILE_OPERATION};
    const EditorParameter output{EDITOR_TEXT("modules"), EDITOR_RECORD};
    const EditorQueryDesc query{sizeof(EditorQueryDesc), EDITOR_TEXT("profile.modules"), EDITOR_TEXT("builtin.profile"),
        nullptr, 0, Read, this, EDITOR_QUERY_VERSIONED, EDITOR_APPLICATION, &output, 1,
        [](void *data, const EditorHost *) { return static_cast<ProfileCommands *>(data)->m_Revision; }};
    EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("profile.settings"); module.commands = &command; module.commandCount = 1;
    module.queries = &query; module.queryCount = 1;
    return m_Editor.Register(module, error);
}
} // namespace vdev
