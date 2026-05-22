#ifndef EMULATOR_H
#define EMULATOR_H

#include "app/app.h"
#include <glib.h>

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

#include "customspeed.h"

typedef struct st_EMULATOR
{
    bool max_speed;
    bool paused;

    bool development_mode;

    bool show_demo_window;
} st_EMULATOR;

extern st_EMULATOR g_emulator;

#define EMULATOR_TEST_PAUSED (g_emulator.paused)
#define EMULATOR_TEST_MAX_SPEED (g_emulator.max_speed)

#define EMULATOR_TEST_NORMAL_SPEED ((!EMULATOR_TEST_MAX_SPEED) && (CUSTOMSPEED_TEST_SPEED_100))
#define EMULATOR_TEST_CUSTOM_SPEED ((!EMULATOR_TEST_MAX_SPEED) && (!CUSTOMSPEED_TEST_SPEED_100))

#define EMULATOR_TEST_DEVELOPMENT_MODE (g_emulator.development_mode)
#define emulator_set_development_mode(value) (g_emulator.development_mode = value)

#ifdef __cplusplus
extern "C"
{
#endif
    void emulator_quit(int exit_value);
    gpointer emulator_thread(gpointer ptr);

    void emulator_max_speed(bool value);
    void emulator_pause(bool value);
    void emulator_switch_to_normal_speed(void);
    void emulator_switch_to_custom_speed(void);
    const char *emulator_get_speed_status_as_text(void);
#ifdef __cplusplus
}
#endif

#endif // EMULATOR_H