#ifndef SDLAPP_IMGUI_VIDEO_H
#define SDLAPP_IMGUI_VIDEO_H

#include <glib.h>
#include "libs/sdlapp/sdlapp.h"

#ifdef __cplusplus
extern "C"
{
#endif
    void iface_sdl3_video_window_resize_event_handler(SdlAppWindow *win, SDL_Event *event);
#ifdef __cplusplus
}
#endif

#endif /* SDLAPP_IMGUI_VIDEO_H */
