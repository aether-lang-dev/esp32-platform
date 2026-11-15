#import <Foundation/Foundation.h>
@interface ControllerConsole : NSObject
@property(nonatomic,readonly) NSString *text;
@property(nonatomic,readonly) BOOL busy;
@property(nonatomic,copy) void (^changed)(void);
- (instancetype)initWithCredential:(NSDictionary *)credential;
- (void)run:(NSString *)source showRC:(BOOL)rc showResult:(BOOL)result;
- (void)cancel;
- (void)reset;
@end
