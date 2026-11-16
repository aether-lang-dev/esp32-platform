#import "ControllerActions.h"
static void require(BOOL ok,const char *why) { if (!ok) { fprintf(stderr,"FAIL %s\n",why); exit(1); } }
static NSString *packet(NSString *result) { return [NSString stringWithFormat:@"AE_ACTION_RC= 0\nAE_ACTION_RESULT= %@\n",result]; }
int main(void) {
    @autoreleasepool {
        require(!ControllerActionSource(nil) && !ControllerActionSource(@"relay; ADDRESS SWITCH 'ON 1'") && !ControllerActionSource(@"start"),"unknown/injected names refused");
        require([ControllerActionSource(@"relay") hasPrefix:@"ADDRESS SWITCH 'STATUS'\n"],"fixed relay read");
        require([ControllerActionSource(@"countdown") hasPrefix:@"ADDRESS OUTLET 'STATUS'\n"],"fixed countdown read");
        NSString *error=nil;
        require([ControllerActionSummary(@"relay",packet(@"OFF"),&error) isEqual:@"Relay: OFF"] && !error,"relay response");
        require([ControllerActionSummary(@"app",packet(@"{\"app\":\"main\",\"name\":\"bench\",\"activeVersion\":6,\"highestVersion\":7}"),&error) containsString:@"v6 · floor 7"],"app response");
        require([ControllerActionSummary(@"countdown",packet(@"{\"app\":\"outlet\",\"state\":\"idle\",\"remaining\":0,\"busy\":false}"),&error) isEqual:@"Countdown: idle · 0 s"],"countdown response");
        NSArray *invalid=@[@"",@"AE_ACTION_RC= 0\n",@"AE_ACTION_RC= 0\nAE_ACTION_RC= 0\nAE_ACTION_RESULT= OFF\n",@"unexpected\nAE_ACTION_RC= 0\nAE_ACTION_RESULT= OFF\n",@"AE_ACTION_RC= 0oops\nAE_ACTION_RESULT= OFF\n",@"AE_ACTION_RC= 13\nAE_ACTION_RESULT= OFF\n",packet(@"OFF\nON"),packet(@"on")];
        for (NSString *text in invalid) require(!ControllerActionSummary(@"relay",text,&error) && error.length,"malformed or rejected response not success");
        require(!ControllerActionSummary(@"app",packet(@"{\"app\":\"main\",\"name\":\"bench\",\"activeVersion\":true,\"highestVersion\":7}"),&error),"boolean version refused");
        require(!ControllerActionSummary(@"countdown",packet(@"{\"app\":\"outlet\",\"state\":\"idle\",\"remaining\":30,\"busy\":false}"),&error),"inconsistent countdown refused");
        require([ControllerActionSource(@"on10") hasPrefix:@"ADDRESS OUTLET 'START 10'\n"] && [ControllerActionSource(@"on30") hasPrefix:@"ADDRESS OUTLET 'START 30'\n"] && [ControllerActionSource(@"off") hasPrefix:@"ADDRESS OUTLET 'CANCEL'\n"],"fixed bounded control sources");
        require(!ControllerActionSource(@"on") && !ControllerActionSource(@"on999"),"no indefinite or arbitrary On");
        require([ControllerActionSummary(@"on10",packet(@"{\"app\":\"outlet\",\"state\":\"running\",\"remaining\":10,\"busy\":false}"),&error) isEqual:@"Outlet: ON · auto-off in 10 s"],"timed On acknowledgement");
        require([ControllerActionSummary(@"off",packet(@"{\"app\":\"outlet\",\"state\":\"idle\",\"remaining\":0,\"busy\":false}"),&error) isEqual:@"Outlet: OFF"],"Off acknowledgement");
        require(!ControllerActionSummary(@"on10",packet(@"{\"app\":\"outlet\",\"state\":\"running\",\"remaining\":30,\"busy\":false}"),&error),"reject longer than selected preset");
        require(!ControllerActionSummary(@"off",packet(@"{\"app\":\"outlet\",\"state\":\"running\",\"remaining\":1,\"busy\":false}"),&error),"never report Off while timer runs");
        require(!ControllerActionSummary(@"off",packet(@"{\"app\":\"outlet\",\"state\":\"idle\",\"remaining\":0,\"busy\":true}"),&error),"never report Off when another owner holds output");
        puts("Named action allowlist, framing, denial and typed status validation PASS");
    }
    return 0;
}
