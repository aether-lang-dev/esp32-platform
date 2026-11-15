#import "ControllerTransport.h"
#import <Network/Network.h>
#import <Security/Security.h>

/* Bounded TLS1.3 external PSK; certificate fallback is explicitly rejected. */
@interface ControllerRequest : NSObject
@property nw_connection_t connection;
@property dispatch_queue_t queue;
@property NSMutableData *received;
@property(copy) void (^completion)(NSDictionary *, NSString *);
@property BOOL finished;
@property NSString *lastState;
@end
@implementation ControllerRequest
- (void)finish:(NSDictionary *)body error:(NSString *)error {
    if (self.finished) return;
    self.finished = YES;
    nw_connection_cancel(self.connection);
    self.completion(body, error);
    self.completion = nil;
    nw_connection_set_state_changed_handler(self.connection, nil);
}
- (void)receive {
    nw_connection_receive(self.connection, 1, 4096, ^(dispatch_data_t content, nw_content_context_t context, bool complete, nw_error_t error) {
        (void)context;
        if (self.finished) return;
        if (content) dispatch_data_apply(content, ^bool(dispatch_data_t region, size_t offset, const void *buffer, size_t size) {
            (void)region; (void)offset;
            [self.received appendBytes:buffer length:size];
            return true;
        });
        if (self.received.length > 16384) { [self finish:nil error:@"Response exceeds limit"]; return; }
        NSData *boundary = [@"\r\n\r\n" dataUsingEncoding:NSUTF8StringEncoding];
        NSRange split = [self.received rangeOfData:boundary options:0 range:NSMakeRange(0, self.received.length)];
        if (split.location != NSNotFound) {
            NSData *headerData = [self.received subdataWithRange:NSMakeRange(0, split.location)];
            NSString *headers = [[NSString alloc] initWithData:headerData encoding:NSASCIIStringEncoding];
            if (![headers hasPrefix:@"HTTP/1.1 200 "] && ![headers hasPrefix:@"HTTP/1.0 200 "]) {
                [self finish:nil error:@"Device rejected status request"]; return;
            }
            NSUInteger start = NSMaxRange(split);
            NSData *bodyData = [self.received subdataWithRange:NSMakeRange(start, self.received.length-start)];
            id body = [NSJSONSerialization JSONObjectWithData:bodyData options:0 error:nil];
            if ([body isKindOfClass:[NSDictionary class]] && [body[@"activeVersion"] isKindOfClass:[NSNumber class]] &&
                [body[@"highestVersion"] isKindOfClass:[NSNumber class]]) {
                [self finish:body error:nil]; return;
            }
        }
        if (error || complete) { [self finish:nil error:@"Incomplete device response"]; return; }
        [self receive];
    });
}
@end

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

void ControllerReadStatus(NSDictionary *credential, void (^completion)(NSDictionary *, NSString *)) {
    if (!ControllerCredentialValid(credential)) { completion(nil, @"Invalid native credential"); return; }
    NSString *identity = credential[@"identity"], *hex = credential[@"key"], *host = credential[@"host"];
    NSMutableData *key = [NSMutableData dataWithLength:32];
    for (NSUInteger i=0; i<32; ++i) {
        unsigned value=0;
        [[NSScanner scannerWithString:[hex substringWithRange:NSMakeRange(i*2, 2)]] scanHexInt:&value];
        ((unsigned char *)key.mutableBytes)[i] = (unsigned char)value;
    }
    ControllerRequest *request = [ControllerRequest new];
    request.queue = dispatch_queue_create("dev.aether.controller.transport", DISPATCH_QUEUE_SERIAL);
    request.received = [NSMutableData data];
    request.completion = completion;
    NSNumber *port = credential[@"port"] ?: @443;
    if (![port isKindOfClass:[NSNumber class]] || port.intValue < 1 || port.intValue > 65535) {
        completion(nil, @"Invalid native endpoint"); return;
    }
    nw_parameters_t parameters = nw_parameters_create_secure_tcp(^(nw_protocol_options_t tls) {
        sec_protocol_options_t options = nw_tls_copy_sec_protocol_options(tls);
        sec_protocol_options_set_min_tls_protocol_version(options, tls_protocol_version_TLSv13);
        sec_protocol_options_set_max_tls_protocol_version(options, tls_protocol_version_TLSv13);
        sec_protocol_options_append_tls_ciphersuite(options, tls_ciphersuite_AES_128_GCM_SHA256);
        sec_protocol_options_append_tls_ciphersuite(options, (tls_ciphersuite_t)0x00a8); /* Apple PSK configuration; minimum remains TLS1.3. */
        NSData *idData = [identity dataUsingEncoding:NSUTF8StringEncoding];
        dispatch_data_t secret = dispatch_data_create(key.bytes, key.length, request.queue, ^{ (void)key; });
        dispatch_data_t name = dispatch_data_create(idData.bytes, idData.length, request.queue, ^{ (void)idData; });
        sec_protocol_options_add_pre_shared_key(options, secret, name);
        sec_protocol_options_set_peer_authentication_required(options, false); /* PSK binder authenticates; certificate path below is rejected. */
        sec_protocol_options_set_verify_block(options, ^(sec_protocol_metadata_t metadata, sec_trust_t trust, sec_protocol_verify_complete_t complete) {
            (void)metadata; (void)trust;
            complete(false); /* This path must authenticate by PSK, never a certificate. */
        }, request.queue);
    }, NW_PARAMETERS_DEFAULT_CONFIGURATION);
    request.connection = nw_connection_create(nw_endpoint_create_host(host.UTF8String, port.stringValue.UTF8String), parameters);
    nw_connection_set_queue(request.connection, request.queue);
    nw_connection_set_state_changed_handler(request.connection, ^(nw_connection_state_t state, nw_error_t error) {
        if (request.finished) return;
        request.lastState = [NSString stringWithFormat:@"state %d (%d:%d)", (int)state,
            error ? (int)nw_error_get_error_domain(error) : 0, error ? nw_error_get_error_code(error) : 0];
        if (state == nw_connection_state_failed) {
            NSString *message = [NSString stringWithFormat:@"TLS connection failed (%d:%d)",
                error ? (int)nw_error_get_error_domain(error) : 0, error ? nw_error_get_error_code(error) : 0];
            [request finish:nil error:message];
        } else if (state == nw_connection_state_ready) {
            NSString *http = [NSString stringWithFormat:@"GET /status HTTP/1.1\r\nHost: %@\r\nConnection: close\r\n\r\n", host];
            NSData *bytes = [http dataUsingEncoding:NSUTF8StringEncoding];
            dispatch_data_t data = dispatch_data_create(bytes.bytes, bytes.length, request.queue, ^{ (void)bytes; });
            nw_connection_send(request.connection, data, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, true, ^(nw_error_t sendError) {
                if (sendError) [request finish:nil error:@"Status send failed"];
                else [request receive];
            });
        }
    });
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10*NSEC_PER_SEC), request.queue, ^{
        if (!request.finished) [request finish:nil error:[@"Device connection timed out: " stringByAppendingString:request.lastState ?: @"starting"]];
    });
    nw_connection_start(request.connection);
}
