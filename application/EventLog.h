#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "application/EditorApplication.h"
#include "core/runtime/Log.h"

namespace vdev {
struct LogEntry {
    LogLevel level{};
    std::string text;
    int repeats = 1;
    bool status = true;
    uint64_t sequence = 0;
};

// Application lifetime. Native callbacks post to a bounded queue; only the
// editor thread publishes entries. Views read copied query results.
// log.entries pages at UTF-8 boundaries within one revision: first/offset are
// row/byte cursors, next/offset resume the result, and repeated sequence values
// are consecutive portions of the same entry. A stale revision must restart.
class EventLog {
  public:
    bool Register(EditorApplication &editor, std::string &error);
    void Append(LogLevel level, const std::string &text, bool status = true);
    void Post(LogLevel level, const std::string &text);
    void Drain();
    void HideStatus();
    const std::vector<LogEntry> &Entries() const { return m_Entries; }
    uint64_t Revision() const { return m_Revision; }
    uint64_t ErrorRevision() const { return m_ErrorRevision; }
  private:
    static EditorStatus Clear(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus AppendCommand(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Open(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Summary(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Read(void *, const EditorHost *, const EditorRequest *, EditorSink);
    void Append(LogEntry entry);
    const std::thread::id m_Thread = std::this_thread::get_id();
    std::vector<LogEntry> m_Entries, m_Pending;
    std::mutex m_Mutex;
    uint64_t m_Revision = 0, m_ErrorRevision = 0, m_Sequence = 0;
};
} // namespace vdev
