#import "ControllerTransport.h"
#import <Security/Security.h>

BOOL ControllerCredentialValid(id credential) {
    if (![credential isKindOfClass:[NSDictionary class]]) return NO;
    NSString *identity = credential[@"identity"], *hex = credential[@"key"], *host = credential[@"host"];
    NSCharacterSet *badID = [[NSCharacterSet characterSetWithCharactersInString:@"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-"] invertedSet];
    NSCharacterSet *badHex = [[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdefABCDEF"] invertedSet];
    if (![identity isKindOfClass:[NSString class]] || ![hex isKindOfClass:[NSString class]] || ![host isKindOfClass:[NSString class]] ||
        identity.length < 1 || identity.length > 64 || [identity rangeOfCharacterFromSet:badID].location != NSNotFound ||
        hex.length != 64 || [hex rangeOfCharacterFromSet:badHex].location != NSNotFound ||
        host.length < 1 || host.length > 253 || [host rangeOfCharacterFromSet:badID].location != NSNotFound) {
        return NO;
    }
    if ([[hex lowercaseString] isEqualToString:[@"" stringByPaddingToLength:64 withString:@"0" startingAtIndex:0]] ||
        [[hex lowercaseString] isEqualToString:[@"" stringByPaddingToLength:64 withString:@"f" startingAtIndex:0]]) return NO;
    id port = credential[@"port"];
    if (port && (![port isKindOfClass:[NSNumber class]] || CFGetTypeID((__bridge CFTypeRef)port) == CFBooleanGetTypeID() || [port intValue] < 1 || [port intValue] > 65535)) return NO;
    return YES;
}
