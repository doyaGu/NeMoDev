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
                             std::function<void(const std::string &)> completed) {
    if (Busy() || !work) return false;
    m_Label = std::move(label);
    m_Work = std::move(work);
    m_Completed = std::move(completed);
    return true;
}

void RuntimeOperation::Start() {
    if (!m_Work) return;
    try {
        m_Future = std::async(std::launch::async, [work = std::move(m_Work)] {
#ifdef _WIN32
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            struct ComScope { HRESULT result; ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); } } com{initialized};
#endif
            try { work(); return std::string(); }
            catch (const std::exception &error) { return std::string(error.what()); }
            catch (...) { return std::string("The runtime operation raised an exception"); }
        });
        m_Work = {};
    } catch (const std::exception &error) {
        m_Work = {};
        m_StartError = error.what();
    }
}

void RuntimeOperation::Poll() {
    if (m_StartError.empty() && (!m_Future.valid() || m_Future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)) return;
    const std::string error = m_StartError.empty() ? m_Future.get() : std::move(m_StartError);
    m_StartError.clear();
    m_Label.clear();
    auto completed = std::move(m_Completed);
    m_Completed = {};
    if (completed) completed(error);
}

void RuntimeOperation::Finish() {
    while (Busy()) {
        Start();
        if (m_Future.valid()) m_Future.wait();
        Poll();
    }
}

} // namespace vdev
