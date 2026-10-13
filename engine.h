/*---------------------------------------------------------------------------*\
| engine.h  —  RGB control engine for the Corsair K65 LUX RGB (macOS / IOKit)  |
|              Shared API for the CLI (krgb) and the menu bar app (kRGB.app).  |
\*---------------------------------------------------------------------------*/
#ifndef KRGB_ENGINE_H
#define KRGB_ENGINE_H

/* Effect codes (shared with the app). */
enum {
    KRGB_OFF       = 0,
    KRGB_RAINBOW   = 1,
    KRGB_STATIC    = 2,
    KRGB_BREATHING = 3,
    KRGB_CYCLE     = 4,
    KRGB_RAIN      = 5,   /* falling colored droplets        */
    KRGB_BUG       = 6,   /* wandering light bug             */
    KRGB_MATRIX    = 7,   /* Matrix-style code (directional) */
    KRGB_CASCADE   = 8,   /* rainbow waterfall (directional) */
    KRGB_PSY       = 9    /* psychedelic plasma              */
};

/* Travel direction for the directional effects. */
enum {
    DIR_L2R = 0,   /* left   -> right  */
    DIR_R2L = 1,   /* right  -> left   */
    DIR_T2B = 2,   /* top    -> bottom */
    DIR_B2T = 3    /* bottom -> top    */
};

/* Finds and opens the keyboard's control interface. 0 = ok, -1 = failure. */
int  krgb_open(void);
void krgb_close(void);

/* 1 if the last reports have been failing in a row (keyboard disconnected). */
int  krgb_link_lost(void);

/* Keyboard mode switching. */
void krgb_software_mode(void);   /* accepts our colors (direct mode)       */
void krgb_hardware_mode(void);   /* hands control back to the HW effects   */

/* Render one specific frame. */
void krgb_frame_rainbow(double base_hue);                                  /* rainbow by index    */
void krgb_frame_wave(double base_hue, int direction);                      /* directional wave    */
void krgb_frame_uniform(unsigned char r, unsigned char g, unsigned char b);/* solid color         */
void krgb_frame_rain(double dphase);                                       /* rain (stateful)     */
void krgb_frame_bug(double dphase);                                        /* bug (stateful)      */
void krgb_frame_matrix(double dphase, int direction);                      /* Matrix (directional)*/
void krgb_frame_cascade(double dphase, int direction);                     /* rainbow waterfall   */
void krgb_frame_psychedelic(double dphase);                                /* psychedelic plasma  */

/* Generic per-effect render. 'phase' advances every frame (in degrees);
   'static_rgb' (0xRRGGBB) is only used with KRGB_STATIC; 'direction' only
   applies to the directional effects. */
void krgb_render(int effect, double phase, unsigned int static_rgb, int direction);

int  krgb_num_keys(void);

/* Diagnostics (used by the CLI): lists the keyboard's HID interfaces. */
void krgb_list(void);

#endif /* KRGB_ENGINE_H */
