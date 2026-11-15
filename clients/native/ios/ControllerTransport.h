#import <Foundation/Foundation.h>
/* Native-only API: credential dictionaries never cross into Aether/UI state. */
BOOL ControllerCredentialValid(id credential);
void ControllerReadStatus(NSDictionary *credential, void (^completion)(NSDictionary *, NSString *));
