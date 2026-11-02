#include "ui/app/UiTestRun.h"
#import <Foundation/Foundation.h>

namespace vdev {
void UiTestRun::PrepareProcess() {
    // Tests restore the editor's workspace themselves. AppKit's previous
    // crash recovery prompt must not block an unattended test process.
    // The argument domain is volatile: no user defaults are written.
    @autoreleasepool {
        NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
        NSMutableDictionary *arguments = [[defaults volatileDomainForName:NSArgumentDomain] mutableCopy];
        if (!arguments) arguments = [NSMutableDictionary dictionary];
        arguments[@"ApplePersistenceIgnoreState"] = @YES;
        arguments[@"NSQuitAlwaysKeepsWindows"] = @NO;
        [defaults setVolatileDomain:arguments forName:NSArgumentDomain];
    }
}
} // namespace vdev
