// Observe the unmodified production app through the native window server.
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <signal.h>
#include <stdio.h>

static void Check(BOOL condition, NSString *message) {
    if (!condition) [NSException raise:@"PackageStartupFailure" format:@"%@", message];
}

static void WaitForEvents(void) {
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.1]];
}

int main(int argc, const char **argv) {
    @autoreleasepool {
        if (argc != 2) return 2;
        NSString *app = [NSString stringWithUTF8String:argv[1]];
        NSString *temporary = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
        NSFileManager *files = NSFileManager.defaultManager;
        [files createDirectoryAtPath:temporary withIntermediateDirectories:YES attributes:nil error:NULL];
        NSString *logPath = [temporary stringByAppendingPathComponent:@"startup.log"];
        [files createFileAtPath:logPath contents:nil attributes:nil];
        NSFileHandle *log = [NSFileHandle fileHandleForWritingAtPath:logPath];
        NSTask *task = [NSTask new];
        int result = 1;
        @try {
            task.executableURL = [NSURL fileURLWithPath:[app stringByAppendingPathComponent:@"Contents/MacOS/NeMoDev"]];
            task.currentDirectoryURL = [NSURL fileURLWithPath:temporary];
            NSMutableDictionary *environment = [NSProcessInfo.processInfo.environment mutableCopy];
            for (NSString *key in [environment.allKeys copy])
                if ([key hasPrefix:@"DYLD_"] || [key hasPrefix:@"LD_"] || [key hasPrefix:@"SDL_"] ||
                    [@[@"VIRTOOLS_PATH", @"LIBRARY_PATH", @"CMAKE_PREFIX_PATH"] containsObject:key])
                    [environment removeObjectForKey:key];
            environment[@"PATH"] = @"/usr/bin:/bin:/usr/sbin:/sbin";
            environment[@"SDL_AUDIO_DRIVER"] = @"dummy";
            task.environment = environment;
            task.standardOutput = log;
            task.standardError = log;
            NSError *error = nil;
            Check([task launchAndReturnError:&error], error.localizedDescription ?: @"Application launch failed");
            BOOL initialized = NO;
            NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:60];
            while (task.running && deadline.timeIntervalSinceNow > 0) {
                NSArray *windows = CFBridgingRelease(CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID));
                for (NSDictionary *window in windows) {
                    NSString *title = window[(__bridge id)kCGWindowName];
                    if ([window[(__bridge id)kCGWindowOwnerPID] intValue] == task.processIdentifier &&
                        [title containsString:@"NeMoDev"] && [title containsString:@"New Composition"]) {
                        Check(![title hasSuffix:@" *"], @"The initial composition is unexpectedly modified");
                        initialized = YES;
                        break;
                    }
                }
                if (initialized) break;
                WaitForEvents();
            }
            Check(initialized, @"The relocated production app did not show its initialized composition window");
            NSRunningApplication *application = [NSRunningApplication runningApplicationWithProcessIdentifier:task.processIdentifier];
            Check([application terminate], @"Cannot request normal application closure");
            deadline = [NSDate dateWithTimeIntervalSinceNow:20];
            while (task.running && deadline.timeIntervalSinceNow > 0) WaitForEvents();
            Check(!task.running, @"Application did not respond to normal closure");
            Check(task.terminationStatus == 0, @"The relocated production app failed during shutdown");
            puts("Relocated macOS editor: native GPU initialization, new composition window, clean shutdown");
            result = 0;
        } @catch (NSException *failure) {
            fprintf(stderr, "%s\n", failure.reason.UTF8String);
        } @finally {
            if (task.running) {
                kill(task.processIdentifier, SIGKILL);
                [task waitUntilExit];
            }
            [log closeFile];
            NSData *output = [files contentsAtPath:logPath];
            if (output.length) fwrite(output.bytes, 1, output.length, stdout);
            [files removeItemAtPath:temporary error:NULL];
        }
        return result;
    }
}
