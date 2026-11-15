#import "ControllerConsole.h"
#import "ControllerTransport.h"
static NSUInteger opens, runs, reads, currentSequence;
static BOOL holdRead, expireRead, failRead;
static void (^held)(NSDictionary *,NSString *);
void ControllerReplRequest(NSDictionary *credential,NSString *action,NSString *session,NSUInteger sequence,NSString *source,void (^reply)(NSDictionary *,NSString *)) {
    (void)credential; (void)session; (void)source;
    if ([action isEqual:@"open"]) { opens++; reply(@{@"session":@"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",@"sequence":@0},nil); }
    else if ([action isEqual:@"run"]) { runs++; currentSequence=sequence; reply(@{@"sequence":@(sequence)},nil); }
    else if ([action isEqual:@"events"]) {
        reads++;
        if (holdRead) { holdRead=NO; held=[reply copy]; return; }
        if (failRead) { reply(nil,@"Device connection timed out"); return; }
        if (expireRead) { reply(nil,@"session expired or replaced"); return; }
        reply(@{@"sequence":@(currentSequence),@"cursor":@1,@"state":@"done",@"lost":@NO,@"cancelled":@NO,@"exitCode":@0,@"runtimeOutcome":@0,@"events": sequence ? @[] : @[@{@"hex":@"3432",@"end":@YES}]},nil);
    } else reply(@{@"accepted":@YES},nil);
}
static void require(BOOL ok,const char *why) { if (!ok) { fprintf(stderr,"FAIL %s\n",why); exit(1); } }
int main(void) {
    @autoreleasepool {
        ControllerConsole *console=[[ControllerConsole alloc] initWithCredential:@{}];
        __block int stage=0;
        dispatch_source_t timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());
        dispatch_source_set_timer(timer,DISPATCH_TIME_NOW,10*NSEC_PER_MSEC,NSEC_PER_MSEC);
        dispatch_source_set_event_handler(timer,^{
            if (console.busy) return;
            switch (stage++) {
                case 0:
                    [console keepAliveIfDue:NSProcessInfo.processInfo.systemUptime+100];
                    require(!reads && !opens,"no session means no keepalive or automatic open");
                    [console run:@"answer = 41" showRC:NO showResult:NO]; break;
                case 1: {
                    require([console.sessionStatus containsString:@"variables retained"],"ready status");
                    [console clearOutput];
                    require(!console.text.length && runs==1,"Clear output preserves session without executing");
                    require([[console previousCommand:@"unsent draft"] isEqual:@"answer = 41"],"recall raw command");
                    require([[console nextCommand:@"ignored edit"] isEqual:@"unsent draft"],"restore unsent draft");
                    NSUInteger before=reads;
                    [console keepAliveIfDue:NSProcessInfo.processInfo.systemUptime+1];
                    require(reads==before,"do not refresh early");
                    holdRead=YES;
                    [console keepAliveIfDue:NSProcessInfo.processInfo.systemUptime+21];
                    [console keepAliveIfDue:NSProcessInfo.processInfo.systemUptime+22];
                    require(reads==before+1 && held,"only one idle request in flight");
                    [console run:@"SAY answer + 1" showRC:NO showResult:NO];
                    held(nil,@"session expired or replaced"); held=nil;
                    break;
                }
                case 2:
                    require(runs==2 && opens==1 && [console.sessionStatus containsString:@"variables retained"],"stale refresh cannot expire a new command");
                    [console clearOutput];
                    require([[console previousCommand:@"another draft"] isEqual:@"SAY answer + 1"],"Clear output preserves recall");
                    require([[console previousCommand:@"ignored edit"] isEqual:@"answer = 41"],"Previous reaches older command");
                    require([[console nextCommand:@"ignored edit"] isEqual:@"SAY answer + 1"],"Next reaches newer command");
                    require([[console nextCommand:@"ignored edit"] isEqual:@"another draft"],"Next restores original draft");
                    require([[console nextCommand:@"draft at newest"] isEqual:@"draft at newest"],"Next at newest preserves edited draft");
                    failRead=YES;
                    [console keepAliveIfDue:NSProcessInfo.processInfo.systemUptime+21];
                    break;
                case 3:
                    require([console.sessionStatus containsString:@"reconnecting"] && opens==1,"transient read retains the session");
                    failRead=NO;
                    [console keepAliveIfDue:NSProcessInfo.processInfo.systemUptime+6];
                    break;
                case 4:
                    require([console.sessionStatus containsString:@"variables retained"] && opens==1,"retry recovers without reopening");
                    expireRead=YES;
                    [console keepAliveIfDue:NSProcessInfo.processInfo.systemUptime+21];
                    break;
                default:
                    require([console.sessionStatus containsString:@"needs attention"] && opens==1,"expiry is visible, never silently replaced");
                    [console clearOutput];
                    require([console.sessionStatus containsString:@"session expired or replaced"],"Clear output retains failure reason");
                    [console reset];
                    require([console.sessionStatus containsString:@"No session"],"reset clears expired session state");
                    expireRead=NO;
                    [console runNamedSource:@"SAY 42" completion:^(NSString *output,NSString *error){
                        require(!error && [output isEqual:@"42\n"],"named capture contains only command output");
                        require([[console previousCommand:@"draft"] isEqual:@"SAY answer + 1"],"named source does not enter typed-command recall");
                        puts("Session keepalive timing, races, expiry, recall and isolated named output PASS");
                        exit(0);
                    }];
                    break;
            }
        });
        dispatch_resume(timer);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),dispatch_get_main_queue(),^{fputs("Session test timed out\n",stderr);exit(1);});
        dispatch_main();
    }
}
