#import "ControllerConsole.h"
#import "ControllerActions.h"
#import <dispatch/dispatch.h>
int main(int argc,const char **argv) {
    @autoreleasepool {
        if (argc!=3 && argc!=4) return 2;
        NSMutableDictionary *credential=[[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:@(argv[1])] options:NSJSONReadingMutableContainers error:nil] mutableCopy];
        credential[@"host"]=@(argv[2]);
        if (argc==4) credential[@"port"]=@([@(argv[3]) intValue]);
        ControllerConsole *console=[[ControllerConsole alloc] initWithCredential:credential];
        NSArray *names=@[@"app",@"relay",@"countdown"];
        NSArray *denied=@[@"ADDRESS SWITCH 'ON 1'",@"ADDRESS OUTLET 'START 1'",@"ADDRESS OUTLET 'CANCEL'",@"ADDRESS APPS 'RUN main'",@"ADDRESS APPS 'CALL main outlet CANCEL'"];
        __block NSUInteger index=0;
        dispatch_source_t timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());
        dispatch_source_set_timer(timer,DISPATCH_TIME_NOW,100*NSEC_PER_MSEC,10*NSEC_PER_MSEC);
        dispatch_source_set_event_handler(timer,^{
            if (console.busy) return;
            NSUInteger step=index++;
            if (step<names.count) {
                NSString *name=names[step];
                [console runNamedSource:ControllerActionSource(name) completion:^(NSString *output,NSString *error){
                    NSString *failure=error;
                    NSString *summary=error ? nil : ControllerActionSummary(name,output,&failure);
                    if (!summary) { fprintf(stderr,"%s\n",(failure ?: @"Invalid action response").UTF8String); exit(1); }
                    printf("%s\n",summary.UTF8String); fflush(stdout);
                }];
            } else if (step<names.count+denied.count) {
                NSString *command=denied[step-names.count];
                [console runNamedSource:[command stringByAppendingString:@"\nSAY 'CONTROL_RC=' RC"] completion:^(NSString *output,NSString *error){
                    if (error || ![output isEqual:@"CONTROL_RC= 13\n"]) { fprintf(stderr,"Control denial failed: %s\n",(error ?: output).UTF8String); exit(1); }
                    puts("Control operation denied RC13");
                }];
            } else if (step==names.count+denied.count) {
                if (![[console previousCommand:@"unsent draft"] isEqual:@"unsent draft"]) { fputs("Named actions polluted typed-command history\n",stderr); exit(1); }
                [console reset];
            } else { puts("Named ADDRESS actions and host-enforced control denials PASS"); exit(0); }
        });
        dispatch_resume(timer);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,60*NSEC_PER_SEC),dispatch_get_main_queue(),^{fputs("Actions probe timed out\n",stderr);exit(1);});
        dispatch_main();
    }
}
