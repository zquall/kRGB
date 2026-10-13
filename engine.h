/*---------------------------------------------------------------------------*\
| engine.h  —  Motor de control RGB para Corsair K65 LUX RGB (macOS / IOKit)   |
|              API compartida por el CLI (krgb) y el app de barra (kRGB.app).  |
\*---------------------------------------------------------------------------*/
#ifndef KRGB_ENGINE_H
#define KRGB_ENGINE_H

/* Códigos de efecto (compartidos con el app). */
enum {
    KRGB_OFF       = 0,
    KRGB_RAINBOW   = 1,
    KRGB_STATIC    = 2,
    KRGB_BREATHING = 3,
    KRGB_CYCLE     = 4,
    KRGB_RAIN      = 5,   /* lluvia de gotas de colores        */
    KRGB_BUG       = 6,   /* bicho de luz que deambula          */
    KRGB_MATRIX    = 7,   /* código estilo Matrix (direccional) */
    KRGB_CASCADE   = 8,   /* catarata arcoíris (direccional)    */
    KRGB_PSY       = 9    /* plasma psicodélico                 */
};

/* Direcciones de la ola del rainbow. */
enum {
    DIR_L2R = 0,   /* izquierda -> derecha */
    DIR_R2L = 1,   /* derecha -> izquierda */
    DIR_T2B = 2,   /* arriba -> abajo      */
    DIR_B2T = 3    /* abajo -> arriba      */
};

/* Encuentra y abre la interfaz de control del teclado. 0 = ok, -1 = falla. */
int  krgb_open(void);
void krgb_close(void);

/* 1 si los últimos envíos vienen fallando en serie (teclado desconectado). */
int  krgb_link_lost(void);

/* Cambio de modo del teclado. */
void krgb_software_mode(void);   /* acepta colores nuestros (modo directo)  */
void krgb_hardware_mode(void);   /* devuelve el control a los efectos del HW */

/* Render de un frame concreto. */
void krgb_frame_rainbow(double base_hue);                                  /* arcoíris por índice */
void krgb_frame_wave(double base_hue, int direction);                      /* ola direccional     */
void krgb_frame_uniform(unsigned char r, unsigned char g, unsigned char b);/* color sólido        */
void krgb_frame_rain(double dphase);                                       /* lluvia (con memoria)*/
void krgb_frame_bug(double dphase);                                        /* bicho (con memoria) */
void krgb_frame_matrix(double dphase, int direction);                      /* Matrix (direccional)*/
void krgb_frame_cascade(double dphase, int direction);                     /* catarata arcoíris   */
void krgb_frame_psychedelic(double dphase);                                /* plasma psicodélico  */

/* Render genérico por efecto. 'phase' avanza por frame (grados);
   'static_rgb' (0xRRGGBB) solo se usa con KRGB_STATIC; 'direction' solo con el rainbow. */
void krgb_render(int effect, double phase, unsigned int static_rgb, int direction);

int  krgb_num_keys(void);

/* Diagnóstico (usado por el CLI): lista las interfaces HID del teclado. */
void krgb_list(void);

#endif /* KRGB_ENGINE_H */
