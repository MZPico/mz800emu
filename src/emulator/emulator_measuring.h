#ifndef EMULATOR_MEASURING_H
#define EMULATOR_MEASURING_H

#include "main.h"
#include "hw-generic/gdg/video.h"
#include <glib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define MAX_MEASURING_FRAME_TIMING_IN_SECS 60 
#define MAX_MEASURING_FRAME_TIMING_SAMPLES (VIDEO_SCREENS_PER_SEC * MAX_MEASURING_FRAME_TIMING_IN_SECS)
#define PREDEF_MEASURING_FRAME_TIMING (VIDEO_SCREENS_PER_SEC * 1)

#define MAX_MEASURING_GDG_IN_SECS 10
#define EMULATOR_MEASURING_DEFAULT_GDG_UPDATE_SEC (1)

typedef struct st_EMULATOR_MEASURING_FRAME_TIMING_STATS
{
    double mean;
    double min;
    double max;
    double median;
    double stddev;
    uint32_t samples_count;
    double total_width;
    double real_total_width;
} st_EMULATOR_MEASURING_FRAME_TIMING_STATS;

typedef struct st_EMULATOR_MEASURING_FRAME_TIMING
{
    bool enabled;
    bool console_output_enabled;
    struct timespec last_time;
    double samples[MAX_MEASURING_FRAME_TIMING_SAMPLES];
    uint32_t head;
    uint32_t tail;
    uint32_t measuring_width;
    uint32_t stats_predef; // Počet měření, po kterém se provede statistika
    uint32_t stats_cntr;   // Počítadlo měření - az se vynuluje, provede se statistika
    st_EMULATOR_MEASURING_FRAME_TIMING_STATS stats;
} st_EMULATOR_MEASURING_FRAME_TIMING;

typedef struct st_EMULATOR_MEASURING_GDG
{
    bool enabled;
    bool console_output_enabled;
    GRWLock rwlock;
    GMutex mutex;
    GCond cond;
    GTimer *timer;
    uint64_t last_gdg_ticks;
    float elapsed_time;
    float current_gdg_frequency;
    float current_fps;
    float current_gdg_frequency_percent;
    // pokud bezí vlastní vlákno
    GThread *thread;
    gulong thread_usleep_time;
} st_EMULATOR_MEASURING_GDG;

typedef struct st_EMULATOR_MEASURING
{
    st_EMULATOR_MEASURING_FRAME_TIMING frame_timing;
    st_EMULATOR_MEASURING_GDG gdg;
} st_EMULATOR_MEASURING;

extern st_EMULATOR_MEASURING g_emulator_measuring;

#define EMULATOR_MEASURING_TEST_FRAME_TIMING_ENABLED (g_emulator_measuring.frame_timing.enabled)
#define EMULATOR_MEASURING_SET_FRAME_TIMING_ENABLED(x) (g_emulator_measuring.frame_timing.enabled = (x))
#define EMULATOR_MEASURING_FRAME_TIMING_ENABLED_PTR (&g_emulator_measuring.frame_timing.enabled)

#define EMULATOR_MEASURING_FRAME_TIMING_GET_PREDEF_TIME_IN_SEC() (g_emulator_measuring.frame_timing.stats_predef / VIDEO_SCREENS_PER_SEC)
#define EMULATOR_MEASURING_FRAME_TIMING_SET_PREDEF_TIME_IN_SEC(x) (g_emulator_measuring.frame_timing.stats_predef = x * VIDEO_SCREENS_PER_SEC)

#define EMULATOR_MEASURING_FRAME_TIMING_GET_MEASURING_WIDTH_IN_SEC() (g_emulator_measuring.frame_timing.measuring_width / VIDEO_SCREENS_PER_SEC)
#define EMULATOR_MEASURING_FRAME_TIMING_SET_MEASURING_WIDTH_IN_SEC(x) (g_emulator_measuring.frame_timing.measuring_width = x * VIDEO_SCREENS_PER_SEC)

#define EMULATOR_MEASURING_TEST_GDG_ENABLED (g_emulator_measuring.gdg.enabled)
#define EMULATOR_MEASURING_GDG_ENABLED_PTR (&g_emulator_measuring.gdg.enabled)

#define EMULATOR_MEASURING_GET_GDG_UPDATE_TIME_SEC() (g_emulator_measuring.gdg.thread_usleep_time / 1000000)
#define EMULATOR_MEASURING_SET_GDG_ENABLED_AND_UPDATE_TIME_SEC(enabled, sec) (emulator_measuring_gdg_set_enabled(&g_emulator_measuring.gdg, enabled, sec))

#ifdef __cplusplus
extern "C"
{
#endif
    void emulator_measuring_init(void);
    void emulator_measuring_exit(void);
    void emulator_measuring_frame_timing_reset(void);
    void emulator_measuring_frame_timing_event(void);

    void emulator_measuring_gdg_init(st_EMULATOR_MEASURING_GDG *msgdg);
    void emulator_measuring_gdg_exit(st_EMULATOR_MEASURING_GDG *msgdg);
    void emulator_measuring_gdg_event(st_EMULATOR_MEASURING_GDG *msgdg);
    bool emulator_measuring_gdg_set_enabled(st_EMULATOR_MEASURING_GDG *msgdg, bool enabled, int update_time_sec);
#ifdef __cplusplus
}
#endif

#endif // EMULATOR_MEASURING_H