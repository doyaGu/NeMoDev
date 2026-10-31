#include "tests/ui/ViewportScheduling.h"
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <sys/resource.h>
#include <time.h>
#include <mach/mach.h>
#include <mach/task_policy.h>

namespace vdevuitest {
namespace {
bool GetRole(task_role_t &role) {
    task_category_policy_data_t category{};
    mach_msg_type_number_t count = TASK_CATEGORY_POLICY_COUNT;
    boolean_t defaults = FALSE;
    if (task_policy_get(mach_task_self(), TASK_CATEGORY_POLICY, reinterpret_cast<task_policy_t>(&category),
                        &count, &defaults) != KERN_SUCCESS) return false;
    role = category.role;
    return true;
}
std::uint64_t Milliseconds() {
    timespec time{};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return std::uint64_t(time.tv_sec) * 1000 + time.tv_nsec / 1000000;
}
}

bool ViewportScheduling::Start() {
    if (m_Activity) return false;
    @autoreleasepool {
        id activity = [[NSProcessInfo processInfo]
            beginActivityWithOptions:NSActivityUserInitiated
                              reason:@"NeMoDev viewport throughput measurement"];
        m_Activity = (__bridge_retained void *)activity;
    }
    return m_Activity != nullptr;
}

bool ViewportScheduling::Restore() {
    if (m_Activity) {
        @autoreleasepool {
            id activity = CFBridgingRelease(m_Activity);
            m_Activity = nullptr;
            [[NSProcessInfo processInfo] endActivity:activity];
        }
    }
    return true;
}

ViewportScheduling::~ViewportScheduling() { Restore(); }

bool ViewportScheduling::WaitForNominal() {
    const auto start = Milliseconds();
    std::uint64_t nominalSince = 0;
    @autoreleasepool {
        NSProcessInfo *info = [NSProcessInfo processInfo];
        for (;;) {
            const auto now = Milliseconds();
            m_CooldownMilliseconds = double(now - start);
            if (now - start >= MaximumCooldownMilliseconds) return false;
            bool lowPower = false;
            if (@available(macOS 12.0, *)) lowPower = info.lowPowerModeEnabled;
            if (info.thermalState == NSProcessInfoThermalStateNominal && !lowPower) {
                if (!nominalSince) nominalSince = now;
                if (now - nominalSince >= MinimumCooldownMilliseconds) return true;
            } else nominalSince = 0;
            const timespec interval{0, 250000000};
            nanosleep(&interval, nullptr);
        }
    }
}

bool ViewportScheduling::Snapshot(Usage &usage) const {
    timespec thread{};
    rusage process{};
    qos_class_t qos = QOS_CLASS_UNSPECIFIED;
    int priority = 0;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &thread) || getrusage(RUSAGE_SELF, &process) ||
        pthread_get_qos_class_np(pthread_self(), &qos, &priority)) return false;
    usage.threadNanoseconds = std::uint64_t(thread.tv_sec) * 1000000000ull + thread.tv_nsec;
    usage.processMicroseconds = std::uint64_t(process.ru_utime.tv_sec + process.ru_stime.tv_sec) * 1000000ull +
                                process.ru_utime.tv_usec + process.ru_stime.tv_usec;
    usage.voluntarySwitches = process.ru_nvcsw;
    usage.involuntarySwitches = process.ru_nivcsw;
    usage.qos = int(qos);
    usage.priority = priority;
    task_role_t role = TASK_UNSPECIFIED;
    if (GetRole(role)) usage.taskRole = int(role);
    @autoreleasepool {
        usage.thermal = int([NSProcessInfo processInfo].thermalState);
        usage.foreground = [NSRunningApplication currentApplication].active;
        if (@available(macOS 12.0, *)) usage.lowPower = [NSProcessInfo processInfo].lowPowerModeEnabled;
    }
    return true;
}

} // namespace vdevuitest
