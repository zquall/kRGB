/*---------------------------------------------------------------------------*\
| main.m  —  Entry point of the kRGB menu bar app                             |
\*---------------------------------------------------------------------------*/
#import <Cocoa/Cocoa.h>
#import "AppDelegate.h"

int main(int argc, const char *argv[])
{
    @autoreleasepool
    {
        NSApplication *app = [NSApplication sharedApplication];
        AppDelegate  *del = [[AppDelegate alloc] init];
        [app setDelegate:del];
        /* Agent: lives in the menu bar only, with no Dock icon. */
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [app run];
    }
    return 0;
}
