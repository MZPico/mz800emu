#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <glib.h>

#include "iface.h"
#include "iface_video.h"
#include "iface_joy.h"
#include "iface_audio.h"

bool iface_init(void)
{
    g_print("Initializing interface...\n");

    if(!iface_video_init())
    {
        return false;
    };

#if HAVE_JOY
    if(!iface_joy_init())
    {
        return false;
    };
#endif

    if(!iface_audio_init())
    {
        return false;
    };

    return true;
}

void iface_exit(void)
{
    g_print("Quitting interface...\n");

    iface_audio_exit();
#if HAVE_JOY
    iface_joy_exit();
#endif
    iface_video_exit();
}
