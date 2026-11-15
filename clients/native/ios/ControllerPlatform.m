#import "ControllerTransport.h"
#import "ControllerConsole.h"
#import "ControllerHistory.h"
#import <Security/Security.h>
#import <UIKit/UIKit.h>
static NSString *display = @"Ready · tap Check plug status";
static BOOL busy;
static NSUInteger checks;
static int revision;
static void consoleProbe(void);
static void layoutProbe(void);
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
    if (getenv("CONTROLLER_LAYOUT_PROBE")) dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),dispatch_get_main_queue(),^{ layoutProbe(); });
    if (getenv("CONTROLLER_PROBE_AUTORUN")) {
        controller_import_credential();
        controller_check_status();
        if (getenv("CONTROLLER_CONSOLE_PROBE")) consoleProbe();
    }
}

static ControllerConsole *console;
static NSString *consoleDisplay=@"Enter a command, then tap Run.";
static int consoleRevision;
static BOOL historyFollowRequested;
static ControllerConsole *ownerConsole(void) {
    if (console) return console;
    NSMutableDictionary *query=[keyQuery() mutableCopy];
    query[(__bridge id)kSecReturnData]=@YES;
    CFTypeRef value=NULL;
    if (SecItemCopyMatching((__bridge CFDictionaryRef)query,&value)!=errSecSuccess) return nil;
    NSData *raw=CFBridgingRelease(value);
    NSDictionary *credential=[NSJSONSerialization JSONObjectWithData:raw options:0 error:nil];
    if (!ControllerCredentialValid(credential)) return nil;
    console=[[ControllerConsole alloc] initWithCredential:credential];
    console.changed=^{
        consoleDisplay=console.text; consoleRevision++;
        NSURL *cache=[[[NSFileManager defaultManager] URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask] firstObject];
        [[NSJSONSerialization dataWithJSONObject:@{@"transcript":consoleDisplay,@"busy":@(console.busy),@"sessionState":console.sessionStatus} options:0 error:nil] writeToURL:[cache URLByAppendingPathComponent:@"console-result.json"] atomically:YES];
    };
    return console;
}
const char *controller_console_text(void) { return consoleDisplay.UTF8String; }
int controller_console_length(void) { return (int)[consoleDisplay lengthOfBytesUsingEncoding:NSUTF8StringEncoding]; }
int controller_console_revision(void) { return consoleRevision; }
void controller_console_run(const char *source,int rc,int result) {
    historyFollowRequested=YES;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes)
        if ([scene isKindOfClass:[UIWindowScene class]])
            for (UIWindow *window in ((UIWindowScene *)scene).windows) [window endEditing:YES];
    ControllerConsole *owner=ownerConsole();
    if (!owner) { consoleDisplay=@"Import an owner credential first."; consoleRevision++; return; }
    [owner run:[NSString stringWithUTF8String:source] ?: @"" showRC:rc!=0 showResult:result!=0];
}
void controller_console_cancel(void) { [ownerConsole() cancel]; }
void controller_console_reset(void) { [ownerConsole() reset]; }

static void consoleProbe(void) {
    controller_console_run("x = 41\nSAY x + 1\nADDRESS APPS 'STATUS main'\nSAY RESULT",1,1);
}

extern void *aether_ui_get_widget(int handle);
void controller_command_editor(int handle) {
    UIView *view=(__bridge UIView *)aether_ui_get_widget(handle);
    if (![view isKindOfClass:[UITextView class]]) return;
    UITextView *editor=(UITextView *)view;
    editor.autocorrectionType=UITextAutocorrectionTypeNo;
    editor.autocapitalizationType=UITextAutocapitalizationTypeNone;
    editor.smartQuotesType=UITextSmartQuotesTypeNo;
    editor.smartDashesType=UITextSmartDashesTypeNo;
    UIToolbar *toolbar=[[UIToolbar alloc] initWithFrame:CGRectMake(0,0,320,44)];
    toolbar.items=@[[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil],
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:editor action:@selector(resignFirstResponder)]];
    editor.inputAccessoryView=toolbar;
}

// Explicit development-only geometry report: no credential fields or handles.
static void layoutRows(UIView *view, UIWindow *window, NSMutableArray *rows) {
    CGRect frame=[view convertRect:view.bounds toView:window];
    NSMutableDictionary *row=[@{@"kind":NSStringFromClass(view.class),@"x":@(frame.origin.x),@"y":@(frame.origin.y),@"width":@(frame.size.width),@"height":@(frame.size.height)} mutableCopy];
    if ([view isKindOfClass:[UILabel class]]) row[@"text"]=((UILabel *)view).text ?: @"";
    if ([view isKindOfClass:[UIScrollView class]]) { row[@"contentWidth"]=@(((UIScrollView *)view).contentSize.width); row[@"contentHeight"]=@(((UIScrollView *)view).contentSize.height); }
    [rows addObject:row];
    for (UIView *child in view.subviews) layoutRows(child,window,rows);
}
static void layoutProbe(void) {
    NSMutableArray *rows=[NSMutableArray array];
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes)
        if ([scene isKindOfClass:[UIWindowScene class]])
            for (UIWindow *window in ((UIWindowScene *)scene).windows) layoutRows(window,window,rows);
    NSURL *cache=[[[NSFileManager defaultManager] URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask] firstObject];
    [[NSJSONSerialization dataWithJSONObject:rows options:0 error:nil] writeToURL:[cache URLByAppendingPathComponent:@"console-layout.json"] atomically:YES];
}

int controller_history_begin(int handle) {
    UIScrollView *scroll=(__bridge UIScrollView *)aether_ui_get_widget(handle);
    if (![scroll isKindOfClass:[UIScrollView class]]) return 0;
    int follow=historyFollowRequested ? 2 : (ControllerHistoryAtBottom(scroll) ? 1 : 0);
    historyFollowRequested=NO;
    return follow;
}
void controller_history_end(int handle,int follow) {
    UIScrollView *scroll=(__bridge UIScrollView *)aether_ui_get_widget(handle);
    if ([scroll isKindOfClass:[UIScrollView class]]) {
        ControllerHistoryFollow(scroll,follow!=0);
        if (follow==2) ControllerHistoryReveal(scroll);
    }
}

void controller_console_tick(void) { [console keepAliveIfDue:NSProcessInfo.processInfo.systemUptime]; }
const char *controller_console_state(void) { return (console ? console.sessionStatus : @"No session · Run opens one").UTF8String; }
int controller_console_state_length(void) { return (int)[(console ? console.sessionStatus : @"No session · Run opens one") lengthOfBytesUsingEncoding:NSUTF8StringEncoding]; }
void controller_console_clear(void) {
    if (console) [console clearOutput];
    else { consoleDisplay=@""; consoleRevision++; }
}
static NSString *recalled=@"";
void controller_console_recall(int direction,const char *source) {
    NSString *draft=[NSString stringWithUTF8String:source] ?: @"";
    ControllerConsole *owner=ownerConsole();
    recalled=owner ? (direction<0 ? [owner previousCommand:draft] : [owner nextCommand:draft]) : draft;
}
const char *controller_console_recalled(void) { return recalled.UTF8String; }
int controller_console_recalled_length(void) { return (int)[recalled lengthOfBytesUsingEncoding:NSUTF8StringEncoding]; }
