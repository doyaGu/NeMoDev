#include "application/EventLog.h"
#include "workspace/EditorWorkspace.h"
#include "editor/ImGuiDrawing.h"
#include "core/runtime/CkRuntimeHost.h"
#include <stdexcept>
#include <thread>

using namespace vdev;
namespace {
void Check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
const EditorField &Field(const EditorResponse &result, const char *key) {
    Check(result.status == EDITOR_OK, "event log query failed");
    for (const auto &field : result.fields) if (field.key == key) return field;
    throw std::runtime_error("event log field missing");
}
}
void TestEventLog() {
    EventLog log;
    EditorApplication editor({});
    EditorWorkspace workspace(editor);
    std::string error;
    Check(workspace.Register(error) && log.Register(editor, error) &&
          editor.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    const auto query = [&](const char *name) { return editor.Query(name, {editor.Scope(), nullptr, 0}); };
    log.Append(LogLevel::Info, "start", false);
    Check(Field(query("log.summary"), "status").text.empty(), "startup log appeared on status bar");
    log.Append(LogLevel::Error, "error");
    const auto first = query("log.entries");
    const auto revision = log.Revision(), errors = log.ErrorRevision();
    log.Append(LogLevel::Error, "error");
    const auto repeated = query("log.entries");
    Check(log.Entries().size() == 2 && log.Entries().back().repeats == 2 && log.Revision() > revision &&
          log.ErrorRevision() > errors, "repeated error did not refresh view/status state");
    Check(Field(first, "repeats").integers.back() == 1 && Field(repeated, "repeats").integers.back() == 2,
          "published event log projection changed in place");
    log.HideStatus();
    Check(Field(query("log.summary"), "status").text.empty(), "status suppression did not publish");
    editor.NativeAvailable(false);
    std::thread worker([&] { for (int i = 0; i < 100; ++i) log.Post(LogLevel::Warning, "worker"); });
    worker.join();
    Check(log.Entries().size() == 2, "worker mutated published log");
    log.Drain();
    Check(log.Entries().back().text == "worker" && log.Entries().back().repeats == 100,
          "worker log coalescing or delivery lost messages");
    const auto token = editor.Scope().document;
    const auto clear = editor.Execute("log.clear", {editor.Scope(), nullptr, 0});
    Check(clear.status == EDITOR_OK && log.Entries().size() == 1 && log.Entries()[0].text == "Event Log Cleared" &&
          editor.Scope().document == token, "busy log clear changed document scope or failed");
    for (int i = 0; i < 5500; ++i) log.Append(LogLevel::Info, std::to_string(i));
    const auto trimmed = query("log.entries");
    Check(log.Entries().size() <= 5000 && Field(trimmed, "texts").texts.back() == "5499", "event log retention failed");
    log.Append(LogLevel::Error, "error after trim");
    Check(Field(query("log.summary"), "error-revision").integer > errors, "trimming hid a new error");
    std::thread pending([&] { log.Post(LogLevel::Warning, "queued before clear"); });
    pending.join();
    Check(editor.Execute("log.clear", {editor.Scope(), nullptr, 0}).status == EDITOR_OK, "second clear failed");
    log.Drain();
    Check(log.Entries().size() == 1 && log.Entries()[0].text == "Event Log Cleared", "clear resurrected queued log entries");
    std::thread bounded([&] { for (int i = 0; i < 7000; ++i) log.Post(LogLevel::Info, "queued " + std::to_string(i)); });
    bounded.join();
    log.Drain();
    Check(log.Entries().size() <= 5000 && log.Entries().back().text == "queued 6999", "worker log queue is unbounded or lost latest entries");
    Check(editor.Execute("log.clear", {editor.Scope(), nullptr, 0}).status == EDITOR_OK, "clear before paging failed");
    std::string large;
    for (int i = 0; i < 400000; ++i) large += u8"读";
    log.Append(LogLevel::Info, large);
    const auto pinned = log.Revision();
    uint64_t firstIndex = 0, offset = 0;
    unsigned pages = 0;
    std::string joined;
    do {
        const EditorValue cursor[] = {{EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, {}, {}, firstIndex},
            {EDITOR_TEXT("offset"), EDITOR_INTEGER_VALUE, {}, {}, offset},
            {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, pinned}};
        const auto page = editor.Query("log.entries", {editor.Scope(), cursor, 3});
        const auto &texts = Field(page, "texts").texts;
        const auto &sequences = Field(page, "sequences").integers;
        for (size_t i = 0; i < texts.size(); ++i)
            if (sequences[i] == log.Entries().back().sequence) joined += texts[i];
        firstIndex = Field(page, "next").integer;
        offset = Field(page, "offset").integer;
        Check(++pages < 10, "event log cursor did not advance");
    } while (offset || firstIndex < log.Entries().size());
    Check(pages > 1 && joined == large, "bounded queries lost a large UTF-8 log entry");
    const EditorValue brokenCursor[] = {{EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, {}, {}, 1},
        {EDITOR_TEXT("offset"), EDITOR_INTEGER_VALUE, {}, {}, 1}};
    Check(editor.Query("log.entries", {editor.Scope(), brokenCursor, 2}).status == EDITOR_INVALID,
          "cursor split a UTF-8 character");
    log.Append(LogLevel::Info, "new revision");
    const EditorValue oldRevision{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, pinned};
    Check(editor.Query("log.entries", {editor.Scope(), &oldRevision, 1}).status == EDITOR_STALE,
          "a stale cursor mixed log revisions");
}
