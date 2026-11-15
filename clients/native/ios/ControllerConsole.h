#import <Foundation/Foundation.h>
@interface ControllerConsole : NSObject
@property(nonatomic,readonly) NSString *text;
@property(nonatomic,readonly) BOOL busy;
@property(nonatomic,readonly) NSString *sessionStatus;
@property(nonatomic,copy) void (^changed)(void);
- (instancetype)initWithCredential:(NSDictionary *)credential;
- (void)run:(NSString *)source showRC:(BOOL)rc showResult:(BOOL)result;
- (void)runNamedSource:(NSString *)source completion:(void (^)(NSString *,NSString *))completion;
- (void)keepAliveIfDue:(NSTimeInterval)now;
- (void)clearOutput;
- (NSString *)previousCommand:(NSString *)draft;
- (NSString *)nextCommand:(NSString *)draft;
- (void)cancel;
- (void)reset;
@end
