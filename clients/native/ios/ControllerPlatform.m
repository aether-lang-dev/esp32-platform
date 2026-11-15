#import "ControllerTransport.h"
#import <Security/Security.h>
#import <UIKit/UIKit.h>
static NSString *display = @"No owner credential imported";
static BOOL busy;
static NSUInteger checks;
static int revision;
static void publish(NSString *text) {
    display = text;
    revision++;
    NSURL *cache = [[[NSFileManager defaultManager] URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask] firstObject];
    NSData *report = [NSJSONSerialization dataWithJSONObject:@{@"status":text} options:0 error:nil];
    [report writeToURL:[cache URLByAppendingPathComponent:@"status-result.json"] atomically:YES];
}
static NSDictionary *keyQuery(void) {
    return @{(__bridge id)kSecClass:(__bridge id)kSecClassGenericPassword,
             (__bridge id)kSecAttrService:@"dev.aether.controller.owner",
             (__bridge id)kSecAttrAccount:@"owner-v1",
             (__bridge id)kSecAttrSynchronizable:@NO};
}
int controller_status_revision(void) { return revision; }
const char *controller_status_text(void) { return display.UTF8String; }
int controller_status_length(void) { return (int)[display lengthOfBytesUsingEncoding:NSUTF8StringEncoding]; }
void controller_import_credential(void) {
    NSURL *cache = [[[NSFileManager defaultManager] URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask] firstObject];
    NSURL *path = [cache URLByAppendingPathComponent:@"owner-credential.json"];
    NSNumber *exclude = @YES;
    [path setResourceValue:exclude forKey:NSURLIsExcludedFromBackupKey error:nil];
    NSData *raw = [NSData dataWithContentsOfURL:path];
    if (!raw || raw.length > 2048) { publish(@"No local credential import staged"); return; }
    NSDictionary *data = [NSJSONSerialization JSONObjectWithData:raw options:0 error:nil];
    if (!ControllerCredentialValid(data)) { publish(@"Invalid credential import"); return; }
    NSMutableDictionary *query = [keyQuery() mutableCopy];
    NSDictionary *attributes = @{(__bridge id)kSecValueData:raw,
        (__bridge id)kSecAttrAccessible:(__bridge id)kSecAttrAccessibleWhenPasscodeSetThisDeviceOnly};
    OSStatus status = SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef)attributes);
    if (status == errSecItemNotFound) {
        [query addEntriesFromDictionary:attributes];
        status = SecItemAdd((__bridge CFDictionaryRef)query, NULL);
    }
    if (status != errSecSuccess) { publish(@"Local Keychain import failed"); return; }
    [[NSFileManager defaultManager] removeItemAtURL:path error:nil];
    publish(@"Owner credential stored on this phone only");
}
void controller_check_status(void) {
    if (busy) return;
    NSMutableDictionary *query = [keyQuery() mutableCopy];
    query[(__bridge id)kSecReturnData] = @YES;
    CFTypeRef value = NULL;
    if (SecItemCopyMatching((__bridge CFDictionaryRef)query, &value) != errSecSuccess) {
        publish(@"Import an owner credential first"); return;
    }
    NSData *raw = CFBridgingRelease(value);
    NSDictionary *credential = [NSJSONSerialization JSONObjectWithData:raw options:0 error:nil];
    busy = YES;
    NSUInteger check = ++checks;
    publish([NSString stringWithFormat:@"Check %lu · connecting securely…", (unsigned long)check]);
    ControllerReadStatus(credential, ^(NSDictionary *body, NSString *error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            busy = NO;
            NSDateFormatter *clock = [[NSDateFormatter alloc] init];
            clock.dateFormat = @"HH:mm:ss";
            NSString *result = error ?: [NSString stringWithFormat:@"Connected · app v%@ · floor %@", body[@"activeVersion"], body[@"highestVersion"]];
            publish([NSString stringWithFormat:@"Check %lu · %@\n%@", (unsigned long)check, [clock stringFromDate:[NSDate date]], result]);
        });
    });
}

void controller_probe_if_requested(void) {
    if (getenv("CONTROLLER_PROBE_AUTORUN")) {
        controller_import_credential();
        controller_check_status();
    }
}
