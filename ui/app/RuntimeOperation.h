#pragma once

#include <functional>
#include <future>
#include <deque>
#include <memory>
#include <string>

namespace vdev {

// Exclusive, document-scoped runtime tasks. Queue on the UI thread, Start after
// rendering the frame, Poll before drawing the next. While Busy, the UI
// must not access CK, its document, or runtime-backed panels/renderers.
// Worker completion runs in Poll on the UI thread, after the worker exits.
// Admission failures are reported immediately on the UI thread. Tasks
// queued by a completion continue that workflow before unrelated queued work.
// Changing documents discards old queued work and completions; it does not
// interrupt running native code. Finish is a blocking shutdown drain.
class RuntimeOperation {
public:
    class DocumentScope {
    public:
        bool IsCurrent() const { return !m_Identity.expired(); }
    private:
        std::weak_ptr<int> m_Identity;
        friend class RuntimeOperation;
    };
    using Completion = std::function<void(const std::string &error)>;
    // Checked on the UI thread when a task reaches the head of the queue.
    using Admission = std::function<std::string(const std::string &label)>;
    explicit RuntimeOperation(Admission admission = {}) : m_Admission(std::move(admission)) {}
    ~RuntimeOperation();
    bool Queue(std::string label, std::function<void()> work,
               Completion completed);
    DocumentScope CurrentDocument() const;
    void DocumentChanged();
    void Start();
    void Poll();
    // Shutdown only: finish queued/running work before tearing down CK.
    void Finish();
    bool Busy() const { return Active() || !m_Next.empty() || !m_Pending.empty(); }
    const std::string &Label() const;

private:
    struct Task {
        DocumentScope document;
        std::string label;
        std::function<void()> work;
        Completion completed;
    };
    bool Active() const { return m_Task.work || m_Future.valid() || !m_StartError.empty(); }
    void Promote();
    void Complete(Task task, const std::string &error);
    Admission m_Admission;
    std::shared_ptr<int> m_Document = std::make_shared<int>(0);
    std::deque<Task> m_Pending, m_Next;
    bool m_Completing = false;
    Task m_Task;
    std::string m_StartError;
    std::future<std::string> m_Future;
};

} // namespace vdev
