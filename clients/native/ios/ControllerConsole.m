#import "ControllerConsole.h"
#import "ControllerTransport.h"
@interface ControllerConsole ()
@property NSDictionary *credential;
@property NSString *session;
@property NSMutableString *history;
@property(readwrite) BOOL busy;
@property NSUInteger sequence;
@property NSUInteger cursor;
@property NSUInteger generation;
@property NSUInteger polls;
@property BOOL uncertain;
@property BOOL maintenanceInFlight;
@property NSTimeInterval nextKeepAlive;
@property NSMutableArray<NSString *> *commands;
@property NSUInteger historyIndex;
@property NSString *draft;
@property NSString *refreshError;
@property NSString *failureReason;
@end
@implementation ControllerConsole
- (instancetype)initWithCredential:(NSDictionary *)credential {
    if ((self=[super init])) { _credential=[credential copy]; _commands=[NSMutableArray array]; _draft=@""; _history=[NSMutableString stringWithString:@"Read-only console · timers and ADDRESS APPS STATUS/LIST\n"]; }
    return self;
}
- (NSString *)text { return [self.history copy]; }
- (NSString *)sessionStatus {
    if (self.uncertain) return [NSString stringWithFormat:@"Session needs attention · %@ · reset before retrying",self.failureReason ?: @"unknown outcome"];
    if (self.busy) return @"Session busy";
    if (self.refreshError) return [NSString stringWithFormat:@"Session reconnecting · %@",self.refreshError];
    if (self.session) return @"Session ready · variables retained · keepalive on";
    return @"No session · Run opens one";
}
- (void)clearOutput { [self.history setString:@""]; if (self.changed) self.changed(); }
- (NSString *)previousCommand:(NSString *)draft {
    if (!self.commands.count) return draft;
    if (self.historyIndex==self.commands.count) self.draft=[draft copy];
    if (self.historyIndex>0) self.historyIndex--;
    return self.commands[self.historyIndex];
}
- (NSString *)nextCommand:(NSString *)draft {
    if (self.historyIndex>=self.commands.count) return draft;
    self.historyIndex++;
    return self.historyIndex==self.commands.count ? self.draft : self.commands[self.historyIndex];
}
- (void)keepAliveIfDue:(NSTimeInterval)now {
    if (!self.session || self.busy || self.uncertain || self.maintenanceInFlight || now<self.nextKeepAlive) return;
    self.maintenanceInFlight=YES;
    [self request:@"events" sequence:self.cursor source:@"" reply:^(NSDictionary *body,NSString *error){
        self.maintenanceInFlight=NO;
        if (error) {
            if ([error isEqual:@"session expired or replaced"]) { [self fail:error]; return; }
            // A failed idle read has no command outcome to make uncertain.
            // Keep the handle and retry soon; never silently reopen a session.
            self.refreshError=error;
            self.nextKeepAlive=NSProcessInfo.processInfo.systemUptime+5;
            if (self.changed) self.changed();
            return;
        }
        if (![body[@"sequence"] isKindOfClass:[NSNumber class]] || [body[@"sequence"] unsignedIntegerValue]!=self.sequence ||
            ![body[@"state"] isEqual:@"done"] || ![body[@"cursor"] isKindOfClass:[NSNumber class]] || [body[@"cursor"] unsignedIntegerValue]!=self.cursor) {
            [self fail:@"Session refresh returned unexpected state"]; return;
        }
        BOOL recovered=self.refreshError!=nil;
        self.refreshError=nil;
        self.nextKeepAlive=NSProcessInfo.processInfo.systemUptime+20;
        if (recovered && self.changed) self.changed();
    }];
}
- (void)append:(NSString *)text {
    [self.history appendString:text];
    if (self.history.length>32000) {
        NSRange tail=[self.history rangeOfComposedCharacterSequencesForRange:NSMakeRange(self.history.length-28000,28000)];
        self.history=[[@"[earlier history trimmed]\n" stringByAppendingString:[self.history substringWithRange:tail]] mutableCopy];
    }
    if (self.changed) self.changed();
}
- (void)request:(NSString *)action sequence:(NSUInteger)sequence source:(NSString *)source reply:(void (^)(NSDictionary *,NSString *))reply {
    NSUInteger generation=self.generation;
    ControllerReplRequest(self.credential,action,self.session,sequence,source,^(NSDictionary *body,NSString *error){
        dispatch_async(dispatch_get_main_queue(),^{ if (generation==self.generation) reply(body,error); });
    });
}
- (void)fail:(NSString *)error {
    self.busy=NO; self.uncertain=YES; self.failureReason=error;
    if ([error isEqual:@"session expired or replaced"]) { self.session=nil; self.sequence=self.cursor=0; }
    [self append:[NSString stringWithFormat:@"[%@ · reset session before retrying]\n",error]];
}
- (void)run:(NSString *)source showRC:(BOOL)rc showResult:(BOOL)result {
    if (self.busy) { [self append:@"[command already running]\n"]; return; }
    if (self.uncertain) { [self append:@"[reset session before retrying]\n"]; return; }
    NSMutableString *submitted=[source mutableCopy];
    if (rc) [submitted appendString:@"\nSAY 'RC =' RC"];
    if (result) [submitted appendString:@"\nSAY 'RESULT =' RESULT"];
    if (!source.length || [submitted lengthOfBytesUsingEncoding:NSUTF8StringEncoding]>8192 || [source rangeOfString:@"\0"].location!=NSNotFound) { [self append:@"[command must be 1–8192 UTF-8 bytes without NUL]\n"]; return; }
    // A new command supersedes any harmless idle read already in flight.
    self.generation++; self.maintenanceInFlight=NO; self.refreshError=nil;
    if (![self.commands.lastObject isEqual:source]) {
        [self.commands addObject:[source copy]];
        if (self.commands.count>20) [self.commands removeObjectAtIndex:0];
    }
    self.historyIndex=self.commands.count; self.draft=@"";
    self.busy=YES;
    [self append:[NSString stringWithFormat:@"rexx> %@\n",source]];
    void (^submit)(void)=^{
        NSUInteger next=self.sequence+1;
        [self request:@"run" sequence:next source:submitted reply:^(NSDictionary *body,NSString *error){
            if (error || ![body[@"sequence"] isKindOfClass:[NSNumber class]] || [body[@"sequence"] unsignedIntegerValue]!=next) { [self fail:error ?: @"Invalid command acknowledgement"]; return; }
            self.sequence=next; self.cursor=0; self.polls=0;
            [self poll];
        }];
    };
    if (self.session) { submit(); return; }
    [self request:@"open" sequence:0 source:@"" reply:^(NSDictionary *body,NSString *error){
        NSString *sid=body[@"session"];
        NSCharacterSet *bad=[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"] invertedSet];
        if (error || ![sid isKindOfClass:[NSString class]] || sid.length!=32 || [sid rangeOfCharacterFromSet:bad].location!=NSNotFound) { [self fail:error ?: @"Invalid session acknowledgement"]; return; }
        self.session=sid; submit();
    }];
}
- (void)poll {
    if (++self.polls>400) { [self fail:@"Observation limit reached; command may still be running"]; return; }
    [self request:@"events" sequence:self.cursor source:@"" reply:^(NSDictionary *body,NSString *error){
        if (error) { [self fail:error]; return; }
        if (![body[@"sequence"] isKindOfClass:[NSNumber class]] || [body[@"sequence"] unsignedIntegerValue]!=self.sequence ||
            ![body[@"cursor"] isKindOfClass:[NSNumber class]] || [body[@"cursor"] unsignedIntegerValue]<self.cursor ||
            ![body[@"events"] isKindOfClass:[NSArray class]] || ![body[@"state"] isKindOfClass:[NSString class]] ||
            ![body[@"lost"] isKindOfClass:[NSNumber class]] || ![body[@"cancelled"] isKindOfClass:[NSNumber class]] ||
            ![body[@"exitCode"] isKindOfClass:[NSNumber class]] || ![body[@"runtimeOutcome"] isKindOfClass:[NSNumber class]]) { [self fail:@"Invalid console event response"]; return; }
        NSMutableString *output=[NSMutableString string];
        if ([body[@"lost"] boolValue]) [output appendString:@"[device output buffer overrun]\n"];
        for (id event in body[@"events"]) {
            if (![event isKindOfClass:[NSDictionary class]]) { [self fail:@"Invalid output event"]; return; }
            NSString *hex=event[@"hex"];
            NSCharacterSet *bad=[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdefABCDEF"] invertedSet];
            if (![event[@"end"] isKindOfClass:[NSNumber class]] || ![hex isKindOfClass:[NSString class]] || hex.length%2 || hex.length>16384 || [hex rangeOfCharacterFromSet:bad].location!=NSNotFound) { [self fail:@"Invalid output encoding"]; return; }
            NSMutableData *data=[NSMutableData dataWithLength:hex.length/2];
            for (NSUInteger i=0;i<data.length;i++) { unsigned byte=0; [[NSScanner scannerWithString:[hex substringWithRange:NSMakeRange(i*2,2)]] scanHexInt:&byte]; ((unsigned char *)data.mutableBytes)[i]=(unsigned char)byte; }
            NSString *text=[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
            [output appendString:text ?: @"[non-UTF8 output]"];
            if ([event[@"end"] boolValue]) [output appendString:@"\n"];
        }
        self.cursor=[body[@"cursor"] unsignedIntegerValue];
        if (output.length) [self append:output];
        if ([body[@"state"] isEqual:@"done"]) {
            self.busy=NO;
            self.nextKeepAlive=NSProcessInfo.processInfo.systemUptime+20;
            [self append:[NSString stringWithFormat:@"[exit %@%@]\n",body[@"exitCode"] ?: @"?",[body[@"cancelled"] boolValue] ? @" · cancelled" : @""]];
            return;
        }
        if (![body[@"state"] isEqual:@"running"] && ![body[@"state"] isEqual:@"pending"]) { [self fail:@"Unexpected console state"]; return; }
        NSUInteger generation=self.generation;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,200*NSEC_PER_MSEC),dispatch_get_main_queue(),^{ if (generation==self.generation && self.busy) [self poll]; });
    }];
}
- (void)cancel {
    if (!self.session || !self.busy) { [self append:@"[no running command]\n"]; return; }
    [self request:@"cancel" sequence:0 source:@"" reply:^(NSDictionary *body,NSString *error){
        (void)body;
        if (error) [self append:[NSString stringWithFormat:@"[cancel failed: %@]\n",error]];
        else [self append:@"[cancellation requested]\n"];
    }];
}
- (void)reset {
    if (self.busy) { [self append:@"[cancel and wait for completion before resetting]\n"]; return; }
    if (!self.session) { self.uncertain=NO; self.failureReason=nil; self.refreshError=nil; self.sequence=self.cursor=0; self.generation++; [self append:@"[local session reset]\n"]; return; }
    self.generation++; self.maintenanceInFlight=NO;
    self.busy=YES;
    [self request:@"close" sequence:0 source:@"" reply:^(NSDictionary *body,NSString *error){
        (void)body;
        self.busy=NO;
        if (error && ![error isEqual:@"session expired or replaced"]) { [self fail:error]; return; }
        self.session=nil; self.sequence=self.cursor=0; self.uncertain=NO; self.failureReason=nil; self.refreshError=nil; self.generation++;
        [self append:@"[session closed · variables cleared on next run]\n"];
    }];
}
@end
