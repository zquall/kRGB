/*---------------------------------------------------------------------------*\
| AppDelegate.m  —  kRGB menu bar                                             |
|                                                                             |
|   Status icon + menu (effects / solid color / speed / launch at login).     |
|   The effect runs on a background thread; the menu only changes state.      |
\*---------------------------------------------------------------------------*/
#import "AppDelegate.h"
#import "engine.h"
#import <ServiceManagement/ServiceManagement.h>
#import <time.h>

/*---------------------------------------------------------*\
| State shared with the effect thread                       |
\*---------------------------------------------------------*/
static volatile int          g_effect      = KRGB_RAINBOW;
static volatile int          g_speed_milli = 2000;        /* degrees/frame * 1000 */
static volatile unsigned int g_static_rgb  = 0x0020FF;    /* default color        */
static volatile int          g_run         = 1;
static volatile int          g_have_dev    = 0;
static volatile int          g_direction   = DIR_R2L;      /* travel direction         */
static volatile int          g_retry_req   = 0;            /* menu asked to reconnect  */
static volatile int          g_loop_done   = 0;            /* effect thread has exited */

/* Pointer to the delegate so the effect thread can refresh the UI. */
static AppDelegate *g_app = nil;

static void notify_ui(void)
{
    dispatch_async(dispatch_get_main_queue(), ^{ [g_app updateUI]; });
}

/*---------------------------------------------------------*\
| Effect thread: sole owner of the keyboard I/O             |
\*---------------------------------------------------------*/
static void effect_loop(void)
{
    int last = -1;
    double phase = 0.0;
    int    ticks = 0;                                /* spaces out the retries */
    struct timespec fast = { 0, 33L  * 1000000L };   /* ~30 fps */
    struct timespec slow = { 0, 200L * 1000000L };

    while(g_run)
    {
        /*--- No keyboard: retry every ~2 s (or right away if the menu asked) ---*/
        if(!g_have_dev)
        {
            if(g_retry_req || ++ticks >= 10)          /* 10 * 200 ms = 2 s */
            {
                g_retry_req = 0;
                ticks       = 0;
                krgb_close();                         /* drop any stale handle */
                if(krgb_open() == 0)
                {
                    g_have_dev = 1;
                    last       = -1;                  /* force the mode to be re-sent */
                    notify_ui();
                }
            }
            nanosleep(&slow, NULL);
            continue;
        }

        /*--- Manual reconnect with the keyboard already open: reopen anyway ---*/
        if(g_retry_req)
        {
            g_retry_req = 0;
            krgb_close();
            g_have_dev  = (krgb_open() == 0) ? 1 : 0;
            last        = -1;
            ticks       = 0;
            notify_ui();
            if(!g_have_dev) { nanosleep(&slow, NULL); continue; }
        }

        int eff = g_effect;
        if(eff != last)
        {
            if(eff == KRGB_OFF) krgb_hardware_mode();
            else                krgb_software_mode();
            last = eff;
        }

        if(eff != KRGB_OFF)
            krgb_render(eff, phase, g_static_rgb, g_direction);

        /*--- Did the link drop? Release it and switch to retry mode ---*/
        if(krgb_link_lost())
        {
            krgb_close();
            g_have_dev = 0;
            last       = -1;
            ticks      = 0;
            notify_ui();
            continue;
        }

        if(eff == KRGB_OFF) { nanosleep(&slow, NULL); continue; }

        phase += g_speed_milli / 1000.0;
        if(phase >= 360.0) phase -= 360.0;

        nanosleep((eff == KRGB_STATIC) ? &slow : &fast, NULL);
    }
    g_loop_done = 1;
}

@implementation AppDelegate
{
    NSStatusItem *statusItem;
    NSMenuItem   *miStatus;
    NSArray<NSMenuItem *> *effectItems;    /* items tagged with the effect code */
    NSMenuItem   *colorParent;
    NSArray<NSMenuItem *> *speedItems;     /* items tagged with the speed (milli) */
    NSArray<NSMenuItem *> *dirItems;       /* items tagged with the direction     */
    NSMenuItem   *miLogin;
}

- (void)applicationDidFinishLaunching:(NSNotification *)note
{
    g_app = self;
    statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];

    NSMenu *menu = [[NSMenu alloc] init];

    miStatus = [[NSMenuItem alloc] initWithTitle:@"kRGB" action:nil keyEquivalent:@""];
    [miStatus setEnabled:NO];
    [menu addItem:miStatus];
    [menu addItem:[NSMenuItem separatorItem]];

    /*----- Effects -----*/
    NSMenuItem *rainbow = [self effectItem:@"Rainbow wave" tag:KRGB_RAINBOW   toMenu:menu];
    NSMenuItem *rain    = [self effectItem:@"Rain"         tag:KRGB_RAIN      toMenu:menu];
    NSMenuItem *matrix  = [self effectItem:@"Matrix"       tag:KRGB_MATRIX    toMenu:menu];
    NSMenuItem *cascade = [self effectItem:@"Waterfall"    tag:KRGB_CASCADE   toMenu:menu];
    NSMenuItem *psy     = [self effectItem:@"Psychedelic"  tag:KRGB_PSY       toMenu:menu];
    NSMenuItem *bug     = [self effectItem:@"Bug"          tag:KRGB_BUG       toMenu:menu];
    NSMenuItem *breath  = [self effectItem:@"Breathing"    tag:KRGB_BREATHING toMenu:menu];
    NSMenuItem *cycle   = [self effectItem:@"Color cycle"  tag:KRGB_CYCLE     toMenu:menu];

    colorParent = [[NSMenuItem alloc] initWithTitle:@"Solid color" action:nil keyEquivalent:@""];
    NSMenu *colorMenu = [[NSMenu alloc] init];
    NSArray *colors = @[ @[@"Blue", @(0x0020FF)], @[@"Cyan", @(0x00FFFF)], @[@"Green", @(0x00FF00)],
                         @[@"Red", @(0xFF0000)], @[@"Purple", @(0x8000FF)], @[@"White", @(0xFFFFFF)] ];
    for(NSArray *c in colors)
    {
        NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:c[0]
                                                    action:@selector(pickColor:) keyEquivalent:@""];
        it.target = self;
        it.tag    = [c[1] integerValue];
        [colorMenu addItem:it];
    }
    colorParent.submenu = colorMenu;
    [menu addItem:colorParent];

    NSMenuItem *off = [self effectItem:@"Off (keyboard takes over)" tag:KRGB_OFF toMenu:menu];

    effectItems = @[ rainbow, rain, matrix, cascade, psy, bug, breath, cycle, off ];

    [menu addItem:[NSMenuItem separatorItem]];

    /*----- Speed (5 levels) -----*/
    NSMenuItem *speedParent = [[NSMenuItem alloc] initWithTitle:@"Speed" action:nil keyEquivalent:@""];
    NSMenu *speedMenu = [[NSMenu alloc] init];
    NSArray *speeds = @[ @[@"Very slow", @500], @[@"Slow", @1000], @[@"Normal", @2000],
                         @[@"Fast", @4000], @[@"Very fast", @7000] ];
    NSMutableArray *sItems = [NSMutableArray array];
    for(NSArray *s in speeds)
    {
        NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:s[0]
                                                    action:@selector(pickSpeed:) keyEquivalent:@""];
        it.target = self;
        it.tag    = [s[1] integerValue];
        [speedMenu addItem:it];
        [sItems addObject:it];
    }
    speedItems = sItems;
    speedParent.submenu = speedMenu;
    [menu addItem:speedParent];

    /*----- Travel direction (rainbow / Matrix / waterfall) -----*/
    NSMenuItem *dirParent = [[NSMenuItem alloc] initWithTitle:@"Direction" action:nil keyEquivalent:@""];
    NSMenu *dirMenu = [[NSMenu alloc] init];
    NSArray *dirs = @[ @[@"Left → Right", @(DIR_L2R)], @[@"Right → Left", @(DIR_R2L)],
                       @[@"Top → Bottom", @(DIR_T2B)], @[@"Bottom → Top", @(DIR_B2T)] ];
    NSMutableArray *dItems = [NSMutableArray array];
    for(NSArray *d in dirs)
    {
        NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:d[0]
                                                    action:@selector(pickDirection:) keyEquivalent:@""];
        it.target = self;
        it.tag    = [d[1] integerValue];
        [dirMenu addItem:it];
        [dItems addObject:it];
    }
    dirItems = dItems;
    dirParent.submenu = dirMenu;
    [menu addItem:dirParent];

    [menu addItem:[NSMenuItem separatorItem]];

    /*----- Launch at login -----*/
    miLogin = [[NSMenuItem alloc] initWithTitle:@"Launch at login"
                                         action:@selector(toggleLogin:) keyEquivalent:@""];
    miLogin.target = self;
    [menu addItem:miLogin];

    NSMenuItem *retry = [[NSMenuItem alloc] initWithTitle:@"Reconnect"
                                                   action:@selector(retry:) keyEquivalent:@""];
    retry.target = self;
    [menu addItem:retry];

    NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit"
                                                  action:@selector(quit:) keyEquivalent:@"q"];
    quit.target = self;
    [menu addItem:quit];

    statusItem.menu = menu;

    /*----- Open the keyboard and start the thread -----*/
    g_have_dev = (krgb_open() == 0) ? 1 : 0;
    [self updateUI];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{ effect_loop(); });
}

- (NSMenuItem *)effectItem:(NSString *)title tag:(NSInteger)tag toMenu:(NSMenu *)menu
{
    NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:title
                                                action:@selector(pickEffect:) keyEquivalent:@""];
    it.target = self;
    it.tag    = tag;
    [menu addItem:it];
    return it;
}

- (BOOL)loginEnabled
{
    if(@available(macOS 13.0, *))
        return SMAppService.mainAppService.status == SMAppServiceStatusEnabled;
    return NO;
}

- (void)updateUI
{
    NSString *icon, *label;
    if(!g_have_dev) { icon = @"⚠️"; label = @"kRGB — keyboard disconnected (reconnecting…)"; }
    else switch(g_effect)
    {
        case KRGB_RAINBOW:   icon = @"🌈"; label = @"kRGB — Rainbow wave"; break;
        case KRGB_RAIN:      icon = @"🌧️"; label = @"kRGB — Rain";         break;
        case KRGB_MATRIX:    icon = @"🟢"; label = @"kRGB — Matrix";       break;
        case KRGB_CASCADE:   icon = @"🌊"; label = @"kRGB — Waterfall";    break;
        case KRGB_PSY:       icon = @"🍄"; label = @"kRGB — Psychedelic";  break;
        case KRGB_BUG:       icon = @"🐛"; label = @"kRGB — Bug";          break;
        case KRGB_BREATHING: icon = @"🫧"; label = @"kRGB — Breathing";    break;
        case KRGB_CYCLE:     icon = @"🌀"; label = @"kRGB — Color cycle";  break;
        case KRGB_STATIC:    icon = @"🎨"; label = @"kRGB — Solid color";  break;
        default:             icon = @"⌨️"; label = @"kRGB — Off";          break;
    }
    statusItem.button.title = icon;
    miStatus.title = label;

    for(NSMenuItem *it in effectItems)
        it.state = (it.tag == g_effect) ? NSControlStateValueOn : NSControlStateValueOff;
    colorParent.state = (g_effect == KRGB_STATIC) ? NSControlStateValueOn : NSControlStateValueOff;
    for(NSMenuItem *it in speedItems)
        it.state = (it.tag == g_speed_milli) ? NSControlStateValueOn : NSControlStateValueOff;
    for(NSMenuItem *it in dirItems)
        it.state = (it.tag == g_direction) ? NSControlStateValueOn : NSControlStateValueOff;
    miLogin.state = [self loginEnabled] ? NSControlStateValueOn : NSControlStateValueOff;
}

/*----- Menu actions -----*/
- (void)pickEffect:(NSMenuItem *)sender { g_effect = (int)sender.tag; [self updateUI]; }

- (void)pickColor:(NSMenuItem *)sender
{
    g_static_rgb = (unsigned int)sender.tag;
    g_effect     = KRGB_STATIC;
    [self updateUI];
}

- (void)pickSpeed:(NSMenuItem *)sender { g_speed_milli = (int)sender.tag; [self updateUI]; }

- (void)pickDirection:(NSMenuItem *)sender { g_direction = (int)sender.tag; [self updateUI]; }

- (void)toggleLogin:(id)sender
{
    if(@available(macOS 13.0, *))
    {
        NSError *err = nil;
        if(SMAppService.mainAppService.status == SMAppServiceStatusEnabled)
            [SMAppService.mainAppService unregisterAndReturnError:&err];
        else
            [SMAppService.mainAppService registerAndReturnError:&err];

        if(err)
        {
            NSAlert *a = [[NSAlert alloc] init];
            a.messageText     = @"Could not change the launch-at-login setting";
            a.informativeText = err.localizedDescription;
            [a runModal];
        }
    }
    else
    {
        NSAlert *a = [[NSAlert alloc] init];
        a.messageText = @"Requires macOS 13 or later";
        [a runModal];
    }
    [self updateUI];
}

/* We never touch the device here: the effect thread owns the I/O. */
- (void)retry:(id)sender { g_retry_req = 1; }

- (void)quit:(id)sender { [NSApp terminate:nil]; }

- (void)applicationWillTerminate:(NSNotification *)note
{
    g_run = 0;
    struct timespec t = { 0, 20L * 1000000L };
    for(int i = 0; i < 40 && !g_loop_done; i++)
        nanosleep(&t, NULL);          /* wait for the thread to release the I/O (max ~800 ms) */
    if(g_have_dev)
    {
        krgb_hardware_mode();         /* hand control back to the keyboard */
        krgb_close();
    }
}

@end
