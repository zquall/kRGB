/*---------------------------------------------------------------------------*\
| main.m  —  Arranque del app de barra de menú kRGB                           |
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
        /* Agente: vive solo en la barra de menú, sin ícono en el Dock. */
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [app run];
    }
    return 0;
}
