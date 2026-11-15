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

void ControllerReadStatus(NSDictionary *credential, void (^completion)(NSDictionary *, NSString *)) {
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
        int count = aether_controller_tls_status(host.UTF8String, port,
            (AetherValue *)name.bytes, (int)name.length, (AetherValue *)key.mutableBytes, 32,
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
        if (![body isKindOfClass:[NSDictionary class]] || ![body[@"activeVersion"] isKindOfClass:[NSNumber class]] ||
            ![body[@"highestVersion"] isKindOfClass:[NSNumber class]]) {
            completion(nil, @"Invalid device status"); return;
        }
        completion(body,nil);
    });
}
