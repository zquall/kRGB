#import <Cocoa/Cocoa.h>

@interface AppDelegate : NSObject <NSApplicationDelegate>
- (void)updateUI;   /* refreshes icon/status; always on the main thread */
@end
