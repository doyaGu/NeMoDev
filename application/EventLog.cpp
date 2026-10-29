#include "application/EventLog.h"
#include <algorithm>
#include <limits>

namespace vdev {
namespace {
void Add(std::vector<LogEntry> &entries, LogEntry entry) {
    if (!entries.empty() && entries.back().level == entry.level && entries.back().text == entry.text) {
        auto &last = entries.back();
        last.repeats += std::min(entry.repeats, std::numeric_limits<int>::max() - last.repeats);
        last.status = entry.status;
        return;
    }
    entries.push_back(std::move(entry));
    if (entries.size() > 5000) entries.erase(entries.begin(), entries.begin() + 1000);
}
}
void EventLog::Append(LogEntry entry) {
    entry.sequence = ++m_Sequence;
    if (entry.level == LogLevel::Error) ++m_ErrorRevision;
    Add(m_Entries, std::move(entry));
    ++m_Revision;
}
void EventLog::Append(LogLevel level, const std::string &text, bool status) { Append({level, text, 1, status}); }
void EventLog::Post(LogLevel level, const std::string &text) {
    if (std::this_thread::get_id() == m_Thread) { Append(level, text); return; }
    std::lock_guard<std::mutex> lock(m_Mutex);
    Add(m_Pending, {level, text});
}
void EventLog::Drain() {
    std::vector<LogEntry> pending;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        pending.swap(m_Pending);
    }
    for (auto &entry : pending) Append(std::move(entry));
}
void EventLog::HideStatus() {
    if (!m_Entries.empty() && m_Entries.back().status) { m_Entries.back().status = false; ++m_Revision; }
}
EditorStatus EventLog::Clear(void *data, const EditorHost *, const EditorRequest *, EditorSink) {
    auto &self = *static_cast<EventLog *>(data);
    self.Drain();
    self.m_Entries.clear();
    self.Append(LogLevel::Info, "Event Log Cleared");
    return EDITOR_OK;
}
EditorStatus EventLog::Open(void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    const EditorRequest navigation{request->scope, nullptr, 0};
    return host->navigateView(host->data, EditorString EDITOR_TEXT("application.event-log"), 0, &navigation, sink);
}
EditorStatus EventLog::Summary(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
    const auto &self = *static_cast<EventLog *>(data);
    const std::string empty;
    const auto &status = self.m_Entries.empty() || !self.m_Entries.back().status ? empty : self.m_Entries.back().text;
    const EditorValue fields[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, self.m_Revision},
        {EDITOR_TEXT("count"), EDITOR_INTEGER_VALUE, {}, {}, self.m_Entries.size()},
        {EDITOR_TEXT("error-revision"), EDITOR_INTEGER_VALUE, {}, {}, self.m_ErrorRevision},
        {EDITOR_TEXT("status"), EDITOR_TEXT_VALUE, {}, EditorText(status)}};
    return sink.write ? sink.write(sink.data, fields, 4) : EDITOR_INVALID;
}
EditorStatus EventLog::Read(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    const auto &self = *static_cast<EventLog *>(data);
    uint64_t first = 0, offset = 0, revision = 0;
    for (uint32_t i = 0; i < request->count; ++i) {
        const auto &field = request->values[i];
        const std::string key(field.key.data, field.key.size);
        if (key == "first") first = field.integer;
        if (key == "offset") offset = field.integer;
        if (key == "revision") revision = field.integer;
    }
    if (revision && revision != self.m_Revision) return EDITOR_STALE;
    if (first > self.m_Entries.size() || (first == self.m_Entries.size() && offset)) return EDITOR_INVALID;
    if (first < self.m_Entries.size() && (offset > self.m_Entries[size_t(first)].text.size() ||
        (offset < self.m_Entries[size_t(first)].text.size() &&
         (static_cast<unsigned char>(self.m_Entries[size_t(first)].text[size_t(offset)]) & 0xc0) == 0x80))) return EDITOR_INVALID;
    std::vector<uint64_t> sequences, levels, repeats;
    std::vector<EditorString> texts;
    size_t budget = 256 * 1024;
    while (first < self.m_Entries.size() && budget >= 4) {
        const auto &entry = self.m_Entries[size_t(first)];
        size_t end = std::min(entry.text.size(), size_t(offset) + budget);
        while (end < entry.text.size() && end > offset &&
               (static_cast<unsigned char>(entry.text[end]) & 0xc0) == 0x80) --end;
        if (end == offset && end < entry.text.size()) return EDITOR_INVALID;
        sequences.push_back(entry.sequence);
        levels.push_back(uint64_t(entry.level));
        repeats.push_back(uint64_t(entry.repeats));
        texts.push_back({entry.text.data() + offset, uint32_t(end - offset)});
        budget -= end - size_t(offset);
        offset = end;
        if (offset == entry.text.size()) { ++first; offset = 0; }
    }
    const EditorValue fields[] = {
        {EDITOR_TEXT("sequences"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {sequences.data(), uint32_t(sequences.size())}},
        {EDITOR_TEXT("levels"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {levels.data(), uint32_t(levels.size())}},
        {EDITOR_TEXT("repeats"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {repeats.data(), uint32_t(repeats.size())}},
        {EDITOR_TEXT("texts"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {texts.data(), uint32_t(texts.size())}},
        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, self.m_Revision},
        {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE, {}, {}, first},
        {EDITOR_TEXT("offset"), EDITOR_INTEGER_VALUE, {}, {}, offset}};
    return sink.write ? sink.write(sink.data, fields, 7) : EDITOR_INVALID;
}
bool EventLog::Register(EditorApplication &editor, std::string &error) {
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("log.clear"), EDITOR_TEXT("builtin.log"), EDITOR_TEXT("Clear Event Log"),
         nullptr, 0, Clear, this, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("log.open"), EDITOR_TEXT("builtin.log"), EDITOR_TEXT("Event Log"),
         nullptr, 0, Open, this, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE}};
    const EditorParameter summary[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("count"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("error-revision"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("status"), EDITOR_TEXT_VALUE}};
    const EditorParameter entries[] = {{EDITOR_TEXT("sequences"), EDITOR_INTEGER_LIST},
        {EDITOR_TEXT("levels"), EDITOR_INTEGER_LIST}, {EDITOR_TEXT("repeats"), EDITOR_INTEGER_LIST},
        {EDITOR_TEXT("texts"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("offset"), EDITOR_INTEGER_VALUE}};
    const EditorParameter cursor[] = {{EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("offset"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, 1}};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("log.summary"), EDITOR_TEXT("builtin.log"), nullptr, 0, Summary,
         this, EDITOR_QUERY_VERSIONED, EDITOR_WORKSPACE, summary, 4,
         [](void *data, const EditorHost *) { return static_cast<EventLog *>(data)->Revision(); }},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("log.entries"), EDITOR_TEXT("builtin.log"), cursor, 3, Read,
         this, EDITOR_QUERY_EACH_READ, EDITOR_WORKSPACE, entries, 7}};
    const EditorString dependency = EDITOR_TEXT("workspace.views");
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("application.log");
    module.dependencies = &dependency;
    module.dependencyCount = 1;
    module.commands = commands;
    module.commandCount = 2;
    module.queries = queries;
    module.queryCount = 2;
    return editor.Register(module, error);
}
} // namespace vdev
