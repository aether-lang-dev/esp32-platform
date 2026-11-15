#import "ControllerActions.h"
#import <math.h>
NSString *ControllerActionSource(NSString *identifier) {
    if (![identifier isKindOfClass:[NSString class]]) return nil;
    NSDictionary *commands=@{@"app":@"ADDRESS APPS 'STATUS main'",@"relay":@"ADDRESS SWITCH 'STATUS'",@"countdown":@"ADDRESS OUTLET 'STATUS'"};
    NSString *command=commands[identifier];
    if (!command) return nil;
    return [command stringByAppendingString:@"\nSAY 'AE_ACTION_RC=' RC\nSAY 'AE_ACTION_RESULT=' RESULT"];
}
static BOOL integer(id value) {
    return [value isKindOfClass:[NSNumber class]] && CFGetTypeID((__bridge CFTypeRef)value)!=CFBooleanGetTypeID() &&
        [value doubleValue]>=0 && [value doubleValue]<=UINT32_MAX && floor([value doubleValue])==[value doubleValue];
}
static NSString *refuse(NSString **error,NSString *message) { if (error) *error=message; return nil; }
NSString *ControllerActionSummary(NSString *identifier,NSString *output,NSString **error) {
    if (error) *error=nil;
    if (!ControllerActionSource(identifier) || ![output isKindOfClass:[NSString class]] || output.length>16384) return refuse(error,@"Invalid named action response");
    NSString *rc=nil,*result=nil;
    for (NSString *line in [output componentsSeparatedByString:@"\n"]) {
        if (!line.length) continue;
        if ([line hasPrefix:@"AE_ACTION_RC="] && !rc) rc=[[line substringFromIndex:13] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        else if ([line hasPrefix:@"AE_ACTION_RESULT="] && !result) result=[[line substringFromIndex:17] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        else return refuse(error,@"Unexpected named action output");
    }
    NSScanner *scanner=[NSScanner scannerWithString:rc ?: @""];
    NSInteger code=-1;
    if (!rc || !result || ![scanner scanInteger:&code] || !scanner.isAtEnd || code<0) return refuse(error,@"Incomplete named action response");
    if (code) return refuse(error,[NSString stringWithFormat:@"Action rejected (RC %ld)",(long)code]);
    if ([identifier isEqual:@"relay"]) {
        if (![result isEqual:@"ON"] && ![result isEqual:@"OFF"]) return refuse(error,@"Invalid relay status");
        return [@"Relay: " stringByAppendingString:result];
    }
    id body=[NSJSONSerialization JSONObjectWithData:[result dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil];
    if (![body isKindOfClass:[NSDictionary class]]) return refuse(error,@"Invalid status object");
    if ([identifier isEqual:@"app"]) {
        NSString *name=body[@"name"];
        NSCharacterSet *bad=[[NSCharacterSet characterSetWithCharactersInString:@"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-"] invertedSet];
        if (![body[@"app"] isEqual:@"main"] || ![name isKindOfClass:[NSString class]] || !name.length || name.length>64 ||
            [name rangeOfCharacterFromSet:bad].location!=NSNotFound || !integer(body[@"activeVersion"]) || !integer(body[@"highestVersion"]) ||
            [body[@"activeVersion"] unsignedLongLongValue]>[body[@"highestVersion"] unsignedLongLongValue]) return refuse(error,@"Invalid app status");
        return [NSString stringWithFormat:@"App: %@ · v%@ · floor %@",name,body[@"activeVersion"],body[@"highestVersion"]];
    }
    id busy=body[@"busy"];
    if (![body[@"app"] isEqual:@"outlet"] || !integer(body[@"remaining"]) || [body[@"remaining"] intValue]>60 ||
        ![busy isKindOfClass:[NSNumber class]] || CFGetTypeID((__bridge CFTypeRef)busy)!=CFBooleanGetTypeID() ||
        ![body[@"state"] isEqual:([body[@"remaining"] intValue] ? @"running" : @"idle")]) return refuse(error,@"Invalid countdown status");
    return [NSString stringWithFormat:@"Countdown: %@ · %@ s%@",body[@"state"],body[@"remaining"],[busy boolValue] ? @" · output busy" : @""];
}
