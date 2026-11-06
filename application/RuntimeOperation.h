#pragma once

#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <cstdint>
#include <utility>
#include <stdexcept>

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
    // One native workflow may wait for input between tasks. Its request keeps
    // other workflows out; Enter scopes task submission, including continuations.
    // Requests and the queue are used on their creating UI thread. The queue
    // outlives its requests; running native work still finishes before teardown.
    class Request {
      public:
        class Scope {
          public:
            ~Scope();
            Scope(const Scope &) = delete;
            Scope &operator=(const Scope &) = delete;

          private:
            Scope(RuntimeOperation *, uint64_t);
            RuntimeOperation *m_Runtime;
            uint64_t m_Previous = 0;
            friend class Request;
            friend class RuntimeOperation;
        };
        Request() = default;
        Request(Request &&) noexcept;
        Request &operator=(Request &&) noexcept;
        Request(const Request &) = delete;
        Request &operator=(const Request &) = delete;
        ~Request();
        explicit operator bool() const;
        bool Available() const;
        Scope Enter() const;
        void Reset();

      private:
        Request(RuntimeOperation *runtime, uint64_t id) : m_Runtime(runtime), m_Id(id) {}
        RuntimeOperation *m_Runtime = nullptr;
        uint64_t m_Id = 0;
        friend class RuntimeOperation;
    };
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
    using Availability = std::function<bool()>;
    explicit RuntimeOperation(Admission admission = {}, Executor executor = {}, Availability available = {})
        : m_Admission(std::move(admission)), m_Executor(std::move(executor)), m_Available(std::move(available)) {}
    ~RuntimeOperation();
    Request Begin(const std::string &label);
    // Deferred input callbacks retain the submitting workflow, just as worker
    // completions do. They carry no ownership and do not keep a request alive.
    template <class Callback> auto Continue(Callback callback) {
        if (!Thread() || m_Checking) throw std::logic_error("Runtime continuations belong to the UI thread");
        const auto request = m_Submitting;
        return [this, request, callback = std::move(callback)](auto &&...arguments) mutable -> decltype(auto) {
            if (!Thread() || m_Checking) throw std::logic_error("Runtime continuations belong to the UI thread");
            Request::Scope submitting(this, request);
            return callback(std::forward<decltype(arguments)>(arguments)...);
        };
    }
    bool Queue(std::string label, std::function<void()> work, Completion completed,
               Delivery delivery = Delivery::CurrentDocument, Lifetime lifetime = Lifetime::Document);
    // Probe a new request now. Queue also accepts an unreserved FIFO batch or
    // the current workflow's continuation; dispatch always checks admission.
    bool CanQueue(const std::string &label) const;
    // Native access, without the stricter no-edit/no-gesture task admission.
    // Busy describes the queue/worker only; a request waiting for input is idle.
    bool Available() const { return Available(0); }
    DocumentScope CurrentDocument() const;
    void DocumentChanged();
    void Start();
    void Poll();
    // Shutdown only: finish queued/running work before tearing down CK.
    void Finish();
    bool Busy() const { return !Thread() || Active() || !m_Next.empty() || !m_Pending.empty(); }
    const std::string &Label() const;

  private:
    struct Task {
        DocumentScope document;
        std::string label;
        std::function<void()> work;
        Completion completed;
        Delivery delivery = Delivery::CurrentDocument;
        Lifetime lifetime = Lifetime::Document;
        uint64_t request = 0;
    };
    bool Thread() const { return m_Thread == std::this_thread::get_id(); }
    bool Owns(uint64_t id) const { return Thread() && id && id == m_Request; }
    bool Available(uint64_t id) const {
        return Thread() && !Busy() && (!m_Request || id == m_Request) && NativeReady();
    }
    bool NativeReady() const;
    std::string AdmissionError(const std::string &) const;
    std::string RequestError(const Task &) const;
    bool Active() const { return m_Task.work || m_Future.valid() || !m_StartError.empty(); }
    void Promote();
    void Complete(Task task, const std::string &error);
    Admission m_Admission;
    Executor m_Executor;
    Availability m_Available;
    const std::thread::id m_Thread = std::this_thread::get_id();
    uint64_t m_Request = 0, m_Submitting = 0, m_NextRequest = 0;
    mutable bool m_Checking = false;
    std::shared_ptr<int> m_Document = std::make_shared<int>(0);
    std::deque<Task> m_Pending, m_Next;
    bool m_Completing = false;
    Task m_Task;
    std::string m_StartError;
    std::future<std::string> m_Future;
};

} // namespace vdev
