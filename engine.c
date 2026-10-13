/*---------------------------------------------------------------------------*\
| engine.c  —  RGB control engine for the Corsair K65 LUX RGB (macOS / IOKit)  |
|                                                                             |
|   Zero external dependencies: only IOKit + CoreFoundation (system).         |
|   Corsair protocol (software mode, color stream split into R/G/B,           |
|   then submit) ported from OpenRGB (CorsairPeripheralController, GPL-2.0).   |
\*---------------------------------------------------------------------------*/

#include "engine.h"
#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*---------------------------------------------------------*\
| Device identity                                           |
\*---------------------------------------------------------*/
#define CORSAIR_VID          0x1B1C
#define K65_LUX_PID          0x1B37
#define CONTROL_USAGE_PAGE   0xFFC2   /* control interface (not the typing one) */
#define REPORT_LEN           64       /* real payload, no report-id byte       */

/*---------------------------------------------------------*\
| Corsair protocol constants (from OpenRGB)                 |
\*---------------------------------------------------------*/
#define CMD_WRITE            0x07
#define CMD_STREAM           0x7F
#define PROP_SPECIAL_FUNC    0x04
#define PROP_LIGHTING_CTRL   0x05
#define PROP_SUBMIT_KB_24    0x28
#define LIGHTING_SOFTWARE    0x02
#define LIGHTING_HARDWARE    0x01

/*---------------------------------------------------------*\
| LED->offset map for "NORMAL" keyboards (K65/K70 LUX...).  |
\*---------------------------------------------------------*/
static const int keys[] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0C,0x0D,0x0E,0x0F,0x11,0x12,
    0x14,0x15,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20,0x21,0x24,0x25,0x26,
    0x27,0x28,0x2A,0x2B,0x2C,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,
    0x3C,0x3D,0x3E,0x3F,0x40,0x42,0x43,0x44,0x45,0x48,73,  74,  75,  76,  78,
    79,  80,  81,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  96,  97,
    98,  99,  100, 101, 102, 103, 104, 105, 108, 109, 110, 111, 112, 113, 115,
    116, 117, 120, 121, 122, 123, 124, 126, 127, 128, 129, 132, 133, 134, 135,
    136, 137, 139, 140, 141, 0x10,114
};
static const int NUM_KEYS = (int)(sizeof(keys) / sizeof(keys[0]));

/*---------------------------------------------------------*\
| Physical geometry: matrix_map[row][col] = the key's       |
| logical index. Copied from OpenRGB ("normal" layout,      |
| the one the K65 uses). Inverting it gives each key's      |
| (x,y) position, enabling waves in all 4 directions.       |
\*---------------------------------------------------------*/
#define MNA 0xFFFFFFFFu
static const unsigned int matrix_map[6][23] = {
    {   0, MNA,  10,  18,  28,  36, MNA,  46,  55,  64,  74, MNA,  84,  93, 102,   6,  15,  24,  33,  26,  35,  44,  53 },
    {   1,  11,  19,  29,  37,  47,  56,  65,  75,  85,  94, MNA, 103,   7,  25, MNA,  42,  51,  60,  62,  72,  82,  91 },
    {   2, MNA,  12,  20,  30,  38, MNA,  48,  57,  66,  76,  86,  95, 104,  70,  80,  34,  43,  52,   9,  17,  27, 100 },
    {   3, MNA,  13,  21,  31,  39, MNA,  49,  58,  67,  77,  87,  96, 105,  98, 112, MNA, MNA, MNA,  45,  54,  63, MNA },
    {   4, 111,  22,  32,  40,  50, MNA,  59, MNA,  68,  78,  88,  97, 106,  61, MNA, MNA,  81, MNA,  73,  83,  92, 109 },
    {   5,  14,  23, MNA, MNA, MNA, MNA,  41, MNA, MNA, MNA, MNA,  69,  79,  89,  71,  90,  99, 108, 101, MNA, 110, MNA }
};
static double g_kx[128], g_ky[128];   /* normalized 0..1 position per logical index */
static int    g_geo_ready = 0;

static void ensure_geometry(void)
{
    if(g_geo_ready) return;
    for(int i = 0; i < 128; i++) { g_kx[i] = 0.5; g_ky[i] = 0.5; }
    for(int r = 0; r < 6; r++)
        for(int c = 0; c < 23; c++)
        {
            unsigned int v = matrix_map[r][c];
            if(v != MNA && v < 128) { g_kx[v] = c / 22.0; g_ky[v] = r / 5.0; }
        }
    g_geo_ready = 1;
}

static IOHIDDeviceRef g_dev = NULL;

/* Link health: how many reports failed in a row. If the keyboard gets       */
/* unplugged (or the Mac sleeps) EVERY report starts failing; one isolated   */
/* failure does not count, because the first success resets the counter.     */
#define LINK_FAIL_MAX 24          /* ~2 frames (one frame is 12 reports) */
static int g_fail_streak = 0;
static int g_reported    = 0;     /* avoids spamming the log while retrying */

int krgb_link_lost(void) { return g_fail_streak >= LINK_FAIL_MAX; }

/*---------------------------------------------------------*\
| Helpers                                                   |
\*---------------------------------------------------------*/
static int get_int(IOHIDDeviceRef d, CFStringRef key)
{
    CFTypeRef ref = IOHIDDeviceGetProperty(d, key);
    int val = 0;
    if(ref && CFGetTypeID(ref) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &val);
    return val;
}

static void dict_set_int(CFMutableDictionaryRef d, CFStringRef key, int val)
{
    CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &val);
    CFDictionarySetValue(d, key, n);
    CFRelease(n);
}

/* Sends a 64-byte Output report (payload = b[1..64], report id 0). */
static int send_report(const unsigned char *b65)
{
    if(!g_dev) return -1;
    IOReturn r = IOHIDDeviceSetReport(g_dev, kIOHIDReportTypeOutput,
                                      0 /*reportID*/, b65 + 1, REPORT_LEN);
    if(r == kIOReturnSuccess) { g_fail_streak = 0; return 0; }
    if(g_fail_streak < LINK_FAIL_MAX) g_fail_streak++;
    return -1;
}

/*---------------------------------------------------------*\
| Protocol packets (indices identical to OpenRGB)           |
\*---------------------------------------------------------*/
void krgb_software_mode(void)
{
    unsigned char b[65]; memset(b, 0, sizeof(b));
    b[0x01] = CMD_WRITE;
    b[0x02] = PROP_LIGHTING_CTRL;
    b[0x03] = LIGHTING_SOFTWARE;
    b[0x05] = 0x03;                 /* 3 = keyboard */
    send_report(b);
}

void krgb_hardware_mode(void)
{
    unsigned char b[65]; memset(b, 0, sizeof(b));
    b[0x01] = CMD_WRITE;
    b[0x02] = PROP_SPECIAL_FUNC;
    b[0x03] = LIGHTING_HARDWARE;
    send_report(b);
}

static void stream_packet(unsigned char packet_id, unsigned char data_sz,
                          const unsigned char *data)
{
    unsigned char b[65]; memset(b, 0, sizeof(b));
    b[0x01] = CMD_STREAM;
    b[0x02] = packet_id;
    b[0x03] = data_sz;
    memcpy(&b[0x05], data, data_sz);
    send_report(b);
}

static void submit_kb(unsigned char channel, unsigned char packet_count,
                      unsigned char finish)
{
    unsigned char b[65]; memset(b, 0, sizeof(b));
    b[0x01] = CMD_WRITE;
    b[0x02] = PROP_SUBMIT_KB_24;
    b[0x03] = channel;
    b[0x04] = packet_count;
    b[0x05] = finish;
    send_report(b);
}

static void set_leds_full(const unsigned char *r, const unsigned char *g,
                          const unsigned char *b)
{
    const unsigned char data_sz = 24;   /* third packet, NORMAL layout */

    stream_packet(1, 60, &r[0]);
    stream_packet(2, 60, &r[60]);
    stream_packet(3, data_sz, &r[120]);
    submit_kb(1, 3, 1);

    stream_packet(1, 60, &g[0]);
    stream_packet(2, 60, &g[60]);
    stream_packet(3, data_sz, &g[120]);
    submit_kb(2, 3, 1);

    stream_packet(1, 60, &b[0]);
    stream_packet(2, 60, &b[60]);
    stream_packet(3, data_sz, &b[120]);
    submit_kb(3, 3, 2);
}

/*---------------------------------------------------------*\
| Effects / render                                          |
\*---------------------------------------------------------*/
static void hsv2rgb(double h, double s, double v,
                    unsigned char *R, unsigned char *G, unsigned char *B)
{
    h = fmod(h, 360.0); if(h < 0) h += 360.0;
    double c = v * s;
    double x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
    double m = v - c, r, g, b;
    if      (h <  60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else              { r = c; g = 0; b = x; }
    *R = (unsigned char)((r + m) * 255.0);
    *G = (unsigned char)((g + m) * 255.0);
    *B = (unsigned char)((b + m) * 255.0);
}

void krgb_frame_rainbow(double base_hue)
{
    unsigned char R[168], G[168], B[168];
    memset(R, 0, sizeof(R)); memset(G, 0, sizeof(G)); memset(B, 0, sizeof(B));
    for(int i = 0; i < NUM_KEYS; i++)
    {
        unsigned char r, g, b;
        hsv2rgb(base_hue + (360.0 * i) / NUM_KEYS, 1.0, 1.0, &r, &g, &b);
        R[keys[i]] = r; G[keys[i]] = g; B[keys[i]] = b;
    }
    set_leds_full(R, G, B);
}

void krgb_frame_uniform(unsigned char r, unsigned char g, unsigned char b)
{
    unsigned char R[168], G[168], B[168];
    memset(R, 0, sizeof(R)); memset(G, 0, sizeof(G)); memset(B, 0, sizeof(B));
    for(int i = 0; i < NUM_KEYS; i++)
    {
        R[keys[i]] = r; G[keys[i]] = g; B[keys[i]] = b;
    }
    set_leds_full(R, G, B);
}

void krgb_frame_wave(double base_hue, int direction)
{
    ensure_geometry();
    unsigned char R[168], G[168], B[168];
    memset(R, 0, sizeof(R)); memset(G, 0, sizeof(G)); memset(B, 0, sizeof(B));
    for(int i = 0; i < NUM_KEYS; i++)
    {
        double a;
        switch(direction)
        {
            case DIR_L2R: a = 1.0 - g_kx[i]; break;
            case DIR_R2L: a =       g_kx[i]; break;
            case DIR_T2B: a = 1.0 - g_ky[i]; break;
            case DIR_B2T: a =       g_ky[i]; break;
            default:      a =       g_kx[i]; break;
        }
        unsigned char r, g, b;
        hsv2rgb(base_hue + 360.0 * a, 1.0, 1.0, &r, &g, &b);
        R[keys[i]] = r; G[keys[i]] = g; B[keys[i]] = b;
    }
    set_leds_full(R, G, B);
}

/*---------------------------------------------------------*\
| Stateful animated effects (rain / bug).                   |
|                                                           |
|  krgb_render only hands us 'phase' (0..360, absolute).    |
|  phase_delta() recovers how much it advanced since the    |
|  previous frame -> so the animations honor the speed      |
|  picked in the menu without changing the API.             |
\*---------------------------------------------------------*/
static double g_prev_phase = 0.0;
static int    g_prev_valid = 0;

static double phase_delta(double phase)
{
    if(!g_prev_valid) { g_prev_phase = phase; g_prev_valid = 1; return 0.0; }
    double d = phase - g_prev_phase;
    if(d < 0.0) d += 360.0;            /* the phase wrapped at 360 */
    g_prev_phase = phase;
    return d;
}

static double frand(void) { return (double)rand() / ((double)RAND_MAX + 1.0); }

/*----- Rain -----*/
#define RAIN_DROPS 16
typedef struct { double x, y, len, spd, hue; } drop_t;
static drop_t g_drops[RAIN_DROPS];
static int    g_rain_ready = 0;

static void drop_spawn(drop_t *d, int from_top)
{
    d->x   = frand();
    d->len = 0.25 + 0.45 * frand();          /* tail length        */
    d->spd = 0.6  + 1.2  * frand();           /* fall speed         */
    d->hue = 360.0 * frand();
    d->y   = from_top ? -d->len * frand()     /* respawn at the top */
                      : frand() * (1.0 + d->len) - d->len;  /* scattered seeding */
}

void krgb_frame_rain(double dphase)
{
    ensure_geometry();
    if(!g_rain_ready) { for(int i = 0; i < RAIN_DROPS; i++) drop_spawn(&g_drops[i], 0); g_rain_ready = 1; }

    double dt = dphase / 360.0;               /* 1 turn = one reference fall */
    for(int i = 0; i < RAIN_DROPS; i++)
    {
        g_drops[i].y += g_drops[i].spd * dt * 3.0;
        if(g_drops[i].y - g_drops[i].len > 1.0) drop_spawn(&g_drops[i], 1);
    }

    unsigned char R[168], G[168], B[168];
    memset(R, 0, sizeof(R)); memset(G, 0, sizeof(G)); memset(B, 0, sizeof(B));
    for(int i = 0; i < NUM_KEYS; i++)
    {
        double kx = g_kx[i], ky = g_ky[i];
        double br = 0.0, hue = 0.0;
        for(int d = 0; d < RAIN_DROPS; d++)
        {
            double dx = fabs(kx - g_drops[d].x);
            if(dx > 0.05) continue;                       /* outside the column */
            double dy = g_drops[d].y - ky;                /* >0 if the key is in the tail */
            if(dy < 0.0 || dy > g_drops[d].len) continue;
            double t    = 1.0 - dy / g_drops[d].len;      /* 1 at the head, 0 at the end */
            double colw = 1.0 - dx / 0.05;                /* fades at the column edge */
            double b    = t * t * colw;                   /* bright head, faint tail */
            if(b > br) { br = b; hue = g_drops[d].hue; }   /* brightest droplet wins */
        }
        if(br <= 0.0) continue;
        unsigned char r, g, bb;
        hsv2rgb(hue, 1.0, br > 1.0 ? 1.0 : br, &r, &g, &bb);
        R[keys[i]] = r; G[keys[i]] = g; B[keys[i]] = bb;
    }
    set_leds_full(R, G, B);
}

/*----- Bug -----*/
#define BUG_TRAIL 7
static double g_bug_x, g_bug_y, g_bug_vx, g_bug_vy, g_bug_hue;
static double g_bug_tx[BUG_TRAIL], g_bug_ty[BUG_TRAIL];
static int    g_bug_ready = 0;

static void bug_init(void)
{
    g_bug_x = frand(); g_bug_y = frand();
    double ang = 2.0 * M_PI * frand();
    g_bug_vx = cos(ang); g_bug_vy = sin(ang);
    g_bug_hue = 360.0 * frand();
    for(int i = 0; i < BUG_TRAIL; i++) { g_bug_tx[i] = g_bug_x; g_bug_ty[i] = g_bug_y; }
    g_bug_ready = 1;
}

void krgb_frame_bug(double dphase)
{
    ensure_geometry();
    if(!g_bug_ready) bug_init();

    double dt = dphase / 360.0;
    g_bug_x += g_bug_vx * dt * 2.0;
    g_bug_y += g_bug_vy * dt * 2.0;
    if(g_bug_x < 0.0) { g_bug_x = 0.0; g_bug_vx = -g_bug_vx; }   /* bounces off the edges */
    if(g_bug_x > 1.0) { g_bug_x = 1.0; g_bug_vx = -g_bug_vx; }
    if(g_bug_y < 0.0) { g_bug_y = 0.0; g_bug_vy = -g_bug_vy; }
    if(g_bug_y > 1.0) { g_bug_y = 1.0; g_bug_vy = -g_bug_vy; }

    double a = atan2(g_bug_vy, g_bug_vx) + (frand() - 0.5) * dt * 4.0;  /* lively wobble */
    g_bug_vx = cos(a); g_bug_vy = sin(a);
    g_bug_hue += dphase * 0.5; if(g_bug_hue >= 360.0) g_bug_hue -= 360.0;

    for(int i = BUG_TRAIL - 1; i > 0; i--) { g_bug_tx[i] = g_bug_tx[i-1]; g_bug_ty[i] = g_bug_ty[i-1]; }
    g_bug_tx[0] = g_bug_x; g_bug_ty[0] = g_bug_y;

    unsigned char R[168], G[168], B[168];
    memset(R, 0, sizeof(R)); memset(G, 0, sizeof(G)); memset(B, 0, sizeof(B));
    double kbr[168]; memset(kbr, 0, sizeof(kbr));   /* brightness already set per key */

    /* The head and each trail point = ONE single key (the nearest one). */
    for(int s = 0; s < BUG_TRAIL; s++)
    {
        int    best_i = -1;
        double best_d = 1e9;
        for(int i = 0; i < NUM_KEYS; i++)
        {
            double dx = g_kx[i] - g_bug_tx[s];
            double dy = g_ky[i] - g_bug_ty[s];
            double d  = dx*dx + dy*dy;
            if(d < best_d) { best_d = d; best_i = i; }
        }
        if(best_i < 0) continue;

        double fade = 1.0 - (double)s / BUG_TRAIL;
        double br   = fade * fade * fade;             /* head=1, trail falls off cubed */
        int    off  = keys[best_i];
        if(br <= kbr[off]) continue;                  /* the head (s=0) beats the trail */
        kbr[off] = br;

        unsigned char r, g, bb;
        hsv2rgb(g_bug_hue - s * 6.0, 1.0, br, &r, &g, &bb);
        R[off] = r; G[off] = g; B[off] = bb;
    }
    set_leds_full(R, G, B);
}

/*----- Matrix -----*/
/* Projects key i onto (p, lane) for the chosen direction:                */
/*   p    = progress along the fall axis (0 = the entry edge).            */
/*   lane = perpendicular lane (column if vertical, row if horizontal).   */
static double axis_p(int i, int dir)
{
    switch(dir)
    {
        case DIR_T2B: return       g_ky[i];   /* falls downward    */
        case DIR_B2T: return 1.0 - g_ky[i];   /* falls upward      */
        case DIR_L2R: return       g_kx[i];   /* runs rightward    */
        case DIR_R2L: return 1.0 - g_kx[i];   /* runs leftward     */
        default:      return       g_ky[i];
    }
}
static double axis_lane(int i, int dir)
{
    return (dir == DIR_T2B || dir == DIR_B2T) ? g_kx[i]   /* lanes = columns */
                                              : g_ky[i];  /* lanes = rows    */
}

#define MTX_STREAMS 30
typedef struct { double lane, head, spd, len; } mstream_t;
static mstream_t g_mtx[MTX_STREAMS];
static int    g_mtx_ready = 0;
static int    g_mtx_dir   = -1;              /* direction it was seeded with */
static double g_lanes[64];
static int    g_nlanes    = 0;
static double g_lane_tol  = 0.02;

static void mtx_build_lanes(int dir)
{
    g_nlanes   = 0;
    int vert   = (dir == DIR_T2B || dir == DIR_B2T);
    g_lane_tol = vert ? 0.015 : 0.05;         /* columns close / rows far apart */
    for(int i = 0; i < NUM_KEYS; i++)
    {
        double L = axis_lane(i, dir);
        int found = 0;
        for(int k = 0; k < g_nlanes; k++) if(fabs(g_lanes[k] - L) < 0.02) { found = 1; break; }
        if(!found && g_nlanes < 64) g_lanes[g_nlanes++] = L;
    }
}

static void mstream_spawn(mstream_t *m, int seeded)
{
    m->lane = g_nlanes ? g_lanes[(int)(frand() * g_nlanes) % g_nlanes] : frand();
    m->len  = 0.40 + 0.60 * frand();          /* long Matrix-style tail */
    m->spd  = 0.70 + 0.80 * frand();
    m->head = seeded ? frand() * (1.0 + m->len)   /* scattered seeding at startup */
                     : -frand() * 0.5;            /* respawns at the entry edge    */
}

static void matrix_core(mstream_t *arr, int count, int *ready, int *seeddir,
                        double dphase, int direction)
{
    ensure_geometry();
    if(!*ready || *seeddir != direction)     /* (re)seed if the direction changed */
    {
        mtx_build_lanes(direction);
        for(int i = 0; i < count; i++) mstream_spawn(&arr[i], 1);
        *ready = 1;
        *seeddir = direction;
    }

    double dt = dphase / 360.0;
    for(int i = 0; i < count; i++)
    {
        arr[i].head += arr[i].spd * dt * 3.0;
        if(arr[i].head - arr[i].len > 1.0) mstream_spawn(&arr[i], 0);
    }

    unsigned char R[168], G[168], B[168];
    memset(R, 0, sizeof(R)); memset(G, 0, sizeof(G)); memset(B, 0, sizeof(B));
    for(int i = 0; i < NUM_KEYS; i++)
    {
        double p = axis_p(i, direction);
        double L = axis_lane(i, direction);
        double br = 0.0, near_head = 1.0;
        for(int s = 0; s < count; s++)
        {
            if(fabs(L - arr[s].lane) > g_lane_tol) continue;     /* outside the lane */
            double dist = arr[s].head - p;                       /* >0 if the head already passed */
            if(dist < 0.0 || dist > arr[s].len) continue;
            double t = 1.0 - dist / arr[s].len;                  /* 1 at the head */
            if(t > br) { br = t; near_head = dist; }
        }
        if(br <= 0.0) continue;

        unsigned char r, g, b;
        if(near_head < 0.06) { r = 190; g = 255; b = 190; }       /* near-white head */
        else { double v = br * br; r = 0; g = (unsigned char)(255.0 * v); b = (unsigned char)(45.0 * v); }
        R[keys[i]] = r; G[keys[i]] = g; B[keys[i]] = b;
    }
    set_leds_full(R, G, B);
}

void krgb_frame_matrix(double dphase, int direction)
{
    matrix_core(g_mtx, MTX_STREAMS, &g_mtx_ready, &g_mtx_dir, dphase, direction);
}

/*----- Rainbow waterfall -----*/
/* A continuous rainbow curtain pouring in the chosen direction, with       */
/* brightness bands sliding down to suggest falling water. The whole        */
/* keyboard stays lit (these are not discrete droplets).                    */
static double g_cat_flow = 0.0;

void krgb_frame_cascade(double dphase, int direction)
{
    ensure_geometry();
    g_cat_flow += dphase;
    if(g_cat_flow > 1.0e6) g_cat_flow -= 1.0e6;

    const double CYCLES = 2.0;   /* rainbows stacked along the fall axis */

    unsigned char R[168], G[168], B[168];
    memset(R, 0, sizeof(R)); memset(G, 0, sizeof(G)); memset(B, 0, sizeof(B));
    for(int i = 0; i < NUM_KEYS; i++)
    {
        double p    = axis_p(i, direction);      /* 0 = top of the fall, 1 = bottom  */
        double lane = axis_lane(i, direction);

        /* color: rainbow along the fall axis, rotating over time */
        double hue = g_cat_flow + (1.0 - p) * 360.0 * CYCLES + lane * 20.0;

        /* water texture: descending brightness bands, offset per lane */
        double streak = sin((1.0 - p) * CYCLES * 4.0 * M_PI
                            + g_cat_flow * (M_PI / 180.0) * 2.0
                            + lane * 7.0);
        double v = 0.60 + 0.40 * (0.5 + 0.5 * streak);   /* 0.60 .. 1.00 */

        unsigned char r, g, b;
        hsv2rgb(hue, 1.0, v, &r, &g, &b);
        R[keys[i]] = r; G[keys[i]] = g; B[keys[i]] = b;
    }
    set_leds_full(R, G, B);
}

/*----- Psychedelic (plasma) -----*/
/* Each key hue comes from a plasma field: several waves at different       */
/* frequencies plus an orbiting center. Because the field is continuous,     */
/* neighboring keys blend; and since it animates, it swirls and mutates.    */
static double g_psy_t = 0.0;

void krgb_frame_psychedelic(double dphase)
{
    ensure_geometry();
    g_psy_t += dphase * (M_PI / 180.0);          /* time in radians */
    double t  = g_psy_t;
    double cx = 0.5 + 0.45 * sin(t * 0.40);      /* orbiting center    */
    double cy = 0.5 + 0.45 * cos(t * 0.33);

    unsigned char R[168], G[168], B[168];
    memset(R, 0, sizeof(R)); memset(G, 0, sizeof(G)); memset(B, 0, sizeof(B));
    for(int i = 0; i < NUM_KEYS; i++)
    {
        double x = g_kx[i], y = g_ky[i];
        double dx = x - cx, dy = y - cy;
        double d  = sqrt(dx * dx + dy * dy);

        double f = sin(x * 5.0 + t * 1.1)                 /* horizontal wave   */
                 + sin(y * 7.0 - t * 0.9)                 /* vertical wave     */
                 + sin((x + y) * 4.0 + t * 0.7)           /* diagonal wave     */
                 + sin(d * 10.0 - t * 1.6);               /* radial wave       */
        /* f ranges from -4 to 4 */

        double hue = f * 70.0 + t * (180.0 / M_PI) * 0.25;    /* field color + global spin */
        double v   = 0.72 + 0.28 * (0.5 + 0.5 * sin(f * 1.3 + t * 0.5)); /* brightness with life */

        unsigned char r, g, b;
        hsv2rgb(hue, 1.0, v, &r, &g, &b);
        R[keys[i]] = r; G[keys[i]] = g; B[keys[i]] = b;
    }
    set_leds_full(R, G, B);
}

int krgb_num_keys(void) { return NUM_KEYS; }

void krgb_render(int effect, double phase, unsigned int static_rgb, int direction)
{
    unsigned char r, g, b;
    switch(effect)
    {
        case KRGB_RAINBOW:
            krgb_frame_wave(phase * 2.0, direction);   /* directional wave, 2x vs the others */
            break;

        case KRGB_CYCLE:                                  /* whole keyboard, one rotating hue  */
            hsv2rgb(phase, 1.0, 1.0, &r, &g, &b);
            krgb_frame_uniform(r, g, b);
            break;

        case KRGB_BREATHING: {                            /* drifting hue + pulsing brightness */
            double v = 0.15 + 0.85 * (0.5 + 0.5 * sin(phase * (M_PI / 180.0) * 3.0));
            hsv2rgb(phase, 1.0, v, &r, &g, &b);
            krgb_frame_uniform(r, g, b);
            break;
        }

        case KRGB_RAIN:                                   /* falling colored droplets, 2x */
            krgb_frame_rain(phase_delta(phase) * 2.0);
            break;

        case KRGB_BUG:                                    /* wandering light bug, 2x */
            krgb_frame_bug(phase_delta(phase) * 2.0);
            break;

        case KRGB_MATRIX:                                 /* Matrix code, follows the direction, 2x */
            krgb_frame_matrix(phase_delta(phase) * 2.0, direction);
            break;

        case KRGB_CASCADE:                                /* rainbow waterfall, follows the direction, 2x */
            krgb_frame_cascade(phase_delta(phase) * 2.0, direction);
            break;

        case KRGB_PSY:                                    /* psychedelic plasma, 2x */
            krgb_frame_psychedelic(phase_delta(phase) * 2.0);
            break;

        case KRGB_STATIC:
            krgb_frame_uniform((static_rgb >> 16) & 0xFF,
                               (static_rgb >>  8) & 0xFF,
                               (static_rgb      ) & 0xFF);
            break;

        default:
            break;
    }
}

/*---------------------------------------------------------*\
| Device discovery / open / close                           |
\*---------------------------------------------------------*/
static IOHIDManagerRef make_manager(int match_pid)
{
    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    CFMutableDictionaryRef m = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    dict_set_int(m, CFSTR(kIOHIDVendorIDKey), CORSAIR_VID);
    if(match_pid)
        dict_set_int(m, CFSTR(kIOHIDProductIDKey), K65_LUX_PID);
    IOHIDManagerSetDeviceMatching(mgr, m);
    CFRelease(m);
    IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone);
    return mgr;
}

int krgb_open(void)
{
    if(g_dev) return 0;   /* already open */

    IOHIDManagerRef mgr = make_manager(1);
    CFSetRef set = IOHIDManagerCopyDevices(mgr);
    CFIndex  n   = set ? CFSetGetCount(set) : 0;

    IOHIDDeviceRef found = NULL;
    if(n > 0)
    {
        IOHIDDeviceRef *arr = calloc(n, sizeof(IOHIDDeviceRef));
        CFSetGetValues(set, (const void **)arr);
        for(CFIndex i = 0; i < n; i++)
            if(get_int(arr[i], CFSTR(kIOHIDPrimaryUsagePageKey)) == CONTROL_USAGE_PAGE)
            {
                found = arr[i];
                break;
            }
        if(found) CFRetain(found);
        free(arr);
    }
    if(set) CFRelease(set);
    IOHIDManagerClose(mgr, kIOHIDOptionsTypeNone);
    CFRelease(mgr);              /* otherwise one leaks per retry (every 2 s) */

    if(!found)
    {
        if(!g_reported)          /* only once: we retry in a loop */
        {
            g_reported = 1;
            if(n > 0) fprintf(stderr, "Keyboard present but no control interface found (usagePage 0x%04X).\n", CONTROL_USAGE_PAGE);
            else      fprintf(stderr, "K65 LUX RGB not found (VID 0x1B1C PID 0x1B37).\n");
        }
        return -1;
    }

    IOReturn r = IOHIDDeviceOpen(found, kIOHIDOptionsTypeNone);
    if(r != kIOReturnSuccess)
    {
        if(!g_reported) { g_reported = 1; fprintf(stderr, "IOHIDDeviceOpen failed: 0x%08X\n", r); }
        CFRelease(found);
        return -1;
    }
    g_dev         = found;
    g_fail_streak = 0;
    g_reported    = 0;           /* if it drops again, report it again */
    return 0;
}

void krgb_close(void)
{
    if(g_dev)
    {
        IOHIDDeviceClose(g_dev, kIOHIDOptionsTypeNone);
        CFRelease(g_dev);
        g_dev = NULL;
    }
    g_fail_streak = 0;   /* leaves the link ready for a clean krgb_open() */
}

void krgb_list(void)
{
    IOHIDManagerRef mgr = make_manager(0);
    CFSetRef set = IOHIDManagerCopyDevices(mgr);
    if(!set) { printf("No Corsair HID device found.\n"); return; }

    CFIndex n = CFSetGetCount(set);
    IOHIDDeviceRef *arr = calloc(n, sizeof(IOHIDDeviceRef));
    CFSetGetValues(set, (const void **)arr);

    printf("Corsair HID interfaces (VID 0x%04X): %ld\n\n", CORSAIR_VID, (long)n);
    for(CFIndex i = 0; i < n; i++)
    {
        int pid  = get_int(arr[i], CFSTR(kIOHIDProductIDKey));
        int up   = get_int(arr[i], CFSTR(kIOHIDPrimaryUsagePageKey));
        int us   = get_int(arr[i], CFSTR(kIOHIDPrimaryUsageKey));
        int out  = get_int(arr[i], CFSTR(kIOHIDMaxOutputReportSizeKey));
        int in   = get_int(arr[i], CFSTR(kIOHIDMaxInputReportSizeKey));
        int feat = get_int(arr[i], CFSTR(kIOHIDMaxFeatureReportSizeKey));
        const char *tag = (up == CONTROL_USAGE_PAGE) ? "  <- CONTROL (this one)"
                        : (up == 0x01 ? "  <- typing (do not use)" : "");
        printf("[%ld] PID 0x%04X  usagePage 0x%04X usage 0x%02X  out=%d in=%d feat=%d%s\n",
               (long)i, pid, up, us, out, in, feat, tag);
    }
    free(arr);
    CFRelease(set);
}
