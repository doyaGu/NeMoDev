#pragma once

namespace vdevuitest {

// Keep a hidden throughput test active without changing window focus or the
// application's foreground/background role. No work is done per frame.
class ViewportScheduling {
public:
    ViewportScheduling() = default;
    ViewportScheduling(const ViewportScheduling &) = delete;
    ViewportScheduling &operator=(const ViewportScheduling &) = delete;
#ifdef __APPLE__
    ~ViewportScheduling();
    bool Start();
    bool Restore();
    static const char *Name() { return "user-initiated-activity"; }
private:
    void *m_Activity = nullptr;
#else
    bool Start() { return true; }
    bool Restore() { return true; }
    static const char *Name() { return "system-default"; }
#endif
};

} // namespace vdevuitest
