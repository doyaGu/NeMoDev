#include "tests/ui/ViewportScheduling.h"
#import <Foundation/Foundation.h>

namespace vdevuitest {

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
    if (!m_Activity) return true;
    @autoreleasepool {
        id activity = CFBridgingRelease(m_Activity);
        m_Activity = nullptr;
        [[NSProcessInfo processInfo] endActivity:activity];
    }
    return true;
}

ViewportScheduling::~ViewportScheduling() { Restore(); }

} // namespace vdevuitest
