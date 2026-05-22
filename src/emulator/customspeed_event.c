#ifndef CUSTOMSPEED_EVENT_C
#define CUSTOMSPEED_EVENT_C
#include "main.h"
#include "customspeed.h"

/**
 * Set the next custom speed event.
 *
 * @param
 * @return
 */
static inline void customspeed_event_set_next(void)
{
    g_customspeed.speed_frame_width = g_customspeed.speed_frame_width_requested;
    g_customspeed.speed_sync_event.ticks += g_customspeed.speed_frame_width_requested;
    g_customspeed.speed_in_percentage = g_customspeed.speed_in_percentage_requested;
}
#endif /* CUSTOMSPEED_EVENT_C */
