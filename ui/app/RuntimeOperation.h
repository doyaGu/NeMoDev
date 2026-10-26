#pragma once

#include <functional>
#include <future>
#include <string>

namespace vdev {

// One exclusive runtime operation. Queue on the UI thread, Start after
// rendering the frame, Poll before drawing the next. While Busy, the UI
// must not access CK, its document, or runtime-backed panels/renderers.
// Completion always runs in Poll on the UI thread, after the worker exits.
class RuntimeOperation {
public:
    ~RuntimeOperation();
    bool Queue(std::string label, std::function<void()> work,
               std::function<void(const std::string &error)> completed);
    void Start();
    void Poll();
    // Shutdown only: finish queued/running work before tearing down CK.
    void Finish();
    bool Busy() const { return m_Work || m_Future.valid() || !m_StartError.empty(); }
    const std::string &Label() const { return m_Label; }

private:
    std::string m_Label, m_StartError;
    std::function<void()> m_Work;
    std::function<void(const std::string &)> m_Completed;
    std::future<std::string> m_Future;
};

} // namespace vdev
