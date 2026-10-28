#include "ui/app/RuntimeOperation.h"

#include <chrono>
#include <exception>
#ifdef _WIN32
#include <objbase.h>
#endif

namespace vdev {

RuntimeOperation::~RuntimeOperation() {
    if (m_Future.valid()) m_Future.wait();
}

bool RuntimeOperation::Queue(std::string label, std::function<void()> work,
                             Completion completed) {
    if (!work) return false;
    // Admission is repeated at dispatch, since an earlier task can change
    // the document or leave an Edit open before this request is reached.
    if (!Busy() && !m_Completing && m_Admission) {
        const std::string error = m_Admission(label);
        if (!error.empty()) {
            Complete({CurrentDocument(), std::move(label), {}, std::move(completed)}, error);
            return false;
        }
    }
    auto &queue = m_Completing ? m_Next : m_Pending;
    queue.push_back({CurrentDocument(), std::move(label), std::move(work), std::move(completed)});
    return true;
}

RuntimeOperation::DocumentScope RuntimeOperation::CurrentDocument() const {
    DocumentScope scope;
    scope.m_Identity = m_Document;
    return scope;
}

void RuntimeOperation::DocumentChanged() { m_Document = std::make_shared<int>(0); }

const std::string &RuntimeOperation::Label() const {
    if (Active()) return m_Task.label;
    if (!m_Next.empty()) return m_Next.front().label;
    if (!m_Pending.empty()) return m_Pending.front().label;
    return m_Task.label; // empty when idle
}

void RuntimeOperation::Complete(Task task, const std::string &error) {
    if (!task.document.IsCurrent() || !task.completed) return;
    struct Scope {
        bool &value;
        bool previous;
        ~Scope() { value = previous; }
    } scope{m_Completing, m_Completing};
    m_Completing = true;
    task.completed(error);
}

void RuntimeOperation::Promote() {
    if (m_Task.work && !m_Task.document.IsCurrent()) m_Task = {};
    while (!Active() && (!m_Next.empty() || !m_Pending.empty())) {
        auto &queue = m_Next.empty() ? m_Pending : m_Next;
        Task task = std::move(queue.front());
        queue.pop_front();
        if (!task.document.IsCurrent()) continue;
        const std::string error = m_Admission ? m_Admission(task.label) : std::string();
        if (!error.empty()) { Complete(std::move(task), error); continue; }
        m_Task = std::move(task);
    }
}

void RuntimeOperation::Start() {
    Promote();
    if (!m_Task.work) return;
    try {
        m_Future = std::async(std::launch::async, [work = std::move(m_Task.work)] {
#ifdef _WIN32
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            struct ComScope { HRESULT result; ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); } } com{initialized};
#endif
            try { work(); return std::string(); }
            catch (const std::exception &error) { return std::string(error.what()); }
            catch (...) { return std::string("The runtime operation raised an exception"); }
        });
        m_Task.work = {};
    } catch (const std::exception &error) {
        m_Task.work = {};
        m_StartError = error.what();
    }
}

void RuntimeOperation::Poll() {
    if (!m_StartError.empty() || (m_Future.valid() && m_Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)) {
        const std::string error = m_StartError.empty() ? m_Future.get() : std::move(m_StartError);
        m_StartError.clear();
        Task task = std::move(m_Task);
        m_Task = {};
        Complete(std::move(task), error);
    }
    Promote();
}

void RuntimeOperation::Finish() {
    while (Busy()) {
        Start();
        if (m_Future.valid()) m_Future.wait();
        Poll();
    }
}

} // namespace vdev
