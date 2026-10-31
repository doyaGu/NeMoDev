#pragma once

#include <cstdint>

namespace vdevuitest {

// Keep a hidden throughput test active without changing focus or priority.
// Thermal preparation and diagnostics run outside the measured frames.
class ViewportScheduling {
public:
    struct Usage {
        std::uint64_t threadNanoseconds = 0, processMicroseconds = 0;
        std::uint64_t voluntarySwitches = 0, involuntarySwitches = 0;
        int qos = 0, priority = 0, thermal = 0, taskRole = -1;
        bool foreground = false, lowPower = false;
    };
    ViewportScheduling() = default;
    ViewportScheduling(const ViewportScheduling &) = delete;
    ViewportScheduling &operator=(const ViewportScheduling &) = delete;
    double CooldownMilliseconds() const { return m_CooldownMilliseconds; }
#ifdef __APPLE__
    static constexpr int MinimumCooldownMilliseconds = 30000;
    static constexpr int MaximumCooldownMilliseconds = 180000;
    ~ViewportScheduling();
    bool Start();
    bool Restore();
    bool WaitForNominal();
    bool Snapshot(Usage &) const;
    static const char *Name() { return "user-initiated-activity"; }
    static const char *ThermalPolicy() { return "nominal"; }
private:
    void *m_Activity = nullptr;
#else
    static constexpr int MinimumCooldownMilliseconds = 0;
    static constexpr int MaximumCooldownMilliseconds = 0;
    bool Start() { return true; }
    bool Restore() { return true; }
    bool WaitForNominal() { return true; }
    bool Snapshot(Usage &) const { return false; }
    static const char *Name() { return "system-default"; }
    static const char *ThermalPolicy() { return "system-default"; }
#endif
private:
    double m_CooldownMilliseconds = 0;
};

} // namespace vdevuitest
