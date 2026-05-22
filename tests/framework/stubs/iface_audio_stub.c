/*
 * iface_audio_stub.c — stub implementace audio low-level rozhraní pro headless testy
 *
 * Nahrazuje reálný SDL3 audio backend.
 * Všechny funkce jsou no-op.
 *
 * Licence: GPLv3
 */

#include <stdbool.h>

/*
 * Tyto funkce jsou deklarovány jako extern v iface_audio.h
 * a normálně implementovány v src/iface-audio/sdl3/sdl3_audio.c
 *
 * V headless režimu je nepotřebujeme — audio se neinicializuje.
 */

bool iface_audio_lowlevel_init(void)
{
    return true;
}

void iface_audio_lowlevel_quit(void)
{
    /* nic */
}

void iface_audio_lowlevel_pause(void)
{
    /* nic */
}

void iface_audio_lowlevel_resume(void)
{
    /* nic */
}
