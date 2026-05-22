#ifndef IFACE_SDL3_EVENTS_H
#define IFACE_SDL3_EVENTS_H

#include <SDL3/SDL.h>
#include <glib.h>
#include <stdint.h>
#include "libs/sdlapp/sdlapp.h"

#ifdef __cplusplus
extern "C"
{
#endif
    void iface_sdl3_events_cb(SdlAppWindow *win, SDL_Event *event, gpointer user_data);
#ifdef __cplusplus
}
#endif

#endif /* IFACE_SDL3_EVENTS_H */
