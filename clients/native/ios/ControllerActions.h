#import <Foundation/Foundation.h>
NSString *ControllerActionSource(NSString *identifier);
NSString *ControllerActionSummary(NSString *identifier, NSString *output, NSString **error);

BOOL ControllerActionIsControl(NSString *identifier);
