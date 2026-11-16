#import "ControllerConsole.h"
#import "ControllerActions.h"
#import <dispatch/dispatch.h>
int main(int argc,const char **argv) {
    @autoreleasepool {
        if ((argc!=3 && argc!=4) || !getenv("ALLOW_LIVE_OUTLET_CONTROL") || strcmp(getenv("ALLOW_LIVE_OUTLET_CONTROL"),"1")) { fputs("Requires ALLOW_LIVE_OUTLET_CONTROL=1 and a confirmed empty outlet\n",stderr); return 2; }
        NSMutableDictionary *credential=[[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:@(argv[1])] options:NSJSONReadingMutableContainers error:nil] mutableCopy];
        credential[@"host"]=@(argv[2]);
        if (argc==4) credential[@"port"]=@([@(argv[3]) intValue]);
        ControllerConsole *console=[[ControllerConsole alloc] initWithCredential:credential];
        NSArray *actions=@[@"off",@"on10",@"relay",@"close",@"relay",@"countdown",@"on30",@"off",@"relay",@"countdown",@"deny",@"close"];
        __block NSUInteger index=0;
        __block NSTimeInterval waitUntil=0;
        dispatch_source_t timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());
        dispatch_source_set_timer(timer,DISPATCH_TIME_NOW,100*NSEC_PER_MSEC,10*NSEC_PER_MSEC);
        dispatch_source_set_event_handler(timer,^{
            if (console.busy || NSProcessInfo.processInfo.systemUptime<waitUntil) return;
            if (index==actions.count) { puts("Timed outlet presets, timer after session close, Off and denied direct relay control PASS"); exit(0); }
            NSUInteger step=index++;
            NSString *action=actions[step];
            if ([action isEqual:@"close"]) {
                [console reset];
                if (step==3) { waitUntil=NSProcessInfo.processInfo.systemUptime+12; puts("Session closed; waiting for plug-owned timer"); fflush(stdout); }
                return;
            }
            NSString *source=[action isEqual:@"deny"] ? @"ADDRESS SWITCH 'ON 1'\nSAY 'CONTROL_RC=' RC" : ControllerActionSource(action);
            [console runNamedSource:source completion:^(NSString *output,NSString *error){
                if ([action isEqual:@"deny"]) {
                    if (error || ![output isEqual:@"CONTROL_RC= 13\n"]) { fputs("Direct relay control was not denied\n",stderr);exit(1); }
                    puts("Direct relay control denied RC13"); return;
                }
                NSString *failure=error;
                NSString *summary=error ? nil : ControllerActionSummary(action,output,&failure);
                BOOL expected=summary!=nil;
                if (step==2) expected=expected && [summary isEqual:@"Relay: ON"];
                if (step==4 || step==8) expected=expected && [summary isEqual:@"Relay: OFF"];
                if (step==5 || step==9) expected=expected && [summary isEqual:@"Countdown: idle · 0 s"];
                if (!expected) { fprintf(stderr,"Control probe failed: %s\n",(failure ?: summary ?: @"unexpected state").UTF8String); exit(1); }
                printf("%s\n",summary.UTF8String); fflush(stdout);
            }];
        });
        dispatch_resume(timer);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,90*NSEC_PER_SEC),dispatch_get_main_queue(),^{fputs("Control probe timed out; plug timer remains bounded\n",stderr);exit(1);});
        dispatch_main();
    }
}
