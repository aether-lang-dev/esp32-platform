#import "ControllerConsole.h"
#import "ControllerTransport.h"
static unsigned calls;
void ControllerReplRequest(NSDictionary *credential,NSString *action,NSString *session,NSUInteger sequence,NSString *source,void (^reply)(NSDictionary *,NSString *)) {
    (void)credential; (void)action; (void)session; (void)sequence; (void)source; (void)reply;
    calls++; // Deliberately pending: a second Run must never submit again.
}
int main(void) {
    @autoreleasepool {
        ControllerConsole *console=[[ControllerConsole alloc] initWithCredential:@{}];
        [console run:@"" showRC:YES showResult:YES];
        [console run:[@"" stringByPaddingToLength:8192 withString:@"x" startingAtIndex:0] showRC:YES showResult:YES];
        [console run:[@"" stringByPaddingToLength:3000 withString:@"€" startingAtIndex:0] showRC:NO showResult:NO];
        unichar zero=0;
        [console run:[NSString stringWithCharacters:&zero length:1] showRC:NO showResult:NO];
        if (calls || console.busy) return 1;
        [console run:@"SAY 42" showRC:NO showResult:NO];
        [console run:@"SAY 43" showRC:NO showResult:NO];
        [console reset];
        if (calls!=1 || !console.busy || ![console.text containsString:@"already running"]) return 2;
        puts("Native console UTF-8/source bounds and duplicate-Run guard PASS");
    }
    return 0;
}
