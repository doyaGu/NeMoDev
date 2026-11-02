#include "application/MessageCommands.h"

#include <iterator>

namespace vdev {
namespace {
const EditorValue &Field(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key) return request.values[i];
    static const EditorValue empty{};
    return empty;
}
std::string Text(EditorString text) { return {text.data ? text.data : "", text.size}; }
}
EditorStatus MessageCommands::Show(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto &self = *static_cast<MessageCommands *>(data);
    const auto icon = Field(*request, "icon").integer, cancel = Field(*request, "cancel").integer;
    auto format = Text(Field(*request, "format").text);
    if (format.empty()) format = "message";
    if (icon > 6 || cancel > 1 || (format != "message" && format != "report") ||
        (format == "report" && (icon || cancel))) return EDITOR_INVALID;
    const auto id = self.m_Next;
    const EditorValue output{EDITOR_TEXT("message"), EDITOR_INTEGER_VALUE, {}, {}, id};
    if (sink.write) {
        const auto status = sink.write(sink.data, &output, 1);
        if (status != EDITOR_OK) return status;
    }
    self.m_Messages.push_back({id, Text(Field(*request, "text").text), Text(Field(*request, "title").text), format, icon, cancel != 0});
    ++self.m_Next;
    ++self.m_Revision;
    return EDITOR_OK;
}
EditorStatus MessageCommands::Respond(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
    auto &self = *static_cast<MessageCommands *>(data);
    if (self.m_Messages.empty() || self.m_Messages.front().id != Field(*request, "message").integer) return EDITOR_STALE;
    const auto choice = Text(Field(*request, "choice").text);
    if (choice != "accept" && (choice != "cancel" || !self.m_Messages.front().cancel)) return EDITOR_INVALID;
    self.m_Messages.pop_front();
    ++self.m_Revision;
    return EDITOR_OK;
}
EditorStatus MessageCommands::Read(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
    const auto &self = *static_cast<MessageCommands *>(data);
    if (self.m_Messages.empty()) return EDITOR_NO_CHANGE;
    const auto &message = self.m_Messages.front();
    const EditorValue fields[] = {
        {EDITOR_TEXT("message"), EDITOR_INTEGER_VALUE, {}, {}, message.id},
        {EDITOR_TEXT("text"), EDITOR_TEXT_VALUE, {}, EditorText(message.text)},
        {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE, {}, EditorText(message.title)},
        {EDITOR_TEXT("icon"), EDITOR_INTEGER_VALUE, {}, {}, message.icon},
        {EDITOR_TEXT("cancel"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(message.cancel)},
        {EDITOR_TEXT("format"), EDITOR_TEXT_VALUE, {}, EditorText(message.format)}};
    return sink.write ? sink.write(sink.data, fields, uint32_t(std::size(fields))) : EDITOR_INVALID;
}
bool MessageCommands::Register(EditorApplication &editor, std::string &error) {
    const EditorParameter show[] = {{EDITOR_TEXT("text"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE, 1}, {EDITOR_TEXT("icon"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("cancel"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("format"), EDITOR_TEXT_VALUE, 1}};
    const EditorParameter reply[] = {{EDITOR_TEXT("message"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("choice"), EDITOR_TEXT_VALUE}};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("message.show"), EDITOR_TEXT("builtin.messages"), EDITOR_TEXT("Show Message"),
         show, uint32_t(std::size(show)), Show, this, EDITOR_APPLICATION, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("message.respond"), EDITOR_TEXT("builtin.messages"), EDITOR_TEXT("Answer Message"),
         reply, uint32_t(std::size(reply)), Respond, this, EDITOR_APPLICATION, EDITOR_SESSION_CHANGE}};
    const EditorParameter fields[] = {{EDITOR_TEXT("message"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("text"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("icon"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("cancel"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("format"), EDITOR_TEXT_VALUE}};
    const EditorQueryDesc query{sizeof(EditorQueryDesc), EDITOR_TEXT("message.current"), EDITOR_TEXT("builtin.messages"),
        nullptr, 0, Read, this, EDITOR_QUERY_VERSIONED, EDITOR_APPLICATION, fields, uint32_t(std::size(fields)),
        [](void *data, const EditorHost *) { return static_cast<MessageCommands *>(data)->m_Revision; }};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("application.messages");
    module.commands = commands;
    module.commandCount = uint32_t(std::size(commands));
    module.queries = &query;
    module.queryCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev
