#ifndef IFACE_KEYBOARD_H
#define IFACE_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <SDL3/SDL.h>

typedef struct iface_keyboard_state_t
{
    int lalt;
    int ralt;
    int lshift;
    int rshift;
} iface_keyboard_state_t;

extern iface_keyboard_state_t g_iface_kbdstate;

#define IFACE_KEYBOARD_PRESSED true
#define IFACE_KEYBOARD_RELEASED false

#ifdef __cplusplus
extern "C"
{
#endif
    extern void iface_keyboard_event_keydown(SDL_Keycode scancode);
    extern void iface_keyboard_event_keyup(SDL_Keycode scancode);
    extern void iface_keyboard_full_scan(const bool *state, SDL_Keymod kmod, int numkeys);

#ifdef __cplusplus
}
#endif
#endif // IFACE_KEYBOARD_H
