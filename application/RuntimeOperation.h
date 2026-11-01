#pragma once

#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <string>

namespace vdev {

// Exclusive runtime tasks. Queue on the UI thread, Start after
// rendering the frame, Poll before drawing the next. While Busy, the UI
// must not access CK, its document, or runtime-backed panels/renderers.
// Worker completion runs in Poll on the UI thread, after the worker exits.
// Admission failures are reported immediately on the UI thread. Tasks
// queued by a completion continue that workflow before unrelated queued work.
// Changing documents discards unstarted old work. Ordinary callbacks expire;
// application-owned callbacks still record completion or skipped work. Running
// native code is not interrupted. Finish is a blocking shutdown drain.
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
    enum class Delivery { CurrentDocument, Always };
    // Application resources such as the Snapshot Buffer survive document switches.
    enum class Lifetime { Document, Application };
    // Checked on the UI thread when a task reaches the head of the queue.
    using Admission = std::function<std::string(const std::string &label)>;
    using Executor = std::function<void(const std::string &, const std::function<void()> &)>;
    explicit RuntimeOperation(Admission admission = {}, Executor executor = {})
        : m_Admission(std::move(admission)), m_Executor(std::move(executor)) {}
    ~RuntimeOperation();
    bool Queue(std::string label, std::function<void()> work, Completion completed,
               Delivery delivery = Delivery::CurrentDocument, Lifetime lifetime = Lifetime::Document);
    bool CanQueue(const std::string &label) const { return !m_Admission || m_Admission(label).empty(); }
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
        Delivery delivery = Delivery::CurrentDocument;
        Lifetime lifetime = Lifetime::Document;
    };
    bool Active() const { return m_Task.work || m_Future.valid() || !m_StartError.empty(); }
    void Promote();
    void Complete(Task task, const std::string &error);
    Admission m_Admission;
    Executor m_Executor;
    std::shared_ptr<int> m_Document = std::make_shared<int>(0);
    std::deque<Task> m_Pending, m_Next;
    bool m_Completing = false;
    Task m_Task;
    std::string m_StartError;
    std::future<std::string> m_Future;
};

} // namespace vdev
