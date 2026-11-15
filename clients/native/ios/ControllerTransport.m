#import "ControllerTransport.h"
#include "aether_config.h"
#include "aether_wipe.h"
#import "ControllerTLS.h"
#import <arpa/inet.h>
#import <sys/socket.h>
#import <strings.h>
#import <unistd.h>

/* A duplicate references the same socket even if its original fd is reused.
 * Expiry and completion serialize ownership, so cancellation never closes
 * somebody else's descriptor. No secret values live in this timer object. */
@interface ControllerDeadline : NSObject
@property int fd;
@property BOOL done;
@property BOOL expired;
@end
@implementation ControllerDeadline
- (instancetype)init { if ((self=[super init])) _fd=-1; return self; }
- (BOOL)attach:(int)fd {
    @synchronized(self) {
        if (self.done || self.expired || self.fd>=0) return NO;
        self.fd=dup(fd);
        return self.fd>=0;
    }
}
- (void)expire {
    @synchronized(self) {
        if (self.done) return;
        self.expired=YES;
        if (self.fd>=0) { shutdown(self.fd,SHUT_RDWR); close(self.fd); self.fd=-1; }
    }
}
- (BOOL)finish {
    @synchronized(self) {
        self.done=YES;
        if (self.fd>=0) { close(self.fd); self.fd=-1; }
        return self.expired;
    }
}
@end
static _Thread_local void *activeDeadline;
extern int tcp_fd_raw(void *socket);
int controller_tls_deadlines(void *socket) {
    ControllerDeadline *deadline=(__bridge ControllerDeadline *)activeDeadline;
    int fd=tcp_fd_raw(socket);
    struct timeval interval={.tv_sec=0,.tv_usec=500000};
    return !deadline || fd<0 || ![deadline attach:fd] ||
        setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&interval,sizeof interval)!=0 ||
        setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&interval,sizeof interval)!=0 ? -1 : 0;
}

static void ControllerRequest(NSDictionary *credential, NSData *request, BOOL statusOnly, void (^completion)(NSDictionary *, NSString *)) {
    if (!ControllerCredentialValid(credential)) { completion(nil, @"Invalid native credential"); return; }
    NSString *host = credential[@"host"], *identity = credential[@"identity"], *hex = credential[@"key"];
    struct in_addr address;
    if (inet_pton(AF_INET, host.UTF8String, &address) != 1) {
        completion(nil, @"Use the plug's IPv4 address for this connection proof"); return;
    }
    int port = [credential[@"port"] ?: @443 intValue];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSMutableData *key = [NSMutableData dataWithLength:32];
        for (NSUInteger i=0; i<32; ++i) {
            unsigned value=0;
            [[NSScanner scannerWithString:[hex substringWithRange:NSMakeRange(i*2, 2)]] scanHexInt:&value];
            ((unsigned char *)key.mutableBytes)[i] = (unsigned char)value;
        }
        NSData *name = [identity dataUsingEncoding:NSUTF8StringEncoding];
        NSMutableData *output = [NSMutableData dataWithLength:16384];
        NSMutableData *error = [NSMutableData dataWithLength:256];
        ControllerDeadline *deadline=[ControllerDeadline new];
        activeDeadline=(__bridge void *)deadline;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC),dispatch_get_global_queue(QOS_CLASS_UTILITY,0), ^{ [deadline expire]; });
        int count = aether_controller_tls_request(host.UTF8String, port,
            (AetherValue *)name.bytes, (int)name.length, (AetherValue *)key.mutableBytes, 32,
            (AetherValue *)request.bytes, (int)request.length,
            (AetherValue *)output.mutableBytes, (int)output.length, (AetherValue *)error.mutableBytes, (int)error.length);
        activeDeadline=NULL;
        BOOL expired=[deadline finish];
        aether_wipe(key.mutableBytes, key.length);
        if (expired) { completion(nil,@"Device connection timed out"); return; }
        if (aether_last_outcome() != 0) { completion(nil, @"Native TLS runtime failure"); return; }
        if (count <= 0 || count > (int)output.length) {
            NSString *why = [[NSString alloc] initWithBytes:error.bytes length:strnlen(error.bytes,error.length) encoding:NSUTF8StringEncoding];
            completion(nil, why.length ? why : @"TLS request failed"); return;
        }
        NSData *bodyData = [output subdataWithRange:NSMakeRange(0,(NSUInteger)count)];
        id body = [NSJSONSerialization JSONObjectWithData:bodyData options:0 error:nil];
        if ([body isKindOfClass:[NSDictionary class]] && [body[@"error"] isKindOfClass:[NSString class]]) { completion(nil,body[@"error"]); return; }
        if (![body isKindOfClass:[NSDictionary class]] || (statusOnly && (![body[@"activeVersion"] isKindOfClass:[NSNumber class]] ||
            ![body[@"highestVersion"] isKindOfClass:[NSNumber class]]))) {
            completion(nil, @"Invalid device status"); return;
        }
        completion(body,nil);
    });
}

void ControllerReadStatus(NSDictionary *credential, void (^completion)(NSDictionary *, NSString *)) {
    if (!ControllerCredentialValid(credential)) { completion(nil,@"Invalid native credential"); return; }
    NSString *http=[NSString stringWithFormat:@"GET /status HTTP/1.1\r\nHost: %@\r\nConnection: close\r\n\r\n",credential[@"host"]];
    ControllerRequest(credential,[http dataUsingEncoding:NSUTF8StringEncoding],YES,completion);
}
void ControllerReplRequest(NSDictionary *credential, NSString *action, NSString *session,
    NSUInteger sequence, NSString *source, void (^completion)(NSDictionary *, NSString *)) {
    NSDictionary *routes=@{@"open":@"/repl/open",@"run":@"/repl/command",@"events":@"/repl/events",@"cancel":@"/repl/cancel",@"close":@"/repl/close"};
    if (!ControllerCredentialValid(credential) || !routes[action] || sequence>UINT32_MAX) { completion(nil,@"Invalid console request"); return; }
    BOOL open=[action isEqual:@"open"], events=[action isEqual:@"events"], run=[action isEqual:@"run"];
    NSCharacterSet *bad=[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"] invertedSet];
    if (!open && (session.length!=32 || [session rangeOfCharacterFromSet:bad].location!=NSNotFound)) { completion(nil,@"Invalid console session"); return; }
    // Console initially requests timers and read-only APPS access, no relay grants.
    NSString *body=open ? @"1027" : (run ? source : @"");
    NSData *data=[body dataUsingEncoding:NSUTF8StringEncoding];
    if (!data || (run && (!data.length || data.length>8192 || [body rangeOfString:@"\0"].location!=NSNotFound))) { completion(nil,@"Command must be 1–8192 UTF-8 bytes without NUL"); return; }
    NSMutableString *http=[NSMutableString stringWithFormat:@"%@ %@ HTTP/1.1\r\nHost: %@\r\nConnection: close\r\n",events ? @"GET" : @"POST",routes[action],credential[@"host"]];
    if (!open) [http appendFormat:@"X-Repl-Session: %@\r\n",session];
    if (run) [http appendFormat:@"X-Repl-Sequence: %lu\r\n",(unsigned long)sequence];
    if (events) [http appendFormat:@"X-Repl-After: %lu\r\n",(unsigned long)sequence];
    if (!events) [http appendFormat:@"Content-Length: %lu\r\n",(unsigned long)data.length];
    [http appendString:@"\r\n"];
    NSMutableData *request=[[http dataUsingEncoding:NSUTF8StringEncoding] mutableCopy];
    if (!events) [request appendData:data];
    ControllerRequest(credential,request,NO,completion);
}
