#include "application/RuntimeOperation.h"

#include <chrono>
#include <exception>
#include <utility>
#ifdef _WIN32
#include <objbase.h>
#endif

namespace vdev {

RuntimeOperation::Request::Scope::Scope(RuntimeOperation *runtime, uint64_t id)
    : m_Runtime(runtime && runtime->Thread() && !runtime->m_Checking ? runtime : nullptr) {
    if (m_Runtime) {
        m_Previous = m_Runtime->m_Submitting;
        m_Runtime->m_Submitting = id;
    }
}
RuntimeOperation::Request::Scope::~Scope() {
    if (m_Runtime) m_Runtime->m_Submitting = m_Runtime->Owns(m_Previous) ? m_Previous : 0;
}
RuntimeOperation::Request::Request(Request &&other) noexcept
    : m_Runtime(std::exchange(other.m_Runtime, nullptr)), m_Id(std::exchange(other.m_Id, 0)) {}
RuntimeOperation::Request &RuntimeOperation::Request::operator=(Request &&other) noexcept {
    if (this != &other) {
        Reset();
        m_Runtime = std::exchange(other.m_Runtime, nullptr);
        m_Id = std::exchange(other.m_Id, 0);
    }
    return *this;
}
RuntimeOperation::Request::~Request() {
    Reset();
}
RuntimeOperation::Request::operator bool() const {
    return m_Runtime && m_Runtime->Owns(m_Id);
}
bool RuntimeOperation::Request::Available() const {
    return bool(*this) && m_Runtime->Available(m_Id);
}
RuntimeOperation::Request::Scope RuntimeOperation::Request::Enter() const {
    return Scope(m_Runtime, m_Id);
}
void RuntimeOperation::Request::Reset() {
    if (!m_Runtime || !m_Runtime->Thread() || m_Runtime->m_Checking) return;
    if (m_Runtime->m_Request == m_Id) m_Runtime->m_Request = 0;
    if (m_Runtime->m_Submitting == m_Id) m_Runtime->m_Submitting = 0;
    m_Runtime = nullptr;
    m_Id = 0;
}
RuntimeOperation::Request RuntimeOperation::Begin(const std::string &label) {
    if (!Available() || m_Checking || !AdmissionError(label).empty()) return {};
    do {
        ++m_NextRequest;
    } while (!m_NextRequest);
    m_Request = m_NextRequest;
    return Request(this, m_Request);
}
bool RuntimeOperation::CanQueue(const std::string &label) const {
    return Thread() && !m_Checking && Available(m_Submitting) && AdmissionError(label).empty();
}
std::string RuntimeOperation::AdmissionError(const std::string &label) const {
    if (!NativeReady()) return "The native runtime is unavailable";
    if (!m_Admission) return {};
    struct Checking {
        bool &active;
        ~Checking() { active = false; }
    } checking{m_Checking};
    m_Checking = true;
    try {
        return m_Admission(label);
    } catch (const std::exception &error) {
        return error.what()[0] ? error.what() : "Runtime task admission failed";
    } catch (...) {
        return "Runtime task admission failed";
    }
}
bool RuntimeOperation::NativeReady() const {
    if (m_Checking) return false;
    if (!m_Available) return true;
    struct Checking {
        bool &active;
        ~Checking() { active = false; }
    } checking{m_Checking};
    m_Checking = true;
    try {
        return m_Available();
    } catch (...) {
        return false;
    }
}
std::string RuntimeOperation::RequestError(const Task &task) const {
    if (task.request && task.request != m_Request) return "The operation ended before the task started";
    if (m_Request && task.request != m_Request) return "Finish the current operation before " + task.label;
    return {};
}

RuntimeOperation::~RuntimeOperation() {
    if (m_Future.valid()) m_Future.wait();
}

bool RuntimeOperation::Queue(std::string label, std::function<void()> work, Completion completed, Delivery delivery,
                             Lifetime lifetime) {
    if (!Thread() || m_Checking || !work) return false;
    Task task{CurrentDocument(), std::move(label), std::move(work), std::move(completed),
              delivery,          lifetime,         m_Submitting};
    auto error = RequestError(task);
    // Admission is repeated at dispatch, since an earlier task can change
    // the document or leave an Edit open before this request is reached.
    if (error.empty() && !Busy() && !m_Completing) error = AdmissionError(task.label);
    if (!error.empty()) {
        task.work = {};
        Complete(std::move(task), error);
        return false;
    }
    auto &queue = m_Completing ? m_Next : m_Pending;
    queue.push_back(std::move(task));
    return true;
}

RuntimeOperation::DocumentScope RuntimeOperation::CurrentDocument() const {
    DocumentScope scope;
    if (Thread()) scope.m_Identity = m_Document;
    return scope;
}

void RuntimeOperation::DocumentChanged() {
    if (!Thread() || m_Checking) return;
    m_Document = std::make_shared<int>(0);
}

const std::string &RuntimeOperation::Label() const {
    static const std::string empty;
    if (!Thread()) return empty;
    if (Active()) return m_Task.label;
    if (!m_Next.empty()) return m_Next.front().label;
    if (!m_Pending.empty()) return m_Pending.front().label;
    return m_Task.label; // empty when idle
}

void RuntimeOperation::Complete(Task task, const std::string &error) {
    if ((!task.document.IsCurrent() && task.lifetime == Lifetime::Document &&
         task.delivery == Delivery::CurrentDocument) ||
        !task.completed)
        return;
    struct Scope {
        bool &value;
        bool previous;
        ~Scope() { value = previous; }
    } scope{m_Completing, m_Completing};
    m_Completing = true;
    Request::Scope submitting(this, task.request);
    task.completed(error);
}

void RuntimeOperation::Promote() {
    if (m_Task.work && m_Task.lifetime == Lifetime::Document && !m_Task.document.IsCurrent()) {
        Task expired = std::move(m_Task);
        m_Task = {};
        Complete(std::move(expired), "The document changed before the operation started");
    }
    while (!Active() && (!m_Next.empty() || !m_Pending.empty())) {
        auto &queue = m_Next.empty() ? m_Pending : m_Next;
        Task task = std::move(queue.front());
        queue.pop_front();
        if (task.lifetime == Lifetime::Document && !task.document.IsCurrent()) {
            Complete(std::move(task), "The document changed before the operation started");
            continue;
        }
        auto error = RequestError(task);
        if (error.empty()) error = AdmissionError(task.label);
        if (!error.empty()) {
            Complete(std::move(task), error);
            continue;
        }
        m_Task = std::move(task);
    }
}

void RuntimeOperation::Start() {
    if (!Thread() || m_Checking || m_Completing) return;
    Promote();
    if (!m_Task.work) return;
    // Poll can promote before drawing. Edits or input may start during that
    // frame; repeat admission immediately before transferring native ownership.
    auto error = RequestError(m_Task);
    if (error.empty()) error = AdmissionError(m_Task.label);
    if (!error.empty()) {
        auto refused = std::move(m_Task);
        m_Task = {};
        refused.work = {};
        Complete(std::move(refused), error);
        return;
    }
    try {
        m_Future = std::async(std::launch::async,
                              [work = std::move(m_Task.work), label = m_Task.label, executor = m_Executor] {
#ifdef _WIN32
                                  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                                  struct ComScope {
                                      HRESULT result;
                                      ~ComScope() {
                                          if (SUCCEEDED(result)) CoUninitialize();
                                      }
                                  } com{initialized};
#endif
                                  try {
                                      if (executor)
                                          executor(label, work);
                                      else
                                          work();
                                      return std::string();
                                  } catch (const std::exception &error) {
                                      return std::string(error.what());
                                  } catch (...) {
                                      return std::string("The runtime operation raised an exception");
                                  }
                              });
        m_Task.work = {};
    } catch (const std::exception &error) {
        m_Task.work = {};
        m_StartError = error.what();
    }
}

void RuntimeOperation::Poll() {
    if (!Thread() || m_Checking || m_Completing) return;
    if (!m_StartError.empty() ||
        (m_Future.valid() && m_Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)) {
        const std::string error = m_StartError.empty() ? m_Future.get() : std::move(m_StartError);
        m_StartError.clear();
        Task task = std::move(m_Task);
        m_Task = {};
        Complete(std::move(task), error);
    }
    Promote();
}

void RuntimeOperation::Finish() {
    if (!Thread() || m_Checking || m_Completing) return;
    while (Busy()) {
        Start();
        if (m_Future.valid()) m_Future.wait();
        Poll();
    }
}

} // namespace vdev
