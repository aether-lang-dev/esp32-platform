#import "ControllerTransport.h"
#import <dispatch/dispatch.h>
int main(int argc, const char **argv) {
    @autoreleasepool {
        if (argc != 3 && argc != 4) return 2;
        NSData *raw = [NSData dataWithContentsOfFile:@(argv[1])];
        NSMutableDictionary *credential = [[NSJSONSerialization JSONObjectWithData:raw options:NSJSONReadingMutableContainers error:nil] mutableCopy];
        credential[@"host"] = @(argv[2]);
        if (argc == 4) credential[@"port"] = @([@(argv[3]) intValue]);
        ControllerReadStatus(credential, ^(NSDictionary *body, NSString *error) {
            if (error) { fprintf(stderr, "%s\n", error.UTF8String); exit(1); }
            printf("Native TLS1.3-PSK status: active=%lld floor=%lld\n", [body[@"activeVersion"] longLongValue], [body[@"highestVersion"] longLongValue]);
            exit(0);
        });
        dispatch_main();
    }
}
