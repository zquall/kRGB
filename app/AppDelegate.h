#import <Cocoa/Cocoa.h>

@interface AppDelegate : NSObject <NSApplicationDelegate>
- (void)updateUI;   /* refresca ícono/estado; siempre en el hilo principal */
@end
