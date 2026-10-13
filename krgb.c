/*---------------------------------------------------------------------------*\
| krgb.c  —  CLI de prueba para el motor kRGB (Corsair K65 LUX RGB / macOS)    |
|                                                                             |
|   Compilar:  clang -O2 -Wall krgb.c engine.c -o krgb \                      |
|                    -framework IOKit -framework CoreFoundation               |
|   Uso:       ./krgb list | color R G B | wave [vel] | hardware              |
\*---------------------------------------------------------------------------*/

#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int s) { (void)s; g_stop = 1; }

static void run_wave(double speed)
{
    signal(SIGINT, on_sigint);
    krgb_software_mode();

    struct timespec frame = { 0, 33L * 1000L * 1000L };   /* ~30 fps */
    double base = 0.0;

    printf("Rainbow wave (~30 fps). Ctrl-C para parar.\n");
    fflush(stdout);

    while(!g_stop)
    {
        krgb_frame_rainbow(base);
        base += speed;
        if(base >= 360.0) base -= 360.0;
        nanosleep(&frame, NULL);
    }

    krgb_hardware_mode();
    printf("\nEfecto detenido, control devuelto al teclado.\n");
}

int main(int argc, char **argv)
{
    if(argc >= 2 && strcmp(argv[1], "list") == 0)
    {
        krgb_list();
        return 0;
    }

    if(argc >= 2 && strcmp(argv[1], "hardware") == 0)
    {
        if(krgb_open() != 0) return 1;
        krgb_hardware_mode();
        printf("Control devuelto al teclado (modo hardware).\n");
        return 0;
    }

    if(argc >= 2 && strcmp(argv[1], "wave") == 0)
    {
        double speed = (argc >= 3) ? atof(argv[2]) : 2.0;
        if(krgb_open() != 0) return 1;
        run_wave(speed);
        return 0;
    }

    if(argc == 5 && strcmp(argv[1], "color") == 0)
    {
        if(krgb_open() != 0) return 1;
        krgb_software_mode();
        krgb_frame_uniform((unsigned char)atoi(argv[2]),
                           (unsigned char)atoi(argv[3]),
                           (unsigned char)atoi(argv[4]));
        printf("Color (%s, %s, %s) enviado a %d teclas.\n",
               argv[2], argv[3], argv[4], krgb_num_keys());
        return 0;
    }

    fprintf(stderr,
        "Uso:\n"
        "  %s list             Lista las interfaces HID del teclado\n"
        "  %s color R G B       Pinta todo el teclado (valores 0..255)\n"
        "  %s wave [velocidad]  Efecto rainbow animado (Ctrl-C para parar)\n"
        "  %s hardware          Devuelve el control al teclado\n",
        argv[0], argv[0], argv[0], argv[0]);
    return 1;
}
