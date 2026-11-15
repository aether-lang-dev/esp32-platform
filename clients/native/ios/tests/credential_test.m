#import "ControllerTransport.h"
#include <assert.h>
int main(void) {
    @autoreleasepool {
        NSString *hex = [@"" stringByPaddingToLength:64 withString:@"1" startingAtIndex:0];
        NSDictionary *valid = @{@"identity":@"unit-test",@"key":hex,@"host":@"127.0.0.1"};
        assert(ControllerCredentialValid(valid));
        assert(!ControllerCredentialValid(@[]));
        assert(!ControllerCredentialValid(nil));
        for (NSDictionary *patch in @[@{@"key":@"bad"},@{@"identity":@"bad\nidentity"},
            @{@"host":@"bad\r\nHost:other"},@{@"port":@0},@{@"port":@65536},@{@"port":@YES},
            @{@"key":[@"" stringByPaddingToLength:64 withString:@"0" startingAtIndex:0]},
            @{@"key":[@"" stringByPaddingToLength:64 withString:@"f" startingAtIndex:0]}]) {
            NSMutableDictionary *candidate = [valid mutableCopy]; [candidate addEntriesFromDictionary:patch];
            assert(!ControllerCredentialValid(candidate));
        }
        puts("Native credential type, bounds, endpoint injection and empty-key checks PASS");
    }
}
