#import "ControllerConsole.h"
#import <dispatch/dispatch.h>
int main(int argc,const char **argv) {
    @autoreleasepool {
        if (argc!=3 && argc!=4) return 2;
        NSMutableDictionary *credential=[[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:@(argv[1])] options:NSJSONReadingMutableContainers error:nil] mutableCopy];
        credential[@"host"]=@(argv[2]);
        if (argc==4) credential[@"port"]=@([@(argv[3]) intValue]);
        ControllerConsole *console=[[ControllerConsole alloc] initWithCredential:credential];
        __block int stage=0;
        __block BOOL cancelled=NO;
        __block NSUInteger offset=0;
        dispatch_source_t timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());
        dispatch_source_set_timer(timer,dispatch_time(DISPATCH_TIME_NOW,0),100*NSEC_PER_MSEC,10*NSEC_PER_MSEC);
        dispatch_source_set_event_handler(timer,^{
            if (console.busy) {
                if (stage==4 && !cancelled && [console.text containsString:@"streaming\n"]) { cancelled=YES; [console cancel]; }
                return;
            }
            NSString *part=[console.text substringFromIndex:MIN(offset,console.text.length)];
            if ([part containsString:@"reset session before retrying"]) { fprintf(stderr,"%s",console.text.UTF8String); exit(1); }
            if (stage==2 && ![part containsString:@"42\n"]) { fprintf(stderr,"Missing persistent-variable result\n%s",console.text.UTF8String); exit(1); }
            if (stage==3 && ![part containsString:@"activeVersion"]) { fprintf(stderr,"Missing ADDRESS status\n%s",console.text.UTF8String); exit(1); }
            if (stage==4 && ![part containsString:@"cancelled"]) { fprintf(stderr,"Missing cancellation\n%s",console.text.UTF8String); exit(1); }
            if (stage==5 && ![part containsString:@"13\n"]) { fprintf(stderr,"Relay command was not denied\n%s",console.text.UTF8String); exit(1); }
            if (stage==7 && ![part containsString:@"recovered\n"]) { fprintf(stderr,"Missing recovery after syntax error\n%s",console.text.UTF8String); exit(1); }
            if (stage==9 && ![part containsString:@"X\n"]) { fprintf(stderr,"Reset did not clear variables\n%s",console.text.UTF8String); exit(1); }
            offset=console.text.length;
            switch (stage++) {
                case 0: [console run:@"x = 41" showRC:YES showResult:YES]; break;
                case 1: [console run:@"SAY x + 1" showRC:NO showResult:NO]; break;
                case 2: [console run:@"ADDRESS APPS 'STATUS main'\nSAY RESULT" showRC:YES showResult:YES]; break;
                case 3: [console run:@"SAY 'streaming'\nDO FOREVER\nCALL WAIT_MS 100\nEND" showRC:NO showResult:NO]; break;
                case 4: [console run:@"ADDRESS SWITCH 'ON 1'\nSAY RC" showRC:NO showResult:NO]; break;
                case 5: [console run:@"SAY (" showRC:NO showResult:NO]; break;
                case 6: [console run:@"SAY 'recovered'" showRC:NO showResult:NO]; break;
                case 7: [console reset]; break;
                case 8: [console run:@"SAY x" showRC:NO showResult:NO]; break;
                case 9: [console reset]; break;
                default: printf("Native console persistence, ADDRESS, denied relay, streaming, cancel, syntax recovery and reset PASS\n%s",console.text.UTF8String); exit(0);
            }
        });
        dispatch_resume(timer);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,90*NSEC_PER_SEC),dispatch_get_main_queue(),^{fprintf(stderr,"Console probe timed out\n%s",console.text.UTF8String);exit(1);});
        dispatch_main();
    }
}
